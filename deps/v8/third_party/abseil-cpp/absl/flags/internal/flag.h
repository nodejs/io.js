//
// Copyright 2019 The Abseil Authors.
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

#ifndef ABSL_FLAGS_INTERNAL_FLAG_H_
#define ABSL_FLAGS_INTERNAL_FLAG_H_

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/config.h"
#include "absl/flags/internal/commandlineflag.h"
#include "absl/flags/internal/registry.h"
#include "absl/flags/internal/sequence_lock.h"
#include "absl/flags/marshalling.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

///////////////////////////////////////////////////////////////////////////////
// Forward declaration of absl::Flag<T> public API.
namespace flags_internal {
template <typename T>
class Flag;
}  // namespace flags_internal

template <typename T>
using Flag = flags_internal::Flag<T>;

template <typename T>
ABSL_MUST_USE_RESULT T GetFlag(const absl::Flag<T>& flag);

template <typename T>
void SetFlag(absl::Flag<T>* flag, const T& v);

template <typename T, typename V>
void SetFlag(absl::Flag<T>* flag, const V& v);

template <typename U>
const CommandLineFlag& GetFlagReflectionHandle(const absl::Flag<U>& f);

///////////////////////////////////////////////////////////////////////////////
// Flag value type operations, eg., parsing, copying, etc. are provided
// by function specific to that type with a signature matching FlagOpFn.

