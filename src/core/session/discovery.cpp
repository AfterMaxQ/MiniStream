#include "core/session/discovery.hpp"

#include <asio.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string_view>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <iphlpapi.h>
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace ministream {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'M'}, std::byte{'S'}, std::byte{'D'}, std::byte{'1'}};
constexpr std::byte kVersion{3};
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

std::string system_label(DiscoverySystem system) {
  switch (system) {
    case DiscoverySystem::Windows:
      return "Windows";
    case DiscoverySystem::MacOS:
      return "macOS";
    case DiscoverySystem::Linux:
      return "Linux";
    case DiscoverySystem::Unknown:
      return "Unknown";
  }
  return "Unknown";
}

std::string codec_label(const DiscoveryCapabilities& capabilities) {
  if (capabilities.h264 && capabilities.hevc) {
    return "H.264/HEVC";
  }
  if (capabilities.hevc) {
    return "HEVC";
  }
  if (capabilities.h264) {
    return "H.264";
  }
  return "No video";
}

std::uint32_t to_u32(const IPv4Octets& value) noexcept {
  return (static_cast<std::uint32_t>(value[0]) << 24U) |
         (static_cast<std::uint32_t>(value[1]) << 16U) |
         (static_cast<std::uint32_t>(value[2]) << 8U) |
         static_cast<std::uint32_t>(value[3]);
}

IPv4Octets from_u32(std::uint32_t value) noexcept {
  return {static_cast<std::uint8_t>(value >> 24U),
          static_cast<std::uint8_t>(value >> 16U),
          static_cast<std::uint8_t>(value >> 8U), static_cast<std::uint8_t>(value)};
}

bool valid_netmask(std::uint32_t mask, unsigned& prefix_length) noexcept {
  prefix_length = 0;
  bool zero_seen = false;
  for (int bit = 31; bit >= 0; --bit) {
    const bool set = (mask & (std::uint32_t{1} << bit)) != 0;
    if (set && zero_seen) {
      return false;
    }
    if (set) {
      ++prefix_length;
    } else {
      zero_seen = true;
    }
  }
  return true;
}

std::string format_ipv4(const IPv4Octets& value) {
  return std::to_string(value[0]) + "." + std::to_string(value[1]) + "." +
         std::to_string(value[2]) + "." + std::to_string(value[3]);
}

IPv4Octets sockaddr_ipv4(const sockaddr_in& address) noexcept {
  return from_u32(ntohl(address.sin_addr.s_addr));
}

std::vector<DiscoveryInterface> enumerate_interfaces() {
  std::vector<DiscoveryInterface> interfaces;
#ifdef _WIN32
  ULONG buffer_size = 16U * 1024U;
  std::vector<std::byte> buffer(buffer_size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
  auto result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters,
                                     &buffer_size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    buffer.resize(buffer_size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters,
                                  &buffer_size);
  }
  if (result != NO_ERROR) {
    return interfaces;
  }
  for (const auto* adapter = adapters; adapter; adapter = adapter->Next) {
    const bool up = adapter->OperStatus == IfOperStatusUp;
    const bool loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
    for (const auto* unicast = adapter->FirstUnicastAddress; unicast;
         unicast = unicast->Next) {
      if (!unicast->Address.lpSockaddr ||
          unicast->Address.lpSockaddr->sa_family != AF_INET ||
          unicast->OnLinkPrefixLength > 32) {
        continue;
      }
      const auto* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
      const auto prefix = static_cast<unsigned>(unicast->OnLinkPrefixLength);
      const auto mask = prefix == 0 ? 0U : 0xFFFFFFFFU << (32U - prefix);
      std::string name = adapter->AdapterName ? adapter->AdapterName : "interface";
      interfaces.push_back({std::move(name), sockaddr_ipv4(*address), from_u32(mask), up,
                            loopback});
    }
  }
#else
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) {
    return interfaces;
  }
  for (const auto* current = list; current; current = current->ifa_next) {
    if (!current->ifa_addr || current->ifa_addr->sa_family != AF_INET ||
        !current->ifa_netmask) {
      continue;
    }
    const auto* address = reinterpret_cast<const sockaddr_in*>(current->ifa_addr);
    const auto* netmask = reinterpret_cast<const sockaddr_in*>(current->ifa_netmask);
    const auto name = current->ifa_name ? current->ifa_name : "interface";
    interfaces.push_back({name, sockaddr_ipv4(*address), sockaddr_ipv4(*netmask),
                          (current->ifa_flags & IFF_UP) != 0,
                          (current->ifa_flags & IFF_LOOPBACK) != 0});
  }
  freeifaddrs(list);
