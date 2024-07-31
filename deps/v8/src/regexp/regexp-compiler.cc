// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-compiler.h"

#include "src/base/safe_conversions.h"
#include "src/execution/isolate.h"
#include "src/objects/fixed-array-inl.h"
#include "src/regexp/regexp-macro-assembler-arch.h"
#include "src/strings/unicode-inl.h"
#include "src/zone/zone-list-inl.h"

#ifdef V8_INTL_SUPPORT
#include "src/regexp/special-case.h"
#include "unicode/locid.h"
#include "unicode/uniset.h"
#include "unicode/utypes.h"
#endif  // V8_INTL_SUPPORT

namespace v8 {
namespace internal {

using namespace regexp_compiler_constants;  // NOLINT(build/namespaces)

// -------------------------------------------------------------------
// Implementation of the Irregexp regular expression engine.
//
// The Irregexp regular expression engine is intended to be a complete
// implementation of ECMAScript regular expressions.  It generates either
// bytecodes or native code.

//   The Irregexp regexp engine is structured in three steps.
//   1) The parser generates an abstract syntax tree.  See ast.cc.
//   2) From the AST a node network is created.  The nodes are all
//      subclasses of RegExpNode.  The nodes represent states when
//      executing a regular expression.  Several optimizations are
//      performed on the node network.
//   3) From the nodes we generate either byte codes or native code
//      that can actually execute the regular expression (perform
//      the search).  The code generation step is described in more
//      detail below.

// Code generation.
//
//   The nodes are divided into four main categories.
//   * Choice nodes
//        These represent places where the regular expression can
//        match in more than one way.  For example on entry to an
//        alternation (foo|bar) or a repetition (*, +, ? or {}).
//   * Action nodes
//        These represent places where some action should be
//        performed.  Examples include recording the current position
//        in the input string to a register (in order to implement
//        captures) or other actions on register for example in order
//        to implement the counters needed for {} repetitions.
//   * Matching nodes
//        These attempt to match some element part of the input string.
//        Examples of elements include character classes, plain strings
//        or back references.
//   * End nodes
//        These are used to implement the actions required on finding
//        a successful match or failing to find a match.
//
//   The code generated (whether as byte codes or native code) maintains
//   some state as it runs.  This consists of the following elements:
//
//   * The capture registers.  Used for string captures.
//   * Other registers.  Used for counters etc.
//   * The current position.
//   * The stack of backtracking information.  Used when a matching node
//     fails to find a match and needs to try an alternative.
//
// Conceptual regular expression execution model:
//
//   There is a simple conceptual model of regular expression execution
//   which will be presented first.  The actual code generated is a more
//   efficient simulation of the simple conceptual model:
//
//   * Choice nodes are implemented as follows:
//     For each choice except the last {
//       push current position
//       push backtrack code location
//       <generate code to test for choice>
//       backtrack code location:
//       pop current position
//     }
//     <generate code to test for last choice>
//
//   * Actions nodes are generated as follows
//     <push affected registers on backtrack stack>
//     <generate code to perform action>
//     push backtrack code location
//     <generate code to test for following nodes>
//     backtrack code location:
//     <pop affected registers to restore their state>
//     <pop backtrack location from stack and go to it>
//
//   * Matching nodes are generated as follows:
//     if input string matches at current position
//       update current position
//       <generate code to test for following nodes>
//     else
//       <pop backtrack location from stack and go to it>
//
//   Thus it can be seen that the current position is saved and restored
//   by the choice nodes, whereas the registers are saved and restored by
//   by the action nodes that manipulate them.
//
//   The other interesting aspect of this model is that nodes are generated
//   at the point where they are needed by a recursive call to Emit().  If
//   the node has already been code generated then the Emit() call will
//   generate a jump to the previously generated code instead.  In order to
//   limit recursion it is possible for the Emit() function to put the node
//   on a work list for later generation and instead generate a jump.  The
//   destination of the jump is resolved later when the code is generated.
//
// Actual regular expression code generation.
//
//   Code generation is actually more complicated than the above.  In order to
//   improve the efficiency of the generated code some optimizations are
//   performed
//
//   * Choice nodes have 1-character lookahead.
//     A choice node looks at the following character and eliminates some of
//     the choices immediately based on that character.  This is not yet
//     implemented.
//   * Simple greedy loops store reduced backtracking information.
//     A quantifier like /.*foo/m will greedily match the whole input.  It will
//     then need to backtrack to a point where it can match "foo".  The naive
//     implementation of this would push each character position onto the
//     backtracking stack, then pop them off one by one.  This would use space
//     proportional to the length of the input string.  However since the "."
//     can only match in one way and always has a constant length (in this case
//     of 1) it suffices to store the current position on the top of the stack
//     once.  Matching now becomes merely incrementing the current position and
//     backtracking becomes decrementing the current position and checking the
//     result against the stored current position.  This is faster and saves
//     space.
//   * The current state is virtualized.
//     This is used to defer expensive operations until it is clear that they
//     are needed and to generate code for a node more than once, allowing
//     specialized an efficient versions of the code to be created. This is
//     explained in the section below.
//
// Execution state virtualization.
//
//   Instead of emitting code, nodes that manipulate the state can record their
//   manipulation in an object called the Trace.  The Trace object can record a
//   current position offset, an optional backtrack code location on the top of
//   the virtualized backtrack stack and some register changes.  When a node is
//   to be emitted it can flush the Trace or update it.  Flushing the Trace
//   will emit code to bring the actual state into line with the virtual state.
//   Avoiding flushing the state can postpone some work (e.g. updates of capture
//   registers).  Postponing work can save time when executing the regular
//   expression since it may be found that the work never has to be done as a
//   failure to match can occur.  In addition it is much faster to jump to a
//   known backtrack code location than it is to pop an unknown backtrack
//   location from the stack and jump there.
//
//   The virtual state found in the Trace affects code generation.  For example
//   the virtual state contains the difference between the actual current
//   position and the virtual current position, and matching code needs to use
//   this offset to attempt a match in the correct location of the input
//   string.  Therefore code generated for a non-trivial trace is specialized
//   to that trace.  The code generator therefore has the ability to generate
//   code for each node several times.  In order to limit the size of the
//   generated code there is an arbitrary limit on how many specialized sets of
//   code may be generated for a given node.  If the limit is reached, the
//   trace is flushed and a generic version of the code for a node is emitted.
//   This is subsequently used for that node.  The code emitted for non-generic
//   trace is not recorded in the node and so it cannot currently be reused in
//   the event that code generation is requested for an identical trace.

namespace {

constexpr base::uc32 MaxCodeUnit(const bool one_byte) {
  static_assert(String::kMaxOneByteCharCodeU <=
                std::numeric_limits<uint16_t>::max());
  static_assert(String::kMaxUtf16CodeUnitU <=
                std::numeric_limits<uint16_t>::max());
  return one_byte ? String::kMaxOneByteCharCodeU : String::kMaxUtf16CodeUnitU;
}

constexpr uint32_t CharMask(const bool one_byte) {
  static_assert(base::bits::IsPowerOfTwo(String::kMaxOneByteCharCodeU + 1));
  static_assert(base::bits::IsPowerOfTwo(String::kMaxUtf16CodeUnitU + 1));
  return MaxCodeUnit(one_byte);
}

}  // namespace

void RegExpTree::AppendToText(RegExpText* text, Zone* zone) { UNREACHABLE(); }

void RegExpAtom::AppendToText(RegExpText* text, Zone* zone) {
  text->AddElement(TextElement::Atom(this), zone);
}

void RegExpClassRanges::AppendToText(RegExpText* text, Zone* zone) {
  text->AddElement(TextElement::ClassRanges(this), zone);
}

void RegExpText::AppendToText(RegExpText* text, Zone* zone) {
  for (int i = 0; i < elements()->length(); i++)
    text->AddElement(elements()->at(i), zone);
}

TextElement TextElement::Atom(RegExpAtom* atom) {
  return TextElement(ATOM, atom);
}

TextElement TextElement::ClassRanges(RegExpClassRanges* class_ranges) {
  return TextElement(CLASS_RANGES, class_ranges);
}

int TextElement::length() const {
  switch (text_type()) {
    case ATOM:
      return atom()->length();

    case CLASS_RANGES:
      return 1;
  }
  UNREACHABLE();
}

class RecursionCheck {
 public:
  explicit RecursionCheck(RegExpCompiler* compiler) : compiler_(compiler) {
    compiler->IncrementRecursionDepth();
  }
  ~RecursionCheck() { compiler_->DecrementRecursionDepth(); }

 private:
  RegExpCompiler* compiler_;
};

// Attempts to compile the regexp using an Irregexp code generator.  Returns
// a fixed array or a null handle depending on whether it succeeded.
RegExpCompiler::RegExpCompiler(Isolate* isolate, Zone* zone, int capture_count,
                               RegExpFlags flags, bool one_byte)
    : next_register_(JSRegExp::RegistersForCaptureCount(capture_count)),
      unicode_lookaround_stack_register_(kNoRegister),
      unicode_lookaround_position_register_(kNoRegister),
      work_list_(nullptr),
      recursion_depth_(0),
      flags_(flags),
      one_byte_(one_byte),
      reg_exp_too_big_(false),
      limiting_recursion_(false),
      optimize_(v8_flags.regexp_optimization),
      read_backward_(false),
      current_expansion_factor_(1),
      frequency_collator_(),
      isolate_(isolate),
      zone_(zone) {
  accept_ = zone->New<EndNode>(EndNode::ACCEPT, zone);
  DCHECK_GE(RegExpMacroAssembler::kMaxRegister, next_register_ - 1);
}

RegExpCompiler::CompilationResult RegExpCompiler::Assemble(
    Isolate* isolate, RegExpMacroAssembler* macro_assembler, RegExpNode* start,
    int capture_count, Handle<String> pattern) {
  macro_assembler_ = macro_assembler;

  ZoneVector<RegExpNode*> work_list(zone());
  work_list_ = &work_list;
  Label fail;
  macro_assembler_->PushBacktrack(&fail);
  Trace new_trace;
  start->Emit(this, &new_trace);
  macro_assembler_->BindJumpTarget(&fail);
  macro_assembler_->Fail();
  while (!work_list.empty()) {
    RegExpNode* node = work_list.back();
    work_list.pop_back();
    node->set_on_work_list(false);
    if (!node->label()->is_bound()) node->Emit(this, &new_trace);
  }
  if (reg_exp_too_big_) {
    if (v8_flags.correctness_fuzzer_suppressions) {
      FATAL("Aborting on excess zone allocation");
    }
    macro_assembler_->AbortedCodeGeneration();
    return CompilationResult::RegExpTooBig();
  }

  Handle<HeapObject> code = macro_assembler_->GetCode(pattern);
  isolate->IncreaseTotalRegexpCodeGenerated(code);
  work_list_ = nullptr;

  return {code, next_register_};
}

bool Trace::DeferredAction::Mentions(int that) {
  if (action_type() == ActionNode::CLEAR_CAPTURES) {
    Interval range = static_cast<DeferredClearCaptures*>(this)->range();
    return range.Contains(that);
  } else {
    return reg() == that;
  }
}

bool Trace::mentions_reg(int reg) {
  for (DeferredAction* action = actions_; action != nullptr;
       action = action->next()) {
    if (action->Mentions(reg)) return true;
  }
  return false;
}

bool Trace::GetStoredPosition(int reg, int* cp_offset) {
  DCHECK_EQ(0, *cp_offset);
  for (DeferredAction* action = actions_; action != nullptr;
       action = action->next()) {
    if (action->Mentions(reg)) {
      if (action->action_type() == ActionNode::STORE_POSITION) {
        *cp_offset = static_cast<DeferredCapture*>(action)->cp_offset();
        return true;
      } else {
        return false;
      }
    }
  }
  return false;
}

// A (dynamically-sized) set of unsigned integers that behaves especially well
// on small integers (< kFirstLimit). May do zone-allocation.
class DynamicBitSet : public ZoneObject {
 public:
  V8_EXPORT_PRIVATE bool Get(unsigned value) const {
    if (value < kFirstLimit) {
      return (first_ & (1 << value)) != 0;
    } else if (remaining_ == nullptr) {
      return false;
    } else {
      return remaining_->Contains(value);
    }
  }

  // Destructively set a value in this set.
  void Set(unsigned value, Zone* zone) {
    if (value < kFirstLimit) {
      first_ |= (1 << value);
    } else {
      if (remaining_ == nullptr)
        remaining_ = zone->New<ZoneList<unsigned>>(1, zone);
      if (remaining_->is_empty() || !remaining_->Contains(value))
        remaining_->Add(value, zone);
    }
  }

 private:
  static constexpr unsigned kFirstLimit = 32;

  uint32_t first_ = 0;
  ZoneList<unsigned>* remaining_ = nullptr;
};

int Trace::FindAffectedRegisters(DynamicBitSet* affected_registers,
                                 Zone* zone) {
  int max_register = RegExpCompiler::kNoRegister;
  for (DeferredAction* action = actions_; action != nullptr;
       action = action->next()) {
    if (action->action_type() == ActionNode::CLEAR_CAPTURES) {
      Interval range = static_cast<DeferredClearCaptures*>(action)->range();
      for (int i = range.from(); i <= range.to(); i++)
        affected_registers->Set(i, zone);
      if (range.to() > max_register) max_register = range.to();
    } else {
      affected_registers->Set(action->reg(), zone);
      if (action->reg() > max_register) max_register = action->reg();
    }
  }
  return max_register;
}

void Trace::RestoreAffectedRegisters(RegExpMacroAssembler* assembler,
                                     int max_register,
                                     const DynamicBitSet& registers_to_pop,
                                     const DynamicBitSet& registers_to_clear) {
  for (int reg = max_register; reg >= 0; reg--) {
    if (registers_to_pop.Get(reg)) {
      assembler->PopRegister(reg);
    } else if (registers_to_clear.Get(reg)) {
      int clear_to = reg;
      while (reg > 0 && registers_to_clear.Get(reg - 1)) {
        reg--;
      }
      assembler->ClearRegisters(reg, clear_to);
    }
  }
}

void Trace::PerformDeferredActions(RegExpMacroAssembler* assembler,
                                   int max_register,
                                   const DynamicBitSet& affected_registers,
                                   DynamicBitSet* registers_to_pop,
                                   DynamicBitSet* registers_to_clear,
                                   Zone* zone) {
  // The "+1" is to avoid a push_limit of zero if stack_limit_slack() is 1.
  const int push_limit = (assembler->stack_limit_slack() + 1) / 2;

  // Count pushes performed to force a stack limit check occasionally.
  int pushes = 0;

  for (int reg = 0; reg <= max_register; reg++) {
    if (!affected_registers.Get(reg)) continue;

    // The chronologically first deferred action in the trace
    // is used to infer the action needed to restore a register
    // to its previous state (or not, if it's safe to ignore it).
    enum DeferredActionUndoType { IGNORE, RESTORE, CLEAR };
    DeferredActionUndoType undo_action = IGNORE;

    int value = 0;
    bool absolute = false;
    bool clear = false;
    static const int kNoStore = kMinInt;
    int store_position = kNoStore;
    // This is a little tricky because we are scanning the actions in reverse
    // historical order (newest first).
    for (DeferredAction* action = actions_; action != nullptr;
         action = action->next()) {
      if (action->Mentions(reg)) {
        switch (action->action_type()) {
          case ActionNode::SET_REGISTER_FOR_LOOP: {
            Trace::DeferredSetRegisterForLoop* psr =
                static_cast<Trace::DeferredSetRegisterForLoop*>(action);
            if (!absolute) {
              value += psr->value();
              absolute = true;
            }
            // SET_REGISTER_FOR_LOOP is only used for newly introduced loop
            // counters. They can have a significant previous value if they
            // occur in a loop. TODO(lrn): Propagate this information, so
            // we can set undo_action to IGNORE if we know there is no value to
            // restore.
            undo_action = RESTORE;
            DCHECK_EQ(store_position, kNoStore);
            DCHECK(!clear);
            break;
          }
          case ActionNode::INCREMENT_REGISTER:
            if (!absolute) {
              value++;
            }
            DCHECK_EQ(store_position, kNoStore);
            DCHECK(!clear);
            undo_action = RESTORE;
            break;
          case ActionNode::STORE_POSITION: {
            Trace::DeferredCapture* pc =
                static_cast<Trace::DeferredCapture*>(action);
            if (!clear && store_position == kNoStore) {
              store_position = pc->cp_offset();
            }

            // For captures we know that stores and clears alternate.
            // Other register, are never cleared, and if the occur
            // inside a loop, they might be assigned more than once.
            if (reg <= 1) {
              // Registers zero and one, aka "capture zero", is
              // always set correctly if we succeed. There is no
              // need to undo a setting on backtrack, because we
              // will set it again or fail.
              undo_action = IGNORE;
            } else {
              undo_action = pc->is_capture() ? CLEAR : RESTORE;
            }
            DCHECK(!absolute);
            DCHECK_EQ(value, 0);
            break;
          }
          case ActionNode::CLEAR_CAPTURES: {
            // Since we're scanning in reverse order, if we've already
            // set the position we have to ignore historically earlier
            // clearing operations.
            if (store_position == kNoStore) {
              clear = true;
            }
            undo_action = RESTORE;
            DCHECK(!absolute);
            DCHECK_EQ(value, 0);
            break;
          }
          default:
            UNREACHABLE();
        }
      }
    }
    // Prepare for the undo-action (e.g., push if it's going to be popped).
    if (undo_action == RESTORE) {
      pushes++;
      RegExpMacroAssembler::StackCheckFlag stack_check =
          RegExpMacroAssembler::kNoStackLimitCheck;
      if (pushes == push_limit) {
        stack_check = RegExpMacroAssembler::kCheckStackLimit;
        pushes = 0;
      }

      assembler->PushRegister(reg, stack_check);
      registers_to_pop->Set(reg, zone);
    } else if (undo_action == CLEAR) {
      registers_to_clear->Set(reg, zone);
    }
    // Perform the chronologically last action (or accumulated increment)
    // for the register.
    if (store_position != kNoStore) {
      assembler->WriteCurrentPositionToRegister(reg, store_position);
    } else if (clear) {
      assembler->ClearRegisters(reg, reg);
    } else if (absolute) {
      assembler->SetRegister(reg, value);
    } else if (value != 0) {
      assembler->AdvanceRegister(reg, value);
    }
  }
}

// This is called as we come into a loop choice node and some other tricky
// nodes.  It normalizes the state of the code generator to ensure we can
// generate generic code.
void Trace::Flush(RegExpCompiler* compiler, RegExpNode* successor) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();

  DCHECK(!is_trivial());

  if (actions_ == nullptr && backtrack() == nullptr) {
    // Here we just have some deferred cp advances to fix and we are back to
    // a normal situation.  We may also have to forget some information gained
    // through a quick check that was already performed.
    if (cp_offset_ != 0) assembler->AdvanceCurrentPosition(cp_offset_);
    // Create a new trivial state and generate the node with that.
    Trace new_state;
    successor->Emit(compiler, &new_state);
    return;
  }

  // Generate deferred actions here along with code to undo them again.
  DynamicBitSet affected_registers;

