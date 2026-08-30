#include "core/security/pairing_wire.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace ministream;

TEST_CASE("pairing offer carries role nonce identity and ephemeral key") {
  PairingOffer offer;
  offer.role = PairingRole::Initiator;
  offer.nonce = 0x0102030405060708ULL;
  std::ranges::fill(offer.identity, std::byte{0x11});
  std::ranges::fill(offer.ephemeral, std::byte{0x22});

  const auto bytes = encode_pairing_offer(offer);
  REQUIRE(bytes[3] == std::byte{'2'});
  REQUIRE(decode_pairing_offer(bytes) == offer);
  auto malformed = bytes;
  malformed[4] = std::byte{0xFF};
  REQUIRE_FALSE(decode_pairing_offer(malformed).has_value());
  auto old_version = bytes;
  old_version[3] = std::byte{'1'};
  REQUIRE_FALSE(decode_pairing_offer(old_version).has_value());
}

TEST_CASE("pairing confirmation is explicit and versioned") {
  REQUIRE(decode_pairing_confirmation(encode_pairing_confirmation(true)) == true);
  REQUIRE(decode_pairing_confirmation(encode_pairing_confirmation(false)) == false);
}

TEST_CASE("pairing transcript has stable initiator and responder ordering") {
  PairingOffer initiator{PairingRole::Initiator, 10};
  PairingOffer responder{PairingRole::Responder, 20};
  initiator.identity[0] = std::byte{1};
  responder.identity[0] = std::byte{2};
  const auto transcript = pairing_transcript(initiator, responder);
  REQUIRE(transcript.has_value());
  REQUIRE(transcript->protocol_version == 2);
  REQUIRE(transcript->initiator_nonce == 10);
  REQUIRE(transcript->responder_nonce == 20);
  REQUIRE(transcript->initiator_identity[0] == std::byte{1});
  REQUIRE_FALSE(pairing_transcript(responder, initiator).has_value());
}
