// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ISOLATE_DATA_H_
#define V8_EXECUTION_ISOLATE_DATA_H_

#include "src/builtins/builtins.h"
#include "src/codegen/constants-arch.h"
#include "src/codegen/external-reference-table.h"
#include "src/execution/stack-guard.h"
#include "src/execution/thread-local-top.h"
#include "src/heap/linear-allocation-area.h"
#include "src/roots/roots.h"
#include "src/sandbox/code-pointer-table.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/trusted-pointer-table.h"
#include "src/utils/utils.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

class Isolate;

#if V8_HOST_ARCH_64_BIT
// No padding is currently required for fast_c_call_XXX and wasm64_oob_offset_
// fields.
#define ISOLATE_DATA_FAST_C_CALL_PADDING(V)
#define ISOLATE_DATA_WASM64_OOB_PADDING(V)

#else
// Aligns fast_c_call_XXX fields so that they stay in the same CPU cache line.
#define ISOLATE_DATA_FAST_C_CALL_PADDING(V)               \
  V(kFastCCallAlignmentPaddingOffset, kSystemPointerSize, \
    fast_c_call_alignment_padding)

// Aligns wasm64_oob_offset_ field to 8 bytes to avoid issues with different
// field alignment vs cross-compilation.
// The wasm64_oob_offset_ is currently aligned, so don't add the padding.
#define ISOLATE_DATA_WASM64_OOB_PADDING(V)
// #define ISOLATE_DATA_WASM64_OOB_PADDING(V)                      \
//   V(kWasm64OOBOffsetAlignmentPaddingOffset, kSystemPointerSize, \
//     wasm64_oob_offset_alignment_padding)

#endif  // V8_HOST_ARCH_64_BIT

// IsolateData fields, defined as: V(Offset, Size, Name)
#define ISOLATE_DATA_FIELDS(V)                                                \
  /* Misc. fields. */                                                         \
  V(kCageBaseOffset, kSystemPointerSize, cage_base)                           \
  V(kStackGuardOffset, StackGuard::kSizeInBytes, stack_guard)                 \
  V(kIsMarkingFlag, kUInt8Size, is_marking_flag)                              \
  V(kIsMinorMarkingFlag, kUInt8Size, is_minor_marking_flag)                   \
  V(kIsSharedSpaceIsolateFlag, kUInt8Size, is_shared_space_isolate_flag)      \
  V(kUsesSharedHeapFlag, kUInt8Size, uses_shared_heap_flag)                   \
  V(kExecutionModeOffset, kUInt8Size, execution_mode)                         \
  V(kStackIsIterableOffset, kUInt8Size, stack_is_iterable)                    \
  V(kErrorMessageParam, kUInt8Size, error_message_param)                      \
  V(kTablesAlignmentPaddingOffset, 1, tables_alignment_padding)               \
  /* Tier 0 tables (small but fast access). */                                \
  V(kBuiltinTier0EntryTableOffset,                                            \
    Builtins::kBuiltinTier0Count* kSystemPointerSize,                         \
    builtin_tier0_entry_table)                                                \
  V(kBuiltinsTier0TableOffset,                                                \
    Builtins::kBuiltinTier0Count* kSystemPointerSize, builtin_tier0_table)    \
  /* Misc. fields. */                                                         \
  V(kNewAllocationInfoOffset, LinearAllocationArea::kSize,                    \
    new_allocation_info)                                                      \
  V(kOldAllocationInfoOffset, LinearAllocationArea::kSize,                    \
    old_allocation_info)                                                      \
  ISOLATE_DATA_FAST_C_CALL_PADDING(V)                                         \
  V(kFastCCallCallerFPOffset, kSystemPointerSize, fast_c_call_caller_fp)      \
  V(kFastCCallCallerPCOffset, kSystemPointerSize, fast_c_call_caller_pc)      \
  V(kFastApiCallTargetOffset, kSystemPointerSize, fast_api_call_target)       \
  V(kLongTaskStatsCounterOffset, kSizetSize, long_task_stats_counter)         \
  V(kThreadLocalTopOffset, ThreadLocalTop::kSizeInBytes, thread_local_top)    \
  V(kHandleScopeDataOffset, HandleScopeData::kSizeInBytes, handle_scope_data) \
  V(kEmbedderDataOffset, Internals::kNumIsolateDataSlots* kSystemPointerSize, \
    embedder_data)                                                            \
  ISOLATE_DATA_FIELDS_POINTER_COMPRESSION(V)                                  \
  ISOLATE_DATA_FIELDS_SANDBOX(V)                                              \
  V(kApiCallbackThunkArgumentOffset, kSystemPointerSize,                      \
    api_callback_thunk_argument)                                              \
  V(kContinuationPreservedEmbedderDataOffset, kSystemPointerSize,             \
    continuation_preserved_embedder_data)                                     \
  ISOLATE_DATA_WASM64_OOB_PADDING(V)                                          \
  V(kWasm64OOBOffset, kInt64Size, wasm64_oob_offset)                          \
  /* Full tables (arbitrary size, potentially slower access). */              \
  V(kRootsTableOffset, RootsTable::kEntriesCount* kSystemPointerSize,         \
    roots_table)                                                              \
  V(kExternalReferenceTableOffset, ExternalReferenceTable::kSizeInBytes,      \
    external_reference_table)                                                 \
  V(kBuiltinEntryTableOffset, Builtins::kBuiltinCount* kSystemPointerSize,    \
    builtin_entry_table)                                                      \
  V(kBuiltinTableOffset, Builtins::kBuiltinCount* kSystemPointerSize,         \
    builtin_table)

