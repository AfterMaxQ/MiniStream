#include "windows/video/mf_decoder.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <codecapi.h>
#include <d3d10.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>

#include <algorithm>
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
  ComPtr<ID3D11VideoDevice> video_device;
  ComPtr<ID3D11VideoContext> video_context;
  ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  ComPtr<ID3D11VideoProcessor> processor;
  CodecConfig config{};
  TextureFrame latest{};
  UINT manager_token{};
  bool mf_started{};
  bool com_owned{};
  bool started{};
  bool configured{};
  bool needs_parameter_sets{true};
  std::mutex mutex;
};

namespace {
const GUID* input_subtype(VideoCodec codec) noexcept {
  switch (codec) {
    case VideoCodec::H264: return &MFVideoFormat_H264;
    case VideoCodec::Hevc: return &MFVideoFormat_HEVC;
  }
  return nullptr;
}

ComPtr<IMFTransform> create_decoder(VideoCodec codec, IMFDXGIDeviceManager* manager) {
  const auto* subtype = input_subtype(codec);
  if (!subtype) return {};
  MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, *subtype};
  IMFActivate** activates = nullptr;
  UINT32 count{};
  // The runtime uses the synchronous ProcessInput/ProcessOutput contract.
  // Windows' system decoders support DXVA on that path; an asynchronous
  // hardware MFT requires a different event loop and must not be selected here.
  const auto status = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
      MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
      &input, nullptr, &activates, &count);
  ComPtr<IMFTransform> selected;
  if (SUCCEEDED(status) && activates) {
    for (UINT32 index = 0; index < count; ++index) {
      if (!selected) {
        ComPtr<IMFTransform> candidate;
        ComPtr<IMFAttributes> attributes;
        UINT32 aware{}, asynchronous{};
        if (SUCCEEDED(activates[index]->ActivateObject(IID_PPV_ARGS(&candidate))) &&
            SUCCEEDED(candidate->GetAttributes(&attributes))) {
          attributes->GetUINT32(MF_SA_D3D11_AWARE, &aware);
          attributes->GetUINT32(MF_TRANSFORM_ASYNC, &asynchronous);
          if (aware && !asynchronous && (!manager || SUCCEEDED(candidate->ProcessMessage(
              MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(manager))))) {
            attributes->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE);
            selected = candidate;
          }
        }
      }
      activates[index]->Release();
    }
  }
  CoTaskMemFree(activates);
  return selected;
}
}  // namespace

MfDecoder::MfDecoder() : impl_(std::make_unique<Impl>()) {}
MfDecoder::~MfDecoder() { stop(); }
MfDecoder::MfDecoder(MfDecoder&&) noexcept = default;
MfDecoder& MfDecoder::operator=(MfDecoder&&) noexcept = default;

bool MfDecoder::hardware_available(VideoCodec codec) noexcept {
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
  bool available = false;
  if (SUCCEEDED(MFStartup(MF_VERSION))) {
    { available = static_cast<bool>(create_decoder(codec, nullptr)); }
    MFShutdown();
  }
  if (SUCCEEDED(initialized)) CoUninitialize();
  return available;
}

Result<void, MfDecodeError> MfDecoder::start() {
  if (impl_->started) return Result<void, MfDecodeError>::ok();
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  if (FAILED(MFStartup(MF_VERSION))) {
    if (SUCCEEDED(initialized)) CoUninitialize();
    return Result<void, MfDecodeError>::err(MfDecodeError::Unavailable);
  }
  impl_->mf_started = true;
  impl_->com_owned = SUCCEEDED(initialized);
  D3D_FEATURE_LEVEL level{};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
      D3D11_SDK_VERSION, &impl_->device, &level, &impl_->context)) ||
      FAILED(impl_->device.As(&impl_->video_device)) ||
      FAILED(impl_->context.As(&impl_->video_context))) {
    stop();
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  }
  ComPtr<ID3D10Multithread> multithread;
  if (SUCCEEDED(impl_->device.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
  if (FAILED(MFCreateDXGIDeviceManager(&impl_->manager_token, &impl_->manager)) ||
      FAILED(impl_->manager->ResetDevice(impl_->device.Get(), impl_->manager_token))) {
    stop();
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  }
  impl_->started = true;
  return Result<void, MfDecodeError>::ok();
}

bool MfDecoder::select_output_type() {
  for (DWORD index = 0; ; ++index) {
    ComPtr<IMFMediaType> type;
    if (FAILED(impl_->transform->GetOutputAvailableType(0, index, &type))) return false;
    GUID subtype{};
    if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
        subtype == (impl_->config.hdr10 ? MFVideoFormat_P010 : MFVideoFormat_NV12) &&
        SUCCEEDED(impl_->transform->SetOutputType(0, type.Get(), 0))) return true;
  }
}

