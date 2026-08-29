#include "core/input/input_coalescer.hpp"

namespace ministream {

void InputCoalescer::update(GamepadState state, SteadyClock::time_point now) noexcept {
  if (!pending_.has_value()) {
    due_at_ = now + kWindow;
  }
  pending_ = state;
}

std::optional<GamepadPacket> InputCoalescer::flush_if_due(
    SteadyClock::time_point now) noexcept {
  if (!pending_.has_value() || now < due_at_) {
    return std::nullopt;
  }

  const auto timestamp = std::chrono::duration_cast<Microseconds>(
                             now.time_since_epoch())
                             .count();
  GamepadPacket packet{next_sequence_++, static_cast<std::uint64_t>(timestamp), *pending_};
  pending_.reset();
  return packet;
}

}  // namespace ministream
