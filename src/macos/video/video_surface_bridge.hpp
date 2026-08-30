#pragma once

#include <QObject>
#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <mutex>
#include <optional>

namespace ministream {

struct SurfaceFrame {
  CVPixelBufferRef pixel_buffer{};
  std::uint64_t timestamp_us{};
};

class VideoSurfaceBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool frameAvailable READ frameAvailable NOTIFY frameAvailableChanged)

 public:
  explicit VideoSurfaceBridge(QObject* parent = nullptr);
  ~VideoSurfaceBridge() override;

  [[nodiscard]] bool frameAvailable() const noexcept;
  void publish(CVPixelBufferRef pixel_buffer, std::uint64_t timestamp_us);
  std::optional<SurfaceFrame> take();

 signals:
  void frameAvailableChanged();

 private:
  mutable std::mutex mutex_;
  SurfaceFrame latest_;
};

}  // namespace ministream
