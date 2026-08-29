#include "core/transport/reliable_control.hpp"

#include <algorithm>
#include <array>
#include <chrono>

namespace ministream {
namespace {
constexpr std::array<Microseconds, 3> kRetryDelays{
    std::chrono::milliseconds{20}, std::chrono::milliseconds{40},
    std::chrono::milliseconds{80}};
}

std::optional<ControlSeq> ReliableControl::send(
    ControlMessage message, SteadyClock::time_point now) {
  if (pending_.size() == kMaxPending) {
    return std::nullopt;
  }
  const auto sequence = next_sequence_++;
  message.sequence = sequence;
  pending_.emplace(sequence, Pending{std::move(message), 0, now + kRetryDelays[0]});
  return sequence;
}

void ReliableControl::acknowledge(ControlSeq sequence) { pending_.erase(sequence); }

std::vector<ControlMessage> ReliableControl::due_retries(SteadyClock::time_point now) {
  std::vector<ControlMessage> retries;
  for (auto iterator = pending_.begin(); iterator != pending_.end();) {
    auto& pending = iterator->second;
    if (now < pending.due) {
      ++iterator;
      continue;
    }
    if (pending.retries == kRetryDelays.size()) {
      failures_.push_back(iterator->first);
      iterator = pending_.erase(iterator);
      continue;
    }
    retries.push_back(pending.message);
    ++pending.retries;
    const auto delay_index = std::min(pending.retries, kRetryDelays.size() - 1);
    pending.due += kRetryDelays[delay_index];
    ++iterator;
  }
  return retries;
}

std::vector<ControlSeq> ReliableControl::take_failures() {
  auto failures = std::move(failures_);
  failures_.clear();
  return failures;
}

std::size_t ReliableControl::pending() const noexcept { return pending_.size(); }

}  // namespace ministream
