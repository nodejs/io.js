// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SIMPLIFIED_LOWERING_VERIFIER_H_
#define V8_COMPILER_SIMPLIFIED_LOWERING_VERIFIER_H_

#include <optional>

#include "src/base/container-utils.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/representation-change.h"

namespace v8 {
namespace internal {
namespace compiler {

class OperationTyper;

class SimplifiedLoweringVerifier final {
 public:
  struct PerNodeData {
    std::optional<Type> type = std::nullopt;
    Truncation truncation = Truncation::Any(IdentifyZeros::kDistinguishZeros);
  };

  SimplifiedLoweringVerifier(Zone* zone, Graph* graph)
      : hints_(zone),
        machine_uses_of_constants_(zone),
        data_(zone),
        graph_(graph),
        zone_(zone) {}

  void VisitNode(Node* node, OperationTyper& op_typer);

  void RecordHint(Node* node) {
    DCHECK_EQ(node->opcode(), IrOpcode::kSLVerifierHint);
    hints_.push_back(node);
  }
  const ZoneVector<Node*>& inserted_hints() const { return hints_; }
  void RecordMachineUsesOfConstant(Node* constant, Node::Uses uses) {
    DCHECK(IrOpcode::IsMachineConstantOpcode(constant->opcode()));
    auto it = machine_uses_of_constants_.find(constant);
    if (it == machine_uses_of_constants_.end()) {
      it =
          machine_uses_of_constants_.emplace(constant, ZoneVector<Node*>(zone_))
              .first;
    }
    base::vector_append(it->second, uses);
  }
  const ZoneUnorderedMap<Node*, ZoneVector<Node*>>& machine_uses_of_constants()
      const {
    return machine_uses_of_constants_;
  }

  std::optional<Type> GetType(Node* node) const {
    if (NodeProperties::IsTyped(node)) {
      Type type = NodeProperties::GetType(node);
      // We do not use the static type for constants, even if we have one,
      // because those are cached in the graph and shared between machine
      // and non-machine subgraphs. The former might have assigned
      // Type::Machine() to them.
      if (IrOpcode::IsMachineConstantOpcode(node->opcode())) {
        DCHECK(type.Is(Type::Machine()));
      } else {
        return type;
      }
    }
    // For nodes that have not been typed before SL, we use the type that has
    // been inferred by the verifier.
    if (node->id() < data_.size()) {
      return data_[node->id()].type;
    }
    return std::nullopt;
  }

 private:
  void ResizeDataIfNecessary(Node* node) {
    if (data_.size() <= node->id()) {
      data_.resize(node->id() + 1);
    }
    DCHECK_EQ(data_[node->id()].truncation,
              Truncation::Any(IdentifyZeros::kDistinguishZeros));
  }

  void SetType(Node* node, const Type& type) {
    ResizeDataIfNecessary(node);
    data_[node->id()].type = type;
  }

  Type InputType(Node* node, int input_index) const {
    // TODO(nicohartmann): Check that inputs are typed, once all operators are
    // supported.
    auto type_opt = GetType(node->InputAt(input_index));
    return type_opt.has_value() ? *type_opt : Type::None();
  }

  void SetTruncation(Node* node, const Truncation& truncation) {
    ResizeDataIfNecessary(node);
    data_[node->id()].truncation = truncation;
  }

  Truncation InputTruncation(Node* node, int input_index) const {
    static const Truncation any_truncation =
        Truncation::Any(IdentifyZeros::kDistinguishZeros);

    Node* input = node->InputAt(input_index);
    if (input->id() < data_.size()) {
      return data_[input->id()].truncation;
    }
    return any_truncation;
  }

  void CheckType(Node* node, const Type& type);
  void CheckAndSet(Node* node, const Type& type, const Truncation& trunc);
  void ReportInvalidTypeCombination(Node* node, const std::vector<Type>& types);

  // Generalize to a less strict truncation in the context of a given type. For
  // example, a Truncation::kWord32[kIdentifyZeros] does not have any effect on
  // a type Range(0, 100), because all equivalence classes are singleton, for
  // the values of the given type. We can use Truncation::Any[kDistinguishZeros]
  // instead to avoid a combinatorial explosion of occurring type-truncation-
  // pairs.
  Truncation GeneralizeTruncation(const Truncation& truncation,
                                  const Type& type) const;
  Truncation JoinTruncation(const Truncation& t1, const Truncation& t2);
  Truncation JoinTruncation(const Truncation& t1, const Truncation& t2,
                            const Truncation& t3) {
    return JoinTruncation(JoinTruncation(t1, t2), t3);
  }

  Zone* graph_zone() const { return graph_->zone(); }

  ZoneVector<Node*> hints_;
  ZoneUnorderedMap<Node*, ZoneVector<Node*>> machine_uses_of_constants_;
  ZoneVector<PerNodeData> data_;
  Graph* graph_;
  Zone* zone_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SIMPLIFIED_LOWERING_VERIFIER_H_
