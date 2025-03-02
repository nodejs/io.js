// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_STRING_HASHER_H_
#define V8_STRINGS_STRING_HASHER_H_

#include "src/common/globals.h"

namespace v8 {

namespace base {
template <typename T>
class Vector;
}  // namespace base

namespace internal {

// A simple incremental string hasher. Slow but allows for special casing each
// individual character.
class RunningStringHasher final {
 public:
  explicit RunningStringHasher(uint32_t seed) : running_hash_(seed) {}

  V8_INLINE void AddCharacter(uint16_t c);
  V8_INLINE uint32_t Finalize();

 private:
  uint32_t running_hash_;
};

// Helper class for incrementally calculating string hashes in a form suitable
// for storing into Name::raw_hash_field.
class V8_EXPORT_PRIVATE StringHasher final {
 public:
  StringHasher() = delete;
  template <typename char_t>
  static inline uint32_t HashSequentialString(const char_t* chars,
                                              uint32_t length, uint64_t seed);

  // Calculated hash value for a string consisting of 1 to
  // String::kMaxArrayIndexSize digits with no leading zeros (except "0").
  // value is represented decimal value.
  static V8_INLINE uint32_t MakeArrayIndexHash(uint32_t value, uint32_t length);

  // No string is allowed to have a hash of zero.  That value is reserved
  // for internal properties.  If the hash calculation yields zero then we
  // use 27 instead.
  static const int kZeroHash = 27;

  static V8_INLINE uint32_t GetTrivialHash(uint32_t length);
};

// Useful for std containers that require something ()'able.
struct SeededStringHasher {
  explicit SeededStringHasher(uint64_t hashseed) : hashseed_(hashseed) {}
  inline std::size_t operator()(const char* name) const;

  uint64_t hashseed_;
};

// Useful for std containers that require something ()'able.
struct StringEquals {
  bool operator()(const char* name1, const char* name2) const {
    return strcmp(name1, name2) == 0;
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_STRINGS_STRING_HASHER_H_
