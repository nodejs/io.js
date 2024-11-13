// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_CODE_GENERATOR_H_
#define V8_MAGLEV_MAGLEV_CODE_GENERATOR_H_

#include "src/codegen/maglev-safepoint-table.h"
#include "src/common/globals.h"
#include "src/deoptimizer/frame-translation-builder.h"
#include "src/maglev/maglev-assembler.h"
#include "src/maglev/maglev-code-gen-state.h"
#include "src/utils/identity-map.h"

namespace v8 {
namespace internal {
namespace maglev {

class Graph;
class MaglevCompilationInfo;

class MaglevCodeGenerator final {
 public:
  MaglevCodeGenerator(LocalIsolate* isolate,
                      MaglevCompilationInfo* compilation_info, Graph* graph);

  V8_NODISCARD bool Assemble();

  MaybeHandle<Code> Generate(Isolate* isolate);

  GlobalHandleVector<Map> RetainedMaps(Isolate* isolate);

 private:
  V8_NODISCARD bool EmitCode();
  void EmitDeferredCode();
  V8_NODISCARD bool EmitDeopts();
  void EmitExceptionHandlerTrampolines();
  void EmitMetadata();
  void RecordInlinedFunctions();

  GlobalHandleVector<Map> CollectRetainedMaps(DirectHandle<Code> code);
  Handle<DeoptimizationData> GenerateDeoptimizationData(
      LocalIsolate* local_isolate);
  MaybeHandle<Code> BuildCodeObject(LocalIsolate* local_isolate);

  int stack_slot_count() const { return code_gen_state_.stack_slots(); }
  int stack_slot_count_with_fixed_frame() const {
    return stack_slot_count() + StandardFrameConstants::kFixedSlotCount;
  }
  uint16_t parameter_count() const { return code_gen_state_.parameter_count(); }

  MaglevAssembler* masm() { return &masm_; }

  LocalIsolate* local_isolate_;
  MaglevSafepointTableBuilder safepoint_table_builder_;
  FrameTranslationBuilder frame_translation_builder_;
  MaglevCodeGenState code_gen_state_;
  MaglevAssembler masm_;
  Graph* const graph_;

  IdentityMap<int, base::DefaultAllocationPolicy> deopt_literals_;
  int deopt_exit_start_offset_ = -1;
  int handler_table_offset_ = 0;
  int inlined_function_count_ = 0;

  bool code_gen_succeeded_ = false;

  Handle<DeoptimizationData> deopt_data_;
  MaybeHandle<Code> code_;
  GlobalHandleVector<Map> retained_maps_;
  Zone* zone_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_CODE_GENERATOR_H_
