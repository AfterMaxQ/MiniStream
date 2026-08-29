#pragma once

#include "core/time/clock.hpp"
#include "core/transport/packetizer.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ministream {

struct ReassemblyConfig {
  Microseconds deadline{5000};
  std::size_t max_incomplete_frames{2};
};

class FrameReassembler {
 public:
  explicit FrameReassembler(ReassemblyConfig config = {});

  std::optional<EncodedFrame> push(const Datagram& datagram, SteadyClock::time_point now);
  std::vector<std::uint32_t> expire(SteadyClock::time_point now);

 private:
  struct IncompleteFrame {
    std::uint64_t capture_timestamp_us{};
    bool keyframe{};
    SteadyClock::time_point deadline;
    std::vector<std::optional<std::vector<std::byte>>> shards;
    std::size_t received{};
  };

  void erase_frame(std::uint32_t frame_id);

  ReassemblyConfig config_;
  std::unordered_map<std::uint32_t, IncompleteFrame> frames_;
  std::deque<std::uint32_t> insertion_order_;
};

}  // namespace ministream
