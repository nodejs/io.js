// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOFAN_TYPER_H_
#define V8_COMPILER_TURBOFAN_TYPER_H_

#include "src/compiler/operation-typer.h"
#include "src/compiler/turbofan-graph.h"

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

// Forward declarations.
class LoopVariableOptimizer;

class V8_EXPORT_PRIVATE Typer {
 public:
  enum Flag : uint8_t {
    kNoFlags = 0,
    kThisIsReceiver = 1u << 0,       // Parameter this is an Object.
    kNewTargetIsReceiver = 1u << 1,  // Parameter new.target is an Object.
  };
  using Flags = base::Flags<Flag>;

  Typer(JSHeapBroker* broker, Flags flags, Graph* graph,
        TickCounter* tick_counter);
  ~Typer();
  Typer(const Typer&) = delete;
  Typer& operator=(const Typer&) = delete;

  void Run();
  // TODO(bmeurer,jarin): Remove this once we have a notion of "roots" on Graph.
  void Run(const ZoneVector<Node*>& roots,
           LoopVariableOptimizer* induction_vars);

 private:
  class Visitor;
  class Decorator;

  Flags flags() const { return flags_; }
  Graph* graph() const { return graph_; }
  Zone* zone() const { return graph()->zone(); }
  OperationTyper* operation_typer() { return &operation_typer_; }
  JSHeapBroker* broker() const { return broker_; }

  Flags const flags_;
  Graph* const graph_;
  Decorator* decorator_;
  TypeCache const* cache_;
  JSHeapBroker* broker_;
  OperationTyper operation_typer_;
  TickCounter* const tick_counter_;

  Type singleton_false_;
  Type singleton_true_;
};

DEFINE_OPERATORS_FOR_FLAGS(Typer::Flags)

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOFAN_TYPER_H_
