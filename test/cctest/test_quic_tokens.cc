#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#include <gtest/gtest.h>
#include <ngtcp2/ngtcp2.h>
#include <node_sockaddr-inl.h>
#include <quic/cid.h>
#include <quic/tokens.h>
#include <util-inl.h>
#include <string>
#include <unordered_map>

using node::quic::CID;
using node::quic::RegularToken;
using node::quic::RetryToken;
using node::quic::StatelessResetToken;
using node::quic::TokenSecret;

TEST(TokenSecret, Basics) {
  uint8_t secret[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6};
  TokenSecret fixed_secret(secret);

  for (int n = 0; n < TokenSecret::QUIC_TOKENSECRET_LEN; n++) {
    CHECK_EQ(fixed_secret[n], secret[n]);
  }

  // Copy assignment works
  TokenSecret other = fixed_secret;
  for (int n = 0; n < TokenSecret::QUIC_TOKENSECRET_LEN; n++) {
    CHECK_EQ(other[n], secret[n]);
  }
}

TEST(StatelessResetToken, Basic) {
  ngtcp2_cid cid_;
  uint8_t secret[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6};
  uint8_t nothing[StatelessResetToken::kStatelessTokenLen]{};
  uint8_t cid_data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
  ngtcp2_cid_init(&cid_, cid_data, 10);

  TokenSecret fixed_secret(secret);

  CID cid(cid_);

  CHECK(!StatelessResetToken::kInvalid);
  const uint8_t* zeroed = StatelessResetToken::kInvalid;
  CHECK_EQ(memcmp(zeroed, nothing, StatelessResetToken::kStatelessTokenLen), 0);
  CHECK_EQ(StatelessResetToken::kInvalid.ToString(), "");

  StatelessResetToken token(fixed_secret, cid);
  CHECK(token);
  CHECK_EQ(token.ToString(), "e21ea22bb78cae0ab8c7daa422240857");

  // Token generation should be deterministic
  StatelessResetToken token2(fixed_secret, cid);

  CHECK_EQ(token, token2);

  // Let's pretend out secret is also a token just for the sake
  // of the test. That's ok because they're the same length.
  StatelessResetToken token3(secret);

  CHECK_NE(token, token3);

  // Copy constructor works.
  StatelessResetToken token4 = token3;
  CHECK_EQ(token3, token4);

  uint8_t wrapped[StatelessResetToken::kStatelessTokenLen];
  StatelessResetToken token5(wrapped, fixed_secret, cid);
  CHECK_EQ(token5, token);

  // StatelessResetTokens will be used as keys in a map...
  StatelessResetToken::Map<std::string> map;
  map[token] = "abc";
  map[token3] = "xyz";
  CHECK_EQ(map[token], "abc");
  CHECK_EQ(map[token4], "xyz");

  // And as values in a CID::Map...
  CID::Map<StatelessResetToken> tokens;
  tokens.emplace(cid, token);
  auto found = tokens.find(cid);
  CHECK_NE(found, tokens.end());
  CHECK_EQ(found->second, token);
}

TEST(RetryToken, Basic) {
  auto& random = CID::Factory::random();
  TokenSecret secret;
  node::SocketAddress address;
  CHECK(node::SocketAddress::New(AF_INET, "123.123.123.123", 1234, &address));
  auto retry_cid = random.Generate();
  auto odcid = random.Generate();
  RetryToken token(NGTCP2_PROTO_VER_MAX, address, retry_cid, odcid, secret);
  auto result = token.Validate(NGTCP2_PROTO_VER_MAX,
                               address,
                               retry_cid,
                               secret,
                               // Set a large expiration just to be safe
                               10000000000);
  CHECK_NE(result, std::nullopt);
  CHECK_EQ(result.value(), odcid);

  // We can pass the data into a new instance...
  ngtcp2_vec token_data = token;
  RetryToken token2(token_data.base, token_data.len);
  auto result2 = token.Validate(NGTCP2_PROTO_VER_MAX,
                                address,
                                retry_cid,
                                secret,
                                // Set a large expiration just to be safe
                                10000000000);
  CHECK_NE(result2, std::nullopt);
  CHECK_EQ(result2.value(), odcid);

  auto noresult = token.Validate(NGTCP2_PROTO_VER_MAX,
                                 address,
                                 retry_cid,
                                 secret,
                                 // Use a very small expiration that is
                                 // guaranteed to fail
                                 0);
  CHECK_EQ(noresult, std::nullopt);

  // Fails if we change the retry_cid...
  auto noresult2 = token.Validate(
      NGTCP2_PROTO_VER_MAX, address, random.Generate(), secret, 10000000000);
  CHECK_EQ(noresult2, std::nullopt);

  // Also fails if we change the address....
  CHECK(node::SocketAddress::New(AF_INET, "123.123.123.124", 1234, &address));

  auto noresult3 = token.Validate(
      NGTCP2_PROTO_VER_MAX, address, retry_cid, secret, 10000000000);
  CHECK_EQ(noresult3, std::nullopt);
}

TEST(RegularToken, Basic) {
  TokenSecret secret;
  node::SocketAddress address;
  CHECK(node::SocketAddress::New(AF_INET, "123.123.123.123", 1234, &address));
  RegularToken token(NGTCP2_PROTO_VER_MAX, address, secret);
  CHECK(token.Validate(NGTCP2_PROTO_VER_MAX,
                       address,
                       secret,
                       // Set a large expiration just to be safe
                       10000000000));

  // We can pass the data into a new instance...
  ngtcp2_vec token_data = token;
  RegularToken token2(token_data.base, token_data.len);
  CHECK(token.Validate(NGTCP2_PROTO_VER_MAX,
                       address,
                       secret,
                       // Set a large expiration just to be safe
                       10000000000));

  CHECK(!token.Validate(NGTCP2_PROTO_VER_MAX,
                        address,
                        secret,
                        // Use a very small expiration that is
                        // guaranteed to fail
                        0));

  // Also fails if we change the address....
  CHECK(node::SocketAddress::New(AF_INET, "123.123.123.124", 1234, &address));

  CHECK(!token.Validate(NGTCP2_PROTO_VER_MAX, address, secret, 10000000000));
}
#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
