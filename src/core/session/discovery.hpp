#pragma once

#include "core/base/result.hpp"
#include "core/time/clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ministream {

inline constexpr std::uint16_t kDiscoveryPort = 47990;
inline constexpr std::size_t kMaxDiscoveryBytes = 64;

struct DiscoveryAdvertisement {
  std::string name;
  std::uint16_t session_port{};

  friend bool operator==(const DiscoveryAdvertisement&,
                         const DiscoveryAdvertisement&) = default;
};

struct DiscoveredHost {
  std::string name;
  std::string address;
  std::uint16_t session_port{};
};

enum class DiscoveryError { Bind, Send, Receive };

std::array<std::byte, 8> encode_discovery_query();
bool is_discovery_query(std::span<const std::byte> bytes);
std::vector<std::byte> encode_discovery_advertisement(
    const DiscoveryAdvertisement& advertisement);
std::optional<DiscoveryAdvertisement> decode_discovery_advertisement(
    std::span<const std::byte> bytes);

class DiscoveryHost {
 public:
  DiscoveryHost();
  ~DiscoveryHost();
  DiscoveryHost(DiscoveryHost&&) noexcept;
  DiscoveryHost& operator=(DiscoveryHost&&) noexcept;
  DiscoveryHost(const DiscoveryHost&) = delete;
  DiscoveryHost& operator=(const DiscoveryHost&) = delete;

  Result<void, DiscoveryError> start();
  Result<bool, DiscoveryError> poll(const DiscoveryAdvertisement& advertisement);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

Result<std::vector<DiscoveredHost>, DiscoveryError> discover_hosts(Microseconds timeout);

}  // namespace ministream