#ifdef V8_COMPRESS_POINTERS
#define ISOLATE_DATA_FIELDS_POINTER_COMPRESSION(V)            \
  V(kExternalPointerTableOffset, ExternalPointerTable::kSize, \
    external_pointer_table)                                   \
  V(kSharedExternalPointerTableOffset, kSystemPointerSize,    \
    shared_external_pointer_table)
#else
#define ISOLATE_DATA_FIELDS_POINTER_COMPRESSION(V)
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_ENABLE_SANDBOX
#define ISOLATE_DATA_FIELDS_SANDBOX(V)                             \
  V(kTrustedCageBaseOffset, kSystemPointerSize, trusted_cage_base) \
  V(kTrustedPointerTableOffset, TrustedPointerTable::kSize,        \
    trusted_pointer_table)
#else
#define ISOLATE_DATA_FIELDS_SANDBOX(V)
#endif  // V8_ENABLE_SANDBOX

// This class contains a collection of data accessible from both C++ runtime
// and compiled code (including builtins, interpreter bytecode handlers and
// optimized code). The compiled code accesses the isolate data fields
// indirectly via the root register.
class IsolateData final {
 public:
  IsolateData(Isolate* isolate, Address cage_base, Address trusted_cage_base)
      : cage_base_(cage_base),
        stack_guard_(isolate)
#ifdef V8_ENABLE_SANDBOX
        ,
        trusted_cage_base_(trusted_cage_base)
#endif
  {
  }

  IsolateData(const IsolateData&) = delete;
  IsolateData& operator=(const IsolateData&) = delete;

  static constexpr intptr_t kIsolateRootBias = kRootRegisterBias;

  // The value of the kRootRegister.
  Address isolate_root() const {
    return reinterpret_cast<Address>(this) + kIsolateRootBias;
  }

  // Root-register-relative offsets.

#define V(Offset, Size, Name) \
  static constexpr int Name##_offset() { return Offset - kIsolateRootBias; }
  ISOLATE_DATA_FIELDS(V)
#undef V

  static constexpr int root_slot_offset(RootIndex root_index) {
    return roots_table_offset() + RootsTable::offset_of(root_index);
  }

