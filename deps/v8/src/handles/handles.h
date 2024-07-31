// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_HANDLES_H_
#define V8_HANDLES_HANDLES_H_

#include <type_traits>

#include "src/base/functional.h"
#include "src/base/macros.h"
#include "src/common/checks.h"
#include "src/common/globals.h"
#include "src/objects/tagged.h"
#include "src/zone/zone.h"
#include "v8-handle-base.h"  // NOLINT(build/include_directory)

#ifdef V8_ENABLE_DIRECT_HANDLE
#include "src/flags/flags.h"
#endif

namespace v8 {

class HandleScope;

namespace internal {

// Forward declarations.
#ifdef V8_ENABLE_DIRECT_HANDLE
class DirectHandleBase;
#endif
class HandleScopeImplementer;
class Isolate;
class LocalHeap;
class LocalIsolate;
class TaggedIndex;
class Object;
class OrderedHashMap;
class OrderedHashSet;
class OrderedNameDictionary;
class RootVisitor;
class SmallOrderedHashMap;
class SmallOrderedHashSet;
class SmallOrderedNameDictionary;
class SwissNameDictionary;
class WasmExportedFunctionData;

constexpr Address kTaggedNullAddress = 0x1;

// ----------------------------------------------------------------------------
// Base class for Handle instantiations. Don't use directly.
class HandleBase {
 public:
  // Check if this handle refers to the exact same object as the other handle.
  V8_INLINE bool is_identical_to(const HandleBase& that) const;
#ifdef V8_ENABLE_DIRECT_HANDLE
  V8_INLINE bool is_identical_to(const DirectHandleBase& that) const;
#endif
  V8_INLINE bool is_null() const { return location_ == nullptr; }

  // Returns the raw address where this handle is stored. This should only be
  // used for hashing handles; do not ever try to dereference it.
  V8_INLINE Address address() const {
    return reinterpret_cast<Address>(location_);
  }

  // Returns the address to where the raw pointer is stored.
  // TODO(leszeks): This should probably be a const Address*, to encourage using
  // PatchValue for modifying the handle's value.
  V8_INLINE Address* location() const {
    SLOW_DCHECK(location_ == nullptr || IsDereferenceAllowed());
    return location_;
  }

 protected:
#ifdef V8_ENABLE_DIRECT_HANDLE
  friend class DirectHandleBase;
#endif

  V8_INLINE explicit HandleBase(Address* location) : location_(location) {}
  V8_INLINE explicit HandleBase(Address object, Isolate* isolate);
  V8_INLINE explicit HandleBase(Address object, LocalIsolate* isolate);
  V8_INLINE explicit HandleBase(Address object, LocalHeap* local_heap);

#ifdef DEBUG
  V8_EXPORT_PRIVATE bool IsDereferenceAllowed() const;
#else
  V8_INLINE bool IsDereferenceAllowed() const { return true; }
#endif  // DEBUG

  // This uses type Address* as opposed to a pointer type to a typed
  // wrapper class, because it doesn't point to instances of such a
  // wrapper class. Design overview: https://goo.gl/Ph4CGz
  Address* location_;
};

// ----------------------------------------------------------------------------
// A Handle provides a reference to an object that survives relocation by
// the garbage collector.
//
// Handles are only valid within a HandleScope. When a handle is created
// for an object a cell is allocated in the current HandleScope.
//
// Also note that Handles do not provide default equality comparison or hashing
// operators on purpose. Such operators would be misleading, because intended
// semantics is ambiguous between Handle location and object identity. Instead
// use either {is_identical_to} or {location} explicitly.
template <typename T>
class Handle final : public HandleBase {
 public:
  V8_INLINE Handle() : HandleBase(nullptr) {}

  V8_INLINE explicit Handle(Address* location) : HandleBase(location) {
    // TODO(jkummerow): Runtime type check here as a SLOW_DCHECK?
  }

  V8_INLINE Handle(Tagged<T> object, Isolate* isolate);
  V8_INLINE Handle(Tagged<T> object, LocalIsolate* isolate);
  V8_INLINE Handle(Tagged<T> object, LocalHeap* local_heap);

  // Allocate a new handle for the object.
  V8_INLINE static Handle<T> New(Tagged<T> object, Isolate* isolate);

  // Constructor for handling automatic up casting.
  // Ex. Handle<JSFunction> can be passed when Handle<Object> is expected.
  template <typename S, typename = std::enable_if_t<is_subtype_v<S, T>>>
  V8_INLINE Handle(Handle<S> handle) : HandleBase(handle) {}

