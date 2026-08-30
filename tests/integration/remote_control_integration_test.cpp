#include "app/controlled/controlled_runtime.hpp"
#include "app/remote/remote_runtime.hpp"
#include "core/net/udp_endpoint.hpp"
#include "core/video/codec_config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

using namespace ministream;
using namespace std::chrono_literals;

namespace {

ControlledCapabilities controlled_capabilities() {
  return {{true, "test H.264 capture"},
          {true, "test system audio"},
          {true, "test input"},
          {true, "test UDP"},
          {false, "optional"},
          true,
          false,
          false,
          1920,
          1080,
          60};
}

RemoteCapabilities remote_capabilities() {
  return {{true, "test H.264 decoder"},
          {true, "test audio output"},
          {true, "test input"},
          {true, "test UDP"},
          true,
          false,
          false,
          1920,
          1080,
          60};
}

DiscoveryAdvertisement advertisement() {
  return {DiscoverySystem::Windows,
          "loopback-controlled",
          0,
          DiscoveryCapabilities{true, false, false, true, true, false},
          1920,
          1080,
          60,
          true};
}

class LoopbackControlledBackend final : public ControlledBackend {
 public:
  ControlledCapabilities inspect() const override { return controlled_capabilities(); }
  bool start() override {
    started = true;
    return true;
  }
  void stop() noexcept override { started = false; }

  std::optional<EncodedFrame> next_video() override {
    if (!started) {
      return std::nullopt;
    }
    const auto id = video_id++;
    return EncodedFrame{id, 10'000U + id, (id % 30U) == 0U,
                        {std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
                         static_cast<std::byte>(id & 0xFFU)}};
  }

  CodecConfig codec_config() const override {
    return {VideoCodec::H264, 1920, 1080, 60, false, {std::byte{0x67}, std::byte{0x42}}};
  }

  bool configure_video(const CodecConfig& config) override {
    configured = config;
    return config.codec == VideoCodec::H264 && config.width == 1920 &&
           config.height == 1080 && config.fps == 60 && !config.hdr10;
  }

  bool reconfigure_bitrate(std::uint32_t bitrate_bps) override {
    bitrate = bitrate_bps;
    return started && bitrate_bps != 0;
  }

  std::optional<PcmBlock> next_audio() override {
    if (!started) {
      return std::nullopt;
    }
    PcmBlock block;
    block.host_timestamp_us = 20'000U + static_cast<std::uint64_t>(audio_id) * 10'000U;
    block.frames = 480;
    block.interleaved_stereo.assign(kOpusFrameSamplesPerChannel * 2U, 0.0F);
    ++audio_id;
    return block;
  }

  bool inject_input(const DesktopInput& input) override {
    injected_inputs.push_back(input);
    return true;
  }

  void clear_gamepad() noexcept override { ++clear_gamepad_calls; }

  bool started{};
  std::uint32_t video_id{};
  std::uint32_t audio_id{};
  std::uint32_t bitrate{};
  std::optional<CodecConfig> configured;
  std::vector<DesktopInput> injected_inputs;
  unsigned clear_gamepad_calls{};
};

class LoopbackRemoteBackend final : public RemoteBackend {
 public:
  RemoteCapabilities inspect() const override { return remote_capabilities(); }
  bool start() override {
    started = true;
    return true;
  }
  void stop() noexcept override { started = false; }

  bool configure_video(const CodecConfig& config) override {
    ++codec_config_attempts;
    configured = config;
    if (reject_first_codec_config && codec_config_attempts == 1U) {
      return false;
    }
    return started && config.codec == VideoCodec::H264 && config.width == 1920 &&
           config.height == 1080 && config.fps == 60 && !config.hdr10 &&
           !config.parameter_sets.empty();
  }

  bool decode_video(std::span<const std::byte> encoded, std::uint64_t timestamp_us) override {
    decoded_video.emplace_back(encoded.begin(), encoded.end());
    last_video_timestamp = timestamp_us;
    return true;
  }

  bool play_audio(std::span<const float> samples) override {
    played_audio.insert(played_audio.end(), samples.begin(), samples.end());
    return true;
  }

  void play_rumble(std::uint16_t, std::uint16_t, std::uint32_t) override {}

  void clear_rumble() noexcept override { ++rumble_clear_calls; }

