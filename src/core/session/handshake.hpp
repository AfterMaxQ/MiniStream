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

struct Hello {
  VideoCodec codec{VideoCodec::H264};
  std::uint16_t width{};
  std::uint16_t height{};
  std::uint16_t fps{};
  std::uint32_t target_bitrate_bps{};
  std::uint64_t nonce{};
  bool operator==(const Hello&) const = default;
};

struct Accept {
  SessionId session_id{};
  VideoCodec codec{VideoCodec::H264};
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

 private:
  Hello hello_;
  std::optional<SteadyClock::time_point> last_send_;
  bool accepted_{};
};

}  // namespace ministream
