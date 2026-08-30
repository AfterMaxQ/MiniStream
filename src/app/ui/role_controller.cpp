#include "app/ui/role_controller.hpp"

#include "app/controlled/controlled_runtime.hpp"
#include "app/remote/remote_runtime.hpp"
#include "core/config/stream_profile.hpp"
#ifdef _WIN32
#include "windows/platform/controlled_backend.hpp"
#include "windows/platform/remote_backend.hpp"
#endif
#ifdef __APPLE__
#include "macos/platform/controlled_backend.hpp"
#include "macos/platform/remote_backend.hpp"
#endif

#include <QSysInfo>

#include <algorithm>
#include <string>

namespace ministream {
namespace {

DiscoverySystem current_system() {
  const auto product = QSysInfo::productType().toLower();
  if (product == QStringLiteral("windows")) {
    return DiscoverySystem::Windows;
  }
  if (product == QStringLiteral("osx") || product == QStringLiteral("macos")) {
    return DiscoverySystem::MacOS;
  }
  if (product == QStringLiteral("linux")) {
    return DiscoverySystem::Linux;
  }
  return DiscoverySystem::Unknown;
}

std::string current_device_name() {
  const auto name = QSysInfo::machineHostName().trimmed();
  const auto value = name.isEmpty() ? QStringLiteral("This device") : name;
  auto result = value.toUtf8().toStdString();
  if (result.size() > kMaxDiscoveryNameBytes) {
    result.resize(kMaxDiscoveryNameBytes);
  }
  return result;
}

DiscoveryAdvertisement controlled_advertisement(const ControlledCapabilities& capabilities) {
  const auto profile = stream_profile(StreamProfileId::Debug1080);
  return {current_system(), current_device_name(), 0,
          DiscoveryCapabilities{capabilities.video.ready,
                                capabilities.video.ready,
                                false,
                                capabilities.audio.ready,
                                capabilities.input.ready,
                                capabilities.optional_gamepad.ready},
          static_cast<std::uint16_t>(profile.width),
          static_cast<std::uint16_t>(profile.height),
          static_cast<std::uint16_t>(profile.fps),
          false};
}

QString capability_text(const PlatformCapability& capability) {
  return QString::fromStdString(capability.detail);
}

QString first_failure(const ControlledCapabilities& capabilities) {
  for (const auto* capability : {&capabilities.video, &capabilities.audio,
                                 &capabilities.input, &capabilities.network}) {
    if (!capability->ready && !capability->detail.empty()) {
      return capability_text(*capability);
    }
  }
  return {};
}

QString first_failure(const RemoteCapabilities& capabilities) {
  for (const auto* capability : {&capabilities.video, &capabilities.audio,
                                 &capabilities.input, &capabilities.network}) {
    if (!capability->ready && !capability->detail.empty()) {
      return capability_text(*capability);
    }
  }
  return {};
}

QString first_line(const std::string& card) {
  const auto end = card.find('\n');
  return QString::fromStdString(card.substr(0, end));
}

}  // namespace

RoleController::RoleController(QObject* parent) : QObject(parent) {
#ifdef _WIN32
  auto controlled_backend = std::make_unique<WindowsControlledBackend>();
  controlled_capabilities_ = controlled_backend->inspect();
  controlled_ = std::make_unique<ControlledRuntime>(
      std::move(controlled_backend), controlled_advertisement(controlled_capabilities_));

  auto remote_backend = std::make_unique<WindowsRemoteBackend>();
  remote_capabilities_ = remote_backend->inspect();
  remote_ = std::make_unique<RemoteRuntime>(std::move(remote_backend));
  mode_ = RoleMode::Controlled;
#endif
#ifdef __APPLE__
  auto controlled_backend = std::make_unique<MacControlledBackend>();
  controlled_capabilities_ = controlled_backend->inspect();
  controlled_ = std::make_unique<ControlledRuntime>(
      std::move(controlled_backend), controlled_advertisement(controlled_capabilities_));
  auto remote_backend = std::make_unique<MacRemoteBackend>();
  remote_capabilities_ = remote_backend->inspect();
  remote_ = std::make_unique<RemoteRuntime>(std::move(remote_backend));
  mode_ = RoleMode::Remote;
#endif

  tick_timer_.setInterval(10);
  connect(&tick_timer_, &QTimer::timeout, this, &RoleController::tick);
  tick_timer_.start();
}

RoleController::~RoleController() {
  cleanupCurrentMode();
#if defined(_WIN32) || defined(__APPLE__)
  if (controlled_) {
    controlled_->stop();
  }
  if (remote_) {
    remote_->stop();
  }
#endif
}

int RoleController::mode() const noexcept { return static_cast<int>(mode_); }

void RoleController::setMode(int mode) {
  const auto requested = static_cast<RoleMode>(mode);
  if ((requested != RoleMode::Controlled && requested != RoleMode::Remote) ||
      requested == mode_ ||
      (requested == RoleMode::Controlled && !controlledAvailable()) ||
      (requested == RoleMode::Remote && !remoteAvailable())) {
    return;
  }
  cleanupCurrentMode();
  failure_text_.clear();
  mode_ = requested;
  emit modeChanged();
  emit stateChanged();
}

bool RoleController::controlledAvailable() const noexcept {
#if defined(_WIN32) || defined(__APPLE__)
  return controlled_ != nullptr;
#else
  return false;
#endif
}

bool RoleController::remoteAvailable() const noexcept {
#if defined(_WIN32) || defined(__APPLE__)
  return remote_ != nullptr;
#else
  return false;
#endif
}

bool RoleController::ready() const noexcept {
  if (mode_ == RoleMode::Controlled) {
    return controlled_capabilities_.ready();
  }
#ifdef _WIN32
  return remote_capabilities_.ready();
#elif defined(__APPLE__)
  return remote_capabilities_.ready();
#else
  return false;
#endif
}

bool RoleController::broadcasting() const noexcept {
#if defined(_WIN32) || defined(__APPLE__)
  return controlled_ && controlled_->hosting();
#else
  return false;
#endif
}

bool RoleController::searching() const noexcept {
#if defined(_WIN32) || defined(__APPLE__)
  return false;
#else
  return false;
#endif
}

bool RoleController::connected() const noexcept {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled) {
    return controlled_ && (controlled_->pairing() || controlled_->streaming());
  }
  return remote_ && remote_->connected();
#elif defined(__APPLE__)
  return remote_ && remote_->connected();
#else
  return false;
#endif
}

