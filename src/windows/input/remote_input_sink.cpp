#include "windows/input/remote_input_sink.hpp"
#include "windows/input/desktop_key_windows.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace ministream {

Result<void, RemoteInputError> RemoteInputSink::inject(const DesktopInput& input) const {
  INPUT event{};
  switch (input.kind) {
    case DesktopInputKind::Key:
      {
      const auto key = desktop_key_from_wire(input.data);
      const auto translation = key ? windows_key_translation(*key) : std::nullopt;
      if (!translation || (input.flags & ~kDesktopKeyRelease) != 0U) {
        return Result<void, RemoteInputError>::err(RemoteInputError::InvalidEvent);
      }
      event.type = INPUT_KEYBOARD;
      event.ki.wScan = translation->scan_code;
      event.ki.dwFlags = KEYEVENTF_SCANCODE |
                         (translation->extended ? KEYEVENTF_EXTENDEDKEY : 0U) |
                         ((input.flags & kDesktopKeyRelease) ? KEYEVENTF_KEYUP : 0U);
      break;
      }
    case DesktopInputKind::MouseMove:
      event.type = INPUT_MOUSE;
      event.mi.dx = input.x;
      event.mi.dy = input.y;
      event.mi.dwFlags = MOUSEEVENTF_MOVE;
      break;
    case DesktopInputKind::MouseButton:
      event.type = INPUT_MOUSE;
      switch (input.flags & ~kDesktopMouseRelease) {
        case static_cast<std::uint16_t>(DesktopMouseButton::Left):
          event.mi.dwFlags = (input.flags & kDesktopMouseRelease) ? MOUSEEVENTF_LEFTUP
                                                                   : MOUSEEVENTF_LEFTDOWN;
          break;
        case static_cast<std::uint16_t>(DesktopMouseButton::Right):
          event.mi.dwFlags = (input.flags & kDesktopMouseRelease) ? MOUSEEVENTF_RIGHTUP
                                                                   : MOUSEEVENTF_RIGHTDOWN;
          break;
        case static_cast<std::uint16_t>(DesktopMouseButton::Middle):
          event.mi.dwFlags = (input.flags & kDesktopMouseRelease) ? MOUSEEVENTF_MIDDLEUP
                                                                   : MOUSEEVENTF_MIDDLEDOWN;
          break;
        default:
          return Result<void, RemoteInputError>::err(RemoteInputError::InvalidEvent);
      }
      break;
    case DesktopInputKind::MouseWheel:
      event.type = INPUT_MOUSE;
      event.mi.mouseData = static_cast<DWORD>(static_cast<SHORT>(input.y));
      event.mi.dwFlags = MOUSEEVENTF_WHEEL;
      break;
    default:
      return Result<void, RemoteInputError>::err(RemoteInputError::InvalidEvent);
  }
  return SendInput(1, &event, sizeof(INPUT)) == 1
             ? Result<void, RemoteInputError>::ok()
             : Result<void, RemoteInputError>::err(RemoteInputError::InjectionFailed);
}

}  // namespace ministream
