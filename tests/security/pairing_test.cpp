#include "core/security/pairing.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace ministream;

TEST_CASE("pairing transcript binds both identities, ephemerals, and nonces") {
  const auto host_identity = generate_identity();
  const auto client_identity = generate_identity();
  const auto host_ephemeral = generate_ephemeral_keypair();
  const auto client_ephemeral = generate_ephemeral_keypair();
  REQUIRE(host_identity.has_value());
  REQUIRE(client_identity.has_value());
  REQUIRE(host_ephemeral.has_value());
  REQUIRE(client_ephemeral.has_value());

  PairingTranscript transcript{
      1, 100, 200, host_identity->public_key, client_identity->public_key,
      host_ephemeral->public_key, client_ephemeral->public_key};
  const auto sas = compute_pairing_sas(transcript);
  REQUIRE(sas < 1'000'000);
  REQUIRE(compute_pairing_sas(transcript) == sas);
  transcript.responder_nonce ^= 1U;
  REQUIRE(compute_pairing_sas(transcript) != sas);
}

TEST_CASE("pairing activation requires confirmation from both peers") {
  PairingConfirmation confirmation;
  REQUIRE_FALSE(confirmation.ready());
  confirmation.confirm_local();
  REQUIRE_FALSE(confirmation.ready());
  confirmation.confirm_peer();
  REQUIRE(confirmation.ready());
}

TEST_CASE("stored identity rejects a substituted ephemeral signature") {
  const auto identity = generate_identity();
  const auto attacker = generate_identity();
  const auto ephemeral = generate_ephemeral_keypair();
  REQUIRE(identity);
  REQUIRE(attacker);
  REQUIRE(ephemeral);
  const auto signature = sign_session_ephemeral(*identity, ephemeral->public_key, 10, 20);
  REQUIRE(signature.has_value());
  REQUIRE(verify_session_ephemeral(
      identity->public_key, ephemeral->public_key, 10, 20, *signature));
  REQUIRE_FALSE(verify_session_ephemeral(
      attacker->public_key, ephemeral->public_key, 10, 20, *signature));
}

TEST_CASE("ephemeral key exchange derives opposite directional keys") {
  const auto host = generate_ephemeral_keypair();
  const auto client = generate_ephemeral_keypair();
  REQUIRE(host);
  REQUIRE(client);
  const auto host_keys = derive_session_keys(*host, client->public_key, false);
  const auto client_keys = derive_session_keys(*client, host->public_key, true);
  REQUIRE(host_keys);
  REQUIRE(client_keys);
  REQUIRE(host_keys->tx == client_keys->rx);
  REQUIRE(host_keys->rx == client_keys->tx);
}
