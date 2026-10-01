#include "app/ui/role_controller.hpp"
#include "app/ui/control_escape_filter.hpp"
#include "app/ui/clipboard_bridge.hpp"
#include "app/ui/game_keyboard_filter.hpp"

#include "app/controlled/controlled_runtime.hpp"
#include "app/remote/remote_runtime.hpp"
#include "core/config/stream_profile.hpp"
#ifdef _WIN32
#include "windows/platform/controlled_backend.hpp"
#include "windows/platform/remote_backend.hpp"
#include "windows/input/window_input_source.hpp"
#include "windows/input/desktop_key_windows.hpp"
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
#include <QGuiApplication>
#include <QSettings>

#include <algorithm>
#include <cmath>
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

std::shared_ptr<PairingTrust> load_pairing_trust() {
  auto settings = std::make_shared<QSettings>(QSettings::NativeFormat, QSettings::UserScope,
                                             QStringLiteral("AfterMaxQ"), QStringLiteral("MiniStream"));
  const auto saved_seed = settings->value(QStringLiteral("pairing/identitySeed")).toByteArray();
  auto identity = saved_seed.isEmpty() ? generate_identity()
      : identity_from_seed({reinterpret_cast<const std::byte*>(saved_seed.constData()),
                            static_cast<std::size_t>(saved_seed.size())});
  if (!identity) return {};
  if (saved_seed.isEmpty()) {
    settings->setValue(QStringLiteral("pairing/identitySeed"),
        QByteArray(reinterpret_cast<const char*>(identity->secret_key.data()), 32));
    settings->sync();
    if (settings->status() != QSettings::NoError) return {};
  }
  std::vector<PairingTrust::PublicKey> peers;
  const auto saved_peers = settings->value(QStringLiteral("pairing/trustedDevices")).toStringList();
  for (const auto& value : saved_peers) {
    const auto bytes = QByteArray::fromHex(value.toLatin1());
    if (value.size() != 64 || bytes.size() != 32 || peers.size() >= 256) continue;
    PairingTrust::PublicKey key{};
    std::copy_n(reinterpret_cast<const std::byte*>(bytes.constData()), key.size(), key.begin());
    peers.push_back(key);
  }
  return std::make_shared<PairingTrust>(*identity, std::move(peers),
      [settings](const std::vector<PairingTrust::PublicKey>& updated) {
        QStringList values;
        for (const auto& key : updated)
          values.push_back(QString::fromLatin1(
              QByteArray(reinterpret_cast<const char*>(key.data()), 32).toHex()));
        settings->setValue(QStringLiteral("pairing/trustedDevices"), values);
        settings->sync();
        return settings->status() == QSettings::NoError;
      });
}

}  // namespace

