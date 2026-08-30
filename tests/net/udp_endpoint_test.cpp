#include "core/net/udp_endpoint.hpp"
#include "support/loopback_test_helpers.hpp"

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
  bool locked = false;
  std::jthread server_thread([&] {
    const auto request = server.receive(500ms);
    if (request) {
      locked = static_cast<bool>(server.lock_peer(*request));
      replied = static_cast<bool>(server.reply(request->datagram.bytes));
    }
  });

  const std::vector<std::byte> payload{
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
  REQUIRE(client.send(payload));
  const auto response = client.receive(500ms);
  server_thread.join();
  REQUIRE(locked);
  REQUIRE(replied);
  REQUIRE(response);
  REQUIRE(response->datagram.bytes == payload);
}

TEST_CASE("UDP endpoint rejects oversized datagrams before the socket") {
  UdpEndpoint endpoint;
  REQUIRE(endpoint.bind(0));
  REQUIRE_FALSE(endpoint.send(std::vector<std::byte>(kMaxDatagramBytes + 1)));
}

TEST_CASE("UDP endpoint drains a bounded batch instead of one packet per tick") {
  UdpEndpoint receiver;
  UdpEndpoint sender;
  REQUIRE(receiver.bind(0));
  REQUIRE(sender.bind(0));
  REQUIRE(sender.set_remote("127.0.0.1", receiver.local_port()));

  for (std::uint8_t value = 0; value < 64; ++value) {
    REQUIRE(sender.send(std::vector<std::byte>{static_cast<std::byte>(value)}));
  }

  const auto first = receiver.try_receive_batch(32);
  const auto second = receiver.try_receive_batch(32);
  REQUIRE(first.size() == 32);
  REQUIRE(second.size() == 32);
  REQUIRE(loopback_payloads_in_order(first, 0));
  REQUIRE(loopback_payloads_in_order(second, 32));
}

TEST_CASE("UDP endpoint keeps a locked peer while ignoring stray senders") {
  UdpEndpoint receiver;
  UdpEndpoint legitimate;
  UdpEndpoint stray;
  REQUIRE(receiver.bind(0));
  REQUIRE(legitimate.bind(0));
  REQUIRE(stray.bind(0));
  REQUIRE(legitimate.set_remote("127.0.0.1", receiver.local_port()));
  REQUIRE(stray.set_remote("127.0.0.1", receiver.local_port()));

  REQUIRE(legitimate.send(std::vector<std::byte>{std::byte{0x01}}));
  const auto first = receiver.try_receive();
  REQUIRE(first.has_value());
  REQUIRE(receiver.lock_peer(*first));
  REQUIRE(receiver.peer_locked());

  REQUIRE(stray.send(std::vector<std::byte>{std::byte{0xEE}}));
  REQUIRE(legitimate.send(std::vector<std::byte>{std::byte{0x02}}));
  const auto batch = receiver.try_receive_batch(8);
  REQUIRE(batch.size() == 1);
  REQUIRE(batch.front().datagram.bytes == std::vector<std::byte>{std::byte{0x02}});

  receiver.clear_peer();
  REQUIRE_FALSE(receiver.peer_locked());
}
