// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEOPTIMIZER_DEOPTIMIZER_H_
#define V8_DEOPTIMIZER_DEOPTIMIZER_H_

#include <vector>

#include "src/builtins/builtins.h"
#include "src/codegen/source-position.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/deoptimizer/frame-description.h"
#include "src/deoptimizer/translated-state.h"
#include "src/diagnostics/code-tracer.h"
#include "src/objects/js-function.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/value-type.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

enum class BuiltinContinuationMode;

class DeoptimizedFrameInfo;
class Isolate;

class Deoptimizer : public Malloced {
 public:
  struct DeoptInfo {
    DeoptInfo(SourcePosition position, DeoptimizeReason deopt_reason,
              uint32_t node_id, int deopt_id)
        : position(position),
          deopt_reason(deopt_reason),
          node_id(node_id),
          deopt_id(deopt_id) {}

    const SourcePosition position;
    const DeoptimizeReason deopt_reason;
    const uint32_t node_id;
    const int deopt_id;
  };

  // Whether the deopt exit is contained by the outermost loop containing the
  // osr'd loop. For example:
  //
  //  for (;;) {
  //    for (;;) {
  //    }  // OSR is triggered on this backedge.
  //  }  // This is the outermost loop containing the osr'd loop.
  static bool DeoptExitIsInsideOsrLoop(Isolate* isolate,
                                       Tagged<JSFunction> function,
                                       BytecodeOffset deopt_exit_offset,
                                       BytecodeOffset osr_offset);
  static DeoptInfo GetDeoptInfo(Tagged<Code> code, Address from);
  DeoptInfo GetDeoptInfo() const {
    return Deoptimizer::GetDeoptInfo(compiled_code_, from_);
  }

  static const char* MessageFor(DeoptimizeKind kind);

  Handle<JSFunction> function() const;
  Handle<Code> compiled_code() const;
  DeoptimizeKind deopt_kind() const { return deopt_kind_; }

  // Where the deopt exit occurred *in the outermost frame*, i.e in the
  // function we generated OSR'd code for. If the deopt occurred in an inlined
  // function, this would point at the corresponding outermost Call bytecode.
  BytecodeOffset bytecode_offset_in_outermost_frame() const {
    return bytecode_offset_in_outermost_frame_;
  }

  static Deoptimizer* New(Address raw_function, DeoptimizeKind kind,
                          Address from, int fp_to_sp_delta, Isolate* isolate);
  static Deoptimizer* Grab(Isolate* isolate);

  // The returned object with information on the optimized frame needs to be
  // freed before another one can be generated.
  static DeoptimizedFrameInfo* DebuggerInspectableFrame(JavaScriptFrame* frame,
                                                        int jsframe_index,
                                                        Isolate* isolate);

  // Deoptimize the function now. Its current optimized code will never be run
  // again and any activations of the optimized code will get deoptimized when
  // execution returns. If {code} is specified then the given code is targeted
  // instead of the function code (e.g. OSR code not installed on function).
  static void DeoptimizeFunction(Tagged<JSFunction> function,
                                 Tagged<Code> code = {});

  // Deoptimize all code in the given isolate.
  V8_EXPORT_PRIVATE static void DeoptimizeAll(Isolate* isolate);

  // Deoptimizes all optimized code that has been previously marked
  // (via code->set_marked_for_deoptimization) and unlinks all functions that
  // refer to that code.
  static void DeoptimizeMarkedCode(Isolate* isolate);

  // Deoptimizes all optimized code that implements the given function (whether
  // directly or inlined).
  static void DeoptimizeAllOptimizedCodeWithFunction(
      Isolate* isolate, Handle<SharedFunctionInfo> function);

  // Check the given address against a list of allowed addresses, to prevent a
  // potential attacker from using the frame creation process in the
  // deoptimizer, in particular the signing process, to gain control over the
  // program.
  // When building mksnapshot, always return false.
  static bool IsValidReturnAddress(Address pc, Isolate* isolate);

  ~Deoptimizer();

  void MaterializeHeapObjects();

  static void ComputeOutputFrames(Deoptimizer* deoptimizer);

  V8_EXPORT_PRIVATE static Builtin GetDeoptimizationEntry(DeoptimizeKind kind);

  // InstructionStream generation support.
  static int input_offset() { return offsetof(Deoptimizer, input_); }
  static int output_count_offset() {
    return offsetof(Deoptimizer, output_count_);
  }
  static int output_offset() { return offsetof(Deoptimizer, output_); }

  static int caller_frame_top_offset() {
    return offsetof(Deoptimizer, caller_frame_top_);
  }

  Isolate* isolate() const { return isolate_; }

  static constexpr int kMaxNumberOfEntries = 16384;

  // This marker is passed to Deoptimizer::New as {deopt_exit_index} on
  // platforms that have fixed deopt sizes. The actual deoptimization id is then
  // calculated from the return address.
  static constexpr unsigned kFixedExitSizeMarker = kMaxUInt32;

