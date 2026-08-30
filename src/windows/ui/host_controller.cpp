#include "windows/ui/host_controller.hpp"

#include "core/config/stream_profile.hpp"
#include "core/input/desktop_input.hpp"
#include "core/session/handshake.hpp"
#include "core/protocol/wire.hpp"
#include "core/video/codec_config_wire.hpp"
#include "windows/audio/wasapi_loopback.hpp"
#include "windows/input/remote_input_sink.hpp"
#include "windows/input/virtual_gamepad.hpp"
#include "windows/video/dxgi_capture.hpp"
#include "windows/video/nvenc_encoder.hpp"

#include <QSysInfo>

#include <algorithm>
#include <random>

namespace ministream {
namespace {

std::uint64_t random_nonce() {
  std::random_device random;
  return (static_cast<std::uint64_t>(random()) << 32U) | random();
}

QString text(const CapabilityStatus& capability) {
  return QString::fromStdString(capability.detail);
}

}  // namespace

HostController::HostController(QObject* parent) : QObject(parent) {
  connect(&poll_timer_, &QTimer::timeout, this, &HostController::pollNetwork);
  poll_timer_.setInterval(10);
  refresh();
}

void HostController::createMediaCrypto() {
  if (!session_keys_ || media_sender_) {
    return;
  }
  crypto_ = std::make_unique<SessionCrypto>(
      session_id_, session_keys_->tx, session_keys_->rx, 0x4D535448U, 0x4D535443U);
  scheduler_ = std::make_unique<PacketScheduler>();
  media_sender_ = std::make_unique<MediaSender>(session_id_, *crypto_, *scheduler_);
  encoder_ = std::make_unique<NvencEncoder>();
  opus_encoder_ = std::make_unique<OpusEncoder48kStereo>();
  input_sink_ = std::make_unique<RemoteInputSink>();
}

HostController::~HostController() { stopHost(); }

bool HostController::ready() const noexcept { return capabilities_.ready(); }
bool HostController::videoReady() const noexcept { return capabilities_.video.ready; }
bool HostController::audioReady() const noexcept { return capabilities_.audio.ready; }
bool HostController::controllerReady() const noexcept {
  return capabilities_.controller.ready;
}
bool HostController::networkReady() const noexcept { return capabilities_.network.ready; }
QString HostController::videoDetail() const { return text(capabilities_.video); }
QString HostController::audioDetail() const { return text(capabilities_.audio); }
QString HostController::controllerDetail() const { return text(capabilities_.controller); }
QString HostController::networkDetail() const { return text(capabilities_.network); }
bool HostController::hosting() const noexcept { return hosting_; }
bool HostController::pairing() const noexcept { return pairing_; }
QString HostController::pairingCode() const { return pairing_code_; }

void HostController::refresh() {
  capabilities_ = inspect_host_capabilities();
  emit capabilitiesChanged();
}

void HostController::startHost() {
  if (hosting_ || !ready()) {
    return;
  }
  capture_ = std::make_unique<DxgiCapture>();
  audio_ = std::make_unique<WasapiLoopback>();
  gamepad_ = std::make_unique<VirtualGamepad>();
  discovery_ = std::make_unique<DiscoveryHost>();
  session_ = std::make_unique<UdpEndpoint>();
  const auto identity = generate_identity();
  const auto ephemeral = generate_ephemeral_keypair();
  if (!capture_->initialize() || !audio_->start() || !gamepad_->start() ||
      !discovery_->start() || !session_->bind(48000) || !identity || !ephemeral) {
    stopHost();
    refresh();
    return;
  }
  identity_ = *identity;
  ephemeral_ = *ephemeral;
  hosting_ = true;
  poll_timer_.start();
  emit hostingChanged();
}

void HostController::stopHost() {
  poll_timer_.stop();
  if (gamepad_) {
    gamepad_->stop();
  }
  session_.reset();
  media_sender_.reset();
  scheduler_.reset();
  crypto_.reset();
  input_sink_.reset();
  opus_encoder_.reset();
  encoder_.reset();
  audio_pending_.clear();
  encoder_attempted_ = false;
  codec_config_sent_ = false;
  discovery_.reset();
  gamepad_.reset();
  audio_.reset();
  capture_.reset();
  identity_.reset();
  ephemeral_.reset();
  resetPairing();
  if (hosting_) {
    hosting_ = false;
    emit hostingChanged();
  }
}

void HostController::confirmPairing() {
  if (!pairing_ || !session_) {
    return;
  }
  confirmation_.confirm_local();
  const auto message = encode_pairing_confirmation(true);
  session_->reply(message);
  if (confirmation_.ready()) {
    if (ephemeral_ && peer_offer_) {
      const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, false);
      if (keys) {
        session_keys_ = *keys;
        createMediaCrypto();
      }
    }
    resetPairing();
  }
}

