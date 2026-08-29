#pragma once

#include "core/base/result.hpp"
#include "core/security/identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ministream {

enum class CryptoError {
  Initialization,
  InvalidPacket,
  AuthenticationFailed,
  Replay,
  NonceExhausted,
  KeyExchangeFailed,
};

struct PairingTranscript {
  std::uint8_t protocol_version{};
  std::uint64_t initiator_nonce{};
  std::uint64_t responder_nonce{};
  std::array<std::byte, 32> initiator_identity{};
  std::array<std::byte, 32> responder_identity{};
  std::array<std::byte, 32> initiator_ephemeral{};
  std::array<std::byte, 32> responder_ephemeral{};
};

Result<DeviceIdentity, CryptoError> generate_identity();
Result<EphemeralKeyPair, CryptoError> generate_ephemeral_keypair();
std::uint32_t compute_pairing_sas(const PairingTranscript& transcript);
Result<Signature, CryptoError> sign_session_ephemeral(
    const DeviceIdentity& identity, const std::array<std::byte, 32>& ephemeral,
    std::uint64_t initiator_nonce, std::uint64_t responder_nonce);
bool verify_session_ephemeral(
    const std::array<std::byte, 32>& identity_public,
    const std::array<std::byte, 32>& ephemeral, std::uint64_t initiator_nonce,
    std::uint64_t responder_nonce, const Signature& signature);
Result<SessionKeys, CryptoError> derive_session_keys(
    const EphemeralKeyPair& local, const std::array<std::byte, 32>& peer_public,
    bool initiator);

class PairingConfirmation {
 public:
  void confirm_local() noexcept { local_ = true; }
  void confirm_peer() noexcept { peer_ = true; }
  [[nodiscard]] bool ready() const noexcept { return local_ && peer_; }

 private:
  bool local_{};
  bool peer_{};
};

}  // namespace ministream
