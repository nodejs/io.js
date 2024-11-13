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

#include "absl/flags/internal/flag.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/base/const_init.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/optimization.h"
#include "absl/flags/config.h"
#include "absl/flags/internal/commandlineflag.h"
#include "absl/flags/usage_config.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

// The help message indicating that the commandline flag has been stripped. It
// will not show up when doing "-help" and its variants. The flag is stripped
// if ABSL_FLAGS_STRIP_HELP is set to 1 before including absl/flags/flag.h
const char kStrippedFlagHelp[] = "\001\002\003\004 (unknown) \004\003\002\001";

namespace {

// Currently we only validate flag values for user-defined flag types.
bool ShouldValidateFlagValue(FlagFastTypeId flag_type_id) {
#define DONT_VALIDATE(T, _) \
  if (flag_type_id == base_internal::FastTypeId<T>()) return false;
  ABSL_FLAGS_INTERNAL_SUPPORTED_TYPES(DONT_VALIDATE)
#undef DONT_VALIDATE

  return true;
}

// RAII helper used to temporarily unlock and relock `absl::Mutex`.
// This is used when we need to ensure that locks are released while
// invoking user supplied callbacks and then reacquired, since callbacks may
// need to acquire these locks themselves.
class MutexRelock {
 public:
  explicit MutexRelock(absl::Mutex& mu) : mu_(mu) { mu_.Unlock(); }
  ~MutexRelock() { mu_.Lock(); }

  MutexRelock(const MutexRelock&) = delete;
  MutexRelock& operator=(const MutexRelock&) = delete;

