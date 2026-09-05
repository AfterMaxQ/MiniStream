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

class CGDisplayStreamCapture {
 public:
  CGDisplayStreamCapture();
  ~CGDisplayStreamCapture();
  CGDisplayStreamCapture(CGDisplayStreamCapture&&) noexcept;
  CGDisplayStreamCapture& operator=(CGDisplayStreamCapture&&) noexcept;
  CGDisplayStreamCapture(const CGDisplayStreamCapture&) = delete;
  CGDisplayStreamCapture& operator=(const CGDisplayStreamCapture&) = delete;

  Result<void, DisplayCaptureError> start(std::uint32_t width = 0,
                                          std::uint32_t height = 0);
  std::optional<CapturedDisplayFrame> take_latest();
  void stop() noexcept;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace ministream
