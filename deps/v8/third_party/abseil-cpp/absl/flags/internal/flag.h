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
// Flag current value auxiliary structs.

constexpr int64_t UninitializedFlagValue() {
  return static_cast<int64_t>(0xababababababababll);
}

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
  kAlignedBuffer = 3,
};

template <typename T>
static constexpr FlagValueStorageKind StorageKind() {
  return FlagUseValueAndInitBitStorage<T>::value
             ? FlagValueStorageKind::kValueAndInitBit
         : FlagUseOneWordStorage<T>::value
             ? FlagValueStorageKind::kOneWordAtomic
         : FlagUseSequenceLockStorage<T>::value
             ? FlagValueStorageKind::kSequenceLocked
             : FlagValueStorageKind::kAlignedBuffer;
}

struct FlagOneWordValue {
  constexpr explicit FlagOneWordValue(int64_t v) : value(v) {}
  std::atomic<int64_t> value;
};

template <typename T>
struct alignas(8) FlagValueAndInitBit {
  T value;
  // Use an int instead of a bool to guarantee that a non-zero value has
  // a bit set.
  uint8_t init;
};

template <typename T,
          FlagValueStorageKind Kind = flags_internal::StorageKind<T>()>
struct FlagValue;

template <typename T>
struct FlagValue<T, FlagValueStorageKind::kValueAndInitBit> : FlagOneWordValue {
  constexpr FlagValue() : FlagOneWordValue(0) {}
  bool Get(const SequenceLock&, T& dst) const {
    int64_t storage = value.load(std::memory_order_acquire);
    if (ABSL_PREDICT_FALSE(storage == 0)) {
      return false;
    }
    dst = absl::bit_cast<FlagValueAndInitBit<T>>(storage).value;
    return true;
  }
};

template <typename T>
struct FlagValue<T, FlagValueStorageKind::kOneWordAtomic> : FlagOneWordValue {
  constexpr FlagValue() : FlagOneWordValue(UninitializedFlagValue()) {}
  bool Get(const SequenceLock&, T& dst) const {
    int64_t one_word_val = value.load(std::memory_order_acquire);
    if (ABSL_PREDICT_FALSE(one_word_val == UninitializedFlagValue())) {
      return false;
    }
    std::memcpy(&dst, static_cast<const void*>(&one_word_val), sizeof(T));
    return true;
  }
};

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

template <typename T>
struct FlagValue<T, FlagValueStorageKind::kAlignedBuffer> {
  bool Get(const SequenceLock&, T&) const { return false; }

  alignas(T) char value[sizeof(T)];
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
  // int. To do that we pass the "assumed" type id (which is deduced from type
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
  // respect const correctness, so be very carefull using them.

  // This is a shared helper routine which encapsulates most of the magic. Since
  // it is only used inside the three routines below, which are defined in
  // flag.cc, we can define it in that file as well.
  template <typename StorageT>
  StorageT* OffsetValue() const;
  // This is an accessor for a value stored in an aligned buffer storage
  // used for non-trivially-copyable data types.
  // Returns a mutable pointer to the start of a buffer.
  void* AlignedBufferValue() const;

  // The same as above, but used for sequencelock-protected storage.
  std::atomic<uint64_t>* AtomicBufferValue() const;

  // This is an accessor for a value stored as one word atomic. Returns a
  // mutable reference to an atomic value.
  std::atomic<int64_t>& OneWordValue() const;

  // Attempts to parse supplied `value` string. If parsing is successful,
  // returns new value. Otherwise returns nullptr.
  std::unique_ptr<void, DynValueDeleter> TryParse(absl::string_view value,
                                                  std::string& err) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());
  // Stores the flag value based on the pointer to the source.
  void StoreValue(const void* src) ABSL_EXCLUSIVE_LOCKS_REQUIRED(*DataGuard());

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
  // Type-specific operations "vtable".
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
  switch (op) {
    case FlagOp::kAlloc: {
      std::allocator<T> alloc;
      return std::allocator_traits<std::allocator<T>>::allocate(alloc, 1);
    }
    case FlagOp::kDelete: {
      T* p = static_cast<T*>(v2);
      p->~T();
      std::allocator<T> alloc;
      std::allocator_traits<std::allocator<T>>::deallocate(alloc, p, 1);
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
      size_t offset =
          (sizeof(FlagImpl) + round_to - 1) / round_to * round_to;
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

  // Make the registrar "die" gracefully as an empty struct on a line where
  // registration happens. Registrar objects are intended to live only as
  // temporary.
  constexpr operator FlagRegistrarEmpty() const { return {}; }  // NOLINT

 private:
  Flag<T>& flag_;  // Flag being registered (not owned).
};

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_INTERNAL_FLAG_H_
