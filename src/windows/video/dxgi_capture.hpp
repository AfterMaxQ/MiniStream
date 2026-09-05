#pragma once

#include "core/base/result.hpp"
#include "core/time/clock.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>

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

struct DxgiCaptureInfo {
  std::string adapter_name;
  std::string output_name;
  std::uintptr_t monitor{};
  DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
  DXGI_COLOR_SPACE_TYPE color_space{DXGI_COLOR_SPACE_CUSTOM};
  std::uint32_t bits_per_color{};
  std::uint32_t width{};
  std::uint32_t height{};
  bool has_color_space{};
};

enum class DxgiCaptureStatus { Ready, NeedsConversion, HdrActive, UnsupportedFormat };

[[nodiscard]] DxgiCaptureStatus classify_dxgi_capture(
    const DxgiCaptureInfo& info) noexcept;
[[nodiscard]] std::string describe_dxgi_capture(const DxgiCaptureInfo& info);

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
                                             std::uint32_t height, bool hdr10 = false);

  // The encoder registers the captured texture on this same device. The
  // returned interfaces are owned by DxgiCapture and remain valid until the
  // capture object is destroyed or moved from.
  [[nodiscard]] ID3D11Device* device() const noexcept;
  [[nodiscard]] ID3D11DeviceContext* context() const noexcept;
  [[nodiscard]] DXGI_FORMAT format() const noexcept;
  [[nodiscard]] DxgiCaptureInfo capture_info() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
