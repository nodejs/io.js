// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_STRING_VIEW_H_
#define V8_COMPILER_TURBOSHAFT_STRING_VIEW_H_

#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

// `StringView` implements the `ForeachIterable` concept for iterating the
// characters of a string.
class StringView {
 public:
  using value_type = V<Word32>;
  using iterator_type = V<WordPtr>;

  StringView(const DisallowGarbageCollection& can_rely_on_no_gc,
             V<String> string, String::Encoding encoding,
             ConstOrV<WordPtr> start_index = 0,
             ConstOrV<WordPtr> character_count = V<WordPtr>::Invalid())
      : string_(string),
        encoding_(encoding),
        start_index_(start_index),
        character_count_(character_count),
        can_rely_on_no_gc_(&can_rely_on_no_gc) {}

  StringView(V<String> string, String::Encoding encoding,
             ConstOrV<WordPtr> start_index = 0,
             ConstOrV<WordPtr> character_count = V<WordPtr>::Invalid())
      : string_(string),
        encoding_(encoding),
        start_index_(start_index),
        character_count_(character_count),
        can_rely_on_no_gc_(nullptr) {}

  template <typename A>
  iterator_type Begin(A& assembler) {
    static_assert(OFFSET_OF_DATA_START(SeqOneByteString) ==
                  OFFSET_OF_DATA_START(SeqTwoByteString));
    const size_t data_offset = OFFSET_OF_DATA_START(SeqOneByteString);
    const int stride = (encoding_ == String::ONE_BYTE_ENCODING ? 1 : 2);
    if (can_rely_on_no_gc_ == nullptr) {
      // TODO(nicohartmann): If we cannot rely on no GC happening during
      // iteration, we cannot operate on raw inner pointers but have to
      // recompute the character address from the base on each dereferencing.
      UNIMPLEMENTED();
    }
    V<WordPtr> begin_offset = assembler.WordPtrAdd(
        assembler.BitcastTaggedToWordPtr(string_),
        assembler.WordPtrAdd(
            data_offset - kHeapObjectTag,
            assembler.WordPtrMul(assembler.resolve(start_index_), stride)));
    V<WordPtr> count;
    if (character_count_.is_constant()) {
      count = assembler.resolve(character_count_);
    } else if (character_count_.value().valid()) {
      count = character_count_.value();
    } else {
      // TODO(nicohartmann): Load from string.
      UNIMPLEMENTED();
    }
    end_offset_ =
        assembler.WordPtrAdd(begin_offset, assembler.WordPtrMul(count, stride));
    return begin_offset;
  }

  template <typename A>
  OptionalV<Word32> IsEnd(A& assembler, iterator_type current_iterator) const {
    return assembler.UintPtrLessThanOrEqual(end_offset_, current_iterator);
  }

  template <typename A>
  iterator_type Advance(A& assembler, iterator_type current_iterator) const {
    const int stride = (encoding_ == String::ONE_BYTE_ENCODING ? 1 : 2);
    return assembler.WordPtrAdd(current_iterator, stride);
  }

  template <typename A>
  value_type Dereference(A& assembler, iterator_type current_iterator) const {
    const auto loaded_rep = encoding_ == String::ONE_BYTE_ENCODING
                                ? MemoryRepresentation::Uint8()
                                : MemoryRepresentation::Uint16();
    return assembler.Load(current_iterator, LoadOp::Kind::RawAligned(),
                          loaded_rep);
  }

 private:
  V<String> string_;
  String::Encoding encoding_;
  ConstOrV<WordPtr> start_index_;
  ConstOrV<WordPtr> character_count_;
  V<WordPtr> end_offset_;
  const DisallowGarbageCollection* can_rely_on_no_gc_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_STRING_VIEW_H_
