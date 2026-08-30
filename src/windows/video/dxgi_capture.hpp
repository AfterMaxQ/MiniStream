#pragma once

#include "core/base/result.hpp"
#include "core/time/clock.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace ministream {

enum class CaptureError { Initialize, NoOutput, Timeout, AccessLost, Acquire, UnsupportedFormat };

struct CapturedFrame {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  std::uint64_t frame_id{};
  SteadyClock::time_point captured_at{};
  DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
  std::uint32_t width{};
  std::uint32_t height{};
};

class DxgiCapture {
 public:
  DxgiCapture();
  ~DxgiCapture();
  DxgiCapture(DxgiCapture&&) noexcept;
  DxgiCapture& operator=(DxgiCapture&&) noexcept;
  DxgiCapture(const DxgiCapture&) = delete;
  DxgiCapture& operator=(const DxgiCapture&) = delete;

  Result<void, CaptureError> initialize();
  Result<CapturedFrame, CaptureError> acquire(Microseconds timeout);
  Result<CapturedFrame, CaptureError> resize(const CapturedFrame& frame,
                                             std::uint32_t width,
                                             std::uint32_t height);

  // The encoder registers the captured texture on this same device. The
  // returned interfaces are owned by DxgiCapture and remain valid until the
  // capture object is destroyed or moved from.
  [[nodiscard]] ID3D11Device* device() const noexcept;
  [[nodiscard]] ID3D11DeviceContext* context() const noexcept;
  [[nodiscard]] DXGI_FORMAT format() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
