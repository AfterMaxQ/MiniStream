#include "macos/video/cgdisplaystream_capture.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <chrono>
#include <mutex>

@interface MiniStreamDisplayOutput : NSObject <SCStreamOutput>
- (instancetype)initWithHandler:(void (^)(CMSampleBufferRef))handler;
@end

@implementation MiniStreamDisplayOutput {
  void (^_handler)(CMSampleBufferRef);
}
- (instancetype)initWithHandler:(void (^)(CMSampleBufferRef))handler {
  self = [super init];
  if (self) _handler = [handler copy];
  return self;
}
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sample
        ofType:(SCStreamOutputType)type {
  (void)stream;
  if (type == SCStreamOutputTypeScreen && _handler) _handler(sample);
}
@end

namespace ministream {

struct CGDisplayStreamCapture::Impl {
  SCStream* stream{};
  MiniStreamDisplayOutput* output{};
  dispatch_queue_t queue{};
  CVPixelBufferRef latest{};
  std::uint64_t latest_timestamp_us{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::mutex mutex;
  std::uint64_t generation{};
  ~Impl() { if (latest) CVPixelBufferRelease(latest); }
};

CGDisplayStreamCapture::CGDisplayStreamCapture() : impl_(std::make_shared<Impl>()) {}
CGDisplayStreamCapture::~CGDisplayStreamCapture() { stop(); }
CGDisplayStreamCapture::CGDisplayStreamCapture(CGDisplayStreamCapture&&) noexcept = default;
CGDisplayStreamCapture& CGDisplayStreamCapture::operator=(CGDisplayStreamCapture&&) noexcept = default;

Result<void, DisplayCaptureError> CGDisplayStreamCapture::start(std::uint32_t width,
                                                               std::uint32_t height, bool hdr10) {
  stop();
  if (!CGPreflightScreenCaptureAccess()) {
    CGRequestScreenCaptureAccess();
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Permission);
  }
  const auto display_id = CGMainDisplayID();
  const auto native_width = static_cast<std::uint32_t>(CGDisplayPixelsWide(display_id));
  const auto native_height = static_cast<std::uint32_t>(CGDisplayPixelsHigh(display_id));
  impl_->width = width == 0 ? native_width : width;
  impl_->height = height == 0 ? native_height : height;
  if (impl_->width == 0 || impl_->height == 0 || impl_->width > native_width ||
      impl_->height > native_height)
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Initialize);

  // Keep asynchronous completion storage alive even if the bounded wait times out.
  struct ContentRequest {
    SCShareableContent* content{};
    dispatch_semaphore_t done{dispatch_semaphore_create(0)};
  };
  const auto request = std::make_shared<ContentRequest>();
  [SCShareableContent getShareableContentWithCompletionHandler:
      ^(SCShareableContent* content, NSError* error) {
        if (!error) request->content = content;
        dispatch_semaphore_signal(request->done);
      }];
  if (dispatch_semaphore_wait(request->done,
      dispatch_time(DISPATCH_TIME_NOW, 3LL * NSEC_PER_SEC)) != 0 || !request->content)
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Initialize);
  SCDisplay* display = nil;
  for (SCDisplay* candidate in request->content.displays) {
    if (candidate.displayID == display_id) { display = candidate; break; }
  }
  if (!display) return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Initialize);

  SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
  SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
  config.width = impl_->width;
  config.height = impl_->height;
  config.pixelFormat = kCVPixelFormatType_32BGRA;
  if (hdr10) {
    if (@available(macOS 15.0, *)) {
      config.captureDynamicRange = SCCaptureDynamicRangeHDRCanonicalDisplay;
      config.pixelFormat = kCVPixelFormatType_ARGB2101010LEPacked;
      config.colorSpaceName = kCGColorSpaceITUR_2100_PQ;
    } else {
      return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Initialize);
    }
  }
  config.minimumFrameInterval = CMTimeMake(1, 60);
  config.queueDepth = 3;
  config.showsCursor = NO;
  config.capturesAudio = NO;
  const std::weak_ptr<Impl> weak_state = impl_;
  const auto generation = impl_->generation;
  impl_->output = [[MiniStreamDisplayOutput alloc] initWithHandler:^(CMSampleBufferRef sample) {
    const auto state = weak_state.lock();
    if (!state || !sample || !CMSampleBufferIsValid(sample) || !CMSampleBufferDataIsReady(sample)) return;
    NSArray* attachments = (__bridge NSArray*)CMSampleBufferGetSampleAttachmentsArray(sample, false);
    NSNumber* status = attachments.firstObject[SCStreamFrameInfoStatus];
    if (!status || status.integerValue != SCFrameStatusComplete) return;
    CVPixelBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
    if (!buffer) return;
    std::scoped_lock lock(state->mutex);
    if (state->generation != generation) return;
    CVPixelBufferRetain(buffer);
    if (state->latest) CVPixelBufferRelease(state->latest);
    state->latest = buffer;
    state->latest_timestamp_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
  }];
  impl_->queue = dispatch_queue_create("com.aftermaxq.ministream.capture", DISPATCH_QUEUE_SERIAL);
  impl_->stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];
  NSError* error = nil;
  if (!impl_->stream || ![impl_->stream addStreamOutput:impl_->output type:SCStreamOutputTypeScreen
      sampleHandlerQueue:impl_->queue error:&error]) {
    stop();
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Start);
  }
  struct StartRequest {
    bool succeeded{};
    dispatch_semaphore_t done{dispatch_semaphore_create(0)};
  };
  const auto started = std::make_shared<StartRequest>();
  [impl_->stream startCaptureWithCompletionHandler:^(NSError* start_error) {
    started->succeeded = start_error == nil;
    dispatch_semaphore_signal(started->done);
  }];
  if (dispatch_semaphore_wait(started->done,
      dispatch_time(DISPATCH_TIME_NOW, 3LL * NSEC_PER_SEC)) != 0 || !started->succeeded) {
    stop();
    return Result<void, DisplayCaptureError>::err(DisplayCaptureError::Start);
  }
  return Result<void, DisplayCaptureError>::ok();
}

std::optional<CapturedDisplayFrame> CGDisplayStreamCapture::take_latest() {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->latest) return std::nullopt;
  CapturedDisplayFrame result{impl_->latest, impl_->latest_timestamp_us, impl_->width, impl_->height};
  impl_->latest = nullptr;
  return result;
}

void CGDisplayStreamCapture::stop() noexcept {
  if (!impl_) return;
  {
    std::scoped_lock lock(impl_->mutex);
    ++impl_->generation;
    if (impl_->latest) CVPixelBufferRelease(impl_->latest);
    impl_->latest = nullptr;
  }
  SCStream* stream = impl_->stream;
  MiniStreamDisplayOutput* output = impl_->output;
  impl_->stream = nil;
  impl_->output = nil;
  impl_->queue = nil;
  if (stream) {
    if (output) [stream removeStreamOutput:output type:SCStreamOutputTypeScreen error:nullptr];
    [stream stopCaptureWithCompletionHandler:^(NSError* error) {
      // Retain both objects until the asynchronous stop has completed.
      (void)error;
      (void)stream;
      (void)output;
    }];
  }
}

}  // namespace ministream
