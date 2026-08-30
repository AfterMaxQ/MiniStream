#include "app/remote/remote_runtime.hpp"

#include "core/config/stream_profile.hpp"
#include "core/input/rumble_packet.hpp"
#include "core/protocol/wire.hpp"
#include "core/session/session_control.hpp"
#include "core/video/codec_config_wire.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <utility>

namespace ministream {
namespace {

std::uint64_t random_nonce() {
  std::random_device random;
  return (static_cast<std::uint64_t>(random()) << 32U) | random();
}

}  // namespace

RemoteRuntime::RemoteRuntime(std::unique_ptr<RemoteBackend> backend,
                             DiscoveryConfig discovery_config,
                             DiscoveryInterfaceProvider interface_provider)
    : backend_(std::move(backend)),
      discovery_config_(std::move(discovery_config)),
      interface_provider_(std::move(interface_provider)) {
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
  if (discovery_) {
    discovery_->stop();
    discovery_.reset();
  }
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

DiscoveryState RemoteRuntime::discovery_state() const noexcept {
  return discovery_ ? discovery_->state() : DiscoveryState::Idle;
}

std::optional<DiscoveryError> RemoteRuntime::last_discovery_error() const noexcept {
  return discovery_ ? discovery_->last_error() : std::nullopt;
}

void RemoteRuntime::set_telemetry_callback(
    std::function<void(const StreamSnapshot&)> callback) {
  telemetry_callback_ = std::move(callback);
}

bool RemoteRuntime::refresh(Microseconds timeout) {
  if (state_ != RoleState::RemoteBrowsing || !backend_) {
    return false;
  }
  return begin_discovery(timeout);
}

bool RemoteRuntime::begin_discovery(Microseconds timeout) {
  if (state_ != RoleState::RemoteBrowsing || !backend_) {
    return false;
  }
  if (!discovery_) {
    discovery_ = std::make_unique<DiscoveryClient>(discovery_config_, interface_provider_);
  }
  if (discovery_->active()) {
    return false;
  }
  if (const auto started = discovery_->start(timeout); !started) {
    hosts_.clear();
    return false;
  }
  hosts_.clear();
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

  const auto profile = select_common_stream_profile(*selected_host_, inspect());
  if (!profile) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }
  selected_min_bitrate_bps_ = profile->minimum_bitrate_bps;
  selected_max_bitrate_bps_ = profile->maximum_bitrate_bps;
  const Hello hello{HandshakeRole::Controller, profile->codec, profile->hdr10,
                    static_cast<std::uint16_t>(profile->width),
                    static_cast<std::uint16_t>(profile->height),
                    static_cast<std::uint16_t>(profile->fps),
                    static_cast<std::uint32_t>(profile->initial_bitrate_bps), random_nonce()};
  hello_ = hello;
  handshake_retrier_.emplace(hello);
  const auto initial_hello = handshake_retrier_->next_hello(SteadyClock::now());
  if (!initial_hello || !session_->send(encode_hello(*initial_hello))) {
    disconnect_session();
    selected_host_.reset();
    return false;
  }
  state_ = RoleState::RemoteConnecting;
  return true;
}

void RemoteRuntime::confirm_pairing() {
  if (!pairing() || !session_) {
    return;
  }
  confirmation_.confirm_local();
  const auto now = SteadyClock::now();
  send_pairing_confirmation(true);
  confirmation_retrier_.sent(now);
  if (!confirmation_.ready() || !ephemeral_ || !peer_offer_) {
    return;
  }
  finish_streaming();
}

void RemoteRuntime::send_pairing_offer(SteadyClock::time_point now) {
  if (!session_ || !local_offer_) {
    return;
  }
  if (pairing_offer_retrier_.due(now)) {
    session_->send(encode_pairing_offer(*local_offer_));
    pairing_offer_retrier_.sent(now);
  }
}

void RemoteRuntime::send_pairing_confirmation(bool accepted) {
  if (session_) {
    session_->send(encode_pairing_confirmation(accepted));
  }
}

void RemoteRuntime::finish_streaming() {
  if (!ephemeral_ || !peer_offer_) {
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
      if (is_disconnect_control(*payload)) {
        disconnect_session();
      } else if (const auto config = decode_codec_config(*payload); config) {
        codec_configured_ = false;
        codec_configured_ = backend_->configure_video(*config);
      }
    }
    return;
  }
  if (common->type == PacketType::Video) {
    if (!codec_configured_) {
      return;
    }
    if (const auto frame = media_receiver_->receive_video(incoming.datagram, SteadyClock::now());
        frame) {
      backend_->decode_video(frame->bytes, frame->capture_timestamp_us);
    }
    return;
  }
  if (common->type == PacketType::VideoFec) {
    if (!codec_configured_) {
      return;
    }
    if (const auto frame = media_receiver_->receive_video_fec(
            incoming.datagram, SteadyClock::now());
        frame) {
      backend_->decode_video(frame->bytes, frame->capture_timestamp_us);
    }
    return;
  }
  if (common->type == PacketType::Audio) {
    if (const auto packet = media_receiver_->receive_audio(incoming.datagram); packet) {
      audio_jitter_.push(*packet);
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
                   common->type == PacketType::VideoFec || common->type == PacketType::Audio ||
                   common->type == PacketType::Feedback) &&
            streaming()) {
      poll_media(incoming);
      return;
    }
  }

  if (state_ == RoleState::RemoteConnecting) {
    if (const auto accepted = decode_accept(bytes);
        accepted && handshake_retrier_ &&
            accepted->bitrate_bps >= selected_min_bitrate_bps_ &&
            accepted->bitrate_bps <= selected_max_bitrate_bps_ &&
            handshake_retrier_->accept(*accepted)) {
      session_id_ = accepted->session_id;
      const auto identity = generate_identity();
      const auto ephemeral = generate_ephemeral_keypair();
      if (!identity || !ephemeral || !hello_) {
        disconnect_session();
        return;
      }
      identity_ = *identity;
      ephemeral_ = *ephemeral;
      local_offer_ = PairingOffer{PairingRole::Initiator, hello_->nonce,
                                  identity_->public_key, ephemeral_->public_key};
      peer_offer_.reset();
      pairing_offer_retrier_.reset();
      send_pairing_offer(SteadyClock::now());
      state_ = RoleState::Pairing;
      return;
    }
  }

  if (const auto offer = decode_pairing_offer(bytes); offer) {
    if (state_ != RoleState::Pairing || offer->role != PairingRole::Responder ||
        !local_offer_) {
      return;
    }
    if (peer_offer_ && *peer_offer_ != *offer) {
      return;
    }
    peer_offer_ = *offer;
    const auto transcript = pairing_transcript(*local_offer_, *peer_offer_);
    if (!transcript) {
      return;
    }
    pairing_code_ = std::to_string(compute_pairing_sas(*transcript));
    if (pairing_code_.size() < 6) {
      pairing_code_.insert(pairing_code_.begin(), 6 - pairing_code_.size(), '0');
    }
    pairing_offer_retrier_.reset();
    return;
  }

  if (const auto accepted = decode_pairing_confirmation(bytes);
      accepted && state_ == RoleState::Pairing) {
    if (!*accepted) {
      disconnect_session();
      return;
    }
    confirmation_.confirm_peer();
    if (confirmation_.ready()) {
      finish_streaming();
    }
  }
}

