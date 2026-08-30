#include "core/audio/jitter_buffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace ministream;

namespace {
AudioPacket packet(std::uint32_t sequence) {
  return AudioPacket{sequence, sequence * 10'000ULL, 480, {std::byte{1}}};
}
}  // namespace

TEST_CASE("audio jitter buffer returns reordered packet by expected sequence") {
  AudioJitterBuffer buffer;
  buffer.push(packet(2));
  buffer.push(packet(1));
  const auto first = buffer.pop(1);
  REQUIRE(first.kind == AudioPlayoutKind::Packet);
  REQUIRE(first.packet->sequence == 1);
}

TEST_CASE("audio jitter buffer requests PLC instead of waiting for a missing packet") {
  AudioJitterBuffer buffer;
  buffer.push(packet(2));
  auto expected = 1U;
  const auto missing = buffer.pop(expected);
  REQUIRE(missing.kind == AudioPlayoutKind::Plc);
  REQUIRE_FALSE(missing.packet);
  ++expected;
  const auto first_after_loss = buffer.pop(expected);
  REQUIRE(first_after_loss.kind == AudioPlayoutKind::Packet);
  REQUIRE(first_after_loss.packet->sequence == expected);
  ++expected;
  REQUIRE(buffer.pop(expected).kind == AudioPlayoutKind::Plc);
}

TEST_CASE("audio jitter buffer never grows beyond twenty milliseconds") {
  AudioJitterBuffer buffer({10ms, 20ms});
  buffer.push(packet(1));
  buffer.push(packet(2));
  buffer.push(packet(3));
  REQUIRE(buffer.buffered_duration() == 20ms);
  REQUIRE(buffer.pop(1).kind == AudioPlayoutKind::Plc);
  REQUIRE(buffer.pop(2).kind == AudioPlayoutKind::Packet);
}
