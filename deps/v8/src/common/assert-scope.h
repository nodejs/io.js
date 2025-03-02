// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_ASSERT_SCOPE_H_
#define V8_COMMON_ASSERT_SCOPE_H_

#include <stdint.h>

#include <optional>

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Isolate;

enum PerThreadAssertType {
  // Dummy type for indicating a valid PerThreadAsserts data. This is
  // represented by an always-on bit, and is cleared when a scope's saved data
  // is zeroed -- it should never be set or cleared on the actual per-thread
  // data by a scope.
  ASSERT_TYPE_IS_VALID_MARKER,

  SAFEPOINTS_ASSERT,
  HEAP_ALLOCATION_ASSERT,
  HANDLE_ALLOCATION_ASSERT,
  HANDLE_DEREFERENCE_ASSERT,
  HANDLE_USAGE_ON_ALL_THREADS_ASSERT,
  CODE_DEPENDENCY_CHANGE_ASSERT,
  CODE_ALLOCATION_ASSERT,
  // Dummy type for disabling GC mole.
  GC_MOLE,
  POSITION_INFO_SLOW_ASSERT,
};

using PerThreadAsserts = base::EnumSet<PerThreadAssertType, uint32_t>;

// Empty assert scope, used for debug-only scopes in release mode so that
// the release-enabled PerThreadAssertScope is always an alias for, or a
// subclass of PerThreadAssertScopeDebugOnly, and can be used in place of it.
// This class is also templated so that it still has distinct instances for each
// debug scope -- this is necessary for GCMole to be able to recognise
// DisableGCMole scopes as distinct from other assert scopes.
template <bool kAllow, PerThreadAssertType... kTypes>
class V8_NODISCARD PerThreadAssertScopeEmpty {
 public:
  // Define a constructor to avoid unused variable warnings.
  // NOLINTNEXTLINE
  PerThreadAssertScopeEmpty() {}
  void Release() {}
};

template <bool kAllow, PerThreadAssertType... kTypes>
class V8_NODISCARD PerThreadAssertScope
    : public PerThreadAssertScopeEmpty<kAllow, kTypes...> {
 public:
  V8_EXPORT_PRIVATE PerThreadAssertScope();
  V8_EXPORT_PRIVATE ~PerThreadAssertScope();

  PerThreadAssertScope(const PerThreadAssertScope&) = delete;
  PerThreadAssertScope& operator=(const PerThreadAssertScope&) = delete;

  V8_EXPORT_PRIVATE static bool IsAllowed();

  void Release();

 private:
  PerThreadAsserts old_data_;
};

// Per-isolate assert scopes.

#define PER_ISOLATE_DCHECK_TYPE(V, enable)                              \
  /* Scope to document where we do not expect javascript execution. */  \
  /* Scope to introduce an exception to DisallowJavascriptExecution. */ \
  V(AllowJavascriptExecution, DisallowJavascriptExecution,              \
    javascript_execution_assert, enable)                                \
  /* Scope to document where we do not expect deoptimization. */        \
  /* Scope to introduce an exception to DisallowDeoptimization. */      \
  V(AllowDeoptimization, DisallowDeoptimization, deoptimization_assert, \
    enable)                                                             \
  /* Scope to document where we do not expect deoptimization. */        \
  /* Scope to introduce an exception to DisallowDeoptimization. */      \
  V(AllowCompilation, DisallowCompilation, compilation_assert, enable)  \
  /* Scope to document where we do not expect exceptions. */            \
  /* Scope to introduce an exception to DisallowExceptions. */          \
  V(AllowExceptions, DisallowExceptions, no_exception_assert, enable)

#define PER_ISOLATE_CHECK_TYPE(V, enable)                                    \
  /* Scope in which javascript execution leads to exception being thrown. */ \
  /* Scope to introduce an exception to ThrowOnJavascriptExecution. */       \
  V(NoThrowOnJavascriptExecution, ThrowOnJavascriptExecution,                \
    javascript_execution_throws, enable)                                     \
  /* Scope in which javascript execution causes dumps. */                    \
  /* Scope in which javascript execution doesn't cause dumps. */             \
  V(NoDumpOnJavascriptExecution, DumpOnJavascriptExecution,                  \
    javascript_execution_dump, enable)

