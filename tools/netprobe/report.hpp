#pragma once

#include <cstdint>
#include <vector>

namespace ministream::netprobe {

struct Report {
  std::uint64_t sent{};
  std::uint64_t received{};
  std::uint64_t payload_bytes{};
  double elapsed_seconds{};
  std::vector<double> rtt_ms;

  void print() const;
};

}  // namespace ministream::netprobe
