#pragma once

#include "core/base/result.hpp"
#include "core/audio/pcm_block.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace ministream {

enum class AudioCaptureError { Initialize, UnsupportedFormat, Start, NoData, Read };

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
