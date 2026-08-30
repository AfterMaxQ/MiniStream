#include "macos/input/accessibility_input.hpp"

#import <ApplicationServices/ApplicationServices.h>

namespace ministream {

namespace {

std::optional<std::uint16_t> key_code(std::uint32_t key) noexcept {
  if (key >= static_cast<std::uint32_t>('A') && key <= static_cast<std::uint32_t>('Z')) {
    constexpr std::uint16_t codes[] = {
        0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46, 45, 31, 35, 12, 15, 1, 17,
        32, 9, 13, 7, 16, 6};
    return codes[key - static_cast<std::uint32_t>('A')];
  }
  if (key >= static_cast<std::uint32_t>('a') && key <= static_cast<std::uint32_t>('z')) {
    return key_code(key - static_cast<std::uint32_t>('a' - 'A'));
  }
  if (key >= static_cast<std::uint32_t>('0') && key <= static_cast<std::uint32_t>('9')) {
    constexpr std::uint16_t codes[] = {29, 18, 19, 20, 21, 23, 22, 26, 28, 25};
    return codes[key - static_cast<std::uint32_t>('0')];
  }
  switch (key) {
    case 0x01000000U: return 53;   // Escape
    case 0x01000001U: return 48;   // Tab
    case 0x01000003U: return 51;   // Backspace
    case 0x01000004U:
    case 0x01000005U: return 36;   // Return / Enter
    case 0x01000007U: return 51;   // Delete
    case 0x01000010U: return 115;  // Home
    case 0x01000011U: return 119;  // End
    case 0x01000012U: return 123;  // Left
    case 0x01000013U: return 126;  // Up
    case 0x01000014U: return 124;  // Right
    case 0x01000015U: return 125;  // Down
    case 0x01000016U: return 116;  // Page up
    case 0x01000017U: return 121;  // Page down
    case 0x01000020U: return 56;   // Shift
    case 0x01000021U: return 59;   // Control
    case 0x01000022U: return 55;   // Command
    case 0x01000023U: return 58;   // Option
    case 0x01000024U: return 57;   // Caps lock
    default: break;
  }
  if (key >= 0x01000030U && key <= 0x0100003BU) {
    constexpr std::uint16_t codes[] = {122, 120, 99, 118, 96, 97, 98, 100, 101, 109, 103, 111};
    return codes[key - 0x01000030U];
  }
  return std::nullopt;
}

}  // namespace

bool AccessibilityInput::trusted() noexcept { return AXIsProcessTrusted(); }

std::optional<DesktopInput> AccessibilityInput::key_from_qt(std::uint32_t qt_key,
                                                             bool pressed) noexcept {
  const auto code = key_code(qt_key);
  if (!code) return std::nullopt;
  return DesktopInput{DesktopInputKind::Key,
                      static_cast<std::uint16_t>(pressed ? 0 : 0x0002U), 0, 0, *code};
}

std::optional<DesktopInput> AccessibilityInput::mouse_button_from_qt(
    std::uint32_t qt_button, bool pressed) noexcept {
  std::uint16_t button = 0;
  switch (qt_button) {
    case 1: button = static_cast<std::uint16_t>(DesktopMouseButton::Left); break;
    case 2: button = static_cast<std::uint16_t>(DesktopMouseButton::Right); break;
    case 4: button = static_cast<std::uint16_t>(DesktopMouseButton::Middle); break;
    default: return std::nullopt;
  }
  if (!pressed) button = static_cast<std::uint16_t>(button | kDesktopMouseRelease);
  return DesktopInput{DesktopInputKind::MouseButton, button, 0, 0, 0};
}

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
    const auto button = input.flags & ~kDesktopMouseRelease;
    const bool down = (input.flags & kDesktopMouseRelease) == 0U;
    CGEventType type = kCGEventLeftMouseDown;
    CGMouseButton mouse_button = kCGMouseButtonLeft;
    if (button == static_cast<std::uint16_t>(DesktopMouseButton::Right)) {
      type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp;
      mouse_button = kCGMouseButtonRight;
    } else if (button == static_cast<std::uint16_t>(DesktopMouseButton::Middle)) {
      type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
      mouse_button = kCGMouseButtonCenter;
    } else if (button != static_cast<std::uint16_t>(DesktopMouseButton::Left)) {
      return Result<void, AccessibilityInputError>::err(AccessibilityInputError::InvalidEvent);
    } else if (!down) {
      type = kCGEventLeftMouseUp;
    }
    event = CGEventCreateMouseEvent(nullptr, type, location, mouse_button);
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
