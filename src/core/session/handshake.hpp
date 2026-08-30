#pragma once

#include "core/protocol/value_types.hpp"
#include "core/time/clock.hpp"
#include "core/video/codec_config.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ministream {

enum class HandshakeRole : std::uint8_t { Controller = 1, Controlled = 2 };

struct Hello {
  HandshakeRole sender_role{HandshakeRole::Controller};
  VideoCodec codec{VideoCodec::H264};
  bool hdr10{};
  std::uint16_t width{};
  std::uint16_t height{};
  std::uint16_t fps{};
  std::uint32_t target_bitrate_bps{};
  std::uint64_t nonce{};
  bool operator==(const Hello&) const = default;
};

struct Accept {
  HandshakeRole sender_role{HandshakeRole::Controlled};
  SessionId session_id{};
  VideoCodec codec{VideoCodec::H264};
  bool hdr10{};
  std::uint16_t width{};
  std::uint16_t height{};
  std::uint16_t fps{};
  std::uint32_t bitrate_bps{};
  std::uint64_t hello_nonce{};
  bool operator==(const Accept&) const = default;
};

std::vector<std::byte> encode_hello(const Hello& hello);
std::optional<Hello> decode_hello(std::span<const std::byte> bytes);
std::vector<std::byte> encode_accept(const Accept& accept);
std::optional<Accept> decode_accept(std::span<const std::byte> bytes);

class HandshakeRetrier {
 public:
  explicit HandshakeRetrier(Hello hello);
  std::optional<Hello> next_hello(SteadyClock::time_point now);
  bool accept(const Accept& accept);
  [[nodiscard]] bool exhausted() const noexcept;
  [[nodiscard]] bool expired(SteadyClock::time_point now) const noexcept;

 private:
  Hello hello_;
  std::optional<SteadyClock::time_point> last_send_;
  unsigned send_count_{};
  bool accepted_{};
};

class PairingMessageRetrier {
 public:
  [[nodiscard]] bool due(SteadyClock::time_point now) const noexcept;
  [[nodiscard]] bool expired(SteadyClock::time_point now) const noexcept;
  void sent(SteadyClock::time_point now) noexcept;
  void reset() noexcept;

 private:
  static constexpr unsigned kMaxSends = 4;
  std::optional<SteadyClock::time_point> last_send_;
  unsigned send_count_{};
};

}  // namespace ministream
