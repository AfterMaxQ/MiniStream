#include "app/ui/video_surface_item.hpp"

#include "app/ui/windows_video_surface_bridge.hpp"

#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/qsgtexture_platform.h>
#include <QSGNode>

#include <d3d11.h>

#include <cstdint>
#include <utility>

namespace ministream {

struct VideoSurfaceItem::Impl {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> current;
  std::uint32_t width{};
  std::uint32_t height{};
};

VideoSurfaceItem::VideoSurfaceItem(QQuickItem* parent)
    : QQuickItem(parent), impl_(std::make_unique<Impl>()) {
  setFlag(ItemHasContents, true);
}

VideoSurfaceItem::~VideoSurfaceItem() = default;

QObject* VideoSurfaceItem::bridge() const noexcept { return bridge_.data(); }

void VideoSurfaceItem::setBridge(QObject* bridge) {
  if (bridge_.data() == bridge) {
    return;
  }
  if (bridge_) {
    disconnect(bridge_, nullptr, this, nullptr);
  }
  bridge_ = bridge;
  if (auto* surface = qobject_cast<WindowsVideoSurfaceBridge*>(bridge_.data())) {
    connect(surface, &WindowsVideoSurfaceBridge::frameAvailableChanged, this,
            &VideoSurfaceItem::onBridgeFrameAvailable, Qt::QueuedConnection);
    if (surface->frameAvailable()) {
      frame_available_.store(true, std::memory_order_release);
    }
  }
  emit bridgeChanged();
  update();
}

bool VideoSurfaceItem::frameAvailable() const noexcept {
  if (frame_available_.load(std::memory_order_acquire)) {
    return true;
  }
  const auto* surface = qobject_cast<const WindowsVideoSurfaceBridge*>(bridge_.data());
  return surface && surface->frameAvailable();
}

void VideoSurfaceItem::onBridgeFrameAvailable() {
  frame_available_.store(true, std::memory_order_release);
  update();
  emit frameAvailableChanged();
}

QSGNode* VideoSurfaceItem::updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) {
  auto* node = static_cast<QSGSimpleTextureNode*>(old_node);
  if (!node) {
    node = new QSGSimpleTextureNode();
    node->setFiltering(QSGTexture::Linear);
  }

  if (auto* surface = qobject_cast<WindowsVideoSurfaceBridge*>(bridge_.data())) {
    if (const auto frame = surface->take(); frame) {
      impl_->current = frame->texture;
      impl_->width = frame->width;
      impl_->height = frame->height;
      frame_available_.store(true, std::memory_order_release);
    }
  }

  if (impl_->current && window() && impl_->width != 0 && impl_->height != 0) {
    auto* texture = QNativeInterface::QSGD3D11Texture::fromNative(
        impl_->current.Get(), window(),
        QSize(static_cast<int>(impl_->width), static_cast<int>(impl_->height)));
    if (texture) {
      node->setTexture(texture);
      node->setOwnsTexture(true);
      node->setRect(boundingRect());
    }
  }
  return node;
}

}  // namespace ministream
