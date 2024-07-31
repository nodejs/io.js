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

#ifndef ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_
#define ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"

#if defined(_MSC_VER) && !defined(__clang__)
#define ABSL_HAVE_WORKING_CONSTEXPR_STATIC_INIT 0
#else
#define ABSL_HAVE_WORKING_CONSTEXPR_STATIC_INIT 1
#endif

#if defined(_MSC_VER)
#define ABSL_HAVE_WORKING_ATOMIC_POINTER 0
#else
#define ABSL_HAVE_WORKING_ATOMIC_POINTER 1
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

template <typename T>
class AtomicHook;

// To workaround AtomicHook not being constant-initializable on some platforms,
// prefer to annotate instances with `ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES`
// instead of `ABSL_CONST_INIT`.
#if ABSL_HAVE_WORKING_CONSTEXPR_STATIC_INIT
#define ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES ABSL_CONST_INIT
#else
#define ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES
#endif

// `AtomicHook` is a helper class, templatized on a raw function pointer type,
// for implementing Abseil customization hooks.  It is a callable object that
// dispatches to the registered hook.  Objects of type `AtomicHook` must have
// static or thread storage duration.
//
// A default constructed object performs a no-op (and returns a default
// constructed object) if no hook has been registered.
//
// Hooks can be pre-registered via constant initialization, for example:
//
// ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES static AtomicHook<void(*)()>
//     my_hook(DefaultAction);
//
// and then changed at runtime via a call to `Store()`.
//
// Reads and writes guarantee memory_order_acquire/memory_order_release
// semantics.
template <typename ReturnType, typename... Args>
class AtomicHook<ReturnType (*)(Args...)> {
 public:
  using FnPtr = ReturnType (*)(Args...);

  // Constructs an object that by default performs a no-op (and
  // returns a default constructed object) when no hook as been registered.
  constexpr AtomicHook() : AtomicHook(DummyFunction) {}

  // Constructs an object that by default dispatches to/returns the
  // pre-registered default_fn when no hook has been registered at runtime.
#if ABSL_HAVE_WORKING_ATOMIC_POINTER && ABSL_HAVE_WORKING_CONSTEXPR_STATIC_INIT
  explicit constexpr AtomicHook(FnPtr default_fn)
      : hook_(default_fn), default_fn_(default_fn) {}
#elif ABSL_HAVE_WORKING_CONSTEXPR_STATIC_INIT
  explicit constexpr AtomicHook(FnPtr default_fn)
      : hook_(kUninitialized), default_fn_(default_fn) {}
#else
  // As of January 2020, on all known versions of MSVC this constructor runs in
  // the global constructor sequence.  If `Store()` is called by a dynamic
  // initializer, we want to preserve the value, even if this constructor runs
  // after the call to `Store()`.  If not, `hook_` will be
  // zero-initialized by the linker and we have no need to set it.
  // https://developercommunity.visualstudio.com/content/problem/336946/class-with-constexpr-constructor-not-using-static.html
  explicit constexpr AtomicHook(FnPtr default_fn)
      : /* hook_(deliberately omitted), */ default_fn_(default_fn) {
    static_assert(kUninitialized == 0, "here we rely on zero-initialization");
  }
#endif

  // Stores the provided function pointer as the value for this hook.
  //
  // This is intended to be called once.  Multiple calls are legal only if the
  // same function pointer is provided for each call.  The store is implemented
  // as a memory_order_release operation, and read accesses are implemented as
  // memory_order_acquire.
  void Store(FnPtr fn) {
    bool success = DoStore(fn);
    static_cast<void>(success);
    assert(success);
  }

  // Invokes the registered callback.  If no callback has yet been registered, a
  // default-constructed object of the appropriate type is returned instead.
  template <typename... CallArgs>
  ReturnType operator()(CallArgs&&... args) const {
    return DoLoad()(std::forward<CallArgs>(args)...);
  }

  // Returns the registered callback, or nullptr if none has been registered.
  // Useful if client code needs to conditionalize behavior based on whether a
  // callback was registered.
  //
  // Note that atomic_hook.Load()() and atomic_hook() have different semantics:
  // operator()() will perform a no-op if no callback was registered, while
  // Load()() will dereference a null function pointer.  Prefer operator()() to
  // Load()() unless you must conditionalize behavior on whether a hook was
  // registered.
  FnPtr Load() const {
    FnPtr ptr = DoLoad();
    return (ptr == DummyFunction) ? nullptr : ptr;
  }

 private:
  static ReturnType DummyFunction(Args...) {
    return ReturnType();
  }

  // Current versions of MSVC (as of September 2017) have a broken
  // implementation of std::atomic<T*>:  Its constructor attempts to do the
  // equivalent of a reinterpret_cast in a constexpr context, which is not
  // allowed.
  //
  // This causes an issue when building with LLVM under Windows.  To avoid this,
  // we use a less-efficient, intptr_t-based implementation on Windows.
#if ABSL_HAVE_WORKING_ATOMIC_POINTER
  // Return the stored value, or DummyFunction if no value has been stored.
  FnPtr DoLoad() const { return hook_.load(std::memory_order_acquire); }

  // Store the given value.  Returns false if a different value was already
  // stored to this object.
  bool DoStore(FnPtr fn) {
    assert(fn);
    FnPtr expected = default_fn_;
    const bool store_succeeded = hook_.compare_exchange_strong(
        expected, fn, std::memory_order_acq_rel, std::memory_order_acquire);
    const bool same_value_already_stored = (expected == fn);
    return store_succeeded || same_value_already_stored;
  }

  std::atomic<FnPtr> hook_;
#else  // !ABSL_HAVE_WORKING_ATOMIC_POINTER
  // Use a sentinel value unlikely to be the address of an actual function.
  static constexpr intptr_t kUninitialized = 0;

  static_assert(sizeof(intptr_t) >= sizeof(FnPtr),
                "intptr_t can't contain a function pointer");

  FnPtr DoLoad() const {
    const intptr_t value = hook_.load(std::memory_order_acquire);
    if (value == kUninitialized) {
      return default_fn_;
    }
    return reinterpret_cast<FnPtr>(value);
  }

  bool DoStore(FnPtr fn) {
    assert(fn);
    const auto value = reinterpret_cast<intptr_t>(fn);
    intptr_t expected = kUninitialized;
    const bool store_succeeded = hook_.compare_exchange_strong(
        expected, value, std::memory_order_acq_rel, std::memory_order_acquire);
    const bool same_value_already_stored = (expected == value);
    return store_succeeded || same_value_already_stored;
  }

  std::atomic<intptr_t> hook_;
#endif

  const FnPtr default_fn_;
};

#undef ABSL_HAVE_WORKING_ATOMIC_POINTER
#undef ABSL_HAVE_WORKING_CONSTEXPR_STATIC_INIT

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_
