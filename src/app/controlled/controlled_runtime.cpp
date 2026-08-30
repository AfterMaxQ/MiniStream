#include "app/controlled/controlled_runtime.hpp"

#include "core/protocol/wire.hpp"
#include "core/input/gamepad_packet.hpp"
#include "core/input/rumble_packet.hpp"
#include "core/video/codec_config_wire.hpp"

#include <chrono>
#include <random>

namespace ministream {
namespace {

std::uint64_t random_nonce() {
  std::random_device random;
  return (static_cast<std::uint64_t>(random()) << 32U) | random();
}

}  // namespace

ControlledRuntime::ControlledRuntime(std::unique_ptr<ControlledBackend> backend,
                                     DiscoveryAdvertisement advertisement)
    : backend_(std::move(backend)), advertisement_(std::move(advertisement)) {
  advertisement_.controllable = false;
}

ControlledRuntime::~ControlledRuntime() { stop(); }

ControlledCapabilities ControlledRuntime::inspect() const {
  return backend_ ? backend_->inspect() : ControlledCapabilities{};
}

bool ControlledRuntime::start() {
  if (!backend_ || state_ != RoleState::Idle || !inspect().ready()) {
    return false;
  }
  if (!backend_->start()) {
    backend_->stop();
    return false;
  }
  backend_->set_rumble_sender(
      [this](const RumblePacket& packet) { send_rumble(packet); });

  discovery_ = std::make_unique<DiscoveryHost>();
  session_ = std::make_unique<UdpEndpoint>();
  const auto identity = generate_identity();
  const auto ephemeral = generate_ephemeral_keypair();
  if (!identity || !ephemeral || !discovery_->start() || !session_->bind(0)) {
    stop();
    return false;
  }
  identity_ = *identity;
  ephemeral_ = *ephemeral;

  advertisement_.session_port = session_->local_port();
  if (advertisement_.session_port == 0 || advertisement_.device_name.empty()) {
    stop();
    return false;
  }
  advertisement_.controllable = true;
  state_ = RoleState::Broadcasting;
  return true;
}

void ControlledRuntime::stop() noexcept {
  if (backend_) {
    backend_->stop();
  }
  scheduler_.reset();
  media_sender_.reset();
  crypto_.reset();
  audio_encoder_.reset();
  audio_pending_.clear();
  audio_sequence_ = 0;
  gamepad_sequence_filter_ = GamepadSequenceFilter{};
  discovery_.reset();
  session_.reset();
  identity_.reset();
  ephemeral_.reset();
  peer_offer_.reset();
  local_offer_.reset();
  session_keys_.reset();
  pairing_code_.clear();
  confirmation_ = PairingConfirmation{};
  advertisement_.controllable = false;
  advertisement_.session_port = 0;
  state_ = RoleState::Idle;
}

bool ControlledRuntime::hosting() const noexcept {
  return state_ == RoleState::Broadcasting || state_ == RoleState::Pairing ||
         state_ == RoleState::Streaming;
}

bool ControlledRuntime::pairing() const noexcept { return state_ == RoleState::Pairing; }

bool ControlledRuntime::streaming() const noexcept { return state_ == RoleState::Streaming; }

RoleState ControlledRuntime::state() const noexcept { return state_; }

const std::string& ControlledRuntime::pairing_code() const noexcept { return pairing_code_; }

const DiscoveryAdvertisement& ControlledRuntime::advertisement() const noexcept {
  return advertisement_;
}

void ControlledRuntime::create_media_sender() {
  if (!session_keys_ || media_sender_ || !session_) {
    return;
  }
  crypto_ = std::make_unique<SessionCrypto>(
      session_id_, session_keys_->tx, session_keys_->rx, 0x4D535448U, 0x4D535443U);
  scheduler_ = std::make_unique<PacketScheduler>();
  media_sender_ = std::make_unique<MediaSender>(session_id_, *crypto_, *scheduler_);
  audio_encoder_ = std::make_unique<OpusEncoder48kStereo>();
  if (!audio_encoder_->ready()) {
    media_sender_.reset();
    scheduler_.reset();
    crypto_.reset();
  }
}

void ControlledRuntime::confirm_pairing() {
  if (!pairing() || !session_) {
    return;
  }
  confirmation_.confirm_local();
  const auto message = encode_pairing_confirmation(true);
  session_->reply(message);
  if (!confirmation_.ready() || !ephemeral_ || !peer_offer_) {
    return;
  }
  const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, false);
  if (!keys) {
    stop();
    return;
  }
  session_keys_ = *keys;
  create_media_sender();
  if (!media_sender_) {
    stop();
    return;
  }
  state_ = RoleState::Streaming;
  reset_pairing();
}

void ControlledRuntime::cancel_pairing() {
  if (session_) {
    const auto message = encode_pairing_confirmation(false);
    session_->reply(message);
  }
  reset_pairing();
}

