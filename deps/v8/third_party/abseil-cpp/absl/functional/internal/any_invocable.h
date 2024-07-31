// Copyright 2022 The Abseil Authors.
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
//
// Implementation details for `absl::AnyInvocable`

#ifndef ABSL_FUNCTIONAL_INTERNAL_ANY_INVOCABLE_H_
#define ABSL_FUNCTIONAL_INTERNAL_ANY_INVOCABLE_H_

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This implementation of the proposed `any_invocable` uses an approach that  //
// chooses between local storage and remote storage for the contained target  //
// object based on the target object's size, alignment requirements, and      //
// whether or not it has a nothrow move constructor. Additional optimizations //
// are performed when the object is a trivially copyable type [basic.types].  //
//                                                                            //
// There are three datamembers per `AnyInvocable` instance                    //
//                                                                            //
// 1) A union containing either                                               //
//        - A pointer to the target object referred to via a void*, or        //
//        - the target object, emplaced into a raw char buffer                //
//                                                                            //
// 2) A function pointer to a "manager" function operation that takes a       //
//    discriminator and logically branches to either perform a move operation //
//    or destroy operation based on that discriminator.                       //
//                                                                            //
// 3) A function pointer to an "invoker" function operation that invokes the  //
//    target object, directly returning the result.                           //
//                                                                            //
// When in the logically empty state, the manager function is an empty        //
// function and the invoker function is one that would be undefined-behavior  //
// to call.                                                                   //
//                                                                            //
// An additional optimization is performed when converting from one           //
// AnyInvocable to another where only the noexcept specification and/or the   //
// cv/ref qualifiers of the function type differ. In these cases, the         //
// conversion works by "moving the guts", similar to if they were the same    //
// exact type, as opposed to having to perform an additional layer of         //
// wrapping through remote storage.                                           //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// IWYU pragma: private, include "absl/functional/any_invocable.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/internal/invoke.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/meta/type_traits.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Helper macro used to prevent spelling `noexcept` in language versions older
// than C++17, where it is not part of the type system, in order to avoid
// compilation failures and internal compiler errors.
#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 201703L
#define ABSL_INTERNAL_NOEXCEPT_SPEC(noex) noexcept(noex)
#else
#define ABSL_INTERNAL_NOEXCEPT_SPEC(noex)
#endif

// Defined in functional/any_invocable.h
template <class Sig>
class AnyInvocable;

namespace internal_any_invocable {

// Constants relating to the small-object-storage for AnyInvocable
enum StorageProperty : std::size_t {
  kAlignment = alignof(std::max_align_t),  // The alignment of the storage
  kStorageSize = sizeof(void*) * 2         // The size of the storage
};

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction for checking if a type is an AnyInvocable instantiation.
// This is used during conversion operations.
template <class T>
struct IsAnyInvocable : std::false_type {};

template <class Sig>
struct IsAnyInvocable<AnyInvocable<Sig>> : std::true_type {};
//
////////////////////////////////////////////////////////////////////////////////

// A type trait that tells us whether or not a target function type should be
// stored locally in the small object optimization storage
template <class T>
using IsStoredLocally = std::integral_constant<
    bool, sizeof(T) <= kStorageSize && alignof(T) <= kAlignment &&
              kAlignment % alignof(T) == 0 &&
              std::is_nothrow_move_constructible<T>::value>;

// An implementation of std::remove_cvref_t of C++20.
template <class T>
using RemoveCVRef =
    typename std::remove_cv<typename std::remove_reference<T>::type>::type;

////////////////////////////////////////////////////////////////////////////////
//
// An implementation of the C++ standard INVOKE<R> pseudo-macro, operation is
// equivalent to std::invoke except that it forces an implicit conversion to the
// specified return type. If "R" is void, the function is executed and the
// return value is simply ignored.
template <class ReturnType, class F, class... P,
          typename = absl::enable_if_t<std::is_void<ReturnType>::value>>
void InvokeR(F&& f, P&&... args) {
  absl::base_internal::invoke(std::forward<F>(f), std::forward<P>(args)...);
}

template <class ReturnType, class F, class... P,
          absl::enable_if_t<!std::is_void<ReturnType>::value, int> = 0>
ReturnType InvokeR(F&& f, P&&... args) {
  // GCC 12 has a false-positive -Wmaybe-uninitialized warning here.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
  return absl::base_internal::invoke(std::forward<F>(f),
                                     std::forward<P>(args)...);
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic pop
#endif
}

//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
///
// A metafunction that takes a "T" corresponding to a parameter type of the
// user's specified function type, and yields the parameter type to use for the
// type-erased invoker. In order to prevent observable moves, this must be
// either a reference or, if the type is trivial, the original parameter type
// itself. Since the parameter type may be incomplete at the point that this
// metafunction is used, we can only do this optimization for scalar types
// rather than for any trivial type.
template <typename T>
T ForwardImpl(std::true_type);

template <typename T>
T&& ForwardImpl(std::false_type);

// NOTE: We deliberately use an intermediate struct instead of a direct alias,
// as a workaround for b/206991861 on MSVC versions < 1924.
template <class T>
struct ForwardedParameter {
  using type = decltype((
      ForwardImpl<T>)(std::integral_constant<bool,
                                             std::is_scalar<T>::value>()));
};

template <class T>
using ForwardedParameterType = typename ForwardedParameter<T>::type;
//
////////////////////////////////////////////////////////////////////////////////

// A discriminator when calling the "manager" function that describes operation
// type-erased operation should be invoked.
//
// "relocate_from_to" specifies that the manager should perform a move.
//
// "dispose" specifies that the manager should perform a destroy.
enum class FunctionToCall : bool { relocate_from_to, dispose };

// The portion of `AnyInvocable` state that contains either a pointer to the
// target object or the object itself in local storage
union TypeErasedState {
  struct {
    // A pointer to the type-erased object when remotely stored
    void* target;
    // The size of the object for `RemoteManagerTrivial`
    std::size_t size;
  } remote;

