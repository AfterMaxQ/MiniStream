#pragma once

#include "core/session/role.hpp"
#include "platform/capabilities.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace ministream {

class ControlledRuntime;
class RemoteRuntime;

class RoleController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
  Q_PROPERTY(bool controlledAvailable READ controlledAvailable CONSTANT)
  Q_PROPERTY(bool remoteAvailable READ remoteAvailable CONSTANT)
  Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
  Q_PROPERTY(bool broadcasting READ broadcasting NOTIFY stateChanged)
  Q_PROPERTY(bool searching READ searching NOTIFY stateChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
  Q_PROPERTY(bool pairing READ pairing NOTIFY stateChanged)
  Q_PROPERTY(bool remoteInputActive READ remoteInputActive NOTIFY stateChanged)
  Q_PROPERTY(QString deviceLabel READ deviceLabel CONSTANT)
  Q_PROPERTY(QString broadcastStatus READ broadcastStatus NOTIFY stateChanged)
  Q_PROPERTY(QStringList hosts READ hosts NOTIFY stateChanged)
  Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY stateChanged)
  Q_PROPERTY(QString selectedDeviceLabel READ selectedDeviceLabel NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  Q_PROPERTY(bool videoReady READ videoReady NOTIFY stateChanged)
  Q_PROPERTY(bool audioReady READ audioReady NOTIFY stateChanged)
  Q_PROPERTY(bool inputReady READ inputReady NOTIFY stateChanged)
  Q_PROPERTY(bool networkReady READ networkReady NOTIFY stateChanged)
  Q_PROPERTY(QString videoDetail READ videoDetail NOTIFY stateChanged)
  Q_PROPERTY(QString audioDetail READ audioDetail NOTIFY stateChanged)
  Q_PROPERTY(QString inputDetail READ inputDetail NOTIFY stateChanged)
  Q_PROPERTY(QString networkDetail READ networkDetail NOTIFY stateChanged)
  Q_PROPERTY(bool permissionActionAvailable READ permissionActionAvailable NOTIFY stateChanged)
  Q_PROPERTY(QObject* videoSurface READ videoSurface NOTIFY stateChanged)

 public:
  explicit RoleController(QObject* parent = nullptr);
  ~RoleController() override;

  [[nodiscard]] int mode() const noexcept;
  Q_INVOKABLE void setMode(int mode);
  [[nodiscard]] bool controlledAvailable() const noexcept;
  [[nodiscard]] bool remoteAvailable() const noexcept;
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool broadcasting() const noexcept;
  [[nodiscard]] bool searching() const noexcept;
  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] bool pairing() const noexcept;
  [[nodiscard]] bool remoteInputActive() const noexcept;
  [[nodiscard]] QString deviceLabel() const;
  [[nodiscard]] QString broadcastStatus() const;
  [[nodiscard]] QStringList hosts() const;
  [[nodiscard]] QString pairingCode() const;
  [[nodiscard]] QString selectedDeviceLabel() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] bool videoReady() const noexcept;
  [[nodiscard]] bool audioReady() const noexcept;
  [[nodiscard]] bool inputReady() const noexcept;
  [[nodiscard]] bool networkReady() const noexcept;
  [[nodiscard]] QString videoDetail() const;
  [[nodiscard]] QString audioDetail() const;
  [[nodiscard]] QString inputDetail() const;
  [[nodiscard]] QString networkDetail() const;
  [[nodiscard]] bool permissionActionAvailable() const noexcept;
  [[nodiscard]] QObject* videoSurface() const noexcept;

  Q_INVOKABLE void startBroadcast();
  Q_INVOKABLE void stopBroadcast();
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void findDevices();
  Q_INVOKABLE void connectToDevice(int index);
  Q_INVOKABLE void confirmPairing();
  Q_INVOKABLE void cancelPairing();
  Q_INVOKABLE void toggleRemoteInput();
  Q_INVOKABLE void releaseRemoteInput();
  Q_INVOKABLE void routeKey(int key, bool pressed);
  Q_INVOKABLE void routeMouseMove(int dx, int dy);
  Q_INVOKABLE void routeMouseButton(int button, bool pressed);
  Q_INVOKABLE void routeMouseWheel(int delta);
  Q_INVOKABLE void disconnect();
  Q_INVOKABLE void openPermissionSettings();

 signals:
  void modeChanged();
  void stateChanged();

 private:
  void tick();
  void cleanupCurrentMode();
  void refreshCapabilities();

  RoleMode mode_{RoleMode::Remote};
  ControlledCapabilities controlled_capabilities_;
  RemoteCapabilities remote_capabilities_;
  QString failure_text_;
  QTimer tick_timer_;
#if defined(_WIN32) || defined(__APPLE__)
  // The object is a small Qt-facing adapter around the platform's latest
  // native texture. It is kept before the runtimes so it outlives them.
  std::unique_ptr<QObject> video_surface_;
  std::unique_ptr<ControlledRuntime> controlled_;
  std::unique_ptr<RemoteRuntime> remote_;
#endif
};

}  // namespace ministream
