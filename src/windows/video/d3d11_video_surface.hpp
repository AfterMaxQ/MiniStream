#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace ministream {

struct D3D11SurfaceFrame {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  std::uint64_t timestamp_us{};
  std::uint32_t width{};
  std::uint32_t height{};
};

class D3D11VideoSurface {
 public:
  D3D11VideoSurface();
  ~D3D11VideoSurface();
  D3D11VideoSurface(D3D11VideoSurface&&) noexcept;
  D3D11VideoSurface& operator=(D3D11VideoSurface&&) noexcept;
  D3D11VideoSurface(const D3D11VideoSurface&) = delete;
  D3D11VideoSurface& operator=(const D3D11VideoSurface&) = delete;

  void publish(D3D11SurfaceFrame frame);
  std::optional<D3D11SurfaceFrame> take_latest();
  [[nodiscard]] bool available() const noexcept;
  void clear() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
