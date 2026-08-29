#pragma once

#include "core/base/result.hpp"
#include "core/input/gamepad_state.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace ministream {

enum class InputError { DriverMissing, Connection, Target, Submit };

struct RumbleState {
  std::uint16_t low{};
  std::uint16_t high{};
};

class VirtualGamepad {
 public:
  VirtualGamepad();
  ~VirtualGamepad();
  VirtualGamepad(VirtualGamepad&&) noexcept;
  VirtualGamepad& operator=(VirtualGamepad&&) noexcept;
  VirtualGamepad(const VirtualGamepad&) = delete;
  VirtualGamepad& operator=(const VirtualGamepad&) = delete;

  Result<void, InputError> start();
  Result<void, InputError> submit(const GamepadState& state);
  void set_rumble_callback(std::function<void(RumbleState)> callback);
  void stop();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
