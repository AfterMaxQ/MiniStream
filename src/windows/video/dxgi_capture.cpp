#include "windows/video/dxgi_capture.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>

namespace ministream {
namespace {

std::string utf8(const wchar_t* text) {
  if (text == nullptr || *text == L'\0') {
    return {};
  }
  const auto length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) {
    return {};
  }
  std::string converted(static_cast<std::size_t>(length), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, text, -1, converted.data(), length, nullptr, nullptr) ==
      0) {
    return {};
  }
  converted.pop_back();
  return converted;
}

const char* dxgi_format_name(DXGI_FORMAT format) noexcept {
  switch (format) {
    case DXGI_FORMAT_UNKNOWN:
      return "DXGI_FORMAT_UNKNOWN";
    case DXGI_FORMAT_B8G8R8A8_UNORM:
      return "DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UNORM:
      return "DXGI_FORMAT_R10G10B10A2_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      return "DXGI_FORMAT_R16G16B16A16_FLOAT";
    default:
      return "DXGI_FORMAT_OTHER";
  }
}

const char* dxgi_color_space_name(DXGI_COLOR_SPACE_TYPE color_space) noexcept {
  switch (color_space) {
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
      return "DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709";
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
      return "DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020";
    case DXGI_COLOR_SPACE_CUSTOM:
      return "DXGI_COLOR_SPACE_CUSTOM";
    default:
      return "DXGI_COLOR_SPACE_OTHER";
  }
}

constexpr char kFp16ToBgraShader[] = R"(
struct VertexOutput {
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD0;
};

VertexOutput vertex_main(uint vertex_id : SV_VertexID) {
  const float2 texcoord = float2((vertex_id << 1) & 2, vertex_id & 2);
  VertexOutput output;
  output.position = float4(texcoord.x * 2.0 - 1.0, 1.0 - texcoord.y * 2.0, 0.0, 1.0);
  output.texcoord = texcoord;
  return output;
}

Texture2D<float4> source_texture : register(t0);
SamplerState source_sampler : register(s0);
cbuffer ColorOptions : register(b0) { uint source_hdr; uint output_hdr; uint2 padding; };

float3 pq(float3 color) {
  float3 p = pow(max(color, 0.0) * (80.0 / 10000.0), 2610.0 / 16384.0);
  return pow((3424.0 / 4096.0 + (2413.0 / 128.0) * p) /
             (1.0 + (2392.0 / 128.0) * p), 2523.0 / 32.0);
}

float4 pixel_main(VertexOutput input) : SV_TARGET {
  const float4 source = source_texture.SampleLevel(source_sampler, input.texcoord, 0.0);
  float3 linear_color = max(source.rgb, 0.0);
  if (output_hdr != 0) {
    float3 rec2020 = mul(float3x3(0.627404, 0.329283, 0.043313,
                                 0.069097, 0.919540, 0.011362,
                                 0.016391, 0.088013, 0.895595), linear_color);
    return float4(pq(rec2020), 1.0);
  }
  if (source_hdr != 0) {
    linear_color *= 80.0 / 203.0;
    float luminance = dot(linear_color, float3(0.2126, 0.7152, 0.0722));
    linear_color *= (1.0 + luminance / 24.27) / (1.0 + luminance);
  }
  linear_color = saturate(linear_color);
  const float3 exponent = float3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4);
  const float3 high = 1.055 * pow(linear_color, exponent) - 0.055;
  const float3 low = 12.92 * linear_color;
  const float3 use_low = 1.0 - step(0.0031308, linear_color);
  return float4(lerp(high, low, use_low), 1.0);
}
)";

}  // namespace

DxgiCaptureStatus classify_dxgi_capture(const DxgiCaptureInfo& info) noexcept {
  if (info.has_color_space &&
      info.color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
    return DxgiCaptureStatus::HdrActive;
  }
  if (info.has_color_space &&
      info.color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 &&
      info.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
    return DxgiCaptureStatus::NeedsConversion;
  }
  if (info.format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    return DxgiCaptureStatus::UnsupportedFormat;
  }
  return DxgiCaptureStatus::Ready;
}