Result<void, MfDecodeError> MfDecoder::configure(const CodecConfig& config) {
  impl_->configured = false;
  impl_->transform.Reset();
  impl_->processor.Reset();
  impl_->enumerator.Reset();
  { std::scoped_lock lock(impl_->mutex); impl_->latest = {}; }
  if (!impl_->started || config.width == 0 || config.height == 0 || config.fps == 0 ||
      (config.hdr10 && config.codec != VideoCodec::Hevc) ||
      config.parameter_sets.empty() || !input_subtype(config.codec))
    return Result<void, MfDecodeError>::err(MfDecodeError::InvalidConfig);
  impl_->transform = create_decoder(config.codec, impl_->manager.Get());
  impl_->config = config;
  if (!impl_->transform) return Result<void, MfDecodeError>::err(MfDecodeError::Unavailable);
  ComPtr<IMFMediaType> input;
  if (FAILED(MFCreateMediaType(&input)) ||
      FAILED(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
      FAILED(input->SetGUID(MF_MT_SUBTYPE, *input_subtype(config.codec))) ||
      FAILED(MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, config.width, config.height)) ||
      FAILED(MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, config.fps, 1)) ||
      FAILED(impl_->transform->SetInputType(0, input.Get(), 0)) || !select_output_type() ||
      FAILED(impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
      FAILED(impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0)))
    return Result<void, MfDecodeError>::err(MfDecodeError::Initialize);
  impl_->config = config;
  impl_->needs_parameter_sets = true;
  impl_->configured = true;
  return Result<void, MfDecodeError>::ok();
}

Result<void, MfDecodeError> MfDecoder::decode(std::span<const std::byte> encoded,
                                             std::uint64_t timestamp_us) {
  if (!ready() || encoded.empty()) return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  // VideoToolbox sends SPS/PPS out of band. MF consumes Annex B in-band.
  const auto prefix = impl_->needs_parameter_sets ? impl_->config.parameter_sets.size() : 0U;
  ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(prefix + encoded.size()), &buffer)))
    return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  BYTE* destination{};
  if (FAILED(buffer->Lock(&destination, nullptr, nullptr)))
    return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  if (prefix) std::memcpy(destination, impl_->config.parameter_sets.data(), prefix);
  std::memcpy(destination + prefix, encoded.data(), encoded.size());
  buffer->Unlock();
  ComPtr<IMFSample> sample;
  if (FAILED(buffer->SetCurrentLength(static_cast<DWORD>(prefix + encoded.size()))) ||
      FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get())) ||
      FAILED(sample->SetSampleTime(static_cast<LONGLONG>(timestamp_us) * 10)) ||
      FAILED(sample->SetSampleDuration(10'000'000LL / impl_->config.fps)))
    return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  auto status = impl_->transform->ProcessInput(0, sample.Get(), 0);
  if (status == MF_E_NOTACCEPTING) {
    if (auto drained = drain_output(); !drained) return drained;
    status = impl_->transform->ProcessInput(0, sample.Get(), 0);
  }
  if (FAILED(status)) return Result<void, MfDecodeError>::err(MfDecodeError::Input);
  impl_->needs_parameter_sets = false;
  return drain_output();
}

