// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/string-builder-optimizer.h"

#include <algorithm>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/optional.h"
#include "src/base/small-vector.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/types.h"
#include "src/objects/code.h"
#include "src/objects/map-inl.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace {

// Returns true if {node} is a kStringConcat or a kNewConsString.
bool IsConcat(Node* node) {
  return node->opcode() == IrOpcode::kStringConcat ||
         node->opcode() == IrOpcode::kNewConsString;
}

// Returns true if {node} is considered as a literal string by the string
// builder optimizer:
//    - it's a literal string
//    - or it's a kStringFromSingleCharCode
bool IsLiteralString(Node* node, JSHeapBroker* broker) {
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      HeapObjectMatcher m(node);
      return m.HasResolvedValue() && m.Ref(broker).IsString() &&
             m.Ref(broker).AsString().IsContentAccessible();
    }
    case IrOpcode::kStringFromSingleCharCode:
      return true;
    default:
      return false;
  }
}

// Returns true if {node} has at least one concatenation or phi in its uses.
bool HasConcatOrPhiUse(Node* node) {
  for (Node* use : node->uses()) {
    if (IsConcat(use) || use->opcode() == IrOpcode::kPhi) {
      return true;
    }
  }
  return false;
}

}  // namespace

OneOrTwoByteAnalysis::State OneOrTwoByteAnalysis::ConcatResultIsOneOrTwoByte(
    State a, State b) {
  DCHECK(a != State::kUnknown && b != State::kUnknown);
  if (a == State::kOneByte && b == State::kOneByte) {
    return State::kOneByte;
  }
  if (a == State::kTwoByte || b == State::kTwoByte) {
    return State::kTwoByte;
  }
  return State::kCantKnow;
}

