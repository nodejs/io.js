// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_STRING_BUILDER_H_
#define V8_STRINGS_STRING_BUILDER_H_

#include "src/common/assert-scope.h"
#include "src/handles/handles.h"
#include "src/objects/string.h"

namespace v8 {
namespace internal {

class FixedArrayBuilder {
 public:
  explicit FixedArrayBuilder(Isolate* isolate, int initial_capacity);
  explicit FixedArrayBuilder(DirectHandle<FixedArray> backing_store);

  // Creates a FixedArrayBuilder which allocates its backing store lazily when
  // EnsureCapacity is called.
  static FixedArrayBuilder Lazy(Isolate* isolate);

  bool HasCapacity(int elements);
  void EnsureCapacity(Isolate* isolate, int elements);

  void Add(Tagged<Object> value);
  void Add(Tagged<Smi> value);

  DirectHandle<FixedArray> array() { return array_; }

  int length() { return length_; }

  int capacity();

 private:
  explicit FixedArrayBuilder(Isolate* isolate);

  DirectHandle<FixedArray> array_;
  int length_;
  bool has_non_smi_elements_;
};

class ReplacementStringBuilder {
 public:
  ReplacementStringBuilder(Heap* heap, DirectHandle<String> subject,
                           int estimated_part_count);

  // Caution: Callers must ensure the builder has enough capacity.
  static inline void AddSubjectSlice(FixedArrayBuilder* builder, int from,
                                     int to);

  inline void AddSubjectSlice(int from, int to);

  void AddString(DirectHandle<String> string);

  MaybeDirectHandle<String> ToString();

  void IncrementCharacterCount(int by) {
    if (character_count_ > String::kMaxLength - by) {
      static_assert(String::kMaxLength < kMaxInt);
      character_count_ = kMaxInt;
    } else {
      character_count_ += by;
    }
  }

 private:
  void AddElement(DirectHandle<Object> element);
  void EnsureCapacity(int elements);

  Heap* heap_;
  FixedArrayBuilder array_builder_;
  DirectHandle<String> subject_;
  int character_count_;
  bool is_one_byte_;
};

class IncrementalStringBuilder {
 public:
  explicit IncrementalStringBuilder(Isolate* isolate);

  V8_INLINE String::Encoding CurrentEncoding() { return encoding_; }

  template <typename SrcChar, typename DestChar>
  V8_INLINE void Append(SrcChar c);

  V8_INLINE void AppendCharacter(uint8_t c);

  template <int N>
  V8_INLINE void AppendCStringLiteral(const char (&literal)[N]);

  template <typename SrcChar>
  V8_INLINE void AppendCString(const SrcChar* s);

  V8_INLINE void AppendInt(int i);

  V8_INLINE bool CurrentPartCanFit(int length) {
    return part_length_ - current_index_ > length;
  }

  // We make a rough estimate to find out if the current string can be
  // serialized without allocating a new string part.
  V8_INLINE int EscapedLengthIfCurrentPartFits(int length);

  void AppendString(DirectHandle<String> string);

  MaybeDirectHandle<String> Finish();

  V8_INLINE bool HasOverflowed() const { return overflowed_; }

  int Length() const;

  // Change encoding to two-byte.
  V8_INLINE void ChangeEncoding();

  template <typename DestChar>
  class NoExtend {
   public:
    inline NoExtend(Tagged<String> string, int offset,
                    const DisallowGarbageCollection& no_gc);

#ifdef DEBUG
    inline ~NoExtend();
#endif

    V8_INLINE void Append(DestChar c) { *(cursor_++) = c; }
    V8_INLINE void AppendCString(const char* s) {
      const uint8_t* u = reinterpret_cast<const uint8_t*>(s);
      while (*u != '\0') Append(*(u++));
    }

    int written() { return static_cast<int>(cursor_ - start_); }

   private:
    DestChar* start_;
    DestChar* cursor_;
#ifdef DEBUG
    Tagged<String> string_;
#endif
    DISALLOW_GARBAGE_COLLECTION(no_gc_)
  };

  template <typename DestChar>
  class NoExtendBuilder : public NoExtend<DestChar> {
   public:
    inline NoExtendBuilder(IncrementalStringBuilder* builder,
                           int required_length,
                           const DisallowGarbageCollection& no_gc);

    ~NoExtendBuilder() {
      builder_->current_index_ += NoExtend<DestChar>::written();
      DCHECK(builder_->HasValidCurrentIndex());
    }

   private:
    IncrementalStringBuilder* builder_;
  };

  Isolate* isolate() { return isolate_; }

 private:
  V8_INLINE Factory* factory();

  V8_INLINE DirectHandle<String> accumulator() { return accumulator_; }

  V8_INLINE void set_accumulator(DirectHandle<String> string) {
    accumulator_.PatchValue(*string);
  }

  V8_INLINE DirectHandle<String> current_part() { return current_part_; }

  V8_INLINE void set_current_part(DirectHandle<String> string) {
    current_part_.PatchValue(*string);
  }

  // Add the current part to the accumulator.
  void Accumulate(DirectHandle<String> new_part);

  // Finish the current part and allocate a new part.
  void Extend();

  bool HasValidCurrentIndex() const;

  // Shrink current part to the right size.
  V8_INLINE void ShrinkCurrentPart();

  void AppendStringByCopy(DirectHandle<String> string);
  bool CanAppendByCopy(DirectHandle<String> string);

  static const int kInitialPartLength = 32;
  static const int kMaxPartLength = 16 * 1024;
  static const int kPartLengthGrowthFactor = 2;
  static const int kIntToCStringBufferSize = 100;

  Isolate* isolate_;
  String::Encoding encoding_;
  bool overflowed_;
  int part_length_;
  int current_index_;
  DirectHandle<String> accumulator_;
  DirectHandle<String> current_part_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_STRINGS_STRING_BUILDER_H_
