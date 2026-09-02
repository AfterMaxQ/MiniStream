#include "windows/video/mf_decoder.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <codecapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <utility>

namespace ministream {

using Microsoft::WRL::ComPtr;

struct MfDecoder::Impl {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IMFDXGIDeviceManager> manager;
  ComPtr<IMFTransform> transform;
  CodecConfig config{};
  TextureFrame latest{};
  UINT manager_token{};
  bool mf_started{};
  bool com_owned{};
  bool started{};
  bool configured{};
  std::mutex mutex;
};

namespace {

const GUID* input_subtype(VideoCodec codec) noexcept {
  switch (codec) {
    case VideoCodec::H264:
      return &MFVideoFormat_H264;
    case VideoCodec::Hevc:
      return &MFVideoFormat_HEVC;
  }
  return nullptr;
}

bool enumerate_decoder(VideoCodec codec) noexcept {
  const auto* subtype = input_subtype(codec);
  if (!subtype) {
    return false;
  }
  MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, *subtype};
  IMFActivate** activates = nullptr;
  UINT32 count = 0;
  constexpr UINT32 flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                           MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_HARDWARE |
                           MFT_ENUM_FLAG_SORTANDFILTER;
  const auto status = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, flags, &input, nullptr,
                                &activates, &count);
  if (FAILED(status) || count == 0 || activates == nullptr) {
    if (activates) {
      CoTaskMemFree(activates);
    }
    return false;
  }
  for (UINT32 index = 0; index < count; ++index) {
    activates[index]->Release();
  }
  CoTaskMemFree(activates);
  return true;
}

bool set_video_type(IMFMediaType* type, VideoCodec codec, std::uint32_t width,
                    std::uint32_t height, std::uint32_t fps) noexcept {
  const auto* subtype = input_subtype(codec);
  return subtype != nullptr && SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
         SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, *subtype)) &&
         SUCCEEDED(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height)) &&
         SUCCEEDED(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, fps, 1));
}

}  // namespace

MfDecoder::MfDecoder() : impl_(std::make_unique<Impl>()) {}
MfDecoder::~MfDecoder() { stop(); }
MfDecoder::MfDecoder(MfDecoder&&) noexcept = default;
MfDecoder& MfDecoder::operator=(MfDecoder&&) noexcept = default;

bool MfDecoder::hardware_available(VideoCodec codec) noexcept {
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    return false;
  }
  if (FAILED(MFStartup(MF_VERSION))) {
    if (SUCCEEDED(initialized)) {
      CoUninitialize();
    }
    return false;
  }
  const bool available = enumerate_decoder(codec);
  MFShutdown();
  if (SUCCEEDED(initialized)) {
    CoUninitialize();
  }
  return available;
}

Result<void, MfDecodeError> MfDecoder::start() {
  if (impl_->started) {
    return Result<void, MfDecodeError>::ok();
  }
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  }
  if (FAILED(MFStartup(MF_VERSION))) {
    if (SUCCEEDED(initialized)) {
      CoUninitialize();
    }
    return Result<void, MfDecodeError>::err(MfDecodeError::Unavailable);
  }
  impl_->mf_started = true;
  impl_->com_owned = SUCCEEDED(initialized);
  D3D_FEATURE_LEVEL level{};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &impl_->device, &level,
                               &impl_->context))) {
    stop();
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  }
  if (FAILED(MFCreateDXGIDeviceManager(&impl_->manager_token, &impl_->manager)) ||
      FAILED(impl_->manager->ResetDevice(impl_->device.Get(), impl_->manager_token))) {
    stop();
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  }
  impl_->started = true;
  return Result<void, MfDecodeError>::ok();
}

Result<void, MfDecodeError> MfDecoder::configure(const CodecConfig& config) {
  if (!impl_->started || config.width == 0 || config.height == 0 || config.fps == 0 ||
      config.hdr10 || !input_subtype(config.codec)) {
    return Result<void, MfDecodeError>::err(
        config.codec == VideoCodec::H264 || config.codec == VideoCodec::Hevc
            ? MfDecodeError::InvalidConfig
            : MfDecodeError::UnsupportedCodec);
  }
  if (!hardware_available(config.codec)) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Unavailable);
  }
  impl_->transform.Reset();
  MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, *input_subtype(config.codec)};
  IMFActivate** activates = nullptr;
  UINT32 count = 0;
  constexpr UINT32 flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                           MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_HARDWARE |
                           MFT_ENUM_FLAG_SORTANDFILTER;
  if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, flags, &input, nullptr,
                       &activates, &count)) || count == 0) {
    if (activates) {
      CoTaskMemFree(activates);
    }
    return Result<void, MfDecodeError>::err(MfDecodeError::Unavailable);
  }
  const auto activated = activates[0]->ActivateObject(IID_PPV_ARGS(&impl_->transform));
  for (UINT32 index = 0; index < count; ++index) {
    activates[index]->Release();
  }
  CoTaskMemFree(activates);
  if (FAILED(activated)) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  }
  ComPtr<IMFAttributes> attributes;
  if (SUCCEEDED(impl_->transform->GetAttributes(&attributes))) {
    attributes->SetUINT32(MF_SA_D3D11_AWARE, TRUE);
    attributes->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE);
  }
  if (FAILED(impl_->transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                               reinterpret_cast<ULONG_PTR>(impl_->manager.Get())))) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  }
  ComPtr<IMFMediaType> input_type;
  ComPtr<IMFMediaType> output_type;
  if (FAILED(MFCreateMediaType(&input_type)) ||
      !set_video_type(input_type.Get(), config.codec, config.width, config.height, config.fps) ||
      FAILED(impl_->transform->SetInputType(0, input_type.Get(), 0)) ||
      FAILED(MFCreateMediaType(&output_type)) ||
      FAILED(output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
      FAILED(output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)) ||
      FAILED(MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, config.width, config.height)) ||
      FAILED(MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, config.fps, 1)) ||
      FAILED(impl_->transform->SetOutputType(0, output_type.Get(), 0)) ||
      FAILED(impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
      FAILED(impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0))) {
    impl_->transform.Reset();
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  }
  impl_->config = config;
  impl_->configured = true;
  return Result<void, MfDecodeError>::ok();
}

