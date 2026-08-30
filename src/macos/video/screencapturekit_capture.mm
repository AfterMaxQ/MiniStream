#include "macos/video/screencapturekit_capture.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>

#include <dispatch/dispatch.h>

#include <chrono>
#include <mutex>
#include <utility>

namespace ministream {

struct ScreenCaptureKitCapture::Impl {
  CGDisplayStreamRef stream{};
  CVPixelBufferRef latest{};
  std::uint64_t latest_timestamp_us{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::mutex mutex;
};

ScreenCaptureKitCapture::ScreenCaptureKitCapture() : impl_(std::make_unique<Impl>()) {}
ScreenCaptureKitCapture::~ScreenCaptureKitCapture() { stop(); }
ScreenCaptureKitCapture::ScreenCaptureKitCapture(ScreenCaptureKitCapture&&) noexcept = default;
ScreenCaptureKitCapture& ScreenCaptureKitCapture::operator=(ScreenCaptureKitCapture&&) noexcept = default;

Result<void, DisplayCaptureError> ScreenCaptureKitCapture::start() {
  stop();
  if (@available(macOS 10.15, *)) {
    if (!CGPreflightScreenCaptureAccess()) {
      CGRequestScreenCaptureAccess();
      return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Permission);
    }
  }
  const auto display = CGMainDisplayID();
  const auto width = static_cast<std::uint32_t>(CGDisplayPixelsWide(display));
  const auto height = static_cast<std::uint32_t>(CGDisplayPixelsHigh(display));
  if (width == 0 || height == 0) {
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Initialize);
  }
  impl_->width = width;
  impl_->height = height;
  NSDictionary* properties = @{
      (__bridge NSString*)kCGDisplayStreamShowCursor: @NO,
  };
  dispatch_queue_t queue = dispatch_queue_create("com.aftermaxq.ministream.capture",
                                                  DISPATCH_QUEUE_SERIAL);
  auto* weak_impl = impl_.get();
  impl_->stream = CGDisplayStreamCreateWithDispatchQueue(
      display, width, height, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)properties,
      queue, ^(CGDisplayStreamFrameStatus status, uint64_t display_time,
               IOSurfaceRef surface, CGDisplayStreamUpdateRef) {
        auto* impl = weak_impl;
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

std::optional<CapturedDisplayFrame> ScreenCaptureKitCapture::take_latest() {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->latest) {
    return std::nullopt;
  }
  CapturedDisplayFrame result{impl_->latest, impl_->latest_timestamp_us,
                              impl_->width, impl_->height};
  impl_->latest = nullptr;
  return result;
}

void ScreenCaptureKitCapture::stop() noexcept {
  if (!impl_) {
    return;
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
