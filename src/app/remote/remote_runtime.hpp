#pragma once

#include "core/audio/audio_packet.hpp"
#include "core/audio/drift_controller.hpp"
#include "core/audio/jitter_buffer.hpp"
#include "core/audio/opus_codec.hpp"
#include "core/input/desktop_input.hpp"
#include "core/input/gamepad_packet.hpp"
#include "core/input/input_coalescer.hpp"
#include "core/input/input_capture.hpp"
#include "core/input/remote_input_router.hpp"
#include "core/media/media_pipeline.hpp"
#include "core/net/udp_endpoint.hpp"
#include "core/security/identity.hpp"
#include "core/security/pairing.hpp"
#include "core/security/pairing_wire.hpp"
#include "core/session/discovery.hpp"
#include "core/session/handshake.hpp"
#include "core/session/role.hpp"
#include "platform/remote_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ministream {

class RemoteRuntime {
 public:
  explicit RemoteRuntime(std::unique_ptr<RemoteBackend> backend);
  ~RemoteRuntime();

  RemoteRuntime(const RemoteRuntime&) = delete;
  RemoteRuntime& operator=(const RemoteRuntime&) = delete;

  [[nodiscard]] RemoteCapabilities inspect() const;
  [[nodiscard]] bool start();
  void stop() noexcept;
  [[nodiscard]] RoleState state() const noexcept;
  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] bool pairing() const noexcept;
  [[nodiscard]] bool streaming() const noexcept;
  [[nodiscard]] bool remote_input_active() const noexcept;
  [[nodiscard]] const std::string& pairing_code() const noexcept;
  [[nodiscard]] const std::vector<DiscoveredHost>& hosts() const noexcept;
  [[nodiscard]] const std::optional<DiscoveredHost>& selected_host() const noexcept;

  bool refresh(Microseconds timeout = Microseconds{250});
  bool connect(std::size_t index);
  void confirm_pairing();
  void cancel_pairing();
  void toggle_input();
  void release_input() noexcept;
  bool route_input(const DesktopInput& input);
  void tick();

 private:
  void disconnect_session() noexcept;
  void reset_pairing() noexcept;
  void process_datagram(const ReceivedDatagram& incoming);
  void poll_media(const ReceivedDatagram& incoming);
  void send_input(const DesktopInput& input);
  void send_gamepad(const GamepadPacket& packet);

  std::unique_ptr<RemoteBackend> backend_;
  RoleState state_{RoleState::Idle};
  std::vector<DiscoveredHost> hosts_;
  std::optional<DiscoveredHost> selected_host_;
  std::unique_ptr<UdpEndpoint> session_;
  std::unique_ptr<MediaReceiver> media_receiver_;
  std::unique_ptr<SessionCrypto> crypto_;
  std::unique_ptr<OpusDecoder48kStereo> audio_decoder_;
  AudioJitterBuffer audio_jitter_;
  std::uint32_t expected_audio_sequence_{};
  std::optional<DeviceIdentity> identity_;
  std::optional<EphemeralKeyPair> ephemeral_;
  std::optional<PairingOffer> local_offer_;
  std::optional<PairingOffer> peer_offer_;
  std::optional<SessionKeys> session_keys_;
  PairingConfirmation confirmation_;
  std::string pairing_code_;
  InputCapture input_capture_;
  std::unique_ptr<RemoteInputRouter> input_router_;
  InputCoalescer gamepad_coalescer_;
  SessionId session_id_{1};
  bool codec_configured_{};
};

}  // namespace ministream