 private:
  absl::Mutex& mu_;
};

// This is a freelist of leaked flag values and guard for its access.
// When we can't guarantee it is safe to reuse the memory for flag values,
// we move the memory to the freelist where it lives indefinitely, so it can
// still be safely accessed. This also prevents leak checkers from complaining
// about the leaked memory that can no longer be accessed through any pointer.
ABSL_CONST_INIT absl::Mutex s_freelist_guard(absl::kConstInit);
ABSL_CONST_INIT std::vector<void*>* s_freelist = nullptr;

void AddToFreelist(void* p) {
  absl::MutexLock l(&s_freelist_guard);
  if (!s_freelist) {
    s_freelist = new std::vector<void*>;
  }
  s_freelist->push_back(p);
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////

uint64_t NumLeakedFlagValues() {
  absl::MutexLock l(&s_freelist_guard);
  return s_freelist == nullptr ? 0u : s_freelist->size();
}

///////////////////////////////////////////////////////////////////////////////
// Persistent state of the flag data.

class FlagImpl;

class FlagState : public flags_internal::FlagStateInterface {
 public:
  template <typename V>
  FlagState(FlagImpl& flag_impl, const V& v, bool modified,
            bool on_command_line, int64_t counter)
      : flag_impl_(flag_impl),
        value_(v),
        modified_(modified),
        on_command_line_(on_command_line),
        counter_(counter) {}

  ~FlagState() override {
    if (flag_impl_.ValueStorageKind() != FlagValueStorageKind::kHeapAllocated &&
        flag_impl_.ValueStorageKind() != FlagValueStorageKind::kSequenceLocked)
      return;
    flags_internal::Delete(flag_impl_.op_, value_.heap_allocated);
  }

 private:
  friend class FlagImpl;

  // Restores the flag to the saved state.
  void Restore() const override {
    if (!flag_impl_.RestoreState(*this)) return;

    ABSL_INTERNAL_LOG(INFO,
                      absl::StrCat("Restore saved value of ", flag_impl_.Name(),
                                   " to: ", flag_impl_.CurrentValue()));
  }

  // Flag and saved flag data.
  FlagImpl& flag_impl_;
  union SavedValue {
    explicit SavedValue(void* v) : heap_allocated(v) {}
    explicit SavedValue(int64_t v) : one_word(v) {}

    void* heap_allocated;
    int64_t one_word;
  } value_;
  bool modified_;
  bool on_command_line_;
  int64_t counter_;
};

///////////////////////////////////////////////////////////////////////////////
// Flag implementation, which does not depend on flag value type.

DynValueDeleter::DynValueDeleter(FlagOpFn op_arg) : op(op_arg) {}

void DynValueDeleter::operator()(void* ptr) const {
  if (op == nullptr) return;

  Delete(op, ptr);
}

MaskedPointer::MaskedPointer(ptr_t rhs, bool is_candidate) : ptr_(rhs) {
  if (is_candidate) {
    ApplyMask(kUnprotectedReadCandidate);
  }
}

bool MaskedPointer::IsUnprotectedReadCandidate() const {
  return CheckMask(kUnprotectedReadCandidate);
}

bool MaskedPointer::HasBeenRead() const { return CheckMask(kHasBeenRead); }

void MaskedPointer::Set(FlagOpFn op, const void* src, bool is_candidate) {
  flags_internal::Copy(op, src, Ptr());
  if (is_candidate) {
    ApplyMask(kUnprotectedReadCandidate);
  }
}
void MaskedPointer::MarkAsRead() { ApplyMask(kHasBeenRead); }

void MaskedPointer::ApplyMask(mask_t mask) {
  ptr_ = reinterpret_cast<ptr_t>(reinterpret_cast<mask_t>(ptr_) | mask);
}
bool MaskedPointer::CheckMask(mask_t mask) const {
  return (reinterpret_cast<mask_t>(ptr_) & mask) != 0;
}

void FlagImpl::Init() {
  new (&data_guard_) absl::Mutex;

  auto def_kind = static_cast<FlagDefaultKind>(def_kind_);

  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kValueAndInitBit:
    case FlagValueStorageKind::kOneWordAtomic: {
      alignas(int64_t) std::array<char, sizeof(int64_t)> buf{};
      if (def_kind == FlagDefaultKind::kGenFunc) {
        (*default_value_.gen_func)(buf.data());
      } else {
        assert(def_kind != FlagDefaultKind::kDynamicValue);
        std::memcpy(buf.data(), &default_value_, Sizeof(op_));
      }
      if (ValueStorageKind() == FlagValueStorageKind::kValueAndInitBit) {
        // We presume here the memory layout of FlagValueAndInitBit struct.
        uint8_t initialized = 1;
        std::memcpy(buf.data() + Sizeof(op_), &initialized,
                    sizeof(initialized));
      }
      // Type can contain valid uninitialized bits, e.g. padding.
      ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(buf.data(), buf.size());
      OneWordValue().store(absl::bit_cast<int64_t>(buf),
                           std::memory_order_release);
      break;
    }
    case FlagValueStorageKind::kSequenceLocked: {
      // For this storage kind the default_value_ always points to gen_func
      // during initialization.
      assert(def_kind == FlagDefaultKind::kGenFunc);
      (*default_value_.gen_func)(AtomicBufferValue());
      break;
    }
    case FlagValueStorageKind::kHeapAllocated:
      // For this storage kind the default_value_ always points to gen_func
      // during initialization.
      assert(def_kind == FlagDefaultKind::kGenFunc);
      // Flag value initially points to the internal buffer.
      MaskedPointer ptr_value = PtrStorage().load(std::memory_order_acquire);
      (*default_value_.gen_func)(ptr_value.Ptr());
      // Default value is a candidate for an unprotected read.
      PtrStorage().store(MaskedPointer(ptr_value.Ptr(), true),
                         std::memory_order_release);
      break;
  }
  seq_lock_.MarkInitialized();
}

absl::Mutex* FlagImpl::DataGuard() const {
  absl::call_once(const_cast<FlagImpl*>(this)->init_control_, &FlagImpl::Init,
                  const_cast<FlagImpl*>(this));

  // data_guard_ is initialized inside Init.
  return reinterpret_cast<absl::Mutex*>(&data_guard_);
}

void FlagImpl::AssertValidType(FlagFastTypeId rhs_type_id,
                               const std::type_info* (*gen_rtti)()) const {
  FlagFastTypeId lhs_type_id = flags_internal::FastTypeId(op_);

  // `rhs_type_id` is the fast type id corresponding to the declaration
  // visible at the call site. `lhs_type_id` is the fast type id
  // corresponding to the type specified in flag definition. They must match
  //  for this operation to be well-defined.
  if (ABSL_PREDICT_TRUE(lhs_type_id == rhs_type_id)) return;

  const std::type_info* lhs_runtime_type_id =
      flags_internal::RuntimeTypeId(op_);
  const std::type_info* rhs_runtime_type_id = (*gen_rtti)();

  if (lhs_runtime_type_id == rhs_runtime_type_id) return;

#ifdef ABSL_INTERNAL_HAS_RTTI
  if (*lhs_runtime_type_id == *rhs_runtime_type_id) return;
#endif

  ABSL_INTERNAL_LOG(
      FATAL, absl::StrCat("Flag '", Name(),
                          "' is defined as one type and declared as another"));
}

std::unique_ptr<void, DynValueDeleter> FlagImpl::MakeInitValue() const {
  void* res = nullptr;
  switch (DefaultKind()) {
    case FlagDefaultKind::kDynamicValue:
      res = flags_internal::Clone(op_, default_value_.dynamic_value);
      break;
    case FlagDefaultKind::kGenFunc:
      res = flags_internal::Alloc(op_);
      (*default_value_.gen_func)(res);
      break;
    default:
      res = flags_internal::Clone(op_, &default_value_);
      break;
  }
  return {res, DynValueDeleter{op_}};
}

void FlagImpl::StoreValue(const void* src, ValueSource source) {
  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kValueAndInitBit:
    case FlagValueStorageKind::kOneWordAtomic: {
      // Load the current value to avoid setting 'init' bit manually.
      int64_t one_word_val = OneWordValue().load(std::memory_order_acquire);
      std::memcpy(&one_word_val, src, Sizeof(op_));
      OneWordValue().store(one_word_val, std::memory_order_release);
      seq_lock_.IncrementModificationCount();
      break;
    }
    case FlagValueStorageKind::kSequenceLocked: {
      seq_lock_.Write(AtomicBufferValue(), src, Sizeof(op_));
      break;
    }
    case FlagValueStorageKind::kHeapAllocated:
      MaskedPointer ptr_value = PtrStorage().load(std::memory_order_acquire);

      if (ptr_value.IsUnprotectedReadCandidate() && ptr_value.HasBeenRead()) {
        // If current value is a candidate for an unprotected read and if it was
        // already read at least once, follow up reads (if any) are done without
        // mutex protection. We can't guarantee it is safe to reuse this memory
        // since it may have been accessed by another thread concurrently, so
        // instead we move the memory to a freelist so it can still be safely
        // accessed, and allocate a new one for the new value.
        AddToFreelist(ptr_value.Ptr());
        ptr_value = MaskedPointer(Clone(op_, src), source == kCommandLine);
      } else {
        // Current value either was set programmatically or was never read.
        // We can reuse the memory since all accesses to this value (if any)
        // were protected by mutex. That said, if a new value comes from command
        // line it now becomes a candidate for an unprotected read.
        ptr_value.Set(op_, src, source == kCommandLine);
      }

      PtrStorage().store(ptr_value, std::memory_order_release);
      seq_lock_.IncrementModificationCount();
      break;
  }
  modified_ = true;
  InvokeCallback();
}

