#include "windows/video/dxgi_cursor.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace ministream {
namespace {
constexpr char kShader[] = R"(
struct VertexOutput { float4 position : SV_POSITION; };
VertexOutput vertex_main(uint id : SV_VertexID) {
  float2 uv = float2((id << 1) & 2, id & 2);
  VertexOutput output;
  output.position = float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
  return output;
}
Texture2D<float4> background : register(t0);
Texture2D<uint4> pointer_shape : register(t1);
cbuffer Options : register(b0) {
  int2 pointer_origin; int2 tile_origin;
  uint pointer_type; uint linear_source; uint2 padding;
};
float3 to_linear(float3 c) {
  return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}
float3 to_srgb(float3 c) {
  c = saturate(c);
  return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
}
float4 pixel_main(VertexOutput input) : SV_TARGET {
  int2 pixel = int2(input.position.xy);
  float4 source = background.Load(int3(pixel - tile_origin, 0));
  uint4 pointer = pointer_shape.Load(int3(pixel - pointer_origin, 0));
  if (pointer_type == 2) {
    float3 color = float3(pointer.rgb) / 255.0;
    if (linear_source != 0) color = to_linear(color);
    return float4(lerp(source.rgb, color, float(pointer.a) / 255.0), source.a);
  }
  float3 encoded = linear_source != 0 ? to_srgb(source.rgb) : source.rgb;
  uint3 screen = uint3(round(saturate(encoded) * 255.0));
  uint3 result = pointer_type == 1 ? (screen & uint3(pointer.a, pointer.a, pointer.a)) ^ pointer.rgb
      : pointer.a == 255 ? screen ^ pointer.rgb : pointer.rgb;
  float3 color = float3(result) / 255.0;
  if (linear_source != 0) {
    // Transparent/inverted mask pixels preserve the HDR desktop outside the cursor.
    color = to_linear(color);
    if (pointer_type == 1 && pointer.a == 255 && all(pointer.rgb == 0)) color = source.rgb;
    if (pointer_type == 4 && pointer.a == 255 && all(pointer.rgb == 0)) color = source.rgb;
  }
  return float4(color, source.a);
}
)";
}

struct DxgiCursorCompositor::Impl {
  UINT width{}, height{}, type{};
  std::vector<std::uint32_t> pixels;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> shape, background;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shape_view, background_view;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
  Microsoft::WRL::ComPtr<ID3D11Buffer> options;
};
DxgiCursorCompositor::DxgiCursorCompositor() : impl_(std::make_unique<Impl>()) {}
DxgiCursorCompositor::~DxgiCursorCompositor() = default;

bool DxgiCursorCompositor::update_shape(const DXGI_OUTDUPL_POINTER_SHAPE_INFO& info,
                                      std::span<const std::byte> bytes) {
  const bool mono = info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
  const auto height = mono ? info.Height / 2 : info.Height;
  const auto minimum_pitch = mono ? (info.Width + 7) / 8 : static_cast<std::uint64_t>(info.Width) * 4;
  if ((!mono && info.Type != DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR &&
                info.Type != DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) ||
      info.Width == 0 || height == 0 || info.Width > 1024 || height > 1024 ||
      (mono && (info.Height % 2 != 0)) || info.Pitch < minimum_pitch ||
      static_cast<std::uint64_t>(info.Pitch) * info.Height > bytes.size()) return false;
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(info.Width) * height);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < info.Width; ++x) {
      if (mono) {
        const auto offset = static_cast<std::size_t>(y) * info.Pitch + x / 8;
        const auto bit = 0x80U >> (x % 8);
        const bool and_bit = (std::to_integer<unsigned>(bytes[offset]) & bit) != 0;
        const bool xor_bit = (std::to_integer<unsigned>(bytes[offset + height * info.Pitch]) & bit) != 0;
        pixels[y * info.Width + x] = (and_bit ? 0xff000000U : 0U) | (xor_bit ? 0x00ffffffU : 0U);
      } else {
        const auto offset = static_cast<std::size_t>(y) * info.Pitch + x * 4;
        // DXGI is BGRA; the integer shader texture is RGBA.
        pixels[y * info.Width + x] = std::to_integer<std::uint32_t>(bytes[offset + 2]) |
            (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8) |
            (std::to_integer<std::uint32_t>(bytes[offset]) << 16) |
            (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24);
      }
    }
  }
  impl_->width = info.Width; impl_->height = height; impl_->type = info.Type;
  impl_->pixels = std::move(pixels);
  impl_->shape.Reset(); impl_->shape_view.Reset();
  impl_->background.Reset(); impl_->background_view.Reset();
  return true;
}

