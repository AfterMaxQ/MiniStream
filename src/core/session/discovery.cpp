#include "core/session/discovery.hpp"

#include <asio.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace ministream {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'M'}, std::byte{'S'}, std::byte{'D'}, std::byte{'1'}};
constexpr std::byte kVersion{1};
constexpr std::byte kQuery{1};
constexpr std::byte kAdvertisement{2};

}  // namespace

std::array<std::byte, 8> encode_discovery_query() {
  return {kMagic[0], kMagic[1], kMagic[2], kMagic[3], kVersion, kQuery,
          std::byte{0}, std::byte{0}};
}

bool is_discovery_query(std::span<const std::byte> bytes) {
  return bytes.size() == 8 && std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) &&
         bytes[4] == kVersion && bytes[5] == kQuery;
}

std::vector<std::byte> encode_discovery_advertisement(
    const DiscoveryAdvertisement& advertisement) {
  if (advertisement.name.empty() || advertisement.name.size() > 48 ||
      advertisement.session_port == 0) {
    return {};
  }
  std::vector<std::byte> bytes{
      kMagic[0], kMagic[1], kMagic[2], kMagic[3], kVersion, kAdvertisement,
      static_cast<std::byte>(advertisement.session_port >> 8U),
      static_cast<std::byte>(advertisement.name.size()),
      static_cast<std::byte>(advertisement.session_port)};
  bytes.reserve(9 + advertisement.name.size());
  for (const auto character : advertisement.name) {
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return bytes;
}

std::optional<DiscoveryAdvertisement> decode_discovery_advertisement(
    std::span<const std::byte> bytes) {
  if (bytes.size() < 10 || bytes.size() > kMaxDiscoveryBytes ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
      bytes[4] != kVersion || bytes[5] != kAdvertisement) {
    return std::nullopt;
  }
  const auto name_size = std::to_integer<std::size_t>(bytes[7]);
  if (name_size == 0 || name_size > 48 || bytes.size() != 9 + name_size) {
    return std::nullopt;
  }
  const auto port = static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(bytes[6]) << 8U) |
      std::to_integer<std::uint16_t>(bytes[8]));
  if (port == 0) {
    return std::nullopt;
  }
  std::string name;
  name.reserve(name_size);
  for (const auto byte : bytes.subspan(9)) {
    name.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
  }
  return DiscoveryAdvertisement{std::move(name), port};
}

struct DiscoveryHost::Impl {
  asio::io_context io;
  asio::ip::udp::socket socket{io};
};

DiscoveryHost::DiscoveryHost() : impl_(std::make_unique<Impl>()) {}
DiscoveryHost::~DiscoveryHost() = default;
DiscoveryHost::DiscoveryHost(DiscoveryHost&&) noexcept = default;
DiscoveryHost& DiscoveryHost::operator=(DiscoveryHost&&) noexcept = default;

Result<void, DiscoveryError> DiscoveryHost::start() {
  asio::error_code error;
  impl_->socket.open(asio::ip::udp::v4(), error);
  if (error) {
    return Result<void, DiscoveryError>::err(DiscoveryError::Bind);
  }
  impl_->socket.set_option(asio::socket_base::reuse_address(true), error);
  impl_->socket.bind({asio::ip::udp::v4(), kDiscoveryPort}, error);
  if (error) {
    return Result<void, DiscoveryError>::err(DiscoveryError::Bind);
  }
  impl_->socket.non_blocking(true, error);
  return error ? Result<void, DiscoveryError>::err(DiscoveryError::Bind)
               : Result<void, DiscoveryError>::ok();
}

Result<bool, DiscoveryError> DiscoveryHost::poll(
    const DiscoveryAdvertisement& advertisement) {
  std::array<std::byte, kMaxDiscoveryBytes> buffer{};
  asio::ip::udp::endpoint sender;
  asio::error_code error;
  const auto received = impl_->socket.receive_from(asio::buffer(buffer), sender, 0, error);
  if (error == asio::error::would_block || error == asio::error::try_again) {
    return Result<bool, DiscoveryError>::ok(false);
  }
  if (error) {
    return Result<bool, DiscoveryError>::err(DiscoveryError::Receive);
  }
  if (!is_discovery_query(std::span{buffer}.first(received))) {
    return Result<bool, DiscoveryError>::ok(false);
  }
  const auto reply = encode_discovery_advertisement(advertisement);
  impl_->socket.send_to(asio::buffer(reply), sender, 0, error);
  return error ? Result<bool, DiscoveryError>::err(DiscoveryError::Send)
               : Result<bool, DiscoveryError>::ok(true);
}

Result<std::vector<DiscoveredHost>, DiscoveryError> discover_hosts(Microseconds timeout) {
  asio::io_context io;
  asio::ip::udp::socket socket(io);
  asio::error_code error;
  socket.open(asio::ip::udp::v4(), error);
  if (error) {
    return Result<std::vector<DiscoveredHost>, DiscoveryError>::err(DiscoveryError::Bind);
  }
  socket.bind({asio::ip::udp::v4(), 0}, error);
  socket.set_option(asio::socket_base::broadcast(true), error);
  socket.non_blocking(true, error);
  if (error) {
    return Result<std::vector<DiscoveredHost>, DiscoveryError>::err(DiscoveryError::Bind);
  }
  const auto query = encode_discovery_query();
  socket.send_to(asio::buffer(query),
                 {asio::ip::address_v4::broadcast(), kDiscoveryPort}, 0, error);
  if (error) {
    return Result<std::vector<DiscoveredHost>, DiscoveryError>::err(DiscoveryError::Send);
  }

  std::vector<DiscoveredHost> hosts;
  const auto deadline = SteadyClock::now() + timeout;
  while (SteadyClock::now() < deadline) {
    std::array<std::byte, kMaxDiscoveryBytes> buffer{};
    asio::ip::udp::endpoint sender;
    const auto received = socket.receive_from(asio::buffer(buffer), sender, 0, error);
    if (!error) {
      if (const auto advertisement =
              decode_discovery_advertisement(std::span{buffer}.first(received))) {
        const auto address = sender.address().to_string();
        const auto duplicate = std::ranges::any_of(hosts, [&](const DiscoveredHost& host) {
          return host.address == address && host.session_port == advertisement->session_port;
        });
        if (!duplicate) {
          hosts.push_back({advertisement->name, address, advertisement->session_port});
        }
      }
    } else if (error != asio::error::would_block && error != asio::error::try_again) {
      return Result<std::vector<DiscoveredHost>, DiscoveryError>::err(
          DiscoveryError::Receive);
    }
    error.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return Result<std::vector<DiscoveredHost>, DiscoveryError>::ok(std::move(hosts));
}

}  // namespace ministream