absl::string_view FlagImpl::Name() const { return name_; }

std::string FlagImpl::Filename() const {
  return flags_internal::GetUsageConfig().normalize_filename(filename_);
}

std::string FlagImpl::Help() const {
  return HelpSourceKind() == FlagHelpKind::kLiteral ? help_.literal
                                                    : help_.gen_func();
}

FlagFastTypeId FlagImpl::TypeId() const {
  return flags_internal::FastTypeId(op_);
}

int64_t FlagImpl::ModificationCount() const {
  return seq_lock_.ModificationCount();
}

bool FlagImpl::IsSpecifiedOnCommandLine() const {
  absl::MutexLock l(DataGuard());
  return on_command_line_;
}

std::string FlagImpl::DefaultValue() const {
  absl::MutexLock l(DataGuard());

  auto obj = MakeInitValue();
  return flags_internal::Unparse(op_, obj.get());
}

std::string FlagImpl::CurrentValue() const {
  auto* guard = DataGuard();  // Make sure flag initialized
  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kValueAndInitBit:
    case FlagValueStorageKind::kOneWordAtomic: {
      const auto one_word_val =
          absl::bit_cast<std::array<char, sizeof(int64_t)>>(
              OneWordValue().load(std::memory_order_acquire));
      return flags_internal::Unparse(op_, one_word_val.data());
    }
    case FlagValueStorageKind::kSequenceLocked: {
      std::unique_ptr<void, DynValueDeleter> cloned(flags_internal::Alloc(op_),
                                                    DynValueDeleter{op_});
      ReadSequenceLockedData(cloned.get());
      return flags_internal::Unparse(op_, cloned.get());
    }
    case FlagValueStorageKind::kHeapAllocated: {
      absl::MutexLock l(guard);
      return flags_internal::Unparse(
          op_, PtrStorage().load(std::memory_order_acquire).Ptr());
    }
  }

  return "";
}

