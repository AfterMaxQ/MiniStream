#include "macos/audio/system_audio_capture.hpp"

#import <AudioToolbox/AudioToolbox.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace ministream {

struct SystemAudioCapture::Impl {
  SCStream* stream{};
  NSObject* output{};
  dispatch_queue_t queue{};
  mutable std::mutex mutex;
  std::deque<PcmBlock> blocks;
  static constexpr std::size_t kMaxBlocks = 16;
  std::atomic_bool started{};

  void append(CMSampleBufferRef sample_buffer);
};

namespace {

constexpr std::uint32_t kOutputSampleRate = 48'000;
constexpr std::uint32_t kOutputChannels = 2;

std::uint64_t now_us() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
}

float read_sample(const AudioBuffer& buffer, std::size_t frame, std::size_t channel,
                  const AudioStreamBasicDescription& format) {
  if (!buffer.mData || buffer.mNumberChannels == 0) {
    return 0.0F;
  }
  const auto channels = static_cast<std::size_t>(buffer.mNumberChannels);
  const auto index = frame * channels + std::min(channel, channels - 1U);
  if ((format.mFormatFlags & kAudioFormatFlagIsFloat) != 0 &&
      format.mBitsPerChannel == 32) {
    return static_cast<const float*>(buffer.mData)[index];
  }
  if ((format.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0 &&
      format.mBitsPerChannel == 16) {
    return static_cast<const std::int16_t*>(buffer.mData)[index] / 32768.0F;
  }
  if ((format.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0 &&
      format.mBitsPerChannel == 32) {
    return static_cast<const std::int32_t*>(buffer.mData)[index] / 2147483648.0F;
  }
  return 0.0F;
}

bool convert_sample_buffer(CMSampleBufferRef sample_buffer, PcmBlock& result) {
  if (!sample_buffer || !CMSampleBufferDataIsReady(sample_buffer)) {
    return false;
  }
  const auto* format = CMAudioFormatDescriptionGetStreamBasicDescription(
      CMSampleBufferGetFormatDescription(sample_buffer));
  if (!format || format->mSampleRate != static_cast<Float64>(kOutputSampleRate) ||
      format->mChannelsPerFrame == 0 || format->mChannelsPerFrame > 8 ||
      format->mBitsPerChannel == 0) {
    return false;
  }

  size_t list_size = 0;
  const auto list_status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
      sample_buffer, &list_size, nullptr, 0, kCFAllocatorDefault, kCFAllocatorDefault,
      kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, nullptr);
  if ((list_status != noErr && list_status != kCMSampleBufferError_ArrayTooSmall) ||
      list_size < sizeof(AudioBufferList)) {
    return false;
  }
  std::vector<std::byte> storage(list_size);
  auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
  if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
          sample_buffer, &list_size, list, list_size, kCFAllocatorDefault, kCFAllocatorDefault,
          kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, nullptr) != noErr) {
    return false;
  }
  const auto frames = static_cast<std::size_t>(CMSampleBufferGetNumSamples(sample_buffer));
  if (frames == 0 || list->mNumberBuffers == 0) {
    return false;
  }

  result.host_timestamp_us = now_us();
  const auto timestamp = CMSampleBufferGetPresentationTimeStamp(sample_buffer);
  if (CMTIME_IS_VALID(timestamp) && timestamp.timescale != 0) {
    const auto seconds = CMTimeGetSeconds(timestamp);
    if (std::isfinite(seconds) && seconds >= 0.0) {
      result.host_timestamp_us = static_cast<std::uint64_t>(seconds * 1'000'000.0);
    }
  }
  result.frames = static_cast<std::uint32_t>(frames);
  result.interleaved_stereo.resize(frames * kOutputChannels);

  std::size_t channel_offset = 0;
  for (std::size_t buffer_index = 0; buffer_index < list->mNumberBuffers; ++buffer_index) {
    const auto& buffer = list->mBuffers[buffer_index];
    const auto channels = static_cast<std::size_t>(buffer.mNumberChannels);
    if (channels == 0) {
      continue;
    }
    const auto output_channels = std::min<std::size_t>(channels, kOutputChannels - channel_offset);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < output_channels; ++channel) {
        result.interleaved_stereo[frame * kOutputChannels + channel_offset + channel] =
            read_sample(buffer, frame, channel, *format);
      }
    }
    channel_offset += output_channels;
    if (channel_offset == kOutputChannels) {
      break;
    }
  }
  if (channel_offset == 1) {
    for (std::size_t frame = frames; frame-- > 0;) {
      result.interleaved_stereo[frame * kOutputChannels + 1] =
          result.interleaved_stereo[frame * kOutputChannels];
    }
  } else if (channel_offset == 0) {
    return false;
  }
  return true;
}

