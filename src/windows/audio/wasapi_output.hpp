#pragma once

#include "core/base/result.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace ministream {

enum class AudioOutputError { Initialize, Format, Start, Stopped, BufferFull };

class WasapiOutput {
 public:
  WasapiOutput();
  ~WasapiOutput();
  WasapiOutput(WasapiOutput&&) noexcept;
  WasapiOutput& operator=(WasapiOutput&&) noexcept;
  WasapiOutput(const WasapiOutput&) = delete;
  WasapiOutput& operator=(const WasapiOutput&) = delete;

  Result<void, AudioOutputError> start();
  Result<void, AudioOutputError> push(std::span<const float> interleaved_stereo);
  void stop() noexcept;
  [[nodiscard]] bool started() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
