#include "core/session/handshake.hpp"
#include "core/protocol/wire.hpp"

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

bool valid_role(std::uint8_t role) {
  return role == static_cast<std::uint8_t>(HandshakeRole::Controller) ||
         role == static_cast<std::uint8_t>(HandshakeRole::Controlled);
}

constexpr std::byte kHelloKind{1};
constexpr std::byte kAcceptKind{2};

}  // namespace

std::vector<std::byte> encode_hello(const Hello& hello) {
  std::vector<std::byte> bytes;
  bytes.reserve(23);
  bytes.push_back(static_cast<std::byte>(kProtocolVersion));
  bytes.push_back(kHelloKind);
  bytes.push_back(static_cast<std::byte>(hello.sender_role));
  bytes.push_back(static_cast<std::byte>(hello.codec));
  bytes.push_back(hello.hdr10 ? std::byte{1} : std::byte{0});
  put(bytes, hello.width, 2);
  put(bytes, hello.height, 2);
  put(bytes, hello.fps, 2);
  put(bytes, hello.target_bitrate_bps, 4);
  put(bytes, hello.nonce, 8);
  return bytes;
}

std::optional<Hello> decode_hello(std::span<const std::byte> bytes) {
  if (bytes.size() != 23 || bytes[0] != static_cast<std::byte>(kProtocolVersion) ||
      bytes[1] != kHelloKind || (bytes[4] != std::byte{0} && bytes[4] != std::byte{1})) {
    return std::nullopt;
  }
  const auto role = std::to_integer<std::uint8_t>(bytes[2]);
  const auto codec = std::to_integer<std::uint8_t>(bytes[3]);
  const auto width = static_cast<std::uint16_t>(get(bytes.subspan(5, 2)));
  const auto height = static_cast<std::uint16_t>(get(bytes.subspan(7, 2)));
  const auto fps = static_cast<std::uint16_t>(get(bytes.subspan(9, 2)));
  if (role != static_cast<std::uint8_t>(HandshakeRole::Controller) ||
      !valid_role(role) || !valid_codec(codec) || width == 0 || height == 0 || fps == 0) {
    return std::nullopt;
  }
  return Hello{static_cast<HandshakeRole>(role), static_cast<VideoCodec>(codec),
               bytes[4] == std::byte{1}, width, height, fps,
               static_cast<std::uint32_t>(get(bytes.subspan(11, 4))),
               get(bytes.subspan(15, 8))};
}

std::vector<std::byte> encode_accept(const Accept& accept) {
  std::vector<std::byte> bytes;
  bytes.reserve(27);
  bytes.push_back(static_cast<std::byte>(kProtocolVersion));
  bytes.push_back(kAcceptKind);
  bytes.push_back(static_cast<std::byte>(accept.sender_role));
  bytes.push_back(static_cast<std::byte>(accept.codec));
  bytes.push_back(accept.hdr10 ? std::byte{1} : std::byte{0});
  put(bytes, accept.width, 2);
  put(bytes, accept.height, 2);
  put(bytes, accept.fps, 2);
  put(bytes, accept.bitrate_bps, 4);
  put(bytes, accept.session_id, 4);
  put(bytes, accept.hello_nonce, 8);
  return bytes;
}

std::optional<Accept> decode_accept(std::span<const std::byte> bytes) {
  if (bytes.size() != 27 || bytes[0] != static_cast<std::byte>(kProtocolVersion) ||
      bytes[1] != kAcceptKind || (bytes[4] != std::byte{0} && bytes[4] != std::byte{1})) {
    return std::nullopt;
  }
  const auto role = std::to_integer<std::uint8_t>(bytes[2]);
  const auto codec = std::to_integer<std::uint8_t>(bytes[3]);
  const auto width = static_cast<std::uint16_t>(get(bytes.subspan(5, 2)));
  const auto height = static_cast<std::uint16_t>(get(bytes.subspan(7, 2)));
  const auto fps = static_cast<std::uint16_t>(get(bytes.subspan(9, 2)));
  if (role != static_cast<std::uint8_t>(HandshakeRole::Controlled) ||
      !valid_role(role) || !valid_codec(codec) || width == 0 || height == 0 || fps == 0) {
    return std::nullopt;
  }
  return Accept{static_cast<HandshakeRole>(role),
                static_cast<SessionId>(get(bytes.subspan(15, 4))),
                static_cast<VideoCodec>(codec), bytes[4] == std::byte{1}, width, height,
                fps, static_cast<std::uint32_t>(get(bytes.subspan(11, 4))),
                get(bytes.subspan(19, 8))};
}

HandshakeRetrier::HandshakeRetrier(Hello hello) : hello_(hello) {}

std::optional<Hello> HandshakeRetrier::next_hello(SteadyClock::time_point now) {
  if (accepted_ || send_count_ >= 4U ||
      (last_send_ && now - *last_send_ < std::chrono::milliseconds{250})) {
    return std::nullopt;
  }
  last_send_ = now;
  ++send_count_;
  return hello_;
}

bool HandshakeRetrier::accept(const Accept& accept) {
  accepted_ = hello_.sender_role == HandshakeRole::Controller &&
              accept.sender_role == HandshakeRole::Controlled &&
              accept.hello_nonce == hello_.nonce && accept.codec == hello_.codec &&
              accept.width == hello_.width && accept.height == hello_.height &&
              accept.fps == hello_.fps && accept.hdr10 == hello_.hdr10 &&
              accept.session_id != 0 && accept.bitrate_bps != 0;
  return accepted_;
}

bool HandshakeRetrier::exhausted() const noexcept {
  return !accepted_ && send_count_ >= 4U;
}

bool HandshakeRetrier::expired(SteadyClock::time_point now) const noexcept {
  return exhausted() && last_send_ &&
         now - *last_send_ >= std::chrono::milliseconds{250};
}

PairingMessageRetrier::PairingMessageRetrier(Microseconds interval) noexcept
    : interval_(interval) {}

bool PairingMessageRetrier::due(SteadyClock::time_point now) const noexcept {
  return send_count_ < kMaxSends &&
         (!last_send_ || now - *last_send_ >= interval_);
}

bool PairingMessageRetrier::expired(SteadyClock::time_point now) const noexcept {
  return send_count_ >= kMaxSends && last_send_ &&
         now - *last_send_ >= interval_;
}

void PairingMessageRetrier::sent(SteadyClock::time_point now) noexcept {
  if (send_count_ < kMaxSends) {
    last_send_ = now;
    ++send_count_;
  }
}

void PairingMessageRetrier::reset() noexcept {
  last_send_.reset();
  send_count_ = 0;
}

}  // namespace ministream