  if (backtrack() != nullptr) {
    // Here we have a concrete backtrack location.  These are set up by choice
    // nodes and so they indicate that we have a deferred save of the current
    // position which we may need to emit here.
    assembler->PushCurrentPosition();
  }

  int max_register =
      FindAffectedRegisters(&affected_registers, compiler->zone());
  DynamicBitSet registers_to_pop;
  DynamicBitSet registers_to_clear;
  PerformDeferredActions(assembler, max_register, affected_registers,
                         &registers_to_pop, &registers_to_clear,
                         compiler->zone());
  if (cp_offset_ != 0) {
    assembler->AdvanceCurrentPosition(cp_offset_);
  }

  // Create a new trivial state and generate the node with that.
  Label undo;
  assembler->PushBacktrack(&undo);
  if (successor->KeepRecursing(compiler)) {
    Trace new_state;
    successor->Emit(compiler, &new_state);
  } else {
    compiler->AddWork(successor);
    assembler->GoTo(successor->label());
  }

  // On backtrack we need to restore state.
  assembler->BindJumpTarget(&undo);
  RestoreAffectedRegisters(assembler, max_register, registers_to_pop,
                           registers_to_clear);
  if (backtrack() == nullptr) {
    assembler->Backtrack();
  } else {
    assembler->PopCurrentPosition();
    assembler->GoTo(backtrack());
  }
}

void NegativeSubmatchSuccess::Emit(RegExpCompiler* compiler, Trace* trace) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();

  // Omit flushing the trace. We discard the entire stack frame anyway.

  if (!label()->is_bound()) {
    // We are completely independent of the trace, since we ignore it,
    // so this code can be used as the generic version.
    assembler->Bind(label());
  }

  // Throw away everything on the backtrack stack since the start
  // of the negative submatch and restore the character position.
  assembler->ReadCurrentPositionFromRegister(current_position_register_);
  assembler->ReadStackPointerFromRegister(stack_pointer_register_);
  if (clear_capture_count_ > 0) {
    // Clear any captures that might have been performed during the success
    // of the body of the negative look-ahead.
    int clear_capture_end = clear_capture_start_ + clear_capture_count_ - 1;
    assembler->ClearRegisters(clear_capture_start_, clear_capture_end);
  }
  // Now that we have unwound the stack we find at the top of the stack the
  // backtrack that the BeginNegativeSubmatch node got.
  assembler->Backtrack();
}

void EndNode::Emit(RegExpCompiler* compiler, Trace* trace) {
  if (!trace->is_trivial()) {
    trace->Flush(compiler, this);
    return;
  }
  RegExpMacroAssembler* assembler = compiler->macro_assembler();
  if (!label()->is_bound()) {
    assembler->Bind(label());
  }
  switch (action_) {
    case ACCEPT:
      assembler->Succeed();
      return;
    case BACKTRACK:
      assembler->GoTo(trace->backtrack());
      return;
    case NEGATIVE_SUBMATCH_SUCCESS:
      // This case is handled in a different virtual method.
      UNREACHABLE();
  }
  UNIMPLEMENTED();
}

void GuardedAlternative::AddGuard(Guard* guard, Zone* zone) {
  if (guards_ == nullptr) guards_ = zone->New<ZoneList<Guard*>>(1, zone);
  guards_->Add(guard, zone);
}

ActionNode* ActionNode::SetRegisterForLoop(int reg, int val,
                                           RegExpNode* on_success) {
  ActionNode* result =
      on_success->zone()->New<ActionNode>(SET_REGISTER_FOR_LOOP, on_success);
  result->data_.u_store_register.reg = reg;
  result->data_.u_store_register.value = val;
  return result;
}

ActionNode* ActionNode::IncrementRegister(int reg, RegExpNode* on_success) {
  ActionNode* result =
      on_success->zone()->New<ActionNode>(INCREMENT_REGISTER, on_success);
  result->data_.u_increment_register.reg = reg;
  return result;
}

ActionNode* ActionNode::StorePosition(int reg, bool is_capture,
                                      RegExpNode* on_success) {
  ActionNode* result =
      on_success->zone()->New<ActionNode>(STORE_POSITION, on_success);
  result->data_.u_position_register.reg = reg;
  result->data_.u_position_register.is_capture = is_capture;
  return result;
}

ActionNode* ActionNode::ClearCaptures(Interval range, RegExpNode* on_success) {
  ActionNode* result =
      on_success->zone()->New<ActionNode>(CLEAR_CAPTURES, on_success);
  result->data_.u_clear_captures.range_from = range.from();
  result->data_.u_clear_captures.range_to = range.to();
  return result;
}

ActionNode* ActionNode::BeginPositiveSubmatch(int stack_reg, int position_reg,
                                              RegExpNode* on_success) {
  ActionNode* result =
      on_success->zone()->New<ActionNode>(BEGIN_POSITIVE_SUBMATCH, on_success);
  result->data_.u_submatch.stack_pointer_register = stack_reg;
  result->data_.u_submatch.current_position_register = position_reg;
  return result;
}

ActionNode* ActionNode::BeginNegativeSubmatch(int stack_reg, int position_reg,
                                              RegExpNode* on_success) {
  ActionNode* result =
      on_success->zone()->New<ActionNode>(BEGIN_NEGATIVE_SUBMATCH, on_success);
  result->data_.u_submatch.stack_pointer_register = stack_reg;
  result->data_.u_submatch.current_position_register = position_reg;
  return result;
}

ActionNode* ActionNode::PositiveSubmatchSuccess(int stack_reg, int position_reg,
                                                int clear_register_count,
                                                int clear_register_from,
                                                RegExpNode* on_success) {
  ActionNode* result = on_success->zone()->New<ActionNode>(
      POSITIVE_SUBMATCH_SUCCESS, on_success);
  result->data_.u_submatch.stack_pointer_register = stack_reg;
  result->data_.u_submatch.current_position_register = position_reg;
  result->data_.u_submatch.clear_register_count = clear_register_count;
  result->data_.u_submatch.clear_register_from = clear_register_from;
  return result;
}

ActionNode* ActionNode::EmptyMatchCheck(int start_register,
                                        int repetition_register,
                                        int repetition_limit,
                                        RegExpNode* on_success) {
  ActionNode* result =
      on_success->zone()->New<ActionNode>(EMPTY_MATCH_CHECK, on_success);
  result->data_.u_empty_match_check.start_register = start_register;
  result->data_.u_empty_match_check.repetition_register = repetition_register;
  result->data_.u_empty_match_check.repetition_limit = repetition_limit;
  return result;
}

ActionNode* ActionNode::ModifyFlags(RegExpFlags flags, RegExpNode* on_success) {
  ActionNode* result =
      on_success->zone()->New<ActionNode>(MODIFY_FLAGS, on_success);
  result->data_.u_modify_flags.flags = flags;
  return result;
}

#define DEFINE_ACCEPT(Type) \
  void Type##Node::Accept(NodeVisitor* visitor) { visitor->Visit##Type(this); }
FOR_EACH_NODE_TYPE(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

// -------------------------------------------------------------------
// Emit code.

void ChoiceNode::GenerateGuard(RegExpMacroAssembler* macro_assembler,
                               Guard* guard, Trace* trace) {
  switch (guard->op()) {
    case Guard::LT:
      DCHECK(!trace->mentions_reg(guard->reg()));
      macro_assembler->IfRegisterGE(guard->reg(), guard->value(),
                                    trace->backtrack());
      break;
    case Guard::GEQ:
      DCHECK(!trace->mentions_reg(guard->reg()));
      macro_assembler->IfRegisterLT(guard->reg(), guard->value(),
                                    trace->backtrack());
      break;
  }
}

namespace {

#ifdef DEBUG
bool ContainsOnlyUtf16CodeUnits(unibrow::uchar* chars, int length) {
  static_assert(sizeof(unibrow::uchar) == 4);
  for (int i = 0; i < length; i++) {
    if (chars[i] > String::kMaxUtf16CodeUnit) return false;
  }
  return true;
}
#endif  // DEBUG

// Returns the number of characters in the equivalence class, omitting those
// that cannot occur in the source string because it is Latin1.
int GetCaseIndependentLetters(Isolate* isolate, base::uc16 character,
                              bool one_byte_subject, unibrow::uchar* letters,
                              int letter_length) {
#ifdef V8_INTL_SUPPORT
  if (RegExpCaseFolding::IgnoreSet().contains(character)) {
    letters[0] = character;
    DCHECK(ContainsOnlyUtf16CodeUnits(letters, 1));
    return 1;
  }
  bool in_special_add_set =
      RegExpCaseFolding::SpecialAddSet().contains(character);

  icu::UnicodeSet set;
  set.add(character);
  set = set.closeOver(USET_CASE_INSENSITIVE);

  UChar32 canon = 0;
  if (in_special_add_set) {
    canon = RegExpCaseFolding::Canonicalize(character);
  }

  int32_t range_count = set.getRangeCount();
  int items = 0;
  for (int32_t i = 0; i < range_count; i++) {
    UChar32 start = set.getRangeStart(i);
    UChar32 end = set.getRangeEnd(i);
    CHECK(end - start + items <= letter_length);
    for (UChar32 cu = start; cu <= end; cu++) {
      if (one_byte_subject && cu > String::kMaxOneByteCharCode) break;
      if (in_special_add_set && RegExpCaseFolding::Canonicalize(cu) != canon) {
        continue;
      }
      letters[items++] = static_cast<unibrow::uchar>(cu);
    }
  }
  DCHECK(ContainsOnlyUtf16CodeUnits(letters, items));
  return items;
#else
  int length =
      isolate->jsregexp_uncanonicalize()->get(character, '\0', letters);
  // Unibrow returns 0 or 1 for characters where case independence is
  // trivial.
  if (length == 0) {
    letters[0] = character;
    length = 1;
  }

  if (one_byte_subject) {
    int new_length = 0;
    for (int i = 0; i < length; i++) {
      if (letters[i] <= String::kMaxOneByteCharCode) {
        letters[new_length++] = letters[i];
      }
    }
    length = new_length;
  }

  DCHECK(ContainsOnlyUtf16CodeUnits(letters, length));
  return length;
#endif  // V8_INTL_SUPPORT
}

inline bool EmitSimpleCharacter(Isolate* isolate, RegExpCompiler* compiler,
                                base::uc16 c, Label* on_failure, int cp_offset,
                                bool check, bool preloaded) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();
  bool bound_checked = false;
  if (!preloaded) {
    assembler->LoadCurrentCharacter(cp_offset, on_failure, check);
    bound_checked = true;
  }
  assembler->CheckNotCharacter(c, on_failure);
  return bound_checked;
}

// Only emits non-letters (things that don't have case).  Only used for case
// independent matches.
inline bool EmitAtomNonLetter(Isolate* isolate, RegExpCompiler* compiler,
                              base::uc16 c, Label* on_failure, int cp_offset,
                              bool check, bool preloaded) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  bool one_byte = compiler->one_byte();
  unibrow::uchar chars[4];
  int length = GetCaseIndependentLetters(isolate, c, one_byte, chars, 4);
  if (length < 1) {
    // This can't match.  Must be an one-byte subject and a non-one-byte
    // character.  We do not need to do anything since the one-byte pass
    // already handled this.
    return false;  // Bounds not checked.
  }
  bool checked = false;
  // We handle the length > 1 case in a later pass.
  if (length == 1) {
    if (one_byte && c > String::kMaxOneByteCharCodeU) {
      // Can't match - see above.
      return false;  // Bounds not checked.
    }
    if (!preloaded) {
      macro_assembler->LoadCurrentCharacter(cp_offset, on_failure, check);
      checked = check;
    }
    macro_assembler->CheckNotCharacter(c, on_failure);
  }
  return checked;
}

bool ShortCutEmitCharacterPair(RegExpMacroAssembler* macro_assembler,
                               bool one_byte, base::uc16 c1, base::uc16 c2,
                               Label* on_failure) {
  const uint32_t char_mask = CharMask(one_byte);
  base::uc16 exor = c1 ^ c2;
  // Check whether exor has only one bit set.
  if (((exor - 1) & exor) == 0) {
    // If c1 and c2 differ only by one bit.
    // Ecma262UnCanonicalize always gives the highest number last.
    DCHECK(c2 > c1);
    base::uc16 mask = char_mask ^ exor;
    macro_assembler->CheckNotCharacterAfterAnd(c1, mask, on_failure);
    return true;
  }
  DCHECK(c2 > c1);
  base::uc16 diff = c2 - c1;
  if (((diff - 1) & diff) == 0 && c1 >= diff) {
    // If the characters differ by 2^n but don't differ by one bit then
    // subtract the difference from the found character, then do the or
    // trick.  We avoid the theoretical case where negative numbers are
    // involved in order to simplify code generation.
    base::uc16 mask = char_mask ^ diff;
    macro_assembler->CheckNotCharacterAfterMinusAnd(c1 - diff, diff, mask,
                                                    on_failure);
    return true;
  }
  return false;
}

// Only emits letters (things that have case).  Only used for case independent
// matches.
inline bool EmitAtomLetter(Isolate* isolate, RegExpCompiler* compiler,
                           base::uc16 c, Label* on_failure, int cp_offset,
                           bool check, bool preloaded) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  bool one_byte = compiler->one_byte();
  unibrow::uchar chars[4];
  int length = GetCaseIndependentLetters(isolate, c, one_byte, chars, 4);
  if (length <= 1) return false;
  // We may not need to check against the end of the input string
  // if this character lies before a character that matched.
  if (!preloaded) {
    macro_assembler->LoadCurrentCharacter(cp_offset, on_failure, check);
  }
  Label ok;
  switch (length) {
    case 2: {
      if (ShortCutEmitCharacterPair(macro_assembler, one_byte, chars[0],
                                    chars[1], on_failure)) {
      } else {
        macro_assembler->CheckCharacter(chars[0], &ok);
        macro_assembler->CheckNotCharacter(chars[1], on_failure);
        macro_assembler->Bind(&ok);
      }
      break;
    }
    case 4:
      macro_assembler->CheckCharacter(chars[3], &ok);
      [[fallthrough]];
    case 3:
      macro_assembler->CheckCharacter(chars[0], &ok);
      macro_assembler->CheckCharacter(chars[1], &ok);
      macro_assembler->CheckNotCharacter(chars[2], on_failure);
      macro_assembler->Bind(&ok);
      break;
    default:
      UNREACHABLE();
  }
  return true;
}

void EmitBoundaryTest(RegExpMacroAssembler* masm, int border,
                      Label* fall_through, Label* above_or_equal,
                      Label* below) {
  if (below != fall_through) {
    masm->CheckCharacterLT(border, below);
    if (above_or_equal != fall_through) masm->GoTo(above_or_equal);
  } else {
    masm->CheckCharacterGT(border - 1, above_or_equal);
  }
}

void EmitDoubleBoundaryTest(RegExpMacroAssembler* masm, int first, int last,
                            Label* fall_through, Label* in_range,
                            Label* out_of_range) {
  if (in_range == fall_through) {
    if (first == last) {
      masm->CheckNotCharacter(first, out_of_range);
    } else {
      masm->CheckCharacterNotInRange(first, last, out_of_range);
    }
  } else {
    if (first == last) {
      masm->CheckCharacter(first, in_range);
    } else {
      masm->CheckCharacterInRange(first, last, in_range);
    }
    if (out_of_range != fall_through) masm->GoTo(out_of_range);
  }
}

// even_label is for ranges[i] to ranges[i + 1] where i - start_index is even.
// odd_label is for ranges[i] to ranges[i + 1] where i - start_index is odd.
void EmitUseLookupTable(RegExpMacroAssembler* masm,
                        ZoneList<base::uc32>* ranges, uint32_t start_index,
                        uint32_t end_index, base::uc32 min_char,
                        Label* fall_through, Label* even_label,
                        Label* odd_label) {
  static const uint32_t kSize = RegExpMacroAssembler::kTableSize;
  static const uint32_t kMask = RegExpMacroAssembler::kTableMask;

  base::uc32 base = (min_char & ~kMask);
  USE(base);

  // Assert that everything is on one kTableSize page.
  for (uint32_t i = start_index; i <= end_index; i++) {
    DCHECK_EQ(ranges->at(i) & ~kMask, base);
  }
  DCHECK(start_index == 0 || (ranges->at(start_index - 1) & ~kMask) <= base);

  char templ[kSize];
  Label* on_bit_set;
  Label* on_bit_clear;
  int bit;
  if (even_label == fall_through) {
    on_bit_set = odd_label;
    on_bit_clear = even_label;
    bit = 1;
  } else {
    on_bit_set = even_label;
    on_bit_clear = odd_label;
    bit = 0;
  }
  for (uint32_t i = 0; i < (ranges->at(start_index) & kMask) && i < kSize;
       i++) {
    templ[i] = bit;
  }
  uint32_t j = 0;
  bit ^= 1;
  for (uint32_t i = start_index; i < end_index; i++) {
    for (j = (ranges->at(i) & kMask); j < (ranges->at(i + 1) & kMask); j++) {
      templ[j] = bit;
    }
    bit ^= 1;
  }
  for (uint32_t i = j; i < kSize; i++) {
    templ[i] = bit;
  }
  Factory* factory = masm->isolate()->factory();
  // TODO(erikcorry): Cache these.
  Handle<ByteArray> ba = factory->NewByteArray(kSize, AllocationType::kOld);
  for (uint32_t i = 0; i < kSize; i++) {
    ba->set(i, templ[i]);
  }
  masm->CheckBitInTable(ba, on_bit_set);
  if (on_bit_clear != fall_through) masm->GoTo(on_bit_clear);
}

void CutOutRange(RegExpMacroAssembler* masm, ZoneList<base::uc32>* ranges,
                 uint32_t start_index, uint32_t end_index, uint32_t cut_index,
                 Label* even_label, Label* odd_label) {
  bool odd = (((cut_index - start_index) & 1) == 1);
  Label* in_range_label = odd ? odd_label : even_label;
  Label dummy;
  EmitDoubleBoundaryTest(masm, ranges->at(cut_index),
                         ranges->at(cut_index + 1) - 1, &dummy, in_range_label,
                         &dummy);
  DCHECK(!dummy.is_linked());
  // Cut out the single range by rewriting the array.  This creates a new
  // range that is a merger of the two ranges on either side of the one we
  // are cutting out.  The oddity of the labels is preserved.
  for (uint32_t j = cut_index; j > start_index; j--) {
    ranges->at(j) = ranges->at(j - 1);
  }
  for (uint32_t j = cut_index + 1; j < end_index; j++) {
    ranges->at(j) = ranges->at(j + 1);
  }
}