@interface MiniStreamSystemAudioOutput : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithImpl:(ministream::SystemAudioCapture::Impl*)impl;
@end

@implementation MiniStreamSystemAudioOutput {
  ministream::SystemAudioCapture::Impl* _impl;
}

- (instancetype)initWithImpl:(ministream::SystemAudioCapture::Impl*)impl {
  self = [super init];
  if (self) {
    _impl = impl;
  }
  return self;
}

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                  ofType:(SCStreamOutputType)type {
  (void)stream;
  if (type == SCStreamOutputTypeAudio && _impl) {
    _impl->append(sampleBuffer);
  }
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
  (void)stream;
  (void)error;
  if (_impl) {
    _impl->started.store(false, std::memory_order_release);
  }
}
@end

}  // namespace

void SystemAudioCapture::Impl::append(CMSampleBufferRef sample_buffer) {
  PcmBlock block;
  if (!convert_sample_buffer(sample_buffer, block)) {
    return;
  }
  std::scoped_lock lock(mutex);
  if (blocks.size() == kMaxBlocks) {
    blocks.pop_front();
  }
  blocks.push_back(std::move(block));
}

SystemAudioCapture::SystemAudioCapture() : impl_(std::make_unique<Impl>()) {}
SystemAudioCapture::~SystemAudioCapture() { stop(); }
SystemAudioCapture::SystemAudioCapture(SystemAudioCapture&&) noexcept = default;
SystemAudioCapture& SystemAudioCapture::operator=(SystemAudioCapture&&) noexcept = default;

SystemAudioCapability SystemAudioCapture::inspect() noexcept {
  if (@available(macOS 12.3, *)) {
    if (@available(macOS 10.15, *)) {
      if (!CGPreflightScreenCaptureAccess()) {
        return {false, "Screen Recording permission required for system audio"};
      }
    }
    return {true, "ScreenCaptureKit system audio available"};
  }
  return {false, "ScreenCaptureKit system audio requires macOS 12.3 or newer"};
}

