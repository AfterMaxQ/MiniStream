#pragma once

#include <cstdint>

namespace ministream {

struct MediaHeader {
  std::uint32_t packet_seq{};
  std::uint32_t frame_id{};
  std::uint16_t shard_index{};
  std::uint16_t shard_count{};
  std::uint64_t capture_timestamp_us{};
};

}  // namespace ministream
