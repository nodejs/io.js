// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "span.h"

#include <algorithm>

namespace crdtp {

bool SpanLessThan(span<uint8_t> x, span<uint8_t> y) noexcept {
  auto min_size = std::min(x.size(), y.size());
  const int r = min_size == 0 ? 0 : memcmp(x.data(), y.data(), min_size);
  return (r < 0) || (r == 0 && x.size() < y.size());
}

bool SpanEquals(span<uint8_t> x, span<uint8_t> y) noexcept {
  auto len = x.size();
  if (len != y.size())
    return false;
  return x.data() == y.data() || len == 0 ||
         std::memcmp(x.data(), y.data(), len) == 0;
}

bool SpanLessThan(span<char> x, span<char> y) noexcept {
  auto min_size = std::min(x.size(), y.size());
  const int r = min_size == 0 ? 0 : memcmp(x.data(), y.data(), min_size);
  return (r < 0) || (r == 0 && x.size() < y.size());
}

bool SpanEquals(span<char> x, span<char> y) noexcept {
  auto len = x.size();
  if (len != y.size())
    return false;
  return x.data() == y.data() || len == 0 ||
         std::memcmp(x.data(), y.data(), len) == 0;
}

}  // namespace crdtp
