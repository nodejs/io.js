// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_PROPERTIES_H_
#define V8_COMPILER_NODE_PROPERTIES_H_

#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/heap-refs.h"
#include "src/compiler/node.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/types.h"

namespace v8 {
namespace internal {
namespace compiler {

class Graph;
class Operator;
class CommonOperatorBuilder;

// A facade that simplifies access to the different kinds of inputs to a node.
class V8_EXPORT_PRIVATE NodeProperties {
 public:
  // ---------------------------------------------------------------------------
  // Input layout.
  // Inputs are always arranged in order as follows:
  //     0 [ values, context, frame state, effects, control ] node->InputCount()

  static int FirstValueIndex(const Node* node) { return 0; }
  static int FirstContextIndex(Node* node) { return PastValueIndex(node); }
  static int FirstFrameStateIndex(Node* node) { return PastContextIndex(node); }
  static int FirstEffectIndex(Node* node) { return PastFrameStateIndex(node); }
  static int FirstControlIndex(Node* node) { return PastEffectIndex(node); }

  static int PastValueIndex(Node* node) {
    return FirstValueIndex(node) + node->op()->ValueInputCount();
  }

  static int PastContextIndex(Node* node) {
    return FirstContextIndex(node) +
           OperatorProperties::GetContextInputCount(node->op());
  }

  static int PastFrameStateIndex(Node* node) {
    return FirstFrameStateIndex(node) +
           OperatorProperties::GetFrameStateInputCount(node->op());
  }

  static int PastEffectIndex(Node* node) {
    return FirstEffectIndex(node) + node->op()->EffectInputCount();
  }

  static int PastControlIndex(Node* node) {
    return FirstControlIndex(node) + node->op()->ControlInputCount();
  }

  // ---------------------------------------------------------------------------
  // Input accessors.

  static Node* GetValueInput(Node* node, int index) {
    CHECK_LE(0, index);
    CHECK_LT(index, node->op()->ValueInputCount());
    return node->InputAt(FirstValueIndex(node) + index);
  }

  static const Node* GetValueInput(const Node* node, int index) {
    CHECK_LE(0, index);
    CHECK_LT(index, node->op()->ValueInputCount());
    return node->InputAt(FirstValueIndex(node) + index);
  }

  static Node* GetContextInput(Node* node) {
    CHECK(OperatorProperties::HasContextInput(node->op()));
    return node->InputAt(FirstContextIndex(node));
  }

  static Node* GetFrameStateInput(Node* node) {
    CHECK(OperatorProperties::HasFrameStateInput(node->op()));
    return node->InputAt(FirstFrameStateIndex(node));
  }

  static Node* GetEffectInput(Node* node, int index = 0) {
    CHECK_LE(0, index);
    CHECK_LT(index, node->op()->EffectInputCount());
    return node->InputAt(FirstEffectIndex(node) + index);
  }

  static Node* GetControlInput(Node* node, int index = 0) {
    CHECK_LE(0, index);
    CHECK_LT(index, node->op()->ControlInputCount());
    return node->InputAt(FirstControlIndex(node) + index);
  }

  // ---------------------------------------------------------------------------
  // Edge kinds.

  static bool IsValueEdge(Edge edge);
  static bool IsContextEdge(Edge edge);
  static bool IsFrameStateEdge(Edge edge);
  static bool IsEffectEdge(Edge edge);
  static bool IsControlEdge(Edge edge);

  // ---------------------------------------------------------------------------
  // Miscellaneous predicates.

