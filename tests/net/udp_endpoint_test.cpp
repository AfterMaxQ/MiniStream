#include "core/net/udp_endpoint.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace ministream;

TEST_CASE("UDP endpoint exchanges a bounded loopback datagram") {
  UdpEndpoint server;
  UdpEndpoint client;
  REQUIRE(server.bind(0));
  REQUIRE(client.bind(0));
  REQUIRE(client.set_remote("127.0.0.1", server.local_port()));

  bool replied = false;
  std::jthread server_thread([&] {
    const auto request = server.receive(500ms);
    if (request) {
      replied = static_cast<bool>(server.reply(request->datagram.bytes));
    }
  });

  const std::vector<std::byte> payload{
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
  REQUIRE(client.send(payload));
  const auto response = client.receive(500ms);
  server_thread.join();
  REQUIRE(replied);
  REQUIRE(response);
  REQUIRE(response->datagram.bytes == payload);
}

TEST_CASE("UDP endpoint rejects oversized datagrams before the socket") {
  UdpEndpoint endpoint;
  REQUIRE(endpoint.bind(0));
  REQUIRE_FALSE(endpoint.send(std::vector<std::byte>(kMaxDatagramBytes + 1)));
}
