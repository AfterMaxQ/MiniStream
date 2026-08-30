#pragma once

#include "core/input/desktop_input.hpp"
#include "core/protocol/value_types.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace ministream {

struct ReliableDesktopInput {
  ControlSeq sequence{};
  DesktopInput input;
  bool operator==(const ReliableDesktopInput&) const = default;
};

inline constexpr std::size_t kReliableDesktopInputBytes =
    sizeof(ControlSeq) + kDesktopInputBytes;

bool is_reliable_desktop_input(const DesktopInput& input) noexcept;
std::vector<std::byte> encode_reliable_desktop_input(const ReliableDesktopInput& input);
std::optional<ReliableDesktopInput> decode_reliable_desktop_input(
    std::span<const std::byte> bytes);

class ReliableDesktopInputReceiver {
 public:
  using Injector = std::function<bool(const DesktopInput&)>;

  explicit ReliableDesktopInputReceiver(Injector injector);
  std::vector<ControlSeq> receive(const ReliableDesktopInput& input);
  void reset() noexcept;
  [[nodiscard]] std::size_t pending() const noexcept;
  [[nodiscard]] ControlSeq next_sequence() const noexcept;

 private:
  static constexpr std::size_t kMaxPending = 64;
  Injector injector_;
  ControlSeq next_sequence_{1};
  std::map<ControlSeq, DesktopInput> pending_;
};

}  // namespace ministream