  // Local-storage for the type-erased object when small and trivial enough
  alignas(kAlignment) char storage[kStorageSize];
};

// A typed accessor for the object in `TypeErasedState` storage
template <class T>
T& ObjectInLocalStorage(TypeErasedState* const state) {
  // We launder here because the storage may be reused with the same type.
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606L
  return *std::launder(reinterpret_cast<T*>(&state->storage));
#elif ABSL_HAVE_BUILTIN(__builtin_launder)
  return *__builtin_launder(reinterpret_cast<T*>(&state->storage));
#else

  // When `std::launder` or equivalent are not available, we rely on undefined
  // behavior, which works as intended on Abseil's officially supported
  // platforms as of Q2 2022.
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
  return *reinterpret_cast<T*>(&state->storage);
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif
}

// The type for functions issuing lifetime-related operations: move and dispose
// A pointer to such a function is contained in each `AnyInvocable` instance.
// NOTE: When specifying `FunctionToCall::`dispose, the same state must be
// passed as both "from" and "to".
using ManagerType = void(FunctionToCall /*operation*/,
                         TypeErasedState* /*from*/, TypeErasedState* /*to*/)
    ABSL_INTERNAL_NOEXCEPT_SPEC(true);

// The type for functions issuing the actual invocation of the object
// A pointer to such a function is contained in each AnyInvocable instance.
template <bool SigIsNoexcept, class ReturnType, class... P>
using InvokerType = ReturnType(TypeErasedState*, ForwardedParameterType<P>...)
    ABSL_INTERNAL_NOEXCEPT_SPEC(SigIsNoexcept);

// The manager that is used when AnyInvocable is empty
inline void EmptyManager(FunctionToCall /*operation*/,
                         TypeErasedState* /*from*/,
                         TypeErasedState* /*to*/) noexcept {}

// The manager that is used when a target function is in local storage and is
// a trivially copyable type.
inline void LocalManagerTrivial(FunctionToCall /*operation*/,
                                TypeErasedState* const from,
                                TypeErasedState* const to) noexcept {
  // This single statement without branching handles both possible operations.
  //
  // For FunctionToCall::dispose, "from" and "to" point to the same state, and
  // so this assignment logically would do nothing.
  //
  // Note: Correctness here relies on http://wg21.link/p0593, which has only
  // become standard in C++20, though implementations do not break it in
  // practice for earlier versions of C++.
  //
  // The correct way to do this without that paper is to first placement-new a
  // default-constructed T in "to->storage" prior to the memmove, but doing so
  // requires a different function to be created for each T that is stored
  // locally, which can cause unnecessary bloat and be less cache friendly.
  *to = *from;

  // Note: Because the type is trivially copyable, the destructor does not need
  // to be called ("trivially copyable" requires a trivial destructor).
}

// The manager that is used when a target function is in local storage and is
// not a trivially copyable type.
template <class T>
void LocalManagerNontrivial(FunctionToCall operation,
                            TypeErasedState* const from,
                            TypeErasedState* const to) noexcept {
  static_assert(IsStoredLocally<T>::value,
                "Local storage must only be used for supported types.");
  static_assert(!std::is_trivially_copyable<T>::value,
                "Locally stored types must be trivially copyable.");

  T& from_object = (ObjectInLocalStorage<T>)(from);

  switch (operation) {
    case FunctionToCall::relocate_from_to:
      // NOTE: Requires that the left-hand operand is already empty.
      ::new (static_cast<void*>(&to->storage)) T(std::move(from_object));
      ABSL_FALLTHROUGH_INTENDED;
    case FunctionToCall::dispose:
      from_object.~T();  // Must not throw. // NOLINT
      return;
  }
  ABSL_UNREACHABLE();
}

// The invoker that is used when a target function is in local storage
// Note: QualTRef here is the target function type along with cv and reference
// qualifiers that must be used when calling the function.
template <bool SigIsNoexcept, class ReturnType, class QualTRef, class... P>
ReturnType LocalInvoker(
    TypeErasedState* const state,
    ForwardedParameterType<P>... args) noexcept(SigIsNoexcept) {
  using RawT = RemoveCVRef<QualTRef>;
  static_assert(
      IsStoredLocally<RawT>::value,
      "Target object must be in local storage in order to be invoked from it.");

  auto& f = (ObjectInLocalStorage<RawT>)(state);
  return (InvokeR<ReturnType>)(static_cast<QualTRef>(f),
                               static_cast<ForwardedParameterType<P>>(args)...);
}

// The manager that is used when a target function is in remote storage and it
// has a trivial destructor
inline void RemoteManagerTrivial(FunctionToCall operation,
                                 TypeErasedState* const from,
                                 TypeErasedState* const to) noexcept {
  switch (operation) {
    case FunctionToCall::relocate_from_to:
      // NOTE: Requires that the left-hand operand is already empty.
      to->remote = from->remote;
      return;
    case FunctionToCall::dispose:
#if defined(__cpp_sized_deallocation)
      ::operator delete(from->remote.target, from->remote.size);
#else   // __cpp_sized_deallocation
      ::operator delete(from->remote.target);
#endif  // __cpp_sized_deallocation
      return;
  }
  ABSL_UNREACHABLE();
}

// The manager that is used when a target function is in remote storage and the
// destructor of the type is not trivial
template <class T>
void RemoteManagerNontrivial(FunctionToCall operation,
                             TypeErasedState* const from,
                             TypeErasedState* const to) noexcept {
  static_assert(!IsStoredLocally<T>::value,
                "Remote storage must only be used for types that do not "
                "qualify for local storage.");

  switch (operation) {
    case FunctionToCall::relocate_from_to:
      // NOTE: Requires that the left-hand operand is already empty.
      to->remote.target = from->remote.target;
      return;
    case FunctionToCall::dispose:
      ::delete static_cast<T*>(from->remote.target);  // Must not throw.
      return;
  }
  ABSL_UNREACHABLE();
}

// The invoker that is used when a target function is in remote storage
template <bool SigIsNoexcept, class ReturnType, class QualTRef, class... P>
ReturnType RemoteInvoker(
    TypeErasedState* const state,
    ForwardedParameterType<P>... args) noexcept(SigIsNoexcept) {
  using RawT = RemoveCVRef<QualTRef>;
  static_assert(!IsStoredLocally<RawT>::value,
                "Target object must be in remote storage in order to be "
                "invoked from it.");

  auto& f = *static_cast<RawT*>(state->remote.target);
  return (InvokeR<ReturnType>)(static_cast<QualTRef>(f),
                               static_cast<ForwardedParameterType<P>>(args)...);
}

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction that checks if a type T is an instantiation of
// absl::in_place_type_t (needed for constructor constraints of AnyInvocable).
template <class T>
struct IsInPlaceType : std::false_type {};

template <class T>
struct IsInPlaceType<absl::in_place_type_t<T>> : std::true_type {};
//
////////////////////////////////////////////////////////////////////////////////

// A constructor name-tag used with CoreImpl (below) to request the
// conversion-constructor. QualDecayedTRef is the decayed-type of the object to
// wrap, along with the cv and reference qualifiers that must be applied when
// performing an invocation of the wrapped object.
template <class QualDecayedTRef>
struct TypedConversionConstruct {};

// A helper base class for all core operations of AnyInvocable. Most notably,
// this class creates the function call operator and constraint-checkers so that
// the top-level class does not have to be a series of partial specializations.
//
// Note: This definition exists (as opposed to being a declaration) so that if
// the user of the top-level template accidentally passes a template argument
// that is not a function type, they will get a static_assert in AnyInvocable's
// class body rather than an error stating that Impl is not defined.
template <class Sig>
class Impl {};  // Note: This is partially-specialized later.

// A std::unique_ptr deleter that deletes memory allocated via ::operator new.
#if defined(__cpp_sized_deallocation)
class TrivialDeleter {
 public:
  explicit TrivialDeleter(std::size_t size) : size_(size) {}

