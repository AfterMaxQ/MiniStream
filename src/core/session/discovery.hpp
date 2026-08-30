#pragma once

#include "core/base/result.hpp"
#include "core/time/clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ministream {

inline constexpr std::uint16_t kDiscoveryPort = 47990;
inline constexpr std::size_t kMaxDiscoveryBytes = 128;
inline constexpr std::size_t kMaxDiscoveryNameBytes = 48;

enum class DiscoverySystem : std::uint8_t { Unknown = 0, Windows = 1, MacOS = 2, Linux = 3 };

struct DiscoveryCapabilities {
  bool h264{};
  bool hevc{};
  bool hdr10{};
  bool audio{};
  bool keyboard_mouse{};
  bool gamepad{};

  friend bool operator==(const DiscoveryCapabilities&, const DiscoveryCapabilities&) = default;
};

struct DiscoveryAdvertisement {
  DiscoverySystem system{DiscoverySystem::Unknown};
  std::string device_name;
  std::uint16_t session_port{};
  DiscoveryCapabilities capabilities;
  std::uint16_t max_width{};
  std::uint16_t max_height{};
  std::uint16_t max_fps{};
  bool controllable{};

  friend bool operator==(const DiscoveryAdvertisement&,
                         const DiscoveryAdvertisement&) = default;
};

struct DiscoveredHost {
  DiscoverySystem system{DiscoverySystem::Unknown};
  std::string device_name;
  std::string address;
  std::uint16_t session_port{};
  DiscoveryCapabilities capabilities;
  std::uint16_t max_width{};
  std::uint16_t max_height{};
  std::uint16_t max_fps{};
  bool controllable{};
};

enum class DiscoveryError { Bind, Send, Receive, NoUsableInterface, PermissionDenied };

using IPv4Octets = std::array<std::uint8_t, 4>;

struct DiscoveryInterface {
  std::string name;
  IPv4Octets address{};
  IPv4Octets netmask{};
  bool up{};
  bool loopback{};
};

std::optional<IPv4Octets> directed_broadcast(IPv4Octets address,
                                             IPv4Octets netmask) noexcept;
std::vector<IPv4Octets> discovery_targets(
    const std::vector<DiscoveryInterface>& interfaces);

enum class DiscoveryState { Idle, Searching, Complete, Failed };

struct DiscoveryPollResult {
  DiscoveryState state{DiscoveryState::Idle};
  std::vector<DiscoveredHost> hosts;
  std::optional<DiscoveryError> error;
};

struct DiscoveryConfig {
  std::uint16_t port{kDiscoveryPort};
  Microseconds retry_interval{150'000};
  // Tests and local diagnostics may provide an explicit destination.  Normal
  // callers leave this empty so the client enumerates active IPv4 interfaces.
  std::vector<IPv4Octets> target_override;
};

using DiscoveryInterfaceProvider = std::function<std::vector<DiscoveryInterface>()>;

std::array<std::byte, 8> encode_discovery_query();
bool is_discovery_query(std::span<const std::byte> bytes);
std::vector<std::byte> encode_discovery_advertisement(
    const DiscoveryAdvertisement& advertisement);
std::optional<DiscoveryAdvertisement> decode_discovery_advertisement(
    std::span<const std::byte> bytes);
std::string format_discovered_host(const DiscoveredHost& host);

class DiscoveryHost {
 public:
  DiscoveryHost();
  ~DiscoveryHost();
  DiscoveryHost(DiscoveryHost&&) noexcept;
  DiscoveryHost& operator=(DiscoveryHost&&) noexcept;
  DiscoveryHost(const DiscoveryHost&) = delete;
  DiscoveryHost& operator=(const DiscoveryHost&) = delete;

  Result<void, DiscoveryError> start(DiscoveryConfig config = {});
  Result<bool, DiscoveryError> poll(const DiscoveryAdvertisement& advertisement);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class DiscoveryClient {
 public:
  struct Impl;

  explicit DiscoveryClient(DiscoveryConfig config = {},
                           DiscoveryInterfaceProvider provider = {});
  ~DiscoveryClient();
  DiscoveryClient(DiscoveryClient&&) noexcept;
  DiscoveryClient& operator=(DiscoveryClient&&) noexcept;
  DiscoveryClient(const DiscoveryClient&) = delete;
  DiscoveryClient& operator=(const DiscoveryClient&) = delete;

  Result<void, DiscoveryError> start(Microseconds timeout);
  DiscoveryPollResult poll(SteadyClock::time_point now = SteadyClock::now());
  [[nodiscard]] DiscoveryState state() const noexcept;
  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] std::optional<DiscoveryError> last_error() const noexcept;
  void stop() noexcept;

 private:
  std::unique_ptr<Impl> impl_;
};

Result<std::vector<DiscoveredHost>, DiscoveryError> discover_hosts(Microseconds timeout);

}  // namespace ministream
