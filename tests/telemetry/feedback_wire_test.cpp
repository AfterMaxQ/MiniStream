#include "core/input/rumble_packet.hpp"
#include "core/telemetry/feedback_wire.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("feedback wire round trips bounded receiver counters") {
  const FeedbackReport report{7, 1000, 2000, 99, 3, 1200, 4, 1};
  REQUIRE(decode_feedback_report(encode_feedback_report(report)) == report);
  REQUIRE(decode_feedback_report(encode_feedback_report(report), 7) == report);
  REQUIRE_FALSE(decode_feedback_report(encode_feedback_report(report), 8));
}

TEST_CASE("feedback wire rejects unknown kind truncated and oversized reports") {
  const FeedbackReport report{7, 1000, 2000, 99, 3, 1200, 4, 1};
  auto bytes = encode_feedback_report(report);
  bytes[1] = std::byte{99};
  REQUIRE_FALSE(decode_feedback_report(bytes));
  bytes = encode_feedback_report(report);
  bytes.pop_back();
  REQUIRE_FALSE(decode_feedback_report(bytes));

  auto oversized = report;
  oversized.received_video_packets = kMaxFeedbackPacketCount + 1U;
  REQUIRE(encode_feedback_report(oversized).empty());
}

TEST_CASE("feedback wire remains distinct from six byte rumble payload") {
  const RumblePacket rumble{100, 200, 300};
  const auto bytes = encode_rumble_packet(rumble);
  REQUIRE(decode_rumble_packet(bytes) == rumble);
  REQUIRE_FALSE(decode_feedback_report(bytes));
}

TEST_CASE("feedback counters are consumed as per-report deltas") {
  const FeedbackReport previous{7, 1000, 2000, 100, 4, 1200, 9, 3};
  const FeedbackReport current{8, 1100, 2100, 125, 7, 1300, 12, 5};
  const auto delta = feedback_delta(current, previous);
  REQUIRE(delta.received_video_packets == 25);
  REQUIRE(delta.lost_video_packets == 3);
  REQUIRE(delta.fec_unrecoverable == 2);

  const FeedbackReport reset{9, 1200, 2200, 2, 1, 800, 1, 0};
  const auto after_reset = feedback_delta(reset, current);
  REQUIRE(after_reset.received_video_packets == 2);
  REQUIRE(after_reset.lost_video_packets == 1);
  REQUIRE(after_reset.fec_unrecoverable == 0);
}

TEST_CASE("feedback counters preserve deltas across bounded counter wrap") {
  const FeedbackReport previous{7, 1000, 2000, 999'999, 999'998, 1200, 999'999, 999'999};
  const FeedbackReport current{8, 1100, 2100, 998, 1, 1300, 0, 2};
  const auto delta = feedback_delta(current, previous);
  REQUIRE(delta.received_video_packets == 1'000);
  REQUIRE(delta.lost_video_packets == 4);
  REQUIRE(delta.fec_unrecoverable == 4);
}
