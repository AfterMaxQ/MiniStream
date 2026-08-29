#include "core/transport/reassembler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

using namespace std::chrono_literals;
using namespace ministream;

namespace {

EncodedFrame make_frame(std::uint32_t id, std::size_t bytes) {
  EncodedFrame frame{id, static_cast<std::uint64_t>(id) * 1000U, id == 1, {}};
  frame.bytes.resize(bytes);
  for (std::size_t i = 0; i < bytes; ++i) {
    frame.bytes[i] = static_cast<std::byte>((i + id) % 251U);
  }
  return frame;
}

}  // namespace

TEST_CASE("frame reassembler accepts shuffled and duplicate shards") {
  const auto source = make_frame(1, kVideoShardPayloadBytes * 3 + 37);
  auto packets = packetize_video(source, 9);
  std::reverse(packets.begin(), packets.end());
  packets.insert(packets.begin() + 1, packets.front());

  FrameReassembler reassembler;
  std::optional<EncodedFrame> completed;
  const auto now = SteadyClock::time_point{};
  for (const auto& packet : packets) {
    if (auto frame = reassembler.push(packet, now)) {
      completed = std::move(frame);
    }
  }

  REQUIRE(completed.has_value());
  REQUIRE(completed->frame_id == source.frame_id);
  REQUIRE(completed->keyframe);
  REQUIRE(completed->bytes == source.bytes);
}

TEST_CASE("frame reassembler expires missing shards at its deadline") {
  const auto packets = packetize_video(make_frame(2, kVideoShardPayloadBytes * 2), 9);
  FrameReassembler reassembler({5ms, 2});
  const auto start = SteadyClock::time_point{};

  REQUIRE_FALSE(reassembler.push(packets.front(), start));
  REQUIRE(reassembler.expire(start + 4ms).empty());
  REQUIRE(reassembler.expire(start + 5ms) == std::vector<std::uint32_t>{2});
  REQUIRE_FALSE(reassembler.push(packets.back(), start + 6ms));
}

TEST_CASE("frame reassembler drops the oldest frame when bounded") {
  const auto frame1 = packetize_video(make_frame(10, kVideoShardPayloadBytes * 2), 9);
  const auto frame2 = packetize_video(make_frame(11, kVideoShardPayloadBytes * 2), 9);
  const auto frame3 = packetize_video(make_frame(12, kVideoShardPayloadBytes * 2), 9);
  FrameReassembler reassembler({50ms, 2});
  const auto now = SteadyClock::time_point{};

  REQUIRE_FALSE(reassembler.push(frame1.front(), now));
  REQUIRE_FALSE(reassembler.push(frame2.front(), now));
  REQUIRE_FALSE(reassembler.push(frame3.front(), now));
  const auto completed = reassembler.push(frame2.back(), now);
  REQUIRE(completed.has_value());
  REQUIRE(completed->frame_id == 11);
  REQUIRE_FALSE(reassembler.push(frame1.back(), now));
}
