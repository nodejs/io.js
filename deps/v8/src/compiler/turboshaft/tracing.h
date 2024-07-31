// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TRACING_H_
#define V8_COMPILER_TURBOSHAFT_TRACING_H_

#include "src/base/contextual.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/turboshaft/graph-visualizer.h"
#include "src/compiler/turboshaft/graph.h"

namespace v8::internal::compiler::turboshaft {

class Tracing : public base::ContextualClass<Tracing> {
 public:
  explicit Tracing(OptimizedCompilationInfo* info) : info_(info) {
    DCHECK_NOT_NULL(info_);
  }

  using OperationDataPrinter =
      std::function<bool(std::ostream&, const Graph&, OpIndex)>;
  using BlockDataPrinter =
      std::function<bool(std::ostream&, const Graph&, BlockIndex)>;

  inline bool is_enabled() const { return info_->trace_turbo_json(); }

  void PrintPerOperationData(const char* data_name, const Graph& graph,
                             OperationDataPrinter printer) {
    DCHECK(printer);
    if (!is_enabled()) return;
    TurboJsonFile json_of(info_, std::ios_base::app);
    PrintTurboshaftCustomDataPerOperation(json_of, data_name, graph, printer);
  }
  void PrintPerBlockData(const char* data_name, const Graph& graph,
                         BlockDataPrinter printer) {
    DCHECK(printer);
    if (!is_enabled()) return;
    TurboJsonFile json_of(info_, std::ios_base::app);
    PrintTurboshaftCustomDataPerBlock(json_of, data_name, graph, printer);
  }

 private:
  OptimizedCompilationInfo* info_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TRACING_H_
