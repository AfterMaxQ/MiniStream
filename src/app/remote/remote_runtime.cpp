#include "app/remote/remote_runtime.hpp"

#include "core/config/stream_profile.hpp"
#include "core/input/rumble_packet.hpp"
#include "core/protocol/wire.hpp"
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

RemoteRuntime::RemoteRuntime(std::unique_ptr<RemoteBackend> backend)
    : backend_(std::move(backend)) {
  input_router_ = std::make_unique<RemoteInputRouter>(
      input_capture_, [this](const DesktopInput& input) { send_input(input); });
}

RemoteRuntime::~RemoteRuntime() { stop(); }

RemoteCapabilities RemoteRuntime::inspect() const {
  return backend_ ? backend_->inspect() : RemoteCapabilities{};
}

bool RemoteRuntime::start() {
  if (!backend_ || state_ != RoleState::Idle || !inspect().ready()) {
    return false;
  }
  if (!backend_->start()) {
    backend_->stop();
    return false;
  }
  state_ = RoleState::RemoteBrowsing;
  return true;
}

void RemoteRuntime::stop() noexcept {
  disconnect_session();
  if (backend_) {
    backend_->stop();
  }
  hosts_.clear();
  selected_host_.reset();
  state_ = RoleState::Idle;
}

RoleState RemoteRuntime::state() const noexcept { return state_; }

bool RemoteRuntime::connected() const noexcept {
  return state_ == RoleState::Pairing || state_ == RoleState::Streaming;
}

bool RemoteRuntime::pairing() const noexcept { return state_ == RoleState::Pairing; }

bool RemoteRuntime::streaming() const noexcept { return state_ == RoleState::Streaming; }

bool RemoteRuntime::remote_input_active() const noexcept { return input_capture_.remote(); }

const std::string& RemoteRuntime::pairing_code() const noexcept { return pairing_code_; }

const std::vector<DiscoveredHost>& RemoteRuntime::hosts() const noexcept { return hosts_; }

const std::optional<DiscoveredHost>& RemoteRuntime::selected_host() const noexcept {
  return selected_host_;
}

bool RemoteRuntime::refresh(Microseconds timeout) {
  if (state_ != RoleState::RemoteBrowsing || !backend_) {
    return false;
  }
  const auto result = discover_hosts(timeout);
  if (!result) {
    hosts_.clear();
    return false;
  }
  hosts_ = *result;
  return true;
}

bool RemoteRuntime::connect(std::size_t index) {
  if (state_ != RoleState::RemoteBrowsing || index >= hosts_.size() || !backend_) {
    return false;
  }
  disconnect_session();
  selected_host_ = hosts_[index];
  session_ = std::make_unique<UdpEndpoint>();
  if (!session_->bind(0) ||
      !session_->set_remote(selected_host_->address, selected_host_->session_port)) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }

  const auto profile = stream_profile(StreamProfileId::Quality4K);
  const Hello hello{HandshakeRole::Controller, profile.codec,
                    static_cast<std::uint16_t>(profile.width),
                    static_cast<std::uint16_t>(profile.height),
                    static_cast<std::uint16_t>(profile.fps),
                    static_cast<std::uint32_t>(profile.initial_bitrate_bps), random_nonce()};
  if (!session_->send(encode_hello(hello))) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }
  const auto accepted = session_->receive(std::chrono::seconds{1});
  const auto decoded_accept = accepted ? decode_accept(accepted->datagram.bytes)
                                       : std::nullopt;
  if (!decoded_accept || decoded_accept->hello_nonce != hello.nonce ||
      decoded_accept->codec != hello.codec || decoded_accept->width != hello.width ||
      decoded_accept->height != hello.height || decoded_accept->fps != hello.fps) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }

  const auto identity = generate_identity();
  const auto ephemeral = generate_ephemeral_keypair();
  if (!identity || !ephemeral) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }
  identity_ = *identity;
  ephemeral_ = *ephemeral;
  local_offer_ = PairingOffer{PairingRole::Initiator, hello.nonce,
                              identity_->public_key, ephemeral_->public_key};
  if (!session_->send(encode_pairing_offer(*local_offer_))) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }
  const auto response = session_->receive(std::chrono::seconds{1});
  if (!response || !(peer_offer_ = decode_pairing_offer(response->datagram.bytes))) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }
  const auto transcript = pairing_transcript(*local_offer_, *peer_offer_);
  if (!transcript) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }
  pairing_code_ = std::to_string(compute_pairing_sas(*transcript));
  if (pairing_code_.size() < 6) {
    pairing_code_.insert(pairing_code_.begin(), 6 - pairing_code_.size(), '0');
  }
  state_ = RoleState::Pairing;
  return true;
}

