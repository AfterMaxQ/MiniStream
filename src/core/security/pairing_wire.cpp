#include "core/security/pairing_wire.hpp"
#include "core/protocol/wire.hpp"

#include <algorithm>

namespace ministream {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'M'}, std::byte{'S'}, std::byte{'P'}, std::byte{'2'}};
constexpr std::byte kOffer{1};
constexpr std::byte kConfirmation{2};

void put_u64(std::span<std::byte, 8> output, std::uint64_t value) {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<std::byte>(value >> ((7U - index) * 8U));
  }
}

std::uint64_t get_u64(std::span<const std::byte, 8> input) {
  std::uint64_t value{};
  for (const auto byte : input) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}

bool valid_header(std::span<const std::byte> bytes, std::byte kind) {
  return bytes.size() >= 6 && std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) &&
         bytes[4] == kind;
}

}  // namespace

std::array<std::byte, 78> encode_pairing_offer(const PairingOffer& offer) {
  std::array<std::byte, 78> bytes{};
  std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
  bytes[4] = kOffer;
  bytes[5] = static_cast<std::byte>(offer.role);
  put_u64(std::span<std::byte, 8>{bytes.data() + 6, 8}, offer.nonce);
  std::copy(offer.identity.begin(), offer.identity.end(), bytes.begin() + 14);
  std::copy(offer.ephemeral.begin(), offer.ephemeral.end(), bytes.begin() + 46);
  return bytes;
}

std::optional<PairingOffer> decode_pairing_offer(std::span<const std::byte> bytes) {
  if (bytes.size() != 78 || !valid_header(bytes, kOffer)) {
    return std::nullopt;
  }
  const auto role = std::to_integer<std::uint8_t>(bytes[5]);
  if (role < static_cast<std::uint8_t>(PairingRole::Initiator) ||
      role > static_cast<std::uint8_t>(PairingRole::Responder)) {
    return std::nullopt;
  }
  PairingOffer offer;
  offer.role = static_cast<PairingRole>(role);
  offer.nonce = get_u64(std::span<const std::byte, 8>{bytes.data() + 6, 8});
  std::copy_n(bytes.begin() + 14, 32, offer.identity.begin());
  std::copy_n(bytes.begin() + 46, 32, offer.ephemeral.begin());
  return offer;
}

std::array<std::byte, 6> encode_pairing_confirmation(bool accepted) {
  return {kMagic[0], kMagic[1], kMagic[2], kMagic[3], kConfirmation,
          accepted ? std::byte{1} : std::byte{0}};
}

std::optional<bool> decode_pairing_confirmation(std::span<const std::byte> bytes) {
  if (bytes.size() != 6 || !valid_header(bytes, kConfirmation) ||
      (bytes[5] != std::byte{0} && bytes[5] != std::byte{1})) {
    return std::nullopt;
  }
  return bytes[5] == std::byte{1};
}

std::optional<PairingTranscript> pairing_transcript(const PairingOffer& initiator,
                                                    const PairingOffer& responder) {
  if (initiator.role != PairingRole::Initiator ||
      responder.role != PairingRole::Responder) {
    return std::nullopt;
  }
  return PairingTranscript{kProtocolVersion,
                           initiator.nonce,
                           responder.nonce,
                           initiator.identity,
                           responder.identity,
                           initiator.ephemeral,
                           responder.ephemeral};
}

}  // namespace ministream