bool RoleController::pairing() const noexcept {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled) {
    return controlled_ && controlled_->pairing();
  }
  return remote_ && remote_->pairing();
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Controlled) {
    return controlled_ && controlled_->pairing();
  }
  return remote_ && remote_->pairing();
#else
  return false;
#endif
}

bool RoleController::remoteInputActive() const noexcept {
#ifdef _WIN32
  return remote_ && remote_->remote_input_active();
#elif defined(__APPLE__)
  return remote_ && remote_->remote_input_active();
#else
  return false;
#endif
}

QString RoleController::deviceLabel() const {
  QString system;
  switch (current_system()) {
    case DiscoverySystem::Windows:
      system = QStringLiteral("Windows");
      break;
    case DiscoverySystem::MacOS:
      system = QStringLiteral("macOS");
      break;
    case DiscoverySystem::Linux:
      system = QStringLiteral("Linux");
      break;
    case DiscoverySystem::Unknown:
      system = QSysInfo::productType().isEmpty() ? QStringLiteral("Unknown")
                                                 : QSysInfo::productType();
      break;
  }
  return QStringLiteral("%1 · %2").arg(system, QString::fromStdString(current_device_name()));
}

QString RoleController::broadcastStatus() const {
#if defined(_WIN32) || defined(__APPLE__)
  return broadcasting() ? QStringLiteral("Visible on local network")
                        : QStringLiteral("Not visible on local network");
#else
  return QStringLiteral("Controlled mode is not available on this build");
#endif
}

