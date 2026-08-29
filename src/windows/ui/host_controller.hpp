#pragma once

#include "core/net/udp_endpoint.hpp"
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

namespace ministream {

class DxgiCapture;
class WasapiLoopback;
class VirtualGamepad;

class HostController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool ready READ ready NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool videoReady READ videoReady NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool audioReady READ audioReady NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool controllerReady READ controllerReady NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool networkReady READ networkReady NOTIFY capabilitiesChanged)
  Q_PROPERTY(QString videoDetail READ videoDetail NOTIFY capabilitiesChanged)
  Q_PROPERTY(QString audioDetail READ audioDetail NOTIFY capabilitiesChanged)
  Q_PROPERTY(QString controllerDetail READ controllerDetail NOTIFY capabilitiesChanged)
  Q_PROPERTY(QString networkDetail READ networkDetail NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool hosting READ hosting NOTIFY hostingChanged)
  Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged)
  Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY pairingChanged)

 public:
  explicit HostController(QObject* parent = nullptr);
  ~HostController() override;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool videoReady() const noexcept;
  [[nodiscard]] bool audioReady() const noexcept;
  [[nodiscard]] bool controllerReady() const noexcept;
  [[nodiscard]] bool networkReady() const noexcept;
  [[nodiscard]] QString videoDetail() const;
  [[nodiscard]] QString audioDetail() const;
  [[nodiscard]] QString controllerDetail() const;
  [[nodiscard]] QString networkDetail() const;
  [[nodiscard]] bool hosting() const noexcept;
  [[nodiscard]] bool pairing() const noexcept;
  [[nodiscard]] QString pairingCode() const;

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
  std::optional<DeviceIdentity> identity_;
  std::optional<EphemeralKeyPair> ephemeral_;
  std::optional<PairingOffer> peer_offer_;
  std::optional<PairingOffer> local_offer_;
  std::optional<SessionKeys> session_keys_;
  PairingConfirmation confirmation_;
};

}  // namespace ministream
