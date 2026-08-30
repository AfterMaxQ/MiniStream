#include "core/input/reliable_desktop_input.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace ministream;

namespace {

DesktopInput key(DesktopKey value, bool pressed) {
  return {DesktopInputKind::Key,
          static_cast<std::uint16_t>(pressed ? 0 : kDesktopKeyRelease), 0, 0,
          static_cast<std::uint16_t>(value)};
}

}  // namespace

TEST_CASE("reliable desktop input has a bounded reversible envelope") {
  const ReliableDesktopInput input{42, key(DesktopKey::W, false)};
  const auto bytes = encode_reliable_desktop_input(input);
  REQUIRE(bytes.size() == kReliableDesktopInputBytes);
  REQUIRE(decode_reliable_desktop_input(bytes) == input);

  auto malformed = bytes;
  malformed.pop_back();
  REQUIRE_FALSE(decode_reliable_desktop_input(malformed).has_value());
  REQUIRE(encode_reliable_desktop_input(
              {43, {DesktopInputKind::MouseMove, 0, 1, 2, 0}})
              .empty());
}

TEST_CASE("reliable desktop input applies buffered edges once in sequence order") {
  std::vector<DesktopInput> injected;
  ReliableDesktopInputReceiver receiver(
      [&](const DesktopInput& input) {
        injected.push_back(input);
        return true;
      });

  REQUIRE(receiver.receive({2, key(DesktopKey::W, false)}).empty());
  REQUIRE(injected.empty());
  REQUIRE(receiver.pending() == 1);

  REQUIRE(receiver.receive({1, key(DesktopKey::W, true)}) ==
          std::vector<ControlSeq>{1, 2});
  REQUIRE(injected ==
          std::vector<DesktopInput>{key(DesktopKey::W, true), key(DesktopKey::W, false)});
  REQUIRE(receiver.pending() == 0);

  REQUIRE(receiver.receive({1, key(DesktopKey::W, true)}) ==
          std::vector<ControlSeq>{1});
  REQUIRE(injected.size() == 2);
}

TEST_CASE("reliable desktop input retries failed injection without advancing") {
  unsigned attempts{};
  ReliableDesktopInputReceiver receiver([&](const DesktopInput&) {
    ++attempts;
    return attempts > 1;
  });

  REQUIRE(receiver.receive({1, key(DesktopKey::Space, true)}).empty());
  REQUIRE(receiver.pending() == 1);
  REQUIRE(receiver.receive({1, key(DesktopKey::Space, true)}) ==
          std::vector<ControlSeq>{1});
  REQUIRE(attempts == 2);
  REQUIRE(receiver.pending() == 0);
}

TEST_CASE("reliable desktop input bounds the out-of-order window") {
  ReliableDesktopInputReceiver receiver([](const DesktopInput&) { return true; });
  REQUIRE(receiver.receive({65, key(DesktopKey::A, true)}).empty());
  REQUIRE(receiver.pending() == 0);
  REQUIRE(receiver.next_sequence() == 1);
}
