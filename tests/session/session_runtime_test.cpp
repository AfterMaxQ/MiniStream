#include "core/session/session_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ministream;

TEST_CASE("session runtime starts and stops services in deterministic order") {
  std::vector<std::string> calls;
  SessionServices services;
  services.transport = {"transport", [&] { calls.emplace_back("+transport"); return true; },
                        [&] { calls.emplace_back("-transport"); }};
  services.video = {"video", [&] { calls.emplace_back("+video"); return true; },
                    [&] { calls.emplace_back("-video"); }};
  services.audio = {"audio", [&] { calls.emplace_back("+audio"); return true; },
                    [&] { calls.emplace_back("-audio"); }};
  services.input = {"input", [&] { calls.emplace_back("+input"); return true; },
                    [&] { calls.emplace_back("-input"); }};

  SessionRuntime runtime(std::move(services));
  REQUIRE(runtime.start());
  REQUIRE(runtime.state() == SessionState::Streaming);
  REQUIRE(runtime.stop());
  REQUIRE(runtime.state() == SessionState::Idle);
  REQUIRE(calls == std::vector<std::string>{
                       "+transport", "+video", "+audio", "+input",
                       "-input", "-audio", "-video", "-transport"});
}

TEST_CASE("session runtime unwinds started services after a startup failure") {
  std::vector<std::string> calls;
  SessionServices services;
  services.transport = {"transport", [&] { calls.emplace_back("+transport"); return true; },
                        [&] { calls.emplace_back("-transport"); }};
  services.video = {"video", [&] { calls.emplace_back("+video"); return false; },
                    [&] { calls.emplace_back("-video"); }};

  SessionRuntime runtime(std::move(services));
  const auto result = runtime.start();
  REQUIRE_FALSE(result);
  REQUIRE(result.error().component == "video");
  REQUIRE(runtime.state() == SessionState::Failed);
  REQUIRE(calls == std::vector<std::string>{"+transport", "+video", "-transport"});
  REQUIRE(runtime.reset_failure());
  REQUIRE(runtime.state() == SessionState::Idle);
}
