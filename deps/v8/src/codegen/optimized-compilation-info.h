// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_OPTIMIZED_COMPILATION_INFO_H_
#define V8_CODEGEN_OPTIMIZED_COMPILATION_INFO_H_

#include <memory>

#include "src/base/vector.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/source-position-table.h"
#include "src/codegen/tick-counter.h"
#include "src/common/globals.h"
#include "src/diagnostics/basic-block-profiler.h"
#include "src/execution/frames.h"
#include "src/handles/handles.h"
#include "src/handles/persistent-handles.h"
#include "src/objects/objects.h"
#include "src/objects/tagged.h"
#include "src/utils/identity-map.h"
#include "src/utils/utils.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-builtin-list.h"
#endif

namespace v8 {

namespace tracing {
class TracedValue;
}  // namespace tracing

namespace internal {

class FunctionLiteral;
class Isolate;
class JavaScriptFrame;
class JSGlobalObject;
class Zone;

namespace compiler {
class NodeObserver;
class JSHeapBroker;
}

namespace wasm {
struct WasmCompilationResult;
}  // namespace wasm

// OptimizedCompilationInfo encapsulates the information needed to compile
// optimized code for a given function, and the results of the optimized
// compilation.
class V8_EXPORT_PRIVATE OptimizedCompilationInfo final {
 public:
  // Various configuration flags for a compilation, as well as some properties
  // of the compiled code produced by a compilation.

#define FLAGS(V)                                                     \
  V(FunctionContextSpecializing, function_context_specializing, 0)   \
  V(Inlining, inlining, 1)                                           \
  V(DisableFutureOptimization, disable_future_optimization, 2)       \
  V(Splitting, splitting, 3)                                         \
  V(SourcePositions, source_positions, 4)                            \
  V(BailoutOnUninitialized, bailout_on_uninitialized, 5)             \
  V(LoopPeeling, loop_peeling, 6)                                    \
  V(SwitchJumpTable, switch_jump_table, 7)                           \
  V(CalledWithCodeStartRegister, called_with_code_start_register, 8) \
  V(AllocationFolding, allocation_folding, 9)                        \
  V(AnalyzeEnvironmentLiveness, analyze_environment_liveness, 10)    \
  V(TraceTurboJson, trace_turbo_json, 11)                            \
  V(TraceTurboGraph, trace_turbo_graph, 12)                          \
  V(TraceTurboScheduled, trace_turbo_scheduled, 13)                  \
  V(TraceTurboAllocation, trace_turbo_allocation, 14)                \
  V(TraceHeapBroker, trace_heap_broker, 15)                          \
  V(DiscardResultForTesting, discard_result_for_testing, 16)         \
  V(InlineJSWasmCalls, inline_js_wasm_calls, 17)                     \
  V(TurboshaftTraceReduction, turboshaft_trace_reduction, 18)        \
  V(CouldNotInlineAllCandidates, could_not_inline_all_candidates, 19)

