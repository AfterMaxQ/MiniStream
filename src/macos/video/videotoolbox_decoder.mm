#include "macos/video/videotoolbox_decoder.hpp"

#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace ministream {

struct VideoToolboxDecoder::Impl {
  CodecConfig config;
  CMVideoFormatDescriptionRef format{};
  VTDecompressionSessionRef session{};
  CVPixelBufferRef latest{};
  std::uint64_t latest_timestamp_us{};
  std::mutex mutex;
};

namespace {

struct NalUnit {
  const std::uint8_t* data{};
  std::size_t size{};
};

std::vector<NalUnit> split_annex_b(std::span<const std::byte> bytes) {
  std::vector<NalUnit> result;
  const auto* raw = reinterpret_cast<const std::uint8_t*>(bytes.data());
  const auto is_start = [&](std::size_t index, std::size_t* prefix) {
    if (index + 3 <= bytes.size() && raw[index] == 0 && raw[index + 1] == 0 &&
        raw[index + 2] == 1) {
      *prefix = 3;
      return true;
    }
    if (index + 4 <= bytes.size() && raw[index] == 0 && raw[index + 1] == 0 &&
        raw[index + 2] == 0 && raw[index + 3] == 1) {
      *prefix = 4;
      return true;
    }
    return false;
  };
  std::size_t start = 0;
  while (start < bytes.size()) {
    std::size_t prefix = 0;
    if (!is_start(start, &prefix)) {
      ++start;
      continue;
    }
    const auto nal_start = start + prefix;
    std::size_t end = nal_start;
    while (end < bytes.size()) {
      std::size_t next_prefix = 0;
      if (is_start(end, &next_prefix)) {
        break;
      }
      ++end;
    }
    if (end > nal_start) {
      result.push_back({raw + nal_start, end - nal_start});
    }
    start = end;
  }
  return result;
}

void output_callback(void* refcon, void*, OSStatus status, VTDecodeInfoFlags,
                     CVImageBufferRef image, CMTime presentation_time, CMTime) {
  if (status != noErr || image == nullptr || refcon == nullptr) {
    return;
  }
  auto* impl = static_cast<VideoToolboxDecoder::Impl*>(refcon);
  std::scoped_lock lock(impl->mutex);
  if (impl->latest != nullptr) {
    CVPixelBufferRelease(impl->latest);
  }
  impl->latest = static_cast<CVPixelBufferRef>(image);
  CVPixelBufferRetain(impl->latest);
  impl->latest_timestamp_us = static_cast<std::uint64_t>(
      std::max<Float64>(0.0, CMTimeGetSeconds(presentation_time)) * 1'000'000.0);
}

}  // namespace

VideoToolboxDecoder::VideoToolboxDecoder() : impl_(std::make_unique<Impl>()) {}
VideoToolboxDecoder::~VideoToolboxDecoder() { stop(); }
VideoToolboxDecoder::VideoToolboxDecoder(VideoToolboxDecoder&&) noexcept = default;
VideoToolboxDecoder& VideoToolboxDecoder::operator=(VideoToolboxDecoder&&) noexcept = default;

Result<void, VideoDecodeError> VideoToolboxDecoder::initialize(const CodecConfig& config) {
  stop();
  if (config.width == 0 || config.height == 0 || config.parameter_sets.empty()) {
    return Result<void, VideoDecodeError>::err(VideoDecodeError::InvalidConfig);
  }
  const auto nals = split_annex_b(config.parameter_sets);
  if (config.codec == VideoCodec::H264) {
    std::array<const std::uint8_t*, 2> sets{};
    std::array<std::size_t, 2> sizes{};
    std::size_t count = 0;
    for (const auto nal : nals) {
      if (nal.size == 0) continue;
      const auto type = nal.data[0] & 0x1FU;
      if ((type == 7 || type == 8) && count < sets.size()) {
        sets[count] = nal.data;
        sizes[count++] = nal.size;
      }
    }
    if (count < 2 || CMVideoFormatDescriptionCreateFromH264ParameterSets(
                         kCFAllocatorDefault, static_cast<int>(count), sets.data(), sizes.data(),
                         4, &impl_->format) != noErr) {
      return Result<void, VideoDecodeError>::err(VideoDecodeError::FormatDescription);
    }
  } else if (config.codec == VideoCodec::Hevc) {
    std::array<const std::uint8_t*, 3> sets{};
    std::array<std::size_t, 3> sizes{};
    std::size_t count = 0;
    for (const auto nal : nals) {
      if (nal.size < 2) continue;
      const auto type = (nal.data[0] >> 1U) & 0x3FU;
      if ((type == 32 || type == 33 || type == 34) && count < sets.size()) {
        sets[count] = nal.data;
        sizes[count++] = nal.size;
      }
    }
    if (count < 3 || CMVideoFormatDescriptionCreateFromHEVCParameterSets(
                         kCFAllocatorDefault, static_cast<int>(count), sets.data(), sizes.data(),
                         4, nullptr, &impl_->format) != noErr) {
      return Result<void, VideoDecodeError>::err(VideoDecodeError::FormatDescription);
    }
  } else {
    return Result<void, VideoDecodeError>::err(VideoDecodeError::UnsupportedCodec);
  }

  CFDictionaryRef spec = (__bridge CFDictionaryRef)@{
      (__bridge NSString*)kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder:
          @YES};
  VTDecompressionOutputCallbackRecord callback{output_callback, impl_.get()};
  if (VTDecompressionSessionCreate(kCFAllocatorDefault, impl_->format, spec, nullptr,
                                   &callback, &impl_->session) != noErr) {
    stop();
    return Result<void, VideoDecodeError>::err(VideoDecodeError::Session);
  }
  impl_->config = config;
  return Result<void, VideoDecodeError>::ok();
}