Result<void, SystemAudioCaptureError> SystemAudioCapture::start() {
  stop();
  const auto capability = inspect();
  if (!capability.ready) {
    if (capability.detail.find("permission") != std::string::npos) {
      if (@available(macOS 10.15, *)) {
        CGRequestScreenCaptureAccess();
      }
      return Result<void, SystemAudioCaptureError>::err(SystemAudioCaptureError::Permission);
    }
    return Result<void, SystemAudioCaptureError>::err(SystemAudioCaptureError::Unavailable);
  }

  __block SCShareableContent* content = nil;
  __block NSError* content_error = nil;
  dispatch_semaphore_t content_done = dispatch_semaphore_create(0);
  dispatch_retain(content_done);
  [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* value,
                                                                  NSError* error) {
    content = [value retain];
    content_error = [error retain];
    dispatch_semaphore_signal(content_done);
    dispatch_release(content_done);
  }];
  const auto wait_result = dispatch_semaphore_wait(
      content_done, dispatch_time(DISPATCH_TIME_NOW, 3LL * NSEC_PER_SEC));
  dispatch_release(content_done);
  if (wait_result != 0 || !content || content.displays.count == 0) {
    [content_error release];
    [content release];
    return Result<void, SystemAudioCaptureError>::err(
        wait_result == 0 ? SystemAudioCaptureError::Unavailable
                         : SystemAudioCaptureError::Initialize);
  }

  SCDisplay* display = content.displays.firstObject;
  SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display
                                                   excludingApplications:@[]
                                                        exceptingWindows:@[]];
  SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
  configuration.capturesAudio = YES;
  configuration.excludesCurrentProcessAudio = NO;
  configuration.sampleRate = kOutputSampleRate;
  configuration.channelCount = kOutputChannels;
  impl_->queue = dispatch_queue_create("com.aftermaxq.ministream.system-audio",
                                       DISPATCH_QUEUE_SERIAL);
  auto* output = [[MiniStreamSystemAudioOutput alloc] initWithImpl:impl_.get()];
  NSError* error = nil;
  impl_->stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:output];
  const auto added = impl_->stream &&
                     [impl_->stream addStreamOutput:output
                                                 type:SCStreamOutputTypeAudio
                                   sampleHandlerQueue:impl_->queue
                                                error:&error];
  if (!added) {
    [output release];
    [configuration release];
    [filter release];
    [content release];
    stop();
    return Result<void, SystemAudioCaptureError>::err(SystemAudioCaptureError::Initialize);
  }

  __block bool completed = false;
  __block NSError* start_error = nil;
  dispatch_semaphore_t start_done = dispatch_semaphore_create(0);
  dispatch_retain(start_done);
  [impl_->stream startCaptureWithCompletionHandler:^(NSError* value) {
    start_error = [value retain];
    completed = true;
    dispatch_semaphore_signal(start_done);
    dispatch_release(start_done);
  }];
  const auto start_wait = dispatch_semaphore_wait(
      start_done, dispatch_time(DISPATCH_TIME_NOW, 3LL * NSEC_PER_SEC));
  dispatch_release(start_done);
  [configuration release];
  [filter release];
  [content release];
  if (start_wait != 0 || !completed || start_error != nil) {
    [start_error release];
    [output release];
    stop();
    return Result<void, SystemAudioCaptureError>::err(SystemAudioCaptureError::Start);
  }
  [start_error release];
  impl_->output = output;
  impl_->started.store(true, std::memory_order_release);
  return Result<void, SystemAudioCaptureError>::ok();
}

Result<PcmBlock, SystemAudioCaptureError> SystemAudioCapture::read() {
  if (!impl_ || !impl_->started.load(std::memory_order_acquire)) {
    return Result<PcmBlock, SystemAudioCaptureError>::err(SystemAudioCaptureError::NoData);
  }
  std::scoped_lock lock(impl_->mutex);
  if (impl_->blocks.empty()) {
    return Result<PcmBlock, SystemAudioCaptureError>::err(SystemAudioCaptureError::NoData);
  }
  auto result = std::move(impl_->blocks.front());
  impl_->blocks.pop_front();
  return Result<PcmBlock, SystemAudioCaptureError>::ok(std::move(result));
}

void SystemAudioCapture::stop() noexcept {
  if (!impl_) {
    return;
  }
  if (impl_->stream) {
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_retain(done);
    [impl_->stream stopCaptureWithCompletionHandler:^(NSError*) {
      dispatch_semaphore_signal(done);
      dispatch_release(done);
    }];
    (void)dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 1LL * NSEC_PER_SEC));
    dispatch_release(done);
    [impl_->stream release];
    impl_->stream = nil;
  }
  if (impl_->queue) {
    dispatch_release(impl_->queue);
    impl_->queue = nil;
  }
  if (impl_->output) {
    [impl_->output release];
    impl_->output = nil;
  }
  impl_->started.store(false, std::memory_order_release);
  std::scoped_lock lock(impl_->mutex);
  impl_->blocks.clear();
}

}  // namespace ministream
