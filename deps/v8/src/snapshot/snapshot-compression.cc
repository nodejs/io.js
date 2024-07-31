// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/snapshot-compression.h"

#include "src/base/platform/elapsed-timer.h"
#include "src/utils/memcopy.h"
#include "src/utils/utils.h"
#include "third_party/zlib/google/compression_utils_portable.h"

namespace v8 {
namespace internal {

uint32_t GetUncompressedSize(const Bytef* compressed_data) {
  uint32_t size;
  MemCopy(&size, compressed_data, sizeof(size));
  return size;
}

SnapshotData SnapshotCompression::Compress(
    const SnapshotData* uncompressed_data) {
  SnapshotData snapshot_data;
  base::ElapsedTimer timer;
  if (v8_flags.profile_deserialization) timer.Start();

  static_assert(sizeof(Bytef) == 1, "");
  const uLongf input_size =
      static_cast<uLongf>(uncompressed_data->RawData().size());
  uint32_t payload_length =
      static_cast<uint32_t>(uncompressed_data->RawData().size());

  uLongf compressed_data_size = compressBound(input_size);

  // Allocating >= the final amount we will need.
  snapshot_data.AllocateData(
      static_cast<uint32_t>(sizeof(payload_length) + compressed_data_size));

  uint8_t* compressed_data =
      const_cast<uint8_t*>(snapshot_data.RawData().begin());
  // Since we are doing raw compression (no zlib or gzip headers), we need to
  // manually store the uncompressed size.
  MemCopy(compressed_data, &payload_length, sizeof(payload_length));

  CHECK_EQ(
      zlib_internal::CompressHelper(
          zlib_internal::ZRAW, compressed_data + sizeof(payload_length),
          &compressed_data_size,
          reinterpret_cast<const Bytef*>(uncompressed_data->RawData().begin()),
          input_size, Z_DEFAULT_COMPRESSION, nullptr, nullptr),
      Z_OK);

  // Reallocating to exactly the size we need.
  snapshot_data.Resize(static_cast<uint32_t>(compressed_data_size) +
                       sizeof(payload_length));
  DCHECK_EQ(payload_length,
            GetUncompressedSize(snapshot_data.RawData().begin()));

  if (v8_flags.profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Compressing %d bytes took %0.3f ms]\n", payload_length, ms);
  }
  return snapshot_data;
}

SnapshotData SnapshotCompression::Decompress(
    base::Vector<const uint8_t> compressed_data) {
  SnapshotData snapshot_data;
  base::ElapsedTimer timer;
  if (v8_flags.profile_deserialization) timer.Start();

  const Bytef* input_bytef =
      reinterpret_cast<const Bytef*>(compressed_data.begin());

  // Since we are doing raw compression (no zlib or gzip headers), we need to
  // manually retrieve the uncompressed size.
  uint32_t uncompressed_payload_length = GetUncompressedSize(input_bytef);
  input_bytef += sizeof(uncompressed_payload_length);

  snapshot_data.AllocateData(uncompressed_payload_length);

  uLongf uncompressed_size = uncompressed_payload_length;
  CHECK_EQ(zlib_internal::UncompressHelper(
               zlib_internal::ZRAW,
               const_cast<Bytef*>(snapshot_data.RawData().begin()),
               &uncompressed_size, input_bytef,
               static_cast<uLong>(compressed_data.size() -
                                  sizeof(uncompressed_payload_length))),
           Z_OK);

  if (v8_flags.profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Decompressing %d bytes took %0.3f ms]\n",
           uncompressed_payload_length, ms);
  }
  return snapshot_data;
}

}  // namespace internal
}  // namespace v8
