#include "app/controlled/controlled_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace ministream;

namespace {

class FakeControlledBackend final : public ControlledBackend {
 public:
  explicit FakeControlledBackend(ControlledCapabilities capabilities)
      : capabilities_(std::move(capabilities)) {}

  ControlledCapabilities inspect() const override { return capabilities_; }
  bool start() override {
    ++start_calls;
    return start_result;
  }
  void stop() noexcept override { ++stop_calls; }
  std::optional<EncodedFrame> next_video() override { return std::nullopt; }
  std::optional<PcmBlock> next_audio() override { return std::nullopt; }
  bool inject_input(const DesktopInput&) override { return true; }
  void clear_gamepad() noexcept override { ++clear_gamepad_calls; }

  ControlledCapabilities capabilities_;
  bool start_result{true};
  unsigned start_calls{};
  unsigned stop_calls{};
  unsigned clear_gamepad_calls{};
};

ControlledCapabilities ready_capabilities() {
  return {{true, "video"}, {true, "audio"}, {true, "input"}, {true, "network"},
          {false, "optional"}};
}

DiscoveryAdvertisement advertisement() {
  return {DiscoverySystem::Windows,
          "Test device",
          0,
          DiscoveryCapabilities{true, true, true, true, true, false},
          1920,
          1080,
          60,
          true};
}

}  // namespace

TEST_CASE("controlled capabilities do not require an optional gamepad") {
  const auto capabilities = ready_capabilities();
  REQUIRE(capabilities.ready());
  REQUIRE_FALSE(capabilities.optional_gamepad.ready);
}

TEST_CASE("controlled runtime gates start on required capabilities") {
  auto capabilities = ready_capabilities();
  capabilities.video.ready = false;
  auto backend = std::make_unique<FakeControlledBackend>(capabilities);
  auto* backend_ptr = backend.get();
  ControlledRuntime runtime(std::move(backend), advertisement());

  REQUIRE_FALSE(runtime.start());
  REQUIRE(backend_ptr->start_calls == 0);
  REQUIRE_FALSE(runtime.hosting());
}

TEST_CASE("controlled runtime stops a started backend and withdraws its advertisement") {
  auto backend = std::make_unique<FakeControlledBackend>(ready_capabilities());
  auto* backend_ptr = backend.get();
  ControlledRuntime runtime(std::move(backend), advertisement());

  REQUIRE(runtime.start());
  REQUIRE(runtime.hosting());
  REQUIRE(runtime.advertisement().controllable);
  runtime.stop();
  REQUIRE_FALSE(runtime.hosting());
  REQUIRE_FALSE(runtime.advertisement().controllable);
  REQUIRE(backend_ptr->stop_calls == 1);
}

TEST_CASE("controlled runtime refreshes its advertisement before starting") {
  auto backend = std::make_unique<FakeControlledBackend>(ready_capabilities());
  ControlledRuntime runtime(std::move(backend), advertisement());

  auto refreshed = advertisement();
  refreshed.device_name = "Refreshed device";
  refreshed.capabilities.hevc = false;
  REQUIRE(runtime.set_advertisement(refreshed));
  REQUIRE(runtime.advertisement().device_name == "Refreshed device");
  REQUIRE_FALSE(runtime.advertisement().controllable);

  REQUIRE(runtime.start());
  REQUIRE_FALSE(runtime.set_advertisement(advertisement()));
  runtime.stop();
}
