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

#include "absl/strings/match.h"

#include <algorithm>
#include <cstdint>

#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/optimization.h"
#include "absl/numeric/bits.h"
#include "absl/strings/ascii.h"
#include "absl/strings/internal/memutil.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

bool EqualsIgnoreCase(absl::string_view piece1,
                      absl::string_view piece2) noexcept {
  return (piece1.size() == piece2.size() &&
          0 == absl::strings_internal::memcasecmp(piece1.data(), piece2.data(),
                                                  piece1.size()));
  // memcasecmp uses absl::ascii_tolower().
}

bool StrContainsIgnoreCase(absl::string_view haystack,
                           absl::string_view needle) noexcept {
  while (haystack.size() >= needle.size()) {
    if (StartsWithIgnoreCase(haystack, needle)) return true;
    haystack.remove_prefix(1);
  }
  return false;
}

bool StrContainsIgnoreCase(absl::string_view haystack,
                           char needle) noexcept {
  char upper_needle = absl::ascii_toupper(static_cast<unsigned char>(needle));
  char lower_needle = absl::ascii_tolower(static_cast<unsigned char>(needle));
  if (upper_needle == lower_needle) {
    return StrContains(haystack, needle);
  } else {
    const char both_cstr[3] = {lower_needle, upper_needle, '\0'};
    return haystack.find_first_of(both_cstr) != absl::string_view::npos;
  }
}

bool StartsWithIgnoreCase(absl::string_view text,
                          absl::string_view prefix) noexcept {
  return (text.size() >= prefix.size()) &&
         EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

bool EndsWithIgnoreCase(absl::string_view text,
                        absl::string_view suffix) noexcept {
  return (text.size() >= suffix.size()) &&
         EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}

absl::string_view FindLongestCommonPrefix(absl::string_view a,
                                          absl::string_view b) {
  const absl::string_view::size_type limit = std::min(a.size(), b.size());
  const char* const pa = a.data();
  const char* const pb = b.data();
  absl::string_view::size_type count = (unsigned) 0;

  if (ABSL_PREDICT_FALSE(limit < 8)) {
    while (ABSL_PREDICT_TRUE(count + 2 <= limit)) {
      uint16_t xor_bytes = absl::little_endian::Load16(pa + count) ^
                           absl::little_endian::Load16(pb + count);
      if (ABSL_PREDICT_FALSE(xor_bytes != 0)) {
        if (ABSL_PREDICT_TRUE((xor_bytes & 0xff) == 0)) ++count;
        return absl::string_view(pa, count);
      }
      count += 2;
    }
    if (ABSL_PREDICT_TRUE(count != limit)) {
      if (ABSL_PREDICT_TRUE(pa[count] == pb[count])) ++count;
    }
    return absl::string_view(pa, count);
  }

  do {
    uint64_t xor_bytes = absl::little_endian::Load64(pa + count) ^
                         absl::little_endian::Load64(pb + count);
    if (ABSL_PREDICT_FALSE(xor_bytes != 0)) {
      count += static_cast<uint64_t>(absl::countr_zero(xor_bytes) >> 3);
      return absl::string_view(pa, count);
    }
    count += 8;
  } while (ABSL_PREDICT_TRUE(count + 8 < limit));

  count = limit - 8;
  uint64_t xor_bytes = absl::little_endian::Load64(pa + count) ^
                       absl::little_endian::Load64(pb + count);
  if (ABSL_PREDICT_TRUE(xor_bytes != 0)) {
    count += static_cast<uint64_t>(absl::countr_zero(xor_bytes) >> 3);
    return absl::string_view(pa, count);
  }
  return absl::string_view(pa, limit);
}

absl::string_view FindLongestCommonSuffix(absl::string_view a,
                                          absl::string_view b) {
  const absl::string_view::size_type limit = std::min(a.size(), b.size());
  if (limit == 0) return absl::string_view();

  const char* pa = a.data() + a.size() - 1;
  const char* pb = b.data() + b.size() - 1;
  absl::string_view::size_type count = (unsigned) 0;
  while (count < limit && *pa == *pb) {
    --pa;
    --pb;
    ++count;
  }

  return absl::string_view(++pa, count);
}

ABSL_NAMESPACE_END
}  // namespace absl
