#include "core/session/discovery.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("LAN discovery advertisement has a bounded validated wire format") {
  const DiscoveryAdvertisement advertisement{
      DiscoverySystem::Windows,
      "Living Room PC",
      48000,
      DiscoveryCapabilities{true, true, true, true, true, false},
      3840,
      2160,
      60,
      true};
  const auto bytes = encode_discovery_advertisement(advertisement);
  REQUIRE(bytes.size() <= kMaxDiscoveryBytes);
  REQUIRE(decode_discovery_advertisement(bytes) == advertisement);

  auto unknown_flags = bytes;
  unknown_flags[6] = std::byte{0x80};
  REQUIRE_FALSE(decode_discovery_advertisement(unknown_flags).has_value());
  REQUIRE_FALSE(encode_discovery_advertisement(
                    {DiscoverySystem::Windows, "Living Room PC", 0,
                     advertisement.capabilities, 3840, 2160, 60, true})
                    .size());
  REQUIRE_FALSE(encode_discovery_advertisement(
                    {DiscoverySystem::Windows, std::string(49, 'x'), 48000,
                     advertisement.capabilities, 3840, 2160, 60, true})
                    .size());
  REQUIRE_FALSE(encode_discovery_advertisement(
                    {DiscoverySystem::Windows, "Living Room PC", 48000,
                     advertisement.capabilities, 3840, 2160, 60, false})
                    .size());

  auto truncated = bytes;
  truncated.pop_back();
  REQUIRE_FALSE(decode_discovery_advertisement(truncated).has_value());
}

TEST_CASE("LAN discovery query is versioned and rejects unrelated traffic") {
  const auto query = encode_discovery_query();
  REQUIRE(is_discovery_query(query));
  auto unrelated = query;
  unrelated[0] = std::byte{0};
  REQUIRE_FALSE(is_discovery_query(unrelated));
}

TEST_CASE("discovered device formatting shows stream parameters without gamepad details") {
  const DiscoveredHost host{DiscoverySystem::Windows,
                            "Living Room PC",
                            "192.168.1.20",
                            48000,
                            DiscoveryCapabilities{true, true, true, true, true, true},
                            3840,
                            2160,
                            60,
                            true};
  REQUIRE(format_discovered_host(host) ==
          "Windows | Living Room PC\nH.264/HEVC | 3840x2160 60 fps | HDR10 | Audio");
  REQUIRE(format_discovered_host(host).find("gamepad") == std::string::npos);
}