#endif
  return interfaces;
}

bool same_target(const IPv4Octets& left, const IPv4Octets& right) noexcept {
  return left == right;
}

bool permission_error(const asio::error_code& error) noexcept {
#ifdef _WIN32
  return error == asio::error::access_denied || error.value() == WSAEACCES;
#else
  return error == asio::error::access_denied;
#endif
}

}  // namespace

std::optional<IPv4Octets> directed_broadcast(IPv4Octets address,
                                             IPv4Octets netmask) noexcept {
  unsigned prefix_length = 0;
  const auto mask = to_u32(netmask);
  if (!valid_netmask(mask, prefix_length) || prefix_length >= 31U) {
    return std::nullopt;
  }
  return from_u32((to_u32(address) & mask) | ~mask);
}

std::vector<IPv4Octets> discovery_targets(
    const std::vector<DiscoveryInterface>& interfaces) {
  std::vector<IPv4Octets> targets;
  bool usable_interface = false;
  for (const auto& interface : interfaces) {
    if (!interface.up || interface.loopback) {
      continue;
    }
    usable_interface = true;
    const auto target = directed_broadcast(interface.address, interface.netmask);
    if (target && std::ranges::none_of(targets, [&](const IPv4Octets& existing) {
          return same_target(existing, *target);
        })) {
      targets.push_back(*target);
    }
  }
  const IPv4Octets limited_broadcast{255, 255, 255, 255};
  if (usable_interface && std::ranges::none_of(
                              targets, [&](const IPv4Octets& existing) {
                                return same_target(existing, limited_broadcast);
                              })) {
    targets.push_back(limited_broadcast);
  }
  return targets;
}

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

std::string format_discovered_host(const DiscoveredHost& host) {
  const auto identity = system_label(host.system) + " | " + host.device_name;
  const auto parameters = codec_label(host.capabilities) + " | " +
                          std::to_string(host.max_width) + "x" +
                          std::to_string(host.max_height) + " " +
                          std::to_string(host.max_fps) + " fps | " +
                          (host.capabilities.hdr10 ? "HDR10" : "SDR") + " | " +
                          (host.capabilities.audio ? "Audio" : "No audio");
  return identity + "\n" + parameters;
}

struct DiscoveryHost::Impl {
  asio::io_context io;
  asio::ip::udp::socket socket{io};
};

DiscoveryHost::DiscoveryHost() : impl_(std::make_unique<Impl>()) {}
DiscoveryHost::~DiscoveryHost() = default;
DiscoveryHost::DiscoveryHost(DiscoveryHost&&) noexcept = default;
DiscoveryHost& DiscoveryHost::operator=(DiscoveryHost&&) noexcept = default;

Result<void, DiscoveryError> DiscoveryHost::start(DiscoveryConfig config) {
  if (config.port == 0) {
    return Result<void, DiscoveryError>::err(DiscoveryError::Bind);
  }
  asio::error_code error;
  impl_->socket.open(asio::ip::udp::v4(), error);
  if (error) {
    return Result<void, DiscoveryError>::err(DiscoveryError::Bind);
  }
  impl_->socket.set_option(asio::socket_base::reuse_address(true), error);
  impl_->socket.bind({asio::ip::udp::v4(), config.port}, error);
  if (error) {
    return Result<void, DiscoveryError>::err(DiscoveryError::Bind);
  }
  impl_->socket.non_blocking(true, error);
  if (error) {
    return Result<void, DiscoveryError>::err(DiscoveryError::Bind);
  }
  std::clog << "discovery listening: 0.0.0.0:" << config.port << '\n';
  return Result<void, DiscoveryError>::ok();
}

Result<bool, DiscoveryError> DiscoveryHost::poll(
    const DiscoveryAdvertisement& advertisement) {
  if (!advertisement.controllable) {
    return Result<bool, DiscoveryError>::ok(false);
  }
  std::array<std::byte, kMaxDiscoveryBytes> buffer{};
  bool replied = false;
  for (unsigned count = 0; count < 64; ++count) {
    asio::ip::udp::endpoint sender;
    asio::error_code error;
    const auto received = impl_->socket.receive_from(asio::buffer(buffer), sender, 0, error);
    if (error == asio::error::would_block || error == asio::error::try_again) {
      break;
    }
    if (error == asio::error::message_size) {
      continue;
    }
    if (error) {
      return Result<bool, DiscoveryError>::err(DiscoveryError::Receive);
    }
    if (!is_discovery_query(std::span{buffer}.first(received))) {
      continue;
    }
    const auto reply = encode_discovery_advertisement(advertisement);
    impl_->socket.send_to(asio::buffer(reply), sender, 0, error);
    if (error) {
      return Result<bool, DiscoveryError>::err(
          permission_error(error) ? DiscoveryError::PermissionDenied : DiscoveryError::Send);
    }
    replied = true;
    std::clog << "discovery reply target=" << sender.address().to_string() << ":"
              << sender.port() << " device=" << advertisement.device_name
              << " session_port=" << advertisement.session_port << '\n';
  }
  return Result<bool, DiscoveryError>::ok(replied);
}

