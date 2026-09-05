#include "app/ui/video_surface_item.hpp"
#include "app/ui/native_video_texture.hpp"
#include "app/ui/windows_video_surface_bridge.hpp"

#include <QSGRendererInterface>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTexture>
#include <QtQuick/qsgtexture_platform.h>
#include <dxgi.h>

namespace ministream {
using Microsoft::WRL::ComPtr;

struct VideoSurfaceItem::Impl {
  ComPtr<ID3D11Texture2D> current;
  std::uint32_t width{};
  std::uint32_t height{};
};

namespace {
struct D3DVideoNode final : QSGSimpleTextureNode {
  ComPtr<ID3D11Texture2D> native_texture;
  std::unique_ptr<QSGTexture> texture_owner;
  D3DVideoNode() { setFiltering(QSGTexture::Linear); }
  ~D3DVideoNode() override { texture_owner.reset(); }

  bool present(QQuickWindow* window, ID3D11Texture2D* source, QSize size) {
    const auto* renderer = window->rendererInterface();
    if (renderer->graphicsApi() != QSGRendererInterface::Direct3D11) return false;
    auto* device = static_cast<ID3D11Device*>(renderer->getResource(
        window, QSGRendererInterface::DeviceResource));
    if (!device) return false;
    // MF and Qt own different D3D11 devices. Import the shared RGB texture on
    // Qt's device, then copy while holding the producer's keyed mutex.
    ComPtr<IDXGIResource> resource;
    HANDLE handle{};
    ComPtr<ID3D11Texture2D> shared;
    ComPtr<IDXGIKeyedMutex> gate;
    if (FAILED(source->QueryInterface(IID_PPV_ARGS(&resource))) ||
        FAILED(resource->GetSharedHandle(&handle)) ||
        FAILED(device->OpenSharedResource(handle, IID_PPV_ARGS(&shared))) ||
        FAILED(shared.As(&gate))) return false;
    D3D11_TEXTURE2D_DESC description{};
    shared->GetDesc(&description);
    description.MiscFlags = 0;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const bool linear = description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    const bool srgb = !linear && hdr_output(window);
    if (srgb) description.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    ComPtr<ID3D11Texture2D> target;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &target))) return false;
    if (gate->AcquireSync(1, 5) != S_OK) return false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    context->CopyResource(target.Get(), shared.Get());
    context->Flush();
    // The published texture is immutable; leave it readable after a scene
    // graph rebuild, which can import the same last frame again.
    if (FAILED(gate->ReleaseSync(1))) return false;
    auto wrapper = NativeVideoTexture::create(window,
        reinterpret_cast<quint64>(target.Get()), size,
        linear ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8,
        srgb ? QRhiTexture::sRGB : QRhiTexture::Flags{});
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
  connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* win) {
    if (!win) return;
    connect(win, &QQuickWindow::beforeRendering, this, [this, win] {
      const bool hdr = hdr_output(win);
      if (hdr_output_.exchange(hdr) != hdr)
        QMetaObject::invokeMethod(this, [this] { emit hdrOutputChanged(); update(); }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
  });
}
VideoSurfaceItem::~VideoSurfaceItem() = default;
QObject* VideoSurfaceItem::bridge() const noexcept { return bridge_.data(); }

void VideoSurfaceItem::setBridge(QObject* bridge) {
  if (bridge_.data() == bridge) return;
  if (bridge_) disconnect(bridge_, nullptr, this, nullptr);
  bridge_ = bridge;
  reset_pending_.store(true, std::memory_order_release);
  frame_available_.store(false, std::memory_order_release);
  if (auto* surface = qobject_cast<WindowsVideoSurfaceBridge*>(bridge_.data())) {
    connect(surface, &WindowsVideoSurfaceBridge::frameAvailableChanged, this,
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
  const auto* surface = qobject_cast<const WindowsVideoSurfaceBridge*>(bridge_.data());
  if (!surface || !surface->frameAvailable()) {
    reset_pending_.store(true, std::memory_order_release);
    frame_available_.store(false, std::memory_order_release);
    emit frameAvailableChanged();
  }
  update();
}

QSGNode* VideoSurfaceItem::updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) {
  auto* node = static_cast<D3DVideoNode*>(old_node);
  if (reset_pending_.exchange(false, std::memory_order_acq_rel)) {
    delete node;
    node = nullptr;
    impl_->current.Reset();
  }
  bool changed = false;
  if (auto* surface = qobject_cast<WindowsVideoSurfaceBridge*>(bridge_.data())) {
    if (const auto frame = surface->take()) {
      impl_->current = frame->texture;
      impl_->width = frame->width;
      impl_->height = frame->height;
      changed = true;
    }
  }
  if (!impl_->current || !window()) return node;
  if (!node) { node = new D3DVideoNode(); changed = true; }
  if (changed && node->present(window(), impl_->current.Get(),
      QSize(static_cast<int>(impl_->width), static_cast<int>(impl_->height)))) {
    const auto ratio = static_cast<double>(impl_->width) / impl_->height;
    const bool resized = aspect_ratio_.exchange(ratio) != ratio;
    if (!frame_available_.exchange(true, std::memory_order_acq_rel) || resized) {
      QMetaObject::invokeMethod(this, [this] { emit frameAvailableChanged(); }, Qt::QueuedConnection);
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
