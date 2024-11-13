// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_COMPILATION_UNIT_H_
#define V8_MAGLEV_MAGLEV_COMPILATION_UNIT_H_

#include "src/common/globals.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/compiler/heap-refs.h"

namespace v8 {
namespace internal {
namespace maglev {

enum class ValueRepresentation : uint8_t;
class MaglevCompilationInfo;
class MaglevGraphLabeller;
class Node;

// Per-unit data, i.e. once per top-level function and once per inlined
// function.
class MaglevCompilationUnit : public ZoneObject {
 public:
  static MaglevCompilationUnit* New(Zone* zone, MaglevCompilationInfo* info,
                                    Handle<JSFunction> function) {
    return zone->New<MaglevCompilationUnit>(info, function);
  }
  static MaglevCompilationUnit* NewInner(
      Zone* zone, const MaglevCompilationUnit* caller,
      compiler::SharedFunctionInfoRef shared_function_info,
      compiler::FeedbackVectorRef feedback_vector) {
    return zone->New<MaglevCompilationUnit>(
        caller->info(), caller, shared_function_info, feedback_vector);
  }
  static MaglevCompilationUnit* NewDummy(Zone* zone,
                                         const MaglevCompilationUnit* caller,
                                         int register_count,
                                         uint16_t parameter_count,
                                         uint16_t max_arguments) {
    return zone->New<MaglevCompilationUnit>(
        caller->info(), caller, register_count, parameter_count, max_arguments);
  }

  MaglevCompilationUnit(MaglevCompilationInfo* info,
                        DirectHandle<JSFunction> function);

  MaglevCompilationUnit(MaglevCompilationInfo* info,
                        const MaglevCompilationUnit* caller,
                        compiler::SharedFunctionInfoRef shared_function_info,
                        compiler::FeedbackVectorRef feedback_vector);

  MaglevCompilationUnit(MaglevCompilationInfo* info,
                        const MaglevCompilationUnit* caller, int register_count,
                        uint16_t parameter_count, uint16_t max_arguments);

  MaglevCompilationInfo* info() const { return info_; }
  const MaglevCompilationUnit* caller() const { return caller_; }
  compiler::JSHeapBroker* broker() const;
  LocalIsolate* local_isolate() const;
  Zone* zone() const;
  int register_count() const { return register_count_; }
  uint16_t parameter_count() const { return parameter_count_; }
  uint16_t max_arguments() const { return max_arguments_; }
  bool is_osr() const;
  BytecodeOffset osr_offset() const;
  int inlining_depth() const { return inlining_depth_; }
  bool is_inline() const { return inlining_depth_ != 0; }
  bool has_graph_labeller() const;
  MaglevGraphLabeller* graph_labeller() const;
  compiler::SharedFunctionInfoRef shared_function_info() const {
    return shared_function_info_.value();
  }
  compiler::BytecodeArrayRef bytecode() const { return bytecode_.value(); }
  compiler::FeedbackVectorRef feedback() const { return feedback_.value(); }

  void RegisterNodeInGraphLabeller(const Node* node);

 private:
  MaglevCompilationInfo* const info_;
  const MaglevCompilationUnit* const caller_;
  const compiler::OptionalSharedFunctionInfoRef shared_function_info_;
  const compiler::OptionalBytecodeArrayRef bytecode_;
  const compiler::OptionalFeedbackVectorRef feedback_;
  const int register_count_;
  const uint16_t parameter_count_;
  const uint16_t max_arguments_;
  const int inlining_depth_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_COMPILATION_UNIT_H_