#define PER_ISOLATE_ASSERT_SCOPE_DECLARATION(ScopeType)              \
  class V8_NODISCARD ScopeType {                                     \
   public:                                                           \
    V8_EXPORT_PRIVATE explicit ScopeType(Isolate* isolate);          \
    ScopeType(const ScopeType&) = delete;                            \
    ScopeType& operator=(const ScopeType&) = delete;                 \
    V8_EXPORT_PRIVATE ~ScopeType();                                  \
                                                                     \
    static bool IsAllowed(Isolate* isolate);                         \
                                                                     \
    V8_EXPORT_PRIVATE static void Open(Isolate* isolate,             \
                                       bool* was_execution_allowed); \
    V8_EXPORT_PRIVATE static void Close(Isolate* isolate,            \
                                        bool was_execution_allowed); \
                                                                     \
   private:                                                          \
    Isolate* isolate_;                                               \
    bool old_data_;                                                  \
  };

#define PER_ISOLATE_ASSERT_ENABLE_SCOPE(EnableType, _1, _2, _3) \
  PER_ISOLATE_ASSERT_SCOPE_DECLARATION(EnableType)

#define PER_ISOLATE_ASSERT_DISABLE_SCOPE(_1, DisableType, _2, _3) \
  PER_ISOLATE_ASSERT_SCOPE_DECLARATION(DisableType)

PER_ISOLATE_DCHECK_TYPE(PER_ISOLATE_ASSERT_ENABLE_SCOPE, true)
PER_ISOLATE_CHECK_TYPE(PER_ISOLATE_ASSERT_ENABLE_SCOPE, true)
PER_ISOLATE_DCHECK_TYPE(PER_ISOLATE_ASSERT_DISABLE_SCOPE, false)
PER_ISOLATE_CHECK_TYPE(PER_ISOLATE_ASSERT_DISABLE_SCOPE, false)

#ifdef DEBUG
#define PER_ISOLATE_DCHECK_ENABLE_SCOPE(EnableType, DisableType, field, _)    \
  class EnableType##DebugOnly : public EnableType {                           \
   public:                                                                    \
    explicit EnableType##DebugOnly(Isolate* isolate) : EnableType(isolate) {} \
  };
#else
#define PER_ISOLATE_DCHECK_ENABLE_SCOPE(EnableType, DisableType, field, _) \
  class V8_NODISCARD EnableType##DebugOnly {                               \
   public:                                                                 \
    explicit EnableType##DebugOnly(Isolate* isolate) {}                    \
  };
#endif

#ifdef DEBUG
#define PER_ISOLATE_DCHECK_DISABLE_SCOPE(EnableType, DisableType, field, _) \
  class DisableType##DebugOnly : public DisableType {                       \
   public:                                                                  \
    explicit DisableType##DebugOnly(Isolate* isolate)                       \
        : DisableType(isolate) {}                                           \
  };
#else
#define PER_ISOLATE_DCHECK_DISABLE_SCOPE(EnableType, DisableType, field, _) \
  class V8_NODISCARD DisableType##DebugOnly {                               \
   public:                                                                  \
    explicit DisableType##DebugOnly(Isolate* isolate) {}                    \
  };
#endif

PER_ISOLATE_DCHECK_TYPE(PER_ISOLATE_DCHECK_ENABLE_SCOPE, true)
PER_ISOLATE_DCHECK_TYPE(PER_ISOLATE_DCHECK_DISABLE_SCOPE, false)

#ifdef DEBUG
template <bool kAllow, PerThreadAssertType... kTypes>
using PerThreadAssertScopeDebugOnly = PerThreadAssertScope<kAllow, kTypes...>;
#else
template <bool kAllow, PerThreadAssertType... kTypes>
using PerThreadAssertScopeDebugOnly =
    PerThreadAssertScopeEmpty<kAllow, kTypes...>;
#endif

// Per-thread assert scopes.

