#include "macos/audio/coreaudio_capture.hpp"

#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <vector>

namespace ministream {

struct CoreAudioCapture::Impl {
  AudioUnit unit{};
  mutable std::mutex mutex;
  std::vector<float> samples;
  bool started{};
};

namespace {
OSStatus input_callback(void* refcon, AudioUnitRenderActionFlags*, const AudioTimeStamp* timestamp,
                        UInt32 bus, UInt32 frames, AudioBufferList*) {
  auto* impl = static_cast<CoreAudioCapture::Impl*>(refcon);
  if (!impl || !impl->unit || frames == 0 || frames > 4096) {
    return noErr;
  }
  std::array<float, 8192> buffer{};
  AudioBufferList list{};
  list.mNumberBuffers = 1;
  list.mBuffers[0].mNumberChannels = 2;
  list.mBuffers[0].mDataByteSize = static_cast<UInt32>(frames * 2U * sizeof(float));
  list.mBuffers[0].mData = buffer.data();
  if (AudioUnitRender(impl->unit, nullptr, timestamp, bus, frames, &list) != noErr) {
    return noErr;
  }
  std::scoped_lock lock(impl->mutex);
  constexpr std::size_t max_samples = 48'000U * 2U / 5U;
  if (impl->samples.size() + frames * 2U > max_samples) {
    const auto drop = impl->samples.size() + frames * 2U - max_samples;
    impl->samples.erase(impl->samples.begin(), impl->samples.begin() +
                                             static_cast<std::ptrdiff_t>(drop));
  }
  impl->samples.insert(impl->samples.end(), buffer.begin(),
                       buffer.begin() + static_cast<std::ptrdiff_t>(frames * 2U));
  return noErr;
}
}  // namespace

CoreAudioCapture::CoreAudioCapture() : impl_(std::make_unique<Impl>()) {}
CoreAudioCapture::~CoreAudioCapture() { stop(); }
CoreAudioCapture::CoreAudioCapture(CoreAudioCapture&&) noexcept = default;
CoreAudioCapture& CoreAudioCapture::operator=(CoreAudioCapture&&) noexcept = default;

Result<void, CoreAudioCaptureError> CoreAudioCapture::start() {
  stop();
  AudioComponentDescription description{};
  description.componentType = kAudioUnitType_Output;
  description.componentSubType = kAudioUnitSubType_DefaultInput;
  description.componentManufacturer = kAudioUnitManufacturer_Apple;
  const auto component = AudioComponentFindNext(nullptr, &description);
  if (!component || AudioComponentInstanceNew(component, &impl_->unit) != noErr) {
    return Result<void, CoreAudioCaptureError>::err(CoreAudioCaptureError::Initialize);
  }
  UInt32 enabled = 1;
  if (AudioUnitSetProperty(impl_->unit, kAudioOutputUnitProperty_EnableIO,
                           kAudioUnitScope_Input, 1, &enabled, sizeof(enabled)) != noErr) {
    stop();
    return Result<void, CoreAudioCaptureError>::err(CoreAudioCaptureError::Format);
  }
  AudioStreamBasicDescription format{};
  format.mSampleRate = 48'000.0;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
  format.mBytesPerPacket = sizeof(float) * 2U;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = sizeof(float) * 2U;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = sizeof(float) * 8U;
  if (AudioUnitSetProperty(impl_->unit, kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Output, 1, &format, sizeof(format)) != noErr) {
    stop();
    return Result<void, CoreAudioCaptureError>::err(CoreAudioCaptureError::Format);
  }
  AURenderCallbackStruct callback{input_callback, impl_.get()};
  if (AudioUnitSetProperty(impl_->unit, kAudioOutputUnitProperty_SetInputCallback,
                           kAudioUnitScope_Global, 1, &callback, sizeof(callback)) != noErr ||
      AudioUnitInitialize(impl_->unit) != noErr || AudioOutputUnitStart(impl_->unit) != noErr) {
    stop();
    return Result<void, CoreAudioCaptureError>::err(CoreAudioCaptureError::Start);
  }
  impl_->started = true;
  return Result<void, CoreAudioCaptureError>::ok();
}

Result<PcmBlock, CoreAudioCaptureError> CoreAudioCapture::read() {
  if (!impl_->started) {
    return Result<PcmBlock, CoreAudioCaptureError>::err(CoreAudioCaptureError::NoData);
  }
  constexpr std::size_t frame_samples = 480U * 2U;
  std::scoped_lock lock(impl_->mutex);
  if (impl_->samples.size() < frame_samples) {
    return Result<PcmBlock, CoreAudioCaptureError>::err(CoreAudioCaptureError::NoData);
  }
  PcmBlock block;
  block.host_timestamp_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  block.frames = 480;
  block.interleaved_stereo.assign(impl_->samples.begin(),
                                  impl_->samples.begin() + frame_samples);
  impl_->samples.erase(impl_->samples.begin(), impl_->samples.begin() + frame_samples);
  return Result<PcmBlock, CoreAudioCaptureError>::ok(std::move(block));
}

void CoreAudioCapture::stop() noexcept {
  if (!impl_) return;
  if (impl_->unit) {
    if (impl_->started) AudioOutputUnitStop(impl_->unit);
    AudioUnitUninitialize(impl_->unit);
    AudioComponentInstanceDispose(impl_->unit);
    impl_->unit = nullptr;
  }
  impl_->started = false;
  std::scoped_lock lock(impl_->mutex);
  impl_->samples.clear();
}

}  // namespace ministream
