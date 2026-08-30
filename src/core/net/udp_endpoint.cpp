#include "core/net/udp_endpoint.hpp"

#include <chrono>
#include <utility>

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
  if (!error) {
    asio::error_code buffer_error;
    socket_.set_option(asio::socket_base::receive_buffer_size(4 * 1024 * 1024),
                       buffer_error);
  }
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
  peer_locked_ = true;
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
  if (!remote_) {
    return Result<std::size_t, NetError>::err(NetError::NoPeer);
  }
  asio::error_code error;
  const auto sent = socket_.send_to(
      asio::buffer(bytes.data(), bytes.size()), *remote_, 0, error);
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
  if (peer_locked_ && (!remote_ || sender.address() != remote_->address() ||
                       sender.port() != remote_->port())) {
    return Result<ReceivedDatagram, NetError>::err(NetError::Receive);
  }
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
  if (peer_locked_ && (!remote_ || sender.address() != remote_->address() ||
                       sender.port() != remote_->port())) {
    return std::nullopt;
  }
  ReceivedDatagram result;
  result.datagram.bytes.assign(buffer.begin(), buffer.begin() + received);
  result.sender_address = sender.address().to_string();
  result.sender_port = sender.port();
  return result;
}

std::vector<ReceivedDatagram> UdpEndpoint::try_receive_batch(std::size_t max_packets) {
  std::vector<ReceivedDatagram> packets;
  packets.reserve(max_packets);
  while (packets.size() < max_packets) {
    std::array<std::byte, kMaxDatagramBytes> buffer{};
    asio::ip::udp::endpoint sender;
    asio::error_code error;
    const auto received = socket_.receive_from(asio::buffer(buffer), sender, 0, error);
    if (error == asio::error::would_block || error == asio::error::try_again) {
      break;
    }
    if (error == asio::error::message_size) {
      continue;
    }
    if (error) {
      break;
    }
    if (peer_locked_ && (!remote_ || sender.address() != remote_->address() ||
                         sender.port() != remote_->port())) {
      continue;
    }
    ReceivedDatagram result;
    result.datagram.bytes.assign(buffer.begin(), buffer.begin() + received);
    result.sender_address = sender.address().to_string();
    result.sender_port = sender.port();
    packets.push_back(std::move(result));
  }
  return packets;
}

Result<void, NetError> UdpEndpoint::lock_peer(const ReceivedDatagram& incoming) {
  if (incoming.sender_address.empty() || incoming.sender_port == 0) {
    return Result<void, NetError>::err(NetError::NoPeer);
  }
  asio::error_code error;
  const auto address = asio::ip::make_address(incoming.sender_address, error);
  if (error || !address.is_v4()) {
    return Result<void, NetError>::err(NetError::Resolve);
  }
  const auto candidate = asio::ip::udp::endpoint(address, incoming.sender_port);
  if (peer_locked_ && (!remote_ || *remote_ != candidate)) {
    return Result<void, NetError>::err(NetError::NoPeer);
  }
  remote_ = candidate;
  peer_locked_ = true;
  return Result<void, NetError>::ok();
}

void UdpEndpoint::clear_peer() noexcept {
  remote_.reset();
  peer_locked_ = false;
}

bool UdpEndpoint::peer_locked() const noexcept { return peer_locked_ && remote_.has_value(); }

std::uint16_t UdpEndpoint::local_port() const {
  asio::error_code error;
  const auto endpoint = socket_.local_endpoint(error);
  return error ? 0 : endpoint.port();
}

}  // namespace ministream