// Unicode case.  Split the search space into kSize spaces that are handled
// with recursion.
void SplitSearchSpace(ZoneList<base::uc32>* ranges, uint32_t start_index,
                      uint32_t end_index, uint32_t* new_start_index,
                      uint32_t* new_end_index, base::uc32* border) {
  static const uint32_t kSize = RegExpMacroAssembler::kTableSize;
  static const uint32_t kMask = RegExpMacroAssembler::kTableMask;

  base::uc32 first = ranges->at(start_index);
  base::uc32 last = ranges->at(end_index) - 1;

  *new_start_index = start_index;
  *border = (ranges->at(start_index) & ~kMask) + kSize;
  while (*new_start_index < end_index) {
    if (ranges->at(*new_start_index) > *border) break;
    (*new_start_index)++;
  }
  // new_start_index is the index of the first edge that is beyond the
  // current kSize space.

  // For very large search spaces we do a binary chop search of the non-Latin1
  // space instead of just going to the end of the current kSize space.  The
  // heuristics are complicated a little by the fact that any 128-character
  // encoding space can be quickly tested with a table lookup, so we don't
  // wish to do binary chop search at a smaller granularity than that.  A
  // 128-character space can take up a lot of space in the ranges array if,
  // for example, we only want to match every second character (eg. the lower
  // case characters on some Unicode pages).
  uint32_t binary_chop_index = (end_index + start_index) / 2;
  // The first test ensures that we get to the code that handles the Latin1
  // range with a single not-taken branch, speeding up this important
  // character range (even non-Latin1 charset-based text has spaces and
  // punctuation).
  if (*border - 1 > String::kMaxOneByteCharCode &&  // Latin1 case.
      end_index - start_index > (*new_start_index - start_index) * 2 &&
      last - first > kSize * 2 && binary_chop_index > *new_start_index &&
      ranges->at(binary_chop_index) >= first + 2 * kSize) {
    uint32_t scan_forward_for_section_border = binary_chop_index;
    uint32_t new_border = (ranges->at(binary_chop_index) | kMask) + 1;

    while (scan_forward_for_section_border < end_index) {
      if (ranges->at(scan_forward_for_section_border) > new_border) {
        *new_start_index = scan_forward_for_section_border;
        *border = new_border;
        break;
      }
      scan_forward_for_section_border++;
    }
  }

  DCHECK(*new_start_index > start_index);
  *new_end_index = *new_start_index - 1;
  if (ranges->at(*new_end_index) == *border) {
    (*new_end_index)--;
  }
  if (*border >= ranges->at(end_index)) {
    *border = ranges->at(end_index);
    *new_start_index = end_index;  // Won't be used.
    *new_end_index = end_index - 1;
  }
}

// Gets a series of segment boundaries representing a character class.  If the
// character is in the range between an even and an odd boundary (counting from
// start_index) then go to even_label, otherwise go to odd_label.  We already
// know that the character is in the range of min_char to max_char inclusive.
// Either label can be nullptr indicating backtracking.  Either label can also
// be equal to the fall_through label.
void GenerateBranches(RegExpMacroAssembler* masm, ZoneList<base::uc32>* ranges,
                      uint32_t start_index, uint32_t end_index,
                      base::uc32 min_char, base::uc32 max_char,
                      Label* fall_through, Label* even_label,
                      Label* odd_label) {
  DCHECK_LE(min_char, String::kMaxUtf16CodeUnit);
  DCHECK_LE(max_char, String::kMaxUtf16CodeUnit);

  base::uc32 first = ranges->at(start_index);
  base::uc32 last = ranges->at(end_index) - 1;

  DCHECK_LT(min_char, first);

  // Just need to test if the character is before or on-or-after
  // a particular character.
  if (start_index == end_index) {
    EmitBoundaryTest(masm, first, fall_through, even_label, odd_label);
    return;
  }

  // Another almost trivial case:  There is one interval in the middle that is
  // different from the end intervals.
  if (start_index + 1 == end_index) {
    EmitDoubleBoundaryTest(masm, first, last, fall_through, even_label,
                           odd_label);
    return;
  }

  // It's not worth using table lookup if there are very few intervals in the
  // character class.
  if (end_index - start_index <= 6) {
    // It is faster to test for individual characters, so we look for those
    // first, then try arbitrary ranges in the second round.
    static uint32_t kNoCutIndex = -1;
    uint32_t cut = kNoCutIndex;
    for (uint32_t i = start_index; i < end_index; i++) {
      if (ranges->at(i) == ranges->at(i + 1) - 1) {
        cut = i;
        break;
      }
    }
    if (cut == kNoCutIndex) cut = start_index;
    CutOutRange(masm, ranges, start_index, end_index, cut, even_label,
                odd_label);
    DCHECK_GE(end_index - start_index, 2);
    GenerateBranches(masm, ranges, start_index + 1, end_index - 1, min_char,
                     max_char, fall_through, even_label, odd_label);
    return;
  }

  // If there are a lot of intervals in the regexp, then we will use tables to
  // determine whether the character is inside or outside the character class.
  static const int kBits = RegExpMacroAssembler::kTableSizeBits;

  if ((max_char >> kBits) == (min_char >> kBits)) {
    EmitUseLookupTable(masm, ranges, start_index, end_index, min_char,
                       fall_through, even_label, odd_label);
    return;
  }

  if ((min_char >> kBits) != first >> kBits) {
    masm->CheckCharacterLT(first, odd_label);
    GenerateBranches(masm, ranges, start_index + 1, end_index, first, max_char,
                     fall_through, odd_label, even_label);
    return;
  }

  uint32_t new_start_index = 0;
  uint32_t new_end_index = 0;
  base::uc32 border = 0;

  SplitSearchSpace(ranges, start_index, end_index, &new_start_index,
                   &new_end_index, &border);

  Label handle_rest;
  Label* above = &handle_rest;
  if (border == last + 1) {
    // We didn't find any section that started after the limit, so everything
    // above the border is one of the terminal labels.
    above = (end_index & 1) != (start_index & 1) ? odd_label : even_label;
    DCHECK(new_end_index == end_index - 1);
  }

  DCHECK_LE(start_index, new_end_index);
  DCHECK_LE(new_start_index, end_index);
  DCHECK_LT(start_index, new_start_index);
  DCHECK_LT(new_end_index, end_index);
  DCHECK(new_end_index + 1 == new_start_index ||
         (new_end_index + 2 == new_start_index &&
          border == ranges->at(new_end_index + 1)));
  DCHECK_LT(min_char, border - 1);
  DCHECK_LT(border, max_char);
  DCHECK_LT(ranges->at(new_end_index), border);
  DCHECK(border < ranges->at(new_start_index) ||
         (border == ranges->at(new_start_index) &&
          new_start_index == end_index && new_end_index == end_index - 1 &&
          border == last + 1));
  DCHECK(new_start_index == 0 || border >= ranges->at(new_start_index - 1));

  masm->CheckCharacterGT(border - 1, above);
  Label dummy;
  GenerateBranches(masm, ranges, start_index, new_end_index, min_char,
                   border - 1, &dummy, even_label, odd_label);
  if (handle_rest.is_linked()) {
    masm->Bind(&handle_rest);
    bool flip = (new_start_index & 1) != (start_index & 1);
    GenerateBranches(masm, ranges, new_start_index, end_index, border, max_char,
                     &dummy, flip ? odd_label : even_label,
                     flip ? even_label : odd_label);
  }
}

void EmitClassRanges(RegExpMacroAssembler* macro_assembler,
                     RegExpClassRanges* cr, bool one_byte, Label* on_failure,
                     int cp_offset, bool check_offset, bool preloaded,
                     Zone* zone) {
  ZoneList<CharacterRange>* ranges = cr->ranges(zone);
  CharacterRange::Canonicalize(ranges);

  // Now that all processing (like case-insensitivity) is done, clamp the
  // ranges to the set of ranges that may actually occur in the subject string.
  if (one_byte) CharacterRange::ClampToOneByte(ranges);

  const int ranges_length = ranges->length();
  if (ranges_length == 0) {
    if (!cr->is_negated()) {
      macro_assembler->GoTo(on_failure);
    }
    if (check_offset) {
      macro_assembler->CheckPosition(cp_offset, on_failure);
    }
    return;
  }

  const base::uc32 max_char = MaxCodeUnit(one_byte);
  if (ranges_length == 1 && ranges->at(0).IsEverything(max_char)) {
    if (cr->is_negated()) {
      macro_assembler->GoTo(on_failure);
    } else {
      // This is a common case hit by non-anchored expressions.
      if (check_offset) {
        macro_assembler->CheckPosition(cp_offset, on_failure);
      }
    }
    return;
  }

  if (!preloaded) {
    macro_assembler->LoadCurrentCharacter(cp_offset, on_failure, check_offset);
  }

  if (cr->is_standard(zone) && macro_assembler->CheckSpecialClassRanges(
                                   cr->standard_type(), on_failure)) {
    return;
  }

  static constexpr int kMaxRangesForInlineBranchGeneration = 16;
  if (ranges_length > kMaxRangesForInlineBranchGeneration) {
    // For large range sets, emit a more compact instruction sequence to avoid
    // a potentially problematic increase in code size.
    // Note the flipped logic below (we check InRange if negated, NotInRange if
    // not negated); this is necessary since the method falls through on
    // failure whereas we want to fall through on success.
    if (cr->is_negated()) {
      if (macro_assembler->CheckCharacterInRangeArray(ranges, on_failure)) {
        return;
      }
    } else {
      if (macro_assembler->CheckCharacterNotInRangeArray(ranges, on_failure)) {
        return;
      }
    }
  }

  // Generate a flat list of range boundaries for consumption by
  // GenerateBranches. See the comment on that function for how the list should
  // be structured
  ZoneList<base::uc32>* range_boundaries =
      zone->New<ZoneList<base::uc32>>(ranges_length * 2, zone);

  bool zeroth_entry_is_failure = !cr->is_negated();

  for (int i = 0; i < ranges_length; i++) {
    CharacterRange& range = ranges->at(i);
    if (range.from() == 0) {
      DCHECK_EQ(i, 0);
      zeroth_entry_is_failure = !zeroth_entry_is_failure;
    } else {
      range_boundaries->Add(range.from(), zone);
    }
    // `+ 1` to convert from inclusive to exclusive `to`.
    // [from, to] == [from, to+1[.
    range_boundaries->Add(range.to() + 1, zone);
  }
  int end_index = range_boundaries->length() - 1;
  if (range_boundaries->at(end_index) > max_char) {
    end_index--;
  }

  Label fall_through;
  GenerateBranches(macro_assembler, range_boundaries,
                   0,  // start_index.
                   end_index,
                   0,  // min_char.
                   max_char, &fall_through,
                   zeroth_entry_is_failure ? &fall_through : on_failure,
                   zeroth_entry_is_failure ? on_failure : &fall_through);
  macro_assembler->Bind(&fall_through);
}

}  // namespace

RegExpNode::~RegExpNode() = default;

RegExpNode::LimitResult RegExpNode::LimitVersions(RegExpCompiler* compiler,
                                                  Trace* trace) {
  // If we are generating a greedy loop then don't stop and don't reuse code.
  if (trace->stop_node() != nullptr) {
    return CONTINUE;
  }

  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  if (trace->is_trivial()) {
    if (label_.is_bound() || on_work_list() || !KeepRecursing(compiler)) {
      // If a generic version is already scheduled to be generated or we have
      // recursed too deeply then just generate a jump to that code.
      macro_assembler->GoTo(&label_);
      // This will queue it up for generation of a generic version if it hasn't
      // already been queued.
      compiler->AddWork(this);
      return DONE;
    }
    // Generate generic version of the node and bind the label for later use.
    macro_assembler->Bind(&label_);
    return CONTINUE;
  }

  // We are being asked to make a non-generic version.  Keep track of how many
  // non-generic versions we generate so as not to overdo it.
  trace_count_++;
  if (KeepRecursing(compiler) && compiler->optimize() &&
      trace_count_ < kMaxCopiesCodeGenerated) {
    return CONTINUE;
  }

  // If we get here code has been generated for this node too many times or
  // recursion is too deep.  Time to switch to a generic version.  The code for
  // generic versions above can handle deep recursion properly.
  bool was_limiting = compiler->limiting_recursion();
  compiler->set_limiting_recursion(true);
  trace->Flush(compiler, this);
  compiler->set_limiting_recursion(was_limiting);
  return DONE;
}

bool RegExpNode::KeepRecursing(RegExpCompiler* compiler) {
  return !compiler->limiting_recursion() &&
         compiler->recursion_depth() <= RegExpCompiler::kMaxRecursion;
}

void ActionNode::FillInBMInfo(Isolate* isolate, int offset, int budget,
                              BoyerMooreLookahead* bm, bool not_at_start) {
  base::Optional<RegExpFlags> old_flags;
  if (action_type_ == MODIFY_FLAGS) {
    // It is not guaranteed that we hit the resetting modify flags node, due to
    // recursion budget limitation for filling in BMInfo. Therefore we reset the
    // flags manually to the previous state after recursing.
    old_flags = bm->compiler()->flags();
    bm->compiler()->set_flags(flags());
  }
  if (action_type_ == POSITIVE_SUBMATCH_SUCCESS) {
    // Anything may follow a positive submatch success, thus we need to accept
    // all characters from this position onwards.
    bm->SetRest(offset);
  } else {
    on_success()->FillInBMInfo(isolate, offset, budget - 1, bm, not_at_start);
  }
  SaveBMInfo(bm, not_at_start, offset);
  if (old_flags.has_value()) {
    bm->compiler()->set_flags(*old_flags);
  }
}

void ActionNode::GetQuickCheckDetails(QuickCheckDetails* details,
                                      RegExpCompiler* compiler, int filled_in,
                                      bool not_at_start) {
  if (action_type_ == SET_REGISTER_FOR_LOOP) {
    on_success()->GetQuickCheckDetailsFromLoopEntry(details, compiler,
                                                    filled_in, not_at_start);
  } else {
    if (action_type() == MODIFY_FLAGS) {
      compiler->set_flags(flags());
    }
    on_success()->GetQuickCheckDetails(details, compiler, filled_in,
                                       not_at_start);
  }
}

void AssertionNode::FillInBMInfo(Isolate* isolate, int offset, int budget,
                                 BoyerMooreLookahead* bm, bool not_at_start) {
  // Match the behaviour of EatsAtLeast on this node.
  if (assertion_type() == AT_START && not_at_start) return;
  on_success()->FillInBMInfo(isolate, offset, budget - 1, bm, not_at_start);
  SaveBMInfo(bm, not_at_start, offset);
}

void NegativeLookaroundChoiceNode::GetQuickCheckDetails(
    QuickCheckDetails* details, RegExpCompiler* compiler, int filled_in,
    bool not_at_start) {
  RegExpNode* node = continue_node();
  return node->GetQuickCheckDetails(details, compiler, filled_in, not_at_start);
}

namespace {

// Takes the left-most 1-bit and smears it out, setting all bits to its right.
inline uint32_t SmearBitsRight(uint32_t v) {
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  return v;
}

}  // namespace

bool QuickCheckDetails::Rationalize(bool asc) {
  bool found_useful_op = false;
  const uint32_t char_mask = CharMask(asc);
  mask_ = 0;
  value_ = 0;
  int char_shift = 0;
  for (int i = 0; i < characters_; i++) {
    Position* pos = &positions_[i];
    if ((pos->mask & String::kMaxOneByteCharCode) != 0) {
      found_useful_op = true;
    }
    mask_ |= (pos->mask & char_mask) << char_shift;
    value_ |= (pos->value & char_mask) << char_shift;
    char_shift += asc ? 8 : 16;
  }
  return found_useful_op;
}

int RegExpNode::EatsAtLeast(bool not_at_start) {
  return not_at_start ? eats_at_least_.eats_at_least_from_not_start
                      : eats_at_least_.eats_at_least_from_possibly_start;
}

EatsAtLeastInfo RegExpNode::EatsAtLeastFromLoopEntry() {
  // SET_REGISTER_FOR_LOOP is only used to initialize loop counters, and it
  // implies that the following node must be a LoopChoiceNode. If we need to
  // set registers to constant values for other reasons, we could introduce a
  // new action type SET_REGISTER that doesn't imply anything about its
  // successor.
  UNREACHABLE();
}

void RegExpNode::GetQuickCheckDetailsFromLoopEntry(QuickCheckDetails* details,
                                                   RegExpCompiler* compiler,
                                                   int characters_filled_in,
                                                   bool not_at_start) {
  // See comment in RegExpNode::EatsAtLeastFromLoopEntry.
  UNREACHABLE();
}

EatsAtLeastInfo LoopChoiceNode::EatsAtLeastFromLoopEntry() {
  DCHECK_EQ(alternatives_->length(), 2);  // There's just loop and continue.

  if (read_backward()) {
    // The eats_at_least value is not used if reading backward. The
    // EatsAtLeastPropagator should've zeroed it as well.
    DCHECK_EQ(eats_at_least_info()->eats_at_least_from_possibly_start, 0);
    DCHECK_EQ(eats_at_least_info()->eats_at_least_from_not_start, 0);
    return {};
  }

  // Figure out how much the loop body itself eats, not including anything in
  // the continuation case. In general, the nodes in the loop body should report
  // that they eat at least the number eaten by the continuation node, since any
  // successful match in the loop body must also include the continuation node.
  // However, in some cases involving positive lookaround, the loop body under-
  // reports its appetite, so use saturated math here to avoid negative numbers.
  uint8_t loop_body_from_not_start = base::saturated_cast<uint8_t>(
      loop_node_->EatsAtLeast(true) - continue_node_->EatsAtLeast(true));
  uint8_t loop_body_from_possibly_start = base::saturated_cast<uint8_t>(
      loop_node_->EatsAtLeast(false) - continue_node_->EatsAtLeast(true));

  // Limit the number of loop iterations to avoid overflow in subsequent steps.
  int loop_iterations = base::saturated_cast<uint8_t>(min_loop_iterations());

  EatsAtLeastInfo result;
  result.eats_at_least_from_not_start =
      base::saturated_cast<uint8_t>(loop_iterations * loop_body_from_not_start +
                                    continue_node_->EatsAtLeast(true));
  if (loop_iterations > 0 && loop_body_from_possibly_start > 0) {
    // First loop iteration eats at least one, so all subsequent iterations
    // and the after-loop chunk are guaranteed to not be at the start.
    result.eats_at_least_from_possibly_start = base::saturated_cast<uint8_t>(
        loop_body_from_possibly_start +
        (loop_iterations - 1) * loop_body_from_not_start +
        continue_node_->EatsAtLeast(true));
  } else {
    // Loop body might eat nothing, so only continue node contributes.
    result.eats_at_least_from_possibly_start =
        continue_node_->EatsAtLeast(false);
  }
  return result;
}