  static bool IsCommon(Node* node) {
    return IrOpcode::IsCommonOpcode(node->opcode());
  }
  static bool IsControl(Node* node) {
    return IrOpcode::IsControlOpcode(node->opcode());
  }
  static bool IsConstant(Node* node) {
    return IrOpcode::IsConstantOpcode(node->opcode());
  }
  static bool IsPhi(Node* node) {
    return IrOpcode::IsPhiOpcode(node->opcode());
  }
#if V8_ENABLE_WEBASSEMBLY
  static bool IsSimd128Operation(Node* node) {
    return IrOpcode::IsSimd128Opcode(node->opcode());
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // Determines whether exceptions thrown by the given node are handled locally
  // within the graph (i.e. an IfException projection is present). Optionally
  // the present IfException projection is returned via {out_exception}.
  static bool IsExceptionalCall(Node* node, Node** out_exception = nullptr);

  // Returns the node producing the successful control output of {node}. This is
  // the IfSuccess projection of {node} if present and {node} itself otherwise.
  static Node* FindSuccessfulControlProjection(Node* node);

  // Returns whether the node acts as the identity function on a value
  // input. The input that is passed through is returned via {out_value}.
  static bool IsValueIdentity(Node* node, Node** out_value) {
    switch (node->opcode()) {
      case IrOpcode::kTypeGuard:
        *out_value = GetValueInput(node, 0);
        return true;
      default:
        return false;
    }
  }

  // ---------------------------------------------------------------------------
  // Miscellaneous mutators.

  static void ReplaceValueInput(Node* node, Node* value, int index);
  static void ReplaceContextInput(Node* node, Node* context);
  static void ReplaceControlInput(Node* node, Node* control, int index = 0);
  static void ReplaceEffectInput(Node* node, Node* effect, int index = 0);
  static void ReplaceFrameStateInput(Node* node, Node* frame_state);
  static void RemoveNonValueInputs(Node* node);
  static void RemoveValueInputs(Node* node);

  // Replaces all value inputs of {node} with the single input {value}.
  static void ReplaceValueInputs(Node* node, Node* value);

  // Merge the control node {node} into the end of the graph, introducing a
  // merge node or expanding an existing merge node if necessary.
  static void MergeControlToEnd(Graph* graph, CommonOperatorBuilder* common,
                                Node* node);

  // Removes the control node {node} from the end of the graph, reducing the
  // existing merge node's input count.
  static void RemoveControlFromEnd(Graph* graph, CommonOperatorBuilder* common,
                                   Node* node);

  // Replace all uses of {node} with the given replacement nodes. All occurring
  // use kinds need to be replaced, {nullptr} is only valid if a use kind is
  // guaranteed not to exist.
  static void ReplaceUses(Node* node, Node* value, Node* effect = nullptr,
                          Node* success = nullptr, Node* exception = nullptr);

  // Safe wrapper to mutate the operator of a node. Checks that the node is
  // currently in a state that satisfies constraints of the new operator.
  static void ChangeOp(Node* node, const Operator* new_op);
  // Like `ChangeOp`, but without checking constraints.
  static void ChangeOpUnchecked(Node* node, const Operator* new_op);

  // ---------------------------------------------------------------------------
  // Miscellaneous utilities.

  // Find the last frame state that is effect-wise before the given node. This
  // assumes a linear effect-chain up to a {CheckPoint} node in the graph.
  // Returns {unreachable_sentinel} if {node} is determined to be unreachable.
  static Node* FindFrameStateBefore(Node* node, Node* unreachable_sentinel);

  // Collect the output-value projection for the given output index.
  static Node* FindProjection(Node* node, size_t projection_index);

  // Collect the value projections from a node.
  static void CollectValueProjections(Node* node, Node** proj, size_t count);

  // Collect the branch-related projections from a node, such as IfTrue,
  // IfFalse, IfSuccess, IfException, IfValue and IfDefault.
  //  - Branch: [ IfTrue, IfFalse ]
  //  - Call  : [ IfSuccess, IfException ]
  //  - Switch: [ IfValue, ..., IfDefault ]
  static void CollectControlProjections(Node* node, Node** proj, size_t count);

  // Return the MachineRepresentation of a Projection based on its input.
  static MachineRepresentation GetProjectionType(Node const* projection);

  // Checks if two nodes are the same, looking past {CheckHeapObject}.
  static bool IsSame(Node* a, Node* b);

  // Check if two nodes have equal operators and reference-equal inputs. Used
  // for value numbering/hash-consing.
  static bool Equals(Node* a, Node* b);
  // A corresponding hash function.
  static size_t HashCode(Node* node);

  // Walks up the {effect} chain to find a witness that provides map
  // information about the {receiver}. Can look through potentially
  // side effecting nodes.
  enum InferMapsResult {
    kNoMaps,         // No maps inferred.
    kReliableMaps,   // Maps can be trusted.
    kUnreliableMaps  // Maps might have changed (side-effect).
  };
  // DO NOT USE InferMapsUnsafe IN NEW CODE. Use MapInference instead.
  static InferMapsResult InferMapsUnsafe(JSHeapBroker* broker, Node* receiver,
                                         Effect effect,
                                         ZoneRefSet<Map>* maps_out);

  // Return the initial map of the new-target if the allocation can be inlined.
  static OptionalMapRef GetJSCreateMap(JSHeapBroker* broker, Node* receiver);

  // Walks up the {effect} chain to check that there's no observable side-effect
  // between the {effect} and it's {dominator}. Aborts the walk if there's join
  // in the effect chain.
  static bool NoObservableSideEffectBetween(Node* effect, Node* dominator);

  // Returns true if the {receiver} can be a primitive value (i.e. is not
  // definitely a JavaScript object); might walk up the {effect} chain to
  // find map checks on {receiver}.
  static bool CanBePrimitive(JSHeapBroker* broker, Node* receiver,
                             Effect effect);

  // Returns true if the {receiver} can be null or undefined. Might walk
  // up the {effect} chain to find map checks for {receiver}.
  static bool CanBeNullOrUndefined(JSHeapBroker* broker, Node* receiver,
                                   Effect effect);

  // ---------------------------------------------------------------------------
  // Context.

  // Walk up the context chain from the given {node} until we reduce the {depth}
  // to 0 or hit a node that does not extend the context chain ({depth} will be
  // updated accordingly).
  static Node* GetOuterContext(Node* node, size_t* depth);

  // ---------------------------------------------------------------------------
  // Type.

  static bool IsTyped(const Node* node) { return !node->type().IsInvalid(); }
  static Type GetType(const Node* node) {
    DCHECK(IsTyped(node));
    return node->type();
  }
  static Type GetTypeOrAny(const Node* node);
  static void SetType(Node* node, Type type) {
    DCHECK(!type.IsInvalid());
    node->set_type(type);
  }
  static void RemoveType(Node* node) { node->set_type(Type::Invalid()); }
  static bool AllValueInputsAreTyped(Node* node);

 private:
  static inline bool IsInputRange(Edge edge, int first, int count);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_PROPERTIES_H_
