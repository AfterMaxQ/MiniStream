#include "macos/video/videotoolbox_encoder.hpp"

#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace ministream {

struct VideoToolboxEncoder::Impl {
  VTCompressionSessionRef session{};
  VideoEncodeConfig config{};
  CodecConfig codec_config{};
  std::deque<EncodedFrame> ready_frames;
  std::atomic_bool force_next_idr{false};
  bool awaiting_idr{};
  std::uint32_t next_frame_id{};
  std::mutex mutex;
};

namespace {

std::vector<std::byte> annex_b(CMBlockBufferRef block) {
  size_t length{};
  char* data{};
  if (CMBlockBufferGetDataPointer(block, 0, nullptr, &length, &data) != kCMBlockBufferNoErr ||
      !data || length == 0) {
    return {};
  }
  std::vector<std::byte> result;
  std::size_t offset = 0;
  while (offset + 4 <= length) {
    const auto nal_length = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset])) << 24U) |
                            (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + 1])) << 16U) |
                            (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + 2])) << 8U) |
                            static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + 3]));
    offset += 4;
    if (nal_length == 0 || offset + nal_length > length) {
      return {};
    }
    result.insert(result.end(), {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}});
    result.insert(result.end(), reinterpret_cast<const std::byte*>(data + offset),
                  reinterpret_cast<const std::byte*>(data + offset + nal_length));
    offset += nal_length;
  }
  return result;
}

std::vector<std::byte> parameter_sets(CMFormatDescriptionRef format, VideoCodec codec) {
  std::vector<std::byte> result;
  if (!format) {
    return result;
  }

  if (codec == VideoCodec::H264) {
    size_t count = 0;
    int header_length = 0;
    const std::uint8_t* first = nullptr;
    size_t first_size = 0;
    if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            format, 0, &first, &first_size, &count, &header_length) != noErr) {
      return result;
    }
    (void)first;
    (void)first_size;
    for (size_t index = 0; index < count; ++index) {
      const std::uint8_t* bytes = nullptr;
      size_t size = 0;
      if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
              format, index, &bytes, &size, nullptr, nullptr) != noErr || !bytes || size == 0) {
        continue;
      }
      result.insert(result.end(), {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}});
      result.insert(result.end(), reinterpret_cast<const std::byte*>(bytes),
                    reinterpret_cast<const std::byte*>(bytes + size));
    }
  } else if (codec == VideoCodec::Hevc) {
    size_t count = 0;
    int header_length = 0;
    const std::uint8_t* first = nullptr;
    size_t first_size = 0;
    if (CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
            format, 0, &first, &first_size, &count, &header_length) != noErr) {
      return result;
    }
    (void)first;
    (void)first_size;
    for (size_t index = 0; index < count; ++index) {
      const std::uint8_t* bytes = nullptr;
      size_t size = 0;
      if (CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
              format, index, &bytes, &size, nullptr, nullptr) != noErr || !bytes || size == 0) {
        continue;
      }
      result.insert(result.end(), {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}});
      result.insert(result.end(), reinterpret_cast<const std::byte*>(bytes),
                    reinterpret_cast<const std::byte*>(bytes + size));
    }
  }
  return result;
}