bool RegExpNode::EmitQuickCheck(RegExpCompiler* compiler,
                                Trace* bounds_check_trace, Trace* trace,
                                bool preload_has_checked_bounds,
                                Label* on_possible_success,
                                QuickCheckDetails* details,
                                bool fall_through_on_failure,
                                ChoiceNode* predecessor) {
  DCHECK_NOT_NULL(predecessor);
  if (details->characters() == 0) return false;
  GetQuickCheckDetails(details, compiler, 0,
                       trace->at_start() == Trace::FALSE_VALUE);
  if (details->cannot_match()) return false;
  if (!details->Rationalize(compiler->one_byte())) return false;
  DCHECK(details->characters() == 1 ||
         compiler->macro_assembler()->CanReadUnaligned());
  uint32_t mask = details->mask();
  uint32_t value = details->value();

  RegExpMacroAssembler* assembler = compiler->macro_assembler();

  if (trace->characters_preloaded() != details->characters()) {
    DCHECK(trace->cp_offset() == bounds_check_trace->cp_offset());
    // The bounds check is performed using the minimum number of characters
    // any choice would eat, so if the bounds check fails, then none of the
    // choices can succeed, so we can just immediately backtrack, rather
    // than go to the next choice. The number of characters preloaded may be
    // less than the number used for the bounds check.
    int eats_at_least = predecessor->EatsAtLeast(
        bounds_check_trace->at_start() == Trace::FALSE_VALUE);
    DCHECK_GE(eats_at_least, details->characters());
    assembler->LoadCurrentCharacter(
        trace->cp_offset(), bounds_check_trace->backtrack(),
        !preload_has_checked_bounds, details->characters(), eats_at_least);
  }

  bool need_mask = true;

  if (details->characters() == 1) {
    // If number of characters preloaded is 1 then we used a byte or 16 bit
    // load so the value is already masked down.
    const uint32_t char_mask = CharMask(compiler->one_byte());
    if ((mask & char_mask) == char_mask) need_mask = false;
    mask &= char_mask;
  } else {
    // For 2-character preloads in one-byte mode or 1-character preloads in
    // two-byte mode we also use a 16 bit load with zero extend.
    static const uint32_t kTwoByteMask = 0xFFFF;
    static const uint32_t kFourByteMask = 0xFFFFFFFF;
    if (details->characters() == 2 && compiler->one_byte()) {
      if ((mask & kTwoByteMask) == kTwoByteMask) need_mask = false;
    } else if (details->characters() == 1 && !compiler->one_byte()) {
      if ((mask & kTwoByteMask) == kTwoByteMask) need_mask = false;
    } else {
      if (mask == kFourByteMask) need_mask = false;
    }
  }

  if (fall_through_on_failure) {
    if (need_mask) {
      assembler->CheckCharacterAfterAnd(value, mask, on_possible_success);
    } else {
      assembler->CheckCharacter(value, on_possible_success);
    }
  } else {
    if (need_mask) {
      assembler->CheckNotCharacterAfterAnd(value, mask, trace->backtrack());
    } else {
      assembler->CheckNotCharacter(value, trace->backtrack());
    }
  }
  return true;
}

// Here is the meat of GetQuickCheckDetails (see also the comment on the
// super-class in the .h file).
//
// We iterate along the text object, building up for each character a
// mask and value that can be used to test for a quick failure to match.
// The masks and values for the positions will be combined into a single
// machine word for the current character width in order to be used in
// generating a quick check.
void TextNode::GetQuickCheckDetails(QuickCheckDetails* details,
                                    RegExpCompiler* compiler,
                                    int characters_filled_in,
                                    bool not_at_start) {
  // Do not collect any quick check details if the text node reads backward,
  // since it reads in the opposite direction than we use for quick checks.
  if (read_backward()) return;
  Isolate* isolate = compiler->macro_assembler()->isolate();
  DCHECK(characters_filled_in < details->characters());
  int characters = details->characters();
  const uint32_t char_mask = CharMask(compiler->one_byte());
  for (int k = 0; k < elements()->length(); k++) {
    TextElement elm = elements()->at(k);
    if (elm.text_type() == TextElement::ATOM) {
      base::Vector<const base::uc16> quarks = elm.atom()->data();
      for (int i = 0; i < characters && i < quarks.length(); i++) {
        QuickCheckDetails::Position* pos =
            details->positions(characters_filled_in);
        base::uc16 c = quarks[i];
        if (IsIgnoreCase(compiler->flags())) {
          unibrow::uchar chars[4];
          int length = GetCaseIndependentLetters(
              isolate, c, compiler->one_byte(), chars, 4);
          if (length == 0) {
            // This can happen because all case variants are non-Latin1, but we
            // know the input is Latin1.
            details->set_cannot_match();
            pos->determines_perfectly = false;
            return;
          }
          if (length == 1) {
            // This letter has no case equivalents, so it's nice and simple
            // and the mask-compare will determine definitely whether we have
            // a match at this character position.
            pos->mask = char_mask;
            pos->value = chars[0];
            pos->determines_perfectly = true;
          } else {
            uint32_t common_bits = char_mask;
            uint32_t bits = chars[0];
            for (int j = 1; j < length; j++) {
              uint32_t differing_bits = ((chars[j] & common_bits) ^ bits);
              common_bits ^= differing_bits;
              bits &= common_bits;
            }
            // If length is 2 and common bits has only one zero in it then
            // our mask and compare instruction will determine definitely
            // whether we have a match at this character position.  Otherwise
            // it can only be an approximate check.
            uint32_t one_zero = (common_bits | ~char_mask);
            if (length == 2 && ((~one_zero) & ((~one_zero) - 1)) == 0) {
              pos->determines_perfectly = true;
            }
            pos->mask = common_bits;
            pos->value = bits;
          }
        } else {
          // Don't ignore case.  Nice simple case where the mask-compare will
          // determine definitely whether we have a match at this character
          // position.
          if (c > char_mask) {
            details->set_cannot_match();
            pos->determines_perfectly = false;
            return;
          }
          pos->mask = char_mask;
          pos->value = c;
          pos->determines_perfectly = true;
        }
        characters_filled_in++;
        DCHECK(characters_filled_in <= details->characters());
        if (characters_filled_in == details->characters()) {
          return;
        }
      }
    } else {
      QuickCheckDetails::Position* pos =
          details->positions(characters_filled_in);
      RegExpClassRanges* tree = elm.class_ranges();
      ZoneList<CharacterRange>* ranges = tree->ranges(zone());
      if (tree->is_negated() || ranges->is_empty()) {
        // A quick check uses multi-character mask and compare.  There is no
        // useful way to incorporate a negative char class into this scheme
        // so we just conservatively create a mask and value that will always
        // succeed.
        // Likewise for empty ranges (empty ranges can occur e.g. when
        // compiling for one-byte subjects and impossible (non-one-byte) ranges
        // have been removed).
        pos->mask = 0;
        pos->value = 0;
      } else {
        int first_range = 0;
        while (ranges->at(first_range).from() > char_mask) {
          first_range++;
          if (first_range == ranges->length()) {
            details->set_cannot_match();
            pos->determines_perfectly = false;
            return;
          }
        }
        CharacterRange range = ranges->at(first_range);
        const base::uc32 first_from = range.from();
        const base::uc32 first_to =
            (range.to() > char_mask) ? char_mask : range.to();
        const uint32_t differing_bits = (first_from ^ first_to);
        // A mask and compare is only perfect if the differing bits form a
        // number like 00011111 with one single block of trailing 1s.
        if ((differing_bits & (differing_bits + 1)) == 0 &&
            first_from + differing_bits == first_to) {
          pos->determines_perfectly = true;
        }
        uint32_t common_bits = ~SmearBitsRight(differing_bits);
        uint32_t bits = (first_from & common_bits);
        for (int i = first_range + 1; i < ranges->length(); i++) {
          range = ranges->at(i);
          const base::uc32 from = range.from();
          if (from > char_mask) continue;
          const base::uc32 to =
              (range.to() > char_mask) ? char_mask : range.to();
          // Here we are combining more ranges into the mask and compare
          // value.  With each new range the mask becomes more sparse and
          // so the chances of a false positive rise.  A character class
          // with multiple ranges is assumed never to be equivalent to a
          // mask and compare operation.
          pos->determines_perfectly = false;
          uint32_t new_common_bits = (from ^ to);
          new_common_bits = ~SmearBitsRight(new_common_bits);
          common_bits &= new_common_bits;
          bits &= new_common_bits;
          uint32_t new_differing_bits = (from & common_bits) ^ bits;
          common_bits ^= new_differing_bits;
          bits &= common_bits;
        }
        pos->mask = common_bits;
        pos->value = bits;
      }
      characters_filled_in++;
      DCHECK(characters_filled_in <= details->characters());
      if (characters_filled_in == details->characters()) return;
    }
  }
  DCHECK(characters_filled_in != details->characters());
  if (!details->cannot_match()) {
    on_success()->GetQuickCheckDetails(details, compiler, characters_filled_in,
                                       true);
  }
}

void QuickCheckDetails::Clear() {
  for (int i = 0; i < characters_; i++) {
    positions_[i].mask = 0;
    positions_[i].value = 0;
    positions_[i].determines_perfectly = false;
  }
  characters_ = 0;
}

void QuickCheckDetails::Advance(int by, bool one_byte) {
  if (by >= characters_ || by < 0) {
    DCHECK_IMPLIES(by < 0, characters_ == 0);
    Clear();
    return;
  }
  DCHECK_LE(characters_ - by, 4);
  DCHECK_LE(characters_, 4);
  for (int i = 0; i < characters_ - by; i++) {
    positions_[i] = positions_[by + i];
  }
  for (int i = characters_ - by; i < characters_; i++) {
    positions_[i].mask = 0;
    positions_[i].value = 0;
    positions_[i].determines_perfectly = false;
  }
  characters_ -= by;
  // We could change mask_ and value_ here but we would never advance unless
  // they had already been used in a check and they won't be used again because
  // it would gain us nothing.  So there's no point.
}

void QuickCheckDetails::Merge(QuickCheckDetails* other, int from_index) {
  DCHECK(characters_ == other->characters_);
  if (other->cannot_match_) {
    return;
  }
  if (cannot_match_) {
    *this = *other;
    return;
  }
  for (int i = from_index; i < characters_; i++) {
    QuickCheckDetails::Position* pos = positions(i);
    QuickCheckDetails::Position* other_pos = other->positions(i);
    if (pos->mask != other_pos->mask || pos->value != other_pos->value ||
        !other_pos->determines_perfectly) {
      // Our mask-compare operation will be approximate unless we have the
      // exact same operation on both sides of the alternation.
      pos->determines_perfectly = false;
    }
    pos->mask &= other_pos->mask;
    pos->value &= pos->mask;
    other_pos->value &= pos->mask;
    uint32_t differing_bits = (pos->value ^ other_pos->value);
    pos->mask &= ~differing_bits;
    pos->value &= pos->mask;
  }
}

class VisitMarker {
 public:
  explicit VisitMarker(NodeInfo* info) : info_(info) {
    DCHECK(!info->visited);
    info->visited = true;
  }
  ~VisitMarker() { info_->visited = false; }

 private:
  NodeInfo* info_;
};

// Temporarily sets traversed_loop_initialization_node_.
class LoopInitializationMarker {
 public:
  explicit LoopInitializationMarker(LoopChoiceNode* node) : node_(node) {
    DCHECK(!node_->traversed_loop_initialization_node_);
    node_->traversed_loop_initialization_node_ = true;
  }
  ~LoopInitializationMarker() {
    DCHECK(node_->traversed_loop_initialization_node_);
    node_->traversed_loop_initialization_node_ = false;
  }
  LoopInitializationMarker(const LoopInitializationMarker&) = delete;
  LoopInitializationMarker& operator=(const LoopInitializationMarker&) = delete;

 private:
  LoopChoiceNode* node_;
};

// Temporarily decrements min_loop_iterations_.
class IterationDecrementer {
 public:
  explicit IterationDecrementer(LoopChoiceNode* node) : node_(node) {
    DCHECK_GT(node_->min_loop_iterations_, 0);
    --node_->min_loop_iterations_;
  }
  ~IterationDecrementer() { ++node_->min_loop_iterations_; }
  IterationDecrementer(const IterationDecrementer&) = delete;
  IterationDecrementer& operator=(const IterationDecrementer&) = delete;

 private:
  LoopChoiceNode* node_;
};

RegExpNode* SeqRegExpNode::FilterOneByte(int depth, RegExpFlags flags) {
  if (info()->replacement_calculated) return replacement();
  if (depth < 0) return this;
  DCHECK(!info()->visited);
  VisitMarker marker(info());
  return FilterSuccessor(depth - 1, flags);
}

RegExpNode* SeqRegExpNode::FilterSuccessor(int depth, RegExpFlags flags) {
  RegExpNode* next = on_success_->FilterOneByte(depth - 1, flags);
  if (next == nullptr) return set_replacement(nullptr);
  on_success_ = next;
  return set_replacement(this);
}

// We need to check for the following characters: 0x39C 0x3BC 0x178.
bool RangeContainsLatin1Equivalents(CharacterRange range) {
  // TODO(dcarney): this could be a lot more efficient.
  return range.Contains(0x039C) || range.Contains(0x03BC) ||
         range.Contains(0x0178);
}

namespace {

bool RangesContainLatin1Equivalents(ZoneList<CharacterRange>* ranges) {
  for (int i = 0; i < ranges->length(); i++) {
    // TODO(dcarney): this could be a lot more efficient.
    if (RangeContainsLatin1Equivalents(ranges->at(i))) return true;
  }
  return false;
}

}  // namespace

RegExpNode* TextNode::FilterOneByte(int depth, RegExpFlags flags) {
  if (info()->replacement_calculated) return replacement();
  if (depth < 0) return this;
  DCHECK(!info()->visited);
  VisitMarker marker(info());
  int element_count = elements()->length();
  for (int i = 0; i < element_count; i++) {
    TextElement elm = elements()->at(i);
    if (elm.text_type() == TextElement::ATOM) {
      base::Vector<const base::uc16> quarks = elm.atom()->data();
      for (int j = 0; j < quarks.length(); j++) {
        base::uc16 c = quarks[j];
        if (IsIgnoreCase(flags)) {
          c = unibrow::Latin1::TryConvertToLatin1(c);
        }
        if (c > unibrow::Latin1::kMaxChar) return set_replacement(nullptr);
        // Replace quark in case we converted to Latin-1.
        base::uc16* writable_quarks = const_cast<base::uc16*>(quarks.begin());
        writable_quarks[j] = c;
      }
    } else {
      DCHECK(elm.text_type() == TextElement::CLASS_RANGES);
      RegExpClassRanges* cr = elm.class_ranges();
      ZoneList<CharacterRange>* ranges = cr->ranges(zone());
      CharacterRange::Canonicalize(ranges);
      // Now they are in order so we only need to look at the first.
      int range_count = ranges->length();
      if (cr->is_negated()) {
        if (range_count != 0 && ranges->at(0).from() == 0 &&
            ranges->at(0).to() >= String::kMaxOneByteCharCode) {
          // This will be handled in a later filter.
          if (IsIgnoreCase(flags) && RangesContainLatin1Equivalents(ranges)) {
            continue;
          }
          return set_replacement(nullptr);
        }
      } else {
        if (range_count == 0 ||
            ranges->at(0).from() > String::kMaxOneByteCharCode) {
          // This will be handled in a later filter.
          if (IsIgnoreCase(flags) && RangesContainLatin1Equivalents(ranges)) {
            continue;
          }
          return set_replacement(nullptr);
        }
      }
    }
  }
  return FilterSuccessor(depth - 1, flags);
}

RegExpNode* LoopChoiceNode::FilterOneByte(int depth, RegExpFlags flags) {
  if (info()->replacement_calculated) return replacement();
  if (depth < 0) return this;
  if (info()->visited) return this;
  {
    VisitMarker marker(info());

    RegExpNode* continue_replacement =
        continue_node_->FilterOneByte(depth - 1, flags);
    // If we can't continue after the loop then there is no sense in doing the
    // loop.
    if (continue_replacement == nullptr) return set_replacement(nullptr);
  }

  return ChoiceNode::FilterOneByte(depth - 1, flags);
}

RegExpNode* ChoiceNode::FilterOneByte(int depth, RegExpFlags flags) {
  if (info()->replacement_calculated) return replacement();
  if (depth < 0) return this;
  if (info()->visited) return this;
  VisitMarker marker(info());
  int choice_count = alternatives_->length();

  for (int i = 0; i < choice_count; i++) {
    GuardedAlternative alternative = alternatives_->at(i);
    if (alternative.guards() != nullptr &&
        alternative.guards()->length() != 0) {
      set_replacement(this);
      return this;
    }
  }

  int surviving = 0;
  RegExpNode* survivor = nullptr;
  for (int i = 0; i < choice_count; i++) {
    GuardedAlternative alternative = alternatives_->at(i);
    RegExpNode* replacement =
        alternative.node()->FilterOneByte(depth - 1, flags);
    DCHECK(replacement != this);  // No missing EMPTY_MATCH_CHECK.
    if (replacement != nullptr) {
      alternatives_->at(i).set_node(replacement);
      surviving++;
      survivor = replacement;
    }
  }
  if (surviving < 2) return set_replacement(survivor);

  set_replacement(this);
  if (surviving == choice_count) {
    return this;
  }
  // Only some of the nodes survived the filtering.  We need to rebuild the
  // alternatives list.
  ZoneList<GuardedAlternative>* new_alternatives =
      zone()->New<ZoneList<GuardedAlternative>>(surviving, zone());
  for (int i = 0; i < choice_count; i++) {
    RegExpNode* replacement =
        alternatives_->at(i).node()->FilterOneByte(depth - 1, flags);
    if (replacement != nullptr) {
      alternatives_->at(i).set_node(replacement);
      new_alternatives->Add(alternatives_->at(i), zone());
    }
  }
  alternatives_ = new_alternatives;
  return this;
}

RegExpNode* NegativeLookaroundChoiceNode::FilterOneByte(int depth,
                                                        RegExpFlags flags) {
  if (info()->replacement_calculated) return replacement();
  if (depth < 0) return this;
  if (info()->visited) return this;
  VisitMarker marker(info());
  // Alternative 0 is the negative lookahead, alternative 1 is what comes
  // afterwards.
  RegExpNode* node = continue_node();
  RegExpNode* replacement = node->FilterOneByte(depth - 1, flags);
  if (replacement == nullptr) return set_replacement(nullptr);
  alternatives_->at(kContinueIndex).set_node(replacement);

  RegExpNode* neg_node = lookaround_node();
  RegExpNode* neg_replacement = neg_node->FilterOneByte(depth - 1, flags);
  // If the negative lookahead is always going to fail then
  // we don't need to check it.
  if (neg_replacement == nullptr) return set_replacement(replacement);
  alternatives_->at(kLookaroundIndex).set_node(neg_replacement);
  return set_replacement(this);
}

void LoopChoiceNode::GetQuickCheckDetails(QuickCheckDetails* details,
                                          RegExpCompiler* compiler,
                                          int characters_filled_in,
                                          bool not_at_start) {
  if (body_can_be_zero_length_ || info()->visited) return;
  not_at_start = not_at_start || this->not_at_start();
  DCHECK_EQ(alternatives_->length(), 2);  // There's just loop and continue.
  if (traversed_loop_initialization_node_ && min_loop_iterations_ > 0 &&
      loop_node_->EatsAtLeast(not_at_start) >
          continue_node_->EatsAtLeast(true)) {
    // Loop body is guaranteed to execute at least once, and consume characters
    // when it does, meaning the only possible quick checks from this point
    // begin with the loop body. We may recursively visit this LoopChoiceNode,
    // but we temporarily decrease its minimum iteration counter so we know when
    // to check the continue case.
    IterationDecrementer next_iteration(this);
    loop_node_->GetQuickCheckDetails(details, compiler, characters_filled_in,
                                     not_at_start);
  } else {
    // Might not consume anything in the loop body, so treat it like a normal
    // ChoiceNode (and don't recursively visit this node again).
    VisitMarker marker(info());
    ChoiceNode::GetQuickCheckDetails(details, compiler, characters_filled_in,
                                     not_at_start);
  }
}

