#include "core/security/session_crypto.hpp"

#include "core/protocol/wire.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <limits>
#include <mutex>

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

void put_u32(std::span<std::byte, 4> output, std::uint32_t value) {
  for (std::size_t i = 0; i < output.size(); ++i) {
    output[i] = static_cast<std::byte>((value >> ((3U - i) * 8U)) & 0xFFU);
  }
}

void put_u64(std::span<std::byte, 8> output, std::uint64_t value) {
  for (std::size_t i = 0; i < output.size(); ++i) {
    output[i] = static_cast<std::byte>((value >> ((7U - i) * 8U)) & 0xFFU);
  }
}

std::uint64_t get_u64(std::span<const std::byte, 8> input) {
  std::uint64_t value = 0;
  for (auto byte : input) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}

std::array<std::byte, crypto_aead_chacha20poly1305_ietf_NPUBBYTES> nonce(
    std::uint32_t prefix, std::uint64_t counter) {
  std::array<std::byte, crypto_aead_chacha20poly1305_ietf_NPUBBYTES> result{};
  put_u32(std::span<std::byte, 4>{result.data(), 4}, prefix);
  put_u64(std::span<std::byte, 8>{result.data() + 4, 8}, counter);
  return result;
}

}  // namespace

SessionCrypto::SessionCrypto(
    SessionId session_id, std::array<std::byte, 32> tx_key,
    std::array<std::byte, 32> rx_key, std::uint32_t tx_nonce_prefix,
    std::uint32_t rx_nonce_prefix)
    : session_id_(session_id),
      tx_key_(std::move(tx_key)),
      rx_key_(std::move(rx_key)),
      tx_nonce_prefix_(tx_nonce_prefix),
      rx_nonce_prefix_(rx_nonce_prefix) {}

Result<Datagram, CryptoError> SessionCrypto::seal(
    PacketType type, std::span<const std::byte> plaintext) {
  constexpr auto overhead = kCommonHeaderBytes + sizeof(std::uint64_t) +
                            crypto_aead_chacha20poly1305_ietf_ABYTES;
  if (!ensure_sodium()) {
    return Result<Datagram, CryptoError>::err(CryptoError::Initialization);
  }
  if (plaintext.size() + overhead > kMaxDatagramBytes) {
    return Result<Datagram, CryptoError>::err(CryptoError::InvalidPacket);
  }
  if (tx_counter_ == std::numeric_limits<std::uint64_t>::max()) {
    return Result<Datagram, CryptoError>::err(CryptoError::NonceExhausted);
  }

  const auto counter = tx_counter_++;
  const auto payload_bytes = static_cast<std::uint16_t>(
      sizeof(counter) + plaintext.size() + crypto_aead_chacha20poly1305_ietf_ABYTES);
  const auto common = encode_common_header({type, session_id_, payload_bytes});
  std::array<std::byte, sizeof(counter)> counter_bytes{};
  put_u64(std::span<std::byte, 8>{counter_bytes.data(), 8}, counter);
  std::array<std::byte, kCommonHeaderBytes + sizeof(counter)> associated{};
  std::copy(common.begin(), common.end(), associated.begin());
  std::copy(counter_bytes.begin(), counter_bytes.end(), associated.begin() + kCommonHeaderBytes);

  std::vector<std::byte> ciphertext(
      plaintext.size() + crypto_aead_chacha20poly1305_ietf_ABYTES);
  unsigned long long ciphertext_bytes = 0;
  const auto packet_nonce = nonce(tx_nonce_prefix_, counter);
  if (crypto_aead_chacha20poly1305_ietf_encrypt(
          raw(ciphertext.data()), &ciphertext_bytes, raw(plaintext.data()), plaintext.size(),
          raw(associated.data()), associated.size(), nullptr, raw(packet_nonce.data()),
          raw(tx_key_.data())) != 0) {
    return Result<Datagram, CryptoError>::err(CryptoError::AuthenticationFailed);
  }
  ciphertext.resize(static_cast<std::size_t>(ciphertext_bytes));

  Datagram datagram;
  datagram.bytes.reserve(associated.size() + ciphertext.size());
  datagram.bytes.insert(datagram.bytes.end(), associated.begin(), associated.end());
  datagram.bytes.insert(datagram.bytes.end(), ciphertext.begin(), ciphertext.end());
  return Result<Datagram, CryptoError>::ok(std::move(datagram));
}

Result<std::vector<std::byte>, CryptoError> SessionCrypto::open(
    const Datagram& datagram) {
  if (!ensure_sodium()) {
    return Result<std::vector<std::byte>, CryptoError>::err(CryptoError::Initialization);
  }
  const auto bytes = std::span<const std::byte>{datagram.bytes};
  constexpr auto minimum = kCommonHeaderBytes + sizeof(std::uint64_t) +
                           crypto_aead_chacha20poly1305_ietf_ABYTES;
  if (bytes.size() < minimum || bytes.size() > kMaxDatagramBytes) {
    return Result<std::vector<std::byte>, CryptoError>::err(CryptoError::InvalidPacket);
  }
  const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
  if (!common || common->session_id != session_id_ ||
      common->payload_bytes != bytes.size() - kCommonHeaderBytes) {
    return Result<std::vector<std::byte>, CryptoError>::err(CryptoError::InvalidPacket);
  }
  const auto counter = get_u64(bytes.subspan<kCommonHeaderBytes, sizeof(std::uint64_t)>());
  if (!replay_window_.would_accept(counter)) {
    return Result<std::vector<std::byte>, CryptoError>::err(CryptoError::Replay);
  }

  const auto associated = bytes.first(kCommonHeaderBytes + sizeof(std::uint64_t));
  const auto ciphertext = bytes.subspan(associated.size());
  std::vector<std::byte> plaintext(
      ciphertext.size() - crypto_aead_chacha20poly1305_ietf_ABYTES);
  unsigned long long plaintext_bytes = 0;
  const auto packet_nonce = nonce(rx_nonce_prefix_, counter);
  if (crypto_aead_chacha20poly1305_ietf_decrypt(
          raw(plaintext.data()), &plaintext_bytes, nullptr, raw(ciphertext.data()),
          ciphertext.size(), raw(associated.data()), associated.size(),
          raw(packet_nonce.data()), raw(rx_key_.data())) != 0) {
    return Result<std::vector<std::byte>, CryptoError>::err(
        CryptoError::AuthenticationFailed);
  }
  plaintext.resize(static_cast<std::size_t>(plaintext_bytes));
  replay_window_.commit(counter);
  return Result<std::vector<std::byte>, CryptoError>::ok(std::move(plaintext));
}

}  // namespace ministream
