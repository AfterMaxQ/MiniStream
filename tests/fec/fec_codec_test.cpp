#include "core/fec/fec_codec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using namespace ministream;

namespace {
std::vector<Datagram> make_data() {
  std::vector<Datagram> data;
  for (std::size_t shard = 0; shard < 20; ++shard) {
    Datagram datagram;
    datagram.bytes.resize(1100 + shard);
    for (std::size_t byte = 0; byte < datagram.bytes.size(); ++byte) {
      datagram.bytes[byte] = static_cast<std::byte>((shard * 17 + byte) % 251U);
    }
    data.push_back(std::move(datagram));
  }
  return data;
}
}  // namespace

TEST_CASE("20+2 FEC restores two erased data shards") {
  const auto data = make_data();
  FecCodec codec;
  auto block = codec.encode(data, 2);
  REQUIRE(block.layout.data_shards == 20);
  REQUIRE(block.layout.parity_shards == 2);
  REQUIRE(block.layout.shard_bytes % 64 == 0);

  block.shards[3].present = false;
  block.shards[3].bytes.clear();
  block.shards[17].present = false;
  block.shards[17].bytes.clear();
  REQUIRE(codec.recover(block));
  REQUIRE(block.shards[3].bytes == data[3].bytes);
  REQUIRE(block.shards[17].bytes == data[17].bytes);
}

TEST_CASE("20+2 FEC fails cleanly after three erasures") {
  FecCodec codec;
  auto block = codec.encode(make_data(), 2);
  for (auto index : {1U, 7U, 19U}) {
    block.shards[index].present = false;
    block.shards[index].bytes.clear();
  }
  REQUIRE_FALSE(codec.recover(block));
}
