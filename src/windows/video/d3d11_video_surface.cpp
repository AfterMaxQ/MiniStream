#include "windows/video/d3d11_video_surface.hpp"

#include <mutex>
#include <utility>

namespace ministream {

struct D3D11VideoSurface::Impl {
  mutable std::mutex mutex;
  std::optional<D3D11SurfaceFrame> latest;
};

D3D11VideoSurface::D3D11VideoSurface() : impl_(std::make_unique<Impl>()) {}
D3D11VideoSurface::~D3D11VideoSurface() = default;
D3D11VideoSurface::D3D11VideoSurface(D3D11VideoSurface&&) noexcept = default;
D3D11VideoSurface& D3D11VideoSurface::operator=(D3D11VideoSurface&&) noexcept = default;

void D3D11VideoSurface::publish(D3D11SurfaceFrame frame) {
  if (!frame.texture) {
    return;
  }
  std::scoped_lock lock(impl_->mutex);
  impl_->latest = std::move(frame);
}

std::optional<D3D11SurfaceFrame> D3D11VideoSurface::take_latest() {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->latest) {
    return std::nullopt;
  }
  auto result = std::move(impl_->latest);
  impl_->latest.reset();
  return result;
}

bool D3D11VideoSurface::available() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->latest.has_value();
}

void D3D11VideoSurface::clear() noexcept {
  std::scoped_lock lock(impl_->mutex);
  impl_->latest.reset();
}

}  // namespace ministream
