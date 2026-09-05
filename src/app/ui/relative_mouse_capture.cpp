#include "app/ui/relative_mouse_capture.hpp"

#include <QCursor>
#include <QGuiApplication>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#endif

namespace ministream {

RelativeMouseCapture::RelativeMouseCapture(QObject* parent) : QObject(parent) {
  QCoreApplication::instance()->installNativeEventFilter(this);
  connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) setActive(false);
          });
}

RelativeMouseCapture::~RelativeMouseCapture() { setActive(false); }

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
    if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) { emit captureFailed(); return; }
    RECT client{};
    GetClientRect(reinterpret_cast<HWND>(window_->winId()), &client);
    POINT native_center{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
    ClientToScreen(reinterpret_cast<HWND>(window_->winId()), &native_center);
    SetCursorPos(native_center.x, native_center.y);
    const RECT clip{native_center.x, native_center.y, native_center.x + 1, native_center.y + 1};
    if (!ClipCursor(&clip)) {
      const RAWINPUTDEVICE remove{0x01, 0x02, RIDEV_REMOVE, nullptr};
      RegisterRawInputDevices(&remove, 1, sizeof(remove));
      emit captureFailed();
      return;
    }
#elif defined(__APPLE__)
    QCursor::setPos(window_->mapToGlobal(QPoint(window_->width() / 2, window_->height() / 2)));
    if (CGAssociateMouseAndMouseCursorPosition(false) != kCGErrorSuccess) {
      emit captureFailed();
      return;
    }
#endif
    window_->setCursor(Qt::BlankCursor);
    remainder_ = {};
  } else {
#ifdef _WIN32
    ClipCursor(nullptr);
    const RAWINPUTDEVICE remove{0x01, 0x02, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&remove, 1, sizeof(remove));
#elif defined(__APPLE__)
    CGAssociateMouseAndMouseCursorPosition(true);
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
    emit moved(input.data.mouse.lLastX, input.data.mouse.lLastY);
#elif defined(__APPLE__)
  if (type != "mac_generic_NSEvent") return false;
  NSEvent* event = static_cast<NSEvent*>(message);
  if (event.type != NSEventTypeMouseMoved && event.type != NSEventTypeLeftMouseDragged &&
      event.type != NSEventTypeRightMouseDragged && event.type != NSEventTypeOtherMouseDragged)
    return false;
  remainder_ += QPointF(event.deltaX, event.deltaY);
  const int dx = static_cast<int>(remainder_.x());
  const int dy = static_cast<int>(remainder_.y());
  remainder_ -= QPointF(dx, dy);
  if (dx || dy) emit moved(dx, dy);
#endif
  return false;
}

}  // namespace ministream
