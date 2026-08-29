#include "core/input/input_capture.hpp"

namespace ministream {

InputCapture::Lease::~Lease() { release(); }

bool InputCapture::Lease::active() const noexcept {
  return owner_ != nullptr && owner_->captured(device_);
}

InputCapture::Lease::Lease(Lease&& other) noexcept
    : owner_(other.owner_), device_(other.device_) {
  other.owner_ = nullptr;
}

InputCapture::Lease& InputCapture::Lease::operator=(Lease&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  release();
  owner_ = other.owner_;
  device_ = other.device_;
  other.owner_ = nullptr;
  return *this;
}

void InputCapture::Lease::release() noexcept {
  if (owner_ == nullptr) {
    return;
  }
  owner_->release_device(device_);
  owner_ = nullptr;
}

bool InputCapture::enter_remote() noexcept {
  auto state = state_.load(std::memory_order_acquire);
  for (;;) {
    if ((state & kRemoteBit) != 0U) {
      return true;
    }
    const auto next = static_cast<std::uint8_t>(state | kRemoteBit);
    if (state_.compare_exchange_weak(state, next, std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
      return true;
    }
  }
}

void InputCapture::leave_remote() noexcept {
  // Clearing the remote bit and all device bits invalidates existing leases.
  // Their destructors remain safe because release_device() only clears bits.
  state_.store(0, std::memory_order_release);
}

bool InputCapture::remote() const noexcept {
  return (state_.load(std::memory_order_acquire) & kRemoteBit) != 0U;
}

bool InputCapture::captured(InputDevice device) const noexcept {
  const auto mask = device_bit(device);
  return mask != 0U &&
         (state_.load(std::memory_order_acquire) & mask) != 0U;
}

bool InputCapture::routes_to_remote(InputDevice device) const noexcept {
  const auto state = state_.load(std::memory_order_acquire);
  const auto mask = device_bit(device);
  return mask != 0U && (state & kRemoteBit) != 0U && (state & mask) != 0U;
}

std::optional<InputCapture::Lease> InputCapture::capture(
    InputDevice device) noexcept {
  const auto mask = device_bit(device);
  if (mask == 0U) {
    return std::nullopt;
  }

  auto state = state_.load(std::memory_order_acquire);
  for (;;) {
    if ((state & kRemoteBit) == 0U || (state & mask) != 0U) {
      return std::nullopt;
    }
    const auto next = static_cast<std::uint8_t>(state | mask);
    if (state_.compare_exchange_weak(state, next, std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
      return Lease{this, device};
    }
  }
}

void InputCapture::release_device(InputDevice device) noexcept {
  const auto mask = device_bit(device);
  if (mask != 0U) {
    state_.fetch_and(static_cast<std::uint8_t>(~mask), std::memory_order_acq_rel);
  }
}

}  // namespace ministream
