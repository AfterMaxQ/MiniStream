#pragma once

#include "core/base/result.hpp"
#include "core/input/desktop_input.hpp"

#include <set>

namespace ministream {

enum class RemoteInputError { InvalidEvent, InjectionFailed };

class RemoteInputSink {
 public:
  void set_display(std::uintptr_t monitor) noexcept { monitor_ = monitor; }
  Result<void, RemoteInputError> inject(const DesktopInput& input);
  void clear() noexcept;

 private:
  std::uintptr_t monitor_{};
  std::set<DesktopKey> pressed_keys_;
  std::set<DesktopMouseButton> pressed_buttons_;
};

}  // namespace ministream
