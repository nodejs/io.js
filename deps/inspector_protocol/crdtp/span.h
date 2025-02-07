// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_SPAN_H_
#define CRDTP_SPAN_H_

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <string>

#include "export.h"

namespace crdtp {
// =============================================================================
// span - sequence of bytes
// =============================================================================

// This template is similar to std::span, which will be included in C++20.
template <typename T>
class span {
 public:
  using index_type = size_t;

  constexpr span() : data_(nullptr), size_(0) {}
  constexpr span(const T* data, index_type size) : data_(data), size_(size) {}

  constexpr const T* data() const { return data_; }

  constexpr const T* begin() const { return data_; }
  constexpr const T* end() const { return data_ + size_; }

  constexpr const T& operator[](index_type idx) const { return data_[idx]; }

  constexpr span<T> subspan(index_type offset, index_type count) const {
    return span(data_ + offset, count);
  }

  constexpr span<T> subspan(index_type offset) const {
    return span(data_ + offset, size_ - offset);
  }

  constexpr bool empty() const { return size_ == 0; }

  constexpr index_type size() const { return size_; }
  constexpr index_type size_bytes() const { return size_ * sizeof(T); }

 private:
  const T* data_;
  index_type size_;
};

template <size_t N>
constexpr span<char> MakeSpan(const char (&str)[N]) {
  return span<char>(str, N - 1);
}

template <size_t N>
constexpr span<uint8_t> SpanFrom(const char (&str)[N]) {
  return span<uint8_t>(reinterpret_cast<const uint8_t*>(str), N - 1);
}

constexpr inline span<uint8_t> SpanFrom(const char* str) {
  return str ? span<uint8_t>(reinterpret_cast<const uint8_t*>(str), strlen(str))
             : span<uint8_t>();
}

inline span<uint8_t> SpanFrom(const std::string& v) {
  return span<uint8_t>(reinterpret_cast<const uint8_t*>(v.data()), v.size());
}

// This SpanFrom routine works for std::vector<uint8_t> and
// std::vector<uint16_t>, but also for base::span<const uint8_t> in Chromium.
template <typename C,
          typename = std::enable_if_t<
              std::is_unsigned<typename C::value_type>{} &&
              std::is_member_function_pointer<decltype(&C::size)>{}>>
inline span<typename C::value_type> SpanFrom(const C& v) {
  return span<typename C::value_type>(v.data(), v.size());
}

// Less than / equality comparison functions for sorting / searching for byte
// spans.
CRDTP_EXPORT bool SpanLessThan(span<uint8_t> x, span<uint8_t> y) noexcept;
CRDTP_EXPORT bool SpanEquals(span<uint8_t> x, span<uint8_t> y) noexcept;

// Less than / equality comparison functions for sorting / searching for byte
// spans.
CRDTP_EXPORT bool SpanLessThan(span<char> x, span<char> y) noexcept;
CRDTP_EXPORT bool SpanEquals(span<char> x, span<char> y) noexcept;

struct SpanLt {
  bool operator()(span<uint8_t> l, span<uint8_t> r) const {
    return SpanLessThan(l, r);
  }
};
}  // namespace crdtp

#endif  // CRDTP_SPAN_H_