void RemoteRuntime::tick() {
  const auto now = SteadyClock::now();
  if (backend_) {
    backend_->tick(now);
  }
  if (discovery_ && discovery_->active()) {
    const auto result = discovery_->poll(now);
    if (result.state == DiscoveryState::Complete) {
      hosts_ = result.hosts;
    } else if (result.state == DiscoveryState::Failed) {
      hosts_.clear();
    }
  }

  if (!session_) {
    return;
  }
  if (media_receiver_) {
    const auto unrecoverable_before = media_receiver_->fec_unrecoverable_frames();
    media_receiver_->expire_video(now);
    if (media_receiver_->fec_unrecoverable_frames() > unrecoverable_before) {
      request_keyframe(now);
    }
  }
  if (state_ == RoleState::RemoteConnecting || pairing() || streaming()) {
    for (const auto& incoming : session_->try_receive_batch(512)) {
      process_datagram(incoming);
      if (!session_) {
        break;
      }
    }
  }
  if (!session_) {
    return;
  }
  if (state_ == RoleState::RemoteConnecting && handshake_retrier_) {
    if (const auto hello = handshake_retrier_->next_hello(now); hello) {
      session_->send(encode_hello(*hello));
    } else if (handshake_retrier_->expired(now)) {
      disconnect_session();
      return;
    }
  }
  if (state_ == RoleState::Pairing && !peer_offer_) {
    if (pairing_offer_retrier_.due(now)) {
      send_pairing_offer(now);
    } else if (pairing_offer_retrier_.expired(now)) {
      disconnect_session();
      return;
    }
  }
  if (state_ == RoleState::Pairing && !confirmation_.ready() &&
      confirmation_.local_confirmed() && peer_offer_) {
    if (confirmation_retrier_.due(now)) {
      send_pairing_confirmation(true);
      confirmation_retrier_.sent(now);
    } else if (confirmation_retrier_.expired(now)) {
      disconnect_session();
      return;
    }
  }
  if (streaming()) {
    play_audio(now);
  }
  if (streaming() && input_capture_.routes_to_remote(InputDevice::Gamepad) && backend_) {
    if (const auto gamepad = backend_->poll_gamepad(); gamepad) {
      gamepad_coalescer_.update(*gamepad, now);
    }
    if (const auto packet = gamepad_coalescer_.flush_if_due(now); packet) {
      send_gamepad(*packet);
    }
  }
  if (streaming()) {
    send_feedback(now);
    if (media_receiver_) {
      StreamSample sample;
      const auto received = media_receiver_->received_video_packets();
      const auto lost = media_receiver_->lost_video_packets();
      const auto total = received + lost;
      sample.loss_fraction = total == 0
                                 ? 0.0
                                 : static_cast<double>(lost) / static_cast<double>(total);
      sample.fec_recovered = media_receiver_->fec_recovered_frames();
      sample.fec_unrecoverable = media_receiver_->fec_unrecoverable_frames();
      sample.audio_buffer_ms = static_cast<double>(
          std::chrono::duration_cast<Microseconds>(audio_jitter_.buffered_duration()).count()) /
                               1000.0;
      sample.controller_connected = true;
      telemetry_.push(sample);
      if (const auto snapshot = telemetry_.publish_if_due(now); snapshot &&
          telemetry_callback_) {
        telemetry_callback_(*snapshot);
      }
    }
  }
}