void ControlledRuntime::process_datagram(const ReceivedDatagram& incoming) {
  const auto bytes = std::span<const std::byte>{incoming.datagram.bytes};
  if (bytes.size() >= kCommonHeaderBytes) {
    if (const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
        common && common->type == PacketType::Input && crypto_) {
      if (const auto payload = crypto_->open(incoming.datagram); payload) {
        if (const auto input = decode_desktop_input(*payload); input && backend_) {
          backend_->inject_input(*input);
        } else if (const auto gamepad = decode_gamepad_packet(*payload); gamepad && backend_ &&
                   gamepad_sequence_filter_.accept(gamepad->sequence)) {
          backend_->submit_gamepad(gamepad->state);
        }
      }
      return;
    }
  }

  if (const auto hello = decode_hello(bytes); hello && session_) {
    if (!backend_ || !backend_->configure_video(
                         {hello->codec, hello->width, hello->height, hello->fps,
                          false, {}})) {
      return;
    }
    negotiated_codec_ = hello->codec;
    negotiated_width_ = hello->width;
    negotiated_height_ = hello->height;
    negotiated_fps_ = hello->fps;
    negotiated_bitrate_ = hello->target_bitrate_bps;
    session_->reply(encode_accept(
        {HandshakeRole::Controlled, session_id_, hello->codec, hello->width, hello->height,
         hello->fps, hello->target_bitrate_bps, hello->nonce}));
    return;
  }

  if (const auto offer = decode_pairing_offer(bytes);
      offer && offer->role == PairingRole::Initiator && identity_ && ephemeral_ && session_) {
    peer_offer_ = *offer;
    local_offer_ = PairingOffer{PairingRole::Responder, random_nonce(),
                                identity_->public_key, ephemeral_->public_key};
    const auto transcript = pairing_transcript(*peer_offer_, *local_offer_);
    if (!transcript) {
      return;
    }
    pairing_code_ = std::to_string(compute_pairing_sas(*transcript));
    if (pairing_code_.size() < 6) {
      pairing_code_.insert(pairing_code_.begin(), 6 - pairing_code_.size(), '0');
    }
    state_ = RoleState::Pairing;
    session_->reply(encode_pairing_offer(*local_offer_));
    return;
  }

  if (const auto accepted = decode_pairing_confirmation(bytes); accepted && session_) {
    if (!*accepted) {
      reset_pairing();
      return;
    }
    confirmation_.confirm_peer();
    if (confirmation_.ready() && ephemeral_ && peer_offer_) {
      const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, false);
      if (!keys) {
        stop();
        return;
      }
      session_keys_ = *keys;
      create_media_sender();
      if (!media_sender_) {
        stop();
        return;
      }
      state_ = RoleState::Streaming;
      reset_pairing();
    }
  }
}

void ControlledRuntime::send_pending_video(SteadyClock::time_point now) {
  if (!media_sender_ || !session_ || !crypto_ || !backend_) {
    return;
  }
  if (const auto frame = backend_->next_video(); frame) {
    media_sender_->enqueue_video(*frame, now);
    if (const auto config = backend_->codec_config(); !config.parameter_sets.empty()) {
      const auto payload = encode_codec_config(config);
      if (const auto packet = crypto_->seal(PacketType::Control, payload); packet) {
        session_->reply(packet->bytes);
      }
    }
  }
}

void ControlledRuntime::send_pending_audio(SteadyClock::time_point now) {
  if (!media_sender_ || !audio_encoder_ || !audio_encoder_->ready() || !backend_) {
    return;
  }
  const auto pcm = backend_->next_audio();
  if (!pcm) {
    return;
  }
  audio_pending_.insert(audio_pending_.end(), pcm->interleaved_stereo.begin(),
                        pcm->interleaved_stereo.end());
  while (audio_pending_.size() >= kOpusFrameSamplesPerChannel * 2U) {
    const auto encoded = audio_encoder_->encode(
        std::span<const float>{audio_pending_.data(), kOpusFrameSamplesPerChannel * 2U});
    if (encoded) {
      media_sender_->enqueue_audio(
          {audio_sequence_++, pcm->host_timestamp_us,
           static_cast<std::uint16_t>(kOpusFrameSamplesPerChannel), *encoded},
          now);
    }
    audio_pending_.erase(audio_pending_.begin(),
                         audio_pending_.begin() + kOpusFrameSamplesPerChannel * 2U);
  }
}

void ControlledRuntime::send_rumble(const RumblePacket& packet) {
  if (!session_ || !crypto_ || !streaming()) {
    return;
  }
  const auto payload = encode_rumble_packet(packet);
  if (const auto sealed = crypto_->seal(PacketType::Feedback, payload)) {
    session_->reply(sealed->bytes);
  }
}

void ControlledRuntime::tick() {
  if (!hosting() || !discovery_ || !session_) {
    return;
  }
  discovery_->poll(advertisement_);
  if (const auto incoming = session_->try_receive(); incoming) {
    process_datagram(*incoming);
  }
  if (!streaming()) {
    return;
  }
  const auto now = SteadyClock::now();
  send_pending_video(now);
  send_pending_audio(now);
  if (!scheduler_) {
    return;
  }
  for (unsigned count = 0; count < 32; ++count) {
    const auto datagram = scheduler_->next(now);
    if (!datagram) {
      break;
    }
    session_->reply(datagram->bytes);
  }
}

void ControlledRuntime::reset_pairing() noexcept {
  const bool was_pairing = pairing();
  pairing_code_.clear();
  peer_offer_.reset();
  local_offer_.reset();
  confirmation_ = PairingConfirmation{};
  if (was_pairing) {
    state_ = RoleState::Broadcasting;
  }
}

}  // namespace ministream
