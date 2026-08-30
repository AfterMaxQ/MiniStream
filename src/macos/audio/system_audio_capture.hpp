#pragma once

#include "core/audio/pcm_block.hpp"
#include "core/base/result.hpp"

#include <memory>
#include <string>

namespace ministream {

enum class SystemAudioCaptureError { Unavailable, Permission, Initialize, Format, Start, NoData };

struct SystemAudioCapability {
  bool ready{};
  std::string detail;
};

class SystemAudioCapture {
 public:
  struct Impl;

  SystemAudioCapture();
  ~SystemAudioCapture();
  SystemAudioCapture(SystemAudioCapture&&) noexcept;
  SystemAudioCapture& operator=(SystemAudioCapture&&) noexcept;
  SystemAudioCapture(const SystemAudioCapture&) = delete;
  SystemAudioCapture& operator=(const SystemAudioCapture&) = delete;

  [[nodiscard]] static SystemAudioCapability inspect() noexcept;
  Result<void, SystemAudioCaptureError> start();
  Result<PcmBlock, SystemAudioCaptureError> read();
  void stop() noexcept;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
