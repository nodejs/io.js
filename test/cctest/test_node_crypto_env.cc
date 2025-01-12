#include <ncrypto.h>
#include "crypto/crypto_bio.h"
#include "gtest/gtest.h"
#include "node_options.h"
#include "node_test_fixture.h"
#include "openssl/err.h"

using v8::Local;
using v8::String;

/*
 * This test verifies that an object created by LoadBIO supports BIO_tell
 * and BIO_seek, otherwise PEM_read_bio_PrivateKey fails on some keys
 * (if OpenSSL needs to rewind pointer between pem_read_bio_key()
 * and pem_read_bio_key_legacy() inside PEM_read_bio_PrivateKey).
 */
class NodeCryptoEnv : public EnvironmentTestFixture {};

TEST_F(NodeCryptoEnv, LoadBIO) {
  v8::HandleScope handle_scope(isolate_);
  Argv argv;
  Env env{handle_scope, argv};
  //  just put a random string into BIO
  Local<String> key = String::NewFromUtf8(isolate_, "abcdef").ToLocalChecked();
  ncrypto::BIOPointer bio(node::crypto::LoadBIO(*env, key));
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  const int ofs = 2;
  ASSERT_EQ(BIO_seek(bio.get(), ofs), ofs);
  ASSERT_EQ(BIO_tell(bio.get()), ofs);
#endif
  ASSERT_EQ(ERR_peek_error(), 0UL) << "There should not have left "
                                      "any errors on the OpenSSL error stack\n";
}
