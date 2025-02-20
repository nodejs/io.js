// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_HANDLES_H_
#define V8_HANDLES_HANDLES_H_

#include <type_traits>
#include <vector>

#include "src/base/hashing.h"
#include "src/base/macros.h"
#include "src/common/checks.h"
#include "src/common/globals.h"
#include "src/objects/casting.h"
#include "src/objects/tagged.h"
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
template <typename T>
class DirectHandleUnchecked;
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
class ZoneAllocationPolicy;

constexpr Address kTaggedNullAddress = 0x1;

// ----------------------------------------------------------------------------
// Base class for Handle instantiations. Don't use directly.
class HandleBase {
 public:
  // Check if this handle refers to the exact same object as the other handle.
  V8_INLINE bool is_identical_to(const HandleBase& that) const;
#ifdef V8_ENABLE_DIRECT_HANDLE
  V8_INLINE bool is_identical_to(const DirectHandleBase& that) const;
#else
  template <typename T>
  V8_INLINE bool is_identical_to(const DirectHandle<T>& that) const {
    return is_identical_to(that.handle_);
  }
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

#ifdef V8_ENABLE_DIRECT_HANDLE
  V8_INLINE ValueHelper::InternalRepresentationType repr() const {
    return location_ ? *location_ : ValueHelper::kEmpty;
  }
#else
  V8_INLINE ValueHelper::InternalRepresentationType repr() const {
    return location_;
  }
#endif  // V8_ENABLE_DIRECT_HANDLE

 protected:
#ifdef V8_ENABLE_DIRECT_HANDLE
  friend class DirectHandleBase;

  static Address* indirect_handle(Address object);
  static Address* indirect_handle(Address object, Isolate* isolate);
  static Address* indirect_handle(Address object, LocalIsolate* isolate);
  static Address* indirect_handle(Address object, LocalHeap* local_heap);

  template <typename T>
  friend IndirectHandle<T> indirect_handle(DirectHandle<T> handle);
  template <typename T>
  friend IndirectHandle<T> indirect_handle(DirectHandle<T> handle,
                                           Isolate* isolate);
  template <typename T>
  friend IndirectHandle<T> indirect_handle(DirectHandle<T> handle,
                                           LocalIsolate* isolate);
  template <typename T>
  friend IndirectHandle<T> indirect_handle(DirectHandle<T> handle,
                                           LocalHeap* local_heap);
#endif  // V8_ENABLE_DIRECT_HANDLE

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
  // Handles denote strong references.
  static_assert(!is_maybe_weak_v<T>);

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
  template <typename S>
  V8_INLINE Handle(Handle<S> handle)
    requires(is_subtype_v<S, T>)
      : HandleBase(handle) {}