void LoopChoiceNode::GetQuickCheckDetailsFromLoopEntry(
    QuickCheckDetails* details, RegExpCompiler* compiler,
    int characters_filled_in, bool not_at_start) {
  if (traversed_loop_initialization_node_) {
    // We already entered this loop once, exited via its continuation node, and
    // followed an outer loop's back-edge to before the loop entry point. We
    // could try to reset the minimum iteration count to its starting value at
    // this point, but that seems like more trouble than it's worth. It's safe
    // to keep going with the current (possibly reduced) minimum iteration
    // count.
    GetQuickCheckDetails(details, compiler, characters_filled_in, not_at_start);
  } else {
    // We are entering a loop via its counter initialization action, meaning we
    // are guaranteed to run the loop body at least some minimum number of times
    // before running the continuation node. Set a flag so that this node knows
    // (now and any times we visit it again recursively) that it was entered
    // from the top.
    LoopInitializationMarker marker(this);
    GetQuickCheckDetails(details, compiler, characters_filled_in, not_at_start);
  }
}

void LoopChoiceNode::FillInBMInfo(Isolate* isolate, int offset, int budget,
                                  BoyerMooreLookahead* bm, bool not_at_start) {
  if (body_can_be_zero_length_ || budget <= 0) {
    bm->SetRest(offset);
    SaveBMInfo(bm, not_at_start, offset);
    return;
  }
  ChoiceNode::FillInBMInfo(isolate, offset, budget - 1, bm, not_at_start);
  SaveBMInfo(bm, not_at_start, offset);
}

void ChoiceNode::GetQuickCheckDetails(QuickCheckDetails* details,
                                      RegExpCompiler* compiler,
                                      int characters_filled_in,
                                      bool not_at_start) {
  not_at_start = (not_at_start || not_at_start_);
  int choice_count = alternatives_->length();
  DCHECK_LT(0, choice_count);
  alternatives_->at(0).node()->GetQuickCheckDetails(
      details, compiler, characters_filled_in, not_at_start);
  for (int i = 1; i < choice_count; i++) {
    QuickCheckDetails new_details(details->characters());
    RegExpNode* node = alternatives_->at(i).node();
    node->GetQuickCheckDetails(&new_details, compiler, characters_filled_in,
                               not_at_start);
    // Here we merge the quick match details of the two branches.
    details->Merge(&new_details, characters_filled_in);
  }
}

namespace {

// Check for [0-9A-Z_a-z].
void EmitWordCheck(RegExpMacroAssembler* assembler, Label* word,
                   Label* non_word, bool fall_through_on_word) {
  if (assembler->CheckSpecialClassRanges(
          fall_through_on_word ? StandardCharacterSet::kWord
                               : StandardCharacterSet::kNotWord,
          fall_through_on_word ? non_word : word)) {
    // Optimized implementation available.
    return;
  }
  assembler->CheckCharacterGT('z', non_word);
  assembler->CheckCharacterLT('0', non_word);
  assembler->CheckCharacterGT('a' - 1, word);
  assembler->CheckCharacterLT('9' + 1, word);
  assembler->CheckCharacterLT('A', non_word);
  assembler->CheckCharacterLT('Z' + 1, word);
  if (fall_through_on_word) {
    assembler->CheckNotCharacter('_', non_word);
  } else {
    assembler->CheckCharacter('_', word);
  }
}

// Emit the code to check for a ^ in multiline mode (1-character lookbehind
// that matches newline or the start of input).
void EmitHat(RegExpCompiler* compiler, RegExpNode* on_success, Trace* trace) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();

  // We will load the previous character into the current character register.
  Trace new_trace(*trace);
  new_trace.InvalidateCurrentCharacter();

  // A positive (> 0) cp_offset means we've already successfully matched a
  // non-empty-width part of the pattern, and thus cannot be at or before the
  // start of the subject string. We can thus skip both at-start and
  // bounds-checks when loading the one-character lookbehind.
  const bool may_be_at_or_before_subject_string_start =
      new_trace.cp_offset() <= 0;

  Label ok;
  if (may_be_at_or_before_subject_string_start) {
    // The start of input counts as a newline in this context, so skip to ok if
    // we are at the start.
    assembler->CheckAtStart(new_trace.cp_offset(), &ok);
  }

  // If we've already checked that we are not at the start of input, it's okay
  // to load the previous character without bounds checks.
  const bool can_skip_bounds_check = !may_be_at_or_before_subject_string_start;
  assembler->LoadCurrentCharacter(new_trace.cp_offset() - 1,
                                  new_trace.backtrack(), can_skip_bounds_check);
  if (!assembler->CheckSpecialClassRanges(StandardCharacterSet::kLineTerminator,
                                          new_trace.backtrack())) {
    // Newline means \n, \r, 0x2028 or 0x2029.
    if (!compiler->one_byte()) {
      assembler->CheckCharacterAfterAnd(0x2028, 0xFFFE, &ok);
    }
    assembler->CheckCharacter('\n', &ok);
    assembler->CheckNotCharacter('\r', new_trace.backtrack());
  }
  assembler->Bind(&ok);
  on_success->Emit(compiler, &new_trace);
}

}  // namespace

// Emit the code to handle \b and \B (word-boundary or non-word-boundary).
void AssertionNode::EmitBoundaryCheck(RegExpCompiler* compiler, Trace* trace) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();
  Isolate* isolate = assembler->isolate();
  Trace::TriBool next_is_word_character = Trace::UNKNOWN;
  bool not_at_start = (trace->at_start() == Trace::FALSE_VALUE);
  BoyerMooreLookahead* lookahead = bm_info(not_at_start);
  if (lookahead == nullptr) {
    int eats_at_least =
        std::min(kMaxLookaheadForBoyerMoore, EatsAtLeast(not_at_start));
    if (eats_at_least >= 1) {
      BoyerMooreLookahead* bm =
          zone()->New<BoyerMooreLookahead>(eats_at_least, compiler, zone());
      FillInBMInfo(isolate, 0, kRecursionBudget, bm, not_at_start);
      if (bm->at(0)->is_non_word()) next_is_word_character = Trace::FALSE_VALUE;
      if (bm->at(0)->is_word()) next_is_word_character = Trace::TRUE_VALUE;
    }
  } else {
    if (lookahead->at(0)->is_non_word())
      next_is_word_character = Trace::FALSE_VALUE;
    if (lookahead->at(0)->is_word()) next_is_word_character = Trace::TRUE_VALUE;
  }
  bool at_boundary = (assertion_type_ == AssertionNode::AT_BOUNDARY);
  if (next_is_word_character == Trace::UNKNOWN) {
    Label before_non_word;
    Label before_word;
    if (trace->characters_preloaded() != 1) {
      assembler->LoadCurrentCharacter(trace->cp_offset(), &before_non_word);
    }
    // Fall through on non-word.
    EmitWordCheck(assembler, &before_word, &before_non_word, false);
    // Next character is not a word character.
    assembler->Bind(&before_non_word);
    Label ok;
    BacktrackIfPrevious(compiler, trace, at_boundary ? kIsNonWord : kIsWord);
    assembler->GoTo(&ok);

    assembler->Bind(&before_word);
    BacktrackIfPrevious(compiler, trace, at_boundary ? kIsWord : kIsNonWord);
    assembler->Bind(&ok);
  } else if (next_is_word_character == Trace::TRUE_VALUE) {
    BacktrackIfPrevious(compiler, trace, at_boundary ? kIsWord : kIsNonWord);
  } else {
    DCHECK(next_is_word_character == Trace::FALSE_VALUE);
    BacktrackIfPrevious(compiler, trace, at_boundary ? kIsNonWord : kIsWord);
  }
}

void AssertionNode::BacktrackIfPrevious(
    RegExpCompiler* compiler, Trace* trace,
    AssertionNode::IfPrevious backtrack_if_previous) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();
  Trace new_trace(*trace);
  new_trace.InvalidateCurrentCharacter();

  Label fall_through;
  Label* non_word = backtrack_if_previous == kIsNonWord ? new_trace.backtrack()
                                                        : &fall_through;
  Label* word = backtrack_if_previous == kIsNonWord ? &fall_through
                                                    : new_trace.backtrack();

  // A positive (> 0) cp_offset means we've already successfully matched a
  // non-empty-width part of the pattern, and thus cannot be at or before the
  // start of the subject string. We can thus skip both at-start and
  // bounds-checks when loading the one-character lookbehind.
  const bool may_be_at_or_before_subject_string_start =
      new_trace.cp_offset() <= 0;

  if (may_be_at_or_before_subject_string_start) {
    // The start of input counts as a non-word character, so the question is
    // decided if we are at the start.
    assembler->CheckAtStart(new_trace.cp_offset(), non_word);
  }

  // If we've already checked that we are not at the start of input, it's okay
  // to load the previous character without bounds checks.
  const bool can_skip_bounds_check = !may_be_at_or_before_subject_string_start;
  assembler->LoadCurrentCharacter(new_trace.cp_offset() - 1, non_word,
                                  can_skip_bounds_check);
  EmitWordCheck(assembler, word, non_word, backtrack_if_previous == kIsNonWord);

  assembler->Bind(&fall_through);
  on_success()->Emit(compiler, &new_trace);
}

void AssertionNode::GetQuickCheckDetails(QuickCheckDetails* details,
                                         RegExpCompiler* compiler,
                                         int filled_in, bool not_at_start) {
  if (assertion_type_ == AT_START && not_at_start) {
    details->set_cannot_match();
    return;
  }
  return on_success()->GetQuickCheckDetails(details, compiler, filled_in,
                                            not_at_start);
}

void AssertionNode::Emit(RegExpCompiler* compiler, Trace* trace) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();
  switch (assertion_type_) {
    case AT_END: {
      Label ok;
      assembler->CheckPosition(trace->cp_offset(), &ok);
      assembler->GoTo(trace->backtrack());
      assembler->Bind(&ok);
      break;
    }
    case AT_START: {
      if (trace->at_start() == Trace::FALSE_VALUE) {
        assembler->GoTo(trace->backtrack());
        return;
      }
      if (trace->at_start() == Trace::UNKNOWN) {
        assembler->CheckNotAtStart(trace->cp_offset(), trace->backtrack());
        Trace at_start_trace = *trace;
        at_start_trace.set_at_start(Trace::TRUE_VALUE);
        on_success()->Emit(compiler, &at_start_trace);
        return;
      }
    } break;
    case AFTER_NEWLINE:
      EmitHat(compiler, on_success(), trace);
      return;
    case AT_BOUNDARY:
    case AT_NON_BOUNDARY: {
      EmitBoundaryCheck(compiler, trace);
      return;
    }
  }
  on_success()->Emit(compiler, trace);
}

namespace {

bool DeterminedAlready(QuickCheckDetails* quick_check, int offset) {
  if (quick_check == nullptr) return false;
  if (offset >= quick_check->characters()) return false;
  return quick_check->positions(offset)->determines_perfectly;
}

void UpdateBoundsCheck(int index, int* checked_up_to) {
  if (index > *checked_up_to) {
    *checked_up_to = index;
  }
}

}  // namespace

// We call this repeatedly to generate code for each pass over the text node.
// The passes are in increasing order of difficulty because we hope one
// of the first passes will fail in which case we are saved the work of the
// later passes.  for example for the case independent regexp /%[asdfghjkl]a/
// we will check the '%' in the first pass, the case independent 'a' in the
// second pass and the character class in the last pass.
//
// The passes are done from right to left, so for example to test for /bar/
// we will first test for an 'r' with offset 2, then an 'a' with offset 1
// and then a 'b' with offset 0.  This means we can avoid the end-of-input
// bounds check most of the time.  In the example we only need to check for
// end-of-input when loading the putative 'r'.
//
// A slight complication involves the fact that the first character may already
// be fetched into a register by the previous node.  In this case we want to
// do the test for that character first.  We do this in separate passes.  The
// 'preloaded' argument indicates that we are doing such a 'pass'.  If such a
// pass has been performed then subsequent passes will have true in
// first_element_checked to indicate that that character does not need to be
// checked again.
//
// In addition to all this we are passed a Trace, which can
// contain an AlternativeGeneration object.  In this AlternativeGeneration
// object we can see details of any quick check that was already passed in
// order to get to the code we are now generating.  The quick check can involve
// loading characters, which means we do not need to recheck the bounds
// up to the limit the quick check already checked.  In addition the quick
// check can have involved a mask and compare operation which may simplify
// or obviate the need for further checks at some character positions.
void TextNode::TextEmitPass(RegExpCompiler* compiler, TextEmitPassType pass,
                            bool preloaded, Trace* trace,
                            bool first_element_checked, int* checked_up_to) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();
  Isolate* isolate = assembler->isolate();
  bool one_byte = compiler->one_byte();
  Label* backtrack = trace->backtrack();
  QuickCheckDetails* quick_check = trace->quick_check_performed();
  int element_count = elements()->length();
  int backward_offset = read_backward() ? -Length() : 0;
  for (int i = preloaded ? 0 : element_count - 1; i >= 0; i--) {
    TextElement elm = elements()->at(i);
    int cp_offset = trace->cp_offset() + elm.cp_offset() + backward_offset;
    if (elm.text_type() == TextElement::ATOM) {
      if (SkipPass(pass, IsIgnoreCase(compiler->flags()))) continue;
      base::Vector<const base::uc16> quarks = elm.atom()->data();
      for (int j = preloaded ? 0 : quarks.length() - 1; j >= 0; j--) {
        if (first_element_checked && i == 0 && j == 0) continue;
        if (DeterminedAlready(quick_check, elm.cp_offset() + j)) continue;
        base::uc16 quark = quarks[j];
        if (IsIgnoreCase(compiler->flags())) {
          // Everywhere else we assume that a non-Latin-1 character cannot match
          // a Latin-1 character. Avoid the cases where this is assumption is
          // invalid by using the Latin1 equivalent instead.
          quark = unibrow::Latin1::TryConvertToLatin1(quark);
        }
        bool needs_bounds_check =
            *checked_up_to < cp_offset + j || read_backward();
        bool bounds_checked = false;
        switch (pass) {
          case NON_LATIN1_MATCH:
            DCHECK(one_byte);
            if (quark > String::kMaxOneByteCharCode) {
              assembler->GoTo(backtrack);
              return;
            }
            break;
          case NON_LETTER_CHARACTER_MATCH:
            bounds_checked =
                EmitAtomNonLetter(isolate, compiler, quark, backtrack,
                                  cp_offset + j, needs_bounds_check, preloaded);
            break;
          case SIMPLE_CHARACTER_MATCH:
            bounds_checked = EmitSimpleCharacter(isolate, compiler, quark,
                                                 backtrack, cp_offset + j,
                                                 needs_bounds_check, preloaded);
            break;
          case CASE_CHARACTER_MATCH:
            bounds_checked =
                EmitAtomLetter(isolate, compiler, quark, backtrack,
                               cp_offset + j, needs_bounds_check, preloaded);
            break;
          default:
            break;
        }
        if (bounds_checked) UpdateBoundsCheck(cp_offset + j, checked_up_to);
      }
    } else {
      DCHECK_EQ(TextElement::CLASS_RANGES, elm.text_type());
      if (pass == CHARACTER_CLASS_MATCH) {
        if (first_element_checked && i == 0) continue;
        if (DeterminedAlready(quick_check, elm.cp_offset())) continue;
        RegExpClassRanges* cr = elm.class_ranges();
        bool bounds_check = *checked_up_to < cp_offset || read_backward();
        EmitClassRanges(assembler, cr, one_byte, backtrack, cp_offset,
                        bounds_check, preloaded, zone());
        UpdateBoundsCheck(cp_offset, checked_up_to);
      }
    }
  }
}

int TextNode::Length() {
  TextElement elm = elements()->last();
  DCHECK_LE(0, elm.cp_offset());
  return elm.cp_offset() + elm.length();
}

bool TextNode::SkipPass(TextEmitPassType pass, bool ignore_case) {
  if (ignore_case) {
    return pass == SIMPLE_CHARACTER_MATCH;
  } else {
    return pass == NON_LETTER_CHARACTER_MATCH || pass == CASE_CHARACTER_MATCH;
  }
}

TextNode* TextNode::CreateForCharacterRanges(Zone* zone,
                                             ZoneList<CharacterRange>* ranges,
                                             bool read_backward,
                                             RegExpNode* on_success) {
  DCHECK_NOT_NULL(ranges);
  // TODO(jgruber): There's no fundamental need to create this
  // RegExpClassRanges; we could refactor to avoid the allocation.
  return zone->New<TextNode>(zone->New<RegExpClassRanges>(zone, ranges),
                             read_backward, on_success);
}

TextNode* TextNode::CreateForSurrogatePair(
    Zone* zone, CharacterRange lead, ZoneList<CharacterRange>* trail_ranges,
    bool read_backward, RegExpNode* on_success) {
  ZoneList<CharacterRange>* lead_ranges = CharacterRange::List(zone, lead);
  ZoneList<TextElement>* elms = zone->New<ZoneList<TextElement>>(2, zone);
  elms->Add(
      TextElement::ClassRanges(zone->New<RegExpClassRanges>(zone, lead_ranges)),
      zone);
  elms->Add(TextElement::ClassRanges(
                zone->New<RegExpClassRanges>(zone, trail_ranges)),
            zone);
  return zone->New<TextNode>(elms, read_backward, on_success);
}

TextNode* TextNode::CreateForSurrogatePair(
    Zone* zone, ZoneList<CharacterRange>* lead_ranges, CharacterRange trail,
    bool read_backward, RegExpNode* on_success) {
  ZoneList<CharacterRange>* trail_ranges = CharacterRange::List(zone, trail);
  ZoneList<TextElement>* elms = zone->New<ZoneList<TextElement>>(2, zone);
  elms->Add(
      TextElement::ClassRanges(zone->New<RegExpClassRanges>(zone, lead_ranges)),
      zone);
  elms->Add(TextElement::ClassRanges(
                zone->New<RegExpClassRanges>(zone, trail_ranges)),
            zone);
  return zone->New<TextNode>(elms, read_backward, on_success);
}

// This generates the code to match a text node.  A text node can contain
// straight character sequences (possibly to be matched in a case-independent
// way) and character classes.  For efficiency we do not do this in a single
// pass from left to right.  Instead we pass over the text node several times,
// emitting code for some character positions every time.  See the comment on
// TextEmitPass for details.
void TextNode::Emit(RegExpCompiler* compiler, Trace* trace) {
  LimitResult limit_result = LimitVersions(compiler, trace);
  if (limit_result == DONE) return;
  DCHECK(limit_result == CONTINUE);

  if (trace->cp_offset() + Length() > RegExpMacroAssembler::kMaxCPOffset) {
    compiler->SetRegExpTooBig();
    return;
  }

  if (compiler->one_byte()) {
    int dummy = 0;
    TextEmitPass(compiler, NON_LATIN1_MATCH, false, trace, false, &dummy);
  }

  bool first_elt_done = false;
  int bound_checked_to = trace->cp_offset() - 1;
  bound_checked_to += trace->bound_checked_up_to();

  // If a character is preloaded into the current character register then
  // check that now.
  if (trace->characters_preloaded() == 1) {
    for (int pass = kFirstRealPass; pass <= kLastPass; pass++) {
      TextEmitPass(compiler, static_cast<TextEmitPassType>(pass), true, trace,
                   false, &bound_checked_to);
    }
    first_elt_done = true;
  }

  for (int pass = kFirstRealPass; pass <= kLastPass; pass++) {
    TextEmitPass(compiler, static_cast<TextEmitPassType>(pass), false, trace,
                 first_elt_done, &bound_checked_to);
  }

  Trace successor_trace(*trace);
  // If we advance backward, we may end up at the start.
  successor_trace.AdvanceCurrentPositionInTrace(
      read_backward() ? -Length() : Length(), compiler);
  successor_trace.set_at_start(read_backward() ? Trace::UNKNOWN
                                               : Trace::FALSE_VALUE);
  RecursionCheck rc(compiler);
  on_success()->Emit(compiler, &successor_trace);
}

