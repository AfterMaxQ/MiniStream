#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ministream {

using SessionId = std::uint32_t;
using ControlSeq = std::uint32_t;

inline constexpr std::size_t kMaxDatagramBytes = 1200;

struct Datagram {
  std::vector<std::byte> bytes;
};

}  // namespace ministream