  // Access a member of the T object referenced by this handle.
  //
  // This is actually a double dereference -- first it dereferences the Handle
  // pointing to a Tagged<T>, and then continues through Tagged<T>::operator->.
  // This means that this is only permitted for Tagged<T> with an operator->,
  // i.e. for on-heap object T.
  V8_INLINE Tagged<T> operator->() const {
    // For non-HeapObjects, there's no on-heap object to dereference, so
    // disallow using operator->.
    //
    // If you got an error here and want to access the Tagged<T>, use
    // operator* -- e.g. for `Tagged<Smi>::value()`, use `(*handle).value()`.
    static_assert(
        is_subtype_v<T, HeapObject>,
        "This handle does not reference a heap object. Use `(*handle).foo`.");
    return **this;
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

  // Consider declaring values that contain empty handles as
  // MaybeHandle to force validation before being used as handles.
  static const Handle<T> null() { return Handle<T>(); }

  // Location equality.
  bool equals(Handle<T> other) const { return address() == other.address(); }

  // Patches this Handle's value, in-place, with a new value. All indirect
  // handles with the same location will see this update.
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

  using MaybeType = MaybeHandle<T>;

 private:
  // Handles of different classes are allowed to access each other's location_.
  template <typename>
  friend class Handle;
  // MaybeHandle is allowed to access location_.
  template <typename>
  friend class MaybeHandle;
  // Casts are allowed to access location_.
  template <typename To, typename From>
  friend inline IndirectHandle<To> UncheckedCast(IndirectHandle<From> value);
};

template <typename T>
std::ostream& operator<<(std::ostream& os, IndirectHandle<T> handle);

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
  //
  // TODO(42203211): When direct handles are enabled, the version with
  // HandleType = DirectHandle does not need to be called, as it simply
  // closes the scope (which is done by the scope's destructor anyway)
  // and returns its parameter. This will be cleaned up after direct
  // handles ship.
  template <typename T, template <typename> typename HandleType>
    requires(std::is_convertible_v<HandleType<T>, DirectHandle<T>>)
  HandleType<T> CloseAndEscape(HandleType<T> handle_value);

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

#if defined(ENABLE_GLOBAL_HANDLE_ZAPPING) || \
    defined(ENABLE_LOCAL_HANDLE_ZAPPING)
  // Zaps the handles in the half-open interval [start, end).
  V8_EXPORT_PRIVATE static void ZapRange(Address* start, Address* end,
                                         uintptr_t value = kHandleZapValue);
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

template <typename T>
struct is_direct_handle : public std::false_type {};
template <typename T>
static constexpr bool is_direct_handle_v = is_direct_handle<T>::value;

#ifdef V8_ENABLE_DIRECT_HANDLE

// Direct handles should not be used without conservative stack scanning,
// as this would break the correctness of the GC.
static_assert(V8_ENABLE_CONSERVATIVE_STACK_SCANNING_BOOL);

// ----------------------------------------------------------------------------
// Base class for DirectHandle instantiations. Don't use directly.
class V8_TRIVIAL_ABI DirectHandleBase :
#ifdef ENABLE_SLOW_DCHECKS
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

  V8_INLINE ValueHelper::InternalRepresentationType repr() const {
    return obj_;
  }

#ifdef ENABLE_SLOW_DCHECKS
  // Counts the number of allocated handles for the current thread that are
  // below the stack marker. The number is only accurate if
  // V8_HAS_ATTRIBUTE_TRIVIAL_ABI, otherwise it's zero.
  V8_INLINE static int NumberOfHandles() { return number_of_handles_; }

  // Scope to temporarily reset the number of allocated handles.
  class V8_NODISCARD ResetNumberOfHandlesScope {
   public:
    ResetNumberOfHandlesScope() : saved_number_of_handles_(number_of_handles_) {
      number_of_handles_ = 0;
    }
    ~ResetNumberOfHandlesScope() {
      number_of_handles_ = saved_number_of_handles_;
    }

   private:
    int saved_number_of_handles_;
  };
#else
  class V8_NODISCARD ResetNumberOfHandlesScope {};
#endif  // ENABLE_SLOW_DCHECKS

 protected:
  friend class HandleBase;

#if defined(ENABLE_SLOW_DCHECKS) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI
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

  DirectHandleBase(Address obj, no_checking_tag do_not_check)
      : StackAllocated(do_not_check), obj_(obj) {
    Register();
  }

  // This is a direct pointer to either a tagged object or SMI. Design overview:
  // https://docs.google.com/document/d/1uRGYQM76vk1fc_aDqDH3pm2qhaJtnK2oyzeVng4cS6I/
  Address obj_;

 private:
  V8_INLINE void Register() {
#if defined(ENABLE_SLOW_DCHECKS) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI
    ++number_of_handles_;
#endif
  }

  V8_INLINE void Unregister() {
#if defined(ENABLE_SLOW_DCHECKS) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI
    SLOW_DCHECK(number_of_handles_ > 0);
    --number_of_handles_;
#endif
  }

#ifdef ENABLE_SLOW_DCHECKS
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
class DirectHandle : public DirectHandleBase {
 public:
  // Handles denote strong references.
  static_assert(!is_maybe_weak_v<T>);

  V8_INLINE DirectHandle() : DirectHandle(kTaggedNullAddress) {}

  V8_INLINE DirectHandle(Tagged<T> object, Isolate* isolate)
      : DirectHandle(object) {}
  V8_INLINE DirectHandle(Tagged<T> object, LocalIsolate* isolate)
      : DirectHandle(object) {}
  V8_INLINE DirectHandle(Tagged<T> object, LocalHeap* local_heap)
      : DirectHandle(object) {}