// Scope to document where we do not expect handles to be created.
using DisallowHandleAllocation =
    PerThreadAssertScopeDebugOnly<false, HANDLE_ALLOCATION_ASSERT>;

// Scope to introduce an exception to DisallowHandleAllocation.
using AllowHandleAllocation =
    PerThreadAssertScopeDebugOnly<true, HANDLE_ALLOCATION_ASSERT>;

// Scope to document where we do not expect safepoints to be entered.
using DisallowSafepoints =
    PerThreadAssertScopeDebugOnly<false, SAFEPOINTS_ASSERT>;

// Scope to introduce an exception to DisallowSafepoints.
using AllowSafepoints = PerThreadAssertScopeDebugOnly<true, SAFEPOINTS_ASSERT>;

// Scope to document where we do not expect any allocation.
using DisallowHeapAllocation =
    PerThreadAssertScopeDebugOnly<false, HEAP_ALLOCATION_ASSERT>;

// Scope to introduce an exception to DisallowHeapAllocation.
using AllowHeapAllocation =
    PerThreadAssertScopeDebugOnly<true, HEAP_ALLOCATION_ASSERT>;

// Like AllowHeapAllocation, but enabled in release builds.
using AllowHeapAllocationInRelease =
    PerThreadAssertScope<true, HEAP_ALLOCATION_ASSERT>;

// Scope to document where we do not expect any handle dereferences.
using DisallowHandleDereference =
    PerThreadAssertScopeDebugOnly<false, HANDLE_DEREFERENCE_ASSERT>;

// Scope to introduce an exception to DisallowHandleDereference.
using AllowHandleDereference =
    PerThreadAssertScopeDebugOnly<true, HANDLE_DEREFERENCE_ASSERT>;

// Explicitly allow handle dereference and creation for all threads/isolates on
// one particular thread.
using AllowHandleUsageOnAllThreads =
    PerThreadAssertScopeDebugOnly<true, HANDLE_USAGE_ON_ALL_THREADS_ASSERT>;

// Scope to document where we do not expect code dependencies to change.
using DisallowCodeDependencyChange =
    PerThreadAssertScopeDebugOnly<false, CODE_DEPENDENCY_CHANGE_ASSERT>;

// Scope to introduce an exception to DisallowCodeDependencyChange.
using AllowCodeDependencyChange =
    PerThreadAssertScopeDebugOnly<true, CODE_DEPENDENCY_CHANGE_ASSERT>;

// Scope to document where we do not expect code to be allocated.
using DisallowCodeAllocation =
    PerThreadAssertScopeDebugOnly<false, CODE_ALLOCATION_ASSERT>;

// Scope to introduce an exception to DisallowCodeAllocation.
using AllowCodeAllocation =
    PerThreadAssertScopeDebugOnly<true, CODE_ALLOCATION_ASSERT>;

// Scope to document where we do not expect garbage collections. It differs from
// DisallowHeapAllocation by also forbidding safepoints.
using DisallowGarbageCollection =
    PerThreadAssertScopeDebugOnly<false, SAFEPOINTS_ASSERT,
                                  HEAP_ALLOCATION_ASSERT>;

// Like DisallowGarbageCollection, but enabled in release builds.
using DisallowGarbageCollectionInRelease =
    PerThreadAssertScope<false, SAFEPOINTS_ASSERT, HEAP_ALLOCATION_ASSERT>;

// Scope to skip gc mole verification in places where we do tricky raw
// work.
using DisableGCMole = PerThreadAssertScopeDebugOnly<false, GC_MOLE>;

// Scope to ensure slow path for obtaining position info is not called
using DisallowPositionInfoSlow =
    PerThreadAssertScopeDebugOnly<false, POSITION_INFO_SLOW_ASSERT>;

// Scope to add an exception to disallowing position info slow path
using AllowPositionInfoSlow =
    PerThreadAssertScopeDebugOnly<true, POSITION_INFO_SLOW_ASSERT>;

