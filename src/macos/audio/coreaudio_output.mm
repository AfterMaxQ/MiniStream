#include "macos/audio/coreaudio_output.hpp"

#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <array>

namespace ministream {

struct CoreAudioOutput::Impl {
  AudioUnit unit{};
  mutable std::mutex mutex;
  static constexpr std::size_t kMaxSamples = 48'000U * 2U * 60U / 1000U;  // 60 ms
  std::array<float, kMaxSamples> samples{};
  std::size_t read_index{};
  std::size_t write_index{};
  std::size_t sample_count{};
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
  std::array<float, 8192> scratch{};
  const auto writable = std::min(requested, scratch.size());
  {
    std::scoped_lock lock(impl->mutex);
    const auto available = std::min(writable, impl->sample_count);
    for (std::size_t i = 0; i < available; ++i) {
      scratch[i] = impl->samples[impl->read_index];
      impl->read_index =
          (impl->read_index + 1U) % CoreAudioOutput::Impl::kMaxSamples;
    }
    impl->sample_count -= available;
    if (available < requested) {
      ++impl->underruns;
    }
  }
  if (output->mBuffers[0].mData != nullptr) {
    std::memcpy(output->mBuffers[0].mData, scratch.data(), writable * sizeof(float));
    if (writable < requested) {
      std::memset(static_cast<std::byte*>(output->mBuffers[0].mData) +
                      writable * sizeof(float),
                  0, (requested - writable) * sizeof(float));
    }
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
  if (samples.size() > Impl::kMaxSamples) samples = samples.last(Impl::kMaxSamples);
  if (samples.size() > Impl::kMaxSamples - impl_->sample_count) {
    const auto dropped = samples.size() - (Impl::kMaxSamples - impl_->sample_count);
    impl_->read_index = (impl_->read_index + dropped) % Impl::kMaxSamples;
    impl_->sample_count -= dropped;
  }
  for (const auto sample : samples) {
    impl_->samples[impl_->write_index] = sample;
    impl_->write_index = (impl_->write_index + 1U) % Impl::kMaxSamples;
  }
  impl_->sample_count += samples.size();
  return Result<void, CoreAudioError>::ok();
}

CoreAudioStats CoreAudioOutput::stats() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return {impl_->underruns.load(), impl_->sample_count / 2U};
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
  impl_->read_index = 0;
  impl_->write_index = 0;
  impl_->sample_count = 0;
}

}  // namespace ministream
