#include "core/fec/video_fec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <span>
#include <vector>

using namespace ministream;
using namespace std::chrono_literals;

TEST_CASE("video FEC counts unique received and recovered data shards") {
  EncodedFrame frame{201, 12'345, true,
                     std::vector<std::byte>(kVideoFecShardPayloadBytes * 4U + 3U)};
  for (std::size_t index = 0; index < frame.bytes.size(); ++index) {
    frame.bytes[index] = static_cast<std::byte>((index * 7U) & 0xFFU);
  }
  const auto generated = VideoFecEncoder{77}.encode_frame(frame, 0.2);
  REQUIRE(generated.video_datagrams.size() == 5);
  REQUIRE(generated.fec_datagrams.size() == 1);

  VideoFecReassembler reassembler;
  const auto now = SteadyClock::time_point{};
  const auto parity_payload = std::span<const std::byte>{generated.fec_datagrams.front().bytes}
                                  .subspan(kCommonHeaderBytes);
  REQUIRE_FALSE(reassembler.push_parity(parity_payload, now).has_value());

  std::optional<EncodedFrame> restored;
  for (std::size_t index = 0; index < generated.video_datagrams.size(); ++index) {
    if (index == 1) {
      continue;
    }
    const auto shard = parse_video_datagram(generated.video_datagrams[index]);
    REQUIRE(shard.has_value());
    restored = reassembler.push_data(*shard, now);
  }

  REQUIRE(restored.has_value());
  REQUIRE(restored->bytes == frame.bytes);
  REQUIRE(reassembler.received_data_shards() == 4);
  REQUIRE(reassembler.lost_data_shards() == 1);
  REQUIRE(reassembler.recovered_data_shards() == 1);
}

TEST_CASE("video FEC counts missing shards when an incomplete frame expires") {
  EncodedFrame frame{202, 12'346, false,
                     std::vector<std::byte>(kVideoFecShardPayloadBytes * 2U + 3U)};
  const auto generated = VideoFecEncoder{78}.encode_frame(frame, 0.2);
  REQUIRE(generated.video_datagrams.size() == 3);

  VideoFecReassembler reassembler;
  const auto now = SteadyClock::time_point{};
  for (std::size_t index : {std::size_t{0}, std::size_t{2}}) {
    const auto shard = parse_video_datagram(generated.video_datagrams[index]);
    REQUIRE(shard.has_value());
    REQUIRE_FALSE(reassembler.push_data(*shard, now).has_value());
  }
  REQUIRE(reassembler.received_data_shards() == 2);
  REQUIRE(reassembler.expire(now + 26ms).size() == 1);
  REQUIRE(reassembler.lost_data_shards() == 1);
}

TEST_CASE("video FEC does not recreate an expired frame from late shards") {
  EncodedFrame frame{203, 12'347, false,
                     std::vector<std::byte>(kVideoFecShardPayloadBytes * 2U + 3U)};
  const auto generated = VideoFecEncoder{79}.encode_frame(frame, 0.2);
  REQUIRE(generated.video_datagrams.size() == 3);
  REQUIRE(generated.fec_datagrams.size() == 1);

  VideoFecReassembler reassembler;
  const auto start = SteadyClock::time_point{};
  const auto first = parse_video_datagram(generated.video_datagrams.front());
  REQUIRE(first.has_value());
  REQUIRE_FALSE(reassembler.push_data(*first, start).has_value());

  const auto expired = reassembler.expire(start + 26ms);
  REQUIRE(expired.size() == 1);
  REQUIRE(reassembler.unrecoverable_frames() == 1);
  REQUIRE(reassembler.lost_data_shards() == 2);

  const auto late = parse_video_datagram(generated.video_datagrams[1]);
  REQUIRE(late.has_value());
  REQUIRE_FALSE(reassembler.push_data(*late, start + 30ms).has_value());
  REQUIRE(reassembler.received_data_shards() == 1);
  REQUIRE(reassembler.lost_data_shards() == 2);
}