base::Optional<std::pair<int64_t, int64_t>> OneOrTwoByteAnalysis::TryGetRange(
    Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kChangeTaggedToFloat64:
    case IrOpcode::kTruncateFloat64ToWord32:
      return TryGetRange(node->InputAt(0));

    case IrOpcode::kInt32Add:
    case IrOpcode::kInt32AddWithOverflow:
    case IrOpcode::kInt64Add:
    case IrOpcode::kInt64AddWithOverflow:
    case IrOpcode::kFloat32Add:
    case IrOpcode::kFloat64Add: {
      base::Optional<std::pair<int64_t, int64_t>> left =
          TryGetRange(node->InputAt(0));
      base::Optional<std::pair<int64_t, int64_t>> right =
          TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        int32_t high_bound;
        if (base::bits::SignedAddOverflow32(static_cast<int32_t>(left->second),
                                            static_cast<int32_t>(right->second),
                                            &high_bound)) {
          // The range would overflow a 32-bit integer.
          return base::nullopt;
        }
        return std::pair{left->first + right->first, high_bound};
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kInt32Sub:
    case IrOpcode::kInt32SubWithOverflow:
    case IrOpcode::kInt64Sub:
    case IrOpcode::kInt64SubWithOverflow:
    case IrOpcode::kFloat32Sub:
    case IrOpcode::kFloat64Sub: {
      base::Optional<std::pair<int64_t, int64_t>> left =
          TryGetRange(node->InputAt(0));
      base::Optional<std::pair<int64_t, int64_t>> right =
          TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        if (left->first - right->second < 0) {
          // The range would contain negative values.
          return base::nullopt;
        }
        return std::pair{left->first - right->second,
                         left->second - right->first};
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kWord32And:
    case IrOpcode::kWord64And: {
      // Note that the minimal value for "a & b" is always 0, regardless of the
      // max for "a" or "b". And the maximal value is the min of "max of a" and
      // "max of b".
      base::Optional<std::pair<int64_t, int64_t>> left =
          TryGetRange(node->InputAt(0));
      base::Optional<std::pair<int64_t, int64_t>> right =
          TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        return std::pair{0, std::min(left->second, right->second)};
      } else if (left.has_value()) {
        return std::pair{0, left->second};
      } else if (right.has_value()) {
        return std::pair{0, right->second};
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kInt32Mul:
    case IrOpcode::kInt32MulWithOverflow:
    case IrOpcode::kInt64Mul:
    case IrOpcode::kFloat32Mul:
    case IrOpcode::kFloat64Mul: {
      base::Optional<std::pair<int64_t, int64_t>> left =
          TryGetRange(node->InputAt(0));
      base::Optional<std::pair<int64_t, int64_t>> right =
          TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        int32_t high_bound;
        if (base::bits::SignedMulOverflow32(static_cast<int32_t>(left->second),
                                            static_cast<int32_t>(right->second),
                                            &high_bound)) {
          // The range would overflow a 32-bit integer.
          return base::nullopt;
        }
        return std::pair{left->first * right->first,
                         left->second * right->second};
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kCall: {
      HeapObjectMatcher m(node->InputAt(0));
      if (m.HasResolvedValue() && m.Ref(broker()).IsCode()) {
        CodeRef code = m.Ref(broker()).AsCode();
        if (code.object()->is_builtin()) {
          Builtin builtin = code.object()->builtin_id();
          switch (builtin) {
            // TODO(dmercadier): handle more builtins.
            case Builtin::kMathRandom:
              return std::pair{0, 1};
            default:
              return base::nullopt;
          }
        }
      }
      return base::nullopt;
    }

#define CONST_CASE(op, matcher)                                       \
  case IrOpcode::k##op: {                                             \
    matcher m(node);                                                  \
    if (m.HasResolvedValue()) {                                       \
      if (m.ResolvedValue() < 0 ||                                    \
          m.ResolvedValue() >= std::numeric_limits<int32_t>::min()) { \
        return base::nullopt;                                         \
      }                                                               \
      return std::pair{m.ResolvedValue(), m.ResolvedValue()};         \
    } else {                                                          \
      return base::nullopt;                                           \
    }                                                                 \
  }
      CONST_CASE(Float32Constant, Float32Matcher)
      CONST_CASE(Float64Constant, Float64Matcher)
      CONST_CASE(Int32Constant, Int32Matcher)
      CONST_CASE(Int64Constant, Int64Matcher)
      CONST_CASE(NumberConstant, NumberMatcher)
#undef CONST_CASE

    default:
      return base::nullopt;
  }
}

// Tries to determine whether {node} is a 1-byte or a 2-byte string. This
// function assumes that {node} is part of a string builder: if it's a
// concatenation and its left hand-side is something else than a literal string,
// it returns only whether the right hand-side is 1/2-byte: the String builder
// analysis will take care of propagating the state of the left hand-side.
OneOrTwoByteAnalysis::State OneOrTwoByteAnalysis::OneOrTwoByte(Node* node) {
  // TODO(v8:13785,dmercadier): once externalization can no longer convert a
  // 1-byte into a 2-byte string, compute the proper OneOrTwoByte state.
  return State::kCantKnow;
#if 0
  if (states_[node->id()] != State::kUnknown) {
    return states_[node->id()];
  }
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      HeapObjectMatcher m(node);
      if (m.HasResolvedValue() && m.Ref(broker()).IsString()) {
        StringRef string = m.Ref(broker()).AsString();
        if (string.object()->IsOneByteRepresentation()) {
          states_[node->id()] = State::kOneByte;
          return State::kOneByte;
        } else {
          DCHECK(string.object()->IsTwoByteRepresentation());
          states_[node->id()] = State::kTwoByte;
          return State::kTwoByte;
        }
      } else {
        states_[node->id()] = State::kCantKnow;
        return State::kCantKnow;
      }
    }

    case IrOpcode::kStringFromSingleCharCode: {
      Node* input = node->InputAt(0);
      switch (input->opcode()) {
        case IrOpcode::kStringCharCodeAt: {
          State state = OneOrTwoByte(input->InputAt(0));
          states_[node->id()] = state;
          return state;
        }

        default: {
          base::Optional<std::pair<int64_t, int64_t>> range =
              TryGetRange(input);
          if (!range.has_value()) {
            states_[node->id()] = State::kCantKnow;
            return State::kCantKnow;
          } else if (range->first >= 0 && range->second < 255) {
            states_[node->id()] = State::kOneByte;
            return State::kOneByte;
          } else {
            // For values greater than 0xFF, with the current analysis, we have
            // no way of knowing if the result will be on 1 or 2 bytes. For
            // instance, `String.fromCharCode(0x120064 & 0xffff)` will
            // be a 1-byte string, although the analysis will consider that its
            // range is [0, 0xffff].
            states_[node->id()] = State::kCantKnow;
            return State::kCantKnow;
          }
        }
      }
    }

    case IrOpcode::kStringConcat:
    case IrOpcode::kNewConsString: {
      Node* lhs = node->InputAt(1);
      Node* rhs = node->InputAt(2);

      DCHECK(IsLiteralString(rhs, broker()));
      State rhs_state = OneOrTwoByte(rhs);

      // OneOrTwoByte is only called for Nodes that are part of a String
      // Builder. As a result, a StringConcat/NewConsString is either:
      //  - between 2 string literal if it is the 1st concatenation of the
      //    string builder.
      //  - between the beginning of the string builder and a literal string.
      // Thus, if {lhs} is not a literal string, we ignore its State: the
      // analysis should already have been done on its predecessors anyways.
      State lhs_state =
          IsLiteralString(lhs, broker()) ? OneOrTwoByte(lhs) : rhs_state;

      State node_state = ConcatResultIsOneOrTwoByte(rhs_state, lhs_state);
      states_[node->id()] = node_state;

      return node_state;
    }

    default:
      states_[node->id()] = State::kCantKnow;
      return State::kCantKnow;
  }
#endif
}

bool StringBuilderOptimizer::BlockShouldFinalizeStringBuilders(
    BasicBlock* block) {
  DCHECK_LT(block->id().ToInt(), blocks_to_trimmings_map_.size());
  return blocks_to_trimmings_map_[block->id().ToInt()].has_value();
}

ZoneVector<Node*> StringBuilderOptimizer::GetStringBuildersToFinalize(
    BasicBlock* block) {
  DCHECK(BlockShouldFinalizeStringBuilders(block));
  return blocks_to_trimmings_map_[block->id().ToInt()].value();
}

OneOrTwoByteAnalysis::State StringBuilderOptimizer::GetOneOrTwoByte(
    Node* node) {
  DCHECK(ConcatIsInStringBuilder(node));
  // TODO(v8:13785,dmercadier): once externalization can no longer convert a
  // 1-byte into a 2-byte string, return the proper OneOrTwoByte status for the
  // node (= remove the next line and uncomment the 2 after).
  return OneOrTwoByteAnalysis::State::kCantKnow;
  // int string_builder_number = GetStringBuilderIdForConcat(node);
  // return string_builders_[string_builder_number].one_or_two_bytes;
}

bool StringBuilderOptimizer::IsStringBuilderEnd(Node* node) {
  Status status = GetStatus(node);
  DCHECK_IMPLIES(status.state == State::kEndStringBuilder ||
                     status.state == State::kEndStringBuilderLoopPhi,
                 status.id != kInvalidId &&
                     StringBuilderIsValid(string_builders_[status.id]));
  return status.state == State::kEndStringBuilder ||
         status.state == State::kEndStringBuilderLoopPhi;
}