namespace flags_internal {

enum class FlagOp {
  kAlloc,
  kDelete,
  kCopy,
  kCopyConstruct,
  kSizeof,
  kFastTypeId,
  kRuntimeTypeId,
  kParse,
  kUnparse,
  kValueOffset,
};
using FlagOpFn = void* (*)(FlagOp, const void*, void*, void*);

// Forward declaration for Flag value specific operations.
template <typename T>
void* FlagOps(FlagOp op, const void* v1, void* v2, void* v3);

// Allocate aligned memory for a flag value.
inline void* Alloc(FlagOpFn op) {
  return op(FlagOp::kAlloc, nullptr, nullptr, nullptr);
}
// Deletes memory interpreting obj as flag value type pointer.
inline void Delete(FlagOpFn op, void* obj) {
  op(FlagOp::kDelete, nullptr, obj, nullptr);
}
// Copies src to dst interpreting as flag value type pointers.
inline void Copy(FlagOpFn op, const void* src, void* dst) {
  op(FlagOp::kCopy, src, dst, nullptr);
}
// Construct a copy of flag value in a location pointed by dst
// based on src - pointer to the flag's value.
inline void CopyConstruct(FlagOpFn op, const void* src, void* dst) {
  op(FlagOp::kCopyConstruct, src, dst, nullptr);
}
// Makes a copy of flag value pointed by obj.
inline void* Clone(FlagOpFn op, const void* obj) {
  void* res = flags_internal::Alloc(op);
  flags_internal::CopyConstruct(op, obj, res);
  return res;
}
// Returns true if parsing of input text is successful.
inline bool Parse(FlagOpFn op, absl::string_view text, void* dst,
                  std::string* error) {
  return op(FlagOp::kParse, &text, dst, error) != nullptr;
}
// Returns string representing supplied value.
inline std::string Unparse(FlagOpFn op, const void* val) {
  std::string result;
  op(FlagOp::kUnparse, val, &result, nullptr);
  return result;
}
// Returns size of flag value type.
inline size_t Sizeof(FlagOpFn op) {
  // This sequence of casts reverses the sequence from
  // `flags_internal::FlagOps()`
  return static_cast<size_t>(reinterpret_cast<intptr_t>(
      op(FlagOp::kSizeof, nullptr, nullptr, nullptr)));
}
// Returns fast type id corresponding to the value type.
inline FlagFastTypeId FastTypeId(FlagOpFn op) {
  return reinterpret_cast<FlagFastTypeId>(
      op(FlagOp::kFastTypeId, nullptr, nullptr, nullptr));
}
// Returns fast type id corresponding to the value type.
inline const std::type_info* RuntimeTypeId(FlagOpFn op) {
  return reinterpret_cast<const std::type_info*>(
      op(FlagOp::kRuntimeTypeId, nullptr, nullptr, nullptr));
}
// Returns offset of the field value_ from the field impl_ inside of
// absl::Flag<T> data. Given FlagImpl pointer p you can get the
// location of the corresponding value as:
//      reinterpret_cast<char*>(p) + ValueOffset().
inline ptrdiff_t ValueOffset(FlagOpFn op) {
  // This sequence of casts reverses the sequence from
  // `flags_internal::FlagOps()`
  return static_cast<ptrdiff_t>(reinterpret_cast<intptr_t>(
      op(FlagOp::kValueOffset, nullptr, nullptr, nullptr)));
}

// Returns an address of RTTI's typeid(T).
template <typename T>
inline const std::type_info* GenRuntimeTypeId() {
#ifdef ABSL_INTERNAL_HAS_RTTI
  return &typeid(T);
#else
  return nullptr;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Flag help auxiliary structs.

// This is help argument for absl::Flag encapsulating the string literal pointer
// or pointer to function generating it as well as enum descriminating two
// cases.
using HelpGenFunc = std::string (*)();

template <size_t N>
struct FixedCharArray {
  char value[N];

  template <size_t... I>
  static constexpr FixedCharArray<N> FromLiteralString(
      absl::string_view str, absl::index_sequence<I...>) {
    return (void)str, FixedCharArray<N>({{str[I]..., '\0'}});
  }
};

template <typename Gen, size_t N = Gen::Value().size()>
constexpr FixedCharArray<N + 1> HelpStringAsArray(int) {
  return FixedCharArray<N + 1>::FromLiteralString(
      Gen::Value(), absl::make_index_sequence<N>{});
}

template <typename Gen>
constexpr std::false_type HelpStringAsArray(char) {
  return std::false_type{};
}

union FlagHelpMsg {
  constexpr explicit FlagHelpMsg(const char* help_msg) : literal(help_msg) {}
  constexpr explicit FlagHelpMsg(HelpGenFunc help_gen) : gen_func(help_gen) {}

  const char* literal;
  HelpGenFunc gen_func;
};

enum class FlagHelpKind : uint8_t { kLiteral = 0, kGenFunc = 1 };

struct FlagHelpArg {
  FlagHelpMsg source;
  FlagHelpKind kind;
};

extern const char kStrippedFlagHelp[];

// These two HelpArg overloads allows us to select at compile time one of two
// way to pass Help argument to absl::Flag. We'll be passing
// AbslFlagHelpGenFor##name as Gen and integer 0 as a single argument to prefer
// first overload if possible. If help message is evaluatable on constexpr
// context We'll be able to make FixedCharArray out of it and we'll choose first
// overload. In this case the help message expression is immediately evaluated
// and is used to construct the absl::Flag. No additional code is generated by
// ABSL_FLAG Otherwise SFINAE kicks in and first overload is dropped from the
// consideration, in which case the second overload will be used. The second
// overload does not attempt to evaluate the help message expression
// immediately and instead delays the evaluation by returning the function
// pointer (&T::NonConst) generating the help message when necessary. This is
// evaluatable in constexpr context, but the cost is an extra function being
// generated in the ABSL_FLAG code.
template <typename Gen, size_t N>
constexpr FlagHelpArg HelpArg(const FixedCharArray<N>& value) {
  return {FlagHelpMsg(value.value), FlagHelpKind::kLiteral};
}

template <typename Gen>
constexpr FlagHelpArg HelpArg(std::false_type) {
  return {FlagHelpMsg(&Gen::NonConst), FlagHelpKind::kGenFunc};
}

///////////////////////////////////////////////////////////////////////////////
// Flag default value auxiliary structs.

// Signature for the function generating the initial flag value (usually
// based on default value supplied in flag's definition)
using FlagDfltGenFunc = void (*)(void*);

union FlagDefaultSrc {
  constexpr explicit FlagDefaultSrc(FlagDfltGenFunc gen_func_arg)
      : gen_func(gen_func_arg) {}

#define ABSL_FLAGS_INTERNAL_DFLT_FOR_TYPE(T, name) \
  T name##_value;                                  \
  constexpr explicit FlagDefaultSrc(T value) : name##_value(value) {}  // NOLINT
  ABSL_FLAGS_INTERNAL_BUILTIN_TYPES(ABSL_FLAGS_INTERNAL_DFLT_FOR_TYPE)
#undef ABSL_FLAGS_INTERNAL_DFLT_FOR_TYPE

  void* dynamic_value;
  FlagDfltGenFunc gen_func;
};

enum class FlagDefaultKind : uint8_t {
  kDynamicValue = 0,
  kGenFunc = 1,
  kOneWord = 2  // for default values UP to one word in size
};

struct FlagDefaultArg {
  FlagDefaultSrc source;
  FlagDefaultKind kind;
};

// This struct and corresponding overload to InitDefaultValue are used to
// facilitate usage of {} as default value in ABSL_FLAG macro.
// TODO(rogeeff): Fix handling types with explicit constructors.
struct EmptyBraces {};

template <typename T>
constexpr T InitDefaultValue(T t) {
  return t;
}

template <typename T>
constexpr T InitDefaultValue(EmptyBraces) {
  return T{};
}

template <typename ValueT, typename GenT,
          typename std::enable_if<std::is_integral<ValueT>::value, int>::type =
              ((void)GenT{}, 0)>
constexpr FlagDefaultArg DefaultArg(int) {
  return {FlagDefaultSrc(GenT{}.value), FlagDefaultKind::kOneWord};
}

template <typename ValueT, typename GenT>
constexpr FlagDefaultArg DefaultArg(char) {
  return {FlagDefaultSrc(&GenT::Gen), FlagDefaultKind::kGenFunc};
}

///////////////////////////////////////////////////////////////////////////////
// Flag storage selector traits. Each trait indicates what kind of storage kind
// to use for the flag value.

template <typename T>
using FlagUseValueAndInitBitStorage =
    std::integral_constant<bool, std::is_trivially_copyable<T>::value &&
                                     std::is_default_constructible<T>::value &&
                                     (sizeof(T) < 8)>;

template <typename T>
using FlagUseOneWordStorage =
    std::integral_constant<bool, std::is_trivially_copyable<T>::value &&
                                     (sizeof(T) <= 8)>;

template <class T>
using FlagUseSequenceLockStorage =
    std::integral_constant<bool, std::is_trivially_copyable<T>::value &&
                                     (sizeof(T) > 8)>;

enum class FlagValueStorageKind : uint8_t {
  kValueAndInitBit = 0,
  kOneWordAtomic = 1,
  kSequenceLocked = 2,
  kHeapAllocated = 3,
};

// This constexpr function returns the storage kind for the given flag value
// type.
template <typename T>
static constexpr FlagValueStorageKind StorageKind() {
  return FlagUseValueAndInitBitStorage<T>::value
             ? FlagValueStorageKind::kValueAndInitBit
         : FlagUseOneWordStorage<T>::value
             ? FlagValueStorageKind::kOneWordAtomic
         : FlagUseSequenceLockStorage<T>::value
             ? FlagValueStorageKind::kSequenceLocked
             : FlagValueStorageKind::kHeapAllocated;
}

// This is a base class for the storage classes used by kOneWordAtomic and
// kValueAndInitBit storage kinds. It literally just stores the one word value
// as an atomic. By default, it is initialized to a magic value that is unlikely
// a valid value for the flag value type.
struct FlagOneWordValue {
  constexpr static int64_t Uninitialized() {
    return static_cast<int64_t>(0xababababababababll);
  }

  constexpr FlagOneWordValue() : value(Uninitialized()) {}
  constexpr explicit FlagOneWordValue(int64_t v) : value(v) {}
  std::atomic<int64_t> value;
};

// This class represents a memory layout used by kValueAndInitBit storage kind.
template <typename T>
struct alignas(8) FlagValueAndInitBit {
  T value;
  // Use an int instead of a bool to guarantee that a non-zero value has
  // a bit set.
  uint8_t init;
};

// This class implements an aligned pointer with two options stored via masks
// in unused bits of the pointer value (due to alignment requirement).
//  - IsUnprotectedReadCandidate - indicates that the value can be switched to
//    unprotected read without a lock.
//  - HasBeenRead - indicates that the value has been read at least once.
//  - AllowsUnprotectedRead - combination of the two options above and indicates
//    that the value can now be read without a lock.
// Further details of these options and their use is covered in the description
// of the FlagValue<T, FlagValueStorageKind::kHeapAllocated> specialization.
class MaskedPointer {
 public:
  using mask_t = uintptr_t;
  using ptr_t = void*;

  static constexpr int RequiredAlignment() { return 4; }

  constexpr explicit MaskedPointer(ptr_t rhs) : ptr_(rhs) {}
  MaskedPointer(ptr_t rhs, bool is_candidate);

  void* Ptr() const {
    return reinterpret_cast<void*>(reinterpret_cast<mask_t>(ptr_) &
                                   kPtrValueMask);
  }
  bool AllowsUnprotectedRead() const {
    return (reinterpret_cast<mask_t>(ptr_) & kAllowsUnprotectedRead) ==
           kAllowsUnprotectedRead;
  }
  bool IsUnprotectedReadCandidate() const;
  bool HasBeenRead() const;

  void Set(FlagOpFn op, const void* src, bool is_candidate);
  void MarkAsRead();

 private:
  // Masks
  // Indicates that the flag value either default or originated from command
  // line.
  static constexpr mask_t kUnprotectedReadCandidate = 0x1u;
  // Indicates that flag has been read.
  static constexpr mask_t kHasBeenRead = 0x2u;
  static constexpr mask_t kAllowsUnprotectedRead =
      kUnprotectedReadCandidate | kHasBeenRead;
  static constexpr mask_t kPtrValueMask = ~kAllowsUnprotectedRead;

  void ApplyMask(mask_t mask);
  bool CheckMask(mask_t mask) const;

  ptr_t ptr_;
};

// This class implements a type erased storage of the heap allocated flag value.
// It is used as a base class for the storage class for kHeapAllocated storage
// kind. The initial_buffer is expected to have an alignment of at least
// MaskedPointer::RequiredAlignment(), so that the bits used by the
// MaskedPointer to store masks are set to 0. This guarantees that value starts
// in an uninitialized state.
struct FlagMaskedPointerValue {
  constexpr explicit FlagMaskedPointerValue(MaskedPointer::ptr_t initial_buffer)
      : value(MaskedPointer(initial_buffer)) {}

  std::atomic<MaskedPointer> value;
};

// This is the forward declaration for the template that represents a storage
// for the flag values. This template is expected to be explicitly specialized
// for each storage kind and it does not have a generic default
// implementation.
template <typename T,
          FlagValueStorageKind Kind = flags_internal::StorageKind<T>()>
struct FlagValue;

// This specialization represents the storage of flag values types with the
// kValueAndInitBit storage kind. It is based on the FlagOneWordValue class
// and relies on memory layout in FlagValueAndInitBit<T> to indicate that the
// value has been initialized or not.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kValueAndInitBit> : FlagOneWordValue {
  constexpr FlagValue() : FlagOneWordValue(0) {}
  bool Get(const SequenceLock&, T& dst) const {
    int64_t storage = value.load(std::memory_order_acquire);
    if (ABSL_PREDICT_FALSE(storage == 0)) {
      // This assert is to ensure that the initialization inside FlagImpl::Init
      // is able to set init member correctly.
      static_assert(offsetof(FlagValueAndInitBit<T>, init) == sizeof(T),
                    "Unexpected memory layout of FlagValueAndInitBit");
      return false;
    }
    dst = absl::bit_cast<FlagValueAndInitBit<T>>(storage).value;
    return true;
  }
};

// This specialization represents the storage of flag values types with the
// kOneWordAtomic storage kind. It is based on the FlagOneWordValue class
// and relies on the magic uninitialized state of default constructed instead of
// FlagOneWordValue to indicate that the value has been initialized or not.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kOneWordAtomic> : FlagOneWordValue {
  constexpr FlagValue() : FlagOneWordValue() {}
  bool Get(const SequenceLock&, T& dst) const {
    int64_t one_word_val = value.load(std::memory_order_acquire);
    if (ABSL_PREDICT_FALSE(one_word_val == FlagOneWordValue::Uninitialized())) {
      return false;
    }
    std::memcpy(&dst, static_cast<const void*>(&one_word_val), sizeof(T));
    return true;
  }
};