  static constexpr int BuiltinEntrySlotOffset(Builtin id) {
    DCHECK(Builtins::IsBuiltinId(id));
    return (Builtins::IsTier0(id) ? builtin_tier0_entry_table_offset()
                                  : builtin_entry_table_offset()) +
           Builtins::ToInt(id) * kSystemPointerSize;
  }
  // TODO(ishell): remove in favour of typified id version.
  static constexpr int builtin_slot_offset(int builtin_index) {
    return BuiltinSlotOffset(Builtins::FromInt(builtin_index));
  }
  static constexpr int BuiltinSlotOffset(Builtin id) {
    return (Builtins::IsTier0(id) ? builtin_tier0_table_offset()
                                  : builtin_table_offset()) +
           Builtins::ToInt(id) * kSystemPointerSize;
  }

  static constexpr int jslimit_offset() {
    return stack_guard_offset() + StackGuard::jslimit_offset();
  }

  static constexpr int real_jslimit_offset() {
    return stack_guard_offset() + StackGuard::real_jslimit_offset();
  }

#define V(Offset, Size, Name) \
  Address Name##_address() { return reinterpret_cast<Address>(&Name##_); }
  ISOLATE_DATA_FIELDS(V)
#undef V

  Address fast_c_call_caller_fp() const { return fast_c_call_caller_fp_; }
  Address fast_c_call_caller_pc() const { return fast_c_call_caller_pc_; }
  Address fast_api_call_target() const { return fast_api_call_target_; }
  // The value of kPointerCageBaseRegister.
  Address cage_base() const { return cage_base_; }
  StackGuard* stack_guard() { return &stack_guard_; }
  Address* builtin_tier0_entry_table() { return builtin_tier0_entry_table_; }
  Address* builtin_tier0_table() { return builtin_tier0_table_; }
  RootsTable& roots() { return roots_table_; }
  Address api_callback_thunk_argument() const {
    return api_callback_thunk_argument_;
  }
  Tagged<Object> continuation_preserved_embedder_data() const {
    return continuation_preserved_embedder_data_;
  }
  void set_continuation_preserved_embedder_data(Tagged<Object> data) {
    continuation_preserved_embedder_data_ = data;
  }
  const RootsTable& roots() const { return roots_table_; }
  ExternalReferenceTable* external_reference_table() {
    return &external_reference_table_;
  }
  ThreadLocalTop& thread_local_top() { return thread_local_top_; }
  ThreadLocalTop const& thread_local_top() const { return thread_local_top_; }
  Address* builtin_entry_table() { return builtin_entry_table_; }
  Address* builtin_table() { return builtin_table_; }
  bool stack_is_iterable() const {
    DCHECK(stack_is_iterable_ == 0 || stack_is_iterable_ == 1);
    return stack_is_iterable_ != 0;
  }

