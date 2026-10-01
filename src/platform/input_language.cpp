#include "platform/input_language.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <map>
#elif defined(__APPLE__)
#include <Carbon/Carbon.h>
#endif

namespace ministream {
struct InputLanguageGuard::Impl {
  bool foreground{};
  bool active{};
#ifdef _WIN32
  HKL english{};
  HKL previous{};
  std::map<HWND, HKL> windows;
#elif defined(__APPLE__)
  TISInputSourceRef previous{};
  TISInputSourceRef english{};
#endif
};
InputLanguageGuard::InputLanguageGuard(bool foreground) : impl_(std::make_unique<Impl>()) {
  impl_->foreground = foreground;
}
InputLanguageGuard::~InputLanguageGuard() { setEnglish(false); }
void InputLanguageGuard::setEnglish(bool enabled) {
  if (enabled == impl_->active) return;
#ifdef _WIN32
  if (enabled) {
    impl_->english = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
    if (!impl_->english) return;
    impl_->previous = GetKeyboardLayout(0);
    if (!impl_->foreground) ActivateKeyboardLayout(impl_->english, 0);
  } else {
    if (!impl_->foreground && GetKeyboardLayout(0) == impl_->english)
      ActivateKeyboardLayout(impl_->previous, 0);
    for (const auto& [window, previous] : impl_->windows) {
      if (IsWindow(window) && GetKeyboardLayout(GetWindowThreadProcessId(window, nullptr)) == impl_->english)
        PostMessageW(window, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(previous));
    }
    impl_->windows.clear();
  }
#elif defined(__APPLE__)
  if (enabled) {
    auto sources = TISCreateInputSourceList(nullptr, false);
    if (!sources) return;
    for (CFIndex i = 0; i < CFArrayGetCount(sources); ++i) {
      auto source = static_cast<TISInputSourceRef>(const_cast<void*>(CFArrayGetValueAtIndex(sources, i)));
      auto id = static_cast<CFStringRef>(TISGetInputSourceProperty(source, kTISPropertyInputSourceID));
      if (id && (CFEqual(id, CFSTR("com.apple.keylayout.US")) || CFEqual(id, CFSTR("com.apple.keylayout.ABC"))) &&
          TISGetInputSourceProperty(source, kTISPropertyInputSourceIsEnabled) == kCFBooleanTrue) {
        impl_->english = static_cast<TISInputSourceRef>(const_cast<void*>(CFRetain(source)));
        break;
      }
    }
    CFRelease(sources);
    if (!impl_->english) return;
    impl_->previous = TISCopyCurrentKeyboardInputSource();
    if (TISSelectInputSource(impl_->english) != noErr) {
      if (impl_->previous) CFRelease(impl_->previous);
      CFRelease(impl_->english);
      impl_->previous = impl_->english = nullptr;
      return;
    }
  } else {
    auto current = TISCopyCurrentKeyboardInputSource();
    if (current && impl_->previous && CFEqual(current, impl_->english))
      TISSelectInputSource(impl_->previous);
    if (current) CFRelease(current);
    if (impl_->previous) CFRelease(impl_->previous);
    if (impl_->english) CFRelease(impl_->english);
    impl_->previous = impl_->english = nullptr;
  }
#endif
  impl_->active = enabled;
  refresh();
}
void InputLanguageGuard::refresh() {
#ifdef _WIN32
  if (!impl_->active || !impl_->foreground) return;
  const auto window = GetForegroundWindow();
  if (!window) return;
  const auto current = GetKeyboardLayout(GetWindowThreadProcessId(window, nullptr));
  if (current == impl_->english) return;
  impl_->windows.try_emplace(window, current);
  DWORD_PTR result{};
  SendMessageTimeoutW(window, WM_INPUTLANGCHANGEREQUEST, 0,
      reinterpret_cast<LPARAM>(impl_->english), SMTO_ABORTIFHUNG | SMTO_BLOCK, 20, &result);
#elif defined(__APPLE__)
  if (!impl_->active || !impl_->english) return;
  auto current = TISCopyCurrentKeyboardInputSource();
  if (current && !CFEqual(current, impl_->english)) TISSelectInputSource(impl_->english);
  if (current) CFRelease(current);
#endif
}
}
