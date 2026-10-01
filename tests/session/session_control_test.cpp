#include "core/session/session_control.hpp"
#include "core/session/clipboard_channel.hpp"

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

TEST_CASE("heartbeat control is distinct from every state-changing control") {
  const auto heartbeat = encode_heartbeat_control();
  REQUIRE(heartbeat.size() == 1);
  REQUIRE(is_heartbeat_control(heartbeat));
  REQUIRE_FALSE(is_disconnect_control(heartbeat));
  REQUIRE_FALSE(is_request_keyframe_control(heartbeat));
  REQUIRE_FALSE(decode_input_ack_control(heartbeat).has_value());
}

TEST_CASE("clipboard transfers bounded text once despite packet and acknowledgement loss") {
  for (const auto size : {std::size_t{0}, std::size_t{2000}, ClipboardChannel::kMaxTextBytes}) {
    ClipboardChannel sender, receiver;
    const std::string text(size, 'x');
    REQUIRE(sender.queue(text));
    REQUIRE_FALSE(sender.queue(std::string(ClipboardChannel::kMaxTextBytes + 1, 'x')));
    std::vector<std::string> delivered;
    bool drop_packet = true, drop_ack = true;
    auto now = SteadyClock::time_point{};
    auto deliver = [&](const std::string& value) { delivered.push_back(value); };
    ClipboardChannel::Sender ack = [&](auto bytes) {
      if (drop_ack) { drop_ack = false; return true; }
      REQUIRE(sender.receive(bytes, now, [](auto) { return true; }, {}));
      return true;
    };
    ClipboardChannel::Sender send = [&](auto bytes) {
      if (drop_packet) { drop_packet = false; return true; }
      REQUIRE(bytes.size() <= 1200);
      REQUIRE(receiver.receive(bytes, now, ack, deliver));
      return true;
    };
    for (unsigned i = 0; i < 100 && delivered.empty(); ++i) {
      sender.tick(now, send);
      now += std::chrono::milliseconds{100};
    }
    REQUIRE(delivered == std::vector<std::string>{text});
    for (unsigned i = 0; i < 5; ++i) {
      sender.tick(now, send);
      now += std::chrono::milliseconds{200};
    }
    REQUIRE(delivered.size() == 1);
  }
}