// This specialization represents the storage of flag values types with the
// kSequenceLocked storage kind. This storage is used by trivially copyable
// types with size greater than 8 bytes. This storage relies on uninitialized
// state of the SequenceLock to indicate that the value has been initialized or
// not. This storage also provides lock-free read access to the underlying
// value once it is initialized.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kSequenceLocked> {
  bool Get(const SequenceLock& lock, T& dst) const {
    return lock.TryRead(&dst, value_words, sizeof(T));
  }

  static constexpr int kNumWords =
      flags_internal::AlignUp(sizeof(T), sizeof(uint64_t)) / sizeof(uint64_t);

  alignas(T) alignas(
      std::atomic<uint64_t>) std::atomic<uint64_t> value_words[kNumWords];
};

// This specialization represents the storage of flag values types with the
// kHeapAllocated storage kind. This is a storage of last resort and is used
// if none of other storage kinds are applicable.
//
// Generally speaking the values with this storage kind can't be accessed
// atomically and thus can't be read without holding a lock. If we would ever
// want to avoid the lock, we'd need to leak the old value every time new flag
// value is being set (since we are in danger of having a race condition
// otherwise).
//
// Instead of doing that, this implementation attempts to cater to some common
// use cases by allowing at most 2 values to be leaked - default value and
// value set from the command line.
//
// This specialization provides an initial buffer for the first flag value. This
// is where the default value is going to be stored. We attempt to reuse this
// buffer if possible, including storing the value set from the command line
// there.
//
// As long as we only read this value, we can access it without a lock (in
// practice we still use the lock for the very first read to be able set
// "has been read" option on this flag).
//
// If flag is specified on the command line we store the parsed value either
// in the internal buffer (if the default value never been read) or we leak the
// default value and allocate the new storage for the parse value. This value is
// also a candidate for an unprotected read. If flag is set programmatically
// after the command line is parsed, the storage for this value is going to be
// leaked. Note that in both scenarios we are not going to have a real leak.
// Instead we'll store the leaked value pointers in the internal freelist to
// avoid triggering the memory leak checker complains.
//
// If the flag is ever set programmatically, it stops being the candidate for an
// unprotected read, and any follow up access to the flag value requires a lock.
// Note that if the value if set programmatically before the command line is
// parsed, we can switch back to enabling unprotected reads for that value.
template <typename T>
struct FlagValue<T, FlagValueStorageKind::kHeapAllocated>
    : FlagMaskedPointerValue {
  // We const initialize the value with unmasked pointer to the internal buffer,
  // making sure it is not a candidate for unprotected read. This way we can
  // ensure Init is done before any access to the flag value.
  constexpr FlagValue() : FlagMaskedPointerValue(&buffer[0]) {}

  bool Get(const SequenceLock&, T& dst) const {
    MaskedPointer ptr_value = value.load(std::memory_order_acquire);

    if (ABSL_PREDICT_TRUE(ptr_value.AllowsUnprotectedRead())) {
      ::new (static_cast<void*>(&dst)) T(*static_cast<T*>(ptr_value.Ptr()));
      return true;
    }
    return false;
  }

  alignas(MaskedPointer::RequiredAlignment()) alignas(
      T) char buffer[sizeof(T)]{};
};

