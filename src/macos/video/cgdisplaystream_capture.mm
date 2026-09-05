#include "macos/video/cgdisplaystream_capture.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>

#include <dispatch/dispatch.h>

#include <chrono>
#include <mutex>
#include <utility>

namespace ministream {

struct CGDisplayStreamCapture::Impl {
  CGDisplayStreamRef stream{};
  CVPixelBufferRef latest{};
  std::uint64_t latest_timestamp_us{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::mutex mutex;
  std::uint64_t generation{};
};

CGDisplayStreamCapture::CGDisplayStreamCapture() : impl_(std::make_shared<Impl>()) {}
CGDisplayStreamCapture::~CGDisplayStreamCapture() { stop(); }
CGDisplayStreamCapture::CGDisplayStreamCapture(CGDisplayStreamCapture&&) noexcept = default;
CGDisplayStreamCapture& CGDisplayStreamCapture::operator=(CGDisplayStreamCapture&&) noexcept = default;

Result<void, DisplayCaptureError> CGDisplayStreamCapture::start(std::uint32_t width,
                                                                std::uint32_t height) {
  stop();
  if (@available(macOS 10.15, *)) {
    if (!CGPreflightScreenCaptureAccess()) {
      CGRequestScreenCaptureAccess();
      return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Permission);
    }
  }
  const auto display = CGMainDisplayID();
  const auto native_width = static_cast<std::uint32_t>(CGDisplayPixelsWide(display));
  const auto native_height = static_cast<std::uint32_t>(CGDisplayPixelsHigh(display));
  if (native_width == 0 || native_height == 0) {
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Initialize);
  }
  impl_->width = width == 0 ? native_width : width;
  impl_->height = height == 0 ? native_height : height;
  if (impl_->width == 0 || impl_->height == 0 || impl_->width > native_width ||
      impl_->height > native_height) {
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Initialize);
  }
  NSDictionary* properties = @{
      (__bridge NSString*)kCGDisplayStreamShowCursor: @NO,
  };
  dispatch_queue_t queue = dispatch_queue_create("com.aftermaxq.ministream.capture",
                                                  DISPATCH_QUEUE_SERIAL);
  // Stop can leave a callback already queued. Keep its state alive and reject
  // output from older capture sessions after a role/size change.
  const auto state = impl_;
  const auto generation = impl_->generation;
  impl_->stream = CGDisplayStreamCreateWithDispatchQueue(
      display, impl_->width, impl_->height, kCVPixelFormatType_32BGRA,
      (__bridge CFDictionaryRef)properties, queue,
      ^(CGDisplayStreamFrameStatus status, uint64_t display_time,
        IOSurfaceRef surface, CGDisplayStreamUpdateRef) {
        auto* impl = state.get();
        if (!impl || status != kCGDisplayStreamFrameStatusFrameComplete || !surface) {
          return;
        }
        CVPixelBufferRef pixel_buffer{};
        if (CVPixelBufferCreateWithIOSurface(kCFAllocatorDefault, surface, nullptr,
                                             &pixel_buffer) != kCVReturnSuccess ||
            !pixel_buffer) {
          return;
        }
        std::scoped_lock lock(impl->mutex);
        if (impl->generation != generation) {
          CVPixelBufferRelease(pixel_buffer);
          return;
        }
        if (impl->latest) {
          CVPixelBufferRelease(impl->latest);
        }
        impl->latest = pixel_buffer;
        impl->latest_timestamp_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        (void)display_time;
      });
  dispatch_release(queue);
  if (!impl_->stream || CGDisplayStreamStart(impl_->stream) != kCGErrorSuccess) {
    stop();
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Start);
  }
  return Result<void, DisplayCaptureError>::ok();
}

std::optional<CapturedDisplayFrame> CGDisplayStreamCapture::take_latest() {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->latest) {
    return std::nullopt;
  }
  CapturedDisplayFrame result{impl_->latest, impl_->latest_timestamp_us,
                              impl_->width, impl_->height};
  impl_->latest = nullptr;
  return result;
}

void CGDisplayStreamCapture::stop() noexcept {
  if (!impl_) {
    return;
  }
  {
    std::scoped_lock lock(impl_->mutex);
    ++impl_->generation;
  }
  if (impl_->stream) {
    CGDisplayStreamStop(impl_->stream);
    CFRelease(impl_->stream);
    impl_->stream = nullptr;
  }
  std::scoped_lock lock(impl_->mutex);
  if (impl_->latest) {
    CVPixelBufferRelease(impl_->latest);
    impl_->latest = nullptr;
  }
}

}  // namespace ministream
