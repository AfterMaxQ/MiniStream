#include "macos/video/videotoolbox_encoder.hpp"

#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace ministream {

struct VideoToolboxEncoder::Impl {
  VTCompressionSessionRef session{};
  VideoEncodeConfig config{};
  CodecConfig codec_config{};
  EncodedFrame latest{};
  bool has_latest{};
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
  bool keyframe = false;
  if (auto attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
      attachments && CFArrayGetCount(attachments) > 0) {
    auto* dictionary = static_cast<CFDictionaryRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(attachments, 0)));
    keyframe = !CFDictionaryContainsKey(dictionary, kCMSampleAttachmentKey_NotSync);
  }
  std::scoped_lock lock(impl->mutex);
  impl->latest = {impl->next_frame_id++, 0, keyframe,
                  std::move(bytes)};
  impl->has_latest = true;
  if (keyframe) {
    impl->codec_config.parameter_sets = impl->latest.bytes;
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
  if (VTCompressionSessionCreate(kCFAllocatorDefault, config.width, config.height, codec,
                                 nullptr, nullptr, nullptr, output_callback, impl_.get(),
                                 &impl_->session) != noErr ||
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

Result<EncodedFrame, VideoEncodeError> VideoToolboxEncoder::encode(
    CVPixelBufferRef pixel_buffer, std::uint64_t timestamp_us, bool force_idr) {
  if (!ready() || !pixel_buffer) {
    return Result<EncodedFrame, VideoEncodeError>::err(VideoEncodeError::Unavailable);
  }
  CFDictionaryRef properties = nullptr;
  if (force_idr) {
    properties = (__bridge CFDictionaryRef)@{
        (__bridge NSString*)kVTEncodeFrameOptionKey_ForceKeyFrame: @YES,
    };
  }
  const auto pts = CMTimeMake(static_cast<int64_t>(timestamp_us), 1'000'000);
  const auto duration = CMTimeMake(1, static_cast<int32_t>(impl_->config.fps));
  const auto status = VTCompressionSessionEncodeFrame(impl_->session, pixel_buffer, pts,
                                                      duration, properties, nullptr, nullptr);
  if (status != noErr || VTCompressionSessionCompleteFrames(impl_->session, kCMTimeInvalid) != noErr) {
    return Result<EncodedFrame, VideoEncodeError>::err(VideoEncodeError::Encode);
  }
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->has_latest) {
    return Result<EncodedFrame, VideoEncodeError>::err(VideoEncodeError::Encode);
  }
  auto result = std::move(impl_->latest);
  impl_->latest = {};
  impl_->has_latest = false;
  impl_->next_frame_id = 0;
  result.capture_timestamp_us = timestamp_us;
  impl_->codec_config.parameter_sets = result.keyframe ? result.bytes
                                                        : impl_->codec_config.parameter_sets;
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
  impl_->latest = {};
  impl_->has_latest = false;
  impl_->codec_config = {};
}

bool VideoToolboxEncoder::ready() const noexcept { return impl_ && impl_->session != nullptr; }
const CodecConfig& VideoToolboxEncoder::codec_config() const noexcept { return impl_->codec_config; }

}  // namespace ministream
