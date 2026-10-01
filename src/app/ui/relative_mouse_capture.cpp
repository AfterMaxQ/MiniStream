#include "app/ui/relative_mouse_capture.hpp"

#include <QCursor>
#include <QGuiApplication>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#endif

namespace ministream {

RelativeMouseCapture::RelativeMouseCapture(QObject* parent) : QObject(parent) {
  QCoreApplication::instance()->installNativeEventFilter(this);
  flush_timer_.setTimerType(Qt::PreciseTimer);
  flush_timer_.setInterval(1);
  connect(&flush_timer_, &QTimer::timeout, this, [this] {
    const auto delta = pending_;
    pending_ = {};
    if (active_ && !delta.isNull()) emit moved(delta.x(), delta.y());
  });
  connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) setActive(false);
          });
}

RelativeMouseCapture::~RelativeMouseCapture() {
  setActive(false);
  QCoreApplication::instance()->removeNativeEventFilter(this);
}

void RelativeMouseCapture::setWindow(QWindow* window) {
  if (window_ == window) return;
  setActive(false);
  if (window_) disconnect(window_, nullptr, this, nullptr);
  window_ = window;
  if (window_) {
    connect(window_, &QWindow::activeChanged, this, [this] {
      if (!window_ || !window_->isActive()) setActive(false);
    });
    connect(window_, &QObject::destroyed, this, [this] { setActive(false); });
  }
  emit windowChanged();
}

void RelativeMouseCapture::setActive(bool active) {
  if (active == active_) return;
  if (active && (!window_ || !window_->isActive())) return;
  if (active) {
    restore_position_ = QCursor::pos();
#ifdef _WIN32
    const RAWINPUTDEVICE mouse{0x01, 0x02, 0, reinterpret_cast<HWND>(window_->winId())};
    if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) {
      emit captureFailed(QStringLiteral("Unable to capture raw mouse input.")); return;
    }
    RECT client{};
    GetClientRect(reinterpret_cast<HWND>(window_->winId()), &client);
    POINT native_center{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
    ClientToScreen(reinterpret_cast<HWND>(window_->winId()), &native_center);
    SetCursorPos(native_center.x, native_center.y);
    const RECT clip{native_center.x, native_center.y, native_center.x + 1, native_center.y + 1};
    if (!ClipCursor(&clip)) {
      const RAWINPUTDEVICE remove{0x01, 0x02, RIDEV_REMOVE, nullptr};
      RegisterRawInputDevices(&remove, 1, sizeof(remove));
      emit captureFailed(QStringLiteral("Unable to lock the game cursor."));
      return;
    }
#elif defined(__APPLE__)
    QCursor::setPos(window_->mapToGlobal(QPoint(window_->width() / 2, window_->height() / 2)));
    if (CGAssociateMouseAndMouseCursorPosition(false) != kCGErrorSuccess) {
      emit captureFailed(QStringLiteral("Allow Accessibility access for game mouse capture."));
      return;
    }
    auto manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!manager) {
      CGAssociateMouseAndMouseCursorPosition(true);
      QCursor::setPos(restore_position_);
      emit captureFailed(QStringLiteral("Unable to open raw mouse input."));
      return;
    }
    NSDictionary* match = @{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop),
                             @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_Mouse) };
    IOHIDManagerSetDeviceMatching(manager, (__bridge CFDictionaryRef)match);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), kCFRunLoopCommonModes);
    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
      IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetMain(), kCFRunLoopCommonModes);
      CFRelease(manager);
      CGAssociateMouseAndMouseCursorPosition(true);
      QCursor::setPos(restore_position_);
      emit captureFailed(QStringLiteral("Allow MiniStream in System Settings → Privacy & Security → Input Monitoring, then reopen it."));
      return;
    }
    hid_manager_ = manager;
    raw_mouse_ = false;
    if (auto devices = IOHIDManagerCopyDevices(manager)) {
      for (id device in (__bridge NSSet*)devices) {
        if (auto elements = IOHIDDeviceCopyMatchingElements((__bridge IOHIDDeviceRef)device,
                                                           nullptr, kIOHIDOptionsTypeNone)) {
          for (id element in (__bridge NSArray*)elements) {
            auto hid = (__bridge IOHIDElementRef)element;
            if (IOHIDElementGetUsagePage(hid) == kHIDPage_GenericDesktop &&
                IOHIDElementIsRelative(hid) &&
                IOHIDElementGetUsage(hid) == kHIDUsage_GD_X) raw_mouse_ = true;
          }
          CFRelease(elements);
        }
      }
      CFRelease(devices);
    }
    IOHIDManagerRegisterInputValueCallback(manager,
        [](void* context, IOReturn result, void* sender, IOHIDValueRef value) {
          hidValue(context, result, sender, value);
        }, this);
    mouse_coalescing_was_enabled_ = [NSEvent isMouseCoalescingEnabled];
    [NSEvent setMouseCoalescingEnabled:NO];
