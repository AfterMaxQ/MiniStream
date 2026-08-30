#pragma once

#include "core/base/result.hpp"
#include "core/audio/pcm_block.hpp"

#include <memory>

namespace ministream {

enum class CoreAudioCaptureError { Initialize, Format, Start, NoData };

class CoreAudioCapture {
 public:
  struct Impl;

  CoreAudioCapture();
  ~CoreAudioCapture();
  CoreAudioCapture(CoreAudioCapture&&) noexcept;
  CoreAudioCapture& operator=(CoreAudioCapture&&) noexcept;
  CoreAudioCapture(const CoreAudioCapture&) = delete;
  CoreAudioCapture& operator=(const CoreAudioCapture&) = delete;

  Result<void, CoreAudioCaptureError> start();
  Result<PcmBlock, CoreAudioCaptureError> read();
  void stop() noexcept;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
