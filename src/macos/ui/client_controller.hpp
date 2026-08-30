#pragma once

#include "core/input/input_capture.hpp"
#include "core/audio/audio_packet.hpp"
#include "core/audio/jitter_buffer.hpp"
#include "core/audio/opus_codec.hpp"
#include "core/media/media_pipeline.hpp"
#include "core/net/udp_endpoint.hpp"
#include "core/security/identity.hpp"
#include "core/security/pairing.hpp"
#include "core/security/pairing_wire.hpp"
#include "core/session/discovery.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>
#include <cstdint>
#include <vector>

namespace ministream {

class VideoToolboxDecoder;
class CoreAudioOutput;

class ClientController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList hosts READ hosts NOTIFY hostsChanged)
  Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
  Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged)
  Q_PROPERTY(bool remoteInputActive READ remoteInputActive NOTIFY remoteInputChanged)
  Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY pairingChanged)

 public:
  explicit ClientController(QObject* parent = nullptr);
  ~ClientController() override;

  [[nodiscard]] QStringList hosts() const;
  [[nodiscard]] bool searching() const noexcept;
  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] bool pairing() const noexcept;
  [[nodiscard]] bool remoteInputActive() const noexcept;
  [[nodiscard]] QString pairingCode() const;

  Q_INVOKABLE void refreshHosts();
  Q_INVOKABLE void connectToHost(int index);
  Q_INVOKABLE void toggleRemoteInput();
  Q_INVOKABLE void releaseRemoteInput();
  Q_INVOKABLE void confirmPairing();
  Q_INVOKABLE void cancelPairing();

 signals:
  void hostsChanged();
  void searchingChanged();
  void connectedChanged();
  void pairingChanged();
  void remoteInputChanged();

 private:
  void disconnectSession();
  void pollConfirmation();
  void resetPairing();
  void createMediaReceiver();
  void pollMedia(const ReceivedDatagram& incoming);

  QStringList host_labels_;
  std::vector<DiscoveredHost> discovered_;
  bool searching_{};
  bool connected_{};
  bool pairing_{};
  QString pairing_code_;
  QTimer poll_timer_;
  std::unique_ptr<UdpEndpoint> session_;
  std::optional<DeviceIdentity> identity_;
  std::optional<EphemeralKeyPair> ephemeral_;
  std::optional<PairingOffer> local_offer_;
  std::optional<PairingOffer> peer_offer_;
  std::optional<SessionKeys> session_keys_;
  std::unique_ptr<VideoToolboxDecoder> video_decoder_;
  std::unique_ptr<CoreAudioOutput> audio_output_;
  std::unique_ptr<OpusDecoder48kStereo> audio_decoder_;
  std::unique_ptr<SessionCrypto> crypto_;
  std::unique_ptr<MediaReceiver> media_receiver_;
  AudioJitterBuffer audio_jitter_;
  std::uint32_t expected_audio_sequence_{};
  InputCapture input_capture_;
  std::optional<InputCapture::Lease> keyboard_lease_;
  std::optional<InputCapture::Lease> mouse_lease_;
  std::optional<InputCapture::Lease> gamepad_lease_;
  PairingConfirmation confirmation_;
  SessionId session_id_{1};
};

}  // namespace ministream
