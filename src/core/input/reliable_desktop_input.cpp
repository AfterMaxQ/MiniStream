#include "core/input/reliable_desktop_input.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace ministream {
namespace {

void put_u32(std::span<std::byte, 4> output, std::uint32_t value) {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<std::byte>(value >> ((3U - index) * 8U));
  }
}

std::uint32_t get_u32(std::span<const std::byte, 4> input) {
  std::uint32_t value{};
  for (const auto byte : input) {
    value = (value << 8U) | std::to_integer<std::uint32_t>(byte);
  }
  return value;
}

}  // namespace

bool is_reliable_desktop_input(const DesktopInput& input) noexcept {
  return input.kind == DesktopInputKind::Key ||
         input.kind == DesktopInputKind::MouseButton ||
         input.kind == DesktopInputKind::ReleaseAll;
}

std::vector<std::byte> encode_reliable_desktop_input(const ReliableDesktopInput& input) {
  if (!is_reliable_desktop_input(input.input)) {
    return {};
  }
  const auto encoded_input = encode_desktop_input(input.input);
  if (encoded_input.empty()) {
    return {};
  }
  std::vector<std::byte> bytes(kReliableDesktopInputBytes);
  put_u32(std::span<std::byte, 4>{bytes.data(), 4}, input.sequence);
  std::copy(encoded_input.begin(), encoded_input.end(), bytes.begin() + 4);
  return bytes;
}

std::optional<ReliableDesktopInput> decode_reliable_desktop_input(
    std::span<const std::byte> bytes) {
  if (bytes.size() != kReliableDesktopInputBytes) {
    return std::nullopt;
  }
  const auto input = decode_desktop_input(bytes.subspan(4));
  if (!input || !is_reliable_desktop_input(*input)) {
    return std::nullopt;
  }
  return ReliableDesktopInput{
      get_u32(std::span<const std::byte, 4>{bytes.data(), 4}), *input};
}

ReliableDesktopInputReceiver::ReliableDesktopInputReceiver(Injector injector)
    : injector_(std::move(injector)) {}

std::vector<ControlSeq> ReliableDesktopInputReceiver::receive(
    const ReliableDesktopInput& input) {
  if (!is_reliable_desktop_input(input.input)) {
    return {};
  }
  const auto distance = static_cast<std::int32_t>(input.sequence - next_sequence_);
  if (distance < 0) {
    return {input.sequence};
  }
  if (static_cast<std::size_t>(distance) >= kMaxPending) {
    return {};
  }
  // A missing key-down must never prevent an emergency release. Late packets
  // before this barrier are acknowledged, but can no longer press a key again.
  if (input.input.kind == DesktopInputKind::ReleaseAll) {
    if (!injector_ || !injector_(input.input)) return {};
    std::vector<ControlSeq> acknowledgements{input.sequence};
    for (auto iterator = pending_.begin(); iterator != pending_.end();) {
      if (static_cast<std::int32_t>(iterator->first - input.sequence) <= 0) {
        acknowledgements.push_back(iterator->first);
        iterator = pending_.erase(iterator);
      } else {
        ++iterator;
      }
    }
    next_sequence_ = input.sequence + 1;
    return acknowledgements;
  }
  pending_.try_emplace(input.sequence, input.input);

  std::vector<ControlSeq> acknowledgements;
  for (;;) {
    const auto iterator = pending_.find(next_sequence_);
    if (iterator == pending_.end()) {
      break;
    }
    if (!injector_ || !injector_(iterator->second)) {
      break;
    }
    acknowledgements.push_back(iterator->first);
    pending_.erase(iterator);
    ++next_sequence_;
  }
  return acknowledgements;
}

void ReliableDesktopInputReceiver::reset() noexcept {
  next_sequence_ = 1;
  pending_.clear();
}

std::size_t ReliableDesktopInputReceiver::pending() const noexcept {
  return pending_.size();
}

ControlSeq ReliableDesktopInputReceiver::next_sequence() const noexcept {
  return next_sequence_;
}

}  // namespace ministream
