// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_
#define V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_

#include "src/base/template-meta-programming/string-literal.h"
#include "src/compiler/zone-stats.h"

namespace v8::internal::compiler::turboshaft {

// In debug builds, `ZoneWithNamePointer` is a lightweight wrapper around a raw
// pointer to a zone-allocated object that encodes the identity of the zone (in
// terms of the zone's name) in its C++ type. This makes it more explicit what
// the lifetime of the respective object is (aka until the corresponding zone is
// gone) and provides an additional layer of safety against misuse with other
// pointer types. Such pointers are typically created by the respective zone.
// Example:
//
//   ZoneWithName<kGraphZoneName> graph_zone;
//   ZoneWithNamePointer<Graph, kGraphZoneName> graph = graph_zone.New<Graph>();
//   foo(graph_zone, graph);
//
// Both `ZoneWithName` as well as `ZoneWithNamePointer` will implicitly convert
// to the underlying raw `Zone*` and `Graph*` to make its use as smooth as
// possible, even when `foo`'s arguments expects raw types. NOTE: In release
// builds, `ZoneWithNamePointer<T, Name>` is merely an alias to `T*`.
#if defined(DEBUG) && defined(HAS_CPP_CLASS_TYPES_AS_TEMPLATE_ARGS)
template <typename T, base::tmp::StringLiteral Name>
class ZoneWithNamePointerImpl final {
 public:
  using pointer_type = T*;

  ZoneWithNamePointerImpl() = default;
  ZoneWithNamePointerImpl(std::nullptr_t)  // NOLINT(runtime/explicit)
      : ptr_(nullptr) {}
  explicit ZoneWithNamePointerImpl(pointer_type ptr) : ptr_(ptr) {}

  ZoneWithNamePointerImpl(const ZoneWithNamePointerImpl&) V8_NOEXCEPT = default;
  ZoneWithNamePointerImpl(ZoneWithNamePointerImpl&&) V8_NOEXCEPT = default;
  template <typename U>
  ZoneWithNamePointerImpl(const ZoneWithNamePointerImpl<U, Name>& other)
      V8_NOEXCEPT  // NOLINT(runtime/explicit)
    requires(std::is_convertible_v<U*, pointer_type>)
      : ptr_(static_cast<U*>(other)) {}
  ZoneWithNamePointerImpl& operator=(const ZoneWithNamePointerImpl&)
      V8_NOEXCEPT = default;
  ZoneWithNamePointerImpl& operator=(ZoneWithNamePointerImpl&&)
      V8_NOEXCEPT = default;
  template <typename U>
  ZoneWithNamePointerImpl& operator=(
      const ZoneWithNamePointerImpl<U, Name>& other) V8_NOEXCEPT
    requires(std::is_convertible_v<U*, pointer_type>)
  {
    ptr_ = static_cast<U*>(other);
  }

  operator pointer_type() const { return get(); }  // NOLINT(runtime/explicit)
  T& operator*() const { return *get(); }
  pointer_type operator->() { return get(); }

 private:
  pointer_type get() const { return ptr_; }

  pointer_type ptr_ = pointer_type{};
};

template <typename T, base::tmp::StringLiteral Name>
using ZoneWithNamePointer = ZoneWithNamePointerImpl<T, Name>;
#else
template <typename T, auto>
using ZoneWithNamePointer = T*;
#endif

#ifdef HAS_CPP_CLASS_TYPES_AS_TEMPLATE_ARGS
template <base::tmp::StringLiteral Name>
#else
template <auto Name>
#endif
class ZoneWithName final {
 public:
  ZoneWithName(ZoneStats* pool, const char* name,
               bool support_zone_compression = false)
      : scope_(pool, name, support_zone_compression) {
#ifdef HAS_CPP_CLASS_TYPES_AS_TEMPLATE_ARGS
    DCHECK_EQ(std::strcmp(name, Name.c_str()), 0);
#endif
  }

  ZoneWithName(const ZoneWithName&) = delete;
  ZoneWithName(ZoneWithName&& other) V8_NOEXCEPT
      : scope_(std::move(other.scope_)) {}
  ZoneWithName& operator=(const ZoneWithName&) = delete;
  ZoneWithName& operator=(ZoneWithName&& other) V8_NOEXCEPT {
    scope_ = std::move(other.scope_);
    return *this;
  }

  template <typename T, typename... Args>
  ZoneWithNamePointer<T, Name> New(Args&&... args) {
    return ZoneWithNamePointer<T, Name>{
        get()->template New<T>(std::forward<Args>(args)...)};
  }

  template <typename T>
  ZoneWithNamePointer<T, Name> AllocateArray(size_t length) {
    return ZoneWithNamePointer<T, Name>{
        get()->template AllocateArray<T>(length)};
  }

  Zone* get() { return scope_.zone(); }
  operator Zone*() { return get(); }  // NOLINT(runtime/explicit)
  Zone* operator->() { return get(); }

  void Destroy() { scope_.Destroy(); }

 private:
  // NOTE: `ZoneStats::Scope` actually allocates a new zone.
  ZoneStats::Scope scope_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_