QStringList RoleController::hosts() const {
#ifdef _WIN32
  QStringList result;
  if (remote_) {
    for (const auto& host : remote_->hosts()) {
      if (host.controllable) {
        result.push_back(QString::fromStdString(format_discovered_host(host)));
      }
    }
  }
  return result;
#elif defined(__APPLE__)
  QStringList result;
  if (remote_) {
    for (const auto& host : remote_->hosts()) {
      if (host.controllable) {
        result.push_back(QString::fromStdString(format_discovered_host(host)));
      }
    }
  }
  return result;
#else
  return {};
#endif
}

QString RoleController::pairingCode() const {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled) {
    return controlled_ ? QString::fromStdString(controlled_->pairing_code()) : QString{};
  }
  return remote_ ? QString::fromStdString(remote_->pairing_code()) : QString{};
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Controlled) {
    return controlled_ ? QString::fromStdString(controlled_->pairing_code()) : QString{};
  }
  return remote_ ? QString::fromStdString(remote_->pairing_code()) : QString{};
#else
  return {};
#endif
}

QString RoleController::selectedDeviceLabel() const {
#ifdef _WIN32
  if (remote_ && remote_->selected_host()) {
    return first_line(format_discovered_host(*remote_->selected_host()));
  }
  return {};
#elif defined(__APPLE__)
  if (remote_ && remote_->selected_host()) {
    return first_line(format_discovered_host(*remote_->selected_host()));
  }
  return {};
#else
  return {};
#endif
}

QString RoleController::statusText() const {
  if (!failure_text_.isEmpty()) {
    return failure_text_;
  }
  if (mode_ == RoleMode::Controlled) {
    if (!controlledAvailable()) {
      return QStringLiteral("Controlled mode is not available on this build");
    }
    if (!ready()) {
      return first_failure(controlled_capabilities_);
    }
    return broadcastStatus();
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
  if (!ready()) {
    return first_failure(remote_capabilities_);
  }
  return QStringLiteral("Find a device on the local network");
}

bool RoleController::videoReady() const noexcept {
  return mode_ == RoleMode::Controlled ? controlled_capabilities_.video.ready
                                        : remote_capabilities_.video.ready;
}
bool RoleController::audioReady() const noexcept {
  return mode_ == RoleMode::Controlled ? controlled_capabilities_.audio.ready
                                        : remote_capabilities_.audio.ready;
}
bool RoleController::inputReady() const noexcept {
  return mode_ == RoleMode::Controlled ? controlled_capabilities_.input.ready
                                        : remote_capabilities_.input.ready;
}
bool RoleController::networkReady() const noexcept {
  return mode_ == RoleMode::Controlled ? controlled_capabilities_.network.ready
                                        : remote_capabilities_.network.ready;
}

QString RoleController::videoDetail() const {
  return capability_text(mode_ == RoleMode::Controlled ? controlled_capabilities_.video
                                                        : remote_capabilities_.video);
}
QString RoleController::audioDetail() const {
  return capability_text(mode_ == RoleMode::Controlled ? controlled_capabilities_.audio
                                                        : remote_capabilities_.audio);
}
QString RoleController::inputDetail() const {
  return capability_text(mode_ == RoleMode::Controlled ? controlled_capabilities_.input
                                                        : remote_capabilities_.input);
}
QString RoleController::networkDetail() const {
  return capability_text(mode_ == RoleMode::Controlled ? controlled_capabilities_.network
                                                        : remote_capabilities_.network);
}

void RoleController::startBroadcast() {
#if defined(_WIN32) || defined(__APPLE__)
  if (mode_ != RoleMode::Controlled || !controlled_) {
    return;
  }
  if (!controlled_->start()) {
    failure_text_ = QStringLiteral("Unable to start control broadcast.");
  } else {
    failure_text_.clear();
  }
  emit stateChanged();
#endif
}

void RoleController::stopBroadcast() {
#if defined(_WIN32) || defined(__APPLE__)
  if (controlled_) {
    controlled_->stop();
    failure_text_.clear();
    emit stateChanged();
  }
#endif
}

void RoleController::refreshCapabilities() {
#if defined(_WIN32) || defined(__APPLE__)
  if (controlled_) {
    controlled_capabilities_ = controlled_->inspect();
  }
  if (remote_) {
    remote_capabilities_ = remote_->inspect();
  }
#endif
}

void RoleController::refresh() {
  refreshCapabilities();
#ifdef _WIN32
  if (mode_ == RoleMode::Remote && remote_) {
    if (remote_->state() == RoleState::Idle) {
      (void)remote_->start();
    }
    remote_->refresh(Microseconds{250});
  }
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    if (remote_->state() == RoleState::Idle) {
      (void)remote_->start();
    }
    (void)remote_->refresh(Microseconds{250});
  }
#endif
  failure_text_.clear();
  emit stateChanged();
}

