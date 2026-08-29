#pragma once

#include "core/base/result.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace ministream {

inline constexpr std::size_t kOpusFrameSamplesPerChannel = 480;

enum class AudioCodecError { Initialization, InvalidFrame, Encode, Decode };

class OpusEncoder48kStereo {
 public:
  OpusEncoder48kStereo();
  ~OpusEncoder48kStereo();
  OpusEncoder48kStereo(const OpusEncoder48kStereo&) = delete;
  OpusEncoder48kStereo& operator=(const OpusEncoder48kStereo&) = delete;

  [[nodiscard]] bool ready() const noexcept;
  Result<std::vector<std::byte>, AudioCodecError> encode(
      std::span<const float> interleaved_stereo);

 private:
  void* encoder_{};
};

class OpusDecoder48kStereo {
 public:
  OpusDecoder48kStereo();
  ~OpusDecoder48kStereo();
  OpusDecoder48kStereo(const OpusDecoder48kStereo&) = delete;
  OpusDecoder48kStereo& operator=(const OpusDecoder48kStereo&) = delete;

  [[nodiscard]] bool ready() const noexcept;
  Result<std::vector<float>, AudioCodecError> decode(std::span<const std::byte> packet);
  Result<std::vector<float>, AudioCodecError> decode_loss();

 private:
  Result<std::vector<float>, AudioCodecError> decode_impl(
      const unsigned char* packet, std::size_t bytes, bool fec);
  void* decoder_{};
};

}  // namespace ministream
