#pragma once

#include <QQuickWindow>
#include <QSGTexture>
#include <rhi/qrhi.h>
#include <memory>

namespace ministream {

inline bool hdr_output(QQuickWindow* window) {
  const auto* chain = window ? window->swapChain() : nullptr;
  return chain && chain->format() == QRhiSwapChain::HDRExtendedSrgbLinear;
}

// Import with the actual format. The convenience native wrappers assume RGBA8.
class NativeVideoTexture final : public QSGTexture {
 public:
  static std::unique_ptr<QSGTexture> create(QQuickWindow* window, quint64 handle,
      QSize size, QRhiTexture::Format format, QRhiTexture::Flags flags = {}) {
    auto* rhi = window->rhi();
    if (!rhi) return {};
    auto result = std::make_unique<NativeVideoTexture>();
    result->texture_.reset(rhi->newTexture(format, size, 1, flags));
    if (!result->texture_->createFrom({handle, 0})) return {};
    return result;
  }
  qint64 comparisonKey() const override { return reinterpret_cast<qint64>(texture_.get()); }
  QSize textureSize() const override { return texture_->pixelSize(); }
  bool hasAlphaChannel() const override { return false; }
  bool hasMipmaps() const override { return false; }
  QRhiTexture* rhiTexture() const override { return texture_.get(); }
 private:
  std::unique_ptr<QRhiTexture> texture_;
};

}  // namespace ministream