void RemoteRuntime::play_audio(SteadyClock::time_point now) {
  if (!streaming() || !backend_ || !audio_decoder_) {
    return;
  }
  if (!audio_primed_) {
    if (!audio_jitter_.ready_for_playout()) {
      return;
    }
    audio_primed_ = true;
    next_audio_playout_ = now;
  }
  if (!next_audio_playout_ || now < *next_audio_playout_) {
    return;
  }

  const auto playout = audio_jitter_.pop(expected_audio_sequence_);
  if (playout.kind == AudioPlayoutKind::Packet && playout.packet) {
    if (const auto samples = audio_decoder_->decode(playout.packet->opus); samples) {
      backend_->play_audio(*samples);
    }
  } else if (const auto samples = audio_decoder_->decode_loss(); samples) {
    backend_->play_audio(*samples);
  }
  ++expected_audio_sequence_;
  constexpr auto kAudioPlayoutInterval = std::chrono::milliseconds{10};
  *next_audio_playout_ += kAudioPlayoutInterval;
}

void RemoteRuntime::send_feedback(SteadyClock::time_point now) {
  if (!streaming() || !session_ || !crypto_ || !media_receiver_) {
    return;
  }
  if (last_feedback_send_ &&
      now - *last_feedback_send_ < std::chrono::milliseconds{100}) {
    return;
  }

  const auto receiver_timestamp = std::chrono::duration_cast<Microseconds>(
      now.time_since_epoch()).count();
  const auto wire_counter = [](std::uint64_t value) {
    return static_cast<std::uint32_t>(value % kFeedbackCounterModulus);
  };
  const FeedbackReport report{
      feedback_sequence_++,
      receiver_timestamp < 0 ? 0U : static_cast<std::uint64_t>(receiver_timestamp),
      0,
       wire_counter(media_receiver_->received_video_packets()),
       wire_counter(media_receiver_->lost_video_packets()),
       0,
       wire_counter(media_receiver_->fec_recovered_frames()),
       wire_counter(media_receiver_->fec_unrecoverable_frames())};
  const auto payload = encode_feedback_report(report);
  if (payload.empty()) {
    return;
  }
  if (const auto sealed = crypto_->seal(PacketType::Telemetry, payload);
      sealed && session_->send(sealed->bytes)) {
    last_feedback_send_ = now;
  }
}

void RemoteRuntime::request_keyframe(SteadyClock::time_point now) {
  if (!streaming() || !session_ || !crypto_ ||
      (last_keyframe_request_ &&
       now - *last_keyframe_request_ < std::chrono::milliseconds{300})) {
    return;
  }
  const auto payload = encode_request_keyframe_control();
  if (const auto sealed = crypto_->seal(PacketType::Control, payload);
      sealed && session_->send(sealed->bytes)) {
    last_keyframe_request_ = now;
  }
}

void RemoteRuntime::disconnect_session() noexcept {
  if (backend_) {
    backend_->clear_rumble();
  }
  if (pairing() && session_) {
    send_pairing_confirmation(false);
  }
  if (streaming() && session_ && crypto_) {
    const auto control = encode_disconnect_control();
    if (const auto sealed = crypto_->seal(PacketType::Control, control); sealed) {
      session_->send(sealed->bytes);
    }
  }
  release_input();
  media_receiver_.reset();
  crypto_.reset();
  audio_decoder_.reset();
  codec_configured_ = false;
  expected_audio_sequence_ = 0;
  audio_primed_ = false;
  next_audio_playout_.reset();
  audio_jitter_ = AudioJitterBuffer{};
  gamepad_coalescer_ = InputCoalescer{};
  session_keys_.reset();
  selected_min_bitrate_bps_ = 0;
  selected_max_bitrate_bps_ = 0;
  identity_.reset();
  ephemeral_.reset();
  hello_.reset();
  handshake_retrier_.reset();
  pairing_offer_retrier_.reset();
  confirmation_retrier_.reset();
  last_keyframe_request_.reset();
  last_feedback_send_.reset();
  feedback_sequence_ = 0;
  telemetry_ = StreamAggregator{};
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
  pairing_offer_retrier_.reset();
  confirmation_retrier_.reset();
  if (state_ == RoleState::Pairing) {
    state_ = RoleState::RemoteBrowsing;
  }
}

}  // namespace ministream
