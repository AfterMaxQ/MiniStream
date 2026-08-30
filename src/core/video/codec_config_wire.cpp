#include "core/video/codec_config_wire.hpp"

#include "core/protocol/value_types.hpp"

namespace ministream {
namespace {
void put(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(static_cast<std::byte>(value >> 24U));
  out.push_back(static_cast<std::byte>(value >> 16U));
  out.push_back(static_cast<std::byte>(value >> 8U));
  out.push_back(static_cast<std::byte>(value));
}
std::uint32_t get(std::span<const std::byte> in, std::size_t offset) {
  return (std::to_integer<std::uint32_t>(in[offset]) << 24U) |
         (std::to_integer<std::uint32_t>(in[offset + 1]) << 16U) |
         (std::to_integer<std::uint32_t>(in[offset + 2]) << 8U) |
         std::to_integer<std::uint32_t>(in[offset + 3]);
}
}  // namespace

std::vector<std::byte> encode_codec_config(const CodecConfig& config) {
  if (config.width == 0 || config.height == 0 || config.fps == 0 ||
      config.parameter_sets.size() > kMaxDatagramBytes - 17U) {
    return {};
  }
  std::vector<std::byte> bytes;
  bytes.reserve(17U + config.parameter_sets.size());
  bytes.push_back(std::byte{1});
  bytes.push_back(static_cast<std::byte>(config.codec));
  bytes.push_back(config.hdr10 ? std::byte{1} : std::byte{0});
  put(bytes, config.width);
  put(bytes, config.height);
  put(bytes, config.fps);
  const auto size = static_cast<std::uint16_t>(config.parameter_sets.size());
  bytes.push_back(static_cast<std::byte>(size >> 8U));
  bytes.push_back(static_cast<std::byte>(size));
  bytes.insert(bytes.end(), config.parameter_sets.begin(), config.parameter_sets.end());
  return bytes;
}

std::optional<CodecConfig> decode_codec_config(std::span<const std::byte> bytes) {
  if (bytes.size() < 17U || std::to_integer<std::uint8_t>(bytes[0]) != 1U ||
      (std::to_integer<std::uint8_t>(bytes[1]) != static_cast<std::uint8_t>(VideoCodec::H264) &&
       std::to_integer<std::uint8_t>(bytes[1]) != static_cast<std::uint8_t>(VideoCodec::Hevc))) {
    return std::nullopt;
  }
  const auto size = static_cast<std::size_t>(
      (std::to_integer<std::uint16_t>(bytes[15]) << 8U) |
      std::to_integer<std::uint16_t>(bytes[16]));
  if (size == 0 || bytes.size() != 17U + size || get(bytes, 3) == 0 || get(bytes, 7) == 0 ||
      get(bytes, 11) == 0) {
    return std::nullopt;
  }
  CodecConfig config;
  config.codec = static_cast<VideoCodec>(std::to_integer<std::uint8_t>(bytes[1]));
  config.hdr10 = std::to_integer<std::uint8_t>(bytes[2]) != 0;
  config.width = get(bytes, 3);
  config.height = get(bytes, 7);
  config.fps = get(bytes, 11);
  config.parameter_sets.assign(bytes.begin() + 17, bytes.end());
  return config;
}

}  // namespace ministream
