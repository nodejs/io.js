// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_MODULE_H_
#define V8_WASM_WASM_MODULE_H_

#include <map>
#include <memory>
#include <optional>

#include "src/base/platform/mutex.h"
#include "src/base/vector.h"
#include "src/codegen/signature.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/branch-hint-map.h"
#include "src/wasm/constant-expression.h"
#include "src/wasm/struct-types.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-init-expr.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/well-known-imports.h"

namespace v8::internal {
class WasmModuleObject;
}

namespace v8::internal::wasm {

using WasmName = base::Vector<const char>;

struct AsmJsOffsets;
class ErrorThrower;
#if V8_ENABLE_DRUMBRAKE
class WasmInterpreterRuntime;
#endif  // V8_ENABLE_DRUMBRAKE
class WellKnownImportsList;

// Reference to a string in the wire bytes.
class WireBytesRef {
 public:
  constexpr WireBytesRef() = default;
  constexpr WireBytesRef(uint32_t offset, uint32_t length)
      : offset_(offset), length_(length) {
    DCHECK_IMPLIES(offset_ == 0, length_ == 0);
    DCHECK_LE(offset_, offset_ + length_);  // no uint32_t overflow.
  }

  uint32_t offset() const { return offset_; }
  uint32_t length() const { return length_; }
  uint32_t end_offset() const { return offset_ + length_; }
  bool is_empty() const { return length_ == 0; }
  bool is_set() const { return offset_ != 0; }

 private:
  uint32_t offset_ = 0;
  uint32_t length_ = 0;
};

// Static representation of a wasm function.
struct WasmFunction {
  const FunctionSig* sig = nullptr;  // signature of the function.
  uint32_t func_index = 0;           // index into the function table.
  uint32_t sig_index = 0;            // index into the signature table.
  WireBytesRef code = {};            // code of this function.
  bool imported = false;
  bool exported = false;
  bool declared = false;
};

// Static representation of a wasm global variable.
struct WasmGlobal {
  ValueType type;                // type of the global.
  bool mutability = false;       // {true} if mutable.
  ConstantExpression init = {};  // the initialization expression of the global.
  union {
    // Index of imported mutable global.
    uint32_t index;
    // Offset into global memory (if not imported & mutable). Expressed in bytes
    // for value-typed globals, and in tagged words for reference-typed globals.
    uint32_t offset;
  };
  bool shared = false;
  bool imported = false;
  bool exported = false;
};

// Note: An exception tag signature only uses the params portion of a function
// signature.
using WasmTagSig = FunctionSig;

// Static representation of a wasm tag type.
struct WasmTag {
  explicit WasmTag(const WasmTagSig* sig, uint32_t sig_index)
      : sig(sig), sig_index(sig_index) {}
  const FunctionSig* ToFunctionSig() const { return sig; }

  const WasmTagSig* sig;  // type signature of the tag.
  uint32_t sig_index;
};

enum ModuleOrigin : uint8_t {
  kWasmOrigin,
  kAsmJsSloppyOrigin,
  kAsmJsStrictOrigin
};

enum BoundsCheckStrategy : int8_t {
  // Emit protected instructions, use the trap handler for OOB detection.
  kTrapHandler,
  // Emit explicit bounds checks.
  kExplicitBoundsChecks,
  // Emit no bounds checks at all (for testing only).
  kNoBoundsChecks
};

// Static representation of a wasm memory.
struct WasmMemory {
  uint32_t index = 0;              // index into the memory table
  uint32_t initial_pages = 0;      // initial size of the memory in 64k pages
  uint32_t maximum_pages = 0;      // maximum size of the memory in 64k pages
  bool is_shared = false;          // true if memory is a SharedArrayBuffer
  bool has_maximum_pages = false;  // true if there is a maximum memory size
  bool is_memory64 = false;        // true if the memory is 64 bit
  bool imported = false;           // true if the memory is imported
  bool exported = false;           // true if the memory is exported
  // Computed information, cached here for faster compilation.
  // Updated via {UpdateComputedInformation}.
  uintptr_t min_memory_size = 0;  // smallest size of any memory in bytes
  uintptr_t max_memory_size = 0;  // largest size of any memory in bytes
  BoundsCheckStrategy bounds_checks = kExplicitBoundsChecks;