  V8_INLINE static DirectHandle FromAddress(Address object) {
    return DirectHandle(object);
  }
  V8_INLINE static DirectHandle FromSlot(Address* slot) {
    return FromAddress(slot != nullptr ? *slot : kTaggedNullAddress);
  }

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

  // Consider declaring values that contain empty handles as
  // MaybeDirectHandle to force validation before being used as handles.
  V8_INLINE static const DirectHandle<T> null() { return DirectHandle<T>(); }

  // Address equality.
  bool equals(DirectHandle<T> other) const {
    return address() == other.address();
  }

  // Sets this DirectHandle's value. This is equivalent to handle assignment,
  // except for the check that is equivalent to that performed in
  // Handle<T>::PatchValue.
  // TODO(42203211): Calls to this method will eventually be replaced by direct
  // handle assignments, when the migration to direct handles is complete.
  void SetValue(Tagged<T> new_value) {
    SLOW_DCHECK(obj_ != kTaggedNullAddress && IsDereferenceAllowed());
    obj_ = new_value.ptr();
  }

  using MaybeType = MaybeDirectHandle<T>;

 private:
  // DirectHandles of different classes are allowed to access each other's
  // obj_.
  template <typename>
  friend class DirectHandle;
  // MaybeDirectHandle is allowed to access obj_.
  template <typename>
  friend class MaybeDirectHandle;
  friend class DirectHandleUnchecked<T>;
  // Casts are allowed to access obj_.
  template <typename To, typename From>
  friend inline DirectHandle<To> UncheckedCast(DirectHandle<From> value);

  V8_INLINE explicit DirectHandle(Tagged<T> object);
  V8_INLINE explicit DirectHandle(Address object) : DirectHandleBase(object) {}