///////////////////////////////////////////////////////////////////////////////
// Flag callback auxiliary structs.

// Signature for the mutation callback used by watched Flags
// The callback is noexcept.
// TODO(rogeeff): add noexcept after C++17 support is added.
using FlagCallbackFunc = void (*)();

struct FlagCallback {
  FlagCallbackFunc func;
  absl::Mutex guard;  // Guard for concurrent callback invocations.
};

///////////////////////////////////////////////////////////////////////////////
// Flag implementation, which does not depend on flag value type.
// The class encapsulates the Flag's data and access to it.

struct DynValueDeleter {
  explicit DynValueDeleter(FlagOpFn op_arg = nullptr);
  void operator()(void* ptr) const;

  FlagOpFn op;
};

class FlagState;

// These are only used as constexpr global objects.
// They do not use a virtual destructor to simplify their implementation.
// They are not destroyed except at program exit, so leaks do not matter.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
class FlagImpl final : public CommandLineFlag {
 public:
  constexpr FlagImpl(const char* name, const char* filename, FlagOpFn op,
                     FlagHelpArg help, FlagValueStorageKind value_kind,
                     FlagDefaultArg default_arg)
      : name_(name),
        filename_(filename),
        op_(op),
        help_(help.source),
        help_source_kind_(static_cast<uint8_t>(help.kind)),
        value_storage_kind_(static_cast<uint8_t>(value_kind)),
        def_kind_(static_cast<uint8_t>(default_arg.kind)),
        modified_(false),
        on_command_line_(false),
        callback_(nullptr),
        default_value_(default_arg.source),
        data_guard_{} {}

