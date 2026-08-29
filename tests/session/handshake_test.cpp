#include "core/session/handshake.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace ministream;

TEST_CASE("handshake messages have deterministic validated wire formats") {
  const Hello hello{VideoCodec::Hevc, 3840, 2160, 60, 50'000'000, 0x0102030405060708ULL};
  auto encoded_hello = encode_hello(hello);
  REQUIRE(decode_hello(encoded_hello) == hello);

  const Accept accept{42, VideoCodec::Hevc, 3840, 2160, 60, 50'000'000, hello.nonce};
  const auto encoded_accept = encode_accept(accept);
  REQUIRE(decode_accept(encoded_accept) == accept);

  encoded_hello[0] = std::byte{0xFF};
  REQUIRE_FALSE(decode_hello(encoded_hello));
}

TEST_CASE("handshake retries HELLO every 250 ms and validates the nonce") {
  const Hello hello{VideoCodec::H264, 1920, 1080, 60, 20'000'000, 77};
  HandshakeRetrier retrier(hello);
  const auto start = SteadyClock::time_point{};
  REQUIRE(retrier.next_hello(start));
  REQUIRE_FALSE(retrier.next_hello(start + 249ms));
  REQUIRE(retrier.next_hello(start + 250ms));

  REQUIRE_FALSE(retrier.accept({1, VideoCodec::H264, 1920, 1080, 60, 20'000'000, 78}));
  REQUIRE(retrier.accept({1, VideoCodec::H264, 1920, 1080, 60, 20'000'000, 77}));
  REQUIRE_FALSE(retrier.next_hello(start + 1s));
}
