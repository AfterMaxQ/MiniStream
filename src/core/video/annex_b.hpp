#pragma once

#include "core/video/codec_config.hpp"

#include <span>
#include <vector>

namespace ministream {

// Views into one Annex B access unit; the caller keeps the bytes alive.
std::vector<std::span<const std::byte>> split_annex_b(std::span<const std::byte> bytes);
// Only SPS/PPS (and HEVC VPS) belong in the small codec configuration packet.
std::vector<std::byte> extract_parameter_sets(VideoCodec codec,
                                             std::span<const std::byte> bytes);

}  // namespace ministream
