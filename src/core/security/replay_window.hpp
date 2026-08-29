#pragma once

#include <bitset>
#include <cstdint>
#include <optional>

namespace ministream {

class ReplayWindow {
 public:
  [[nodiscard]] bool would_accept(std::uint64_t counter) const noexcept {
    if (!highest_ || counter > *highest_) {
      return true;
    }
    const auto distance = *highest_ - counter;
    return distance < seen_.size() && !seen_.test(static_cast<std::size_t>(distance));
  }

  void commit(std::uint64_t counter) noexcept {
    if (!highest_) {
      highest_ = counter;
      seen_.set(0);
      return;
    }
    if (counter > *highest_) {
      const auto distance = counter - *highest_;
      if (distance >= seen_.size()) {
        seen_.reset();
      } else {
        seen_ <<= static_cast<std::size_t>(distance);
      }
      highest_ = counter;
      seen_.set(0);
      return;
    }
    seen_.set(static_cast<std::size_t>(*highest_ - counter));
  }

 private:
  std::optional<std::uint64_t> highest_;
  std::bitset<1024> seen_;
};

}  // namespace ministream
