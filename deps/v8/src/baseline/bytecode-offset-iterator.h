// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_BYTECODE_OFFSET_ITERATOR_H_
#define V8_BASELINE_BYTECODE_OFFSET_ITERATOR_H_

#include <optional>

#include "src/base/vlq.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects/bytecode-array.h"

namespace v8 {
namespace internal {

class BytecodeArray;

namespace baseline {

class V8_EXPORT_PRIVATE BytecodeOffsetIterator {
 public:
  explicit BytecodeOffsetIterator(Handle<TrustedByteArray> mapping_table,
                                  Handle<BytecodeArray> bytecodes);
  // Non-handlified version for use when no GC can happen.
  explicit BytecodeOffsetIterator(Tagged<TrustedByteArray> mapping_table,
                                  Tagged<BytecodeArray> bytecodes);
  ~BytecodeOffsetIterator();

  inline void Advance() {
    DCHECK(!done());
    current_pc_start_offset_ = current_pc_end_offset_;
    current_pc_end_offset_ += ReadPosition();
    current_bytecode_offset_ = bytecode_iterator_.current_offset();
    bytecode_iterator_.Advance();
  }

  inline void AdvanceToBytecodeOffset(int bytecode_offset) {
    while (current_bytecode_offset() < bytecode_offset) {
      Advance();
    }
    DCHECK_EQ(bytecode_offset, current_bytecode_offset());
  }

  inline void AdvanceToPCOffset(Address pc_offset) {
    while (current_pc_end_offset() < pc_offset) {
      Advance();
    }
    DCHECK_GT(pc_offset, current_pc_start_offset());
    DCHECK_LE(pc_offset, current_pc_end_offset());
  }

  // For this iterator, done() means that it is not safe to Advance().
  // Values are cached, so reads are always allowed.
  inline bool done() const { return current_index_ >= data_length_; }

  inline Address current_pc_start_offset() const {
    return current_pc_start_offset_;
  }

  inline Address current_pc_end_offset() const {
    return current_pc_end_offset_;
  }

  inline int current_bytecode_offset() const {
    return current_bytecode_offset_;
  }

  static void UpdatePointersCallback(void* iterator) {
    reinterpret_cast<BytecodeOffsetIterator*>(iterator)->UpdatePointers();
  }

  void UpdatePointers();

 private:
  void Initialize();
  inline int ReadPosition() {
    return base::VLQDecodeUnsigned(data_start_address_, &current_index_);
  }

  Handle<TrustedByteArray> mapping_table_;
  uint8_t* data_start_address_;
  int data_length_;
  int current_index_;
  Address current_pc_start_offset_;
  Address current_pc_end_offset_;
  int current_bytecode_offset_;
  Tagged<BytecodeArray> bytecode_handle_storage_;
  interpreter::BytecodeArrayIterator bytecode_iterator_;
  LocalHeap* local_heap_;
  std::optional<DisallowGarbageCollection> no_gc_;
};

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BYTECODE_OFFSET_ITERATOR_H_
