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
constexpr std::byte kVersion{2};
constexpr std::byte kQuery{1};
constexpr std::byte kAdvertisement{2};
constexpr std::uint8_t kControllable = 1U << 0U;
constexpr std::uint8_t kH264 = 1U << 1U;
constexpr std::uint8_t kHevc = 1U << 2U;
constexpr std::uint8_t kHdr10 = 1U << 3U;
constexpr std::uint8_t kAudio = 1U << 4U;
constexpr std::uint8_t kKeyboardMouse = 1U << 5U;
constexpr std::uint8_t kGamepad = 1U << 6U;
constexpr std::uint8_t kKnownFlags = kControllable | kH264 | kHevc | kHdr10 | kAudio |
                                      kKeyboardMouse | kGamepad;
constexpr std::size_t kAdvertisementHeaderBytes = 17;

std::uint8_t encode_flags(const DiscoveryAdvertisement& advertisement) {
  std::uint8_t flags = kControllable;
  if (advertisement.capabilities.h264) flags |= kH264;
  if (advertisement.capabilities.hevc) flags |= kHevc;
  if (advertisement.capabilities.hdr10) flags |= kHdr10;
  if (advertisement.capabilities.audio) flags |= kAudio;
  if (advertisement.capabilities.keyboard_mouse) flags |= kKeyboardMouse;
  if (advertisement.capabilities.gamepad) flags |= kGamepad;
  return flags;
}

bool valid_system(std::uint8_t value) {
  return value >= static_cast<std::uint8_t>(DiscoverySystem::Windows) &&
         value <= static_cast<std::uint8_t>(DiscoverySystem::Linux);
}

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
  if (advertisement.system == DiscoverySystem::Unknown || advertisement.device_name.empty() ||
      advertisement.device_name.size() > kMaxDiscoveryNameBytes ||
      advertisement.session_port == 0 || advertisement.max_width == 0 ||
      advertisement.max_height == 0 || advertisement.max_fps == 0 || !advertisement.controllable ||
      (advertisement.capabilities.hdr10 && !advertisement.capabilities.hevc)) {
    return {};
  }
  std::vector<std::byte> bytes{kMagic[0],
                               kMagic[1],
                               kMagic[2],
                               kMagic[3],
                               kVersion,
                               kAdvertisement,
                               static_cast<std::byte>(encode_flags(advertisement)),
                               static_cast<std::byte>(advertisement.system),
                               static_cast<std::byte>(advertisement.session_port >> 8U),
                               static_cast<std::byte>(advertisement.session_port),
                               static_cast<std::byte>(advertisement.max_width >> 8U),
                               static_cast<std::byte>(advertisement.max_width),
                               static_cast<std::byte>(advertisement.max_height >> 8U),
                               static_cast<std::byte>(advertisement.max_height),
                               static_cast<std::byte>(advertisement.max_fps >> 8U),
                               static_cast<std::byte>(advertisement.max_fps),
                               static_cast<std::byte>(advertisement.device_name.size())};
  bytes.reserve(kAdvertisementHeaderBytes + advertisement.device_name.size());
  for (const auto character : advertisement.device_name) {
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return bytes;
}

std::optional<DiscoveryAdvertisement> decode_discovery_advertisement(
    std::span<const std::byte> bytes) {
  if (bytes.size() < kAdvertisementHeaderBytes + 1 || bytes.size() > kMaxDiscoveryBytes ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
      bytes[4] != kVersion || bytes[5] != kAdvertisement) {
    return std::nullopt;
  }
  const auto flags = std::to_integer<std::uint8_t>(bytes[6]);
  const auto system = std::to_integer<std::uint8_t>(bytes[7]);
  const auto port = static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(bytes[8]) << 8U) |
      std::to_integer<std::uint16_t>(bytes[9]));
  const auto max_width = static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(bytes[10]) << 8U) |
      std::to_integer<std::uint16_t>(bytes[11]));
  const auto max_height = static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(bytes[12]) << 8U) |
      std::to_integer<std::uint16_t>(bytes[13]));
  const auto max_fps = static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(bytes[14]) << 8U) |
      std::to_integer<std::uint16_t>(bytes[15]));
  const auto name_size = std::to_integer<std::size_t>(bytes[16]);
  if ((flags & ~kKnownFlags) != 0 || (flags & kControllable) == 0 || !valid_system(system) ||
      port == 0 || max_width == 0 || max_height == 0 || max_fps == 0 || name_size == 0 ||
      name_size > kMaxDiscoveryNameBytes || bytes.size() != kAdvertisementHeaderBytes + name_size ||
      (flags & kHdr10) != 0 && (flags & kHevc) == 0) {
    return std::nullopt;
  }
  std::string device_name;
  device_name.reserve(name_size);
  for (const auto byte : bytes.subspan(kAdvertisementHeaderBytes)) {
    device_name.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
  }
  return DiscoveryAdvertisement{
      static_cast<DiscoverySystem>(system),
      std::move(device_name),
      port,
      DiscoveryCapabilities{(flags & kH264) != 0,
                            (flags & kHevc) != 0,
                            (flags & kHdr10) != 0,
                            (flags & kAudio) != 0,
                            (flags & kKeyboardMouse) != 0,
                            (flags & kGamepad) != 0},
      max_width,
      max_height,
      max_fps,
      true};
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
  if (!advertisement.controllable) {
    return Result<bool, DiscoveryError>::ok(false);
  }
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
        if (!advertisement->controllable) {
          continue;
        }
        const auto address = sender.address().to_string();
        const auto duplicate = std::ranges::any_of(hosts, [&](const DiscoveredHost& host) {
          return host.address == address && host.session_port == advertisement->session_port;
        });
        if (!duplicate) {
          hosts.push_back({advertisement->system,
                           advertisement->device_name,
                           address,
                           advertisement->session_port,
                           advertisement->capabilities,
                           advertisement->max_width,
                           advertisement->max_height,
                           advertisement->max_fps,
                           advertisement->controllable});
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