  // Constant access methods
  int64_t ReadOneWord() const ABSL_LOCKS_EXCLUDED(*DataGuard());
  bool ReadOneBool() const ABSL_LOCKS_EXCLUDED(*DataGuard());
  void Read(void* dst) const override ABSL_LOCKS_EXCLUDED(*DataGuard());
  void Read(bool* value) const ABSL_LOCKS_EXCLUDED(*DataGuard()) {
    *value = ReadOneBool();
  }
  template <typename T,
            absl::enable_if_t<flags_internal::StorageKind<T>() ==
                                  FlagValueStorageKind::kOneWordAtomic,
                              int> = 0>
  void Read(T* value) const ABSL_LOCKS_EXCLUDED(*DataGuard()) {
    int64_t v = ReadOneWord();
    std::memcpy(value, static_cast<const void*>(&v), sizeof(T));
  }
  template <typename T,
            typename std::enable_if<flags_internal::StorageKind<T>() ==
                                        FlagValueStorageKind::kValueAndInitBit,
                                    int>::type = 0>
  void Read(T* value) const ABSL_LOCKS_EXCLUDED(*DataGuard()) {
    *value = absl::bit_cast<FlagValueAndInitBit<T>>(ReadOneWord()).value;
  }

  // Mutating access methods
  void Write(const void* src) ABSL_LOCKS_EXCLUDED(*DataGuard());

