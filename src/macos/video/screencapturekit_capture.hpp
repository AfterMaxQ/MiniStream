#pragma once

#include "core/base/result.hpp"

#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace ministream {

enum class DisplayCaptureError { Permission, Initialize, Start };

struct CapturedDisplayFrame {
  CVPixelBufferRef pixel_buffer{};
  std::uint64_t timestamp_us{};
  std::uint32_t width{};
  std::uint32_t height{};
};

class ScreenCaptureKitCapture {
 public:
  ScreenCaptureKitCapture();
  ~ScreenCaptureKitCapture();
  ScreenCaptureKitCapture(ScreenCaptureKitCapture&&) noexcept;
  ScreenCaptureKitCapture& operator=(ScreenCaptureKitCapture&&) noexcept;
  ScreenCaptureKitCapture(const ScreenCaptureKitCapture&) = delete;
  ScreenCaptureKitCapture& operator=(const ScreenCaptureKitCapture&) = delete;

  Result<void, DisplayCaptureError> start();
  std::optional<CapturedDisplayFrame> take_latest();
  void stop() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
