#pragma once

#include "core/protocol/value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ministream {

struct FecLayout {
  std::uint16_t data_shards{};
  std::uint16_t parity_shards{};
  std::size_t shard_bytes{};
};

struct FecShard {
  std::uint16_t index{};
  std::uint16_t original_payload_bytes{};
  bool present{};
  std::vector<std::byte> bytes;
};

struct FecBlock {
  FecLayout layout;
  std::vector<FecShard> shards;
};

class FecCodec {
 public:
  FecBlock encode(std::span<const Datagram> data, std::uint16_t parity_shards) const;
  bool recover(FecBlock& block) const;
};

}  // namespace ministream