bool StringBuilderOptimizer::IsNonLoopPhiStringBuilderEnd(Node* node) {
  return IsStringBuilderEnd(node) && !IsLoopPhi(node);
}

bool StringBuilderOptimizer::IsStringBuilderConcatInput(Node* node) {
  Status status = GetStatus(node);
  DCHECK_IMPLIES(status.state == State::kConfirmedInStringBuilder,
                 status.id != kInvalidId &&
                     StringBuilderIsValid(string_builders_[status.id]));
  return status.state == State::kConfirmedInStringBuilder;
}

bool StringBuilderOptimizer::ConcatIsInStringBuilder(Node* node) {
  DCHECK(IsConcat(node));
  Status status = GetStatus(node);
  DCHECK_IMPLIES(status.state == State::kConfirmedInStringBuilder ||
                     status.state == State::kBeginStringBuilder ||
                     status.state == State::kEndStringBuilder,
                 status.id != kInvalidId &&
                     StringBuilderIsValid(string_builders_[status.id]));
  return status.state == State::kConfirmedInStringBuilder ||
         status.state == State::kBeginStringBuilder ||
         status.state == State::kEndStringBuilder;
}

int StringBuilderOptimizer::GetStringBuilderIdForConcat(Node* node) {
  DCHECK(IsConcat(node));
  Status status = GetStatus(node);
  DCHECK(status.state == State::kConfirmedInStringBuilder ||
         status.state == State::kBeginStringBuilder ||
         status.state == State::kEndStringBuilder);
  DCHECK_NE(status.id, kInvalidId);
  return status.id;
}

bool StringBuilderOptimizer::IsFirstConcatInStringBuilder(Node* node) {
  if (!ConcatIsInStringBuilder(node)) return false;
  Status status = GetStatus(node);
  return status.state == State::kBeginStringBuilder;
}

// Duplicates the {input_idx}th input of {node} if it has multiple uses, so that
// the replacement only has one use and can safely be marked as
// State::kConfirmedInStringBuilder and properly optimized in
// EffectControlLinearizer (in particular, this will allow to safely remove
// StringFromSingleCharCode that are only used for a StringConcat that we
// optimize).
void StringBuilderOptimizer::ReplaceConcatInputIfNeeded(Node* node,
                                                        int input_idx) {
  if (!IsLiteralString(node->InputAt(input_idx), broker())) return;
  Node* input = node->InputAt(input_idx);
  DCHECK_EQ(input->op()->EffectOutputCount(), 0);
  DCHECK_EQ(input->op()->ControlOutputCount(), 0);
  if (input->UseCount() > 1) {
    input = graph()->CloneNode(input);
    node->ReplaceInput(input_idx, input);
  }
  Status node_status = GetStatus(node);
  DCHECK_NE(node_status.id, kInvalidId);
  SetStatus(input, State::kConfirmedInStringBuilder, node_status.id);
}

// If all of the predecessors of {node} are part of a string builder and have
// the same id, returns this id. Otherwise, returns kInvalidId.
int StringBuilderOptimizer::GetPhiPredecessorsCommonId(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kPhi);
  int id = kInvalidId;
  for (int i = 0; i < node->op()->ValueInputCount(); i++) {
    Node* input = NodeProperties::GetValueInput(node, i);
    Status status = GetStatus(input);
    switch (status.state) {
      case State::kBeginStringBuilder:
      case State::kInStringBuilder:
      case State::kPendingPhi:
        if (id == kInvalidId) {
          // Initializind {id}.
          id = status.id;
        } else if (id != status.id) {
          // 2 inputs belong to different StringBuilder chains.
          return kInvalidId;
        }
        break;
      case State::kInvalid:
      case State::kUnvisited:
        return kInvalidId;
      default:
        UNREACHABLE();
    }
  }
  DCHECK_NE(id, kInvalidId);
  return id;
}

