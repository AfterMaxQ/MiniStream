#include "app/ui/role_controller.hpp"

#ifdef _WIN32
#include "windows/ui/host_controller.hpp"
#endif
#ifdef __APPLE__
#include "macos/ui/client_controller.hpp"
#endif

#include <QSysInfo>

namespace ministream {

RoleController::RoleController(QObject* parent) : QObject(parent) {
#ifdef _WIN32
  host_ = std::make_unique<HostController>();
  connect(host_.get(), &HostController::capabilitiesChanged, this,
          &RoleController::stateChanged);
  connect(host_.get(), &HostController::hostingChanged, this,
          &RoleController::stateChanged);
  connect(host_.get(), &HostController::pairingChanged, this,
          &RoleController::stateChanged);
#endif
#ifdef __APPLE__
  client_ = std::make_unique<ClientController>();
  connect(client_.get(), &ClientController::hostsChanged, this, &RoleController::stateChanged);
  connect(client_.get(), &ClientController::searchingChanged, this,
          &RoleController::stateChanged);
  connect(client_.get(), &ClientController::connectedChanged, this,
          &RoleController::stateChanged);
  connect(client_.get(), &ClientController::pairingChanged, this,
          &RoleController::stateChanged);
  connect(client_.get(), &ClientController::remoteInputChanged, this,
          &RoleController::stateChanged);
#endif
}

RoleController::~RoleController() { cleanupCurrentMode(); }

int RoleController::mode() const noexcept { return static_cast<int>(mode_); }

void RoleController::setMode(int mode) {
  const auto requested = static_cast<RoleMode>(mode);
  if (requested != RoleMode::Controlled && requested != RoleMode::Remote) {
    return;
  }
  if (requested == mode_) {
    return;
  }
  cleanupCurrentMode();
  mode_ = requested;
  emit modeChanged();
  emit stateChanged();
}

bool RoleController::controlledAvailable() const noexcept {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

bool RoleController::remoteAvailable() const noexcept {
#ifdef __APPLE__
  return true;
#else
  return false;
#endif
}

bool RoleController::ready() const noexcept {
  if (mode_ == RoleMode::Controlled) {
#ifdef _WIN32
    return host_ && host_->ready();
#else
    return false;
#endif
  }
#ifdef __APPLE__
  return client_ != nullptr;
#else
  return false;
#endif
}

bool RoleController::broadcasting() const noexcept {
#ifdef _WIN32
  return host_ && host_->hosting();
#else
  return false;
#endif
}

bool RoleController::searching() const noexcept {
#ifdef __APPLE__
  return client_ && client_->searching();
#else
  return false;
#endif
}

bool RoleController::connected() const noexcept {
#ifdef __APPLE__
  return client_ && client_->connected();
#else
  return false;
#endif
}

bool RoleController::pairing() const noexcept {
#ifdef _WIN32
  if (host_ && host_->pairing()) {
    return true;
  }
#endif
#ifdef __APPLE__
  if (client_ && client_->pairing()) {
    return true;
  }
#endif
  return false;
}

bool RoleController::remoteInputActive() const noexcept {
#ifdef __APPLE__
  return client_ && client_->remoteInputActive();
#else
  return false;
#endif
}

QString RoleController::deviceLabel() const {
#ifdef _WIN32
  return host_ ? host_->deviceLabel() : QStringLiteral("This device");
#else
  const auto name = QSysInfo::machineHostName().trimmed();
  const auto device = name.isEmpty() ? QStringLiteral("This device") : name;
  return QStringLiteral("%1 · %2").arg(QSysInfo::prettyProductName(), device);
#endif
}

QString RoleController::broadcastStatus() const {
#ifdef _WIN32
  return host_ ? host_->broadcastStatus() : QStringLiteral("Not visible on local network");
#else
  return QStringLiteral("Controlled mode is not available on this build");
#endif
}

QStringList RoleController::hosts() const {
#ifdef __APPLE__
  return client_ ? client_->hosts() : QStringList{};
#else
  return {};
#endif
}

QString RoleController::pairingCode() const {
#ifdef _WIN32
  if (host_ && host_->pairing()) {
    return host_->pairingCode();
  }
#endif
#ifdef __APPLE__
  if (client_) {
    return client_->pairingCode();
  }
#endif
  return {};
}

QString RoleController::selectedDeviceLabel() const {
#ifdef __APPLE__
  return client_ ? client_->selectedDeviceLabel() : QString{};
#else
  return {};
#endif
}

QString RoleController::statusText() const {
  if (mode_ == RoleMode::Controlled) {
    return controlledAvailable() ? broadcastStatus()
                                 : QStringLiteral("Controlled mode is not available on this build");
  }
  if (!remoteAvailable()) {
    return QStringLiteral("Remote control is not available on this build");
  }
  if (pairing()) {
    return QStringLiteral("Confirm the same code on both devices.");
  }
  if (connected()) {
    return QStringLiteral("Connected");
  }
  return QStringLiteral("Find a device on the local network");
}

bool RoleController::videoReady() const noexcept {
#ifdef _WIN32
  return host_ && host_->videoReady();
#else
  return false;
#endif
}

bool RoleController::audioReady() const noexcept {
#ifdef _WIN32
  return host_ && host_->audioReady();
#else
  return false;
#endif
}

bool RoleController::inputReady() const noexcept {
#ifdef _WIN32
  return host_ && host_->inputReady();
#else
  return false;
#endif
}

bool RoleController::networkReady() const noexcept {
#ifdef _WIN32
  return host_ && host_->networkReady();
#else
  return false;
#endif
}

QString RoleController::videoDetail() const {
#ifdef _WIN32
  return host_ ? host_->videoDetail() : QString{};
#else
  return QStringLiteral("Remote video backend ready on supported systems");
#endif
}

QString RoleController::audioDetail() const {
#ifdef _WIN32
  return host_ ? host_->audioDetail() : QString{};
#else
  return QStringLiteral("Remote audio backend ready on supported systems");
#endif
}

QString RoleController::inputDetail() const {
#ifdef _WIN32
  return host_ ? host_->inputDetail() : QString{};
#else
  return QStringLiteral("Window-local keyboard and mouse");
#endif
}

QString RoleController::networkDetail() const {
#ifdef _WIN32
  return host_ ? host_->networkDetail() : QString{};
#else
  return QStringLiteral("UDP available");
#endif
}

void RoleController::startBroadcast() {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled && host_) {
    host_->startHost();
    emit stateChanged();
  }
#endif
}

void RoleController::stopBroadcast() {
#ifdef _WIN32
  if (host_) {
    host_->stopHost();
    emit stateChanged();
  }
#endif
}

void RoleController::refresh() {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled && host_) {
    host_->refresh();
  }
#endif
#ifdef __APPLE__
  if (mode_ == RoleMode::Remote && client_) {
    client_->refreshHosts();
  }
#endif
  emit stateChanged();
}