  // Access a member of the T object referenced by this handle.
  //
  // This is actually a double dereference -- first it dereferences the Handle
  // pointing to a Tagged<T>, and then continues through Tagged<T>::operator->.
  // This means that this is only permitted for Tagged<T> with an operator->,
  // i.e. for on-heap object T.
  V8_INLINE Tagged<T> operator->() const {
    if constexpr (is_subtype_v<T, HeapObject>) {
      return **this;
    } else {
      // `static_assert(false)` in this else clause was an unconditional error
      // before CWG2518. See https://reviews.llvm.org/D144285
#if defined(__clang__) && __clang_major__ >= 17
      // For non-HeapObjects, there's no on-heap object to dereference, so
      // disallow using operator->.
      //
      // If you got an error here and want to access the Tagged<T>, use
      // operator* -- e.g. for `Tagged<Smi>::value()`, use `(*handle).value()`.
      static_assert(
          false,
          "This handle does not reference a heap object. Use `(*handle).foo`.");
#endif
    }
  }

  V8_INLINE Tagged<T> operator*() const {
    // This static type check also fails for forward class declarations. We
    // check on access instead of on construction to allow Handles to forward
    // declared types.
    static_assert(is_taggable_v<T>, "static type violation");
    // Direct construction of Tagged from address, without a type check, because
    // we rather trust Handle<T> to contain a T than include all the respective
    // -inl.h headers for SLOW_DCHECKs.
    SLOW_DCHECK(IsDereferenceAllowed());
    return Tagged<T>(*location());
  }

  template <typename S>
  inline static const Handle<T> cast(Handle<S> that);

  // Consider declaring values that contain empty handles as
  // MaybeHandle to force validation before being used as handles.
  static const Handle<T> null() { return Handle<T>(); }

  // Location equality.
  bool equals(Handle<T> other) const { return address() == other.address(); }

  // Patches this Handle's value, in-place, with a new value. All handles with
  // the same location will see this update.
  void PatchValue(Tagged<T> new_value) {
    SLOW_DCHECK(location_ != nullptr && IsDereferenceAllowed());
    *location_ = new_value.ptr();
  }

  // Provide function object for location equality comparison.
  struct equal_to {
    V8_INLINE bool operator()(Handle<T> lhs, Handle<T> rhs) const {
      return lhs.equals(rhs);
    }
  };

  // Provide function object for location hashing.
  struct hash {
    V8_INLINE size_t operator()(Handle<T> const& handle) const {
      return base::hash<Address>()(handle.address());
    }
  };

 private:
  // Handles of different classes are allowed to access each other's location_.
  template <typename>
  friend class Handle;
  // MaybeHandle is allowed to access location_.
  template <typename>
  friend class MaybeHandle;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, Handle<T> handle);

// ----------------------------------------------------------------------------
// A stack-allocated class that governs a number of local handles.
// After a handle scope has been created, all local handles will be
// allocated within that handle scope until either the handle scope is
// deleted or another handle scope is created.  If there is already a
// handle scope and a new one is created, all allocations will take
// place in the new handle scope until it is deleted.  After that,
// new handles will again be allocated in the original handle scope.
//
// After the handle scope of a local handle has been deleted the
// garbage collector will no longer track the object stored in the
// handle and may deallocate it.  The behavior of accessing a handle
// for which the handle scope has been deleted is undefined.
class V8_NODISCARD HandleScope {
 public:
  explicit V8_INLINE HandleScope(Isolate* isolate);
  inline HandleScope(HandleScope&& other) V8_NOEXCEPT;
  HandleScope(const HandleScope&) = delete;
  HandleScope& operator=(const HandleScope&) = delete;

  // Allow placement new.
  void* operator new(size_t size, void* storage) {
    return ::operator new(size, storage);
  }

  // Prevent heap allocation or illegal handle scopes.
  void* operator new(size_t size) = delete;
  void operator delete(void* size_t) = delete;

  V8_INLINE ~HandleScope();

  inline HandleScope& operator=(HandleScope&& other) V8_NOEXCEPT;

  // Counts the number of allocated handles.
  V8_EXPORT_PRIVATE static int NumberOfHandles(Isolate* isolate);

  // Creates a new handle with the given value.
  V8_INLINE static Address* CreateHandle(Isolate* isolate, Address value);

  // Deallocates any extensions used by the current scope.
  V8_EXPORT_PRIVATE static void DeleteExtensions(Isolate* isolate);

  static Address current_next_address(Isolate* isolate);
  static Address current_limit_address(Isolate* isolate);
  static Address current_level_address(Isolate* isolate);

  // Closes the HandleScope (invalidating all handles
  // created in the scope of the HandleScope) and returns
  // a Handle backed by the parent scope holding the
  // value of the argument handle.
  template <typename T>
  Handle<T> CloseAndEscape(Handle<T> handle_value);

