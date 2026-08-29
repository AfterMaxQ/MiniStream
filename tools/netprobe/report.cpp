#include "netprobe/report.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace ministream::netprobe {

void Report::print() const {
  auto sorted = rtt_ms;
  std::sort(sorted.begin(), sorted.end());
  const auto mean = sorted.empty()
                        ? 0.0
                        : std::accumulate(sorted.begin(), sorted.end(), 0.0) /
                              static_cast<double>(sorted.size());
  const auto p95 = sorted.empty()
                       ? 0.0
                       : sorted[static_cast<std::size_t>(0.95 * (sorted.size() - 1))];
  const auto loss = sent == 0 ? 0.0 : 100.0 * static_cast<double>(sent - received) /
                                          static_cast<double>(sent);
  const auto throughput = elapsed_seconds <= 0.0
                              ? 0.0
                              : static_cast<double>(payload_bytes) * 8.0 /
                                    elapsed_seconds / 1'000'000.0;
  std::cout << std::fixed << std::setprecision(2)
            << "sent=" << sent << " received=" << received << " loss=" << loss
            << "% rtt_mean=" << mean << "ms rtt_p95=" << p95
            << "ms throughput=" << throughput << "Mbps\n";
}

}  // namespace ministream::netprobe
