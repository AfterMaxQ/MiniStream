#pragma once

#include "core/base/result.hpp"
#include "core/input/desktop_input.hpp"

#include <set>

namespace ministream {

enum class RemoteInputError { InvalidEvent, InjectionFailed };

class RemoteInputSink {
 public:
  Result<void, RemoteInputError> inject(const DesktopInput& input);
  void clear() noexcept;

 private:
  std::set<DesktopKey> pressed_keys_;
  std::set<DesktopMouseButton> pressed_buttons_;
};

}  // namespace ministream
