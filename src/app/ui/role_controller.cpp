#include "app/ui/role_controller.hpp"

#include "app/controlled/controlled_runtime.hpp"
#include "app/remote/remote_runtime.hpp"
#include "core/config/stream_profile.hpp"
#ifdef _WIN32
#include "windows/platform/controlled_backend.hpp"
#include "windows/platform/remote_backend.hpp"
#include "windows/input/window_input_source.hpp"
#include "app/ui/windows_video_surface_bridge.hpp"
#endif
#ifdef __APPLE__
#include "macos/platform/controlled_backend.hpp"
#include "macos/platform/remote_backend.hpp"
#include "macos/input/accessibility_input.hpp"
#include "macos/video/video_surface_bridge.hpp"
#endif

#include <QSysInfo>
#include <QDesktopServices>
#include <QUrl>

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
          DiscoveryCapabilities{capabilities.h264,
                                capabilities.hevc,
                                capabilities.hdr10,
                                capabilities.audio.ready,
                                capabilities.input.ready,
                                capabilities.optional_gamepad.ready},
          static_cast<std::uint16_t>(capabilities.max_width != 0
                                         ? capabilities.max_width
                                         : profile.width),
          static_cast<std::uint16_t>(capabilities.max_height != 0
                                         ? capabilities.max_height
                                         : profile.height),
          static_cast<std::uint16_t>(capabilities.max_fps != 0 ? capabilities.max_fps
                                                               : profile.fps),
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
  auto* remote_backend_ptr = remote_backend.get();
  remote_capabilities_ = remote_backend->inspect();
  remote_ = std::make_unique<RemoteRuntime>(std::move(remote_backend));
  video_surface_ = std::make_unique<WindowsVideoSurfaceBridge>(remote_backend_ptr);
  mode_ = RoleMode::Controlled;
#endif
#ifdef __APPLE__
  video_surface_ = std::make_unique<VideoSurfaceBridge>();
  auto controlled_backend = std::make_unique<MacControlledBackend>();
  controlled_capabilities_ = controlled_backend->inspect();
  controlled_ = std::make_unique<ControlledRuntime>(
      std::move(controlled_backend), controlled_advertisement(controlled_capabilities_));
  auto* video_surface = static_cast<VideoSurfaceBridge*>(video_surface_.get());
  auto remote_backend = std::make_unique<MacRemoteBackend>(video_surface);
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
#ifdef _WIN32
  // The adapter's destructor detaches a callback from the still-live backend.
  video_surface_.reset();
#endif
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
  return mode_ == RoleMode::Remote && remote_ &&
         remote_->discovery_state() == DiscoveryState::Searching;
#else
  return false;
#endif
}

bool RoleController::connecting() const noexcept {
#if defined(_WIN32) || defined(__APPLE__)
  return mode_ == RoleMode::Remote && remote_ &&
         remote_->state() == RoleState::RemoteConnecting;
#else
  return false;
#endif
}

bool RoleController::connected() const noexcept {
#ifdef _WIN32
  if (mode_ == RoleMode::Controlled) {
    return controlled_ && controlled_->streaming();
  }
  return remote_ && remote_->streaming();
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Controlled) {
    return controlled_ && controlled_->streaming();
  }
  return remote_ && remote_->streaming();
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
  if (controlled_ && controlled_->last_discovery_error()) {
    switch (*controlled_->last_discovery_error()) {
      case DiscoveryError::PermissionDenied:
        return QStringLiteral("Local network access is blocked in system settings");
      case DiscoveryError::NoUsableInterface:
        return QStringLiteral("No usable network interface is available");
      case DiscoveryError::Bind:
      case DiscoveryError::Send:
      case DiscoveryError::Receive:
        return QStringLiteral("Local network discovery is unavailable");
    }
  }
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
  if (searching()) {
    return QStringLiteral("Searching local network");
  }
  if (remote_->state() == RoleState::RemoteConnecting) {
    const auto label = selectedDeviceLabel();
    return label.isEmpty() ? QStringLiteral("Connecting to device")
                           : QStringLiteral("Connecting to %1").arg(label);
  }
  if (pairing()) {
    return QStringLiteral("Confirm the same code on both devices.");
  }
  if (connected()) {
    return QStringLiteral("Connected");
  }
  if (remote_->discovery_state() == DiscoveryState::Failed &&
      remote_->last_discovery_error()) {
    switch (*remote_->last_discovery_error()) {
      case DiscoveryError::PermissionDenied:
        return QStringLiteral(
            "Local network access is blocked. Allow MiniStream in system settings.");
      case DiscoveryError::NoUsableInterface:
        return QStringLiteral("No usable network interface is available.");
      case DiscoveryError::Bind:
      case DiscoveryError::Send:
      case DiscoveryError::Receive:
        return QStringLiteral("Local network discovery is unavailable. Check firewall settings.");
    }
  }
  if (remote_->discovery_state() == DiscoveryState::Complete && remote_->hosts().empty()) {
    return QStringLiteral("No devices found on the local network.");
  }
  if (remote_->discovery_state() == DiscoveryState::Complete) {
    return QStringLiteral("Select a device to connect.");
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

bool RoleController::permissionActionAvailable() const noexcept {
#ifdef __APPLE__
  if (mode_ != RoleMode::Controlled) {
    return false;
  }
  const auto has_permission_detail = [](const PlatformCapability& capability) {
    return capability.detail.find("permission required") != std::string::npos;
  };
  return has_permission_detail(controlled_capabilities_.video) ||
         has_permission_detail(controlled_capabilities_.input);
#else
  return false;
#endif
}

QObject* RoleController::videoSurface() const noexcept {
#if defined(_WIN32) || defined(__APPLE__)
  return video_surface_.get();
#else
  return nullptr;
#endif
}

void RoleController::startBroadcast() {
#if defined(_WIN32) || defined(__APPLE__)
  if (mode_ != RoleMode::Controlled || !controlled_) {
    return;
  }
  if (!controlled_->start()) {
    controlled_capabilities_ = controlled_->inspect();
    failure_text_ = controlled_capabilities_.video.detail.empty()
                        ? QStringLiteral("Unable to start control broadcast.")
                        : QString::fromStdString(controlled_capabilities_.video.detail);
  } else {
    controlled_capabilities_ = controlled_->inspect();
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
    if (controlled_->state() == RoleState::Idle) {
      controlled_->set_advertisement(controlled_advertisement(controlled_capabilities_));
    }
  }
  if (remote_) {
    remote_capabilities_ = remote_->inspect();
  }
#endif
}

void RoleController::refresh() {
  refreshCapabilities();
#if defined(_WIN32) || defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    if (remote_->state() == RoleState::Idle) {
      (void)remote_->start();
    }
    (void)remote_->begin_discovery();
  }
