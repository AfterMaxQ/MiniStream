#include "core/session/capabilities.hpp"
#include "core/session/session_state.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("session lifecycle accepts only explicit transitions") {
  REQUIRE(can_transition(SessionState::Idle, SessionState::Connecting));
  REQUIRE(can_transition(SessionState::Connecting, SessionState::Negotiating));
  REQUIRE(can_transition(SessionState::Negotiating, SessionState::Streaming));
  REQUIRE(can_transition(SessionState::Streaming, SessionState::Recovering));
  REQUIRE(can_transition(SessionState::Recovering, SessionState::Streaming));
  REQUIRE(can_transition(SessionState::Streaming, SessionState::Disconnecting));
  REQUIRE(can_transition(SessionState::Disconnecting, SessionState::Idle));
  REQUIRE(can_transition(SessionState::Failed, SessionState::Idle));

  REQUIRE_FALSE(can_transition(SessionState::Idle, SessionState::Streaming));
  REQUIRE_FALSE(can_transition(SessionState::Negotiating, SessionState::Idle));
  REQUIRE_FALSE(can_transition(SessionState::Failed, SessionState::Streaming));
}

TEST_CASE("active session states may fail explicitly") {
  REQUIRE(can_transition(SessionState::Connecting, SessionState::Failed));
  REQUIRE(can_transition(SessionState::Negotiating, SessionState::Failed));
  REQUIRE(can_transition(SessionState::Streaming, SessionState::Failed));
  REQUIRE(can_transition(SessionState::Recovering, SessionState::Failed));
  REQUIRE(can_transition(SessionState::Disconnecting, SessionState::Failed));
}

TEST_CASE("device capabilities describe negotiated limits") {
  DeviceCapabilities capabilities{true, true, true, true, 3840, 2160, 60};
  REQUIRE(capabilities.hevc);
  REQUIRE(capabilities.hdr10);
  REQUIRE(capabilities.max_width == 3840);
}