bool DxgiCursorCompositor::compose(ID3D11Device* device, ID3D11DeviceContext* context,
                                 ID3D11Texture2D* desktop,
                                 const DXGI_OUTDUPL_POINTER_POSITION& position) {
  if (!device || !context || !desktop) return false;
  if (!position.Visible || impl_->pixels.empty()) return true;
  D3D11_TEXTURE2D_DESC desc{};
  desktop->GetDesc(&desc);
  const auto x = static_cast<std::int64_t>(position.Position.x);
  const auto y = static_cast<std::int64_t>(position.Position.y);
  const auto left = std::max<std::int64_t>(0, x), top = std::max<std::int64_t>(0, y);
  const auto right = std::min<std::int64_t>(desc.Width, x + impl_->width);
  const auto bottom = std::min<std::int64_t>(desc.Height, y + impl_->height);
  if (right <= left || bottom <= top) return true;
  if (!impl_->vertex || !impl_->pixel || !impl_->options) {
    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, errors;
    if (FAILED(D3DCompile(kShader, sizeof(kShader), nullptr, nullptr, nullptr,
                         "vertex_main", "vs_5_0", 0, 0, &vs, &errors)) ||
        FAILED(D3DCompile(kShader, sizeof(kShader), nullptr, nullptr, nullptr,
                         "pixel_main", "ps_5_0", 0, 0, &ps, &errors))) {
      if (errors) std::clog << "DXGI cursor shader: " << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
      return false;
    }
    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &impl_->vertex)) ||
        FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &impl_->pixel))) return false;
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = 32; buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&buffer, nullptr, &impl_->options))) return false;
  }
  if (!impl_->shape || !impl_->shape_view) {
    D3D11_TEXTURE2D_DESC shape{};
    shape.Width = impl_->width; shape.Height = impl_->height;
    shape.MipLevels = 1; shape.ArraySize = 1; shape.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    shape.SampleDesc.Count = 1; shape.Usage = D3D11_USAGE_IMMUTABLE; shape.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{impl_->pixels.data(), impl_->width * 4, 0};
    if (FAILED(device->CreateTexture2D(&shape, &data, &impl_->shape)) ||
        FAILED(device->CreateShaderResourceView(impl_->shape.Get(), nullptr, &impl_->shape_view))) return false;
  }
  D3D11_TEXTURE2D_DESC tile_desc{};
  if (impl_->background) impl_->background->GetDesc(&tile_desc);
  if (!impl_->background || !impl_->background_view || tile_desc.Format != desc.Format) {
    impl_->background.Reset(); impl_->background_view.Reset();
    tile_desc = desc; tile_desc.Width = impl_->width; tile_desc.Height = impl_->height;
    tile_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&tile_desc, nullptr, &impl_->background)) ||
        FAILED(device->CreateShaderResourceView(impl_->background.Get(), nullptr, &impl_->background_view))) return false;
  }
  // Read only the cursor-sized background tile on the GPU; no desktop readback.
  const D3D11_BOX box{static_cast<UINT>(left), static_cast<UINT>(top), 0,
                      static_cast<UINT>(right), static_cast<UINT>(bottom), 1};
  context->CopySubresourceRegion(impl_->background.Get(), 0, 0, 0, 0, desktop, 0, &box);
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(device->CreateRenderTargetView(desktop, nullptr, &target))) return false;
  const std::array<std::int32_t, 8> options{position.Position.x, position.Position.y,
      static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
      static_cast<std::int32_t>(impl_->type), desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1 : 0, 0, 0};
  context->UpdateSubresource(impl_->options.Get(), 0, nullptr, options.data(), 0, 0);
  auto* constant = impl_->options.Get(); context->PSSetConstantBuffers(0, 1, &constant);
  auto* render_target = target.Get(); context->OMSetRenderTargets(1, &render_target, nullptr);
  context->OMSetBlendState(nullptr, nullptr, 0xffffffffU);
  context->RSSetState(nullptr);
  const D3D11_VIEWPORT viewport{static_cast<float>(left), static_cast<float>(top),
      static_cast<float>(right - left), static_cast<float>(bottom - top), 0, 1};
  context->RSSetViewports(1, &viewport);
  context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(impl_->vertex.Get(), nullptr, 0); context->PSSetShader(impl_->pixel.Get(), nullptr, 0);
  ID3D11ShaderResourceView* resources[]{impl_->background_view.Get(), impl_->shape_view.Get()};
  context->PSSetShaderResources(0, 2, resources);
  context->Draw(3, 0);
  ID3D11ShaderResourceView* empty[2]{}; context->PSSetShaderResources(0, 2, empty);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  return true;
}
}  // namespace ministream
