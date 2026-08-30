#pragma once

#include "windows/video/d3d11_video_surface.hpp"

#include <QObject>

#include <memory>
#include <optional>

namespace ministream {

class WindowsRemoteBackend;

class WindowsVideoSurfaceBridge final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool frameAvailable READ frameAvailable NOTIFY frameAvailableChanged)

 public:
  explicit WindowsVideoSurfaceBridge(WindowsRemoteBackend* backend,
                                     QObject* parent = nullptr);
  ~WindowsVideoSurfaceBridge() override;

  [[nodiscard]] bool frameAvailable() const noexcept;
  std::optional<D3D11SurfaceFrame> take();

 signals:
  void frameAvailableChanged();

 private:
  WindowsRemoteBackend* backend_{};
};

}  // namespace ministream
