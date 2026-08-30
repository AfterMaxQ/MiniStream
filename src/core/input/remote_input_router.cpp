#include "core/input/remote_input_router.hpp"

#include <utility>

namespace ministream {

RemoteInputRouter::RemoteInputRouter(InputCapture& capture, Sender sender)
    : capture_(capture), sender_(std::move(sender)) {}

bool RemoteInputRouter::begin() {
  if (!capture_.enter_remote()) {
    return false;
  }
  auto keyboard = capture_.capture(InputDevice::Keyboard);
  auto mouse = capture_.capture(InputDevice::Mouse);
  auto gamepad = capture_.capture(InputDevice::Gamepad);
  if (!keyboard || !mouse || !gamepad) {
    end();
    return false;
  }
  keyboard_lease_ = std::move(*keyboard);
  mouse_lease_ = std::move(*mouse);
  gamepad_lease_ = std::move(*gamepad);
  return true;
}

void RemoteInputRouter::end() noexcept {
  keyboard_lease_.reset();
  mouse_lease_.reset();
  gamepad_lease_.reset();
  capture_.leave_remote();
}

bool RemoteInputRouter::active() const noexcept { return capture_.remote(); }

bool RemoteInputRouter::route(const DesktopInput& input) {
  if (!active() || !sender_) {
    return false;
  }
  // VK_ESCAPE is always reserved for the local window. The F12 combination
  // is handled by the UI shortcut before events reach this router.
  if (input.kind == DesktopInputKind::Key && input.data == 0x1BU) {
    end();
    return false;
  }
  sender_(input);
  return true;
}

}  // namespace ministream
