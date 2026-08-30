#pragma once

#include "core/protocol/value_types.hpp"
#include "core/time/clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace ministream {

enum class Priority : std::size_t { Input = 0, Control = 1, Audio = 2, Video = 3, Telemetry = 4 };

class PacketScheduler {
 public:
  bool enqueue(Priority priority, Datagram datagram, SteadyClock::time_point deadline);
  std::optional<Datagram> next(SteadyClock::time_point now);
  std::vector<Datagram> drain(SteadyClock::time_point now, std::size_t max_packets);
  [[nodiscard]] Microseconds estimated_video_queue_delay() const;
  [[nodiscard]] std::uint64_t video_rate_bps() const noexcept;
  void set_video_rate(std::uint64_t bits_per_second);

 private:
  struct QueuedDatagram {
    Datagram datagram;
    SteadyClock::time_point deadline;
  };

  void discard_expired(SteadyClock::time_point now);
  void refill_video_tokens(SteadyClock::time_point now);
  [[nodiscard]] double max_video_tokens_bits() const noexcept;

  static constexpr std::array<std::size_t, 5> kQueueLimits{64, 64, 128, 512, 64};
  std::array<std::deque<QueuedDatagram>, 5> queues_;
  std::uint64_t video_rate_bps_{20'000'000};
  double video_tokens_bits_{};
  std::optional<SteadyClock::time_point> last_refill_;
};

}  // namespace ministream