RoleController::RoleController(QObject* parent) : QObject(parent) {
  QSettings settings(QSettings::NativeFormat, QSettings::UserScope, "AfterMaxQ", "MiniStream");
  game_mode_ = settings.value("input/gameMode", true).toBool();
  shared_clipboard_ = settings.value("session/sharedClipboard", true).toBool();
  english_keyboard_ = settings.value("input/englishKeyboard", true).toBool();
  const double sensitivity = settings.value("input/mouseSensitivity", 1.0).toDouble();
  mouse_sensitivity_ = std::isfinite(sensitivity) ? std::clamp(sensitivity, 0.1, 4.0) : 1.0;
  new GameKeyboardFilter([this] { return game_mode_ && remoteInputActive(); },
      [this](const QKeyEvent& event, bool pressed) {
        std::optional<DesktopKey> physical;
        for (std::uint16_t usage = 4; usage <= 0xE3; ++usage) {
          const auto key = desktop_key_from_wire(usage);
          if (!key) continue;
#ifdef _WIN32
          const auto native = windows_key_translation(*key);
          if (native && event.nativeScanCode() &&
              native->scan_code == (event.nativeScanCode() & 0xFF) &&
              native->extended == ((event.nativeScanCode() & 0xFF00) != 0)) physical = key;
#elif defined(__APPLE__)
          if (AccessibilityInput::native_key_code(*key) == event.nativeVirtualKey()) physical = key;
#endif
          if (physical) break;
        }
#if defined(_WIN32) || defined(__APPLE__)
        if (physical && remote_) remote_->route_input({DesktopInputKind::Key,
            static_cast<std::uint16_t>(pressed ? 0 : kDesktopKeyRelease), 0, 0,
            static_cast<std::uint16_t>(*physical)});
        else routeKey(event.key(), pressed);
#endif
      }, this);
  new ControlEscapeFilter([this] { return remoteInputActive(); },
                          [this] { releaseRemoteInput(); }, this);
  pairing_trust_ = load_pairing_trust();
#ifdef _WIN32
  auto controlled_backend = std::make_unique<WindowsControlledBackend>();
  controlled_capabilities_ = controlled_backend->inspect();
  controlled_ = std::make_unique<ControlledRuntime>(
      std::move(controlled_backend), controlled_advertisement(controlled_capabilities_),
      DiscoveryConfig{}, SessionTiming{}, pairing_trust_);

  auto remote_backend = std::make_unique<WindowsRemoteBackend>();
  auto* remote_backend_ptr = remote_backend.get();
  remote_capabilities_ = remote_backend->inspect();
  remote_ = std::make_unique<RemoteRuntime>(std::move(remote_backend), DiscoveryConfig{},
      DiscoveryInterfaceProvider{}, SessionTiming{}, pairing_trust_);
  video_surface_ = std::make_unique<WindowsVideoSurfaceBridge>(remote_backend_ptr);
  mode_ = RoleMode::Controlled;
#endif
#ifdef __APPLE__
  video_surface_ = std::make_unique<VideoSurfaceBridge>();
  auto controlled_backend = std::make_unique<MacControlledBackend>();
  controlled_capabilities_ = controlled_backend->inspect();
  controlled_ = std::make_unique<ControlledRuntime>(
      std::move(controlled_backend), controlled_advertisement(controlled_capabilities_),
      DiscoveryConfig{}, SessionTiming{}, pairing_trust_);
  auto* video_surface = static_cast<VideoSurfaceBridge*>(video_surface_.get());
  auto remote_backend = std::make_unique<MacRemoteBackend>(video_surface);
  remote_capabilities_ = remote_backend->inspect();
  remote_ = std::make_unique<RemoteRuntime>(std::move(remote_backend), DiscoveryConfig{},
      DiscoveryInterfaceProvider{}, SessionTiming{}, pairing_trust_);
  mode_ = RoleMode::Remote;
#endif

  clipboard_bridge_ = new ClipboardBridge([this](std::string text) {
#if defined(_WIN32) || defined(__APPLE__)
    if (mode_ == RoleMode::Controlled && controlled_) controlled_->send_clipboard(std::move(text));
    else if (remote_) remote_->send_clipboard(std::move(text));
#endif
  }, this);
#if defined(_WIN32) || defined(__APPLE__)
  auto receive = [this](const std::string& text) {
    clipboard_bridge_->setActive(shared_clipboard_ && connected(), false);
    clipboard_bridge_->receive(text);
  };
  controlled_->set_clipboard_callback(receive);
  remote_->set_clipboard_callback(receive);
  controlled_->set_clipboard_enabled(shared_clipboard_);
  remote_->set_clipboard_enabled(shared_clipboard_);
#endif
  tick_timer_.setInterval(10);
  tick_timer_.setTimerType(Qt::PreciseTimer);
  connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive && !connected()) {
              refreshCapabilities();
              failure_text_.clear();
              emit stateChanged();
            }
          });
  connect(&tick_timer_, &QTimer::timeout, this, &RoleController::tick);
  tick_timer_.start();
  discovery_timer_.setInterval(5000);
  connect(&discovery_timer_, &QTimer::timeout, this, [this] {
    if (mode_ == RoleMode::Remote && !connected() && !connecting() && !pairing()) findDevices(true);
  });
  discovery_timer_.start();
  QTimer::singleShot(0, this, [this] { if (mode_ == RoleMode::Remote) findDevices(true); });
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
  if (!pairing_trust_) return false;
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
         remote_->discovery_state() == DiscoveryState::Searching &&
         !(discovery_background_ && discovery_initialized_);
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

QStringList RoleController::qualityOptions() const {
  return {QStringLiteral("Smooth · 1080p60 · HEVC / H.264"),
          QStringLiteral("Sharp · 1440p60 · HEVC"),
          QStringLiteral("Ultra · 4K60 · HEVC")};
}

int RoleController::streamQuality() const noexcept { return stream_quality_; }

