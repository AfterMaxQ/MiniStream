#pragma once

#include "core/security/pairing.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ministream {

enum class PairingRole : std::uint8_t { Initiator = 1, Responder = 2 };

struct PairingOffer {
  PairingRole role{PairingRole::Initiator};
  std::uint64_t nonce{};
  std::array<std::byte, 32> identity{};
  std::array<std::byte, 32> ephemeral{};

  friend bool operator==(const PairingOffer&, const PairingOffer&) = default;
};

std::array<std::byte, 78> encode_pairing_offer(const PairingOffer& offer);
std::optional<PairingOffer> decode_pairing_offer(std::span<const std::byte> bytes);
std::array<std::byte, 6> encode_pairing_confirmation(bool accepted);
std::optional<bool> decode_pairing_confirmation(std::span<const std::byte> bytes);
std::optional<PairingTranscript> pairing_transcript(const PairingOffer& initiator,
                                                    const PairingOffer& responder);

}  // namespace ministream
