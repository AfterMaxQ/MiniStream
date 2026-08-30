#pragma once

#include "core/video/codec_config.hpp"

#include <optional>
#include <span>
#include <vector>

namespace ministream {

std::vector<std::byte> encode_codec_config(const CodecConfig& config);
std::optional<CodecConfig> decode_codec_config(std::span<const std::byte> bytes);

}  // namespace ministream
