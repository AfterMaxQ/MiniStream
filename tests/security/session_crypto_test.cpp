#include "core/security/session_crypto.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

using namespace ministream;

namespace {
std::array<std::byte, 32> key(std::byte value) {
  std::array<std::byte, 32> result{};
  result.fill(value);
  return result;
}

SessionCrypto sender() {
  return SessionCrypto(7, key(std::byte{0x11}), key(std::byte{0x22}), 0x12345678, 0x87654321);
}

SessionCrypto receiver() {
  return SessionCrypto(7, key(std::byte{0x22}), key(std::byte{0x11}), 0x87654321, 0x12345678);
}
}  // namespace

TEST_CASE("session crypto authenticates and encrypts a datagram") {
  auto tx = sender();
  auto rx = receiver();
  const std::vector<std::byte> plaintext{
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  const auto sealed = tx.seal(PacketType::Input, plaintext);
  REQUIRE(sealed.has_value());
  REQUIRE(std::search(
              sealed.value().bytes.begin(), sealed.value().bytes.end(),
              plaintext.begin(), plaintext.end()) == sealed.value().bytes.end());

  const auto opened = rx.open(sealed.value());
  REQUIRE(opened.has_value());
  REQUIRE(opened.value() == plaintext);
}

TEST_CASE("session crypto rejects tampering and duplicate replay") {
  auto tx = sender();
  auto rx = receiver();
  auto packet = tx.seal(PacketType::Control, {std::byte{9}}).value();
  auto tampered = packet;
  tampered.bytes.back() ^= std::byte{1};
  REQUIRE_FALSE(rx.open(tampered).has_value());
  REQUIRE(rx.open(packet).has_value());
  const auto replay = rx.open(packet);
  REQUIRE_FALSE(replay.has_value());
  REQUIRE(replay.error() == CryptoError::Replay);
}

TEST_CASE("session crypto accepts reordered packets once within the replay window") {
  auto tx = sender();
  auto rx = receiver();
  const auto first = tx.seal(PacketType::Input, {std::byte{1}}).value();
  const auto second = tx.seal(PacketType::Input, {std::byte{2}}).value();
  const auto third = tx.seal(PacketType::Input, {std::byte{3}}).value();

  REQUIRE(rx.open(third).has_value());
  REQUIRE(rx.open(first).has_value());
  REQUIRE(rx.open(second).has_value());
  REQUIRE_FALSE(rx.open(first).has_value());
}

TEST_CASE("session crypto authenticates the routing header") {
  auto tx = sender();
  auto rx = receiver();
  auto packet = tx.seal(PacketType::Audio, {std::byte{7}}).value();
  packet.bytes[5] = static_cast<std::byte>(PacketType::Video);
  const auto result = rx.open(packet);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == CryptoError::AuthenticationFailed);
}
