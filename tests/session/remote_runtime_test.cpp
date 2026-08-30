#include "app/remote/remote_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace ministream;

namespace {

class FakeRemoteBackend final : public RemoteBackend {
 public:
  explicit FakeRemoteBackend(RemoteCapabilities capabilities)
      : capabilities_(std::move(capabilities)) {}

  RemoteCapabilities inspect() const override { return capabilities_; }
  bool start() override {
    ++start_calls;
    return start_result;
  }
  void stop() noexcept override { ++stop_calls; }
  bool configure_video(const CodecConfig&) override { return true; }
  bool decode_video(std::span<const std::byte>, std::uint64_t) override { return true; }
  bool play_audio(std::span<const float>) override { return true; }
  void play_rumble(std::uint16_t, std::uint16_t, std::uint32_t) override {}

  RemoteCapabilities capabilities_;
  bool start_result{true};
  unsigned start_calls{};
  unsigned stop_calls{};
};

RemoteCapabilities ready_capabilities() {
  return {{true, "video"}, {true, "audio"}, {true, "input"}, {true, "network"}};
}

}  // namespace

TEST_CASE("remote runtime requires a usable native backend") {
  auto capabilities = ready_capabilities();
  capabilities.video.ready = false;
  auto backend = std::make_unique<FakeRemoteBackend>(capabilities);
  auto* backend_ptr = backend.get();
  RemoteRuntime runtime(std::move(backend));

  REQUIRE_FALSE(runtime.start());
  REQUIRE(backend_ptr->start_calls == 0);
  REQUIRE(runtime.state() == RoleState::Idle);
}

TEST_CASE("remote runtime starts browsing and stops its backend cleanly") {
  auto backend = std::make_unique<FakeRemoteBackend>(ready_capabilities());
  auto* backend_ptr = backend.get();
  RemoteRuntime runtime(std::move(backend));

  REQUIRE(runtime.start());
  REQUIRE(runtime.state() == RoleState::RemoteBrowsing);
  REQUIRE_FALSE(runtime.connected());
  runtime.stop();
  REQUIRE(runtime.state() == RoleState::Idle);
  REQUIRE(backend_ptr->start_calls == 1);
  REQUIRE(backend_ptr->stop_calls == 1);
}