  bool started{};
  std::optional<CodecConfig> configured;
  bool reject_first_codec_config{};
  unsigned codec_config_attempts{};
  std::vector<std::vector<std::byte>> decoded_video;
  std::vector<float> played_audio;
  std::uint64_t last_video_timestamp{};
  unsigned rumble_clear_calls{};
};

std::uint16_t unused_udp_port() {
  UdpEndpoint probe;
  REQUIRE(probe.bind(0));
  const auto port = probe.local_port();
  REQUIRE(port != 0);
  return port;
}

void pump(ControlledRuntime& controlled, RemoteRuntime& remote, unsigned rounds = 1) {
  for (unsigned index = 0; index < rounds; ++index) {
    controlled.tick();
    remote.tick();
    std::this_thread::sleep_for(1ms);
  }
}

}  // namespace

TEST_CASE("loopback control session completes handshake pairing and media") {
  const auto discovery_port = unused_udp_port();
  DiscoveryConfig discovery_config;
  discovery_config.port = discovery_port;
  discovery_config.retry_interval = 50ms;
  discovery_config.target_override = {{127, 0, 0, 1}};

  auto controlled_backend = std::make_unique<LoopbackControlledBackend>();
  auto* controlled_backend_ptr = controlled_backend.get();
  ControlledRuntime controlled(std::move(controlled_backend), advertisement(), discovery_config);
  std::vector<StreamSnapshot> controlled_snapshots;
  controlled.set_telemetry_callback(
      [&](const StreamSnapshot& snapshot) { controlled_snapshots.push_back(snapshot); });
  REQUIRE(controlled.start());
  REQUIRE(controlled.advertisement().controllable);

  auto remote_backend = std::make_unique<LoopbackRemoteBackend>();
  auto* remote_backend_ptr = remote_backend.get();
  remote_backend_ptr->reject_first_codec_config = true;
  RemoteRuntime remote(std::move(remote_backend), discovery_config);
  std::vector<StreamSnapshot> remote_snapshots;
  remote.set_telemetry_callback(
      [&](const StreamSnapshot& snapshot) { remote_snapshots.push_back(snapshot); });
  REQUIRE(remote.start());
  REQUIRE(remote.begin_discovery(500ms));
  for (unsigned attempt = 0; attempt < 750U && remote.discovery_state() == DiscoveryState::Searching;
       ++attempt) {
    pump(controlled, remote);
  }
  REQUIRE(remote.discovery_state() == DiscoveryState::Complete);
  REQUIRE(remote.hosts().size() == 1);
  REQUIRE(remote.hosts().front().address == "127.0.0.1");
  REQUIRE(remote.hosts().front().session_port == controlled.advertisement().session_port);

  REQUIRE(remote.connect(0));
  for (unsigned attempt = 0; attempt < 750U &&
                                (!remote.pairing() || !controlled.pairing());
       ++attempt) {
    pump(controlled, remote);
  }
  INFO("controlled state=" << static_cast<int>(controlled.state())
                            << " remote state=" << static_cast<int>(remote.state()));
  REQUIRE(remote.pairing());
  REQUIRE(controlled.pairing());
  REQUIRE_FALSE(remote.pairing_code().empty());
  REQUIRE(remote.pairing_code() == controlled.pairing_code());

  remote.confirm_pairing();
  controlled.confirm_pairing();
  for (unsigned attempt = 0; attempt < 750U &&
                                (!remote.streaming() || !controlled.streaming());
       ++attempt) {
    pump(controlled, remote);
  }
  REQUIRE(remote.streaming());
  REQUIRE(controlled.streaming());
  REQUIRE(remote_backend_ptr->configured.has_value());
  REQUIRE(controlled_backend_ptr->bitrate == 20'000'000);

  remote.toggle_input();
  REQUIRE(remote.remote_input_active());
  const DesktopInput key_down{DesktopInputKind::Key, 0, 0, 0,
                              static_cast<std::uint16_t>(DesktopKey::W)};
  REQUIRE(remote.route_input(key_down));
  for (unsigned attempt = 0; attempt < 100U && controlled_backend_ptr->injected_inputs.empty();
       ++attempt) {
    pump(controlled, remote);
  }
  REQUIRE(controlled_backend_ptr->injected_inputs.size() == 1);
  REQUIRE(controlled_backend_ptr->injected_inputs.front() == key_down);

  for (unsigned attempt = 0; attempt < 800U &&
                                (remote_backend_ptr->decoded_video.empty() ||
                                 remote_backend_ptr->played_audio.empty());
       ++attempt) {
    pump(controlled, remote);
  }
  REQUIRE_FALSE(remote_backend_ptr->decoded_video.empty());
  REQUIRE_FALSE(remote_backend_ptr->played_audio.empty());
  REQUIRE(remote_backend_ptr->codec_config_attempts >= 2U);
  REQUIRE(remote_backend_ptr->last_video_timestamp != 0);

  pump(controlled, remote, 140);
  REQUIRE_FALSE(controlled_snapshots.empty());
  REQUIRE_FALSE(remote_snapshots.empty());
  REQUIRE(controlled_snapshots.back().bitrate_bps != 0);
  REQUIRE(remote_snapshots.back().controller_connected);
  REQUIRE(remote_snapshots.back().fec_unrecoverable == 0);

  const auto rumble_clear_before_stop = remote_backend_ptr->rumble_clear_calls;
  const auto gamepad_clear_before_stop = controlled_backend_ptr->clear_gamepad_calls;
  remote.stop();
  REQUIRE(remote_backend_ptr->rumble_clear_calls == rumble_clear_before_stop + 1);
  pump(controlled, remote, 2);
  REQUIRE(controlled.state() == RoleState::Broadcasting);
  REQUIRE(controlled.advertisement().controllable);
  REQUIRE(controlled_backend_ptr->clear_gamepad_calls == gamepad_clear_before_stop + 1);

  REQUIRE(remote.start());
  REQUIRE(remote.begin_discovery(500ms));
  for (unsigned attempt = 0;
       attempt < 750U && remote.discovery_state() == DiscoveryState::Searching; ++attempt) {
    pump(controlled, remote);
  }
  REQUIRE(remote.discovery_state() == DiscoveryState::Complete);
  REQUIRE(remote.hosts().size() == 1);
  REQUIRE(remote.connect(0));
  for (unsigned attempt = 0; attempt < 750U &&
                                (!remote.pairing() || !controlled.pairing());
       ++attempt) {
    pump(controlled, remote);
  }
  REQUIRE(remote.pairing());
  REQUIRE(controlled.pairing());
  remote.confirm_pairing();
  controlled.confirm_pairing();
  for (unsigned attempt = 0; attempt < 750U &&
                                (!remote.streaming() || !controlled.streaming());
       ++attempt) {
    pump(controlled, remote);
  }
  REQUIRE(remote.streaming());
  REQUIRE(controlled.streaming());

  controlled.stop();
  for (unsigned attempt = 0; attempt < 100U && remote.streaming(); ++attempt) {
    pump(controlled, remote);
  }
  REQUIRE(remote.state() == RoleState::RemoteBrowsing);
  remote.stop();
  REQUIRE(remote.state() == RoleState::Idle);
  REQUIRE(controlled.state() == RoleState::Idle);
  REQUIRE_FALSE(controlled.advertisement().controllable);
}

TEST_CASE("UDP receive batch drains a five-thousand packet burst") {
  UdpEndpoint sender;
  UdpEndpoint receiver;
  REQUIRE(sender.bind(0));
  REQUIRE(receiver.bind(0));
  REQUIRE(sender.set_remote("127.0.0.1", receiver.local_port()));
  REQUIRE(receiver.lock_peer({Datagram{{std::byte{0x42}}}, "127.0.0.1", sender.local_port()}));

  constexpr std::size_t packet_count = 5'000;
  const std::array<std::byte, 8> payload{std::byte{0x42}, std::byte{0x11}, std::byte{0x22},
                                         std::byte{0x33}, std::byte{0x44}, std::byte{0x55},
                                         std::byte{0x66}, std::byte{0x77}};
  for (std::size_t index = 0; index < packet_count; ++index) {
    REQUIRE(sender.send(payload));
  }

  std::size_t received = 0;
  for (unsigned attempt = 0; attempt < 100U && received < packet_count; ++attempt) {
    received += receiver.try_receive_batch(packet_count - received).size();
    if (received < packet_count) {
      std::this_thread::sleep_for(1ms);
    }
  }
  REQUIRE(received == packet_count);
}
