#include "macos/input/accessibility_input.hpp"

#import <ApplicationServices/ApplicationServices.h>

namespace ministream {

bool AccessibilityInput::trusted() noexcept { return AXIsProcessTrusted(); }

Result<void, AccessibilityInputError> AccessibilityInput::inject(
    const DesktopInput& input) const {
  if (!trusted()) {
    return Result<void, AccessibilityInputError>::err(AccessibilityInputError::Permission);
  }
  CGEventRef event = nullptr;
  if (input.kind == DesktopInputKind::Key) {
    event = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(input.data),
                                       (input.flags & 0x0002U) == 0U);
  } else if (input.kind == DesktopInputKind::MouseMove) {
    auto current = CGEventCreate(nullptr);
    const auto location = current ? CGEventGetLocation(current) : CGPointZero;
    if (current) CFRelease(current);
    event = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved,
                                    CGPointMake(location.x + input.x, location.y + input.y),
                                    kCGMouseButtonLeft);
  } else if (input.kind == DesktopInputKind::MouseButton) {
    auto current = CGEventCreate(nullptr);
    const auto location = current ? CGEventGetLocation(current) : CGPointZero;
    if (current) CFRelease(current);
    const bool down = (input.flags & 0x0002U) == 0U;
    event = CGEventCreateMouseEvent(nullptr, down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp,
                                    location, kCGMouseButtonLeft);
  } else if (input.kind == DesktopInputKind::MouseWheel) {
    event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, input.y);
  } else {
    return Result<void, AccessibilityInputError>::err(AccessibilityInputError::InvalidEvent);
  }
  if (!event) {
    return Result<void, AccessibilityInputError>::err(AccessibilityInputError::PostFailed);
  }
  CGEventPost(kCGHIDEventTap, event);
  CFRelease(event);
  return Result<void, AccessibilityInputError>::ok();
}

}  // namespace ministream