  explicit DirectHandle(no_checking_tag do_not_check)
      : DirectHandleBase(kTaggedNullAddress, do_not_check) {}
  explicit DirectHandle(const DirectHandle<T>& other,
                        no_checking_tag do_not_check)
      : DirectHandleBase(other.obj_, do_not_check) {}
};

template <typename T>
IndirectHandle<T> indirect_handle(DirectHandle<T> handle) {
  if (handle.is_null()) return IndirectHandle<T>();
  return IndirectHandle<T>(HandleBase::indirect_handle(handle.address()));
}

template <typename T>
IndirectHandle<T> indirect_handle(DirectHandle<T> handle, Isolate* isolate) {
  if (handle.is_null()) return IndirectHandle<T>();
  return IndirectHandle<T>(
      HandleBase::indirect_handle(handle.address(), isolate));
}

template <typename T>
IndirectHandle<T> indirect_handle(DirectHandle<T> handle,
                                  LocalIsolate* isolate) {
  if (handle.is_null()) return IndirectHandle<T>();
  return IndirectHandle<T>(
      HandleBase::indirect_handle(handle.address(), isolate));
}

template <typename T>
IndirectHandle<T> indirect_handle(DirectHandle<T> handle,
                                  LocalHeap* local_heap) {
  if (handle.is_null()) return IndirectHandle<T>();
  return IndirectHandle<T>(
      HandleBase::indirect_handle(handle.address(), local_heap));
}

#else  // !V8_ENABLE_DIRECT_HANDLE

// ----------------------------------------------------------------------------
// When conservative stack scanning is disabled, DirectHandle is a wrapper
// around IndirectHandle (i.e. Handle). To preserve conservative stack scanning
// semantics, DirectHandle be implicitly created from an IndirectHandle, but
// does not implicitly convert to an IndirectHandle.
template <typename T>
class V8_TRIVIAL_ABI DirectHandle :
#ifdef ENABLE_SLOW_DCHECKS
    // TODO(42203211): Setting this to true enables the check for
    // stack-allocated "fake" direct handles disabled in non-CSS builds.
    // Consider enabling it, if it is not too expensive even as a SLOW_DCHECK.
    public api_internal::StackAllocated<false>
#else
    public api_internal::StackAllocated<false>
#endif
{
 public:
  V8_INLINE static const DirectHandle null() {
    return DirectHandle(Handle<T>::null());
  }
  V8_INLINE static DirectHandle<T> New(Tagged<T> object, Isolate* isolate) {
    return DirectHandle(Handle<T>::New(object, isolate));
  }

  V8_INLINE DirectHandle() = default;

  V8_INLINE DirectHandle(Tagged<T> object, Isolate* isolate)
      : handle_(object, isolate) {}
  V8_INLINE DirectHandle(Tagged<T> object, LocalIsolate* isolate)
      : handle_(object, isolate) {}
  V8_INLINE DirectHandle(Tagged<T> object, LocalHeap* local_heap)
      : handle_(object, local_heap) {}

  template <typename S>
  V8_INLINE DirectHandle(DirectHandle<S> handle)
    requires(is_subtype_v<S, T>)
      : handle_(handle.handle_) {}

  template <typename S>
  V8_INLINE DirectHandle(IndirectHandle<S> handle)
    requires(is_subtype_v<S, T>)
      : handle_(handle) {}

  V8_INLINE static DirectHandle FromSlot(Address* slot) {
    return DirectHandle(IndirectHandle<T>(slot));
  }

  V8_INLINE IndirectHandle<T> operator->() const { return handle_; }
  V8_INLINE Tagged<T> operator*() const { return *handle_; }
  V8_INLINE bool is_null() const { return handle_.is_null(); }

  V8_INLINE Address address() const { return handle_.address(); }
  V8_INLINE ValueHelper::InternalRepresentationType repr() const {
    return handle_.repr();
  }

  // Sets this Handle's value, in place, with a new value. Notice that, for
  // efficiency reasons, this is implemented by calling method PatchValue of the
  // underlying indirect handle. However, it should be considered as equivalent
  // to a simple handle assignment, i.e., as if affecting only the specific
  // handle and not all other indirect handles with the same location.
  // TODO(42203211): Calls to this method will eventually be replaced by direct
  // handle assignments, when the migration to direct handles is complete.
  V8_INLINE void SetValue(Tagged<T> new_value) {
    handle_.PatchValue(new_value);
  }

  V8_INLINE bool equals(DirectHandle<T> other) const {
    return handle_.equals(other.handle_);
  }

  template <typename S>
  V8_INLINE bool is_identical_to(Handle<S> other) const {
    return handle_.is_identical_to(other);
  }
  template <typename S>
  V8_INLINE bool is_identical_to(DirectHandle<S> other) const {
    return handle_.is_identical_to(other.handle_);
  }

  using MaybeType = MaybeDirectHandle<T>;

 private:
  // Handles of various different classes are allowed to access handle_.
  friend class HandleBase;
  template <typename>
  friend class DirectHandle;
  template <typename>
  friend class MaybeDirectHandle;
  friend class DirectHandleUnchecked<T>;
  // Casts are allowed to access handle_.
  template <typename To, typename From>
  friend inline DirectHandle<To> UncheckedCast(DirectHandle<From> value);
  template <typename U>
  friend inline IndirectHandle<U> indirect_handle(DirectHandle<U>);
  template <typename U>
  friend inline IndirectHandle<U> indirect_handle(DirectHandle<U>, Isolate*);
  template <typename U>
  friend inline IndirectHandle<U> indirect_handle(DirectHandle<U>,
                                                  LocalIsolate*);
  template <typename U>
  friend inline IndirectHandle<U> indirect_handle(DirectHandle<U>, LocalHeap*);

  explicit DirectHandle(no_checking_tag do_not_check)
      : StackAllocated(do_not_check), handle_() {}
  explicit DirectHandle(const DirectHandle<T>& other,
                        no_checking_tag do_not_check)
      : StackAllocated(do_not_check), handle_(other.handle_) {}

  IndirectHandle<T> handle_;
};

template <typename T>
V8_INLINE IndirectHandle<T> indirect_handle(DirectHandle<T> handle) {
  return handle.handle_;
}

template <typename T>
V8_INLINE IndirectHandle<T> indirect_handle(DirectHandle<T> handle,
                                            Isolate* isolate) {
  return handle.handle_;
}

template <typename T>
V8_INLINE IndirectHandle<T> indirect_handle(DirectHandle<T> handle,
                                            LocalIsolate* isolate) {
  return handle.handle_;
}

template <typename T>
V8_INLINE IndirectHandle<T> indirect_handle(DirectHandle<T> handle,
                                            LocalHeap* local_heap) {
  return handle.handle_;
}

#endif  // V8_ENABLE_DIRECT_HANDLE

// A variant of DirectHandle that is suitable for off-stack allocation.
// Used internally by DirectHandleVector<T>. Not to be used directly!
template <typename T>
class V8_TRIVIAL_ABI DirectHandleUnchecked final : public DirectHandle<T> {
 public:
  DirectHandleUnchecked() : DirectHandle<T>(DirectHandle<T>::do_not_check) {}

#if defined(ENABLE_SLOW_DCHECKS) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI
  // In this case, the check is also enforced in the copy constructor and we
  // need to suppress it.
  DirectHandleUnchecked(const DirectHandleUnchecked& other) V8_NOEXCEPT
      : DirectHandle<T>(other, DirectHandle<T>::do_not_check) {}
  DirectHandleUnchecked& operator=(const DirectHandleUnchecked&)
      V8_NOEXCEPT = default;
#endif