  // Interfaces to operate on callbacks.
  void SetCallback(const FlagCallbackFunc mutation_callback)
      ABSL_LOCKS_EXCLUDED(*DataGuard());
  void InvokeCallback() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());

  // Used in read/write operations to validate source/target has correct type.
  // For example if flag is declared as absl::Flag<int> FLAGS_foo, a call to
  // absl::GetFlag(FLAGS_foo) validates that the type of FLAGS_foo is indeed
  // int. To do that we pass the assumed type id (which is deduced from type
  // int) as an argument `type_id`, which is in turn is validated against the
  // type id stored in flag object by flag definition statement.
  void AssertValidType(FlagFastTypeId type_id,
                       const std::type_info* (*gen_rtti)()) const;

 private:
  template <typename T>
  friend class Flag;
  friend class FlagState;

  // Ensures that `data_guard_` is initialized and returns it.
  absl::Mutex* DataGuard() const
      ABSL_LOCK_RETURNED(reinterpret_cast<absl::Mutex*>(data_guard_));
  // Returns heap allocated value of type T initialized with default value.
  std::unique_ptr<void, DynValueDeleter> MakeInitValue() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());
  // Flag initialization called via absl::call_once.
  void Init();

  // Offset value access methods. One per storage kind. These methods to not
  // respect const correctness, so be very careful using them.

  // This is a shared helper routine which encapsulates most of the magic. Since
  // it is only used inside the three routines below, which are defined in
  // flag.cc, we can define it in that file as well.
  template <typename StorageT>
  StorageT* OffsetValue() const;

  // The same as above, but used for sequencelock-protected storage.
  std::atomic<uint64_t>* AtomicBufferValue() const;

  // This is an accessor for a value stored as one word atomic. Returns a
  // mutable reference to an atomic value.
  std::atomic<int64_t>& OneWordValue() const;

  std::atomic<MaskedPointer>& PtrStorage() const;

  // Attempts to parse supplied `value` string. If parsing is successful,
  // returns new value. Otherwise returns nullptr.
  std::unique_ptr<void, DynValueDeleter> TryParse(absl::string_view value,
                                                  std::string& err) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());
  // Stores the flag value based on the pointer to the source.
  void StoreValue(const void* src, ValueSource source)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());

  // Copy the flag data, protected by `seq_lock_` into `dst`.
  //
  // REQUIRES: ValueStorageKind() == kSequenceLocked.
  void ReadSequenceLockedData(void* dst) const
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  FlagHelpKind HelpSourceKind() const {
    return static_cast<FlagHelpKind>(help_source_kind_);
  }
  FlagValueStorageKind ValueStorageKind() const {
    return static_cast<FlagValueStorageKind>(value_storage_kind_);
  }
  FlagDefaultKind DefaultKind() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard()) {
    return static_cast<FlagDefaultKind>(def_kind_);
  }

  // CommandLineFlag interface implementation
  absl::string_view Name() const override;
  std::string Filename() const override;
  std::string Help() const override;
  FlagFastTypeId TypeId() const override;
  bool IsSpecifiedOnCommandLine() const override
      ABSL_LOCKS_EXCLUDED(*DataGuard());
  std::string DefaultValue() const override ABSL_LOCKS_EXCLUDED(*DataGuard());
  std::string CurrentValue() const override ABSL_LOCKS_EXCLUDED(*DataGuard());
  bool ValidateInputValue(absl::string_view value) const override
      ABSL_LOCKS_EXCLUDED(*DataGuard());
  void CheckDefaultValueParsingRoundtrip() const override
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  int64_t ModificationCount() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());

  // Interfaces to save and restore flags to/from persistent state.
  // Returns current flag state or nullptr if flag does not support
  // saving and restoring a state.
  std::unique_ptr<FlagStateInterface> SaveState() override
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  // Restores the flag state to the supplied state object. If there is
  // nothing to restore returns false. Otherwise returns true.
  bool RestoreState(const FlagState& flag_state)
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  bool ParseFrom(absl::string_view value, FlagSettingMode set_mode,
                 ValueSource source, std::string& error) override
      ABSL_LOCKS_EXCLUDED(*DataGuard());

  // Immutable flag's state.

  // Flags name passed to ABSL_FLAG as second arg.
  const char* const name_;
  // The file name where ABSL_FLAG resides.
  const char* const filename_;
  // Type-specific operations vtable.
  const FlagOpFn op_;
  // Help message literal or function to generate it.
  const FlagHelpMsg help_;
  // Indicates if help message was supplied as literal or generator func.
  const uint8_t help_source_kind_ : 1;
  // Kind of storage this flag is using for the flag's value.
  const uint8_t value_storage_kind_ : 2;

  uint8_t : 0;  // The bytes containing the const bitfields must not be
                // shared with bytes containing the mutable bitfields.

  // Mutable flag's state (guarded by `data_guard_`).

  // def_kind_ is not guard by DataGuard() since it is accessed in Init without
  // locks.
  uint8_t def_kind_ : 2;
  // Has this flag's value been modified?
  bool modified_ : 1 ABSL_GUARDED_BY(*DataGuard());
  // Has this flag been specified on command line.
  bool on_command_line_ : 1 ABSL_GUARDED_BY(*DataGuard());

  // Unique tag for absl::call_once call to initialize this flag.
  absl::once_flag init_control_;

  // Sequence lock / mutation counter.
  flags_internal::SequenceLock seq_lock_;

  // Optional flag's callback and absl::Mutex to guard the invocations.
  FlagCallback* callback_ ABSL_GUARDED_BY(*DataGuard());
  // Either a pointer to the function generating the default value based on the
  // value specified in ABSL_FLAG or pointer to the dynamically set default
  // value via SetCommandLineOptionWithMode. def_kind_ is used to distinguish
  // these two cases.
  FlagDefaultSrc default_value_;

  // This is reserved space for an absl::Mutex to guard flag data. It will be
  // initialized in FlagImpl::Init via placement new.
  // We can't use "absl::Mutex data_guard_", since this class is not literal.
  // We do not want to use "absl::Mutex* data_guard_", since this would require
  // heap allocation during initialization, which is both slows program startup
  // and can fail. Using reserved space + placement new allows us to avoid both
  // problems.
  alignas(absl::Mutex) mutable char data_guard_[sizeof(absl::Mutex)];
};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

