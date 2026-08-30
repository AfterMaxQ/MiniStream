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

TEST_CASE("input acknowledgement carries a stable control sequence") {
  const auto bytes = encode_input_ack_control(0x01020304U);
  REQUIRE(bytes.size() == 5);
  REQUIRE(decode_input_ack_control(bytes) == 0x01020304U);

  auto wrong_kind = bytes;
  wrong_kind[0] = std::byte{0xFF};
  REQUIRE_FALSE(decode_input_ack_control(wrong_kind).has_value());
}
