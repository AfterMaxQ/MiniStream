#include "app/ui/video_surface_item.hpp"
#include "macos/video/video_surface_bridge.hpp"

#import <CoreImage/CoreImage.h>
#import <Metal/Metal.h>

#include <QSGRendererInterface>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTexture>
#include <QtQuick/qsgtexture_platform.h>

namespace ministream {

struct VideoSurfaceItem::Impl {
  CVPixelBufferRef current{};
  ~Impl() { if (current) CVPixelBufferRelease(current); }
};

namespace {

// Both wrappers and native textures belong to the render-thread node. Qt's
// fromNative() does not retain/own the underlying Metal texture.
struct MetalVideoNode final : QSGSimpleTextureNode {
  id<MTLDevice> device{};
  id<MTLCommandQueue> queue{};
  CIContext* context{};
  id<MTLTexture> native_texture{};
  std::unique_ptr<QSGTexture> texture_owner;

  MetalVideoNode() { setFiltering(QSGTexture::Linear); }
  ~MetalVideoNode() override { texture_owner.reset(); }

  bool present(QQuickWindow* window, CVPixelBufferRef buffer) {
    const auto* renderer = window->rendererInterface();
    if (renderer->graphicsApi() != QSGRendererInterface::Metal) return false;
    if (!device) {
      device = (__bridge id<MTLDevice>)renderer->getResource(
          window, QSGRendererInterface::DeviceResource);
      if (!device) return false;
      queue = [device newCommandQueue];
      context = [CIContext contextWithMTLDevice:device options:nil];
    }
    if (!queue || !context) return false;
    const auto width = CVPixelBufferGetWidth(buffer);
    const auto height = CVPixelBufferGetHeight(buffer);
    if (width == 0 || height == 0) return false;
    auto* descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:width height:height mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> target = [device newTextureWithDescriptor:descriptor];
    CIImage* image = [CIImage imageWithCVPixelBuffer:buffer options:nil];
    id<MTLCommandBuffer> commands = [queue commandBuffer];
    if (!target || !image || !commands) return false;
    CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    [context render:image toMTLTexture:target commandBuffer:commands
             bounds:CGRectMake(0, 0, width, height) colorSpace:colors];
    if (colors) CGColorSpaceRelease(colors);
    [commands commit];
    [commands waitUntilCompleted];
    if (commands.status == MTLCommandBufferStatusError) return false;
    std::unique_ptr<QSGTexture> wrapper(QNativeInterface::QSGMetalTexture::fromNative(
        target, window, QSize(static_cast<int>(width), static_cast<int>(height))));
    if (!wrapper) return false;
    setTexture(wrapper.get());
    texture_owner = std::move(wrapper);
    native_texture = target;
    return true;
  }
};

}  // namespace

VideoSurfaceItem::VideoSurfaceItem(QQuickItem* parent)
    : QQuickItem(parent), impl_(std::make_unique<Impl>()) {
  setFlag(ItemHasContents, true);
}
VideoSurfaceItem::~VideoSurfaceItem() = default;
QObject* VideoSurfaceItem::bridge() const noexcept { return bridge_.data(); }

void VideoSurfaceItem::setBridge(QObject* bridge) {
  if (bridge_.data() == bridge) return;
  if (bridge_) disconnect(bridge_, nullptr, this, nullptr);
  bridge_ = bridge;
  reset_pending_.store(true, std::memory_order_release);
  frame_available_.store(false, std::memory_order_release);
  if (auto* surface = qobject_cast<VideoSurfaceBridge*>(bridge_.data())) {
    connect(surface, &VideoSurfaceBridge::frameAvailableChanged, this,
            &VideoSurfaceItem::onBridgeFrameAvailable, Qt::QueuedConnection);
  }
  emit bridgeChanged();
  emit frameAvailableChanged();
  update();
}

bool VideoSurfaceItem::frameAvailable() const noexcept {
  return frame_available_.load(std::memory_order_acquire);
}

void VideoSurfaceItem::onBridgeFrameAvailable() {
  const auto* surface = qobject_cast<const VideoSurfaceBridge*>(bridge_.data());
  if (!surface || !surface->frameAvailable()) {
    reset_pending_.store(true, std::memory_order_release);
    frame_available_.store(false, std::memory_order_release);
    emit frameAvailableChanged();
  }
  update();
}

QSGNode* VideoSurfaceItem::updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) {
  auto* node = static_cast<MetalVideoNode*>(old_node);
  if (reset_pending_.exchange(false, std::memory_order_acq_rel)) {
    delete node;
    node = nullptr;
    if (impl_->current) CVPixelBufferRelease(impl_->current);
    impl_->current = nullptr;
  }
  bool changed = false;
  if (auto* surface = qobject_cast<VideoSurfaceBridge*>(bridge_.data())) {
    if (const auto frame = surface->take()) {
      if (impl_->current) CVPixelBufferRelease(impl_->current);
      impl_->current = frame->pixel_buffer;
      changed = true;
    }
  }
  if (!impl_->current || !window()) return node;
  if (!node) { node = new MetalVideoNode(); changed = true; }
  if (changed && node->present(window(), impl_->current)) {
    if (!frame_available_.exchange(true, std::memory_order_acq_rel)) {
      QMetaObject::invokeMethod(this, [this] { emit frameAvailableChanged(); },
                                Qt::QueuedConnection);
    }
  }
  if (!node->texture_owner) { delete node; return nullptr; }
  const QSizeF fitted = QSizeF(node->texture_owner->textureSize()).scaled(
      boundingRect().size(), Qt::KeepAspectRatio);
  node->setRect(QRectF((width() - fitted.width()) / 2,
                       (height() - fitted.height()) / 2, fitted.width(), fitted.height()));
  return node;
}

}  // namespace ministream
