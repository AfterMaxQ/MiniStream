#pragma once

#include "core/base/result.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace ministream {

enum class CoreAudioError { Initialize, Format, Start, Stopped };

struct CoreAudioStats {
  std::uint64_t underruns{};
  std::size_t buffered_frames{};
};

class CoreAudioOutput {
 public:
  CoreAudioOutput();
  ~CoreAudioOutput();
  CoreAudioOutput(CoreAudioOutput&&) noexcept;
  CoreAudioOutput& operator=(CoreAudioOutput&&) noexcept;
  CoreAudioOutput(const CoreAudioOutput&) = delete;
  CoreAudioOutput& operator=(const CoreAudioOutput&) = delete;

  Result<void, CoreAudioError> start();
  Result<void, CoreAudioError> push(std::span<const float> interleaved_stereo);
  CoreAudioStats stats() const noexcept;
  void stop() noexcept;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
