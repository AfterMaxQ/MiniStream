#pragma once

#include "core/time/clock.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ministream {

class ClipboardChannel {
 public:
  static constexpr std::size_t kMaxTextBytes = 256 * 1024;
  using Sender = std::function<bool(std::span<const std::byte>)>;
  using Receiver = std::function<void(const std::string&)>;
  bool queue(std::string text);
  void tick(SteadyClock::time_point now, const Sender& send);
  bool receive(std::span<const std::byte> bytes, SteadyClock::time_point now,
               const Sender& send, const Receiver& deliver);
  void reset();
  void cancelOutgoing() { outgoing_.reset(); }
 private:
  static constexpr std::size_t kChunk = 896;
  struct Outgoing {
    std::uint32_t id{};
    std::string text;
    std::vector<bool> acknowledged;
    std::vector<std::optional<SteadyClock::time_point>> sent;
    std::optional<SteadyClock::time_point> started;
  };
  struct Incoming {
    std::uint32_t id{};
    std::string text;
    std::vector<bool> received;
    SteadyClock::time_point started;
  };
  std::uint32_t next_id_{1};
  std::uint32_t last_incoming_id_{};
  std::optional<Outgoing> outgoing_;
  std::optional<Incoming> incoming_;
};

}  // namespace ministream
