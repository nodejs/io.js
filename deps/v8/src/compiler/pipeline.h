// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PIPELINE_H_
#define V8_COMPILER_PIPELINE_H_

#include <memory>

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "src/codegen/interface-descriptors.h"
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
}  // namespace wasm

namespace compiler::turboshaft {
class Graph;
class PipelineData;
class TurboshaftCompilationJob;
}  // namespace compiler::turboshaft

namespace compiler {

class CodeAssemblerState;
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

  using CodeAssemblerGenerator =
      std::function<void(compiler::CodeAssemblerState*)>;
  using CodeAssemblerInstaller =
      std::function<void(Builtin builtin, Handle<Code> code)>;

  static std::unique_ptr<TurbofanCompilationJob>
  NewCSLinkageCodeStubBuiltinCompilationJob(
      Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
      CodeAssemblerInstaller installer,
      const AssemblerOptions& assembler_options,
      CallDescriptors::Key interface_descriptor, const char* name,
      const ProfileDataFromFile* profile_data, int finalize_order);

  static std::unique_ptr<TurbofanCompilationJob>
  NewJSLinkageCodeStubBuiltinCompilationJob(
      Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
      CodeAssemblerInstaller installer,
      const AssemblerOptions& assembler_options, int argc, const char* name,
      const ProfileDataFromFile* profile_data, int finalize_order);

  static std::unique_ptr<TurbofanCompilationJob>
  NewBytecodeHandlerCompilationJob(Isolate* isolate, Builtin builtin,
                                   CodeAssemblerGenerator generator,
                                   CodeAssemblerInstaller installer,
                                   const AssemblerOptions& assembler_options,
                                   const char* name,
                                   const ProfileDataFromFile* profile_data,
                                   int finalize_order);

#if V8_ENABLE_WEBASSEMBLY
  // Run the pipeline on a machine graph and generate code.
  static wasm::WasmCompilationResult GenerateCodeForWasmNativeStub(
      CallDescriptor* call_descriptor, MachineGraph* mcgraph, CodeKind kind,
      const char* debug_name, const AssemblerOptions& assembler_options,
      SourcePositionTable* source_positions = nullptr);

  static wasm::WasmCompilationResult
  GenerateCodeForWasmNativeStubFromTurboshaft(
      const wasm::CanonicalSig* sig, wasm::WrapperCompilationInfo wrapper_info,
      const char* debug_name, const AssemblerOptions& assembler_options,
      SourcePositionTable* source_positions);

  static wasm::WasmCompilationResult GenerateWasmCode(
      wasm::CompilationEnv* env, WasmCompilationData& compilation_data,
      wasm::WasmDetectedFeatures* detected, Counters* counters);

  // Returns a new compilation job for a wasm heap stub.
  static std::unique_ptr<TurbofanCompilationJob> NewWasmHeapStubCompilationJob(
      Isolate* isolate, CallDescriptor* call_descriptor,
      std::unique_ptr<Zone> zone, Graph* graph, CodeKind kind,
      std::unique_ptr<char[]> debug_name, const AssemblerOptions& options);

  static std::unique_ptr<compiler::turboshaft::TurboshaftCompilationJob>
  NewWasmTurboshaftWrapperCompilationJob(
      Isolate* isolate, const wasm::CanonicalSig* sig,
      wasm::WrapperCompilationInfo wrapper_info,
      std::unique_ptr<char[]> debug_name, const AssemblerOptions& options);
#endif

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
