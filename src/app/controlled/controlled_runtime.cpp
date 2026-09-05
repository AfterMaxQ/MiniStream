#include "app/controlled/controlled_runtime.hpp"

#include "core/protocol/wire.hpp"
#include "core/input/gamepad_packet.hpp"
#include "core/input/rumble_packet.hpp"
#include "core/config/stream_profile.hpp"
#include "core/session/session_control.hpp"
#include "core/video/codec_config_wire.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <random>
#include <utility>

namespace ministream {
namespace {

std::uint64_t random_nonce() {
  std::random_device random;
  return (static_cast<std::uint64_t>(random()) << 32U) | random();
}

}  // namespace

ControlledRuntime::ControlledRuntime(std::unique_ptr<ControlledBackend> backend,
                                     DiscoveryAdvertisement advertisement,
                                     DiscoveryConfig discovery_config,
                                     SessionTiming timing)
    : backend_(std::move(backend)),
      advertisement_(std::move(advertisement)),
      discovery_config_(std::move(discovery_config)),
      timing_(timing),
      reliable_input_receiver_([this](const DesktopInput& input) {
        if (!backend_) {
          return false;
        }
        if (input.kind == DesktopInputKind::ReleaseAll) {
          backend_->clear_input();
          backend_->clear_gamepad();
          last_gamepad_receive_.reset();
          return true;
        }
        return backend_->inject_input(input);
      }),
      confirmation_retrier_(timing_.confirmation_retry_interval) {
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
  const auto active_capabilities = inspect();
  if (!active_capabilities.ready()) {
    stop();
    return false;
  }
  advertisement_.capabilities = {
      active_capabilities.h264, active_capabilities.hevc,
      active_capabilities.hdr10, active_capabilities.audio.ready,
      active_capabilities.input.ready, active_capabilities.optional_gamepad.ready};
  if (active_capabilities.max_width != 0) {
    advertisement_.max_width = static_cast<std::uint16_t>(std::min<std::uint32_t>(
        active_capabilities.max_width, std::numeric_limits<std::uint16_t>::max()));
  }
  if (active_capabilities.max_height != 0) {
    advertisement_.max_height = static_cast<std::uint16_t>(std::min<std::uint32_t>(
        active_capabilities.max_height, std::numeric_limits<std::uint16_t>::max()));
  }
  if (active_capabilities.max_fps != 0) {
    advertisement_.max_fps = static_cast<std::uint16_t>(std::min<std::uint32_t>(
        active_capabilities.max_fps, std::numeric_limits<std::uint16_t>::max()));
  }
  backend_->set_rumble_sender(
      [this](const RumblePacket& packet) { send_rumble(packet); });

  discovery_ = std::make_unique<DiscoveryHost>();
  session_ = std::make_unique<UdpEndpoint>();
  const auto identity = generate_identity();
  const auto ephemeral = generate_ephemeral_keypair();
  if (!identity || !ephemeral || !discovery_->start(discovery_config_) || !session_->bind(0)) {
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
  last_discovery_error_.reset();
  state_ = RoleState::Broadcasting;
  return true;
}

void ControlledRuntime::stop() noexcept {
  if (streaming() && session_ && crypto_) {
    const auto control = encode_disconnect_control();
    if (const auto sealed = crypto_->seal(PacketType::Control, control); sealed) {
      session_->reply(sealed->bytes);
    }
  }
  if (backend_) {
    backend_->clear_input();
    backend_->clear_gamepad();
    backend_->stop();
  }
  scheduler_.reset();
  media_sender_.reset();
  crypto_.reset();
  rate_controller_.reset();
  encoder_bitrate_bps_ = 0;
  current_fec_ratio_ = 0.03;
  audio_encoder_.reset();
  audio_pending_.clear();
  audio_pending_timestamp_us_.reset();
  {
    std::scoped_lock lock(rumble_mutex_);
    rumble_pending_.clear();
  }
  audio_sequence_ = 0;
  gamepad_sequence_filter_ = GamepadSequenceFilter{};
  last_gamepad_receive_.reset();
  reliable_input_receiver_.reset();
  discovery_.reset();
  last_discovery_error_.reset();
  session_.reset();
  identity_.reset();
  ephemeral_.reset();
  peer_hello_.reset();
  peer_handshake_deadline_.reset();
  pairing_deadline_.reset();
  confirmation_grace_deadline_.reset();
  next_confirmation_grace_send_.reset();
  last_authenticated_receive_.reset();
  last_heartbeat_send_.reset();
  peer_offer_.reset();
  local_offer_.reset();
  session_keys_.reset();
  pairing_code_.clear();
  confirmation_ = PairingConfirmation{};
  confirmation_retrier_.reset();
  last_codec_config_sent_.reset();
  last_codec_config_send_.reset();
  last_feedback_sequence_.reset();
  last_feedback_report_.reset();
  telemetry_ = StreamAggregator{};
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

std::optional<DiscoveryError> ControlledRuntime::last_discovery_error() const noexcept {
  return last_discovery_error_;
}

bool ControlledRuntime::set_advertisement(DiscoveryAdvertisement advertisement) {
  if (state_ != RoleState::Idle || advertisement.system == DiscoverySystem::Unknown ||
      advertisement.device_name.empty() || advertisement.session_port != 0 ||
      advertisement.max_width == 0 || advertisement.max_height == 0 ||
      advertisement.max_fps == 0) {
    return false;
  }
  advertisement.controllable = false;
  advertisement_ = std::move(advertisement);
  return true;
}

void ControlledRuntime::set_telemetry_callback(
    std::function<void(const StreamSnapshot&)> callback) {
  telemetry_callback_ = std::move(callback);
}

void ControlledRuntime::create_media_sender() {
  if (!session_keys_ || media_sender_ || !session_) {
    return;
  }
  crypto_ = std::make_unique<SessionCrypto>(
      session_id_, session_keys_->tx, session_keys_->rx, 0x4D535448U, 0x4D535443U);
  scheduler_ = std::make_unique<PacketScheduler>();
  encoder_bitrate_bps_ = std::max<std::uint32_t>(1U, negotiated_bitrate_);
  current_fec_ratio_ = 0.03;
  scheduler_->set_video_rate(
      required_video_wire_rate(encoder_bitrate_bps_, current_fec_ratio_));
  media_sender_ = std::make_unique<MediaSender>(session_id_, *crypto_, *scheduler_);
  media_sender_->set_fec_ratio(current_fec_ratio_);
  const auto profile = negotiated_width_ == 1920 && negotiated_height_ == 1080
                           ? stream_profile(StreamProfileId::Debug1080)
                           : (negotiated_width_ == 2560 && negotiated_height_ == 1440
                                  ? stream_profile(StreamProfileId::Balanced1440)
                                  : stream_profile(StreamProfileId::Quality4K));
  rate_controller_ = std::make_unique<RateController>(
      profile.minimum_bitrate_bps, profile.maximum_bitrate_bps, negotiated_bitrate_);
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
  const auto now = SteadyClock::now();
  send_pairing_confirmation(true);
  confirmation_retrier_.sent(now);
  if (!confirmation_.ready() || !ephemeral_ || !peer_offer_) {
    return;
  }
  finish_streaming(now);
}

void ControlledRuntime::finish_streaming(SteadyClock::time_point now) {
  if (!ephemeral_ || !peer_offer_) {
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
  begin_confirmation_grace(now);
  last_authenticated_receive_ = now;
  last_heartbeat_send_.reset();
}

void ControlledRuntime::begin_confirmation_grace(
    SteadyClock::time_point now) noexcept {
  confirmation_grace_deadline_ = now + timing_.confirmation_grace;
  next_confirmation_grace_send_ = now + timing_.confirmation_grace_interval;
}

void ControlledRuntime::tick_confirmation_grace(SteadyClock::time_point now) {
  if (!confirmation_grace_deadline_) {
    return;
  }
  if (now >= *confirmation_grace_deadline_) {
    confirmation_grace_deadline_.reset();
    next_confirmation_grace_send_.reset();
    return;
  }
  if (next_confirmation_grace_send_ && now >= *next_confirmation_grace_send_) {
    send_pairing_confirmation(true);
    next_confirmation_grace_send_ = now + timing_.confirmation_grace_interval;
  }
}

void ControlledRuntime::cancel_pairing() {
  if (session_) {
    const auto message = encode_pairing_confirmation(false);
    session_->reply(message);
  }
  clear_peer_session();
}

void ControlledRuntime::process_datagram(const ReceivedDatagram& incoming) {
  const auto bytes = std::span<const std::byte>{incoming.datagram.bytes};
  if (bytes.size() >= kCommonHeaderBytes) {
    if (const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
        common && common->type == PacketType::Input && crypto_) {
      if (const auto payload = crypto_->open(incoming.datagram); payload) {
        last_authenticated_receive_ = SteadyClock::now();
        if (const auto reliable = decode_reliable_desktop_input(*payload); reliable) {
          for (const auto sequence : reliable_input_receiver_.receive(*reliable)) {
            send_input_ack(sequence);
          }
        } else if (const auto input = decode_desktop_input(*payload);
                   input && backend_ &&
                   (input->kind == DesktopInputKind::MouseMove ||
                    input->kind == DesktopInputKind::MouseWheel)) {
          (void)backend_->inject_input(*input);
        } else if (const auto gamepad = decode_gamepad_packet(*payload); gamepad && backend_ &&
                   gamepad_sequence_filter_.accept(gamepad->sequence)) {
          (void)backend_->submit_gamepad(gamepad->state);
          last_gamepad_receive_ = SteadyClock::now();
        }
      }
      return;
    }
  }

  if (bytes.size() >= kCommonHeaderBytes) {
    if (const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
        common && common->type == PacketType::Telemetry && crypto_ && streaming()) {
      if (const auto payload = crypto_->open(incoming.datagram); payload) {
        last_authenticated_receive_ = SteadyClock::now();
        if (const auto report = decode_feedback_report(*payload); report &&
            (!last_feedback_sequence_ ||
             static_cast<std::int32_t>(report->report_sequence - *last_feedback_sequence_) > 0)) {
          last_feedback_sequence_ = report->report_sequence;
          apply_feedback(*report);
        }
      }
      return;
    }
  }

  if (bytes.size() >= kCommonHeaderBytes) {
    if (const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
        common && common->type == PacketType::Control && crypto_ && streaming()) {
      if (const auto payload = crypto_->open(incoming.datagram); payload) {
        last_authenticated_receive_ = SteadyClock::now();
        if (is_disconnect_control(*payload)) {
          clear_peer_session();
        } else if (is_request_keyframe_control(*payload) && backend_) {
          backend_->request_keyframe();
          last_codec_config_send_.reset();
        }
      }
      return;
    }
  }

  if (const auto hello = decode_hello(bytes);
      hello && session_ && state_ == RoleState::Broadcasting) {
    if (peer_hello_ && *peer_hello_ != *hello) {
      return;
    }
    if (!peer_hello_) {
      const auto capabilities = inspect();
      const bool codec_supported = hello->codec == VideoCodec::H264
                                       ? capabilities.h264 && advertisement_.capabilities.h264
                                       : capabilities.hevc && advertisement_.capabilities.hevc;
      const bool dimensions_supported = capabilities.max_width >= hello->width &&
                                        capabilities.max_height >= hello->height &&
                                        capabilities.max_fps >= hello->fps &&
                                        advertisement_.max_width >= hello->width &&
                                        advertisement_.max_height >= hello->height &&
                                        advertisement_.max_fps >= hello->fps;
      if (!codec_supported || !dimensions_supported ||
          (hello->hdr10 && (!capabilities.hdr10 || !advertisement_.capabilities.hdr10))) {
        return;
      }
      const auto profile = hello->width == 1920 && hello->height == 1080
                               ? stream_profile(StreamProfileId::Debug1080)
                               : (hello->width == 2560 && hello->height == 1440
                                      ? stream_profile(StreamProfileId::Balanced1440)
                                      : stream_profile(StreamProfileId::Quality4K));
      negotiated_bitrate_ = static_cast<std::uint32_t>(std::clamp(
          hello->target_bitrate_bps == 0 ? profile.initial_bitrate_bps
                                         : static_cast<std::uint64_t>(hello->target_bitrate_bps),
          profile.minimum_bitrate_bps, profile.maximum_bitrate_bps));
      if (!ephemeral_) {
        const auto ephemeral = generate_ephemeral_keypair();
        if (!ephemeral) {
          return;
        }
        ephemeral_ = *ephemeral;
      }
      if (!backend_ || !backend_->configure_video(
                           {hello->codec, hello->width, hello->height, hello->fps,
                            hello->hdr10, {}}) ||
          !backend_->reconfigure_bitrate(negotiated_bitrate_)) {
        return;
      }
      if (!session_->peer_locked() && !session_->lock_peer(incoming)) {
        return;
      }
      peer_hello_ = *hello;
      negotiated_codec_ = hello->codec;
      negotiated_width_ = hello->width;
      negotiated_height_ = hello->height;
      negotiated_fps_ = hello->fps;
    }
    peer_handshake_deadline_ = SteadyClock::now() + timing_.handshake_lease;
    session_->reply(encode_accept(
        {HandshakeRole::Controlled, session_id_, hello->codec, hello->hdr10, hello->width,
         hello->height, hello->fps, negotiated_bitrate_, hello->nonce}));
    return;
  }

  if (const auto offer = decode_pairing_offer(bytes);
      offer && (state_ == RoleState::Broadcasting || state_ == RoleState::Pairing) &&
      offer->role == PairingRole::Initiator && identity_ && ephemeral_ && session_) {
    if (!peer_hello_ || offer->nonce != peer_hello_->nonce) {
      return;
    }
    if (peer_offer_ && *peer_offer_ != *offer) {
      return;
    }
    peer_offer_ = *offer;
    if (!local_offer_) {
      local_offer_ = PairingOffer{PairingRole::Responder, random_nonce(),
                                  identity_->public_key, ephemeral_->public_key};
    }
    const auto transcript = pairing_transcript(*peer_offer_, *local_offer_);
    if (!transcript) {
      return;
    }
    pairing_code_ = std::to_string(compute_pairing_sas(*transcript));
    if (pairing_code_.size() < 6) {
      pairing_code_.insert(pairing_code_.begin(), 6 - pairing_code_.size(), '0');
    }
    if (state_ == RoleState::Broadcasting) {
      state_ = RoleState::Pairing;
      peer_handshake_deadline_.reset();
      pairing_deadline_ = SteadyClock::now() + timing_.pairing_lease;
    }
    session_->reply(encode_pairing_offer(*local_offer_));
    return;
  }

  if (const auto accepted = decode_pairing_confirmation(bytes); accepted && session_) {
    const auto now = SteadyClock::now();
    if (state_ == RoleState::Pairing) {
      if (!*accepted) {
        clear_peer_session();
        return;
      }
      confirmation_.confirm_peer();
      if (confirmation_.ready() && ephemeral_ && peer_offer_) {
        finish_streaming(now);
      }
    } else if (*accepted && streaming() && confirmation_grace_deadline_ &&
               now < *confirmation_grace_deadline_) {
      send_pairing_confirmation(true);
    }
  }
}

void ControlledRuntime::apply_feedback(const FeedbackReport& report) {
  const auto delta = feedback_delta(report, last_feedback_report_);
  last_feedback_report_ = report;
  if (!rate_controller_ || !scheduler_ || !media_sender_ || !backend_) {
    return;
  }
  const auto total = delta.received_video_packets + delta.lost_video_packets;
  const auto loss = total == 0
                        ? 0.0
                        : static_cast<double>(delta.lost_video_packets) /
                              static_cast<double>(total);
  const NetworkFeedback feedback{
      scheduler_->estimated_video_queue_delay(), Microseconds{0},
      Microseconds{static_cast<std::int64_t>(report.jitter_us)}, loss,
      static_cast<std::uint32_t>(std::min<std::uint64_t>(
          delta.fec_unrecoverable, std::numeric_limits<std::uint32_t>::max()))};
  const auto decision = rate_controller_->update(feedback, SteadyClock::now());
  if (decision.bitrate_bps != encoder_bitrate_bps_) {
    if (!backend_->reconfigure_bitrate(static_cast<std::uint32_t>(decision.bitrate_bps))) {
      std::clog << "rate update rejected by encoder; keeping bitrate="
                << encoder_bitrate_bps_ << '\n';
      return;
    }
    encoder_bitrate_bps_ = decision.bitrate_bps;
  }
  current_fec_ratio_ = decision.fec_ratio;
  media_sender_->set_fec_ratio(current_fec_ratio_);
  const auto wire_rate = required_video_wire_rate(encoder_bitrate_bps_, current_fec_ratio_);
  if (wire_rate != scheduler_->video_rate_bps()) {
    scheduler_->set_video_rate(wire_rate);
  }
}

void ControlledRuntime::send_pending_video(SteadyClock::time_point now) {
  if (!media_sender_ || !session_ || !crypto_ || !backend_) {
    return;
  }
  const auto frame = backend_->next_video();
  // Capturing/encoding may take longer than a packet's queue deadline.
  now = SteadyClock::now();
  const auto config = backend_->codec_config();
  if (!config.parameter_sets.empty()) {
    constexpr auto kCodecConfigRetryInterval = std::chrono::milliseconds{500};
    const bool changed = !last_codec_config_sent_ || *last_codec_config_sent_ != config;
    const bool retry = !last_codec_config_send_ ||
                       now - *last_codec_config_send_ >= kCodecConfigRetryInterval;
    if (changed || retry) {
      const auto payload = encode_codec_config(config);
      if (!payload.empty()) {
        if (const auto packet = crypto_->seal(PacketType::Control, payload);
            packet && session_->reply(packet->bytes)) {
          last_codec_config_sent_ = config;
          last_codec_config_send_ = now;
        }
      }
    }
  }
  if (frame) {
    // P-frames behind a paced IDR must survive until that IDR has left the
    // queue. Dropping all of them at 25 ms otherwise causes an IDR loop.
    const auto budget = frame->keyframe ? Microseconds{500'000} : std::clamp(
        scheduler_->estimated_video_queue_delay() + Microseconds{25'000},
        Microseconds{25'000}, Microseconds{500'000});
    if (media_sender_->enqueue_video(*frame, now, budget) == 0) backend_->request_keyframe();
  }
}

void ControlledRuntime::send_pending_audio(SteadyClock::time_point now) {
  if (!media_sender_ || !audio_encoder_ || !audio_encoder_->ready() || !backend_) {
    return;
  }
  constexpr std::size_t kMaxPendingSamples = 48'000U * 2U * 60U / 1000U;
  // Drain capture bursts, retaining at most 60 ms of fresh audio.
  for (unsigned count = 0; count < 32; ++count) {
    const auto pcm = backend_->next_audio();
    if (!pcm) break;
    if (pcm->frames == 0 || pcm->interleaved_stereo.size() != static_cast<std::size_t>(pcm->frames) * 2U)
      continue;
    if (pcm->discontinuity) audio_pending_.clear();
    if (audio_pending_.empty()) audio_pending_timestamp_us_ = pcm->host_timestamp_us;
    audio_pending_.insert(audio_pending_.end(), pcm->interleaved_stereo.begin(),
                          pcm->interleaved_stereo.end());
    if (audio_pending_.size() > kMaxPendingSamples) {
      const auto dropped = audio_pending_.size() - kMaxPendingSamples;
      audio_pending_.erase(audio_pending_.begin(), audio_pending_.begin() + dropped);
      *audio_pending_timestamp_us_ += dropped / 2U * 1'000'000ULL / 48'000ULL;
    }
  }
  while (audio_pending_.size() >= kOpusFrameSamplesPerChannel * 2U) {
    const auto encoded = audio_encoder_->encode(
        std::span<const float>{audio_pending_.data(), kOpusFrameSamplesPerChannel * 2U});
    if (encoded) {
      now = SteadyClock::now();
      media_sender_->enqueue_audio(
          {audio_sequence_++, *audio_pending_timestamp_us_,
           static_cast<std::uint16_t>(kOpusFrameSamplesPerChannel), *encoded},
          now);
    }
    audio_pending_.erase(audio_pending_.begin(),
                         audio_pending_.begin() + kOpusFrameSamplesPerChannel * 2U);
    *audio_pending_timestamp_us_ += 10'000;
  }
}

void ControlledRuntime::send_rumble(const RumblePacket& packet) {
  std::scoped_lock lock(rumble_mutex_);
  if (rumble_pending_.size() < 16U) {
    rumble_pending_.push_back(packet);
  }
}

void ControlledRuntime::send_pending_rumble() {
  if (!session_ || !crypto_ || !streaming()) {
    return;
  }
  for (;;) {
    RumblePacket packet;
    {
      std::scoped_lock lock(rumble_mutex_);
      if (rumble_pending_.empty()) {
        break;
      }
      packet = rumble_pending_.front();
      rumble_pending_.pop_front();
    }
    const auto payload = encode_rumble_packet(packet);
    if (const auto sealed = crypto_->seal(PacketType::Feedback, payload)) {
      session_->reply(sealed->bytes);
    }
  }
}

void ControlledRuntime::tick() {
  if (!hosting() || !discovery_ || !session_) {
    return;
  }
  const auto now = SteadyClock::now();
  if (state_ == RoleState::Broadcasting && peer_hello_ &&
      peer_handshake_deadline_ && now >= *peer_handshake_deadline_) {
    clear_peer_session();
  }
  if (pairing() && pairing_deadline_ && now >= *pairing_deadline_) {
    send_pairing_confirmation(false);
    clear_peer_session();
    return;
  }
  if (state_ == RoleState::Broadcasting && !session_->peer_locked()) {
    const auto result = discovery_->poll(advertisement_);
    if (result) {
      last_discovery_error_.reset();
    } else {
      last_discovery_error_ = result.error();
    }
  }
  for (const auto& incoming : session_->try_receive_batch(512)) {
    if (session_->peer_locked() && !session_->matches_peer(incoming)) {
      continue;
    }
    process_datagram(incoming);
    if (!session_) return;
  }
  if (streaming() && last_authenticated_receive_ &&
      now - *last_authenticated_receive_ >= timing_.liveness_timeout) {
    clear_peer_session();
    return;
  }
  if (pairing() && confirmation_.local_confirmed() && !confirmation_.ready()) {
    if (confirmation_retrier_.due(now)) {
      send_pairing_confirmation(true);
      confirmation_retrier_.sent(now);
    } else if (confirmation_retrier_.expired(now)) {
      confirmation_retrier_.reset();
    }
  }
  if (!streaming()) {
    // Capture starts while advertising; do not replay pairing-time audio later.
    for (unsigned count = 0; backend_ && count < 32 && backend_->next_audio(); ++count) {}
    return;
  }
  if (last_gamepad_receive_ && now - *last_gamepad_receive_ >= std::chrono::milliseconds{250}) {
    backend_->clear_gamepad();
    last_gamepad_receive_.reset();
  }
  tick_confirmation_grace(now);
  send_heartbeat(now);
  send_pending_rumble();
  send_pending_video(now);
  send_pending_audio(now);
  if (!scheduler_) {
    return;
  }
  constexpr std::size_t kMaxSendPacketsPerTick = 256;
  bool fatal_send_error{};
  scheduler_->consume_ready(SteadyClock::now(), kMaxSendPacketsPerTick, [&](const Datagram& datagram) {
    const auto result = session_->reply(datagram.bytes);
    if (result) {
      return true;
    }
    fatal_send_error = result.error() != NetError::WouldBlock;
    return false;
  });
  if (fatal_send_error) {
    clear_peer_session();
    return;
  }
  StreamSample sample;
  sample.bitrate_bps = encoder_bitrate_bps_;
  sample.fec_ratio = media_sender_ ? media_sender_->fec_ratio() : 0.0;
  sample.send_queue_ms = static_cast<double>(
      std::chrono::duration_cast<Microseconds>(scheduler_->estimated_video_queue_delay())
          .count()) /
                         1000.0;
  sample.controller_connected = true;
  telemetry_.push(sample);
  if (const auto snapshot = telemetry_.publish_if_due(now); snapshot && telemetry_callback_) {
    telemetry_callback_(*snapshot);
  }
}

void ControlledRuntime::send_pairing_confirmation(bool accepted) {
  if (session_) {
    session_->reply(encode_pairing_confirmation(accepted));
  }
}

void ControlledRuntime::send_heartbeat(SteadyClock::time_point now) {
  if (!session_ || !crypto_ ||
      (last_heartbeat_send_ &&
       now - *last_heartbeat_send_ < timing_.heartbeat_interval)) {
    return;
  }
  const auto payload = encode_heartbeat_control();
  if (const auto sealed = crypto_->seal(PacketType::Control, payload);
      sealed && session_->reply(sealed->bytes)) {
    last_heartbeat_send_ = now;
  }
}

void ControlledRuntime::send_input_ack(ControlSeq sequence) {
  if (!session_ || !crypto_) {
    return;
  }
  const auto payload = encode_input_ack_control(sequence);
  if (const auto sealed = crypto_->seal(PacketType::Control, payload); sealed) {
    (void)session_->reply(sealed->bytes);
  }
}

void ControlledRuntime::clear_peer_session() noexcept {
  if (backend_) {
    backend_->clear_input();
    backend_->clear_gamepad();
  }
  scheduler_.reset();
  media_sender_.reset();
  crypto_.reset();
  rate_controller_.reset();
  encoder_bitrate_bps_ = 0;
  current_fec_ratio_ = 0.03;
  audio_encoder_.reset();
  audio_pending_.clear();
  audio_pending_timestamp_us_.reset();
  {
    std::scoped_lock lock(rumble_mutex_);
    rumble_pending_.clear();
  }
  audio_sequence_ = 0;
  gamepad_sequence_filter_ = GamepadSequenceFilter{};
  reliable_input_receiver_.reset();
  session_keys_.reset();
  peer_hello_.reset();
  peer_handshake_deadline_.reset();
  confirmation_grace_deadline_.reset();
  next_confirmation_grace_send_.reset();
  last_authenticated_receive_.reset();
  last_heartbeat_send_.reset();
  ephemeral_.reset();
  last_codec_config_sent_.reset();
  last_codec_config_send_.reset();
  last_feedback_sequence_.reset();
  last_feedback_report_.reset();
  telemetry_ = StreamAggregator{};
  last_gamepad_receive_.reset();
  if (session_) {
    session_->clear_peer();
  }
  reset_pairing();
  if (state_ != RoleState::Idle) {
    state_ = RoleState::Broadcasting;
  }
}

void ControlledRuntime::reset_pairing() noexcept {
  const bool was_pairing = pairing();
  pairing_code_.clear();
  pairing_deadline_.reset();
  confirmation_grace_deadline_.reset();
  next_confirmation_grace_send_.reset();
  peer_offer_.reset();
  local_offer_.reset();
  confirmation_ = PairingConfirmation{};
  confirmation_retrier_.reset();
  if (was_pairing) {
    state_ = RoleState::Broadcasting;
  }
}

}  // namespace ministream
