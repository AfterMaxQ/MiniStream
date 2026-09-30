#include "windows/video/dxgi_capture.hpp"
#include "windows/video/dxgi_cursor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <array>
#include <cstring>

using namespace ministream;

TEST_CASE("DXGI cursor composition handles color masks clipping and visibility", "[cursor]") {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
      &device, nullptr, &context)));
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = 4; desc.Height = 4; desc.MipLevels = 1; desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  std::array<std::uint32_t, 16> background;
  background.fill(0xff204060U);
  D3D11_SUBRESOURCE_DATA data{background.data(), 16, 0};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> desktop;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, &data, &desktop)));
  DxgiCursorCompositor cursor;
  DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR, 2, 1, 8, {1, 0}};
  std::array<std::byte, 8> pixels{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
                                 std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
  DXGI_OUTDUPL_POINTER_POSITION position{{1, 1}, TRUE};
  std::uint32_t expected = 0xffff0000U;
  unsigned sample_x = 1;
  SECTION("color cursor uses DXGI top-left without subtracting the hotspot") {}
  SECTION("left edge clips the first cursor pixel") { position.Position.x = -1; sample_x = 0; expected = 0xff00ff00U; }
  SECTION("a hidden cursor leaves the desktop untouched") { position.Visible = FALSE; expected = background[0]; }
  SECTION("masked color XOR preserves the desktop alpha") {
    shape.Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR;
    pixels.fill(std::byte{255}); expected = 0xffdfbf9fU;
  }
  SECTION("monochrome AND XOR supports an inverted caret") {
    shape = {DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME, 2, 2, 1, {0, 0}};
    pixels[0] = std::byte{0xc0}; pixels[1] = std::byte{0x80}; expected = 0xffdfbf9fU;
  }
  REQUIRE(cursor.update_shape(shape, pixels));
  REQUIRE(cursor.compose(device.Get(), context.Get(), desktop.Get(), position));
  auto read_desc = desc;
  read_desc.Usage = D3D11_USAGE_STAGING; read_desc.BindFlags = 0;
  read_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&read_desc, nullptr, &readback)));
  context->CopyResource(readback.Get(), desktop.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  REQUIRE(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::uint32_t actual{};
  std::memcpy(&actual, static_cast<const std::byte*>(mapped.pData) + mapped.RowPitch + sample_x * 4, 4);
  context->Unmap(readback.Get(), 0);
  REQUIRE(actual == expected);
}

TEST_CASE("DXGI capture initializes the primary Windows output", "[.hardware]") {
  DxgiCapture capture;
  REQUIRE(capture.initialize());
  const auto info = capture.capture_info();
  INFO(describe_dxgi_capture(info));
  const auto frame = capture.acquire(Microseconds{1'000'000});
  REQUIRE(frame);
  D3D11_TEXTURE2D_DESC frame_description{};
  frame->texture->GetDesc(&frame_description);
  std::clog << "DXGI acquired frame: format=" << frame->format << ", size=" << frame->width
            << "x" << frame->height << ", bind_flags=0x" << std::hex
            << frame_description.BindFlags << ", misc_flags=0x" << frame_description.MiscFlags
            << std::dec << '\n';
  const auto converted = capture.resize(*frame, frame->width, frame->height);
  REQUIRE(converted);
  REQUIRE(converted->format == DXGI_FORMAT_B8G8R8A8_UNORM);
}

TEST_CASE("DXGI cursor composition preserves the FP16 HDR desktop", "[cursor]") {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
      0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = 2; desc.Height = 1; desc.MipLevels = 1; desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  std::array<std::uint16_t, 8> background{0x4400, 0x3800, 0x3400, 0x3c00,
                                        0x4400, 0x3800, 0x3400, 0x3c00};
  D3D11_SUBRESOURCE_DATA data{background.data(), 16, 0};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> desktop;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, &data, &desktop)));
  DxgiCursorCompositor cursor;
  DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR, 1, 1, 4, {0, 0}};
  std::array<std::byte, 4> white{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
  REQUIRE(cursor.update_shape(shape, white));
  REQUIRE(cursor.compose(device.Get(), context.Get(), desktop.Get(), {{0, 0}, TRUE}));
  desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback)));
  context->CopyResource(readback.Get(), desktop.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  REQUIRE(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::array<std::uint16_t, 8> pixels{};
  std::memcpy(pixels.data(), mapped.pData, sizeof(pixels));
  context->Unmap(readback.Get(), 0);
  REQUIRE(pixels[0] == 0x3c00);
  REQUIRE(pixels[1] == 0x3c00);
  REQUIRE(pixels[2] == 0x3c00);
  REQUIRE(pixels[4] == 0x4400);
  REQUIRE(pixels[5] == 0x3800);
  REQUIRE(pixels[6] == 0x3400);
}

TEST_CASE("DXGI cursor rejects truncated and malformed native shapes", "[cursor]") {
  DxgiCursorCompositor cursor;
  std::array<std::byte, 4> data{};
  REQUIRE_FALSE(cursor.update_shape({DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR, 2, 1, 8, {}}, data));
  REQUIRE_FALSE(cursor.update_shape({DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR, 2, 1, 4, {}}, data));
  REQUIRE_FALSE(cursor.update_shape({DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME, 2, 3, 1, {}}, data));
  REQUIRE_FALSE(cursor.update_shape({DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR, 0, 1, 0, {}}, data));
}

TEST_CASE("SDR output with a non-BGRA surface is not diagnosed as HDR") {
  DxgiCaptureInfo info{};
  info.format = DXGI_FORMAT_R10G10B10A2_UNORM;
  info.has_color_space = true;
  info.color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

  REQUIRE(classify_dxgi_capture(info) == DxgiCaptureStatus::UnsupportedFormat);
}

TEST_CASE("Windows HDR status comes from the selected output color space") {
  DxgiCaptureInfo info{};
  info.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  info.has_color_space = true;
  info.color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

  REQUIRE(classify_dxgi_capture(info) == DxgiCaptureStatus::HdrActive);
}

TEST_CASE("SDR FP16 desktop capture is converted before encoding") {
  DxgiCaptureInfo info{};
  info.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  info.has_color_space = true;
  info.color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

  REQUIRE(classify_dxgi_capture(info) == DxgiCaptureStatus::NeedsConversion);
}

TEST_CASE("DXGI diagnostics identify the selected output and capture surface") {
  DxgiCaptureInfo info{};
  info.adapter_name = "NVIDIA Test Adapter";
  info.output_name = R"(\\.\DISPLAY2)";
  info.monitor = 0x1234U;
  info.format = DXGI_FORMAT_R10G10B10A2_UNORM;
  info.has_color_space = true;
  info.color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  info.bits_per_color = 10;
  info.width = 3840;
  info.height = 2160;

  const auto detail = describe_dxgi_capture(info);
  REQUIRE(detail.find("adapter=NVIDIA Test Adapter") != std::string::npos);
  REQUIRE(detail.find(R"(output=\\.\DISPLAY2)") != std::string::npos);
  REQUIRE(detail.find("monitor=0x1234") != std::string::npos);
  REQUIRE(detail.find("format=DXGI_FORMAT_R10G10B10A2_UNORM") != std::string::npos);
  REQUIRE(detail.find("color_space=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709") !=
          std::string::npos);
  REQUIRE(detail.find("bits_per_color=10") != std::string::npos);
  REQUIRE(detail.find("size=3840x2160") != std::string::npos);
}