namespace {

// Returns true if {first} comes before {second} in {block}.
bool ComesBeforeInBlock(Node* first, Node* second, BasicBlock* block) {
  for (Node* node : *block->nodes()) {
    if (node == first) {
      return true;
    }
    if (node == second) {
      return false;
    }
  }
  UNREACHABLE();
}

static constexpr int kMaxPredecessors = 15;

// Compute up to {kMaxPredecessors} predecessors of {start} that are not past
// {end}, and store them in {dst}. Returns true if there are less than
// {kMaxPredecessors} such predecessors and false otherwise.
bool ComputePredecessors(
    BasicBlock* start, BasicBlock* end,
    base::SmallVector<BasicBlock*, kMaxPredecessors>* dst) {
  dst->push_back(start);
  size_t stack_pointer = 0;
  while (stack_pointer < dst->size()) {
    BasicBlock* current = (*dst)[stack_pointer++];
    if (current == end) continue;
    for (BasicBlock* pred : current->predecessors()) {
      if (std::find(dst->begin(), dst->end(), pred) == dst->end()) {
        if (dst->size() == kMaxPredecessors) return false;
        dst->push_back(pred);
      }
    }
  }
  return true;
}

// Returns false if {node} makes its string input escape this use. For instance,
// a Phi or a Store make their input escape, but a kStringLength consumes its
// inputs.
bool OpcodeIsAllowed(IrOpcode::Value op) {
  switch (op) {
    case IrOpcode::kStringLength:
    case IrOpcode::kStringConcat:
    case IrOpcode::kNewConsString:
    case IrOpcode::kStringCharCodeAt:
    case IrOpcode::kStringCodePointAt:
    case IrOpcode::kStringIndexOf:
    case IrOpcode::kObjectIsString:
    case IrOpcode::kStringToLowerCaseIntl:
    case IrOpcode::kStringToNumber:
    case IrOpcode::kStringToUpperCaseIntl:
    case IrOpcode::kStringEqual:
    case IrOpcode::kStringLessThan:
    case IrOpcode::kStringLessThanOrEqual:
    case IrOpcode::kCheckString:
    case IrOpcode::kCheckStringOrStringWrapper:
    case IrOpcode::kTypedStateValues:
      return true;
    default:
      return false;
  }
}

// Returns true if {sb_child_block} can be a valid successor for
// {previous_block} in the string builder, considering that {other_child_block}
// is another successor of {previous_block} (which uses the string builder that
// is in {previous_block}).We are mainly checking for the following scenario:
//
//               |
//               v
//       +---> LoopPhi
//       |       |
//       |       v
//       |      node ----------> other_child
//       |       |
//       |       v
//       |     child
//       |      ...
//       |       |
//       +-------+
//
// Where {node} and {child} are inside a loop (and could be part of a string
// builder), but {other_child} is not, and the control flow doesn't exit the
// loop in between {node} and {child}. The string builder should not be used in
// such situations, because by the time {other_child} is reached, its input will
// be invalid, because {child} will have mutated it. (here, node's block would
// be {previous_block}, child's would be {sb_child_block} and other_child's
// would be {other_child_block}).
bool ValidControlFlowForStringBuilder(BasicBlock* sb_child_block,
                                      BasicBlock* other_child_block,
                                      BasicBlock* previous_block,
                                      ZoneVector<BasicBlock*> loop_headers) {
  if (loop_headers.empty()) return true;
  // Due to how we visit the graph, {sb_child_block} is the block that
  // VisitGraph is currently visiting, which means that it has to be in all the
  // loops of {loop_headers} (and in particular in the latest one).
  // {other_child_block} on the other hand could be in the loop or not, which is
  // what this function tries to determine.
  DCHECK(loop_headers.back()->LoopContains(sb_child_block));
  if (sb_child_block->IsLoopHeader()) {
    // {sb_child_block} starts a loop. This is OK for {other_child_block} only
    // if {other_child_block} is before the loop (because if it's after, then
    // the value it will receive will be invalid), or if both
    // {other_child_block} and {previous_block} are inside the loop. The latter
    // case corresponds to:
    //
    //  +--------> sb_child_block
    //  |         /             \
    //  |        |               \
    //  |        v                v
    //  | previous_block         other_child_block
    //  |        |
    //  +--------+
    //
    // Where {other_child_block} eventually reaches {previous_block} (or exits
    // the loop through some other path).
    return other_child_block->rpo_number() < sb_child_block->rpo_number() ||
           (sb_child_block->LoopContains(previous_block) &&
            (sb_child_block->LoopContains(other_child_block)));
  } else {
    // Both {sb_child_block} and {other_child_block} should be in the same loop.
    return loop_headers.back()->LoopContains(other_child_block);
  }
}

// Return true if {maybe_dominator} dominates {maybe_dominee} and is less than
// {kMaxDominatorSteps} steps away (to avoid going back too far if
// {maybe_dominee} is much deeper in the graph that {maybe_dominator}).
bool IsClosebyDominator(BasicBlock* maybe_dominator,
                        BasicBlock* maybe_dominee) {
  static constexpr int kMaxDominatorSteps = 10;
  if (maybe_dominee->dominator_depth() + kMaxDominatorSteps <
      maybe_dominator->dominator_depth()) {
    // {maybe_dominee} is too far from {maybe_dominator} to compute quickly if
    // it's dominated by {maybe_dominator} or not.
    return false;
  }
  while (maybe_dominee != maybe_dominator &&
         maybe_dominator->dominator_depth() <
             maybe_dominee->dominator_depth()) {
    maybe_dominee = maybe_dominee->dominator();
  }
  return maybe_dominee == maybe_dominator;
}

// Returns true if {node} is a Phi that has both {input1} and {input2} as
// inputs.
bool IsPhiContainingGivenInputs(Node* node, Node* input1, Node* input2,
                                Schedule* schedule) {
  if (node->opcode() != IrOpcode::kPhi ||
      schedule->block(node)->IsLoopHeader()) {
    return false;
  }
  bool has_input1 = false, has_input2 = false;
  for (Node* input : node->inputs()) {
    if (input == input1) {
      has_input1 = true;
    } else if (input == input2) {
      has_input2 = true;
    }
  }
  return has_input1 && has_input2;
}

// Returns true if {phi} has 3 inputs (including the Loop or Merge), and its
// first two inputs are either Phi themselves, or StringConcat/NewConsString.
// This is used to quickly eliminate Phi nodes that cannot be part of a String
// Builder.
bool PhiInputsAreConcatsOrPhi(Node* phi) {
  DCHECK_EQ(phi->opcode(), IrOpcode::kPhi);
  return phi->InputCount() == 3 &&
         (phi->InputAt(0)->opcode() == IrOpcode::kPhi ||
          IsConcat(phi->InputAt(0))) &&
         (phi->InputAt(1)->opcode() == IrOpcode::kPhi ||
          IsConcat(phi->InputAt(1)));
}

}  // namespace