  // Implicit conversion from handles.
  // NOLINTNEXTLINE(runtime/explicit)
  DirectHandleUnchecked(const DirectHandle<T>& other) V8_NOEXCEPT
      : DirectHandle<T>(other, DirectHandle<T>::do_not_check) {}
  // NOLINTNEXTLINE(runtime/explicit)
  DirectHandleUnchecked(const Handle<T>& other) V8_NOEXCEPT
      : DirectHandle<T>(other, DirectHandle<T>::do_not_check) {}
};

#if V8_ENABLE_DIRECT_HANDLE

// Off-stack allocated direct handles must be registered as strong roots.
// For off-stack indirect handles, this is not necessary.
template <typename T>
class StrongRootAllocator<DirectHandleUnchecked<T>>
    : public StrongRootAllocatorBase {
 public:
  using value_type = DirectHandleUnchecked<T>;
  static_assert(std::is_standard_layout_v<value_type>);
  static_assert(sizeof(value_type) == sizeof(Address));

  template <typename HeapOrIsolateT>
  explicit StrongRootAllocator(HeapOrIsolateT* heap_or_isolate)
      : StrongRootAllocatorBase(heap_or_isolate) {}
  template <typename U>
  StrongRootAllocator(const StrongRootAllocator<U>& other) noexcept
      : StrongRootAllocatorBase(other) {}

  value_type* allocate(size_t n) {
    return reinterpret_cast<value_type*>(allocate_impl(n));
  }
  void deallocate(value_type* p, size_t n) noexcept {
    return deallocate_impl(reinterpret_cast<Address*>(p), n);
  }
};

#endif  // V8_ENABLE_DIRECT_HANDLE

template <typename T>
class DirectHandleVector {
 private:
  using element_type = internal::DirectHandleUnchecked<T>;

#ifdef V8_ENABLE_DIRECT_HANDLE
  using allocator_type = internal::StrongRootAllocator<element_type>;

  template <typename IsolateT>
  static allocator_type make_allocator(IsolateT* isolate) noexcept {
    return allocator_type(isolate);
  }

  using vector_type = std::vector<element_type, allocator_type>;
#else
  using vector_type = std::vector<element_type>;
#endif

 public:
  using value_type = DirectHandle<T>;
  using reference = value_type&;
  using const_reference = const value_type&;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using iterator =
      internal::WrappedIterator<typename vector_type::iterator, value_type>;
  using const_iterator =
      internal::WrappedIterator<typename vector_type::const_iterator,
                                const value_type>;

#ifdef V8_ENABLE_DIRECT_HANDLE
  template <typename IsolateT>
  explicit DirectHandleVector(IsolateT* isolate)
      : backing_(make_allocator(isolate)) {}
  template <typename IsolateT>
  DirectHandleVector(IsolateT* isolate, size_t n)
      : backing_(n, make_allocator(isolate)) {}
  template <typename IsolateT>
  DirectHandleVector(IsolateT* isolate, std::initializer_list<value_type> init)
      : backing_(make_allocator(isolate)) {
    if (init.size() == 0) return;
    backing_.reserve(init.size());
    backing_.insert(backing_.end(), init.begin(), init.end());
  }
#else
  template <typename IsolateT>
  explicit DirectHandleVector(IsolateT* isolate) : backing_() {}
  template <typename IsolateT>
  DirectHandleVector(IsolateT* isolate, size_t n) : backing_(n) {}
  template <typename IsolateT>
  DirectHandleVector(IsolateT* isolate, std::initializer_list<value_type> init)
      : backing_() {
    if (init.size() == 0) return;
    backing_.reserve(init.size());
    backing_.insert(backing_.end(), init.begin(), init.end());
  }
#endif

