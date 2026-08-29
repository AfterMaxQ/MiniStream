#include "core/session/handshake.hpp"

#include <chrono>

namespace ministream {
namespace {

void put(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    bytes.push_back(static_cast<std::byte>(
        (value >> ((count - i - 1U) * 8U)) & 0xFFU));
  }
}

std::uint64_t get(std::span<const std::byte> bytes) {
  std::uint64_t value = 0;
  for (auto byte : bytes) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}

bool valid_codec(std::uint8_t codec) {
  return codec == static_cast<std::uint8_t>(VideoCodec::H264) ||
         codec == static_cast<std::uint8_t>(VideoCodec::Hevc);
}

}  // namespace

std::vector<std::byte> encode_hello(const Hello& hello) {
  std::vector<std::byte> bytes;
  bytes.reserve(20);
  bytes.push_back(std::byte{1});
  bytes.push_back(static_cast<std::byte>(hello.codec));
  put(bytes, hello.width, 2);
  put(bytes, hello.height, 2);
  put(bytes, hello.fps, 2);
  put(bytes, hello.target_bitrate_bps, 4);
  put(bytes, hello.nonce, 8);
  return bytes;
}

std::optional<Hello> decode_hello(std::span<const std::byte> bytes) {
  if (bytes.size() != 20 || bytes[0] != std::byte{1}) {
    return std::nullopt;
  }
  const auto codec = std::to_integer<std::uint8_t>(bytes[1]);
  const auto width = static_cast<std::uint16_t>(get(bytes.subspan(2, 2)));
  const auto height = static_cast<std::uint16_t>(get(bytes.subspan(4, 2)));
  const auto fps = static_cast<std::uint16_t>(get(bytes.subspan(6, 2)));
  if (!valid_codec(codec) || width == 0 || height == 0 || fps == 0) {
    return std::nullopt;
  }
  return Hello{
      static_cast<VideoCodec>(codec), width, height, fps,
      static_cast<std::uint32_t>(get(bytes.subspan(8, 4))), get(bytes.subspan(12, 8))};
}

std::vector<std::byte> encode_accept(const Accept& accept) {
  std::vector<std::byte> bytes;
  bytes.reserve(24);
  bytes.push_back(std::byte{2});
  bytes.push_back(static_cast<std::byte>(accept.codec));
  put(bytes, accept.width, 2);
  put(bytes, accept.height, 2);
  put(bytes, accept.fps, 2);
  put(bytes, accept.bitrate_bps, 4);
  put(bytes, accept.session_id, 4);
  put(bytes, accept.hello_nonce, 8);
  return bytes;
}

std::optional<Accept> decode_accept(std::span<const std::byte> bytes) {
  if (bytes.size() != 24 || bytes[0] != std::byte{2}) {
    return std::nullopt;
  }
  const auto codec = std::to_integer<std::uint8_t>(bytes[1]);
  const auto width = static_cast<std::uint16_t>(get(bytes.subspan(2, 2)));
  const auto height = static_cast<std::uint16_t>(get(bytes.subspan(4, 2)));
  const auto fps = static_cast<std::uint16_t>(get(bytes.subspan(6, 2)));
  if (!valid_codec(codec) || width == 0 || height == 0 || fps == 0) {
    return std::nullopt;
  }
  return Accept{
      static_cast<SessionId>(get(bytes.subspan(12, 4))),
      static_cast<VideoCodec>(codec), width, height, fps,
      static_cast<std::uint32_t>(get(bytes.subspan(8, 4))), get(bytes.subspan(16, 8))};
}

HandshakeRetrier::HandshakeRetrier(Hello hello) : hello_(hello) {}

std::optional<Hello> HandshakeRetrier::next_hello(SteadyClock::time_point now) {
  if (accepted_ || (last_send_ && now - *last_send_ < std::chrono::milliseconds{250})) {
    return std::nullopt;
  }
  last_send_ = now;
  return hello_;
}

bool HandshakeRetrier::accept(const Accept& accept) {
  accepted_ = accept.hello_nonce == hello_.nonce && accept.codec == hello_.codec &&
              accept.width == hello_.width && accept.height == hello_.height &&
              accept.fps == hello_.fps;
  return accepted_;
}

}  // namespace ministream