  void operator()(void* target) const {
    ::operator delete(target, size_);
  }

 private:
  std::size_t size_;
};
#else   // __cpp_sized_deallocation
class TrivialDeleter {
 public:
  explicit TrivialDeleter(std::size_t) {}

  void operator()(void* target) const { ::operator delete(target); }
};
#endif  // __cpp_sized_deallocation

template <bool SigIsNoexcept, class ReturnType, class... P>
class CoreImpl;

constexpr bool IsCompatibleConversion(void*, void*) { return false; }
template <bool NoExceptSrc, bool NoExceptDest, class... T>
constexpr bool IsCompatibleConversion(CoreImpl<NoExceptSrc, T...>*,
                                      CoreImpl<NoExceptDest, T...>*) {
  return !NoExceptDest || NoExceptSrc;
}

// A helper base class for all core operations of AnyInvocable that do not
// depend on the cv/ref qualifiers of the function type.
template <bool SigIsNoexcept, class ReturnType, class... P>
class CoreImpl {
 public:
  using result_type = ReturnType;

  CoreImpl() noexcept : manager_(EmptyManager), invoker_(nullptr) {}

  enum class TargetType {
    kPointer,
    kCompatibleAnyInvocable,
    kIncompatibleAnyInvocable,
    kOther,
  };

