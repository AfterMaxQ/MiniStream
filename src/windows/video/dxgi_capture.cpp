#include "windows/video/dxgi_capture.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace ministream {

struct DxgiCapture::Impl {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> video_processor_enumerator;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> video_processor;
  std::uint32_t processor_input_width{};
  std::uint32_t processor_input_height{};
  std::uint32_t processor_output_width{};
  std::uint32_t processor_output_height{};
  std::uint64_t next_frame_id{};
};

DxgiCapture::DxgiCapture() : impl_(std::make_unique<Impl>()) {}
DxgiCapture::~DxgiCapture() = default;
DxgiCapture::DxgiCapture(DxgiCapture&&) noexcept = default;
DxgiCapture& DxgiCapture::operator=(DxgiCapture&&) noexcept = default;

Result<void, CaptureError> DxgiCapture::initialize() {
  constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL created_level{};
  if (FAILED(D3D11CreateDevice(
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
  if (FAILED(output1->DuplicateOutput(impl_->device.Get(), &impl_->duplication))) {
    return Result<void, CaptureError>::err(CaptureError::Initialize);
  }
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
  impl_->duplication->ReleaseFrame();
  if (FAILED(converted)) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Acquire);
  }
  D3D11_TEXTURE2D_DESC description{};
  texture->GetDesc(&description);
  return Result<CapturedFrame, CaptureError>::ok(
      {std::move(texture), impl_->next_frame_id++, SteadyClock::now(),
       description.Format, description.Width, description.Height});
}

Result<CapturedFrame, CaptureError> DxgiCapture::resize(const CapturedFrame& frame,
                                                        std::uint32_t width,
                                                        std::uint32_t height) {
  if (!impl_ || !impl_->device || !impl_->context || !frame.texture || width == 0 ||
      height == 0) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Initialize);
  }
  if (frame.width == width && frame.height == height) {
    return Result<CapturedFrame, CaptureError>::ok(frame);
  }
  if (frame.format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    return Result<CapturedFrame, CaptureError>::err(CaptureError::Acquire);
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
    if (FAILED(impl_->video_processor_enumerator->CheckVideoProcessorFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, &input_support)) ||
        FAILED(impl_->video_processor_enumerator->CheckVideoProcessorFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, &output_support)) ||
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
  if (!impl_ || !impl_->duplication) {
    return DXGI_FORMAT_UNKNOWN;
  }
  DXGI_OUTDUPL_DESC description{};
  impl_->duplication->GetDesc(&description);
  return description.ModeDesc.Format;
}

}  // namespace ministream
