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
  if (!keyboard || !mouse) {
    end();
    return false;
  }
  keyboard_lease_ = std::move(*keyboard);
  mouse_lease_ = std::move(*mouse);
  // Gamepads are optional.  Keyboard and mouse remain the baseline path even
  // when no local gamepad is connected or available to the native backend.
  if (gamepad) {
    gamepad_lease_ = std::move(*gamepad);
  }
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
  // The UI owns the local shortcuts only while remote input is inactive. In
  // remote mode Esc/F11 are ordinary game input and must be forwarded.
  sender_(input);
  return true;
}

}  // namespace ministream
