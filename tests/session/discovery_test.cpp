#include "core/session/discovery.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("LAN discovery advertisement has a bounded validated wire format") {
  const DiscoveryAdvertisement advertisement{"Living Room PC", 48000};
  const auto bytes = encode_discovery_advertisement(advertisement);
  REQUIRE(bytes.size() <= kMaxDiscoveryBytes);
  REQUIRE(decode_discovery_advertisement(bytes) == advertisement);

  auto malformed = bytes;
  malformed[7] = std::byte{0xFF};
  REQUIRE_FALSE(decode_discovery_advertisement(malformed).has_value());
  REQUIRE_FALSE(encode_discovery_advertisement({std::string(49, 'x'), 48000}).size());
}

TEST_CASE("LAN discovery query is versioned and rejects unrelated traffic") {
  const auto query = encode_discovery_query();
  REQUIRE(is_discovery_query(query));
  auto unrelated = query;
  unrelated[0] = std::byte{0};
  REQUIRE_FALSE(is_discovery_query(unrelated));
}