void HostController::cancelPairing() {
  if (session_) {
    const auto message = encode_pairing_confirmation(false);
    session_->reply(message);
  }
  resetPairing();
}

void HostController::pollNetwork() {
  auto name = QSysInfo::machineHostName().toStdString();
  name.resize(std::min<std::size_t>(name.size(), 48));
  discovery_->poll({name.empty() ? "Windows PC" : name, 48000});

  const auto incoming = session_->try_receive();
  if (!incoming) {
    // Media is produced only after the authenticated pairing is complete.
  } else {
    const auto common = incoming->datagram.bytes.size() >= kCommonHeaderBytes
                            ? decode_common_header(std::span<const std::byte>{incoming->datagram.bytes}
                                                       .first<kCommonHeaderBytes>())
                            : std::nullopt;
    if (common && common->type == PacketType::Input && crypto_ && input_sink_) {
      if (const auto payload = crypto_->open(incoming->datagram)) {
        if (const auto input = decode_desktop_input(*payload)) {
          input_sink_->inject(*input);
        }
      }
      return;
    }
    if (const auto hello = decode_hello(incoming->datagram.bytes)) {
      negotiated_codec_ = hello->codec;
      negotiated_width_ = hello->width;
      negotiated_height_ = hello->height;
      negotiated_fps_ = hello->fps;
      negotiated_bitrate_ = hello->target_bitrate_bps;
      session_->reply(encode_accept(
          {1, hello->codec, hello->width, hello->height, hello->fps,
           hello->target_bitrate_bps, hello->nonce}));
      return;
    }
    if (const auto offer = decode_pairing_offer(incoming->datagram.bytes);
        offer && offer->role == PairingRole::Initiator && identity_ && ephemeral_) {
      peer_offer_ = *offer;
      local_offer_ = PairingOffer{PairingRole::Responder, random_nonce(),
                                  identity_->public_key, ephemeral_->public_key};
      const auto transcript = pairing_transcript(*peer_offer_, *local_offer_);
      if (!transcript) {
        return;
      }
      pairing_code_ = QStringLiteral("%1").arg(
          compute_pairing_sas(*transcript), 6, 10, QLatin1Char('0'));
      pairing_ = true;
      session_->reply(encode_pairing_offer(*local_offer_));
      emit pairingChanged();
      return;
    }
    if (const auto accepted = decode_pairing_confirmation(incoming->datagram.bytes)) {
      if (*accepted) {
        confirmation_.confirm_peer();
        if (confirmation_.ready()) {
          if (ephemeral_ && peer_offer_) {
            const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, false);
            if (keys) {
              session_keys_ = *keys;
              createMediaCrypto();
            }
          }
          resetPairing();
        }
      } else {
        resetPairing();
      }
    }
  }

  if (!media_sender_ || !session_keys_ || !capture_) {
    return;
  }
  const auto now = SteadyClock::now();
  if (const auto frame = capture_->acquire(Microseconds{0}); frame) {
    if (!encoder_attempted_) {
      encoder_attempted_ = true;
      encoder_->initialize(capture_->device(), capture_->context(),
                           {negotiated_codec_, frame->width, frame->height,
                            negotiated_fps_, negotiated_bitrate_, false});
    }
    if (encoder_->ready()) {
      if (const auto encoded = encoder_->encode(*frame, static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(
                  frame->captured_at.time_since_epoch())
                  .count()));
          encoded) {
        media_sender_->enqueue_video(*encoded, now);
        if (!codec_config_sent_ && !encoder_->codec_config().parameter_sets.empty()) {
          if (const auto config = encode_codec_config(encoder_->codec_config());
              !config.empty()) {
            if (const auto control = crypto_->seal(PacketType::Control, config); control) {
              session_->reply(control->bytes);
              codec_config_sent_ = true;
            }
          }
        }
      }
    }
  }
  if (opus_encoder_ && opus_encoder_->ready()) {
    if (const auto pcm = audio_->read(); pcm) {
      audio_pending_.insert(audio_pending_.end(), pcm->interleaved_stereo.begin(),
                            pcm->interleaved_stereo.end());
      while (audio_pending_.size() >= kOpusFrameSamplesPerChannel * 2U) {
        const auto encoded = opus_encoder_->encode(std::span<const float>{
            audio_pending_.data(), kOpusFrameSamplesPerChannel * 2U});
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
  }
  while (auto datagram = scheduler_->next(now)) {
    session_->reply(datagram->bytes);
  }
}

void HostController::resetPairing() {
  const bool was_pairing = pairing_;
  pairing_ = false;
  pairing_code_.clear();
  peer_offer_.reset();
  local_offer_.reset();
  confirmation_ = PairingConfirmation{};
  if (was_pairing) {
    emit pairingChanged();
  }
}

}  // namespace ministream
