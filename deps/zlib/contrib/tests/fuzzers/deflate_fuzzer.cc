// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzzer/FuzzedDataProvider.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
  int level = fdp.PickValueInArray({-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
  int windowBits = fdp.PickValueInArray({9, 10, 11, 12, 13, 14, 15});
  int memLevel = fdp.PickValueInArray({1, 2, 3, 4, 5, 6, 7, 8, 9});
  int strategy = fdp.PickValueInArray(
      {Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED});

   if (fdp.ConsumeBool()) {
    // Gzip wrapper.
    windowBits += 16;
  } else if (fdp.ConsumeBool()) {
    // Raw deflate.
    windowBits *= -1;
  } else {
    // Default: zlib wrapper.
  }

  std::vector<uint8_t> src;
  std::vector<uint8_t> compressed;
  static const int kMinChunk = 1;
  static const int kMaxChunk = 512 * 1024;

  z_stream stream;
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  int ret =
      deflateInit2(&stream, level, Z_DEFLATED, windowBits, memLevel, strategy);
  ASSERT(ret == Z_OK);

  // Stream with random-sized input and output buffers.
  while (fdp.ConsumeBool()) {
    if (fdp.ConsumeBool()) {
      // Check that copying the stream's state works. Gating this behind
      // ConsumeBool() allows to interleave deflateCopy() with deflate() calls
      // to better stress the code.
      z_stream stream2;
      ASSERT(deflateCopy(&stream2, &stream) == Z_OK);
      ret = deflateEnd(&stream);
      ASSERT(ret == Z_OK || Z_DATA_ERROR);
      memset(&stream, 0xff, sizeof(stream));

      ASSERT(deflateCopy(&stream, &stream2) == Z_OK);
      ret = deflateEnd(&stream2);
      ASSERT(ret == Z_OK || Z_DATA_ERROR);
    }

    std::vector<uint8_t> src_chunk = fdp.ConsumeBytes<uint8_t>(
        fdp.ConsumeIntegralInRange(kMinChunk, kMaxChunk));
    std::vector<uint8_t> out_chunk(
        fdp.ConsumeIntegralInRange(kMinChunk, kMaxChunk));
    stream.next_in = src_chunk.data();
    stream.avail_in = src_chunk.size();
    stream.next_out = out_chunk.data();
    stream.avail_out = out_chunk.size();
    ret = deflate(&stream, Z_NO_FLUSH);
    ASSERT(ret == Z_OK || ret == Z_BUF_ERROR);

    src.insert(src.end(), src_chunk.begin(), src_chunk.end() - stream.avail_in);
    compressed.insert(compressed.end(), out_chunk.begin(),
                      out_chunk.end() - stream.avail_out);
  }
  // Finish up.
  while (true) {
    std::vector<uint8_t> out_chunk(
        fdp.ConsumeIntegralInRange(kMinChunk, kMaxChunk));
    stream.next_in = Z_NULL;
    stream.avail_in = 0;
    stream.next_out = out_chunk.data();
    stream.avail_out = out_chunk.size();
    ret = deflate(&stream, Z_FINISH);
    compressed.insert(compressed.end(), out_chunk.begin(),
                      out_chunk.end() - stream.avail_out);
    if (ret == Z_STREAM_END) {
      break;
    }
    ASSERT(ret == Z_OK || Z_BUF_ERROR);
  }
  deflateEnd(&stream);

  // Check deflateBound().
  // Use a newly initialized stream since computing the bound on a "used" stream
  // may not yield a correct result (https://github.com/madler/zlib/issues/944).
  z_stream bound_stream;
  bound_stream.zalloc = Z_NULL;
  bound_stream.zfree = Z_NULL;
  ret = deflateInit2(&bound_stream, level, Z_DEFLATED, windowBits, memLevel,
                     strategy);
  ASSERT(ret == Z_OK);
  size_t deflate_bound = deflateBound(&bound_stream, src.size());
  ASSERT(compressed.size() <= deflate_bound);
  deflateEnd(&bound_stream);

  // Verify that the data decompresses correctly.
  ret = inflateInit2(&stream, windowBits);
  ASSERT(ret == Z_OK);
  // Make room for at least one byte so it's never empty.
  std::vector<uint8_t> decompressed(src.size() + 1);
  stream.next_in = compressed.data();
  stream.avail_in = compressed.size();
  stream.next_out = decompressed.data();
  stream.avail_out = decompressed.size();
  ret = inflate(&stream, Z_FINISH);
  ASSERT(ret == Z_STREAM_END);
  decompressed.resize(decompressed.size() - stream.avail_out);
  inflateEnd(&stream);

  ASSERT(decompressed == src);

  return 0;
}