  // Returns true if this address points to data stored in this instance. If
  // it's the case then the value can be accessed indirectly through the root
  // register.
  bool contains(Address address) const {
    static_assert(std::is_unsigned<Address>::value);
    Address start = reinterpret_cast<Address>(this);
    return (address - start) < sizeof(*this);
  }

// Offset of a ThreadLocalTop member from {isolate_root()}.
#define THREAD_LOCAL_TOP_MEMBER_OFFSET(Name)                              \
  static uint32_t Name##_offset() {                                       \
    return static_cast<uint32_t>(IsolateData::thread_local_top_offset() + \
                                 OFFSET_OF(ThreadLocalTop, Name##_));     \
  }

  THREAD_LOCAL_TOP_MEMBER_OFFSET(topmost_script_having_context)
  THREAD_LOCAL_TOP_MEMBER_OFFSET(is_on_central_stack_flag)
  THREAD_LOCAL_TOP_MEMBER_OFFSET(context)
#undef THREAD_LOCAL_TOP_MEMBER_OFFSET

 private:
  // Static layout definition.
  //
  // Note: The location of fields within IsolateData is significant. The
  // closer they are to the value of kRootRegister (i.e.: isolate_root()), the
  // cheaper it is to access them. See also: https://crbug.com/993264.
  // The recommended guideline is to put frequently-accessed fields close to
  // the beginning of IsolateData.
#define FIELDS(V)                                                      \
  ISOLATE_DATA_FIELDS(V)                                               \
  /* This padding aligns IsolateData size by 8 bytes. */               \
  V(kPaddingOffset,                                                    \
    8 + RoundUp<8>(static_cast<int>(kPaddingOffset)) - kPaddingOffset) \
  /* Total size. */                                                    \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(0, FIELDS)
#undef FIELDS

  const Address cage_base_;

  // Fields related to the system and JS stack. In particular, this contains
  // the stack limit used by stack checks in generated code.
  StackGuard stack_guard_;

  //
  // Hot flags that are regularly checked.
  //

  // These flags are regularly checked by write barriers.
  // Only valid values are 0 or 1.
  uint8_t is_marking_flag_ = false;
  uint8_t is_minor_marking_flag_ = false;
  uint8_t is_shared_space_isolate_flag_ = false;
  uint8_t uses_shared_heap_flag_ = false;

  // Storage for is_profiling and should_check_side_effects booleans.
  // This value is checked on every API callback/getter call.
  base::Flags<IsolateExecutionModeFlag, uint8_t, std::atomic<uint8_t>>
      execution_mode_ = {IsolateExecutionModeFlag::kNoFlags};
  static_assert(sizeof(execution_mode_) == 1);

  //
  // Not super hot flags, which are put here because we have to align the
  // builtin entry table to kSystemPointerSize anyway.
  //

  // Whether the StackFrameIteratorForProfiler can successfully iterate the
  // current stack. The only valid values are 0 or 1.
  uint8_t stack_is_iterable_ = 1;

  // Field to pass value for error throwing builtins. Currently, it is used to
  // pass the type of the `Dataview` operation to print out operation's name in
  // case of an error.
  uint8_t error_message_param_;

  // Ensure the following tables are kSystemPointerSize-byte aligned.
  static_assert(FIELD_SIZE(kTablesAlignmentPaddingOffset) > 0);
  uint8_t tables_alignment_padding_[FIELD_SIZE(kTablesAlignmentPaddingOffset)];

  // Tier 0 tables. See also builtin_entry_table_ and builtin_table_.
  Address builtin_tier0_entry_table_[Builtins::kBuiltinTier0Count] = {};
  Address builtin_tier0_table_[Builtins::kBuiltinTier0Count] = {};

  LinearAllocationArea new_allocation_info_;
  LinearAllocationArea old_allocation_info_;

#if !V8_HOST_ARCH_64_BIT
  // Aligns fast_c_call_XXX fields so that they stay in the same CPU cache line.
  Address fast_c_call_alignment_padding_;
#endif

  // Stores the state of the caller for MacroAssembler::CallCFunction so that
  // the sampling CPU profiler can iterate the stack during such calls. These
  // are stored on IsolateData so that they can be stored to with only one move
  // instruction in compiled code.
  struct {
    // The FP and PC that are saved right before MacroAssembler::CallCFunction.
    Address fast_c_call_caller_fp_ = kNullAddress;
    Address fast_c_call_caller_pc_ = kNullAddress;
  };
  // The address of the fast API callback right before it's executed from
  // generated code.
  Address fast_api_call_target_ = kNullAddress;

  // Used for implementation of LongTaskStats. Counts the number of potential
  // long tasks.
  size_t long_task_stats_counter_ = 0;

  ThreadLocalTop thread_local_top_;
  HandleScopeData handle_scope_data_;

  // These fields are accessed through the API, offsets must be kept in sync
  // with v8::internal::Internals (in include/v8-internal.h) constants. The
  // layout consistency is verified in Isolate::CheckIsolateLayout() using
  // runtime checks.
  void* embedder_data_[Internals::kNumIsolateDataSlots] = {};

  // Tables containing pointers to objects outside of the V8 sandbox.
#ifdef V8_COMPRESS_POINTERS
  ExternalPointerTable external_pointer_table_;
  ExternalPointerTable* shared_external_pointer_table_;
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_ENABLE_SANDBOX
  const Address trusted_cage_base_;

  TrustedPointerTable trusted_pointer_table_;
#endif  // V8_ENABLE_SANDBOX

  // This is a storage for an additional argument for the Api callback thunk
  // functions, see InvokeAccessorGetterCallback and InvokeFunctionCallback.
  Address api_callback_thunk_argument_ = kNullAddress;

  // This is data that should be preserved on newly created continuations.
  Tagged<Object> continuation_preserved_embedder_data_ = Smi::zero();

#if !V8_HOST_ARCH_64_BIT
  // Aligns wasm64_oob_offset_ field to 8 bytes to avoid cross-compilation
  // issues on some 32-bit configurations.
  // Address wasm64_oob_offset_alignment_padding_;
#endif
  // An offset that always generates an invalid address when added to any
  // start address of a Wasm memory. This is used to force an out-of-bounds
  // access on Wasm memory64.
  int64_t wasm64_oob_offset_ = 0xf000'0000'0000'0000;

  RootsTable roots_table_;
  ExternalReferenceTable external_reference_table_;

  // The entry points for builtins. This corresponds to
  // InstructionStream::InstructionStart() for each InstructionStream object in
  // the builtins table below. The entry table is in IsolateData for easy access
  // through kRootRegister.
  Address builtin_entry_table_[Builtins::kBuiltinCount] = {};

  // The entries in this array are tagged pointers to Code objects.
  Address builtin_table_[Builtins::kBuiltinCount] = {};

  // Ensure the size is 8-byte aligned in order to make alignment of the field
  // following the IsolateData field predictable. This solves the issue with
  // C++ compilers for 32-bit platforms which are not consistent at aligning
  // int64_t fields.
  // In order to avoid dealing with zero-size arrays the padding size is always
  // in the range [8, 15).
  static_assert(kPaddingOffsetEnd + 1 - kPaddingOffset >= 8);
  char padding_[kPaddingOffsetEnd + 1 - kPaddingOffset];

  V8_INLINE static void AssertPredictableLayout();

  friend class Isolate;
  friend class Heap;
  FRIEND_TEST(HeapTest, ExternalLimitDefault);
  FRIEND_TEST(HeapTest, ExternalLimitStaysAboveDefaultForExplicitHandling);
};

// IsolateData object must have "predictable" layout which does not change when
// cross-compiling to another platform. Otherwise there may be compatibility
// issues because of different compilers used for snapshot generator and
// actual V8 code.
void IsolateData::AssertPredictableLayout() {
  static_assert(std::is_standard_layout<StackGuard>::value);
  static_assert(std::is_standard_layout<RootsTable>::value);
  static_assert(std::is_standard_layout<ThreadLocalTop>::value);
  static_assert(std::is_standard_layout<ExternalReferenceTable>::value);
  static_assert(std::is_standard_layout<IsolateData>::value);
  static_assert(std::is_standard_layout<LinearAllocationArea>::value);
#define V(Offset, Size, Name)                                          \
  static_assert(                                                       \
      std::is_standard_layout<decltype(IsolateData::Name##_)>::value); \
  static_assert(offsetof(IsolateData, Name##_) == Offset);
  ISOLATE_DATA_FIELDS(V)
#undef V
  // Some C++ compilers on some 32-bits configurations want to align |int64_t|
  // field to 8 while normally, Clang aligns this field to 4. In particular,
  // when building for Android/arm or Windows/ia32. Catch this issue early.
  static_assert(IsAligned(offsetof(IsolateData, wasm64_oob_offset_),
                          sizeof(IsolateData::wasm64_oob_offset_)));
  static_assert(sizeof(IsolateData) == IsolateData::kSize);
}

#undef ISOLATE_DATA_FIELDS_POINTER_COMPRESSION
#undef ISOLATE_DATA_FIELDS

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ISOLATE_DATA_H_
