#include "windows/input/remote_input_sink.hpp"
#include "windows/input/desktop_key_windows.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace ministream {

Result<void, RemoteInputError> RemoteInputSink::inject(const DesktopInput& input) {
  INPUT event{};
  std::optional<DesktopKey> key;
  std::optional<DesktopMouseButton> button;
  bool release{};

  switch (input.kind) {
    case DesktopInputKind::Key: {
      key = desktop_key_from_wire(input.data);
      const auto translation = key ? windows_key_translation(*key) : std::nullopt;
      if (!translation || (input.flags & ~kDesktopKeyRelease) != 0U) {
        return Result<void, RemoteInputError>::err(RemoteInputError::InvalidEvent);
      }
      release = (input.flags & kDesktopKeyRelease) != 0U;
      event.type = INPUT_KEYBOARD;
      event.ki.wScan = translation->scan_code;
      event.ki.dwFlags = KEYEVENTF_SCANCODE |
                         (translation->extended ? KEYEVENTF_EXTENDEDKEY : 0U) |
                         (release ? KEYEVENTF_KEYUP : 0U);
      break;
    }
    case DesktopInputKind::MouseMove:
      event.type = INPUT_MOUSE;
      event.mi.dx = input.x;
      event.mi.dy = input.y;
      event.mi.dwFlags = MOUSEEVENTF_MOVE;
      break;
    case DesktopInputKind::MouseButton: {
      const auto raw_button = input.flags & ~kDesktopMouseRelease;
      if (raw_button < static_cast<std::uint16_t>(DesktopMouseButton::Left) ||
          raw_button > static_cast<std::uint16_t>(DesktopMouseButton::Middle)) {
        return Result<void, RemoteInputError>::err(RemoteInputError::InvalidEvent);
      }
      button = static_cast<DesktopMouseButton>(raw_button);
      release = (input.flags & kDesktopMouseRelease) != 0U;
      event.type = INPUT_MOUSE;
      switch (*button) {
        case DesktopMouseButton::Left:
          event.mi.dwFlags = release ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
          break;
        case DesktopMouseButton::Right:
          event.mi.dwFlags = release ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
          break;
        case DesktopMouseButton::Middle:
          event.mi.dwFlags = release ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN;
          break;
      }
      break;
    }
    case DesktopInputKind::MouseWheel:
      event.type = INPUT_MOUSE;
      event.mi.mouseData = static_cast<DWORD>(static_cast<SHORT>(input.y));
      event.mi.dwFlags = MOUSEEVENTF_WHEEL;
      break;
    default:
      return Result<void, RemoteInputError>::err(RemoteInputError::InvalidEvent);
  }

  if (SendInput(1, &event, sizeof(INPUT)) != 1) {
    return Result<void, RemoteInputError>::err(RemoteInputError::InjectionFailed);
  }
  if (key) {
    if (release) {
      pressed_keys_.erase(*key);
    } else {
      pressed_keys_.insert(*key);
    }
  } else if (button) {
    if (release) {
      pressed_buttons_.erase(*button);
    } else {
      pressed_buttons_.insert(*button);
    }
  }
  return Result<void, RemoteInputError>::ok();
}

void RemoteInputSink::clear() noexcept {
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
