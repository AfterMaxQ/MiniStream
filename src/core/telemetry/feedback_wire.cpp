#include "core/telemetry/feedback_wire.hpp"

#include <algorithm>

namespace ministream {
namespace {

void put_u32(std::span<std::byte> output, std::uint32_t value) {
  output[0] = static_cast<std::byte>(value >> 24U);
  output[1] = static_cast<std::byte>(value >> 16U);
  output[2] = static_cast<std::byte>(value >> 8U);
  output[3] = static_cast<std::byte>(value);
}

void put_u64(std::span<std::byte> output, std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    output[index] = static_cast<std::byte>(value >> ((7U - index) * 8U));
  }
}

std::uint32_t get_u32(std::span<const std::byte> input) {
  return (std::to_integer<std::uint32_t>(input[0]) << 24U) |
         (std::to_integer<std::uint32_t>(input[1]) << 16U) |
         (std::to_integer<std::uint32_t>(input[2]) << 8U) |
         std::to_integer<std::uint32_t>(input[3]);
}

std::uint64_t get_u64(std::span<const std::byte> input) {
  std::uint64_t value = 0;
  for (const auto byte : input.first<8>()) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}

}  // namespace

std::vector<std::byte> encode_feedback_report(const FeedbackReport& report) {
  if (report.received_video_packets > kMaxFeedbackPacketCount ||
      report.lost_video_packets > kMaxFeedbackPacketCount ||
      report.jitter_us > kMaxFeedbackJitterUs ||
      report.fec_recovered > kMaxFeedbackPacketCount ||
      report.fec_unrecoverable > kMaxFeedbackPacketCount) {
    return {};
  }
  std::vector<std::byte> bytes(kFeedbackReportBytes);
  bytes[0] = static_cast<std::byte>(kFeedbackReportVersion);
  bytes[1] = static_cast<std::byte>(kFeedbackReportKind);
  put_u32(std::span{bytes}.subspan(2, 4), report.report_sequence);
  put_u64(std::span{bytes}.subspan(6, 8), report.receiver_timestamp_us);
  put_u64(std::span{bytes}.subspan(14, 8), report.sender_timestamp_us);
  put_u32(std::span{bytes}.subspan(22, 4), report.received_video_packets);
  put_u32(std::span{bytes}.subspan(26, 4), report.lost_video_packets);
  put_u32(std::span{bytes}.subspan(30, 4), report.jitter_us);
  put_u64(std::span{bytes}.subspan(34, 8), report.fec_recovered);
  put_u64(std::span{bytes}.subspan(42, 8), report.fec_unrecoverable);
  return bytes;
}

std::optional<FeedbackReport> decode_feedback_report(
    std::span<const std::byte> bytes, std::optional<std::uint32_t> expected_sequence) {
  if (bytes.size() != kFeedbackReportBytes ||
      std::to_integer<std::uint8_t>(bytes[0]) != kFeedbackReportVersion ||
      std::to_integer<std::uint8_t>(bytes[1]) != kFeedbackReportKind) {
    return std::nullopt;
  }
  FeedbackReport report;
  report.report_sequence = get_u32(bytes.subspan(2, 4));
  if (expected_sequence && report.report_sequence != *expected_sequence) {
    return std::nullopt;
  }
  report.receiver_timestamp_us = get_u64(bytes.subspan(6, 8));
  report.sender_timestamp_us = get_u64(bytes.subspan(14, 8));
  report.received_video_packets = get_u32(bytes.subspan(22, 4));
  report.lost_video_packets = get_u32(bytes.subspan(26, 4));
  report.jitter_us = get_u32(bytes.subspan(30, 4));
  report.fec_recovered = get_u64(bytes.subspan(34, 8));
  report.fec_unrecoverable = get_u64(bytes.subspan(42, 8));
  if (report.received_video_packets > kMaxFeedbackPacketCount ||
      report.lost_video_packets > kMaxFeedbackPacketCount ||
      report.jitter_us > kMaxFeedbackJitterUs ||
      report.fec_recovered > kMaxFeedbackPacketCount ||
      report.fec_unrecoverable > kMaxFeedbackPacketCount) {
    return std::nullopt;
  }
  return report;
}

FeedbackDelta feedback_delta(const FeedbackReport& current,
                             std::optional<FeedbackReport> previous) {
  const auto subtract_counter = [](std::uint64_t current_value,
                                   std::uint64_t previous_value) {
    if (current_value >= previous_value) {
      return current_value - previous_value;
    }
    // Reports use a bounded counter so they remain inside the fixed wire
    // format.  A decrease from the high end is a wrap; a decrease elsewhere
    // is treated as a receiver reset.
    const auto wrap_threshold =
        (static_cast<std::uint64_t>(kMaxFeedbackPacketCount) * 3U) / 4U;
    return previous_value >= wrap_threshold
               ? kFeedbackCounterModulus - previous_value + current_value
               : current_value;
  };
  const auto previous_received = previous ? previous->received_video_packets : 0U;
  const auto previous_lost = previous ? previous->lost_video_packets : 0U;
  const auto previous_unrecoverable = previous ? previous->fec_unrecoverable : 0U;
  return {subtract_counter(current.received_video_packets, previous_received),
          subtract_counter(current.lost_video_packets, previous_lost),
          subtract_counter(current.fec_unrecoverable, previous_unrecoverable)};
}

}  // namespace ministream
