// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_BIT_VECTOR_H_
#define V8_UTILS_BIT_VECTOR_H_

#include <algorithm>

#include "src/base/bits.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE BitVector : public ZoneObject {
 public:
  // Iterator for the elements of this BitVector.
  class Iterator {
   public:
    V8_EXPORT_PRIVATE inline void operator++() {
      int bit_in_word = current_index_ & (kDataBits - 1);
      if (bit_in_word < kDataBits - 1) {
        uintptr_t remaining_bits = *ptr_ >> (bit_in_word + 1);
        if (remaining_bits) {
          int next_bit_in_word = base::bits::CountTrailingZeros(remaining_bits);
          current_index_ += next_bit_in_word + 1;
          return;
        }
      }

      // Move {current_index_} down to the beginning of the current word, before
      // starting to search for the next non-empty word.
      current_index_ = RoundDown(current_index_, kDataBits);
      do {
        ++ptr_;
        current_index_ += kDataBits;
        if (ptr_ == end_) return;
      } while (*ptr_ == 0);

      uintptr_t trailing_zeros = base::bits::CountTrailingZeros(*ptr_);
      current_index_ += trailing_zeros;
    }

    int operator*() const {
      DCHECK_NE(end_, ptr_);
      DCHECK(target_->Contains(current_index_));
      return current_index_;
    }

    bool operator==(const Iterator& other) const {
      DCHECK_EQ(target_, other.target_);
      DCHECK_EQ(end_, other.end_);
      DCHECK_IMPLIES(current_index_ == other.current_index_,
                     ptr_ == other.ptr_);
      return current_index_ == other.current_index_;
    }

    bool operator!=(const Iterator& other) const { return !(*this == other); }

   private:
    static constexpr struct StartTag {
    } kStartTag = {};
    static constexpr struct EndTag {
    } kEndTag = {};

    explicit Iterator(const BitVector* target, StartTag)
        :
#ifdef DEBUG
          target_(target),
#endif
          ptr_(target->data_begin_),
          end_(target->data_end_),
          current_index_(0) {
      DCHECK_LT(ptr_, end_);
      while (*ptr_ == 0) {
        ++ptr_;
        current_index_ += kDataBits;
        if (ptr_ == end_) return;
      }
      current_index_ += base::bits::CountTrailingZeros(*ptr_);
    }

    explicit Iterator(const BitVector* target, EndTag)
        :
#ifdef DEBUG
          target_(target),
#endif
          ptr_(target->data_end_),
          end_(target->data_end_),
          current_index_(target->data_length() * kDataBits) {
    }

#ifdef DEBUG
    const BitVector* target_;
#endif
    uintptr_t* ptr_;
    uintptr_t* end_;
    int current_index_;

    friend class BitVector;
  };

  static constexpr int kDataBits = kBitsPerSystemPointer;
  static constexpr int kDataBitShift = kBitsPerSystemPointerLog2;

  BitVector() = default;

  BitVector(int length, Zone* zone) : length_(length) {
    DCHECK_LE(0, length);
    int data_length = (length + kDataBits - 1) >> kDataBitShift;
    if (data_length > 1) {
      data_.ptr_ = zone->AllocateArray<uintptr_t>(data_length);
      std::fill_n(data_.ptr_, data_length, 0);
      data_begin_ = data_.ptr_;
      data_end_ = data_begin_ + data_length;
    }
  }

  BitVector(const BitVector& other, Zone* zone)
      : length_(other.length_), data_(other.data_.inline_) {
    if (!other.is_inline()) {
      int data_length = other.data_length();
      DCHECK_LT(1, data_length);
      data_.ptr_ = zone->AllocateArray<uintptr_t>(data_length);
      data_begin_ = data_.ptr_;
      data_end_ = data_begin_ + data_length;
      std::copy_n(other.data_begin_, data_length, data_begin_);
    }
  }

  // Disallow copy and copy-assignment.
  BitVector(const BitVector&) = delete;
  BitVector& operator=(const BitVector&) = delete;

  BitVector(BitVector&& other) V8_NOEXCEPT { *this = std::move(other); }

  BitVector& operator=(BitVector&& other) V8_NOEXCEPT {
    length_ = other.length_;
    data_ = other.data_;
    if (other.is_inline()) {
      data_begin_ = &data_.inline_;
      data_end_ = data_begin_ + other.data_length();
    } else {
      data_begin_ = other.data_begin_;
      data_end_ = other.data_end_;
      // Reset other to inline.
      other.length_ = 0;
      other.data_begin_ = &other.data_.inline_;
      other.data_end_ = other.data_begin_ + 1;
    }
    return *this;
  }

  void CopyFrom(const BitVector& other) {
    DCHECK_EQ(other.length(), length());
    DCHECK_EQ(is_inline(), other.is_inline());
    std::copy_n(other.data_begin_, data_length(), data_begin_);
  }

  void Resize(int new_length, Zone* zone) {
    DCHECK_GT(new_length, length());
    int old_data_length = data_length();
    DCHECK_LE(1, old_data_length);
    int new_data_length = (new_length + kDataBits - 1) >> kDataBitShift;
    if (new_data_length > old_data_length) {
      uintptr_t* new_data = zone->AllocateArray<uintptr_t>(new_data_length);

      // Copy over the data.
      std::copy_n(data_begin_, old_data_length, new_data);
      // Zero out the rest of the data.
      std::fill(new_data + old_data_length, new_data + new_data_length, 0);

      data_begin_ = new_data;
      data_end_ = new_data + new_data_length;
    }
    length_ = new_length;
  }

  bool Contains(int i) const {
    DCHECK(i >= 0 && i < length());
    return (data_begin_[word(i)] & bit(i)) != 0;
  }

  void Add(int i) {
    DCHECK(i >= 0 && i < length());
    data_begin_[word(i)] |= bit(i);
  }

  void AddAll() {
    // TODO(leszeks): This sets bits outside of the length of this bit-vector,
    // which is observable if we resize it or copy from it. If this is a
    // problem, we should clear the high bits either on add, or on resize/copy.
    memset(data_begin_, -1, sizeof(*data_begin_) * data_length());
  }

  void Remove(int i) {
    DCHECK(i >= 0 && i < length());
    data_begin_[word(i)] &= ~bit(i);
  }

  void Union(const BitVector& other) {
    DCHECK_EQ(other.length(), length());
    for (int i = 0; i < data_length(); i++) {
      data_begin_[i] |= other.data_begin_[i];
    }
  }

  bool UnionIsChanged(const BitVector& other) {
    DCHECK(other.length() == length());
    bool changed = false;
    for (int i = 0; i < data_length(); i++) {
      uintptr_t old_data = data_begin_[i];
      data_begin_[i] |= other.data_begin_[i];
      if (data_begin_[i] != old_data) changed = true;
    }
    return changed;
  }

  void Intersect(const BitVector& other) {
    DCHECK(other.length() == length());
    for (int i = 0; i < data_length(); i++) {
      data_begin_[i] &= other.data_begin_[i];
    }
  }

  bool IntersectIsChanged(const BitVector& other) {
    DCHECK(other.length() == length());
    bool changed = false;
    for (int i = 0; i < data_length(); i++) {
      uintptr_t old_data = data_begin_[i];
      data_begin_[i] &= other.data_begin_[i];
      if (data_begin_[i] != old_data) changed = true;
    }
    return changed;
  }

  void Subtract(const BitVector& other) {
    DCHECK(other.length() == length());
    for (int i = 0; i < data_length(); i++) {
      data_begin_[i] &= ~other.data_begin_[i];
    }
  }

  void Clear() { std::fill_n(data_begin_, data_length(), 0); }

  bool IsEmpty() const {
    return std::all_of(data_begin_, data_end_, std::logical_not<uintptr_t>{});
  }

  bool Equals(const BitVector& other) const {
    return std::equal(data_begin_, data_end_, other.data_begin_);
  }

  int Count() const;

  int length() const { return length_; }

  Iterator begin() const { return Iterator(this, Iterator::kStartTag); }

  Iterator end() const { return Iterator(this, Iterator::kEndTag); }

