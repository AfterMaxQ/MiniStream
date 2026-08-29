#pragma once

#include "core/base/result.hpp"
#include "core/protocol/value_types.hpp"
#include "core/time/clock.hpp"

#include <asio.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ministream {

enum class NetError { Resolve, Bind, Send, Receive, Timeout, Oversized, NoPeer };

struct ReceivedDatagram {
  Datagram datagram;
  std::string sender_address;
  std::uint16_t sender_port{};
};

class UdpEndpoint {
 public:
  UdpEndpoint();

  Result<void, NetError> bind(std::uint16_t port);
  Result<void, NetError> set_remote(const std::string& host, std::uint16_t port);
  Result<std::size_t, NetError> send(std::span<const std::byte> bytes);
  Result<std::size_t, NetError> reply(std::span<const std::byte> bytes);
  Result<ReceivedDatagram, NetError> receive(Microseconds timeout);
  std::optional<ReceivedDatagram> try_receive();
  [[nodiscard]] std::uint16_t local_port() const;

 private:
  Result<void, NetError> ensure_open();

  asio::io_context io_;
  asio::ip::udp::socket socket_;
  std::optional<asio::ip::udp::endpoint> remote_;
  std::optional<asio::ip::udp::endpoint> last_sender_;
};

}  // namespace ministream