///////////////////////////////////////////////////////////////////////////////
// The Flag object parameterized by the flag's value type. This class implements
// flag reflection handle interface.

template <typename T>
class Flag {
 public:
  constexpr Flag(const char* name, const char* filename, FlagHelpArg help,
                 const FlagDefaultArg default_arg)
      : impl_(name, filename, &FlagOps<T>, help,
              flags_internal::StorageKind<T>(), default_arg),
        value_() {}

  // CommandLineFlag interface
  absl::string_view Name() const { return impl_.Name(); }
  std::string Filename() const { return impl_.Filename(); }
  std::string Help() const { return impl_.Help(); }
  // Do not use. To be removed.
  bool IsSpecifiedOnCommandLine() const {
    return impl_.IsSpecifiedOnCommandLine();
  }
  std::string DefaultValue() const { return impl_.DefaultValue(); }
  std::string CurrentValue() const { return impl_.CurrentValue(); }

 private:
  template <typename, bool>
  friend class FlagRegistrar;
  friend class FlagImplPeer;

  T Get() const {
    // See implementation notes in CommandLineFlag::Get().
    union U {
      T value;
      U() {}
      ~U() { value.~T(); }
    };
    U u;

#if !defined(NDEBUG)
    impl_.AssertValidType(base_internal::FastTypeId<T>(), &GenRuntimeTypeId<T>);
#endif

    if (ABSL_PREDICT_FALSE(!value_.Get(impl_.seq_lock_, u.value))) {
      impl_.Read(&u.value);
    }
    return std::move(u.value);
  }
  void Set(const T& v) {
    impl_.AssertValidType(base_internal::FastTypeId<T>(), &GenRuntimeTypeId<T>);
    impl_.Write(&v);
  }