#endif
    window_->setCursor(Qt::BlankCursor);
    remainder_ = {};
    pending_ = {};
    flush_timer_.start();
  } else {
    flush_timer_.stop();
    pending_ = {};
#ifdef _WIN32
    ClipCursor(nullptr);
    const RAWINPUTDEVICE remove{0x01, 0x02, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&remove, 1, sizeof(remove));
#elif defined(__APPLE__)
    if (hid_manager_) {
      auto manager = static_cast<IOHIDManagerRef>(hid_manager_);
      IOHIDManagerRegisterInputValueCallback(manager, nullptr, nullptr);
      IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetMain(), kCFRunLoopCommonModes);
      IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
      CFRelease(manager);
      hid_manager_ = nullptr;
      raw_mouse_ = false;
    }
    CGAssociateMouseAndMouseCursorPosition(true);
    [NSEvent setMouseCoalescingEnabled:mouse_coalescing_was_enabled_];
#endif
    if (window_) window_->unsetCursor();
    QCursor::setPos(restore_position_);
  }
  active_ = active;
  emit activeChanged();
}

bool RelativeMouseCapture::nativeEventFilter(const QByteArray& type, void* message, qintptr*) {
  if (!active_ || !window_ || !window_->isActive()) return false;
#ifdef _WIN32
  if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
  const auto* msg = static_cast<MSG*>(message);
  if (msg->message != WM_INPUT || msg->hwnd != reinterpret_cast<HWND>(window_->winId())) return false;
  RAWINPUT input{};
  UINT size = sizeof(input);
  if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT,
                      &input, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
      input.header.dwType != RIM_TYPEMOUSE || (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
    return false;
  if (input.data.mouse.lLastX || input.data.mouse.lLastY)
    pending_ += QPoint(input.data.mouse.lLastX, input.data.mouse.lLastY);
#elif defined(__APPLE__)
  if (raw_mouse_) return false;
  if (type != "mac_generic_NSEvent") return false;
  NSEvent* event = static_cast<NSEvent*>(message);
  if (event.type != NSEventTypeMouseMoved && event.type != NSEventTypeLeftMouseDragged &&
      event.type != NSEventTypeRightMouseDragged && event.type != NSEventTypeOtherMouseDragged)
    return false;
  remainder_ += QPointF(event.deltaX, event.deltaY);
  const int dx = static_cast<int>(remainder_.x());
  const int dy = static_cast<int>(remainder_.y());
  remainder_ -= QPointF(dx, dy);
  if (dx || dy) pending_ += QPoint(dx, dy);
#endif
  return false;
}

#ifdef __APPLE__
void RelativeMouseCapture::hidValue(void* context, int result, void*, void* value) {
  auto* self = static_cast<RelativeMouseCapture*>(context);
  if (result != kIOReturnSuccess || !self->raw_mouse_ || !self->active_ || !self->window_ ||
      !self->window_->isActive()) return;
  auto hid_value = static_cast<IOHIDValueRef>(value);
  auto element = IOHIDValueGetElement(hid_value);
  if (IOHIDElementGetUsagePage(element) != kHIDPage_GenericDesktop ||
      !IOHIDElementIsRelative(element)) return;
  const auto delta = static_cast<int>(IOHIDValueGetIntegerValue(hid_value));
  if (IOHIDElementGetUsage(element) == kHIDUsage_GD_X) self->pending_.rx() += delta;
  else if (IOHIDElementGetUsage(element) == kHIDUsage_GD_Y) self->pending_.ry() += delta;
}
#endif

}  // namespace ministream
