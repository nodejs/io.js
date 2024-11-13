// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/baseline/liftoff-compiler.h"

#include <optional>

#include "src/base/enum-set.h"
#include "src/codegen/assembler-inl.h"
// TODO(clemensb): Remove dependences on compiler stuff.
#include "src/codegen/external-reference.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/register-configuration.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/wasm-compiler.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/objects/contexts.h"
#include "src/objects/smi.h"
#include "src/roots/roots.h"
#include "src/tracing/trace-event.h"
#include "src/utils/ostreams.h"
#include "src/utils/utils.h"
#include "src/wasm/baseline/liftoff-assembler-inl.h"
#include "src/wasm/baseline/liftoff-register.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/memory-tracing.h"
#include "src/wasm/object-access.h"
#include "src/wasm/signature-hashing.h"
#include "src/wasm/simd-shuffle.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8::internal::wasm {

using VarState = LiftoffAssembler::VarState;
constexpr auto kRegister = VarState::kRegister;
constexpr auto kIntConst = VarState::kIntConst;
constexpr auto kStack = VarState::kStack;

namespace {

#define __ asm_.

// It's important that we don't modify the LiftoffAssembler's cache state
// in conditionally-executed code paths. Creating these witnesses helps
// enforce that (using DCHECKs in the cache state).
// Conditional jump instructions require a witness to have been created (to
// make sure we don't forget); the witness should stay alive until the label
// is bound where regular control flow resumes. This implies that when we're
// jumping to a trap, the live range of the witness isn't important.
#define FREEZE_STATE(witness_name) FreezeCacheState witness_name(asm_)

#define TRACE(...)                                                \
  do {                                                            \
    if (v8_flags.trace_liftoff) PrintF("[liftoff] " __VA_ARGS__); \
  } while (false)

#define WASM_TRUSTED_INSTANCE_DATA_FIELD_OFFSET(name) \
  ObjectAccess::ToTagged(WasmTrustedInstanceData::k##name##Offset)

template <int expected_size, int actual_size>
struct assert_field_size {
  static_assert(expected_size == actual_size,
                "field in WasmInstance does not have the expected size");
  static constexpr int size = actual_size;
};

#define WASM_TRUSTED_INSTANCE_DATA_FIELD_SIZE(name) \
  FIELD_SIZE(WasmTrustedInstanceData::k##name##Offset)

#define LOAD_INSTANCE_FIELD(dst, name, load_size, pinned)            \
  __ LoadFromInstance(                                               \
      dst, LoadInstanceIntoRegister(pinned, dst),                    \
      WASM_TRUSTED_INSTANCE_DATA_FIELD_OFFSET(name),                 \
      assert_field_size<WASM_TRUSTED_INSTANCE_DATA_FIELD_SIZE(name), \
                        load_size>::size);

#define LOAD_TAGGED_PTR_INSTANCE_FIELD(dst, name, pinned)                  \
  static_assert(                                                           \
      WASM_TRUSTED_INSTANCE_DATA_FIELD_SIZE(name) == kTaggedSize,          \
      "field in WasmTrustedInstanceData does not have the expected size"); \
  __ LoadTaggedPointerFromInstance(                                        \
      dst, LoadInstanceIntoRegister(pinned, dst),                          \
      WASM_TRUSTED_INSTANCE_DATA_FIELD_OFFSET(name));

#define LOAD_PROTECTED_PTR_INSTANCE_FIELD(dst, name, pinned)                 \
  static_assert(                                                             \
      WASM_TRUSTED_INSTANCE_DATA_FIELD_SIZE(Protected##name) == kTaggedSize, \
      "field in WasmTrustedInstanceData does not have the expected size");   \
  __ LoadProtectedPointer(                                                   \
      dst, LoadInstanceIntoRegister(pinned, dst),                            \
      WASM_TRUSTED_INSTANCE_DATA_FIELD_OFFSET(Protected##name));

// Liftoff's code comments are intentionally without source location to keep
// readability up.
#ifdef V8_CODE_COMMENTS
#define CODE_COMMENT(str) __ RecordComment(str, SourceLocation{})
#define SCOPED_CODE_COMMENT(str)                                   \
  AssemblerBase::CodeComment scoped_comment_##__LINE__(&asm_, str, \
                                                       SourceLocation{})
#else
#define CODE_COMMENT(str) ((void)0)
#define SCOPED_CODE_COMMENT(str) ((void)0)
#endif

// For fuzzing purposes, we count each instruction as one "step". Certain
// "bulk" type instructions (dealing with memories, tables, strings, arrays)
// can take much more time. For simplicity, we count them all as a fixed
// large number of steps.
constexpr int kHeavyInstructionSteps = 1000;

constexpr ValueKind kIntPtrKind = LiftoffAssembler::kIntPtrKind;
constexpr ValueKind kSmiKind = LiftoffAssembler::kSmiKind;

// Used to construct fixed-size signatures: MakeSig::Returns(...).Params(...);
using MakeSig = FixedSizeSignature<ValueKind>;

#if V8_TARGET_ARCH_ARM64
// On ARM64, the Assembler keeps track of pointers to Labels to resolve
// branches to distant targets. Moving labels would confuse the Assembler,
// thus store the label in the Zone.
class MovableLabel {
 public:
  MOVE_ONLY_NO_DEFAULT_CONSTRUCTOR(MovableLabel);
  explicit MovableLabel(Zone* zone) : label_(zone->New<Label>()) {}

  Label* get() { return label_; }

 private:
  Label* label_;
};
#else
// On all other platforms, just store the Label directly.
class MovableLabel {
 public:
  MOVE_ONLY_NO_DEFAULT_CONSTRUCTOR(MovableLabel);
  explicit MovableLabel(Zone*) {}

  Label* get() { return &label_; }

 private:
  Label label_;
};
#endif

compiler::CallDescriptor* GetLoweredCallDescriptor(
    Zone* zone, compiler::CallDescriptor* call_desc) {
  return kSystemPointerSize == 4
             ? compiler::GetI32WasmCallDescriptor(zone, call_desc)
             : call_desc;
}

constexpr Condition GetCompareCondition(WasmOpcode opcode) {
  switch (opcode) {
    case kExprI32Eq:
      return kEqual;
    case kExprI32Ne:
      return kNotEqual;
    case kExprI32LtS:
      return kLessThan;
    case kExprI32LtU:
      return kUnsignedLessThan;
    case kExprI32GtS:
      return kGreaterThan;
    case kExprI32GtU:
      return kUnsignedGreaterThan;
    case kExprI32LeS:
      return kLessThanEqual;
    case kExprI32LeU:
      return kUnsignedLessThanEqual;
    case kExprI32GeS:
      return kGreaterThanEqual;
    case kExprI32GeU:
      return kUnsignedGreaterThanEqual;
    default:
      UNREACHABLE();
  }
}

// Builds a {DebugSideTable}.
class DebugSideTableBuilder {
  using Entry = DebugSideTable::Entry;
  using Value = Entry::Value;

 public:
  enum AssumeSpilling {
    // All register values will be spilled before the pc covered by the debug
    // side table entry. Register slots will be marked as stack slots in the
    // generated debug side table entry.
    kAssumeSpilling,
    // Register slots will be written out as they are.
    kAllowRegisters,
    // Register slots cannot appear since we already spilled.
    kDidSpill
  };

  class EntryBuilder {
   public:
    explicit EntryBuilder(int pc_offset, int stack_height,
                          std::vector<Value> changed_values)
        : pc_offset_(pc_offset),
          stack_height_(stack_height),
          changed_values_(std::move(changed_values)) {}

    Entry ToTableEntry() {
      return Entry{pc_offset_, stack_height_, std::move(changed_values_)};
    }

    void MinimizeBasedOnPreviousStack(const std::vector<Value>& last_values) {
      auto dst = changed_values_.begin();
      auto end = changed_values_.end();
      for (auto src = dst; src != end; ++src) {
        if (src->index < static_cast<int>(last_values.size()) &&
            *src == last_values[src->index]) {
          continue;
        }
        if (dst != src) *dst = *src;
        ++dst;
      }
      changed_values_.erase(dst, end);
    }

    int pc_offset() const { return pc_offset_; }
    void set_pc_offset(int new_pc_offset) { pc_offset_ = new_pc_offset; }

   private:
    int pc_offset_;
    int stack_height_;
    std::vector<Value> changed_values_;
  };

  // Adds a new entry in regular code.
  void NewEntry(int pc_offset,
                base::Vector<DebugSideTable::Entry::Value> values) {
    entries_.emplace_back(pc_offset, static_cast<int>(values.size()),
                          GetChangedStackValues(last_values_, values));
  }

  // Adds a new entry for OOL code, and returns a pointer to a builder for
  // modifying that entry.
  EntryBuilder* NewOOLEntry(base::Vector<DebugSideTable::Entry::Value> values) {
    constexpr int kNoPcOffsetYet = -1;
    ool_entries_.emplace_back(kNoPcOffsetYet, static_cast<int>(values.size()),
                              GetChangedStackValues(last_ool_values_, values));
    return &ool_entries_.back();
  }

  void SetNumLocals(int num_locals) {
    DCHECK_EQ(-1, num_locals_);
    DCHECK_LE(0, num_locals);
    num_locals_ = num_locals;
  }

  std::unique_ptr<DebugSideTable> GenerateDebugSideTable() {
    DCHECK_LE(0, num_locals_);

    // Connect {entries_} and {ool_entries_} by removing redundant stack
    // information from the first {ool_entries_} entry (based on
    // {last_values_}).
    if (!entries_.empty() && !ool_entries_.empty()) {
      ool_entries_.front().MinimizeBasedOnPreviousStack(last_values_);
    }

    std::vector<Entry> entries;
    entries.reserve(entries_.size() + ool_entries_.size());
    for (auto& entry : entries_) entries.push_back(entry.ToTableEntry());
    for (auto& entry : ool_entries_) entries.push_back(entry.ToTableEntry());
    DCHECK(std::is_sorted(
        entries.begin(), entries.end(),
        [](Entry& a, Entry& b) { return a.pc_offset() < b.pc_offset(); }));
    return std::make_unique<DebugSideTable>(num_locals_, std::move(entries));
  }

 private:
  static std::vector<Value> GetChangedStackValues(
      std::vector<Value>& last_values,
      base::Vector<DebugSideTable::Entry::Value> values) {
    std::vector<Value> changed_values;
    int old_stack_size = static_cast<int>(last_values.size());
    last_values.resize(values.size());

    int index = 0;
    for (const auto& value : values) {
      if (index >= old_stack_size || last_values[index] != value) {
        changed_values.push_back(value);
        last_values[index] = value;
      }
      ++index;
    }
    return changed_values;
  }

  int num_locals_ = -1;
  // Keep a snapshot of the stack of the last entry, to generate a delta to the
  // next entry.
  std::vector<Value> last_values_;
  std::vector<EntryBuilder> entries_;
  // Keep OOL code entries separate so we can do proper delta-encoding (more
  // entries might be added between the existing {entries_} and the
  // {ool_entries_}). Store the entries in a list so the pointer is not
  // invalidated by adding more entries.
  std::vector<Value> last_ool_values_;
  std::list<EntryBuilder> ool_entries_;
};

void CheckBailoutAllowed(LiftoffBailoutReason reason, const char* detail,
                         const CompilationEnv* env) {
  // Decode errors are ok.
  if (reason == kDecodeError) return;

  // --liftoff-only ensures that tests actually exercise the Liftoff path
  // without bailing out. We also fail for missing CPU support, to avoid
  // running any TurboFan code under --liftoff-only.
  if (v8_flags.liftoff_only) {
    FATAL("--liftoff-only: treating bailout as fatal error. Cause: %s", detail);
  }

  // Missing CPU features are generally OK, except with --liftoff-only.
  if (reason == kMissingCPUFeature) return;

  // If --enable-testing-opcode-in-wasm is set, we are expected to bailout with
  // "testing opcode".
  if (v8_flags.enable_testing_opcode_in_wasm &&
      strcmp(detail, "testing opcode") == 0) {
    return;
  }

  // Some externally maintained architectures don't fully implement Liftoff yet.
#if V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_S390X || V8_TARGET_ARCH_PPC || \
    V8_TARGET_ARCH_PPC64 || V8_TARGET_ARCH_LOONG64
  return;
#endif

#if V8_TARGET_ARCH_ARM
  // Allow bailout for missing ARMv7 support.
  if (!CpuFeatures::IsSupported(ARMv7) && reason == kUnsupportedArchitecture) {
    return;
  }
#endif

#define LIST_FEATURE(name, ...) WasmEnabledFeature::name,
  constexpr WasmEnabledFeatures kExperimentalFeatures{
      FOREACH_WASM_EXPERIMENTAL_FEATURE_FLAG(LIST_FEATURE)};
#undef LIST_FEATURE

  // Bailout is allowed if any experimental feature is enabled.
  if (env->enabled_features.contains_any(kExperimentalFeatures)) return;

  // Otherwise, bailout is not allowed.
  FATAL("Liftoff bailout should not happen. Cause: %s\n", detail);
}

class TempRegisterScope {
 public:
  LiftoffRegister Acquire(RegClass rc) {
    LiftoffRegList candidates = free_temps_ & GetCacheRegList(rc);
    DCHECK(!candidates.is_empty());
    return free_temps_.clear(candidates.GetFirstRegSet());
  }

  void Return(LiftoffRegister&& temp) {
    DCHECK(!free_temps_.has(temp));
    free_temps_.set(temp);
  }

  void Return(Register&& temp) {
    Return(LiftoffRegister{temp});
    temp = no_reg;
  }

  LiftoffRegList AddTempRegisters(int count, RegClass rc,
                                  LiftoffAssembler* lasm,
                                  LiftoffRegList pinned) {
    LiftoffRegList temps;
    pinned |= free_temps_;
    for (int i = 0; i < count; ++i) {
      temps.set(lasm->GetUnusedRegister(rc, pinned | temps));
    }
    free_temps_ |= temps;
    return temps;
  }

 private:
  LiftoffRegList free_temps_;
};

class ScopedTempRegister {
 public:
  ScopedTempRegister(TempRegisterScope& temp_scope, RegClass rc)
      : reg_(temp_scope.Acquire(rc)), temp_scope_(&temp_scope) {}

  ScopedTempRegister(const ScopedTempRegister&) = delete;

  ScopedTempRegister(ScopedTempRegister&& other) V8_NOEXCEPT
      : reg_(other.reg_),
        temp_scope_(other.temp_scope_) {
    other.temp_scope_ = nullptr;
  }

  ScopedTempRegister& operator=(const ScopedTempRegister&) = delete;

  ~ScopedTempRegister() {
    if (temp_scope_) Reset();
  }

  LiftoffRegister reg() const {
    DCHECK_NOT_NULL(temp_scope_);
    return reg_;
  }

  Register gp_reg() const { return reg().gp(); }

  void Reset() {
    DCHECK_NOT_NULL(temp_scope_);
    temp_scope_->Return(std::move(reg_));
    temp_scope_ = nullptr;
  }

 private:
  LiftoffRegister reg_;
  TempRegisterScope* temp_scope_;
};

class LiftoffCompiler {
 public:
  using ValidationTag = Decoder::NoValidationTag;
  using Value = ValueBase<ValidationTag>;
  static constexpr bool kUsesPoppedArgs = false;

  // Some constants for tier-up checks.
  // In general we use the number of executed machine code bytes as an estimate
  // of how much time was spent in this function.
  // - {kTierUpCostForCheck} is the cost for checking for the tier-up itself,
  //   which is added to the PC distance on every tier-up check. This cost is
  //   for loading the tiering budget, subtracting from one entry in the array,
  //   and the conditional branch if the value is negative.
  // - {kTierUpCostForFunctionEntry} reflects the cost for calling the frame
  //   setup stub in the function prologue (the time is spent in another code
  //   object and hence not reflected in the PC distance).
  static constexpr int kTierUpCostForCheck = 20;
  static constexpr int kTierUpCostForFunctionEntry = 40;

  struct ElseState {
    explicit ElseState(Zone* zone) : label(zone), state(zone) {}
    MovableLabel label;
    LiftoffAssembler::CacheState state;
  };

  struct TryInfo {
    explicit TryInfo(Zone* zone) : catch_state(zone) {}
    LiftoffAssembler::CacheState catch_state;
    Label catch_label;
    bool catch_reached = false;
    bool in_handler = false;
  };

  struct Control : public ControlBase<Value, ValidationTag> {
    ElseState* else_state = nullptr;
    LiftoffAssembler::CacheState label_state;
    MovableLabel label;
    TryInfo* try_info = nullptr;
    // Number of exceptions on the stack below this control.
    int num_exceptions = 0;

    MOVE_ONLY_NO_DEFAULT_CONSTRUCTOR(Control);

    template <typename... Args>
    explicit Control(Zone* zone, Args&&... args) V8_NOEXCEPT
        : ControlBase(zone, std::forward<Args>(args)...),
          label_state(zone),
          label(zone) {}
  };

  using FullDecoder = WasmFullDecoder<ValidationTag, LiftoffCompiler>;
  using ValueKindSig = LiftoffAssembler::ValueKindSig;

  class MostlySmallValueKindSig : public Signature<ValueKind> {
   public:
    MostlySmallValueKindSig(Zone* zone, const FunctionSig* sig)
        : Signature<ValueKind>(sig->return_count(), sig->parameter_count(),
                               MakeKinds(inline_storage_, zone, sig)) {}

   private:
    static constexpr size_t kInlineStorage = 8;

    static ValueKind* MakeKinds(ValueKind* storage, Zone* zone,
                                const FunctionSig* sig) {
      const size_t size = sig->parameter_count() + sig->return_count();
      if (V8_UNLIKELY(size > kInlineStorage)) {
        storage = zone->AllocateArray<ValueKind>(size);
      }
      std::transform(sig->all().begin(), sig->all().end(), storage,
                     [](ValueType type) { return type.kind(); });
      return storage;
    }

    ValueKind inline_storage_[kInlineStorage];
  };

  // For debugging, we need to spill registers before a trap or a stack check to
  // be able to inspect them.
  struct SpilledRegistersForInspection : public ZoneObject {
    struct Entry {
      int offset;
      LiftoffRegister reg;
      ValueKind kind;
    };
    ZoneVector<Entry> entries;

    explicit SpilledRegistersForInspection(Zone* zone) : entries(zone) {}
  };

  struct OutOfLineSafepointInfo {
    ZoneVector<int> slots;
    LiftoffRegList spills;

    explicit OutOfLineSafepointInfo(Zone* zone) : slots(zone) {}
  };

  struct OutOfLineCode {
    MovableLabel label;
    MovableLabel continuation;
    Builtin builtin;
    WasmCodePosition position;
    LiftoffRegList regs_to_save;
    Register cached_instance_data;
    OutOfLineSafepointInfo* safepoint_info;
    // These two pointers will only be used for debug code:
    SpilledRegistersForInspection* spilled_registers;
    DebugSideTableBuilder::EntryBuilder* debug_sidetable_entry_builder;

    // Named constructors:
    static OutOfLineCode Trap(
        Zone* zone, Builtin builtin, WasmCodePosition pos,
        SpilledRegistersForInspection* spilled_registers,
        OutOfLineSafepointInfo* safepoint_info,
        DebugSideTableBuilder::EntryBuilder* debug_sidetable_entry_builder) {
      DCHECK_LT(0, pos);
      return {
          MovableLabel{zone},            // label
          MovableLabel{zone},            // continuation
          builtin,                       // builtin
          pos,                           // position
          {},                            // regs_to_save
          no_reg,                        // cached_instance_data
          safepoint_info,                // safepoint_info
          spilled_registers,             // spilled_registers
          debug_sidetable_entry_builder  // debug_side_table_entry_builder
      };
    }
    static OutOfLineCode StackCheck(
        Zone* zone, WasmCodePosition pos, LiftoffRegList regs_to_save,
        Register cached_instance_data,
        SpilledRegistersForInspection* spilled_regs,
        OutOfLineSafepointInfo* safepoint_info,
        DebugSideTableBuilder::EntryBuilder* debug_sidetable_entry_builder) {
      return {
          MovableLabel{zone},            // label
          MovableLabel{zone},            // continuation
          Builtin::kWasmStackGuard,      // builtin
          pos,                           // position
          regs_to_save,                  // regs_to_save
          cached_instance_data,          // cached_instance_data
          safepoint_info,                // safepoint_info
          spilled_regs,                  // spilled_registers
          debug_sidetable_entry_builder  // debug_side_table_entry_builder
      };
    }
    static OutOfLineCode TierupCheck(
        Zone* zone, WasmCodePosition pos, LiftoffRegList regs_to_save,
        Register cached_instance_data,
        SpilledRegistersForInspection* spilled_regs,
        OutOfLineSafepointInfo* safepoint_info,
        DebugSideTableBuilder::EntryBuilder* debug_sidetable_entry_builder) {
      return {
          MovableLabel{zone},            // label
          MovableLabel{zone},            // continuation,
          Builtin::kWasmTriggerTierUp,   // builtin
          pos,                           // position
          regs_to_save,                  // regs_to_save
          cached_instance_data,          // cached_instance_data
          safepoint_info,                // safepoint_info
          spilled_regs,                  // spilled_registers
          debug_sidetable_entry_builder  // debug_side_table_entry_builder
      };
    }
  };

  LiftoffCompiler(compiler::CallDescriptor* call_descriptor,
                  CompilationEnv* env, Zone* zone,
                  std::unique_ptr<AssemblerBuffer> buffer,
                  DebugSideTableBuilder* debug_sidetable_builder,
                  const LiftoffOptions& options)
      : asm_(zone, std::move(buffer)),
        descriptor_(GetLoweredCallDescriptor(zone, call_descriptor)),
        env_(env),
        debug_sidetable_builder_(debug_sidetable_builder),
        for_debugging_(options.for_debugging),
        func_index_(options.func_index),
        out_of_line_code_(zone),
        source_position_table_builder_(zone),
        protected_instructions_(zone),
        zone_(zone),
        safepoint_table_builder_(zone_),
        next_breakpoint_ptr_(options.breakpoints.begin()),
        next_breakpoint_end_(options.breakpoints.end()),
        dead_breakpoint_(options.dead_breakpoint),
        handlers_(zone),
        max_steps_(options.max_steps),
        nondeterminism_(options.nondeterminism) {
    // We often see huge numbers of traps per function, so pre-reserve some
    // space in that vector. 128 entries is enough for ~94% of functions on
    // modern modules, as of 2022-06-03.
    out_of_line_code_.reserve(128);

    DCHECK(options.is_initialized());
    // If there are no breakpoints, both pointers should be nullptr.
    DCHECK_IMPLIES(
        next_breakpoint_ptr_ == next_breakpoint_end_,
        next_breakpoint_ptr_ == nullptr && next_breakpoint_end_ == nullptr);
    DCHECK_IMPLIES(!for_debugging_, debug_sidetable_builder_ == nullptr);
  }

  bool did_bailout() const { return bailout_reason_ != kSuccess; }
  LiftoffBailoutReason bailout_reason() const { return bailout_reason_; }

  void GetCode(CodeDesc* desc) {
    asm_.GetCode(nullptr, desc, &safepoint_table_builder_,
                 handler_table_offset_);
  }

  std::unique_ptr<AssemblerBuffer> ReleaseBuffer() {
    return asm_.ReleaseBuffer();
  }

  std::unique_ptr<LiftoffFrameDescriptionForDeopt> ReleaseFrameDescriptions() {
    return std::move(frame_description_);
  }

  base::OwnedVector<uint8_t> GetSourcePositionTable() {
    return source_position_table_builder_.ToSourcePositionTableVector();
  }

  base::OwnedVector<uint8_t> GetProtectedInstructionsData() const {
    return base::OwnedVector<uint8_t>::Of(base::Vector<const uint8_t>::cast(
        base::VectorOf(protected_instructions_)));
  }

  uint32_t GetTotalFrameSlotCountForGC() const {
    return __ GetTotalFrameSlotCountForGC();
  }

  uint32_t OolSpillCount() const { return __ OolSpillCount(); }

  void unsupported(FullDecoder* decoder, LiftoffBailoutReason reason,
                   const char* detail) {
    DCHECK_NE(kSuccess, reason);
    if (did_bailout()) return;
    bailout_reason_ = reason;
    TRACE("unsupported: %s\n", detail);
    decoder->errorf(decoder->pc_offset(), "unsupported liftoff operation: %s",
                    detail);
    UnuseLabels(decoder);
    CheckBailoutAllowed(reason, detail, env_);
  }

  bool DidAssemblerBailout(FullDecoder* decoder) {
    if (decoder->failed() || !__ did_bailout()) return false;
    unsupported(decoder, __ bailout_reason(), __ bailout_detail());
    return true;
  }

  V8_INLINE bool CheckSupportedType(FullDecoder* decoder, ValueKind kind,
                                    const char* context) {
    if (V8_LIKELY(supported_types_.contains(kind))) return true;
    return MaybeBailoutForUnsupportedType(decoder, kind, context);
  }

  V8_NOINLINE bool MaybeBailoutForUnsupportedType(FullDecoder* decoder,
                                                  ValueKind kind,
                                                  const char* context) {
    DCHECK(!supported_types_.contains(kind));

    // Lazily update {supported_types_}; then check again.
    if (CpuFeatures::SupportsWasmSimd128()) supported_types_.Add(kS128);
    if (supported_types_.contains(kind)) return true;

    LiftoffBailoutReason bailout_reason;
    switch (kind) {
      case kS128:
        bailout_reason = kSimd;
        break;
      default:
        UNREACHABLE();
    }
    base::EmbeddedVector<char, 128> buffer;
    SNPrintF(buffer, "%s %s", name(kind), context);
    unsupported(decoder, bailout_reason, buffer.begin());
    return false;
  }

  void UnuseLabels(FullDecoder* decoder) {
#ifdef DEBUG
    auto Unuse = [](Label* label) {
      label->Unuse();
      label->UnuseNear();
    };
    // Unuse all labels now, otherwise their destructor will fire a DCHECK error
    // if they where referenced before.
    uint32_t control_depth = decoder ? decoder->control_depth() : 0;
    for (uint32_t i = 0; i < control_depth; ++i) {
      Control* c = decoder->control_at(i);
      Unuse(c->label.get());
      if (c->else_state) Unuse(c->else_state->label.get());
      if (c->try_info != nullptr) Unuse(&c->try_info->catch_label);
    }
    for (auto& ool : out_of_line_code_) Unuse(ool.label.get());
#endif
  }

  void StartFunction(FullDecoder* decoder) {
    if (v8_flags.trace_liftoff && !v8_flags.trace_wasm_decoder) {
      StdoutStream{} << "hint: add --trace-wasm-decoder to also see the wasm "
                        "instructions being decoded\n";
    }
    int num_locals = decoder->num_locals();
    __ set_num_locals(num_locals);
    for (int i = 0; i < num_locals; ++i) {
      ValueKind kind = decoder->local_type(i).kind();
      __ set_local_kind(i, kind);
    }
  }

  class ParameterProcessor {
   public:
    ParameterProcessor(LiftoffCompiler* compiler, uint32_t num_params)
        : compiler_(compiler), num_params_(num_params) {}

    void Process() {
      // First pass: collect parameter registers.
      while (NextParam()) {
        MaybeCollectRegister();
        if (needs_gp_pair_) {
          NextLocation();
          MaybeCollectRegister();
        }
      }
      // Second pass: allocate parameters.
      param_idx_ = 0;
      input_idx_ = kFirstInputIdx;
      while (NextParam()) {
        LiftoffRegister reg = LoadToReg(param_regs_);
        // In-sandbox corruption can replace one function's code with another's.
        // That's mostly safe, but certain signature mismatches can violate
        // security-relevant invariants later. To maintain such invariants,
        // explicitly clear the high word of any i32 parameters in 64-bit
        // registers.
        // 'clear_i32_upper_half' is empty on LoongArch64, MIPS64 and riscv64,
        // because they will explicitly zero-extend their lower halves before
        // using them for memory accesses anyway.
        // In addition, the generic js-to-wasm wrapper does a sign-extension
        // of i32 parameters, so clearing the upper half is required for
        // correctness in this case.
#if V8_TARGET_ARCH_64_BIT
        if (kind_ == kI32 && location_.IsRegister()) {
          compiler_->asm_.clear_i32_upper_half(reg.gp());
        }
#endif
        if (needs_gp_pair_) {
          NextLocation();
          LiftoffRegister reg2 = LoadToReg(param_regs_ | LiftoffRegList{reg});
          reg = LiftoffRegister::ForPair(reg.gp(), reg2.gp());
        }
        compiler_->asm_.PushRegister(kind_, reg);
      }
    }

   private:
    bool NextParam() {
      if (param_idx_ >= num_params_) {
        DCHECK_EQ(input_idx_, compiler_->descriptor_->InputCount());
        return false;
      }
      kind_ = compiler_->asm_.local_kind(param_idx_++);
      needs_gp_pair_ = needs_gp_reg_pair(kind_);
      reg_kind_ = needs_gp_pair_ ? kI32 : kind_;
      rc_ = reg_class_for(reg_kind_);
      NextLocation();
      return true;
    }

    void NextLocation() {
      location_ = compiler_->descriptor_->GetInputLocation(input_idx_++);
    }

    LiftoffRegister CurrentRegister() {
      DCHECK(!location_.IsAnyRegister());
      return LiftoffRegister::from_external_code(rc_, reg_kind_,
                                                 location_.AsRegister());
    }

    void MaybeCollectRegister() {
      if (!location_.IsRegister()) return;
      DCHECK(!param_regs_.has(CurrentRegister()));
      param_regs_.set(CurrentRegister());
    }

    LiftoffRegister LoadToReg(LiftoffRegList pinned) {
      if (location_.IsRegister()) {
        LiftoffRegister reg = CurrentRegister();
        DCHECK(compiler_->asm_.cache_state()->is_free(reg));
        // Unpin the register, to avoid depending on the set of allocatable
        // registers being larger than the set of parameter registers.
        param_regs_.clear(reg);
        return reg;
      }
      DCHECK(location_.IsCallerFrameSlot());
      LiftoffRegister reg = compiler_->asm_.GetUnusedRegister(rc_, pinned);
      compiler_->asm_.LoadCallerFrameSlot(reg, -location_.AsCallerFrameSlot(),
                                          reg_kind_);
      return reg;
    }

    // Input 0 is the code target, 1 is the instance.
    static constexpr uint32_t kFirstInputIdx = 2;

    LiftoffCompiler* compiler_;
    const uint32_t num_params_;
    uint32_t param_idx_{0};
    uint32_t input_idx_{kFirstInputIdx};
    ValueKind kind_;
    bool needs_gp_pair_;
    ValueKind reg_kind_;
    RegClass rc_;
    LinkageLocation location_{LinkageLocation::ForAnyRegister()};
    LiftoffRegList param_regs_;
  };

  void StackCheck(FullDecoder* decoder, WasmCodePosition position) {
    CODE_COMMENT("stack check");
    if (!v8_flags.wasm_stack_checks) return;

    LiftoffRegList regs_to_save = __ cache_state()->used_registers;
    // The cached instance data will be reloaded separately.
    if (__ cache_state()->cached_instance_data != no_reg) {
      DCHECK(regs_to_save.has(__ cache_state()->cached_instance_data));
      regs_to_save.clear(__ cache_state()->cached_instance_data);
    }
    SpilledRegistersForInspection* spilled_regs = nullptr;

    OutOfLineSafepointInfo* safepoint_info =
        zone_->New<OutOfLineSafepointInfo>(zone_);
    __ cache_state()->GetTaggedSlotsForOOLCode(
        &safepoint_info->slots, &safepoint_info->spills,
        for_debugging_
            ? LiftoffAssembler::CacheState::SpillLocation::kStackSlots
            : LiftoffAssembler::CacheState::SpillLocation::kTopOfStack);
    if (V8_UNLIKELY(for_debugging_)) {
      // When debugging, we do not just push all registers to the stack, but we
      // spill them to their proper stack locations such that we can inspect
      // them.
      // The only exception is the cached memory start, which we just push
      // before the stack check and pop afterwards.
      regs_to_save = {};
      if (__ cache_state()->cached_mem_start != no_reg) {
        regs_to_save.set(__ cache_state()->cached_mem_start);
      }
      spilled_regs = GetSpilledRegistersForInspection();
    }
    out_of_line_code_.push_back(OutOfLineCode::StackCheck(
        zone_, position, regs_to_save, __ cache_state()->cached_instance_data,
        spilled_regs, safepoint_info, RegisterOOLDebugSideTableEntry(decoder)));
    OutOfLineCode& ool = out_of_line_code_.back();
    __ StackCheck(ool.label.get());
    __ bind(ool.continuation.get());
  }

  void TierupCheck(FullDecoder* decoder, WasmCodePosition position,
                   int budget_used) {
    if (for_debugging_ != kNotForDebugging) return;
    SCOPED_CODE_COMMENT("tierup check");
    budget_used += kTierUpCostForCheck;
    // We never want to blow the entire budget at once.
    const int max_budget_use = std::max(1, v8_flags.wasm_tiering_budget / 4);
    if (budget_used > max_budget_use) budget_used = max_budget_use;

    // We should always decrement the budget, and we don't expect integer
    // overflows in the budget calculation.
    DCHECK_LE(1, budget_used);

    SpilledRegistersForInspection* spilled_regs = nullptr;

    OutOfLineSafepointInfo* safepoint_info =
        zone_->New<OutOfLineSafepointInfo>(zone_);
    __ cache_state()->GetTaggedSlotsForOOLCode(
        &safepoint_info->slots, &safepoint_info->spills,
        LiftoffAssembler::CacheState::SpillLocation::kTopOfStack);

    LiftoffRegList regs_to_save = __ cache_state()->used_registers;
    // The cached instance will be reloaded separately.
    if (__ cache_state()->cached_instance_data != no_reg) {
      DCHECK(regs_to_save.has(__ cache_state()->cached_instance_data));
      regs_to_save.clear(__ cache_state()->cached_instance_data);
    }

    out_of_line_code_.push_back(OutOfLineCode::TierupCheck(
        zone_, position, regs_to_save, __ cache_state()->cached_instance_data,
        spilled_regs, safepoint_info, RegisterOOLDebugSideTableEntry(decoder)));
    OutOfLineCode& ool = out_of_line_code_.back();

    FREEZE_STATE(tierup_check);
    __ CheckTierUp(declared_function_index(env_->module, func_index_),
                   budget_used, ool.label.get(), tierup_check);

    __ bind(ool.continuation.get());
  }

  bool SpillLocalsInitially(FullDecoder* decoder, uint32_t num_params) {
    int actual_locals = __ num_locals() - num_params;
    DCHECK_LE(0, actual_locals);
    constexpr int kNumCacheRegisters = kLiftoffAssemblerGpCacheRegs.Count();
    // If we have many locals, we put them on the stack initially. This avoids
    // having to spill them on merge points. Use of these initial values should
    // be rare anyway.
    if (actual_locals > kNumCacheRegisters / 2) return true;
    // If there are locals which are not i32 or i64, we also spill all locals,
    // because other types cannot be initialized to constants.
    for (uint32_t param_idx = num_params; param_idx < __ num_locals();
         ++param_idx) {
      ValueKind kind = __ local_kind(param_idx);
      if (kind != kI32 && kind != kI64) return true;
    }
    return false;
  }

  V8_NOINLINE V8_PRESERVE_MOST void TraceFunctionEntry(FullDecoder* decoder) {
    CODE_COMMENT("trace function entry");
    __ SpillAllRegisters();
    source_position_table_builder_.AddPosition(
        __ pc_offset(), SourcePosition(decoder->position()), false);
    __ CallBuiltin(Builtin::kWasmTraceEnter);
    DefineSafepoint();
  }

  bool dynamic_tiering() {
    return env_->dynamic_tiering && for_debugging_ == kNotForDebugging &&
           (v8_flags.wasm_tier_up_filter == -1 ||
            v8_flags.wasm_tier_up_filter == func_index_);
  }

  bool inlining_enabled(FullDecoder* decoder) {
    // Force inlining for wasm-gc modules, regardless of the flag's value.
    return decoder->enabled_.has_inlining() || decoder->module_->is_wasm_gc;
  }

  void StartFunctionBody(FullDecoder* decoder, Control* block) {
    for (uint32_t i = 0; i < __ num_locals(); ++i) {
      if (!CheckSupportedType(decoder, __ local_kind(i), "param")) return;
    }

    // Parameter 0 is the instance parameter.
    uint32_t num_params =
        static_cast<uint32_t>(decoder->sig_->parameter_count());

    __ CodeEntry();

    if (inlining_enabled(decoder)) {
      CODE_COMMENT("frame setup");
      int declared_func_index =
          func_index_ - env_->module->num_imported_functions;
      DCHECK_GE(declared_func_index, 0);
      __ CallFrameSetupStub(declared_func_index);
    } else {
      __ EnterFrame(StackFrame::WASM);
    }
    __ set_has_frame(true);
    pc_offset_stack_frame_construction_ = __ PrepareStackFrame();
    // {PrepareStackFrame} is the first platform-specific assembler method.
    // If this failed, we can bail out immediately, avoiding runtime overhead
    // and potential failures because of other unimplemented methods.
    // A platform implementing {PrepareStackFrame} must ensure that we can
    // finish compilation without errors even if we hit unimplemented
    // LiftoffAssembler methods.
    if (DidAssemblerBailout(decoder)) return;

    // Input 0 is the call target, the instance is at 1.
    [[maybe_unused]] constexpr int kInstanceParameterIndex = 1;
    // Check that {kWasmInstanceRegister} matches our call descriptor.
    DCHECK_EQ(kWasmInstanceRegister,
              Register::from_code(
                  descriptor_->GetInputLocation(kInstanceParameterIndex)
                      .AsRegister()));
    __ cache_state()->SetInstanceCacheRegister(kWasmInstanceRegister);

    if (num_params) {
      CODE_COMMENT("process parameters");
      ParameterProcessor processor(this, num_params);
      processor.Process();
    }
    int params_size = __ TopSpillOffset();

    // Initialize locals beyond parameters.
    if (num_params < __ num_locals()) CODE_COMMENT("init locals");
    if (SpillLocalsInitially(decoder, num_params)) {
      bool has_refs = false;
      for (uint32_t param_idx = num_params; param_idx < __ num_locals();
           ++param_idx) {
        ValueKind kind = __ local_kind(param_idx);
        has_refs |= is_reference(kind);
        __ PushStack(kind);
      }
      int spill_size = __ TopSpillOffset() - params_size;
      __ FillStackSlotsWithZero(params_size, spill_size);

      // Initialize all reference type locals with ref.null.
      if (has_refs) {
        LiftoffRegList pinned;
        Register null_ref_reg =
            pinned.set(__ GetUnusedRegister(kGpReg, pinned).gp());
        Register wasm_null_ref_reg =
            pinned.set(__ GetUnusedRegister(kGpReg, pinned).gp());
        LoadNullValue(null_ref_reg, kWasmExternRef);
        LoadNullValue(wasm_null_ref_reg, kWasmAnyRef);
        for (uint32_t local_index = num_params; local_index < __ num_locals();
             ++local_index) {
          ValueType type = decoder->local_types_[local_index];
          if (type.is_reference()) {
            __ Spill(__ cache_state()->stack_state[local_index].offset(),
                     IsSubtypeOf(type, kWasmExternRef, decoder->module_) ||
                             IsSubtypeOf(type, kWasmExnRef, decoder->module_)
                         ? LiftoffRegister(null_ref_reg)
                         : LiftoffRegister(wasm_null_ref_reg),
                     type.kind());
          }
        }
      }
    } else {
      for (uint32_t param_idx = num_params; param_idx < __ num_locals();
           ++param_idx) {
        ValueKind kind = __ local_kind(param_idx);
        // Anything which is not i32 or i64 requires spilling.
        DCHECK(kind == kI32 || kind == kI64);
        __ PushConstant(kind, int32_t{0});
      }
    }

    DCHECK_EQ(__ num_locals(), __ cache_state()->stack_height());

    if (V8_UNLIKELY(debug_sidetable_builder_)) {
      debug_sidetable_builder_->SetNumLocals(__ num_locals());
    }

    if (V8_UNLIKELY(for_debugging_)) {
      __ ResetOSRTarget();
      if (V8_UNLIKELY(max_steps_)) {
        // Generate the single OOL code to jump to if {max_steps_} have been
        // executed.
        DCHECK_EQ(0, out_of_line_code_.size());
        // This trap is never intercepted (e.g. by a debugger), so we do not
        // need safepoint information (which would be difficult to compute if
        // the OOL code is shared).
        out_of_line_code_.push_back(OutOfLineCode::Trap(
            zone_, Builtin::kThrowWasmTrapUnreachable, decoder->position(),
            nullptr, nullptr, nullptr));

        // Subtract 16 steps for the function call itself (including the
        // function prologue), plus 1 for each local (including parameters). Do
        // this only *after* setting up the frame completely, even though we
        // already executed the work then.
        CheckMaxSteps(decoder, 16 + __ num_locals());
      }
    } else {
      DCHECK(!max_steps_);
    }

    // If debug code is enabled, assert that the first parameter is a
    // WasmTrustedInstanceData.
    if (v8_flags.debug_code) {
      SCOPED_CODE_COMMENT("Check instance data parameter type");
      LiftoffRegList pinned;
      Register scratch = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
      Register instance = pinned.set(LoadInstanceIntoRegister(pinned, scratch));
      // Load the map.
      __ LoadMap(scratch, instance);
      // Load the instance type.
      __ Load(LiftoffRegister{scratch}, scratch, no_reg,
              wasm::ObjectAccess::ToTagged(Map::kInstanceTypeOffset),
              LoadType::kI32Load16U);
      // If not WASM_TRUSTED_INSTANCE_DATA_TYPE -> error.
      Label ok;
      FreezeCacheState frozen{asm_};
      __ emit_i32_cond_jumpi(kEqual, &ok, scratch,
                             WASM_TRUSTED_INSTANCE_DATA_TYPE, frozen);
      __ AssertUnreachable(AbortReason::kUnexpectedInstanceType);
      __ bind(&ok);
    }

    // The function-prologue stack check is associated with position 0, which
    // is never a position of any instruction in the function.
    StackCheck(decoder, 0);

    if (V8_UNLIKELY(v8_flags.trace_wasm)) TraceFunctionEntry(decoder);
  }

  void GenerateOutOfLineCode(OutOfLineCode* ool) {
    CODE_COMMENT((std::string("OOL: ") + Builtins::name(ool->builtin)).c_str());
    __ bind(ool->label.get());
    const bool is_stack_check = ool->builtin == Builtin::kWasmStackGuard;
    const bool is_tierup = ool->builtin == Builtin::kWasmTriggerTierUp;

    if (!ool->regs_to_save.is_empty()) {
      __ PushRegisters(ool->regs_to_save);
    }
    if (V8_UNLIKELY(ool->spilled_registers != nullptr)) {
      for (auto& entry : ool->spilled_registers->entries) {
        // We should not push and spill the same register.
        DCHECK(!ool->regs_to_save.has(entry.reg));
        __ Spill(entry.offset, entry.reg, entry.kind);
      }
    }

    source_position_table_builder_.AddPosition(
        __ pc_offset(), SourcePosition(ool->position), true);
    __ CallBuiltin(ool->builtin);
    // It is safe to not check for existing safepoint at this address since we
    // just emitted a call.
    auto safepoint = safepoint_table_builder_.DefineSafepoint(&asm_);

    if (ool->safepoint_info) {
      for (auto index : ool->safepoint_info->slots) {
        safepoint.DefineTaggedStackSlot(index);
      }

      int total_frame_size = __ GetTotalFrameSize();
      // {total_frame_size} is the highest offset from the FP that is used to
      // store a value. The offset of the first spill slot should therefore be
      // {(total_frame_size / kSystemPointerSize) + 1}. However, spill slots
      // don't start at offset '0' but at offset '-1' (or
      // {-kSystemPointerSize}). Therefore we have to add another '+ 1' to the
      // index of the first spill slot.
      int index = (total_frame_size / kSystemPointerSize) + 2;

      __ RecordSpillsInSafepoint(safepoint, ool->regs_to_save,
                                 ool->safepoint_info->spills, index);
    }

    DCHECK_EQ(!debug_sidetable_builder_, !ool->debug_sidetable_entry_builder);
    if (V8_UNLIKELY(ool->debug_sidetable_entry_builder)) {
      ool->debug_sidetable_entry_builder->set_pc_offset(__ pc_offset());
    }
    DCHECK_EQ(ool->continuation.get()->is_bound(), is_stack_check || is_tierup);
    if (is_stack_check) {
      MaybeOSR();
    }
    if (!ool->regs_to_save.is_empty()) __ PopRegisters(ool->regs_to_save);
    if (is_stack_check || is_tierup) {
      if (V8_UNLIKELY(ool->spilled_registers != nullptr)) {
        DCHECK(for_debugging_);
        for (auto& entry : ool->spilled_registers->entries) {
          __ Fill(entry.reg, entry.offset, entry.kind);
        }
      }
      if (ool->cached_instance_data != no_reg) {
        __ LoadInstanceDataFromFrame(ool->cached_instance_data);
      }
      __ emit_jump(ool->continuation.get());
    } else {
      __ AssertUnreachable(AbortReason::kUnexpectedReturnFromWasmTrap);
    }
  }

  void FinishFunction(FullDecoder* decoder) {
    if (DidAssemblerBailout(decoder)) return;
    __ AlignFrameSize();
#if DEBUG
    int frame_size = __ GetTotalFrameSize();
#endif
    for (OutOfLineCode& ool : out_of_line_code_) {
      GenerateOutOfLineCode(&ool);
    }
    DCHECK_EQ(frame_size, __ GetTotalFrameSize());
    __ PatchPrepareStackFrame(pc_offset_stack_frame_construction_,
                              &safepoint_table_builder_,
                              inlining_enabled(decoder));
    __ FinishCode();
    safepoint_table_builder_.Emit(&asm_, __ GetTotalFrameSlotCountForGC());
    // Emit the handler table.
    if (!handlers_.empty()) {
      handler_table_offset_ = HandlerTable::EmitReturnTableStart(&asm_);
      for (auto& handler : handlers_) {
        HandlerTable::EmitReturnEntry(&asm_, handler.pc_offset,
                                      handler.handler.get()->pos());
      }
    }
    __ MaybeEmitOutOfLineConstantPool();
    // The previous calls may have also generated a bailout.
    DidAssemblerBailout(decoder);
    DCHECK_EQ(num_exceptions_, 0);

    if (inlining_enabled(decoder) && !encountered_call_instructions_.empty()) {
      // Update the call targets stored in the WasmModule.
      TypeFeedbackStorage& type_feedback = env_->module->type_feedback;
      base::SharedMutexGuard<base::kExclusive> mutex_guard(
          &type_feedback.mutex);
      FunctionTypeFeedback& function_feedback =
          type_feedback.feedback_for_function[func_index_];
      function_feedback.liftoff_frame_size = __ GetTotalFrameSize();
      base::OwnedVector<uint32_t>& call_targets =
          function_feedback.call_targets;
      if (call_targets.empty()) {
        call_targets =
            base::OwnedVector<uint32_t>::Of(encountered_call_instructions_);
      } else {
        DCHECK_EQ(call_targets.as_vector(),
                  base::VectorOf(encountered_call_instructions_));
      }
    }

    if (frame_description_) {
      frame_description_->total_frame_size = __ GetTotalFrameSize();
    }
  }

  void OnFirstError(FullDecoder* decoder) {
    if (!did_bailout()) bailout_reason_ = kDecodeError;
    UnuseLabels(decoder);
    asm_.AbortCompilation();
  }

  // Rule of thumb: an instruction is "heavy" when its runtime is linear in
  // some random variable that the fuzzer generates.
#define FUZZER_HEAVY_INSTRUCTION                      \
  do {                                                \
    if (V8_UNLIKELY(max_steps_ != nullptr)) {         \
      CheckMaxSteps(decoder, kHeavyInstructionSteps); \
    }                                                 \
  } while (false)

  V8_NOINLINE void CheckMaxSteps(FullDecoder* decoder, int steps_done = 1) {
    DCHECK_LE(1, steps_done);
    SCOPED_CODE_COMMENT("check max steps");
    LiftoffRegList pinned;
    LiftoffRegister max_steps = pinned.set(__ GetUnusedRegister(kGpReg, {}));
    LiftoffRegister max_steps_addr =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    {
      FREEZE_STATE(frozen);
      __ LoadConstant(
          max_steps_addr,
          WasmValue::ForUintPtr(reinterpret_cast<uintptr_t>(max_steps_)));
      __ Load(max_steps, max_steps_addr.gp(), no_reg, 0, LoadType::kI32Load);
      // Subtract first (and store the result), so the caller sees that
      // max_steps ran negative. Since we never subtract too much at once, we
      // cannot underflow.
      DCHECK_GE(kMaxInt / 16, steps_done);  // An arbitrary limit.
      __ emit_i32_subi(max_steps.gp(), max_steps.gp(), steps_done);
      __ Store(max_steps_addr.gp(), no_reg, 0, max_steps, StoreType::kI32Store,
               pinned);
      // Abort if max steps have been executed.
      DCHECK_EQ(Builtin::kThrowWasmTrapUnreachable,
                out_of_line_code_.front().builtin);
      Label* trap_label = out_of_line_code_.front().label.get();
      __ emit_i32_cond_jumpi(kLessThan, trap_label, max_steps.gp(), 0, frozen);
    }
  }

  V8_NOINLINE void EmitDebuggingInfo(FullDecoder* decoder, WasmOpcode opcode) {
    DCHECK(for_debugging_);

    // Snapshot the value types (from the decoder) here, for potentially
    // building a debug side table entry later. Arguments will have been popped
    // from the stack later (when we need them), and Liftoff does not keep
    // precise type information.
    stack_value_types_for_debugging_ = GetStackValueTypesForDebugging(decoder);

    if (!WasmOpcodes::IsBreakable(opcode)) return;

    bool has_breakpoint = false;
    if (next_breakpoint_ptr_) {
      if (*next_breakpoint_ptr_ == 0) {
        // A single breakpoint at offset 0 indicates stepping.
        DCHECK_EQ(next_breakpoint_ptr_ + 1, next_breakpoint_end_);
        has_breakpoint = true;
      } else {
        while (next_breakpoint_ptr_ != next_breakpoint_end_ &&
               *next_breakpoint_ptr_ < decoder->position()) {
          // Skip unreachable breakpoints.
          ++next_breakpoint_ptr_;
        }
        if (next_breakpoint_ptr_ == next_breakpoint_end_) {
          next_breakpoint_ptr_ = next_breakpoint_end_ = nullptr;
        } else if (*next_breakpoint_ptr_ == decoder->position()) {
          has_breakpoint = true;
        }
      }
    }
    if (has_breakpoint) {
      CODE_COMMENT("breakpoint");
      EmitBreakpoint(decoder);
      // Once we emitted an unconditional breakpoint, we don't need to check
      // function entry breaks any more.
      did_function_entry_break_checks_ = true;
    } else if (!did_function_entry_break_checks_) {
      did_function_entry_break_checks_ = true;
      CODE_COMMENT("check function entry break");
      Label do_break;
      Label no_break;
      Register flag = __ GetUnusedRegister(kGpReg, {}).gp();

      // Check the "hook on function call" flag. If set, trigger a break.
      LOAD_INSTANCE_FIELD(flag, HookOnFunctionCallAddress, kSystemPointerSize,
                          {});
      FREEZE_STATE(frozen);
      __ Load(LiftoffRegister{flag}, flag, no_reg, 0, LoadType::kI32Load8U, {});
      __ emit_cond_jump(kNotZero, &do_break, kI32, flag, no_reg, frozen);

      // Check if we should stop on "script entry".
      LOAD_INSTANCE_FIELD(flag, BreakOnEntry, kUInt8Size, {});
      __ emit_cond_jump(kZero, &no_break, kI32, flag, no_reg, frozen);

      __ bind(&do_break);
      EmitBreakpoint(decoder);
      __ bind(&no_break);
    } else if (dead_breakpoint_ == decoder->position()) {
      DCHECK(!next_breakpoint_ptr_ ||
             *next_breakpoint_ptr_ != dead_breakpoint_);
      // The top frame is paused at this position, but the breakpoint was
      // removed. Adding a dead breakpoint here ensures that the source
      // position exists, and that the offset to the return address is the
      // same as in the old code.
      CODE_COMMENT("dead breakpoint");
      Label cont;
      __ emit_jump(&cont);
      EmitBreakpoint(decoder);
      __ bind(&cont);
    }
    if (V8_UNLIKELY(max_steps_ != nullptr)) {
      CheckMaxSteps(decoder);
    }
  }

  void NextInstruction(FullDecoder* decoder, WasmOpcode opcode) {
    TraceCacheState(decoder);
    SLOW_DCHECK(__ ValidateCacheState());
    CODE_COMMENT(WasmOpcodes::OpcodeName(
        WasmOpcodes::IsPrefixOpcode(opcode)
            ? decoder->read_prefixed_opcode<ValidationTag>(decoder->pc()).first
            : opcode));

    if (!has_outstanding_op() && decoder->control_at(0)->reachable()) {
      // Decoder stack and liftoff stack have to be in sync if current code
      // path is reachable.
      DCHECK_EQ(decoder->stack_size() + __ num_locals() + num_exceptions_,
                __ cache_state()->stack_state.size());
    }

    // Add a single check, so that the fast path can be inlined while
    // {EmitDebuggingInfo} stays outlined.
    if (V8_UNLIKELY(for_debugging_)) EmitDebuggingInfo(decoder, opcode);
  }

  void EmitBreakpoint(FullDecoder* decoder) {
    DCHECK(for_debugging_);
    source_position_table_builder_.AddPosition(
        __ pc_offset(), SourcePosition(decoder->position()), true);
    __ CallBuiltin(Builtin::kWasmDebugBreak);
    DefineSafepointWithCalleeSavedRegisters();
    RegisterDebugSideTableEntry(decoder,
                                DebugSideTableBuilder::kAllowRegisters);
    MaybeOSR();
  }

  void PushControl(Control* block) {
    // The Liftoff stack includes implicit exception refs stored for catch
    // blocks, so that they can be rethrown.
    block->num_exceptions = num_exceptions_;
  }

  void Block(FullDecoder* decoder, Control* block) { PushControl(block); }

  void Loop(FullDecoder* decoder, Control* loop) {
    // Before entering a loop, spill all locals to the stack, in order to free
    // the cache registers, and to avoid unnecessarily reloading stack values
    // into registers at branches.
    // TODO(clemensb): Come up with a better strategy here, involving
    // pre-analysis of the function.
    __ SpillLocals();

    __ SpillLoopArgs(loop->start_merge.arity);

    // Loop labels bind at the beginning of the block.
    __ bind(loop->label.get());

    // Save the current cache state for the merge when jumping to this loop.
    loop->label_state.Split(*__ cache_state());

    PushControl(loop);

    if (!dynamic_tiering()) {
      // When the budget-based tiering mechanism is enabled, use that to
      // check for interrupt requests; otherwise execute a stack check in the
      // loop header.
      StackCheck(decoder, decoder->position());
    }
  }

  void Try(FullDecoder* decoder, Control* block) {
    block->try_info = zone_->New<TryInfo>(zone_);
    PushControl(block);
  }

  // Load the property in {kReturnRegister0}.
  LiftoffRegister GetExceptionProperty(const VarState& exception,
                                       RootIndex root_index) {
    DCHECK(root_index == RootIndex::kwasm_exception_tag_symbol ||
           root_index == RootIndex::kwasm_exception_values_symbol);

    LiftoffRegList pinned;
    LiftoffRegister tag_symbol_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadExceptionSymbol(tag_symbol_reg.gp(), pinned, root_index);
    LiftoffRegister context_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LOAD_TAGGED_PTR_INSTANCE_FIELD(context_reg.gp(), NativeContext, pinned);

    VarState tag_symbol{kRef, tag_symbol_reg, 0};
    VarState context{kRef, context_reg, 0};

    CallBuiltin(Builtin::kWasmGetOwnProperty,
                MakeSig::Returns(kRef).Params(kRef, kRef, kRef),
                {exception, tag_symbol, context}, kNoSourcePosition);

    return LiftoffRegister(kReturnRegister0);
  }

  void CatchException(FullDecoder* decoder, const TagIndexImmediate& imm,
                      Control* block, base::Vector<Value> values) {
    DCHECK(block->is_try_catch());
    __ emit_jump(block->label.get());

    // This is the last use of this label. Re-use the field for the label of the
    // next catch block, and jump there if the tag does not match.
    __ bind(&block->try_info->catch_label);
    block->try_info->catch_label.Unuse();
    block->try_info->catch_label.UnuseNear();

    __ cache_state()->Split(block->try_info->catch_state);

    CODE_COMMENT("load caught exception tag");
    DCHECK_EQ(__ cache_state()->stack_state.back().kind(), kRef);
    LiftoffRegister caught_tag =
        GetExceptionProperty(__ cache_state()->stack_state.back(),
                             RootIndex::kwasm_exception_tag_symbol);
    LiftoffRegList pinned;
    pinned.set(caught_tag);

    CODE_COMMENT("load expected exception tag");
    Register imm_tag = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    LOAD_TAGGED_PTR_INSTANCE_FIELD(imm_tag, TagsTable, pinned);
    __ LoadTaggedPointer(
        imm_tag, imm_tag, no_reg,
        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(imm.index));

    CODE_COMMENT("compare tags");

    if (imm.tag->sig->parameter_count() == 1 &&
        imm.tag->sig->GetParam(0) == kWasmExternRef) {
      // Check for the special case where the tag is WebAssembly.JSTag and the
      // exception is not a WebAssembly.Exception. In this case the exception is
      // caught and pushed on the operand stack.
      // Only perform this check if the tag signature is the same as
      // the JSTag signature, i.e. a single externref, otherwise we know
      // statically that it cannot be the JSTag.
      LiftoffRegister undefined =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      __ LoadFullPointer(
          undefined.gp(), kRootRegister,
          IsolateData::root_slot_offset(RootIndex::kUndefinedValue));
      LiftoffRegister js_tag = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      LOAD_TAGGED_PTR_INSTANCE_FIELD(js_tag.gp(), NativeContext, pinned);
      __ LoadTaggedPointer(
          js_tag.gp(), js_tag.gp(), no_reg,
          NativeContext::SlotOffset(Context::WASM_JS_TAG_INDEX));
      __ LoadTaggedPointer(
          js_tag.gp(), js_tag.gp(), no_reg,
          wasm::ObjectAccess::ToTagged(WasmTagObject::kTagOffset));
      {
        LiftoffAssembler::CacheState initial_state(zone_);
        LiftoffAssembler::CacheState end_state(zone_);
        Label js_exception;
        Label done;
        Label uncaught;
        initial_state.Split(*__ cache_state());
        {
          FREEZE_STATE(state_merged_explicitly);
          // If the tag is undefined, this is not a wasm exception. Go to a
          // different block to process the JS exception. Otherwise compare it
          // with the expected tag.
          __ emit_cond_jump(kEqual, &js_exception, kRefNull, caught_tag.gp(),
                            undefined.gp(), state_merged_explicitly);
          __ emit_cond_jump(kNotEqual, &uncaught, kRefNull, imm_tag,
                            caught_tag.gp(), state_merged_explicitly);
        }
        // Case 1: A wasm exception with a matching tag.
        GetExceptionValues(decoder, __ cache_state()->stack_state.back(),
                           imm.tag);
        // GetExceptionValues modified the cache state. Remember the new state
        // to merge the end state of case 2 into it.
        end_state.Steal(*__ cache_state());
        __ emit_jump(&done);

        __ bind(&js_exception);
        __ cache_state()->Split(initial_state);
        {
          FREEZE_STATE(state_merged_explicitly);
          __ emit_cond_jump(kNotEqual, &uncaught, kRefNull, imm_tag,
                            js_tag.gp(), state_merged_explicitly);
        }
        // Case 2: A JS exception, and the expected tag is JSTag.
        // TODO(thibaudm): Can we avoid some state splitting/stealing by
        // reserving this register earlier and not modifying the state in this
        // block?
        LiftoffRegister exception = __ PeekToRegister(0, pinned);
        __ PushRegister(kRef, exception);
        // The exception is now on the stack twice: once as an implicit operand
        // for rethrow, and once as the "unpacked" value.
        __ MergeFullStackWith(end_state);
        __ emit_jump(&done);

        // Case 3: Either a wasm exception with a mismatching tag, or a JS
        // exception but the expected tag is not JSTag.
        __ bind(&uncaught);
        __ cache_state()->Steal(initial_state);
        __ MergeFullStackWith(block->try_info->catch_state);
        __ emit_jump(&block->try_info->catch_label);

        __ bind(&done);
        __ cache_state()->Steal(end_state);
      }
    } else {
      {
        FREEZE_STATE(frozen);
        Label caught;
        __ emit_cond_jump(kEqual, &caught, kRefNull, imm_tag, caught_tag.gp(),
                          frozen);
        // The tags don't match, merge the current state into the catch state
        // and jump to the next handler.
        __ MergeFullStackWith(block->try_info->catch_state);
        __ emit_jump(&block->try_info->catch_label);
        __ bind(&caught);
      }
      GetExceptionValues(decoder, __ cache_state()->stack_state.back(),
                         imm.tag);
    }
    if (!block->try_info->in_handler) {
      block->try_info->in_handler = true;
      num_exceptions_++;
    }
  }

  void Rethrow(FullDecoder* decoder, const VarState& exception) {
    CallBuiltin(Builtin::kWasmRethrow, MakeSig::Params(kRef), {exception},
                decoder->position());
  }

  void Delegate(FullDecoder* decoder, uint32_t depth, Control* block) {
    DCHECK_EQ(block, decoder->control_at(0));
    Control* target = decoder->control_at(depth);
    DCHECK(block->is_incomplete_try());
    __ bind(&block->try_info->catch_label);
    if (block->try_info->catch_reached) {
      __ cache_state()->Steal(block->try_info->catch_state);
      if (depth == decoder->control_depth() - 1) {
        // Delegate to the caller, do not emit a landing pad.
        Rethrow(decoder, __ cache_state()->stack_state.back());
        MaybeOSR();
      } else {
        DCHECK(target->is_incomplete_try());
        if (target->try_info->catch_reached) {
          __ MergeStackWith(target->try_info->catch_state, 1,
                            LiftoffAssembler::kForwardJump);
        } else {
          target->try_info->catch_state = __ MergeIntoNewState(
              __ num_locals(), 1, target->stack_depth + target->num_exceptions);
          target->try_info->catch_reached = true;
        }
        __ emit_jump(&target->try_info->catch_label);
      }
    }
  }

  void Rethrow(FullDecoder* decoder, Control* try_block) {
    int index = try_block->try_info->catch_state.stack_height() - 1;
    auto& exception = __ cache_state()->stack_state[index];
    Rethrow(decoder, exception);
    int pc_offset = __ pc_offset();
    MaybeOSR();
    EmitLandingPad(decoder, pc_offset);
  }

  void CatchAll(FullDecoder* decoder, Control* block) {
    DCHECK(block->is_try_catchall() || block->is_try_catch());
    DCHECK_EQ(decoder->control_at(0), block);
    __ bind(&block->try_info->catch_label);
    __ cache_state()->Split(block->try_info->catch_state);
    if (!block->try_info->in_handler) {
      block->try_info->in_handler = true;
      num_exceptions_++;
    }
  }

  void TryTable(FullDecoder* decoder, Control* block) {
    block->try_info = zone_->New<TryInfo>(zone_);
    PushControl(block);
  }

  void CatchCase(FullDecoder* decoder, Control* block,
                 const CatchCase& catch_case, base::Vector<Value> values) {
    DCHECK(block->is_try_table());

    // This is the last use of this label. Re-use the field for the label of the
    // next catch block, and jump there if the tag does not match.
    __ bind(&block->try_info->catch_label);
    block->try_info->catch_label.Unuse();
    block->try_info->catch_label.UnuseNear();
    __ cache_state()->Split(block->try_info->catch_state);

    if (catch_case.kind == kCatchAll || catch_case.kind == kCatchAllRef) {
      // The landing pad pushed the exception on the stack, so keep
      // it there for {kCatchAllRef}, and drop it for {kCatchAll}.
      if (catch_case.kind == kCatchAll) {
        __ DropValues(1);
      }
      BrOrRet(decoder, catch_case.br_imm.depth);
      return;
    }

    CODE_COMMENT("load caught exception tag");
    DCHECK_EQ(__ cache_state()->stack_state.back().kind(), kRef);
    LiftoffRegister caught_tag =
        GetExceptionProperty(__ cache_state()->stack_state.back(),
                             RootIndex::kwasm_exception_tag_symbol);
    LiftoffRegList pinned;
    pinned.set(caught_tag);

    CODE_COMMENT("load expected exception tag");
    Register imm_tag = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    LOAD_TAGGED_PTR_INSTANCE_FIELD(imm_tag, TagsTable, pinned);
    __ LoadTaggedPointer(imm_tag, imm_tag, no_reg,
                         wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(
                             catch_case.maybe_tag.tag_imm.index));

    VarState exn = __ cache_state() -> stack_state.back();

    CODE_COMMENT("compare tags");
    if (catch_case.maybe_tag.tag_imm.tag->sig->parameter_count() == 1 &&
        catch_case.maybe_tag.tag_imm.tag->sig->GetParam(0) == kWasmExternRef) {
      // Check for the special case where the tag is WebAssembly.JSTag and the
      // exception is not a WebAssembly.Exception. In this case the exception is
      // caught and pushed on the operand stack.
      // Only perform this check if the tag signature is the same as
      // the JSTag signature, i.e. a single externref, otherwise we know
      // statically that it cannot be the JSTag.
      LiftoffRegister undefined =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      __ LoadFullPointer(
          undefined.gp(), kRootRegister,
          IsolateData::root_slot_offset(RootIndex::kUndefinedValue));
      LiftoffRegister js_tag = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      LOAD_TAGGED_PTR_INSTANCE_FIELD(js_tag.gp(), NativeContext, pinned);
      __ LoadTaggedPointer(
          js_tag.gp(), js_tag.gp(), no_reg,
          NativeContext::SlotOffset(Context::WASM_JS_TAG_INDEX));
      __ LoadTaggedPointer(
          js_tag.gp(), js_tag.gp(), no_reg,
          wasm::ObjectAccess::ToTagged(WasmTagObject::kTagOffset));
      {
        LiftoffAssembler::CacheState initial_state(zone_);
        LiftoffAssembler::CacheState end_state(zone_);
        Label js_exception;
        Label done;
        Label uncaught;
        initial_state.Split(*__ cache_state());
        {
          FREEZE_STATE(state_merged_explicitly);
          // If the tag is undefined, this is not a wasm exception. Go to a
          // different block to process the JS exception. Otherwise compare it
          // with the expected tag.
          __ emit_cond_jump(kEqual, &js_exception, kRefNull, caught_tag.gp(),
                            undefined.gp(), state_merged_explicitly);
          __ emit_cond_jump(kNotEqual, &uncaught, kRefNull, imm_tag,
                            caught_tag.gp(), state_merged_explicitly);
        }
        // Case 1: A wasm exception with a matching tag.
        CODE_COMMENT("unpack exception");
        GetExceptionValues(decoder, __ cache_state()->stack_state.back(),
                           catch_case.maybe_tag.tag_imm.tag);
        // GetExceptionValues modified the cache state. Remember the new state
        // to merge the end state of case 2 into it.
        end_state.Steal(*__ cache_state());
        __ emit_jump(&done);

        __ bind(&js_exception);
        __ cache_state() -> Split(initial_state);
        {
          FREEZE_STATE(state_merged_explicitly);
          __ emit_cond_jump(kNotEqual, &uncaught, kRefNull, imm_tag,
                            js_tag.gp(), state_merged_explicitly);
        }
        // Case 2: A JS exception, and the expected tag is JSTag.
        // TODO(thibaudm): Can we avoid some state splitting/stealing by
        // reserving this register earlier and not modifying the state in this
        // block?
        CODE_COMMENT("JS exception caught by JSTag");
        LiftoffRegister exception = __ PeekToRegister(0, pinned);
        __ PushRegister(kRefNull, exception);
        // The exception is now on the stack twice: once as an implicit operand
        // for rethrow, and once as the "unpacked" value.
        __ MergeFullStackWith(end_state);
        __ emit_jump(&done);

        // Case 3: Either a wasm exception with a mismatching tag, or a JS
        // exception but the expected tag is not JSTag.
        __ bind(&uncaught);
        __ cache_state() -> Steal(initial_state);
        __ MergeFullStackWith(block->try_info->catch_state);
        __ emit_jump(&block->try_info->catch_label);

        __ bind(&done);
        __ cache_state() -> Steal(end_state);
      }
    } else {
      {
        FREEZE_STATE(frozen);
        Label caught;
        __ emit_cond_jump(kEqual, &caught, kRefNull, imm_tag, caught_tag.gp(),
                          frozen);
        // The tags don't match, merge the current state into the catch state
        // and jump to the next handler.
        __ MergeFullStackWith(block->try_info->catch_state);
        __ emit_jump(&block->try_info->catch_label);
        __ bind(&caught);
      }

      CODE_COMMENT("unpack exception");
      pinned = {};
      GetExceptionValues(decoder, __ cache_state()->stack_state.back(),
                         catch_case.maybe_tag.tag_imm.tag);
    }

    if (catch_case.kind == kCatchRef) {
      // Append the exception on the operand stack.
      DCHECK(exn.is_stack());
      auto rc = reg_class_for(kRefNull);
      LiftoffRegister reg = __ GetUnusedRegister(rc, pinned);
      __ Fill(reg, exn.offset(), kRefNull);
      __ PushRegister(kRefNull, reg);
    }
    // There is an extra copy of the exception at this point, below the unpacked
    // values (if any). It will be dropped in the branch below.
    BrOrRet(decoder, catch_case.br_imm.depth);
    bool is_last = &catch_case == &block->catch_cases.last();
    if (is_last && !decoder->HasCatchAll(block)) {
      __ bind(&block->try_info->catch_label);
      __ cache_state()->Steal(block->try_info->catch_state);
      ThrowRef(decoder, nullptr);
    }
  }

  void ThrowRef(FullDecoder* decoder, Value*) {
    // Like Rethrow, but pops the exception from the stack.
    VarState exn = __ PopVarState();
    CallBuiltin(Builtin::kWasmRethrow, MakeSig::Params(kRef), {exn},
                decoder->position());
    int pc_offset = __ pc_offset();
    MaybeOSR();
    EmitLandingPad(decoder, pc_offset);
  }

  // Before emitting the conditional branch, {will_freeze} will be initialized
  // to prevent cache state changes in conditionally executed code.
  void JumpIfFalse(FullDecoder* decoder, Label* false_dst,
                   std::optional<FreezeCacheState>& will_freeze) {
    DCHECK(!will_freeze.has_value());
    Condition cond =
        test_and_reset_outstanding_op(kExprI32Eqz) ? kNotZero : kZero;

    if (!has_outstanding_op()) {
      // Unary comparison.
      Register value = __ PopToRegister().gp();
      will_freeze.emplace(asm_);
      __ emit_cond_jump(cond, false_dst, kI32, value, no_reg, *will_freeze);
      return;
    }

    // Binary comparison of i32 values.
    cond = Negate(GetCompareCondition(outstanding_op_));
    outstanding_op_ = kNoOutstandingOp;
    VarState rhs_slot = __ cache_state()->stack_state.back();
    if (rhs_slot.is_const()) {
      // Compare to a constant.
      int32_t rhs_imm = rhs_slot.i32_const();
      __ cache_state()->stack_state.pop_back();
      Register lhs = __ PopToRegister().gp();
      will_freeze.emplace(asm_);
      __ emit_i32_cond_jumpi(cond, false_dst, lhs, rhs_imm, *will_freeze);
      return;
    }

    Register rhs = __ PopToRegister().gp();
    VarState lhs_slot = __ cache_state()->stack_state.back();
    if (lhs_slot.is_const()) {
      // Compare a constant to an arbitrary value.
      int32_t lhs_imm = lhs_slot.i32_const();
      __ cache_state()->stack_state.pop_back();
      // Flip the condition, because {lhs} and {rhs} are swapped.
      will_freeze.emplace(asm_);
      __ emit_i32_cond_jumpi(Flip(cond), false_dst, rhs, lhs_imm, *will_freeze);
      return;
    }

    // Compare two arbitrary values.
    Register lhs = __ PopToRegister(LiftoffRegList{rhs}).gp();
    will_freeze.emplace(asm_);
    __ emit_cond_jump(cond, false_dst, kI32, lhs, rhs, *will_freeze);
  }

  void If(FullDecoder* decoder, const Value& cond, Control* if_block) {
    DCHECK_EQ(if_block, decoder->control_at(0));
    DCHECK(if_block->is_if());

    // Allocate the else state.
    if_block->else_state = zone_->New<ElseState>(zone_);

    // Test the condition on the value stack, jump to else if zero.
    std::optional<FreezeCacheState> frozen;
    JumpIfFalse(decoder, if_block->else_state->label.get(), frozen);
    frozen.reset();

    // Store the state (after popping the value) for executing the else branch.
    if_block->else_state->state.Split(*__ cache_state());

    PushControl(if_block);
  }

  void FallThruTo(FullDecoder* decoder, Control* c) {
    DCHECK_IMPLIES(c->is_try_catchall(), !c->end_merge.reached);
    if (c->end_merge.reached) {
      __ MergeStackWith(c->label_state, c->br_merge()->arity,
                        LiftoffAssembler::kForwardJump);
    } else {
      c->label_state = __ MergeIntoNewState(__ num_locals(), c->end_merge.arity,
                                            c->stack_depth + c->num_exceptions);
    }
    __ emit_jump(c->label.get());
    TraceCacheState(decoder);
  }

  void FinishOneArmedIf(FullDecoder* decoder, Control* c) {
    DCHECK(c->is_onearmed_if());
    if (c->end_merge.reached) {
      // Someone already merged to the end of the if. Merge both arms into that.
      if (c->reachable()) {
        // Merge the if state into the end state.
        __ MergeFullStackWith(c->label_state);
        __ emit_jump(c->label.get());
      }
      // Merge the else state into the end state. Set this state as the current
      // state first so helper functions know which registers are in use.
      __ bind(c->else_state->label.get());
      __ cache_state()->Steal(c->else_state->state);
      __ MergeFullStackWith(c->label_state);
      __ cache_state()->Steal(c->label_state);
    } else if (c->reachable()) {
      // No merge yet at the end of the if, but we need to create a merge for
      // the both arms of this if. Thus init the merge point from the current
      // state, then merge the else state into that.
      DCHECK_EQ(c->start_merge.arity, c->end_merge.arity);
      c->label_state =
          __ MergeIntoNewState(__ num_locals(), c->start_merge.arity,
                               c->stack_depth + c->num_exceptions);
      __ emit_jump(c->label.get());
      // Merge the else state into the end state. Set this state as the current
      // state first so helper functions know which registers are in use.
      __ bind(c->else_state->label.get());
      __ cache_state()->Steal(c->else_state->state);
      __ MergeFullStackWith(c->label_state);
      __ cache_state()->Steal(c->label_state);
    } else {
      // No merge needed, just continue with the else state.
      __ bind(c->else_state->label.get());
      __ cache_state()->Steal(c->else_state->state);
    }
  }

  void FinishTry(FullDecoder* decoder, Control* c) {
    DCHECK(c->is_try_catch() || c->is_try_catchall() || c->is_try_table());
    if (!c->end_merge.reached) {
      if (c->try_info->catch_reached && !c->is_try_table()) {
        // Drop the implicit exception ref.
        __ DropExceptionValueAtOffset(__ num_locals() + c->stack_depth +
                                      c->num_exceptions);
      }
      // Else we did not enter the catch state, continue with the current state.
    } else {
      if (c->reachable()) {
        __ MergeStackWith(c->label_state, c->br_merge()->arity,
                          LiftoffAssembler::kForwardJump);
      }
      __ cache_state()->Steal(c->label_state);
    }
    if (c->try_info->catch_reached && !c->is_try_table()) {
      num_exceptions_--;
    }
  }

  void PopControl(FullDecoder* decoder, Control* c) {
    if (c->is_loop()) return;  // A loop just falls through.
    if (c->is_onearmed_if()) {
      // Special handling for one-armed ifs.
      FinishOneArmedIf(decoder, c);
    } else if (c->is_try_catch() || c->is_try_catchall() || c->is_try_table()) {
      FinishTry(decoder, c);
    } else if (c->end_merge.reached) {
      // There is a merge already. Merge our state into that, then continue with
      // that state.
      if (c->reachable()) {
        __ MergeFullStackWith(c->label_state);
      }
      __ cache_state()->Steal(c->label_state);
    } else {
      // No merge, just continue with our current state.
    }

    if (!c->label.get()->is_bound()) __ bind(c->label.get());
  }

  // Call a C function (with default C calling conventions). Returns the
  // register holding the result if any.
  LiftoffRegister GenerateCCall(ValueKind return_kind,
                                const std::initializer_list<VarState> args,
                                ExternalReference ext_ref) {
    SCOPED_CODE_COMMENT(
        std::string{"Call extref: "} +
        ExternalReferenceTable::NameOfIsolateIndependentAddress(
            ext_ref.address(), IsolateGroup::current()->external_ref_table()));
    __ SpillAllRegisters();
    __ CallC(args, ext_ref);
    if (needs_gp_reg_pair(return_kind)) {
      return LiftoffRegister::ForPair(kReturnRegister0, kReturnRegister1);
    }
    return LiftoffRegister{kReturnRegister0};
  }

  void GenerateCCallWithStackBuffer(const LiftoffRegister* result_regs,
                                    ValueKind return_kind,
                                    ValueKind out_argument_kind,
                                    const std::initializer_list<VarState> args,
                                    ExternalReference ext_ref) {
    SCOPED_CODE_COMMENT(
        std::string{"Call extref: "} +
        ExternalReferenceTable::NameOfIsolateIndependentAddress(
            ext_ref.address(), IsolateGroup::current()->external_ref_table()));

    // Before making a call, spill all cache registers.
    __ SpillAllRegisters();

    // Store arguments on our stack, then align the stack for calling to C.
    int param_bytes = 0;
    for (const VarState& arg : args) {
      param_bytes += value_kind_size(arg.kind());
    }
    int out_arg_bytes =
        out_argument_kind == kVoid ? 0 : value_kind_size(out_argument_kind);
    int stack_bytes = std::max(param_bytes, out_arg_bytes);
    __ CallCWithStackBuffer(args, result_regs, return_kind, out_argument_kind,
                            stack_bytes, ext_ref);
  }

  template <typename EmitFn, typename... Args>
  typename std::enable_if<!std::is_member_function_pointer<EmitFn>::value>::type
  CallEmitFn(EmitFn fn, Args... args) {
    fn(args...);
  }

  template <typename EmitFn, typename... Args>
  typename std::enable_if<std::is_member_function_pointer<EmitFn>::value>::type
  CallEmitFn(EmitFn fn, Args... args) {
    (asm_.*fn)(ConvertAssemblerArg(args)...);
  }

  // Wrap a {LiftoffRegister} with implicit conversions to {Register} and
  // {DoubleRegister}.
  struct AssemblerRegisterConverter {
    LiftoffRegister reg;
    operator LiftoffRegister() { return reg; }
    operator Register() { return reg.gp(); }
    operator DoubleRegister() { return reg.fp(); }
  };

  // Convert {LiftoffRegister} to {AssemblerRegisterConverter}, other types stay
  // unchanged.
  template <typename T>
  typename std::conditional<std::is_same<LiftoffRegister, T>::value,
                            AssemblerRegisterConverter, T>::type
  ConvertAssemblerArg(T t) {
    return {t};
  }

  template <typename EmitFn, typename ArgType>
  struct EmitFnWithFirstArg {
    EmitFn fn;
    ArgType first_arg;
  };

  template <typename EmitFn, typename ArgType>
  EmitFnWithFirstArg<EmitFn, ArgType> BindFirst(EmitFn fn, ArgType arg) {
    return {fn, arg};
  }

  template <typename EmitFn, typename T, typename... Args>
  void CallEmitFn(EmitFnWithFirstArg<EmitFn, T> bound_fn, Args... args) {
    CallEmitFn(bound_fn.fn, bound_fn.first_arg, ConvertAssemblerArg(args)...);
  }

  template <ValueKind src_kind, ValueKind result_kind,
            ValueKind result_lane_kind = kVoid, class EmitFn>
  void EmitUnOp(EmitFn fn) {
    constexpr RegClass src_rc = reg_class_for(src_kind);
    constexpr RegClass result_rc = reg_class_for(result_kind);
    LiftoffRegister src = __ PopToRegister();
    LiftoffRegister dst = src_rc == result_rc
                              ? __ GetUnusedRegister(result_rc, {src}, {})
                              : __ GetUnusedRegister(result_rc, {});
    CallEmitFn(fn, dst, src);
    if (V8_UNLIKELY(nondeterminism_)) {
      LiftoffRegList pinned{dst};
      if (result_kind == ValueKind::kF32 || result_kind == ValueKind::kF64) {
        CheckNan(dst, pinned, result_kind);
      } else if (result_kind == ValueKind::kS128 &&
                 (result_lane_kind == kF32 || result_lane_kind == kF64)) {
        // TODO(irezvov): Add NaN detection for F16.
        CheckS128Nan(dst, pinned, result_lane_kind);
      }
    }
    __ PushRegister(result_kind, dst);
  }

  template <ValueKind kind>
  void EmitFloatUnOpWithCFallback(
      bool (LiftoffAssembler::*emit_fn)(DoubleRegister, DoubleRegister),
      ExternalReference (*fallback_fn)()) {
    auto emit_with_c_fallback = [this, emit_fn, fallback_fn](
                                    LiftoffRegister dst, LiftoffRegister src) {
      if ((asm_.*emit_fn)(dst.fp(), src.fp())) return;
      ExternalReference ext_ref = fallback_fn();
      GenerateCCallWithStackBuffer(&dst, kVoid, kind, {VarState{kind, src, 0}},
                                   ext_ref);
    };
    EmitUnOp<kind, kind>(emit_with_c_fallback);
  }

  enum TypeConversionTrapping : bool { kCanTrap = true, kNoTrap = false };
  template <ValueKind dst_kind, ValueKind src_kind,
            TypeConversionTrapping can_trap>
  void EmitTypeConversion(FullDecoder* decoder, WasmOpcode opcode,
                          ExternalReference (*fallback_fn)()) {
    static constexpr RegClass src_rc = reg_class_for(src_kind);
    static constexpr RegClass dst_rc = reg_class_for(dst_kind);
    LiftoffRegister src = __ PopToRegister();
    LiftoffRegister dst = src_rc == dst_rc
                              ? __ GetUnusedRegister(dst_rc, {src}, {})
                              : __ GetUnusedRegister(dst_rc, {});
    Label* trap =
        can_trap ? AddOutOfLineTrap(decoder,
                                    Builtin::kThrowWasmTrapFloatUnrepresentable)
                 : nullptr;
    if (!__ emit_type_conversion(opcode, dst, src, trap)) {
      DCHECK_NOT_NULL(fallback_fn);
      ExternalReference ext_ref = fallback_fn();
      if (can_trap) {
        // External references for potentially trapping conversions return int.
        LiftoffRegister ret_reg =
            __ GetUnusedRegister(kGpReg, LiftoffRegList{dst});
        LiftoffRegister dst_regs[] = {ret_reg, dst};
        GenerateCCallWithStackBuffer(dst_regs, kI32, dst_kind,
                                     {VarState{src_kind, src, 0}}, ext_ref);
        // It's okay that this is short-lived: we're trapping anyway.
        FREEZE_STATE(trapping);
        __ emit_cond_jump(kEqual, trap, kI32, ret_reg.gp(), no_reg, trapping);
      } else {
        GenerateCCallWithStackBuffer(&dst, kVoid, dst_kind,
                                     {VarState{src_kind, src, 0}}, ext_ref);
      }
    }
    __ PushRegister(dst_kind, dst);
  }

  void EmitIsNull(WasmOpcode opcode, ValueType type) {
    LiftoffRegList pinned;
    LiftoffRegister ref = pinned.set(__ PopToRegister());
    LiftoffRegister null = __ GetUnusedRegister(kGpReg, pinned);
    LoadNullValueForCompare(null.gp(), pinned, type);
    // Prefer to overwrite one of the input registers with the result
    // of the comparison.
    LiftoffRegister dst = __ GetUnusedRegister(kGpReg, {ref, null}, {});
#if defined(V8_COMPRESS_POINTERS)
    // As the value in the {null} register is only the tagged pointer part,
    // we may only compare 32 bits, not the full pointer size.
    __ emit_i32_set_cond(opcode == kExprRefIsNull ? kEqual : kNotEqual,
                         dst.gp(), ref.gp(), null.gp());
#else
    __ emit_ptrsize_set_cond(opcode == kExprRefIsNull ? kEqual : kNotEqual,
                             dst.gp(), ref, null);
#endif
    __ PushRegister(kI32, dst);
  }

  void UnOp(FullDecoder* decoder, WasmOpcode opcode, const Value& value,
            Value* result) {
#define CASE_I32_UNOP(opcode, fn) \
  case kExpr##opcode:             \
    return EmitUnOp<kI32, kI32>(&LiftoffAssembler::emit_##fn);
#define CASE_I64_UNOP(opcode, fn) \
  case kExpr##opcode:             \
    return EmitUnOp<kI64, kI64>(&LiftoffAssembler::emit_##fn);
#define CASE_FLOAT_UNOP(opcode, kind, fn) \
  case kExpr##opcode:                     \
    return EmitUnOp<k##kind, k##kind>(&LiftoffAssembler::emit_##fn);
#define CASE_FLOAT_UNOP_WITH_CFALLBACK(opcode, kind, fn)                     \
  case kExpr##opcode:                                                        \
    return EmitFloatUnOpWithCFallback<k##kind>(&LiftoffAssembler::emit_##fn, \
                                               &ExternalReference::wasm_##fn);
#define CASE_TYPE_CONVERSION(opcode, dst_kind, src_kind, ext_ref, can_trap) \
  case kExpr##opcode:                                                       \
    return EmitTypeConversion<k##dst_kind, k##src_kind, can_trap>(          \
        decoder, kExpr##opcode, ext_ref);
    switch (opcode) {
      CASE_I32_UNOP(I32Clz, i32_clz)
      CASE_I32_UNOP(I32Ctz, i32_ctz)
      CASE_FLOAT_UNOP(F32Abs, F32, f32_abs)
      CASE_FLOAT_UNOP(F32Neg, F32, f32_neg)
      CASE_FLOAT_UNOP_WITH_CFALLBACK(F32Ceil, F32, f32_ceil)
      CASE_FLOAT_UNOP_WITH_CFALLBACK(F32Floor, F32, f32_floor)
      CASE_FLOAT_UNOP_WITH_CFALLBACK(F32Trunc, F32, f32_trunc)
      CASE_FLOAT_UNOP_WITH_CFALLBACK(F32NearestInt, F32, f32_nearest_int)
      CASE_FLOAT_UNOP(F32Sqrt, F32, f32_sqrt)
      CASE_FLOAT_UNOP(F64Abs, F64, f64_abs)
      CASE_FLOAT_UNOP(F64Neg, F64, f64_neg)
      CASE_FLOAT_UNOP_WITH_CFALLBACK(F64Ceil, F64, f64_ceil)
      CASE_FLOAT_UNOP_WITH_CFALLBACK(F64Floor, F64, f64_floor)
      CASE_FLOAT_UNOP_WITH_CFALLBACK(F64Trunc, F64, f64_trunc)
      CASE_FLOAT_UNOP_WITH_CFALLBACK(F64NearestInt, F64, f64_nearest_int)
      CASE_FLOAT_UNOP(F64Sqrt, F64, f64_sqrt)
      CASE_TYPE_CONVERSION(I32ConvertI64, I32, I64, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(I32SConvertF32, I32, F32, nullptr, kCanTrap)
      CASE_TYPE_CONVERSION(I32UConvertF32, I32, F32, nullptr, kCanTrap)
      CASE_TYPE_CONVERSION(I32SConvertF64, I32, F64, nullptr, kCanTrap)
      CASE_TYPE_CONVERSION(I32UConvertF64, I32, F64, nullptr, kCanTrap)
      CASE_TYPE_CONVERSION(I32ReinterpretF32, I32, F32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(I64SConvertI32, I64, I32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(I64UConvertI32, I64, I32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(I64SConvertF32, I64, F32,
                           &ExternalReference::wasm_float32_to_int64, kCanTrap)
      CASE_TYPE_CONVERSION(I64UConvertF32, I64, F32,
                           &ExternalReference::wasm_float32_to_uint64, kCanTrap)
      CASE_TYPE_CONVERSION(I64SConvertF64, I64, F64,
                           &ExternalReference::wasm_float64_to_int64, kCanTrap)
      CASE_TYPE_CONVERSION(I64UConvertF64, I64, F64,
                           &ExternalReference::wasm_float64_to_uint64, kCanTrap)
      CASE_TYPE_CONVERSION(I64ReinterpretF64, I64, F64, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(F32SConvertI32, F32, I32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(F32UConvertI32, F32, I32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(F32SConvertI64, F32, I64,
                           &ExternalReference::wasm_int64_to_float32, kNoTrap)
      CASE_TYPE_CONVERSION(F32UConvertI64, F32, I64,
                           &ExternalReference::wasm_uint64_to_float32, kNoTrap)
      CASE_TYPE_CONVERSION(F32ConvertF64, F32, F64, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(F32ReinterpretI32, F32, I32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(F64SConvertI32, F64, I32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(F64UConvertI32, F64, I32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(F64SConvertI64, F64, I64,
                           &ExternalReference::wasm_int64_to_float64, kNoTrap)
      CASE_TYPE_CONVERSION(F64UConvertI64, F64, I64,
                           &ExternalReference::wasm_uint64_to_float64, kNoTrap)
      CASE_TYPE_CONVERSION(F64ConvertF32, F64, F32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(F64ReinterpretI64, F64, I64, nullptr, kNoTrap)
      CASE_I32_UNOP(I32SExtendI8, i32_signextend_i8)
      CASE_I32_UNOP(I32SExtendI16, i32_signextend_i16)
      CASE_I64_UNOP(I64SExtendI8, i64_signextend_i8)
      CASE_I64_UNOP(I64SExtendI16, i64_signextend_i16)
      CASE_I64_UNOP(I64SExtendI32, i64_signextend_i32)
      CASE_I64_UNOP(I64Clz, i64_clz)
      CASE_I64_UNOP(I64Ctz, i64_ctz)
      CASE_TYPE_CONVERSION(I32SConvertSatF32, I32, F32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(I32UConvertSatF32, I32, F32, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(I32SConvertSatF64, I32, F64, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(I32UConvertSatF64, I32, F64, nullptr, kNoTrap)
      CASE_TYPE_CONVERSION(I64SConvertSatF32, I64, F32,
                           &ExternalReference::wasm_float32_to_int64_sat,
                           kNoTrap)
      CASE_TYPE_CONVERSION(I64UConvertSatF32, I64, F32,
                           &ExternalReference::wasm_float32_to_uint64_sat,
                           kNoTrap)
      CASE_TYPE_CONVERSION(I64SConvertSatF64, I64, F64,
                           &ExternalReference::wasm_float64_to_int64_sat,
                           kNoTrap)
      CASE_TYPE_CONVERSION(I64UConvertSatF64, I64, F64,
                           &ExternalReference::wasm_float64_to_uint64_sat,
                           kNoTrap)
      case kExprI32Eqz:
        DCHECK(decoder->lookahead(0, kExprI32Eqz));
        if ((decoder->lookahead(1, kExprBrIf) ||
             decoder->lookahead(1, kExprIf)) &&
            !for_debugging_) {
          DCHECK(!has_outstanding_op());
          outstanding_op_ = kExprI32Eqz;
          break;
        }
        return EmitUnOp<kI32, kI32>(&LiftoffAssembler::emit_i32_eqz);
      case kExprI64Eqz:
        return EmitUnOp<kI64, kI32>(&LiftoffAssembler::emit_i64_eqz);
      case kExprI32Popcnt:
        return EmitUnOp<kI32, kI32>(
            [this](LiftoffRegister dst, LiftoffRegister src) {
              if (__ emit_i32_popcnt(dst.gp(), src.gp())) return;
              LiftoffRegister result =
                  GenerateCCall(kI32, {VarState{kI32, src, 0}},
                                ExternalReference::wasm_word32_popcnt());
              if (result != dst) __ Move(dst.gp(), result.gp(), kI32);
            });
      case kExprI64Popcnt:
        return EmitUnOp<kI64, kI64>(
            [this](LiftoffRegister dst, LiftoffRegister src) {
              if (__ emit_i64_popcnt(dst, src)) return;
              // The c function returns i32. We will zero-extend later.
              LiftoffRegister result =
                  GenerateCCall(kI32, {VarState{kI64, src, 0}},
                                ExternalReference::wasm_word64_popcnt());
              // Now zero-extend the result to i64.
              __ emit_type_conversion(kExprI64UConvertI32, dst, result,
                                      nullptr);
            });
      case kExprRefIsNull:
      // We abuse ref.as_non_null, which isn't otherwise used in this switch, as
      // a sentinel for the negation of ref.is_null.
      case kExprRefAsNonNull:
        return EmitIsNull(opcode, value.type);
      case kExprAnyConvertExtern: {
        VarState input_state = __ cache_state()->stack_state.back();
        CallBuiltin(Builtin::kWasmAnyConvertExtern,
                    MakeSig::Returns(kRefNull).Params(kRefNull), {input_state},
                    decoder->position());
        __ DropValues(1);
        __ PushRegister(kRef, LiftoffRegister(kReturnRegister0));
        return;
      }
      case kExprExternConvertAny: {
        LiftoffRegList pinned;
        LiftoffRegister ref = pinned.set(__ PopToModifiableRegister(pinned));
        LiftoffRegister null = __ GetUnusedRegister(kGpReg, pinned);
        LoadNullValueForCompare(null.gp(), pinned, kWasmAnyRef);
        Label label;
        {
          FREEZE_STATE(frozen);
          __ emit_cond_jump(kNotEqual, &label, kRefNull, ref.gp(), null.gp(),
                            frozen);
          LoadNullValue(ref.gp(), kWasmExternRef);
          __ bind(&label);
        }
        __ PushRegister(kRefNull, ref);
        return;
      }
      default:
        UNREACHABLE();
    }
#undef CASE_I32_UNOP
#undef CASE_I64_UNOP
#undef CASE_FLOAT_UNOP
#undef CASE_FLOAT_UNOP_WITH_CFALLBACK
#undef CASE_TYPE_CONVERSION
  }

  template <ValueKind src_kind, ValueKind result_kind, typename EmitFn,
            typename EmitFnImm>
  void EmitBinOpImm(EmitFn fn, EmitFnImm fnImm) {
    static constexpr RegClass src_rc = reg_class_for(src_kind);
    static constexpr RegClass result_rc = reg_class_for(result_kind);

    VarState rhs_slot = __ cache_state()->stack_state.back();
    // Check if the RHS is an immediate.
    if (rhs_slot.is_const()) {
      __ cache_state()->stack_state.pop_back();
      int32_t imm = rhs_slot.i32_const();

      LiftoffRegister lhs = __ PopToRegister();
      // Either reuse {lhs} for {dst}, or choose a register (pair) which does
      // not overlap, for easier code generation.
      LiftoffRegList pinned{lhs};
      LiftoffRegister dst = src_rc == result_rc
                                ? __ GetUnusedRegister(result_rc, {lhs}, pinned)
                                : __ GetUnusedRegister(result_rc, pinned);

      CallEmitFn(fnImm, dst, lhs, imm);
      static_assert(result_kind != kF32 && result_kind != kF64,
                    "Unhandled nondeterminism for fuzzing.");
      __ PushRegister(result_kind, dst);
    } else {
      // The RHS was not an immediate.
      EmitBinOp<src_kind, result_kind>(fn);
    }
  }

  template <ValueKind src_kind, ValueKind result_kind,
            bool swap_lhs_rhs = false, ValueKind result_lane_kind = kVoid,
            typename EmitFn>
  void EmitBinOp(EmitFn fn) {
    static constexpr RegClass src_rc = reg_class_for(src_kind);
    static constexpr RegClass result_rc = reg_class_for(result_kind);
    LiftoffRegister rhs = __ PopToRegister();
    LiftoffRegister lhs = __ PopToRegister(LiftoffRegList{rhs});
    LiftoffRegister dst = src_rc == result_rc
                              ? __ GetUnusedRegister(result_rc, {lhs, rhs}, {})
                              : __ GetUnusedRegister(result_rc, {});

    if (swap_lhs_rhs) std::swap(lhs, rhs);

    CallEmitFn(fn, dst, lhs, rhs);
    if (V8_UNLIKELY(nondeterminism_)) {
      LiftoffRegList pinned{dst};
      if (result_kind == ValueKind::kF32 || result_kind == ValueKind::kF64) {
        CheckNan(dst, pinned, result_kind);
      } else if (result_kind == ValueKind::kS128 &&
                 (result_lane_kind == kF32 || result_lane_kind == kF64)) {
        CheckS128Nan(dst, pinned, result_lane_kind);
      }
    }
    __ PushRegister(result_kind, dst);
  }

  void EmitDivOrRem64CCall(LiftoffRegister dst, LiftoffRegister lhs,
                           LiftoffRegister rhs, ExternalReference ext_ref,
                           Label* trap_by_zero,
                           Label* trap_unrepresentable = nullptr) {
    // Cannot emit native instructions, build C call.
    LiftoffRegister ret = __ GetUnusedRegister(kGpReg, LiftoffRegList{dst});
    LiftoffRegister result_regs[] = {ret, dst};
    GenerateCCallWithStackBuffer(result_regs, kI32, kI64,
                                 {{kI64, lhs, 0}, {kI64, rhs, 0}}, ext_ref);
    FREEZE_STATE(trapping);
    __ emit_i32_cond_jumpi(kEqual, trap_by_zero, ret.gp(), 0, trapping);
    if (trap_unrepresentable) {
      __ emit_i32_cond_jumpi(kEqual, trap_unrepresentable, ret.gp(), -1,
                             trapping);
    }
  }

  template <WasmOpcode opcode>
  void EmitI32CmpOp(FullDecoder* decoder) {
    DCHECK(decoder->lookahead(0, opcode));
    if ((decoder->lookahead(1, kExprBrIf) || decoder->lookahead(1, kExprIf)) &&
        !for_debugging_) {
      DCHECK(!has_outstanding_op());
      outstanding_op_ = opcode;
      return;
    }
    return EmitBinOp<kI32, kI32>(BindFirst(&LiftoffAssembler::emit_i32_set_cond,
                                           GetCompareCondition(opcode)));
  }

  template <ValueKind kind, ExternalReference(ExtRefFn)()>
  void EmitBitRotationCCall() {
    EmitBinOp<kind, kind>([this](LiftoffRegister dst, LiftoffRegister input,
                                 LiftoffRegister shift) {
      // The shift is always passed as 32-bit value.
      if (needs_gp_reg_pair(kind)) shift = shift.low();
      LiftoffRegister result =
          GenerateCCall(kind, {{kind, input, 0}, {kI32, shift, 0}}, ExtRefFn());
      if (dst != result) __ Move(dst, result, kind);
    });
  }

  template <typename EmitFn, typename EmitFnImm>
  void EmitI64Shift(EmitFn fn, EmitFnImm fnImm) {
    return EmitBinOpImm<kI64, kI64>(
        [this, fn](LiftoffRegister dst, LiftoffRegister src,
                   LiftoffRegister amount) {
          CallEmitFn(fn, dst, src,
                     amount.is_gp_pair() ? amount.low_gp() : amount.gp());
        },
        fnImm);
  }

  void BinOp(FullDecoder* decoder, WasmOpcode opcode, const Value& lhs,
             const Value& rhs, Value* result) {
    switch (opcode) {
      case kExprI32Add:
        return EmitBinOpImm<kI32, kI32>(&LiftoffAssembler::emit_i32_add,
                                        &LiftoffAssembler::emit_i32_addi);
      case kExprI32Sub:
        return EmitBinOp<kI32, kI32>(&LiftoffAssembler::emit_i32_sub);
      case kExprI32Mul:
        return EmitBinOp<kI32, kI32>(&LiftoffAssembler::emit_i32_mul);
      case kExprI32And:
        return EmitBinOpImm<kI32, kI32>(&LiftoffAssembler::emit_i32_and,
                                        &LiftoffAssembler::emit_i32_andi);
      case kExprI32Ior:
        return EmitBinOpImm<kI32, kI32>(&LiftoffAssembler::emit_i32_or,
                                        &LiftoffAssembler::emit_i32_ori);
      case kExprI32Xor:
        return EmitBinOpImm<kI32, kI32>(&LiftoffAssembler::emit_i32_xor,
                                        &LiftoffAssembler::emit_i32_xori);
      case kExprI32Eq:
        return EmitI32CmpOp<kExprI32Eq>(decoder);
      case kExprI32Ne:
        return EmitI32CmpOp<kExprI32Ne>(decoder);
      case kExprI32LtS:
        return EmitI32CmpOp<kExprI32LtS>(decoder);
      case kExprI32LtU:
        return EmitI32CmpOp<kExprI32LtU>(decoder);
      case kExprI32GtS:
        return EmitI32CmpOp<kExprI32GtS>(decoder);
      case kExprI32GtU:
        return EmitI32CmpOp<kExprI32GtU>(decoder);
      case kExprI32LeS:
        return EmitI32CmpOp<kExprI32LeS>(decoder);
      case kExprI32LeU:
        return EmitI32CmpOp<kExprI32LeU>(decoder);
      case kExprI32GeS:
        return EmitI32CmpOp<kExprI32GeS>(decoder);
      case kExprI32GeU:
        return EmitI32CmpOp<kExprI32GeU>(decoder);
      case kExprI64Add:
        return EmitBinOpImm<kI64, kI64>(&LiftoffAssembler::emit_i64_add,
                                        &LiftoffAssembler::emit_i64_addi);
      case kExprI64Sub:
        return EmitBinOp<kI64, kI64>(&LiftoffAssembler::emit_i64_sub);
      case kExprI64Mul:
        return EmitBinOp<kI64, kI64>(&LiftoffAssembler::emit_i64_mul);
      case kExprI64And:
        return EmitBinOpImm<kI64, kI64>(&LiftoffAssembler::emit_i64_and,
                                        &LiftoffAssembler::emit_i64_andi);
      case kExprI64Ior:
        return EmitBinOpImm<kI64, kI64>(&LiftoffAssembler::emit_i64_or,
                                        &LiftoffAssembler::emit_i64_ori);
      case kExprI64Xor:
        return EmitBinOpImm<kI64, kI64>(&LiftoffAssembler::emit_i64_xor,
                                        &LiftoffAssembler::emit_i64_xori);
      case kExprI64Eq:
        return EmitBinOp<kI64, kI32>(
            BindFirst(&LiftoffAssembler::emit_i64_set_cond, kEqual));
      case kExprI64Ne:
        return EmitBinOp<kI64, kI32>(
            BindFirst(&LiftoffAssembler::emit_i64_set_cond, kNotEqual));
      case kExprI64LtS:
        return EmitBinOp<kI64, kI32>(
            BindFirst(&LiftoffAssembler::emit_i64_set_cond, kLessThan));
      case kExprI64LtU:
        return EmitBinOp<kI64, kI32>(
            BindFirst(&LiftoffAssembler::emit_i64_set_cond, kUnsignedLessThan));
      case kExprI64GtS:
        return EmitBinOp<kI64, kI32>(
            BindFirst(&LiftoffAssembler::emit_i64_set_cond, kGreaterThan));
      case kExprI64GtU:
        return EmitBinOp<kI64, kI32>(BindFirst(
            &LiftoffAssembler::emit_i64_set_cond, kUnsignedGreaterThan));
      case kExprI64LeS:
        return EmitBinOp<kI64, kI32>(
            BindFirst(&LiftoffAssembler::emit_i64_set_cond, kLessThanEqual));
      case kExprI64LeU:
        return EmitBinOp<kI64, kI32>(BindFirst(
            &LiftoffAssembler::emit_i64_set_cond, kUnsignedLessThanEqual));
      case kExprI64GeS:
        return EmitBinOp<kI64, kI32>(
            BindFirst(&LiftoffAssembler::emit_i64_set_cond, kGreaterThanEqual));
      case kExprI64GeU:
        return EmitBinOp<kI64, kI32>(BindFirst(
            &LiftoffAssembler::emit_i64_set_cond, kUnsignedGreaterThanEqual));
      case kExprF32Eq:
        return EmitBinOp<kF32, kI32>(
            BindFirst(&LiftoffAssembler::emit_f32_set_cond, kEqual));
      case kExprF32Ne:
        return EmitBinOp<kF32, kI32>(
            BindFirst(&LiftoffAssembler::emit_f32_set_cond, kNotEqual));
      case kExprF32Lt:
        return EmitBinOp<kF32, kI32>(
            BindFirst(&LiftoffAssembler::emit_f32_set_cond, kUnsignedLessThan));
      case kExprF32Gt:
        return EmitBinOp<kF32, kI32>(BindFirst(
            &LiftoffAssembler::emit_f32_set_cond, kUnsignedGreaterThan));
      case kExprF32Le:
        return EmitBinOp<kF32, kI32>(BindFirst(
            &LiftoffAssembler::emit_f32_set_cond, kUnsignedLessThanEqual));
      case kExprF32Ge:
        return EmitBinOp<kF32, kI32>(BindFirst(
            &LiftoffAssembler::emit_f32_set_cond, kUnsignedGreaterThanEqual));
      case kExprF64Eq:
        return EmitBinOp<kF64, kI32>(
            BindFirst(&LiftoffAssembler::emit_f64_set_cond, kEqual));
      case kExprF64Ne:
        return EmitBinOp<kF64, kI32>(
            BindFirst(&LiftoffAssembler::emit_f64_set_cond, kNotEqual));
      case kExprF64Lt:
        return EmitBinOp<kF64, kI32>(
            BindFirst(&LiftoffAssembler::emit_f64_set_cond, kUnsignedLessThan));
      case kExprF64Gt:
        return EmitBinOp<kF64, kI32>(BindFirst(
            &LiftoffAssembler::emit_f64_set_cond, kUnsignedGreaterThan));
      case kExprF64Le:
        return EmitBinOp<kF64, kI32>(BindFirst(
            &LiftoffAssembler::emit_f64_set_cond, kUnsignedLessThanEqual));
      case kExprF64Ge:
        return EmitBinOp<kF64, kI32>(BindFirst(
            &LiftoffAssembler::emit_f64_set_cond, kUnsignedGreaterThanEqual));
      case kExprI32Shl:
        return EmitBinOpImm<kI32, kI32>(&LiftoffAssembler::emit_i32_shl,
                                        &LiftoffAssembler::emit_i32_shli);
      case kExprI32ShrS:
        return EmitBinOpImm<kI32, kI32>(&LiftoffAssembler::emit_i32_sar,
                                        &LiftoffAssembler::emit_i32_sari);
      case kExprI32ShrU:
        return EmitBinOpImm<kI32, kI32>(&LiftoffAssembler::emit_i32_shr,
                                        &LiftoffAssembler::emit_i32_shri);
      case kExprI32Rol:
        return EmitBitRotationCCall<kI32, ExternalReference::wasm_word32_rol>();
      case kExprI32Ror:
        return EmitBitRotationCCall<kI32, ExternalReference::wasm_word32_ror>();
      case kExprI64Shl:
        return EmitI64Shift(&LiftoffAssembler::emit_i64_shl,
                            &LiftoffAssembler::emit_i64_shli);
      case kExprI64ShrS:
        return EmitI64Shift(&LiftoffAssembler::emit_i64_sar,
                            &LiftoffAssembler::emit_i64_sari);
      case kExprI64ShrU:
        return EmitI64Shift(&LiftoffAssembler::emit_i64_shr,
                            &LiftoffAssembler::emit_i64_shri);
      case kExprI64Rol:
        return EmitBitRotationCCall<kI64, ExternalReference::wasm_word64_rol>();
      case kExprI64Ror:
        return EmitBitRotationCCall<kI64, ExternalReference::wasm_word64_ror>();
      case kExprF32Add:
        return EmitBinOp<kF32, kF32>(&LiftoffAssembler::emit_f32_add);
      case kExprF32Sub:
        return EmitBinOp<kF32, kF32>(&LiftoffAssembler::emit_f32_sub);
      case kExprF32Mul:
        return EmitBinOp<kF32, kF32>(&LiftoffAssembler::emit_f32_mul);
      case kExprF32Div:
        return EmitBinOp<kF32, kF32>(&LiftoffAssembler::emit_f32_div);
      case kExprF32Min:
        return EmitBinOp<kF32, kF32>(&LiftoffAssembler::emit_f32_min);
      case kExprF32Max:
        return EmitBinOp<kF32, kF32>(&LiftoffAssembler::emit_f32_max);
      case kExprF32CopySign:
        return EmitBinOp<kF32, kF32>(&LiftoffAssembler::emit_f32_copysign);
      case kExprF64Add:
        return EmitBinOp<kF64, kF64>(&LiftoffAssembler::emit_f64_add);
      case kExprF64Sub:
        return EmitBinOp<kF64, kF64>(&LiftoffAssembler::emit_f64_sub);
      case kExprF64Mul:
        return EmitBinOp<kF64, kF64>(&LiftoffAssembler::emit_f64_mul);
      case kExprF64Div:
        return EmitBinOp<kF64, kF64>(&LiftoffAssembler::emit_f64_div);
      case kExprF64Min:
        return EmitBinOp<kF64, kF64>(&LiftoffAssembler::emit_f64_min);
      case kExprF64Max:
        return EmitBinOp<kF64, kF64>(&LiftoffAssembler::emit_f64_max);
      case kExprF64CopySign:
        return EmitBinOp<kF64, kF64>(&LiftoffAssembler::emit_f64_copysign);
      case kExprI32DivS:
        return EmitBinOp<kI32, kI32>([this, decoder](LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
          AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapDivByZero);
          // Adding the second trap might invalidate the pointer returned for
          // the first one, thus get both pointers afterwards.
          AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapDivUnrepresentable);
          Label* div_by_zero = out_of_line_code_.end()[-2].label.get();
          Label* div_unrepresentable = out_of_line_code_.end()[-1].label.get();
          __ emit_i32_divs(dst.gp(), lhs.gp(), rhs.gp(), div_by_zero,
                           div_unrepresentable);
        });
      case kExprI32DivU:
        return EmitBinOp<kI32, kI32>([this, decoder](LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
          Label* div_by_zero =
              AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapDivByZero);
          __ emit_i32_divu(dst.gp(), lhs.gp(), rhs.gp(), div_by_zero);
        });
      case kExprI32RemS:
        return EmitBinOp<kI32, kI32>([this, decoder](LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
          Label* rem_by_zero =
              AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapRemByZero);
          __ emit_i32_rems(dst.gp(), lhs.gp(), rhs.gp(), rem_by_zero);
        });
      case kExprI32RemU:
        return EmitBinOp<kI32, kI32>([this, decoder](LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
          Label* rem_by_zero =
              AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapRemByZero);
          __ emit_i32_remu(dst.gp(), lhs.gp(), rhs.gp(), rem_by_zero);
        });
      case kExprI64DivS:
        return EmitBinOp<kI64, kI64>([this, decoder](LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
          AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapDivByZero);
          // Adding the second trap might invalidate the pointer returned for
          // the first one, thus get both pointers afterwards.
          AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapDivUnrepresentable);
          Label* div_by_zero = out_of_line_code_.end()[-2].label.get();
          Label* div_unrepresentable = out_of_line_code_.end()[-1].label.get();
          if (!__ emit_i64_divs(dst, lhs, rhs, div_by_zero,
                                div_unrepresentable)) {
            ExternalReference ext_ref = ExternalReference::wasm_int64_div();
            EmitDivOrRem64CCall(dst, lhs, rhs, ext_ref, div_by_zero,
                                div_unrepresentable);
          }
        });
      case kExprI64DivU:
        return EmitBinOp<kI64, kI64>([this, decoder](LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
          Label* div_by_zero =
              AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapDivByZero);
          if (!__ emit_i64_divu(dst, lhs, rhs, div_by_zero)) {
            ExternalReference ext_ref = ExternalReference::wasm_uint64_div();
            EmitDivOrRem64CCall(dst, lhs, rhs, ext_ref, div_by_zero);
          }
        });
      case kExprI64RemS:
        return EmitBinOp<kI64, kI64>([this, decoder](LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
          Label* rem_by_zero =
              AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapRemByZero);
          if (!__ emit_i64_rems(dst, lhs, rhs, rem_by_zero)) {
            ExternalReference ext_ref = ExternalReference::wasm_int64_mod();
            EmitDivOrRem64CCall(dst, lhs, rhs, ext_ref, rem_by_zero);
          }
        });
      case kExprI64RemU:
        return EmitBinOp<kI64, kI64>([this, decoder](LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
          Label* rem_by_zero =
              AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapRemByZero);
          if (!__ emit_i64_remu(dst, lhs, rhs, rem_by_zero)) {
            ExternalReference ext_ref = ExternalReference::wasm_uint64_mod();
            EmitDivOrRem64CCall(dst, lhs, rhs, ext_ref, rem_by_zero);
          }
        });
      case kExprRefEq: {
#if defined(V8_COMPRESS_POINTERS)
        // In pointer compression, we smi-corrupt (the upper bits of a
        // Smi are arbitrary). So, we should only compare the lower 32 bits.
        return EmitBinOp<kRefNull, kI32>(
            BindFirst(&LiftoffAssembler::emit_i32_set_cond, kEqual));
#else
        return EmitBinOp<kRefNull, kI32>(
            BindFirst(&LiftoffAssembler::emit_ptrsize_set_cond, kEqual));
#endif
      }

      default:
        UNREACHABLE();
    }
  }

  void TraceInstruction(FullDecoder* decoder, uint32_t markid) {
#if V8_TARGET_ARCH_X64
    __ emit_trace_instruction(markid);
#endif
  }

  void I32Const(FullDecoder* decoder, Value* result, int32_t value) {
    __ PushConstant(kI32, value);
  }

  void I64Const(FullDecoder* decoder, Value* result, int64_t value) {
    // The {VarState} stores constant values as int32_t, thus we only store
    // 64-bit constants in this field if it fits in an int32_t. Larger values
    // cannot be used as immediate value anyway, so we can also just put them in
    // a register immediately.
    int32_t value_i32 = static_cast<int32_t>(value);
    if (value_i32 == value) {
      __ PushConstant(kI64, value_i32);
    } else {
      LiftoffRegister reg = __ GetUnusedRegister(reg_class_for(kI64), {});
      __ LoadConstant(reg, WasmValue(value));
      __ PushRegister(kI64, reg);
    }
  }

  void F32Const(FullDecoder* decoder, Value* result, float value) {
    LiftoffRegister reg = __ GetUnusedRegister(kFpReg, {});
    __ LoadConstant(reg, WasmValue(value));
    __ PushRegister(kF32, reg);
  }

  void F64Const(FullDecoder* decoder, Value* result, double value) {
    LiftoffRegister reg = __ GetUnusedRegister(kFpReg, {});
    __ LoadConstant(reg, WasmValue(value));
    __ PushRegister(kF64, reg);
  }

  void RefNull(FullDecoder* decoder, ValueType type, Value*) {
    LiftoffRegister null = __ GetUnusedRegister(kGpReg, {});
    LoadNullValue(null.gp(), type);
    __ PushRegister(type.kind(), null);
  }

  void RefFunc(FullDecoder* decoder, uint32_t function_index, Value* result) {
    CallBuiltin(Builtin::kWasmRefFunc,
                MakeSig::Returns(kRef).Params(kI32, kI32),
                {VarState{kI32, static_cast<int>(function_index), 0},
                 VarState{kI32, 0, 0}},
                decoder->position());
    __ PushRegister(kRef, LiftoffRegister(kReturnRegister0));
  }

  void RefAsNonNull(FullDecoder* decoder, const Value& arg, Value* result) {
    // The decoder only calls this function if the type is nullable.
    DCHECK(arg.type.is_nullable());
    LiftoffRegList pinned;
    LiftoffRegister obj = pinned.set(__ PopToRegister(pinned));
    if (null_check_strategy_ == compiler::NullCheckStrategy::kExplicit ||
        IsSubtypeOf(kWasmI31Ref.AsNonNull(), arg.type, decoder->module_) ||
        IsSubtypeOf(arg.type, kWasmExternRef, decoder->module_) ||
        IsSubtypeOf(arg.type, kWasmExnRef, decoder->module_)) {
      // Use an explicit null check if
      // (1) we cannot use trap handler or
      // (2) the object might be a Smi or
      // (3) the object might be a JS object.
      MaybeEmitNullCheck(decoder, obj.gp(), pinned, arg.type);
    } else if (!v8_flags.experimental_wasm_skip_null_checks) {
      // Otherwise, load the word after the map word.
      static_assert(WasmStruct::kHeaderSize > kTaggedSize);
      static_assert(WasmArray::kHeaderSize > kTaggedSize);
      static_assert(WasmInternalFunction::kHeaderSize > kTaggedSize);
      LiftoffRegister dst = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      uint32_t protected_load_pc = 0;
      __ Load(dst, obj.gp(), no_reg, wasm::ObjectAccess::ToTagged(kTaggedSize),
              LoadType::kI32Load, &protected_load_pc);
      RegisterProtectedInstruction(decoder, protected_load_pc);
    }
    __ PushRegister(kRef, obj);
  }

  void Drop(FullDecoder* decoder) { __ DropValues(1); }

  V8_NOINLINE V8_PRESERVE_MOST void TraceFunctionExit(FullDecoder* decoder) {
    CODE_COMMENT("trace function exit");
    // Before making the runtime call, spill all cache registers.
    __ SpillAllRegisters();

    // Store the return value if there is exactly one. Multiple return values
    // are not handled yet.
    size_t num_returns = decoder->sig_->return_count();
    // Put the parameter in its place.
    WasmTraceExitDescriptor descriptor;
    DCHECK_EQ(0, descriptor.GetStackParameterCount());
    DCHECK_EQ(1, descriptor.GetRegisterParameterCount());
    Register param_reg = descriptor.GetRegisterParameter(0);
    if (num_returns == 1) {
      auto& return_slot = __ cache_state()->stack_state.back();
      if (return_slot.is_const()) {
        __ Spill(&return_slot);
      }
      DCHECK(return_slot.is_stack());
      __ LoadSpillAddress(param_reg, return_slot.offset(), return_slot.kind());
    } else {
      // Make sure to pass a "valid" parameter (Smi::zero()).
      LoadSmi(LiftoffRegister{param_reg}, 0);
    }

    source_position_table_builder_.AddPosition(
        __ pc_offset(), SourcePosition(decoder->position()), false);
    __ CallBuiltin(Builtin::kWasmTraceExit);
    DefineSafepoint();
  }

  void TierupCheckOnTailCall(FullDecoder* decoder) {
    if (!dynamic_tiering()) return;
    TierupCheck(decoder, decoder->position(),
                __ pc_offset() + kTierUpCostForFunctionEntry);
  }

  void DoReturn(FullDecoder* decoder, uint32_t /* drop values */) {
    ReturnImpl(decoder);
  }

  void ReturnImpl(FullDecoder* decoder) {
    if (V8_UNLIKELY(v8_flags.trace_wasm)) TraceFunctionExit(decoder);
    if (dynamic_tiering()) {
      TierupCheck(decoder, decoder->position(),
                  __ pc_offset() + kTierUpCostForFunctionEntry);
    }
    size_t num_returns = decoder->sig_->return_count();
    if (num_returns > 0) __ MoveToReturnLocations(decoder->sig_, descriptor_);
    __ LeaveFrame(StackFrame::WASM);
    __ DropStackSlotsAndRet(
        static_cast<uint32_t>(descriptor_->ParameterSlotCount()));
  }

  void LocalGet(FullDecoder* decoder, Value* result,
                const IndexImmediate& imm) {
    auto local_slot = __ cache_state()->stack_state[imm.index];
    __ cache_state()->stack_state.emplace_back(
        local_slot.kind(), __ NextSpillOffset(local_slot.kind()));
    auto* slot = &__ cache_state()->stack_state.back();
    if (local_slot.is_reg()) {
      __ cache_state()->inc_used(local_slot.reg());
      slot->MakeRegister(local_slot.reg());
    } else if (local_slot.is_const()) {
      slot->MakeConstant(local_slot.i32_const());
    } else {
      DCHECK(local_slot.is_stack());
      auto rc = reg_class_for(local_slot.kind());
      LiftoffRegister reg = __ GetUnusedRegister(rc, {});
      __ cache_state()->inc_used(reg);
      slot->MakeRegister(reg);
      __ Fill(reg, local_slot.offset(), local_slot.kind());
    }
  }

  void LocalSetFromStackSlot(VarState* dst_slot, uint32_t local_index) {
    auto& state = *__ cache_state();
    auto& src_slot = state.stack_state.back();
    ValueKind kind = dst_slot->kind();
    if (dst_slot->is_reg()) {
      LiftoffRegister slot_reg = dst_slot->reg();
      if (state.get_use_count(slot_reg) == 1) {
        __ Fill(dst_slot->reg(), src_slot.offset(), kind);
        return;
      }
      state.dec_used(slot_reg);
      dst_slot->MakeStack();
    }
    DCHECK(CompatibleStackSlotTypes(kind, __ local_kind(local_index)));
    RegClass rc = reg_class_for(kind);
    LiftoffRegister dst_reg = __ GetUnusedRegister(rc, {});
    __ Fill(dst_reg, src_slot.offset(), kind);
    *dst_slot = VarState(kind, dst_reg, dst_slot->offset());
    __ cache_state()->inc_used(dst_reg);
  }

  void LocalSet(uint32_t local_index, bool is_tee) {
    auto& state = *__ cache_state();
    auto& source_slot = state.stack_state.back();
    auto& target_slot = state.stack_state[local_index];
    switch (source_slot.loc()) {
      case kRegister:
        if (target_slot.is_reg()) state.dec_used(target_slot.reg());
        target_slot.Copy(source_slot);
        if (is_tee) state.inc_used(target_slot.reg());
        break;
      case kIntConst:
        if (target_slot.is_reg()) state.dec_used(target_slot.reg());
        target_slot.Copy(source_slot);
        break;
      case kStack:
        LocalSetFromStackSlot(&target_slot, local_index);
        break;
    }
    if (!is_tee) __ cache_state()->stack_state.pop_back();
  }

  void LocalSet(FullDecoder* decoder, const Value& value,
                const IndexImmediate& imm) {
    LocalSet(imm.index, false);
  }

  void LocalTee(FullDecoder* decoder, const Value& value, Value* result,
                const IndexImmediate& imm) {
    LocalSet(imm.index, true);
  }

  Register GetGlobalBaseAndOffset(const WasmGlobal* global,
                                  LiftoffRegList* pinned, uint32_t* offset) {
    Register addr = pinned->set(__ GetUnusedRegister(kGpReg, {})).gp();
    if (global->mutability && global->imported) {
      LOAD_TAGGED_PTR_INSTANCE_FIELD(addr, ImportedMutableGlobals, *pinned);
      int field_offset =
          wasm::ObjectAccess::ElementOffsetInTaggedFixedAddressArray(
              global->index);
      __ LoadFullPointer(addr, addr, field_offset);
      *offset = 0;
#ifdef V8_ENABLE_SANDBOX
      __ DecodeSandboxedPointer(addr);
#endif
    } else {
      LOAD_INSTANCE_FIELD(addr, GlobalsStart, kSystemPointerSize, *pinned);
      *offset = global->offset;
    }
      return addr;
  }

  void GetBaseAndOffsetForImportedMutableExternRefGlobal(
      const WasmGlobal* global, LiftoffRegList* pinned, Register* base,
      Register* offset) {
    Register globals_buffer =
        pinned->set(__ GetUnusedRegister(kGpReg, *pinned)).gp();
    LOAD_TAGGED_PTR_INSTANCE_FIELD(globals_buffer,
                                   ImportedMutableGlobalsBuffers, *pinned);
    *base = globals_buffer;
    __ LoadTaggedPointer(
        *base, globals_buffer, no_reg,
        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(global->offset));

    // For the offset we need the index of the global in the buffer, and
    // then calculate the actual offset from the index. Load the index from
    // the ImportedMutableGlobals array of the instance.
    Register imported_mutable_globals =
        pinned->set(__ GetUnusedRegister(kGpReg, *pinned)).gp();

    LOAD_TAGGED_PTR_INSTANCE_FIELD(imported_mutable_globals,
                                   ImportedMutableGlobals, *pinned);
    *offset = imported_mutable_globals;
    int field_offset =
        wasm::ObjectAccess::ElementOffsetInTaggedFixedAddressArray(
            global->index);
    __ Load(LiftoffRegister(*offset), imported_mutable_globals, no_reg,
            field_offset, LoadType::kI32Load);
    __ emit_i32_shli(*offset, *offset, kTaggedSizeLog2);
    __ emit_i32_addi(*offset, *offset,
                     wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(0));
  }

  void GlobalGet(FullDecoder* decoder, Value* result,
                 const GlobalIndexImmediate& imm) {
    const auto* global = &env_->module->globals[imm.index];
    ValueKind kind = global->type.kind();
    if (!CheckSupportedType(decoder, kind, "global")) {
      return;
    }

    if (is_reference(kind)) {
      if (global->mutability && global->imported) {
        LiftoffRegList pinned;
        Register base = no_reg;
        Register offset = no_reg;
        GetBaseAndOffsetForImportedMutableExternRefGlobal(global, &pinned,
                                                          &base, &offset);
        __ LoadTaggedPointer(base, base, offset, 0);
        __ PushRegister(kind, LiftoffRegister(base));
        return;
      }

      LiftoffRegList pinned;
      Register globals_buffer =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
      LOAD_TAGGED_PTR_INSTANCE_FIELD(globals_buffer, TaggedGlobalsBuffer,
                                     pinned);
      Register value = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
      __ LoadTaggedPointer(value, globals_buffer, no_reg,
                           wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(
                               imm.global->offset));
      __ PushRegister(kind, LiftoffRegister(value));
      return;
    }
    LiftoffRegList pinned;
    uint32_t offset = 0;
    Register addr = GetGlobalBaseAndOffset(global, &pinned, &offset);
    LiftoffRegister value =
        pinned.set(__ GetUnusedRegister(reg_class_for(kind), pinned));
    LoadType type = LoadType::ForValueKind(kind);
    __ Load(value, addr, no_reg, offset, type, nullptr, false);
    __ PushRegister(kind, value);
  }

  void GlobalSet(FullDecoder* decoder, const Value&,
                 const GlobalIndexImmediate& imm) {
    auto* global = &env_->module->globals[imm.index];
    ValueKind kind = global->type.kind();
    if (!CheckSupportedType(decoder, kind, "global")) {
      return;
    }

    if (is_reference(kind)) {
      if (global->mutability && global->imported) {
        LiftoffRegList pinned;
        Register value = pinned.set(__ PopToRegister(pinned)).gp();
        Register base = no_reg;
        Register offset = no_reg;
        GetBaseAndOffsetForImportedMutableExternRefGlobal(global, &pinned,
                                                          &base, &offset);
        __ StoreTaggedPointer(base, offset, 0, value, pinned);
        return;
      }

      LiftoffRegList pinned;
      Register globals_buffer =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
      LOAD_TAGGED_PTR_INSTANCE_FIELD(globals_buffer, TaggedGlobalsBuffer,
                                     pinned);
      Register value = pinned.set(__ PopToRegister(pinned)).gp();
      __ StoreTaggedPointer(globals_buffer, no_reg,
                            wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(
                                imm.global->offset),
                            value, pinned);
      return;
    }
    LiftoffRegList pinned;
    uint32_t offset = 0;
    Register addr = GetGlobalBaseAndOffset(global, &pinned, &offset);
    LiftoffRegister reg = pinned.set(__ PopToRegister(pinned));
    StoreType type = StoreType::ForValueKind(kind);
    __ Store(addr, no_reg, offset, reg, type, {}, nullptr, false);
  }

  void TableGet(FullDecoder* decoder, const Value&, Value*,
                const TableIndexImmediate& imm) {
    Register index_high_word = no_reg;
    LiftoffRegList pinned;
    VarState table_index{kI32, static_cast<int>(imm.index), 0};

    // Convert the index to the table to an intptr.
    VarState index = PopIndexToVarState(&index_high_word, &pinned);
    // Trap if any bit in the high word was set.
    CheckHighWordEmptyForTableType(decoder, index_high_word, &pinned);

    ValueType type = imm.table->type;
    bool is_funcref = IsSubtypeOf(type, kWasmFuncRef, env_->module);
    auto stub =
        is_funcref ? Builtin::kWasmTableGetFuncRef : Builtin::kWasmTableGet;

    CallBuiltin(stub, MakeSig::Returns(type.kind()).Params(kI32, kIntPtrKind),
                {table_index, index}, decoder->position());

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    __ PushRegister(type.kind(), LiftoffRegister(kReturnRegister0));
  }

  void TableSet(FullDecoder* decoder, const Value&, const Value&,
                const TableIndexImmediate& imm) {
    Register index_high_word = no_reg;
    LiftoffRegList pinned;
    VarState table_index{kI32, static_cast<int>(imm.index), 0};

    VarState value = __ PopVarState();
    if (value.is_reg()) pinned.set(value.reg());
    // Convert the index to the table to an intptr.
    VarState index = PopIndexToVarState(&index_high_word, &pinned);
    // Trap if any bit in the high word was set.
    CheckHighWordEmptyForTableType(decoder, index_high_word, &pinned);
    VarState extract_shared_part{kI32, 0, 0};

    bool is_funcref = IsSubtypeOf(imm.table->type, kWasmFuncRef, env_->module);
    auto stub =
        is_funcref ? Builtin::kWasmTableSetFuncRef : Builtin::kWasmTableSet;

    CallBuiltin(stub, MakeSig::Params(kI32, kI32, kIntPtrKind, kRefNull),
                {table_index, extract_shared_part, index, value},
                decoder->position());

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);
  }

  Builtin GetBuiltinForTrapReason(TrapReason reason) {
    switch (reason) {
#define RUNTIME_STUB_FOR_TRAP(trap_reason) \
  case k##trap_reason:                     \
    return Builtin::kThrowWasm##trap_reason;

      FOREACH_WASM_TRAPREASON(RUNTIME_STUB_FOR_TRAP)
#undef RUNTIME_STUB_FOR_TRAP
      default:
        UNREACHABLE();
    }
  }

  void Trap(FullDecoder* decoder, TrapReason reason) {
    Label* trap_label =
        AddOutOfLineTrap(decoder, GetBuiltinForTrapReason(reason));
    __ emit_jump(trap_label);
    __ AssertUnreachable(AbortReason::kUnexpectedReturnFromWasmTrap);
  }

  void AssertNullTypecheckImpl(FullDecoder* decoder, const Value& arg,
                               Value* result, Condition cond) {
    LiftoffRegList pinned;
    LiftoffRegister obj = pinned.set(__ PopToRegister(pinned));
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapIllegalCast);
    LiftoffRegister null = __ GetUnusedRegister(kGpReg, pinned);
    LoadNullValueForCompare(null.gp(), pinned, arg.type);
    {
      FREEZE_STATE(trapping);
      __ emit_cond_jump(cond, trap_label, kRefNull, obj.gp(), null.gp(),
                        trapping);
    }
    __ PushRegister(kRefNull, obj);
  }

  void AssertNullTypecheck(FullDecoder* decoder, const Value& arg,
                           Value* result) {
    AssertNullTypecheckImpl(decoder, arg, result, kNotEqual);
  }

  void AssertNotNullTypecheck(FullDecoder* decoder, const Value& arg,
                              Value* result) {
    AssertNullTypecheckImpl(decoder, arg, result, kEqual);
  }

  void NopForTestingUnsupportedInLiftoff(FullDecoder* decoder) {
    unsupported(decoder, kOtherReason, "testing opcode");
  }

  void Select(FullDecoder* decoder, const Value& cond, const Value& fval,
              const Value& tval, Value* result) {
    LiftoffRegList pinned;
    Register condition = pinned.set(__ PopToRegister()).gp();
    ValueKind kind = __ cache_state()->stack_state.end()[-1].kind();
    DCHECK(CompatibleStackSlotTypes(
        kind, __ cache_state()->stack_state.end()[-2].kind()));
    LiftoffRegister false_value = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister true_value = __ PopToRegister(pinned);
    LiftoffRegister dst = __ GetUnusedRegister(true_value.reg_class(),
                                               {true_value, false_value}, {});
    if (!__ emit_select(dst, condition, true_value, false_value, kind)) {
      FREEZE_STATE(frozen);
      // Emit generic code (using branches) instead.
      Label cont;
      Label case_false;
      __ emit_cond_jump(kEqual, &case_false, kI32, condition, no_reg, frozen);
      if (dst != true_value) __ Move(dst, true_value, kind);
      __ emit_jump(&cont);

      __ bind(&case_false);
      if (dst != false_value) __ Move(dst, false_value, kind);
      __ bind(&cont);
    }
    __ PushRegister(kind, dst);
  }

  void BrImpl(FullDecoder* decoder, Control* target) {
    if (dynamic_tiering()) {
      if (target->is_loop()) {
        DCHECK(target->label.get()->is_bound());
        int jump_distance = __ pc_offset() - target->label.get()->pos();
        TierupCheck(decoder, decoder->position(), jump_distance);
      } else {
        // To estimate time spent in this function more accurately, we could
        // increment the tiering budget on forward jumps. However, we don't
        // know the jump distance yet; using a blanket value has been tried
        // and found to not make a difference.
      }
    }
    if (target->br_merge()->reached) {
      __ MergeStackWith(target->label_state, target->br_merge()->arity,
                        target->is_loop() ? LiftoffAssembler::kBackwardJump
                                          : LiftoffAssembler::kForwardJump);
    } else {
      target->label_state =
          __ MergeIntoNewState(__ num_locals(), target->br_merge()->arity,
                               target->stack_depth + target->num_exceptions);
    }
    __ jmp(target->label.get());
  }

  bool NeedsTierupCheck(FullDecoder* decoder, uint32_t br_depth) {
    if (!dynamic_tiering()) return false;
    return br_depth == decoder->control_depth() - 1 ||
           decoder->control_at(br_depth)->is_loop();
  }

  void BrOrRet(FullDecoder* decoder, uint32_t depth) {
    if (depth == decoder->control_depth() - 1) {
      ReturnImpl(decoder);
    } else {
      BrImpl(decoder, decoder->control_at(depth));
    }
  }

  void BrIf(FullDecoder* decoder, const Value& /* cond */, uint32_t depth) {
    // Avoid having sequences of branches do duplicate work.
    if (depth != decoder->control_depth() - 1) {
      __ PrepareForBranch(decoder->control_at(depth)->br_merge()->arity, {});
    }

    Label cont_false;

    // Test the condition on the value stack, jump to {cont_false} if zero.
    std::optional<FreezeCacheState> frozen;
    JumpIfFalse(decoder, &cont_false, frozen);

    BrOrRet(decoder, depth);

    __ bind(&cont_false);
  }

  // Generate a branch table case, potentially reusing previously generated
  // stack transfer code.
  void GenerateBrCase(FullDecoder* decoder, uint32_t br_depth,
                      ZoneMap<uint32_t, MovableLabel>* br_targets) {
    auto [iterator, is_new_target] = br_targets->emplace(br_depth, zone_);
    Label* label = iterator->second.get();
    DCHECK_EQ(is_new_target, !label->is_bound());
    if (is_new_target) {
      __ bind(label);
      BrOrRet(decoder, br_depth);
    } else {
      __ jmp(label);
    }
  }

  // Generate a branch table for input in [min, max).
  // TODO(wasm): Generate a real branch table (like TF TableSwitch).
  void GenerateBrTable(FullDecoder* decoder, LiftoffRegister value,
                       uint32_t min, uint32_t max,
                       BranchTableIterator<ValidationTag>* table_iterator,
                       ZoneMap<uint32_t, MovableLabel>* br_targets,
                       const FreezeCacheState& frozen) {
    DCHECK_LT(min, max);
    // Check base case.
    if (max == min + 1) {
      DCHECK_EQ(min, table_iterator->cur_index());
      GenerateBrCase(decoder, table_iterator->next(), br_targets);
      return;
    }

    uint32_t split = min + (max - min) / 2;
    Label upper_half;
    __ emit_i32_cond_jumpi(kUnsignedGreaterThanEqual, &upper_half, value.gp(),
                           split, frozen);
    // Emit br table for lower half:
    GenerateBrTable(decoder, value, min, split, table_iterator, br_targets,
                    frozen);
    __ bind(&upper_half);
    // table_iterator will trigger a DCHECK if we don't stop decoding now.
    if (did_bailout()) return;
    // Emit br table for upper half:
    GenerateBrTable(decoder, value, split, max, table_iterator, br_targets,
                    frozen);
  }

  void BrTable(FullDecoder* decoder, const BranchTableImmediate& imm,
               const Value& key) {
    LiftoffRegList pinned;
    LiftoffRegister value = pinned.set(__ PopToRegister());

    {
      // All targets must have the same arity (checked by validation), so
      // we can just sample any of them to find that arity.
      auto [sample_depth, unused_length] =
          decoder->read_u32v<Decoder::NoValidationTag>(imm.table,
                                                       "first depth");
      __ PrepareForBranch(decoder->control_at(sample_depth)->br_merge()->arity,
                          pinned);
    }

    BranchTableIterator<ValidationTag> table_iterator{decoder, imm};
    ZoneMap<uint32_t, MovableLabel> br_targets{zone_};

    if (imm.table_count > 0) {
      FREEZE_STATE(frozen);
      Label case_default;
      __ emit_i32_cond_jumpi(kUnsignedGreaterThanEqual, &case_default,
                             value.gp(), imm.table_count, frozen);

      GenerateBrTable(decoder, value, 0, imm.table_count, &table_iterator,
                      &br_targets, frozen);

      __ bind(&case_default);
      // table_iterator will trigger a DCHECK if we don't stop decoding now.
      if (did_bailout()) return;
    }

    // Generate the default case.
    GenerateBrCase(decoder, table_iterator.next(), &br_targets);
    DCHECK(!table_iterator.has_next());
  }

  void Else(FullDecoder* decoder, Control* c) {
    if (c->reachable()) {
      if (c->end_merge.reached) {
        __ MergeFullStackWith(c->label_state);
      } else {
        c->label_state =
            __ MergeIntoNewState(__ num_locals(), c->end_merge.arity,
                                 c->stack_depth + c->num_exceptions);
      }
      __ emit_jump(c->label.get());
    }
    __ bind(c->else_state->label.get());
    __ cache_state()->Steal(c->else_state->state);
  }

  SpilledRegistersForInspection* GetSpilledRegistersForInspection() {
    DCHECK(for_debugging_);
    // If we are generating debugging code, we really need to spill all
    // registers to make them inspectable when stopping at the trap.
    auto* spilled = zone_->New<SpilledRegistersForInspection>(zone_);
    for (uint32_t i = 0, e = __ cache_state()->stack_height(); i < e; ++i) {
      auto& slot = __ cache_state()->stack_state[i];
      if (!slot.is_reg()) continue;
      spilled->entries.push_back(SpilledRegistersForInspection::Entry{
          slot.offset(), slot.reg(), slot.kind()});
      __ RecordUsedSpillOffset(slot.offset());
    }
    return spilled;
  }

  Label* AddOutOfLineTrap(FullDecoder* decoder, Builtin builtin) {
    DCHECK(v8_flags.wasm_bounds_checks);
    OutOfLineSafepointInfo* safepoint_info = nullptr;
    // Execution does not return after a trap. Therefore we don't have to define
    // a safepoint for traps that would preserve references on the stack.
    // However, if this is debug code, then we have to preserve the references
    // so that they can be inspected.
    if (V8_UNLIKELY(for_debugging_)) {
      safepoint_info = zone_->New<OutOfLineSafepointInfo>(zone_);
      __ cache_state()->GetTaggedSlotsForOOLCode(
          &safepoint_info->slots, &safepoint_info->spills,
          LiftoffAssembler::CacheState::SpillLocation::kStackSlots);
    }
    out_of_line_code_.push_back(OutOfLineCode::Trap(
        zone_, builtin, decoder->position(),
        V8_UNLIKELY(for_debugging_) ? GetSpilledRegistersForInspection()
                                    : nullptr,
        safepoint_info, RegisterOOLDebugSideTableEntry(decoder)));
    return out_of_line_code_.back().label.get();
  }

  enum ForceCheck : bool { kDoForceCheck = true, kDontForceCheck = false };
  enum AlignmentCheck : bool {
    kCheckAlignment = true,
    kDontCheckAlignment = false
  };

  // Returns the GP {index} register holding the ptrsized index.
  // Note that the {index} will typically not be pinned, but the returned
  // register will be pinned by the caller. This avoids one pinned register if
  // {full_index} is a pair.
  Register BoundsCheckMem(FullDecoder* decoder, const WasmMemory* memory,
                          uint32_t access_size, uint64_t offset,
                          LiftoffRegister index, LiftoffRegList pinned,
                          ForceCheck force_check,
                          AlignmentCheck check_alignment) {
    // The decoder ensures that the access is not statically OOB.
    DCHECK(base::IsInBounds<uintptr_t>(offset, access_size,
                                       memory->max_memory_size));

    wasm::BoundsCheckStrategy bounds_checks = memory->bounds_checks;

    // After bounds checking, we know that the index must be ptrsize, hence only
    // look at the lower word on 32-bit systems (the high word is bounds-checked
    // further down).
    Register index_ptrsize =
        kNeedI64RegPair && index.is_gp_pair() ? index.low_gp() : index.gp();

    if (check_alignment) {
      AlignmentCheckMem(decoder, access_size, offset, index_ptrsize,
                        pinned | LiftoffRegList{index});
    }

    // Without bounds checks (testing only), just return the ptrsize index.
    if (V8_UNLIKELY(bounds_checks == kNoBoundsChecks)) {
      return index_ptrsize;
    }

    // We already checked that offset is below the max memory size.
    DCHECK_LT(offset, memory->max_memory_size);

    // Early return for trap handler.
    DCHECK_IMPLIES(memory->is_memory64 && !v8_flags.wasm_memory64_trap_handling,
                   bounds_checks == kExplicitBoundsChecks);
    if (!force_check && bounds_checks == kTrapHandler) {
      if (memory->is_memory64) {
        SCOPED_CODE_COMMENT("bounds check memory");
        // If index is outside the guards pages, sets index to a value that will
        // certainly cause (memory_start + offset + index) to be not accessible,
        // to make sure that the OOB access will be caught by the trap handler.
        __ set_trap_on_oob_mem64(index_ptrsize, memory->GetMemory64GuardsSize(),
                                 memory->max_memory_size);
      }

      // With trap handlers we should not have a register pair as input (we
      // would only return the lower half).
      DCHECK(index.is_gp());
      return index_ptrsize;
    }

    SCOPED_CODE_COMMENT("bounds check memory");

    // Set {pc} of the OOL code to {0} to avoid generation of protected
    // instruction information (see {GenerateOutOfLineCode}).
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapMemOutOfBounds);

    // Convert the index to ptrsize, bounds-checking the high word on 32-bit
    // systems for memory64.
    if (!memory->is_memory64) {
      __ emit_u32_to_uintptr(index_ptrsize, index_ptrsize);
    } else if (kSystemPointerSize == kInt32Size) {
      DCHECK_GE(kMaxUInt32, memory->max_memory_size);
      FREEZE_STATE(trapping);
      __ emit_cond_jump(kNotZero, trap_label, kI32, index.high_gp(), no_reg,
                        trapping);
    }

    uintptr_t end_offset = offset + access_size - 1u;

    pinned.set(index_ptrsize);
    LiftoffRegister end_offset_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LiftoffRegister mem_size = __ GetUnusedRegister(kGpReg, pinned);
    // TODO(13957): Clamp the loaded memory size to a safe value.
    if (memory->index == 0) {
      LOAD_INSTANCE_FIELD(mem_size.gp(), Memory0Size, kSystemPointerSize,
                          pinned);
    } else {
      LOAD_PROTECTED_PTR_INSTANCE_FIELD(mem_size.gp(), MemoryBasesAndSizes,
                                        pinned);
      int buffer_offset = wasm::ObjectAccess::ToTagged(ByteArray::kHeaderSize) +
                          kSystemPointerSize * (memory->index * 2 + 1);
      __ LoadFullPointer(mem_size.gp(), mem_size.gp(), buffer_offset);
    }

    __ LoadConstant(end_offset_reg, WasmValue::ForUintPtr(end_offset));

    FREEZE_STATE(trapping);
    // If the end offset is larger than the smallest memory, dynamically check
    // the end offset against the actual memory size, which is not known at
    // compile time. Otherwise, only one check is required (see below).
    if (end_offset > memory->min_memory_size) {
      __ emit_cond_jump(kUnsignedGreaterThanEqual, trap_label, kIntPtrKind,
                        end_offset_reg.gp(), mem_size.gp(), trapping);
    }

    // Just reuse the end_offset register for computing the effective size
    // (which is >= 0 because of the check above).
    LiftoffRegister effective_size_reg = end_offset_reg;
    __ emit_ptrsize_sub(effective_size_reg.gp(), mem_size.gp(),
                        end_offset_reg.gp());

    __ emit_cond_jump(kUnsignedGreaterThanEqual, trap_label, kIntPtrKind,
                      index_ptrsize, effective_size_reg.gp(), trapping);
    return index_ptrsize;
  }

  void AlignmentCheckMem(FullDecoder* decoder, uint32_t access_size,
                         uintptr_t offset, Register index,
                         LiftoffRegList pinned) {
    DCHECK_NE(0, access_size);
    // For access_size 1 there is no minimum alignment.
    if (access_size == 1) return;
    SCOPED_CODE_COMMENT("alignment check");
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapUnalignedAccess);
    Register address = __ GetUnusedRegister(kGpReg, pinned).gp();

    FREEZE_STATE(trapping);
    const uint32_t align_mask = access_size - 1;
    if ((offset & align_mask) == 0) {
      // If {offset} is aligned, we can produce faster code.

      // TODO(ahaas): On Intel, the "test" instruction implicitly computes the
      // AND of two operands. We could introduce a new variant of
      // {emit_cond_jump} to use the "test" instruction without the "and" here.
      // Then we can also avoid using the temp register here.
      __ emit_i32_andi(address, index, align_mask);
      __ emit_cond_jump(kNotEqual, trap_label, kI32, address, no_reg, trapping);
    } else {
      // For alignment checks we only look at the lower 32-bits in {offset}.
      __ emit_i32_addi(address, index, static_cast<uint32_t>(offset));
      __ emit_i32_andi(address, address, align_mask);
      __ emit_cond_jump(kNotEqual, trap_label, kI32, address, no_reg, trapping);
    }
  }

  void TraceMemoryOperation(bool is_store, MachineRepresentation rep,
                            Register index, uintptr_t offset,
                            WasmCodePosition position) {
    // Before making the runtime call, spill all cache registers.
    __ SpillAllRegisters();

    LiftoffRegList pinned;
    if (index != no_reg) pinned.set(index);
    // Get one register for computing the effective offset (offset + index).
    LiftoffRegister effective_offset =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    // TODO(14259): Support multiple memories.
    const WasmMemory* memory = env_->module->memories.data();
    if (memory->is_memory64 && !kNeedI64RegPair) {
      __ LoadConstant(effective_offset,
                      WasmValue(static_cast<uint64_t>(offset)));
      if (index != no_reg) {
        __ emit_i64_add(effective_offset, effective_offset,
                        LiftoffRegister(index));
      }
    } else {
      // The offset is actually a 32-bits number when 'kNeedI64RegPair'
      // is true, so we just do 32-bits operations on it under memory64.
      DCHECK_GE(kMaxUInt32, offset);
      __ LoadConstant(effective_offset,
                      WasmValue(static_cast<uint32_t>(offset)));
      if (index != no_reg) {
        __ emit_i32_add(effective_offset.gp(), effective_offset.gp(), index);
      }
    }

    // Get a register to hold the stack slot for MemoryTracingInfo.
    LiftoffRegister info = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    // Allocate stack slot for MemoryTracingInfo.
    __ AllocateStackSlot(info.gp(), sizeof(MemoryTracingInfo));

    // Reuse the {effective_offset} register for all information to be stored in
    // the MemoryTracingInfo struct.
    LiftoffRegister data = effective_offset;

    // Now store all information into the MemoryTracingInfo struct.
    if (kSystemPointerSize == 8 && !memory->is_memory64) {
      // Zero-extend the effective offset to u64.
      CHECK(__ emit_type_conversion(kExprI64UConvertI32, data, effective_offset,
                                    nullptr));
    }
    __ Store(
        info.gp(), no_reg, offsetof(MemoryTracingInfo, offset), data,
        kSystemPointerSize == 8 ? StoreType::kI64Store : StoreType::kI32Store,
        pinned);
    __ LoadConstant(data, WasmValue(is_store ? 1 : 0));
    __ Store(info.gp(), no_reg, offsetof(MemoryTracingInfo, is_store), data,
             StoreType::kI32Store8, pinned);
    __ LoadConstant(data, WasmValue(static_cast<int>(rep)));
    __ Store(info.gp(), no_reg, offsetof(MemoryTracingInfo, mem_rep), data,
             StoreType::kI32Store8, pinned);

    WasmTraceMemoryDescriptor descriptor;
    DCHECK_EQ(0, descriptor.GetStackParameterCount());
    DCHECK_EQ(1, descriptor.GetRegisterParameterCount());
    Register param_reg = descriptor.GetRegisterParameter(0);
    if (info.gp() != param_reg) {
      __ Move(param_reg, info.gp(), kIntPtrKind);
    }

    source_position_table_builder_.AddPosition(__ pc_offset(),
                                               SourcePosition(position), false);
    __ CallBuiltin(Builtin::kWasmTraceMemory);
    DefineSafepoint();

    __ DeallocateStackSlot(sizeof(MemoryTracingInfo));
  }

  bool IndexStaticallyInBounds(const WasmMemory* memory,
                               const VarState& index_slot, int access_size,
                               uintptr_t* offset) {
    if (!index_slot.is_const()) return false;

    // Potentially zero extend index (which is a 32-bit constant).
    const uintptr_t index = static_cast<uint32_t>(index_slot.i32_const());
    const uintptr_t effective_offset = index + *offset;

    if (effective_offset < index  // overflow
        || !base::IsInBounds<uintptr_t>(effective_offset, access_size,
                                        memory->min_memory_size)) {
      return false;
    }

    *offset = effective_offset;
    return true;
  }

  bool IndexStaticallyInBoundsAndAligned(const WasmMemory* memory,
                                         const VarState& index_slot,
                                         int access_size, uintptr_t* offset) {
    uintptr_t new_offset = *offset;
    if (IndexStaticallyInBounds(memory, index_slot, access_size, &new_offset) &&
        IsAligned(new_offset, access_size)) {
      *offset = new_offset;
      return true;
    }
    return false;
  }

  V8_INLINE Register GetMemoryStart(int memory_index, LiftoffRegList pinned) {
    if (memory_index == __ cache_state()->cached_mem_index) {
      Register memory_start = __ cache_state()->cached_mem_start;
      DCHECK_NE(no_reg, memory_start);
      return memory_start;
    }
    return GetMemoryStart_Slow(memory_index, pinned);
  }

  V8_NOINLINE V8_PRESERVE_MOST Register
  GetMemoryStart_Slow(int memory_index, LiftoffRegList pinned) {
    // This method should only be called if we cannot use the cached memory
    // start.
    DCHECK_NE(memory_index, __ cache_state()->cached_mem_index);
    __ cache_state()->ClearCachedMemStartRegister();
    SCOPED_CODE_COMMENT("load memory start");
    Register memory_start = __ GetUnusedRegister(kGpReg, pinned).gp();
    if (memory_index == 0) {
      LOAD_INSTANCE_FIELD(memory_start, Memory0Start, kSystemPointerSize,
                          pinned);
    } else {
      LOAD_PROTECTED_PTR_INSTANCE_FIELD(memory_start, MemoryBasesAndSizes,
                                        pinned);
      int buffer_offset = wasm::ObjectAccess::ToTagged(
          TrustedFixedAddressArray::OffsetOfElementAt(memory_index * 2));
      __ LoadFullPointer(memory_start, memory_start, buffer_offset);
    }
    __ cache_state()->SetMemStartCacheRegister(memory_start, memory_index);
    return memory_start;
  }

  void LoadMem(FullDecoder* decoder, LoadType type,
               const MemoryAccessImmediate& imm, const Value& index_val,
               Value* result) {
    DCHECK_EQ(type.value_type().kind(), result->type.kind());
    bool needs_f16_to_f32_conv = false;
    if (type.value() == LoadType::kF32LoadF16 &&
        !asm_.supports_f16_mem_access()) {
      needs_f16_to_f32_conv = true;
      type = LoadType::kI32Load16U;
    }
    ValueKind kind = type.value_type().kind();
    if (!CheckSupportedType(decoder, kind, "load")) return;

    uintptr_t offset = imm.offset;
    Register index = no_reg;
    RegClass rc = reg_class_for(kind);

    // Only look at the slot, do not pop it yet (will happen in PopToRegister
    // below, if this is not a statically-in-bounds index).
    auto& index_slot = __ cache_state()->stack_state.back();
    DCHECK_EQ(index_val.type.kind(), index_slot.kind());
    bool i64_offset = imm.memory->is_memory64;
    DCHECK_EQ(i64_offset ? kI64 : kI32, index_slot.kind());
    if (IndexStaticallyInBounds(imm.memory, index_slot, type.size(), &offset)) {
      __ cache_state()->stack_state.pop_back();
      SCOPED_CODE_COMMENT("load from memory (constant offset)");
      LiftoffRegList pinned;
      Register mem = pinned.set(GetMemoryStart(imm.memory->index, pinned));
      LiftoffRegister value = pinned.set(__ GetUnusedRegister(rc, pinned));
      __ Load(value, mem, no_reg, offset, type, nullptr, true, i64_offset);
      if (needs_f16_to_f32_conv) {
        LiftoffRegister dst = __ GetUnusedRegister(kFpReg, {});
        auto conv_ref = ExternalReference::wasm_float16_to_float32();
        GenerateCCallWithStackBuffer(&dst, kVoid, kF32,
                                     {VarState{kI16, value, 0}}, conv_ref);
        __ PushRegister(kF32, dst);
      } else {
        __ PushRegister(kind, value);
      }
    } else {
      LiftoffRegister full_index = __ PopToRegister();
      index =
          BoundsCheckMem(decoder, imm.memory, type.size(), offset, full_index,
                         {}, kDontForceCheck, kDontCheckAlignment);

      SCOPED_CODE_COMMENT("load from memory");
      LiftoffRegList pinned{index};

      // Load the memory start address only now to reduce register pressure
      // (important on ia32).
      Register mem = pinned.set(GetMemoryStart(imm.memory->index, pinned));
      LiftoffRegister value = pinned.set(__ GetUnusedRegister(rc, pinned));

      uint32_t protected_load_pc = 0;
      __ Load(value, mem, index, offset, type, &protected_load_pc, true,
              i64_offset);
      if (imm.memory->bounds_checks == kTrapHandler) {
        RegisterProtectedInstruction(decoder, protected_load_pc);
      }
      if (needs_f16_to_f32_conv) {
        LiftoffRegister dst = __ GetUnusedRegister(kFpReg, {});
        auto conv_ref = ExternalReference::wasm_float16_to_float32();
        GenerateCCallWithStackBuffer(&dst, kVoid, kF32,
                                     {VarState{kI16, value, 0}}, conv_ref);
        __ PushRegister(kF32, dst);
      } else {
        __ PushRegister(kind, value);
      }
    }

    if (V8_UNLIKELY(v8_flags.trace_wasm_memory)) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      TraceMemoryOperation(false, type.mem_type().representation(), index,
                           offset, decoder->position());
    }
  }

  void LoadTransform(FullDecoder* decoder, LoadType type,
                     LoadTransformationKind transform,
                     const MemoryAccessImmediate& imm, const Value& index_val,
                     Value* result) {
    CHECK(CheckSupportedType(decoder, kS128, "LoadTransform"));

    LiftoffRegister full_index = __ PopToRegister();
    // For load splats and load zero, LoadType is the size of the load, and for
    // load extends, LoadType is the size of the lane, and it always loads 8
    // bytes.
    uint32_t access_size =
        transform == LoadTransformationKind::kExtend ? 8 : type.size();
    Register index =
        BoundsCheckMem(decoder, imm.memory, access_size, imm.offset, full_index,
                       {}, kDontForceCheck, kDontCheckAlignment);

    uintptr_t offset = imm.offset;
    LiftoffRegList pinned{index};
    CODE_COMMENT("load with transformation");
    Register addr = GetMemoryStart(imm.mem_index, pinned);
    LiftoffRegister value = __ GetUnusedRegister(reg_class_for(kS128), {});
    uint32_t protected_load_pc = 0;
    __ LoadTransform(value, addr, index, offset, type, transform,
                     &protected_load_pc);

    if (imm.memory->bounds_checks == kTrapHandler) {
      protected_instructions_.emplace_back(
          trap_handler::ProtectedInstructionData{protected_load_pc});
      source_position_table_builder_.AddPosition(
          protected_load_pc, SourcePosition(decoder->position()), true);
      if (for_debugging_) {
        DefineSafepoint(protected_load_pc);
      }
    }
    __ PushRegister(kS128, value);

    if (V8_UNLIKELY(v8_flags.trace_wasm_memory)) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      // Again load extend is different.
      MachineRepresentation mem_rep =
          transform == LoadTransformationKind::kExtend
              ? MachineRepresentation::kWord64
              : type.mem_type().representation();
      TraceMemoryOperation(false, mem_rep, index, offset, decoder->position());
    }
  }

  void LoadLane(FullDecoder* decoder, LoadType type, const Value& _value,
                const Value& _index, const MemoryAccessImmediate& imm,
                const uint8_t laneidx, Value* _result) {
    if (!CheckSupportedType(decoder, kS128, "LoadLane")) {
      return;
    }

    LiftoffRegList pinned;
    LiftoffRegister value = pinned.set(__ PopToRegister());
    LiftoffRegister full_index = __ PopToRegister();
    Register index =
        BoundsCheckMem(decoder, imm.memory, type.size(), imm.offset, full_index,
                       pinned, kDontForceCheck, kDontCheckAlignment);

    bool i64_offset = imm.memory->is_memory64;
    DCHECK_EQ(i64_offset ? kI64 : kI32, _index.type.kind());

    uintptr_t offset = imm.offset;
    pinned.set(index);
    CODE_COMMENT("load lane");
    Register addr = GetMemoryStart(imm.mem_index, pinned);
    LiftoffRegister result = __ GetUnusedRegister(reg_class_for(kS128), {});
    uint32_t protected_load_pc = 0;
    __ LoadLane(result, value, addr, index, offset, type, laneidx,
                &protected_load_pc, i64_offset);
    if (imm.memory->bounds_checks == kTrapHandler) {
      protected_instructions_.emplace_back(
          trap_handler::ProtectedInstructionData{protected_load_pc});
      source_position_table_builder_.AddPosition(
          protected_load_pc, SourcePosition(decoder->position()), true);
      if (for_debugging_) {
        DefineSafepoint(protected_load_pc);
      }
    }

    __ PushRegister(kS128, result);

    if (V8_UNLIKELY(v8_flags.trace_wasm_memory)) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      TraceMemoryOperation(false, type.mem_type().representation(), index,
                           offset, decoder->position());
    }
  }

  void StoreMem(FullDecoder* decoder, StoreType type,
                const MemoryAccessImmediate& imm, const Value& index_val,
                const Value& value_val) {
    ValueKind kind = type.value_type().kind();
    DCHECK_EQ(kind, value_val.type.kind());
    if (!CheckSupportedType(decoder, kind, "store")) return;

    LiftoffRegList pinned;
    LiftoffRegister value = pinned.set(__ PopToRegister());

    if (type.value() == StoreType::kF32StoreF16 &&
        !asm_.supports_f16_mem_access()) {
      type = StoreType::kI32Store16;
      // {value} is always a float, so can't alias with {i16}.
      DCHECK_EQ(kF32, kind);
      LiftoffRegister i16 = pinned.set(__ GetUnusedRegister(kGpReg, {}));
      auto conv_ref = ExternalReference::wasm_float32_to_float16();
      GenerateCCallWithStackBuffer(&i16, kVoid, kI16,
                                   {VarState{kF32, value, 0}}, conv_ref);
      value = i16;
    }

    uintptr_t offset = imm.offset;
    Register index = no_reg;

    auto& index_slot = __ cache_state()->stack_state.back();
    DCHECK_EQ(index_val.type.kind(), index_slot.kind());
    bool i64_offset = imm.memory->is_memory64;
    DCHECK_EQ(i64_offset ? kI64 : kI32, index_val.type.kind());
    if (IndexStaticallyInBounds(imm.memory, index_slot, type.size(), &offset)) {
      __ cache_state()->stack_state.pop_back();
      SCOPED_CODE_COMMENT("store to memory (constant offset)");
      Register mem = pinned.set(GetMemoryStart(imm.memory->index, pinned));
      __ Store(mem, no_reg, offset, value, type, pinned, nullptr, true,
               i64_offset);
    } else {
      LiftoffRegister full_index = __ PopToRegister(pinned);
      ForceCheck force_check =
          kPartialOOBWritesAreNoops ? kDontForceCheck : kDoForceCheck;
      index =
          BoundsCheckMem(decoder, imm.memory, type.size(), imm.offset,
                         full_index, pinned, force_check, kDontCheckAlignment);

      pinned.set(index);
      SCOPED_CODE_COMMENT("store to memory");
      uint32_t protected_store_pc = 0;
      // Load the memory start address only now to reduce register pressure
      // (important on ia32).
      Register mem = pinned.set(GetMemoryStart(imm.memory->index, pinned));
      LiftoffRegList outer_pinned;
      if (V8_UNLIKELY(v8_flags.trace_wasm_memory)) outer_pinned.set(index);
      __ Store(mem, index, offset, value, type, outer_pinned,
               &protected_store_pc, true, i64_offset);
      if (imm.memory->bounds_checks == kTrapHandler) {
        RegisterProtectedInstruction(decoder, protected_store_pc);
      }
    }

    if (V8_UNLIKELY(v8_flags.trace_wasm_memory)) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      TraceMemoryOperation(true, type.mem_rep(), index, offset,
                           decoder->position());
    }
  }

  void StoreLane(FullDecoder* decoder, StoreType type,
                 const MemoryAccessImmediate& imm, const Value& _index,
                 const Value& _value, const uint8_t lane) {
    if (!CheckSupportedType(decoder, kS128, "StoreLane")) return;
    LiftoffRegList pinned;
    LiftoffRegister value = pinned.set(__ PopToRegister());
    LiftoffRegister full_index = __ PopToRegister(pinned);
    ForceCheck force_check =
        kPartialOOBWritesAreNoops ? kDontForceCheck : kDoForceCheck;
    Register index =
        BoundsCheckMem(decoder, imm.memory, type.size(), imm.offset, full_index,
                       pinned, force_check, kDontCheckAlignment);

    bool i64_offset = imm.memory->is_memory64;
    DCHECK_EQ(i64_offset ? kI64 : kI32, _index.type.kind());

    uintptr_t offset = imm.offset;
    pinned.set(index);
    CODE_COMMENT("store lane to memory");
    Register addr = pinned.set(GetMemoryStart(imm.mem_index, pinned));
    uint32_t protected_store_pc = 0;
    __ StoreLane(addr, index, offset, value, type, lane, &protected_store_pc,
                 i64_offset);
    if (imm.memory->bounds_checks == kTrapHandler) {
      protected_instructions_.emplace_back(
          trap_handler::ProtectedInstructionData{protected_store_pc});
      source_position_table_builder_.AddPosition(
          protected_store_pc, SourcePosition(decoder->position()), true);
      if (for_debugging_) {
        DefineSafepoint(protected_store_pc);
      }
    }
    if (V8_UNLIKELY(v8_flags.trace_wasm_memory)) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      TraceMemoryOperation(true, type.mem_rep(), index, offset,
                           decoder->position());
    }
  }

  void CurrentMemoryPages(FullDecoder* /* decoder */,
                          const MemoryIndexImmediate& imm,
                          Value* /* result */) {
    LiftoffRegList pinned;
    LiftoffRegister mem_size = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    if (imm.memory->index == 0) {
      LOAD_INSTANCE_FIELD(mem_size.gp(), Memory0Size, kSystemPointerSize,
                          pinned);
    } else {
      LOAD_PROTECTED_PTR_INSTANCE_FIELD(mem_size.gp(), MemoryBasesAndSizes,
                                        pinned);
      int buffer_offset = wasm::ObjectAccess::ToTagged(ByteArray::kHeaderSize) +
                          kSystemPointerSize * (imm.memory->index * 2 + 1);
      __ LoadFullPointer(mem_size.gp(), mem_size.gp(), buffer_offset);
    }
    // Convert bytes to pages.
    __ emit_ptrsize_shri(mem_size.gp(), mem_size.gp(), kWasmPageSizeLog2);
    if (imm.memory->is_memory64 && kNeedI64RegPair) {
      LiftoffRegister high_word =
          __ GetUnusedRegister(kGpReg, LiftoffRegList{mem_size});
      // The high word is always 0 on 32-bit systems.
      __ LoadConstant(high_word, WasmValue{uint32_t{0}});
      mem_size = LiftoffRegister::ForPair(mem_size.gp(), high_word.gp());
    }
    __ PushRegister(imm.memory->is_memory64 ? kI64 : kI32, mem_size);
  }

  void MemoryGrow(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                  const Value& value, Value* result_val) {
    // Pop the input, then spill all cache registers to make the runtime call.
    LiftoffRegList pinned;
    LiftoffRegister num_pages = pinned.set(__ PopToRegister());
    __ SpillAllRegisters();

    LiftoffRegister result = pinned.set(__ GetUnusedRegister(kGpReg, pinned));

    Label done;

    if (imm.memory->is_memory64) {
      // If the high word is not 0, this will always fail (would grow by
      // >=256TB). The int32_t value will be sign-extended below.
      __ LoadConstant(result, WasmValue(int32_t{-1}));
      if (kNeedI64RegPair) {
        FREEZE_STATE(all_spilled_anyway);
        __ emit_cond_jump(kNotEqual, &done, kI32, num_pages.high_gp(), no_reg,
                          all_spilled_anyway);
        num_pages = num_pages.low();
      } else {
        LiftoffRegister high_word = __ GetUnusedRegister(kGpReg, pinned);
        __ emit_i64_shri(high_word, num_pages, 32);
        FREEZE_STATE(all_spilled_anyway);
        __ emit_cond_jump(kNotEqual, &done, kI32, high_word.gp(), no_reg,
                          all_spilled_anyway);
      }
    }

    WasmMemoryGrowDescriptor descriptor;
    DCHECK_EQ(0, descriptor.GetStackParameterCount());
    DCHECK_EQ(2, descriptor.GetRegisterParameterCount());
    DCHECK_EQ(machine_type(kI32), descriptor.GetParameterType(0));
    DCHECK_EQ(machine_type(kI32), descriptor.GetParameterType(1));

    Register num_pages_param_reg = descriptor.GetRegisterParameter(1);
    if (num_pages.gp() != num_pages_param_reg) {
      __ Move(num_pages_param_reg, num_pages.gp(), kI32);
    }

    // Load the constant after potentially moving the {num_pages} register to
    // avoid overwriting it.
    Register mem_index_param_reg = descriptor.GetRegisterParameter(0);
    __ LoadConstant(LiftoffRegister{mem_index_param_reg},
                    WasmValue(imm.memory->index));

    __ CallBuiltin(Builtin::kWasmMemoryGrow);
    DefineSafepoint();
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    if (kReturnRegister0 != result.gp()) {
      __ Move(result.gp(), kReturnRegister0, kI32);
    }

    __ bind(&done);

    if (imm.memory->is_memory64) {
      LiftoffRegister result64 = result;
      if (kNeedI64RegPair) result64 = __ GetUnusedRegister(kGpRegPair, pinned);
      __ emit_type_conversion(kExprI64SConvertI32, result64, result, nullptr);
      __ PushRegister(kI64, result64);
    } else {
      __ PushRegister(kI32, result);
    }
  }

  base::OwnedVector<ValueType> GetStackValueTypesForDebugging(
      FullDecoder* decoder) {
    DCHECK(for_debugging_);
    auto stack_value_types =
        base::OwnedVector<ValueType>::NewForOverwrite(decoder->stack_size());

    int depth = 0;
    for (ValueType& type : base::Reversed(stack_value_types)) {
      type = decoder->stack_value(++depth)->type;
    }
    return stack_value_types;
  }

  base::OwnedVector<DebugSideTable::Entry::Value>
  GetCurrentDebugSideTableEntries(
      FullDecoder* decoder,
      DebugSideTableBuilder::AssumeSpilling assume_spilling) {
    auto& stack_state = __ cache_state()->stack_state;

#ifdef DEBUG
    // For value types, we use the cached {stack_value_types_for_debugging_}
    // vector (gathered in {NextInstruction}). This still includes call
    // arguments, which Liftoff has already popped at this point. Hence the size
    // of this vector can be larger than the Liftoff stack size. Just ignore
    // that and use the lower part only.
    size_t expected_value_stack_size =
        stack_state.size() - num_exceptions_ - __ num_locals();
    DCHECK_LE(expected_value_stack_size,
              stack_value_types_for_debugging_.size());
#endif

    auto values =
        base::OwnedVector<DebugSideTable::Entry::Value>::NewForOverwrite(
            stack_state.size());

    int index = 0;
    ValueType* stack_value_type_ptr = stack_value_types_for_debugging_.begin();
    // Iterate the operand stack control block by control block, so that we can
    // handle the implicit exception value for try blocks.
    for (int j = decoder->control_depth() - 1; j >= 0; j--) {
      Control* control = decoder->control_at(j);
      Control* next_control = j > 0 ? decoder->control_at(j - 1) : nullptr;
      int end_index = next_control
                          ? next_control->stack_depth + __ num_locals() +
                                next_control->num_exceptions
                          : __ cache_state()->stack_height();
      bool exception_on_stack =
          control->is_try_catch() || control->is_try_catchall();
      for (; index < end_index; ++index) {
        auto& slot = stack_state[index];
        auto& value = values[index];
        value.index = index;
        if (exception_on_stack) {
          value.type = ValueType::Ref(HeapType::kAny);
          exception_on_stack = false;
        } else if (index < static_cast<int>(__ num_locals())) {
          value.type = decoder->local_type(index);
        } else {
          DCHECK_LT(stack_value_type_ptr,
                    stack_value_types_for_debugging_.end());
          value.type = *stack_value_type_ptr++;
        }
        DCHECK(CompatibleStackSlotTypes(slot.kind(), value.type.kind()));
        switch (slot.loc()) {
          case kIntConst:
            value.storage = DebugSideTable::Entry::kConstant;
            value.i32_const = slot.i32_const();
            break;
          case kRegister:
            DCHECK_NE(DebugSideTableBuilder::kDidSpill, assume_spilling);
            if (assume_spilling == DebugSideTableBuilder::kAllowRegisters) {
              value.storage = DebugSideTable::Entry::kRegister;
              value.reg_code = slot.reg().liftoff_code();
              break;
            }
            DCHECK_EQ(DebugSideTableBuilder::kAssumeSpilling, assume_spilling);
            [[fallthrough]];
          case kStack:
            value.storage = DebugSideTable::Entry::kStack;
            value.stack_offset = slot.offset();
            break;
        }
      }
    }
    DCHECK_EQ(values.size(), index);
    DCHECK_EQ(
        stack_value_types_for_debugging_.data() + expected_value_stack_size,
        stack_value_type_ptr);
    return values;
  }

  // Call this after emitting a runtime call that can show up in a stack trace
  // (e.g. because it can trap).
  void RegisterDebugSideTableEntry(
      FullDecoder* decoder,
      DebugSideTableBuilder::AssumeSpilling assume_spilling) {
    if (V8_LIKELY(!debug_sidetable_builder_)) return;
    debug_sidetable_builder_->NewEntry(
        __ pc_offset(),
        GetCurrentDebugSideTableEntries(decoder, assume_spilling).as_vector());
  }

  DebugSideTableBuilder::EntryBuilder* RegisterOOLDebugSideTableEntry(
      FullDecoder* decoder) {
    if (V8_LIKELY(!debug_sidetable_builder_)) return nullptr;
    return debug_sidetable_builder_->NewOOLEntry(
        GetCurrentDebugSideTableEntries(decoder,
                                        DebugSideTableBuilder::kAssumeSpilling)
            .as_vector());
  }

  enum TailCall : bool { kTailCall = true, kNoTailCall = false };

  void CallDirect(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[], Value[]) {
    CallDirect(decoder, imm, args, nullptr, kNoTailCall);
  }

  void CallIndirect(FullDecoder* decoder, const Value& index_val,
                    const CallIndirectImmediate& imm, const Value args[],
                    Value returns[]) {
    CallIndirectImpl(decoder, imm, kNoTailCall);
  }

  void CallRef(FullDecoder* decoder, const Value& func_ref,
               const FunctionSig* sig, const Value args[], Value returns[]) {
    CallRefImpl(decoder, func_ref.type, sig, kNoTailCall);
  }

  void ReturnCall(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[]) {
    TierupCheckOnTailCall(decoder);
    CallDirect(decoder, imm, args, nullptr, kTailCall);
  }

  void ReturnCallIndirect(FullDecoder* decoder, const Value& index_val,
                          const CallIndirectImmediate& imm,
                          const Value args[]) {
    TierupCheckOnTailCall(decoder);
    CallIndirectImpl(decoder, imm, kTailCall);
  }

  void ReturnCallRef(FullDecoder* decoder, const Value& func_ref,
                     const FunctionSig* sig, const Value args[]) {
    TierupCheckOnTailCall(decoder);
    CallRefImpl(decoder, func_ref.type, sig, kTailCall);
  }

  void BrOnNull(FullDecoder* decoder, const Value& ref_object, uint32_t depth,
                bool pass_null_along_branch,
                Value* /* result_on_fallthrough */) {
    // Avoid having sequences of branches do duplicate work.
    if (depth != decoder->control_depth() - 1) {
      __ PrepareForBranch(decoder->control_at(depth)->br_merge()->arity, {});
    }

    Label cont_false;
    LiftoffRegList pinned;
    LiftoffRegister ref =
        pinned.set(pass_null_along_branch ? __ PeekToRegister(0, pinned)
                                          : __ PopToRegister(pinned));
    Register null = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    LoadNullValueForCompare(null, pinned, ref_object.type);
    {
      FREEZE_STATE(frozen);
      __ emit_cond_jump(kNotEqual, &cont_false, ref_object.type.kind(),
                        ref.gp(), null, frozen);
      BrOrRet(decoder, depth);
    }
    __ bind(&cont_false);
    if (!pass_null_along_branch) {
      // We popped the value earlier, must push it back now.
      __ PushRegister(kRef, ref);
    }
  }

  void BrOnNonNull(FullDecoder* decoder, const Value& ref_object,
                   Value* /* result */, uint32_t depth,
                   bool drop_null_on_fallthrough) {
    // Avoid having sequences of branches do duplicate work.
    if (depth != decoder->control_depth() - 1) {
      __ PrepareForBranch(decoder->control_at(depth)->br_merge()->arity, {});
    }

    Label cont_false;
    LiftoffRegList pinned;
    LiftoffRegister ref = pinned.set(__ PeekToRegister(0, pinned));

    Register null = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    LoadNullValueForCompare(null, pinned, ref_object.type);
    {
      FREEZE_STATE(frozen);
      __ emit_cond_jump(kEqual, &cont_false, ref_object.type.kind(), ref.gp(),
                        null, frozen);

      BrOrRet(decoder, depth);
    }
    // Drop the reference if we are not branching.
    if (drop_null_on_fallthrough) __ DropValues(1);
    __ bind(&cont_false);
  }

  template <ValueKind src_kind, ValueKind result_kind,
            ValueKind result_lane_kind = kVoid, typename EmitFn,
            typename... ExtraArgs>
  void EmitTerOp(EmitFn fn, LiftoffRegister dst, LiftoffRegister src1,
                 LiftoffRegister src2, LiftoffRegister src3,
                 ExtraArgs... extra_args) {
    CallEmitFn(fn, dst, src1, src2, src3, extra_args...);
    if (V8_UNLIKELY(nondeterminism_)) {
      LiftoffRegList pinned{dst};
      if (result_kind == ValueKind::kF32 || result_kind == ValueKind::kF64) {
        CheckNan(dst, pinned, result_kind);
      } else if (result_kind == ValueKind::kS128 &&
                 (result_lane_kind == kF32 || result_lane_kind == kF64)) {
        CheckS128Nan(dst, LiftoffRegList{src1, src2, src3, dst},
                     result_lane_kind);
      }
    }
    __ PushRegister(result_kind, dst);
  }

  template <ValueKind src_kind, ValueKind result_kind,
            ValueKind result_lane_kind = kVoid, typename EmitFn>
  void EmitTerOp(EmitFn fn) {
    LiftoffRegister src3 = __ PopToRegister();
    LiftoffRegister src2 = __ PopToRegister(LiftoffRegList{src3});
    LiftoffRegister src1 = __ PopToRegister(LiftoffRegList{src3, src2});
    static constexpr RegClass result_rc = reg_class_for(result_kind);
    // Reusing src1 and src2 will complicate codegen for select for some
    // backend, so we allow only reusing src3 (the mask), and pin src1 and src2.
    // Additionally, only reuse src3 if it does not alias src1/src2,
    // otherwise dst will also alias it src1/src2.
    LiftoffRegister dst =
        (src2 == src3 || src1 == src3)
            ? __ GetUnusedRegister(result_rc, LiftoffRegList{src1, src2})
            : __ GetUnusedRegister(result_rc, {src3},
                                   LiftoffRegList{src1, src2});
    EmitTerOp<src_kind, result_kind, result_lane_kind, EmitFn>(fn, dst, src1,
                                                               src2, src3);
  }

  void EmitRelaxedLaneSelect(int lane_width) {
    DCHECK(lane_width == 8 || lane_width == 32 || lane_width == 64);
#if defined(V8_TARGET_ARCH_IA32) || defined(V8_TARGET_ARCH_X64)
    if (!CpuFeatures::IsSupported(AVX)) {
#if defined(V8_TARGET_ARCH_IA32)
      // On ia32 xmm0 is not a cached register.
      LiftoffRegister mask = LiftoffRegister::from_uncached(xmm0);
#else
      LiftoffRegister mask(xmm0);
#endif
      __ PopToFixedRegister(mask);
      LiftoffRegister src2 = __ PopToModifiableRegister(LiftoffRegList{mask});
      LiftoffRegister src1 = __ PopToRegister(LiftoffRegList{src2, mask});
      EmitTerOp<kS128, kS128>(&LiftoffAssembler::emit_s128_relaxed_laneselect,
                              src2, src1, src2, mask, lane_width);
      return;
    }
#endif
    LiftoffRegList pinned;
    LiftoffRegister mask = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister src2 = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister src1 = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister dst =
        __ GetUnusedRegister(reg_class_for(kS128), {}, pinned);
    EmitTerOp<kS128, kS128>(&LiftoffAssembler::emit_s128_relaxed_laneselect,
                            dst, src1, src2, mask, lane_width);
  }

  template <typename EmitFn, typename EmitFnImm>
  void EmitSimdShiftOp(EmitFn fn, EmitFnImm fnImm) {
    static constexpr RegClass result_rc = reg_class_for(kS128);

    VarState rhs_slot = __ cache_state()->stack_state.back();
    // Check if the RHS is an immediate.
    if (rhs_slot.is_const()) {
      __ cache_state()->stack_state.pop_back();
      int32_t imm = rhs_slot.i32_const();

      LiftoffRegister operand = __ PopToRegister();
      LiftoffRegister dst = __ GetUnusedRegister(result_rc, {operand}, {});

      CallEmitFn(fnImm, dst, operand, imm);
      __ PushRegister(kS128, dst);
    } else {
      LiftoffRegister count = __ PopToRegister();
      LiftoffRegister operand = __ PopToRegister();
      LiftoffRegister dst = __ GetUnusedRegister(result_rc, {operand}, {});

      CallEmitFn(fn, dst, operand, count);
      __ PushRegister(kS128, dst);
    }
  }

  template <ValueKind result_lane_kind>
  void EmitSimdFloatRoundingOpWithCFallback(
      bool (LiftoffAssembler::*emit_fn)(LiftoffRegister, LiftoffRegister),
      ExternalReference (*ext_ref)()) {
    static constexpr RegClass rc = reg_class_for(kS128);
    LiftoffRegister src = __ PopToRegister();
    LiftoffRegister dst = __ GetUnusedRegister(rc, {src}, {});
    if (!(asm_.*emit_fn)(dst, src)) {
      // Return v128 via stack for ARM.
      GenerateCCallWithStackBuffer(&dst, kVoid, kS128,
                                   {VarState{kS128, src, 0}}, ext_ref());
    }
    if (V8_UNLIKELY(nondeterminism_)) {
      LiftoffRegList pinned{dst};
      CheckS128Nan(dst, pinned, result_lane_kind);
    }
    __ PushRegister(kS128, dst);
  }

  template <ValueKind result_lane_kind, bool swap_lhs_rhs = false>
  void EmitSimdFloatBinOpWithCFallback(
      bool (LiftoffAssembler::*emit_fn)(LiftoffRegister, LiftoffRegister,
                                        LiftoffRegister),
      ExternalReference (*ext_ref)()) {
    static constexpr RegClass rc = reg_class_for(kS128);
    LiftoffRegister src2 = __ PopToRegister();
    LiftoffRegister src1 = __ PopToRegister(LiftoffRegList{src2});
    LiftoffRegister dst = __ GetUnusedRegister(rc, {src1, src2}, {});

    if (swap_lhs_rhs) std::swap(src1, src2);

    if (!(asm_.*emit_fn)(dst, src1, src2)) {
      // Return v128 via stack for ARM.
      GenerateCCallWithStackBuffer(
          &dst, kVoid, kS128,
          {VarState{kS128, src1, 0}, VarState{kS128, src2, 0}}, ext_ref());
    }
    if (V8_UNLIKELY(nondeterminism_)) {
      LiftoffRegList pinned{dst};
      CheckS128Nan(dst, pinned, result_lane_kind);
    }
    __ PushRegister(kS128, dst);
  }

  template <ValueKind result_lane_kind, typename EmitFn>
  void EmitSimdFmaOp(EmitFn emit_fn) {
    LiftoffRegList pinned;
    LiftoffRegister src3 = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister src2 = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister src1 = pinned.set(__ PopToRegister(pinned));
    RegClass dst_rc = reg_class_for(kS128);
    LiftoffRegister dst = __ GetUnusedRegister(dst_rc, {});
    (asm_.*emit_fn)(dst, src1, src2, src3);
    if (V8_UNLIKELY(nondeterminism_)) {
      LiftoffRegList pinned{dst};
      CheckS128Nan(dst, pinned, result_lane_kind);
    }
    __ PushRegister(kS128, dst);
  }

  template <ValueKind result_lane_kind, typename EmitFn>
  void EmitSimdFmaOpWithCFallback(EmitFn emit_fn,
                                  ExternalReference (*ext_ref)()) {
    LiftoffRegList pinned;
    LiftoffRegister src3 = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister src2 = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister src1 = pinned.set(__ PopToRegister(pinned));
    static constexpr RegClass dst_rc = reg_class_for(kS128);
    LiftoffRegister dst = __ GetUnusedRegister(dst_rc, {});
    if (!(asm_.*emit_fn)(dst, src1, src2, src3)) {
      // Return v128 via stack for ARM.
      GenerateCCallWithStackBuffer(
          &dst, kVoid, kS128,
          {VarState{kS128, src1, 0}, VarState{kS128, src2, 0},
           VarState{kS128, src3, 0}},
          ext_ref());
    }
    if (V8_UNLIKELY(nondeterminism_)) {
      LiftoffRegList pinned{dst};
      CheckS128Nan(dst, pinned, result_lane_kind);
    }
    __ PushRegister(kS128, dst);
  }

  void SimdOp(FullDecoder* decoder, WasmOpcode opcode, const Value* /* args */,
              Value* /* result */) {
    CHECK(CpuFeatures::SupportsWasmSimd128());
    switch (opcode) {
      case wasm::kExprI8x16Swizzle:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_swizzle);
      case wasm::kExprI8x16RelaxedSwizzle:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i8x16_relaxed_swizzle);
      case wasm::kExprI8x16Popcnt:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_popcnt);
      case wasm::kExprI8x16Splat:
        return EmitUnOp<kI32, kS128>(&LiftoffAssembler::emit_i8x16_splat);
      case wasm::kExprI16x8Splat:
        return EmitUnOp<kI32, kS128>(&LiftoffAssembler::emit_i16x8_splat);
      case wasm::kExprI32x4Splat:
        return EmitUnOp<kI32, kS128>(&LiftoffAssembler::emit_i32x4_splat);
      case wasm::kExprI64x2Splat:
        return EmitUnOp<kI64, kS128>(&LiftoffAssembler::emit_i64x2_splat);
      case wasm::kExprF16x8Splat: {
        auto emit_with_c_fallback = [this](LiftoffRegister dst,
                                           LiftoffRegister src) {
          if (asm_.emit_f16x8_splat(dst, src)) return;
          LiftoffRegister value = __ GetUnusedRegister(kGpReg, {});
          auto conv_ref = ExternalReference::wasm_float32_to_float16();
          GenerateCCallWithStackBuffer(&value, kVoid, kI16,
                                       {VarState{kF32, src, 0}}, conv_ref);
          __ emit_i16x8_splat(dst, value);
        };
        return EmitUnOp<kF32, kS128>(emit_with_c_fallback);
      }
      case wasm::kExprF32x4Splat:
        return EmitUnOp<kF32, kS128, kF32>(&LiftoffAssembler::emit_f32x4_splat);
      case wasm::kExprF64x2Splat:
        return EmitUnOp<kF64, kS128, kF64>(&LiftoffAssembler::emit_f64x2_splat);
      case wasm::kExprI8x16Eq:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_eq);
      case wasm::kExprI8x16Ne:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_ne);
      case wasm::kExprI8x16LtS:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i8x16_gt_s);
      case wasm::kExprI8x16LtU:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i8x16_gt_u);
      case wasm::kExprI8x16GtS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_gt_s);
      case wasm::kExprI8x16GtU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_gt_u);
      case wasm::kExprI8x16LeS:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i8x16_ge_s);
      case wasm::kExprI8x16LeU:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i8x16_ge_u);
      case wasm::kExprI8x16GeS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_ge_s);
      case wasm::kExprI8x16GeU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_ge_u);
      case wasm::kExprI16x8Eq:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_eq);
      case wasm::kExprI16x8Ne:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_ne);
      case wasm::kExprI16x8LtS:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i16x8_gt_s);
      case wasm::kExprI16x8LtU:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i16x8_gt_u);
      case wasm::kExprI16x8GtS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_gt_s);
      case wasm::kExprI16x8GtU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_gt_u);
      case wasm::kExprI16x8LeS:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i16x8_ge_s);
      case wasm::kExprI16x8LeU:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i16x8_ge_u);
      case wasm::kExprI16x8GeS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_ge_s);
      case wasm::kExprI16x8GeU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_ge_u);
      case wasm::kExprI32x4Eq:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_eq);
      case wasm::kExprI32x4Ne:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_ne);
      case wasm::kExprI32x4LtS:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i32x4_gt_s);
      case wasm::kExprI32x4LtU:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i32x4_gt_u);
      case wasm::kExprI32x4GtS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_gt_s);
      case wasm::kExprI32x4GtU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_gt_u);
      case wasm::kExprI32x4LeS:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i32x4_ge_s);
      case wasm::kExprI32x4LeU:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i32x4_ge_u);
      case wasm::kExprI32x4GeS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_ge_s);
      case wasm::kExprI32x4GeU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_ge_u);
      case wasm::kExprI64x2Eq:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_eq);
      case wasm::kExprI64x2Ne:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_ne);
      case wasm::kExprI64x2LtS:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i64x2_gt_s);
      case wasm::kExprI64x2GtS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_gt_s);
      case wasm::kExprI64x2LeS:
        return EmitBinOp<kS128, kS128, true>(
            &LiftoffAssembler::emit_i64x2_ge_s);
      case wasm::kExprI64x2GeS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_ge_s);
      case wasm::kExprF16x8Eq:
        return EmitSimdFloatBinOpWithCFallback<kI16>(
            &LiftoffAssembler::emit_f16x8_eq, ExternalReference::wasm_f16x8_eq);
      case wasm::kExprF16x8Ne:
        return EmitSimdFloatBinOpWithCFallback<kI16>(
            &LiftoffAssembler::emit_f16x8_ne, ExternalReference::wasm_f16x8_ne);
      case wasm::kExprF16x8Lt:
        return EmitSimdFloatBinOpWithCFallback<kI16>(
            &LiftoffAssembler::emit_f16x8_lt, ExternalReference::wasm_f16x8_lt);
      case wasm::kExprF16x8Gt:
        return EmitSimdFloatBinOpWithCFallback<kI16, true>(
            &LiftoffAssembler::emit_f16x8_lt, ExternalReference::wasm_f16x8_lt);
      case wasm::kExprF16x8Le:
        return EmitSimdFloatBinOpWithCFallback<kI16>(
            &LiftoffAssembler::emit_f16x8_le, ExternalReference::wasm_f16x8_le);
      case wasm::kExprF16x8Ge:
        return EmitSimdFloatBinOpWithCFallback<kI16, true>(
            &LiftoffAssembler::emit_f16x8_le, ExternalReference::wasm_f16x8_le);
      case wasm::kExprF32x4Eq:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_f32x4_eq);
      case wasm::kExprF32x4Ne:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_f32x4_ne);
      case wasm::kExprF32x4Lt:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_f32x4_lt);
      case wasm::kExprF32x4Gt:
        return EmitBinOp<kS128, kS128, true>(&LiftoffAssembler::emit_f32x4_lt);
      case wasm::kExprF32x4Le:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_f32x4_le);
      case wasm::kExprF32x4Ge:
        return EmitBinOp<kS128, kS128, true>(&LiftoffAssembler::emit_f32x4_le);
      case wasm::kExprF64x2Eq:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_f64x2_eq);
      case wasm::kExprF64x2Ne:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_f64x2_ne);
      case wasm::kExprF64x2Lt:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_f64x2_lt);
      case wasm::kExprF64x2Gt:
        return EmitBinOp<kS128, kS128, true>(&LiftoffAssembler::emit_f64x2_lt);
      case wasm::kExprF64x2Le:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_f64x2_le);
      case wasm::kExprF64x2Ge:
        return EmitBinOp<kS128, kS128, true>(&LiftoffAssembler::emit_f64x2_le);
      case wasm::kExprS128Not:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_s128_not);
      case wasm::kExprS128And:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_s128_and);
      case wasm::kExprS128Or:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_s128_or);
      case wasm::kExprS128Xor:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_s128_xor);
      case wasm::kExprS128Select:
        return EmitTerOp<kS128, kS128>(&LiftoffAssembler::emit_s128_select);
      case wasm::kExprI8x16Neg:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_neg);
      case wasm::kExprV128AnyTrue:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_v128_anytrue);
      case wasm::kExprI8x16AllTrue:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_i8x16_alltrue);
      case wasm::kExprI8x16BitMask:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_i8x16_bitmask);
      case wasm::kExprI8x16Shl:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i8x16_shl,
                               &LiftoffAssembler::emit_i8x16_shli);
      case wasm::kExprI8x16ShrS:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i8x16_shr_s,
                               &LiftoffAssembler::emit_i8x16_shri_s);
      case wasm::kExprI8x16ShrU:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i8x16_shr_u,
                               &LiftoffAssembler::emit_i8x16_shri_u);
      case wasm::kExprI8x16Add:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_add);
      case wasm::kExprI8x16AddSatS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_add_sat_s);
      case wasm::kExprI8x16AddSatU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_add_sat_u);
      case wasm::kExprI8x16Sub:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_sub);
      case wasm::kExprI8x16SubSatS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_sub_sat_s);
      case wasm::kExprI8x16SubSatU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_sub_sat_u);
      case wasm::kExprI8x16MinS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_min_s);
      case wasm::kExprI8x16MinU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_min_u);
      case wasm::kExprI8x16MaxS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_max_s);
      case wasm::kExprI8x16MaxU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_max_u);
      case wasm::kExprI16x8Neg:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_neg);
      case wasm::kExprI16x8AllTrue:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_i16x8_alltrue);
      case wasm::kExprI16x8BitMask:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_i16x8_bitmask);
      case wasm::kExprI16x8Shl:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i16x8_shl,
                               &LiftoffAssembler::emit_i16x8_shli);
      case wasm::kExprI16x8ShrS:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i16x8_shr_s,
                               &LiftoffAssembler::emit_i16x8_shri_s);
      case wasm::kExprI16x8ShrU:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i16x8_shr_u,
                               &LiftoffAssembler::emit_i16x8_shri_u);
      case wasm::kExprI16x8Add:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_add);
      case wasm::kExprI16x8AddSatS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_add_sat_s);
      case wasm::kExprI16x8AddSatU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_add_sat_u);
      case wasm::kExprI16x8Sub:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_sub);
      case wasm::kExprI16x8SubSatS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_sub_sat_s);
      case wasm::kExprI16x8SubSatU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_sub_sat_u);
      case wasm::kExprI16x8Mul:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_mul);
      case wasm::kExprI16x8MinS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_min_s);
      case wasm::kExprI16x8MinU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_min_u);
      case wasm::kExprI16x8MaxS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_max_s);
      case wasm::kExprI16x8MaxU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_max_u);
      case wasm::kExprI16x8ExtAddPairwiseI8x16S:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_extadd_pairwise_i8x16_s);
      case wasm::kExprI16x8ExtAddPairwiseI8x16U:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_extadd_pairwise_i8x16_u);
      case wasm::kExprI16x8ExtMulLowI8x16S:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_extmul_low_i8x16_s);
      case wasm::kExprI16x8ExtMulLowI8x16U:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_extmul_low_i8x16_u);
      case wasm::kExprI16x8ExtMulHighI8x16S:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_extmul_high_i8x16_s);
      case wasm::kExprI16x8ExtMulHighI8x16U:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_extmul_high_i8x16_u);
      case wasm::kExprI16x8Q15MulRSatS:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_q15mulr_sat_s);
      case wasm::kExprI32x4Neg:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_neg);
      case wasm::kExprI32x4AllTrue:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_i32x4_alltrue);
      case wasm::kExprI32x4BitMask:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_i32x4_bitmask);
      case wasm::kExprI32x4Shl:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i32x4_shl,
                               &LiftoffAssembler::emit_i32x4_shli);
      case wasm::kExprI32x4ShrS:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i32x4_shr_s,
                               &LiftoffAssembler::emit_i32x4_shri_s);
      case wasm::kExprI32x4ShrU:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i32x4_shr_u,
                               &LiftoffAssembler::emit_i32x4_shri_u);
      case wasm::kExprI32x4Add:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_add);
      case wasm::kExprI32x4Sub:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_sub);
      case wasm::kExprI32x4Mul:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_mul);
      case wasm::kExprI32x4MinS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_min_s);
      case wasm::kExprI32x4MinU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_min_u);
      case wasm::kExprI32x4MaxS:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_max_s);
      case wasm::kExprI32x4MaxU:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_max_u);
      case wasm::kExprI32x4DotI16x8S:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_dot_i16x8_s);
      case wasm::kExprI32x4ExtAddPairwiseI16x8S:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_extadd_pairwise_i16x8_s);
      case wasm::kExprI32x4ExtAddPairwiseI16x8U:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_extadd_pairwise_i16x8_u);
      case wasm::kExprI32x4ExtMulLowI16x8S:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_extmul_low_i16x8_s);
      case wasm::kExprI32x4ExtMulLowI16x8U:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_extmul_low_i16x8_u);
      case wasm::kExprI32x4ExtMulHighI16x8S:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_extmul_high_i16x8_s);
      case wasm::kExprI32x4ExtMulHighI16x8U:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_extmul_high_i16x8_u);
      case wasm::kExprI64x2Neg:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_neg);
      case wasm::kExprI64x2AllTrue:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_i64x2_alltrue);
      case wasm::kExprI64x2Shl:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i64x2_shl,
                               &LiftoffAssembler::emit_i64x2_shli);
      case wasm::kExprI64x2ShrS:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i64x2_shr_s,
                               &LiftoffAssembler::emit_i64x2_shri_s);
      case wasm::kExprI64x2ShrU:
        return EmitSimdShiftOp(&LiftoffAssembler::emit_i64x2_shr_u,
                               &LiftoffAssembler::emit_i64x2_shri_u);
      case wasm::kExprI64x2Add:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_add);
      case wasm::kExprI64x2Sub:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_sub);
      case wasm::kExprI64x2Mul:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_mul);
      case wasm::kExprI64x2ExtMulLowI32x4S:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i64x2_extmul_low_i32x4_s);
      case wasm::kExprI64x2ExtMulLowI32x4U:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i64x2_extmul_low_i32x4_u);
      case wasm::kExprI64x2ExtMulHighI32x4S:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i64x2_extmul_high_i32x4_s);
      case wasm::kExprI64x2ExtMulHighI32x4U:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i64x2_extmul_high_i32x4_u);
      case wasm::kExprI64x2BitMask:
        return EmitUnOp<kS128, kI32>(&LiftoffAssembler::emit_i64x2_bitmask);
      case wasm::kExprI64x2SConvertI32x4Low:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i64x2_sconvert_i32x4_low);
      case wasm::kExprI64x2SConvertI32x4High:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i64x2_sconvert_i32x4_high);
      case wasm::kExprI64x2UConvertI32x4Low:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i64x2_uconvert_i32x4_low);
      case wasm::kExprI64x2UConvertI32x4High:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i64x2_uconvert_i32x4_high);
      case wasm::kExprF16x8Abs:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_abs,
            &ExternalReference::wasm_f16x8_abs);
      case wasm::kExprF16x8Neg:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_neg,
            &ExternalReference::wasm_f16x8_neg);
      case wasm::kExprF16x8Sqrt:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_sqrt,
            &ExternalReference::wasm_f16x8_sqrt);
      case wasm::kExprF16x8Ceil:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_ceil,
            &ExternalReference::wasm_f16x8_ceil);
      case wasm::kExprF16x8Floor:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_floor,
            ExternalReference::wasm_f16x8_floor);
      case wasm::kExprF16x8Trunc:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_trunc,
            ExternalReference::wasm_f16x8_trunc);
      case wasm::kExprF16x8NearestInt:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_nearest_int,
            ExternalReference::wasm_f16x8_nearest_int);
      case wasm::kExprF16x8Add:
        return EmitSimdFloatBinOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_add,
            ExternalReference::wasm_f16x8_add);
      case wasm::kExprF16x8Sub:
        return EmitSimdFloatBinOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_sub,
            ExternalReference::wasm_f16x8_sub);
      case wasm::kExprF16x8Mul:
        return EmitSimdFloatBinOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_mul,
            ExternalReference::wasm_f16x8_mul);
      case wasm::kExprF16x8Div:
        return EmitSimdFloatBinOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_div,
            ExternalReference::wasm_f16x8_div);
      case wasm::kExprF16x8Min:
        return EmitSimdFloatBinOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_min,
            ExternalReference::wasm_f16x8_min);
      case wasm::kExprF16x8Max:
        return EmitSimdFloatBinOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_max,
            ExternalReference::wasm_f16x8_max);
      case wasm::kExprF16x8Pmin:
        return EmitSimdFloatBinOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_pmin,
            ExternalReference::wasm_f16x8_pmin);
      case wasm::kExprF16x8Pmax:
        return EmitSimdFloatBinOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_pmax,
            ExternalReference::wasm_f16x8_pmax);
      case wasm::kExprF32x4Abs:
        return EmitUnOp<kS128, kS128, kF32>(&LiftoffAssembler::emit_f32x4_abs);
      case wasm::kExprF32x4Neg:
        return EmitUnOp<kS128, kS128, kF32>(&LiftoffAssembler::emit_f32x4_neg);
      case wasm::kExprF32x4Sqrt:
        return EmitUnOp<kS128, kS128, kF32>(&LiftoffAssembler::emit_f32x4_sqrt);
      case wasm::kExprF32x4Ceil:
        return EmitSimdFloatRoundingOpWithCFallback<kF32>(
            &LiftoffAssembler::emit_f32x4_ceil,
            &ExternalReference::wasm_f32x4_ceil);
      case wasm::kExprF32x4Floor:
        return EmitSimdFloatRoundingOpWithCFallback<kF32>(
            &LiftoffAssembler::emit_f32x4_floor,
            ExternalReference::wasm_f32x4_floor);
      case wasm::kExprF32x4Trunc:
        return EmitSimdFloatRoundingOpWithCFallback<kF32>(
            &LiftoffAssembler::emit_f32x4_trunc,
            ExternalReference::wasm_f32x4_trunc);
      case wasm::kExprF32x4NearestInt:
        return EmitSimdFloatRoundingOpWithCFallback<kF32>(
            &LiftoffAssembler::emit_f32x4_nearest_int,
            ExternalReference::wasm_f32x4_nearest_int);
      case wasm::kExprF32x4Add:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_add);
      case wasm::kExprF32x4Sub:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_sub);
      case wasm::kExprF32x4Mul:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_mul);
      case wasm::kExprF32x4Div:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_div);
      case wasm::kExprF32x4Min:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_min);
      case wasm::kExprF32x4Max:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_max);
      case wasm::kExprF32x4Pmin:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_pmin);
      case wasm::kExprF32x4Pmax:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_pmax);
      case wasm::kExprF64x2Abs:
        return EmitUnOp<kS128, kS128, kF64>(&LiftoffAssembler::emit_f64x2_abs);
      case wasm::kExprF64x2Neg:
        return EmitUnOp<kS128, kS128, kF64>(&LiftoffAssembler::emit_f64x2_neg);
      case wasm::kExprF64x2Sqrt:
        return EmitUnOp<kS128, kS128, kF64>(&LiftoffAssembler::emit_f64x2_sqrt);
      case wasm::kExprF64x2Ceil:
        return EmitSimdFloatRoundingOpWithCFallback<kF64>(
            &LiftoffAssembler::emit_f64x2_ceil,
            &ExternalReference::wasm_f64x2_ceil);
      case wasm::kExprF64x2Floor:
        return EmitSimdFloatRoundingOpWithCFallback<kF64>(
            &LiftoffAssembler::emit_f64x2_floor,
            ExternalReference::wasm_f64x2_floor);
      case wasm::kExprF64x2Trunc:
        return EmitSimdFloatRoundingOpWithCFallback<kF64>(
            &LiftoffAssembler::emit_f64x2_trunc,
            ExternalReference::wasm_f64x2_trunc);
      case wasm::kExprF64x2NearestInt:
        return EmitSimdFloatRoundingOpWithCFallback<kF64>(
            &LiftoffAssembler::emit_f64x2_nearest_int,
            ExternalReference::wasm_f64x2_nearest_int);
      case wasm::kExprF64x2Add:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_add);
      case wasm::kExprF64x2Sub:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_sub);
      case wasm::kExprF64x2Mul:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_mul);
      case wasm::kExprF64x2Div:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_div);
      case wasm::kExprF64x2Min:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_min);
      case wasm::kExprF64x2Max:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_max);
      case wasm::kExprF64x2Pmin:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_pmin);
      case wasm::kExprF64x2Pmax:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_pmax);
      case wasm::kExprI32x4SConvertF32x4:
        return EmitUnOp<kS128, kS128, kF32>(
            &LiftoffAssembler::emit_i32x4_sconvert_f32x4);
      case wasm::kExprI32x4UConvertF32x4:
        return EmitUnOp<kS128, kS128, kF32>(
            &LiftoffAssembler::emit_i32x4_uconvert_f32x4);
      case wasm::kExprF32x4SConvertI32x4:
        return EmitUnOp<kS128, kS128, kF32>(
            &LiftoffAssembler::emit_f32x4_sconvert_i32x4);
      case wasm::kExprF32x4UConvertI32x4:
        return EmitUnOp<kS128, kS128, kF32>(
            &LiftoffAssembler::emit_f32x4_uconvert_i32x4);
      case wasm::kExprF32x4PromoteLowF16x8:
        return EmitSimdFloatRoundingOpWithCFallback<kF32>(
            &LiftoffAssembler::emit_f32x4_promote_low_f16x8,
            &ExternalReference::wasm_f32x4_promote_low_f16x8);
      case wasm::kExprF16x8DemoteF32x4Zero:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_demote_f32x4_zero,
            &ExternalReference::wasm_f16x8_demote_f32x4_zero);
      case wasm::kExprF16x8DemoteF64x2Zero:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_demote_f64x2_zero,
            &ExternalReference::wasm_f16x8_demote_f64x2_zero);
      case wasm::kExprI16x8SConvertF16x8:
        return EmitSimdFloatRoundingOpWithCFallback<kI16>(
            &LiftoffAssembler::emit_i16x8_sconvert_f16x8,
            &ExternalReference::wasm_i16x8_sconvert_f16x8);
      case wasm::kExprI16x8UConvertF16x8:
        return EmitSimdFloatRoundingOpWithCFallback<kI16>(
            &LiftoffAssembler::emit_i16x8_uconvert_f16x8,
            &ExternalReference::wasm_i16x8_uconvert_f16x8);
      case wasm::kExprF16x8SConvertI16x8:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_sconvert_i16x8,
            &ExternalReference::wasm_f16x8_sconvert_i16x8);
      case wasm::kExprF16x8UConvertI16x8:
        return EmitSimdFloatRoundingOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_uconvert_i16x8,
            &ExternalReference::wasm_f16x8_uconvert_i16x8);
      case wasm::kExprI8x16SConvertI16x8:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i8x16_sconvert_i16x8);
      case wasm::kExprI8x16UConvertI16x8:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i8x16_uconvert_i16x8);
      case wasm::kExprI16x8SConvertI32x4:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_sconvert_i32x4);
      case wasm::kExprI16x8UConvertI32x4:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_uconvert_i32x4);
      case wasm::kExprI16x8SConvertI8x16Low:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_sconvert_i8x16_low);
      case wasm::kExprI16x8SConvertI8x16High:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_sconvert_i8x16_high);
      case wasm::kExprI16x8UConvertI8x16Low:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_uconvert_i8x16_low);
      case wasm::kExprI16x8UConvertI8x16High:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_uconvert_i8x16_high);
      case wasm::kExprI32x4SConvertI16x8Low:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_sconvert_i16x8_low);
      case wasm::kExprI32x4SConvertI16x8High:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_sconvert_i16x8_high);
      case wasm::kExprI32x4UConvertI16x8Low:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_uconvert_i16x8_low);
      case wasm::kExprI32x4UConvertI16x8High:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_uconvert_i16x8_high);
      case wasm::kExprS128AndNot:
        return EmitBinOp<kS128, kS128>(&LiftoffAssembler::emit_s128_and_not);
      case wasm::kExprI8x16RoundingAverageU:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i8x16_rounding_average_u);
      case wasm::kExprI16x8RoundingAverageU:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_rounding_average_u);
      case wasm::kExprI8x16Abs:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i8x16_abs);
      case wasm::kExprI16x8Abs:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i16x8_abs);
      case wasm::kExprI32x4Abs:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i32x4_abs);
      case wasm::kExprI64x2Abs:
        return EmitUnOp<kS128, kS128>(&LiftoffAssembler::emit_i64x2_abs);
      case wasm::kExprF64x2ConvertLowI32x4S:
        return EmitUnOp<kS128, kS128, kF64>(
            &LiftoffAssembler::emit_f64x2_convert_low_i32x4_s);
      case wasm::kExprF64x2ConvertLowI32x4U:
        return EmitUnOp<kS128, kS128, kF64>(
            &LiftoffAssembler::emit_f64x2_convert_low_i32x4_u);
      case wasm::kExprF64x2PromoteLowF32x4:
        return EmitUnOp<kS128, kS128, kF64>(
            &LiftoffAssembler::emit_f64x2_promote_low_f32x4);
      case wasm::kExprF32x4DemoteF64x2Zero:
        return EmitUnOp<kS128, kS128, kF32>(
            &LiftoffAssembler::emit_f32x4_demote_f64x2_zero);
      case wasm::kExprI32x4TruncSatF64x2SZero:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_s_zero);
      case wasm::kExprI32x4TruncSatF64x2UZero:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_u_zero);
      case wasm::kExprF16x8Qfma:
        return EmitSimdFmaOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_qfma,
            &ExternalReference::wasm_f16x8_qfma);
      case wasm::kExprF16x8Qfms:
        return EmitSimdFmaOpWithCFallback<kF16>(
            &LiftoffAssembler::emit_f16x8_qfms,
            &ExternalReference::wasm_f16x8_qfms);
      case wasm::kExprF32x4Qfma:
        return EmitSimdFmaOp<kF32>(&LiftoffAssembler::emit_f32x4_qfma);
      case wasm::kExprF32x4Qfms:
        return EmitSimdFmaOp<kF32>(&LiftoffAssembler::emit_f32x4_qfms);
      case wasm::kExprF64x2Qfma:
        return EmitSimdFmaOp<kF64>(&LiftoffAssembler::emit_f64x2_qfma);
      case wasm::kExprF64x2Qfms:
        return EmitSimdFmaOp<kF64>(&LiftoffAssembler::emit_f64x2_qfms);
      case wasm::kExprI16x8RelaxedLaneSelect:
      case wasm::kExprI8x16RelaxedLaneSelect:
        // There is no special hardware instruction for 16-bit wide lanes on
        // any of our platforms, so fall back to bytewise selection for i16x8.
        return EmitRelaxedLaneSelect(8);
      case wasm::kExprI32x4RelaxedLaneSelect:
        return EmitRelaxedLaneSelect(32);
      case wasm::kExprI64x2RelaxedLaneSelect:
        return EmitRelaxedLaneSelect(64);
      case wasm::kExprF32x4RelaxedMin:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_relaxed_min);
      case wasm::kExprF32x4RelaxedMax:
        return EmitBinOp<kS128, kS128, false, kF32>(
            &LiftoffAssembler::emit_f32x4_relaxed_max);
      case wasm::kExprF64x2RelaxedMin:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_relaxed_min);
      case wasm::kExprF64x2RelaxedMax:
        return EmitBinOp<kS128, kS128, false, kF64>(
            &LiftoffAssembler::emit_f64x2_relaxed_max);
      case wasm::kExprI16x8RelaxedQ15MulRS:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_relaxed_q15mulr_s);
      case wasm::kExprI32x4RelaxedTruncF32x4S:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_relaxed_trunc_f32x4_s);
      case wasm::kExprI32x4RelaxedTruncF32x4U:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_relaxed_trunc_f32x4_u);
      case wasm::kExprI32x4RelaxedTruncF64x2SZero:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_relaxed_trunc_f64x2_s_zero);
      case wasm::kExprI32x4RelaxedTruncF64x2UZero:
        return EmitUnOp<kS128, kS128>(
            &LiftoffAssembler::emit_i32x4_relaxed_trunc_f64x2_u_zero);
      case wasm::kExprI16x8DotI8x16I7x16S:
        return EmitBinOp<kS128, kS128>(
            &LiftoffAssembler::emit_i16x8_dot_i8x16_i7x16_s);
      case wasm::kExprI32x4DotI8x16I7x16AddS: {
        // There is no helper for an instruction with 3 SIMD operands
        // and we do not expect to add any more, so inlining it here.
        static constexpr RegClass res_rc = reg_class_for(kS128);
        LiftoffRegList pinned;
        LiftoffRegister acc = pinned.set(__ PopToRegister(pinned));
        LiftoffRegister rhs = pinned.set(__ PopToRegister(pinned));
        LiftoffRegister lhs = pinned.set(__ PopToRegister(pinned));
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
        // x86 platforms save a move when dst == acc, so prefer that.
        LiftoffRegister dst =
            __ GetUnusedRegister(res_rc, {acc}, LiftoffRegList{lhs, rhs});
#else
        // On other platforms, for simplicity, we ensure that none of the
        // registers alias. (If we cared, it would probably be feasible to
        // allow {dst} to alias with {lhs} or {rhs}, but that'd be brittle.)
        LiftoffRegister dst = __ GetUnusedRegister(res_rc, pinned);
#endif

        __ emit_i32x4_dot_i8x16_i7x16_add_s(dst, lhs, rhs, acc);
        __ PushRegister(kS128, dst);
        return;
      }
      default:
        UNREACHABLE();
    }
  }

  template <ValueKind src_kind, ValueKind result_kind, typename EmitFn>
  void EmitSimdExtractLaneOp(EmitFn fn, const SimdLaneImmediate& imm) {
    static constexpr RegClass src_rc = reg_class_for(src_kind);
    static constexpr RegClass result_rc = reg_class_for(result_kind);
    LiftoffRegister lhs = __ PopToRegister();
    LiftoffRegister dst = src_rc == result_rc
                              ? __ GetUnusedRegister(result_rc, {lhs}, {})
                              : __ GetUnusedRegister(result_rc, {});
    fn(dst, lhs, imm.lane);
    __ PushRegister(result_kind, dst);
  }

  template <ValueKind src2_kind, typename EmitFn>
  void EmitSimdReplaceLaneOp(EmitFn fn, const SimdLaneImmediate& imm) {
    static constexpr RegClass src1_rc = reg_class_for(kS128);
    static constexpr RegClass src2_rc = reg_class_for(src2_kind);
    static constexpr RegClass result_rc = reg_class_for(kS128);
    // On backends which need fp pair, src1_rc and result_rc end up being
    // kFpRegPair, which is != kFpReg, but we still want to pin src2 when it is
    // kFpReg, since it can overlap with those pairs.
    static constexpr bool pin_src2 = kNeedS128RegPair && src2_rc == kFpReg;

    // Does not work for arm
    LiftoffRegister src2 = __ PopToRegister();
    LiftoffRegister src1 = (src1_rc == src2_rc || pin_src2)
                               ? __ PopToRegister(LiftoffRegList{src2})
                               : __
                                 PopToRegister();
    LiftoffRegister dst =
        (src2_rc == result_rc || pin_src2)
            ? __ GetUnusedRegister(result_rc, {src1}, LiftoffRegList{src2})
            : __ GetUnusedRegister(result_rc, {src1}, {});
    fn(dst, src1, src2, imm.lane);
    __ PushRegister(kS128, dst);
  }

  void SimdLaneOp(FullDecoder* decoder, WasmOpcode opcode,
                  const SimdLaneImmediate& imm,
                  base::Vector<const Value> inputs, Value* result) {
    CHECK(CpuFeatures::SupportsWasmSimd128());
    switch (opcode) {
#define CASE_SIMD_EXTRACT_LANE_OP(opcode, kind, fn)      \
  case wasm::kExpr##opcode:                              \
    EmitSimdExtractLaneOp<kS128, k##kind>(               \
        [this](LiftoffRegister dst, LiftoffRegister lhs, \
               uint8_t imm_lane_idx) {                   \
          __ emit_##fn(dst, lhs, imm_lane_idx);          \
        },                                               \
        imm);                                            \
    break;
      CASE_SIMD_EXTRACT_LANE_OP(I8x16ExtractLaneS, I32, i8x16_extract_lane_s)
      CASE_SIMD_EXTRACT_LANE_OP(I8x16ExtractLaneU, I32, i8x16_extract_lane_u)
      CASE_SIMD_EXTRACT_LANE_OP(I16x8ExtractLaneS, I32, i16x8_extract_lane_s)
      CASE_SIMD_EXTRACT_LANE_OP(I16x8ExtractLaneU, I32, i16x8_extract_lane_u)
      CASE_SIMD_EXTRACT_LANE_OP(I32x4ExtractLane, I32, i32x4_extract_lane)
      CASE_SIMD_EXTRACT_LANE_OP(I64x2ExtractLane, I64, i64x2_extract_lane)
      CASE_SIMD_EXTRACT_LANE_OP(F32x4ExtractLane, F32, f32x4_extract_lane)
      CASE_SIMD_EXTRACT_LANE_OP(F64x2ExtractLane, F64, f64x2_extract_lane)
#undef CASE_SIMD_EXTRACT_LANE_OP
      case wasm::kExprF16x8ExtractLane:
        EmitSimdExtractLaneOp<kS128, kF32>(
            [this](LiftoffRegister dst, LiftoffRegister lhs,
                   uint8_t imm_lane_idx) {
              if (asm_.emit_f16x8_extract_lane(dst, lhs, imm_lane_idx)) return;
              LiftoffRegister value = __ GetUnusedRegister(kGpReg, {});
              __ emit_i16x8_extract_lane_u(value, lhs, imm_lane_idx);
              auto conv_ref = ExternalReference::wasm_float16_to_float32();
              GenerateCCallWithStackBuffer(
                  &dst, kVoid, kF32, {VarState{kI16, value, 0}}, conv_ref);
            },
            imm);
        break;
#define CASE_SIMD_REPLACE_LANE_OP(opcode, kind, fn)          \
  case wasm::kExpr##opcode:                                  \
    EmitSimdReplaceLaneOp<k##kind>(                          \
        [this](LiftoffRegister dst, LiftoffRegister src1,    \
               LiftoffRegister src2, uint8_t imm_lane_idx) { \
          __ emit_##fn(dst, src1, src2, imm_lane_idx);       \
        },                                                   \
        imm);                                                \
    break;
      CASE_SIMD_REPLACE_LANE_OP(I8x16ReplaceLane, I32, i8x16_replace_lane)
      CASE_SIMD_REPLACE_LANE_OP(I16x8ReplaceLane, I32, i16x8_replace_lane)
      CASE_SIMD_REPLACE_LANE_OP(I32x4ReplaceLane, I32, i32x4_replace_lane)
      CASE_SIMD_REPLACE_LANE_OP(I64x2ReplaceLane, I64, i64x2_replace_lane)
      CASE_SIMD_REPLACE_LANE_OP(F32x4ReplaceLane, F32, f32x4_replace_lane)
      CASE_SIMD_REPLACE_LANE_OP(F64x2ReplaceLane, F64, f64x2_replace_lane)
#undef CASE_SIMD_REPLACE_LANE_OP
      case wasm::kExprF16x8ReplaceLane: {
        EmitSimdReplaceLaneOp<kI32>(
            [this](LiftoffRegister dst, LiftoffRegister src1,
                   LiftoffRegister src2, uint8_t imm_lane_idx) {
              if (asm_.emit_f16x8_replace_lane(dst, src1, src2, imm_lane_idx)) {
                return;
              }
              __ PushRegister(kS128, src1);
              LiftoffRegister value = __ GetUnusedRegister(kGpReg, {});
              auto conv_ref = ExternalReference::wasm_float32_to_float16();
              GenerateCCallWithStackBuffer(&value, kVoid, kI16,
                                           {VarState{kF32, src2, 0}}, conv_ref);
              __ PopToFixedRegister(src1);
              __ emit_i16x8_replace_lane(dst, src1, value, imm_lane_idx);
            },
            imm);
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  void S128Const(FullDecoder* decoder, const Simd128Immediate& imm,
                 Value* result) {
    CHECK(CpuFeatures::SupportsWasmSimd128());
    constexpr RegClass result_rc = reg_class_for(kS128);
    LiftoffRegister dst = __ GetUnusedRegister(result_rc, {});
    bool all_zeroes = std::all_of(std::begin(imm.value), std::end(imm.value),
                                  [](uint8_t v) { return v == 0; });
    bool all_ones = std::all_of(std::begin(imm.value), std::end(imm.value),
                                [](uint8_t v) { return v == 0xff; });
    if (all_zeroes) {
      __ LiftoffAssembler::emit_s128_xor(dst, dst, dst);
    } else if (all_ones) {
      // Any SIMD eq will work, i32x4 is efficient on all archs.
      __ LiftoffAssembler::emit_i32x4_eq(dst, dst, dst);
    } else {
      __ LiftoffAssembler::emit_s128_const(dst, imm.value);
    }
    __ PushRegister(kS128, dst);
  }

  void Simd8x16ShuffleOp(FullDecoder* decoder, const Simd128Immediate& imm,
                         const Value& input0, const Value& input1,
                         Value* result) {
    CHECK(CpuFeatures::SupportsWasmSimd128());
    static constexpr RegClass result_rc = reg_class_for(kS128);
    LiftoffRegList pinned;
    LiftoffRegister rhs = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister lhs = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister dst = __ GetUnusedRegister(result_rc, {lhs, rhs}, {});

    uint8_t shuffle[kSimd128Size];
    memcpy(shuffle, imm.value, sizeof(shuffle));
    bool is_swizzle;
    bool needs_swap;
    wasm::SimdShuffle::CanonicalizeShuffle(lhs == rhs, shuffle, &needs_swap,
                                           &is_swizzle);
    if (needs_swap) {
      std::swap(lhs, rhs);
    }
    __ LiftoffAssembler::emit_i8x16_shuffle(dst, lhs, rhs, shuffle, is_swizzle);
    __ PushRegister(kS128, dst);
  }

  void ToSmi(Register reg) {
    if (COMPRESS_POINTERS_BOOL || kSystemPointerSize == 4) {
      __ emit_i32_shli(reg, reg, kSmiShiftSize + kSmiTagSize);
    } else {
      __ emit_i64_shli(LiftoffRegister{reg}, LiftoffRegister{reg},
                       kSmiShiftSize + kSmiTagSize);
    }
  }

  void Store32BitExceptionValue(Register values_array, int* index_in_array,
                                Register value, LiftoffRegList pinned) {
    Register tmp_reg = __ GetUnusedRegister(kGpReg, pinned).gp();
    // Get the lower half word into tmp_reg and extend to a Smi.
    --*index_in_array;
    __ emit_i32_andi(tmp_reg, value, 0xffff);
    ToSmi(tmp_reg);
    __ StoreTaggedPointer(
        values_array, no_reg,
        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(*index_in_array),
        tmp_reg, pinned, nullptr, LiftoffAssembler::kSkipWriteBarrier);

    // Get the upper half word into tmp_reg and extend to a Smi.
    --*index_in_array;
    __ emit_i32_shri(tmp_reg, value, 16);
    ToSmi(tmp_reg);
    __ StoreTaggedPointer(
        values_array, no_reg,
        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(*index_in_array),
        tmp_reg, pinned, nullptr, LiftoffAssembler::kSkipWriteBarrier);
  }

  void Store64BitExceptionValue(Register values_array, int* index_in_array,
                                LiftoffRegister value, LiftoffRegList pinned) {
    if (kNeedI64RegPair) {
      Store32BitExceptionValue(values_array, index_in_array, value.low_gp(),
                               pinned);
      Store32BitExceptionValue(values_array, index_in_array, value.high_gp(),
                               pinned);
    } else {
      Store32BitExceptionValue(values_array, index_in_array, value.gp(),
                               pinned);
      __ emit_i64_shri(value, value, 32);
      Store32BitExceptionValue(values_array, index_in_array, value.gp(),
                               pinned);
    }
  }

  void Load16BitExceptionValue(LiftoffRegister dst,
                               LiftoffRegister values_array, uint32_t* index,
                               LiftoffRegList pinned) {
    __ LoadSmiAsInt32(
        dst, values_array.gp(),
        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(*index));
    (*index)++;
  }

  void Load32BitExceptionValue(Register dst, LiftoffRegister values_array,
                               uint32_t* index, LiftoffRegList pinned) {
    LiftoffRegister upper = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    Load16BitExceptionValue(upper, values_array, index, pinned);
    __ emit_i32_shli(upper.gp(), upper.gp(), 16);
    Load16BitExceptionValue(LiftoffRegister(dst), values_array, index, pinned);
    __ emit_i32_or(dst, upper.gp(), dst);
  }

  void Load64BitExceptionValue(LiftoffRegister dst,
                               LiftoffRegister values_array, uint32_t* index,
                               LiftoffRegList pinned) {
    if (kNeedI64RegPair) {
      Load32BitExceptionValue(dst.high_gp(), values_array, index, pinned);
      Load32BitExceptionValue(dst.low_gp(), values_array, index, pinned);
    } else {
      Load16BitExceptionValue(dst, values_array, index, pinned);
      __ emit_i64_shli(dst, dst, 48);
      LiftoffRegister tmp_reg =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      Load16BitExceptionValue(tmp_reg, values_array, index, pinned);
      __ emit_i64_shli(tmp_reg, tmp_reg, 32);
      __ emit_i64_or(dst, tmp_reg, dst);
      Load16BitExceptionValue(tmp_reg, values_array, index, pinned);
      __ emit_i64_shli(tmp_reg, tmp_reg, 16);
      __ emit_i64_or(dst, tmp_reg, dst);
      Load16BitExceptionValue(tmp_reg, values_array, index, pinned);
      __ emit_i64_or(dst, tmp_reg, dst);
    }
  }

  void StoreExceptionValue(ValueType type, Register values_array,
                           int* index_in_array, LiftoffRegList pinned) {
    LiftoffRegister value = pinned.set(__ PopToRegister(pinned));
    switch (type.kind()) {
      case kI32:
        Store32BitExceptionValue(values_array, index_in_array, value.gp(),
                                 pinned);
        break;
      case kF32: {
        LiftoffRegister gp_reg =
            pinned.set(__ GetUnusedRegister(kGpReg, pinned));
        __ emit_type_conversion(kExprI32ReinterpretF32, gp_reg, value, nullptr);
        Store32BitExceptionValue(values_array, index_in_array, gp_reg.gp(),
                                 pinned);
        break;
      }
      case kI64:
        Store64BitExceptionValue(values_array, index_in_array, value, pinned);
        break;
      case kF64: {
        LiftoffRegister tmp_reg =
            pinned.set(__ GetUnusedRegister(reg_class_for(kI64), pinned));
        __ emit_type_conversion(kExprI64ReinterpretF64, tmp_reg, value,
                                nullptr);
        Store64BitExceptionValue(values_array, index_in_array, tmp_reg, pinned);
        break;
      }
      case kS128: {
        LiftoffRegister tmp_reg =
            pinned.set(__ GetUnusedRegister(kGpReg, pinned));
        for (int i : {3, 2, 1, 0}) {
          __ emit_i32x4_extract_lane(tmp_reg, value, i);
          Store32BitExceptionValue(values_array, index_in_array, tmp_reg.gp(),
                                   pinned);
        }
        break;
      }
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kRtt: {
        --(*index_in_array);
        __ StoreTaggedPointer(
            values_array, no_reg,
            wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(
                *index_in_array),
            value.gp(), pinned);
        break;
      }
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kVoid:
      case wasm::kBottom:
        UNREACHABLE();
    }
  }

  void LoadExceptionValue(ValueKind kind, LiftoffRegister values_array,
                          uint32_t* index, LiftoffRegList pinned) {
    RegClass rc = reg_class_for(kind);
    LiftoffRegister value = pinned.set(__ GetUnusedRegister(rc, pinned));
    switch (kind) {
      case kI32:
        Load32BitExceptionValue(value.gp(), values_array, index, pinned);
        break;
      case kF32: {
        LiftoffRegister tmp_reg =
            pinned.set(__ GetUnusedRegister(kGpReg, pinned));
        Load32BitExceptionValue(tmp_reg.gp(), values_array, index, pinned);
        __ emit_type_conversion(kExprF32ReinterpretI32, value, tmp_reg,
                                nullptr);
        break;
      }
      case kI64:
        Load64BitExceptionValue(value, values_array, index, pinned);
        break;
      case kF64: {
        RegClass rc_i64 = reg_class_for(kI64);
        LiftoffRegister tmp_reg =
            pinned.set(__ GetUnusedRegister(rc_i64, pinned));
        Load64BitExceptionValue(tmp_reg, values_array, index, pinned);
        __ emit_type_conversion(kExprF64ReinterpretI64, value, tmp_reg,
                                nullptr);
        break;
      }
      case kS128: {
        LiftoffRegister tmp_reg =
            pinned.set(__ GetUnusedRegister(kGpReg, pinned));
        Load32BitExceptionValue(tmp_reg.gp(), values_array, index, pinned);
        __ emit_i32x4_splat(value, tmp_reg);
        for (int lane : {1, 2, 3}) {
          Load32BitExceptionValue(tmp_reg.gp(), values_array, index, pinned);
          __ emit_i32x4_replace_lane(value, value, tmp_reg, lane);
        }
        break;
      }
      case wasm::kRef:
      case wasm::kRefNull:
      case wasm::kRtt: {
        __ LoadTaggedPointer(
            value.gp(), values_array.gp(), no_reg,
            wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(*index));
        (*index)++;
        break;
      }
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
      case wasm::kVoid:
      case wasm::kBottom:
        UNREACHABLE();
    }
    __ PushRegister(kind, value);
  }

  void GetExceptionValues(FullDecoder* decoder, const VarState& exception_var,
                          const WasmTag* tag) {
    LiftoffRegList pinned;
    CODE_COMMENT("get exception values");
    LiftoffRegister values_array = GetExceptionProperty(
        exception_var, RootIndex::kwasm_exception_values_symbol);
    pinned.set(values_array);
    uint32_t index = 0;
    const WasmTagSig* sig = tag->sig;
    for (ValueType param : sig->parameters()) {
      LoadExceptionValue(param.kind(), values_array, &index, pinned);
    }
    DCHECK_EQ(index, WasmExceptionPackage::GetEncodedSize(tag));
  }

  void EmitLandingPad(FullDecoder* decoder, int handler_offset) {
    if (decoder->current_catch() == -1) return;
    MovableLabel handler{zone_};

    // If we return from the throwing code normally, just skip over the handler.
    Label skip_handler;
    __ emit_jump(&skip_handler);

    // Handler: merge into the catch state, and jump to the catch body.
    CODE_COMMENT("-- landing pad --");
    __ bind(handler.get());
    __ ExceptionHandler();
    __ PushException();
    handlers_.push_back({std::move(handler), handler_offset});
    Control* current_try =
        decoder->control_at(decoder->control_depth_of_current_catch());
    DCHECK_NOT_NULL(current_try->try_info);
    if (current_try->try_info->catch_reached) {
      __ MergeStackWith(current_try->try_info->catch_state, 1,
                        LiftoffAssembler::kForwardJump);
    } else {
      current_try->try_info->catch_state = __ MergeIntoNewState(
          __ num_locals(), 1,
          current_try->stack_depth + current_try->num_exceptions);
      current_try->try_info->catch_reached = true;
    }
    __ emit_jump(&current_try->try_info->catch_label);

    __ bind(&skip_handler);
    // Drop the exception.
    __ DropValues(1);
  }

  void Throw(FullDecoder* decoder, const TagIndexImmediate& imm,
             const Value* /* args */) {
    LiftoffRegList pinned;

    // Load the encoded size in a register for the builtin call.
    int encoded_size = WasmExceptionPackage::GetEncodedSize(imm.tag);
    LiftoffRegister encoded_size_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    __ LoadConstant(encoded_size_reg, WasmValue::ForUintPtr(encoded_size));

    // Call the WasmAllocateFixedArray builtin to create the values array.
    CallBuiltin(Builtin::kWasmAllocateFixedArray,
                MakeSig::Returns(kIntPtrKind).Params(kIntPtrKind),
                {VarState{kIntPtrKind, LiftoffRegister{encoded_size_reg}, 0}},
                decoder->position());
    MaybeOSR();

    // The FixedArray for the exception values is now in the first gp return
    // register.
    LiftoffRegister values_array{kReturnRegister0};
    pinned.set(values_array);

    // Now store the exception values in the FixedArray. Do this from last to
    // first value, such that we can just pop them from the value stack.
    CODE_COMMENT("fill values array");
    int index = encoded_size;
    auto* sig = imm.tag->sig;
    for (size_t param_idx = sig->parameter_count(); param_idx > 0;
         --param_idx) {
      ValueType type = sig->GetParam(param_idx - 1);
      StoreExceptionValue(type, values_array.gp(), &index, pinned);
    }
    DCHECK_EQ(0, index);

    // Load the exception tag.
    CODE_COMMENT("load exception tag");
    LiftoffRegister exception_tag =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LOAD_TAGGED_PTR_INSTANCE_FIELD(exception_tag.gp(), TagsTable, pinned);
    __ LoadTaggedPointer(
        exception_tag.gp(), exception_tag.gp(), no_reg,
        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(imm.index));

    // Finally, call WasmThrow.
    CallBuiltin(Builtin::kWasmThrow, MakeSig::Params(kIntPtrKind, kIntPtrKind),
                {VarState{kIntPtrKind, exception_tag, 0},
                 VarState{kIntPtrKind, values_array, 0}},
                decoder->position());

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    int pc_offset = __ pc_offset();
    MaybeOSR();
    EmitLandingPad(decoder, pc_offset);
  }

  void AtomicStoreMem(FullDecoder* decoder, StoreType type,
                      const MemoryAccessImmediate& imm) {
    LiftoffRegList pinned;
    LiftoffRegister value = pinned.set(__ PopToRegister());
    bool i64_offset = imm.memory->is_memory64;
    auto& index_slot = __ cache_state() -> stack_state.back();
    DCHECK_EQ(i64_offset ? kI64 : kI32, index_slot.kind());
    uintptr_t offset = imm.offset;
    LiftoffRegList outer_pinned;
    Register index = no_reg;

    if (IndexStaticallyInBoundsAndAligned(imm.memory, index_slot, type.size(),
                                          &offset)) {
      __ cache_state() -> stack_state.pop_back();  // Pop index.
      CODE_COMMENT("atomic store (constant offset)");
    } else {
      LiftoffRegister full_index = __ PopToRegister(pinned);
      index =
          BoundsCheckMem(decoder, imm.memory, type.size(), imm.offset,
                         full_index, pinned, kDoForceCheck, kCheckAlignment);
      pinned.set(index);
      CODE_COMMENT("atomic store");
    }
    Register addr = pinned.set(GetMemoryStart(imm.mem_index, pinned));
    if (V8_UNLIKELY(v8_flags.trace_wasm_memory) && index != no_reg) {
      outer_pinned.set(index);
    }
    __ AtomicStore(addr, index, offset, value, type, outer_pinned, i64_offset);
    if (V8_UNLIKELY(v8_flags.trace_wasm_memory)) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      TraceMemoryOperation(true, type.mem_rep(), index, offset,
                           decoder->position());
    }
  }

  void AtomicLoadMem(FullDecoder* decoder, LoadType type,
                     const MemoryAccessImmediate& imm) {
    ValueKind kind = type.value_type().kind();
    bool i64_offset = imm.memory->is_memory64;
    auto& index_slot = __ cache_state() -> stack_state.back();
    DCHECK_EQ(i64_offset ? kI64 : kI32, index_slot.kind());
    uintptr_t offset = imm.offset;
    Register index = no_reg;
    LiftoffRegList pinned;

    if (IndexStaticallyInBoundsAndAligned(imm.memory, index_slot, type.size(),
                                          &offset)) {
      __ cache_state() -> stack_state.pop_back();  // Pop index.
      CODE_COMMENT("atomic load (constant offset)");
    } else {
      LiftoffRegister full_index = __ PopToRegister();
      index = BoundsCheckMem(decoder, imm.memory, type.size(), imm.offset,
                             full_index, {}, kDoForceCheck, kCheckAlignment);
      pinned.set(index);
      CODE_COMMENT("atomic load");
    }

    Register addr = pinned.set(GetMemoryStart(imm.mem_index, pinned));
    RegClass rc = reg_class_for(kind);
    LiftoffRegister value = pinned.set(__ GetUnusedRegister(rc, pinned));
    __ AtomicLoad(value, addr, index, offset, type, pinned, i64_offset);
    __ PushRegister(kind, value);

    if (V8_UNLIKELY(v8_flags.trace_wasm_memory)) {
      // TODO(14259): Implement memory tracing for multiple memories.
      CHECK_EQ(0, imm.memory->index);
      TraceMemoryOperation(false, type.mem_type().representation(), index,
                           offset, decoder->position());
    }
  }

  void AtomicBinop(FullDecoder* decoder, StoreType type,
                   const MemoryAccessImmediate& imm,
                   void (LiftoffAssembler::*emit_fn)(Register, Register,
                                                     uintptr_t, LiftoffRegister,
                                                     LiftoffRegister, StoreType,
                                                     bool)) {
    ValueKind result_kind = type.value_type().kind();
    LiftoffRegList pinned;
    LiftoffRegister value = pinned.set(__ PopToRegister());
#ifdef V8_TARGET_ARCH_IA32
    // We have to reuse the value register as the result register so that we
    // don't run out of registers on ia32. For this we use the value register as
    // the result register if it has no other uses. Otherwise we allocate a new
    // register and let go of the value register to get spilled.
    LiftoffRegister result = value;
    if (__ cache_state()->is_used(value)) {
      result = pinned.set(__ GetUnusedRegister(value.reg_class(), pinned));
      __ Move(result, value, result_kind);
      pinned.clear(value);
      value = result;
    }
#else
    LiftoffRegister result =
        pinned.set(__ GetUnusedRegister(value.reg_class(), pinned));
#endif
    auto& index_slot = __ cache_state() -> stack_state.back();
    uintptr_t offset = imm.offset;
    bool i64_offset = imm.memory->is_memory64;
    DCHECK_EQ(i64_offset ? kI64 : kI32, index_slot.kind());
    Register index = no_reg;

    if (IndexStaticallyInBoundsAndAligned(imm.memory, index_slot, type.size(),
                                          &offset)) {
      __ cache_state() -> stack_state.pop_back();  // Pop index.
      CODE_COMMENT("atomic binop (constant offset)");
    } else {
      LiftoffRegister full_index = __ PopToRegister(pinned);
      index =
          BoundsCheckMem(decoder, imm.memory, type.size(), imm.offset,
                         full_index, pinned, kDoForceCheck, kCheckAlignment);

      pinned.set(index);
      CODE_COMMENT("atomic binop");
    }

    Register addr = pinned.set(GetMemoryStart(imm.mem_index, pinned));
    (asm_.*emit_fn)(addr, index, offset, value, result, type, i64_offset);
    __ PushRegister(result_kind, result);
  }

  void AtomicCompareExchange(FullDecoder* decoder, StoreType type,
                             const MemoryAccessImmediate& imm) {
#ifdef V8_TARGET_ARCH_IA32
    // On ia32 we don't have enough registers to first pop all the values off
    // the stack and then start with the code generation. Instead we do the
    // complete address calculation first, so that the address only needs a
    // single register. Afterwards we load all remaining values into the
    // other registers.
    LiftoffRegister full_index = __ PeekToRegister(2, {});

    Register index =
        BoundsCheckMem(decoder, imm.memory, type.size(), imm.offset, full_index,
                       {}, kDoForceCheck, kCheckAlignment);
    LiftoffRegList pinned{index};

    uintptr_t offset = imm.offset;
    Register addr = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    if (imm.memory->index == 0) {
      LOAD_INSTANCE_FIELD(addr, Memory0Start, kSystemPointerSize, pinned);
    } else {
      LOAD_PROTECTED_PTR_INSTANCE_FIELD(addr, MemoryBasesAndSizes, pinned);
      int buffer_offset = wasm::ObjectAccess::ToTagged(ByteArray::kHeaderSize) +
                          kSystemPointerSize * imm.memory->index * 2;
      __ LoadFullPointer(addr, addr, buffer_offset);
    }
    __ emit_i32_add(addr, addr, index);
    pinned.clear(LiftoffRegister(index));
    LiftoffRegister new_value = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister expected = pinned.set(__ PopToRegister(pinned));

    // Pop the index from the stack.
    bool i64_offset = imm.memory->is_memory64;
    DCHECK_EQ(i64_offset ? kI64 : kI32,
              __ cache_state()->stack_state.back().kind());
    __ DropValues(1);

    LiftoffRegister result = expected;
    if (__ cache_state()->is_used(result)) __ SpillRegister(result);

    // We already added the index to addr, so we can just pass no_reg to the
    // assembler now.
    __ AtomicCompareExchange(addr, no_reg, offset, expected, new_value, result,
                             type, i64_offset);
    __ PushRegister(type.value_type().kind(), result);
    return;
#else
    ValueKind result_kind = type.value_type().kind();
    LiftoffRegList pinned;
    LiftoffRegister new_value = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister expected = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister result =
        pinned.set(__ GetUnusedRegister(reg_class_for(result_kind), pinned));

    auto& index_slot = __ cache_state() -> stack_state.back();
    uintptr_t offset = imm.offset;
    bool i64_offset = imm.memory->is_memory64;
    DCHECK_EQ(i64_offset ? kI64 : kI32, index_slot.kind());
    Register index = no_reg;

    if (IndexStaticallyInBoundsAndAligned(imm.memory, index_slot, type.size(),
                                          &offset)) {
      __ cache_state() -> stack_state.pop_back();  // Pop index.
      CODE_COMMENT("atomic cmpxchg (constant offset)");
    } else {
      LiftoffRegister full_index = __ PopToRegister(pinned);
      index =
          BoundsCheckMem(decoder, imm.memory, type.size(), imm.offset,
                         full_index, pinned, kDoForceCheck, kCheckAlignment);
      pinned.set(index);
      CODE_COMMENT("atomic cmpxchg");
    }

    Register addr = pinned.set(GetMemoryStart(imm.mem_index, pinned));
    __ AtomicCompareExchange(addr, index, offset, expected, new_value, result,
                             type, i64_offset);
    __ PushRegister(result_kind, result);
#endif
  }

  void CallBuiltin(Builtin builtin, const ValueKindSig& sig,
                   std::initializer_list<VarState> params, int position) {
    SCOPED_CODE_COMMENT(
        (std::string{"Call builtin: "} + Builtins::name(builtin)));
    auto interface_descriptor = Builtins::CallInterfaceDescriptorFor(builtin);
    auto* call_descriptor = compiler::Linkage::GetStubCallDescriptor(
        zone_,                                          // zone
        interface_descriptor,                           // descriptor
        interface_descriptor.GetStackParameterCount(),  // stack parameter count
        compiler::CallDescriptor::kNoFlags,             // flags
        compiler::Operator::kNoProperties,              // properties
        StubCallMode::kCallWasmRuntimeStub);            // stub call mode

    __ PrepareBuiltinCall(&sig, call_descriptor, params);
    if (position != kNoSourcePosition) {
      source_position_table_builder_.AddPosition(
          __ pc_offset(), SourcePosition(position), true);
    }
    __ CallBuiltin(builtin);
    DefineSafepoint();
  }

  void AtomicWait(FullDecoder* decoder, ValueKind kind,
                  const MemoryAccessImmediate& imm) {
    FUZZER_HEAVY_INSTRUCTION;
    ValueKind index_kind;
    {
      LiftoffRegList pinned;
      LiftoffRegister full_index = __ PeekToRegister(2, pinned);

      Register index_reg =
          BoundsCheckMem(decoder, imm.memory, value_kind_size(kind), imm.offset,
                         full_index, pinned, kDoForceCheck, kCheckAlignment);
      pinned.set(index_reg);

      uintptr_t offset = imm.offset;
      Register index_plus_offset = index_reg;

      if (__ cache_state()->is_used(LiftoffRegister(index_reg))) {
        index_plus_offset =
            pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
        __ Move(index_plus_offset, index_reg, kIntPtrKind);
      }
      if (offset) {
        __ emit_ptrsize_addi(index_plus_offset, index_plus_offset, offset);
      }

      VarState& index = __ cache_state()->stack_state.end()[-3];

      // We replace the index on the value stack with the `index_plus_offset`
      // calculated above. Thereby the BigInt allocation below does not
      // overwrite the calculated value by accident.
      // The kind of `index_plus_offset has to be the same or smaller than the
      // original kind of `index`. The kind of index is kI32 for memory32, and
      // kI64 for memory64. On 64-bit platforms we can use in both cases the
      // kind of `index` also for `index_plus_offset`. Note that
      // `index_plus_offset` fits into a kI32 because we do a bounds check
      // first.
      // On 32-bit platforms, we have to use an kI32 also for memory64, because
      // `index_plus_offset` does not exist in a register pair.
      __ cache_state()->inc_used(LiftoffRegister(index_plus_offset));
      if (index.is_reg()) __ cache_state()->dec_used(index.reg());
      index_kind = index.kind() == kI32 ? kI32 : kIntPtrKind;

      index = VarState{index_kind, LiftoffRegister{index_plus_offset},
                       index.offset()};
    }
    {
      // Convert the top value of the stack (the timeout) from I64 to a BigInt,
      // which we can then pass to the atomic.wait builtin.
      VarState i64_timeout = __ cache_state()->stack_state.back();
      CallBuiltin(
          kNeedI64RegPair ? Builtin::kI32PairToBigInt : Builtin::kI64ToBigInt,
          MakeSig::Returns(kRef).Params(kI64), {i64_timeout},
          decoder->position());
      __ DropValues(1);
      // We put the result on the value stack so that it gets preserved across
      // a potential GC that may get triggered by the BigInt allocation below.
      __ PushRegister(kRef, LiftoffRegister(kReturnRegister0));
    }

    Register expected = no_reg;
    if (kind == kI32) {
      expected = __ PeekToRegister(1, {}).gp();
    } else {
      VarState i64_expected = __ cache_state()->stack_state.end()[-2];
      CallBuiltin(
          kNeedI64RegPair ? Builtin::kI32PairToBigInt : Builtin::kI64ToBigInt,
          MakeSig::Returns(kRef).Params(kI64), {i64_expected},
          decoder->position());
      expected = kReturnRegister0;
    }
    ValueKind expected_kind = kind == kI32 ? kI32 : kRef;

    VarState timeout = __ cache_state()->stack_state.end()[-1];
    VarState index = __ cache_state()->stack_state.end()[-3];

    auto target = kind == kI32 ? Builtin::kWasmI32AtomicWait
                               : Builtin::kWasmI64AtomicWait;

    // The type of {index} can either by i32 or intptr, depending on whether
    // memory32 or memory64 is used. This is okay because both values get passed
    // by register.
    CallBuiltin(target, MakeSig::Params(kI32, index_kind, expected_kind, kRef),
                {{kI32, static_cast<int32_t>(imm.memory->index), 0},
                 index,
                 {expected_kind, LiftoffRegister{expected}, 0},
                 timeout},
                decoder->position());
    // Pop parameters from the value stack.
    __ DropValues(3);

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    __ PushRegister(kI32, LiftoffRegister(kReturnRegister0));
  }

  void AtomicNotify(FullDecoder* decoder, const MemoryAccessImmediate& imm) {
    LiftoffRegList pinned;
    LiftoffRegister num_waiters_to_wake = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister full_index = __ PopToRegister(pinned);
    Register index_reg =
        BoundsCheckMem(decoder, imm.memory, kInt32Size, imm.offset, full_index,
                       pinned, kDoForceCheck, kCheckAlignment);
    pinned.set(index_reg);

    uintptr_t offset = imm.offset;
    Register addr = index_reg;
    if (__ cache_state()->is_used(LiftoffRegister(index_reg))) {
      addr = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
      __ Move(addr, index_reg, kIntPtrKind);
    }
    if (offset) {
      __ emit_ptrsize_addi(addr, addr, offset);
    }

    Register mem_start = GetMemoryStart(imm.memory->index, pinned);
    __ emit_ptrsize_add(addr, addr, mem_start);

    LiftoffRegister result =
        GenerateCCall(kI32,
                      {{kIntPtrKind, LiftoffRegister{addr}, 0},
                       {kI32, num_waiters_to_wake, 0}},
                      ExternalReference::wasm_atomic_notify());

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    __ PushRegister(kI32, result);
  }

#define ATOMIC_STORE_LIST(V)        \
  V(I32AtomicStore, kI32Store)      \
  V(I64AtomicStore, kI64Store)      \
  V(I32AtomicStore8U, kI32Store8)   \
  V(I32AtomicStore16U, kI32Store16) \
  V(I64AtomicStore8U, kI64Store8)   \
  V(I64AtomicStore16U, kI64Store16) \
  V(I64AtomicStore32U, kI64Store32)

#define ATOMIC_LOAD_LIST(V)        \
  V(I32AtomicLoad, kI32Load)       \
  V(I64AtomicLoad, kI64Load)       \
  V(I32AtomicLoad8U, kI32Load8U)   \
  V(I32AtomicLoad16U, kI32Load16U) \
  V(I64AtomicLoad8U, kI64Load8U)   \
  V(I64AtomicLoad16U, kI64Load16U) \
  V(I64AtomicLoad32U, kI64Load32U)

#define ATOMIC_BINOP_INSTRUCTION_LIST(V)         \
  V(Add, I32AtomicAdd, kI32Store)                \
  V(Add, I64AtomicAdd, kI64Store)                \
  V(Add, I32AtomicAdd8U, kI32Store8)             \
  V(Add, I32AtomicAdd16U, kI32Store16)           \
  V(Add, I64AtomicAdd8U, kI64Store8)             \
  V(Add, I64AtomicAdd16U, kI64Store16)           \
  V(Add, I64AtomicAdd32U, kI64Store32)           \
  V(Sub, I32AtomicSub, kI32Store)                \
  V(Sub, I64AtomicSub, kI64Store)                \
  V(Sub, I32AtomicSub8U, kI32Store8)             \
  V(Sub, I32AtomicSub16U, kI32Store16)           \
  V(Sub, I64AtomicSub8U, kI64Store8)             \
  V(Sub, I64AtomicSub16U, kI64Store16)           \
  V(Sub, I64AtomicSub32U, kI64Store32)           \
  V(And, I32AtomicAnd, kI32Store)                \
  V(And, I64AtomicAnd, kI64Store)                \
  V(And, I32AtomicAnd8U, kI32Store8)             \
  V(And, I32AtomicAnd16U, kI32Store16)           \
  V(And, I64AtomicAnd8U, kI64Store8)             \
  V(And, I64AtomicAnd16U, kI64Store16)           \
  V(And, I64AtomicAnd32U, kI64Store32)           \
  V(Or, I32AtomicOr, kI32Store)                  \
  V(Or, I64AtomicOr, kI64Store)                  \
  V(Or, I32AtomicOr8U, kI32Store8)               \
  V(Or, I32AtomicOr16U, kI32Store16)             \
  V(Or, I64AtomicOr8U, kI64Store8)               \
  V(Or, I64AtomicOr16U, kI64Store16)             \
  V(Or, I64AtomicOr32U, kI64Store32)             \
  V(Xor, I32AtomicXor, kI32Store)                \
  V(Xor, I64AtomicXor, kI64Store)                \
  V(Xor, I32AtomicXor8U, kI32Store8)             \
  V(Xor, I32AtomicXor16U, kI32Store16)           \
  V(Xor, I64AtomicXor8U, kI64Store8)             \
  V(Xor, I64AtomicXor16U, kI64Store16)           \
  V(Xor, I64AtomicXor32U, kI64Store32)           \
  V(Exchange, I32AtomicExchange, kI32Store)      \
  V(Exchange, I64AtomicExchange, kI64Store)      \
  V(Exchange, I32AtomicExchange8U, kI32Store8)   \
  V(Exchange, I32AtomicExchange16U, kI32Store16) \
  V(Exchange, I64AtomicExchange8U, kI64Store8)   \
  V(Exchange, I64AtomicExchange16U, kI64Store16) \
  V(Exchange, I64AtomicExchange32U, kI64Store32)

#define ATOMIC_COMPARE_EXCHANGE_LIST(V)       \
  V(I32AtomicCompareExchange, kI32Store)      \
  V(I64AtomicCompareExchange, kI64Store)      \
  V(I32AtomicCompareExchange8U, kI32Store8)   \
  V(I32AtomicCompareExchange16U, kI32Store16) \
  V(I64AtomicCompareExchange8U, kI64Store8)   \
  V(I64AtomicCompareExchange16U, kI64Store16) \
  V(I64AtomicCompareExchange32U, kI64Store32)

  void AtomicOp(FullDecoder* decoder, WasmOpcode opcode, const Value args[],
                const size_t argc, const MemoryAccessImmediate& imm,
                Value* result) {
    switch (opcode) {
#define ATOMIC_STORE_OP(name, type)                \
  case wasm::kExpr##name:                          \
    AtomicStoreMem(decoder, StoreType::type, imm); \
    break;

      ATOMIC_STORE_LIST(ATOMIC_STORE_OP)
#undef ATOMIC_STORE_OP

#define ATOMIC_LOAD_OP(name, type)               \
  case wasm::kExpr##name:                        \
    AtomicLoadMem(decoder, LoadType::type, imm); \
    break;

      ATOMIC_LOAD_LIST(ATOMIC_LOAD_OP)
#undef ATOMIC_LOAD_OP

#define ATOMIC_BINOP_OP(op, name, type)                                        \
  case wasm::kExpr##name:                                                      \
    AtomicBinop(decoder, StoreType::type, imm, &LiftoffAssembler::Atomic##op); \
    break;

      ATOMIC_BINOP_INSTRUCTION_LIST(ATOMIC_BINOP_OP)
#undef ATOMIC_BINOP_OP

#define ATOMIC_COMPARE_EXCHANGE_OP(name, type)            \
  case wasm::kExpr##name:                                 \
    AtomicCompareExchange(decoder, StoreType::type, imm); \
    break;

      ATOMIC_COMPARE_EXCHANGE_LIST(ATOMIC_COMPARE_EXCHANGE_OP)
#undef ATOMIC_COMPARE_EXCHANGE_OP

      case kExprI32AtomicWait:
        AtomicWait(decoder, kI32, imm);
        break;
      case kExprI64AtomicWait:
        AtomicWait(decoder, kI64, imm);
        break;
      case kExprAtomicNotify:
        AtomicNotify(decoder, imm);
        break;
      default:
        UNREACHABLE();
    }
  }

#undef ATOMIC_STORE_LIST
#undef ATOMIC_LOAD_LIST
#undef ATOMIC_BINOP_INSTRUCTION_LIST
#undef ATOMIC_COMPARE_EXCHANGE_LIST

  void AtomicFence(FullDecoder* decoder) { __ AtomicFence(); }

  // Pop a VarState and if needed transform it to an intptr.
  // When truncating from u64 to u32, the {*high_word} is updated to contain
  // the ORed combination of all high words.
  VarState PopIndexToVarState(Register* high_word, LiftoffRegList* pinned) {
    VarState slot = __ PopVarState();
    const bool is_64bit_value = slot.kind() == kI64;
    // For memory32 on a 32-bit system or memory64 on a 64-bit system, there is
    // nothing to do.
    if (Is64() == is_64bit_value) {
      if (slot.is_reg()) pinned->set(slot.reg());
      return slot;
    }

    // {kI64} constants will be stored as 32-bit integers in the {VarState} and
    // will be sign-extended later. Hence we can return constants if they are
    // positive (such that sign-extension and zero-extension are identical).
    if (slot.is_const() && (kIntPtrKind == kI32 || slot.i32_const() >= 0)) {
      return {kIntPtrKind, slot.i32_const(), 0};
    }

    // For memory32 on 64-bit hosts, zero-extend.
    if constexpr (Is64()) {
      DCHECK(!is_64bit_value);  // Handled above.
      LiftoffRegister reg = __ LoadToModifiableRegister(slot, *pinned);
      __ emit_u32_to_uintptr(reg.gp(), reg.gp());
      pinned->set(reg);
      return {kIntPtrKind, reg, 0};
    }

    // For memory64 on 32-bit systems, combine all high words for a zero-check
    // and only use the low words afterwards. This keeps the register pressure
    // managable.
    DCHECK(is_64bit_value && !Is64());  // Other cases are handled above.
    LiftoffRegister reg = __ LoadToRegister(slot, *pinned);
    pinned->set(reg.low());
    if (*high_word == no_reg) {
      // Choose a register to hold the (combination of) high word(s). It cannot
      // be one of the pinned registers, and it cannot be used in the value
      // stack.
      *high_word =
          !pinned->has(reg.high()) && __ cache_state()->is_free(reg.high())
              ? reg.high().gp()
              : __ GetUnusedRegister(kGpReg, *pinned).gp();
      pinned->set(*high_word);
      if (*high_word != reg.high_gp()) {
        __ Move(*high_word, reg.high_gp(), kI32);
      }
    } else if (*high_word != reg.high_gp()) {
      // Combine the new high word into existing high words.
      __ emit_i32_or(*high_word, *high_word, reg.high_gp());
    }
    return {kIntPtrKind, reg.low(), 0};
  }

  // This is a helper function that traps with TableOOB if any bit is set in
  // `high_word`. It is meant to be used after `PopIndexToVarState()` to check
  // if the conversion was valid.
  // Note that this is suboptimal as we add an OOL code for this special
  // condition, and there's also another conditional trap in the caller builtin.
  // However, it only applies for the rare case of 32-bit platforms with
  // table64.
  void CheckHighWordEmptyForTableType(FullDecoder* decoder,
                                      const Register high_word,
                                      LiftoffRegList* pinned) {
    if constexpr (Is64()) {
      DCHECK_EQ(no_reg, high_word);
      return;
    }
    if (high_word == no_reg) return;

    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapTableOutOfBounds);
    FREEZE_STATE(trapping);
    __ emit_cond_jump(kNotZero, trap_label, kI32, high_word, no_reg, trapping);
    // Clearing `high_word` is safe because this never aliases with another
    // in-use register, see `PopIndexToVarState()`.
    pinned->clear(high_word);
  }

  // Same as {PopIndexToVarState}, but can take a VarState in the middle of the
  // stack without popping it.
  // For 64-bit values on 32-bit systems, the resulting VarState will contain a
  // single register whose value will be kMaxUint32 if the high word had any
  // bits set.
  VarState IndexToVarStateSaturating(int stack_index, LiftoffRegList* pinned) {
    DCHECK_LE(0, stack_index);
    DCHECK_LT(stack_index, __ cache_state()->stack_height());
    VarState& slot = __ cache_state()->stack_state.end()[-1 - stack_index];
    const bool is_mem64 = slot.kind() == kI64;
    // For memory32 on a 32-bit system or memory64 on a 64-bit system, there is
    // nothing to do.
    if ((kSystemPointerSize == kInt64Size) == is_mem64) {
      if (slot.is_reg()) pinned->set(slot.reg());
      return slot;
    }

    // {kI64} constants will be stored as 32-bit integers in the {VarState} and
    // will be sign-extended later. Hence we can return constants if they are
    // positive (such that sign-extension and zero-extension are identical).
    if (slot.is_const() && (kIntPtrKind == kI32 || slot.i32_const() >= 0)) {
      return {kIntPtrKind, slot.i32_const(), 0};
    }

    LiftoffRegister reg = __ LoadToModifiableRegister(slot, *pinned);
    // For memory32 on 64-bit hosts, zero-extend.
    if constexpr (Is64()) {
      DCHECK(!is_mem64);  // Handled above.
      __ emit_u32_to_uintptr(reg.gp(), reg.gp());
      pinned->set(reg);
      return {kIntPtrKind, reg, 0};
    }

    // For memory64 on 32-bit systems, saturate the low word.
    DCHECK(is_mem64);  // Other cases are handled above.
    DCHECK_EQ(kSystemPointerSize, kInt32Size);
    pinned->set(reg.low());
    Label ok;
    FREEZE_STATE(frozen);
    __ emit_cond_jump(kZero, &ok, kI32, reg.high().gp(), no_reg, frozen);
    __ LoadConstant(reg.low(), WasmValue{kMaxUInt32});
    __ emit_jump(&ok);
    __ bind(&ok);
    return {kIntPtrKind, reg.low(), 0};
  }

  // Same as {PopIndexToVarState}, but saturates 64-bit values on 32-bit
  // platforms like {IndexToVarStateSaturating}.
  VarState PopIndexToVarStateSaturating(LiftoffRegList* pinned) {
    VarState result = IndexToVarStateSaturating(0, pinned);
    __ DropValues(1);
    return result;
  }

  // The following functions are to be used inside a DCHECK. They always return
  // true and will fail internally on a detected inconsistency.
#ifdef DEBUG
  // Checks that the top-of-stack value matches the declared memory (64-bit or
  // 32-bit).
  bool MatchingMemTypeOnTopOfStack(const WasmMemory* memory) {
    return MatchingIndexTypeOnTopOfStack(memory->is_memory64);
  }

  // Checks that the top-of-stack value matches the expected bitness.
  bool MatchingIndexTypeOnTopOfStack(bool expect_64bit_value) {
    DCHECK_LT(0, __ cache_state()->stack_height());
    ValueKind expected_kind = expect_64bit_value ? kI64 : kI32;
    DCHECK_EQ(expected_kind, __ cache_state()->stack_state.back().kind());
    return true;
  }

  bool MatchingMemType(const WasmMemory* memory, int stack_index) {
    DCHECK_LE(0, stack_index);
    DCHECK_LT(stack_index, __ cache_state()->stack_state.size());
    ValueKind expected_kind = memory->is_memory64 ? kI64 : kI32;
    DCHECK_EQ(expected_kind,
              __ cache_state()->stack_state.end()[-1 - stack_index].kind());
    return true;
  }
#endif

  void MemoryInit(FullDecoder* decoder, const MemoryInitImmediate& imm,
                  const Value&, const Value&, const Value&) {
    FUZZER_HEAVY_INSTRUCTION;
    Register mem_offsets_high_word = no_reg;
    LiftoffRegList pinned;
    VarState size = __ PopVarState();
    if (size.is_reg()) pinned.set(size.reg());
    VarState src = __ PopVarState();
    if (src.is_reg()) pinned.set(src.reg());
    DCHECK(MatchingMemTypeOnTopOfStack(imm.memory.memory));
    VarState dst = PopIndexToVarState(&mem_offsets_high_word, &pinned);

    Register instance_data = __ cache_state() -> cached_instance_data;
    if (instance_data == no_reg) {
      instance_data = __ GetUnusedRegister(kGpReg, pinned).gp();
      __ LoadInstanceDataFromFrame(instance_data);
    }
    pinned.set(instance_data);

    // TODO(crbug.com/41480344): The stack state in the OOL code should reflect
    // the state before popping any values (for a better debugging experience).
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapMemOutOfBounds);
    if (mem_offsets_high_word != no_reg) {
      // If any high word has bits set, jump to the OOB trap.
      FREEZE_STATE(trapping);
      __ emit_cond_jump(kNotZero, trap_label, kI32, mem_offsets_high_word,
                        no_reg, trapping);
      pinned.clear(mem_offsets_high_word);
    }

    LiftoffRegister result =
        GenerateCCall(kI32,
                      {{kIntPtrKind, LiftoffRegister{instance_data}, 0},
                       {kI32, static_cast<int32_t>(imm.memory.index), 0},
                       dst,
                       src,
                       {kI32, static_cast<int32_t>(imm.data_segment.index), 0},
                       size},
                      ExternalReference::wasm_memory_init());
    FREEZE_STATE(trapping);
    __ emit_cond_jump(kEqual, trap_label, kI32, result.gp(), no_reg, trapping);
  }

  void DataDrop(FullDecoder* decoder, const IndexImmediate& imm) {
    LiftoffRegList pinned;

    Register seg_size_array =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    LOAD_TAGGED_PTR_INSTANCE_FIELD(seg_size_array, DataSegmentSizes, pinned);

    LiftoffRegister seg_index =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    // Scale the seg_index for the array access.
    __ LoadConstant(
        seg_index,
        WasmValue(wasm::ObjectAccess::ElementOffsetInTaggedFixedUInt32Array(
            imm.index)));

    // Set the length of the segment to '0' to drop it.
    LiftoffRegister null_reg = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    __ LoadConstant(null_reg, WasmValue(0));
    __ Store(seg_size_array, seg_index.gp(), 0, null_reg, StoreType::kI32Store,
             pinned);
  }

  void MemoryCopy(FullDecoder* decoder, const MemoryCopyImmediate& imm,
                  const Value&, const Value&, const Value&) {
    FUZZER_HEAVY_INSTRUCTION;
    Register mem_offsets_high_word = no_reg;
    LiftoffRegList pinned;

    // The type of {size} is the min of {src} and {dst} (where {kI32 < kI64}).
    DCHECK(MatchingIndexTypeOnTopOfStack(imm.memory_dst.memory->is_memory64 &&
                                         imm.memory_src.memory->is_memory64));
    VarState size = PopIndexToVarState(&mem_offsets_high_word, &pinned);
    DCHECK(MatchingMemTypeOnTopOfStack(imm.memory_src.memory));
    VarState src = PopIndexToVarState(&mem_offsets_high_word, &pinned);
    DCHECK(MatchingMemTypeOnTopOfStack(imm.memory_dst.memory));
    VarState dst = PopIndexToVarState(&mem_offsets_high_word, &pinned);

    Register instance_data = __ cache_state() -> cached_instance_data;
    if (instance_data == no_reg) {
      instance_data = __ GetUnusedRegister(kGpReg, pinned).gp();
      __ LoadInstanceDataFromFrame(instance_data);
    }
    pinned.set(instance_data);

    // TODO(crbug.com/41480344): The stack state in the OOL code should reflect
    // the state before popping any values (for a better debugging experience).
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapMemOutOfBounds);
    DCHECK_IMPLIES(Is64(), mem_offsets_high_word == no_reg);
    if (!Is64() && mem_offsets_high_word != no_reg) {
      // If any high word has bits set, jump to the OOB trap.
      FREEZE_STATE(trapping);
      __ emit_cond_jump(kNotZero, trap_label, kI32, mem_offsets_high_word,
                        no_reg, trapping);
    }

    LiftoffRegister result =
        GenerateCCall(kI32,
                      {{kIntPtrKind, LiftoffRegister{instance_data}, 0},
                       {kI32, static_cast<int32_t>(imm.memory_dst.index), 0},
                       {kI32, static_cast<int32_t>(imm.memory_src.index), 0},
                       dst,
                       src,
                       size},
                      ExternalReference::wasm_memory_copy());
    FREEZE_STATE(trapping);
    __ emit_cond_jump(kEqual, trap_label, kI32, result.gp(), no_reg, trapping);
  }

  void MemoryFill(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                  const Value&, const Value&, const Value&) {
    FUZZER_HEAVY_INSTRUCTION;
    Register mem_offsets_high_word = no_reg;
    LiftoffRegList pinned;
    DCHECK(MatchingMemTypeOnTopOfStack(imm.memory));
    VarState size = PopIndexToVarState(&mem_offsets_high_word, &pinned);
    VarState value = __ PopVarState();
    if (value.is_reg()) pinned.set(value.reg());
    DCHECK(MatchingMemTypeOnTopOfStack(imm.memory));
    VarState dst = PopIndexToVarState(&mem_offsets_high_word, &pinned);

    Register instance_data = __ cache_state() -> cached_instance_data;
    if (instance_data == no_reg) {
      instance_data = __ GetUnusedRegister(kGpReg, pinned).gp();
      __ LoadInstanceDataFromFrame(instance_data);
    }
    pinned.set(instance_data);

    // TODO(crbug.com/41480344): The stack state in the OOL code should reflect
    // the state before popping any values (for a better debugging experience).
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapMemOutOfBounds);
    if (mem_offsets_high_word != no_reg) {
      // If any high word has bits set, jump to the OOB trap.
      FREEZE_STATE(trapping);
      __ emit_cond_jump(kNotZero, trap_label, kI32, mem_offsets_high_word,
                        no_reg, trapping);
    }

    LiftoffRegister result =
        GenerateCCall(kI32,
                      {{kIntPtrKind, LiftoffRegister{instance_data}, 0},
                       {kI32, static_cast<int32_t>(imm.index), 0},
                       dst,
                       value,
                       size},
                      ExternalReference::wasm_memory_fill());
    FREEZE_STATE(trapping);
    __ emit_cond_jump(kEqual, trap_label, kI32, result.gp(), no_reg, trapping);
  }

  void LoadSmi(LiftoffRegister reg, int value) {
    Address smi_value = Smi::FromInt(value).ptr();
    using smi_type = std::conditional_t<kSmiKind == kI32, int32_t, int64_t>;
    __ LoadConstant(reg, WasmValue{static_cast<smi_type>(smi_value)});
  }

  VarState LoadSmiConstant(int32_t constant, LiftoffRegList* pinned) {
    if constexpr (kSmiKind == kI32) {
      int32_t smi_const = static_cast<int32_t>(Smi::FromInt(constant).ptr());
      return VarState{kI32, smi_const, 0};
    } else {
      LiftoffRegister reg = pinned->set(__ GetUnusedRegister(kGpReg, *pinned));
      LoadSmi(reg, constant);
      return VarState{kSmiKind, reg, 0};
    }
  }

  void TableInit(FullDecoder* decoder, const TableInitImmediate& imm,
                 const Value&, const Value&, const Value&) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    VarState table_index = LoadSmiConstant(imm.table.index, &pinned);
    VarState segment_index =
        LoadSmiConstant(imm.element_segment.index, &pinned);
    VarState extract_shared_data = LoadSmiConstant(0, &pinned);

    VarState size = __ PopVarState();
    if (size.is_reg()) pinned.set(size.reg());
    VarState src = __ PopVarState();
    if (src.is_reg()) pinned.set(src.reg());
    Register index_high_word = no_reg;
    VarState dst = PopIndexToVarState(&index_high_word, &pinned);

    // Trap if any bit in high word was set.
    CheckHighWordEmptyForTableType(decoder, index_high_word, &pinned);

    CallBuiltin(
        Builtin::kWasmTableInit,
        MakeSig::Params(kIntPtrKind, kI32, kI32, kSmiKind, kSmiKind, kSmiKind),
        {dst, src, size, table_index, segment_index, extract_shared_data},
        decoder->position());

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);
  }

  void ElemDrop(FullDecoder* decoder, const IndexImmediate& imm) {
    LiftoffRegList pinned;
    Register element_segments =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    LOAD_TAGGED_PTR_INSTANCE_FIELD(element_segments, ElementSegments, pinned);

    LiftoffRegister seg_index =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    __ LoadConstant(
        seg_index,
        WasmValue(
            wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(imm.index)));

    // Mark the segment as dropped by setting it to the empty fixed array.
    Register empty_fixed_array =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    __ LoadFullPointer(
        empty_fixed_array, kRootRegister,
        IsolateData::root_slot_offset(RootIndex::kEmptyFixedArray));

    __ StoreTaggedPointer(element_segments, seg_index.gp(), 0,
                          empty_fixed_array, pinned);
  }

  void TableCopy(FullDecoder* decoder, const TableCopyImmediate& imm,
                 const Value&, const Value&, const Value&) {
    FUZZER_HEAVY_INSTRUCTION;
    Register index_high_word = no_reg;
    LiftoffRegList pinned;

    VarState table_src_index = LoadSmiConstant(imm.table_src.index, &pinned);
    VarState table_dst_index = LoadSmiConstant(imm.table_dst.index, &pinned);
    VarState extract_shared_data = LoadSmiConstant(0, &pinned);

    VarState size = PopIndexToVarState(&index_high_word, &pinned);
    VarState src = PopIndexToVarState(&index_high_word, &pinned);
    VarState dst = PopIndexToVarState(&index_high_word, &pinned);

    // Trap if any bit in the combined high words was set.
    CheckHighWordEmptyForTableType(decoder, index_high_word, &pinned);

    CallBuiltin(
        Builtin::kWasmTableCopy,
        MakeSig::Params(kIntPtrKind, kIntPtrKind, kIntPtrKind, kSmiKind,
                        kSmiKind, kSmiKind),
        {dst, src, size, table_dst_index, table_src_index, extract_shared_data},
        decoder->position());

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);
  }

  void TableGrow(FullDecoder* decoder, const TableIndexImmediate& imm,
                 const Value&, const Value&, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister table_index_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(table_index_reg, imm.index);
    VarState table_index(kSmiKind, table_index_reg, 0);
    // If `delta` is, OOB table.grow should return -1.
    VarState delta = PopIndexToVarStateSaturating(&pinned);
    VarState value = __ PopVarState();
    VarState extract_shared_data(kI32, 0, 0);

    CallBuiltin(Builtin::kWasmTableGrow,
                MakeSig::Returns(kSmiKind).Params(kSmiKind, kIntPtrKind, kI32,
                                                  kRefNull),
                {table_index, delta, extract_shared_data, value},
                decoder->position());

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);
    __ SmiToInt32(kReturnRegister0);
    if (imm.table->is_table64) {
      LiftoffRegister result64 = LiftoffRegister(kReturnRegister0);
      if (kNeedI64RegPair) {
        result64 = LiftoffRegister::ForPair(kReturnRegister0, kReturnRegister1);
      }
      __ emit_type_conversion(kExprI64SConvertI32, result64,
                              LiftoffRegister(kReturnRegister0), nullptr);
      __ PushRegister(kI64, result64);
    } else {
      __ PushRegister(kI32, LiftoffRegister(kReturnRegister0));
    }
  }

  void TableSize(FullDecoder* decoder, const TableIndexImmediate& imm, Value*) {
    // We have to look up instance->tables[table_index].length.

    LiftoffRegList pinned;
    // Get the number of calls array address.
    Register tables = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    LOAD_TAGGED_PTR_INSTANCE_FIELD(tables, Tables, pinned);

    Register table = tables;
    __ LoadTaggedPointer(
        table, tables, no_reg,
        ObjectAccess::ElementOffsetInTaggedFixedArray(imm.index));

    int length_field_size = WasmTableObject::kCurrentLengthOffsetEnd -
                            WasmTableObject::kCurrentLengthOffset + 1;

    Register result = table;
    __ Load(LiftoffRegister(result), table, no_reg,
            wasm::ObjectAccess::ToTagged(WasmTableObject::kCurrentLengthOffset),
            length_field_size == 4 ? LoadType::kI32Load : LoadType::kI64Load);

    __ SmiUntag(result);

    if (imm.table->is_table64) {
      LiftoffRegister result64 = LiftoffRegister(result);
      if (kNeedI64RegPair) {
        result64 = LiftoffRegister::ForPair(
            result, __ GetUnusedRegister(kGpReg, pinned).gp());
      }
      __ emit_type_conversion(kExprI64SConvertI32, result64,
                              LiftoffRegister(result), nullptr);
      __ PushRegister(kI64, result64);
    } else {
      __ PushRegister(kI32, LiftoffRegister(result));
    }
  }

  void TableFill(FullDecoder* decoder, const TableIndexImmediate& imm,
                 const Value&, const Value&, const Value&) {
    FUZZER_HEAVY_INSTRUCTION;
    Register high_words = no_reg;
    LiftoffRegList pinned;

    VarState table_index = LoadSmiConstant(imm.index, &pinned);
    VarState extract_shared_data{kI32, 0, 0};

    VarState count = PopIndexToVarState(&high_words, &pinned);
    VarState value = __ PopVarState();
    if (value.is_reg()) pinned.set(value.reg());
    VarState start = PopIndexToVarState(&high_words, &pinned);
    // Trap if any bit in the combined high words was set.
    CheckHighWordEmptyForTableType(decoder, high_words, &pinned);

    CallBuiltin(
        Builtin::kWasmTableFill,
        MakeSig::Params(kIntPtrKind, kIntPtrKind, kI32, kSmiKind, kRefNull),
        {start, count, extract_shared_data, table_index, value},
        decoder->position());

    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);
  }

  void StructNew(FullDecoder* decoder, const StructIndexImmediate& imm,
                 bool initial_values_on_stack) {
    LiftoffRegister rtt = RttCanon(imm.index, {});

    CallBuiltin(Builtin::kWasmAllocateStructWithRtt,
                MakeSig::Returns(kRef).Params(kRtt, kI32),
                {VarState{kRtt, rtt, 0},
                 VarState{kI32, WasmStruct::Size(imm.struct_type), 0}},
                decoder->position());

    LiftoffRegister obj(kReturnRegister0);
    LiftoffRegList pinned{obj};

    for (uint32_t i = imm.struct_type->field_count(); i > 0;) {
      i--;
      int offset = StructFieldOffset(imm.struct_type, i);
      ValueType field_type = imm.struct_type->field(i);
      LiftoffRegister value = pinned.set(
          initial_values_on_stack
              ? __ PopToRegister(pinned)
              : __ GetUnusedRegister(reg_class_for(field_type.kind()), pinned));
      if (!initial_values_on_stack) {
        if (!CheckSupportedType(decoder, field_type.kind(), "default value")) {
          return;
        }
        SetDefaultValue(value, field_type);
      }
      // Skipping the write barrier is safe as long as:
      // (1) {obj} is freshly allocated, and
      // (2) {obj} is in new-space (not pretenured).
      StoreObjectField(decoder, obj.gp(), no_reg, offset, value, false, pinned,
                       field_type.kind(), LiftoffAssembler::kSkipWriteBarrier);
      pinned.clear(value);
    }
    // If this assert fails then initialization of padding field might be
    // necessary.
    static_assert(Heap::kMinObjectSizeInTaggedWords == 2 &&
                      WasmStruct::kHeaderSize == 2 * kTaggedSize,
                  "empty struct might require initialization of padding field");
    __ PushRegister(kRef, obj);
  }

  void StructNew(FullDecoder* decoder, const StructIndexImmediate& imm,
                 const Value args[], Value* result) {
    StructNew(decoder, imm, true);
  }

  void StructNewDefault(FullDecoder* decoder, const StructIndexImmediate& imm,
                        Value* result) {
    StructNew(decoder, imm, false);
  }

  void StructGet(FullDecoder* decoder, const Value& struct_obj,
                 const FieldImmediate& field, bool is_signed, Value* result) {
    const StructType* struct_type = field.struct_imm.struct_type;
    ValueKind field_kind = struct_type->field(field.field_imm.index).kind();
    if (!CheckSupportedType(decoder, field_kind, "field load")) return;
    int offset = StructFieldOffset(struct_type, field.field_imm.index);
    LiftoffRegList pinned;
    LiftoffRegister obj = pinned.set(__ PopToRegister(pinned));

    auto [explicit_check, implicit_check] =
        null_checks_for_struct_op(struct_obj.type, field.field_imm.index);

    if (explicit_check) {
      MaybeEmitNullCheck(decoder, obj.gp(), pinned, struct_obj.type);
    }
    LiftoffRegister value =
        __ GetUnusedRegister(reg_class_for(field_kind), pinned);
    LoadObjectField(decoder, value, obj.gp(), no_reg, offset, field_kind,
                    is_signed, implicit_check, pinned);
    __ PushRegister(unpacked(field_kind), value);
  }

  void StructSet(FullDecoder* decoder, const Value& struct_obj,
                 const FieldImmediate& field, const Value& field_value) {
    const StructType* struct_type = field.struct_imm.struct_type;
    ValueKind field_kind = struct_type->field(field.field_imm.index).kind();
    int offset = StructFieldOffset(struct_type, field.field_imm.index);
    LiftoffRegList pinned;
    LiftoffRegister value = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister obj = pinned.set(__ PopToRegister(pinned));

    auto [explicit_check, implicit_check] =
        null_checks_for_struct_op(struct_obj.type, field.field_imm.index);

    if (explicit_check) {
      MaybeEmitNullCheck(decoder, obj.gp(), pinned, struct_obj.type);
    }

    StoreObjectField(decoder, obj.gp(), no_reg, offset, value, implicit_check,
                     pinned, field_kind);
  }

  void ArrayNew(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                bool initial_value_on_stack) {
    FUZZER_HEAVY_INSTRUCTION;
    // Max length check.
    {
      LiftoffRegister length =
          __ LoadToRegister(__ cache_state()->stack_state.end()[-1], {});
      Label* trap_label =
          AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapArrayTooLarge);
      FREEZE_STATE(trapping);
      __ emit_i32_cond_jumpi(kUnsignedGreaterThan, trap_label, length.gp(),
                             WasmArray::MaxLength(imm.array_type), trapping);
    }
    ValueType elem_type = imm.array_type->element_type();
    ValueKind elem_kind = elem_type.kind();
    int elem_size = value_kind_size(elem_kind);
    // Allocate the array.
    {
      LiftoffRegister rtt = RttCanon(imm.index, {});
      CallBuiltin(Builtin::kWasmAllocateArray_Uninitialized,
                  MakeSig::Returns(kRef).Params(kRtt, kI32, kI32),
                  {VarState{kRtt, rtt, 0},
                   __ cache_state()->stack_state.end()[-1],  // length
                   VarState{kI32, elem_size, 0}},
                  decoder->position());
    }

    LiftoffRegister obj(kReturnRegister0);
    LiftoffRegList pinned{obj};
    LiftoffRegister length = pinned.set(__ PopToModifiableRegister(pinned));
    LiftoffRegister value =
        pinned.set(__ GetUnusedRegister(reg_class_for(elem_kind), pinned));
    if (initial_value_on_stack) {
      __ PopToFixedRegister(value);
    } else {
      if (!CheckSupportedType(decoder, elem_kind, "default value")) return;
      SetDefaultValue(value, elem_type);
    }

    LiftoffRegister index = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    __ LoadConstant(index, WasmValue(int32_t{0}));

    // Initialize the array's elements.
    // Skipping the write barrier is safe as long as:
    // (1) {obj} is freshly allocated, and
    // (2) {obj} is in new-space (not pretenured).
    ArrayFillImpl(decoder, pinned, obj, index, value, length, elem_kind,
                  LiftoffAssembler::kSkipWriteBarrier);

    __ PushRegister(kRef, obj);
  }

  void ArrayNew(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                const Value& length_value, const Value& initial_value,
                Value* result) {
    ArrayNew(decoder, imm, true);
  }

  void ArrayNewDefault(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                       const Value& length, Value* result) {
    ArrayNew(decoder, imm, false);
  }

  void ArrayFill(FullDecoder* decoder, ArrayIndexImmediate& imm,
                 const Value& array, const Value& /* index */,
                 const Value& /* value */, const Value& /* length */) {
    FUZZER_HEAVY_INSTRUCTION;
    {
      // Null check.
      LiftoffRegList pinned;
      LiftoffRegister array_reg = pinned.set(__ PeekToRegister(3, pinned));
      if (null_check_strategy_ == compiler::NullCheckStrategy::kExplicit) {
        MaybeEmitNullCheck(decoder, array_reg.gp(), pinned, array.type);
      }

      // Bounds checks.
      Label* trap_label =
          AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapArrayOutOfBounds);
      LiftoffRegister array_length =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      bool implicit_null_check =
          array.type.is_nullable() &&
          null_check_strategy_ == compiler::NullCheckStrategy::kTrapHandler;
      LoadObjectField(decoder, array_length, array_reg.gp(), no_reg,
                      ObjectAccess::ToTagged(WasmArray::kLengthOffset), kI32,
                      false, implicit_null_check, pinned);
      LiftoffRegister index = pinned.set(__ PeekToRegister(2, pinned));
      LiftoffRegister length = pinned.set(__ PeekToRegister(0, pinned));
      LiftoffRegister index_plus_length =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      DCHECK(index_plus_length != array_length);
      __ emit_i32_add(index_plus_length.gp(), length.gp(), index.gp());
      FREEZE_STATE(frozen);
      __ emit_cond_jump(kUnsignedGreaterThan, trap_label, kI32,
                        index_plus_length.gp(), array_length.gp(), frozen);
      // Guard against overflow.
      __ emit_cond_jump(kUnsignedGreaterThan, trap_label, kI32, index.gp(),
                        index_plus_length.gp(), frozen);
    }

    LiftoffRegList pinned;
    LiftoffRegister length = pinned.set(__ PopToModifiableRegister(pinned));
    LiftoffRegister value = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister index = pinned.set(__ PopToModifiableRegister(pinned));
    LiftoffRegister obj = pinned.set(__ PopToRegister(pinned));

    ArrayFillImpl(decoder, pinned, obj, index, value, length,
                  imm.array_type->element_type().kind(),
                  LiftoffAssembler::kNoSkipWriteBarrier);
  }

  void ArrayGet(FullDecoder* decoder, const Value& array_obj,
                const ArrayIndexImmediate& imm, const Value& index_val,
                bool is_signed, Value* result) {
    LiftoffRegList pinned;
    LiftoffRegister index = pinned.set(__ PopToModifiableRegister(pinned));
    LiftoffRegister array = pinned.set(__ PopToRegister(pinned));
    if (null_check_strategy_ == compiler::NullCheckStrategy::kExplicit) {
      MaybeEmitNullCheck(decoder, array.gp(), pinned, array_obj.type);
    }
    bool implicit_null_check =
        array_obj.type.is_nullable() &&
        null_check_strategy_ == compiler::NullCheckStrategy::kTrapHandler;
    BoundsCheckArray(decoder, implicit_null_check, array, index, pinned);
    ValueKind elem_kind = imm.array_type->element_type().kind();
    if (!CheckSupportedType(decoder, elem_kind, "array load")) return;
    int elem_size_shift = value_kind_size_log2(elem_kind);
    if (elem_size_shift != 0) {
      __ emit_i32_shli(index.gp(), index.gp(), elem_size_shift);
    }
    LiftoffRegister value =
        __ GetUnusedRegister(reg_class_for(elem_kind), pinned);
    LoadObjectField(decoder, value, array.gp(), index.gp(),
                    wasm::ObjectAccess::ToTagged(WasmArray::kHeaderSize),
                    elem_kind, is_signed, false, pinned);
    __ PushRegister(unpacked(elem_kind), value);
  }

  void ArraySet(FullDecoder* decoder, const Value& array_obj,
                const ArrayIndexImmediate& imm, const Value& index_val,
                const Value& value_val) {
    LiftoffRegList pinned;
    LiftoffRegister value = pinned.set(__ PopToRegister(pinned));
    DCHECK_EQ(reg_class_for(imm.array_type->element_type().kind()),
              value.reg_class());
    LiftoffRegister index = pinned.set(__ PopToModifiableRegister(pinned));
    LiftoffRegister array = pinned.set(__ PopToRegister(pinned));
    if (null_check_strategy_ == compiler::NullCheckStrategy::kExplicit) {
      MaybeEmitNullCheck(decoder, array.gp(), pinned, array_obj.type);
    }
    bool implicit_null_check =
        array_obj.type.is_nullable() &&
        null_check_strategy_ == compiler::NullCheckStrategy::kTrapHandler;
    BoundsCheckArray(decoder, implicit_null_check, array, index, pinned);
    ValueKind elem_kind = imm.array_type->element_type().kind();
    int elem_size_shift = value_kind_size_log2(elem_kind);
    if (elem_size_shift != 0) {
      __ emit_i32_shli(index.gp(), index.gp(), elem_size_shift);
    }
    StoreObjectField(decoder, array.gp(), index.gp(),
                     wasm::ObjectAccess::ToTagged(WasmArray::kHeaderSize),
                     value, false, pinned, elem_kind);
  }

  void ArrayLen(FullDecoder* decoder, const Value& array_obj, Value* result) {
    LiftoffRegList pinned;
    LiftoffRegister obj = pinned.set(__ PopToRegister(pinned));
    if (null_check_strategy_ == compiler::NullCheckStrategy::kExplicit) {
      MaybeEmitNullCheck(decoder, obj.gp(), pinned, array_obj.type);
    }
    LiftoffRegister len = __ GetUnusedRegister(kGpReg, pinned);
    int kLengthOffset = wasm::ObjectAccess::ToTagged(WasmArray::kLengthOffset);
    bool implicit_null_check =
        array_obj.type.is_nullable() &&
        null_check_strategy_ == compiler::NullCheckStrategy::kTrapHandler;
    LoadObjectField(decoder, len, obj.gp(), no_reg, kLengthOffset, kI32,
                    false /* is_signed */, implicit_null_check, pinned);
    __ PushRegister(kI32, len);
  }

  void ArrayCopy(FullDecoder* decoder, const Value& dst, const Value& dst_index,
                 const Value& src, const Value& src_index,
                 const ArrayIndexImmediate& src_imm, const Value& length) {
    // TODO(14034): Unify implementation with TF: Implement this with
    // GenerateCCallWithStackBuffer. Remove runtime function and builtin in
    // wasm.tq.
    CallBuiltin(Builtin::kWasmArrayCopy,
                MakeSig::Params(kI32, kI32, kI32, kRefNull, kRefNull),
                // Builtin parameter order:
                // [dst_index, src_index, length, dst, src].
                {__ cache_state()->stack_state.end()[-4],
                 __ cache_state()->stack_state.end()[-2],
                 __ cache_state()->stack_state.end()[-1],
                 __ cache_state()->stack_state.end()[-5],
                 __ cache_state()->stack_state.end()[-3]},
                decoder->position());
    __ DropValues(5);
  }

  void ArrayNewFixed(FullDecoder* decoder, const ArrayIndexImmediate& array_imm,
                     const IndexImmediate& length_imm,
                     const Value* /* elements */, Value* /* result */) {
    LiftoffRegister rtt = RttCanon(array_imm.index, {});
    ValueKind elem_kind = array_imm.array_type->element_type().kind();
    int32_t elem_count = length_imm.index;
    // Allocate the array.
    CallBuiltin(Builtin::kWasmAllocateArray_Uninitialized,
                MakeSig::Returns(kRef).Params(kRtt, kI32, kI32),
                {VarState{kRtt, rtt, 0}, VarState{kI32, elem_count, 0},
                 VarState{kI32, value_kind_size(elem_kind), 0}},
                decoder->position());

    // Initialize the array with stack arguments.
    LiftoffRegister array(kReturnRegister0);
    if (!CheckSupportedType(decoder, elem_kind, "array.new_fixed")) return;
    for (int i = elem_count - 1; i >= 0; i--) {
      LiftoffRegList pinned{array};
      LiftoffRegister element = pinned.set(__ PopToRegister(pinned));
      int offset =
          WasmArray::kHeaderSize + (i << value_kind_size_log2(elem_kind));
      // Skipping the write barrier is safe as long as:
      // (1) {array} is freshly allocated, and
      // (2) {array} is in new-space (not pretenured).
      StoreObjectField(decoder, array.gp(), no_reg,
                       wasm::ObjectAccess::ToTagged(offset), element, false,
                       pinned, elem_kind, LiftoffAssembler::kSkipWriteBarrier);
    }

    // Push the array onto the stack.
    __ PushRegister(kRef, array);
  }

  void ArrayNewSegment(FullDecoder* decoder,
                       const ArrayIndexImmediate& array_imm,
                       const IndexImmediate& segment_imm,
                       const Value& /* offset */, const Value& /* length */,
                       Value* /* result */) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister rtt = pinned.set(RttCanon(array_imm.index, pinned));

    LiftoffRegister is_element_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(is_element_reg,
            array_imm.array_type->element_type().is_reference());

    LiftoffRegister extract_shared_data_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(extract_shared_data_reg, 0);

    CallBuiltin(
        Builtin::kWasmArrayNewSegment,
        MakeSig::Returns(kRef).Params(kI32, kI32, kI32, kSmiKind, kSmiKind,
                                      kRtt),
        {
            VarState{kI32, static_cast<int>(segment_imm.index), 0},  // segment
            __ cache_state()->stack_state.end()[-2],                 // offset
            __ cache_state()->stack_state.end()[-1],                 // length
            VarState{kSmiKind, is_element_reg, 0},           // is_element
            VarState{kSmiKind, extract_shared_data_reg, 0},  // shared
            VarState{kRtt, rtt, 0}                           // rtt
        },
        decoder->position());

    // Pop parameters from the value stack.
    __ DropValues(2);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result(kReturnRegister0);
    __ PushRegister(kRef, result);
  }

  void ArrayInitSegment(FullDecoder* decoder,
                        const ArrayIndexImmediate& array_imm,
                        const IndexImmediate& segment_imm,
                        const Value& /* array */,
                        const Value& /* array_index */,
                        const Value& /* segment_offset*/,
                        const Value& /* length */) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister segment_index_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(segment_index_reg, static_cast<int32_t>(segment_imm.index));

    LiftoffRegister is_element_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(is_element_reg,
            array_imm.array_type->element_type().is_reference());

    LiftoffRegister extract_shared_data_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(extract_shared_data_reg, 0);

    // Builtin parameter order: array_index, segment_offset, length,
    //                          segment_index, array.
    CallBuiltin(Builtin::kWasmArrayInitSegment,
                MakeSig::Params(kI32, kI32, kI32, kSmiKind, kSmiKind, kSmiKind,
                                kRefNull),
                {__ cache_state()->stack_state.end()[-3],
                 __ cache_state()->stack_state.end()[-2],
                 __ cache_state()->stack_state.end()[-1],
                 VarState{kSmiKind, segment_index_reg, 0},
                 VarState{kSmiKind, is_element_reg, 0},
                 VarState{kSmiKind, extract_shared_data_reg, 0},
                 __ cache_state()->stack_state.end()[-4]},
                decoder->position());
    __ DropValues(4);
  }

  void RefI31(FullDecoder* decoder, const Value& input, Value* result) {
    LiftoffRegister src = __ PopToRegister();
    LiftoffRegister dst = __ GetUnusedRegister(kGpReg, {src}, {});
    if constexpr (SmiValuesAre31Bits()) {
      static_assert(kSmiTag == 0);
      __ emit_i32_shli(dst.gp(), src.gp(), kSmiTagSize);
    } else {
      DCHECK(SmiValuesAre32Bits());
      // Set the topmost bit to sign-extend the second bit. This way,
      // interpretation in JS (if this value escapes there) will be the same as
      // i31.get_s.
      __ emit_i64_shli(dst, src, kSmiTagSize + kSmiShiftSize + 1);
      __ emit_i64_sari(dst, dst, 1);
    }
    __ PushRegister(kRef, dst);
  }

  void I31GetS(FullDecoder* decoder, const Value& input, Value* result) {
    LiftoffRegList pinned;
    LiftoffRegister src = pinned.set(__ PopToRegister());
    MaybeEmitNullCheck(decoder, src.gp(), pinned, input.type);
    LiftoffRegister dst = __ GetUnusedRegister(kGpReg, {src}, {});
    if constexpr (SmiValuesAre31Bits()) {
      __ emit_i32_sari(dst.gp(), src.gp(), kSmiTagSize);
    } else {
      DCHECK(SmiValuesAre32Bits());
      // Topmost bit is already sign-extended.
      __ emit_i64_sari(dst, src, kSmiTagSize + kSmiShiftSize);
    }
    __ PushRegister(kI32, dst);
  }

  void I31GetU(FullDecoder* decoder, const Value& input, Value* result) {
    LiftoffRegList pinned;
    LiftoffRegister src = pinned.set(__ PopToRegister());
    MaybeEmitNullCheck(decoder, src.gp(), pinned, input.type);
    LiftoffRegister dst = __ GetUnusedRegister(kGpReg, {src}, {});
    if constexpr (SmiValuesAre31Bits()) {
      __ emit_i32_shri(dst.gp(), src.gp(), kSmiTagSize);
    } else {
      DCHECK(SmiValuesAre32Bits());
      // Remove topmost bit.
      __ emit_i64_shli(dst, src, 1);
      __ emit_i64_shri(dst, dst, kSmiTagSize + kSmiShiftSize + 1);
    }
    __ PushRegister(kI32, dst);
  }

  LiftoffRegister RttCanon(uint32_t type_index, LiftoffRegList pinned) {
    LiftoffRegister rtt = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LOAD_TAGGED_PTR_INSTANCE_FIELD(rtt.gp(), ManagedObjectMaps, pinned);
    __ LoadTaggedPointer(
        rtt.gp(), rtt.gp(), no_reg,
        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(type_index));
    return rtt;
  }

  enum NullSucceeds : bool {  // --
    kNullSucceeds = true,
    kNullFails = false
  };

  // Falls through on match (=successful type check).
  // Returns the register containing the object.
  void SubtypeCheck(const WasmModule* module, Register obj_reg,
                    ValueType obj_type, Register rtt_reg, ValueType rtt_type,
                    Register scratch_null, Register scratch2, Label* no_match,
                    NullSucceeds null_succeeds,
                    const FreezeCacheState& frozen) {
    Label match;
    bool is_cast_from_any = obj_type.is_reference_to(HeapType::kAny);

    // Skip the null check if casting from any and not {null_succeeds}.
    // In that case the instance type check will identify null as not being a
    // wasm object and fail.
    if (obj_type.is_nullable() && (!is_cast_from_any || null_succeeds)) {
      __ emit_cond_jump(kEqual, null_succeeds ? &match : no_match,
                        obj_type.kind(), obj_reg, scratch_null, frozen);
    }
    Register tmp1 = scratch_null;  // Done with null checks.

    // Add Smi check if the source type may store a Smi (i31ref or JS Smi).
    ValueType i31ref = ValueType::Ref(HeapType::kI31);
    // Ref.extern can also contain Smis, however there isn't any type that
    // could downcast to ref.extern.
    DCHECK(!rtt_type.is_reference_to(HeapType::kExtern));
    // Ref.i31 check has its own implementation.
    DCHECK(!rtt_type.is_reference_to(HeapType::kI31));
    if (IsSubtypeOf(i31ref, obj_type, module)) {
      Label* i31_target =
          IsSubtypeOf(i31ref, rtt_type, module) ? &match : no_match;
      __ emit_smi_check(obj_reg, i31_target, LiftoffAssembler::kJumpOnSmi,
                        frozen);
    }

    __ LoadMap(tmp1, obj_reg);
    // {tmp1} now holds the object's map.

    if (module->types[rtt_type.ref_index()].is_final) {
      // In this case, simply check for map equality.
      __ emit_cond_jump(kNotEqual, no_match, rtt_type.kind(), tmp1, rtt_reg,
                        frozen);
    } else {
      // Check for rtt equality, and if not, check if the rtt is a struct/array
      // rtt.
      __ emit_cond_jump(kEqual, &match, rtt_type.kind(), tmp1, rtt_reg, frozen);

      if (is_cast_from_any) {
        // Check for map being a map for a wasm object (struct, array, func).
        __ Load(LiftoffRegister(scratch2), tmp1, no_reg,
                wasm::ObjectAccess::ToTagged(Map::kInstanceTypeOffset),
                LoadType::kI32Load16U);
        __ emit_i32_subi(scratch2, scratch2, FIRST_WASM_OBJECT_TYPE);
        __ emit_i32_cond_jumpi(kUnsignedGreaterThan, no_match, scratch2,
                               LAST_WASM_OBJECT_TYPE - FIRST_WASM_OBJECT_TYPE,
                               frozen);
      }

      // Constant-time subtyping check: load exactly one candidate RTT from the
      // supertypes list.
      // Step 1: load the WasmTypeInfo into {tmp1}.
      constexpr int kTypeInfoOffset = wasm::ObjectAccess::ToTagged(
          Map::kConstructorOrBackPointerOrNativeContextOffset);
      __ LoadTaggedPointer(tmp1, tmp1, no_reg, kTypeInfoOffset);
      // Step 2: check the list's length if needed.
      uint32_t rtt_depth = GetSubtypingDepth(module, rtt_type.ref_index());
      if (rtt_depth >= kMinimumSupertypeArraySize) {
        LiftoffRegister list_length(scratch2);
        int offset =
            ObjectAccess::ToTagged(WasmTypeInfo::kSupertypesLengthOffset);
        __ LoadSmiAsInt32(list_length, tmp1, offset);
        __ emit_i32_cond_jumpi(kUnsignedLessThanEqual, no_match,
                               list_length.gp(), rtt_depth, frozen);
      }
      // Step 3: load the candidate list slot into {tmp1}, and compare it.
      __ LoadTaggedPointer(
          tmp1, tmp1, no_reg,
          ObjectAccess::ToTagged(WasmTypeInfo::kSupertypesOffset +
                                 rtt_depth * kTaggedSize));
      __ emit_cond_jump(kNotEqual, no_match, rtt_type.kind(), tmp1, rtt_reg,
                        frozen);
    }

    // Fall through to {match}.
    __ bind(&match);
  }

  void RefTest(FullDecoder* decoder, uint32_t ref_index, const Value& obj,
               Value* /* result_val */, bool null_succeeds) {
    Label return_false, done;
    LiftoffRegList pinned;
    LiftoffRegister rtt_reg = pinned.set(RttCanon(ref_index, pinned));
    LiftoffRegister obj_reg = pinned.set(__ PopToRegister(pinned));
    Register scratch_null =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    LiftoffRegister result = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    if (obj.type.is_nullable()) {
      LoadNullValueForCompare(scratch_null, pinned, obj.type);
    }

    {
      FREEZE_STATE(frozen);
      SubtypeCheck(decoder->module_, obj_reg.gp(), obj.type, rtt_reg.gp(),
                   ValueType::Rtt(ref_index), scratch_null, result.gp(),
                   &return_false, null_succeeds ? kNullSucceeds : kNullFails,
                   frozen);

      __ LoadConstant(result, WasmValue(1));
      // TODO(jkummerow): Emit near jumps on platforms that have them.
      __ emit_jump(&done);

      __ bind(&return_false);
      __ LoadConstant(result, WasmValue(0));
      __ bind(&done);
    }
    __ PushRegister(kI32, result);
  }

  void RefTestAbstract(FullDecoder* decoder, const Value& obj, HeapType type,
                       Value* result_val, bool null_succeeds) {
    switch (type.representation()) {
      case HeapType::kEq:
        return AbstractTypeCheck<&LiftoffCompiler::EqCheck>(obj, null_succeeds);
      case HeapType::kI31:
        return AbstractTypeCheck<&LiftoffCompiler::I31Check>(obj,
                                                             null_succeeds);
      case HeapType::kStruct:
        return AbstractTypeCheck<&LiftoffCompiler::StructCheck>(obj,
                                                                null_succeeds);
      case HeapType::kArray:
        return AbstractTypeCheck<&LiftoffCompiler::ArrayCheck>(obj,
                                                               null_succeeds);
      case HeapType::kString:
        return AbstractTypeCheck<&LiftoffCompiler::StringCheck>(obj,
                                                                null_succeeds);
      case HeapType::kNone:
      case HeapType::kNoExtern:
      case HeapType::kNoFunc:
      case HeapType::kNoExn:
        DCHECK(null_succeeds);
        return EmitIsNull(kExprRefIsNull, obj.type);
      case HeapType::kAny:
        // Any may never need a cast as it is either implicitly convertible or
        // never convertible for any given type.
      default:
        UNREACHABLE();
    }
  }

  void RefCast(FullDecoder* decoder, uint32_t ref_index, const Value& obj,
               Value* result, bool null_succeeds) {
    if (v8_flags.experimental_wasm_assume_ref_cast_succeeds) return;

    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapIllegalCast);
    LiftoffRegList pinned;
    LiftoffRegister rtt_reg = pinned.set(RttCanon(ref_index, pinned));
    LiftoffRegister obj_reg = pinned.set(__ PopToRegister(pinned));
    Register scratch_null =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    Register scratch2 = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    if (obj.type.is_nullable()) {
      LoadNullValueForCompare(scratch_null, pinned, obj.type);
    }

    {
      FREEZE_STATE(frozen);
      NullSucceeds on_null = null_succeeds ? kNullSucceeds : kNullFails;
      SubtypeCheck(decoder->module_, obj_reg.gp(), obj.type, rtt_reg.gp(),
                   ValueType::Rtt(ref_index), scratch_null, scratch2,
                   trap_label, on_null, frozen);
    }
    __ PushRegister(obj.type.kind(), obj_reg);
  }

  void RefCastAbstract(FullDecoder* decoder, const Value& obj, HeapType type,
                       Value* result_val, bool null_succeeds) {
    switch (type.representation()) {
      case HeapType::kEq:
        return AbstractTypeCast<&LiftoffCompiler::EqCheck>(obj, decoder,
                                                           null_succeeds);
      case HeapType::kI31:
        return AbstractTypeCast<&LiftoffCompiler::I31Check>(obj, decoder,
                                                            null_succeeds);
      case HeapType::kStruct:
        return AbstractTypeCast<&LiftoffCompiler::StructCheck>(obj, decoder,
                                                               null_succeeds);
      case HeapType::kArray:
        return AbstractTypeCast<&LiftoffCompiler::ArrayCheck>(obj, decoder,
                                                              null_succeeds);
      case HeapType::kString:
        return AbstractTypeCast<&LiftoffCompiler::StringCheck>(obj, decoder,
                                                               null_succeeds);
      case HeapType::kNone:
      case HeapType::kNoExtern:
      case HeapType::kNoFunc:
      case HeapType::kNoExn:
        DCHECK(null_succeeds);
        return AssertNullTypecheck(decoder, obj, result_val);
      case HeapType::kAny:
        // Any may never need a cast as it is either implicitly convertible or
        // never convertible for any given type.
      default:
        UNREACHABLE();
    }
  }

  void BrOnCast(FullDecoder* decoder, uint32_t ref_index, const Value& obj,
                Value* /* result_on_branch */, uint32_t depth,
                bool null_succeeds) {
    // Avoid having sequences of branches do duplicate work.
    if (depth != decoder->control_depth() - 1) {
      __ PrepareForBranch(decoder->control_at(depth)->br_merge()->arity, {});
    }

    Label cont_false;
    LiftoffRegList pinned;
    LiftoffRegister rtt_reg = pinned.set(RttCanon(ref_index, pinned));
    LiftoffRegister obj_reg = pinned.set(__ PeekToRegister(0, pinned));
    Register scratch_null =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    Register scratch2 = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    if (obj.type.is_nullable()) {
      LoadNullValue(scratch_null, kWasmAnyRef);
    }
    FREEZE_STATE(frozen);

    NullSucceeds null_handling = null_succeeds ? kNullSucceeds : kNullFails;
    SubtypeCheck(decoder->module_, obj_reg.gp(), obj.type, rtt_reg.gp(),
                 ValueType::Rtt(ref_index), scratch_null, scratch2, &cont_false,
                 null_handling, frozen);

    BrOrRet(decoder, depth);

    __ bind(&cont_false);
  }

  void BrOnCastFail(FullDecoder* decoder, uint32_t ref_index, const Value& obj,
                    Value* /* result_on_fallthrough */, uint32_t depth,
                    bool null_succeeds) {
    // Avoid having sequences of branches do duplicate work.
    if (depth != decoder->control_depth() - 1) {
      __ PrepareForBranch(decoder->control_at(depth)->br_merge()->arity, {});
    }

    Label cont_branch, fallthrough;
    LiftoffRegList pinned;
    LiftoffRegister rtt_reg = pinned.set(RttCanon(ref_index, pinned));
    LiftoffRegister obj_reg = pinned.set(__ PeekToRegister(0, pinned));
    Register scratch_null =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    Register scratch2 = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    if (obj.type.is_nullable()) {
      LoadNullValue(scratch_null, kWasmAnyRef);
    }
    FREEZE_STATE(frozen);

    NullSucceeds null_handling = null_succeeds ? kNullSucceeds : kNullFails;
    SubtypeCheck(decoder->module_, obj_reg.gp(), obj.type, rtt_reg.gp(),
                 ValueType::Rtt(ref_index), scratch_null, scratch2,
                 &cont_branch, null_handling, frozen);
    __ emit_jump(&fallthrough);

    __ bind(&cont_branch);
    BrOrRet(decoder, depth);

    __ bind(&fallthrough);
  }

  void BrOnCastAbstract(FullDecoder* decoder, const Value& obj, HeapType type,
                        Value* result_on_branch, uint32_t depth,
                        bool null_succeeds) {
    switch (type.representation()) {
      case HeapType::kEq:
        return BrOnAbstractType<&LiftoffCompiler::EqCheck>(obj, decoder, depth,
                                                           null_succeeds);
      case HeapType::kI31:
        return BrOnAbstractType<&LiftoffCompiler::I31Check>(obj, decoder, depth,
                                                            null_succeeds);
      case HeapType::kStruct:
        return BrOnAbstractType<&LiftoffCompiler::StructCheck>(
            obj, decoder, depth, null_succeeds);
      case HeapType::kArray:
        return BrOnAbstractType<&LiftoffCompiler::ArrayCheck>(
            obj, decoder, depth, null_succeeds);
      case HeapType::kString:
        return BrOnAbstractType<&LiftoffCompiler::StringCheck>(
            obj, decoder, depth, null_succeeds);
      case HeapType::kNone:
      case HeapType::kNoExtern:
      case HeapType::kNoFunc:
      case HeapType::kNoExn:
        DCHECK(null_succeeds);
        return BrOnNull(decoder, obj, depth, /*pass_null_along_branch*/ true,
                        nullptr);
      case HeapType::kAny:
        // Any may never need a cast as it is either implicitly convertible or
        // never convertible for any given type.
      default:
        UNREACHABLE();
    }
  }

  void BrOnCastFailAbstract(FullDecoder* decoder, const Value& obj,
                            HeapType type, Value* result_on_fallthrough,
                            uint32_t depth, bool null_succeeds) {
    switch (type.representation()) {
      case HeapType::kEq:
        return BrOnNonAbstractType<&LiftoffCompiler::EqCheck>(
            obj, decoder, depth, null_succeeds);
      case HeapType::kI31:
        return BrOnNonAbstractType<&LiftoffCompiler::I31Check>(
            obj, decoder, depth, null_succeeds);
      case HeapType::kStruct:
        return BrOnNonAbstractType<&LiftoffCompiler::StructCheck>(
            obj, decoder, depth, null_succeeds);
      case HeapType::kArray:
        return BrOnNonAbstractType<&LiftoffCompiler::ArrayCheck>(
            obj, decoder, depth, null_succeeds);
      case HeapType::kString:
        return BrOnNonAbstractType<&LiftoffCompiler::StringCheck>(
            obj, decoder, depth, null_succeeds);
      case HeapType::kNone:
      case HeapType::kNoExtern:
      case HeapType::kNoFunc:
      case HeapType::kNoExn:
        DCHECK(null_succeeds);
        return BrOnNonNull(decoder, obj, nullptr, depth,
                           /*drop_null_on_fallthrough*/ false);
      case HeapType::kAny:
        // Any may never need a cast as it is either implicitly convertible or
        // never convertible for any given type.
      default:
        UNREACHABLE();
    }
  }

  struct TypeCheck {
    Register obj_reg = no_reg;
    ValueType obj_type;
    Register tmp = no_reg;
    Label* no_match;
    bool null_succeeds;

    TypeCheck(ValueType obj_type, Label* no_match, bool null_succeeds)
        : obj_type(obj_type),
          no_match(no_match),
          null_succeeds(null_succeeds) {}

    Register null_reg() { return tmp; }       // After {Initialize}.
    Register instance_type() { return tmp; }  // After {LoadInstanceType}.
  };

  enum PopOrPeek { kPop, kPeek };

  void Initialize(TypeCheck& check, PopOrPeek pop_or_peek, ValueType type) {
    LiftoffRegList pinned;
    if (pop_or_peek == kPop) {
      check.obj_reg = pinned.set(__ PopToRegister(pinned)).gp();
    } else {
      check.obj_reg = pinned.set(__ PeekToRegister(0, pinned)).gp();
    }
    check.tmp = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
    if (check.obj_type.is_nullable()) {
      LoadNullValue(check.null_reg(), type);
    }
  }
  void LoadInstanceType(TypeCheck& check, const FreezeCacheState& frozen,
                        Label* on_smi) {
    // The check for null_succeeds == true has to be handled by the caller!
    // TODO(mliedtke): Reiterate the null_succeeds case once all generic cast
    // instructions are implemented.
    if (!check.null_succeeds && check.obj_type.is_nullable()) {
      __ emit_cond_jump(kEqual, check.no_match, kRefNull, check.obj_reg,
                        check.null_reg(), frozen);
    }
    __ emit_smi_check(check.obj_reg, on_smi, LiftoffAssembler::kJumpOnSmi,
                      frozen);
    __ LoadMap(check.instance_type(), check.obj_reg);
    __ Load(LiftoffRegister(check.instance_type()), check.instance_type(),
            no_reg, wasm::ObjectAccess::ToTagged(Map::kInstanceTypeOffset),
            LoadType::kI32Load16U);
  }

  // Abstract type checkers. They all fall through on match.
  void StructCheck(TypeCheck& check, const FreezeCacheState& frozen) {
    LoadInstanceType(check, frozen, check.no_match);
    LiftoffRegister instance_type(check.instance_type());
    __ emit_i32_cond_jumpi(kNotEqual, check.no_match, check.instance_type(),
                           WASM_STRUCT_TYPE, frozen);
  }

  void ArrayCheck(TypeCheck& check, const FreezeCacheState& frozen) {
    LoadInstanceType(check, frozen, check.no_match);
    LiftoffRegister instance_type(check.instance_type());
    __ emit_i32_cond_jumpi(kNotEqual, check.no_match, check.instance_type(),
                           WASM_ARRAY_TYPE, frozen);
  }

  void I31Check(TypeCheck& check, const FreezeCacheState& frozen) {
    __ emit_smi_check(check.obj_reg, check.no_match,
                      LiftoffAssembler::kJumpOnNotSmi, frozen);
  }

  void EqCheck(TypeCheck& check, const FreezeCacheState& frozen) {
    Label match;
    LoadInstanceType(check, frozen, &match);
    // We're going to test a range of WasmObject instance types with a single
    // unsigned comparison.
    Register tmp = check.instance_type();
    __ emit_i32_subi(tmp, tmp, FIRST_WASM_OBJECT_TYPE);
    __ emit_i32_cond_jumpi(kUnsignedGreaterThan, check.no_match, tmp,
                           LAST_WASM_OBJECT_TYPE - FIRST_WASM_OBJECT_TYPE,
                           frozen);
    __ bind(&match);
  }

  void StringCheck(TypeCheck& check, const FreezeCacheState& frozen) {
    LoadInstanceType(check, frozen, check.no_match);
    LiftoffRegister instance_type(check.instance_type());
    __ emit_i32_cond_jumpi(kUnsignedGreaterThanEqual, check.no_match,
                           check.instance_type(), FIRST_NONSTRING_TYPE, frozen);
  }

  using TypeChecker = void (LiftoffCompiler::*)(TypeCheck& check,
                                                const FreezeCacheState& frozen);

  template <TypeChecker type_checker>
  void AbstractTypeCheck(const Value& object, bool null_succeeds) {
    Label match, no_match, done;
    TypeCheck check(object.type, &no_match, null_succeeds);
    Initialize(check, kPop, object.type);
    LiftoffRegister result(check.tmp);
    {
      FREEZE_STATE(frozen);

      if (null_succeeds && check.obj_type.is_nullable()) {
        __ emit_cond_jump(kEqual, &match, kRefNull, check.obj_reg,
                          check.null_reg(), frozen);
      }

      (this->*type_checker)(check, frozen);

      __ bind(&match);
      __ LoadConstant(result, WasmValue(1));
      // TODO(jkummerow): Emit near jumps on platforms that have them.
      __ emit_jump(&done);

      __ bind(&no_match);
      __ LoadConstant(result, WasmValue(0));
      __ bind(&done);
    }
    __ PushRegister(kI32, result);
  }

  template <TypeChecker type_checker>
  void AbstractTypeCast(const Value& object, FullDecoder* decoder,
                        bool null_succeeds) {
    Label match;
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapIllegalCast);
    TypeCheck check(object.type, trap_label, null_succeeds);
    Initialize(check, kPeek, object.type);
    FREEZE_STATE(frozen);

    if (null_succeeds && check.obj_type.is_nullable()) {
      __ emit_cond_jump(kEqual, &match, kRefNull, check.obj_reg,
                        check.null_reg(), frozen);
    }
    (this->*type_checker)(check, frozen);
    __ bind(&match);
  }

  template <TypeChecker type_checker>
  void BrOnAbstractType(const Value& object, FullDecoder* decoder,
                        uint32_t br_depth, bool null_succeeds) {
    // Avoid having sequences of branches do duplicate work.
    if (br_depth != decoder->control_depth() - 1) {
      __ PrepareForBranch(decoder->control_at(br_depth)->br_merge()->arity, {});
    }

    Label no_match, match;
    TypeCheck check(object.type, &no_match, null_succeeds);
    Initialize(check, kPeek, object.type);
    FREEZE_STATE(frozen);

    if (null_succeeds && check.obj_type.is_nullable()) {
      __ emit_cond_jump(kEqual, &match, kRefNull, check.obj_reg,
                        check.null_reg(), frozen);
    }

    (this->*type_checker)(check, frozen);
    __ bind(&match);
    BrOrRet(decoder, br_depth);

    __ bind(&no_match);
  }

  template <TypeChecker type_checker>
  void BrOnNonAbstractType(const Value& object, FullDecoder* decoder,
                           uint32_t br_depth, bool null_succeeds) {
    // Avoid having sequences of branches do duplicate work.
    if (br_depth != decoder->control_depth() - 1) {
      __ PrepareForBranch(decoder->control_at(br_depth)->br_merge()->arity, {});
    }

    Label no_match, end;
    TypeCheck check(object.type, &no_match, null_succeeds);
    Initialize(check, kPeek, object.type);
    FREEZE_STATE(frozen);

    if (null_succeeds && check.obj_type.is_nullable()) {
      __ emit_cond_jump(kEqual, &end, kRefNull, check.obj_reg, check.null_reg(),
                        frozen);
    }

    (this->*type_checker)(check, frozen);
    __ emit_jump(&end);

    __ bind(&no_match);
    BrOrRet(decoder, br_depth);

    __ bind(&end);
  }

  void StringNewWtf8(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                     const unibrow::Utf8Variant variant, const Value& offset,
                     const Value& size, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    VarState memory_var{kI32, static_cast<int>(imm.index), 0};

    LiftoffRegister variant_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(variant_reg, static_cast<int32_t>(variant));
    VarState variant_var(kSmiKind, variant_reg, 0);

    VarState& size_var = __ cache_state()->stack_state.end()[-1];

    DCHECK(MatchingMemType(imm.memory, 1));
    VarState address = IndexToVarStateSaturating(1, &pinned);

    CallBuiltin(
        Builtin::kWasmStringNewWtf8,
        MakeSig::Returns(kRefNull).Params(kIntPtrKind, kI32, kI32, kSmiKind),
        {address, size_var, memory_var, variant_var}, decoder->position());
    __ DropValues(2);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringNewWtf8Array(FullDecoder* decoder,
                          const unibrow::Utf8Variant variant,
                          const Value& array, const Value& start,
                          const Value& end, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister array_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-3], pinned));
    MaybeEmitNullCheck(decoder, array_reg.gp(), pinned, array.type);
    VarState array_var(kRef, array_reg, 0);

    LiftoffRegister variant_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(variant_reg, static_cast<int32_t>(variant));
    VarState variant_var(kSmiKind, variant_reg, 0);

    CallBuiltin(Builtin::kWasmStringNewWtf8Array,
                MakeSig::Returns(kRefNull).Params(kI32, kI32, kRef, kSmiKind),
                {
                    __ cache_state()->stack_state.end()[-2],  // start
                    __ cache_state()->stack_state.end()[-1],  // end
                    array_var,
                    variant_var,
                },
                decoder->position());
    __ cache_state()->stack_state.pop_back(3);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringNewWtf16(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                      const Value& offset, const Value& size, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    VarState memory_var{kI32, static_cast<int32_t>(imm.index), 0};

    VarState& size_var = __ cache_state()->stack_state.end()[-1];

    LiftoffRegList pinned;
    DCHECK(MatchingMemType(imm.memory, 1));
    VarState address = IndexToVarStateSaturating(1, &pinned);

    CallBuiltin(Builtin::kWasmStringNewWtf16,
                MakeSig::Returns(kRef).Params(kI32, kIntPtrKind, kI32),
                {memory_var, address, size_var}, decoder->position());
    __ DropValues(2);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringNewWtf16Array(FullDecoder* decoder, const Value& array,
                           const Value& start, const Value& end,
                           Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister array_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-3], pinned));
    MaybeEmitNullCheck(decoder, array_reg.gp(), pinned, array.type);
    VarState array_var(kRef, array_reg, 0);

    CallBuiltin(Builtin::kWasmStringNewWtf16Array,
                MakeSig::Returns(kRef).Params(kRef, kI32, kI32),
                {
                    array_var,
                    __ cache_state()->stack_state.end()[-2],  // start
                    __ cache_state()->stack_state.end()[-1],  // end
                },
                decoder->position());
    __ cache_state()->stack_state.pop_back(3);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringConst(FullDecoder* decoder, const StringConstImmediate& imm,
                   Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    VarState index_var{kI32, static_cast<int32_t>(imm.index), 0};

    CallBuiltin(Builtin::kWasmStringConst, MakeSig::Returns(kRef).Params(kI32),
                {index_var}, decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringMeasureWtf8(FullDecoder* decoder,
                         const unibrow::Utf8Variant variant, const Value& str,
                         Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;
    LiftoffRegister string_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, string_reg.gp(), pinned, str.type);
    VarState string_var(kRef, string_reg, 0);

    Builtin builtin;
    switch (variant) {
      case unibrow::Utf8Variant::kUtf8:
        builtin = Builtin::kWasmStringMeasureUtf8;
        break;
      case unibrow::Utf8Variant::kLossyUtf8:
      case unibrow::Utf8Variant::kWtf8:
        builtin = Builtin::kWasmStringMeasureWtf8;
        break;
      case unibrow::Utf8Variant::kUtf8NoTrap:
        UNREACHABLE();
    }
    CallBuiltin(builtin, MakeSig::Returns(kI32).Params(kRef),
                {
                    string_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringMeasureWtf16(FullDecoder* decoder, const Value& str,
                          Value* result) {
    LiftoffRegList pinned;
    LiftoffRegister string_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, string_reg.gp(), pinned, str.type);
    LiftoffRegister value = __ GetUnusedRegister(kGpReg, pinned);
    LoadObjectField(decoder, value, string_reg.gp(), no_reg,
                    wasm::ObjectAccess::ToTagged(
                        compiler::AccessBuilder::ForStringLength().offset),
                    ValueKind::kI32, false /* is_signed */,
                    false /* trapping */, pinned);
    __ PushRegister(kI32, value);
  }

  void StringEncodeWtf8(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                        const unibrow::Utf8Variant variant, const Value& str,
                        const Value& offset, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    DCHECK(MatchingMemType(imm.memory, 0));
    VarState offset_var = IndexToVarStateSaturating(0, &pinned);

    LiftoffRegister string_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-2], pinned));
    MaybeEmitNullCheck(decoder, string_reg.gp(), pinned, str.type);
    VarState string_var(kRef, string_reg, 0);

    VarState memory_var{kI32, static_cast<int32_t>(imm.index), 0};
    VarState variant_var{kI32, static_cast<int32_t>(variant), 0};

    CallBuiltin(Builtin::kWasmStringEncodeWtf8,
                MakeSig::Returns(kI32).Params(kIntPtrKind, kI32, kI32, kRef),
                {offset_var, memory_var, variant_var, string_var},
                decoder->position());
    __ DropValues(2);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringEncodeWtf8Array(FullDecoder* decoder,
                             const unibrow::Utf8Variant variant,
                             const Value& str, const Value& array,
                             const Value& start, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister array_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-2], pinned));
    MaybeEmitNullCheck(decoder, array_reg.gp(), pinned, array.type);
    VarState array_var(kRef, array_reg, 0);

    LiftoffRegister string_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-3], pinned));
    MaybeEmitNullCheck(decoder, string_reg.gp(), pinned, str.type);
    VarState string_var(kRef, string_reg, 0);

    VarState& start_var = __ cache_state()->stack_state.end()[-1];

    LiftoffRegister variant_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(variant_reg, static_cast<int32_t>(variant));
    VarState variant_var(kSmiKind, variant_reg, 0);

    CallBuiltin(Builtin::kWasmStringEncodeWtf8Array,
                MakeSig::Returns(kI32).Params(kRef, kRef, kI32, kSmiKind),
                {
                    string_var,
                    array_var,
                    start_var,
                    variant_var,
                },
                decoder->position());
    __ DropValues(3);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringEncodeWtf16(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                         const Value& str, const Value& offset, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    DCHECK(MatchingMemType(imm.memory, 0));
    VarState offset_var = IndexToVarStateSaturating(0, &pinned);

    LiftoffRegister string_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-2], pinned));
    MaybeEmitNullCheck(decoder, string_reg.gp(), pinned, str.type);
    VarState string_var(kRef, string_reg, 0);

    VarState memory_var{kI32, static_cast<int32_t>(imm.index), 0};

    CallBuiltin(Builtin::kWasmStringEncodeWtf16,
                MakeSig::Returns(kI32).Params(kRef, kIntPtrKind, kI32),
                {string_var, offset_var, memory_var}, decoder->position());
    __ DropValues(2);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringEncodeWtf16Array(FullDecoder* decoder, const Value& str,
                              const Value& array, const Value& start,
                              Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister array_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-2], pinned));
    MaybeEmitNullCheck(decoder, array_reg.gp(), pinned, array.type);
    VarState array_var(kRef, array_reg, 0);

    LiftoffRegister string_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-3], pinned));
    MaybeEmitNullCheck(decoder, string_reg.gp(), pinned, str.type);
    VarState string_var(kRef, string_reg, 0);

    VarState& start_var = __ cache_state()->stack_state.end()[-1];

    CallBuiltin(Builtin::kWasmStringEncodeWtf16Array,
                MakeSig::Returns(kI32).Params(kRef, kRef, kI32),
                {
                    string_var,
                    array_var,
                    start_var,
                },
                decoder->position());
    __ DropValues(3);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringConcat(FullDecoder* decoder, const Value& head, const Value& tail,
                    Value* result) {
    FUZZER_HEAVY_INSTRUCTION;  // Fast, but may create very long strings.
    LiftoffRegList pinned;

    LiftoffRegister tail_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, tail_reg.gp(), pinned, tail.type);
    VarState tail_var(kRef, tail_reg, 0);

    LiftoffRegister head_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, head_reg.gp(), pinned, head.type);
    VarState head_var(kRef, head_reg, 0);

    CallBuiltin(Builtin::kWasmStringConcat,
                MakeSig::Returns(kRef).Params(kRef, kRef),
                {
                    head_var,
                    tail_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringEq(FullDecoder* decoder, const Value& a, const Value& b,
                Value* result) {
    FUZZER_HEAVY_INSTRUCTION;  // Slow path is linear in string length.
    LiftoffRegister result_reg(kReturnRegister0);
    LiftoffRegList pinned{result_reg};
    LiftoffRegister b_reg = pinned.set(__ PopToModifiableRegister(pinned));
    LiftoffRegister a_reg = pinned.set(__ PopToModifiableRegister(pinned));

    __ SpillAllRegisters();

    Label done;

    {
      LiftoffRegister null = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      bool check_for_null = a.type.is_nullable() || b.type.is_nullable();
      if (check_for_null) {
        LoadNullValueForCompare(null.gp(), pinned, kWasmStringRef);
      }

      FREEZE_STATE(frozen);

      // If values pointer-equal, result is 1.
      __ LoadConstant(result_reg, WasmValue(int32_t{1}));
      __ emit_cond_jump(kEqual, &done, kRefNull, a_reg.gp(), b_reg.gp(),
                        frozen);

      // Otherwise if either operand is null, result is 0.
      if (check_for_null) {
        __ LoadConstant(result_reg, WasmValue(int32_t{0}));
        if (a.type.is_nullable()) {
          __ emit_cond_jump(kEqual, &done, kRefNull, a_reg.gp(), null.gp(),
                            frozen);
        }
        if (b.type.is_nullable()) {
          __ emit_cond_jump(kEqual, &done, kRefNull, b_reg.gp(), null.gp(),
                            frozen);
        }
      }

      // Ending the frozen state here is fine, because we already spilled the
      // rest of the cache, and the subsequent runtime call will reset the cache
      // state anyway.
    }

    // Operands are pointer-distinct and neither is null; call out to the
    // runtime.
    VarState a_var(kRef, a_reg, 0);
    VarState b_var(kRef, b_reg, 0);
    CallBuiltin(Builtin::kWasmStringEqual,
                MakeSig::Returns(kI32).Params(kRef, kRef),
                {
                    a_var,
                    b_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    __ bind(&done);

    __ PushRegister(kI32, result_reg);
  }

  void StringIsUSVSequence(FullDecoder* decoder, const Value& str,
                           Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister str_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, str_reg.gp(), pinned, str.type);
    VarState str_var(kRef, str_reg, 0);

    CallBuiltin(Builtin::kWasmStringIsUSVSequence,
                MakeSig::Returns(kI32).Params(kRef),
                {
                    str_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringAsWtf8(FullDecoder* decoder, const Value& str, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    LiftoffRegister str_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, str_reg.gp(), pinned, str.type);
    VarState str_var(kRef, str_reg, 0);

    CallBuiltin(Builtin::kWasmStringAsWtf8, MakeSig::Returns(kRef).Params(kRef),
                {
                    str_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringViewWtf8Advance(FullDecoder* decoder, const Value& view,
                             const Value& pos, const Value& bytes,
                             Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    VarState& bytes_var = __ cache_state()->stack_state.end()[-1];
    VarState& pos_var = __ cache_state()->stack_state.end()[-2];

    LiftoffRegister view_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-3], pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);

    CallBuiltin(Builtin::kWasmStringViewWtf8Advance,
                MakeSig::Returns(kI32).Params(kRef, kI32, kI32),
                {
                    view_var,
                    pos_var,
                    bytes_var,
                },
                decoder->position());
    __ DropValues(3);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringViewWtf8Encode(FullDecoder* decoder,
                            const MemoryIndexImmediate& imm,
                            const unibrow::Utf8Variant variant,
                            const Value& view, const Value& addr,
                            const Value& pos, const Value& bytes,
                            Value* next_pos, Value* bytes_written) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    VarState& bytes_var = __ cache_state()->stack_state.end()[-1];
    VarState& pos_var = __ cache_state()->stack_state.end()[-2];

    DCHECK(MatchingMemType(imm.memory, 2));
    VarState addr_var = IndexToVarStateSaturating(2, &pinned);

    LiftoffRegister view_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-4], pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);

    // TODO(jkummerow): Support Smi offsets when constructing {VarState}s
    // directly; avoid requesting a register.
    LiftoffRegister memory_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(memory_reg, imm.index);
    VarState memory_var(kSmiKind, memory_reg, 0);

    LiftoffRegister variant_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(variant_reg, static_cast<int32_t>(variant));
    VarState variant_var(kSmiKind, variant_reg, 0);

    CallBuiltin(
        Builtin::kWasmStringViewWtf8Encode,
        MakeSig::Returns(kI32, kI32)
            .Params(kIntPtrKind, kI32, kI32, kRef, kSmiKind, kSmiKind),
        {addr_var, pos_var, bytes_var, view_var, memory_var, variant_var},
        decoder->position());
    __ DropValues(4);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister next_pos_reg(kReturnRegister0);
    __ PushRegister(kI32, next_pos_reg);
    LiftoffRegister bytes_written_reg(kReturnRegister1);
    __ PushRegister(kI32, bytes_written_reg);
  }

  void StringViewWtf8Slice(FullDecoder* decoder, const Value& view,
                           const Value& start, const Value& end,
                           Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    VarState& end_var = __ cache_state()->stack_state.end()[-1];
    VarState& start_var = __ cache_state()->stack_state.end()[-2];

    LiftoffRegister view_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-3], pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);

    CallBuiltin(Builtin::kWasmStringViewWtf8Slice,
                MakeSig::Returns(kRef).Params(kRef, kI32, kI32),
                {
                    view_var,
                    start_var,
                    end_var,
                },
                decoder->position());
    __ DropValues(3);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringAsWtf16(FullDecoder* decoder, const Value& str, Value* result) {
    LiftoffRegList pinned;

    LiftoffRegister str_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, str_reg.gp(), pinned, str.type);
    VarState str_var(kRef, str_reg, 0);

    CallBuiltin(Builtin::kWasmStringAsWtf16,
                MakeSig::Returns(kRef).Params(kRef),
                {
                    str_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringViewWtf16GetCodeUnit(FullDecoder* decoder, const Value& view,
                                  const Value& pos, Value* result) {
    LiftoffRegList pinned;
    LiftoffRegister pos_reg = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister view_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);
    VarState pos_var(kI32, pos_reg, 0);

    CallBuiltin(Builtin::kWasmStringViewWtf16GetCodeUnit,
                MakeSig::Returns(kI32).Params(kRef, kI32), {view_var, pos_var},
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringViewWtf16Encode(FullDecoder* decoder,
                             const MemoryIndexImmediate& imm, const Value& view,
                             const Value& offset, const Value& pos,
                             const Value& codeunits, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    VarState& codeunits_var = __ cache_state()->stack_state.end()[-1];
    VarState& pos_var = __ cache_state()->stack_state.end()[-2];

    DCHECK(MatchingMemType(imm.memory, 2));
    VarState offset_var = IndexToVarStateSaturating(2, &pinned);

    LiftoffRegister view_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-4], pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);

    LiftoffRegister memory_reg =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LoadSmi(memory_reg, imm.index);
    VarState memory_var(kSmiKind, memory_reg, 0);

    CallBuiltin(
        Builtin::kWasmStringViewWtf16Encode,
        MakeSig::Returns(kI32).Params(kIntPtrKind, kI32, kI32, kRef, kSmiKind),
        {offset_var, pos_var, codeunits_var, view_var, memory_var},
        decoder->position());
    __ DropValues(4);
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringViewWtf16Slice(FullDecoder* decoder, const Value& view,
                            const Value& start, const Value& end,
                            Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;
    LiftoffRegister end_reg = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister start_reg = pinned.set(__ PopToRegister(pinned));
    LiftoffRegister view_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);
    VarState start_var(kI32, start_reg, 0);
    VarState end_var(kI32, end_reg, 0);

    CallBuiltin(Builtin::kWasmStringViewWtf16Slice,
                MakeSig::Returns(kRef).Params(kRef, kI32, kI32),
                {
                    view_var,
                    start_var,
                    end_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringAsIter(FullDecoder* decoder, const Value& str, Value* result) {
    LiftoffRegList pinned;

    LiftoffRegister str_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, str_reg.gp(), pinned, str.type);
    VarState str_var(kRef, str_reg, 0);

    CallBuiltin(Builtin::kWasmStringAsIter, MakeSig::Returns(kRef).Params(kRef),
                {
                    str_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kRef, result_reg);
  }

  void StringViewIterNext(FullDecoder* decoder, const Value& view,
                          Value* result) {
    LiftoffRegList pinned;

    LiftoffRegister view_reg = pinned.set(__ PopToRegister(pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);

    CallBuiltin(Builtin::kWasmStringViewIterNext,
                MakeSig::Returns(kI32).Params(kRef),
                {
                    view_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringViewIterAdvance(FullDecoder* decoder, const Value& view,
                             const Value& codepoints, Value* result) {
    LiftoffRegList pinned;

    VarState& codepoints_var = __ cache_state()->stack_state.end()[-1];

    LiftoffRegister view_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-2], pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);

    CallBuiltin(Builtin::kWasmStringViewIterAdvance,
                MakeSig::Returns(kI32).Params(kRef, kI32),
                {
                    view_var,
                    codepoints_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ DropValues(2);
    __ PushRegister(kI32, result_reg);
  }

  void StringViewIterRewind(FullDecoder* decoder, const Value& view,
                            const Value& codepoints, Value* result) {
    LiftoffRegList pinned;

    VarState& codepoints_var = __ cache_state()->stack_state.end()[-1];

    LiftoffRegister view_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-2], pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);

    CallBuiltin(Builtin::kWasmStringViewIterRewind,
                MakeSig::Returns(kI32).Params(kRef, kI32),
                {
                    view_var,
                    codepoints_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ DropValues(2);
    __ PushRegister(kI32, result_reg);
  }

  void StringViewIterSlice(FullDecoder* decoder, const Value& view,
                           const Value& codepoints, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;

    VarState& codepoints_var = __ cache_state()->stack_state.end()[-1];

    LiftoffRegister view_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-2], pinned));
    MaybeEmitNullCheck(decoder, view_reg.gp(), pinned, view.type);
    VarState view_var(kRef, view_reg, 0);

    CallBuiltin(Builtin::kWasmStringViewIterSlice,
                MakeSig::Returns(kRef).Params(kRef, kI32),
                {
                    view_var,
                    codepoints_var,
                },
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ DropValues(2);
    __ PushRegister(kRef, result_reg);
  }

  void StringCompare(FullDecoder* decoder, const Value& lhs, const Value& rhs,
                     Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;
    LiftoffRegister rhs_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-1], pinned));
    MaybeEmitNullCheck(decoder, rhs_reg.gp(), pinned, rhs.type);
    VarState rhs_var(kRef, rhs_reg, 0);

    LiftoffRegister lhs_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-2], pinned));
    MaybeEmitNullCheck(decoder, lhs_reg.gp(), pinned, lhs.type);
    VarState lhs_var(kRef, lhs_reg, 0);

    CallBuiltin(Builtin::kStringCompare,
                MakeSig::Returns(kSmiKind).Params(kRef, kRef),
                {lhs_var, rhs_var}, decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ DropValues(2);
    __ SmiToInt32(kReturnRegister0);
    __ PushRegister(kI32, result_reg);
  }

  void StringFromCodePoint(FullDecoder* decoder, const Value& code_point,
                           Value* result) {
    VarState& codepoint_var = __ cache_state()->stack_state.end()[-1];

    CallBuiltin(Builtin::kWasmStringFromCodePoint,
                MakeSig::Returns(kRef).Params(kI32), {codepoint_var},
                decoder->position());
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);

    LiftoffRegister result_reg(kReturnRegister0);
    __ DropValues(1);
    __ PushRegister(kRef, result_reg);
  }

  void StringHash(FullDecoder* decoder, const Value& string, Value* result) {
    FUZZER_HEAVY_INSTRUCTION;
    LiftoffRegList pinned;
    LiftoffRegister string_reg = pinned.set(
        __ LoadToRegister(__ cache_state()->stack_state.end()[-1], pinned));
    MaybeEmitNullCheck(decoder, string_reg.gp(), pinned, string.type);
    VarState string_var(kRef, string_reg, 0);

    CallBuiltin(Builtin::kWasmStringHash, MakeSig::Returns(kI32).Params(kRef),
                {string_var}, decoder->position());

    LiftoffRegister result_reg(kReturnRegister0);
    __ DropValues(1);
    __ PushRegister(kI32, result_reg);
  }

  void Forward(FullDecoder* decoder, const Value& from, Value* to) {
    // Nothing to do here.
  }

 private:
  void CallDirect(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[], Value returns[], TailCall tail_call) {
    MostlySmallValueKindSig sig(zone_, imm.sig);
    for (ValueKind ret : sig.returns()) {
      if (!CheckSupportedType(decoder, ret, "return")) return;
    }

    auto call_descriptor = compiler::GetWasmCallDescriptor(zone_, imm.sig);
    call_descriptor = GetLoweredCallDescriptor(zone_, call_descriptor);

    // One slot would be enough for call_direct, but would make index
    // computations much more complicated.
    size_t vector_slot = encountered_call_instructions_.size() * 2;
    if (inlining_enabled(decoder)) {
      encountered_call_instructions_.push_back(imm.index);
    }

    if (imm.index < env_->module->num_imported_functions) {
      // A direct call to an imported function.
      FUZZER_HEAVY_INSTRUCTION;
      LiftoffRegList pinned;
      Register implicit_arg =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
      Register target = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();

      {
        SCOPED_CODE_COMMENT("Load ref and target for imported function");
        Register dispatch_table = target;
        LOAD_PROTECTED_PTR_INSTANCE_FIELD(dispatch_table,
                                          DispatchTableForImports, pinned);
        __ LoadProtectedPointer(
            implicit_arg, dispatch_table,
            ObjectAccess::ToTagged(WasmDispatchTable::OffsetOf(imm.index) +
                                   WasmDispatchTable::kImplicitArgBias));

        __ LoadFullPointer(
            target, dispatch_table,
            ObjectAccess::ToTagged(WasmDispatchTable::OffsetOf(imm.index) +
                                   WasmDispatchTable::kTargetBias));
      }

      __ PrepareCall(&sig, call_descriptor, &target, implicit_arg);
      if (tail_call) {
        __ PrepareTailCall(
            static_cast<int>(call_descriptor->ParameterSlotCount()),
            static_cast<int>(
                call_descriptor->GetStackParameterDelta(descriptor_)));
        __ TailCallIndirect(target);
      } else {
        source_position_table_builder_.AddPosition(
            __ pc_offset(), SourcePosition(decoder->position()), true);
        __ CallIndirect(&sig, call_descriptor, target);
        FinishCall(decoder, &sig, call_descriptor);
      }
    } else {
      // Inlining direct calls isn't speculative, but existence of the
      // feedback vector currently depends on this flag.
      if (inlining_enabled(decoder)) {
        LiftoffRegister vector = __ GetUnusedRegister(kGpReg, {});
        __ Fill(vector, WasmLiftoffFrameConstants::kFeedbackVectorOffset,
                kIntPtrKind);
        __ IncrementSmi(vector,
                        wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(
                            static_cast<int>(vector_slot)));
        // Warning: {vector} may be clobbered by {IncrementSmi}!
      }
      // A direct call within this module just gets the current instance.
      __ PrepareCall(&sig, call_descriptor);
      // Just encode the function index. This will be patched at instantiation.
      Address addr = static_cast<Address>(imm.index);
      if (tail_call) {
        DCHECK(descriptor_->CanTailCall(call_descriptor));
        __ PrepareTailCall(
            static_cast<int>(call_descriptor->ParameterSlotCount()),
            static_cast<int>(
                call_descriptor->GetStackParameterDelta(descriptor_)));
        __ TailCallNativeWasmCode(addr);
      } else {
        source_position_table_builder_.AddPosition(
            __ pc_offset(), SourcePosition(decoder->position()), true);
        __ CallNativeWasmCode(addr);
        FinishCall(decoder, &sig, call_descriptor);
      }
    }
  }

  void CallIndirectImpl(FullDecoder* decoder, const CallIndirectImmediate& imm,
                        TailCall tail_call) {
    MostlySmallValueKindSig sig(zone_, imm.sig);
    for (ValueKind ret : sig.returns()) {
      if (!CheckSupportedType(decoder, ret, "return")) return;
    }
    const WasmTable* table = imm.table_imm.table;

    if (v8_flags.wasm_deopt &&
        env_->deopt_info_bytecode_offset == decoder->pc_offset() &&
        env_->deopt_location_kind == LocationKindForDeopt::kEagerDeopt) {
      EmitDeoptPoint(decoder);
    }

    LiftoffRegList pinned;
    VarState index_slot = IndexToVarStateSaturating(0, &pinned);

    const bool is_static_index = index_slot.is_const();
    Register index_reg =
        is_static_index
            ? no_reg
            : pinned.set(__ LoadToRegister(index_slot, pinned).gp());

    const uint32_t max_table_size =
        table->has_maximum_size
            ? std::min(table->maximum_size, uint32_t{kV8MaxWasmTableSize})
            : uint32_t{kV8MaxWasmTableSize};
    const bool statically_oob =
        is_static_index &&
        static_cast<uint32_t>(index_slot.i32_const()) >= max_table_size;

    TempRegisterScope temps;
    pinned |= temps.AddTempRegisters(3, kGpReg, &asm_, pinned);

    ScopedTempRegister dispatch_table{temps, kGpReg};
    if (imm.table_imm.index == 0) {
      // Load the dispatch table directly.
      LOAD_PROTECTED_PTR_INSTANCE_FIELD(dispatch_table.gp_reg(), DispatchTable0,
                                        pinned);
    } else {
      // Load the dispatch table from the ProtectedFixedArray of all dispatch
      // tables.
      Register dispatch_tables = dispatch_table.gp_reg();
      LOAD_PROTECTED_PTR_INSTANCE_FIELD(dispatch_tables, DispatchTables,
                                        pinned);
      __ LoadProtectedPointer(dispatch_table.gp_reg(), dispatch_tables,
                              ObjectAccess::ElementOffsetInProtectedFixedArray(
                                  imm.table_imm.index));
    }

    {
      SCOPED_CODE_COMMENT("Check index is in-bounds");
      // Bounds check against the table size: Compare against the dispatch table
      // size, or a constant if the size is statically known.
      const bool needs_dynamic_size =
          !table->has_maximum_size ||
          table->maximum_size != table->initial_size;

      Label* out_of_bounds_label =
          AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapTableOutOfBounds);

      ScopedTempRegister table_size{temps, kGpReg};
      FREEZE_STATE(trapping);
      if (statically_oob) {
        __ emit_jump(out_of_bounds_label);
        // This case is unlikely to happen in production. Thus we just continue
        // generating code afterwards, to make sure that the stack is in a
        // consistent state for following instructions.
      } else if (needs_dynamic_size) {
        __ Load(table_size.reg(), dispatch_table.gp_reg(), no_reg,
                wasm::ObjectAccess::ToTagged(WasmDispatchTable::kLengthOffset),
                LoadType::kI32Load);

        if (is_static_index) {
          __ emit_i32_cond_jumpi(kUnsignedLessThanEqual, out_of_bounds_label,
                                 table_size.gp_reg(), index_slot.i32_const(),
                                 trapping);
        } else {
          ValueKind comparison_type = kI32;
          if (Is64() && table->is_table64) {
            // {index_reg} is a uintptr, so do a ptrsize comparison.
            __ emit_u32_to_uintptr(table_size.gp_reg(), table_size.gp_reg());
            comparison_type = kIntPtrKind;
          }
          __ emit_cond_jump(kUnsignedLessThanEqual, out_of_bounds_label,
                            comparison_type, table_size.gp_reg(), index_reg,
                            trapping);
        }
      } else {
        DCHECK_EQ(max_table_size, table->initial_size);
        if (is_static_index) {
          DCHECK_LT(index_slot.i32_const(), max_table_size);
        } else if (Is64() && table->is_table64) {
          // On 32-bit, this is the same as below, so include the `Is64()` test
          // to statically tell the compiler to skip this branch.
          // Note: {max_table_size} will be sign-extended, which is fine because
          // the MSB is known to be 0 (asserted by the static_assert below).
          static_assert(kV8MaxWasmTableSize <= kMaxInt);
          __ emit_ptrsize_cond_jumpi(kUnsignedGreaterThanEqual,
                                     out_of_bounds_label, index_reg,
                                     max_table_size, trapping);
        } else {
          __ emit_i32_cond_jumpi(kUnsignedGreaterThanEqual, out_of_bounds_label,
                                 index_reg, max_table_size, trapping);
        }
      }
    }

    // If the function index is dynamic, compute a pointer to the dispatch table
    // entry. Otherwise remember the static offset from the dispatch table to
    // add it to later loads from that table.
    ScopedTempRegister dispatch_table_base{std::move(dispatch_table)};
    int dispatch_table_offset = 0;
    if (is_static_index) {
      // Avoid potential integer overflow here by excluding too large
      // (statically OOB) indexes. This code is not reached for statically OOB
      // indexes anyway.
      dispatch_table_offset =
          statically_oob
              ? 0
              : wasm::ObjectAccess::ToTagged(
                    WasmDispatchTable::OffsetOf(index_slot.i32_const()));
    } else {
      // TODO(clemensb): Produce better code for this (via more specialized
      // platform-specific methods?).

      Register entry_offset = index_reg;
      // After this computation we don't need the index register any more. If
      // there is no other user we can overwrite it.
      bool index_reg_still_used =
          __ cache_state() -> get_use_count(LiftoffRegister{index_reg}) > 1;
      if (index_reg_still_used) entry_offset = temps.Acquire(kGpReg).gp();

      __ emit_u32_to_uintptr(entry_offset, index_reg);
      index_reg = no_reg;
      __ emit_ptrsize_muli(entry_offset, entry_offset,
                           WasmDispatchTable::kEntrySize);
      __ emit_ptrsize_add(dispatch_table_base.gp_reg(),
                          dispatch_table_base.gp_reg(), entry_offset);
      if (index_reg_still_used) temps.Return(std::move(entry_offset));
      dispatch_table_offset =
          wasm::ObjectAccess::ToTagged(WasmDispatchTable::kEntriesOffset);
    }

    bool needs_type_check = !EquivalentTypes(
        table->type.AsNonNull(), ValueType::Ref(imm.sig_imm.index),
        decoder->module_, decoder->module_);
    bool needs_null_check = table->type.is_nullable();

    // We do both the type check and the null check by checking the signature,
    // so this shares most code. For the null check we then only check if the
    // stored signature is != -1.
    if (needs_type_check || needs_null_check) {
      SCOPED_CODE_COMMENT(needs_type_check ? "Check signature"
                                           : "Check for null entry");
      ScopedTempRegister real_sig_id{temps, kGpReg};

      // Load the signature from the dispatch table.
      __ Load(real_sig_id.reg(), dispatch_table_base.gp_reg(), no_reg,
              dispatch_table_offset + WasmDispatchTable::kSigBias,
              LoadType::kI32Load);

      // Compare against expected signature.
      // Since Liftoff code is never serialized (hence not reused across
      // isolates / processes) the canonical signature ID is a static integer.
      uint32_t canonical_sig_id =
          decoder->module_->isorecursive_canonical_type_ids[imm.sig_imm.index];
      Label* sig_mismatch_label =
          AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapFuncSigMismatch);
      __ DropValues(1);

      if (!needs_type_check) {
        DCHECK(needs_null_check);
        // Only check for -1 (nulled table entry).
        FREEZE_STATE(frozen);
        __ emit_i32_cond_jumpi(kEqual, sig_mismatch_label, real_sig_id.gp_reg(),
                               -1, frozen);
      } else if (!decoder->module_->types[imm.sig_imm.index].is_final) {
        Label success_label;
        FREEZE_STATE(frozen);
        __ emit_i32_cond_jumpi(kEqual, &success_label, real_sig_id.gp_reg(),
                               canonical_sig_id, frozen);
        if (needs_null_check) {
          __ emit_i32_cond_jumpi(kEqual, sig_mismatch_label,
                                 real_sig_id.gp_reg(), -1, frozen);
        }
        ScopedTempRegister real_rtt{temps, kGpReg};
        __ LoadFullPointer(
            real_rtt.gp_reg(), kRootRegister,
            IsolateData::root_slot_offset(RootIndex::kWasmCanonicalRtts));
        __ LoadTaggedPointer(
            real_rtt.gp_reg(), real_rtt.gp_reg(), real_sig_id.gp_reg(),
            ObjectAccess::ToTagged(WeakArrayList::kHeaderSize), nullptr, true);
        // real_sig_id is not used any more.
        real_sig_id.Reset();
        // Remove the weak reference tag.
        if constexpr (kSystemPointerSize == 4) {
          __ emit_i32_andi(real_rtt.gp_reg(), real_rtt.gp_reg(),
                           static_cast<int32_t>(~kWeakHeapObjectMask));
        } else {
          __ emit_i64_andi(real_rtt.reg(), real_rtt.reg(),
                           static_cast<int32_t>(~kWeakHeapObjectMask));
        }
        // Constant-time subtyping check: load exactly one candidate RTT from
        // the supertypes list.
        // Step 1: load the WasmTypeInfo.
        constexpr int kTypeInfoOffset = wasm::ObjectAccess::ToTagged(
            Map::kConstructorOrBackPointerOrNativeContextOffset);
        ScopedTempRegister type_info{std::move(real_rtt)};
        __ LoadTaggedPointer(type_info.gp_reg(), type_info.gp_reg(), no_reg,
                             kTypeInfoOffset);
        // Step 2: check the list's length if needed.
        uint32_t rtt_depth =
            GetSubtypingDepth(decoder->module_, imm.sig_imm.index);
        if (rtt_depth >= kMinimumSupertypeArraySize) {
          ScopedTempRegister list_length{temps, kGpReg};
          int offset =
              ObjectAccess::ToTagged(WasmTypeInfo::kSupertypesLengthOffset);
          __ LoadSmiAsInt32(list_length.reg(), type_info.gp_reg(), offset);
          __ emit_i32_cond_jumpi(kUnsignedLessThanEqual, sig_mismatch_label,
                                 list_length.gp_reg(), rtt_depth, frozen);
        }
        // Step 3: load the candidate list slot, and compare it.
        ScopedTempRegister maybe_match{std::move(type_info)};
        __ LoadTaggedPointer(
            maybe_match.gp_reg(), maybe_match.gp_reg(), no_reg,
            ObjectAccess::ToTagged(WasmTypeInfo::kSupertypesOffset +
                                   rtt_depth * kTaggedSize));
        ScopedTempRegister formal_rtt{temps, kGpReg};
        // Instead of {pinned}, we use {kGpCacheRegList} as the list of pinned
        // registers, to prevent any attempt to cache the instance, which would
        // be incompatible with the {FREEZE_STATE} that is in effect here.
        LOAD_TAGGED_PTR_INSTANCE_FIELD(formal_rtt.gp_reg(), ManagedObjectMaps,
                                       kGpCacheRegList);
        __ LoadTaggedPointer(
            formal_rtt.gp_reg(), formal_rtt.gp_reg(), no_reg,
            wasm::ObjectAccess::ElementOffsetInTaggedFixedArray(
                imm.sig_imm.index));
        __ emit_cond_jump(kNotEqual, sig_mismatch_label, kRtt,
                          formal_rtt.gp_reg(), maybe_match.gp_reg(), frozen);

        __ bind(&success_label);
      } else {
        FREEZE_STATE(trapping);
        __ emit_i32_cond_jumpi(kNotEqual, sig_mismatch_label,
                               real_sig_id.gp_reg(), canonical_sig_id,
                               trapping);
      }
    } else {
      __ DropValues(1);
    }

    {
      SCOPED_CODE_COMMENT("Execute indirect call");

      // The first parameter will be either a WasmTrustedInstanceData or a
      // WasmImportData.
      Register implicit_arg = temps.Acquire(kGpReg).gp();
      Register target = temps.Acquire(kGpReg).gp();

      {
        SCOPED_CODE_COMMENT("Load implicit arg and target from dispatch table");
        __ LoadProtectedPointer(
            implicit_arg, dispatch_table_base.gp_reg(),
            dispatch_table_offset + WasmDispatchTable::kImplicitArgBias);
        __ Load(LiftoffRegister(target), dispatch_table_base.gp_reg(), no_reg,
                dispatch_table_offset + WasmDispatchTable::kTargetBias,
                LoadType::ForValueKind(kIntPtrKind));
      }

      if (v8_flags.wasm_inlining_call_indirect) {
        SCOPED_CODE_COMMENT("Feedback collection for speculative inlining");

        ScopedTempRegister vector{std::move(dispatch_table_base)};
        __ Fill(vector.reg(), WasmLiftoffFrameConstants::kFeedbackVectorOffset,
                kRef);
        VarState vector_var{kRef, vector.reg(), 0};

        // A constant `uint32_t` is sufficient for the vector slot index.
        // The number of call instructions (and hence feedback vector slots) is
        // capped by the number of instructions, which is capped by the maximum
        // function body size.
        static_assert(kV8MaxWasmFunctionSize <
                      std::numeric_limits<uint32_t>::max() / 2);
        uint32_t vector_slot =
            static_cast<uint32_t>(encountered_call_instructions_.size()) * 2;
        encountered_call_instructions_.push_back(
            FunctionTypeFeedback::kCallIndirect);
        VarState index_var(kI32, vector_slot, 0);

        // Thread the target and ref through the builtin call (i.e., pass them
        // as parameters and return them unchanged) as `CallBuiltin` otherwise
        // clobbers them. (The spilling code in `SpillAllRegisters` is only
        // aware of registers used on Liftoff's abstract value stack, not the
        // ones manually allocated above.)
        // TODO(335082212): We could avoid this and reduce the code size for
        // each call_indirect by moving the target and ref lookup into the
        // builtin as well.
        // However, then we would either (a) need to replicate the optimizations
        // above for static indices etc., which increases code duplication and
        // maintenance cost, or (b) regress performance even more than the
        // builtin call itself already does.
        // All in all, let's keep it simple at first, i.e., share the maximum
        // amount of code when inlining is enabled vs. not.
        VarState target_var(kIntPtrKind, LiftoffRegister(target), 0);
        VarState implicit_arg_var(kRef, LiftoffRegister(implicit_arg), 0);

        // CallIndirectIC(vector: FixedArray, vectorIndex: int32,
        //                target: RawPtr,
        //                ref: WasmTrustedInstanceData|WasmImportData)
        //               -> <target, implicit_arg>
        CallBuiltin(Builtin::kCallIndirectIC,
                    MakeSig::Returns(kIntPtrKind, kIntPtrKind)
                        .Params(kRef, kI32, kIntPtrKind, kRef),
                    {vector_var, index_var, target_var, implicit_arg_var},
                    decoder->position());
        target = kReturnRegister0;
        implicit_arg = kReturnRegister1;
      }

      auto call_descriptor = compiler::GetWasmCallDescriptor(zone_, imm.sig);
      call_descriptor = GetLoweredCallDescriptor(zone_, call_descriptor);

      __ PrepareCall(&sig, call_descriptor, &target, implicit_arg);
      if (tail_call) {
        __ PrepareTailCall(
            static_cast<int>(call_descriptor->ParameterSlotCount()),
            static_cast<int>(
                call_descriptor->GetStackParameterDelta(descriptor_)));
        __ TailCallIndirect(target);
      } else {
        source_position_table_builder_.AddPosition(
            __ pc_offset(), SourcePosition(decoder->position()), true);
        __ CallIndirect(&sig, call_descriptor, target);
        FinishCall(decoder, &sig, call_descriptor);
      }
    }
  }

  void StoreFrameDescriptionForDeopt(FullDecoder* decoder) {
    DCHECK(v8_flags.wasm_deopt);
    DCHECK(!frame_description_);
    frame_description_ = std::make_unique<LiftoffFrameDescriptionForDeopt>(
        LiftoffFrameDescriptionForDeopt{
            decoder->pc_offset(), static_cast<uint32_t>(__ pc_offset()),
            std::vector<LiftoffVarState>(__ cache_state()->stack_state.begin(),
                                         __ cache_state()->stack_state.end()),
            __ cache_state()->cached_instance_data});
  }

  void EmitDeoptPoint(FullDecoder* decoder) {
#if defined(DEBUG) and !defined(V8_TARGET_ARCH_ARM)
    // Liftoff may only use "allocatable registers" as defined by the
    // RegisterConfiguration. (The deoptimizer will not handle non-allocatable
    // registers).
    // Note that this DCHECK is skipped for arm 32 bit as its deoptimizer
    // decides to handle all available double / simd registers.
    const RegisterConfiguration* config = RegisterConfiguration::Default();
    DCHECK_LE(kLiftoffAssemblerFpCacheRegs.Count(),
              config->num_allocatable_simd128_registers());
    for (DoubleRegister reg : kLiftoffAssemblerFpCacheRegs) {
      const int* end = config->allocatable_simd128_codes() +
                       config->num_allocatable_simd128_registers();
      DCHECK(std::find(config->allocatable_simd128_codes(), end, reg.code()) !=
             end);
    }
#endif

    LiftoffAssembler::CacheState initial_state(zone_);
    initial_state.Split(*__ cache_state());
    // TODO(mliedtke): The deopt point should be in out-of-line-code.
    Label deopt_point;
    Label callref;
    __ emit_jump(&callref);
    __ bind(&deopt_point);
    StoreFrameDescriptionForDeopt(decoder);
    CallBuiltin(Builtin::kWasmLiftoffDeoptFinish, MakeSig(), {},
                kNoSourcePosition);
    __ MergeStackWith(initial_state, 0, LiftoffAssembler::kForwardJump);
    __ cache_state() -> Steal(initial_state);
    __ bind(&callref);
  }

  void CallRefImpl(FullDecoder* decoder, ValueType func_ref_type,
                   const FunctionSig* type_sig, TailCall tail_call) {
    MostlySmallValueKindSig sig(zone_, type_sig);
    for (ValueKind ret : sig.returns()) {
      if (!CheckSupportedType(decoder, ret, "return")) return;
    }
    compiler::CallDescriptor* call_descriptor =
        compiler::GetWasmCallDescriptor(zone_, type_sig);
    call_descriptor = GetLoweredCallDescriptor(zone_, call_descriptor);

    Register target_reg = no_reg;
    Register implicit_arg_reg = no_reg;

    if (inlining_enabled(decoder)) {
      if (v8_flags.wasm_deopt &&
          env_->deopt_info_bytecode_offset == decoder->pc_offset() &&
          env_->deopt_location_kind == LocationKindForDeopt::kEagerDeopt) {
        EmitDeoptPoint(decoder);
      }
      LiftoffRegList pinned;
      LiftoffRegister func_ref = pinned.set(__ PopToRegister(pinned));
      LiftoffRegister vector = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      MaybeEmitNullCheck(decoder, func_ref.gp(), pinned, func_ref_type);
      VarState func_ref_var(kRef, func_ref, 0);

#if V8_ENABLE_SANDBOX
      LiftoffRegister sig_hash_reg =
          pinned.set(__ GetUnusedRegister(kGpReg, pinned));
      __ LoadConstant(sig_hash_reg, WasmValue{SignatureHasher::Hash(type_sig)});
      VarState sig_hash_var{kIntPtrKind, sig_hash_reg, 0};
#else
      VarState sig_hash_var{kIntPtrKind, 0, 0};  // Unused by callee.
#endif

      __ Fill(vector, WasmLiftoffFrameConstants::kFeedbackVectorOffset, kRef);
      VarState vector_var{kRef, vector, 0};
      // A constant `uint32_t` is sufficient for the vector slot index.
      // The number of call instructions (and hence feedback vector slots) is
      // capped by the number of instructions, which is capped by the maximum
      // function body size.
      static_assert(kV8MaxWasmFunctionSize <
                    std::numeric_limits<uint32_t>::max() / 2);
      uint32_t vector_slot =
          static_cast<uint32_t>(encountered_call_instructions_.size()) * 2;
      encountered_call_instructions_.push_back(FunctionTypeFeedback::kCallRef);
      VarState index_var(kI32, vector_slot, 0);

      // CallRefIC(vector: FixedArray, vectorIndex: int32,
      //           signatureHash: uintptr,
      //           funcref: WasmFuncRef) -> <target, implicit_arg>
      CallBuiltin(Builtin::kCallRefIC,
                  MakeSig::Returns(kIntPtrKind, kIntPtrKind)
                      .Params(kRef, kI32, kIntPtrKind, kRef),
                  {vector_var, index_var, sig_hash_var, func_ref_var},
                  decoder->position());
      target_reg = LiftoffRegister(kReturnRegister0).gp();
      implicit_arg_reg = kReturnRegister1;
    } else {  // inlining_enabled(decoder)
      // Non-feedback-collecting version.
      // Executing a write barrier needs temp registers; doing this on a
      // conditional branch confuses the LiftoffAssembler's register management.
      // Spill everything up front to work around that.
      __ SpillAllRegisters();

      LiftoffRegList pinned;
      Register func_ref = pinned.set(__ PopToModifiableRegister(pinned)).gp();
      MaybeEmitNullCheck(decoder, func_ref, pinned, func_ref_type);
      implicit_arg_reg = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();
      target_reg = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();

      // Load the WasmInternalFunction from the WasmFuncRef.
      Register internal_function = func_ref;
      __ LoadTrustedPointer(
          internal_function, func_ref,
          ObjectAccess::ToTagged(WasmFuncRef::kTrustedInternalOffset),
          kWasmInternalFunctionIndirectPointerTag);

      // Load the implicit argument (WasmTrustedInstanceData or WasmImportData)
      // and target.
      __ LoadProtectedPointer(
          implicit_arg_reg, internal_function,
          wasm::ObjectAccess::ToTagged(
              WasmInternalFunction::kProtectedImplicitArgOffset));

      __ LoadFullPointer(target_reg, internal_function,
                         wasm::ObjectAccess::ToTagged(
                             WasmInternalFunction::kCallTargetOffset));

      // Now the call target is in {target_reg} and the first parameter
      // (WasmTrustedInstanceData or WasmImportData) is in
      // {implicit_arg_reg}.
    }  // inlining_enabled(decoder)

    __ PrepareCall(&sig, call_descriptor, &target_reg, implicit_arg_reg);
    if (tail_call) {
      __ PrepareTailCall(
          static_cast<int>(call_descriptor->ParameterSlotCount()),
          static_cast<int>(
              call_descriptor->GetStackParameterDelta(descriptor_)));
      __ TailCallIndirect(target_reg);
    } else {
      source_position_table_builder_.AddPosition(
          __ pc_offset(), SourcePosition(decoder->position()), true);
      __ CallIndirect(&sig, call_descriptor, target_reg);
      FinishCall(decoder, &sig, call_descriptor);
    }
  }

  void LoadNullValue(Register null, ValueType type) {
    // TODO(thibaudm): Can we use wasm null for exnref?
    __ LoadFullPointer(
        null, kRootRegister,
        type == kWasmExternRef || type == kWasmNullExternRef ||
                type == kWasmExnRef || type == kWasmNullExnRef
            ? IsolateData::root_slot_offset(RootIndex::kNullValue)
            : IsolateData::root_slot_offset(RootIndex::kWasmNull));
  }

  // Stores the null value representation in the passed register.
  // If pointer compression is active, only the compressed tagged pointer
  // will be stored. Any operations with this register therefore must
  // not compare this against 64 bits using quadword instructions.
  void LoadNullValueForCompare(Register null, LiftoffRegList pinned,
                               ValueType type) {
#if V8_STATIC_ROOTS_BOOL
    // TODO(14616): Extend this for shared types.
    bool is_wasm_null =
        !wasm::IsSubtypeOf(type, wasm::kWasmExternRef, env_->module) &&
        !wasm::IsSubtypeOf(type, wasm::kWasmExnRef, env_->module);
    uint32_t value = is_wasm_null ? StaticReadOnlyRoot::kWasmNull
                                  : StaticReadOnlyRoot::kNullValue;
    __ LoadConstant(LiftoffRegister(null),
                    WasmValue(static_cast<uint32_t>(value)));
#else
    LoadNullValue(null, type);
#endif
  }

  void LoadExceptionSymbol(Register dst, LiftoffRegList pinned,
                           RootIndex root_index) {
    __ LoadFullPointer(dst, kRootRegister,
                       IsolateData::root_slot_offset(root_index));
  }

  void MaybeEmitNullCheck(FullDecoder* decoder, Register object,
                          LiftoffRegList pinned, ValueType type) {
    if (v8_flags.experimental_wasm_skip_null_checks || !type.is_nullable()) {
      return;
    }
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapNullDereference);
    LiftoffRegister null = __ GetUnusedRegister(kGpReg, pinned);
    LoadNullValueForCompare(null.gp(), pinned, type);
    FREEZE_STATE(trapping);
    __ emit_cond_jump(kEqual, trap_label, kRefNull, object, null.gp(),
                      trapping);
  }

  void BoundsCheckArray(FullDecoder* decoder, bool implicit_null_check,
                        LiftoffRegister array, LiftoffRegister index,
                        LiftoffRegList pinned) {
    if (V8_UNLIKELY(v8_flags.experimental_wasm_skip_bounds_checks)) return;
    Label* trap_label =
        AddOutOfLineTrap(decoder, Builtin::kThrowWasmTrapArrayOutOfBounds);
    LiftoffRegister length = __ GetUnusedRegister(kGpReg, pinned);
    constexpr int kLengthOffset =
        wasm::ObjectAccess::ToTagged(WasmArray::kLengthOffset);
    uint32_t protected_instruction_pc = 0;
    __ Load(length, array.gp(), no_reg, kLengthOffset, LoadType::kI32Load,
            implicit_null_check ? &protected_instruction_pc : nullptr);
    if (implicit_null_check) {
      RegisterProtectedInstruction(decoder, protected_instruction_pc);
    }
    FREEZE_STATE(trapping);
    __ emit_cond_jump(kUnsignedGreaterThanEqual, trap_label, kI32, index.gp(),
                      length.gp(), trapping);
  }

  int StructFieldOffset(const StructType* struct_type, int field_index) {
    return wasm::ObjectAccess::ToTagged(WasmStruct::kHeaderSize +
                                        struct_type->field_offset(field_index));
  }

  std::pair<bool, bool> null_checks_for_struct_op(ValueType struct_type,
                                                  int field_index) {
    bool explicit_null_check =
        struct_type.is_nullable() &&
        (null_check_strategy_ == compiler::NullCheckStrategy::kExplicit ||
         field_index > wasm::kMaxStructFieldIndexForImplicitNullCheck);
    bool implicit_null_check =
        struct_type.is_nullable() && !explicit_null_check;
    return {explicit_null_check, implicit_null_check};
  }

  void LoadObjectField(FullDecoder* decoder, LiftoffRegister dst, Register src,
                       Register offset_reg, int offset, ValueKind kind,
                       bool is_signed, bool trapping, LiftoffRegList pinned) {
    uint32_t protected_load_pc = 0;
    if (is_reference(kind)) {
      __ LoadTaggedPointer(dst.gp(), src, offset_reg, offset,
                           trapping ? &protected_load_pc : nullptr);
    } else {
      // Primitive kind.
      LoadType load_type = LoadType::ForValueKind(kind, is_signed);
      __ Load(dst, src, offset_reg, offset, load_type,
              trapping ? &protected_load_pc : nullptr);
    }
    if (trapping) RegisterProtectedInstruction(decoder, protected_load_pc);
  }

  void StoreObjectField(FullDecoder* decoder, Register obj, Register offset_reg,
                        int offset, LiftoffRegister value, bool trapping,
                        LiftoffRegList pinned, ValueKind kind,
                        LiftoffAssembler::SkipWriteBarrier skip_write_barrier =
                            LiftoffAssembler::kNoSkipWriteBarrier) {
    uint32_t protected_load_pc = 0;
    if (is_reference(kind)) {
      __ StoreTaggedPointer(obj, offset_reg, offset, value.gp(), pinned,
                            trapping ? &protected_load_pc : nullptr,
                            skip_write_barrier);
    } else {
      // Primitive kind.
      StoreType store_type = StoreType::ForValueKind(kind);
      __ Store(obj, offset_reg, offset, value, store_type, pinned,
               trapping ? &protected_load_pc : nullptr);
    }
    if (trapping) RegisterProtectedInstruction(decoder, protected_load_pc);
  }

  void SetDefaultValue(LiftoffRegister reg, ValueType type) {
    DCHECK(is_defaultable(type.kind()));
    switch (type.kind()) {
      case kI8:
      case kI16:
      case kI32:
        return __ LoadConstant(reg, WasmValue(int32_t{0}));
      case kI64:
        return __ LoadConstant(reg, WasmValue(int64_t{0}));
      case kF16:
      case kF32:
        return __ LoadConstant(reg, WasmValue(float{0.0}));
      case kF64:
        return __ LoadConstant(reg, WasmValue(double{0.0}));
      case kS128:
        DCHECK(CpuFeatures::SupportsWasmSimd128());
        return __ emit_s128_xor(reg, reg, reg);
      case kRefNull:
        return LoadNullValue(reg.gp(), type);
      case kRtt:
      case kVoid:
      case kBottom:
      case kRef:
        UNREACHABLE();
    }
  }

  void MaybeOSR() {
    if (V8_UNLIKELY(for_debugging_)) {
      __ MaybeOSR();
    }
  }

  void FinishCall(FullDecoder* decoder, ValueKindSig* sig,
                  compiler::CallDescriptor* call_descriptor) {
    if (v8_flags.wasm_deopt &&
        env_->deopt_info_bytecode_offset == decoder->pc_offset() &&
        env_->deopt_location_kind == LocationKindForDeopt::kInlinedCall) {
      StoreFrameDescriptionForDeopt(decoder);
    }

    DefineSafepoint();
    RegisterDebugSideTableEntry(decoder, DebugSideTableBuilder::kDidSpill);
    int pc_offset = __ pc_offset();
    MaybeOSR();
    EmitLandingPad(decoder, pc_offset);
    __ FinishCall(sig, call_descriptor);
  }

  void CheckNan(LiftoffRegister src, LiftoffRegList pinned, ValueKind kind) {
    DCHECK(kind == ValueKind::kF32 || kind == ValueKind::kF64);
    auto nondeterminism_addr = __ GetUnusedRegister(kGpReg, pinned);
    __ LoadConstant(
        nondeterminism_addr,
        WasmValue::ForUintPtr(reinterpret_cast<uintptr_t>(nondeterminism_)));
    __ emit_set_if_nan(nondeterminism_addr.gp(), src.fp(), kind);
  }

  void CheckS128Nan(LiftoffRegister dst, LiftoffRegList pinned,
                    ValueKind lane_kind) {
    RegClass rc = reg_class_for(kS128);
    LiftoffRegister tmp_gp = pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    LiftoffRegister tmp_s128 = pinned.set(__ GetUnusedRegister(rc, pinned));
    LiftoffRegister nondeterminism_addr =
        pinned.set(__ GetUnusedRegister(kGpReg, pinned));
    __ LoadConstant(
        nondeterminism_addr,
        WasmValue::ForUintPtr(reinterpret_cast<uintptr_t>(nondeterminism_)));
    __ emit_s128_set_if_nan(nondeterminism_addr.gp(), dst, tmp_gp.gp(),
                            tmp_s128, lane_kind);
  }

  void ArrayFillImpl(FullDecoder* decoder, LiftoffRegList pinned,
                     LiftoffRegister obj, LiftoffRegister index,
                     LiftoffRegister value, LiftoffRegister length,
                     ValueKind elem_kind,
                     LiftoffAssembler::SkipWriteBarrier skip_write_barrier) {
    // initial_offset = WasmArray::kHeaderSize + index * elem_size.
    LiftoffRegister offset = index;
    if (value_kind_size_log2(elem_kind) != 0) {
      __ emit_i32_shli(offset.gp(), index.gp(),
                       value_kind_size_log2(elem_kind));
    }
    __ emit_i32_addi(offset.gp(), offset.gp(),
                     wasm::ObjectAccess::ToTagged(WasmArray::kHeaderSize));

    // end_offset = initial_offset + length * elem_size.
    LiftoffRegister end_offset = length;
    if (value_kind_size_log2(elem_kind) != 0) {
      __ emit_i32_shli(end_offset.gp(), length.gp(),
                       value_kind_size_log2(elem_kind));
    }
    __ emit_i32_add(end_offset.gp(), end_offset.gp(), offset.gp());

    FREEZE_STATE(frozen_for_conditional_jumps);
    Label loop, done;
    __ bind(&loop);
    __ emit_cond_jump(kUnsignedGreaterThanEqual, &done, kI32, offset.gp(),
                      end_offset.gp(), frozen_for_conditional_jumps);
    StoreObjectField(decoder, obj.gp(), offset.gp(), 0, value, false, pinned,
                     elem_kind, skip_write_barrier);
    __ emit_i32_addi(offset.gp(), offset.gp(), value_kind_size(elem_kind));
    __ emit_jump(&loop);

    __ bind(&done);
  }

  void RegisterProtectedInstruction(FullDecoder* decoder,
                                    uint32_t protected_instruction_pc) {
    protected_instructions_.emplace_back(
        trap_handler::ProtectedInstructionData{protected_instruction_pc});
    source_position_table_builder_.AddPosition(
        protected_instruction_pc, SourcePosition(decoder->position()), true);
    if (for_debugging_) {
      DefineSafepoint(protected_instruction_pc);
    }
  }

  bool has_outstanding_op() const {
    return outstanding_op_ != kNoOutstandingOp;
  }

  bool test_and_reset_outstanding_op(WasmOpcode opcode) {
    DCHECK_NE(kNoOutstandingOp, opcode);
    if (outstanding_op_ != opcode) return false;
    outstanding_op_ = kNoOutstandingOp;
    return true;
  }

  void TraceCacheState(FullDecoder* decoder) const {
    if (!v8_flags.trace_liftoff) return;
    StdoutStream os;
    for (int control_depth = decoder->control_depth() - 1; control_depth >= -1;
         --control_depth) {
      auto* cache_state =
          control_depth == -1 ? __ cache_state()
                              : &decoder->control_at(control_depth)
                                     ->label_state;
      os << PrintCollection(cache_state->stack_state);
      if (control_depth != -1) PrintF("; ");
    }
    os << "\n";
  }

  void DefineSafepoint(int pc_offset = 0) {
    if (pc_offset == 0) pc_offset = __ pc_offset_for_safepoint();
    if (pc_offset == last_safepoint_offset_) return;
    last_safepoint_offset_ = pc_offset;
    auto safepoint = safepoint_table_builder_.DefineSafepoint(&asm_, pc_offset);
    __ cache_state()->DefineSafepoint(safepoint);
  }

  void DefineSafepointWithCalleeSavedRegisters() {
    int pc_offset = __ pc_offset_for_safepoint();
    if (pc_offset == last_safepoint_offset_) return;
    last_safepoint_offset_ = pc_offset;
    auto safepoint = safepoint_table_builder_.DefineSafepoint(&asm_, pc_offset);
    __ cache_state()->DefineSafepointWithCalleeSavedRegisters(safepoint);
  }

  // Return a register holding the instance, populating the "cached instance"
  // register if possible. If no free register is available, the cache is not
  // set and we use {fallback} instead. This can be freely overwritten by the
  // caller then.
  V8_INLINE Register LoadInstanceIntoRegister(LiftoffRegList pinned,
                                              Register fallback) {
    Register instance = __ cache_state() -> cached_instance_data;
    if (V8_UNLIKELY(instance == no_reg)) {
      instance = LoadInstanceIntoRegister_Slow(pinned, fallback);
    }
    return instance;
  }

  V8_NOINLINE V8_PRESERVE_MOST Register
  LoadInstanceIntoRegister_Slow(LiftoffRegList pinned, Register fallback) {
    DCHECK_EQ(no_reg, __ cache_state()->cached_instance_data);
    SCOPED_CODE_COMMENT("load instance");
    Register instance = __ cache_state()->TrySetCachedInstanceRegister(
        pinned | LiftoffRegList{fallback});
    if (instance == no_reg) instance = fallback;
    __ LoadInstanceDataFromFrame(instance);
    return instance;
  }

  static constexpr WasmOpcode kNoOutstandingOp = kExprUnreachable;
  static constexpr base::EnumSet<ValueKind> kUnconditionallySupported{
      // MVP:
      kI32, kI64, kF32, kF64,
      // Extern ref:
      kRef, kRefNull, kRtt, kI8, kI16};

  LiftoffAssembler asm_;

  // Used for merging code generation of subsequent operations (via look-ahead).
  // Set by the first opcode, reset by the second.
  WasmOpcode outstanding_op_ = kNoOutstandingOp;

  // {supported_types_} is updated in {MaybeBailoutForUnsupportedType}.
  base::EnumSet<ValueKind> supported_types_ = kUnconditionallySupported;
  compiler::CallDescriptor* const descriptor_;
  CompilationEnv* const env_;
  DebugSideTableBuilder* const debug_sidetable_builder_;
  base::OwnedVector<ValueType> stack_value_types_for_debugging_;
  const ForDebugging for_debugging_;
  LiftoffBailoutReason bailout_reason_ = kSuccess;
  const int func_index_;
  ZoneVector<OutOfLineCode> out_of_line_code_;
  SourcePositionTableBuilder source_position_table_builder_;
  ZoneVector<trap_handler::ProtectedInstructionData> protected_instructions_;
  // Zone used to store information during compilation. The result will be
  // stored independently, such that this zone can die together with the
  // LiftoffCompiler after compilation.
  Zone* zone_;
  SafepointTableBuilder safepoint_table_builder_;
  // The pc offset of the instructions to reserve the stack frame. Needed to
  // patch the actually needed stack size in the end.
  uint32_t pc_offset_stack_frame_construction_ = 0;
  // For emitting breakpoint, we store a pointer to the position of the next
  // breakpoint, and a pointer after the list of breakpoints as end marker.
  // A single breakpoint at offset 0 indicates that we should prepare the
  // function for stepping by flooding it with breakpoints.
  const int* next_breakpoint_ptr_ = nullptr;
  const int* next_breakpoint_end_ = nullptr;

  // Introduce a dead breakpoint to ensure that the calculation of the return
  // address in OSR is correct.
  int dead_breakpoint_ = 0;

  // Remember whether the did function-entry break checks (for "hook on function
  // call" and "break on entry" a.k.a. instrumentation breakpoint). This happens
  // at the first breakable opcode in the function (if compiling for debugging).
  bool did_function_entry_break_checks_ = false;

  struct HandlerInfo {
    MovableLabel handler;
    int pc_offset;
  };

  ZoneVector<HandlerInfo> handlers_;
  int handler_table_offset_ = Assembler::kNoHandlerTable;

  // Current number of exception refs on the stack.
  int num_exceptions_ = 0;

  // The pc_offset of the last defined safepoint. -1 if no safepoint has been
  // defined yet.
  int last_safepoint_offset_ = -1;

  // Updated during compilation on every "call", "call_indirect", and "call_ref"
  // instruction.
  // Holds the call target, or for "call_indirect" and "call_ref" the sentinels
  // {FunctionTypeFeedback::kCallIndirect} / {FunctionTypeFeedback::kCallRef}.
  // After compilation, this is transferred into {WasmModule::type_feedback}.
  std::vector<uint32_t> encountered_call_instructions_;

  // Pointer to information passed from the fuzzer. The pointers will be
  // embedded in generated code, which will update the values at runtime.
  int32_t* max_steps_;
  int32_t* nondeterminism_;

  std::unique_ptr<LiftoffFrameDescriptionForDeopt> frame_description_;

  const compiler::NullCheckStrategy null_check_strategy_ =
      trap_handler::IsTrapHandlerEnabled() && V8_STATIC_ROOTS_BOOL
          ? compiler::NullCheckStrategy::kTrapHandler
          : compiler::NullCheckStrategy::kExplicit;

  DISALLOW_IMPLICIT_CONSTRUCTORS(LiftoffCompiler);
};

// static
constexpr WasmOpcode LiftoffCompiler::kNoOutstandingOp;
// static
constexpr base::EnumSet<ValueKind> LiftoffCompiler::kUnconditionallySupported;

std::unique_ptr<AssemblerBuffer> NewLiftoffAssemblerBuffer(int func_body_size) {
  size_t code_size_estimate =
      WasmCodeManager::EstimateLiftoffCodeSize(func_body_size);
  // Allocate the initial buffer a bit bigger to avoid reallocation during code
  // generation. Overflows when casting to int are fine, as we will allocate at
  // least {AssemblerBase::kMinimalBufferSize} anyway, so in the worst case we
  // have to grow more often.
  int initial_buffer_size = static_cast<int>(128 + code_size_estimate * 4 / 3);

  return NewAssemblerBuffer(initial_buffer_size);
}

}  // namespace

WasmCompilationResult ExecuteLiftoffCompilation(
    CompilationEnv* env, const FunctionBody& func_body,
    const LiftoffOptions& compiler_options) {
  DCHECK(compiler_options.is_initialized());
  // Liftoff does not validate the code, so that should have run before.
  DCHECK(env->module->function_was_validated(compiler_options.func_index));
  base::TimeTicks start_time;
  if (V8_UNLIKELY(v8_flags.trace_wasm_compilation_times)) {
    start_time = base::TimeTicks::Now();
  }
  int func_body_size = static_cast<int>(func_body.end - func_body.start);
  TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.CompileBaseline", "funcIndex", compiler_options.func_index,
               "bodySize", func_body_size);

  Zone zone(GetWasmEngine()->allocator(), "LiftoffCompilationZone");
  auto call_descriptor = compiler::GetWasmCallDescriptor(&zone, func_body.sig);

  std::unique_ptr<DebugSideTableBuilder> debug_sidetable_builder;
  if (compiler_options.debug_sidetable) {
    debug_sidetable_builder = std::make_unique<DebugSideTableBuilder>();
  }
  DCHECK_IMPLIES(compiler_options.max_steps,
                 compiler_options.for_debugging == kForDebugging);
  WasmDetectedFeatures unused_detected_features;

  WasmFullDecoder<Decoder::NoValidationTag, LiftoffCompiler> decoder(
      &zone, env->module, env->enabled_features,
      compiler_options.detected_features ? compiler_options.detected_features
                                         : &unused_detected_features,
      func_body, call_descriptor, env, &zone,
      NewLiftoffAssemblerBuffer(func_body_size), debug_sidetable_builder.get(),
      compiler_options);
  decoder.Decode();
  LiftoffCompiler* compiler = &decoder.interface();
  if (decoder.failed()) compiler->OnFirstError(&decoder);

  if (auto* counters = compiler_options.counters) {
    // Check that the histogram for the bailout reasons has the correct size.
    DCHECK_EQ(0, counters->liftoff_bailout_reasons()->min());
    DCHECK_EQ(kNumBailoutReasons - 1,
              counters->liftoff_bailout_reasons()->max());
    DCHECK_EQ(kNumBailoutReasons,
              counters->liftoff_bailout_reasons()->num_buckets());
    // Register the bailout reason (can also be {kSuccess}).
    counters->liftoff_bailout_reasons()->AddSample(
        static_cast<int>(compiler->bailout_reason()));
  }

  if (compiler->did_bailout()) return WasmCompilationResult{};

  WasmCompilationResult result;
  compiler->GetCode(&result.code_desc);
  result.instr_buffer = compiler->ReleaseBuffer();
  result.source_positions = compiler->GetSourcePositionTable();
  result.protected_instructions_data = compiler->GetProtectedInstructionsData();
  result.frame_slot_count = compiler->GetTotalFrameSlotCountForGC();
  result.ool_spill_count = compiler->OolSpillCount();
  auto* lowered_call_desc = GetLoweredCallDescriptor(&zone, call_descriptor);
  result.tagged_parameter_slots = lowered_call_desc->GetTaggedParameterSlots();
  result.func_index = compiler_options.func_index;
  result.result_tier = ExecutionTier::kLiftoff;
  result.for_debugging = compiler_options.for_debugging;
  result.frame_has_feedback_slot =
      env->enabled_features.has_inlining() || env->module->is_wasm_gc;
  result.liftoff_frame_descriptions = compiler->ReleaseFrameDescriptions();
  if (auto* debug_sidetable = compiler_options.debug_sidetable) {
    *debug_sidetable = debug_sidetable_builder->GenerateDebugSideTable();
  }

  if (V8_UNLIKELY(v8_flags.trace_wasm_compilation_times)) {
    base::TimeDelta time = base::TimeTicks::Now() - start_time;
    int codesize = result.code_desc.body_size();
    StdoutStream{} << "Compiled function "
                   << reinterpret_cast<const void*>(env->module) << "#"
                   << compiler_options.func_index << " using Liftoff, took "
                   << time.InMilliseconds() << " ms and "
                   << zone.allocation_size() << " bytes; bodysize "
                   << func_body_size << " codesize " << codesize << std::endl;
  }

  DCHECK(result.succeeded());

  return result;
}

std::unique_ptr<DebugSideTable> GenerateLiftoffDebugSideTable(
    const WasmCode* code) {
  auto* native_module = code->native_module();
  auto* function = &native_module->module()->functions[code->index()];
  ModuleWireBytes wire_bytes{native_module->wire_bytes()};
  base::Vector<const uint8_t> function_bytes =
      wire_bytes.GetFunctionBytes(function);
  CompilationEnv env = CompilationEnv::ForModule(native_module);
  bool is_shared =
      native_module->module()->types[function->sig_index].is_shared;
  FunctionBody func_body{function->sig, 0, function_bytes.begin(),
                         function_bytes.end(), is_shared};

  Zone zone(GetWasmEngine()->allocator(), "LiftoffDebugSideTableZone");
  auto call_descriptor = compiler::GetWasmCallDescriptor(&zone, function->sig);
  DebugSideTableBuilder debug_sidetable_builder;
  WasmDetectedFeatures detected;
  constexpr int kSteppingBreakpoints[] = {0};
  DCHECK(code->for_debugging() == kForDebugging ||
         code->for_debugging() == kForStepping);
  base::Vector<const int> breakpoints =
      code->for_debugging() == kForStepping
          ? base::ArrayVector(kSteppingBreakpoints)
          : base::Vector<const int>{};
  WasmFullDecoder<Decoder::NoValidationTag, LiftoffCompiler> decoder(
      &zone, native_module->module(), env.enabled_features, &detected,
      func_body, call_descriptor, &env, &zone,
      NewAssemblerBuffer(AssemblerBase::kDefaultBufferSize),
      &debug_sidetable_builder,
      LiftoffOptions{}
          .set_func_index(code->index())
          .set_for_debugging(code->for_debugging())
          .set_breakpoints(breakpoints));
  decoder.Decode();
  DCHECK(decoder.ok());
  DCHECK(!decoder.interface().did_bailout());
  return debug_sidetable_builder.GenerateDebugSideTable();
}

}  // namespace v8::internal::wasm