  Isolate* isolate() { return isolate_; }

  // Limit for number of handles with --check-handle-count. This is
  // large enough to compile natives and pass unit tests with some
  // slack for future changes to natives.
  static const int kCheckHandleThreshold = 30 * 1024;

 private:
  Isolate* isolate_;
  Address* prev_next_;
  Address* prev_limit_;

#ifdef V8_ENABLE_CHECKS
  int scope_level_ = 0;
#endif

  // Close the handle scope resetting limits to a previous state.
  static V8_INLINE void CloseScope(Isolate* isolate, Address* prev_next,
                                   Address* prev_limit);

  // Extend the handle scope making room for more handles.
  V8_EXPORT_PRIVATE V8_NOINLINE static Address* Extend(Isolate* isolate);

#ifdef ENABLE_HANDLE_ZAPPING
  // Zaps the handles in the half-open interval [start, end).
  V8_EXPORT_PRIVATE static void ZapRange(Address* start, Address* end);
#endif

  friend class v8::HandleScope;
  friend class HandleScopeImplementer;
  friend class Isolate;
  friend class LocalHandles;
  friend class LocalHandleScope;
  friend class PersistentHandles;
};

// Forward declaration for CanonicalHandlesMap.
template <typename V, class AllocationPolicy>
class IdentityMap;

using CanonicalHandlesMap = IdentityMap<Address*, ZoneAllocationPolicy>;

// Seal off the current HandleScope so that new handles can only be created
// if a new HandleScope is entered.
class V8_NODISCARD SealHandleScope final {
 public:
#ifndef DEBUG
  explicit SealHandleScope(Isolate* isolate) {}
  ~SealHandleScope() = default;
#else
  explicit inline SealHandleScope(Isolate* isolate);
  inline ~SealHandleScope();

 private:
  Isolate* isolate_;
  Address* prev_limit_;
  int prev_sealed_level_;
#endif
};

struct HandleScopeData final {
  static constexpr uint32_t kSizeInBytes =
      2 * kSystemPointerSize + 2 * kInt32Size;

  Address* next;
  Address* limit;
  int level;
  int sealed_level;

  void Initialize() {
    next = limit = nullptr;
    sealed_level = level = 0;
  }
};

static_assert(HandleScopeData::kSizeInBytes == sizeof(HandleScopeData));

#ifdef V8_ENABLE_DIRECT_HANDLE
// Direct handles should not be used without conservative stack scanning,
// as this would break the correctness of the GC.
static_assert(V8_ENABLE_CONSERVATIVE_STACK_SCANNING_BOOL);

// ----------------------------------------------------------------------------
// Base class for DirectHandle instantiations. Don't use directly.
class V8_TRIVIAL_ABI DirectHandleBase :
#ifdef DEBUG
    public api_internal::StackAllocated<true>
#else
    public api_internal::StackAllocated<false>
#endif
{
 public:
  // Check if this handle refers to the exact same object as the other handle.
  V8_INLINE bool is_identical_to(const HandleBase& that) const;
  V8_INLINE bool is_identical_to(const DirectHandleBase& that) const;
  V8_INLINE bool is_null() const { return obj_ == kTaggedNullAddress; }

  V8_INLINE Address address() const { return obj_; }

#ifdef DEBUG
  // Counts the number of allocated handles for the current thread.
  // The number is only accurate if V8_HAS_ATTRIBUTE_TRIVIAL_ABI,
  // otherwise it's zero.
  V8_INLINE static int NumberOfHandles() { return number_of_handles_; }
#endif

 protected:
  friend class HandleBase;

#if defined(DEBUG) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI
  // In this case, DirectHandleBase becomes not trivially copyable.
  V8_INLINE DirectHandleBase(const DirectHandleBase& other) V8_NOEXCEPT
      : obj_(other.obj_) {
    Register();
  }
  DirectHandleBase& operator=(const DirectHandleBase&) V8_NOEXCEPT = default;
  V8_INLINE ~DirectHandleBase() V8_NOEXCEPT { Unregister(); }
#endif

  V8_INLINE explicit DirectHandleBase(Address object) : obj_(object) {
    Register();
  }

#ifdef DEBUG
  V8_EXPORT_PRIVATE bool IsDereferenceAllowed() const;
#else
  V8_INLINE bool IsDereferenceAllowed() const { return true; }
#endif  // DEBUG

  // This is a direct pointer to either a tagged object or SMI. Design overview:
  // https://docs.google.com/document/d/1uRGYQM76vk1fc_aDqDH3pm2qhaJtnK2oyzeVng4cS6I/
  Address obj_;

 private:
  V8_INLINE void Register() {
#if defined(DEBUG) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI
    ++number_of_handles_;
#endif
  }

  V8_INLINE void Unregister() {
#if defined(DEBUG) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI
    DCHECK_LT(0, number_of_handles_);
    --number_of_handles_;
#endif
  }

#ifdef DEBUG
  inline static thread_local int number_of_handles_ = 0;
#endif
};

// ----------------------------------------------------------------------------
// A DirectHandle provides a reference to an object without an intermediate
// pointer.
//
// A DirectHandle is a simple wrapper around a tagged pointer to a heap object
// or a SMI. Its methods are symmetrical with Handle, so that Handles can be
// easily migrated.
//
// DirectHandles are intended to be used with conservative stack scanning, as
// they do not provide a mechanism for keeping an object alive across a garbage
// collection.
//
// Further motivation is explained in the design doc:
// https://docs.google.com/document/d/1uRGYQM76vk1fc_aDqDH3pm2qhaJtnK2oyzeVng4cS6I/
template <typename T>
class DirectHandle final : public DirectHandleBase {
 public:
  V8_INLINE DirectHandle() : DirectHandle(kTaggedNullAddress) {}