void FlagImpl::SetCallback(const FlagCallbackFunc mutation_callback) {
  absl::MutexLock l(DataGuard());

  if (callback_ == nullptr) {
    callback_ = new FlagCallback;
  }
  callback_->func = mutation_callback;

  InvokeCallback();
}

void FlagImpl::InvokeCallback() const {
  if (!callback_) return;

  // Make a copy of the C-style function pointer that we are about to invoke
  // before we release the lock guarding it.
  FlagCallbackFunc cb = callback_->func;

  // If the flag has a mutation callback this function invokes it. While the
  // callback is being invoked the primary flag's mutex is unlocked and it is
  // re-locked back after call to callback is completed. Callback invocation is
  // guarded by flag's secondary mutex instead which prevents concurrent
  // callback invocation. Note that it is possible for other thread to grab the
  // primary lock and update flag's value at any time during the callback
  // invocation. This is by design. Callback can get a value of the flag if
  // necessary, but it might be different from the value initiated the callback
  // and it also can be different by the time the callback invocation is
  // completed. Requires that *primary_lock be held in exclusive mode; it may be
  // released and reacquired by the implementation.
  MutexRelock relock(*DataGuard());
  absl::MutexLock lock(&callback_->guard);
  cb();
}

std::unique_ptr<FlagStateInterface> FlagImpl::SaveState() {
  absl::MutexLock l(DataGuard());

  bool modified = modified_;
  bool on_command_line = on_command_line_;
  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kValueAndInitBit:
    case FlagValueStorageKind::kOneWordAtomic: {
      return absl::make_unique<FlagState>(
          *this, OneWordValue().load(std::memory_order_acquire), modified,
          on_command_line, ModificationCount());
    }
    case FlagValueStorageKind::kSequenceLocked: {
      void* cloned = flags_internal::Alloc(op_);
      // Read is guaranteed to be successful because we hold the lock.
      bool success =
          seq_lock_.TryRead(cloned, AtomicBufferValue(), Sizeof(op_));
      assert(success);
      static_cast<void>(success);
      return absl::make_unique<FlagState>(*this, cloned, modified,
                                          on_command_line, ModificationCount());
    }
    case FlagValueStorageKind::kHeapAllocated: {
      return absl::make_unique<FlagState>(
          *this,
          flags_internal::Clone(
              op_, PtrStorage().load(std::memory_order_acquire).Ptr()),
          modified, on_command_line, ModificationCount());
    }
  }
  return nullptr;
}

bool FlagImpl::RestoreState(const FlagState& flag_state) {
  absl::MutexLock l(DataGuard());
  if (flag_state.counter_ == ModificationCount()) {
    return false;
  }

  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kValueAndInitBit:
    case FlagValueStorageKind::kOneWordAtomic:
      StoreValue(&flag_state.value_.one_word, kProgrammaticChange);
      break;
    case FlagValueStorageKind::kSequenceLocked:
    case FlagValueStorageKind::kHeapAllocated:
      StoreValue(flag_state.value_.heap_allocated, kProgrammaticChange);
      break;
  }

  modified_ = flag_state.modified_;
  on_command_line_ = flag_state.on_command_line_;

  return true;
}