// Check that the uses of {node} are valid, assuming that {string_builder_child}
// is the following node in the string builder. In a nutshell, for uses of a
// node (that is part of the string builder) to be valid, they need to all
// appear before the next node of the string builder (because after, the node is
// not valid anymore because we mutate SlicedString and the backing store in
// place). For instance:
//
//     s1 = "123" + "abc";
//     s2 = s1 + "def";
//     l = s1.length();
//
// In this snippet, if `s1` and `s2` are part of the string builder, then the
// uses of `s1` are not actually valid, because `s1.length()` appears after the
// next node of the string builder (`s2`) has been computed.
bool StringBuilderOptimizer::CheckNodeUses(Node* node,
                                           Node* string_builder_child,
                                           Status status) {
  DCHECK(GetStatus(string_builder_child).state == State::kInStringBuilder ||
         GetStatus(string_builder_child).state == State::kPendingPhi);
  BasicBlock* child_block = schedule()->block(string_builder_child);
  if (node->UseCount() == 1) return true;
  BasicBlock* node_block = schedule()->block(node);
  bool is_loop_phi = IsLoopPhi(node);
  bool child_is_in_loop =
      is_loop_phi && LoopContains(node, string_builder_child);
  base::SmallVector<BasicBlock*, kMaxPredecessors> current_predecessors;
  bool predecessors_computed = false;
  for (Node* other_child : node->uses()) {
    if (other_child == string_builder_child) continue;
    BasicBlock* other_child_block = schedule()->block(other_child);
    if (!OpcodeIsAllowed(other_child->opcode())) {
      // {other_child} could write {node} (the beginning of the string builder)
      // in memory (or keep it alive through other means, such as a Phi). This
      // means that if {string_builder_child} modifies the string builder, then
      // the value stored by {other_child} will become out-dated (since
      // {other_child} will probably just write a pointer to the string in
      // memory, and the string pointed by this pointer will be updated by the
      // string builder).
      if (is_loop_phi && child_is_in_loop &&
          !node_block->LoopContains(other_child_block)) {
        // {other_child} keeps the string alive, but this is only after the
        // loop, when {string_builder_child} isn't alive anymore, so this isn't
        // an issue.
        continue;
      }
      return false;
    }
    if (other_child_block == child_block) {
      // Both {child} and {other_child} are in the same block, we need to make
      // sure that {other_child} comes first.
      Status other_status = GetStatus(other_child);
      if (other_status.id != kInvalidId) {
        DCHECK_EQ(other_status.id, status.id);
        // {node} flows into 2 different nodes of the string builder, both of
        // which are in the same BasicBlock, which is not supported. We need to
        // invalidate {other_child} as well, or the input of {child} could be
        // wrong. In theory, we could keep one of {other_child} and {child} (the
        // one that comes the later in the BasicBlock), but it's simpler to keep
        // neither, and end the string builder on {node}.
        SetStatus(other_child, State::kInvalid);
        return false;
      }
      if (!ComesBeforeInBlock(other_child, string_builder_child, child_block)) {
        return false;
      }
      continue;
    }
    if (is_loop_phi) {
      if ((child_is_in_loop && !node_block->LoopContains(other_child_block)) ||
          (!child_is_in_loop && node_block->LoopContains(other_child_block))) {
        // {child} is in the loop and {other_child} isn't (or the other way
        // around). In that case, we skip {other_child}: it will be tested
        // later when we leave the loop (if {child} is in the loop) or has
        // been tested earlier while we were inside the loop (if {child} isn't
        // in the loop).
        continue;
      }
    } else if (!ValidControlFlowForStringBuilder(child_block, other_child_block,
                                                 node_block, loop_headers_)) {
      return false;
    }

    if (IsPhiContainingGivenInputs(other_child, node, string_builder_child,
                                   schedule())) {
      // {other_child} is a Phi that merges {child} and {node} (and maybe some
      // other nodes that we don't care about for now: if {other_child} merges
      // more than 2 nodes, it won't be added to the string builder anyways).
      continue;
    }

    base::SmallVector<BasicBlock*, kMaxPredecessors> other_predecessors;
    bool all_other_predecessors_computed =
        ComputePredecessors(other_child_block, node_block, &other_predecessors);

    // Making sure that {child_block} isn't in the predecessors of
    // {other_child_block}. Otherwise, the use of {node} in {other_child}
    // would be invalid.
    if (std::find(other_predecessors.begin(), other_predecessors.end(),
                  child_block) != other_predecessors.end()) {
      // {child} is in the predecessor of {other_child}, which is definitely
      // invalid (because it means that {other_child} uses an out-dated version
      // of {node}, since {child} modified it).
      return false;
    } else {
      if (all_other_predecessors_computed) {
        // {child} is definitely not in the predecessors of {other_child}, which
        // means that it's either a successor of {other_child} (which is safe),
        // or it's in another path of the graph alltogether (which is also
        // safe).
        continue;
      } else {
        // We didn't compute all the predecessors of {other_child}, so it's
        // possible that {child_block} is one of the predecessor that we didn't
        // compute.
        //
        // Trying to see if we can find {other_child_block} in the
        // predecessors of {child_block}: that would mean that {other_child}
        // is guaranteed to be scheduled before {child}, making it safe.
        if (!predecessors_computed) {
          ComputePredecessors(child_block, node_block, &current_predecessors);
          predecessors_computed = true;
        }
        if (std::find(current_predecessors.begin(), current_predecessors.end(),
                      other_child_block) == current_predecessors.end()) {
          // We didn't find {other_child} in the predecessors of {child}. It
          // means that either {other_child} comes after in the graph (which
          // is unsafe), or that {other_child} and {child} are on two
          // independent subgraphs (which is safe). We have no efficient way
          // to know which one of the two this is, so, we fall back to a
          // stricter approach: the use of {node} in {other_child} is
          // guaranteed to be safe if {other_child_block} dominates
          // {child_block}.
          if (!IsClosebyDominator(other_child_block, child_block)) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

// Check that the uses of the predecessor(s) of {child} in the string builder
// are valid, with respect to {child}. This sounds a bit backwards, but we can't
// check if uses are valid before having computed what the next node in the
// string builder is. Hence, once we've established that {child} is in the
// string builder, we check that the uses of the previous node(s) of the
// string builder are valid. For non-loop phis (ie, merge phis), we simply check
// that the uses of their 2 predecessors are valid. For loop phis, this function
// is called twice: one for the outside-the-loop input (with {input_if_loop_phi}
// = 0), and once for the inside-the-loop input (with  {input_if_loop_phi} = 1).
bool StringBuilderOptimizer::CheckPreviousNodeUses(Node* child, Status status,
                                                   int input_if_loop_phi) {
  if (IsConcat(child)) {
    return CheckNodeUses(child->InputAt(1), child, status);
  }
  if (child->opcode() == IrOpcode::kPhi) {
    BasicBlock* child_block = schedule()->block(child);
    if (child_block->IsLoopHeader()) {
      return CheckNodeUses(child->InputAt(input_if_loop_phi), child, status);
    } else {
      DCHECK_EQ(child->InputCount(), 3);
      return CheckNodeUses(child->InputAt(0), child, status) &&
             CheckNodeUses(child->InputAt(1), child, status);
    }
  }
  UNREACHABLE();
}

void StringBuilderOptimizer::VisitNode(Node* node, BasicBlock* block) {
  if (IsConcat(node)) {
    Node* lhs = node->InputAt(1);
    Node* rhs = node->InputAt(2);

    if (!IsLiteralString(rhs, broker())) {
      SetStatus(node, State::kInvalid);
      return;
    }

    if (IsLiteralString(lhs, broker())) {
      // This node could start a string builder. However, we won't know until
      // we've properly inspected its uses, found a Phi somewhere down its use
      // chain, made sure that the Phi was valid, etc. Pre-emptively, we do a
      // quick check (with HasConcatOrPhiUse) that this node has a
      // StringConcat/NewConsString in its uses, and if so, we set its state as
      // kBeginConcat, and increment the {string_builder_count_}. The goal of
      // the HasConcatOrPhiUse is mainly to avoid incrementing
      // {string_builder_count_} too often for things that are obviously just
      // regular concatenations of 2 constant strings and that can't be
      // beginning of string builders.
      if (HasConcatOrPhiUse(lhs)) {
        SetStatus(node, State::kBeginStringBuilder, string_builder_count_);
        string_builders_.push_back(
            StringBuilder{node, static_cast<int>(string_builder_count_), false,
                          OneOrTwoByteAnalysis::State::kUnknown});
        string_builder_count_++;
      }
      // A concatenation between 2 literal strings has no predecessor in the
      // string builder, and there is thus no more checks/bookkeeping required
      // ==> early return.
      return;
    } else {
      Status lhs_status = GetStatus(lhs);
      switch (lhs_status.state) {
        case State::kBeginStringBuilder:
        case State::kInStringBuilder:
          SetStatus(node, State::kInStringBuilder, lhs_status.id);
          break;
        case State::kPendingPhi: {
          BasicBlock* phi_block = schedule()->block(lhs);
          if (phi_block->LoopContains(block)) {
            // This node uses a PendingPhi and is inside the loop. We
            // speculatively set it to kInStringBuilder.
            SetStatus(node, State::kInStringBuilder, lhs_status.id);
          } else {
            // This node uses a PendingPhi but is not inside the loop, which
            // means that the PendingPhi was never resolved to a kInConcat or a
            // kInvalid, which means that it's actually not valid (because we
            // visit the graph in RPO order, which means that we've already
            // visited the whole loop). Thus, we set the Phi to kInvalid, and
            // thus, we also set the current node to kInvalid.
            SetStatus(lhs, State::kInvalid);
            SetStatus(node, State::kInvalid);
          }
          break;
        }
        case State::kInvalid:
        case State::kUnvisited:
          SetStatus(node, State::kInvalid);
          break;
        default:
          UNREACHABLE();
      }
    }
  } else if (node->opcode() == IrOpcode::kPhi &&
             PhiInputsAreConcatsOrPhi(node)) {
    if (!block->IsLoopHeader()) {
      // This Phi merges nodes after a if/else.
      int id = GetPhiPredecessorsCommonId(node);
      if (id == kInvalidId) {
        SetStatus(node, State::kInvalid);
      } else {
        SetStatus(node, State::kInStringBuilder, id);
      }
    } else {
      // This Phi merges a value from inside the loop with one from before.
      DCHECK_EQ(node->op()->ValueInputCount(), 2);
      Status first_input_status = GetStatus(node->InputAt(0));
      switch (first_input_status.state) {
        case State::kBeginStringBuilder:
        case State::kInStringBuilder:
          SetStatus(node, State::kPendingPhi, first_input_status.id);
          break;
        case State::kPendingPhi:
        case State::kInvalid:
        case State::kUnvisited:
          SetStatus(node, State::kInvalid);
          break;
        default:
          UNREACHABLE();
      }
    }
  } else {
    SetStatus(node, State::kInvalid);
  }

  Status status = GetStatus(node);
  if (status.state == State::kInStringBuilder ||
      status.state == State::kPendingPhi) {
    // We make sure that this node being in the string builder doesn't conflict
    // with other uses of the previous node of the string builder. Note that
    // loop phis can never have the kInStringBuilder state at this point. We
    // thus check their uses when we finish the loop and set the phi's status to
    // InStringBuilder.
    if (!CheckPreviousNodeUses(node, status, 0)) {
      SetStatus(node, State::kInvalid);
      return;
    }
    // Updating following PendingPhi if needed.
    for (Node* use : node->uses()) {
      if (use->opcode() == IrOpcode::kPhi) {
        Status use_status = GetStatus(use);
        if (use_status.state == State::kPendingPhi) {
          // Finished the loop.
          SetStatus(use, State::kInStringBuilder, status.id);
          if (use_status.id == status.id &&
              CheckPreviousNodeUses(use, status, 1)) {
            string_builders_[status.id].has_loop_phi = true;
          } else {
            // One of the uses of {node} is a pending Phi that hasn't the
            // correct id (is that even possible?), or the uses of {node} are
            // invalid. Either way, both {node} and {use} are invalid.
            SetStatus(node, State::kInvalid);
            SetStatus(use, State::kInvalid);
          }
        }
      }
    }
  }
}

// For each potential string builder, checks that their beginning has status
// kBeginStringBuilder, and that they contain at least one phi. Then, all of
// their "valid" nodes are switched from status State::InStringBuilder to status
// State::kConfirmedInStringBuilder (and "valid" kBeginStringBuilder are left
// as kBeginStringBuilder while invalid ones are switched to kInvalid). Nodes
// are considered "valid" if they are before any kPendingPhi in the string
// builder. Put otherwise, switching status from kInStringBuilder to
// kConfirmedInStringBuilder is a cheap way of getting rid of kInStringBuilder
// nodes that are invalid before one of their predecessor is a kPendingPhi that
// was never switched to kInStringBuilder. An example:
//
//               StringConcat [1]
//             kBeginStringBuilder
//                    |
//                    |
//                    v
//          -----> Loop Phi [2] ---------------
//          |   kInStringBuilder              |
//          |         |                       |
//          |         |                       |
//          |         v                       v
//          |    StringConcat [3]        StringConcat [4]
//          |    kInStringBuilder        kInStringBuilder
//          |         |                       |
//          ----------|                       |
//                                            v
//                                 -----> Loop Phi [5] ------------>
//                                 |      kPendingPhi
//                                 |          |
//                                 |          |
//                                 |          v
//                                 |     StringConcat [6]
//                                 |     kInStringBuilder
//                                 |          |
//                                 -----------|
//
// In this graph, nodes [1], [2], [3] and [4] are part of the string builder. In
// particular, node 2 has at some point been assigned the status kPendingPhi
// (because all loop phis start as kPendingPhi), but was later switched to
// status kInStringBuilder (because its uses inside the loop were compatible
// with the string builder), which implicitly made node [3] a valid part of the
// string builder. On the other hand, node [5] was never switched to status
// kInStringBuilder, which means that it is not valid, and any successor of [5]
// isn't valid either (remember that we speculatively set nodes following a
// kPendingPhi to kInStringBuilder). Thus, rather than having to iterate through
// the successors of kPendingPhi nodes to invalidate them, we simply update the
// status of valid nodes to kConfirmedInStringBuilder, after which any
// kInStringBuilder node is actually invalid.
//
// In this function, we also collect all the possible ends for each string
// builder (their can be multiple possible ends if there is a branch before the
// end of a string builder), as well as where trimming for a given string
// builder should be done (either right after the last node, or at the beginning
// of the blocks following this node). For an example of string builder with
// multiple ends, consider this code:
//
//     let s = "a" + "b"
//     for (...) {
//         s += "...";
//     }
//     if (...) return s + "abc";
//     else return s + "def";
//
// Which would produce a graph that looks like:
//
//                     kStringConcat
//                            |
//                            |
//                            v
//               -------> Loop Phi---------------
//               |            |                 |
//               |            |                 |
//               |            v                 |
//               |      kStringConcat           |
//               |            |                 |
//               -------------|                 |
//                                              |
//                                              |
//                  ------------------------------------------
//                  |                                        |
//                  |                                        |
//                  |                                        |
//                  v                                        v
//            kStringConcat [1]                        kStringConcat [2]
//                  |                                        |
//                  |                                        |
//                  v                                        v
//               Return                                   Return
//
// In this case, both kStringConcat [1] and [2] are valid ends for the string
// builder.
void StringBuilderOptimizer::FinalizeStringBuilders() {
  OneOrTwoByteAnalysis one_or_two_byte_analysis(graph(), temp_zone(), broker());

  // We use {to_visit} to iterate through a string builder, and {ends} to
  // collect its ending. To save some memory, these 2 variables are declared a
  // bit early, and we .clear() them at the beginning of each iteration (which
  // shouldn't free their memory), rather than allocating new memory for each
  // string builder.
  ZoneVector<Node*> to_visit(temp_zone());
  ZoneVector<Node*> ends(temp_zone());

  bool one_string_builder_or_more_valid = false;
  for (unsigned int string_builder_id = 0;
       string_builder_id < string_builder_count_; string_builder_id++) {
    StringBuilder* string_builder = &string_builders_[string_builder_id];
    Node* start = string_builder->start;
    Status start_status = GetStatus(start);
    if (start_status.state != State::kBeginStringBuilder ||
        !string_builder->has_loop_phi) {
      // {start} has already been invalidated, or the string builder doesn't
      // contain a loop Phi.
      *string_builder = kInvalidStringBuilder;
      UpdateStatus(start, State::kInvalid);
      continue;
    }
    DCHECK_EQ(start_status.state, State::kBeginStringBuilder);
    DCHECK_EQ(start_status.id, string_builder_id);
    one_string_builder_or_more_valid = true;

    OneOrTwoByteAnalysis::State one_or_two_byte =
        one_or_two_byte_analysis.OneOrTwoByte(start);

    to_visit.clear();
    ends.clear();

    to_visit.push_back(start);
    while (!to_visit.empty()) {
      Node* curr = to_visit.back();
      to_visit.pop_back();

      Status curr_status = GetStatus(curr);
      if (curr_status.state == State::kConfirmedInStringBuilder) continue;

      DCHECK(curr_status.state == State::kInStringBuilder ||
             curr_status.state == State::kBeginStringBuilder);
      DCHECK_IMPLIES(curr_status.state == State::kBeginStringBuilder,
                     curr == start);
      DCHECK_EQ(curr_status.id, start_status.id);
      if (curr_status.state != State::kBeginStringBuilder) {
        UpdateStatus(curr, State::kConfirmedInStringBuilder);
      }

      if (IsConcat(curr)) {
        one_or_two_byte = OneOrTwoByteAnalysis::ConcatResultIsOneOrTwoByte(
            one_or_two_byte, one_or_two_byte_analysis.OneOrTwoByte(curr));
        // Duplicating string inputs if needed, and marking them as
        // InStringBuilder (so that EffectControlLinearizer doesn't lower them).
        ReplaceConcatInputIfNeeded(curr, 1);
        ReplaceConcatInputIfNeeded(curr, 2);
      }

      // Check if {curr} is one of the string builder's ends: if {curr} has no
      // uses that are part of the string builder, then {curr} ends the string
      // builder.
      bool has_use_in_string_builder = false;
      for (Node* next : curr->uses()) {
        Status next_status = GetStatus(next);
        if ((next_status.state == State::kInStringBuilder ||
             next_status.state == State::kConfirmedInStringBuilder) &&
            next_status.id == curr_status.id) {
          if (next_status.state == State::kInStringBuilder) {
            // We only add to {to_visit} when the state is kInStringBuilder to
            // make sure that we don't revisit already-visited nodes.
            to_visit.push_back(next);
          }
          if (!IsLoopPhi(curr) || !LoopContains(curr, next)) {
            // The condition above is true when:
            //  - {curr} is not a loop phi: in that case, {next} is (one of) the
            //    nodes in the string builder after {curr}.
            //  - {curr} is a loop phi, and {next} is not inside the loop: in
            //    that case, {node} is (one of) the nodes in the string builder
            //    that are after {curr}. Note that we ignore uses of {curr}
            //    inside the loop, since if {curr} has no uses **after** the
            //    loop, then it's (one of) the end of the string builder.
            has_use_in_string_builder = true;
          }
        }
      }
      if (!has_use_in_string_builder) {
        ends.push_back(curr);
      }
    }

    // Note that there is no need to check that the ends have no conflicting
    // uses, because none of the ends can be alive at the same time, and thus,
    // uses of the different ends can't be alive at the same time either. The
    // reason that ens can't be alive at the same time is that if 2 ends were
    // alive at the same time, then there exist a node n that is a predecessors
    // of both ends, and that has 2 successors in the string builder (and alive
    // at the same time), which is not possible because CheckNodeUses prevents
    // it.

    // Collecting next blocks where trimming is required (blocks following a
    // loop Phi where the Phi is the last in a string builder), and setting
    // kEndStringBuilder state to nodes where trimming should be done right
    // after computing the node (when the last node in a string builder is not a
    // loop phi).
    for (Node* end : ends) {
      if (IsLoopPhi(end)) {
        BasicBlock* phi_block = schedule()->block(end);
        for (BasicBlock* block : phi_block->successors()) {
          if (phi_block->LoopContains(block)) continue;
          if (!blocks_to_trimmings_map_[block->id().ToInt()].has_value()) {
            blocks_to_trimmings_map_[block->id().ToInt()] =
                ZoneVector<Node*>(temp_zone());
          }
          blocks_to_trimmings_map_[block->id().ToInt()]->push_back(end);
        }
        UpdateStatus(end, State::kEndStringBuilderLoopPhi);
      } else {
        UpdateStatus(end, State::kEndStringBuilder);
      }
    }

    string_builder->one_or_two_bytes = one_or_two_byte;
  }

#ifdef DEBUG
  if (one_string_builder_or_more_valid) {
    broker()->isolate()->set_has_turbofan_string_builders();
  }
#else
  USE(one_string_builder_or_more_valid);
#endif
}

void StringBuilderOptimizer::VisitGraph() {
  // Initial discovery of the potential string builders.
  for (BasicBlock* block : *schedule()->rpo_order()) {
    // Removing finished loops.
    while (!loop_headers_.empty() &&
           loop_headers_.back()->loop_end() == block) {
      loop_headers_.pop_back();
    }
    // Adding new loop if necessary.
    if (block->IsLoopHeader()) {
      loop_headers_.push_back(block);
    }
    // Visiting block content.
    for (Node* node : *block->nodes()) {
      VisitNode(node, block);
    }
  }

  // Finalize valid string builders (moving valid nodes to status
  // kConfirmedInStringBuilder or kEndStringBuilder), and collecting the
  // trimming points.
  FinalizeStringBuilders();
}

void StringBuilderOptimizer::Run() { VisitGraph(); }

StringBuilderOptimizer::StringBuilderOptimizer(JSGraph* jsgraph,
                                               Schedule* schedule,
                                               Zone* temp_zone,
                                               JSHeapBroker* broker)
    : jsgraph_(jsgraph),
      schedule_(schedule),
      temp_zone_(temp_zone),
      broker_(broker),
      blocks_to_trimmings_map_(schedule->BasicBlockCount(), temp_zone),
      status_(jsgraph->graph()->NodeCount(),
              Status{kInvalidId, State::kUnvisited}, temp_zone),
      string_builders_(temp_zone),
      loop_headers_(temp_zone) {}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
