#include "windows/video/dxgi_capture.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace ministream {

struct DxgiCapture::Impl {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
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

}  // namespace ministream