struct DiscoveryClient::Impl {
  explicit Impl(DiscoveryConfig config, DiscoveryInterfaceProvider provider)
      : config(std::move(config)), provider(std::move(provider)), socket(io) {}

  DiscoveryConfig config;
  DiscoveryInterfaceProvider provider;
  asio::io_context io;
  asio::ip::udp::socket socket;
  std::vector<IPv4Octets> targets;
  std::vector<DiscoveredHost> hosts;
  std::optional<SteadyClock::time_point> deadline;
  std::optional<SteadyClock::time_point> next_send;
  DiscoveryState state{DiscoveryState::Idle};
  std::optional<DiscoveryError> error;
};

namespace {

DiscoveryPollResult snapshot(const DiscoveryClient::Impl& impl) {
  return {impl.state, impl.hosts, impl.error};
}

void set_failure(DiscoveryClient::Impl& impl, DiscoveryError error) {
  impl.state = DiscoveryState::Failed;
  impl.error = error;
  impl.deadline.reset();
  impl.next_send.reset();
}

}  // namespace

DiscoveryClient::DiscoveryClient(DiscoveryConfig config,
                                 DiscoveryInterfaceProvider provider)
    : impl_(std::make_unique<Impl>(
          std::move(config), provider ? std::move(provider) : DiscoveryInterfaceProvider{
                                            [] { return enumerate_interfaces(); }})) {}

DiscoveryClient::~DiscoveryClient() { stop(); }
DiscoveryClient::DiscoveryClient(DiscoveryClient&&) noexcept = default;
DiscoveryClient& DiscoveryClient::operator=(DiscoveryClient&&) noexcept = default;

Result<void, DiscoveryError> DiscoveryClient::start(Microseconds timeout) {
  stop();
  if (impl_->config.port == 0) {
    set_failure(*impl_, DiscoveryError::Bind);
    return Result<void, DiscoveryError>::err(DiscoveryError::Bind);
  }
  if (impl_->config.target_override.empty()) {
    const auto interfaces = impl_->provider();
    for (const auto& interface : interfaces) {
      if (!interface.up || interface.loopback) {
        continue;
      }
      if (const auto broadcast = directed_broadcast(interface.address, interface.netmask);
          broadcast) {
        std::clog << "discovery interface name=" << interface.name
                  << " address=" << format_ipv4(interface.address)
                  << " netmask=" << format_ipv4(interface.netmask)
                  << " broadcast=" << format_ipv4(*broadcast) << '\n';
      }
    }
    impl_->targets = discovery_targets(interfaces);
  } else {
    impl_->targets = impl_->config.target_override;
  }
  if (impl_->targets.empty()) {
    set_failure(*impl_, DiscoveryError::NoUsableInterface);
    return Result<void, DiscoveryError>::err(DiscoveryError::NoUsableInterface);
  }

  asio::error_code error;
  impl_->socket.open(asio::ip::udp::v4(), error);
  if (error) {
    set_failure(*impl_, permission_error(error) ? DiscoveryError::PermissionDenied
                                                : DiscoveryError::Bind);
    return Result<void, DiscoveryError>::err(*impl_->error);
  }
  impl_->socket.set_option(asio::socket_base::broadcast(true), error);
  if (!error) {
    impl_->socket.bind({asio::ip::udp::v4(), 0}, error);
  }
  if (!error) {
    impl_->socket.non_blocking(true, error);
  }
  if (error) {
    impl_->socket.close();
    set_failure(*impl_, permission_error(error) ? DiscoveryError::PermissionDenied
                                                : DiscoveryError::Bind);
    return Result<void, DiscoveryError>::err(*impl_->error);
  }

  const auto now = SteadyClock::now();
  impl_->hosts.clear();
  impl_->error.reset();
  impl_->state = DiscoveryState::Searching;
  impl_->deadline = now + std::max(timeout, Microseconds{0});
  impl_->next_send = now;
  return Result<void, DiscoveryError>::ok();
}

