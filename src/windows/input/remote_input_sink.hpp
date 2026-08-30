#pragma once

#include "core/base/result.hpp"
#include "core/input/desktop_input.hpp"

namespace ministream {

enum class RemoteInputError { InvalidEvent, InjectionFailed };

class RemoteInputSink {
 public:
  Result<void, RemoteInputError> inject(const DesktopInput& input) const;
};

}  // namespace ministream
