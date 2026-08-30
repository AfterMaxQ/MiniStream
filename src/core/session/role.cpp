#include "core/session/role.hpp"

namespace ministream {
namespace {

bool is_active(RoleState state) noexcept {
  return state != RoleState::Idle;
}

}  // namespace

bool valid_role_transition(RoleState from, RoleState to) noexcept {
  if (from == to) {
    return true;
  }
  if (to == RoleState::Idle) {
    return is_active(from);
  }

  switch (from) {
    case RoleState::Idle:
      return to == RoleState::ControlledReady || to == RoleState::RemoteBrowsing;
    case RoleState::ControlledReady:
      return to == RoleState::Broadcasting;
    case RoleState::Broadcasting:
    case RoleState::RemoteBrowsing:
      return to == RoleState::Pairing;
    case RoleState::Pairing:
      return to == RoleState::Streaming;
    case RoleState::Streaming:
      return false;
  }
  return false;
}

}  // namespace ministream
