#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstddef>
#include <memory>
#include <span>

namespace ministream {

class DxgiCursorCompositor {
 public:
  DxgiCursorCompositor();
  ~DxgiCursorCompositor();
  bool update_shape(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info,
                    std::span<const std::byte> bytes);
  bool compose(ID3D11Device* device, ID3D11DeviceContext* context,
               ID3D11Texture2D* desktop, const DXGI_OUTDUPL_POINTER_POSITION& position);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
