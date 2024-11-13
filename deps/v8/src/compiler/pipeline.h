// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PIPELINE_H_
#define V8_COMPILER_PIPELINE_H_

#include <memory>

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "src/common/globals.h"
#include "src/objects/code.h"
#include "src/zone/zone-containers.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/module-instantiate.h"
#include "src/wasm/value-type.h"
#endif

namespace v8 {
namespace internal {

struct AssemblerOptions;
class OptimizedCompilationInfo;
class TurbofanCompilationJob;
class ProfileDataFromFile;
class RegisterConfiguration;
struct WasmInliningPosition;

namespace wasm {
struct CompilationEnv;
struct FunctionBody;
struct WasmCompilationResult;
class WasmDetectedFeatures;
struct WasmModule;
}  // namespace wasm

namespace compiler::turboshaft {
class TurboshaftCompilationJob;
class Graph;
}  // namespace compiler::turboshaft

namespace compiler {

class CallDescriptor;
class Graph;
class InstructionSequence;
class JSGraph;
class JSHeapBroker;
class MachineGraph;
class Schedule;
class SourcePositionTable;
struct WasmCompilationData;
class TFPipelineData;
class ZoneStats;

namespace turboshaft {
class PipelineData;
}

struct InstructionRangesAsJSON {
  const InstructionSequence* sequence;
  const ZoneVector<std::pair<int, int>>* instr_origins;
};

std::ostream& operator<<(std::ostream& out, const InstructionRangesAsJSON& s);

class Pipeline : public AllStatic {
 public:
  // Returns a new compilation job for the given JavaScript function.
  static V8_EXPORT_PRIVATE std::unique_ptr<TurbofanCompilationJob>
  NewCompilationJob(Isolate* isolate, Handle<JSFunction> function,
                    CodeKind code_kind, bool has_script,
                    BytecodeOffset osr_offset = BytecodeOffset::None());

#if V8_ENABLE_WEBASSEMBLY
  // Run the pipeline for the WebAssembly compilation info.
  // Note: We pass a pointer to {detected} as it might get mutated while
  // inlining.
  static void GenerateCodeForWasmFunction(
      OptimizedCompilationInfo* info, wasm::CompilationEnv* env,
      WasmCompilationData& compilation_data, MachineGraph* mcgraph,
      CallDescriptor* call_descriptor,
      ZoneVector<WasmInliningPosition>* inlining_positions,
      wasm::WasmDetectedFeatures* detected);

  // Run the pipeline on a machine graph and generate code.
  static wasm::WasmCompilationResult GenerateCodeForWasmNativeStub(
      CallDescriptor* call_descriptor, MachineGraph* mcgraph, CodeKind kind,
      const char* debug_name, const AssemblerOptions& assembler_options,
      SourcePositionTable* source_positions = nullptr);

  static wasm::WasmCompilationResult
  GenerateCodeForWasmNativeStubFromTurboshaft(
      const wasm::WasmModule* module, const wasm::FunctionSig* sig,
      wasm::WrapperCompilationInfo wrapper_info, const char* debug_name,
      const AssemblerOptions& assembler_options,
      SourcePositionTable* source_positions);

  static bool GenerateWasmCodeFromTurboshaftGraph(
      OptimizedCompilationInfo* info, wasm::CompilationEnv* env,
      WasmCompilationData& compilation_data, MachineGraph* mcgraph,
      wasm::WasmDetectedFeatures* detected, CallDescriptor* call_descriptor);

  // Returns a new compilation job for a wasm heap stub.
  static std::unique_ptr<TurbofanCompilationJob> NewWasmHeapStubCompilationJob(
      Isolate* isolate, CallDescriptor* call_descriptor,
      std::unique_ptr<Zone> zone, Graph* graph, CodeKind kind,
      std::unique_ptr<char[]> debug_name, const AssemblerOptions& options);

  static std::unique_ptr<compiler::turboshaft::TurboshaftCompilationJob>
  NewWasmTurboshaftWrapperCompilationJob(
      Isolate* isolate, const wasm::FunctionSig* sig,
      wasm::WrapperCompilationInfo wrapper_info, const wasm::WasmModule* module,
      std::unique_ptr<char[]> debug_name, const AssemblerOptions& options);
#endif

  // Run the pipeline on a machine graph and generate code.
  static MaybeHandle<Code> GenerateCodeForCodeStub(
      Isolate* isolate, CallDescriptor* call_descriptor, Graph* graph,
      JSGraph* jsgraph, SourcePositionTable* source_positions, CodeKind kind,
      const char* debug_name, Builtin builtin, const AssemblerOptions& options,
      const ProfileDataFromFile* profile_data);

  static MaybeHandle<Code> GenerateCodeForTurboshaftBuiltin(
      turboshaft::PipelineData* turboshaft_data,
      CallDescriptor* call_descriptor, Builtin builtin, const char* debug_name,
      const ProfileDataFromFile* profile_data);

  // ---------------------------------------------------------------------------
  // The following methods are for testing purposes only. Avoid production use.
  // ---------------------------------------------------------------------------

  // Run the pipeline on JavaScript bytecode and generate code.
  V8_EXPORT_PRIVATE static MaybeHandle<Code> GenerateCodeForTesting(
      OptimizedCompilationInfo* info, Isolate* isolate);

  // Run the pipeline on a machine graph and generate code. If {schedule} is
  // {nullptr}, then compute a new schedule for code generation.
  V8_EXPORT_PRIVATE static MaybeHandle<Code> GenerateCodeForTesting(
      OptimizedCompilationInfo* info, Isolate* isolate,
      CallDescriptor* call_descriptor, Graph* graph,
      const AssemblerOptions& options, Schedule* schedule = nullptr);

  // Run the instruction selector on a turboshaft graph and generate code.
  V8_EXPORT_PRIVATE static MaybeHandle<Code> GenerateTurboshaftCodeForTesting(
      CallDescriptor* call_descriptor, turboshaft::PipelineData* data);

  // Run just the register allocator phases.
  V8_EXPORT_PRIVATE static void AllocateRegistersForTesting(
      const RegisterConfiguration* config, InstructionSequence* sequence,
      bool run_verifier);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Pipeline);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PIPELINE_H_
