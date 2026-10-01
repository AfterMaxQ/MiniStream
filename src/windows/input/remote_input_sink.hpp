#pragma once

#include "core/base/result.hpp"
#include "core/input/desktop_input.hpp"
#include "platform/input_language.hpp"

#include <set>

namespace ministream {

enum class RemoteInputError { InvalidEvent, InjectionFailed };

class RemoteInputSink {
 public:
  void set_display(std::uintptr_t monitor) noexcept { monitor_ = monitor; }
  Result<void, RemoteInputError> inject(const DesktopInput& input);
  void clear() noexcept;
  void configure(bool game, bool english) { clear(); language_.setEnglish(game && english); }

 private:
  std::uintptr_t monitor_{};
  std::set<DesktopKey> pressed_keys_;
  std::set<DesktopMouseButton> pressed_buttons_;
  InputLanguageGuard language_{true};
};

}  // namespace ministream