template <typename StorageT>
StorageT* FlagImpl::OffsetValue() const {
  char* p = reinterpret_cast<char*>(const_cast<FlagImpl*>(this));
  // The offset is deduced via Flag value type specific op_.
  ptrdiff_t offset = flags_internal::ValueOffset(op_);

  return reinterpret_cast<StorageT*>(p + offset);
}

std::atomic<uint64_t>* FlagImpl::AtomicBufferValue() const {
  assert(ValueStorageKind() == FlagValueStorageKind::kSequenceLocked);
  return OffsetValue<std::atomic<uint64_t>>();
}

std::atomic<int64_t>& FlagImpl::OneWordValue() const {
  assert(ValueStorageKind() == FlagValueStorageKind::kOneWordAtomic ||
         ValueStorageKind() == FlagValueStorageKind::kValueAndInitBit);
  return OffsetValue<FlagOneWordValue>()->value;
}

std::atomic<MaskedPointer>& FlagImpl::PtrStorage() const {
  assert(ValueStorageKind() == FlagValueStorageKind::kHeapAllocated);
  return OffsetValue<FlagMaskedPointerValue>()->value;
}

// Attempts to parse supplied `value` string using parsing routine in the `flag`
// argument. If parsing successful, this function replaces the dst with newly
// parsed value. In case if any error is encountered in either step, the error
// message is stored in 'err'
std::unique_ptr<void, DynValueDeleter> FlagImpl::TryParse(
    absl::string_view value, std::string& err) const {
  std::unique_ptr<void, DynValueDeleter> tentative_value = MakeInitValue();

  std::string parse_err;
  if (!flags_internal::Parse(op_, value, tentative_value.get(), &parse_err)) {
    absl::string_view err_sep = parse_err.empty() ? "" : "; ";
    err = absl::StrCat("Illegal value '", value, "' specified for flag '",
                       Name(), "'", err_sep, parse_err);
    return nullptr;
  }

  return tentative_value;
}

void FlagImpl::Read(void* dst) const {
  auto* guard = DataGuard();  // Make sure flag initialized
  switch (ValueStorageKind()) {
    case FlagValueStorageKind::kValueAndInitBit:
    case FlagValueStorageKind::kOneWordAtomic: {
      const int64_t one_word_val =
          OneWordValue().load(std::memory_order_acquire);
      std::memcpy(dst, &one_word_val, Sizeof(op_));
      break;
    }
    case FlagValueStorageKind::kSequenceLocked: {
      ReadSequenceLockedData(dst);
      break;
    }
    case FlagValueStorageKind::kHeapAllocated: {
      absl::MutexLock l(guard);
      MaskedPointer ptr_value = PtrStorage().load(std::memory_order_acquire);

      flags_internal::CopyConstruct(op_, ptr_value.Ptr(), dst);

      // For unprotected read candidates, mark that the value as has been read.
      if (ptr_value.IsUnprotectedReadCandidate() && !ptr_value.HasBeenRead()) {
        ptr_value.MarkAsRead();
        PtrStorage().store(ptr_value, std::memory_order_release);
      }
      break;
    }
  }
}

int64_t FlagImpl::ReadOneWord() const {
  assert(ValueStorageKind() == FlagValueStorageKind::kOneWordAtomic ||
         ValueStorageKind() == FlagValueStorageKind::kValueAndInitBit);
  auto* guard = DataGuard();  // Make sure flag initialized
  (void)guard;
  return OneWordValue().load(std::memory_order_acquire);
}

bool FlagImpl::ReadOneBool() const {
  assert(ValueStorageKind() == FlagValueStorageKind::kValueAndInitBit);
  auto* guard = DataGuard();  // Make sure flag initialized
  (void)guard;
  return absl::bit_cast<FlagValueAndInitBit<bool>>(
             OneWordValue().load(std::memory_order_acquire))
      .value;
}

void FlagImpl::ReadSequenceLockedData(void* dst) const {
  size_t size = Sizeof(op_);
  // Attempt to read using the sequence lock.
  if (ABSL_PREDICT_TRUE(seq_lock_.TryRead(dst, AtomicBufferValue(), size))) {
    return;
  }
  // We failed due to contention. Acquire the lock to prevent contention
  // and try again.
  absl::ReaderMutexLock l(DataGuard());
  bool success = seq_lock_.TryRead(dst, AtomicBufferValue(), size);
  assert(success);
  static_cast<void>(success);
}