void RemoteRuntime::confirm_pairing() {
  if (!pairing() || !session_) {
    return;
  }
  confirmation_.confirm_local();
  session_->send(encode_pairing_confirmation(true));
  if (!confirmation_.ready() || !ephemeral_ || !peer_offer_) {
    return;
  }
  const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, true);
  if (!keys) {
    disconnect_session();
    return;
  }
  session_keys_ = *keys;
  crypto_ = std::make_unique<SessionCrypto>(
      session_id_, session_keys_->tx, session_keys_->rx, 0x4D535443U, 0x4D535448U);
  media_receiver_ = std::make_unique<MediaReceiver>(session_id_, *crypto_);
  audio_decoder_ = std::make_unique<OpusDecoder48kStereo>();
  if (!audio_decoder_->ready()) {
    disconnect_session();
    return;
  }
  state_ = RoleState::Streaming;
  reset_pairing();
}

void RemoteRuntime::cancel_pairing() {
  if (session_) {
    session_->send(encode_pairing_confirmation(false));
  }
  disconnect_session();
}

void RemoteRuntime::toggle_input() {
  if (remote_input_active()) {
    release_input();
    return;
  }
  if (!streaming() || !input_router_) {
    return;
  }
  if (!input_router_->begin()) {
    input_capture_.leave_remote();
    return;
  }
}

void RemoteRuntime::release_input() noexcept {
  if (input_router_) {
    input_router_->end();
  } else {
    input_capture_.leave_remote();
  }
}

bool RemoteRuntime::route_input(const DesktopInput& input) {
  return input_router_ && input_router_->route(input);
}

void RemoteRuntime::send_input(const DesktopInput& input) {
  if (!session_ || !crypto_) {
    return;
  }
  const auto payload = encode_desktop_input(input);
  if (payload.empty()) {
    return;
  }
  if (const auto packet = crypto_->seal(PacketType::Input, payload); packet) {
    session_->send(packet->bytes);
  }
}

void RemoteRuntime::send_gamepad(const GamepadPacket& packet) {
  if (!session_ || !crypto_) {
    return;
  }
  const auto payload = encode_gamepad_packet(packet);
  if (const auto sealed = crypto_->seal(PacketType::Input, payload)) {
    session_->send(sealed->bytes);
  }
}

