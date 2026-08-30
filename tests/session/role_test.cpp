#include "core/session/role.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("role state machine allows each session direction") {
  REQUIRE(valid_role_transition(RoleState::Idle, RoleState::ControlledReady));
  REQUIRE(valid_role_transition(RoleState::ControlledReady, RoleState::Broadcasting));
  REQUIRE(valid_role_transition(RoleState::Broadcasting, RoleState::Pairing));
  REQUIRE(valid_role_transition(RoleState::Idle, RoleState::RemoteBrowsing));
  REQUIRE(valid_role_transition(RoleState::RemoteBrowsing, RoleState::Pairing));
  REQUIRE(valid_role_transition(RoleState::Pairing, RoleState::Streaming));
}

TEST_CASE("role state machine requires cleanup before changing direction") {
  REQUIRE_FALSE(valid_role_transition(RoleState::Idle, RoleState::Streaming));
  REQUIRE_FALSE(valid_role_transition(RoleState::Streaming, RoleState::Broadcasting));
  REQUIRE(valid_role_transition(RoleState::Streaming, RoleState::Idle));
  REQUIRE(valid_role_transition(RoleState::Broadcasting, RoleState::Idle));
  REQUIRE(valid_role_transition(RoleState::Idle, RoleState::Idle));
}