void output_callback(void* refcon, void*, OSStatus status, VTEncodeInfoFlags,
                     CMSampleBufferRef sample) {
  if (status != noErr || !sample || !refcon) {
    return;
  }
  auto* impl = static_cast<VideoToolboxEncoder::Impl*>(refcon);
  auto block = CMSampleBufferGetDataBuffer(sample);
  if (!block) {
    return;
  }
  auto bytes = annex_b(block);
  if (bytes.empty()) {
    return;
  }
  std::uint64_t timestamp_us = 0;
  const auto pts = CMSampleBufferGetPresentationTimeStamp(sample);
  if (CMTIME_IS_VALID(pts) && pts.timescale != 0) {
    const auto seconds = CMTimeGetSeconds(pts);
    if (std::isfinite(seconds) && seconds >= 0.0) {
      timestamp_us = static_cast<std::uint64_t>(seconds * 1'000'000.0);
    }
  }
  bool keyframe = true;  // NotSync defaults to false when attachments are absent.
  if (auto attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
      attachments && CFArrayGetCount(attachments) > 0) {
    auto* dictionary = static_cast<CFDictionaryRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(attachments, 0)));
    keyframe = !CFDictionaryContainsKey(dictionary, kCMSampleAttachmentKey_NotSync);
  }
  std::scoped_lock lock(impl->mutex);
  constexpr std::size_t kMaxReadyFrames = 6;
  if (impl->ready_frames.size() >= kMaxReadyFrames) {
    impl->ready_frames.clear();
    impl->awaiting_idr = true;
    impl->force_next_idr.store(true, std::memory_order_release);
  }
  if (impl->awaiting_idr && !keyframe) {
    return;
  }
  if (keyframe) {
    impl->awaiting_idr = false;
    impl->force_next_idr.store(false, std::memory_order_release);
  }
  impl->ready_frames.push_back(
      {impl->next_frame_id++, timestamp_us, keyframe, std::move(bytes)});
  if (keyframe) {
    if (const auto format = CMSampleBufferGetFormatDescription(sample)) {
      auto sets = parameter_sets(format, impl->config.codec);
      if (!sets.empty()) {
        impl->codec_config.parameter_sets = std::move(sets);
      }
    }
  }
}

}  // namespace

VideoToolboxEncoder::VideoToolboxEncoder() : impl_(std::make_unique<Impl>()) {}
VideoToolboxEncoder::~VideoToolboxEncoder() { stop(); }
VideoToolboxEncoder::VideoToolboxEncoder(VideoToolboxEncoder&&) noexcept = default;
VideoToolboxEncoder& VideoToolboxEncoder::operator=(VideoToolboxEncoder&&) noexcept = default;

Result<void, VideoEncodeError> VideoToolboxEncoder::start(VideoEncodeConfig config) {
  stop();
  if (config.width == 0 || config.height == 0 || config.fps == 0 || config.bitrate_bps == 0 ||
      (config.codec != VideoCodec::H264 && config.codec != VideoCodec::Hevc) || config.hdr10) {
    return Result<void, VideoEncodeError>::err(VideoEncodeError::InvalidConfig);
  }
  const auto codec = config.codec == VideoCodec::H264 ? kCMVideoCodecType_H264
                                                       : kCMVideoCodecType_HEVC;
  NSDictionary* source_attributes = @{
      (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey:
          @(kCVPixelFormatType_32BGRA),
      (__bridge NSString*)kCVPixelBufferWidthKey: @(config.width),
      (__bridge NSString*)kCVPixelBufferHeightKey: @(config.height),
      (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
  };
  if (VTCompressionSessionCreate(kCFAllocatorDefault, config.width, config.height, codec,
                                 nullptr, (__bridge CFDictionaryRef)source_attributes, nullptr,
                                 output_callback, impl_.get(), &impl_->session) != noErr ||
      !impl_->session) {
    stop();
    return Result<void, VideoEncodeError>::err(VideoEncodeError::Initialize);
  }
  VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
  VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_AllowFrameReordering,
                       kCFBooleanFalse);
  VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_AverageBitRate,
                       (__bridge CFTypeRef)@(config.bitrate_bps));
  VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_ExpectedFrameRate,
                       (__bridge CFTypeRef)@(config.fps));
  VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_MaxKeyFrameInterval,
                       (__bridge CFTypeRef)@(config.fps * 2U));
  if (config.codec == VideoCodec::H264) {
    VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_ProfileLevel,
                         kVTProfileLevel_H264_High_AutoLevel);
  } else {
    VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_ProfileLevel,
                         kVTProfileLevel_HEVC_Main_AutoLevel);
  }
  if (VTCompressionSessionPrepareToEncodeFrames(impl_->session) != noErr) {
    stop();
    return Result<void, VideoEncodeError>::err(VideoEncodeError::Initialize);
  }
  impl_->config = config;
  impl_->codec_config = {config.codec, config.width, config.height, config.fps,
                         config.hdr10, {}};
  return Result<void, VideoEncodeError>::ok();
}

