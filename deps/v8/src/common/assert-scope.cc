// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/assert-scope.h"

#include "src/base/enum-set.h"
#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

namespace {

// All asserts are allowed by default except for one, and the cleared bit is not
// set.
constexpr PerThreadAsserts kInitialValue =
    ~PerThreadAsserts{HANDLE_USAGE_ON_ALL_THREADS_ASSERT};
static_assert(kInitialValue.contains(ASSERT_TYPE_IS_VALID_MARKER));

// The cleared value is the only one where ASSERT_TYPE_IS_VALID_MARKER is not
// set.
constexpr PerThreadAsserts kClearedValue = PerThreadAsserts{};
static_assert(!kClearedValue.contains(ASSERT_TYPE_IS_VALID_MARKER));

// Thread-local storage for assert data.
thread_local PerThreadAsserts current_per_thread_assert_data(kInitialValue);

}  // namespace

template <bool kAllow, PerThreadAssertType... kTypes>
PerThreadAssertScope<kAllow, kTypes...>::PerThreadAssertScope()
    : old_data_(current_per_thread_assert_data) {
  static_assert(((kTypes != ASSERT_TYPE_IS_VALID_MARKER) && ...),
                "PerThreadAssertScope types should not include the "
                "ASSERT_TYPE_IS_VALID_MARKER");
  DCHECK(old_data_.contains(ASSERT_TYPE_IS_VALID_MARKER));
  if (kAllow) {
    current_per_thread_assert_data = old_data_ | PerThreadAsserts({kTypes...});
  } else {
    current_per_thread_assert_data = old_data_ - PerThreadAsserts({kTypes...});
  }
}

template <bool kAllow, PerThreadAssertType... kTypes>
PerThreadAssertScope<kAllow, kTypes...>::~PerThreadAssertScope() {
  Release();
}

template <bool kAllow, PerThreadAssertType... kTypes>
void PerThreadAssertScope<kAllow, kTypes...>::Release() {
  if (old_data_ == kClearedValue) return;
  current_per_thread_assert_data = old_data_;
  old_data_ = kClearedValue;
}

// static
template <bool kAllow, PerThreadAssertType... kTypes>
bool PerThreadAssertScope<kAllow, kTypes...>::IsAllowed() {
  return current_per_thread_assert_data.contains_all({kTypes...});
}

#define PER_ISOLATE_ASSERT_SCOPE_DEFINITION(ScopeType, field, enable)      \
  ScopeType::ScopeType(Isolate* isolate)                                   \
      : isolate_(isolate), old_data_(isolate->field()) {                   \
    DCHECK_NOT_NULL(isolate);                                              \
    isolate_->set_##field(enable);                                         \
  }                                                                        \
                                                                           \
  ScopeType::~ScopeType() { isolate_->set_##field(old_data_); }            \
                                                                           \
  /* static */                                                             \
  bool ScopeType::IsAllowed(Isolate* isolate) { return isolate->field(); } \
                                                                           \
  /* static */                                                             \
  void ScopeType::Open(Isolate* isolate, bool* was_execution_allowed) {    \
    DCHECK_NOT_NULL(isolate);                                              \
    DCHECK_NOT_NULL(was_execution_allowed);                                \
    *was_execution_allowed = isolate->field();                             \
    isolate->set_##field(enable);                                          \
  }                                                                        \
  /* static */                                                             \
  void ScopeType::Close(Isolate* isolate, bool was_execution_allowed) {    \
    DCHECK_NOT_NULL(isolate);                                              \
    isolate->set_##field(was_execution_allowed);                           \
  }

#define PER_ISOLATE_ASSERT_ENABLE_SCOPE_DEFINITION(EnableType, _, field, \
                                                   enable)               \
  PER_ISOLATE_ASSERT_SCOPE_DEFINITION(EnableType, field, enable)

#define PER_ISOLATE_ASSERT_DISABLE_SCOPE_DEFINITION(_, DisableType, field, \
                                                    enable)                \
  PER_ISOLATE_ASSERT_SCOPE_DEFINITION(DisableType, field, enable)

PER_ISOLATE_DCHECK_TYPE(PER_ISOLATE_ASSERT_ENABLE_SCOPE_DEFINITION, true)
PER_ISOLATE_CHECK_TYPE(PER_ISOLATE_ASSERT_ENABLE_SCOPE_DEFINITION, true)
PER_ISOLATE_DCHECK_TYPE(PER_ISOLATE_ASSERT_DISABLE_SCOPE_DEFINITION, false)
PER_ISOLATE_CHECK_TYPE(PER_ISOLATE_ASSERT_DISABLE_SCOPE_DEFINITION, false)

// -----------------------------------------------------------------------------
// Instantiations.

template class PerThreadAssertScope<false, HEAP_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<true, HEAP_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<false, SAFEPOINTS_ASSERT>;
template class PerThreadAssertScope<true, SAFEPOINTS_ASSERT>;
template class PerThreadAssertScope<false, HANDLE_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<true, HANDLE_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<false, HANDLE_DEREFERENCE_ASSERT>;
template class PerThreadAssertScope<true, HANDLE_DEREFERENCE_ASSERT>;
template class PerThreadAssertScope<true, HANDLE_USAGE_ON_ALL_THREADS_ASSERT>;
template class PerThreadAssertScope<false, CODE_DEPENDENCY_CHANGE_ASSERT>;
template class PerThreadAssertScope<true, CODE_DEPENDENCY_CHANGE_ASSERT>;
template class PerThreadAssertScope<false, CODE_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<true, CODE_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<false, GC_MOLE>;
template class PerThreadAssertScope<false, POSITION_INFO_SLOW_ASSERT>;
template class PerThreadAssertScope<true, POSITION_INFO_SLOW_ASSERT>;
template class PerThreadAssertScope<false, SAFEPOINTS_ASSERT,
                                    HEAP_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<true, SAFEPOINTS_ASSERT,
                                    HEAP_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<
    false, CODE_DEPENDENCY_CHANGE_ASSERT, HANDLE_DEREFERENCE_ASSERT,
    HANDLE_ALLOCATION_ASSERT, HEAP_ALLOCATION_ASSERT>;
template class PerThreadAssertScope<
    true, CODE_DEPENDENCY_CHANGE_ASSERT, HANDLE_DEREFERENCE_ASSERT,
    HANDLE_ALLOCATION_ASSERT, HEAP_ALLOCATION_ASSERT>;

static_assert(Internals::kDisallowGarbageCollectionAlign ==
              alignof(DisallowGarbageCollectionInRelease));
static_assert(Internals::kDisallowGarbageCollectionSize ==
              sizeof(DisallowGarbageCollectionInRelease));

}  // namespace internal
}  // namespace v8
