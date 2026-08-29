#include "netprobe/options.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace ministream::netprobe;

TEST_CASE("netprobe parses listen and connect modes") {
  const std::array listen{"ministream-netprobe", "--listen", "47990"};
  const auto listen_options = parse_options(listen);
  REQUIRE(listen_options);
  REQUIRE(listen_options->mode == Mode::Listen);
  REQUIRE(listen_options->port == 47990);

  const std::array connect{
      "ministream-netprobe", "--connect", "192.168.1.2", "--rate-mbps", "40",
      "--duration", "15"};
  const auto connect_options = parse_options(connect);
  REQUIRE(connect_options);
  REQUIRE(connect_options->mode == Mode::Connect);
  REQUIRE(connect_options->host == "192.168.1.2");
  REQUIRE(connect_options->rate_mbps == 40);
  REQUIRE(connect_options->duration_seconds == 15);
}

TEST_CASE("netprobe rejects incomplete or unsafe rates") {
  const std::array missing_host{"ministream-netprobe", "--connect"};
  REQUIRE_FALSE(parse_options(missing_host));
  const std::array zero_rate{
      "ministream-netprobe", "--connect", "127.0.0.1", "--rate-mbps", "0"};
  REQUIRE_FALSE(parse_options(zero_rate));
  const std::array excessive_rate{
      "ministream-netprobe", "--connect", "127.0.0.1", "--rate-mbps", "1001"};
  REQUIRE_FALSE(parse_options(excessive_rate));
}
