#pragma once

#include "core/audio/audio_packet.hpp"
#include "core/audio/opus_codec.hpp"
#include "core/adaptation/rate_controller.hpp"
#include "core/input/desktop_input.hpp"
#include "core/input/gamepad_packet.hpp"
#include "core/input/reliable_desktop_input.hpp"
#include "core/media/media_pipeline.hpp"
#include "core/net/udp_endpoint.hpp"
#include "core/security/identity.hpp"
#include "core/security/pairing.hpp"
#include "core/security/pairing_wire.hpp"
#include "core/session/discovery.hpp"
#include "core/session/role.hpp"
#include "core/session/handshake.hpp"
#include "core/session/session_timing.hpp"
#include "core/telemetry/feedback_wire.hpp"
#include "core/telemetry/stream_aggregator.hpp"
#include "platform/controlled_backend.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ministream {

class ControlledRuntime {
 public:
  ControlledRuntime(std::unique_ptr<ControlledBackend> backend,
                    DiscoveryAdvertisement advertisement,
                    DiscoveryConfig discovery_config = {},
                    SessionTiming timing = {});
  ~ControlledRuntime();

  ControlledRuntime(const ControlledRuntime&) = delete;
  ControlledRuntime& operator=(const ControlledRuntime&) = delete;

  [[nodiscard]] ControlledCapabilities inspect() const;
  [[nodiscard]] bool start();
  void stop() noexcept;
  [[nodiscard]] bool hosting() const noexcept;
  [[nodiscard]] bool pairing() const noexcept;
  [[nodiscard]] bool streaming() const noexcept;
  [[nodiscard]] RoleState state() const noexcept;
  [[nodiscard]] const std::string& pairing_code() const noexcept;
  [[nodiscard]] const DiscoveryAdvertisement& advertisement() const noexcept;
  [[nodiscard]] std::optional<DiscoveryError> last_discovery_error() const noexcept;
  bool set_advertisement(DiscoveryAdvertisement advertisement);
  void set_telemetry_callback(std::function<void(const StreamSnapshot&)> callback);

  void confirm_pairing();
  void cancel_pairing();
  void tick();

 private:
  void reset_pairing() noexcept;
  void create_media_sender();
  void process_datagram(const ReceivedDatagram& incoming);
  void send_pending_audio(SteadyClock::time_point now);
  void send_pending_video(SteadyClock::time_point now);
  void send_rumble(const RumblePacket& packet);
  void send_pending_rumble();
  void apply_feedback(const FeedbackReport& report);
  void clear_peer_session() noexcept;
  void finish_streaming(SteadyClock::time_point now);
  void begin_confirmation_grace(SteadyClock::time_point now) noexcept;
  void tick_confirmation_grace(SteadyClock::time_point now);
  void send_heartbeat(SteadyClock::time_point now);
  void send_pairing_confirmation(bool accepted);
  void send_input_ack(ControlSeq sequence);

  std::unique_ptr<ControlledBackend> backend_;
  DiscoveryAdvertisement advertisement_;
  DiscoveryConfig discovery_config_;
  SessionTiming timing_;
  RoleState state_{RoleState::Idle};
  std::unique_ptr<DiscoveryHost> discovery_;
  std::optional<DiscoveryError> last_discovery_error_;
  std::unique_ptr<UdpEndpoint> session_;
  std::unique_ptr<PacketScheduler> scheduler_;
  std::unique_ptr<SessionCrypto> crypto_;
  std::unique_ptr<MediaSender> media_sender_;
  std::unique_ptr<OpusEncoder48kStereo> audio_encoder_;
  std::vector<float> audio_pending_;
  std::mutex rumble_mutex_;
  std::deque<RumblePacket> rumble_pending_;
  std::uint32_t audio_sequence_{};
  GamepadSequenceFilter gamepad_sequence_filter_;
  ReliableDesktopInputReceiver reliable_input_receiver_;
  std::optional<DeviceIdentity> identity_;
  std::optional<EphemeralKeyPair> ephemeral_;
  std::optional<Hello> peer_hello_;
  std::optional<SteadyClock::time_point> peer_handshake_deadline_;
  std::optional<SteadyClock::time_point> pairing_deadline_;
  std::optional<SteadyClock::time_point> confirmation_grace_deadline_;
  std::optional<SteadyClock::time_point> next_confirmation_grace_send_;
  std::optional<SteadyClock::time_point> last_authenticated_receive_;
  std::optional<SteadyClock::time_point> last_heartbeat_send_;
  std::optional<PairingOffer> peer_offer_;
  std::optional<PairingOffer> local_offer_;
  std::optional<SessionKeys> session_keys_;
  PairingConfirmation confirmation_;
  PairingMessageRetrier confirmation_retrier_;
  std::string pairing_code_;
  VideoCodec negotiated_codec_{VideoCodec::H264};
  std::uint16_t negotiated_width_{1920};
  std::uint16_t negotiated_height_{1080};
  std::uint16_t negotiated_fps_{60};
  std::uint32_t negotiated_bitrate_{20'000'000};
  std::uint64_t encoder_bitrate_bps_{};
  double current_fec_ratio_{0.03};
  std::optional<CodecConfig> last_codec_config_sent_;
  std::optional<SteadyClock::time_point> last_codec_config_send_;
  std::optional<std::uint32_t> last_feedback_sequence_;
  std::optional<FeedbackReport> last_feedback_report_;
  std::unique_ptr<RateController> rate_controller_;
  StreamAggregator telemetry_;
  std::function<void(const StreamSnapshot&)> telemetry_callback_;
  SessionId session_id_{1};
};

}  // namespace ministream
