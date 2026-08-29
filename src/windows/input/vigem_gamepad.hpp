#pragma once

#include "windows/input/virtual_gamepad.hpp"

#include <functional>
#include <memory>

namespace ministream {

class VigemGamepad {
 public:
  VigemGamepad();
  ~VigemGamepad();
  VigemGamepad(VigemGamepad&&) noexcept;
  VigemGamepad& operator=(VigemGamepad&&) noexcept;
  VigemGamepad(const VigemGamepad&) = delete;
  VigemGamepad& operator=(const VigemGamepad&) = delete;

  Result<void, InputError> start();
  Result<void, InputError> submit(const GamepadState& state);
  void set_rumble_callback(std::function<void(RumbleState)> callback);
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