void RoleController::findDevices() {
#ifdef __APPLE__
  if (mode_ == RoleMode::Remote && client_) {
    client_->refreshHosts();
    emit stateChanged();
  }
#endif
}

void RoleController::connectToDevice(int index) {
#ifdef __APPLE__
  if (mode_ == RoleMode::Remote && client_) {
    client_->connectToHost(index);
    emit stateChanged();
  }
#else
  Q_UNUSED(index);
#endif
}

void RoleController::confirmPairing() {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled && host_) {
    host_->confirmPairing();
  }
#endif
#ifdef __APPLE__
  if (mode_ == RoleMode::Remote && client_) {
    client_->confirmPairing();
  }
#endif
  emit stateChanged();
}

void RoleController::cancelPairing() {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled && host_) {
    host_->cancelPairing();
  }
#endif
#ifdef __APPLE__
  if (mode_ == RoleMode::Remote && client_) {
    client_->cancelPairing();
  }
#endif
  emit stateChanged();
}

void RoleController::toggleRemoteInput() {
#ifdef __APPLE__
  if (mode_ == RoleMode::Remote && client_) {
    client_->toggleRemoteInput();
    emit stateChanged();
  }
#endif
}

void RoleController::releaseRemoteInput() {
#ifdef __APPLE__
  if (client_) {
    client_->releaseRemoteInput();
    emit stateChanged();
  }
#endif
}

void RoleController::disconnect() {
  cleanupCurrentMode();
  emit stateChanged();
}

void RoleController::cleanupCurrentMode() {
  releaseRemoteInput();
#ifdef _WIN32
  if (host_) {
    host_->stopHost();
  }
#endif
#ifdef __APPLE__
  if (client_ && (client_->connected() || client_->pairing())) {
    client_->cancelPairing();
  }
#endif
}

}  // namespace ministream
