#include "macos/video/video_surface_bridge.hpp"

namespace ministream {

VideoSurfaceBridge::VideoSurfaceBridge(QObject* parent) : QObject(parent) {}

VideoSurfaceBridge::~VideoSurfaceBridge() {
  std::scoped_lock lock(mutex_);
  if (latest_.pixel_buffer) {
    CVPixelBufferRelease(latest_.pixel_buffer);
  }
}

bool VideoSurfaceBridge::frameAvailable() const noexcept {
  std::scoped_lock lock(mutex_);
  return has_frame_;
}

void VideoSurfaceBridge::publish(CVPixelBufferRef pixel_buffer, std::uint64_t timestamp_us) {
  if (!pixel_buffer) return;
  CVPixelBufferRetain(pixel_buffer);
  {
    std::scoped_lock lock(mutex_);
    if (latest_.pixel_buffer) {
      CVPixelBufferRelease(latest_.pixel_buffer);
    }
    latest_ = {pixel_buffer, timestamp_us};
    has_frame_ = true;
  }
  emit frameAvailableChanged();
}

std::optional<SurfaceFrame> VideoSurfaceBridge::take() {
  std::scoped_lock lock(mutex_);
  if (!latest_.pixel_buffer) return std::nullopt;
  const auto frame = latest_;
  latest_ = {};
  return frame;
}

void VideoSurfaceBridge::clear() noexcept {
  bool had_frame = false;
  {
    std::scoped_lock lock(mutex_);
    had_frame = has_frame_;
    has_frame_ = false;
    if (latest_.pixel_buffer) {
      CVPixelBufferRelease(latest_.pixel_buffer);
      latest_ = {};
      had_frame = true;
    }
  }
  if (had_frame) {
    emit frameAvailableChanged();
  }
}

}  // namespace ministream