  enum Flag {
#define DEF_ENUM(Camel, Lower, Bit) k##Camel = 1 << Bit,
    FLAGS(DEF_ENUM)
#undef DEF_ENUM
  };

#define DEF_GETTER(Camel, Lower, Bit) \
  bool Lower() const {                \
    return GetFlag(k##Camel);         \
  }
  FLAGS(DEF_GETTER)
#undef DEF_GETTER

#define DEF_SETTER(Camel, Lower, Bit) \
  void set_##Lower() {                \
    SetFlag(k##Camel);                \
  }
  FLAGS(DEF_SETTER)
#undef DEF_SETTER

  // Construct a compilation info for optimized compilation.
  OptimizedCompilationInfo(Zone* zone, Isolate* isolate,
                           Handle<SharedFunctionInfo> shared,
                           Handle<JSFunction> closure, CodeKind code_kind,
                           BytecodeOffset osr_offset);
  // For testing.
  OptimizedCompilationInfo(Zone* zone, Isolate* isolate,
                           Handle<SharedFunctionInfo> shared,
                           Handle<JSFunction> closure, CodeKind code_kind)
      : OptimizedCompilationInfo(zone, isolate, shared, closure, code_kind,
                                 BytecodeOffset::None()) {}
  // Construct a compilation info for stub compilation, Wasm, and testing.
  OptimizedCompilationInfo(base::Vector<const char> debug_name, Zone* zone,
                           CodeKind code_kind,
                           Builtin builtin = Builtin::kNoBuiltinId);

  OptimizedCompilationInfo(const OptimizedCompilationInfo&) = delete;
  OptimizedCompilationInfo& operator=(const OptimizedCompilationInfo&) = delete;

  ~OptimizedCompilationInfo();

  Zone* zone() { return zone_; }
  bool is_osr() const { return !osr_offset_.IsNone(); }
  Handle<SharedFunctionInfo> shared_info() const { return shared_info_; }
  bool has_shared_info() const { return !shared_info().is_null(); }
  Handle<BytecodeArray> bytecode_array() const { return bytecode_array_; }
  bool has_bytecode_array() const { return !bytecode_array_.is_null(); }
  Handle<JSFunction> closure() const { return closure_; }
  Handle<Code> code() const { return code_; }
  CodeKind code_kind() const { return code_kind_; }
  Builtin builtin() const { return builtin_; }
  void set_builtin(Builtin builtin) { builtin_ = builtin; }
  BytecodeOffset osr_offset() const { return osr_offset_; }
  void SetNodeObserver(compiler::NodeObserver* observer) {
    DCHECK_NULL(node_observer_);
    node_observer_ = observer;
  }
  compiler::NodeObserver* node_observer() const { return node_observer_; }

  // Code getters and setters.

  void SetCode(Handle<Code> code);

#if V8_ENABLE_WEBASSEMBLY
  void SetWasmCompilationResult(std::unique_ptr<wasm::WasmCompilationResult>);
  std::unique_ptr<wasm::WasmCompilationResult> ReleaseWasmCompilationResult();
#endif  // V8_ENABLE_WEBASSEMBLY

  bool has_context() const;
  Tagged<Context> context() const;

  bool has_native_context() const;
  Tagged<NativeContext> native_context() const;

  bool has_global_object() const;
  Tagged<JSGlobalObject> global_object() const;

  // Accessors for the different compilation modes.
  bool IsOptimizing() const {
    return CodeKindIsOptimizedJSFunction(code_kind());
  }
#if V8_ENABLE_WEBASSEMBLY
  bool IsWasm() const { return code_kind() == CodeKind::WASM_FUNCTION; }
  bool IsWasmBuiltin() const {
    return code_kind() == CodeKind::WASM_TO_JS_FUNCTION ||
           code_kind() == CodeKind::JS_TO_WASM_FUNCTION ||
           (code_kind() == CodeKind::BUILTIN &&
            (builtin() == Builtin::kJSToWasmWrapper ||
             builtin() == Builtin::kJSToWasmHandleReturns ||
             builtin() == Builtin::kWasmToJsWrapperCSA ||
             wasm::BuiltinLookup::IsWasmBuiltinId(builtin())));
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  void set_persistent_handles(
      std::unique_ptr<PersistentHandles> persistent_handles) {
    DCHECK_NULL(ph_);
    ph_ = std::move(persistent_handles);
    DCHECK_NOT_NULL(ph_);
  }

  void set_canonical_handles(
      std::unique_ptr<CanonicalHandlesMap> canonical_handles) {
    DCHECK_NULL(canonical_handles_);
    canonical_handles_ = std::move(canonical_handles);
    DCHECK_NOT_NULL(canonical_handles_);
  }

  template <typename T>
  Handle<T> CanonicalHandle(Tagged<T> object, Isolate* isolate) {
    DCHECK_NOT_NULL(canonical_handles_);
    DCHECK(PersistentHandlesScope::IsActive(isolate));
    auto find_result = canonical_handles_->FindOrInsert(object);
    if (!find_result.already_exists) {
      *find_result.entry = Handle<T>(object, isolate).location();
    }
    return Handle<T>(*find_result.entry);
  }

  void ReopenAndCanonicalizeHandlesInNewScope(Isolate* isolate);

  void AbortOptimization(BailoutReason reason);

  void RetryOptimization(BailoutReason reason);

  BailoutReason bailout_reason() const { return bailout_reason_; }

  int optimization_id() const {
    DCHECK(IsOptimizing());
    return optimization_id_;
  }

  unsigned inlined_bytecode_size() const { return inlined_bytecode_size_; }

  void set_inlined_bytecode_size(unsigned size) {
    inlined_bytecode_size_ = size;
  }

  struct InlinedFunctionHolder {
    Handle<SharedFunctionInfo> shared_info;
    Handle<BytecodeArray> bytecode_array;  // Explicit to prevent flushing.
    InliningPosition position;

    InlinedFunctionHolder(Handle<SharedFunctionInfo> inlined_shared_info,
                          Handle<BytecodeArray> inlined_bytecode,
                          SourcePosition pos);

    void RegisterInlinedFunctionId(size_t inlined_function_id) {
      position.inlined_function_id = static_cast<int>(inlined_function_id);
    }
  };

  using InlinedFunctionList = std::vector<InlinedFunctionHolder>;
  InlinedFunctionList& inlined_functions() { return inlined_functions_; }

  // Returns the inlining id for source position tracking.
  int AddInlinedFunction(Handle<SharedFunctionInfo> inlined_function,
                         Handle<BytecodeArray> inlined_bytecode,
                         SourcePosition pos);

  std::unique_ptr<char[]> GetDebugName() const;

  StackFrame::Type GetOutputStackFrameType() const;

  const char* trace_turbo_filename() const {
    return trace_turbo_filename_.get();
  }

  void set_trace_turbo_filename(std::unique_ptr<char[]> filename) {
    trace_turbo_filename_ = std::move(filename);
  }

  TickCounter& tick_counter() { return tick_counter_; }

  BasicBlockProfilerData* profiler_data() const { return profiler_data_; }
  void set_profiler_data(BasicBlockProfilerData* profiler_data) {
    profiler_data_ = profiler_data;
  }

  std::unique_ptr<PersistentHandles> DetachPersistentHandles() {
    DCHECK_NOT_NULL(ph_);
    return std::move(ph_);
  }

  std::unique_ptr<CanonicalHandlesMap> DetachCanonicalHandles() {
    DCHECK_NOT_NULL(canonical_handles_);
    return std::move(canonical_handles_);
  }

 private:
  void ConfigureFlags();

  void SetFlag(Flag flag) { flags_ |= flag; }
  bool GetFlag(Flag flag) const { return (flags_ & flag) != 0; }

  void SetTracingFlags(bool passes_filter);

  // Storing the raw pointer to the CanonicalHandlesMap is generally not safe.
  // Use DetachCanonicalHandles() to transfer ownership instead.
  // We explicitly allow the JSHeapBroker to store the raw pointer as it is
  // guaranteed that the OptimizedCompilationInfo's lifetime exceeds the
  // lifetime of the broker.
  CanonicalHandlesMap* canonical_handles() { return canonical_handles_.get(); }
  friend class compiler::JSHeapBroker;

  // Compilation flags.
  unsigned flags_ = 0;

  // Take care when accessing this on any background thread.
  Isolate* const isolate_unsafe_;

  const CodeKind code_kind_;
  Builtin builtin_ = Builtin::kNoBuiltinId;

  // We retain a reference the bytecode array specifically to ensure it doesn't
  // get flushed while we are optimizing the code.
  Handle<BytecodeArray> bytecode_array_;
  Handle<SharedFunctionInfo> shared_info_;
  Handle<JSFunction> closure_;

  // The compiled code.
  Handle<Code> code_;

  // Basic block profiling support.
  BasicBlockProfilerData* profiler_data_ = nullptr;

#if V8_ENABLE_WEBASSEMBLY
  // The WebAssembly compilation result, not published in the NativeModule yet.
  std::unique_ptr<wasm::WasmCompilationResult> wasm_compilation_result_;
#endif  // V8_ENABLE_WEBASSEMBLY

  // Entry point when compiling for OSR, {BytecodeOffset::None} otherwise.
  const BytecodeOffset osr_offset_ = BytecodeOffset::None();

  // The zone from which the compilation pipeline working on this
  // OptimizedCompilationInfo allocates.
  Zone* const zone_;

  compiler::NodeObserver* node_observer_ = nullptr;

  BailoutReason bailout_reason_ = BailoutReason::kNoReason;

  InlinedFunctionList inlined_functions_;

  static constexpr int kNoOptimizationId = -1;
  const int optimization_id_;
  unsigned inlined_bytecode_size_ = 0;

  base::Vector<const char> debug_name_;
  std::unique_ptr<char[]> trace_turbo_filename_;

  TickCounter tick_counter_;

  // 1) PersistentHandles created via PersistentHandlesScope inside of
  //    CompilationHandleScope
  // 2) Owned by OptimizedCompilationInfo
  // 3) Owned by the broker's LocalHeap when entering the LocalHeapScope.
  // 4) Back to OptimizedCompilationInfo when exiting the LocalHeapScope.
  //
  // In normal execution it gets destroyed when PipelineData gets destroyed.
  // There is a special case in GenerateCodeForTesting where the JSHeapBroker
  // will not be retired in that same method. In this case, we need to re-attach
  // the PersistentHandles container to the JSHeapBroker.
  std::unique_ptr<PersistentHandles> ph_;

  // Canonical handles follow the same path as described by the persistent
  // handles above. The only difference is that is created in the
  // CanonicalHandleScope(i.e step 1) is different).
  std::unique_ptr<CanonicalHandlesMap> canonical_handles_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_OPTIMIZED_COMPILATION_INFO_H_
