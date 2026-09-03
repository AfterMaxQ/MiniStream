#include "windows/video/dxgi_capture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iostream>

using namespace ministream;

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
