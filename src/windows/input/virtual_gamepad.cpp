#include "windows/input/virtual_gamepad.hpp"

#include "windows/input/vigem_gamepad.hpp"

#include <utility>

namespace ministream {

class VirtualGamepad::Impl {
 public:
  VigemGamepad backend;
};

VirtualGamepad::VirtualGamepad() : impl_(std::make_unique<Impl>()) {}
VirtualGamepad::~VirtualGamepad() = default;
VirtualGamepad::VirtualGamepad(VirtualGamepad&&) noexcept = default;
VirtualGamepad& VirtualGamepad::operator=(VirtualGamepad&&) noexcept = default;

Result<void, InputError> VirtualGamepad::start() { return impl_->backend.start(); }

Result<void, InputError> VirtualGamepad::submit(const GamepadState& state) {
  return impl_->backend.submit(state);
}

void VirtualGamepad::set_rumble_callback(std::function<void(RumbleState)> callback) {
  impl_->backend.set_rumble_callback(std::move(callback));
}

void VirtualGamepad::stop() { impl_->backend.stop(); }

}  // namespace ministream
