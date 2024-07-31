/*
 * A fuzzer focused on node::crypto::ClientHelloParser.
 */

#include <stdlib.h>
#include "crypto/crypto_clienthello-inl.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  node::crypto::ClientHelloParser parser;
  bool end_cb_called = false;
  parser.Start([](void* arg, auto hello) { },
               [](void* arg) { },
               &end_cb_called);
  parser.Parse(data, size);
  return 0;
}
