#pragma once

#include "core/input/desktop_input.hpp"
#include "core/input/input_capture.hpp"

#include <functional>
#include <optional>

namespace ministream {

// Window-local input routing. It deliberately has no global hooks: callers
// feed events from their window and stop the router when input should return
// to the local device.
class RemoteInputRouter {
 public:
  using Sender = std::function<void(const DesktopInput&)>;

  explicit RemoteInputRouter(InputCapture& capture, Sender sender);
  bool begin();
  void end() noexcept;
  [[nodiscard]] bool active() const noexcept;
  bool route(const DesktopInput& input);

 private:
  InputCapture& capture_;
  Sender sender_;
  std::optional<InputCapture::Lease> keyboard_lease_;
  std::optional<InputCapture::Lease> mouse_lease_;
  std::optional<InputCapture::Lease> gamepad_lease_;
};

}  // namespace ministream
