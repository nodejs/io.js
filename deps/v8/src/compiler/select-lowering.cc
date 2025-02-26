// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/select-lowering.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph.h"

namespace v8 {
namespace internal {
namespace compiler {

SelectLowering::SelectLowering(JSGraphAssembler* graph_assembler, Graph* graph)
    : graph_assembler_(graph_assembler), start_(graph->start()) {}

SelectLowering::~SelectLowering() = default;

Reduction SelectLowering::Reduce(Node* node) {
  if (node->opcode() != IrOpcode::kSelect) return NoChange();
  return LowerSelect(node);
}

#define __ gasm()->

Reduction SelectLowering::LowerSelect(Node* node) {
  SelectParameters const p = SelectParametersOf(node->op());

  Node* condition = node->InputAt(0);
  Node* vtrue = node->InputAt(1);
  Node* vfalse = node->InputAt(2);

  bool reset_gasm = false;
  if (gasm()->control() == nullptr) {
    gasm()->InitializeEffectControl(start(), start());
    reset_gasm = true;
  }

  auto done = __ MakeLabel(p.representation());

  __ GotoIf(condition, &done, vtrue);
  __ Goto(&done, vfalse);
  __ Bind(&done);

  if (reset_gasm) {
    gasm()->Reset();
  }

  return Changed(done.PhiAt(0));
}

#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
