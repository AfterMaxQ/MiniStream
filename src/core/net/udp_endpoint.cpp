#include "core/net/udp_endpoint.hpp"

#include <chrono>

namespace ministream {

UdpEndpoint::UdpEndpoint() : socket_(io_) {}

Result<void, NetError> UdpEndpoint::ensure_open() {
  if (socket_.is_open()) {
    return Result<void, NetError>::ok();
  }
  asio::error_code error;
  socket_.open(asio::ip::udp::v4(), error);
  if (error) {
    return Result<void, NetError>::err(NetError::Bind);
  }
  socket_.non_blocking(true, error);
  if (error) {
    return Result<void, NetError>::err(NetError::Bind);
  }
  return Result<void, NetError>::ok();
}

Result<void, NetError> UdpEndpoint::bind(std::uint16_t port) {
  if (auto opened = ensure_open(); !opened) {
    return opened;
  }
  asio::error_code error;
  socket_.bind({asio::ip::udp::v4(), port}, error);
  return error ? Result<void, NetError>::err(NetError::Bind)
               : Result<void, NetError>::ok();
}

Result<void, NetError> UdpEndpoint::set_remote(
    const std::string& host, std::uint16_t port) {
  if (auto opened = ensure_open(); !opened) {
    return opened;
  }
  asio::ip::udp::resolver resolver(io_);
  asio::error_code error;
  const auto results = resolver.resolve(host, std::to_string(port), error);
  if (error || results.empty()) {
    return Result<void, NetError>::err(NetError::Resolve);
  }
  remote_ = *results.begin();
  return Result<void, NetError>::ok();
}

Result<std::size_t, NetError> UdpEndpoint::send(std::span<const std::byte> bytes) {
  if (bytes.size() > kMaxDatagramBytes) {
    return Result<std::size_t, NetError>::err(NetError::Oversized);
  }
  if (!remote_) {
    return Result<std::size_t, NetError>::err(NetError::NoPeer);
  }
  asio::error_code error;
  const auto sent = socket_.send_to(asio::buffer(bytes.data(), bytes.size()), *remote_, 0, error);
  return error ? Result<std::size_t, NetError>::err(NetError::Send)
               : Result<std::size_t, NetError>::ok(sent);
}

Result<std::size_t, NetError> UdpEndpoint::reply(std::span<const std::byte> bytes) {
  if (bytes.size() > kMaxDatagramBytes) {
    return Result<std::size_t, NetError>::err(NetError::Oversized);
  }
  if (!last_sender_) {
    return Result<std::size_t, NetError>::err(NetError::NoPeer);
  }
  asio::error_code error;
  const auto sent = socket_.send_to(
      asio::buffer(bytes.data(), bytes.size()), *last_sender_, 0, error);
  return error ? Result<std::size_t, NetError>::err(NetError::Send)
               : Result<std::size_t, NetError>::ok(sent);
}

Result<ReceivedDatagram, NetError> UdpEndpoint::receive(Microseconds timeout) {
  std::array<std::byte, kMaxDatagramBytes> buffer{};
  asio::ip::udp::endpoint sender;
  std::optional<std::size_t> received;
  asio::error_code receive_error;
  bool timed_out = false;
  asio::steady_timer timer(io_);
  timer.expires_after(timeout);
  socket_.async_receive_from(
      asio::buffer(buffer), sender,
      [&](const asio::error_code& error, std::size_t bytes) {
        receive_error = error;
        if (!error) {
          received = bytes;
        }
        timer.cancel();
      });
  timer.async_wait([&](const asio::error_code& error) {
    if (!error) {
      timed_out = true;
      socket_.cancel();
    }
  });
  io_.restart();
  io_.run();
  if (!received) {
    return Result<ReceivedDatagram, NetError>::err(
        timed_out ? NetError::Timeout : NetError::Receive);
  }
  last_sender_ = sender;
  ReceivedDatagram result;
  result.datagram.bytes.assign(buffer.begin(), buffer.begin() + *received);
  result.sender_address = sender.address().to_string();
  result.sender_port = sender.port();
  return Result<ReceivedDatagram, NetError>::ok(std::move(result));
}

std::optional<ReceivedDatagram> UdpEndpoint::try_receive() {
  std::array<std::byte, kMaxDatagramBytes> buffer{};
  asio::ip::udp::endpoint sender;
  asio::error_code error;
  const auto received = socket_.receive_from(asio::buffer(buffer), sender, 0, error);
  if (error == asio::error::would_block || error == asio::error::try_again) {
    return std::nullopt;
  }
  if (error) {
    return std::nullopt;
  }
  last_sender_ = sender;
  ReceivedDatagram result;
  result.datagram.bytes.assign(buffer.begin(), buffer.begin() + received);
  result.sender_address = sender.address().to_string();
  result.sender_port = sender.port();
  return result;
}

std::uint16_t UdpEndpoint::local_port() const {
  asio::error_code error;
  const auto endpoint = socket_.local_endpoint(error);
  return error ? 0 : endpoint.port();
}

}  // namespace ministream
