// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CRDTP_MAYBE_H_
#define CRDTP_MAYBE_H_

#include <cassert>
#include <memory>

namespace crdtp {

// =============================================================================
// detail::PtrMaybe, detail::ValueMaybe, templates for optional
// pointers / values which are used in ../lib/Forward_h.template.
// =============================================================================

namespace detail {
template <typename T>
class PtrMaybe {
 public:
  PtrMaybe() = default;
  PtrMaybe(std::unique_ptr<T> value) : value_(std::move(value)) {}
  PtrMaybe(PtrMaybe&& other) noexcept : value_(std::move(other.value_)) {}
  void operator=(std::unique_ptr<T> value) { value_ = std::move(value); }

  // std::optional<>-compatible accessors (preferred).
  bool has_value() const { return !!value_; }
  operator bool() const { return has_value(); }
  const T& value() const& {
    assert(has_value());
    return *value_;
  }
  T& value() & {
    assert(has_value());
    return *value_;
  }
  T&& value() && {
    assert(has_value());
    return std::move(*value_);
  }
  const T& value_or(const T& default_value) const {
    return has_value() ? *value_ : default_value;
  }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  T&& operator*() && { return std::move(value()); }

  // Legacy Maybe<> accessors (deprecated).
  T* fromJust() const {
    assert(value_);
    return value_.get();
  }
  T* fromMaybe(T* default_value) const {
    return value_ ? value_.get() : default_value;
  }
  bool isJust() const { return value_ != nullptr; }

 private:
  std::unique_ptr<T> value_;
};

template <typename T>
class ValueMaybe {
 public:
  ValueMaybe() : is_just_(false), value_() {}
  ValueMaybe(T value) : is_just_(true), value_(std::move(value)) {}
  ValueMaybe(ValueMaybe&& other) noexcept
      : is_just_(other.is_just_), value_(std::move(other.value_)) {}
  void operator=(T value) {
    value_ = std::move(value);
    is_just_ = true;
  }

  // std::optional<>-compatible accessors (preferred).
  bool has_value() const { return is_just_; }
  operator bool() const { return has_value(); }
  const T& value() const& {
    assert(is_just_);
    return value_;
  }
  T& value() & {
    assert(is_just_);
    return value_;
  }
  T&& value() && {
    assert(is_just_);
    return *std::move(value_);
  }
  template <typename U>
  T value_or(U&& default_value) const& {
    return is_just_ ? value_ : std::forward<U>(default_value);
  }
  template <typename U>
  T value_or(U&& default_value) && {
    return is_just_ ? std::move(value_) : std::forward<U>(default_value);
  }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  T&& operator*() && { return std::move(value()); }

  // Legacy Maybe<> accessors (deprecated).
  const T& fromJust() const {
    assert(is_just_);
    return value_;
  }
  const T& fromMaybe(const T& default_value) const {
    return is_just_ ? value_ : default_value;
  }
  bool isJust() const { return is_just_; }

 private:
  bool is_just_;
  T value_;
};

template <typename T>
struct MaybeTypedef {
  typedef PtrMaybe<T> type;
};

template <>
struct MaybeTypedef<bool> {
  typedef ValueMaybe<bool> type;
};

template <>
struct MaybeTypedef<int> {
  typedef ValueMaybe<int> type;
};

template <>
struct MaybeTypedef<double> {
  typedef ValueMaybe<double> type;
};

template <>
struct MaybeTypedef<std::string> {
  typedef ValueMaybe<std::string> type;
};

}  // namespace detail

template <typename T>
using Maybe = typename detail::MaybeTypedef<T>::type;

}  // namespace crdtp

#endif  // CRDTP_MAYBE_H_