void FlagImpl::Write(const void* src) {
  absl::MutexLock l(DataGuard());

  if (ShouldValidateFlagValue(flags_internal::FastTypeId(op_))) {
    std::unique_ptr<void, DynValueDeleter> obj{flags_internal::Clone(op_, src),
                                               DynValueDeleter{op_}};
    std::string ignored_error;
    std::string src_as_str = flags_internal::Unparse(op_, src);
    if (!flags_internal::Parse(op_, src_as_str, obj.get(), &ignored_error)) {
      ABSL_INTERNAL_LOG(ERROR, absl::StrCat("Attempt to set flag '", Name(),
                                            "' to invalid value ", src_as_str));
    }
  }

  StoreValue(src, kProgrammaticChange);
}

// Sets the value of the flag based on specified string `value`. If the flag
// was successfully set to new value, it returns true. Otherwise, sets `err`
// to indicate the error, leaves the flag unchanged, and returns false. There
// are three ways to set the flag's value:
//  * Update the current flag value
//  * Update the flag's default value
//  * Update the current flag value if it was never set before
// The mode is selected based on 'set_mode' parameter.
bool FlagImpl::ParseFrom(absl::string_view value, FlagSettingMode set_mode,
                         ValueSource source, std::string& err) {
  absl::MutexLock l(DataGuard());

  switch (set_mode) {
    case SET_FLAGS_VALUE: {
      // set or modify the flag's value
      auto tentative_value = TryParse(value, err);
      if (!tentative_value) return false;

      StoreValue(tentative_value.get(), source);

      if (source == kCommandLine) {
        on_command_line_ = true;
      }
      break;
    }
    case SET_FLAG_IF_DEFAULT: {
      // set the flag's value, but only if it hasn't been set by someone else
      if (modified_) {
        // TODO(rogeeff): review and fix this semantic. Currently we do not fail
        // in this case if flag is modified. This is misleading since the flag's
        // value is not updated even though we return true.
        // *err = absl::StrCat(Name(), " is already set to ",
        //                     CurrentValue(), "\n");
        // return false;
        return true;
      }
      auto tentative_value = TryParse(value, err);
      if (!tentative_value) return false;

      StoreValue(tentative_value.get(), source);
      break;
    }
    case SET_FLAGS_DEFAULT: {
      auto tentative_value = TryParse(value, err);
      if (!tentative_value) return false;

      if (DefaultKind() == FlagDefaultKind::kDynamicValue) {
        void* old_value = default_value_.dynamic_value;
        default_value_.dynamic_value = tentative_value.release();
        tentative_value.reset(old_value);
      } else {
        default_value_.dynamic_value = tentative_value.release();
        def_kind_ = static_cast<uint8_t>(FlagDefaultKind::kDynamicValue);
      }

      if (!modified_) {
        // Need to set both default value *and* current, in this case.
        StoreValue(default_value_.dynamic_value, source);
        modified_ = false;
      }
      break;
    }
  }

  return true;
}

void FlagImpl::CheckDefaultValueParsingRoundtrip() const {
  std::string v = DefaultValue();

  absl::MutexLock lock(DataGuard());

  auto dst = MakeInitValue();
  std::string error;
  if (!flags_internal::Parse(op_, v, dst.get(), &error)) {
    ABSL_INTERNAL_LOG(
        FATAL,
        absl::StrCat("Flag ", Name(), " (from ", Filename(),
                     "): string form of default value '", v,
                     "' could not be parsed; error=", error));
  }

  // We do not compare dst to def since parsing/unparsing may make
  // small changes, e.g., precision loss for floating point types.
}

bool FlagImpl::ValidateInputValue(absl::string_view value) const {
  absl::MutexLock l(DataGuard());

  auto obj = MakeInitValue();
  std::string ignored_error;
  return flags_internal::Parse(op_, value, obj.get(), &ignored_error);
}

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl
