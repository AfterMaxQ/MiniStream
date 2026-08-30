#pragma once

#include "core/protocol/value_types.hpp"
#include "core/time/clock.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace ministream {

enum class ControlKind : std::uint8_t {
  Start,
  Stop,
  RequestIdr,
  CodecConfig,
  BitrateUpdate,
  Ack,
  Input,
};

struct ControlMessage {
  ControlSeq sequence{};
  ControlKind kind{ControlKind::Start};
  std::vector<std::byte> payload;
};

class ReliableControl {
 public:
  std::optional<ControlSeq> send(ControlMessage message, SteadyClock::time_point now);
  void acknowledge(ControlSeq sequence);
  std::vector<ControlMessage> due_retries(SteadyClock::time_point now);
  std::vector<ControlSeq> take_failures();
  [[nodiscard]] std::size_t pending() const noexcept;

 private:
  struct Pending {
    ControlMessage message;
    std::size_t retries{};
    SteadyClock::time_point due;
  };

  static constexpr std::size_t kMaxPending = 64;
  ControlSeq next_sequence_{1};
  std::map<ControlSeq, Pending> pending_;
  std::vector<ControlSeq> failures_;
};

}  // namespace ministream
