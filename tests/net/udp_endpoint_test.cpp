#include "core/net/udp_endpoint.hpp"
#include "support/loopback_test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <utility>
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
  std::thread server_thread([&] {
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

TEST_CASE("UDP endpoint classifies temporary send backpressure separately") {
  const asio::error_code would_block = asio::error::would_block;
  const asio::error_code try_again = asio::error::try_again;
  const asio::error_code network_unreachable = asio::error::network_unreachable;

  REQUIRE(detail::classify_send_error(would_block) == NetError::WouldBlock);
  REQUIRE(detail::classify_send_error(try_again) == NetError::WouldBlock);
  REQUIRE(detail::classify_send_error(network_unreachable) == NetError::Send);
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
  const auto first = receiver.receive(100ms);
  REQUIRE(first);
  REQUIRE(receiver.lock_peer(*first));
  REQUIRE(receiver.peer_locked());

  REQUIRE(stray.send(std::vector<std::byte>{std::byte{0xEE}}));
  REQUIRE(legitimate.send(std::vector<std::byte>{std::byte{0x02}}));
  std::vector<ReceivedDatagram> batch;
  for (unsigned attempt = 0; attempt < 100U && batch.empty(); ++attempt) {
    batch = receiver.try_receive_batch(8);
    if (batch.empty()) {
      std::this_thread::sleep_for(1ms);
    }
  }
  REQUIRE(batch.size() == 1);
  REQUIRE(batch.front().datagram.bytes == std::vector<std::byte>{std::byte{0x02}});

  receiver.clear_peer();
  REQUIRE_FALSE(receiver.peer_locked());
}

TEST_CASE("UDP endpoint rechecks prefetched datagrams after locking a peer") {
  UdpEndpoint receiver;
  UdpEndpoint first_sender;
  UdpEndpoint second_sender;
  REQUIRE(receiver.bind(0));
  REQUIRE(first_sender.bind(0));
  REQUIRE(second_sender.bind(0));
  REQUIRE(first_sender.set_remote("127.0.0.1", receiver.local_port()));
  REQUIRE(second_sender.set_remote("127.0.0.1", receiver.local_port()));

  REQUIRE(first_sender.send(std::vector<std::byte>{std::byte{0x01}}));
  REQUIRE(second_sender.send(std::vector<std::byte>{std::byte{0x02}}));
  std::vector<ReceivedDatagram> prefetched;
  for (unsigned attempt = 0; attempt < 100U && prefetched.size() < 2U; ++attempt) {
    auto batch = receiver.try_receive_batch(2U - prefetched.size());
    for (auto& packet : batch) {
      prefetched.push_back(std::move(packet));
    }
    if (prefetched.size() < 2U) {
      std::this_thread::sleep_for(1ms);
    }
  }
  REQUIRE(prefetched.size() == 2);

  const ReceivedDatagram* first = nullptr;
  const ReceivedDatagram* second = nullptr;
  for (const auto& packet : prefetched) {
    if (packet.datagram.bytes == std::vector<std::byte>{std::byte{0x01}}) {
      first = &packet;
    } else if (packet.datagram.bytes == std::vector<std::byte>{std::byte{0x02}}) {
      second = &packet;
    }
  }
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  REQUIRE(receiver.lock_peer(*first));
  REQUIRE(receiver.matches_peer(*first));
  REQUIRE_FALSE(receiver.matches_peer(*second));
}
