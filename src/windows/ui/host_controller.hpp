#pragma once

#include "core/net/udp_endpoint.hpp"
#include "core/audio/opus_codec.hpp"
#include "core/media/media_pipeline.hpp"
#include "core/video/codec_config.hpp"
#include "core/security/identity.hpp"
#include "core/security/pairing.hpp"
#include "core/security/pairing_wire.hpp"
#include "core/session/discovery.hpp"
#include "windows/platform/host_capabilities.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>
#include <vector>

namespace ministream {

class DxgiCapture;
class WasapiLoopback;
class VirtualGamepad;
class NvencEncoder;
class RemoteInputSink;

class HostController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool ready READ ready NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool videoReady READ videoReady NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool audioReady READ audioReady NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool inputReady READ inputReady NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool networkReady READ networkReady NOTIFY capabilitiesChanged)
  Q_PROPERTY(QString videoDetail READ videoDetail NOTIFY capabilitiesChanged)
  Q_PROPERTY(QString audioDetail READ audioDetail NOTIFY capabilitiesChanged)
  Q_PROPERTY(QString inputDetail READ inputDetail NOTIFY capabilitiesChanged)
  Q_PROPERTY(QString networkDetail READ networkDetail NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool hosting READ hosting NOTIFY hostingChanged)
  Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged)
  Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY pairingChanged)
  Q_PROPERTY(QString deviceLabel READ deviceLabel CONSTANT)
  Q_PROPERTY(QString broadcastStatus READ broadcastStatus NOTIFY hostingChanged)

 public:
  explicit HostController(QObject* parent = nullptr);
  ~HostController() override;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool videoReady() const noexcept;
  [[nodiscard]] bool audioReady() const noexcept;
  [[nodiscard]] bool inputReady() const noexcept;
  [[nodiscard]] bool networkReady() const noexcept;
  [[nodiscard]] QString videoDetail() const;
  [[nodiscard]] QString audioDetail() const;
  [[nodiscard]] QString inputDetail() const;
  [[nodiscard]] QString networkDetail() const;
  [[nodiscard]] bool hosting() const noexcept;
  [[nodiscard]] bool pairing() const noexcept;
  [[nodiscard]] QString pairingCode() const;
  [[nodiscard]] QString deviceLabel() const;
  [[nodiscard]] QString broadcastStatus() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void startHost();
  Q_INVOKABLE void stopHost();
  Q_INVOKABLE void confirmPairing();
  Q_INVOKABLE void cancelPairing();

 signals:
  void capabilitiesChanged();
  void hostingChanged();
  void pairingChanged();

 private:
  void pollNetwork();
  void resetPairing();
  void createMediaCrypto();
  [[nodiscard]] DiscoveryAdvertisement discoveryAdvertisement() const;

  HostCapabilities capabilities_;
  bool hosting_{};
  bool pairing_{};
  QString pairing_code_;
  QTimer poll_timer_;
  std::unique_ptr<DxgiCapture> capture_;
  std::unique_ptr<WasapiLoopback> audio_;
  std::unique_ptr<VirtualGamepad> gamepad_;
  std::unique_ptr<DiscoveryHost> discovery_;
  std::unique_ptr<UdpEndpoint> session_;
  std::unique_ptr<NvencEncoder> encoder_;
  std::unique_ptr<OpusEncoder48kStereo> opus_encoder_;
  std::unique_ptr<PacketScheduler> scheduler_;
  std::unique_ptr<SessionCrypto> crypto_;
  std::unique_ptr<MediaSender> media_sender_;
  std::unique_ptr<RemoteInputSink> input_sink_;
  std::vector<float> audio_pending_;
  VideoCodec negotiated_codec_{VideoCodec::H264};
  std::uint32_t negotiated_width_{1920};
  std::uint32_t negotiated_height_{1080};
  std::uint32_t negotiated_fps_{60};
  std::uint32_t negotiated_bitrate_{20'000'000};
  SessionId session_id_{1};
  std::uint32_t audio_sequence_{};
  bool encoder_attempted_{};
  bool codec_config_sent_{};
  std::optional<DeviceIdentity> identity_;
  std::optional<EphemeralKeyPair> ephemeral_;
  std::optional<PairingOffer> peer_offer_;
  std::optional<PairingOffer> local_offer_;
  std::optional<SessionKeys> session_keys_;
  PairingConfirmation confirmation_;
};

}  // namespace ministream