void RemoteRuntime::poll_media(const ReceivedDatagram& incoming) {
  if (!media_receiver_ || !crypto_ || !backend_) {
    return;
  }
  const auto bytes = std::span<const std::byte>{incoming.datagram.bytes};
  const auto common = bytes.size() >= kCommonHeaderBytes
                          ? decode_common_header(bytes.first<kCommonHeaderBytes>())
                          : std::nullopt;
  if (!common) {
    return;
  }
  if (common->type == PacketType::Control) {
    if (const auto payload = crypto_->open(incoming.datagram); payload) {
      if (const auto config = decode_codec_config(*payload); config) {
        codec_configured_ = backend_->configure_video(*config);
      }
    }
    return;
  }
  if (common->type == PacketType::Video) {
    if (const auto frame = media_receiver_->receive_video(incoming.datagram, SteadyClock::now());
        frame) {
      backend_->decode_video(frame->bytes, frame->capture_timestamp_us);
    }
    return;
  }
  if (common->type == PacketType::Audio) {
    if (const auto packet = media_receiver_->receive_audio(incoming.datagram); packet) {
      audio_jitter_.push(*packet);
      const auto playout = audio_jitter_.pop(expected_audio_sequence_);
      if (playout.kind == AudioPlayoutKind::Plc) {
        if (const auto samples = audio_decoder_->decode_loss(); samples) {
          backend_->play_audio(*samples);
        }
      } else if (playout.packet) {
        ++expected_audio_sequence_;
        if (const auto samples = audio_decoder_->decode(playout.packet->opus); samples) {
          backend_->play_audio(*samples);
        }
      }
    }
    return;
  }
  if (common->type == PacketType::Feedback) {
    if (const auto payload = crypto_->open(incoming.datagram); payload) {
      if (const auto rumble = decode_rumble_packet(*payload); rumble) {
        backend_->play_rumble(rumble->low, rumble->high, rumble->duration_ms);
      }
    }
  }
}

void RemoteRuntime::process_datagram(const ReceivedDatagram& incoming) {
  const auto bytes = std::span<const std::byte>{incoming.datagram.bytes};
  if (bytes.size() >= kCommonHeaderBytes) {
    if (const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
        common && (common->type == PacketType::Control || common->type == PacketType::Video ||
                   common->type == PacketType::Audio || common->type == PacketType::Feedback) &&
            streaming()) {
      poll_media(incoming);
      return;
    }
  }

  if (const auto accepted = decode_pairing_confirmation(bytes); accepted) {
    if (!*accepted) {
      disconnect_session();
      return;
    }
    confirmation_.confirm_peer();
    if (confirmation_.ready() && ephemeral_ && peer_offer_) {
      const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, true);
      if (!keys) {
        disconnect_session();
        return;
      }
      session_keys_ = *keys;
      crypto_ = std::make_unique<SessionCrypto>(
          session_id_, session_keys_->tx, session_keys_->rx, 0x4D535443U, 0x4D535448U);
      media_receiver_ = std::make_unique<MediaReceiver>(session_id_, *crypto_);
      audio_decoder_ = std::make_unique<OpusDecoder48kStereo>();
      if (!audio_decoder_->ready()) {
        disconnect_session();
        return;
      }
      state_ = RoleState::Streaming;
      reset_pairing();
    }
  }
}

void RemoteRuntime::tick() {
  if (!session_ || (state_ != RoleState::Pairing && state_ != RoleState::Streaming)) {
    return;
  }
  if (const auto incoming = session_->try_receive(); incoming) {
    process_datagram(*incoming);
  }
  if (streaming() && input_capture_.routes_to_remote(InputDevice::Gamepad) && backend_) {
    const auto now = SteadyClock::now();
    if (const auto gamepad = backend_->poll_gamepad(); gamepad) {
      gamepad_coalescer_.update(*gamepad, now);
    }
    if (const auto packet = gamepad_coalescer_.flush_if_due(now); packet) {
      send_gamepad(*packet);
    }
  }
}

void RemoteRuntime::disconnect_session() noexcept {
  release_input();
  media_receiver_.reset();
  crypto_.reset();
  audio_decoder_.reset();
  codec_configured_ = false;
  expected_audio_sequence_ = 0;
  audio_jitter_ = AudioJitterBuffer{};
  gamepad_coalescer_ = InputCoalescer{};
  session_keys_.reset();
  identity_.reset();
  ephemeral_.reset();
  session_.reset();
  reset_pairing();
  if (state_ != RoleState::Idle) {
    state_ = RoleState::RemoteBrowsing;
  }
}

void RemoteRuntime::reset_pairing() noexcept {
  pairing_code_.clear();
  local_offer_.reset();
  peer_offer_.reset();
  confirmation_ = PairingConfirmation{};
  if (state_ == RoleState::Pairing) {
    state_ = RoleState::RemoteBrowsing;
  }
}

}  // namespace ministream