  inline int GetMemory64GuardsShift() const {
    return GetMemory64GuardsShift(maximum_pages * kWasmPageSize);
  }
  static int GetMemory64GuardsShift(uint64_t max_memory_size);
  inline uint64_t GetMemory64GuardsSize() const {
    return 1ull << GetMemory64GuardsShift();
  }
};

inline void UpdateComputedInformation(WasmMemory* memory, ModuleOrigin origin) {
  const uintptr_t platform_max_pages =
      memory->is_memory64 ? kV8MaxWasmMemory64Pages : kV8MaxWasmMemory32Pages;
  memory->min_memory_size =
      std::min(platform_max_pages, uintptr_t{memory->initial_pages}) *
      kWasmPageSize;
  memory->max_memory_size =
      std::min(platform_max_pages, uintptr_t{memory->maximum_pages}) *
      kWasmPageSize;

  if (!v8_flags.wasm_bounds_checks) {
    memory->bounds_checks = kNoBoundsChecks;
  } else if (v8_flags.wasm_enforce_bounds_checks) {
    // Explicit bounds checks requested via flag (for testing).
    memory->bounds_checks = kExplicitBoundsChecks;
  } else if (origin != kWasmOrigin) {
    // Asm.js modules can't use trap handling.
    memory->bounds_checks = kExplicitBoundsChecks;
  } else if (memory->is_memory64 && !v8_flags.wasm_memory64_trap_handling) {
    // Memory64 currently always requires explicit bounds checks.
    memory->bounds_checks = kExplicitBoundsChecks;
  } else if (trap_handler::IsTrapHandlerEnabled()) {
    if constexpr (kSystemPointerSize == 4) UNREACHABLE();
    memory->bounds_checks = kTrapHandler;
  } else {
    // If the trap handler is not enabled, fall back to explicit bounds checks.
    memory->bounds_checks = kExplicitBoundsChecks;
  }
}

// Static representation of a wasm literal stringref.
struct WasmStringRefLiteral {
  explicit WasmStringRefLiteral(const WireBytesRef& source) : source(source) {}
  WireBytesRef source;  // start offset in the module bytes.
};

// Static representation of a wasm data segment.
struct WasmDataSegment {
  explicit WasmDataSegment(bool is_active, bool is_shared,
                           uint32_t memory_index, ConstantExpression dest_addr,
                           WireBytesRef source)
      : active(is_active),
        shared(is_shared),
        memory_index(memory_index),
        dest_addr(dest_addr),
        source(source) {}

  static WasmDataSegment PassiveForTesting() {
    return WasmDataSegment{false, false, 0, {}, {}};
  }

  bool active = true;     // true if copied automatically during instantiation.
  bool shared = false;    // true if shared.
  uint32_t memory_index;  // memory index (if active).
  ConstantExpression dest_addr;  // destination memory address (if active).
  WireBytesRef source;           // start offset in the module bytes.
};

// Static representation of wasm element segment (table initializer).
struct WasmElemSegment {
  enum Status {
    kStatusActive,      // copied automatically during instantiation.
    kStatusPassive,     // copied explicitly after instantiation.
    kStatusDeclarative  // purely declarative and never copied.
  };
  enum ElementType { kFunctionIndexElements, kExpressionElements };

  // Construct an active segment.
  WasmElemSegment(bool shared, ValueType type, uint32_t table_index,
                  ConstantExpression offset, ElementType element_type,
                  uint32_t element_count, uint32_t elements_wire_bytes_offset)
      : status(kStatusActive),
        shared(shared),
        type(type),
        table_index(table_index),
        offset(std::move(offset)),
        element_type(element_type),
        element_count(element_count),
        elements_wire_bytes_offset(elements_wire_bytes_offset) {}

  // Construct a passive or declarative segment, which has no table index or
  // offset.
  WasmElemSegment(Status status, bool shared, ValueType type,
                  ElementType element_type, uint32_t element_count,
                  uint32_t elements_wire_bytes_offset)
      : status(status),
        shared(shared),
        type(type),
        table_index(0),
        element_type(element_type),
        element_count(element_count),
        elements_wire_bytes_offset(elements_wire_bytes_offset) {
    DCHECK_NE(status, kStatusActive);
  }

  // Default constructor. Constucts an invalid segment.
  WasmElemSegment()
      : status(kStatusActive),
        shared(false),
        type(kWasmBottom),
        table_index(0),
        element_type(kFunctionIndexElements),
        element_count(0),
        elements_wire_bytes_offset(0) {}

  WasmElemSegment(const WasmElemSegment&) = delete;
  WasmElemSegment(WasmElemSegment&&) V8_NOEXCEPT = default;
  WasmElemSegment& operator=(const WasmElemSegment&) = delete;
  WasmElemSegment& operator=(WasmElemSegment&&) V8_NOEXCEPT = default;

