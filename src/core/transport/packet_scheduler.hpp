#pragma once

#include "core/protocol/value_types.hpp"
#include "core/time/clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace ministream {

enum class Priority : std::size_t { Input = 0, Control = 1, Audio = 2, Video = 3, Telemetry = 4 };

class PacketScheduler {
 public:
  bool enqueue(Priority priority, Datagram datagram, SteadyClock::time_point deadline);
  std::optional<Datagram> next(SteadyClock::time_point now);
  [[nodiscard]] Microseconds estimated_video_queue_delay() const;
  void set_video_rate(std::uint64_t bits_per_second);

 private:
  struct QueuedDatagram {
    Datagram datagram;
    SteadyClock::time_point deadline;
  };

  void discard_expired(SteadyClock::time_point now);
  void refill_video_tokens(SteadyClock::time_point now);

  static constexpr std::array<std::size_t, 5> kQueueLimits{64, 64, 128, 512, 64};
  std::array<std::deque<QueuedDatagram>, 5> queues_;
  std::uint64_t video_rate_bps_{20'000'000};
  double video_tokens_bits_{static_cast<double>(kMaxDatagramBytes * 16)};
  std::optional<SteadyClock::time_point> last_refill_;
};

}  // namespace ministream