void RoleController::findDevices() {
#ifdef _WIN32
  if (mode_ == RoleMode::Remote && remote_) {
    if (remote_->state() == RoleState::Idle && !remote_->start()) {
      failure_text_ = QStringLiteral("Remote backend is not ready.");
    } else if (!remote_->refresh(Microseconds{250})) {
      failure_text_ = QStringLiteral("No devices found on the local network.");
    } else {
      failure_text_.clear();
    }
    emit stateChanged();
  }
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    if (remote_->state() == RoleState::Idle && !remote_->start()) {
      failure_text_ = QStringLiteral("Remote backend is not ready.");
    } else if (!remote_->refresh(Microseconds{250})) {
      failure_text_ = QStringLiteral("No devices found on the local network.");
    } else {
      failure_text_.clear();
    }
    emit stateChanged();
  }
#endif
}

void RoleController::connectToDevice(int index) {
#ifdef _WIN32
  if (mode_ == RoleMode::Remote && remote_) {
    if (!remote_->connect(static_cast<std::size_t>(std::max(index, 0)))) {
      failure_text_ = QStringLiteral("Unable to connect to this device.");
    } else {
      failure_text_.clear();
    }
    emit stateChanged();
  }
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    if (!remote_->connect(static_cast<std::size_t>(std::max(index, 0)))) {
      failure_text_ = QStringLiteral("Unable to connect to this device.");
    } else {
      failure_text_.clear();
    }
    emit stateChanged();
  }
#else
  Q_UNUSED(index);
#endif
}

void RoleController::confirmPairing() {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled && controlled_) {
    controlled_->confirm_pairing();
  } else if (mode_ == RoleMode::Remote && remote_) {
    remote_->confirm_pairing();
  }
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Controlled && controlled_) {
    controlled_->confirm_pairing();
  } else if (mode_ == RoleMode::Remote && remote_) {
    remote_->confirm_pairing();
  }
#endif
  emit stateChanged();
}

void RoleController::cancelPairing() {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled && controlled_) {
    controlled_->cancel_pairing();
  } else if (mode_ == RoleMode::Remote && remote_) {
    remote_->cancel_pairing();
  }
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Controlled && controlled_) {
    controlled_->cancel_pairing();
  } else if (mode_ == RoleMode::Remote && remote_) {
    remote_->cancel_pairing();
  }
#endif
  emit stateChanged();
}

void RoleController::toggleRemoteInput() {
#ifdef _WIN32
  if (mode_ == RoleMode::Remote && remote_) {
    remote_->toggle_input();
  }
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    remote_->toggle_input();
  }
#endif
  emit stateChanged();
}

void RoleController::releaseRemoteInput() {
#ifdef _WIN32
  if (remote_) {
    remote_->release_input();
  }
#elif defined(__APPLE__)
  if (remote_) {
    remote_->release_input();
  }
#endif
  emit stateChanged();
}

void RoleController::disconnect() {
  cleanupCurrentMode();
  failure_text_.clear();
  emit stateChanged();
}

void RoleController::tick() {
#if defined(_WIN32) || defined(__APPLE__)
  if (controlled_ && mode_ == RoleMode::Controlled) {
    controlled_->tick();
  } else if (remote_ && mode_ == RoleMode::Remote) {
    remote_->tick();
  }
#endif
}

void RoleController::cleanupCurrentMode() {
  releaseRemoteInput();
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled) {
    if (controlled_) {
      controlled_->stop();
    }
  } else if (remote_) {
    remote_->stop();
  }
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Controlled) {
    if (controlled_) controlled_->stop();
  } else if (remote_) {
    remote_->stop();
  }
#endif
}

}  // namespace ministream