  // Access to the reflection.
  const CommandLineFlag& Reflect() const { return impl_; }

  // Flag's data
  // The implementation depends on value_ field to be placed exactly after the
  // impl_ field, so that impl_ can figure out the offset to the value and
  // access it.
  FlagImpl impl_;
  FlagValue<T> value_;
};

///////////////////////////////////////////////////////////////////////////////
// Trampoline for friend access

class FlagImplPeer {
 public:
  template <typename T, typename FlagType>
  static T InvokeGet(const FlagType& flag) {
    return flag.Get();
  }
  template <typename FlagType, typename T>
  static void InvokeSet(FlagType& flag, const T& v) {
    flag.Set(v);
  }
  template <typename FlagType>
  static const CommandLineFlag& InvokeReflect(const FlagType& f) {
    return f.Reflect();
  }
};

///////////////////////////////////////////////////////////////////////////////
// Implementation of Flag value specific operations routine.
template <typename T>
void* FlagOps(FlagOp op, const void* v1, void* v2, void* v3) {
  struct AlignedSpace {
    alignas(MaskedPointer::RequiredAlignment()) alignas(T) char buf[sizeof(T)];
  };
  using Allocator = std::allocator<AlignedSpace>;
  switch (op) {
    case FlagOp::kAlloc: {
      Allocator alloc;
      return std::allocator_traits<Allocator>::allocate(alloc, 1);
    }
    case FlagOp::kDelete: {
      T* p = static_cast<T*>(v2);
      p->~T();
      Allocator alloc;
      std::allocator_traits<Allocator>::deallocate(
          alloc, reinterpret_cast<AlignedSpace*>(p), 1);
      return nullptr;
    }
    case FlagOp::kCopy:
      *static_cast<T*>(v2) = *static_cast<const T*>(v1);
      return nullptr;
    case FlagOp::kCopyConstruct:
      new (v2) T(*static_cast<const T*>(v1));
      return nullptr;
    case FlagOp::kSizeof:
      return reinterpret_cast<void*>(static_cast<uintptr_t>(sizeof(T)));
    case FlagOp::kFastTypeId:
      return const_cast<void*>(base_internal::FastTypeId<T>());
    case FlagOp::kRuntimeTypeId:
      return const_cast<std::type_info*>(GenRuntimeTypeId<T>());
    case FlagOp::kParse: {
      // Initialize the temporary instance of type T based on current value in
      // destination (which is going to be flag's default value).
      T temp(*static_cast<T*>(v2));
      if (!absl::ParseFlag<T>(*static_cast<const absl::string_view*>(v1), &temp,
                              static_cast<std::string*>(v3))) {
        return nullptr;
      }
      *static_cast<T*>(v2) = std::move(temp);
      return v2;
    }
    case FlagOp::kUnparse:
      *static_cast<std::string*>(v2) =
          absl::UnparseFlag<T>(*static_cast<const T*>(v1));
      return nullptr;
    case FlagOp::kValueOffset: {
      // Round sizeof(FlagImp) to a multiple of alignof(FlagValue<T>) to get the
      // offset of the data.
      size_t round_to = alignof(FlagValue<T>);
      size_t offset = (sizeof(FlagImpl) + round_to - 1) / round_to * round_to;
      return reinterpret_cast<void*>(offset);
    }
  }
  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// This class facilitates Flag object registration and tail expression-based
// flag definition, for example:
// ABSL_FLAG(int, foo, 42, "Foo help").OnUpdate(NotifyFooWatcher);
struct FlagRegistrarEmpty {};
template <typename T, bool do_register>
class FlagRegistrar {
 public:
  constexpr explicit FlagRegistrar(Flag<T>& flag, const char* filename)
      : flag_(flag) {
    if (do_register)
      flags_internal::RegisterCommandLineFlag(flag_.impl_, filename);
  }

  FlagRegistrar OnUpdate(FlagCallbackFunc cb) && {
    flag_.impl_.SetCallback(cb);
    return *this;
  }

  // Makes the registrar die gracefully as an empty struct on a line where
  // registration happens. Registrar objects are intended to live only as
  // temporary.
  constexpr operator FlagRegistrarEmpty() const { return {}; }  // NOLINT

 private:
  Flag<T>& flag_;  // Flag being registered (not owned).
};

///////////////////////////////////////////////////////////////////////////////
// Test only API
uint64_t NumLeakedFlagValues();

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_INTERNAL_FLAG_H_
