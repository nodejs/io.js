// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_LITERAL_BUFFER_H_
#define V8_PARSING_LITERAL_BUFFER_H_

#include "include/v8config.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/strings/unicode-decoder.h"

namespace v8 {
namespace internal {

// LiteralBuffer -  Collector of chars of literals.
class LiteralBuffer final {
 public:
  LiteralBuffer() = default;
  ~LiteralBuffer() { backing_store_.Dispose(); }

  LiteralBuffer(const LiteralBuffer&) = delete;
  LiteralBuffer& operator=(const LiteralBuffer&) = delete;

  V8_INLINE void AddChar(char code_unit) {
    DCHECK(IsValidAscii(code_unit));
    AddOneByteChar(static_cast<uint8_t>(code_unit));
  }

  V8_INLINE void AddChar(base::uc32 code_unit) {
    if (is_one_byte()) {
      if (code_unit <= static_cast<base::uc32>(unibrow::Latin1::kMaxChar)) {
        AddOneByteChar(static_cast<uint8_t>(code_unit));
        return;
      }
      ConvertToTwoByte();
    }
    AddTwoByteChar(code_unit);
  }

  bool is_one_byte() const { return is_one_byte_; }

  bool Equals(base::Vector<const char> keyword) const {
    return is_one_byte() && keyword.length() == position_ &&
           (memcmp(keyword.begin(), backing_store_.begin(), position_) == 0);
  }

  base::Vector<const uint16_t> two_byte_literal() const {
    return literal<uint16_t>();
  }

  base::Vector<const uint8_t> one_byte_literal() const {
    return literal<uint8_t>();
  }

  template <typename Char>
  base::Vector<const Char> literal() const {
    DCHECK_EQ(is_one_byte_, sizeof(Char) == 1);
    DCHECK_EQ(position_ & (sizeof(Char) - 1), 0);
    return base::Vector<const Char>(
        reinterpret_cast<const Char*>(backing_store_.begin()),
        position_ >> (sizeof(Char) - 1));
  }

  int length() const { return is_one_byte() ? position_ : (position_ >> 1); }

  void Start() {
    position_ = 0;
    is_one_byte_ = true;
  }

  template <typename IsolateT>
  DirectHandle<String> Internalize(IsolateT* isolate) const;

 private:
  static constexpr int kInitialCapacity = 256;
  static constexpr int kGrowthFactor = 4;
  static constexpr int kMaxGrowth = 1 * MB;

  inline bool IsValidAscii(char code_unit) {
    // Control characters and printable characters span the range of
    // valid ASCII characters (0-127). Chars are unsigned on some
    // platforms which causes compiler warnings if the validity check
    // tests the lower bound >= 0 as it's always true.
    return iscntrl(code_unit) || isprint(code_unit);
  }

  V8_INLINE void AddOneByteChar(uint8_t one_byte_char) {
    DCHECK(is_one_byte());
    if (position_ >= backing_store_.length()) ExpandBuffer();
    backing_store_[position_] = one_byte_char;
    position_ += kOneByteSize;
  }

  void AddTwoByteChar(base::uc32 code_unit);
  int NewCapacity(int min_capacity);
  V8_NOINLINE V8_PRESERVE_MOST void ExpandBuffer();
  void ConvertToTwoByte();

  base::Vector<uint8_t> backing_store_;
  int position_ = 0;
  bool is_one_byte_ = true;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_LITERAL_BUFFER_H_
