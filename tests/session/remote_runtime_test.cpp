#include "app/remote/remote_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <chrono>
#include <thread>
#include <vector>

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

TEST_CASE("remote discovery starts asynchronously and reports an empty completion") {
  auto provider = [] {
    return std::vector<DiscoveryInterface>{{"en0", {192, 168, 1, 20},
                                            {255, 255, 255, 0}, true, false}};
  };
  auto backend = std::make_unique<FakeRemoteBackend>(ready_capabilities());
  RemoteRuntime runtime(std::move(backend), {}, provider);
  REQUIRE(runtime.start());

  const auto begin = SteadyClock::now();
  REQUIRE(runtime.begin_discovery(std::chrono::milliseconds{1}));
  REQUIRE(runtime.discovery_state() == DiscoveryState::Searching);
  REQUIRE(SteadyClock::now() - begin < std::chrono::milliseconds{100});

  runtime.tick();
  std::this_thread::sleep_for(std::chrono::milliseconds{3});
  runtime.tick();
  REQUIRE(runtime.discovery_state() == DiscoveryState::Complete);
  REQUIRE(runtime.hosts().empty());
  REQUIRE_FALSE(runtime.last_discovery_error().has_value());
}

TEST_CASE("remote discovery preserves a typed interface failure") {
  auto provider = [] { return std::vector<DiscoveryInterface>{}; };
  auto backend = std::make_unique<FakeRemoteBackend>(ready_capabilities());
  RemoteRuntime runtime(std::move(backend), {}, provider);
  REQUIRE(runtime.start());

  REQUIRE_FALSE(runtime.begin_discovery(std::chrono::milliseconds{100}));
  REQUIRE(runtime.discovery_state() == DiscoveryState::Failed);
  REQUIRE(runtime.last_discovery_error() == DiscoveryError::NoUsableInterface);
  REQUIRE(runtime.hosts().empty());
}
