#include "core/audio/audio_packet.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("audio packet preserves sequence, time, samples, and Opus payload") {
  AudioPacket source{0xFFFFFFFFU, 123456789U, 480, {std::byte{1}, std::byte{2}, std::byte{3}}};
  const auto bytes = encode_audio_packet(source);
  const auto decoded = decode_audio_packet(bytes);
  REQUIRE(decoded == source);
  REQUIRE(sequence_is_newer(0U, 0xFFFFFFFFU));
  REQUIRE_FALSE(sequence_is_newer(0xFFFFFFFFU, 0U));
}

TEST_CASE("audio packet rejects malformed payload length") {
  auto bytes = encode_audio_packet({1, 2, 480, {std::byte{1}, std::byte{2}}});
  bytes[15] = std::byte{3};
  REQUIRE_FALSE(decode_audio_packet(bytes));
  bytes.resize(kMaxDatagramBytes + 1);
  REQUIRE_FALSE(decode_audio_packet(bytes));
}
