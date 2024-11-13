// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/str_cat.h"

#include <assert.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/nullability.h"
#include "absl/strings/internal/resize_uninitialized.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// ----------------------------------------------------------------------
// StrCat()
//    This merges the given strings or integers, with no delimiter. This
//    is designed to be the fastest possible way to construct a string out
//    of a mix of raw C strings, string_views, strings, and integer values.
// ----------------------------------------------------------------------

namespace {
// Append is merely a version of memcpy that returns the address of the byte
// after the area just overwritten.
inline absl::Nonnull<char*> Append(absl::Nonnull<char*> out,
                                   const AlphaNum& x) {
  // memcpy is allowed to overwrite arbitrary memory, so doing this after the
  // call would force an extra fetch of x.size().
  char* after = out + x.size();
  if (x.size() != 0) {
    memcpy(out, x.data(), x.size());
  }
  return after;
}

inline void STLStringAppendUninitializedAmortized(std::string* dest,
                                                  size_t to_append) {
  strings_internal::AppendUninitializedTraits<std::string>::Append(dest,
                                                                   to_append);
}
}  // namespace

std::string StrCat(const AlphaNum& a, const AlphaNum& b) {
  std::string result;
  // Use uint64_t to prevent size_t overflow. We assume it is not possible for
  // in memory strings to overflow a uint64_t.
  constexpr uint64_t kMaxSize = uint64_t{std::numeric_limits<size_t>::max()};
  const uint64_t result_size =
      static_cast<uint64_t>(a.size()) + static_cast<uint64_t>(b.size());
  ABSL_INTERNAL_CHECK(result_size <= kMaxSize, "size_t overflow");
  absl::strings_internal::STLStringResizeUninitialized(
      &result, static_cast<size_t>(result_size));
  char* const begin = &result[0];
  char* out = begin;
  out = Append(out, a);
  out = Append(out, b);
  assert(out == begin + result.size());
  return result;
}

std::string StrCat(const AlphaNum& a, const AlphaNum& b, const AlphaNum& c) {
  std::string result;
  // Use uint64_t to prevent size_t overflow. We assume it is not possible for
  // in memory strings to overflow a uint64_t.
  constexpr uint64_t kMaxSize = uint64_t{std::numeric_limits<size_t>::max()};
  const uint64_t result_size = static_cast<uint64_t>(a.size()) +
                               static_cast<uint64_t>(b.size()) +
                               static_cast<uint64_t>(c.size());
  ABSL_INTERNAL_CHECK(result_size <= kMaxSize, "size_t overflow");
  strings_internal::STLStringResizeUninitialized(
      &result, static_cast<size_t>(result_size));
  char* const begin = &result[0];
  char* out = begin;
  out = Append(out, a);
  out = Append(out, b);
  out = Append(out, c);
  assert(out == begin + result.size());
  return result;
}

std::string StrCat(const AlphaNum& a, const AlphaNum& b, const AlphaNum& c,
                   const AlphaNum& d) {
  std::string result;
  // Use uint64_t to prevent size_t overflow. We assume it is not possible for
  // in memory strings to overflow a uint64_t.
  constexpr uint64_t kMaxSize = uint64_t{std::numeric_limits<size_t>::max()};
  const uint64_t result_size = static_cast<uint64_t>(a.size()) +
                               static_cast<uint64_t>(b.size()) +
                               static_cast<uint64_t>(c.size()) +
                               static_cast<uint64_t>(d.size());
  ABSL_INTERNAL_CHECK(result_size <= kMaxSize, "size_t overflow");
  strings_internal::STLStringResizeUninitialized(
      &result, static_cast<size_t>(result_size));
  char* const begin = &result[0];
  char* out = begin;
  out = Append(out, a);
  out = Append(out, b);
  out = Append(out, c);
  out = Append(out, d);
  assert(out == begin + result.size());
  return result;
}