std::string describe_dxgi_capture(const DxgiCaptureInfo& info) {
  std::ostringstream detail;
  detail << "adapter=" << (info.adapter_name.empty() ? "unknown" : info.adapter_name)
         << ", output=" << (info.output_name.empty() ? "unknown" : info.output_name)
         << ", monitor=0x" << std::hex << info.monitor << std::dec
         << ", format=" << dxgi_format_name(info.format) << "(" << info.format << ")"
         << ", color_space=";
  if (info.has_color_space) {
    detail << dxgi_color_space_name(info.color_space) << "(" << info.color_space << ")";
  } else {
    detail << "unavailable";
  }
  detail << ", bits_per_color=" << info.bits_per_color << ", size=" << info.width << "x"
         << info.height;
  return detail.str();
}

struct DxgiCapture::Impl {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> video_processor_enumerator;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> video_processor;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> fp16_vertex_shader;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> fp16_pixel_shader;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> fp16_sampler;
  Microsoft::WRL::ComPtr<ID3D11Buffer> color_options;
  std::uint32_t processor_input_width{};
  std::uint32_t processor_input_height{};
  std::uint32_t processor_output_width{};
  std::uint32_t processor_output_height{};
  std::uint64_t next_frame_id{};
  DxgiCaptureInfo capture_info;
};

DxgiCapture::DxgiCapture() : impl_(std::make_unique<Impl>()) {}
DxgiCapture::~DxgiCapture() = default;
DxgiCapture::DxgiCapture(DxgiCapture&&) noexcept = default;
DxgiCapture& DxgiCapture::operator=(DxgiCapture&&) noexcept = default;

Result<void, CaptureError> DxgiCapture::initialize() {
  impl_->capture_info = {};
  constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL created_level{};
  if (!impl_->device && FAILED(D3D11CreateDevice(
          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
          D3D11_SDK_VERSION, &impl_->device, &created_level, &impl_->context))) {
    return Result<void, CaptureError>::err(CaptureError::Initialize);
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  Microsoft::WRL::ComPtr<IDXGIOutput> output;
  Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
  if (FAILED(impl_->device.As(&dxgi_device)) ||
      FAILED(dxgi_device->GetAdapter(&adapter)) ||
      FAILED(adapter->EnumOutputs(0, &output)) || FAILED(output.As(&output1))) {
    return Result<void, CaptureError>::err(CaptureError::NoOutput);
  }

  DXGI_ADAPTER_DESC adapter_description{};
  if (SUCCEEDED(adapter->GetDesc(&adapter_description))) {
    impl_->capture_info.adapter_name = utf8(adapter_description.Description);
  }
  DXGI_OUTPUT_DESC output_description{};
  if (SUCCEEDED(output->GetDesc(&output_description))) {
    impl_->capture_info.output_name = utf8(output_description.DeviceName);
    impl_->capture_info.monitor = reinterpret_cast<std::uintptr_t>(output_description.Monitor);
    impl_->capture_info.width = static_cast<std::uint32_t>(std::max<LONG>(
        0, output_description.DesktopCoordinates.right -
               output_description.DesktopCoordinates.left));
    impl_->capture_info.height = static_cast<std::uint32_t>(std::max<LONG>(
        0, output_description.DesktopCoordinates.bottom -
               output_description.DesktopCoordinates.top));
  }
  Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
  DXGI_OUTPUT_DESC1 output_description1{};
  if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&output_description1))) {
    impl_->capture_info.has_color_space = true;
    impl_->capture_info.color_space = output_description1.ColorSpace;
    impl_->capture_info.bits_per_color = output_description1.BitsPerColor;
  }
  Microsoft::WRL::ComPtr<IDXGIOutput5> output5;
  const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
  const auto duplicated = SUCCEEDED(output.As(&output5))
      ? output5->DuplicateOutput1(impl_->device.Get(), 0, 2, formats, &impl_->duplication)
      : output1->DuplicateOutput(impl_->device.Get(), &impl_->duplication);
  if (FAILED(duplicated)) {
    return Result<void, CaptureError>::err(CaptureError::Initialize);
  }
  DXGI_OUTDUPL_DESC duplication_description{};
  impl_->duplication->GetDesc(&duplication_description);
  impl_->capture_info.format = duplication_description.ModeDesc.Format;
  impl_->capture_info.width = duplication_description.ModeDesc.Width;
  impl_->capture_info.height = duplication_description.ModeDesc.Height;
  const auto diagnostic = "DXGI capture: " + describe_dxgi_capture(impl_->capture_info) + "\n";
  std::clog << diagnostic;
  OutputDebugStringA(diagnostic.c_str());
  return Result<void, CaptureError>::ok();
}

