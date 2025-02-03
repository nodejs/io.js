// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_READ_ONLY_SERIALIZER_DESERIALIZER_H_
#define V8_SNAPSHOT_READ_ONLY_SERIALIZER_DESERIALIZER_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {
namespace ro {

// Common functionality for RO serialization and deserialization.

enum Bytecode {
  // kAllocatePage parameters:
  //   Uint30 page_index
  //   Uint30 area_size_in_bytes
  kAllocatePage,
  // kAllocatePageAt parameters:
  //   Uint30 page_index
  //   Uint30 area_size_in_bytes
  //   Uint32 compressed_page_address
  kAllocatePageAt,
  //
  // kSegment parameters:
  //   Uint30 page_index
  //   Uint30 offset
  //   Uint30 size_in_bytes
  //   ... segment byte stream
  kSegment,
  //
  // kRelocateSegment parameters:
  //   ... relocation byte stream
  kRelocateSegment,
  //
  // kReadOnlyRootsTable parameters:
  //   IF_STATIC_ROOTS(... ro roots table slots)
  kReadOnlyRootsTable,
  //
  kFinalizeReadOnlySpace,
};
static constexpr int kNumberOfBytecodes =
    static_cast<int>(kFinalizeReadOnlySpace) + 1;

// Like std::vector<bool> but with a known underlying encoding.
class BitSet final {
 public:
  explicit BitSet(size_t size_in_bits)
      : size_in_bits_(size_in_bits),
        data_(new uint8_t[size_in_bytes()]()),
        owns_data_(true) {}

  explicit BitSet(uint8_t* data, size_t size_in_bits)
      : size_in_bits_(size_in_bits), data_(data), owns_data_(false) {}

  ~BitSet() {
    if (owns_data_) delete[] data_;
  }

  bool contains(int i) const {
    DCHECK(0 <= i && i < static_cast<int>(size_in_bits_));
    return (data_[chunk_index(i)] & bit_mask(i)) != 0;
  }

  void set(int i) {
    DCHECK(0 <= i && i < static_cast<int>(size_in_bits_));
    data_[chunk_index(i)] |= bit_mask(i);
  }

  size_t size_in_bits() const { return size_in_bits_; }
  size_t size_in_bytes() const {
    return RoundUp<kBitsPerByte>(size_in_bits_) / kBitsPerByte;
  }

  const uint8_t* data() const { return data_; }

 private:
  static constexpr int kBitsPerChunk = kUInt8Size * kBitsPerByte;
  static constexpr int chunk_index(int i) { return i / kBitsPerChunk; }
  static constexpr int bit_index(int i) { return i % kBitsPerChunk; }
  static constexpr uint32_t bit_mask(int i) { return 1 << bit_index(i); }

  const size_t size_in_bits_;
  uint8_t* const data_;
  const bool owns_data_;
};

// Tagged slots need relocation after deserialization when V8_STATIC_ROOTS is
// disabled.
//
// Note this encoding works for all remaining build configs, in particular for
// all supported kTaggedSize values.
struct EncodedTagged {
  static constexpr int kOffsetBits = kPageSizeBits;
  static constexpr int kSize = kUInt32Size;
  static constexpr int kPageIndexBits =
      kSize * 8 - kOffsetBits;  // Determines max number of RO pages.

  explicit EncodedTagged(unsigned int page_index, unsigned int offset)
      : page_index(page_index), offset(offset) {
    DCHECK_LT(page_index, 1UL << kPageIndexBits);
    DCHECK_LT(offset, 1UL << kOffsetBits);
  }

  uint32_t ToUint32() const {
    static_assert(kSize == kUInt32Size);
    return *reinterpret_cast<const uint32_t*>(this);
  }
  static EncodedTagged FromUint32(uint32_t v) {
    return FromAddress(reinterpret_cast<Address>(&v));
  }
  static EncodedTagged FromAddress(Address address) {
    return *reinterpret_cast<EncodedTagged*>(address);
  }

  const unsigned int page_index : kPageIndexBits;
  const unsigned int offset : kOffsetBits;  // Shifted by kTaggedSizeLog2.
};
static_assert(EncodedTagged::kSize == sizeof(EncodedTagged));

struct EncodedExternalReference {
  static constexpr int kIsApiReferenceBits = 1;
  static constexpr int kIndexBits = 31;
  static constexpr int kSize = kUInt32Size;

  uint32_t ToUint32() const {
    static_assert(kSize == kUInt32Size);
    return *reinterpret_cast<const uint32_t*>(this);
  }
  static EncodedExternalReference FromUint32(uint32_t v) {
    return *reinterpret_cast<EncodedExternalReference*>(&v);
  }

  // This ctor is needed to convert parameter types. We can't use bool/uint32_t
  // as underlying member types since that messes with field packing on
  // windows.
  EncodedExternalReference(bool is_api_reference, uint32_t index)
      : is_api_reference(is_api_reference), index(index) {}

  int is_api_reference : kIsApiReferenceBits;
  int index : kIndexBits;
};
static_assert(EncodedExternalReference::kSize ==
              sizeof(EncodedExternalReference));

}  // namespace ro
}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_READ_ONLY_SERIALIZER_DESERIALIZER_H_
