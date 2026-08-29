#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace ministream {

enum class InputDevice : std::uint8_t { Keyboard, Mouse, Gamepad };

// Owns the local/remote routing state for input devices. A lease releases its
// device when it is destroyed, and leave_remote() releases every device at
// once. This keeps an input backend from holding a pointer or mouse capture
// after a session is stopped.
class InputCapture {
 public:
  class Lease {
   public:
    Lease() = default;
    ~Lease();
    Lease(Lease&& other) noexcept;
    Lease& operator=(Lease&& other) noexcept;
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] InputDevice device() const noexcept { return device_; }
    void release() noexcept;

   private:
    friend class InputCapture;
    Lease(InputCapture* owner, InputDevice device) noexcept
        : owner_(owner), device_(device) {}

    InputCapture* owner_{};
    InputDevice device_{InputDevice::Keyboard};
  };

  InputCapture() = default;
  InputCapture(const InputCapture&) = delete;
  InputCapture& operator=(const InputCapture&) = delete;

  // Enters remote mode without capturing a device. Backends acquire only the
  // devices they actually route, so a failed device setup cannot trap input.
  bool enter_remote() noexcept;
  void leave_remote() noexcept;

  [[nodiscard]] bool remote() const noexcept;
  [[nodiscard]] bool captured(InputDevice device) const noexcept;
  [[nodiscard]] bool routes_to_remote(InputDevice device) const noexcept;

  std::optional<Lease> capture(InputDevice device) noexcept;

 private:
  static constexpr std::uint8_t kRemoteBit = 0x01U;

  static constexpr std::uint8_t device_bit(InputDevice device) noexcept {
    switch (device) {
      case InputDevice::Keyboard:
        return 0x02U;
      case InputDevice::Mouse:
        return 0x04U;
      case InputDevice::Gamepad:
        return 0x08U;
    }
    return 0;
  }

  void release_device(InputDevice device) noexcept;

  std::atomic<std::uint8_t> state_{};
};

}  // namespace ministream