  // Note: QualDecayedTRef here includes the cv-ref qualifiers associated with
  // the invocation of the Invocable. The unqualified type is the target object
  // type to be stored.
  template <class QualDecayedTRef, class F>
  explicit CoreImpl(TypedConversionConstruct<QualDecayedTRef>, F&& f) {
    using DecayedT = RemoveCVRef<QualDecayedTRef>;

    constexpr TargetType kTargetType =
        (std::is_pointer<DecayedT>::value ||
         std::is_member_pointer<DecayedT>::value)
            ? TargetType::kPointer
        : IsCompatibleAnyInvocable<DecayedT>::value
            ? TargetType::kCompatibleAnyInvocable
        : IsAnyInvocable<DecayedT>::value
            ? TargetType::kIncompatibleAnyInvocable
            : TargetType::kOther;
    // NOTE: We only use integers instead of enums as template parameters in
    // order to work around a bug on C++14 under MSVC 2017.
    // See b/236131881.
    Initialize<kTargetType, QualDecayedTRef>(std::forward<F>(f));
  }

  // Note: QualTRef here includes the cv-ref qualifiers associated with the
  // invocation of the Invocable. The unqualified type is the target object
  // type to be stored.
  template <class QualTRef, class... Args>
  explicit CoreImpl(absl::in_place_type_t<QualTRef>, Args&&... args) {
    InitializeStorage<QualTRef>(std::forward<Args>(args)...);
  }

  CoreImpl(CoreImpl&& other) noexcept {
    other.manager_(FunctionToCall::relocate_from_to, &other.state_, &state_);
    manager_ = other.manager_;
    invoker_ = other.invoker_;
    other.manager_ = EmptyManager;
    other.invoker_ = nullptr;
  }

  CoreImpl& operator=(CoreImpl&& other) noexcept {
    // Put the left-hand operand in an empty state.
    //
    // Note: A full reset that leaves us with an object that has its invariants
    // intact is necessary in order to handle self-move. This is required by
    // types that are used with certain operations of the standard library, such
    // as the default definition of std::swap when both operands target the same
    // object.
    Clear();

    // Perform the actual move/destroy operation on the target function.
    other.manager_(FunctionToCall::relocate_from_to, &other.state_, &state_);
    manager_ = other.manager_;
    invoker_ = other.invoker_;
    other.manager_ = EmptyManager;
    other.invoker_ = nullptr;

    return *this;
  }

  ~CoreImpl() { manager_(FunctionToCall::dispose, &state_, &state_); }

