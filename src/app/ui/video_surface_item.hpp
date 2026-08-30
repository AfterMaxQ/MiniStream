#pragma once

#include <QQuickItem>
#include <QPointer>

#include <atomic>
#include <memory>

namespace ministream {

// Displays the latest decoded frame inside the Qt Quick scene.  The native
// implementation imports platform textures on the render thread; the
// Windows stub keeps the QML surface valid until its D3D11 presentation path
// is connected.
class VideoSurfaceItem : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QObject* bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
  Q_PROPERTY(bool frameAvailable READ frameAvailable NOTIFY frameAvailableChanged)

 public:
  struct Impl;

  explicit VideoSurfaceItem(QQuickItem* parent = nullptr);
  ~VideoSurfaceItem() override;

  [[nodiscard]] QObject* bridge() const noexcept;
  void setBridge(QObject* bridge);
  [[nodiscard]] bool frameAvailable() const noexcept;

 signals:
  void bridgeChanged();
  void frameAvailableChanged();

 private slots:
  void onBridgeFrameAvailable();

 protected:
  QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData* data) override;

 private:
  QPointer<QObject> bridge_;
  std::atomic_bool frame_available_{false};
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
