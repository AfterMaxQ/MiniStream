#include "app/ui/windows_video_surface_bridge.hpp"

#include "windows/platform/remote_backend.hpp"

namespace ministream {

WindowsVideoSurfaceBridge::WindowsVideoSurfaceBridge(WindowsRemoteBackend* backend,
                                                     QObject* parent)
    : QObject(parent), backend_(backend) {
  if (backend_) {
    backend_->set_surface_notifier([this] { emit frameAvailableChanged(); });
  }
}

WindowsVideoSurfaceBridge::~WindowsVideoSurfaceBridge() {
  if (backend_) {
    backend_->set_surface_notifier({});
  }
}

bool WindowsVideoSurfaceBridge::frameAvailable() const noexcept {
  return backend_ && backend_->surface_available();
}

std::optional<D3D11SurfaceFrame> WindowsVideoSurfaceBridge::take() {
  return backend_ ? backend_->take_surface_frame() : std::nullopt;
}

}  // namespace ministream
