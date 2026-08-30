#include "core/session/session_control.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("request-keyframe control is distinct from disconnect control") {
  const auto request = encode_request_keyframe_control();
  REQUIRE(request.size() == 1);
  REQUIRE(is_request_keyframe_control(request));
  REQUIRE_FALSE(is_disconnect_control(request));
  REQUIRE_FALSE(is_request_keyframe_control(encode_disconnect_control()));
}