Result<CapturedFrame, CaptureError> DxgiCapture::acquire(Microseconds timeout) {
  if (!impl_->duplication) {
    if (auto initialized = initialize(); !initialized) {
      return Result<CapturedFrame, CaptureError>::err(initialized.error());
    }
  }

  const auto timeout_ms = std::clamp<std::int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count(), 0,
      std::numeric_limits<DWORD>::max());
  DXGI_OUTDUPL_FRAME_INFO info{};
  Microsoft::WRL::ComPtr<IDXGIResource> resource;
  const auto acquired = impl_->duplication->AcquireNextFrame(
      static_cast<DWORD>(timeout_ms), &info, &resource);
  if (acquired == DXGI_ERROR_WAIT_TIMEOUT) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Timeout);
  }
  if (acquired == DXGI_ERROR_ACCESS_LOST) {
    impl_->duplication.Reset();
    return Result<CapturedFrame, CaptureError>::err(CaptureError::AccessLost);
  }
  if (FAILED(acquired)) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Acquire);
  }

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  const auto converted = resource.As(&texture);
  if (FAILED(converted)) {
    impl_->duplication->ReleaseFrame();
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Acquire);
  }
  D3D11_TEXTURE2D_DESC description{};
  texture->GetDesc(&description);
  // The duplication surface is no longer ours after ReleaseFrame. Copy it
  // while acquired so NVENC (and the static-desktop cache) owns stable pixels.
  auto copy_description = description;
  copy_description.Usage = D3D11_USAGE_DEFAULT;
  copy_description.CPUAccessFlags = 0;
  copy_description.MiscFlags = 0;
  copy_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> owned;
  if (FAILED(impl_->device->CreateTexture2D(&copy_description, nullptr, &owned))) {
    impl_->duplication->ReleaseFrame();
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Acquire);
  }
  impl_->context->CopyResource(owned.Get(), texture.Get());
  impl_->context->Flush();
  impl_->duplication->ReleaseFrame();
  return Result<CapturedFrame, CaptureError>::ok(
      {std::move(owned), impl_->next_frame_id++, SteadyClock::now(),
       description.Format, description.Width, description.Height});
}

