#include "core/transport/packetizer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace ministream;

TEST_CASE("video packetizer produces bounded, reversible shards") {
  EncodedFrame frame;
  frame.frame_id = 91;
  frame.capture_timestamp_us = 123456;
  frame.keyframe = true;
  frame.bytes.resize(100 * 1024);
  for (std::size_t i = 0; i < frame.bytes.size(); ++i) {
    frame.bytes[i] = static_cast<std::byte>(i % 251U);
  }

  const auto datagrams = packetize_video(frame, 17);
  REQUIRE(datagrams.size() > 1);
  REQUIRE(std::ranges::all_of(datagrams, [](const Datagram& datagram) {
    return datagram.bytes.size() <= kMaxDatagramBytes;
  }));

  std::vector<std::byte> restored;
  for (const auto& datagram : datagrams) {
    const auto shard = parse_video_datagram(datagram);
    REQUIRE(shard.has_value());
    REQUIRE(shard->session_id == 17);
    REQUIRE(shard->header.frame_id == 91);
    REQUIRE(shard->header.shard_count == datagrams.size());
    REQUIRE(shard->keyframe);
    restored.insert(restored.end(), shard->payload.begin(), shard->payload.end());
  }
  REQUIRE(restored == frame.bytes);
}

TEST_CASE("video packetizer rejects empty and unrepresentable frames") {
  EncodedFrame empty{1, 2, false, {}};
  REQUIRE(packetize_video(empty, 1).empty());

  EncodedFrame too_large{2, 3, false, {}};
  too_large.bytes.resize(kVideoShardPayloadBytes * (kMaxVideoShards + 1ULL));
  REQUIRE(packetize_video(too_large, 1).empty());
}
