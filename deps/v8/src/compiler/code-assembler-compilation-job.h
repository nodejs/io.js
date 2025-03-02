// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_CODE_ASSEMBLER_COMPILATION_JOB_H_
#define V8_COMPILER_CODE_ASSEMBLER_COMPILATION_JOB_H_

#include "src/codegen/assembler.h"
#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/code-assembler.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/zone-stats.h"

namespace v8 {
namespace internal {
namespace compiler {

class Graph;
class PipelineImpl;
class TFPipelineData;

class CodeAssemblerCompilationJob : public TurbofanCompilationJob {
 public:
  using CodeAssemblerGenerator =
      std::function<void(compiler::CodeAssemblerState*)>;
  using CodeAssemblerInstaller =
      std::function<void(Builtin builtin, Handle<Code> code)>;

  CodeAssemblerCompilationJob(
      Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
      CodeAssemblerInstaller installer,
      const AssemblerOptions& assembler_options,
      std::function<compiler::CallDescriptor*(Zone*)> get_call_descriptor,
      CodeKind code_kind, const char* name,
      const ProfileDataFromFile* profile_data, int finalize_order);

  CodeAssemblerCompilationJob(const CodeAssemblerCompilationJob&) = delete;
  CodeAssemblerCompilationJob& operator=(const CodeAssemblerCompilationJob&) =
      delete;

  static constexpr int kNoFinalizeOrder = -1;
  int FinalizeOrder() const final {
    DCHECK_NE(kNoFinalizeOrder, finalize_order_);
    return finalize_order_;
  }

 protected:
  struct TFDataAndPipeline;

  friend class CodeAssemblerTester;
  V8_EXPORT_PRIVATE static std::unique_ptr<CodeAssemblerCompilationJob>
  NewJobForTesting(
      Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
      CodeAssemblerInstaller installer,
      std::function<compiler::CallDescriptor*(Zone*)> get_call_descriptor,
      CodeKind code_kind, const char* name);

  static bool ShouldOptimizeJumps(Isolate* isolate);

  RawMachineAssembler* raw_assembler() {
    return code_assembler_state_.raw_assembler_.get();
  }
  JSGraph* jsgraph() { return code_assembler_state_.jsgraph_; }

  Status PrepareJobImpl(Isolate* isolate) final;
  // ExecuteJobImpl is implemented in subclasses, as pipelines differ between
  // using Turbofan and Turboshaft.
  Status FinalizeJobImpl(Isolate* isolate) final;

  virtual PipelineImpl* EmplacePipeline(Isolate* isolate) = 0;
  virtual Handle<Code> FinalizeCode(Isolate* isolate) = 0;

  CodeAssemblerGenerator generator_;
  CodeAssemblerInstaller installer_;
  const ProfileDataFromFile* profile_data_;
  int initial_graph_hash_ = 0;

  Zone zone_;
  ZoneStats zone_stats_;
  CodeAssemblerState code_assembler_state_;
  AssemblerOptions assembler_options_;
  OptimizedCompilationInfo compilation_info_;
  std::optional<NodeOriginTable> node_origins_;

  // Conditionally allocated, depending on flags.
  std::unique_ptr<JumpOptimizationInfo> jump_opt_;
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics_;

  // Used to deterministically order finalization.
  int finalize_order_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_CODE_ASSEMBLER_COMPILATION_JOB_H_