  // Check whether or not the AnyInvocable is in the empty state.
  bool HasValue() const { return invoker_ != nullptr; }

  // Effects: Puts the object into its empty state.
  void Clear() {
    manager_(FunctionToCall::dispose, &state_, &state_);
    manager_ = EmptyManager;
    invoker_ = nullptr;
  }

  template <TargetType target_type, class QualDecayedTRef, class F,
            absl::enable_if_t<target_type == TargetType::kPointer, int> = 0>
  void Initialize(F&& f) {
// This condition handles types that decay into pointers, which includes
// function references. Since function references cannot be null, GCC warns
// against comparing their decayed form with nullptr.
// Since this is template-heavy code, we prefer to disable these warnings
// locally instead of adding yet another overload of this function.
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Waddress"
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
    if (static_cast<RemoveCVRef<QualDecayedTRef>>(f) == nullptr) {
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
      manager_ = EmptyManager;
      invoker_ = nullptr;
      return;
    }
    InitializeStorage<QualDecayedTRef>(std::forward<F>(f));
  }

  template <TargetType target_type, class QualDecayedTRef, class F,
            absl::enable_if_t<
                target_type == TargetType::kCompatibleAnyInvocable, int> = 0>
  void Initialize(F&& f) {
    // In this case we can "steal the guts" of the other AnyInvocable.
    f.manager_(FunctionToCall::relocate_from_to, &f.state_, &state_);
    manager_ = f.manager_;
    invoker_ = f.invoker_;

    f.manager_ = EmptyManager;
    f.invoker_ = nullptr;
  }

  template <TargetType target_type, class QualDecayedTRef, class F,
            absl::enable_if_t<
                target_type == TargetType::kIncompatibleAnyInvocable, int> = 0>
  void Initialize(F&& f) {
    if (f.HasValue()) {
      InitializeStorage<QualDecayedTRef>(std::forward<F>(f));
    } else {
      manager_ = EmptyManager;
      invoker_ = nullptr;
    }
  }

  template <TargetType target_type, class QualDecayedTRef, class F,
            typename = absl::enable_if_t<target_type == TargetType::kOther>>
  void Initialize(F&& f) {
    InitializeStorage<QualDecayedTRef>(std::forward<F>(f));
  }

  // Use local (inline) storage for applicable target object types.
  template <class QualTRef, class... Args,
            typename = absl::enable_if_t<
                IsStoredLocally<RemoveCVRef<QualTRef>>::value>>
  void InitializeStorage(Args&&... args) {
    using RawT = RemoveCVRef<QualTRef>;
    ::new (static_cast<void*>(&state_.storage))
        RawT(std::forward<Args>(args)...);

    invoker_ = LocalInvoker<SigIsNoexcept, ReturnType, QualTRef, P...>;
    // We can simplify our manager if we know the type is trivially copyable.
    InitializeLocalManager<RawT>();
  }

  // Use remote storage for target objects that cannot be stored locally.
  template <class QualTRef, class... Args,
            absl::enable_if_t<!IsStoredLocally<RemoveCVRef<QualTRef>>::value,
                              int> = 0>
  void InitializeStorage(Args&&... args) {
    InitializeRemoteManager<RemoveCVRef<QualTRef>>(std::forward<Args>(args)...);
    // This is set after everything else in case an exception is thrown in an
    // earlier step of the initialization.
    invoker_ = RemoteInvoker<SigIsNoexcept, ReturnType, QualTRef, P...>;
  }

  template <class T,
            typename = absl::enable_if_t<std::is_trivially_copyable<T>::value>>
  void InitializeLocalManager() {
    manager_ = LocalManagerTrivial;
  }

  template <class T,
            absl::enable_if_t<!std::is_trivially_copyable<T>::value, int> = 0>
  void InitializeLocalManager() {
    manager_ = LocalManagerNontrivial<T>;
  }

  template <class T>
  using HasTrivialRemoteStorage =
      std::integral_constant<bool, std::is_trivially_destructible<T>::value &&
                                       alignof(T) <=
                                           ABSL_INTERNAL_DEFAULT_NEW_ALIGNMENT>;

  template <class T, class... Args,
            typename = absl::enable_if_t<HasTrivialRemoteStorage<T>::value>>
  void InitializeRemoteManager(Args&&... args) {
    // unique_ptr is used for exception-safety in case construction throws.
    std::unique_ptr<void, TrivialDeleter> uninitialized_target(
        ::operator new(sizeof(T)), TrivialDeleter(sizeof(T)));
    ::new (uninitialized_target.get()) T(std::forward<Args>(args)...);
    state_.remote.target = uninitialized_target.release();
    state_.remote.size = sizeof(T);
    manager_ = RemoteManagerTrivial;
  }