Result<void, MfDecodeError> MfDecoder::decode(std::span<const std::byte> encoded,
                                              std::uint64_t timestamp_us) {
  if (!impl_->configured || encoded.empty()) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  }
  ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(encoded.size()), &buffer))) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  }
  BYTE* destination = nullptr;
  DWORD max_length = 0;
  DWORD current_length = 0;
  if (FAILED(buffer->Lock(&destination, &max_length, &current_length)) ||
      max_length < encoded.size()) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  }
  std::memcpy(destination, encoded.data(), encoded.size());
  buffer->Unlock();
  if (FAILED(buffer->SetCurrentLength(static_cast<DWORD>(encoded.size())))) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  }
  ComPtr<IMFSample> sample;
  if (FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get())) ||
      FAILED(sample->SetSampleTime(static_cast<LONGLONG>(timestamp_us) * 10)) ||
      FAILED(sample->SetSampleDuration(10'000'000LL / impl_->config.fps)) ||
      FAILED(impl_->transform->ProcessInput(0, sample.Get(), 0))) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  }

  MFT_OUTPUT_STREAM_INFO stream_info{};
  if (FAILED(impl_->transform->GetOutputStreamInfo(0, &stream_info))) {
    return Result<void, MfDecodeError>::err(MfDecodeError::Output);
  }
  for (unsigned attempt = 0; attempt < 4; ++attempt) {
    ComPtr<IMFSample> output_sample;
    if ((stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
      if (FAILED(MFCreateSample(&output_sample))) {
        return Result<void, MfDecodeError>::err(MfDecodeError::Output);
      }
      D3D11_TEXTURE2D_DESC texture_description{};
      texture_description.Width = impl_->config.width;
      texture_description.Height = impl_->config.height;
      texture_description.MipLevels = 1;
      texture_description.ArraySize = 1;
      texture_description.Format = DXGI_FORMAT_NV12;
      texture_description.SampleDesc.Count = 1;
      texture_description.Usage = D3D11_USAGE_DEFAULT;
      ComPtr<ID3D11Texture2D> texture;
      if (!impl_->device || FAILED(impl_->device->CreateTexture2D(
                                 &texture_description, nullptr, &texture))) {
        return Result<void, MfDecodeError>::err(MfDecodeError::Output);
      }
      ComPtr<IMFMediaBuffer> output_buffer;
      if (FAILED(MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, texture.Get(), 0, FALSE,
                                           &output_buffer))) {
        return Result<void, MfDecodeError>::err(MfDecodeError::Output);
      }
      if (FAILED(output_sample->AddBuffer(output_buffer.Get()))) {
        return Result<void, MfDecodeError>::err(MfDecodeError::Output);
      }
    }
    MFT_OUTPUT_DATA_BUFFER output{0, output_sample.Get(), 0, nullptr};
    DWORD status = 0;
    const auto result = impl_->transform->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents) {
      output.pEvents->Release();
    }
    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) {
      break;
    }
    if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
      return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    }
    if (FAILED(result) || output.pSample == nullptr) {
      return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    }
    ComPtr<IMFMediaBuffer> output_buffer;
    if (FAILED(output.pSample->GetBufferByIndex(0, &output_buffer))) {
      return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    }
    ComPtr<IMFDXGIBuffer> dxgi_buffer;
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(output_buffer.As(&dxgi_buffer)) ||
        FAILED(dxgi_buffer->GetResource(IID_PPV_ARGS(&texture)))) {
      return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    }
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    {
      std::scoped_lock lock(impl_->mutex);
      impl_->latest = {texture, timestamp_us, description.Width, description.Height};
    }
    break;
  }
  return Result<void, MfDecodeError>::ok();
}

std::optional<MfDecoder::TextureFrame> MfDecoder::take_latest() {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->latest.texture) {
    return std::nullopt;
  }
  auto result = std::move(impl_->latest);
  impl_->latest = {};
  return result;
}

void MfDecoder::stop() noexcept {
  if (!impl_) {
    return;
  }
  if (impl_->transform) {
    impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    impl_->transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
  }
  impl_->transform.Reset();
  {
    std::scoped_lock lock(impl_->mutex);
    impl_->latest = {};
  }
  impl_->configured = false;
  impl_->started = false;
  impl_->manager.Reset();
  impl_->context.Reset();
  impl_->device.Reset();
  if (impl_->mf_started) {
    MFShutdown();
    impl_->mf_started = false;
  }
  if (impl_->com_owned) {
    CoUninitialize();
    impl_->com_owned = false;
  }
}

bool MfDecoder::ready() const noexcept { return impl_ && impl_->started && impl_->configured; }

}  // namespace ministream