  V8_INLINE explicit DirectHandle(Address object) : DirectHandleBase(object) {}

  V8_INLINE explicit DirectHandle(Tagged<T> object);
  V8_INLINE DirectHandle(Tagged<T> object, Isolate* isolate)
      : DirectHandle(object) {}
  V8_INLINE DirectHandle(Tagged<T> object, LocalIsolate* isolate)
      : DirectHandle(object) {}
  V8_INLINE DirectHandle(Tagged<T> object, LocalHeap* local_heap)
      : DirectHandle(object) {}

  V8_INLINE explicit DirectHandle(Address* address)
      : DirectHandle(address == nullptr ? kTaggedNullAddress : *address) {}

  V8_INLINE static DirectHandle<T> New(Tagged<T> object, Isolate* isolate) {
    return DirectHandle<T>(object);
  }

  // Constructor for handling automatic up casting.
  // Ex. DirectHandle<JSFunction> can be passed when DirectHandle<Object> is
  // expected.
  template <typename S, typename = std::enable_if_t<is_subtype_v<S, T>>>
  V8_INLINE DirectHandle(DirectHandle<S> handle) : DirectHandle(handle.obj_) {}

  template <typename S, typename = std::enable_if_t<is_subtype_v<S, T>>>
  V8_INLINE DirectHandle(Handle<S> handle)
      : DirectHandle(handle.location() != nullptr ? *handle.location()
                                                  : kTaggedNullAddress) {}

  V8_INLINE Tagged<T> operator->() const {
    if constexpr (is_subtype_v<T, HeapObject>) {
      return **this;
    } else {
      // For non-HeapObjects, there's no on-heap object to dereference, so
      // disallow using operator->.
      //
      // If you got an error here and want to access the Tagged<T>, use
      // operator* -- e.g. for `Tagged<Smi>::value()`, use `(*handle).value()`.
      static_assert(
          false,
          "This handle does not reference a heap object. Use `(*handle).foo`.");
    }
  }

  V8_INLINE Tagged<T> operator*() const {
    // This static type check also fails for forward class declarations. We
    // check on access instead of on construction to allow DirectHandles to
    // forward declared types.
    static_assert(is_taggable_v<T>, "static type violation");
    // Direct construction of Tagged from address, without a type check, because
    // we rather trust DirectHandle<T> to contain a T than include all the
    // respective -inl.h headers for SLOW_DCHECKs.
    SLOW_DCHECK(IsDereferenceAllowed());
    return Tagged<T>(address());
  }

  template <typename S>
  V8_INLINE static const DirectHandle<T> cast(DirectHandle<S> that);

  template <typename S>
  V8_INLINE static const DirectHandle<T> cast(Handle<S> that);

  // Consider declaring values that contain empty handles as
  // MaybeDirectHandle to force validation before being used as handles.
  V8_INLINE static const DirectHandle<T> null() { return DirectHandle<T>(); }

  // Address equality.
  bool equals(DirectHandle<T> other) const {
    return address() == other.address();
  }

 private:
  // DirectHandles of different classes are allowed to access each other's
  // obj_.
  template <typename>
  friend class DirectHandle;
  // MaybeDirectHandle is allowed to access obj_.
  template <typename>
  friend class MaybeDirectHandle;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, DirectHandle<T> handle);

#endif  // V8_ENABLE_DIRECT_HANDLE

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_HANDLES_H_
