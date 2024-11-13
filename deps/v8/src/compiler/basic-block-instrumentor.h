// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BASIC_BLOCK_INSTRUMENTOR_H_
#define V8_COMPILER_BASIC_BLOCK_INSTRUMENTOR_H_

#include "src/diagnostics/basic-block-profiler.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

class OptimizedCompilationInfo;

namespace compiler {

class Graph;
class Schedule;

namespace turboshaft {
class Graph;
}  // namespace turboshaft

class BasicBlockInstrumentor : public AllStatic {
 public:
  static BasicBlockProfilerData* Instrument(OptimizedCompilationInfo* info,
                                            Graph* graph, Schedule* schedule,
                                            Isolate* isolate);
};

// A profiler which works when reorder_builtins flag was set as true, it will
// store the call graph between builtins, the call graph will be used to reorder
// builtins.
class BasicBlockCallGraphProfiler : public AllStatic {
 public:
  // The method will iterate all the basic blocks and visit all the nodes inside
  // the block, if there is a Call/TailCall node to another builtin, it will
  // save a record line (like builtin A calls builtin B at block N).
  static void StoreCallGraph(OptimizedCompilationInfo* info,
                             Schedule* schedule);
  static void StoreCallGraph(OptimizedCompilationInfo* info,
                             const turboshaft::Graph& graph);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BASIC_BLOCK_INSTRUMENTOR_H_