void Trace::InvalidateCurrentCharacter() { characters_preloaded_ = 0; }

void Trace::AdvanceCurrentPositionInTrace(int by, RegExpCompiler* compiler) {
  // We don't have an instruction for shifting the current character register
  // down or for using a shifted value for anything so lets just forget that
  // we preloaded any characters into it.
  characters_preloaded_ = 0;
  // Adjust the offsets of the quick check performed information.  This
  // information is used to find out what we already determined about the
  // characters by means of mask and compare.
  quick_check_performed_.Advance(by, compiler->one_byte());
  cp_offset_ += by;
  if (cp_offset_ > RegExpMacroAssembler::kMaxCPOffset) {
    compiler->SetRegExpTooBig();
    cp_offset_ = 0;
  }
  bound_checked_up_to_ = std::max(0, bound_checked_up_to_ - by);
}

void TextNode::MakeCaseIndependent(Isolate* isolate, bool is_one_byte,
                                   RegExpFlags flags) {
  if (!IsIgnoreCase(flags)) return;
#ifdef V8_INTL_SUPPORT
  if (NeedsUnicodeCaseEquivalents(flags)) return;
#endif

  int element_count = elements()->length();
  for (int i = 0; i < element_count; i++) {
    TextElement elm = elements()->at(i);
    if (elm.text_type() == TextElement::CLASS_RANGES) {
      RegExpClassRanges* cr = elm.class_ranges();
      // None of the standard character classes is different in the case
      // independent case and it slows us down if we don't know that.
      if (cr->is_standard(zone())) continue;
      ZoneList<CharacterRange>* ranges = cr->ranges(zone());
      CharacterRange::AddCaseEquivalents(isolate, zone(), ranges, is_one_byte);
    }
  }
}

int TextNode::GreedyLoopTextLength() { return Length(); }

RegExpNode* TextNode::GetSuccessorOfOmnivorousTextNode(
    RegExpCompiler* compiler) {
  if (read_backward()) return nullptr;
  if (elements()->length() != 1) return nullptr;
  TextElement elm = elements()->at(0);
  if (elm.text_type() != TextElement::CLASS_RANGES) return nullptr;
  RegExpClassRanges* node = elm.class_ranges();
  ZoneList<CharacterRange>* ranges = node->ranges(zone());
  CharacterRange::Canonicalize(ranges);
  if (node->is_negated()) {
    return ranges->length() == 0 ? on_success() : nullptr;
  }
  if (ranges->length() != 1) return nullptr;
  const base::uc32 max_char = MaxCodeUnit(compiler->one_byte());
  return ranges->at(0).IsEverything(max_char) ? on_success() : nullptr;
}

// Finds the fixed match length of a sequence of nodes that goes from
// this alternative and back to this choice node.  If there are variable
// length nodes or other complications in the way then return a sentinel
// value indicating that a greedy loop cannot be constructed.
int ChoiceNode::GreedyLoopTextLengthForAlternative(
    GuardedAlternative* alternative) {
  int length = 0;
  RegExpNode* node = alternative->node();
  // Later we will generate code for all these text nodes using recursion
  // so we have to limit the max number.
  int recursion_depth = 0;
  while (node != this) {
    if (recursion_depth++ > RegExpCompiler::kMaxRecursion) {
      return kNodeIsTooComplexForGreedyLoops;
    }
    int node_length = node->GreedyLoopTextLength();
    if (node_length == kNodeIsTooComplexForGreedyLoops) {
      return kNodeIsTooComplexForGreedyLoops;
    }
    length += node_length;
    SeqRegExpNode* seq_node = static_cast<SeqRegExpNode*>(node);
    node = seq_node->on_success();
  }
  if (read_backward()) {
    length = -length;
  }
  // Check that we can jump by the whole text length. If not, return sentinel
  // to indicate the we can't construct a greedy loop.
  if (length < RegExpMacroAssembler::kMinCPOffset ||
      length > RegExpMacroAssembler::kMaxCPOffset) {
    return kNodeIsTooComplexForGreedyLoops;
  }
  return length;
}

void LoopChoiceNode::AddLoopAlternative(GuardedAlternative alt) {
  DCHECK_NULL(loop_node_);
  AddAlternative(alt);
  loop_node_ = alt.node();
}

void LoopChoiceNode::AddContinueAlternative(GuardedAlternative alt) {
  DCHECK_NULL(continue_node_);
  AddAlternative(alt);
  continue_node_ = alt.node();
}

void LoopChoiceNode::Emit(RegExpCompiler* compiler, Trace* trace) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  if (trace->stop_node() == this) {
    // Back edge of greedy optimized loop node graph.
    int text_length =
        GreedyLoopTextLengthForAlternative(&(alternatives_->at(0)));
    DCHECK_NE(kNodeIsTooComplexForGreedyLoops, text_length);
    // Update the counter-based backtracking info on the stack.  This is an
    // optimization for greedy loops (see below).
    DCHECK(trace->cp_offset() == text_length);
    macro_assembler->AdvanceCurrentPosition(text_length);
    macro_assembler->GoTo(trace->loop_label());
    return;
  }
  DCHECK_NULL(trace->stop_node());
  if (!trace->is_trivial()) {
    trace->Flush(compiler, this);
    return;
  }
  ChoiceNode::Emit(compiler, trace);
}

int ChoiceNode::CalculatePreloadCharacters(RegExpCompiler* compiler,
                                           int eats_at_least) {
  int preload_characters = std::min(4, eats_at_least);
  DCHECK_LE(preload_characters, 4);
  if (compiler->macro_assembler()->CanReadUnaligned()) {
    bool one_byte = compiler->one_byte();
    if (one_byte) {
      // We can't preload 3 characters because there is no machine instruction
      // to do that.  We can't just load 4 because we could be reading
      // beyond the end of the string, which could cause a memory fault.
      if (preload_characters == 3) preload_characters = 2;
    } else {
      if (preload_characters > 2) preload_characters = 2;
    }
  } else {
    if (preload_characters > 1) preload_characters = 1;
  }
  return preload_characters;
}

// This class is used when generating the alternatives in a choice node.  It
// records the way the alternative is being code generated.
class AlternativeGeneration : public Malloced {
 public:
  AlternativeGeneration()
      : possible_success(),
        expects_preload(false),
        after(),
        quick_check_details() {}
  Label possible_success;
  bool expects_preload;
  Label after;
  QuickCheckDetails quick_check_details;
};

// Creates a list of AlternativeGenerations.  If the list has a reasonable
// size then it is on the stack, otherwise the excess is on the heap.
class AlternativeGenerationList {
 public:
  AlternativeGenerationList(int count, Zone* zone) : alt_gens_(count, zone) {
    for (int i = 0; i < count && i < kAFew; i++) {
      alt_gens_.Add(a_few_alt_gens_ + i, zone);
    }
    for (int i = kAFew; i < count; i++) {
      alt_gens_.Add(new AlternativeGeneration(), zone);
    }
  }
  ~AlternativeGenerationList() {
    for (int i = kAFew; i < alt_gens_.length(); i++) {
      delete alt_gens_[i];
      alt_gens_[i] = nullptr;
    }
  }

  AlternativeGeneration* at(int i) { return alt_gens_[i]; }

 private:
  static const int kAFew = 10;
  ZoneList<AlternativeGeneration*> alt_gens_;
  AlternativeGeneration a_few_alt_gens_[kAFew];
};

void BoyerMoorePositionInfo::Set(int character) {
  SetInterval(Interval(character, character));
}

namespace {

ContainedInLattice AddRange(ContainedInLattice containment, const int* ranges,
                            int ranges_length, Interval new_range) {
  DCHECK_EQ(1, ranges_length & 1);
  DCHECK_EQ(String::kMaxCodePoint + 1, ranges[ranges_length - 1]);
  if (containment == kLatticeUnknown) return containment;
  bool inside = false;
  int last = 0;
  for (int i = 0; i < ranges_length; inside = !inside, last = ranges[i], i++) {
    // Consider the range from last to ranges[i].
    // We haven't got to the new range yet.
    if (ranges[i] <= new_range.from()) continue;
    // New range is wholly inside last-ranges[i].  Note that new_range.to() is
    // inclusive, but the values in ranges are not.
    if (last <= new_range.from() && new_range.to() < ranges[i]) {
      return Combine(containment, inside ? kLatticeIn : kLatticeOut);
    }
    return kLatticeUnknown;
  }
  return containment;
}

int BitsetFirstSetBit(BoyerMoorePositionInfo::Bitset bitset) {
  static_assert(BoyerMoorePositionInfo::kMapSize ==
                2 * kInt64Size * kBitsPerByte);

  // Slight fiddling is needed here, since the bitset is of length 128 while
  // CountTrailingZeros requires an integral type and std::bitset can only
  // convert to unsigned long long. So we handle the most- and least-significant
  // bits separately.

  {
    static constexpr BoyerMoorePositionInfo::Bitset mask(~uint64_t{0});
    BoyerMoorePositionInfo::Bitset masked_bitset = bitset & mask;
    static_assert(kInt64Size >= sizeof(decltype(masked_bitset.to_ullong())));
    uint64_t lsb = masked_bitset.to_ullong();
    if (lsb != 0) return base::bits::CountTrailingZeros(lsb);
  }

  {
    BoyerMoorePositionInfo::Bitset masked_bitset = bitset >> 64;
    uint64_t msb = masked_bitset.to_ullong();
    if (msb != 0) return 64 + base::bits::CountTrailingZeros(msb);
  }

  return -1;
}

}  // namespace

void BoyerMoorePositionInfo::SetInterval(const Interval& interval) {
  w_ = AddRange(w_, kWordRanges, kWordRangeCount, interval);

  if (interval.size() >= kMapSize) {
    map_count_ = kMapSize;
    map_.set();
    return;
  }

  for (int i = interval.from(); i <= interval.to(); i++) {
    int mod_character = (i & kMask);
    if (!map_[mod_character]) {
      map_count_++;
      map_.set(mod_character);
    }
    if (map_count_ == kMapSize) return;
  }
}

void BoyerMoorePositionInfo::SetAll() {
  w_ = kLatticeUnknown;
  if (map_count_ != kMapSize) {
    map_count_ = kMapSize;
    map_.set();
  }
}

BoyerMooreLookahead::BoyerMooreLookahead(int length, RegExpCompiler* compiler,
                                         Zone* zone)
    : length_(length),
      compiler_(compiler),
      max_char_(MaxCodeUnit(compiler->one_byte())) {
  bitmaps_ = zone->New<ZoneList<BoyerMoorePositionInfo*>>(length, zone);
  for (int i = 0; i < length; i++) {
    bitmaps_->Add(zone->New<BoyerMoorePositionInfo>(), zone);
  }
}

// Find the longest range of lookahead that has the fewest number of different
// characters that can occur at a given position.  Since we are optimizing two
// different parameters at once this is a tradeoff.
bool BoyerMooreLookahead::FindWorthwhileInterval(int* from, int* to) {
  int biggest_points = 0;
  // If more than 32 characters out of 128 can occur it is unlikely that we can
  // be lucky enough to step forwards much of the time.
  const int kMaxMax = 32;
  for (int max_number_of_chars = 4; max_number_of_chars < kMaxMax;
       max_number_of_chars *= 2) {
    biggest_points =
        FindBestInterval(max_number_of_chars, biggest_points, from, to);
  }
  if (biggest_points == 0) return false;
  return true;
}

// Find the highest-points range between 0 and length_ where the character
// information is not too vague.  'Too vague' means that there are more than
// max_number_of_chars that can occur at this position.  Calculates the number
// of points as the product of width-of-the-range and
// probability-of-finding-one-of-the-characters, where the probability is
// calculated using the frequency distribution of the sample subject string.
int BoyerMooreLookahead::FindBestInterval(int max_number_of_chars,
                                          int old_biggest_points, int* from,
                                          int* to) {
  int biggest_points = old_biggest_points;
  static const int kSize = RegExpMacroAssembler::kTableSize;
  for (int i = 0; i < length_;) {
    while (i < length_ && Count(i) > max_number_of_chars) i++;
    if (i == length_) break;
    int remembered_from = i;

    BoyerMoorePositionInfo::Bitset union_bitset;
    for (; i < length_ && Count(i) <= max_number_of_chars; i++) {
      union_bitset |= bitmaps_->at(i)->raw_bitset();
    }

    int frequency = 0;

    // Iterate only over set bits.
    int j;
    while ((j = BitsetFirstSetBit(union_bitset)) != -1) {
      DCHECK(union_bitset[j]);  // Sanity check.
      // Add 1 to the frequency to give a small per-character boost for
      // the cases where our sampling is not good enough and many
      // characters have a frequency of zero.  This means the frequency
      // can theoretically be up to 2*kSize though we treat it mostly as
      // a fraction of kSize.
      frequency += compiler_->frequency_collator()->Frequency(j) + 1;
      union_bitset.reset(j);
    }

    // We use the probability of skipping times the distance we are skipping to
    // judge the effectiveness of this.  Actually we have a cut-off:  By
    // dividing by 2 we switch off the skipping if the probability of skipping
    // is less than 50%.  This is because the multibyte mask-and-compare
    // skipping in quickcheck is more likely to do well on this case.
    bool in_quickcheck_range =
        ((i - remembered_from < 4) ||
         (compiler_->one_byte() ? remembered_from <= 4 : remembered_from <= 2));
    // Called 'probability' but it is only a rough estimate and can actually
    // be outside the 0-kSize range.
    int probability = (in_quickcheck_range ? kSize / 2 : kSize) - frequency;
    int points = (i - remembered_from) * probability;
    if (points > biggest_points) {
      *from = remembered_from;
      *to = i - 1;
      biggest_points = points;
    }
  }
  return biggest_points;
}

// Take all the characters that will not prevent a successful match if they
// occur in the subject string in the range between min_lookahead and
// max_lookahead (inclusive) measured from the current position.  If the
// character at max_lookahead offset is not one of these characters, then we
// can safely skip forwards by the number of characters in the range.
int BoyerMooreLookahead::GetSkipTable(int min_lookahead, int max_lookahead,
                                      Handle<ByteArray> boolean_skip_table) {
  const int kSkipArrayEntry = 0;
  const int kDontSkipArrayEntry = 1;

  std::memset(boolean_skip_table->begin(), kSkipArrayEntry,
              boolean_skip_table->length());

  for (int i = max_lookahead; i >= min_lookahead; i--) {
    BoyerMoorePositionInfo::Bitset bitset = bitmaps_->at(i)->raw_bitset();

    // Iterate only over set bits.
    int j;
    while ((j = BitsetFirstSetBit(bitset)) != -1) {
      DCHECK(bitset[j]);  // Sanity check.
      boolean_skip_table->set(j, kDontSkipArrayEntry);
      bitset.reset(j);
    }
  }

  const int skip = max_lookahead + 1 - min_lookahead;
  return skip;
}

// See comment above on the implementation of GetSkipTable.
void BoyerMooreLookahead::EmitSkipInstructions(RegExpMacroAssembler* masm) {
  const int kSize = RegExpMacroAssembler::kTableSize;

  int min_lookahead = 0;
  int max_lookahead = 0;

  if (!FindWorthwhileInterval(&min_lookahead, &max_lookahead)) return;

  // Check if we only have a single non-empty position info, and that info
  // contains precisely one character.
  bool found_single_character = false;
  int single_character = 0;
  for (int i = max_lookahead; i >= min_lookahead; i--) {
    BoyerMoorePositionInfo* map = bitmaps_->at(i);
    if (map->map_count() == 0) continue;

    if (found_single_character || map->map_count() > 1) {
      found_single_character = false;
      break;
    }

    DCHECK(!found_single_character);
    DCHECK_EQ(map->map_count(), 1);

    found_single_character = true;
    single_character = BitsetFirstSetBit(map->raw_bitset());

    DCHECK_NE(single_character, -1);
  }

  int lookahead_width = max_lookahead + 1 - min_lookahead;

  if (found_single_character && lookahead_width == 1 && max_lookahead < 3) {
    // The mask-compare can probably handle this better.
    return;
  }

  if (found_single_character) {
    Label cont, again;
    masm->Bind(&again);
    masm->LoadCurrentCharacter(max_lookahead, &cont, true);
    if (max_char_ > kSize) {
      masm->CheckCharacterAfterAnd(single_character,
                                   RegExpMacroAssembler::kTableMask, &cont);
    } else {
      masm->CheckCharacter(single_character, &cont);
    }
    masm->AdvanceCurrentPosition(lookahead_width);
    masm->GoTo(&again);
    masm->Bind(&cont);
    return;
  }

  Factory* factory = masm->isolate()->factory();
  Handle<ByteArray> boolean_skip_table =
      factory->NewByteArray(kSize, AllocationType::kOld);
  int skip_distance =
      GetSkipTable(min_lookahead, max_lookahead, boolean_skip_table);
  DCHECK_NE(0, skip_distance);

  Label cont, again;
  masm->Bind(&again);
  masm->LoadCurrentCharacter(max_lookahead, &cont, true);
  masm->CheckBitInTable(boolean_skip_table, &cont);
  masm->AdvanceCurrentPosition(skip_distance);
  masm->GoTo(&again);
  masm->Bind(&cont);
}

/* Code generation for choice nodes.
 *
 * We generate quick checks that do a mask and compare to eliminate a
 * choice.  If the quick check succeeds then it jumps to the continuation to
 * do slow checks and check subsequent nodes.  If it fails (the common case)
 * it falls through to the next choice.
 *
 * Here is the desired flow graph.  Nodes directly below each other imply
 * fallthrough.  Alternatives 1 and 2 have quick checks.  Alternative
 * 3 doesn't have a quick check so we have to call the slow check.
 * Nodes are marked Qn for quick checks and Sn for slow checks.  The entire
 * regexp continuation is generated directly after the Sn node, up to the
 * next GoTo if we decide to reuse some already generated code.  Some
 * nodes expect preload_characters to be preloaded into the current
 * character register.  R nodes do this preloading.  Vertices are marked
 * F for failures and S for success (possible success in the case of quick
 * nodes).  L, V, < and > are used as arrow heads.
 *
 * ----------> R
 *             |
 *             V
 *            Q1 -----> S1
 *             |   S   /
 *            F|      /
 *             |    F/
 *             |    /
 *             |   R
 *             |  /
 *             V L
 *            Q2 -----> S2
 *             |   S   /
 *            F|      /
 *             |    F/
 *             |    /
 *             |   R
 *             |  /
 *             V L
 *            S3
 *             |
 *            F|
 *             |
 *             R
 *             |
 * backtrack   V
 * <----------Q4
 *   \    F    |
 *    \        |S
 *     \   F   V
 *      \-----S4
 *
 * For greedy loops we push the current position, then generate the code that
 * eats the input specially in EmitGreedyLoop.  The other choice (the
 * continuation) is generated by the normal code in EmitChoices, and steps back
 * in the input to the starting position when it fails to match.  The loop code
 * looks like this (U is the unwind code that steps back in the greedy loop).
 *
 *              _____
 *             /     \
 *             V     |
 * ----------> S1    |
 *            /|     |
 *           / |S    |
 *         F/  \_____/
 *         /
 *        |<-----
 *        |      \
 *        V       |S
 *        Q2 ---> U----->backtrack
 *        |  F   /
 *       S|     /
 *        V  F /
 *        S2--/
 */