#endif
  failure_text_.clear();
  emit stateChanged();
}

void RoleController::findDevices() {
#if defined(_WIN32) || defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    if (remote_->state() == RoleState::Idle && !remote_->start()) {
      failure_text_ = QStringLiteral("Remote backend is not ready.");
    } else if (!remote_->begin_discovery() && !searching()) {
      failure_text_.clear();
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

void RoleController::routeKey(int key, bool pressed) {
#ifdef _WIN32
  if (remote_ && remoteInputActive()) {
    if (const auto input = WindowInputSource::key(static_cast<std::uint32_t>(key), pressed)) {
      remote_->route_input(*input);
    }
  }
#elif defined(__APPLE__)
  if (remote_ && remoteInputActive()) {
    if (const auto input = AccessibilityInput::key_from_qt(static_cast<std::uint32_t>(key),
                                                            pressed)) {
      remote_->route_input(*input);
    }
  }
#else
  Q_UNUSED(key);
  Q_UNUSED(pressed);
#endif
}

void RoleController::routeMouseMove(int dx, int dy) {
#ifdef _WIN32
  if (remote_ && remoteInputActive()) {
    if (const auto input = WindowInputSource::mouse_move(dx, dy)) {
      remote_->route_input(*input);
    }
  }
#elif defined(__APPLE__)
  if (remote_ && remoteInputActive()) {
    remote_->route_input({DesktopInputKind::MouseMove, 0, dx, dy, 0});
  }
#else
  Q_UNUSED(dx);
  Q_UNUSED(dy);
#endif
}

void RoleController::routeMouseButton(int button, bool pressed) {
#ifdef _WIN32
  if (remote_ && remoteInputActive()) {
    if (const auto input = WindowInputSource::mouse_button(static_cast<std::uint32_t>(button),
                                                           pressed)) {
      remote_->route_input(*input);
    }
  }
#elif defined(__APPLE__)
  if (remote_ && remoteInputActive()) {
    if (const auto input = AccessibilityInput::mouse_button_from_qt(
            static_cast<std::uint32_t>(button), pressed)) {
      remote_->route_input(*input);
    }
  }
#else
  Q_UNUSED(button);
  Q_UNUSED(pressed);
#endif
}

void RoleController::routeMouseWheel(int delta) {
#ifdef _WIN32
  if (remote_ && remoteInputActive()) {
    if (const auto input = WindowInputSource::mouse_wheel(delta)) {
      remote_->route_input(*input);
    }
  }
#elif defined(__APPLE__)
  if (remote_ && remoteInputActive()) {
    remote_->route_input({DesktopInputKind::MouseWheel, 0, 0, delta, 0});
  }
#else
  Q_UNUSED(delta);
#endif
}

void RoleController::disconnect() {
  cleanupCurrentMode();
  failure_text_.clear();
  emit stateChanged();
}

void RoleController::openPermissionSettings() {
#ifdef __APPLE__
  const auto video_needs_access = controlled_capabilities_.video.detail.find(
      "permission required") != std::string::npos;
  const auto url = video_needs_access
                       ? QUrl(QStringLiteral(
                             "x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"))
                       : QUrl(QStringLiteral(
                             "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"));
  QDesktopServices::openUrl(url);
#endif
}

void RoleController::tick() {
#if defined(_WIN32) || defined(__APPLE__)
  if (controlled_ && mode_ == RoleMode::Controlled) {
    const auto before_state = controlled_->state();
    const auto before_discovery_error = controlled_->last_discovery_error();
    controlled_->tick();
    const auto after_state = controlled_->state();
    if (before_state != after_state ||
        before_discovery_error != controlled_->last_discovery_error()) {
      emit stateChanged();
    }
  } else if (remote_ && mode_ == RoleMode::Remote) {
    const auto before_state = remote_->state();
    const auto before_discovery = remote_->discovery_state();
    const auto before_hosts = remote_->hosts().size();
    const auto before_pairing_code = remote_->pairing_code();
    const auto before_video_status = remote_->video_status();
    const auto before_input = remote_->remote_input_active();
    remote_->tick();
    const auto after_state = remote_->state();
    if (before_state != after_state || before_discovery != remote_->discovery_state() ||
        before_hosts != remote_->hosts().size() ||
        before_pairing_code != remote_->pairing_code() ||
        before_video_status != remote_->video_status() ||
        before_input != remote_->remote_input_active()) {
      emit stateChanged();
    }
  }
#endif
}

QString RoleController::videoStatus() const {
#if defined(_WIN32) || defined(__APPLE__)
  if (remote_) return QString::fromStdString(remote_->video_status());
#endif
  return QStringLiteral("Waiting for video");
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