namespace strings_internal {

// Do not call directly - these are not part of the public API.
std::string CatPieces(std::initializer_list<absl::string_view> pieces) {
  std::string result;
  // Use uint64_t to prevent size_t overflow. We assume it is not possible for
  // in memory strings to overflow a uint64_t.
  constexpr uint64_t kMaxSize = uint64_t{std::numeric_limits<size_t>::max()};
  uint64_t total_size = 0;
  for (absl::string_view piece : pieces) {
    total_size += piece.size();
  }
  ABSL_INTERNAL_CHECK(total_size <= kMaxSize, "size_t overflow");
  strings_internal::STLStringResizeUninitialized(
      &result, static_cast<size_t>(total_size));

  char* const begin = &result[0];
  char* out = begin;
  for (absl::string_view piece : pieces) {
    const size_t this_size = piece.size();
    if (this_size != 0) {
      memcpy(out, piece.data(), this_size);
      out += this_size;
    }
  }
  assert(out == begin + result.size());
  return result;
}

// It's possible to call StrAppend with an absl::string_view that is itself a
// fragment of the string we're appending to.  However the results of this are
// random. Therefore, check for this in debug mode.  Use unsigned math so we
// only have to do one comparison. Note, there's an exception case: appending an
// empty string is always allowed.
#define ASSERT_NO_OVERLAP(dest, src) \
  assert(((src).size() == 0) ||      \
         (uintptr_t((src).data() - (dest).data()) > uintptr_t((dest).size())))

void AppendPieces(absl::Nonnull<std::string*> dest,
                  std::initializer_list<absl::string_view> pieces) {
  size_t old_size = dest->size();
  size_t to_append = 0;
  for (absl::string_view piece : pieces) {
    ASSERT_NO_OVERLAP(*dest, piece);
    to_append += piece.size();
  }
  STLStringAppendUninitializedAmortized(dest, to_append);

  char* const begin = &(*dest)[0];
  char* out = begin + old_size;
  for (absl::string_view piece : pieces) {
    const size_t this_size = piece.size();
    if (this_size != 0) {
      memcpy(out, piece.data(), this_size);
      out += this_size;
    }
  }
  assert(out == begin + dest->size());
}

}  // namespace strings_internal

void StrAppend(absl::Nonnull<std::string*> dest, const AlphaNum& a) {
  ASSERT_NO_OVERLAP(*dest, a);
  std::string::size_type old_size = dest->size();
  STLStringAppendUninitializedAmortized(dest, a.size());
  char* const begin = &(*dest)[0];
  char* out = begin + old_size;
  out = Append(out, a);
  assert(out == begin + dest->size());
}

void StrAppend(absl::Nonnull<std::string*> dest, const AlphaNum& a,
               const AlphaNum& b) {
  ASSERT_NO_OVERLAP(*dest, a);
  ASSERT_NO_OVERLAP(*dest, b);
  std::string::size_type old_size = dest->size();
  STLStringAppendUninitializedAmortized(dest, a.size() + b.size());
  char* const begin = &(*dest)[0];
  char* out = begin + old_size;
  out = Append(out, a);
  out = Append(out, b);
  assert(out == begin + dest->size());
}

void StrAppend(absl::Nonnull<std::string*> dest, const AlphaNum& a,
               const AlphaNum& b, const AlphaNum& c) {
  ASSERT_NO_OVERLAP(*dest, a);
  ASSERT_NO_OVERLAP(*dest, b);
  ASSERT_NO_OVERLAP(*dest, c);
  std::string::size_type old_size = dest->size();
  STLStringAppendUninitializedAmortized(dest, a.size() + b.size() + c.size());
  char* const begin = &(*dest)[0];
  char* out = begin + old_size;
  out = Append(out, a);
  out = Append(out, b);
  out = Append(out, c);
  assert(out == begin + dest->size());
}

void StrAppend(absl::Nonnull<std::string*> dest, const AlphaNum& a,
               const AlphaNum& b, const AlphaNum& c, const AlphaNum& d) {
  ASSERT_NO_OVERLAP(*dest, a);
  ASSERT_NO_OVERLAP(*dest, b);
  ASSERT_NO_OVERLAP(*dest, c);
  ASSERT_NO_OVERLAP(*dest, d);
  std::string::size_type old_size = dest->size();
  STLStringAppendUninitializedAmortized(
      dest, a.size() + b.size() + c.size() + d.size());
  char* const begin = &(*dest)[0];
  char* out = begin + old_size;
  out = Append(out, a);
  out = Append(out, b);
  out = Append(out, c);
  out = Append(out, d);
  assert(out == begin + dest->size());
}

ABSL_NAMESPACE_END
}  // namespace absl
