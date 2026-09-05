#pragma once

#include <QQuickItem>
#include <QPointer>

#include <atomic>
#include <memory>

namespace ministream {

// Displays the latest decoded frame inside the Qt Quick scene.  The native
// implementation imports platform textures on the render thread.
class VideoSurfaceItem : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QObject* bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
  Q_PROPERTY(bool frameAvailable READ frameAvailable NOTIFY frameAvailableChanged)
  Q_PROPERTY(qreal aspectRatio READ aspectRatio NOTIFY frameAvailableChanged)
  Q_PROPERTY(bool hdrOutput READ hdrOutput NOTIFY hdrOutputChanged)

 public:
  struct Impl;

  explicit VideoSurfaceItem(QQuickItem* parent = nullptr);
  ~VideoSurfaceItem() override;

  [[nodiscard]] QObject* bridge() const noexcept;
  void setBridge(QObject* bridge);
  [[nodiscard]] bool frameAvailable() const noexcept;
  [[nodiscard]] qreal aspectRatio() const noexcept { return aspect_ratio_.load(); }
  [[nodiscard]] bool hdrOutput() const noexcept { return hdr_output_.load(); }

 signals:
  void bridgeChanged();
  void frameAvailableChanged();
  void hdrOutputChanged();

 private slots:
  void onBridgeFrameAvailable();

 protected:
  QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData* data) override;

 private:
  QPointer<QObject> bridge_;
  std::atomic_bool frame_available_{false};
  std::atomic_bool reset_pending_{true};
  std::atomic<double> aspect_ratio_{16.0 / 9.0};
  std::atomic_bool hdr_output_{false};
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
