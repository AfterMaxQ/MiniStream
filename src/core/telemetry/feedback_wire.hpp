#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ministream {

inline constexpr std::size_t kFeedbackReportBytes = 50;
inline constexpr std::uint8_t kFeedbackReportVersion = 1;
inline constexpr std::uint8_t kFeedbackReportKind = 1;
inline constexpr std::uint32_t kMaxFeedbackPacketCount = 1'000'000;
inline constexpr std::uint64_t kFeedbackCounterModulus =
    static_cast<std::uint64_t>(kMaxFeedbackPacketCount) + 1U;
inline constexpr std::uint32_t kMaxFeedbackJitterUs = 1'000'000;

struct FeedbackReport {
  std::uint32_t report_sequence{};
  std::uint64_t receiver_timestamp_us{};
  std::uint64_t sender_timestamp_us{};
  std::uint32_t received_video_packets{};
  std::uint32_t lost_video_packets{};
  std::uint32_t jitter_us{};
  std::uint64_t fec_recovered{};
  std::uint64_t fec_unrecoverable{};

  friend bool operator==(const FeedbackReport&, const FeedbackReport&) = default;
};

struct FeedbackDelta {
  std::uint64_t received_video_packets{};
  std::uint64_t lost_video_packets{};
  std::uint64_t fec_unrecoverable{};
};

std::vector<std::byte> encode_feedback_report(const FeedbackReport& report);
std::optional<FeedbackReport> decode_feedback_report(
    std::span<const std::byte> bytes,
    std::optional<std::uint32_t> expected_sequence = std::nullopt);
FeedbackDelta feedback_delta(const FeedbackReport& current,
                             std::optional<FeedbackReport> previous);

}  // namespace ministream