Result<void, VideoDecodeError> VideoToolboxDecoder::decode(
    std::span<const std::byte> annex_b, std::uint64_t timestamp_us) {
  if (!impl_->session || annex_b.empty()) {
    return Result<void, VideoDecodeError>::err(VideoDecodeError::Session);
  }
  // VideoToolbox expects length-prefixed NAL units in a CMBlockBuffer. Convert
  // the access unit without touching the decoded pixel buffer.
  const auto nals = split_annex_b(annex_b);
  if (nals.empty()) {
    return Result<void, VideoDecodeError>::err(VideoDecodeError::Decode);
  }
  std::vector<std::byte> avcc;
  for (const auto nal : nals) {
    const auto size = static_cast<std::uint32_t>(nal.size);
    avcc.push_back(static_cast<std::byte>(size >> 24U));
    avcc.push_back(static_cast<std::byte>(size >> 16U));
    avcc.push_back(static_cast<std::byte>(size >> 8U));
    avcc.push_back(static_cast<std::byte>(size));
    avcc.insert(avcc.end(), reinterpret_cast<const std::byte*>(nal.data),
                reinterpret_cast<const std::byte*>(nal.data + nal.size));
  }
  CMBlockBufferRef block{};
  auto* owned = static_cast<std::uint8_t*>(
      CFAllocatorAllocate(kCFAllocatorDefault, avcc.size(), 0));
  if (!owned) {
    return Result<void, VideoDecodeError>::err(VideoDecodeError::Decode);
  }
  std::memcpy(owned, avcc.data(), avcc.size());
  if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, owned, avcc.size(),
                                         kCFAllocatorDefault, nullptr, 0, avcc.size(), 0,
                                         &block) != kCMBlockBufferNoErr) {
    CFAllocatorDeallocate(kCFAllocatorDefault, owned);
    return Result<void, VideoDecodeError>::err(VideoDecodeError::Decode);
  }
  const auto pts = CMTimeMake(static_cast<int64_t>(timestamp_us), 1'000'000);
  CMSampleTimingInfo timing{CMTimeMake(1, static_cast<int32_t>(impl_->config.fps)), pts,
                            kCMTimeInvalid};
  CMSampleBufferRef sample{};
  const auto sample_status = CMSampleBufferCreateReady(kCFAllocatorDefault, block,
                                                        impl_->format, 1, 1, &timing, 0, nullptr,
                                                        &sample);
  CFRelease(block);
  if (sample_status != noErr) {
    return Result<void, VideoDecodeError>::err(VideoDecodeError::Decode);
  }
  // Run the callback before returning so the bridge can publish only the
  // newest frame and never queue an unbounded decoded-frame backlog.
  const auto status = VTDecompressionSessionDecodeFrame(impl_->session, sample, 0,
                                                        nullptr, nullptr);
  CFRelease(sample);
  return status == noErr ? Result<void, VideoDecodeError>::ok()
                         : Result<void, VideoDecodeError>::err(VideoDecodeError::Decode);
}

std::optional<DecodedVideoFrame> VideoToolboxDecoder::take_latest() {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->latest) {
    return std::nullopt;
  }
  DecodedVideoFrame result{impl_->latest, impl_->latest_timestamp_us};
  impl_->latest = nullptr;
  return result;
}

void VideoToolboxDecoder::stop() noexcept {
  if (!impl_) return;
  if (impl_->session) {
    VTDecompressionSessionWaitForAsynchronousFrames(impl_->session);
    VTDecompressionSessionInvalidate(impl_->session);
    CFRelease(impl_->session);
    impl_->session = nullptr;
  }
  if (impl_->format) {
    CFRelease(impl_->format);
    impl_->format = nullptr;
  }
  std::scoped_lock lock(impl_->mutex);
  if (impl_->latest) {
    CVPixelBufferRelease(impl_->latest);
    impl_->latest = nullptr;
  }
}

}  // namespace ministream