// The DISALLOW_GARBAGE_COLLECTION macro can be used to define a
// DisallowGarbageCollection field in classes that isn't present in release
// builds.
#ifdef DEBUG
#define DISALLOW_GARBAGE_COLLECTION(name) DisallowGarbageCollection name;
#else
#define DISALLOW_GARBAGE_COLLECTION(name)
#endif

// Scope to introduce an exception to DisallowGarbageCollection.
using AllowGarbageCollection =
    PerThreadAssertScopeDebugOnly<true, SAFEPOINTS_ASSERT,
                                  HEAP_ALLOCATION_ASSERT>;

// Like AllowGarbageCollection, but enabled in release builds.
using AllowGarbageCollectionInRelease =
    PerThreadAssertScope<true, SAFEPOINTS_ASSERT, HEAP_ALLOCATION_ASSERT>;

// Scope to document where we do not expect any access to the heap.
using DisallowHeapAccess = PerThreadAssertScopeDebugOnly<
    false, CODE_DEPENDENCY_CHANGE_ASSERT, HANDLE_DEREFERENCE_ASSERT,
    HANDLE_ALLOCATION_ASSERT, HEAP_ALLOCATION_ASSERT>;

// Scope to introduce an exception to DisallowHeapAccess.
using AllowHeapAccess = PerThreadAssertScopeDebugOnly<
    true, CODE_DEPENDENCY_CHANGE_ASSERT, HANDLE_DEREFERENCE_ASSERT,
    HANDLE_ALLOCATION_ASSERT, HEAP_ALLOCATION_ASSERT>;

class DisallowHeapAccessIf {
 public:
  explicit DisallowHeapAccessIf(bool condition) {
    if (condition) maybe_disallow_.emplace();
  }

 private:
  std::optional<DisallowHeapAccess> maybe_disallow_;
};

// Like MutexGuard but also asserts that no garbage collection happens while
// we're holding the mutex.
class V8_NODISCARD NoGarbageCollectionMutexGuard {
 public:
  explicit NoGarbageCollectionMutexGuard(base::Mutex* mutex)
      : guard_(mutex), mutex_(mutex), no_gc_(std::in_place) {}

  void Unlock() {
    mutex_->Unlock();
    no_gc_.reset();
  }
  void Lock() {
    mutex_->Lock();
    no_gc_.emplace();
  }

 private:
  base::MutexGuard guard_;
  base::Mutex* mutex_;
  std::optional<DisallowGarbageCollection> no_gc_;
};

// Explicit instantiation declarations.
extern template class PerThreadAssertScope<false, HEAP_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<true, HEAP_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<false, SAFEPOINTS_ASSERT>;
extern template class PerThreadAssertScope<true, SAFEPOINTS_ASSERT>;
extern template class PerThreadAssertScope<false, HANDLE_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<true, HANDLE_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<false, HANDLE_DEREFERENCE_ASSERT>;
extern template class PerThreadAssertScope<true, HANDLE_DEREFERENCE_ASSERT>;
extern template class PerThreadAssertScope<false,
                                           CODE_DEPENDENCY_CHANGE_ASSERT>;
extern template class PerThreadAssertScope<true, CODE_DEPENDENCY_CHANGE_ASSERT>;
extern template class PerThreadAssertScope<false, CODE_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<true, CODE_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<false, GC_MOLE>;
extern template class PerThreadAssertScope<false, POSITION_INFO_SLOW_ASSERT>;
extern template class PerThreadAssertScope<true, POSITION_INFO_SLOW_ASSERT>;
extern template class PerThreadAssertScope<false, SAFEPOINTS_ASSERT,
                                           HEAP_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<true, SAFEPOINTS_ASSERT,
                                           HEAP_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<
    false, CODE_DEPENDENCY_CHANGE_ASSERT, HANDLE_DEREFERENCE_ASSERT,
    HANDLE_ALLOCATION_ASSERT, HEAP_ALLOCATION_ASSERT>;
extern template class PerThreadAssertScope<
    true, CODE_DEPENDENCY_CHANGE_ASSERT, HANDLE_DEREFERENCE_ASSERT,
    HANDLE_ALLOCATION_ASSERT, HEAP_ALLOCATION_ASSERT>;

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_ASSERT_SCOPE_H_
