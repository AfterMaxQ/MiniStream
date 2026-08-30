#include "app/ui/video_surface_item.hpp"

#include "macos/video/video_surface_bridge.hpp"

#import <CoreImage/CoreImage.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>

#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTexture>
#include <QtQuick/qsgtexture_platform.h>

#include <algorithm>
#include <cstddef>

namespace ministream {

struct VideoSurfaceItem::Impl {
  CVPixelBufferRef current{};
  id<MTLDevice> device{};
  id<MTLCommandQueue> queue{};
  CIContext* context{};
  CVMetalTextureCacheRef texture_cache{};

  ~Impl() {
    if (texture_cache) {
      CFRelease(texture_cache);
      texture_cache = nullptr;
    }
    if (current) {
      CVPixelBufferRelease(current);
      current = nullptr;
    }
  }
};

namespace {

QSGTexture* make_texture(QQuickWindow* window, VideoSurfaceItem::Impl& impl,
                         CVPixelBufferRef pixel_buffer) {
  if (!window || !pixel_buffer) {
    return nullptr;
  }
  if (!impl.device) {
    impl.device = MTLCreateSystemDefaultDevice();
    if (!impl.device) {
      return nullptr;
    }
    impl.queue = [impl.device newCommandQueue];
    impl.context = [CIContext contextWithMTLDevice:impl.device options:nil];
    if (!impl.queue || !impl.context ||
        CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, impl.device, nullptr,
                                  &impl.texture_cache) != kCVReturnSuccess) {
      return nullptr;
    }
  }

  // Import the decoded planes without copying them to CPU memory. Core Image
  // applies the CVPixelBuffer's matrix/range attachments when it builds the
  // RGB image, while the cache keeps NV12/P010 compatible with Metal-backed
  // decoders.
  CVMetalTextureRef luma{};
  CVMetalTextureRef chroma{};
  const auto plane_count = CVPixelBufferGetPlaneCount(pixel_buffer);
  if (plane_count >= 2) {
    CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, impl.texture_cache, pixel_buffer, nullptr,
        MTLPixelFormatR8Unorm, CVPixelBufferGetWidthOfPlane(pixel_buffer, 0),
        CVPixelBufferGetHeightOfPlane(pixel_buffer, 0), 0, &luma);
    CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, impl.texture_cache, pixel_buffer, nullptr,
        MTLPixelFormatRG8Unorm, CVPixelBufferGetWidthOfPlane(pixel_buffer, 1),
        CVPixelBufferGetHeightOfPlane(pixel_buffer, 1), 1, &chroma);
  }

  const auto width = CVPixelBufferGetWidth(pixel_buffer);
  const auto height = CVPixelBufferGetHeight(pixel_buffer);
  if (width == 0 || height == 0) {
    if (luma) CFRelease(luma);
    if (chroma) CFRelease(chroma);
    return nullptr;
  }

  MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                   width:width
                                  height:height
                               mipmapped:NO];
  descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  id<MTLTexture> target = [impl.device newTextureWithDescriptor:descriptor];
  CIImage* image = [CIImage imageWithCVPixelBuffer:pixel_buffer options:nil];
  id<MTLCommandBuffer> command_buffer = [impl.queue commandBuffer];
  if (!target || !image || !command_buffer) {
    if (luma) CFRelease(luma);
    if (chroma) CFRelease(chroma);
    return nullptr;
  }

  CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  [impl.context render:image
          toMTLTexture:target
         commandBuffer:command_buffer
                bounds:CGRectMake(0, 0, width, height)
            colorSpace:color_space];
  if (color_space) {
    CGColorSpaceRelease(color_space);
  }
  [command_buffer commit];
  [command_buffer waitUntilCompleted];

  if (luma) CFRelease(luma);
  if (chroma) CFRelease(chroma);
  return QNativeInterface::QSGMetalTexture::fromNative(
      target, window, QSize(static_cast<int>(width), static_cast<int>(height)),
      QQuickWindow::TextureHasAlphaChannel);
}

}  // namespace

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
  if (auto* surface = qobject_cast<VideoSurfaceBridge*>(bridge_.data())) {
    connect(surface, &VideoSurfaceBridge::frameAvailableChanged, this,
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
  const auto* surface = qobject_cast<const VideoSurfaceBridge*>(bridge_.data());
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

  if (auto* surface = qobject_cast<VideoSurfaceBridge*>(bridge_.data())) {
    if (const auto frame = surface->take(); frame) {
      if (impl_->current) {
        CVPixelBufferRelease(impl_->current);
      }
      impl_->current = frame->pixel_buffer;
      frame_available_.store(true, std::memory_order_release);
    }
  }

  if (impl_->current) {
    if (auto* texture = make_texture(window(), *impl_, impl_->current)) {
      node->setTexture(texture);
      node->setOwnsTexture(true);
      node->setRect(boundingRect());
    }
  }
  return node;
}

}  // namespace ministream