GreedyLoopState::GreedyLoopState(bool not_at_start) {
  counter_backtrack_trace_.set_backtrack(&label_);
  if (not_at_start) counter_backtrack_trace_.set_at_start(Trace::FALSE_VALUE);
}

void ChoiceNode::AssertGuardsMentionRegisters(Trace* trace) {
#ifdef DEBUG
  int choice_count = alternatives_->length();
  for (int i = 0; i < choice_count - 1; i++) {
    GuardedAlternative alternative = alternatives_->at(i);
    ZoneList<Guard*>* guards = alternative.guards();
    int guard_count = (guards == nullptr) ? 0 : guards->length();
    for (int j = 0; j < guard_count; j++) {
      DCHECK(!trace->mentions_reg(guards->at(j)->reg()));
    }
  }
#endif
}

void ChoiceNode::SetUpPreLoad(RegExpCompiler* compiler, Trace* current_trace,
                              PreloadState* state) {
  if (state->eats_at_least_ == PreloadState::kEatsAtLeastNotYetInitialized) {
    // Save some time by looking at most one machine word ahead.
    state->eats_at_least_ =
        EatsAtLeast(current_trace->at_start() == Trace::FALSE_VALUE);
  }
  state->preload_characters_ =
      CalculatePreloadCharacters(compiler, state->eats_at_least_);

  state->preload_is_current_ =
      (current_trace->characters_preloaded() == state->preload_characters_);
  state->preload_has_checked_bounds_ = state->preload_is_current_;
}

void ChoiceNode::Emit(RegExpCompiler* compiler, Trace* trace) {
  int choice_count = alternatives_->length();

  if (choice_count == 1 && alternatives_->at(0).guards() == nullptr) {
    alternatives_->at(0).node()->Emit(compiler, trace);
    return;
  }

  AssertGuardsMentionRegisters(trace);

  LimitResult limit_result = LimitVersions(compiler, trace);
  if (limit_result == DONE) return;
  DCHECK(limit_result == CONTINUE);

  // For loop nodes we already flushed (see LoopChoiceNode::Emit), but for
  // other choice nodes we only flush if we are out of code size budget.
  if (trace->flush_budget() == 0 && trace->actions() != nullptr) {
    trace->Flush(compiler, this);
    return;
  }

  RecursionCheck rc(compiler);

  PreloadState preload;
  preload.init();
  GreedyLoopState greedy_loop_state(not_at_start());

  int text_length = GreedyLoopTextLengthForAlternative(&alternatives_->at(0));
  AlternativeGenerationList alt_gens(choice_count, zone());

  if (choice_count > 1 && text_length != kNodeIsTooComplexForGreedyLoops) {
    trace = EmitGreedyLoop(compiler, trace, &alt_gens, &preload,
                           &greedy_loop_state, text_length);
  } else {
    // TODO(erikcorry): Delete this.  We don't need this label, but it makes us
    // match the traces produced pre-cleanup.
    Label second_choice;
    compiler->macro_assembler()->Bind(&second_choice);

    preload.eats_at_least_ = EmitOptimizedUnanchoredSearch(compiler, trace);

    EmitChoices(compiler, &alt_gens, 0, trace, &preload);
  }

  // At this point we need to generate slow checks for the alternatives where
  // the quick check was inlined.  We can recognize these because the associated
  // label was bound.
  int new_flush_budget = trace->flush_budget() / choice_count;
  for (int i = 0; i < choice_count; i++) {
    AlternativeGeneration* alt_gen = alt_gens.at(i);
    Trace new_trace(*trace);
    // If there are actions to be flushed we have to limit how many times
    // they are flushed.  Take the budget of the parent trace and distribute
    // it fairly amongst the children.
    if (new_trace.actions() != nullptr) {
      new_trace.set_flush_budget(new_flush_budget);
    }
    bool next_expects_preload =
        i == choice_count - 1 ? false : alt_gens.at(i + 1)->expects_preload;
    EmitOutOfLineContinuation(compiler, &new_trace, alternatives_->at(i),
                              alt_gen, preload.preload_characters_,
                              next_expects_preload);
  }
}

Trace* ChoiceNode::EmitGreedyLoop(RegExpCompiler* compiler, Trace* trace,
                                  AlternativeGenerationList* alt_gens,
                                  PreloadState* preload,
                                  GreedyLoopState* greedy_loop_state,
                                  int text_length) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  // Here we have special handling for greedy loops containing only text nodes
  // and other simple nodes.  These are handled by pushing the current
  // position on the stack and then incrementing the current position each
  // time around the switch.  On backtrack we decrement the current position
  // and check it against the pushed value.  This avoids pushing backtrack
  // information for each iteration of the loop, which could take up a lot of
  // space.
  DCHECK(trace->stop_node() == nullptr);
  macro_assembler->PushCurrentPosition();
  Label greedy_match_failed;
  Trace greedy_match_trace;
  if (not_at_start()) greedy_match_trace.set_at_start(Trace::FALSE_VALUE);
  greedy_match_trace.set_backtrack(&greedy_match_failed);
  Label loop_label;
  macro_assembler->Bind(&loop_label);
  greedy_match_trace.set_stop_node(this);
  greedy_match_trace.set_loop_label(&loop_label);
  alternatives_->at(0).node()->Emit(compiler, &greedy_match_trace);
  macro_assembler->Bind(&greedy_match_failed);

  Label second_choice;  // For use in greedy matches.
  macro_assembler->Bind(&second_choice);

  Trace* new_trace = greedy_loop_state->counter_backtrack_trace();

  EmitChoices(compiler, alt_gens, 1, new_trace, preload);

  macro_assembler->Bind(greedy_loop_state->label());
  // If we have unwound to the bottom then backtrack.
  macro_assembler->CheckGreedyLoop(trace->backtrack());
  // Otherwise try the second priority at an earlier position.
  macro_assembler->AdvanceCurrentPosition(-text_length);
  macro_assembler->GoTo(&second_choice);
  return new_trace;
}

int ChoiceNode::EmitOptimizedUnanchoredSearch(RegExpCompiler* compiler,
                                              Trace* trace) {
  int eats_at_least = PreloadState::kEatsAtLeastNotYetInitialized;
  if (alternatives_->length() != 2) return eats_at_least;

  GuardedAlternative alt1 = alternatives_->at(1);
  if (alt1.guards() != nullptr && alt1.guards()->length() != 0) {
    return eats_at_least;
  }
  RegExpNode* eats_anything_node = alt1.node();
  if (eats_anything_node->GetSuccessorOfOmnivorousTextNode(compiler) != this) {
    return eats_at_least;
  }

  // Really we should be creating a new trace when we execute this function,
  // but there is no need, because the code it generates cannot backtrack, and
  // we always arrive here with a trivial trace (since it's the entry to a
  // loop.  That also implies that there are no preloaded characters, which is
  // good, because it means we won't be violating any assumptions by
  // overwriting those characters with new load instructions.
  DCHECK(trace->is_trivial());

  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  Isolate* isolate = macro_assembler->isolate();
  // At this point we know that we are at a non-greedy loop that will eat
  // any character one at a time.  Any non-anchored regexp has such a
  // loop prepended to it in order to find where it starts.  We look for
  // a pattern of the form ...abc... where we can look 6 characters ahead
  // and step forwards 3 if the character is not one of abc.  Abc need
  // not be atoms, they can be any reasonably limited character class or
  // small alternation.
  BoyerMooreLookahead* bm = bm_info(false);
  if (bm == nullptr) {
    eats_at_least = std::min(kMaxLookaheadForBoyerMoore, EatsAtLeast(false));
    if (eats_at_least >= 1) {
      bm = zone()->New<BoyerMooreLookahead>(eats_at_least, compiler, zone());
      GuardedAlternative alt0 = alternatives_->at(0);
      alt0.node()->FillInBMInfo(isolate, 0, kRecursionBudget, bm, false);
    }
  }
  if (bm != nullptr) {
    bm->EmitSkipInstructions(macro_assembler);
  }
  return eats_at_least;
}

void ChoiceNode::EmitChoices(RegExpCompiler* compiler,
                             AlternativeGenerationList* alt_gens,
                             int first_choice, Trace* trace,
                             PreloadState* preload) {
  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  SetUpPreLoad(compiler, trace, preload);

  // For now we just call all choices one after the other.  The idea ultimately
  // is to use the Dispatch table to try only the relevant ones.
  int choice_count = alternatives_->length();

  int new_flush_budget = trace->flush_budget() / choice_count;

  for (int i = first_choice; i < choice_count; i++) {
    bool is_last = i == choice_count - 1;
    bool fall_through_on_failure = !is_last;
    GuardedAlternative alternative = alternatives_->at(i);
    AlternativeGeneration* alt_gen = alt_gens->at(i);
    alt_gen->quick_check_details.set_characters(preload->preload_characters_);
    ZoneList<Guard*>* guards = alternative.guards();
    int guard_count = (guards == nullptr) ? 0 : guards->length();
    Trace new_trace(*trace);
    new_trace.set_characters_preloaded(
        preload->preload_is_current_ ? preload->preload_characters_ : 0);
    if (preload->preload_has_checked_bounds_) {
      new_trace.set_bound_checked_up_to(preload->preload_characters_);
    }
    new_trace.quick_check_performed()->Clear();
    if (not_at_start_) new_trace.set_at_start(Trace::FALSE_VALUE);
    if (!is_last) {
      new_trace.set_backtrack(&alt_gen->after);
    }
    alt_gen->expects_preload = preload->preload_is_current_;
    bool generate_full_check_inline = false;
    if (compiler->optimize() &&
        try_to_emit_quick_check_for_alternative(i == 0) &&
        alternative.node()->EmitQuickCheck(
            compiler, trace, &new_trace, preload->preload_has_checked_bounds_,
            &alt_gen->possible_success, &alt_gen->quick_check_details,
            fall_through_on_failure, this)) {
      // Quick check was generated for this choice.
      preload->preload_is_current_ = true;
      preload->preload_has_checked_bounds_ = true;
      // If we generated the quick check to fall through on possible success,
      // we now need to generate the full check inline.
      if (!fall_through_on_failure) {
        macro_assembler->Bind(&alt_gen->possible_success);
        new_trace.set_quick_check_performed(&alt_gen->quick_check_details);
        new_trace.set_characters_preloaded(preload->preload_characters_);
        new_trace.set_bound_checked_up_to(preload->preload_characters_);
        generate_full_check_inline = true;
      }
    } else if (alt_gen->quick_check_details.cannot_match()) {
      if (!fall_through_on_failure) {
        macro_assembler->GoTo(trace->backtrack());
      }
      continue;
    } else {
      // No quick check was generated.  Put the full code here.
      // If this is not the first choice then there could be slow checks from
      // previous cases that go here when they fail.  There's no reason to
      // insist that they preload characters since the slow check we are about
      // to generate probably can't use it.
      if (i != first_choice) {
        alt_gen->expects_preload = false;
        new_trace.InvalidateCurrentCharacter();
      }
      generate_full_check_inline = true;
    }
    if (generate_full_check_inline) {
      if (new_trace.actions() != nullptr) {
        new_trace.set_flush_budget(new_flush_budget);
      }
      for (int j = 0; j < guard_count; j++) {
        GenerateGuard(macro_assembler, guards->at(j), &new_trace);
      }
      alternative.node()->Emit(compiler, &new_trace);
      preload->preload_is_current_ = false;
    }
    macro_assembler->Bind(&alt_gen->after);
  }
}

void ChoiceNode::EmitOutOfLineContinuation(RegExpCompiler* compiler,
                                           Trace* trace,
                                           GuardedAlternative alternative,
                                           AlternativeGeneration* alt_gen,
                                           int preload_characters,
                                           bool next_expects_preload) {
  if (!alt_gen->possible_success.is_linked()) return;

  RegExpMacroAssembler* macro_assembler = compiler->macro_assembler();
  macro_assembler->Bind(&alt_gen->possible_success);
  Trace out_of_line_trace(*trace);
  out_of_line_trace.set_characters_preloaded(preload_characters);
  out_of_line_trace.set_quick_check_performed(&alt_gen->quick_check_details);
  if (not_at_start_) out_of_line_trace.set_at_start(Trace::FALSE_VALUE);
  ZoneList<Guard*>* guards = alternative.guards();
  int guard_count = (guards == nullptr) ? 0 : guards->length();
  if (next_expects_preload) {
    Label reload_current_char;
    out_of_line_trace.set_backtrack(&reload_current_char);
    for (int j = 0; j < guard_count; j++) {
      GenerateGuard(macro_assembler, guards->at(j), &out_of_line_trace);
    }
    alternative.node()->Emit(compiler, &out_of_line_trace);
    macro_assembler->Bind(&reload_current_char);
    // Reload the current character, since the next quick check expects that.
    // We don't need to check bounds here because we only get into this
    // code through a quick check which already did the checked load.
    macro_assembler->LoadCurrentCharacter(trace->cp_offset(), nullptr, false,
                                          preload_characters);
    macro_assembler->GoTo(&(alt_gen->after));
  } else {
    out_of_line_trace.set_backtrack(&(alt_gen->after));
    for (int j = 0; j < guard_count; j++) {
      GenerateGuard(macro_assembler, guards->at(j), &out_of_line_trace);
    }
    alternative.node()->Emit(compiler, &out_of_line_trace);
  }
}

