#include "macos/input/sdl_gamepad.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace ministream {

struct SdlGamepad::Impl {
  SDL_Gamepad* gamepad{};
};

SdlGamepad::SdlGamepad() : impl_(std::make_unique<Impl>()) {
  SDL_InitSubSystem(SDL_INIT_GAMEPAD);
}

SdlGamepad::~SdlGamepad() {
  if (impl_->gamepad != nullptr) {
    SDL_CloseGamepad(impl_->gamepad);
  }
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

SdlGamepad::SdlGamepad(SdlGamepad&&) noexcept = default;
SdlGamepad& SdlGamepad::operator=(SdlGamepad&&) noexcept = default;

std::optional<GamepadState> SdlGamepad::poll_latest() {
  SDL_PumpEvents();
  if (impl_->gamepad == nullptr) {
    int count{};
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids == nullptr || count == 0) {
      SDL_free(ids);
      return std::nullopt;
    }
    impl_->gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
  }
  if (impl_->gamepad == nullptr) {
    return std::nullopt;
  }

  auto pressed = [&](SDL_GamepadButton button) {
    return SDL_GetGamepadButton(impl_->gamepad, button) ? 1U : 0U;
  };
  std::uint32_t buttons{};
  buttons |= pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) ? kGamepadDpadUp : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? kGamepadDpadDown : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? kGamepadDpadLeft : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? kGamepadDpadRight : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_START) ? kGamepadStart : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_BACK) ? kGamepadBack : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_LEFT_STICK) ? kGamepadLeftThumb : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_RIGHT_STICK) ? kGamepadRightThumb : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ? kGamepadLeftShoulder : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ? kGamepadRightShoulder : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_GUIDE) ? kGamepadGuide : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_SOUTH) ? kGamepadA : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_EAST) ? kGamepadB : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_WEST) ? kGamepadX : 0U;
  buttons |= pressed(SDL_GAMEPAD_BUTTON_NORTH) ? kGamepadY : 0U;
  return GamepadState{
      buttons,
      static_cast<std::uint16_t>(SDL_GetGamepadAxis(impl_->gamepad,
                                                    SDL_GAMEPAD_AXIS_LEFT_TRIGGER)),
      static_cast<std::uint16_t>(SDL_GetGamepadAxis(impl_->gamepad,
                                                    SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)),
      SDL_GetGamepadAxis(impl_->gamepad, SDL_GAMEPAD_AXIS_LEFTX),
      SDL_GetGamepadAxis(impl_->gamepad, SDL_GAMEPAD_AXIS_LEFTY),
      SDL_GetGamepadAxis(impl_->gamepad, SDL_GAMEPAD_AXIS_RIGHTX),
      SDL_GetGamepadAxis(impl_->gamepad, SDL_GAMEPAD_AXIS_RIGHTY)};
}

bool SdlGamepad::rumble(std::uint16_t low, std::uint16_t high, Microseconds duration) {
  if (impl_->gamepad == nullptr) {
    return false;
  }
  const auto milliseconds = std::clamp<std::int64_t>(duration.count() / 1000, 0, 60'000);
  return SDL_RumbleGamepad(impl_->gamepad, low, high,
                           static_cast<std::uint32_t>(milliseconds));
}

}  // namespace ministream
