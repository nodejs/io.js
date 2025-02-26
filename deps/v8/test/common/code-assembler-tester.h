// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_COMMON_CODE_ASSEMBLER_TESTER_H_
#define V8_TEST_COMMON_CODE_ASSEMBLER_TESTER_H_

#include "src/codegen/assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/compiler/code-assembler-compilation-job.h"
#include "src/compiler/code-assembler.h"
#include "src/compiler/raw-machine-assembler.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"

namespace v8 {
namespace internal {
namespace compiler {

class CodeAssemblerTester {
 public:
  CodeAssemblerTester(Isolate* isolate,
                      const CallInterfaceDescriptor& descriptor,
                      const char* name = "test")
      : isolate_(isolate),
        job_(CodeAssemblerCompilationJob::NewJobForTesting(
            isolate, Builtin::kNoBuiltinId, [](CodeAssemblerState*) {},
            [this](Builtin, Handle<Code> code) { this->code_ = code; },
            [&descriptor](Zone* zone) {
              return Linkage::GetStubCallDescriptor(
                  zone, descriptor, descriptor.GetStackParameterCount(),
                  CallDescriptor::kNoFlags, Operator::kNoProperties);
            },
            CodeKind::FOR_TESTING, name)),
        scope_(isolate) {}

  // Test generating code for a stub. Assumes VoidDescriptor call interface.
  explicit CodeAssemblerTester(Isolate* isolate, const char* name = "test")
      : CodeAssemblerTester(isolate, VoidDescriptor{}, name) {}

  // Test generating code for a JS function (e.g. builtins).
  CodeAssemblerTester(Isolate* isolate, int parameter_count,
                      CodeKind kind = CodeKind::BUILTIN,
                      const char* name = "test")
      : isolate_(isolate),
        job_(CodeAssemblerCompilationJob::NewJobForTesting(
            isolate, Builtin::kNoBuiltinId, [](CodeAssemblerState*) {},
            [this](Builtin, Handle<Code> code) { this->code_ = code; },
            [parameter_count](Zone* zone) {
              return Linkage::GetJSCallDescriptor(zone, false, parameter_count,
                                                  CallDescriptor::kCanUseRoots);
            },
            kind, name)),
        scope_(isolate) {
    // Parameter count must include at least the receiver.
    DCHECK_LE(1, parameter_count);
  }

  CodeAssemblerTester(Isolate* isolate, CodeKind kind,
                      const char* name = "test")
      : CodeAssemblerTester(isolate, 1, kind, name) {}

  CodeAssemblerTester(Isolate* isolate, CallDescriptor* call_descriptor,
                      const char* name = "test")
      : isolate_(isolate),
        job_(CodeAssemblerCompilationJob::NewJobForTesting(
            isolate, Builtin::kNoBuiltinId, [](CodeAssemblerState*) {},
            [this](Builtin, Handle<Code> code) { this->code_ = code; },
            [call_descriptor](Zone*) { return call_descriptor; },
            CodeKind::FOR_TESTING, name)),
        scope_(isolate) {}

  CodeAssemblerState* state() { return &job_->code_assembler_state_; }

  // Direct low-level access to the machine assembler, for testing only.
  RawMachineAssembler* raw_assembler_for_testing() {
    return job_->raw_assembler();
  }

  Handle<Code> GenerateCode() {
    return GenerateCode(AssemblerOptions::Default(scope_.isolate()));
  }

  Handle<Code> GenerateCode(const AssemblerOptions& options) {
    // The use of a CompilationJob is awkward, but is done to avoid duplicating
    // pipeline code.
    job_->assembler_options_ = options;
    if (state()->InsideBlock()) {
      CodeAssembler(state()).Unreachable();
    }
    CHECK_EQ(CompilationJob::SUCCEEDED, job_->PrepareJob(isolate_));
    CHECK_EQ(CompilationJob::SUCCEEDED,
             job_->ExecuteJob(isolate_->counters()->runtime_call_stats(),
                              isolate_->main_thread_local_isolate()));
    CHECK_EQ(CompilationJob::SUCCEEDED, job_->FinalizeJob(isolate_));
    CHECK(!code_.is_null());
    return code_;
  }

  Handle<Code> GenerateCodeCloseAndEscape() {
    return scope_.CloseAndEscape(GenerateCode());
  }

 private:
  Isolate* isolate_;
  std::unique_ptr<CodeAssemblerCompilationJob> job_;
  HandleScope scope_;
  Handle<Code> code_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_TEST_COMMON_CODE_ASSEMBLER_TESTER_H_
