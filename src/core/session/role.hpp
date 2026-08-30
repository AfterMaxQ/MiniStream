#pragma once

#include <cstdint>

namespace ministream {

enum class RoleMode : std::uint8_t { Controlled = 1, Remote = 2 };

enum class RoleState : std::uint8_t {
  Idle,
  ControlledReady,
  Broadcasting,
  RemoteBrowsing,
  Pairing,
  Streaming,
};

[[nodiscard]] bool valid_role_transition(RoleState from, RoleState to) noexcept;

}  // namespace ministream
