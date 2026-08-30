#include "macos/input/accessibility_input.hpp"

#import <ApplicationServices/ApplicationServices.h>

#include <algorithm>

namespace ministream {

bool AccessibilityInput::trusted() noexcept { return AXIsProcessTrusted(); }

std::optional<std::uint16_t> AccessibilityInput::native_key_code(DesktopKey key) noexcept {
  const auto value = static_cast<std::uint16_t>(key);
  if (value >= static_cast<std::uint16_t>(DesktopKey::A) &&
      value <= static_cast<std::uint16_t>(DesktopKey::Z)) {
    constexpr std::uint16_t codes[] = {
        0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46, 45, 31, 35, 12, 15, 1, 17,
        32, 9, 13, 7, 16, 6};
    return codes[value - static_cast<std::uint16_t>(DesktopKey::A)];
  }
  if (value >= static_cast<std::uint16_t>(DesktopKey::Digit1) &&
      value <= static_cast<std::uint16_t>(DesktopKey::Digit0)) {
    constexpr std::uint16_t codes[] = {18, 19, 20, 21, 23, 22, 26, 28, 25, 29};
    return codes[value - static_cast<std::uint16_t>(DesktopKey::Digit1)];
  }
  if (value >= static_cast<std::uint16_t>(DesktopKey::F1) &&
      value <= static_cast<std::uint16_t>(DesktopKey::F12)) {
    constexpr std::uint16_t codes[] = {122, 120, 99, 118, 96, 97,
                                       98, 100, 101, 109, 103, 111};
    return codes[value - static_cast<std::uint16_t>(DesktopKey::F1)];
  }
  switch (key) {
    case DesktopKey::Enter: return 36;
    case DesktopKey::Escape: return 53;
    case DesktopKey::Backspace: return 51;
    case DesktopKey::Tab: return 48;
    case DesktopKey::Space: return 49;
    case DesktopKey::Minus: return 27;
    case DesktopKey::Equal: return 24;
    case DesktopKey::LeftBracket: return 33;
    case DesktopKey::RightBracket: return 30;
    case DesktopKey::Backslash: return 42;
    case DesktopKey::Semicolon: return 41;
    case DesktopKey::Apostrophe: return 39;
    case DesktopKey::Grave: return 50;
    case DesktopKey::Comma: return 43;
    case DesktopKey::Period: return 47;
    case DesktopKey::Slash: return 44;
    case DesktopKey::CapsLock: return 57;
    case DesktopKey::Insert: return 114;
    case DesktopKey::Home: return 115;
    case DesktopKey::PageUp: return 116;
    case DesktopKey::DeleteForward: return 117;
    case DesktopKey::End: return 119;
    case DesktopKey::PageDown: return 121;
    case DesktopKey::Right: return 124;
    case DesktopKey::Left: return 123;
    case DesktopKey::Down: return 125;
    case DesktopKey::Up: return 126;
    case DesktopKey::LeftControl: return 59;
    case DesktopKey::LeftShift: return 56;
    case DesktopKey::LeftAlt: return 58;
    case DesktopKey::LeftMeta: return 55;
    default: return std::nullopt;
  }
}

std::optional<DesktopInput> AccessibilityInput::key_from_qt(std::uint32_t qt_key,
                                                             bool pressed) noexcept {
  const auto key = desktop_key_from_qt(qt_key);
  if (!key) return std::nullopt;
  return DesktopInput{DesktopInputKind::Key,
                      static_cast<std::uint16_t>(pressed ? 0 : kDesktopKeyRelease), 0, 0,
                      static_cast<std::uint16_t>(*key)};
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
    const DesktopInput& input) {
  if (!trusted()) {
    return Result<void, AccessibilityInputError>::err(AccessibilityInputError::Permission);
  }
  CGEventRef event = nullptr;
  std::optional<DesktopKey> key_state;
  std::optional<DesktopMouseButton> button_state;
  bool release{};
  if (input.kind == DesktopInputKind::Key) {
    const auto key = desktop_key_from_wire(input.data);
    const auto code = key ? native_key_code(*key) : std::nullopt;
    if (!code || (input.flags & ~kDesktopKeyRelease) != 0U) {
      return Result<void, AccessibilityInputError>::err(AccessibilityInputError::InvalidEvent);
    }
    key_state = *key;
    release = (input.flags & kDesktopKeyRelease) != 0U;
    event = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(*code),
                                       !release);
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
    release = !down;
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
    button_state = static_cast<DesktopMouseButton>(button);
    event = CGEventCreateMouseEvent(nullptr, type, location, mouse_button);
  } else if (input.kind == DesktopInputKind::MouseWheel) {
    const auto lines = std::clamp(input.y / 120, -10, 10);
    event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, lines);
  } else {
    return Result<void, AccessibilityInputError>::err(AccessibilityInputError::InvalidEvent);
  }
  if (!event) {
    return Result<void, AccessibilityInputError>::err(AccessibilityInputError::PostFailed);
  }
  CGEventPost(kCGHIDEventTap, event);
  CFRelease(event);
  if (key_state) {
    if (release) {
      pressed_keys_.erase(*key_state);
    } else {
      pressed_keys_.insert(*key_state);
    }
  } else if (button_state) {
    if (release) {
      pressed_buttons_.erase(*button_state);
    } else {
      pressed_buttons_.insert(*button_state);
    }
  }
  return Result<void, AccessibilityInputError>::ok();
}

void AccessibilityInput::clear() noexcept {
  while (!pressed_keys_.empty()) {
    const auto key = *pressed_keys_.begin();
    if (!inject({DesktopInputKind::Key, kDesktopKeyRelease, 0, 0,
                 static_cast<std::uint16_t>(key)})) {
      pressed_keys_.erase(key);
    }
  }
  while (!pressed_buttons_.empty()) {
    const auto button = *pressed_buttons_.begin();
    if (!inject({DesktopInputKind::MouseButton,
                 static_cast<std::uint16_t>(static_cast<std::uint16_t>(button) |
                                            kDesktopMouseRelease),
                 0, 0, 0})) {
      pressed_buttons_.erase(button);
    }
  }
}

}  // namespace ministream