void ActionNode::Emit(RegExpCompiler* compiler, Trace* trace) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();
  LimitResult limit_result = LimitVersions(compiler, trace);
  if (limit_result == DONE) return;
  DCHECK(limit_result == CONTINUE);

  RecursionCheck rc(compiler);

  switch (action_type_) {
    case STORE_POSITION: {
      Trace::DeferredCapture new_capture(data_.u_position_register.reg,
                                         data_.u_position_register.is_capture,
                                         trace);
      Trace new_trace = *trace;
      new_trace.add_action(&new_capture);
      on_success()->Emit(compiler, &new_trace);
      break;
    }
    case INCREMENT_REGISTER: {
      Trace::DeferredIncrementRegister new_increment(
          data_.u_increment_register.reg);
      Trace new_trace = *trace;
      new_trace.add_action(&new_increment);
      on_success()->Emit(compiler, &new_trace);
      break;
    }
    case SET_REGISTER_FOR_LOOP: {
      Trace::DeferredSetRegisterForLoop new_set(data_.u_store_register.reg,
                                                data_.u_store_register.value);
      Trace new_trace = *trace;
      new_trace.add_action(&new_set);
      on_success()->Emit(compiler, &new_trace);
      break;
    }
    case CLEAR_CAPTURES: {
      Trace::DeferredClearCaptures new_capture(Interval(
          data_.u_clear_captures.range_from, data_.u_clear_captures.range_to));
      Trace new_trace = *trace;
      new_trace.add_action(&new_capture);
      on_success()->Emit(compiler, &new_trace);
      break;
    }
    case BEGIN_POSITIVE_SUBMATCH:
    case BEGIN_NEGATIVE_SUBMATCH:
      if (!trace->is_trivial()) {
        trace->Flush(compiler, this);
      } else {
        assembler->WriteCurrentPositionToRegister(
            data_.u_submatch.current_position_register, 0);
        assembler->WriteStackPointerToRegister(
            data_.u_submatch.stack_pointer_register);
        on_success()->Emit(compiler, trace);
      }
      break;
    case EMPTY_MATCH_CHECK: {
      int start_pos_reg = data_.u_empty_match_check.start_register;
      int stored_pos = 0;
      int rep_reg = data_.u_empty_match_check.repetition_register;
      bool has_minimum = (rep_reg != RegExpCompiler::kNoRegister);
      bool know_dist = trace->GetStoredPosition(start_pos_reg, &stored_pos);
      if (know_dist && !has_minimum && stored_pos == trace->cp_offset()) {
        // If we know we haven't advanced and there is no minimum we
        // can just backtrack immediately.
        assembler->GoTo(trace->backtrack());
      } else if (know_dist && stored_pos < trace->cp_offset()) {
        // If we know we've advanced we can generate the continuation
        // immediately.
        on_success()->Emit(compiler, trace);
      } else if (!trace->is_trivial()) {
        trace->Flush(compiler, this);
      } else {
        Label skip_empty_check;
        // If we have a minimum number of repetitions we check the current
        // number first and skip the empty check if it's not enough.
        if (has_minimum) {
          int limit = data_.u_empty_match_check.repetition_limit;
          assembler->IfRegisterLT(rep_reg, limit, &skip_empty_check);
        }
        // If the match is empty we bail out, otherwise we fall through
        // to the on-success continuation.
        assembler->IfRegisterEqPos(data_.u_empty_match_check.start_register,
                                   trace->backtrack());
        assembler->Bind(&skip_empty_check);
        on_success()->Emit(compiler, trace);
      }
      break;
    }
    case POSITIVE_SUBMATCH_SUCCESS: {
      if (!trace->is_trivial()) {
        trace->Flush(compiler, this);
        return;
      }
      assembler->ReadCurrentPositionFromRegister(
          data_.u_submatch.current_position_register);
      assembler->ReadStackPointerFromRegister(
          data_.u_submatch.stack_pointer_register);
      int clear_register_count = data_.u_submatch.clear_register_count;
      if (clear_register_count == 0) {
        on_success()->Emit(compiler, trace);
        return;
      }
      int clear_registers_from = data_.u_submatch.clear_register_from;
      Label clear_registers_backtrack;
      Trace new_trace = *trace;
      new_trace.set_backtrack(&clear_registers_backtrack);
      on_success()->Emit(compiler, &new_trace);

      assembler->Bind(&clear_registers_backtrack);
      int clear_registers_to = clear_registers_from + clear_register_count - 1;
      assembler->ClearRegisters(clear_registers_from, clear_registers_to);

      DCHECK(trace->backtrack() == nullptr);
      assembler->Backtrack();
      return;
    }
    case MODIFY_FLAGS: {
      compiler->set_flags(flags());
      on_success()->Emit(compiler, trace);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void BackReferenceNode::Emit(RegExpCompiler* compiler, Trace* trace) {
  RegExpMacroAssembler* assembler = compiler->macro_assembler();
  if (!trace->is_trivial()) {
    trace->Flush(compiler, this);
    return;
  }

  LimitResult limit_result = LimitVersions(compiler, trace);
  if (limit_result == DONE) return;
  DCHECK(limit_result == CONTINUE);

  RecursionCheck rc(compiler);

  DCHECK_EQ(start_reg_ + 1, end_reg_);
  if (IsIgnoreCase(compiler->flags())) {
    bool unicode = IsEitherUnicode(compiler->flags());
    assembler->CheckNotBackReferenceIgnoreCase(start_reg_, read_backward(),
                                               unicode, trace->backtrack());
  } else {
    assembler->CheckNotBackReference(start_reg_, read_backward(),
                                     trace->backtrack());
  }
  // We are going to advance backward, so we may end up at the start.
  if (read_backward()) trace->set_at_start(Trace::UNKNOWN);

  // Check that the back reference does not end inside a surrogate pair.
  if (IsEitherUnicode(compiler->flags()) && !compiler->one_byte()) {
    assembler->CheckNotInSurrogatePair(trace->cp_offset(), trace->backtrack());
  }
  on_success()->Emit(compiler, trace);
}

void TextNode::CalculateOffsets() {
  int element_count = elements()->length();
  // Set up the offsets of the elements relative to the start.  This is a fixed
  // quantity since a TextNode can only contain fixed-width things.
  int cp_offset = 0;
  for (int i = 0; i < element_count; i++) {
    TextElement& elm = elements()->at(i);
    elm.set_cp_offset(cp_offset);
    cp_offset += elm.length();
  }
}

namespace {

// Assertion propagation moves information about assertions such as
// \b to the affected nodes.  For instance, in /.\b./ information must
// be propagated to the first '.' that whatever follows needs to know
// if it matched a word or a non-word, and to the second '.' that it
// has to check if it succeeds a word or non-word.  In this case the
// result will be something like:
//
//   +-------+        +------------+
//   |   .   |        |      .     |
//   +-------+  --->  +------------+
//   | word? |        | check word |
//   +-------+        +------------+
class AssertionPropagator : public AllStatic {
 public:
  static void VisitText(TextNode* that) {}

  static void VisitAction(ActionNode* that) {
    // If the next node is interested in what it follows then this node
    // has to be interested too so it can pass the information on.
    that->info()->AddFromFollowing(that->on_success()->info());
  }

  static void VisitChoice(ChoiceNode* that, int i) {
    // Anything the following nodes need to know has to be known by
    // this node also, so it can pass it on.
    that->info()->AddFromFollowing(that->alternatives()->at(i).node()->info());
  }

  static void VisitLoopChoiceContinueNode(LoopChoiceNode* that) {
    that->info()->AddFromFollowing(that->continue_node()->info());
  }

  static void VisitLoopChoiceLoopNode(LoopChoiceNode* that) {
    that->info()->AddFromFollowing(that->loop_node()->info());
  }

  static void VisitNegativeLookaroundChoiceLookaroundNode(
      NegativeLookaroundChoiceNode* that) {
    VisitChoice(that, NegativeLookaroundChoiceNode::kLookaroundIndex);
  }

  static void VisitNegativeLookaroundChoiceContinueNode(
      NegativeLookaroundChoiceNode* that) {
    VisitChoice(that, NegativeLookaroundChoiceNode::kContinueIndex);
  }

  static void VisitBackReference(BackReferenceNode* that) {}

  static void VisitAssertion(AssertionNode* that) {}
};

// Propagates information about the minimum size of successful matches from
// successor nodes to their predecessors. Note that all eats_at_least values
// are initialized to zero before analysis.
class EatsAtLeastPropagator : public AllStatic {
 public:
  static void VisitText(TextNode* that) {
    // The eats_at_least value is not used if reading backward.
    if (!that->read_backward()) {
      // We are not at the start after this node, and thus we can use the
      // successor's eats_at_least_from_not_start value.
      uint8_t eats_at_least = base::saturated_cast<uint8_t>(
          that->Length() + that->on_success()
                               ->eats_at_least_info()
                               ->eats_at_least_from_not_start);
      that->set_eats_at_least_info(EatsAtLeastInfo(eats_at_least));
    }
  }

  static void VisitAction(ActionNode* that) {
    switch (that->action_type()) {
      case ActionNode::BEGIN_POSITIVE_SUBMATCH:
      case ActionNode::POSITIVE_SUBMATCH_SUCCESS:
        // We do not propagate eats_at_least data through positive lookarounds,
        // because they rewind input.
        // TODO(v8:11859) Potential approaches for fixing this include:
        // 1. Add a dedicated choice node for positive lookaround, similar to
        //    NegativeLookaroundChoiceNode.
        // 2. Add an eats_at_least_inside_loop field to EatsAtLeastInfo, which
        //    is <= eats_at_least_from_possibly_start, and use that value in
        //    EatsAtLeastFromLoopEntry.
        DCHECK(that->eats_at_least_info()->IsZero());
        break;
      case ActionNode::SET_REGISTER_FOR_LOOP:
        // SET_REGISTER_FOR_LOOP indicates a loop entry point, which means the
        // loop body will run at least the minimum number of times before the
        // continuation case can run.
        that->set_eats_at_least_info(
            that->on_success()->EatsAtLeastFromLoopEntry());
        break;
      case ActionNode::BEGIN_NEGATIVE_SUBMATCH:
      default:
        // Otherwise, the current node eats at least as much as its successor.
        // Note: we can propagate eats_at_least data for BEGIN_NEGATIVE_SUBMATCH
        // because NegativeLookaroundChoiceNode ignores its lookaround successor
        // when computing eats-at-least and quick check information.
        that->set_eats_at_least_info(*that->on_success()->eats_at_least_info());
        break;
    }
  }

  static void VisitChoice(ChoiceNode* that, int i) {
    // The minimum possible match from a choice node is the minimum of its
    // successors.
    EatsAtLeastInfo eats_at_least =
        i == 0 ? EatsAtLeastInfo(UINT8_MAX) : *that->eats_at_least_info();
    eats_at_least.SetMin(
        *that->alternatives()->at(i).node()->eats_at_least_info());
    that->set_eats_at_least_info(eats_at_least);
  }

  static void VisitLoopChoiceContinueNode(LoopChoiceNode* that) {
    if (!that->read_backward()) {
      that->set_eats_at_least_info(
          *that->continue_node()->eats_at_least_info());
    }
  }

  static void VisitLoopChoiceLoopNode(LoopChoiceNode* that) {}

  static void VisitNegativeLookaroundChoiceLookaroundNode(
      NegativeLookaroundChoiceNode* that) {}

  static void VisitNegativeLookaroundChoiceContinueNode(
      NegativeLookaroundChoiceNode* that) {
    that->set_eats_at_least_info(*that->continue_node()->eats_at_least_info());
  }

  static void VisitBackReference(BackReferenceNode* that) {
    if (!that->read_backward()) {
      that->set_eats_at_least_info(*that->on_success()->eats_at_least_info());
    }
  }

  static void VisitAssertion(AssertionNode* that) {
    EatsAtLeastInfo eats_at_least = *that->on_success()->eats_at_least_info();
    if (that->assertion_type() == AssertionNode::AT_START) {
      // If we know we are not at the start and we are asked "how many
      // characters will you match if you succeed?" then we can answer anything
      // since false implies false.  So let's just set the max answer
      // (UINT8_MAX) since that won't prevent us from preloading a lot of
      // characters for the other branches in the node graph.
      eats_at_least.eats_at_least_from_not_start = UINT8_MAX;
    }
    that->set_eats_at_least_info(eats_at_least);
  }
};

}  // namespace

// -------------------------------------------------------------------
// Analysis

// Iterates the node graph and provides the opportunity for propagators to set
// values that depend on successor nodes.
template <typename... Propagators>
class Analysis : public NodeVisitor {
 public:
  Analysis(Isolate* isolate, bool is_one_byte, RegExpFlags flags)
      : isolate_(isolate),
        is_one_byte_(is_one_byte),
        flags_(flags),
        error_(RegExpError::kNone) {}

  void EnsureAnalyzed(RegExpNode* that) {
    StackLimitCheck check(isolate());
    if (check.HasOverflowed()) {
      if (v8_flags.correctness_fuzzer_suppressions) {
        FATAL("Analysis: Aborting on stack overflow");
      }
      fail(RegExpError::kAnalysisStackOverflow);
      return;
    }
    if (that->info()->been_analyzed || that->info()->being_analyzed) return;
    that->info()->being_analyzed = true;
    that->Accept(this);
    that->info()->being_analyzed = false;
    that->info()->been_analyzed = true;
  }

  bool has_failed() { return error_ != RegExpError::kNone; }
  RegExpError error() {
    DCHECK(error_ != RegExpError::kNone);
    return error_;
  }
  void fail(RegExpError error) { error_ = error; }

  Isolate* isolate() const { return isolate_; }

  void VisitEnd(EndNode* that) override {
    // nothing to do
  }

// Used to call the given static function on each propagator / variadic template
// argument.
#define STATIC_FOR_EACH(expr)       \
  do {                              \
    int dummy[] = {((expr), 0)...}; \
    USE(dummy);                     \
  } while (false)

  void VisitText(TextNode* that) override {
    that->MakeCaseIndependent(isolate(), is_one_byte_, flags());
    EnsureAnalyzed(that->on_success());
    if (has_failed()) return;
    that->CalculateOffsets();
    STATIC_FOR_EACH(Propagators::VisitText(that));
  }

  void VisitAction(ActionNode* that) override {
    if (that->action_type() == ActionNode::MODIFY_FLAGS) {
      set_flags(that->flags());
    }
    EnsureAnalyzed(that->on_success());
    if (has_failed()) return;
    STATIC_FOR_EACH(Propagators::VisitAction(that));
  }

  void VisitChoice(ChoiceNode* that) override {
    for (int i = 0; i < that->alternatives()->length(); i++) {
      EnsureAnalyzed(that->alternatives()->at(i).node());
      if (has_failed()) return;
      STATIC_FOR_EACH(Propagators::VisitChoice(that, i));
    }
  }

  void VisitLoopChoice(LoopChoiceNode* that) override {
    DCHECK_EQ(that->alternatives()->length(), 2);  // Just loop and continue.

    // First propagate all information from the continuation node.
    EnsureAnalyzed(that->continue_node());
    if (has_failed()) return;
    STATIC_FOR_EACH(Propagators::VisitLoopChoiceContinueNode(that));

    // Check the loop last since it may need the value of this node
    // to get a correct result.
    EnsureAnalyzed(that->loop_node());
    if (has_failed()) return;
    STATIC_FOR_EACH(Propagators::VisitLoopChoiceLoopNode(that));
  }

  void VisitNegativeLookaroundChoice(
      NegativeLookaroundChoiceNode* that) override {
    DCHECK_EQ(that->alternatives()->length(), 2);  // Lookaround and continue.

    EnsureAnalyzed(that->lookaround_node());
    if (has_failed()) return;
    STATIC_FOR_EACH(
        Propagators::VisitNegativeLookaroundChoiceLookaroundNode(that));

    EnsureAnalyzed(that->continue_node());
    if (has_failed()) return;
    STATIC_FOR_EACH(
        Propagators::VisitNegativeLookaroundChoiceContinueNode(that));
  }

  void VisitBackReference(BackReferenceNode* that) override {
    EnsureAnalyzed(that->on_success());
    if (has_failed()) return;
    STATIC_FOR_EACH(Propagators::VisitBackReference(that));
  }

  void VisitAssertion(AssertionNode* that) override {
    EnsureAnalyzed(that->on_success());
    if (has_failed()) return;
    STATIC_FOR_EACH(Propagators::VisitAssertion(that));
  }

#undef STATIC_FOR_EACH

 private:
  RegExpFlags flags() const { return flags_; }
  void set_flags(RegExpFlags flags) { flags_ = flags; }

  Isolate* isolate_;
  const bool is_one_byte_;
  RegExpFlags flags_;
  RegExpError error_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Analysis);
};

RegExpError AnalyzeRegExp(Isolate* isolate, bool is_one_byte, RegExpFlags flags,
                          RegExpNode* node) {
  Analysis<AssertionPropagator, EatsAtLeastPropagator> analysis(
      isolate, is_one_byte, flags);
  DCHECK_EQ(node->info()->been_analyzed, false);
  analysis.EnsureAnalyzed(node);
  DCHECK_IMPLIES(analysis.has_failed(), analysis.error() != RegExpError::kNone);
  return analysis.has_failed() ? analysis.error() : RegExpError::kNone;
}

void BackReferenceNode::FillInBMInfo(Isolate* isolate, int offset, int budget,
                                     BoyerMooreLookahead* bm,
                                     bool not_at_start) {
  // Working out the set of characters that a backreference can match is too
  // hard, so we just say that any character can match.
  bm->SetRest(offset);
  SaveBMInfo(bm, not_at_start, offset);
}

static_assert(BoyerMoorePositionInfo::kMapSize ==
              RegExpMacroAssembler::kTableSize);

void ChoiceNode::FillInBMInfo(Isolate* isolate, int offset, int budget,
                              BoyerMooreLookahead* bm, bool not_at_start) {
  ZoneList<GuardedAlternative>* alts = alternatives();
  budget = (budget - 1) / alts->length();
  for (int i = 0; i < alts->length(); i++) {
    GuardedAlternative& alt = alts->at(i);
    if (alt.guards() != nullptr && alt.guards()->length() != 0) {
      bm->SetRest(offset);  // Give up trying to fill in info.
      SaveBMInfo(bm, not_at_start, offset);
      return;
    }
    alt.node()->FillInBMInfo(isolate, offset, budget, bm, not_at_start);
  }
  SaveBMInfo(bm, not_at_start, offset);
}

void TextNode::FillInBMInfo(Isolate* isolate, int initial_offset, int budget,
                            BoyerMooreLookahead* bm, bool not_at_start) {
  if (initial_offset >= bm->length()) return;
  int offset = initial_offset;
  int max_char = bm->max_char();
  for (int i = 0; i < elements()->length(); i++) {
    if (offset >= bm->length()) {
      if (initial_offset == 0) set_bm_info(not_at_start, bm);
      return;
    }
    TextElement text = elements()->at(i);
    if (text.text_type() == TextElement::ATOM) {
      RegExpAtom* atom = text.atom();
      for (int j = 0; j < atom->length(); j++, offset++) {
        if (offset >= bm->length()) {
          if (initial_offset == 0) set_bm_info(not_at_start, bm);
          return;
        }
        base::uc16 character = atom->data()[j];
        if (IsIgnoreCase(bm->compiler()->flags())) {
          unibrow::uchar chars[4];
          int length = GetCaseIndependentLetters(
              isolate, character, bm->max_char() == String::kMaxOneByteCharCode,
              chars, 4);
          for (int k = 0; k < length; k++) {
            bm->Set(offset, chars[k]);
          }
        } else {
          if (character <= max_char) bm->Set(offset, character);
        }
      }
    } else {
      DCHECK_EQ(TextElement::CLASS_RANGES, text.text_type());
      RegExpClassRanges* class_ranges = text.class_ranges();
      ZoneList<CharacterRange>* ranges = class_ranges->ranges(zone());
      if (class_ranges->is_negated()) {
        bm->SetAll(offset);
      } else {
        for (int k = 0; k < ranges->length(); k++) {
          CharacterRange& range = ranges->at(k);
          if (static_cast<int>(range.from()) > max_char) continue;
          int to = std::min(max_char, static_cast<int>(range.to()));
          bm->SetInterval(offset, Interval(range.from(), to));
        }
      }
      offset++;
    }
  }
  if (offset >= bm->length()) {
    if (initial_offset == 0) set_bm_info(not_at_start, bm);
    return;
  }
  on_success()->FillInBMInfo(isolate, offset, budget - 1, bm,
                             true);  // Not at start after a text node.
  if (initial_offset == 0) set_bm_info(not_at_start, bm);
}

RegExpNode* RegExpCompiler::OptionallyStepBackToLeadSurrogate(
    RegExpNode* on_success) {
  DCHECK(!read_backward());
  ZoneList<CharacterRange>* lead_surrogates = CharacterRange::List(
      zone(), CharacterRange::Range(kLeadSurrogateStart, kLeadSurrogateEnd));
  ZoneList<CharacterRange>* trail_surrogates = CharacterRange::List(
      zone(), CharacterRange::Range(kTrailSurrogateStart, kTrailSurrogateEnd));

  ChoiceNode* optional_step_back = zone()->New<ChoiceNode>(2, zone());

  int stack_register = UnicodeLookaroundStackRegister();
  int position_register = UnicodeLookaroundPositionRegister();
  RegExpNode* step_back = TextNode::CreateForCharacterRanges(
      zone(), lead_surrogates, true, on_success);
  RegExpLookaround::Builder builder(true, step_back, stack_register,
                                    position_register);
  RegExpNode* match_trail = TextNode::CreateForCharacterRanges(
      zone(), trail_surrogates, false, builder.on_match_success());

  optional_step_back->AddAlternative(
      GuardedAlternative(builder.ForMatch(match_trail)));
  optional_step_back->AddAlternative(GuardedAlternative(on_success));

  return optional_step_back;
}

RegExpNode* RegExpCompiler::PreprocessRegExp(RegExpCompileData* data,
                                             bool is_one_byte) {
  // Wrap the body of the regexp in capture #0.
  RegExpNode* captured_body =
      RegExpCapture::ToNode(data->tree, 0, this, accept());
  RegExpNode* node = captured_body;
  if (!data->tree->IsAnchoredAtStart() && !IsSticky(flags())) {
    // Add a .*? at the beginning, outside the body capture, unless
    // this expression is anchored at the beginning or sticky.
    RegExpNode* loop_node = RegExpQuantifier::ToNode(
        0, RegExpTree::kInfinity, false,
        zone()->New<RegExpClassRanges>(StandardCharacterSet::kEverything), this,
        captured_body, data->contains_anchor);

    if (data->contains_anchor) {
      // Unroll loop once, to take care of the case that might start
      // at the start of input.
      ChoiceNode* first_step_node = zone()->New<ChoiceNode>(2, zone());
      first_step_node->AddAlternative(GuardedAlternative(captured_body));
      first_step_node->AddAlternative(GuardedAlternative(zone()->New<TextNode>(
          zone()->New<RegExpClassRanges>(StandardCharacterSet::kEverything),
          false, loop_node)));
      node = first_step_node;
    } else {
      node = loop_node;
    }
  }
  if (is_one_byte) {
    node = node->FilterOneByte(RegExpCompiler::kMaxRecursion, flags());
    // Do it again to propagate the new nodes to places where they were not
    // put because they had not been calculated yet.
    if (node != nullptr) {
      node = node->FilterOneByte(RegExpCompiler::kMaxRecursion, flags());
    }
  } else if (IsEitherUnicode(flags()) &&
             (IsGlobal(flags()) || IsSticky(flags()))) {
    node = OptionallyStepBackToLeadSurrogate(node);
  }

  if (node == nullptr) node = zone()->New<EndNode>(EndNode::BACKTRACK, zone());
  return node;
}

void RegExpCompiler::ToNodeCheckForStackOverflow() {
  if (StackLimitCheck{isolate()}.HasOverflowed()) {
    V8::FatalProcessOutOfMemory(isolate(), "RegExpCompiler");
  }
}

}  // namespace internal
}  // namespace v8
