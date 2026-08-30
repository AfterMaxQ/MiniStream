#include "macos/audio/coreaudio_output.hpp"

#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace ministream {

struct CoreAudioOutput::Impl {
  AudioUnit unit{};
  mutable std::mutex mutex;
  std::deque<float> samples;
  std::size_t max_samples{48'000U * 2U / 5U};  // 200 ms
  std::atomic<std::uint64_t> underruns{};
  bool started{};
};

namespace {
OSStatus render_callback(void* refcon, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                         UInt32, UInt32 frames, AudioBufferList* output) {
  auto* impl = static_cast<CoreAudioOutput::Impl*>(refcon);
  if (!impl || output->mNumberBuffers == 0) {
    return noErr;
  }
  const auto requested = static_cast<std::size_t>(frames) * 2U;
  std::vector<float> scratch(requested, 0.0F);
  {
    std::scoped_lock lock(impl->mutex);
    const auto available = std::min(requested, impl->samples.size());
    for (std::size_t i = 0; i < available; ++i) {
      scratch[i] = impl->samples.front();
      impl->samples.pop_front();
    }
    if (available < requested) {
      ++impl->underruns;
    }
  }
  if (output->mBuffers[0].mData != nullptr) {
    std::memcpy(output->mBuffers[0].mData, scratch.data(),
                requested * sizeof(float));
  }
  return noErr;
}
}  // namespace

CoreAudioOutput::CoreAudioOutput() : impl_(std::make_unique<Impl>()) {}
CoreAudioOutput::~CoreAudioOutput() { stop(); }
CoreAudioOutput::CoreAudioOutput(CoreAudioOutput&&) noexcept = default;
CoreAudioOutput& CoreAudioOutput::operator=(CoreAudioOutput&&) noexcept = default;

Result<void, CoreAudioError> CoreAudioOutput::start() {
  stop();
  AudioComponentDescription description{};
  description.componentType = kAudioUnitType_Output;
  description.componentSubType = kAudioUnitSubType_DefaultOutput;
  description.componentManufacturer = kAudioUnitManufacturer_Apple;
  const auto component = AudioComponentFindNext(nullptr, &description);
  if (!component || AudioComponentInstanceNew(component, &impl_->unit) != noErr) {
    return Result<void, CoreAudioError>::err(CoreAudioError::Initialize);
  }
  UInt32 enabled = 1;
  if (AudioUnitSetProperty(impl_->unit, kAudioOutputUnitProperty_EnableIO,
                           kAudioUnitScope_Output, 1, &enabled, sizeof(enabled)) != noErr) {
    stop();
    return Result<void, CoreAudioError>::err(CoreAudioError::Format);
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
                           kAudioUnitScope_Input, 0, &format, sizeof(format)) != noErr) {
    stop();
    return Result<void, CoreAudioError>::err(CoreAudioError::Format);
  }
  AURenderCallbackStruct callback{render_callback, impl_.get()};
  if (AudioUnitSetProperty(impl_->unit, kAudioUnitProperty_SetRenderCallback,
                           kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr ||
      AudioUnitInitialize(impl_->unit) != noErr || AudioOutputUnitStart(impl_->unit) != noErr) {
    stop();
    return Result<void, CoreAudioError>::err(CoreAudioError::Start);
  }
  impl_->started = true;
  return Result<void, CoreAudioError>::ok();
}

Result<void, CoreAudioError> CoreAudioOutput::push(std::span<const float> samples) {
  if (!impl_->started) {
    return Result<void, CoreAudioError>::err(CoreAudioError::Stopped);
  }
  if (samples.size() % 2U != 0) {
    return Result<void, CoreAudioError>::err(CoreAudioError::Format);
  }
  std::scoped_lock lock(impl_->mutex);
  const auto room = impl_->max_samples > impl_->samples.size()
                        ? impl_->max_samples - impl_->samples.size()
                        : 0;
  const auto count = std::min(room, samples.size());
  impl_->samples.insert(impl_->samples.end(), samples.begin(), samples.begin() + count);
  return count == samples.size() ? Result<void, CoreAudioError>::ok()
                                 : Result<void, CoreAudioError>::err(CoreAudioError::Format);
}

CoreAudioStats CoreAudioOutput::stats() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return {impl_->underruns.load(), impl_->samples.size() / 2U};
}

void CoreAudioOutput::stop() noexcept {
  if (!impl_) return;
  if (impl_->unit) {
    if (impl_->started) {
      AudioOutputUnitStop(impl_->unit);
    }
    AudioUnitUninitialize(impl_->unit);
    AudioComponentInstanceDispose(impl_->unit);
    impl_->unit = nullptr;
  }
  impl_->started = false;
  std::scoped_lock lock(impl_->mutex);
  impl_->samples.clear();
}

}  // namespace ministream
