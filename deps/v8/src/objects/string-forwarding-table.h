// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_FORWARDING_TABLE_H_
#define V8_OBJECTS_STRING_FORWARDING_TABLE_H_

#include "src/objects/string.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// Mapping from forwarding indices (stored in a string's hash field) to
// internalized strings/external resources.
// The table is used to handle string transitions (temporarily until the next
// full GC, during which actual string transitions happen) that overwrite the
// string buffer. In particular these are Internalization and Externalization.
// The table is organised in "blocks". As writes only append new entries, the
// organisation in blocks allows lock-free writes. We need a lock only for
// growing the table (adding more blocks). When the vector holding the blocks
// needs to grow, we keep a copy of the old vector alive to allow concurrent
// reads while the vector is relocated.
class StringForwardingTable {
 public:
  // Capacity for the first block.
  static constexpr int kInitialBlockSize = 16;
  static_assert(base::bits::IsPowerOfTwo(kInitialBlockSize));
  static constexpr int kInitialBlockSizeHighestBit =
      kBitsPerInt - base::bits::CountLeadingZeros32(kInitialBlockSize) - 1;
  // Initial capacity in the block vector.
  static constexpr int kInitialBlockVectorCapacity = 4;
  static constexpr Tagged<Smi> unused_element() { return Smi::FromInt(0); }
  static constexpr Tagged<Smi> deleted_element() { return Smi::FromInt(1); }

  explicit StringForwardingTable(Isolate* isolate);
  ~StringForwardingTable();

  inline int size() const;
  inline bool empty() const;
  // Returns the index of the added record.
  int AddForwardString(Tagged<String> string, Tagged<String> forward_to);
  template <typename T>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  int AddExternalResourceAndHash(Tagged<String> string, T* resource,
                                 uint32_t raw_hash);
  void UpdateForwardString(int index, Tagged<String> forward_to);
  // Returns true when the resource was set. When an external resource is
  // already set for the record, false is returned and the resource not stored.
  // The caller is responsible for disposing the resource.
  template <typename T>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  bool TryUpdateExternalResource(int index, T* resource);
  Tagged<String> GetForwardString(PtrComprCageBase cage_base, int index) const;
  static Address GetForwardStringAddress(Isolate* isolate, int index);
  V8_EXPORT_PRIVATE uint32_t GetRawHash(PtrComprCageBase cage_base,
                                        int index) const;
  static uint32_t GetRawHashStatic(Isolate* isolate, int index);
  v8::String::ExternalStringResourceBase* GetExternalResource(
      int index, bool* is_one_byte) const;

  template <typename Func>
  V8_INLINE void IterateElements(Func&& callback);
  // Dispose all external resources stored in the table.
  void TearDown();
  void Reset();
  void UpdateAfterYoungEvacuation();
  void UpdateAfterFullEvacuation();

  class Record;

 private:
  class Block;
  class BlockVector;

  // Returns the block for a given index and sets the index within this block
  // as out parameter.
  static inline uint32_t BlockForIndex(int index, uint32_t* index_in_block_out);
  static inline uint32_t IndexInBlock(int index, uint32_t block);
  static inline uint32_t CapacityForBlock(uint32_t block);

  void InitializeBlockVector();
  // Ensure that |block| exists in the BlockVector already. If not, a new block
  // is created (with capacity double the capacity of the last block) and
  // inserted into the BlockVector. The BlockVector itself might grow (to double
  // the capacity).
  BlockVector* EnsureCapacity(uint32_t block);

  Isolate* isolate_;
  std::atomic<BlockVector*> blocks_;
  // We need a vector of BlockVectors to keep old BlockVectors alive when we
  // grow the table, due to concurrent reads that may still hold a pointer to
  // them. |block_vector_sotrage_| is only accessed while we grow with the mutex
  // held. All regular access go through |block_|, which holds a pointer to the
  // current BlockVector.
  std::vector<std::unique_ptr<BlockVector>> block_vector_storage_;
  std::atomic<int> next_free_index_;
  base::SpinningMutex grow_mutex_;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_FORWARDING_TABLE_H_
