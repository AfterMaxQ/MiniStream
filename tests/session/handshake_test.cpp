#include "core/session/handshake.hpp"
#include "core/session/session_timing.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace ministream;

TEST_CASE("handshake messages have deterministic validated wire formats") {
  const Hello hello{HandshakeRole::Controller, VideoCodec::Hevc, true, 3840, 2160, 60,
                    50'000'000, 0x0102030405060708ULL};
  auto encoded_hello = encode_hello(hello);
  REQUIRE(encoded_hello.size() == 23);
  REQUIRE(encoded_hello[0] == std::byte{2});
  REQUIRE(encoded_hello[1] == std::byte{1});
  REQUIRE(decode_hello(encoded_hello) == hello);

  const Accept accept{HandshakeRole::Controlled, 42, VideoCodec::Hevc, true, 3840, 2160, 60,
                      50'000'000, hello.nonce};
  const auto encoded_accept = encode_accept(accept);
  REQUIRE(encoded_accept.size() == 27);
  REQUIRE(encoded_accept[0] == std::byte{2});
  REQUIRE(encoded_accept[1] == std::byte{2});
  REQUIRE(decode_accept(encoded_accept) == accept);

  encoded_hello[0] = std::byte{1};
  REQUIRE_FALSE(decode_hello(encoded_hello));
}

TEST_CASE("handshake retries HELLO every 250 ms and validates the nonce") {
  const Hello hello{HandshakeRole::Controller, VideoCodec::H264, false, 1920, 1080, 60,
                    20'000'000, 77};
  HandshakeRetrier retrier(hello);
  const auto start = SteadyClock::time_point{};
  REQUIRE(retrier.next_hello(start));
  REQUIRE_FALSE(retrier.next_hello(start + 249ms));
  REQUIRE(retrier.next_hello(start + 250ms));

  REQUIRE_FALSE(retrier.accept({HandshakeRole::Controlled, 1, VideoCodec::H264, false, 1920,
                               1080, 60, 20'000'000, 78}));
  REQUIRE(retrier.accept({HandshakeRole::Controlled, 1, VideoCodec::H264, false, 1920, 1080,
                          60, 20'000'000, 77}));
  REQUIRE_FALSE(retrier.next_hello(start + 1s));
}

TEST_CASE("handshake keeps a grace interval after the final HELLO") {
  const Hello hello{HandshakeRole::Controller, VideoCodec::H264, false, 1920, 1080, 60,
                    20'000'000, 77};
  HandshakeRetrier retrier(hello);
  const auto start = SteadyClock::time_point{};
  REQUIRE(retrier.next_hello(start));
  REQUIRE(retrier.next_hello(start + 250ms));
  REQUIRE(retrier.next_hello(start + 500ms));
  REQUIRE(retrier.next_hello(start + 750ms));
  REQUIRE(retrier.exhausted());
  REQUIRE_FALSE(retrier.expired(start + 999ms));
  REQUIRE(retrier.expired(start + 1000ms));
}

TEST_CASE("handshake rejects messages sent by the wrong role") {
  const Hello hello{HandshakeRole::Controlled, VideoCodec::H264, false, 1920, 1080, 60,
                    20'000'000, 77};
  REQUIRE_FALSE(decode_hello(encode_hello(hello)));

  const Accept accept{HandshakeRole::Controller, 1, VideoCodec::H264, false, 1920, 1080, 60,
                      20'000'000, 77};
  REQUIRE_FALSE(decode_accept(encode_accept(accept)));
}

TEST_CASE("pairing messages retry four times and then expire") {
  PairingMessageRetrier retrier;
  const auto start = SteadyClock::time_point{};
  REQUIRE(retrier.due(start));
  retrier.sent(start);
  REQUIRE_FALSE(retrier.due(start + 249ms));
  REQUIRE(retrier.due(start + 250ms));
  retrier.sent(start + 250ms);
  retrier.sent(start + 500ms);
  retrier.sent(start + 750ms);
  REQUIRE_FALSE(retrier.due(start + 1000ms));
  REQUIRE(retrier.expired(start + 1000ms));
  retrier.reset();
  REQUIRE(retrier.due(start + 1000ms));
}

TEST_CASE("session timing leaves human confirmation separate from packet retries") {
  const SessionTiming timing;
  REQUIRE(timing.handshake_lease == 2s);
  REQUIRE(timing.pairing_lease == 60s);
  REQUIRE(timing.confirmation_retry_interval == 250ms);
  REQUIRE(timing.confirmation_grace == 1s);
  REQUIRE(timing.confirmation_grace_interval == 100ms);
  REQUIRE(timing.heartbeat_interval == 500ms);
  REQUIRE(timing.liveness_timeout == 3s);
}