Result<CapturedFrame, CaptureError> DxgiCapture::resize(const CapturedFrame& frame,
                                                        std::uint32_t width,
                                                        std::uint32_t height, bool hdr10) {
  if (!impl_ || !impl_->device || !impl_->context || !frame.texture || width == 0 ||
      height == 0) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
  }
  if (frame.width == width && frame.height == height &&
      frame.format == DXGI_FORMAT_B8G8R8A8_UNORM && !hdr10) {
    return Result<CapturedFrame, CaptureError>::ok(frame);
  }
  if (frame.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
    if (!impl_->capture_info.has_color_space ||
        (impl_->capture_info.color_space != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 &&
         impl_->capture_info.color_space != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)) {
      return Result<CapturedFrame, CaptureError>::err(CaptureError::UnsupportedFormat);
    }
    if (!impl_->fp16_vertex_shader || !impl_->fp16_pixel_shader || !impl_->fp16_sampler) {
      Microsoft::WRL::ComPtr<ID3DBlob> vertex_bytecode;
      Microsoft::WRL::ComPtr<ID3DBlob> pixel_bytecode;
      Microsoft::WRL::ComPtr<ID3DBlob> errors;
      if (FAILED(D3DCompile(kFp16ToBgraShader, sizeof(kFp16ToBgraShader), nullptr, nullptr,
                            nullptr, "vertex_main", "vs_5_0", 0, 0, &vertex_bytecode,
                            &errors)) ||
          FAILED(D3DCompile(kFp16ToBgraShader, sizeof(kFp16ToBgraShader), nullptr, nullptr,
                            nullptr, "pixel_main", "ps_5_0", 0, 0, &pixel_bytecode,
                            &errors)) ||
          FAILED(impl_->device->CreateVertexShader(
              vertex_bytecode->GetBufferPointer(), vertex_bytecode->GetBufferSize(), nullptr,
              &impl_->fp16_vertex_shader)) ||
          FAILED(impl_->device->CreatePixelShader(
              pixel_bytecode->GetBufferPointer(), pixel_bytecode->GetBufferSize(), nullptr,
              &impl_->fp16_pixel_shader))) {
        if (errors && errors->GetBufferPointer()) {
          std::clog << "DXGI FP16 conversion shader failed: "
                    << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
        }
        return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
      }
      D3D11_SAMPLER_DESC sampler_description{};
      sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
      if (FAILED(impl_->device->CreateSamplerState(&sampler_description,
                                                    &impl_->fp16_sampler))) {
        return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
      }
    }

    D3D11_TEXTURE2D_DESC output_description{};
    output_description.Width = width;
    output_description.Height = height;
    output_description.MipLevels = 1;
    output_description.ArraySize = 1;
    output_description.Format = hdr10 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    output_description.SampleDesc.Count = 1;
    output_description.Usage = D3D11_USAGE_DEFAULT;
    output_description.BindFlags = D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> input_view;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output_view;
    if (FAILED(impl_->device->CreateTexture2D(&output_description, nullptr,
                                               &output_texture)) ||
        FAILED(impl_->device->CreateShaderResourceView(frame.texture.Get(), nullptr,
                                                        &input_view)) ||
        FAILED(impl_->device->CreateRenderTargetView(output_texture.Get(), nullptr,
                                                      &output_view))) {
      return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
    }

    if (!impl_->color_options) {
      D3D11_BUFFER_DESC options{};
      options.ByteWidth = 16;
      options.Usage = D3D11_USAGE_DEFAULT;
      options.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      if (FAILED(impl_->device->CreateBuffer(&options, nullptr, &impl_->color_options)))
        return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
    }
    const UINT colors[4] = {
        impl_->capture_info.color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? 1U : 0U,
        hdr10 ? 1U : 0U, 0, 0};
    impl_->context->UpdateSubresource(impl_->color_options.Get(), 0, nullptr, colors, 0, 0);
    ID3D11Buffer* options = impl_->color_options.Get();
    impl_->context->PSSetConstantBuffers(0, 1, &options);
    const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(width),
                                  static_cast<float>(height), 0.0F, 1.0F};
    ID3D11RenderTargetView* render_target = output_view.Get();
    ID3D11ShaderResourceView* shader_resource = input_view.Get();
    ID3D11SamplerState* sampler = impl_->fp16_sampler.Get();
    impl_->context->OMSetRenderTargets(1, &render_target, nullptr);
    impl_->context->RSSetViewports(1, &viewport);
    impl_->context->IASetInputLayout(nullptr);
    impl_->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    impl_->context->VSSetShader(impl_->fp16_vertex_shader.Get(), nullptr, 0);
    impl_->context->PSSetShader(impl_->fp16_pixel_shader.Get(), nullptr, 0);
    impl_->context->PSSetShaderResources(0, 1, &shader_resource);
    impl_->context->PSSetSamplers(0, 1, &sampler);
    impl_->context->Draw(3, 0);
    ID3D11ShaderResourceView* no_resource = nullptr;
    impl_->context->PSSetShaderResources(0, 1, &no_resource);
    impl_->context->OMSetRenderTargets(0, nullptr, nullptr);
    return Result<CapturedFrame, CaptureError>::ok(
        {std::move(output_texture), frame.frame_id, frame.captured_at,
         output_description.Format, width, height});
  }
  if (hdr10 || frame.format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::UnsupportedFormat);
  }

  if (!impl_->video_device || !impl_->video_context || !impl_->video_processor ||
      impl_->processor_input_width != frame.width ||
      impl_->processor_input_height != frame.height ||
      impl_->processor_output_width != width || impl_->processor_output_height != height) {
    if (FAILED(impl_->device.As(&impl_->video_device)) ||
        FAILED(impl_->context.As(&impl_->video_context))) {
      return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
    }
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = frame.width;
    content.InputHeight = frame.height;
    content.InputFrameRate = {60, 1};
    content.OutputWidth = width;
    content.OutputHeight = height;
    content.OutputFrameRate = {60, 1};
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    if (FAILED(impl_->video_device->CreateVideoProcessorEnumerator(
            &content, &impl_->video_processor_enumerator)) ||
        FAILED(impl_->video_device->CreateVideoProcessor(
            impl_->video_processor_enumerator.Get(), 0, &impl_->video_processor))) {
      impl_->video_processor_enumerator.Reset();
      impl_->video_processor.Reset();
      return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
    }
    UINT input_support = 0;
    UINT output_support = 0;
    const auto input_support_result = impl_->video_processor_enumerator->CheckVideoProcessorFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, &input_support);
    const auto output_support_result =
        impl_->video_processor_enumerator->CheckVideoProcessorFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, &output_support);
    if (FAILED(input_support_result) || FAILED(output_support_result) ||
        (input_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 ||
        (output_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
      impl_->video_processor.Reset();
      impl_->video_processor_enumerator.Reset();
      return Result<CapturedFrame, CaptureError>::err(CaptureError::UnsupportedFormat);
    }
    impl_->processor_input_width = frame.width;
    impl_->processor_input_height = frame.height;
    impl_->processor_output_width = width;
    impl_->processor_output_height = height;
  }

  D3D11_TEXTURE2D_DESC output_description{};
  output_description.Width = width;
  output_description.Height = height;
  output_description.MipLevels = 1;
  output_description.ArraySize = 1;
  output_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  output_description.SampleDesc.Count = 1;
  output_description.Usage = D3D11_USAGE_DEFAULT;
  output_description.BindFlags = D3D11_BIND_RENDER_TARGET;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> output_texture;
  if (FAILED(impl_->device->CreateTexture2D(&output_description, nullptr, &output_texture))) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
  }

  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_description{};
  input_description.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  input_description.Texture2D.MipSlice = 0;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view;
  if (FAILED(impl_->video_device->CreateVideoProcessorInputView(
          frame.texture.Get(), impl_->video_processor_enumerator.Get(), &input_description,
          &input_view))) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
  }
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_view_description{};
  output_view_description.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  output_view_description.Texture2D.MipSlice = 0;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> output_view;
  if (FAILED(impl_->video_device->CreateVideoProcessorOutputView(
          output_texture.Get(), impl_->video_processor_enumerator.Get(),
          &output_view_description, &output_view))) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
  }

  RECT source{0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height)};
  RECT destination{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  impl_->video_context->VideoProcessorSetStreamSourceRect(
      impl_->video_processor.Get(), 0, TRUE, &source);
  impl_->video_context->VideoProcessorSetStreamDestRect(
      impl_->video_processor.Get(), 0, TRUE, &destination);
  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable = TRUE;
  stream.pInputSurface = input_view.Get();
  if (FAILED(impl_->video_context->VideoProcessorBlt(
          impl_->video_processor.Get(), output_view.Get(), 0, 1, &stream))) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Acquire);
  }
  return Result<CapturedFrame, CaptureError>::ok(
      {std::move(output_texture), frame.frame_id, frame.captured_at,
       DXGI_FORMAT_B8G8R8A8_UNORM, width, height});
}

ID3D11Device* DxgiCapture::device() const noexcept {
  return impl_ ? impl_->device.Get() : nullptr;
}

ID3D11DeviceContext* DxgiCapture::context() const noexcept {
  return impl_ ? impl_->context.Get() : nullptr;
}

DXGI_FORMAT DxgiCapture::format() const noexcept {
  return impl_ ? impl_->capture_info.format : DXGI_FORMAT_UNKNOWN;
}

DxgiCaptureInfo DxgiCapture::capture_info() const {
  return impl_ ? impl_->capture_info : DxgiCaptureInfo{};
}

}  // namespace ministream
