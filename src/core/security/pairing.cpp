#include "core/security/pairing.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <span>

namespace ministream {
namespace {

bool ensure_sodium() {
  static std::once_flag flag;
  static bool ready = false;
  std::call_once(flag, [] { ready = sodium_init() >= 0; });
  return ready;
}

unsigned char* raw(std::byte* bytes) {
  return reinterpret_cast<unsigned char*>(bytes);
}
const unsigned char* raw(const std::byte* bytes) {
  return reinterpret_cast<const unsigned char*>(bytes);
}

void put_u64(std::span<std::byte, 8> output, std::uint64_t value) {
  for (std::size_t i = 0; i < output.size(); ++i) {
    output[i] = static_cast<std::byte>((value >> ((7U - i) * 8U)) & 0xFFU);
  }
}

std::array<std::byte, 48> signed_ephemeral_message(
    const std::array<std::byte, 32>& ephemeral, std::uint64_t initiator_nonce,
    std::uint64_t responder_nonce) {
  std::array<std::byte, 48> message{};
  std::copy(ephemeral.begin(), ephemeral.end(), message.begin());
  put_u64(std::span<std::byte, 8>{message.data() + 32, 8}, initiator_nonce);
  put_u64(std::span<std::byte, 8>{message.data() + 40, 8}, responder_nonce);
  return message;
}

}  // namespace

Result<DeviceIdentity, CryptoError> generate_identity() {
  if (!ensure_sodium()) {
    return Result<DeviceIdentity, CryptoError>::err(CryptoError::Initialization);
  }
  DeviceIdentity identity;
  if (crypto_sign_keypair(raw(identity.public_key.data()), raw(identity.secret_key.data())) != 0) {
    return Result<DeviceIdentity, CryptoError>::err(CryptoError::KeyExchangeFailed);
  }
  return Result<DeviceIdentity, CryptoError>::ok(std::move(identity));
}

Result<EphemeralKeyPair, CryptoError> generate_ephemeral_keypair() {
  if (!ensure_sodium()) {
    return Result<EphemeralKeyPair, CryptoError>::err(CryptoError::Initialization);
  }
  EphemeralKeyPair keypair;
  crypto_kx_keypair(raw(keypair.public_key.data()), raw(keypair.secret_key.data()));
  return Result<EphemeralKeyPair, CryptoError>::ok(std::move(keypair));
}

std::uint32_t compute_pairing_sas(const PairingTranscript& transcript) {
  std::array<std::byte, 145> bytes{};
  bytes[0] = static_cast<std::byte>(transcript.protocol_version);
  put_u64(std::span<std::byte, 8>{bytes.data() + 1, 8}, transcript.initiator_nonce);
  put_u64(std::span<std::byte, 8>{bytes.data() + 9, 8}, transcript.responder_nonce);
  auto output = bytes.begin() + 17;
  for (const auto* value : {
           &transcript.initiator_identity, &transcript.responder_identity,
           &transcript.initiator_ephemeral, &transcript.responder_ephemeral}) {
    output = std::copy(value->begin(), value->end(), output);
  }
  std::array<unsigned char, 8> hash{};
  if (!ensure_sodium() ||
      crypto_generichash(hash.data(), hash.size(), raw(bytes.data()), bytes.size(), nullptr, 0) != 0) {
    return 0;
  }
  std::uint64_t value = 0;
  for (auto byte : hash) {
    value = (value << 8U) | byte;
  }
  return static_cast<std::uint32_t>(value % 1'000'000U);
}

Result<Signature, CryptoError> sign_session_ephemeral(
    const DeviceIdentity& identity, const std::array<std::byte, 32>& ephemeral,
    std::uint64_t initiator_nonce, std::uint64_t responder_nonce) {
  if (!ensure_sodium()) {
    return Result<Signature, CryptoError>::err(CryptoError::Initialization);
  }
  const auto message =
      signed_ephemeral_message(ephemeral, initiator_nonce, responder_nonce);
  Signature signature{};
  if (crypto_sign_detached(
          raw(signature.data()), nullptr, raw(message.data()), message.size(),
          raw(identity.secret_key.data())) != 0) {
    return Result<Signature, CryptoError>::err(CryptoError::KeyExchangeFailed);
  }
  return Result<Signature, CryptoError>::ok(signature);
}

bool verify_session_ephemeral(
    const std::array<std::byte, 32>& identity_public,
    const std::array<std::byte, 32>& ephemeral, std::uint64_t initiator_nonce,
    std::uint64_t responder_nonce, const Signature& signature) {
  if (!ensure_sodium()) {
    return false;
  }
  const auto message =
      signed_ephemeral_message(ephemeral, initiator_nonce, responder_nonce);
  return crypto_sign_verify_detached(
             raw(signature.data()), raw(message.data()), message.size(),
             raw(identity_public.data())) == 0;
}

Result<SessionKeys, CryptoError> derive_session_keys(
    const EphemeralKeyPair& local, const std::array<std::byte, 32>& peer_public,
    bool initiator) {
  if (!ensure_sodium()) {
    return Result<SessionKeys, CryptoError>::err(CryptoError::Initialization);
  }
  SessionKeys keys;
  const auto result = initiator
                          ? crypto_kx_client_session_keys(
                                raw(keys.rx.data()), raw(keys.tx.data()),
                                raw(local.public_key.data()), raw(local.secret_key.data()),
                                raw(peer_public.data()))
                          : crypto_kx_server_session_keys(
                                raw(keys.rx.data()), raw(keys.tx.data()),
                                raw(local.public_key.data()), raw(local.secret_key.data()),
                                raw(peer_public.data()));
  if (result != 0) {
    return Result<SessionKeys, CryptoError>::err(CryptoError::KeyExchangeFailed);
  }
  return Result<SessionKeys, CryptoError>::ok(keys);
}

}  // namespace ministream
