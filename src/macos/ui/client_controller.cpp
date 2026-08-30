#include "macos/ui/client_controller.hpp"

#include "core/config/stream_profile.hpp"
#include "core/protocol/wire.hpp"
#include "core/session/handshake.hpp"
#include "core/video/codec_config_wire.hpp"
#include "macos/audio/coreaudio_output.hpp"
#include "macos/video/videotoolbox_decoder.hpp"
#include "macos/video/video_surface_bridge.hpp"

#include <chrono>
#include <random>
#include <utility>

namespace ministream {
namespace {

std::uint64_t random_nonce() {
  std::random_device random;
  return (static_cast<std::uint64_t>(random()) << 32U) | random();
}

QString identity_label(const DiscoveredHost& host) {
  const auto card = format_discovered_host(host);
  const auto line_break = card.find('\n');
  return QString::fromStdString(card.substr(0, line_break));
}

}  // namespace

ClientController::ClientController(QObject* parent) : QObject(parent) {
  poll_timer_.setInterval(20);
  connect(&poll_timer_, &QTimer::timeout, this, &ClientController::pollConfirmation);
  input_router_ = std::make_unique<RemoteInputRouter>(
      input_capture_, [this](const DesktopInput& input) { sendDesktopInput(input); });
}

ClientController::~ClientController() { disconnectSession(); }

void ClientController::createMediaReceiver() {
  if (!session_keys_ || media_receiver_) {
    return;
  }
  crypto_ = std::make_unique<SessionCrypto>(
      session_id_, session_keys_->tx, session_keys_->rx, 0x4D535443U, 0x4D535448U);
  media_receiver_ = std::make_unique<MediaReceiver>(session_id_, *crypto_);
  video_decoder_ = std::make_unique<VideoToolboxDecoder>();
  video_surface_ = std::make_unique<VideoSurfaceBridge>();
  audio_decoder_ = std::make_unique<OpusDecoder48kStereo>();
  audio_output_ = std::make_unique<CoreAudioOutput>();
  if (audio_output_) {
    audio_output_->start();
  }
}

QObject* ClientController::videoSurface() const noexcept { return video_surface_.get(); }

void ClientController::sendDesktopInput(const DesktopInput& input) {
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

void ClientController::pollMedia(const ReceivedDatagram& incoming) {
  if (!media_receiver_) {
    return;
  }
  const auto common = incoming.datagram.bytes.size() >= kCommonHeaderBytes
                          ? decode_common_header(std::span<const std::byte>{incoming.datagram.bytes}
                                                     .first<kCommonHeaderBytes>())
                          : std::nullopt;
  if (!common) {
    return;
  }
  if (common->type == PacketType::Control) {
    if (const auto payload = crypto_->open(incoming.datagram)) {
      if (const auto config = decode_codec_config(*payload); config && video_decoder_) {
        video_decoder_->initialize(*config);
      }
    }
  } else if (common->type == PacketType::Video) {
    if (const auto frame = media_receiver_->receive_video(incoming.datagram, SteadyClock::now());
        frame && video_decoder_) {
      video_decoder_->decode(frame->bytes, frame->capture_timestamp_us);
      if (const auto latest = video_decoder_->take_latest(); latest && video_surface_) {
        video_surface_->publish(latest->pixel_buffer, latest->timestamp_us);
        CVPixelBufferRelease(latest->pixel_buffer);
      }
    }
  } else if (common->type == PacketType::Audio) {
    if (const auto packet = media_receiver_->receive_audio(incoming.datagram); packet) {
      audio_jitter_.push(*packet);
      for (;;) {
        const auto playout = audio_jitter_.pop(expected_audio_sequence_);
        if (playout.kind == AudioPlayoutKind::Plc) {
          if (audio_decoder_ && audio_output_) {
            if (const auto samples = audio_decoder_->decode_loss(); samples) {
              audio_output_->push(*samples);
            }
          }
          break;
        }
        ++expected_audio_sequence_;
        if (audio_decoder_ && audio_output_ && playout.packet) {
          if (const auto samples = audio_decoder_->decode(playout.packet->opus); samples) {
            audio_output_->push(*samples);
          }
        }
      }
    }
  }
}

QStringList ClientController::hosts() const { return host_labels_; }
bool ClientController::searching() const noexcept { return searching_; }
bool ClientController::connected() const noexcept { return connected_; }
bool ClientController::pairing() const noexcept { return pairing_; }
bool ClientController::remoteInputActive() const noexcept {
  return input_capture_.remote();
}
QString ClientController::pairingCode() const { return pairing_code_; }
QString ClientController::selectedDeviceLabel() const { return selected_device_label_; }

void ClientController::refreshHosts() {
  if (searching_) {
    return;
  }
  searching_ = true;
  emit searchingChanged();
  host_labels_.clear();
  discovered_.clear();
  if (const auto result = discover_hosts(std::chrono::milliseconds{250})) {
    discovered_ = *result;
    for (const auto& host : discovered_) {
      if (host.controllable) {
        host_labels_.push_back(QString::fromStdString(format_discovered_host(host)));
      }
    }
  }
  searching_ = false;
  emit hostsChanged();
  emit searchingChanged();
}

void ClientController::connectToHost(int index) {
  if (index < 0 || static_cast<std::size_t>(index) >= discovered_.size()) {
    return;
  }
  disconnectSession();
  session_ = std::make_unique<UdpEndpoint>();
  const auto& host = discovered_[static_cast<std::size_t>(index)];
  selected_device_label_ = identity_label(host);
  emit selectedDeviceChanged();
  if (!session_->bind(0) || !session_->set_remote(host.address, host.session_port)) {
    disconnectSession();
    return;
  }
  const auto profile = stream_profile(StreamProfileId::Quality4K);
  const Hello hello{profile.codec, static_cast<std::uint16_t>(profile.width),
                    static_cast<std::uint16_t>(profile.height),
                    static_cast<std::uint16_t>(profile.fps),
                    static_cast<std::uint32_t>(profile.initial_bitrate_bps), random_nonce()};
  const auto hello_bytes = encode_hello(hello);
  if (!session_->send(hello_bytes)) {
    disconnectSession();
    return;
  }
  const auto accepted = session_->receive(std::chrono::seconds{1});
  const auto decoded_accept = accepted ? decode_accept(accepted->datagram.bytes)
                                       : std::nullopt;
  if (!decoded_accept || decoded_accept->hello_nonce != hello.nonce ||
      decoded_accept->codec != hello.codec || decoded_accept->width != hello.width ||
      decoded_accept->height != hello.height || decoded_accept->fps != hello.fps) {
    disconnectSession();
    return;
  }
  connected_ = true;
  emit connectedChanged();

  const auto identity = generate_identity();
  const auto ephemeral = generate_ephemeral_keypair();
  if (!identity || !ephemeral) {
    disconnectSession();
    return;
  }
  identity_ = *identity;
  ephemeral_ = *ephemeral;
  local_offer_ = PairingOffer{PairingRole::Initiator, hello.nonce,
                              identity_->public_key, ephemeral_->public_key};
  const auto offer_bytes = encode_pairing_offer(*local_offer_);
  if (!session_->send(offer_bytes)) {
    disconnectSession();
    return;
  }
  const auto response = session_->receive(std::chrono::seconds{1});
  if (!response || !(peer_offer_ = decode_pairing_offer(response->datagram.bytes))) {
    disconnectSession();
    return;
  }
  const auto transcript = pairing_transcript(*local_offer_, *peer_offer_);
  if (!transcript) {
    disconnectSession();
    return;
  }
  pairing_code_ = QStringLiteral("%1").arg(
      compute_pairing_sas(*transcript), 6, 10, QLatin1Char('0'));
  pairing_ = true;
  emit pairingChanged();
}

void ClientController::toggleRemoteInput() {
  if (input_capture_.remote()) {
    releaseRemoteInput();
    return;
  }
  if (!connected_ || !input_router_) {
    return;
  }
  if (!input_router_->begin()) {
    input_capture_.leave_remote();
    return;
  }
  emit remoteInputChanged();
}

void ClientController::releaseRemoteInput() {
  const bool was_active = input_capture_.remote();
  if (input_router_) {
    input_router_->end();
  } else {
    input_capture_.leave_remote();
  }
  if (was_active) {
    emit remoteInputChanged();
  }
}

void ClientController::confirmPairing() {
  if (!pairing_ || !session_) {
    return;
  }
  confirmation_.confirm_local();
  const auto message = encode_pairing_confirmation(true);
  session_->send(message);
  poll_timer_.start();
}

void ClientController::cancelPairing() {
  if (session_) {
    const auto message = encode_pairing_confirmation(false);
    session_->send(message);
  }
  disconnectSession();
}

void ClientController::pollConfirmation() {
  const auto incoming = session_ ? session_->try_receive() : std::nullopt;
  if (!incoming) {
    return;
  }
  const auto common = incoming->datagram.bytes.size() >= kCommonHeaderBytes
                          ? decode_common_header(std::span<const std::byte>{incoming->datagram.bytes}
                                                     .first<kCommonHeaderBytes>())
                          : std::nullopt;
  if (common && (common->type == PacketType::Control || common->type == PacketType::Video ||
                 common->type == PacketType::Audio) &&
      media_receiver_) {
    pollMedia(*incoming);
    return;
  }
  const auto accepted = decode_pairing_confirmation(incoming->datagram.bytes);
  if (!accepted || !*accepted) {
    disconnectSession();
    return;
  }
  confirmation_.confirm_peer();
  if (confirmation_.ready() && ephemeral_ && peer_offer_) {
    const auto keys = derive_session_keys(*ephemeral_, peer_offer_->ephemeral, true);
    if (keys) {
      session_keys_ = *keys;
      createMediaReceiver();
    }
    resetPairing();
  }
}

void ClientController::disconnectSession() {
  releaseRemoteInput();
  poll_timer_.stop();
  media_receiver_.reset();
  crypto_.reset();
  audio_output_.reset();
  audio_decoder_.reset();
  video_decoder_.reset();
  video_surface_.reset();
  expected_audio_sequence_ = 0;
  session_keys_.reset();
  session_.reset();
  const bool was_connected = connected_;
  connected_ = false;
  if (was_connected) {
    emit connectedChanged();
  }
  if (!selected_device_label_.isEmpty()) {
    selected_device_label_.clear();
    emit selectedDeviceChanged();
  }
  resetPairing();
}

void ClientController::resetPairing() {
  poll_timer_.stop();
  const bool was_pairing = pairing_;
  pairing_ = false;
  pairing_code_.clear();
  local_offer_.reset();
  peer_offer_.reset();
  confirmation_ = PairingConfirmation{};
  if (was_pairing) {
    emit pairingChanged();
  }
}

}  // namespace ministream
