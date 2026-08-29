#pragma once

#include "core/base/result.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace ministream {

enum class AudioCaptureError { Initialize, UnsupportedFormat, Start, NoData, Read };

struct PcmBlock {
  std::uint64_t host_timestamp_us{};
  std::uint32_t frames{};
  std::vector<float> interleaved_stereo;
  bool discontinuity{};
};

class WasapiLoopback {
 public:
  WasapiLoopback();
  ~WasapiLoopback();
  WasapiLoopback(WasapiLoopback&&) noexcept;
  WasapiLoopback& operator=(WasapiLoopback&&) noexcept;
  WasapiLoopback(const WasapiLoopback&) = delete;
  WasapiLoopback& operator=(const WasapiLoopback&) = delete;

  Result<void, AudioCaptureError> start();
  Result<PcmBlock, AudioCaptureError> read();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