  template <class T, class... Args,
            absl::enable_if_t<!HasTrivialRemoteStorage<T>::value, int> = 0>
  void InitializeRemoteManager(Args&&... args) {
    state_.remote.target = ::new T(std::forward<Args>(args)...);
    manager_ = RemoteManagerNontrivial<T>;
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // Type trait to determine if the template argument is an AnyInvocable whose
  // function type is compatible enough with ours such that we can
  // "move the guts" out of it when moving, rather than having to place a new
  // object into remote storage.

  template <typename Other>
  struct IsCompatibleAnyInvocable {
    static constexpr bool value = false;
  };

  template <typename Sig>
  struct IsCompatibleAnyInvocable<AnyInvocable<Sig>> {
    static constexpr bool value =
        (IsCompatibleConversion)(static_cast<
                                     typename AnyInvocable<Sig>::CoreImpl*>(
                                     nullptr),
                                 static_cast<CoreImpl*>(nullptr));
  };

  //
  //////////////////////////////////////////////////////////////////////////////

  TypeErasedState state_;
  ManagerType* manager_;
  InvokerType<SigIsNoexcept, ReturnType, P...>* invoker_;
};

// A constructor name-tag used with Impl to request the
// conversion-constructor
struct ConversionConstruct {};

////////////////////////////////////////////////////////////////////////////////
//
// A metafunction that is normally an identity metafunction except that when
// given a std::reference_wrapper<T>, it yields T&. This is necessary because
// currently std::reference_wrapper's operator() is not conditionally noexcept,
// so when checking if such an Invocable is nothrow-invocable, we must pull out
// the underlying type.
template <class T>
struct UnwrapStdReferenceWrapperImpl {
  using type = T;
};

template <class T>
struct UnwrapStdReferenceWrapperImpl<std::reference_wrapper<T>> {
  using type = T&;
};

template <class T>
using UnwrapStdReferenceWrapper =
    typename UnwrapStdReferenceWrapperImpl<T>::type;
//
////////////////////////////////////////////////////////////////////////////////

// An alias that always yields std::true_type (used with constraints) where
// substitution failures happen when forming the template arguments.
template <class... T>
using TrueAlias =
    std::integral_constant<bool, sizeof(absl::void_t<T...>*) != 0>;

/*SFINAE constraints for the conversion-constructor.*/
template <class Sig, class F,
          class = absl::enable_if_t<
              !std::is_same<RemoveCVRef<F>, AnyInvocable<Sig>>::value>>
using CanConvert = TrueAlias<
    absl::enable_if_t<!IsInPlaceType<RemoveCVRef<F>>::value>,
    absl::enable_if_t<Impl<Sig>::template CallIsValid<F>::value>,
    absl::enable_if_t<
        Impl<Sig>::template CallIsNoexceptIfSigIsNoexcept<F>::value>,
    absl::enable_if_t<std::is_constructible<absl::decay_t<F>, F>::value>>;

/*SFINAE constraints for the std::in_place constructors.*/
template <class Sig, class F, class... Args>
using CanEmplace = TrueAlias<
    absl::enable_if_t<Impl<Sig>::template CallIsValid<F>::value>,
    absl::enable_if_t<
        Impl<Sig>::template CallIsNoexceptIfSigIsNoexcept<F>::value>,
    absl::enable_if_t<std::is_constructible<absl::decay_t<F>, Args...>::value>>;

/*SFINAE constraints for the conversion-assign operator.*/
template <class Sig, class F,
          class = absl::enable_if_t<
              !std::is_same<RemoveCVRef<F>, AnyInvocable<Sig>>::value>>
using CanAssign = TrueAlias<
    absl::enable_if_t<Impl<Sig>::template CallIsValid<F>::value>,
    absl::enable_if_t<
        Impl<Sig>::template CallIsNoexceptIfSigIsNoexcept<F>::value>,
    absl::enable_if_t<std::is_constructible<absl::decay_t<F>, F>::value>>;

/*SFINAE constraints for the reference-wrapper conversion-assign operator.*/
template <class Sig, class F>
using CanAssignReferenceWrapper = TrueAlias<
    absl::enable_if_t<
        Impl<Sig>::template CallIsValid<std::reference_wrapper<F>>::value>,
    absl::enable_if_t<Impl<Sig>::template CallIsNoexceptIfSigIsNoexcept<
        std::reference_wrapper<F>>::value>>;

////////////////////////////////////////////////////////////////////////////////
//
// The constraint for checking whether or not a call meets the noexcept
// callability requirements. This is a preprocessor macro because specifying it
// this way as opposed to a disjunction/branch can improve the user-side error
// messages and avoids an instantiation of std::is_nothrow_invocable_r in the
// cases where the user did not specify a noexcept function type.
//
#define ABSL_INTERNAL_ANY_INVOCABLE_NOEXCEPT_CONSTRAINT(inv_quals, noex) \
  ABSL_INTERNAL_ANY_INVOCABLE_NOEXCEPT_CONSTRAINT_##noex(inv_quals)

// The disjunction below is because we can't rely on std::is_nothrow_invocable_r
// to give the right result when ReturnType is non-moveable in toolchains that
// don't treat non-moveable result types correctly. For example this was the
// case in libc++ before commit c3a24882 (2022-05).
#define ABSL_INTERNAL_ANY_INVOCABLE_NOEXCEPT_CONSTRAINT_true(inv_quals)      \
  absl::enable_if_t<absl::disjunction<                                       \
      std::is_nothrow_invocable_r<                                           \
          ReturnType, UnwrapStdReferenceWrapper<absl::decay_t<F>> inv_quals, \
          P...>,                                                             \
      std::conjunction<                                                      \
          std::is_nothrow_invocable<                                         \
              UnwrapStdReferenceWrapper<absl::decay_t<F>> inv_quals, P...>,  \
          std::is_same<                                                      \
              ReturnType,                                                    \
              absl::base_internal::invoke_result_t<                          \
                  UnwrapStdReferenceWrapper<absl::decay_t<F>> inv_quals,     \
                  P...>>>>::value>

#define ABSL_INTERNAL_ANY_INVOCABLE_NOEXCEPT_CONSTRAINT_false(inv_quals)
//
////////////////////////////////////////////////////////////////////////////////

// A macro to generate partial specializations of Impl with the different
// combinations of supported cv/reference qualifiers and noexcept specifier.
//
// Here, `cv` are the cv-qualifiers if any, `ref` is the ref-qualifier if any,
// inv_quals is the reference type to be used when invoking the target, and
// noex is "true" if the function type is noexcept, or false if it is not.
//
// The CallIsValid condition is more complicated than simply using
// absl::base_internal::is_invocable_r because we can't rely on it to give the
// right result when ReturnType is non-moveable in toolchains that don't treat
// non-moveable result types correctly. For example this was the case in libc++
// before commit c3a24882 (2022-05).
#define ABSL_INTERNAL_ANY_INVOCABLE_IMPL_(cv, ref, inv_quals, noex)            \
  template <class ReturnType, class... P>                                      \
  class Impl<ReturnType(P...) cv ref ABSL_INTERNAL_NOEXCEPT_SPEC(noex)>        \
      : public CoreImpl<noex, ReturnType, P...> {                              \
   public:                                                                     \
    /*The base class, which contains the datamembers and core operations*/     \
    using Core = CoreImpl<noex, ReturnType, P...>;                             \
                                                                               \
    /*SFINAE constraint to check if F is invocable with the proper signature*/ \
    template <class F>                                                         \
    using CallIsValid = TrueAlias<absl::enable_if_t<absl::disjunction<         \
        absl::base_internal::is_invocable_r<ReturnType,                        \
                                            absl::decay_t<F> inv_quals, P...>, \
        std::is_same<ReturnType,                                               \
                     absl::base_internal::invoke_result_t<                     \
                         absl::decay_t<F> inv_quals, P...>>>::value>>;         \
                                                                               \
    /*SFINAE constraint to check if F is nothrow-invocable when necessary*/    \
    template <class F>                                                         \
    using CallIsNoexceptIfSigIsNoexcept =                                      \
        TrueAlias<ABSL_INTERNAL_ANY_INVOCABLE_NOEXCEPT_CONSTRAINT(inv_quals,   \
                                                                  noex)>;      \
                                                                               \
    /*Put the AnyInvocable into an empty state.*/                              \
    Impl() = default;                                                          \
                                                                               \
    /*The implementation of a conversion-constructor from "f*/                 \
    /*This forwards to Core, attaching inv_quals so that the base class*/      \
    /*knows how to properly type-erase the invocation.*/                       \
    template <class F>                                                         \
    explicit Impl(ConversionConstruct, F&& f)                                  \
        : Core(TypedConversionConstruct<                                       \
                   typename std::decay<F>::type inv_quals>(),                  \
               std::forward<F>(f)) {}                                          \
                                                                               \
    /*Forward along the in-place construction parameters.*/                    \
    template <class T, class... Args>                                          \
    explicit Impl(absl::in_place_type_t<T>, Args&&... args)                    \
        : Core(absl::in_place_type<absl::decay_t<T> inv_quals>,                \
               std::forward<Args>(args)...) {}                                 \
                                                                               \
    /*Raises a fatal error when the AnyInvocable is invoked after a move*/     \
    static ReturnType InvokedAfterMove(                                        \
      TypeErasedState*,                                                        \
      ForwardedParameterType<P>...) noexcept(noex) {                           \
      ABSL_HARDENING_ASSERT(false && "AnyInvocable use-after-move");           \
      std::terminate();                                                        \
    }                                                                          \
                                                                               \
    InvokerType<noex, ReturnType, P...>* ExtractInvoker() cv {                 \
      using QualifiedTestType = int cv ref;                                    \
      auto* invoker = this->invoker_;                                          \
      if (!std::is_const<QualifiedTestType>::value &&                          \
          std::is_rvalue_reference<QualifiedTestType>::value) {                \
        ABSL_ASSERT([this]() {                                                 \
          /* We checked that this isn't const above, so const_cast is safe */  \
          const_cast<Impl*>(this)->invoker_ = InvokedAfterMove;                \
          return this->HasValue();                                             \
        }());                                                                  \
      }                                                                        \
      return invoker;                                                          \
    }                                                                          \
                                                                               \
    /*The actual invocation operation with the proper signature*/              \
    ReturnType operator()(P... args) cv ref noexcept(noex) {                   \
      assert(this->invoker_ != nullptr);                                       \
      return this->ExtractInvoker()(                                           \
          const_cast<TypeErasedState*>(&this->state_),                         \
          static_cast<ForwardedParameterType<P>>(args)...);                    \
    }                                                                          \
  }

// Define the `noexcept(true)` specialization only for C++17 and beyond, when
// `noexcept` is part of the type system.
#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 201703L
// A convenience macro that defines specializations for the noexcept(true) and
// noexcept(false) forms, given the other properties.
#define ABSL_INTERNAL_ANY_INVOCABLE_IMPL(cv, ref, inv_quals)    \
  ABSL_INTERNAL_ANY_INVOCABLE_IMPL_(cv, ref, inv_quals, false); \
  ABSL_INTERNAL_ANY_INVOCABLE_IMPL_(cv, ref, inv_quals, true)
#else
#define ABSL_INTERNAL_ANY_INVOCABLE_IMPL(cv, ref, inv_quals) \
  ABSL_INTERNAL_ANY_INVOCABLE_IMPL_(cv, ref, inv_quals, false)
#endif

// Non-ref-qualified partial specializations
ABSL_INTERNAL_ANY_INVOCABLE_IMPL(, , &);
ABSL_INTERNAL_ANY_INVOCABLE_IMPL(const, , const&);

// Lvalue-ref-qualified partial specializations
ABSL_INTERNAL_ANY_INVOCABLE_IMPL(, &, &);
ABSL_INTERNAL_ANY_INVOCABLE_IMPL(const, &, const&);

// Rvalue-ref-qualified partial specializations
ABSL_INTERNAL_ANY_INVOCABLE_IMPL(, &&, &&);
ABSL_INTERNAL_ANY_INVOCABLE_IMPL(const, &&, const&&);

// Undef the detail-only macros.
#undef ABSL_INTERNAL_ANY_INVOCABLE_IMPL
#undef ABSL_INTERNAL_ANY_INVOCABLE_IMPL_
#undef ABSL_INTERNAL_ANY_INVOCABLE_NOEXCEPT_CONSTRAINT_false
#undef ABSL_INTERNAL_ANY_INVOCABLE_NOEXCEPT_CONSTRAINT_true
#undef ABSL_INTERNAL_ANY_INVOCABLE_NOEXCEPT_CONSTRAINT
#undef ABSL_INTERNAL_NOEXCEPT_SPEC

}  // namespace internal_any_invocable
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FUNCTIONAL_INTERNAL_ANY_INVOCABLE_H_