Result<void, MfDecodeError> MfDecoder::drain_output() {
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    MFT_OUTPUT_STREAM_INFO info{};
    if (FAILED(impl_->transform->GetOutputStreamInfo(0, &info)))
      return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    ComPtr<IMFSample> owned;
    if ((info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0) {
      ComPtr<IMFMediaBuffer> buffer;
      if (FAILED(MFCreateSample(&owned)) ||
          FAILED(MFCreateAlignedMemoryBuffer(info.cbSize, info.cbAlignment ? info.cbAlignment - 1 : 0, &buffer)) ||
          FAILED(owned->AddBuffer(buffer.Get())))
        return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    }
    MFT_OUTPUT_DATA_BUFFER output{0, owned.Get(), 0, nullptr};
    DWORD status{};
    const auto result = impl_->transform->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents) output.pEvents->Release();
    if (output.pSample && output.pSample != owned.Get()) owned.Attach(output.pSample);
    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) return Result<void, MfDecodeError>::ok();
    if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
      impl_->processor.Reset();
      impl_->enumerator.Reset();
      if (!select_output_type()) return Result<void, MfDecodeError>::err(MfDecodeError::Output);
      continue;
    }
    if (FAILED(result) || !owned) return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFDXGIBuffer> dxgi;
    ComPtr<ID3D11Texture2D> texture;
    UINT subresource{};
    if (FAILED(owned->GetBufferByIndex(0, &buffer)) || FAILED(buffer.As(&dxgi)) ||
        FAILED(dxgi->GetResource(IID_PPV_ARGS(&texture))) || FAILED(dxgi->GetSubresourceIndex(&subresource)))
      return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    D3D11_TEXTURE2D_DESC input_desc{};
    texture->GetDesc(&input_desc);
    if (!impl_->processor) {
      D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
      content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
      content.InputWidth = input_desc.Width;
      content.InputHeight = input_desc.Height;
      content.OutputWidth = impl_->config.width;
      content.OutputHeight = impl_->config.height;
      content.InputFrameRate = {impl_->config.fps, 1};
      content.OutputFrameRate = content.InputFrameRate;
      content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
      if (FAILED(impl_->video_device->CreateVideoProcessorEnumerator(&content, &impl_->enumerator)) ||
          FAILED(impl_->video_device->CreateVideoProcessor(impl_->enumerator.Get(), 0, &impl_->processor)))
        return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    }
    D3D11_TEXTURE2D_DESC output_desc{};
    output_desc.Width = impl_->config.width;
    output_desc.Height = impl_->config.height;
    output_desc.MipLevels = output_desc.ArraySize = output_desc.SampleDesc.Count = 1;
    // QSGD3D11Texture::fromNative imports RGBA8, including its shader view.
    output_desc.Format = impl_->config.hdr10 ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                            : DXGI_FORMAT_R8G8B8A8_UNORM;
    output_desc.Usage = D3D11_USAGE_DEFAULT;
    output_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    output_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ComPtr<ID3D11Texture2D> rgb;
    ComPtr<IDXGIKeyedMutex> gate;
    ComPtr<ID3D11VideoProcessorInputView> input_view;
    ComPtr<ID3D11VideoProcessorOutputView> output_view;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_view_desc{};
    input_view_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_view_desc.Texture2D.MipSlice = subresource % input_desc.MipLevels;
    input_view_desc.Texture2D.ArraySlice = subresource / input_desc.MipLevels;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_view_desc{};
    output_view_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    if (FAILED(impl_->device->CreateTexture2D(&output_desc, nullptr, &rgb)) ||
        FAILED(rgb.As(&gate)) ||
        FAILED(impl_->video_device->CreateVideoProcessorInputView(texture.Get(), impl_->enumerator.Get(), &input_view_desc, &input_view)) ||
        FAILED(impl_->video_device->CreateVideoProcessorOutputView(rgb.Get(), impl_->enumerator.Get(), &output_view_desc, &output_view)) ||
        gate->AcquireSync(0, 0) != S_OK)
      return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE yuv{};
    yuv.YCbCr_Matrix = 1;  // BT.709, limited-range NV12.
    yuv.Nominal_Range = 2;  // 16..235; 1 means full range.
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb_space{};
    rgb_space.RGB_Range = 0;
    RECT rect{0, 0, static_cast<LONG>(impl_->config.width), static_cast<LONG>(impl_->config.height)};
    impl_->video_context->VideoProcessorSetStreamFrameFormat(impl_->processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    impl_->video_context->VideoProcessorSetStreamSourceRect(impl_->processor.Get(), 0, TRUE, &rect);
    impl_->video_context->VideoProcessorSetStreamDestRect(impl_->processor.Get(), 0, TRUE, &rect);
    impl_->video_context->VideoProcessorSetOutputTargetRect(impl_->processor.Get(), TRUE, &rect);
    impl_->video_context->VideoProcessorSetStreamColorSpace(impl_->processor.Get(), 0, &yuv);
    impl_->video_context->VideoProcessorSetOutputColorSpace(impl_->processor.Get(), &rgb_space);
    if (impl_->config.hdr10) {
      ComPtr<ID3D11VideoContext1> colors;
      if (FAILED(impl_->video_context.As(&colors))) {
        gate->ReleaseSync(0);
        return Result<void, MfDecodeError>::err(MfDecodeError::Output);
      }
      colors->VideoProcessorSetStreamColorSpace1(impl_->processor.Get(), 0,
          DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020);
      colors->VideoProcessorSetOutputColorSpace1(impl_->processor.Get(),
          DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    }
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = input_view.Get();
    const auto converted = impl_->video_context->VideoProcessorBlt(impl_->processor.Get(), output_view.Get(), 0, 1, &stream);
    impl_->context->Flush();
    const auto released = gate->ReleaseSync(1);
    if (FAILED(converted) || FAILED(released)) return Result<void, MfDecodeError>::err(MfDecodeError::Output);
    LONGLONG pts{};
    owned->GetSampleTime(&pts);
    std::scoped_lock lock(impl_->mutex);
    impl_->latest = {rgb, static_cast<std::uint64_t>(std::max<LONGLONG>(0, pts) / 10),
                     impl_->config.width, impl_->config.height};
  }
  return Result<void, MfDecodeError>::ok();
}

std::optional<MfDecoder::TextureFrame> MfDecoder::take_latest() {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->latest.texture) return std::nullopt;
  auto result = std::move(impl_->latest);
  impl_->latest = {};
  return result;
}

void MfDecoder::stop() noexcept {
  if (!impl_) return;
  if (impl_->transform) {
    impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    impl_->transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
  }
  impl_->transform.Reset();
  { std::scoped_lock lock(impl_->mutex); impl_->latest = {}; }
  impl_->configured = impl_->started = false;
  impl_->processor.Reset();
  impl_->enumerator.Reset();
  impl_->video_context.Reset();
  impl_->video_device.Reset();
  impl_->manager.Reset();
  impl_->context.Reset();
  impl_->device.Reset();
  if (impl_->mf_started) { MFShutdown(); impl_->mf_started = false; }
  if (impl_->com_owned) { CoUninitialize(); impl_->com_owned = false; }
}

bool MfDecoder::ready() const noexcept {
  return impl_ && impl_->started && impl_->configured && impl_->transform;
}
}  // namespace ministream