#ifdef DEBUG
  void Print() const;
#endif

 private:
  union DataStorage {
    uintptr_t* ptr_;    // valid if >1 machine word is needed
    uintptr_t inline_;  // valid if <=1 machine word is needed

    explicit DataStorage(uintptr_t value) : inline_(value) {}
  };

  bool is_inline() const { return data_begin_ == &data_.inline_; }
  int data_length() const { return static_cast<int>(data_end_ - data_begin_); }

  V8_INLINE static int word(int index) {
    V8_ASSUME(index >= 0);
    return index >> kDataBitShift;
  }
  V8_INLINE static uintptr_t bit(int index) {
    return uintptr_t{1} << (index & (kDataBits - 1));
  }

  int length_ = 0;
  DataStorage data_{0};
  uintptr_t* data_begin_ = &data_.inline_;
  uintptr_t* data_end_ = &data_.inline_ + 1;
};

class GrowableBitVector {
 public:
  GrowableBitVector() = default;
  GrowableBitVector(int length, Zone* zone) : bits_(length, zone) {}

  bool Contains(int value) const {
    if (!InBitsRange(value)) return false;
    return bits_.Contains(value);
  }

  void Add(int value, Zone* zone) {
    if (V8_UNLIKELY(!InBitsRange(value))) Grow(value, zone);
    bits_.Add(value);
  }

  bool IsEmpty() const { return bits_.IsEmpty(); }

  void Clear() { bits_.Clear(); }

  int length() const { return bits_.length(); }

  bool Equals(const GrowableBitVector& other) const {
    return length() == other.length() && bits_.Equals(other.bits_);
  }

  BitVector::Iterator begin() const { return bits_.begin(); }

  BitVector::Iterator end() const { return bits_.end(); }

 private:
  static constexpr int kInitialLength = 1024;

  // The allocated size is always a power of two, and needs to be strictly
  // bigger than the biggest contained value.
  static constexpr int kMaxSupportedValue = (1 << 30) - 1;

  bool InBitsRange(int value) const { return bits_.length() > value; }

  V8_NOINLINE void Grow(int needed_value, Zone* zone) {
    DCHECK(!InBitsRange(needed_value));
    // Ensure that {RoundUpToPowerOfTwo32} does not overflow {int} range.
    CHECK_GE(kMaxSupportedValue, needed_value);
    int new_length = std::max(
        kInitialLength, static_cast<int>(base::bits::RoundUpToPowerOfTwo32(
                            static_cast<uint32_t>(needed_value + 1))));
    bits_.Resize(new_length, zone);
  }

  BitVector bits_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_BIT_VECTOR_H_