DiscoveryPollResult DiscoveryClient::poll(SteadyClock::time_point now) {
  if (impl_->state != DiscoveryState::Searching || !impl_->deadline ||
      !impl_->socket.is_open()) {
    return snapshot(*impl_);
  }

  if (now < *impl_->deadline && impl_->next_send && now >= *impl_->next_send) {
    const auto query = encode_discovery_query();
    bool sent = false;
    bool permission_denied = false;
    for (const auto& target : impl_->targets) {
      asio::error_code error;
      const auto endpoint = asio::ip::udp::endpoint(asio::ip::address_v4(target),
                                                    impl_->config.port);
      impl_->socket.send_to(asio::buffer(query), endpoint, 0, error);
      if (error) {
        permission_denied = permission_denied || permission_error(error);
        continue;
      }
      sent = true;
      std::clog << "discovery query target=" << endpoint.address().to_string() << ":"
                << endpoint.port() << '\n';
    }
    if (!sent) {
      set_failure(*impl_, permission_denied ? DiscoveryError::PermissionDenied
                                            : DiscoveryError::Send);
      return snapshot(*impl_);
    }
    const auto interval = std::max(impl_->config.retry_interval, Microseconds{1});
    impl_->next_send = now + interval;
  }

  std::array<std::byte, kMaxDiscoveryBytes> buffer{};
  for (unsigned count = 0; count < 256; ++count) {
    asio::ip::udp::endpoint sender;
    asio::error_code error;
    const auto received = impl_->socket.receive_from(asio::buffer(buffer), sender, 0, error);
    if (error == asio::error::would_block || error == asio::error::try_again) {
      break;
    }
    if (error == asio::error::message_size) {
      continue;
    }
    if (error) {
      set_failure(*impl_, permission_error(error) ? DiscoveryError::PermissionDenied
                                                  : DiscoveryError::Receive);
      return snapshot(*impl_);
    }
    const auto advertisement =
        decode_discovery_advertisement(std::span{buffer}.first(received));
    if (!advertisement || !advertisement->controllable || !sender.address().is_v4()) {
      continue;
    }
    const auto address = sender.address().to_string();
    const auto duplicate = std::ranges::any_of(impl_->hosts, [&](const DiscoveredHost& host) {
      return host.address == address && host.session_port == advertisement->session_port;
    });
    if (duplicate) {
      continue;
    }
    impl_->hosts.push_back({advertisement->system,
                            advertisement->device_name,
                            address,
                            advertisement->session_port,
                            advertisement->capabilities,
                            advertisement->max_width,
                            advertisement->max_height,
                            advertisement->max_fps,
                            advertisement->controllable});
    std::clog << "discovery response sender=" << address << ":" << sender.port()
              << " device=" << advertisement->device_name
              << " session_port=" << advertisement->session_port << '\n';
  }

  if (now >= *impl_->deadline) {
    impl_->state = DiscoveryState::Complete;
    impl_->next_send.reset();
    impl_->deadline.reset();
  }
  return snapshot(*impl_);
}

DiscoveryState DiscoveryClient::state() const noexcept { return impl_->state; }
bool DiscoveryClient::active() const noexcept {
  return impl_->state == DiscoveryState::Searching;
}
std::optional<DiscoveryError> DiscoveryClient::last_error() const noexcept {
  return impl_->error;
}

void DiscoveryClient::stop() noexcept {
  if (!impl_) {
    return;
  }
  if (impl_->socket.is_open()) {
    asio::error_code error;
    impl_->socket.close(error);
  }
  impl_->targets.clear();
  impl_->hosts.clear();
  impl_->deadline.reset();
  impl_->next_send.reset();
  impl_->error.reset();
  impl_->state = DiscoveryState::Idle;
}

Result<std::vector<DiscoveredHost>, DiscoveryError> discover_hosts(Microseconds timeout) {
  DiscoveryClient client;
  if (const auto started = client.start(timeout); !started) {
    return Result<std::vector<DiscoveredHost>, DiscoveryError>::err(started.error());
  }
  while (client.active()) {
    const auto result = client.poll();
    if (result.state == DiscoveryState::Failed) {
      return Result<std::vector<DiscoveredHost>, DiscoveryError>::err(*result.error);
    }
    if (result.state == DiscoveryState::Complete) {
      return Result<std::vector<DiscoveredHost>, DiscoveryError>::ok(std::move(result.hosts));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return Result<std::vector<DiscoveredHost>, DiscoveryError>::ok({});
}

}  // namespace ministream
