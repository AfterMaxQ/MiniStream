#include "core/session/discovery.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace ministream;

TEST_CASE("LAN discovery calculates directed broadcasts from injected interfaces") {
  REQUIRE(directed_broadcast({192, 168, 1, 20}, {255, 255, 255, 0}) ==
          std::optional<std::array<std::uint8_t, 4>>{{192, 168, 1, 255}});
  REQUIRE(directed_broadcast({10, 0, 0, 7}, {255, 255, 0, 0}) ==
          std::optional<std::array<std::uint8_t, 4>>{{10, 0, 255, 255}});
  REQUIRE_FALSE(directed_broadcast({10, 0, 0, 7}, {255, 255, 255, 255}).has_value());
  REQUIRE_FALSE(directed_broadcast({10, 0, 0, 7}, {255, 0, 255, 0}).has_value());

  const std::vector<DiscoveryInterface> interfaces{
      {"en0", {192, 168, 1, 20}, {255, 255, 255, 0}, true, false},
      {"utun0", {100, 64, 0, 2}, {255, 255, 255, 255}, true, false},
      {"lo0", {127, 0, 0, 1}, {255, 0, 0, 0}, true, true},
      {"down0", {172, 16, 0, 2}, {255, 255, 0, 0}, false, false},
  };
  REQUIRE(discovery_targets(interfaces) ==
          std::vector<std::array<std::uint8_t, 4>>{{192, 168, 1, 255}, {255, 255, 255, 255}});

  const std::vector<DiscoveryInterface> point_to_point{
      {"utun0", {100, 64, 0, 2}, {255, 255, 255, 255}, true, false}};
  REQUIRE(discovery_targets(point_to_point) ==
          std::vector<std::array<std::uint8_t, 4>>{{255, 255, 255, 255}});
}

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
