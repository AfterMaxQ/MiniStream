#include "core/protocol/wire.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

using namespace ministream;

TEST_CASE("common header has a stable big-endian representation") {
  const CommonHeader header{PacketType::Video, 0x01020304U, 0x0406U};
  const auto encoded = encode_common_header(header);
  const std::array<std::byte, 12> expected{
      std::byte{0x4D}, std::byte{0x53}, std::byte{0x54}, std::byte{0x52},
      std::byte{0x02}, std::byte{0x02}, std::byte{0x01}, std::byte{0x02},
      std::byte{0x03}, std::byte{0x04}, std::byte{0x04}, std::byte{0x06}};

  REQUIRE(encoded == expected);
  const auto decoded = decode_common_header(encoded);
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->type == PacketType::Video);
  REQUIRE(decoded->session_id == 0x01020304U);
  REQUIRE(decoded->payload_bytes == 0x0406U);
}

TEST_CASE("common header rejects malformed routing data") {
  auto bytes = encode_common_header({PacketType::Control, 7U, 4U});

  SECTION("bad magic") {
    bytes[0] = std::byte{0};
    REQUIRE_FALSE(decode_common_header(bytes));
  }
  SECTION("bad version") {
    bytes[4] = std::byte{1};
    REQUIRE_FALSE(decode_common_header(bytes));
  }
  SECTION("unknown packet type") {
    bytes[5] = std::byte{0xFF};
    REQUIRE_FALSE(decode_common_header(bytes));
  }
  SECTION("undersized buffer") {
    REQUIRE_FALSE(decode_common_header(std::span<const std::byte>{bytes}.first(11)));
  }
  SECTION("payload exceeds datagram cap") {
    bytes[10] = std::byte{0x05};
    bytes[11] = std::byte{0x00};
    REQUIRE_FALSE(decode_common_header(bytes));
  }
}