  Status status;
  bool shared;
  ValueType type;
  uint32_t table_index;
  ConstantExpression offset;
  ElementType element_type;
  uint32_t element_count;
  uint32_t elements_wire_bytes_offset;
};

// Static representation of a wasm import.
struct WasmImport {
  WireBytesRef module_name;   // module name.
  WireBytesRef field_name;    // import name.
  ImportExportKindCode kind;  // kind of the import.
  uint32_t index = 0;         // index into the respective space.
};

// Static representation of a wasm export.
struct WasmExport {
  WireBytesRef name;          // exported name.
  ImportExportKindCode kind;  // kind of the export.
  uint32_t index = 0;         // index into the respective space.
};

enum class WasmCompilationHintStrategy : uint8_t {
  kDefault = 0,
  kLazy = 1,
  kEager = 2,
  kLazyBaselineEagerTopTier = 3,
};

enum class WasmCompilationHintTier : uint8_t {
  kDefault = 0,
  kBaseline = 1,
  kOptimized = 2,
};

// Static representation of a wasm compilation hint
struct WasmCompilationHint {
  WasmCompilationHintStrategy strategy;
  WasmCompilationHintTier baseline_tier;
  WasmCompilationHintTier top_tier;
};

#define SELECT_WASM_COUNTER(counters, origin, prefix, suffix)     \
  ((origin) == kWasmOrigin ? (counters)->prefix##_wasm_##suffix() \
                           : (counters)->prefix##_asm_##suffix())

// Uses a map as backing storage when sparsely, or a vector when densely
// populated. Requires {Value} to implement `bool is_set()` to identify
// uninitialized objects.
template <class Value>
class AdaptiveMap {
 public:
  // The technical limitation here is that index+1 must not overflow. Since
  // we have significantly lower maximums on anything that can be named,
  // we can have a tighter limit here to reject useless entries early.
  static constexpr uint32_t kMaxKey = 10'000'000;
  static_assert(kMaxKey < std::numeric_limits<uint32_t>::max());

  AdaptiveMap() : map_(new MapType()) {}

  explicit AdaptiveMap(const AdaptiveMap&) = delete;
  AdaptiveMap& operator=(const AdaptiveMap&) = delete;

  AdaptiveMap(AdaptiveMap&& other) V8_NOEXCEPT { *this = std::move(other); }

  AdaptiveMap& operator=(AdaptiveMap&& other) V8_NOEXCEPT {
    mode_ = other.mode_;
    vector_.swap(other.vector_);
    map_.swap(other.map_);
    return *this;
  }

  void FinishInitialization();

  bool is_set() const { return mode_ != kInitializing; }

  void Put(uint32_t key, const Value& value) {
    DCHECK(mode_ == kInitializing);
    DCHECK_LE(key, kMaxKey);
    map_->insert(std::make_pair(key, value));
  }

  void Put(uint32_t key, Value&& value) {
    DCHECK(mode_ == kInitializing);
    DCHECK_LE(key, kMaxKey);
    map_->insert(std::make_pair(key, std::move(value)));
  }

  const Value* Get(uint32_t key) const {
    if (mode_ == kDense) {
      if (key >= vector_.size()) return nullptr;
      if (!vector_[key].is_set()) return nullptr;
      return &vector_[key];
    } else {
      DCHECK(mode_ == kSparse || mode_ == kInitializing);
      auto it = map_->find(key);
      if (it == map_->end()) return nullptr;
      return &it->second;
    }
  }

  bool Has(uint32_t key) const {
    if (mode_ == kDense) {
      return key < vector_.size() && vector_[key].is_set();
    } else {
      DCHECK(mode_ == kSparse || mode_ == kInitializing);
      return map_->find(key) != map_->end();
    }
  }

  size_t EstimateCurrentMemoryConsumption() const;

 private:
  static constexpr uint32_t kLoadFactor = 4;
  using MapType = std::map<uint32_t, Value>;
  enum Mode { kDense, kSparse, kInitializing };

  Mode mode_{kInitializing};
  std::vector<Value> vector_;
  std::unique_ptr<MapType> map_;
};
using NameMap = AdaptiveMap<WireBytesRef>;
using IndirectNameMap = AdaptiveMap<AdaptiveMap<WireBytesRef>>;

struct ModuleWireBytes;

class V8_EXPORT_PRIVATE LazilyGeneratedNames {
 public:
  WireBytesRef LookupFunctionName(ModuleWireBytes wire_bytes,
                                  uint32_t function_index);

  void AddForTesting(int function_index, WireBytesRef name);
  bool Has(uint32_t function_index);

  size_t EstimateCurrentMemoryConsumption() const;

 private:
  // Lazy loading must guard against concurrent modifications from multiple
  // {WasmModuleObject}s.
  mutable base::Mutex mutex_;
  bool has_functions_{false};
  NameMap function_names_;
};

class V8_EXPORT_PRIVATE AsmJsOffsetInformation {
 public:
  explicit AsmJsOffsetInformation(base::Vector<const uint8_t> encoded_offsets);

  // Destructor defined in wasm-module.cc, where the definition of
  // {AsmJsOffsets} is available.
  ~AsmJsOffsetInformation();

  int GetSourcePosition(int func_index, int byte_offset,
                        bool is_at_number_conversion);

  std::pair<int, int> GetFunctionOffsets(int func_index);

 private:
  void EnsureDecodedOffsets();

  // The offset information table is decoded lazily, hence needs to be
  // protected against concurrent accesses.
  // Exactly one of the two fields below will be set at a time.
  mutable base::Mutex mutex_;

  // Holds the encoded offset table bytes.
  base::OwnedVector<const uint8_t> encoded_offsets_;

  // Holds the decoded offset table.
  std::unique_ptr<AsmJsOffsets> decoded_offsets_;
};

// Used as the supertype for a type at the top of the type hierarchy.
constexpr uint32_t kNoSuperType = std::numeric_limits<uint32_t>::max();

struct TypeDefinition {
  enum Kind : int8_t { kFunction, kStruct, kArray };

  constexpr TypeDefinition(const FunctionSig* sig, uint32_t supertype,
                           bool is_final, bool is_shared)
      : function_sig(sig),
        supertype(supertype),
        kind(kFunction),
        is_final(is_final),
        is_shared(is_shared) {}
  constexpr TypeDefinition(const StructType* type, uint32_t supertype,
                           bool is_final, bool is_shared)
      : struct_type(type),
        supertype(supertype),
        kind(kStruct),
        is_final(is_final),
        is_shared(is_shared) {}
  constexpr TypeDefinition(const ArrayType* type, uint32_t supertype,
                           bool is_final, bool is_shared)
      : array_type(type),
        supertype(supertype),
        kind(kArray),
        is_final(is_final),
        is_shared(is_shared) {}
  constexpr TypeDefinition() = default;

  bool operator==(const TypeDefinition& other) const {
    if (supertype != other.supertype) return false;
    if (kind != other.kind) return false;
    if (is_final != other.is_final) return false;
    if (is_shared != other.is_shared) return false;
    if (kind == kFunction) return *function_sig == *other.function_sig;
    if (kind == kStruct) return *struct_type == *other.struct_type;
    DCHECK_EQ(kArray, kind);
    return *array_type == *other.array_type;
  }

  bool operator!=(const TypeDefinition& other) const {
    return !(*this == other);
  }

  union {
    const FunctionSig* function_sig = nullptr;
    const StructType* struct_type;
    const ArrayType* array_type;
  };
  uint32_t supertype = kNoSuperType;
  Kind kind = kFunction;
  bool is_final = false;
  bool is_shared = false;
  uint8_t subtyping_depth = 0;
};

struct V8_EXPORT_PRIVATE WasmDebugSymbols {
  enum class Type { None, SourceMap, EmbeddedDWARF, ExternalDWARF };
  Type type = Type::None;
  WireBytesRef external_url;
};

class CallSiteFeedback {
 public:
  struct PolymorphicCase {
    int function_index;
    int absolute_call_frequency;
  };

  // Regular constructor: uninitialized/unknown, monomorphic, or polymorphic.
  CallSiteFeedback() : index_or_count_(-1), frequency_or_ool_(0) {}
  CallSiteFeedback(int function_index, int call_count)
      : index_or_count_(function_index), frequency_or_ool_(call_count) {}
  CallSiteFeedback(PolymorphicCase* polymorphic_cases, int num_cases)
      : index_or_count_(-num_cases),
        frequency_or_ool_(reinterpret_cast<intptr_t>(polymorphic_cases)) {}

  // Copying and assignment: prefer moving, as it's cheaper.
  // The code below makes sure external polymorphic storage is copied and/or
  // freed as appropriate.
  CallSiteFeedback(const CallSiteFeedback& other) V8_NOEXCEPT { *this = other; }
  CallSiteFeedback(CallSiteFeedback&& other) V8_NOEXCEPT { *this = other; }
  CallSiteFeedback& operator=(const CallSiteFeedback& other) V8_NOEXCEPT {
    index_or_count_ = other.index_or_count_;
    if (other.is_polymorphic()) {
      int num_cases = other.num_cases();
      PolymorphicCase* polymorphic = new PolymorphicCase[num_cases];
      for (int i = 0; i < num_cases; i++) {
        polymorphic[i].function_index = other.function_index(i);
        polymorphic[i].absolute_call_frequency = other.call_count(i);
      }
      frequency_or_ool_ = reinterpret_cast<intptr_t>(polymorphic);
    } else {
      frequency_or_ool_ = other.frequency_or_ool_;
    }
    return *this;
  }
  CallSiteFeedback& operator=(CallSiteFeedback&& other) V8_NOEXCEPT {
    if (this != &other) {
      index_or_count_ = other.index_or_count_;
      frequency_or_ool_ = other.frequency_or_ool_;
      other.frequency_or_ool_ = 0;
    }
    return *this;
  }

  ~CallSiteFeedback() {
    if (is_polymorphic()) delete[] polymorphic_storage();
  }

  int num_cases() const {
    if (is_monomorphic()) return 1;
    if (is_invalid()) return 0;
    return -index_or_count_;
  }
  int function_index(int i) const {
    DCHECK(!is_invalid());
    if (is_monomorphic()) return index_or_count_;
    return polymorphic_storage()[i].function_index;
  }
  int call_count(int i) const {
    if (index_or_count_ >= 0) return static_cast<int>(frequency_or_ool_);
    return polymorphic_storage()[i].absolute_call_frequency;
  }
  bool has_non_inlineable_targets() const {
    return has_non_inlineable_targets_;
  }
  void set_has_non_inlineable_targets(bool has_non_inlineable_targets) {
    has_non_inlineable_targets_ = has_non_inlineable_targets;
  }

 private:
  bool is_monomorphic() const { return index_or_count_ >= 0; }
  bool is_polymorphic() const { return index_or_count_ <= -2; }
  bool is_invalid() const { return index_or_count_ == -1; }
  const PolymorphicCase* polymorphic_storage() const {
    DCHECK(is_polymorphic());
    return reinterpret_cast<PolymorphicCase*>(frequency_or_ool_);
  }

  int index_or_count_;
  bool has_non_inlineable_targets_ = false;
  intptr_t frequency_or_ool_;
};

struct FunctionTypeFeedback {
  // {feedback_vector} is computed from {call_targets} and the instance-specific
  // feedback vector by {TransitiveTypeFeedbackProcessor}.
  std::vector<CallSiteFeedback> feedback_vector;

  // {call_targets} has one entry per "call", "call_indirect", and "call_ref" in
  // the function.
  // For "call", it holds the index of the called function, for "call_indirect"
  // and "call_ref" the value will be a sentinel {kCallIndirect} / {kCallRef}.
  base::OwnedVector<uint32_t> call_targets;

  // {tierup_priority} is updated and used when triggering tier-up.
  // TODO(clemensb): This does not belong here; find a better place.
  int tierup_priority = 0;

  static constexpr uint32_t kUninitializedLiftoffFrameSize = 1;
  // The size of the stack frame in liftoff in bytes.
  uint32_t liftoff_frame_size : 31 = kUninitializedLiftoffFrameSize;
  // Flag whether the cached {feedback_vector} has to be reprocessed as the data
  // is outdated (signaled by a deopt).
  // This is set by the deoptimizer, so that the next tierup trigger performs
  // the reprocessing. The deoptimizer can't update the cached data, as the new
  // feedback (which caused the deopt) hasn't been processed yet and processing
  // it can trigger allocations. After returning to liftoff, the feedback is
  // updated (which is guaranteed to happen before the next tierup trigger).
  bool needs_reprocessing_after_deopt : 1 = false;

  static constexpr uint32_t kCallRef = 0xFFFFFFFF;
  static constexpr uint32_t kCallIndirect = kCallRef - 1;
  static_assert(kV8MaxWasmTotalFunctions < kCallIndirect);
};

struct TypeFeedbackStorage {
  std::unordered_map<uint32_t, FunctionTypeFeedback> feedback_for_function;
  std::unordered_map<uint32_t, uint32_t> deopt_count_for_function;
  // Accesses to {feedback_for_function} and {deopt_count_for_function} are
  // guarded by this mutex. Multiple reads are allowed (shared lock), but only
  // exclusive writes. Currently known users of the mutex are:
  // - LiftoffCompiler: writes {call_targets}.
  // - TransitiveTypeFeedbackProcessor: reads {call_targets},
  //   writes {feedback_vector}, reads {feedback_vector.size()}.
  // - TriggerTierUp: increments {tierup_priority}.
  // - WasmGraphBuilder: reads {feedback_vector}.
  // - Feedback vector allocation: reads {call_targets.size()}.
  // - PGO ProfileGenerator: reads everything.
  // - PGO deserializer: writes everything, currently not locked, relies on
  //   being called before multi-threading enters the picture.
  // - Deoptimizer: sets needs_reprocessing_after_deopt.
  mutable base::SharedMutex mutex;

  WellKnownImportsList well_known_imports;

  size_t EstimateCurrentMemoryConsumption() const;
};

struct WasmTable {
  ValueType type = kWasmVoid;
  uint32_t initial_size = 0;
  uint32_t maximum_size = 0;
  bool has_maximum_size = false;
  bool is_table64 = false;
  bool shared = false;
  bool imported = false;
  bool exported = false;
  ConstantExpression initial_value = {};
};

// Static representation of a module.
struct V8_EXPORT_PRIVATE WasmModule {
  // ================ Fields ===================================================
  // The signature zone is also used to store the signatures of C++ functions
  // called with the V8 fast API. These signatures are added during
  // instantiation, so the `signature_zone` may be changed even when the
  // `WasmModule` is already `const`.
  mutable Zone signature_zone;
  int start_function_index = -1;   // start function, >= 0 if any

  // Size of the buffer required for all globals that are not imported and
  // mutable.
  uint32_t untagged_globals_buffer_size = 0;
  uint32_t tagged_globals_buffer_size = 0;
  uint32_t num_imported_globals = 0;
  uint32_t num_imported_mutable_globals = 0;
  uint32_t num_imported_functions = 0;
  uint32_t num_imported_tables = 0;
  uint32_t num_imported_tags = 0;
  uint32_t num_declared_functions = 0;  // excluding imported
  // This field is updated when decoding the functions. At this point in time
  // with streaming compilation there can already be background threads running
  // turbofan compilations which will read this to decide on inlining budgets.
  // This can only happen with eager compilation as code execution only starts
  // after the module has been fully decoded and therefore it does not affect
  // production configurations.
  std::atomic<uint32_t> num_small_functions = 0;
  uint32_t num_exported_functions = 0;
  uint32_t num_declared_data_segments = 0;  // From the DataCount section.
  // Position and size of the code section (payload only, i.e. without section
  // ID and length).
  WireBytesRef code = {0, 0};
  WireBytesRef name = {0, 0};
  // Position and size of the name section (payload only, i.e. without section
  // ID and length).
  WireBytesRef name_section = {0, 0};
  // Set to true if this module has wasm-gc types in its type section.
  bool is_wasm_gc = false;
  // Set to true if this module has any shared elements other than memories.
  bool has_shared_part = false;

  std::vector<TypeDefinition> types;  // by type index
  // Maps each type index to its global (cross-module) canonical index as per
  // isorecursive type canonicalization.
  std::vector<uint32_t> isorecursive_canonical_type_ids;
  std::vector<WasmFunction> functions;
  std::vector<WasmGlobal> globals;
  std::vector<WasmDataSegment> data_segments;
  std::vector<WasmTable> tables;
  std::vector<WasmMemory> memories;
  std::vector<WasmImport> import_table;
  std::vector<WasmExport> export_table;
  std::vector<WasmTag> tags;
  std::vector<WasmStringRefLiteral> stringref_literals;
  std::vector<WasmElemSegment> elem_segments;
  std::vector<WasmCompilationHint> compilation_hints;
  BranchHintInfo branch_hints;
  // Pairs of module offsets and mark id.
  std::vector<std::pair<uint32_t, uint32_t>> inst_traces;

  // This is the only member of {WasmModule} where we store dynamic information
  // that's not a decoded representation of the wire bytes.
  // TODO(jkummerow): Rename.
  mutable TypeFeedbackStorage type_feedback;

  const ModuleOrigin origin;
  mutable LazilyGeneratedNames lazily_generated_names;
  WasmDebugSymbols debug_symbols;

  // Asm.js source position information. Only available for modules compiled
  // from asm.js.
  std::unique_ptr<AsmJsOffsetInformation> asm_js_offset_information;

  // {validated_functions} is atomically updated when functions get validated
  // (during compilation, streaming decoding, or via explicit validation).
  static_assert(sizeof(std::atomic<uint8_t>) == 1);
  static_assert(alignof(std::atomic<uint8_t>) == 1);
  mutable std::unique_ptr<std::atomic<uint8_t>[]> validated_functions;

  // ================ Constructors =============================================
  explicit WasmModule(ModuleOrigin = kWasmOrigin);
  WasmModule(const WasmModule&) = delete;
  WasmModule& operator=(const WasmModule&) = delete;

  // ================ Interface for tests ======================================
  // Tests sometimes add times iteratively instead of all at once via module
  // decoding.
  void AddTypeForTesting(TypeDefinition type) {
    types.push_back(type);
    if (type.supertype != kNoSuperType) {
      // Set the subtyping depth. Outside of unit tests this is done by the
      // module decoder.
      DCHECK_GT(types.size(), 0);
      DCHECK_LT(type.supertype, types.size() - 1);
      types.back().subtyping_depth = types[type.supertype].subtyping_depth + 1;
    }
    // Isorecursive canonical type will be computed later.
    isorecursive_canonical_type_ids.push_back(kNoSuperType);
  }

  void AddSignatureForTesting(const FunctionSig* sig, uint32_t supertype,
                              bool is_final, bool is_shared) {
    DCHECK_NOT_NULL(sig);
    AddTypeForTesting(TypeDefinition(sig, supertype, is_final, is_shared));
  }

  void AddStructTypeForTesting(const StructType* type, uint32_t supertype,
                               bool is_final, bool is_shared) {
    DCHECK_NOT_NULL(type);
    AddTypeForTesting(TypeDefinition(type, supertype, is_final, is_shared));
  }

  void AddArrayTypeForTesting(const ArrayType* type, uint32_t supertype,
                              bool is_final, bool is_shared) {
    DCHECK_NOT_NULL(type);
    AddTypeForTesting(TypeDefinition(type, supertype, is_final, is_shared));
  }

  // ================ Accessors ================================================
  bool has_type(uint32_t index) const { return index < types.size(); }

  bool has_signature(uint32_t index) const {
    return index < types.size() &&
           types[index].kind == TypeDefinition::kFunction;
  }
  const FunctionSig* signature(uint32_t index) const {
    DCHECK(has_signature(index));
    size_t num_types = types.size();
    V8_ASSUME(index < num_types);
    return types[index].function_sig;
  }

  bool has_struct(uint32_t index) const {
    return index < types.size() && types[index].kind == TypeDefinition::kStruct;
  }

  const StructType* struct_type(uint32_t index) const {
    DCHECK(has_struct(index));
    size_t num_types = types.size();
    V8_ASSUME(index < num_types);
    return types[index].struct_type;
  }

  bool has_array(uint32_t index) const {
    return index < types.size() && types[index].kind == TypeDefinition::kArray;
  }
  const ArrayType* array_type(uint32_t index) const {
    DCHECK(has_array(index));
    size_t num_types = types.size();
    V8_ASSUME(index < num_types);
    return types[index].array_type;
  }

  uint32_t supertype(uint32_t index) const {
    size_t num_types = types.size();
    V8_ASSUME(index < num_types);
    return types[index].supertype;
  }
  bool has_supertype(uint32_t index) const {
    return supertype(index) != kNoSuperType;
  }

  // Linear search. Returns -1 if types are empty.
  int MaxCanonicalTypeIndex() const {
    if (isorecursive_canonical_type_ids.empty()) return -1;
    return *std::max_element(isorecursive_canonical_type_ids.begin(),
                             isorecursive_canonical_type_ids.end());
  }

  bool function_is_shared(int func_index) const {
    return types[functions[func_index].sig_index].is_shared;
  }

  bool function_was_validated(int func_index) const {
    DCHECK_NOT_NULL(validated_functions);
    static_assert(sizeof(validated_functions[0]) == 1);
    DCHECK_LE(num_imported_functions, func_index);
    int pos = func_index - num_imported_functions;
    DCHECK_LE(pos, num_declared_functions);
    uint8_t byte =
        validated_functions[pos >> 3].load(std::memory_order_relaxed);
    DCHECK_IMPLIES(origin != kWasmOrigin, byte == 0xff);
    return byte & (1 << (pos & 7));
  }

  void set_function_validated(int func_index) const {
    DCHECK_EQ(kWasmOrigin, origin);
    DCHECK_NOT_NULL(validated_functions);
    DCHECK_LE(num_imported_functions, func_index);
    int pos = func_index - num_imported_functions;
    DCHECK_LE(pos, num_declared_functions);
    std::atomic<uint8_t>* atomic_byte = &validated_functions[pos >> 3];
    uint8_t old_byte = atomic_byte->load(std::memory_order_relaxed);
    uint8_t new_bit = 1 << (pos & 7);
    while ((old_byte & new_bit) == 0 &&
           !atomic_byte->compare_exchange_weak(old_byte, old_byte | new_bit,
                                               std::memory_order_relaxed)) {
      // Retry with updated {old_byte}.
    }
  }

  void set_all_functions_validated() const {
    DCHECK_EQ(kWasmOrigin, origin);
    if (num_declared_functions == 0) return;
    DCHECK_NOT_NULL(validated_functions);
    size_t num_words = (num_declared_functions + 7) / 8;
    for (size_t i = 0; i < num_words; ++i) {
      validated_functions[i].store(0xff, std::memory_order_relaxed);
    }
  }

  base::Vector<const WasmFunction> declared_functions() const {
    return base::VectorOf(functions) + num_imported_functions;
  }

#if V8_ENABLE_DRUMBRAKE
  void SetWasmInterpreter(
      std::shared_ptr<WasmInterpreterRuntime> interpreter) const {
    base::MutexGuard lock(&interpreter_mutex_);
    interpreter_ = interpreter;
  }
  mutable std::weak_ptr<WasmInterpreterRuntime> interpreter_;
  mutable base::Mutex interpreter_mutex_;
#endif  // V8_ENABLE_DRUMBRAKE

  size_t EstimateStoredSize() const;                // No tracing.
  size_t EstimateCurrentMemoryConsumption() const;  // With tracing.
};

inline bool is_asmjs_module(const WasmModule* module) {
  return module->origin != kWasmOrigin;
}

// Return the byte offset of the function identified by the given index.
// The offset will be relative to the start of the module bytes.
// Returns -1 if the function index is invalid.
int GetWasmFunctionOffset(const WasmModule* module, uint32_t func_index);

// Returns the function containing the given byte offset.
// Returns -1 if the byte offset is not contained in any
// function of this module.
int GetContainingWasmFunction(const WasmModule* module, uint32_t byte_offset);

// Returns the function containing the given byte offset.
// Will return preceding function if the byte offset is not
// contained within a function.
int GetNearestWasmFunction(const WasmModule* module, uint32_t byte_offset);

// Gets the explicitly defined subtyping depth for the given type.
// Returns 0 if the type has no explicit supertype.
// The result is capped to {kV8MaxRttSubtypingDepth + 1}.
// Invalid cyclic hierarchies will return -1.
V8_EXPORT_PRIVATE int GetSubtypingDepth(const WasmModule* module,
                                        uint32_t type_index);

// Interface to the storage (wire bytes) of a wasm module.
// It is illegal for anyone receiving a ModuleWireBytes to store pointers based
// on module_bytes, as this storage is only guaranteed to be alive as long as
// this struct is alive.
// As {ModuleWireBytes} is just a wrapper around a {base::Vector<const
// uint8_t>}, it should generally be passed by value.
struct V8_EXPORT_PRIVATE ModuleWireBytes {
  explicit ModuleWireBytes(base::Vector<const uint8_t> module_bytes)
      : module_bytes_(module_bytes) {}
  ModuleWireBytes(const uint8_t* start, const uint8_t* end)
      : module_bytes_(start, static_cast<int>(end - start)) {
    DCHECK_GE(kMaxInt, end - start);
  }

  // Get a string stored in the module bytes representing a name.
  WasmName GetNameOrNull(WireBytesRef ref) const;

  // Get a string stored in the module bytes representing a function name.
  WasmName GetNameOrNull(int func_index, const WasmModule* module) const;

  // Checks the given reference is contained within the module bytes.
  bool BoundsCheck(WireBytesRef ref) const {
    uint32_t size = static_cast<uint32_t>(module_bytes_.length());
    return ref.offset() <= size && ref.length() <= size - ref.offset();
  }

  base::Vector<const uint8_t> GetFunctionBytes(
      const WasmFunction* function) const {
    return module_bytes_.SubVector(function->code.offset(),
                                   function->code.end_offset());
  }

  base::Vector<const uint8_t> module_bytes() const { return module_bytes_; }
  const uint8_t* start() const { return module_bytes_.begin(); }
  const uint8_t* end() const { return module_bytes_.end(); }
  size_t length() const { return module_bytes_.length(); }

 private:
  base::Vector<const uint8_t> module_bytes_;
};
ASSERT_TRIVIALLY_COPYABLE(ModuleWireBytes);

// A helper for printing out the names of functions.
struct WasmFunctionName {
  WasmFunctionName(int func_index, WasmName name)
      : func_index_(func_index), name_(name) {}

  const int func_index_;
  const WasmName name_;
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const WasmFunctionName& name);

V8_EXPORT_PRIVATE bool IsWasmCodegenAllowed(Isolate* isolate,
                                            Handle<NativeContext> context);
V8_EXPORT_PRIVATE DirectHandle<String> ErrorStringForCodegen(
    Isolate* isolate, DirectHandle<Context> context);

Handle<JSObject> GetTypeForFunction(Isolate* isolate, const FunctionSig* sig,
                                    bool for_exception = false);
Handle<JSObject> GetTypeForGlobal(Isolate* isolate, bool is_mutable,
                                  ValueType type);
Handle<JSObject> GetTypeForMemory(Isolate* isolate, uint32_t min_size,
                                  std::optional<uint32_t> max_size, bool shared,
                                  bool is_memory64);
Handle<JSObject> GetTypeForTable(Isolate* isolate, ValueType type,
                                 uint32_t min_size,
                                 std::optional<uint32_t> max_size,
                                 bool is_table64);
Handle<JSArray> GetImports(Isolate* isolate,
                           DirectHandle<WasmModuleObject> module);
Handle<JSArray> GetExports(Isolate* isolate,
                           DirectHandle<WasmModuleObject> module);
Handle<JSArray> GetCustomSections(Isolate* isolate,
                                  DirectHandle<WasmModuleObject> module,
                                  DirectHandle<String> name,
                                  ErrorThrower* thrower);

// Get the source position from a given function index and byte offset,
// for either asm.js or pure Wasm modules.
int GetSourcePosition(const WasmModule*, uint32_t func_index,
                      uint32_t byte_offset, bool is_at_number_conversion);

// Translate function index to the index relative to the first declared (i.e.
// non-imported) function.
inline int declared_function_index(const WasmModule* module, int func_index) {
  DCHECK_LE(module->num_imported_functions, func_index);
  int declared_idx = func_index - module->num_imported_functions;
  DCHECK_GT(module->num_declared_functions, declared_idx);
  return declared_idx;
}

// Translate from function index to jump table offset.
int JumpTableOffset(const WasmModule* module, int func_index);

// TruncatedUserString makes it easy to output names up to a certain length, and
// output a truncation followed by '...' if they exceed a limit.
// Use like this:
//   TruncatedUserString<> name (pc, len);
//   printf("... %.*s ...", name.length(), name.start())
template <int kMaxLen = 50>
class TruncatedUserString {
  static_assert(kMaxLen >= 4, "minimum length is 4 (length of '...' plus one)");

 public:
  template <typename T>
  explicit TruncatedUserString(base::Vector<T> name)
      : TruncatedUserString(name.begin(), name.length()) {}

  TruncatedUserString(const uint8_t* start, size_t len)
      : TruncatedUserString(reinterpret_cast<const char*>(start), len) {}

  TruncatedUserString(const char* start, size_t len)
      : start_(start), length_(std::min(kMaxLen, static_cast<int>(len))) {
    if (len > static_cast<size_t>(kMaxLen)) {
      memcpy(buffer_, start, kMaxLen - 3);
      memset(buffer_ + kMaxLen - 3, '.', 3);
      start_ = buffer_;
    }
  }

  const char* start() const { return start_; }

  int length() const { return length_; }

 private:
  const char* start_;
  const int length_;
  char buffer_[kMaxLen];
};

// Print the signature into the given {buffer}, using {delimiter} as separator
// between parameter types and return types. If {buffer} is non-empty, it will
// be null-terminated, even if the signature is cut off. Returns the number of
// characters written, excluding the terminating null-byte.
size_t PrintSignature(base::Vector<char> buffer, const wasm::FunctionSig*,
                      char delimiter = ':');

V8_EXPORT_PRIVATE size_t
GetWireBytesHash(base::Vector<const uint8_t> wire_bytes);

// Get the required number of feedback slots for a function.
int NumFeedbackSlots(const WasmModule* module, int func_index);

}  // namespace v8::internal::wasm

#endif  // V8_WASM_WASM_MODULE_H_