Result<void, VideoEncodeError> VideoToolboxEncoder::submit(
    CVPixelBufferRef pixel_buffer, std::uint64_t timestamp_us, bool force_idr) {
  if (!ready() || !pixel_buffer) {
    return Result<void, VideoEncodeError>::err(VideoEncodeError::Unavailable);
  }
  const bool requested_idr =
      force_idr || impl_->force_next_idr.load(std::memory_order_acquire);
  CFDictionaryRef properties = nullptr;
  if (requested_idr) {
    properties = (__bridge CFDictionaryRef)@{
        (__bridge NSString*)kVTEncodeFrameOptionKey_ForceKeyFrame: @YES,
    };
  }
  const auto pts = CMTimeMake(static_cast<int64_t>(timestamp_us), 1'000'000);
  const auto duration = CMTimeMake(1, static_cast<int32_t>(impl_->config.fps));
  const auto status = VTCompressionSessionEncodeFrame(impl_->session, pixel_buffer, pts,
                                                      duration, properties, nullptr, nullptr);
  if (status != noErr) {
    return Result<void, VideoEncodeError>::err(VideoEncodeError::Encode);
  }
  return Result<void, VideoEncodeError>::ok();
}

std::optional<EncodedFrame> VideoToolboxEncoder::take_next() {
  if (!impl_) {
    return std::nullopt;
  }
  std::scoped_lock lock(impl_->mutex);
  if (impl_->ready_frames.empty()) {
    return std::nullopt;
  }
  auto result = std::move(impl_->ready_frames.front());
  impl_->ready_frames.pop_front();
  return result;
}

std::optional<EncodedFrame> VideoToolboxEncoder::take_latest() { return take_next(); }

Result<EncodedFrame, VideoEncodeError> VideoToolboxEncoder::encode(
    CVPixelBufferRef pixel_buffer, std::uint64_t timestamp_us, bool force_idr) {
  if (const auto submitted = submit(pixel_buffer, timestamp_us, force_idr); !submitted) {
    return Result<EncodedFrame, VideoEncodeError>::err(submitted.error());
  }
  const auto latest = take_latest();
  if (!latest) {
    return Result<EncodedFrame, VideoEncodeError>::err(VideoEncodeError::Encode);
  }
  auto result = *latest;
  if (result.capture_timestamp_us == 0) {
    result.capture_timestamp_us = timestamp_us;
  }
  return Result<EncodedFrame, VideoEncodeError>::ok(std::move(result));
}

void VideoToolboxEncoder::stop() noexcept {
  if (!impl_) return;
  if (impl_->session) {
    VTCompressionSessionCompleteFrames(impl_->session, kCMTimeInvalid);
    VTCompressionSessionInvalidate(impl_->session);
    CFRelease(impl_->session);
    impl_->session = nullptr;
  }
  std::scoped_lock lock(impl_->mutex);
  impl_->ready_frames.clear();
  impl_->awaiting_idr = false;
  impl_->force_next_idr.store(false, std::memory_order_release);
  impl_->next_frame_id = 0;
  impl_->config = {};
  impl_->codec_config = {};
}

void VideoToolboxEncoder::request_idr() noexcept {
  if (impl_) {
    {
      std::scoped_lock lock(impl_->mutex);
      impl_->ready_frames.clear();
      impl_->awaiting_idr = true;
      impl_->force_next_idr.store(true, std::memory_order_release);
    }
  }
}

Result<void, VideoEncodeError> VideoToolboxEncoder::reconfigure_bitrate(
    std::uint32_t bitrate_bps) {
  if (!ready() || bitrate_bps == 0) {
    return Result<void, VideoEncodeError>::err(ready() ? VideoEncodeError::InvalidConfig
                                                        : VideoEncodeError::Unavailable);
  }
  if (VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_AverageBitRate,
                           (__bridge CFTypeRef)@(bitrate_bps)) != noErr) {
    return Result<void, VideoEncodeError>::err(VideoEncodeError::Reconfigure);
  }
  impl_->config.bitrate_bps = bitrate_bps;
  return Result<void, VideoEncodeError>::ok();
}

bool VideoToolboxEncoder::ready() const noexcept { return impl_ && impl_->session != nullptr; }
CodecConfig VideoToolboxEncoder::codec_config() const {
  if (!impl_) {
    return {};
  }
  std::scoped_lock lock(impl_->mutex);
  return impl_->codec_config;
}

}  // namespace ministream