  // Size of deoptimization exit sequence.
  V8_EXPORT_PRIVATE static const int kEagerDeoptExitSize;
  V8_EXPORT_PRIVATE static const int kLazyDeoptExitSize;

  // Tracing.
  static void TraceMarkForDeoptimization(Isolate* isolate, Tagged<Code> code,
                                         const char* reason);
  static void TraceEvictFromOptimizedCodeCache(Isolate* isolate,
                                               Tagged<SharedFunctionInfo> sfi,
                                               const char* reason);

 private:
  void QueueValueForMaterialization(Address output_address, Tagged<Object> obj,
                                    const TranslatedFrame::iterator& iterator);
  void QueueFeedbackVectorForMaterialization(
      Address output_address, const TranslatedFrame::iterator& iterator);

  Deoptimizer(Isolate* isolate, Tagged<JSFunction> function,
              DeoptimizeKind kind, Address from, int fp_to_sp_delta);
  void DeleteFrameDescriptions();

  void DoComputeOutputFrames();
  void DoComputeUnoptimizedFrame(TranslatedFrame* translated_frame,
                                 int frame_index, bool goto_catch_handler);
  void DoComputeInlinedExtraArguments(TranslatedFrame* translated_frame,
                                      int frame_index);
  void DoComputeConstructCreateStubFrame(TranslatedFrame* translated_frame,
                                         int frame_index);
  void DoComputeConstructInvokeStubFrame(TranslatedFrame* translated_frame,
                                         int frame_index);

  static Builtin TrampolineForBuiltinContinuation(BuiltinContinuationMode mode,
                                                  bool must_handle_result);

#if V8_ENABLE_WEBASSEMBLY
  TranslatedValue TranslatedValueForWasmReturnKind(
      base::Optional<wasm::ValueKind> wasm_call_return_kind);
#endif  // V8_ENABLE_WEBASSEMBLY

  void DoComputeBuiltinContinuation(TranslatedFrame* translated_frame,
                                    int frame_index,
                                    BuiltinContinuationMode mode);

  unsigned ComputeInputFrameAboveFpFixedSize() const;
  unsigned ComputeInputFrameSize() const;

  static unsigned ComputeIncomingArgumentSize(
      Tagged<SharedFunctionInfo> shared);

  // Tracing.
  bool tracing_enabled() const { return trace_scope_ != nullptr; }
  bool verbose_tracing_enabled() const {
    return v8_flags.trace_deopt_verbose && tracing_enabled();
  }
  CodeTracer::Scope* trace_scope() const { return trace_scope_; }
  CodeTracer::Scope* verbose_trace_scope() const {
    return v8_flags.trace_deopt_verbose ? trace_scope() : nullptr;
  }
  void TraceDeoptBegin(int optimization_id, BytecodeOffset bytecode_offset);
  void TraceDeoptEnd(double deopt_duration);
#ifdef DEBUG
  static void TraceFoundActivation(Isolate* isolate,
                                   Tagged<JSFunction> function);
#endif
  static void TraceDeoptAll(Isolate* isolate);

  bool is_restart_frame() const { return restart_frame_index_ >= 0; }

  Isolate* isolate_;
  Tagged<JSFunction> function_;
  Tagged<Code> compiled_code_;
  unsigned deopt_exit_index_;
  BytecodeOffset bytecode_offset_in_outermost_frame_ = BytecodeOffset::None();
  DeoptimizeKind deopt_kind_;
  Address from_;
  int fp_to_sp_delta_;
  bool deoptimizing_throw_;
  int catch_handler_data_;
  int catch_handler_pc_offset_;
  int restart_frame_index_;

  // Input frame description.
  FrameDescription* input_;
  // Number of output frames.
  int output_count_;
  // Array of output frame descriptions.
  FrameDescription** output_;

  // Caller frame details computed from input frame.
  intptr_t caller_frame_top_;
  intptr_t caller_fp_;
  intptr_t caller_pc_;
  intptr_t caller_constant_pool_;

  // The argument count of the bottom most frame.
  int actual_argument_count_;

  // Key for lookup of previously materialized objects.
  intptr_t stack_fp_;

  TranslatedState translated_state_;
  struct ValueToMaterialize {
    Address output_slot_address_;
    TranslatedFrame::iterator value_;
  };
  std::vector<ValueToMaterialize> values_to_materialize_;
  std::vector<ValueToMaterialize> feedback_vector_to_materialize_;

#ifdef DEBUG
  DisallowGarbageCollection* disallow_garbage_collection_;
#endif  // DEBUG

  // Note: This is intentionally not a unique_ptr s.t. the Deoptimizer
  // satisfies is_standard_layout, needed for offsetof().
  CodeTracer::Scope* const trace_scope_;

  friend class DeoptimizedFrameInfo;
  friend class FrameDescription;
  friend class FrameWriter;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEOPTIMIZER_DEOPTIMIZER_H_
