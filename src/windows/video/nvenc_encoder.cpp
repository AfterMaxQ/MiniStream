#include "windows/video/nvenc_encoder.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <utility>

#if MINISTREAM_HAVE_NVENC_SDK
#include <nvEncodeAPI.h>
#endif

namespace ministream {

struct NvencEncoder::Impl {
  NvencConfig config;
  CodecConfig codec_config;
  bool force_idr{};
#if MINISTREAM_HAVE_NVENC_SDK
  HMODULE library{};
  NV_ENCODE_API_FUNCTION_LIST api{};
  NV_ENC_CONFIG encode_config{};
  void* session{};
  ID3D11Device* device{};
  ID3D11DeviceContext* context{};
#endif
};

NvencEncoder::NvencEncoder() : impl_(std::make_unique<Impl>()) {}
NvencEncoder::~NvencEncoder() { stop(); }
NvencEncoder::NvencEncoder(NvencEncoder&&) noexcept = default;
NvencEncoder& NvencEncoder::operator=(NvencEncoder&&) noexcept = default;

Result<void, NvencError> NvencEncoder::initialize(
    ID3D11Device* device, ID3D11DeviceContext* context, NvencConfig config) {
  stop();
  if (!device || !context || config.width == 0 || config.height == 0 ||
      config.fps == 0 || config.bitrate_bps == 0 ||
      (config.hdr10 && config.codec != VideoCodec::Hevc)) {
    return Result<void, NvencError>::err(NvencError::InvalidConfig);
  }
#if !MINISTREAM_HAVE_NVENC_SDK
  (void)device;
  (void)context;
  (void)config;
  return Result<void, NvencError>::err(NvencError::Unavailable);
#else
  impl_->config = config;
  impl_->device = device;
  impl_->context = context;
  impl_->library = LoadLibraryW(L"nvEncodeAPI64.dll");
  if (!impl_->library) {
    return Result<void, NvencError>::err(NvencError::Unavailable);
  }
  using CreateInstance = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
  const auto create = reinterpret_cast<CreateInstance>(
      GetProcAddress(impl_->library, "NvEncodeAPICreateInstance"));
  if (!create) {
    stop();
    return Result<void, NvencError>::err(NvencError::Unavailable);
  }
  impl_->api = {};
  impl_->api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
  const auto create_status = create(&impl_->api);
  if (create_status != NV_ENC_SUCCESS || !impl_->api.nvEncOpenEncodeSessionEx) {
    stop();
    return Result<void, NvencError>::err(NvencError::Initialize);
  }

  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
  open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
  open.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
  open.device = device;
  open.apiVersion = NVENCAPI_VERSION;
  const auto open_status = impl_->api.nvEncOpenEncodeSessionEx(&open, &impl_->session);
  if (open_status != NV_ENC_SUCCESS) {
    stop();
    return Result<void, NvencError>::err(NvencError::Initialize);
  }

  NV_ENC_CONFIG encode_config{};
  const auto encode_guid = config.codec == VideoCodec::H264 ? NV_ENC_CODEC_H264_GUID
                                                              : NV_ENC_CODEC_HEVC_GUID;
#if NVENCAPI_MAJOR_VERSION >= 13
  const auto preset_guid = NV_ENC_PRESET_P4_GUID;
#else
  const auto preset_guid = NV_ENC_PRESET_DEFAULT_GUID;
#endif
  // Start from the driver's preset so fields added by newer API revisions
  // receive valid defaults.  A zero-filled config is rejected by some
  // driver versions even when the fields we explicitly set are valid.
  if (impl_->api.nvEncGetEncodePresetConfig) {
    NV_ENC_PRESET_CONFIG preset{};
    preset.version = NV_ENC_PRESET_CONFIG_VER;
    preset.presetCfg.version = NV_ENC_CONFIG_VER;
    const auto preset_status = impl_->api.nvEncGetEncodePresetConfig(
        impl_->session, encode_guid, preset_guid, &preset);
    if (preset_status == NV_ENC_SUCCESS) {
      encode_config = preset.presetCfg;
    }
  }
  encode_config.version = NV_ENC_CONFIG_VER;
  encode_config.gopLength = NVENC_INFINITE_GOPLENGTH;
  encode_config.frameIntervalP = 1;  // no B frames or reorder delay
  encode_config.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
  encode_config.rcParams.version = NV_ENC_RC_PARAMS_VER;
#if NVENCAPI_MAJOR_VERSION >= 13
  encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
#else
  encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
#endif
  encode_config.rcParams.averageBitRate = config.bitrate_bps;
  encode_config.rcParams.maxBitRate = config.bitrate_bps;
  encode_config.rcParams.zeroReorderDelay = 1;
  if (config.codec == VideoCodec::H264) {
    encode_config.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
    encode_config.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
    encode_config.encodeCodecConfig.h264Config.level = NV_ENC_LEVEL_AUTOSELECT;
    encode_config.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
    encode_config.encodeCodecConfig.h264Config.chromaFormatIDC = 1;
  } else {
    encode_config.profileGUID = config.hdr10 ? NV_ENC_HEVC_PROFILE_MAIN10_GUID
                                             : NV_ENC_HEVC_PROFILE_MAIN_GUID;
    encode_config.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;
    encode_config.encodeCodecConfig.hevcConfig.idrPeriod = NVENC_INFINITE_GOPLENGTH;
    encode_config.encodeCodecConfig.hevcConfig.chromaFormatIDC = 1;
#if NVENCAPI_MAJOR_VERSION >= 13
    encode_config.encodeCodecConfig.hevcConfig.outputBitDepth =
        config.hdr10 ? NV_ENC_BIT_DEPTH_10 : NV_ENC_BIT_DEPTH_8;
    encode_config.encodeCodecConfig.hevcConfig.inputBitDepth = NV_ENC_BIT_DEPTH_8;
#else
    encode_config.encodeCodecConfig.hevcConfig.pixelBitDepthMinus8 = config.hdr10 ? 2 : 0;
#endif
  }

  NV_ENC_INITIALIZE_PARAMS init{};
  init.version = NV_ENC_INITIALIZE_PARAMS_VER;
  init.encodeGUID = config.codec == VideoCodec::H264 ? NV_ENC_CODEC_H264_GUID
                                                       : NV_ENC_CODEC_HEVC_GUID;
  init.presetGUID = preset_guid;
  init.encodeWidth = config.width;
  init.encodeHeight = config.height;
  init.darWidth = config.width;
  init.darHeight = config.height;
  init.frameRateNum = config.fps;
  init.frameRateDen = 1;
  init.enableEncodeAsync = 0;
  init.enablePTD = 1;
  init.maxEncodeWidth = config.width;
  init.maxEncodeHeight = config.height;
  init.encodeConfig = &encode_config;
#if NVENCAPI_MAJOR_VERSION >= 13
  init.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
#endif
  const auto init_status = impl_->api.nvEncInitializeEncoder(impl_->session, &init);
  if (init_status != NV_ENC_SUCCESS) {
    stop();
    return Result<void, NvencError>::err(NvencError::Initialize);
  }
  impl_->encode_config = encode_config;
  impl_->codec_config = {config.codec, config.width, config.height, config.fps,
                         config.hdr10, {}};
  return Result<void, NvencError>::ok();
#endif
}

Result<EncodedFrame, NvencError> NvencEncoder::encode(
    const CapturedFrame& frame, std::uint64_t timestamp_us, bool force_idr) {
#if !MINISTREAM_HAVE_NVENC_SDK
  (void)frame;
  (void)timestamp_us;
  (void)force_idr;
  return Result<EncodedFrame, NvencError>::err(NvencError::Unavailable);
#else
  if (!ready() || !frame.texture || frame.format != DXGI_FORMAT_B8G8R8A8_UNORM ||
      frame.width != impl_->config.width || frame.height != impl_->config.height) {
    return Result<EncodedFrame, NvencError>::err(
        ready() ? NvencError::UnsupportedFormat : NvencError::Unavailable);
  }
  NV_ENC_REGISTER_RESOURCE resource{};
  resource.version = NV_ENC_REGISTER_RESOURCE_VER;
  resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
  resource.width = frame.width;
  resource.height = frame.height;
  resource.resourceToRegister = frame.texture.Get();
  resource.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
  if (impl_->api.nvEncRegisterResource(impl_->session, &resource) != NV_ENC_SUCCESS) {
    return Result<EncodedFrame, NvencError>::err(NvencError::RegisterResource);
  }

  NV_ENC_MAP_INPUT_RESOURCE mapped{};
  mapped.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
  mapped.registeredResource = resource.registeredResource;
  const auto mapped_status = impl_->api.nvEncMapInputResource(impl_->session, &mapped);
  if (mapped_status != NV_ENC_SUCCESS) {
    impl_->api.nvEncUnregisterResource(impl_->session, resource.registeredResource);
    return Result<EncodedFrame, NvencError>::err(NvencError::RegisterResource);
  }

  NV_ENC_CREATE_BITSTREAM_BUFFER output{};
  output.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
  if (impl_->api.nvEncCreateBitstreamBuffer(impl_->session, &output) != NV_ENC_SUCCESS) {
    impl_->api.nvEncUnmapInputResource(impl_->session, mapped.mappedResource);
    impl_->api.nvEncUnregisterResource(impl_->session, resource.registeredResource);
    return Result<EncodedFrame, NvencError>::err(NvencError::Encode);
  }

  NV_ENC_PIC_PARAMS picture{};
  picture.version = NV_ENC_PIC_PARAMS_VER;
  picture.inputWidth = frame.width;
  picture.inputHeight = frame.height;
  picture.inputPitch = frame.width;
  picture.inputBuffer = mapped.mappedResource;
  picture.outputBitstream = output.bitstreamBuffer;
  picture.bufferFmt = mapped.mappedBufferFmt;
  picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
  picture.inputTimeStamp = timestamp_us;
  picture.encodePicFlags = (force_idr || impl_->force_idr)
                              ? NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS
                              : 0;
  impl_->force_idr = false;
  const auto encoded = impl_->api.nvEncEncodePicture(impl_->session, &picture);
  if (encoded != NV_ENC_SUCCESS && encoded != NV_ENC_ERR_NEED_MORE_INPUT) {
    impl_->api.nvEncDestroyBitstreamBuffer(impl_->session, output.bitstreamBuffer);
    impl_->api.nvEncUnmapInputResource(impl_->session, mapped.mappedResource);
    impl_->api.nvEncUnregisterResource(impl_->session, resource.registeredResource);
    return Result<EncodedFrame, NvencError>::err(NvencError::Encode);
  }

  NV_ENC_LOCK_BITSTREAM lock{};
  lock.version = NV_ENC_LOCK_BITSTREAM_VER;
  lock.outputBitstream = output.bitstreamBuffer;
  const auto locked = impl_->api.nvEncLockBitstream(impl_->session, &lock);
  EncodedFrame result;
  if (locked == NV_ENC_SUCCESS && lock.bitstreamBufferPtr && lock.bitstreamSizeInBytes > 0) {
    result.frame_id = static_cast<std::uint32_t>(frame.frame_id);
    result.capture_timestamp_us = timestamp_us;
    result.keyframe = lock.pictureType == NV_ENC_PIC_TYPE_IDR ||
                      lock.pictureType == NV_ENC_PIC_TYPE_I || force_idr;
    const auto* begin = static_cast<const std::byte*>(lock.bitstreamBufferPtr);
    result.bytes.assign(begin, begin + lock.bitstreamSizeInBytes);
    if (result.keyframe) {
      impl_->codec_config.parameter_sets = result.bytes;
    }
  }
  if (locked == NV_ENC_SUCCESS) {
    impl_->api.nvEncUnlockBitstream(impl_->session, output.bitstreamBuffer);
  }
  impl_->api.nvEncDestroyBitstreamBuffer(impl_->session, output.bitstreamBuffer);
  impl_->api.nvEncUnmapInputResource(impl_->session, mapped.mappedResource);
  impl_->api.nvEncUnregisterResource(impl_->session, resource.registeredResource);
  if (locked != NV_ENC_SUCCESS) {
    return Result<EncodedFrame, NvencError>::err(NvencError::LockBitstream);
  }
  return Result<EncodedFrame, NvencError>::ok(std::move(result));
#endif
}

Result<void, NvencError> NvencEncoder::reconfigure_bitrate(std::uint32_t bitrate_bps) {
  if (bitrate_bps == 0 || !ready()) {
    return Result<void, NvencError>::err(ready() ? NvencError::InvalidConfig
                                                  : NvencError::Unavailable);
  }
#if !MINISTREAM_HAVE_NVENC_SDK
  return Result<void, NvencError>::err(NvencError::Unavailable);
#else
  // Reconfiguration is deliberately synchronous: no frame is in flight when
  // this method returns, so the next packet observes the new rate.
  NV_ENC_CONFIG config = impl_->encode_config;
  config.version = NV_ENC_CONFIG_VER;
  config.rcParams.version = NV_ENC_RC_PARAMS_VER;
#if NVENCAPI_MAJOR_VERSION >= 13
  config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
#else
  config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
#endif
  config.rcParams.averageBitRate = bitrate_bps;
  config.rcParams.maxBitRate = bitrate_bps;
  NV_ENC_RECONFIGURE_PARAMS params{};
  params.version = NV_ENC_RECONFIGURE_PARAMS_VER;
  params.reInitEncodeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
  params.reInitEncodeParams.encodeGUID = impl_->config.codec == VideoCodec::H264
                                             ? NV_ENC_CODEC_H264_GUID
                                             : NV_ENC_CODEC_HEVC_GUID;
#if NVENCAPI_MAJOR_VERSION >= 13
  params.reInitEncodeParams.presetGUID = NV_ENC_PRESET_P4_GUID;
#else
  params.reInitEncodeParams.presetGUID = NV_ENC_PRESET_DEFAULT_GUID;
#endif
  params.reInitEncodeParams.encodeWidth = impl_->config.width;
  params.reInitEncodeParams.encodeHeight = impl_->config.height;
  params.reInitEncodeParams.darWidth = impl_->config.width;
  params.reInitEncodeParams.darHeight = impl_->config.height;
  params.reInitEncodeParams.frameRateNum = impl_->config.fps;
  params.reInitEncodeParams.frameRateDen = 1;
  params.reInitEncodeParams.enableEncodeAsync = 0;
  params.reInitEncodeParams.enablePTD = 1;
  params.reInitEncodeParams.maxEncodeWidth = impl_->config.width;
  params.reInitEncodeParams.maxEncodeHeight = impl_->config.height;
  params.reInitEncodeParams.encodeConfig = &config;
#if NVENCAPI_MAJOR_VERSION >= 13
  params.reInitEncodeParams.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
#endif
  const auto status = impl_->api.nvEncReconfigureEncoder(impl_->session, &params);
  if (status != NV_ENC_SUCCESS) {
    return Result<void, NvencError>::err(NvencError::Reconfigure);
  }
  impl_->config.bitrate_bps = bitrate_bps;
  return Result<void, NvencError>::ok();
#endif
}

void NvencEncoder::request_idr() noexcept {
  if (impl_) {
    impl_->force_idr = true;
  }
}

void NvencEncoder::stop() noexcept {
#if MINISTREAM_HAVE_NVENC_SDK
  if (impl_ && impl_->session && impl_->api.nvEncDestroyEncoder) {
    impl_->api.nvEncDestroyEncoder(impl_->session);
  }
  if (impl_) {
    impl_->session = nullptr;
    impl_->device = nullptr;
    impl_->context = nullptr;
    if (impl_->library) {
      FreeLibrary(impl_->library);
      impl_->library = nullptr;
    }
  }
#endif
  if (impl_) {
    impl_->codec_config = {};
    impl_->force_idr = false;
#if MINISTREAM_HAVE_NVENC_SDK
    impl_->encode_config = {};
#endif
  }
}

bool NvencEncoder::ready() const noexcept {
#if MINISTREAM_HAVE_NVENC_SDK
  return impl_ && impl_->session != nullptr;
#else
  return false;
#endif
}

const CodecConfig& NvencEncoder::codec_config() const noexcept {
  return impl_->codec_config;
}

}  // namespace ministream