  iterator begin() noexcept { return iterator(backing_.begin()); }
  const_iterator begin() const noexcept {
    return const_iterator(backing_.begin());
  }
  iterator end() noexcept { return iterator(backing_.end()); }
  const_iterator end() const noexcept { return const_iterator(backing_.end()); }

  size_t size() const noexcept { return backing_.size(); }
  bool empty() const noexcept { return backing_.empty(); }
  void reserve(size_t n) { backing_.reserve(n); }
  void shrink_to_fit() { backing_.shrink_to_fit(); }

  DirectHandle<T>& operator[](size_t n) { return backing_[n]; }
  const DirectHandle<T>& operator[](size_t n) const { return backing_[n]; }

  DirectHandle<T>& at(size_t n) { return backing_.at(n); }
  const DirectHandle<T>& at(size_t n) const { return backing_.at(n); }

  DirectHandle<T>& front() { return backing_.front(); }
  const DirectHandle<T>& front() const { return backing_.front(); }
  DirectHandle<T>& back() { return backing_.back(); }
  const DirectHandle<T>& back() const { return backing_.back(); }

  DirectHandle<T>* data() noexcept { return backing_.data(); }
  const DirectHandle<T>* data() const noexcept { return backing_.data(); }

  iterator insert(const_iterator pos, const DirectHandle<T>& value) {
    return iterator(backing_.insert(pos.base(), value));
  }

  template <typename InputIt>
  iterator insert(const_iterator pos, InputIt first, InputIt last) {
    return iterator(backing_.insert(pos.base(), first, last));
  }

  iterator insert(const_iterator pos,
                  std::initializer_list<DirectHandle<T>> init) {
    return iterator(backing_.insert(pos.base(), init.begin(), init.end()));
  }

  DirectHandleVector<T>& operator=(std::initializer_list<value_type> init) {
    backing_.clear();
    backing_.reserve(init.size());
    backing_.insert(backing_.end(), init.begin(), init.end());
    return *this;
  }

  void push_back(const DirectHandle<T>& x) { backing_.push_back(x); }
  void pop_back() { backing_.pop_back(); }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    backing_.push_back(value_type{std::forward<Args>(args)...});
  }

  void clear() noexcept { backing_.clear(); }
  void resize(size_t n) { backing_.resize(n); }
  void swap(DirectHandleVector<T>& other) { backing_.swap(other.backing_); }

  friend bool operator==(const DirectHandleVector<T>& x,
                         const DirectHandleVector<T>& y) {
    return x.backing_ == y.backing_;
  }
  friend bool operator!=(const DirectHandleVector<T>& x,
                         const DirectHandleVector<T>& y) {
    return x.backing_ != y.backing_;
  }
  friend bool operator<(const DirectHandleVector<T>& x,
                        const DirectHandleVector<T>& y) {
    return x.backing_ < y.backing_;
  }
  friend bool operator>(const DirectHandleVector<T>& x,
                        const DirectHandleVector<T>& y) {
    return x.backing_ > y.backing_;
  }
  friend bool operator<=(const DirectHandleVector<T>& x,
                         const DirectHandleVector<T>& y) {
    return x.backing_ <= y.backing_;
  }
  friend bool operator>=(const DirectHandleVector<T>& x,
                         const DirectHandleVector<T>& y) {
    return x.backing_ >= y.backing_;
  }

 private:
  vector_type backing_;
};

template <typename T, template <typename> typename HandleType>
  requires(std::is_convertible_v<HandleType<T>, DirectHandle<T>>)
V8_INLINE DirectHandle<T> direct_handle(HandleType<T> handle) {
  return handle;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, DirectHandle<T> handle);

template <typename T>
struct is_direct_handle<DirectHandle<T>> : public std::true_type {};

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_HANDLES_H_
