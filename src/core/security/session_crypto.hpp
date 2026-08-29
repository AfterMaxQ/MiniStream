#pragma once

#include "core/base/result.hpp"
#include "core/protocol/types.hpp"
#include "core/protocol/value_types.hpp"
#include "core/security/pairing.hpp"
#include "core/security/replay_window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace ministream {

class SessionCrypto {
 public:
  SessionCrypto(
      SessionId session_id, std::array<std::byte, 32> tx_key,
      std::array<std::byte, 32> rx_key, std::uint32_t tx_nonce_prefix,
      std::uint32_t rx_nonce_prefix);

  Result<Datagram, CryptoError> seal(
      PacketType type, std::span<const std::byte> plaintext);
  Result<Datagram, CryptoError> seal(
      PacketType type, std::initializer_list<std::byte> plaintext) {
    return seal(type, std::span<const std::byte>{plaintext.begin(), plaintext.size()});
  }
  Result<std::vector<std::byte>, CryptoError> open(const Datagram& datagram);

 private:
  SessionId session_id_;
  std::array<std::byte, 32> tx_key_;
  std::array<std::byte, 32> rx_key_;
  std::uint32_t tx_nonce_prefix_;
  std::uint32_t rx_nonce_prefix_;
  std::uint64_t tx_counter_{};
  ReplayWindow replay_window_;
};

}  // namespace ministream
