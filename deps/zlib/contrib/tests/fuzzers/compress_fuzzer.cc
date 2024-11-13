// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzzer/FuzzedDataProvider.h>

#include <vector>

#include "zlib.h"

// Fuzzer builds often have NDEBUG set, so roll our own assert macro.
#define ASSERT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d Assert failed: %s\n", __FILE__, __LINE__, #cond); \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider fdp(data, size);
  const int level = fdp.PickValueInArray({-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
  const std::vector<uint8_t> src = fdp.ConsumeRemainingBytes<uint8_t>();

  const unsigned long compress_bound = compressBound(src.size());
  std::vector<uint8_t> compressed;
  compressed.resize(compress_bound);

  unsigned long compressed_size = compress_bound;
  int ret = compress2(compressed.data(), &compressed_size, src.data(),
                      src.size(), level);
  ASSERT(ret == Z_OK);
  ASSERT(compressed_size <= compress_bound);
  compressed.resize(compressed_size);

  std::vector<uint8_t> uncompressed;
  uncompressed.resize(src.size());
  unsigned long uncompressed_size = uncompressed.size();
  ret = uncompress(uncompressed.data(), &uncompressed_size, compressed.data(),
                   compressed.size());
  ASSERT(ret == Z_OK);
  ASSERT(uncompressed_size == src.size());
  ASSERT(uncompressed == src);

  return 0;
}
