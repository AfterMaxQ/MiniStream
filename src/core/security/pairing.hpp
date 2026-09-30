#pragma once

#include "core/base/result.hpp"
#include "core/security/identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

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
Result<DeviceIdentity, CryptoError> identity_from_seed(std::span<const std::byte> seed);
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

Result<Signature, CryptoError> sign_pairing_authorization(
    const DeviceIdentity& identity, const PairingTranscript& transcript,
    bool initiator, bool accepted);
bool verify_pairing_authorization(const PairingTranscript& transcript,
                                 bool initiator, bool accepted, const Signature& signature);

class PairingTrust {
 public:
  using PublicKey = std::array<std::byte, 32>;
  using Save = std::function<bool(const std::vector<PublicKey>&)>;
  PairingTrust(DeviceIdentity identity, std::vector<PublicKey> peers = {}, Save save = {});
  ~PairingTrust();
  [[nodiscard]] const DeviceIdentity& identity() const noexcept { return identity_; }
  [[nodiscard]] bool trusted(const PublicKey& peer) const;
  bool remember(const PublicKey& peer);
  bool forget_all();
  [[nodiscard]] std::size_t size() const noexcept { return peers_.size(); }

 private:
  DeviceIdentity identity_;
  std::vector<PublicKey> peers_;
  Save save_;
};

class PairingConfirmation {
 public:
  void confirm_local() noexcept { local_ = true; }
  void confirm_peer() noexcept { peer_ = true; }
  [[nodiscard]] bool local_confirmed() const noexcept { return local_; }
  [[nodiscard]] bool peer_confirmed() const noexcept { return peer_; }
  [[nodiscard]] bool ready() const noexcept { return local_ && peer_; }

 private:
  bool local_{};
  bool peer_{};
};

}  // namespace ministream
