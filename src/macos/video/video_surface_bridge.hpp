#pragma once

#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <mutex>
#include <optional>

namespace ministream {

struct SurfaceFrame {
  CVPixelBufferRef pixel_buffer{};
  std::uint64_t timestamp_us{};
};

class VideoSurfaceBridge {
 public:
  VideoSurfaceBridge() = default;
  ~VideoSurfaceBridge();

  [[nodiscard]] bool frameAvailable() const noexcept;
  void publish(CVPixelBufferRef pixel_buffer, std::uint64_t timestamp_us);
  std::optional<SurfaceFrame> take();

 private:
  mutable std::mutex mutex_;
  SurfaceFrame latest_;
};

}  // namespace ministream