void RoleController::setStreamQuality(int quality) {
  const int requested = std::clamp(quality, 0, 2);
  if (requested == stream_quality_ || connected() || connecting() || pairing()) return;
  stream_quality_ = requested;
  emit stateChanged();
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
  if (!pairing_trust_) return QStringLiteral("Could not load or save pairing settings.");
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
    return QStringLiteral("Enter this code once on the device you want to control.");
  }
  if (connected()) {
    return QStringLiteral("Connected");
  }
  if (!discovery_failure_.isEmpty()) return discovery_failure_;
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
  if (!pairing_trust_) { emit stateChanged(); return; }
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
  discovery_background_ = false;
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

void RoleController::findDevices(bool background) {
#if defined(_WIN32) || defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    if (remote_->discovery_state() == DiscoveryState::Searching) return;
    discovery_background_ = background;
    if (remote_->state() == RoleState::Idle && !remote_->start()) {
      failure_text_ = QStringLiteral("Remote backend is not ready.");
    } else if (!remote_->begin_discovery() && !searching()) {
      failure_text_.clear();
    } else {
      failure_text_.clear();
    }
    if (!background || !discovery_initialized_) emit stateChanged();
  }
#endif
}

void RoleController::connectToDevice(int index) {
  if (!pairing_trust_) { emit stateChanged(); return; }
  const auto profile = static_cast<StreamProfileId>(stream_quality_);
#if defined(_WIN32) || defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    std::size_t selected = remote_->hosts().size();
    int visible_index{};
    for (std::size_t i = 0; i < remote_->hosts().size(); ++i) {
      if (!remote_->hosts()[i].controllable) continue;
      if (visible_index++ == index) { selected = i; break; }
    }
    if (selected == remote_->hosts().size() || !remote_->connect(selected, profile)) {
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

int RoleController::pairedDeviceCount() const noexcept {
  return pairing_trust_ ? static_cast<int>(pairing_trust_->size()) : 0;
}

bool RoleController::pairWithCode(const QString& code) {
#if defined(_WIN32) || defined(__APPLE__)
  if (mode_ == RoleMode::Controlled && controlled_) {
    const auto accepted = controlled_->pair_with_code(code.toStdString());
    emit stateChanged();
    return accepted;
  }
#endif
  return false;
}

void RoleController::forgetPairedDevices() {
  if (connected() || connecting() || pairing() || !pairing_trust_) return;
  if (!pairing_trust_->forget_all()) failure_text_ = QStringLiteral("Could not save pairing settings.");
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
  if (!remoteInputActive()) {
#if defined(_WIN32) || defined(__APPLE__)
    if (remote_) remote_->set_input_mode(game_mode_, english_keyboard_);
#endif
    game_language_.setEnglish(game_mode_ && english_keyboard_);
  }
#ifdef _WIN32
  if (mode_ == RoleMode::Remote && remote_) {
    remote_->toggle_input();
  }
#elif defined(__APPLE__)
  if (mode_ == RoleMode::Remote && remote_) {
    remote_->toggle_input();
  }
#endif
  if (!remoteInputActive()) game_language_.setEnglish(false);
  emit stateChanged();
}

void RoleController::releaseRemoteInput() {
  game_language_.setEnglish(false);
  mouse_remainder_x_ = mouse_remainder_y_ = 0;
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

bool RoleController::gamepadAvailable() const noexcept {
#if defined(_WIN32) || defined(__APPLE__)
  return remote_ && remote_->selected_host() && remote_->selected_host()->capabilities.gamepad;
#else
  return false;
#endif
}

void RoleController::setEnglishKeyboard(bool enabled) {
  releaseRemoteInput();
  english_keyboard_ = enabled;
  QSettings(QSettings::NativeFormat, QSettings::UserScope, "AfterMaxQ", "MiniStream")
      .setValue("input/englishKeyboard", enabled);
  emit stateChanged();
}

void RoleController::setSharedClipboard(bool enabled) {
  shared_clipboard_ = enabled;
  QSettings(QSettings::NativeFormat, QSettings::UserScope, "AfterMaxQ", "MiniStream")
      .setValue("session/sharedClipboard", enabled);
#if defined(_WIN32) || defined(__APPLE__)
  controlled_->set_clipboard_enabled(enabled);
  remote_->set_clipboard_enabled(enabled);
#endif
  clipboard_bridge_->setActive(enabled && connected(), mode_ == RoleMode::Remote);
  emit stateChanged();
}

void RoleController::setGameMode(bool game) {
  if (game_mode_ == game) return;
  releaseRemoteInput();
  game_mode_ = game;
  QSettings(QSettings::NativeFormat, QSettings::UserScope, "AfterMaxQ", "MiniStream")
      .setValue("input/gameMode", game);
  emit stateChanged();
}

void RoleController::setMouseSensitivity(double value) {
  if (!std::isfinite(value)) return;
  mouse_sensitivity_ = std::clamp(value, 0.1, 4.0);
  mouse_remainder_x_ = mouse_remainder_y_ = 0;
  QSettings(QSettings::NativeFormat, QSettings::UserScope, "AfterMaxQ", "MiniStream")
      .setValue("input/mouseSensitivity", mouse_sensitivity_);
  emit stateChanged();
}

void RoleController::routeMouseMove(int dx, int dy) {
  if (!game_mode_) return;
  mouse_remainder_x_ += dx * mouse_sensitivity_;
  mouse_remainder_y_ += dy * mouse_sensitivity_;
  dx = static_cast<int>(mouse_remainder_x_);
  dy = static_cast<int>(mouse_remainder_y_);
  mouse_remainder_x_ -= dx;
  mouse_remainder_y_ -= dy;
  if (!dx && !dy) return;
#ifdef _WIN32
  if (remote_ && remoteInputActive()) {
    if (const auto input = WindowInputSource::mouse_move(dx, dy)) {
      auto game_input = *input;
      game_input.flags = kDesktopMouseGame;
      remote_->route_input(game_input);
    }
  }
#elif defined(__APPLE__)
  if (remote_ && remoteInputActive()) {
    remote_->route_input({DesktopInputKind::MouseMove, kDesktopMouseGame, dx, dy, 0});
  }
#else
  Q_UNUSED(dx);
  Q_UNUSED(dy);
#endif
}

void RoleController::routeMousePosition(int x, int y) {
#if defined(_WIN32) || defined(__APPLE__)
  if (remote_ && remoteInputActive())
    remote_->route_input({DesktopInputKind::MouseMove, kDesktopMouseAbsolute,
                         std::clamp(x, 0, 65535), std::clamp(y, 0, 65535), 0});
#endif
}

void RoleController::setHdrOutputAvailable(bool available) {
#if defined(_WIN32) || defined(__APPLE__)
  if (remote_) {
    remote_->set_hdr_output(available);
    remote_capabilities_ = remote_->inspect();
    emit stateChanged();
  }
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
  MacControlledBackend::request_permissions();
  refreshCapabilities();
  emit stateChanged();
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
  if (!remoteInputActive()) game_language_.setEnglish(false);
  clipboard_bridge_->setActive(shared_clipboard_ && connected(), mode_ == RoleMode::Remote);
  const int interval = connected() ? 2 : 10;
  if (tick_timer_.interval() != interval) tick_timer_.setInterval(interval);
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
    const auto before_discovery_failure = discovery_failure_;
    const auto before_hosts = hosts();
    const auto before_pairing_code = remote_->pairing_code();
    const auto before_video_status = remote_->video_status();
    const auto before_input = remote_->remote_input_active();
    remote_->tick();
    const auto after_state = remote_->state();
    const bool hosts_changed = before_hosts != hosts();
    if (hosts_changed) emit hostsChanged();
    const auto discovery = remote_->discovery_state();
    const bool first_discovery = !discovery_initialized_ &&
        (discovery == DiscoveryState::Complete || discovery == DiscoveryState::Failed);
    if (discovery == DiscoveryState::Complete) discovery_failure_.clear();
    if (discovery == DiscoveryState::Failed && remote_->last_discovery_error()) {
      switch (*remote_->last_discovery_error()) {
        case DiscoveryError::PermissionDenied: discovery_failure_ = "Allow MiniStream local network access in system settings."; break;
        case DiscoveryError::NoUsableInterface: discovery_failure_ = "No usable network interface is available."; break;
        default: discovery_failure_ = "Local network discovery is unavailable. Check firewall settings."; break;
      }
    }
    if (first_discovery) discovery_initialized_ = true;
    if (before_state != after_state || first_discovery || before_discovery_failure != discovery_failure_ ||
        ((!discovery_background_ || first_discovery) && before_discovery != discovery) ||
        hosts_changed ||
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
  clipboard_bridge_->setActive(false, false);
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
  emit hostsChanged();
}

}  // namespace ministream
