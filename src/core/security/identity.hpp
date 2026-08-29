#pragma once

#include <array>
#include <cstddef>

namespace ministream {

struct DeviceIdentity {
  std::array<std::byte, 32> public_key{};
  std::array<std::byte, 64> secret_key{};
};

struct EphemeralKeyPair {
  std::array<std::byte, 32> public_key{};
  std::array<std::byte, 32> secret_key{};
};

struct SessionKeys {
  std::array<std::byte, 32> tx{};
  std::array<std::byte, 32> rx{};
};

using Signature = std::array<std::byte, 64>;

}  // namespace ministream
