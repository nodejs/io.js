// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/experimental/experimental-interpreter.h"

#include <optional>
#include <string>

#include "src/base/strings.h"
#include "src/common/assert-scope.h"
#include "src/flags/flags.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/string-inl.h"
#include "src/regexp/experimental/experimental.h"
#include "src/strings/char-predicates-inl.h"
#include "src/zone/zone-allocator.h"
#include "src/zone/zone-list-inl.h"

namespace v8 {
namespace internal {

namespace {

constexpr int kUndefinedRegisterValue = -1;

template <class Character>
bool SatisfiesAssertion(RegExpAssertion::Type type,
                        base::Vector<const Character> context, int position) {
  DCHECK_LE(position, context.length());
  DCHECK_GE(position, 0);

  switch (type) {
    case RegExpAssertion::Type::START_OF_INPUT:
      return position == 0;
    case RegExpAssertion::Type::END_OF_INPUT:
      return position == context.length();
    case RegExpAssertion::Type::START_OF_LINE:
      if (position == 0) return true;
      return unibrow::IsLineTerminator(context[position - 1]);
    case RegExpAssertion::Type::END_OF_LINE:
      if (position == context.length()) return true;
      return unibrow::IsLineTerminator(context[position]);
    case RegExpAssertion::Type::BOUNDARY:
      if (context.length() == 0) {
        return false;
      } else if (position == 0) {
        return IsRegExpWord(context[position]);
      } else if (position == context.length()) {
        return IsRegExpWord(context[position - 1]);
      } else {
        return IsRegExpWord(context[position - 1]) !=
               IsRegExpWord(context[position]);
      }
    case RegExpAssertion::Type::NON_BOUNDARY:
      return !SatisfiesAssertion(RegExpAssertion::Type::BOUNDARY, context,
                                 position);
  }
}

base::Vector<RegExpInstruction> ToInstructionVector(
    Tagged<TrustedByteArray> raw_bytes,
    const DisallowGarbageCollection& no_gc) {
  RegExpInstruction* inst_begin =
      reinterpret_cast<RegExpInstruction*>(raw_bytes->begin());
  int inst_num = raw_bytes->length() / sizeof(RegExpInstruction);
  DCHECK_EQ(sizeof(RegExpInstruction) * inst_num, raw_bytes->length());
  return base::Vector<RegExpInstruction>(inst_begin, inst_num);
}

template <class Character>
base::Vector<const Character> ToCharacterVector(
    Tagged<String> str, const DisallowGarbageCollection& no_gc);

template <>
base::Vector<const uint8_t> ToCharacterVector<uint8_t>(
    Tagged<String> str, const DisallowGarbageCollection& no_gc) {
  DCHECK(str->IsFlat());
  String::FlatContent content = str->GetFlatContent(no_gc);
  DCHECK(content.IsOneByte());
  return content.ToOneByteVector();
}

template <>
base::Vector<const base::uc16> ToCharacterVector<base::uc16>(
    Tagged<String> str, const DisallowGarbageCollection& no_gc) {
  DCHECK(str->IsFlat());
  String::FlatContent content = str->GetFlatContent(no_gc);
  DCHECK(content.IsTwoByte());
  return content.ToUC16Vector();
}

class FilterGroups {
 public:
  static base::Vector<int> Filter(
      int pc, base::Vector<int> registers,
      base::Vector<uint64_t> quantifiers_clocks,
      base::Vector<uint64_t> capture_clocks,
      base::Vector<int> filtered_registers,
      base::Vector<const RegExpInstruction> bytecode, Zone* zone) {
    /* Capture groups that were not traversed in the last iteration of a
     * quantifier need to be discarded. In order to determine which groups need
     * to be discarded, the interpreter maintains a clock, an internal count of
     * bytecode instructions executed. Whenever it reaches a quantifier or
     * a capture group, it records the current clock. After a match is found,
     * the interpreter filters out capture groups that were defined in any other
     * iteration than the last. To do so, it compares the last clock value of
     * the group with the last clock value of its parent quantifier/group,
     * keeping only groups that were defined after the parent quantifier/group
     * last iteration. The structure of the bytecode used is explained in
     * `FilterGroupsCompileVisitor` (experimental-compiler.cc). */

    return FilterGroups(pc, bytecode, zone)
        .Run(registers, quantifiers_clocks, capture_clocks, filtered_registers);
  }

 private:
  FilterGroups(int pc, base::Vector<const RegExpInstruction> bytecode,
               Zone* zone)
      : pc_(pc),
        max_clock_(0),
        pc_stack_(zone),
        max_clock_stack_(zone),
        bytecode_(bytecode) {}

  /* Goes back to the parent node, restoring pc_ and max_clock_. If already at
   * the root of the tree, completes the filtering process. */
  void Up() {
    if (pc_stack_.size() > 0) {
      pc_ = pc_stack_.top();
      max_clock_ = max_clock_stack_.top();
      pc_stack_.pop();
      max_clock_stack_.pop();
    }
  }

  /* Increments pc_. When at the end of a node, goes back to the parent node. */
  void IncrementPC() {
    if (IsAtNodeEnd()) {
      Up();
    } else {
      pc_++;
    }
  }

  bool IsAtNodeEnd() {
    return pc_ + 1 == bytecode_.length() ||
           bytecode_[pc_ + 1].opcode != RegExpInstruction::FILTER_CHILD;
  }

  base::Vector<int> Run(base::Vector<int> registers_,
                        base::Vector<uint64_t> quantifiers_clocks_,
                        base::Vector<uint64_t> capture_clocks_,
                        base::Vector<int> filtered_registers_) {
    pc_stack_.push(pc_);
    max_clock_stack_.push(max_clock_);

    while (!pc_stack_.empty()) {
      auto instr = bytecode_[pc_];
      switch (instr.opcode) {
        case RegExpInstruction::FILTER_CHILD:
          // We only need to come back for the next instructions if we are at
          // the end of the node.
          if (!IsAtNodeEnd()) {
            pc_stack_.push(pc_ + 1);
            max_clock_stack_.push(max_clock_);
          }

          // Enter the child's node.
          pc_ = instr.payload.pc;
          break;

        case RegExpInstruction::FILTER_GROUP: {
          int group_id = instr.payload.group_id;

          // Checks whether the captured group should be saved or discarded.
          int register_id = 2 * group_id;
          if (capture_clocks_[register_id] >= max_clock_) {
            filtered_registers_[register_id] = registers_[register_id];
            filtered_registers_[register_id + 1] = registers_[register_id + 1];
            IncrementPC();
          } else {
            // If the node should be discarded, all its children should be too.
            // By going back to the parent, we don't visit the children, and
            // therefore don't copy their registers.
            Up();
          }
          break;
        }

        case RegExpInstruction::FILTER_QUANTIFIER: {
          int quantifier_id = instr.payload.quantifier_id;

          // Checks whether the quantifier should be saved or discarded.
          if (quantifiers_clocks_[quantifier_id] >= max_clock_) {
            max_clock_ = quantifiers_clocks_[quantifier_id];
            IncrementPC();
          } else {
            // If the node should be discarded, all its children should be too.
            // By going back to the parent, we don't visit the children, and
            // therefore don't copy their registers.
            Up();
          }
          break;
        }

        default:
          UNREACHABLE();
      }
    }

    return filtered_registers_;
  }

  int pc_;

  // The last clock encountered (either from a quantifier or a capture group).
  // Any groups whose clock is less then max_clock_ needs to be discarded.
  uint64_t max_clock_;

  // Stores pc_ and max_clock_ when the interpreter enters a node.
  ZoneStack<int> pc_stack_;
  ZoneStack<uint64_t> max_clock_stack_;

  base::Vector<const RegExpInstruction> bytecode_;
};

template <class Character>
class NfaInterpreter {
  // Executes a bytecode program in breadth-first mode, without backtracking.
  // `Character` can be instantiated with `uint8_t` or `base::uc16` for one byte
  // or two byte input strings.
  //
  // In contrast to the backtracking implementation, this has linear time
  // complexity in the length of the input string. Breadth-first mode means
  // that threads are executed in lockstep with respect to their input
  // position, i.e. the threads share a common input index.  This is similar
  // to breadth-first simulation of a non-deterministic finite automaton (nfa),
  // hence the name of the class.
  //
  // To follow the semantics of a backtracking VM implementation, we have to be
  // careful about whether we stop execution when a thread executes ACCEPT.
  // For example, consider execution of the bytecode generated by the regexp
  //
  //   r = /abc|..|[a-c]{10,}/
  //
  // on input "abcccccccccccccc".  Clearly the three alternatives
  // - /abc/
  // - /../
  // - /[a-c]{10,}/
  // all match this input.  A backtracking implementation will report "abc" as
  // match, because it explores the first alternative before the others.
  //
  // However, if we execute breadth first, then we execute the 3 threads
  // - t1, which tries to match /abc/
  // - t2, which tries to match /../
  // - t3, which tries to match /[a-c]{10,}/
  // in lockstep i.e. by iterating over the input and feeding all threads one
  // character at a time.  t2 will execute an ACCEPT after two characters,
  // while t1 will only execute ACCEPT after three characters. Thus we find a
  // match for the second alternative before a match of the first alternative.
  //
  // This shows that we cannot always stop searching as soon as some thread t
  // executes ACCEPT:  If there is a thread u with higher priority than t, then
  // it must be finished first.  If u produces a match, then we can discard the
  // match of t because matches produced by threads with higher priority are
  // preferred over matches of threads with lower priority.  On the other hand,
  // we are allowed to abort all threads with lower priority than t if t
  // produces a match: Such threads can only produce worse matches.  In the
  // example above, we can abort t3 after two characters because of t2's match.
  //
  // Thus the interpreter keeps track of a priority-ordered list of threads.
  // If a thread ACCEPTs, all threads with lower priority are discarded, and
  // the search continues with the threads with higher priority.  If no threads
  // with high priority are left, we return the match that was produced by the
  // ACCEPTing thread with highest priority.
 public:
  NfaInterpreter(Isolate* isolate, RegExp::CallOrigin call_origin,
                 Tagged<TrustedByteArray> bytecode,
                 int register_count_per_match, Tagged<String> input,
                 int32_t input_index, Zone* zone)
      : isolate_(isolate),
        call_origin_(call_origin),
        bytecode_object_(bytecode),
        bytecode_(ToInstructionVector(bytecode, no_gc_)),
        register_count_per_match_(register_count_per_match),
        quantifier_count_(0),
        input_object_(input),
        input_(ToCharacterVector<Character>(input, no_gc_)),
        input_index_(input_index),
        clock(0),
        pc_last_input_index_(
            zone->AllocateArray<LastInputIndex>(bytecode->length()),
            bytecode->length()),
        active_threads_(0, zone),
        blocked_threads_(0, zone),
        register_array_allocator_(zone),
        quantifier_array_allocator_(std::nullopt),
        capture_clock_array_allocator_(std::nullopt),
        best_match_thread_(std::nullopt),
        lookbehind_pc_(0, zone),
        filter_groups_pc_(std::nullopt),
        lookbehind_table_(0, zone),
        zone_(zone) {
    DCHECK(!bytecode_.empty());
    DCHECK_GE(input_index_, 0);
    DCHECK_LE(input_index_, input_.length());

    // Finds the starting PC of every lookbehind. Since they are listed one
    // after the other, they start after each `ACCEPT` and
    // `WRITE_LOOKBEHIND_TABLE` instructions (except the last one). We do not
    // iterate over the last instruction, since it cannot be followed by a
    // lookbehind's bytecode.
    for (int i = 0; i < bytecode_.length() - 1; ++i) {
      // The first instruction to follow the `FILTER_*` section or the `ACCEPT`
      // instruction is the start of the lookbehind of index 0.
      if ((RegExpInstruction::IsFilter(bytecode_[i]) ||
           bytecode_[i].opcode == RegExpInstruction::ACCEPT) &&
          !RegExpInstruction::IsFilter(bytecode_[i + 1])) {
        lookbehind_pc_.Add(i + 1, zone_);
        lookbehind_table_.Add(false, zone_);
      }

      if (bytecode_[i].opcode ==
          RegExpInstruction::Opcode::WRITE_LOOKBEHIND_TABLE) {
        lookbehind_pc_.Add(i + 1, zone_);
        lookbehind_table_.Add(false, zone_);
      }

      // The first `FILTER_*` instruction encountered is the start of the
      // `FILTER_*` section.
      if (!filter_groups_pc_.has_value() &&
          RegExpInstruction::IsFilter(bytecode_[i])) {
        DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
        filter_groups_pc_ = i;
      }

      if (bytecode_[i].opcode == RegExpInstruction::SET_QUANTIFIER_TO_CLOCK) {
        DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
        quantifier_count_ =
            std::max(quantifier_count_, bytecode_[i].payload.quantifier_id + 1);
      }
    }

    // Precomputes the memory consumption of a single thread, to be used by
    // `CheckMemoryConsumption()`.
    if (v8_flags.experimental_regexp_engine_capture_group_opt) {
      quantifier_array_allocator_.emplace(zone_);
      capture_clock_array_allocator_.emplace(zone_);

      memory_consumption_per_thread_ =
          register_count_per_match_ * sizeof(int) +  // RegisterArray
          quantifier_count_ * sizeof(uint64_t) +     // QuantifierClockArray
          register_count_per_match_ * sizeof(uint64_t) +  // CaptureClockArray
          sizeof(InterpreterThread);
    }

    std::fill(pc_last_input_index_.begin(), pc_last_input_index_.end(),
              LastInputIndex());
  }

  // Finds matches and writes their concatenated capture registers to
  // `output_registers`.  `output_registers[i]` has to be valid for all i <
  // output_register_count.  The search continues until all remaining matches
  // have been found or there is no space left in `output_registers`.  Returns
  // the number of matches found.
  int FindMatches(int32_t* output_registers, int output_register_count) {
    const int max_match_num = output_register_count / register_count_per_match_;

    int match_num = 0;
    while (match_num != max_match_num) {
      int err_code = FindNextMatch();
      if (err_code != RegExp::kInternalRegExpSuccess) return err_code;

      if (!FoundMatch()) break;

      base::Vector<int> registers = GetFilteredRegisters(*best_match_thread_);
      output_registers =
          std::copy(registers.begin(), registers.end(), output_registers);

      ++match_num;

      const int match_begin = registers[0];
      const int match_end = registers[1];
      DCHECK_LE(match_begin, match_end);
      const int match_length = match_end - match_begin;
      if (match_length != 0) {
        SetInputIndex(match_end);
      } else if (match_end == input_.length()) {
        // Zero-length match, input exhausted.
        SetInputIndex(match_end);
        break;
      } else {
        // Zero-length match, more input.  We don't want to report more matches
        // here endlessly, so we advance by 1.
        SetInputIndex(match_end + 1);

        // TODO(mbid,v8:10765): If we're in unicode mode, we have to advance to
        // the next codepoint, not to the next code unit. See also
        // `RegExpUtils::AdvanceStringIndex`.
        static_assert(!ExperimentalRegExp::kSupportsUnicode);
      }
    }

    return match_num;
  }

 private:
  // The state of a "thread" executing experimental regexp bytecode.  (Not to
  // be confused with an OS thread.)
  class InterpreterThread {
   public:
    enum class ConsumedCharacter { DidConsume, DidNotConsume };

    InterpreterThread(int pc, int* register_array_begin,
                      uint64_t* quantifier_clock_array_begin,
                      uint64_t* capture_clock_array_begin,
                      ConsumedCharacter consumed_since_last_quantifier)
        : pc(pc),
          register_array_begin(register_array_begin),
          quantifier_clock_array_begin(quantifier_clock_array_begin),
          captures_clock_array_begin(capture_clock_array_begin),
          consumed_since_last_quantifier(consumed_since_last_quantifier) {}

    // This thread's program counter, i.e. the index within `bytecode_` of the
    // next instruction to be executed.
    int pc;
    // Pointer to the array of registers, which is always size
    // `register_count_per_match_`.  Should be deallocated with
    // `register_array_allocator_`.
    int* register_array_begin;

    // Pointer to an array containing the clock when the register was last
    // saved, which is always size `register_count_per_match_`.  Should be
    // deallocated with, respectively, `quantifier_array_allocator_` and
    // `capture_clock_array_allocator_`.
    uint64_t* quantifier_clock_array_begin;
    uint64_t* captures_clock_array_begin;

    // Describe whether the thread consumed a character since it last entered a
    // quantifier. Since quantifier iterations that match the empty string are
    // not allowed, we need to distinguish threads that are allowed to exit a
    // quantifier iteration from those that are not.
    ConsumedCharacter consumed_since_last_quantifier;
  };

  // Handles pending interrupts if there are any.  Returns
  // RegExp::kInternalRegExpSuccess if execution can continue, and an error
  // code otherwise.
  int HandleInterrupts() {
    StackLimitCheck check(isolate_);
    if (call_origin_ == RegExp::CallOrigin::kFromJs) {
      // Direct calls from JavaScript can be interrupted in two ways:
      // 1. A real stack overflow, in which case we let the caller throw the
      //    exception.
      // 2. The stack guard was used to interrupt execution for another purpose,
      //    forcing the call through the runtime system.
      if (check.JsHasOverflowed()) {
        return RegExp::kInternalRegExpException;
      } else if (check.InterruptRequested()) {
        return RegExp::kInternalRegExpRetry;
      }
    } else {
      DCHECK(call_origin_ == RegExp::CallOrigin::kFromRuntime);
      HandleScope handles(isolate_);
      DirectHandle<TrustedByteArray> bytecode_handle(bytecode_object_,
                                                     isolate_);
      DirectHandle<String> input_handle(input_object_, isolate_);

      if (check.JsHasOverflowed()) {
        // We abort the interpreter now anyway, so gc can't invalidate any
        // pointers.
        AllowGarbageCollection yes_gc;
        isolate_->StackOverflow();
        return RegExp::kInternalRegExpException;
      } else if (check.InterruptRequested()) {
        // TODO(mbid): Is this really equivalent to whether the string is
        // one-byte or two-byte? A comment at the declaration of
        // IsOneByteRepresentationUnderneath says that this might fail for
        // external strings.
        const bool was_one_byte =
            String::IsOneByteRepresentationUnderneath(input_object_);

        Tagged<Object> result;
        {
          AllowGarbageCollection yes_gc;
          result = isolate_->stack_guard()->HandleInterrupts();
        }
        if (IsException(result, isolate_)) {
          return RegExp::kInternalRegExpException;
        }

        // If we changed between a LATIN1 and a UC16 string, we need to restart
        // regexp matching with the appropriate template instantiation of
        // RawMatch.
        if (String::IsOneByteRepresentationUnderneath(*input_handle) !=
            was_one_byte) {
          return RegExp::kInternalRegExpRetry;
        }

        // Update objects and pointers in case they have changed during gc.
        bytecode_object_ = *bytecode_handle;
        bytecode_ = ToInstructionVector(bytecode_object_, no_gc_);
        input_object_ = *input_handle;
        input_ = ToCharacterVector<Character>(input_object_, no_gc_);
      }
    }
    return RegExp::kInternalRegExpSuccess;
  }

  // Change the current input index for future calls to `FindNextMatch`.
  void SetInputIndex(int new_input_index) {
    DCHECK_GE(input_index_, 0);
    DCHECK_LE(input_index_, input_.length());

    input_index_ = new_input_index;
  }

  // Find the next match and return the corresponding capture registers and
  // write its capture registers to `best_match_thread_`.  The search starts
  // at the current `input_index_`.  Returns RegExp::kInternalRegExpSuccess if
  // execution could finish regularly (with or without a match) and an error
  // code due to interrupt otherwise.
  int FindNextMatch() {
    DCHECK(active_threads_.is_empty());
    // TODO(mbid,v8:10765): Can we get around resetting `pc_last_input_index_`
    // here? As long as
    //
    //   pc_last_input_index_[pc] < input_index_
    //
    // for all possible program counters pc that are reachable without input
    // from pc = 0 and
    //
    //   pc_last_input_index_[k] <= input_index_
    //
    // for all k > 0 hold I think everything should be fine.  Maybe we can do
    // something about this in `SetInputIndex`.
    std::fill(pc_last_input_index_.begin(), pc_last_input_index_.end(),
              LastInputIndex());
    std::fill(lookbehind_table_.begin(), lookbehind_table_.end(), false);

    // Clean up left-over data from a previous call to FindNextMatch.
    for (InterpreterThread t : blocked_threads_) {
      DestroyThread(t);
    }
    blocked_threads_.Rewind(0);

    for (InterpreterThread t : active_threads_) {
      DestroyThread(t);
    }
    active_threads_.Rewind(0);

    if (best_match_thread_.has_value()) {
      DestroyThread(*best_match_thread_);
      best_match_thread_ = std::nullopt;
    }

    // The lookbehind threads need to be executed before the thread of their
    // parent (lookbehind or main expression). The order of the bytecode (see
    // also `BytecodeAssembler`) ensures that they need to be executed from last
    // to first (as of their position in the bytecode). The main expression
    // bytecode is located at PC 0, and is executed with the lowest priority.
    active_threads_.Add(NewEmptyThread(0), zone_);

    for (int i : lookbehind_pc_) {
      active_threads_.Add(NewEmptyThread(i), zone_);
    }
    // Run the initial thread, potentially forking new threads, until every
    // thread is blocked without further input.
    int err_code = RunActiveThreads();
    if (err_code != RegExp::kInternalRegExpSuccess) return err_code;

    // We stop if one of the following conditions hold:
    // - We have exhausted the entire input.
    // - We have found a match at some point, and there are no remaining
    //   threads with higher priority than the thread that produced the match.
    //   Threads with low priority have been aborted earlier, and the remaining
    //   threads are blocked here, so the latter simply means that
    //   `blocked_threads_` is empty.
    while (input_index_ != input_.length() &&
           !(FoundMatch() && blocked_threads_.is_empty())) {
      DCHECK(active_threads_.is_empty());
      base::uc16 input_char = input_[input_index_];
      ++input_index_;

      std::fill(lookbehind_table_.begin(), lookbehind_table_.end(), false);

      static constexpr int kTicksBetweenInterruptHandling = 64;
      if (input_index_ % kTicksBetweenInterruptHandling == 0) {
        int err_code = HandleInterrupts();
        if (err_code != RegExp::kInternalRegExpSuccess) return err_code;
      }

      // We unblock all blocked_threads_ by feeding them the input char.
      FlushBlockedThreads(input_char);

      // Run all threads until they block or accept.
      err_code = RunActiveThreads();
      if (err_code != RegExp::kInternalRegExpSuccess) return err_code;
    }

    return RegExp::kInternalRegExpSuccess;
  }

  // Run an active thread `t` until it executes a CONSUME_RANGE or ACCEPT
  // instruction, or its PC value was already processed.
  // - If processing of `t` can't continue because of CONSUME_RANGE, it is
  //   pushed on `blocked_threads_`.
  // - If `t` executes ACCEPT, set `best_match` according to `t.match_begin` and
  //   the current input index. All remaining `active_threads_` are discarded.
  int RunActiveThread(InterpreterThread t) {
    ++clock;

    // Since the clock is a `uint64_t`, it is almost guaranteed
    // not to overflow. An `uint64_t` being at least 64 bits, it
    // would take at least a hundred years to overflow if the clock was
    // incremented at each cycle of a 3 GHz processor.
    DCHECK_GT(clock, 0);

    while (true) {
      SBXCHECK_BOUNDS(t.pc, bytecode_.size());
      if (IsPcProcessed(t.pc, t.consumed_since_last_quantifier)) {
        DestroyThread(t);
        return RegExp::kInternalRegExpSuccess;
      }
      MarkPcProcessed(t.pc, t.consumed_since_last_quantifier);

      RegExpInstruction inst = bytecode_[t.pc];
      switch (inst.opcode) {
        case RegExpInstruction::CONSUME_RANGE: {
          blocked_threads_.Add(t, zone_);
          return RegExp::kInternalRegExpSuccess;
        }
        case RegExpInstruction::ASSERTION:
          if (!SatisfiesAssertion(inst.payload.assertion_type, input_,
                                  input_index_)) {
            DestroyThread(t);
            return RegExp::kInternalRegExpSuccess;
          }
          ++t.pc;
          break;
        case RegExpInstruction::FORK: {
          InterpreterThread fork = NewUninitializedThread(inst.payload.pc);
          fork.consumed_since_last_quantifier =
              t.consumed_since_last_quantifier;

          base::Vector<int> fork_registers = GetRegisterArray(fork);
          base::Vector<int> t_registers = GetRegisterArray(t);
          DCHECK_EQ(fork_registers.length(), t_registers.length());
          std::copy(t_registers.begin(), t_registers.end(),
                    fork_registers.begin());

          if (v8_flags.experimental_regexp_engine_capture_group_opt) {
            base::Vector<uint64_t> fork_quantifier_clocks =
                GetQuantifierClockArray(fork);
            base::Vector<uint64_t> t_fork_quantifier_clocks =
                GetQuantifierClockArray(t);
            DCHECK_EQ(fork_quantifier_clocks.length(),
                      t_fork_quantifier_clocks.length());
            std::copy(t_fork_quantifier_clocks.begin(),
                      t_fork_quantifier_clocks.end(),
                      fork_quantifier_clocks.begin());

            base::Vector<uint64_t> fork_capture_clocks =
                GetCaptureClockArray(fork);
            base::Vector<uint64_t> t_fork_capture_clocks =
                GetCaptureClockArray(t);
            DCHECK_EQ(fork_capture_clocks.length(),
                      t_fork_capture_clocks.length());
            std::copy(t_fork_capture_clocks.begin(),
                      t_fork_capture_clocks.end(), fork_capture_clocks.begin());
          }

          active_threads_.Add(fork, zone_);

          if (v8_flags.experimental_regexp_engine_capture_group_opt) {
            int err_code = CheckMemoryConsumption();
            if (err_code != RegExp::kInternalRegExpSuccess) return err_code;
          }

          ++t.pc;
          break;
        }
        case RegExpInstruction::JMP:
          t.pc = inst.payload.pc;
          break;
        case RegExpInstruction::ACCEPT:
          if (best_match_thread_.has_value()) {
            DestroyThread(*best_match_thread_);
          }
          best_match_thread_ = t;

          for (InterpreterThread s : active_threads_) {
            DestroyThread(s);
          }
          active_threads_.Rewind(0);
          return RegExp::kInternalRegExpSuccess;
        case RegExpInstruction::SET_REGISTER_TO_CP:
          SBXCHECK_BOUNDS(inst.payload.register_index,
                          register_count_per_match_);
          GetRegisterArray(t)[inst.payload.register_index] = input_index_;
          if (v8_flags.experimental_regexp_engine_capture_group_opt) {
            GetCaptureClockArray(t)[inst.payload.register_index] = clock;
          }
          ++t.pc;
          break;
        case RegExpInstruction::CLEAR_REGISTER:
          SBXCHECK_BOUNDS(inst.payload.register_index,
                          register_count_per_match_);
          GetRegisterArray(t)[inst.payload.register_index] =
              kUndefinedRegisterValue;
          ++t.pc;
          break;
        case RegExpInstruction::SET_QUANTIFIER_TO_CLOCK:
          DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
          GetQuantifierClockArray(t)[inst.payload.quantifier_id] = clock;
          ++t.pc;
          break;
        case RegExpInstruction::FILTER_QUANTIFIER:
        case RegExpInstruction::FILTER_GROUP:
        case RegExpInstruction::FILTER_CHILD:
          UNREACHABLE();
        case RegExpInstruction::BEGIN_LOOP:
          t.consumed_since_last_quantifier =
              InterpreterThread::ConsumedCharacter::DidNotConsume;
          ++t.pc;
          break;
        case RegExpInstruction::END_LOOP:
          // If the thread did not consume any character during a whole
          // quantifier iteration,then it must be destroyed, since quantifier
          // repetitions are not allowed to match the empty string.
          if (t.consumed_since_last_quantifier ==
              InterpreterThread::ConsumedCharacter::DidNotConsume) {
            DestroyThread(t);
            return RegExp::kInternalRegExpSuccess;
          }
          ++t.pc;
          break;
        case RegExpInstruction::WRITE_LOOKBEHIND_TABLE:
          // Reaching this instruction means that the current lookbehind thread
          // has found a match and needs to be destroyed. Since the lookbehind
          // is verified at this position, we update the `lookbehind_table_`.
          SBXCHECK_BOUNDS(inst.payload.looktable_index,
                          lookbehind_table_.length());
          lookbehind_table_[inst.payload.looktable_index] = true;
          DestroyThread(t);
          return RegExp::kInternalRegExpSuccess;
        case RegExpInstruction::READ_LOOKBEHIND_TABLE:
          // Destroy the thread if the corresponding lookbehind did or did not
          // complete a match at the current position (depending on whether or
          // not the lookbehind is positive). The thread's priority ensures that
          // all the threads of the lookbehind have already been run at this
          // position.
          const int32_t lookbehind_index =
              inst.payload.read_lookbehind.lookbehind_index();
          SBXCHECK_BOUNDS(lookbehind_index, lookbehind_table_.length());
          if (lookbehind_table_[lookbehind_index] !=
              inst.payload.read_lookbehind.is_positive()) {
            DestroyThread(t);
            return RegExp::kInternalRegExpSuccess;
          }

          ++t.pc;
          break;
      }
    }
  }

  // Run each active thread until it can't continue without further input.
  // `active_threads_` is empty afterwards.  `blocked_threads_` are sorted from
  // low to high priority.
  int RunActiveThreads() {
    while (!active_threads_.is_empty()) {
      int err_code = RunActiveThread(active_threads_.RemoveLast());
      if (err_code != RegExp::kInternalRegExpSuccess) return err_code;
    }

    return RegExp::kInternalRegExpSuccess;
  }

  // Unblock all blocked_threads_ by feeding them an `input_char`.  Should only
  // be called with `input_index_` pointing to the character *after*
  // `input_char` so that `pc_last_input_index_` is updated correctly.
  void FlushBlockedThreads(base::uc16 input_char) {
    // The threads in blocked_threads_ are sorted from high to low priority,
    // but active_threads_ needs to be sorted from low to high priority, so we
    // need to activate blocked threads in reverse order.
    for (int i = blocked_threads_.length() - 1; i >= 0; --i) {
      InterpreterThread t = blocked_threads_[i];
      RegExpInstruction inst = bytecode_[t.pc];
      DCHECK_EQ(inst.opcode, RegExpInstruction::CONSUME_RANGE);
      RegExpInstruction::Uc16Range range = inst.payload.consume_range;
      if (input_char >= range.min && input_char <= range.max) {
        ++t.pc;
        t.consumed_since_last_quantifier =
            InterpreterThread::ConsumedCharacter::DidConsume;
        active_threads_.Add(t, zone_);
      } else {
        DestroyThread(t);
      }
    }
    blocked_threads_.Rewind(0);
  }

  bool FoundMatch() const { return best_match_thread_.has_value(); }

  size_t ApproximateTotalMemoryUsage() {
    return (blocked_threads_.length() + active_threads_.length()) *
           memory_consumption_per_thread_;
  }

  // Checks that the approximative memory usage does not go past a fixed
  // threshold. Returns the appropriate error code.
  int CheckMemoryConsumption() {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);

    // Copmputes an approximation of the total current memory usage of the
    // intepreter. It is based only on the threads' consumption, since the rest
    // is negligible in comparison.
    uint64_t approx = (blocked_threads_.length() + active_threads_.length()) *
                      memory_consumption_per_thread_;

    return (approx <
            v8_flags.experimental_regexp_engine_capture_group_opt_max_memory_usage *
                MB)
               ? RegExp::kInternalRegExpSuccess
               : RegExp::kInternalRegExpException;
  }

  base::Vector<int> GetRegisterArray(InterpreterThread t) {
    return base::Vector<int>(t.register_array_begin, register_count_per_match_);
  }

  base::Vector<uint64_t> GetQuantifierClockArray(InterpreterThread t) {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
    DCHECK_NOT_NULL(t.captures_clock_array_begin);

    return base::Vector<uint64_t>(t.quantifier_clock_array_begin,
                                  quantifier_count_);
  }
  base::Vector<uint64_t> GetCaptureClockArray(InterpreterThread t) {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
    DCHECK_NOT_NULL(t.captures_clock_array_begin);

    return base::Vector<uint64_t>(t.captures_clock_array_begin,
                                  register_count_per_match_);
  }

  int* NewRegisterArrayUninitialized() {
    return register_array_allocator_.allocate(register_count_per_match_);
  }

  int* NewRegisterArray(int fill_value) {
    int* array_begin = NewRegisterArrayUninitialized();
    int* array_end = array_begin + register_count_per_match_;
    std::fill(array_begin, array_end, fill_value);
    return array_begin;
  }

  void FreeRegisterArray(int* register_array_begin) {
    register_array_allocator_.deallocate(register_array_begin,
                                         register_count_per_match_);
  }

  uint64_t* NewQuantifierClockArrayUninitialized() {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
    return quantifier_array_allocator_->allocate(quantifier_count_);
  }

  uint64_t* NewQuantifierClockArray(uint64_t fill_value) {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);

    uint64_t* array_begin = NewQuantifierClockArrayUninitialized();
    uint64_t* array_end = array_begin + quantifier_count_;
    std::fill(array_begin, array_end, fill_value);
    return array_begin;
  }

  void FreeQuantifierClockArray(uint64_t* quantifier_clock_array_begin) {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
    quantifier_array_allocator_->deallocate(quantifier_clock_array_begin,
                                            quantifier_count_);
  }

  uint64_t* NewCaptureClockArrayUninitialized() {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
    return capture_clock_array_allocator_->allocate(register_count_per_match_);
  }

  uint64_t* NewCaptureClockArray(uint64_t fill_value) {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
    uint64_t* array_begin = NewCaptureClockArrayUninitialized();
    uint64_t* array_end = array_begin + register_count_per_match_;
    std::fill(array_begin, array_end, fill_value);
    return array_begin;
  }

  void FreeCaptureClockArray(uint64_t* register_array_begin) {
    DCHECK(v8_flags.experimental_regexp_engine_capture_group_opt);
    capture_clock_array_allocator_->deallocate(register_array_begin,
                                               register_count_per_match_);
  }

  // Creates an `InterpreterThread` at the given pc and allocates its arrays.
  // The register array is initialized to `kUndefinedRegisterValue`. The clocks'
  // arrays are set to `nullptr` if irrelevant, or initialized to 0.
  InterpreterThread NewEmptyThread(int pc) {
    if (v8_flags.experimental_regexp_engine_capture_group_opt) {
      return InterpreterThread(
          pc, NewRegisterArray(kUndefinedRegisterValue),
          NewQuantifierClockArray(0), NewCaptureClockArray(0),
          InterpreterThread::ConsumedCharacter::DidConsume);
    } else {
      return InterpreterThread(
          pc, NewRegisterArray(kUndefinedRegisterValue), nullptr, nullptr,
          InterpreterThread::ConsumedCharacter::DidConsume);
    }
  }

  // Creates an `InterpreterThread` at the given pc and allocates its arrays.
  // The clocks' arrays are set to `nullptr` if irrelevant. All arrays are left
  // uninitialized.
  InterpreterThread NewUninitializedThread(int pc) {
    if (v8_flags.experimental_regexp_engine_capture_group_opt) {
      return InterpreterThread(
          pc, NewRegisterArrayUninitialized(),
          NewQuantifierClockArrayUninitialized(),
          NewCaptureClockArrayUninitialized(),
          InterpreterThread::ConsumedCharacter::DidConsume);
    } else {
      return InterpreterThread(
          pc, NewRegisterArrayUninitialized(), nullptr, nullptr,
          InterpreterThread::ConsumedCharacter::DidConsume);
    }
  }

  base::Vector<int> GetFilteredRegisters(InterpreterThread t) {
    base::Vector<int> registers = GetRegisterArray(t);
    if (!v8_flags.experimental_regexp_engine_capture_group_opt) {
      return registers;
    }

    if (filter_groups_pc_.has_value()) {
      base::Vector<int> filtered_registers(
          NewRegisterArray(kUndefinedRegisterValue), register_count_per_match_);

      filtered_registers[0] = registers[0];
      filtered_registers[1] = registers[1];

      return FilterGroups::Filter(
          *filter_groups_pc_, registers, GetQuantifierClockArray(t),
          GetCaptureClockArray(t), filtered_registers, bytecode_, zone_);
    } else {
      return registers;
    }
  }

  void DestroyThread(InterpreterThread t) {
    FreeRegisterArray(t.register_array_begin);

    if (v8_flags.experimental_regexp_engine_capture_group_opt) {
      FreeQuantifierClockArray(t.quantifier_clock_array_begin);
      FreeCaptureClockArray(t.captures_clock_array_begin);
    }
  }

  // It is redundant to have two threads t, t0 execute at the same PC and
  // consumed_since_last_quantifier values, because one of t, t0 matches iff the
  // other does.  We can thus discard the one with lower priority.  We check
  // whether a thread executed at some PC value by recording for every possible
  // value of PC what the value of input_index_ was the last time a thread
  // executed at PC. If a thread tries to continue execution at a PC value that
  // we have seen before at the current input index, we abort it. (We execute
  // threads with higher priority first, so the second thread is guaranteed to
  // have lower priority.)
  //
  // Check whether we've seen an active thread with a given pc and
  // consumed_since_last_quantifier value since the last increment of
  // `input_index_`.
  bool IsPcProcessed(int pc, typename InterpreterThread::ConsumedCharacter
                                 consumed_since_last_quantifier) {
    switch (consumed_since_last_quantifier) {
      case InterpreterThread::ConsumedCharacter::DidConsume:
        DCHECK_LE(pc_last_input_index_[pc].having_consumed_character,
                  input_index_);
        return pc_last_input_index_[pc].having_consumed_character ==
               input_index_;
      case InterpreterThread::ConsumedCharacter::DidNotConsume:
        DCHECK_LE(pc_last_input_index_[pc].not_having_consumed_character,
                  input_index_);
        return pc_last_input_index_[pc].not_having_consumed_character ==
               input_index_;
    }
  }

  // Mark a pc as having been processed since the last increment of
  // `input_index_`.
  void MarkPcProcessed(int pc, typename InterpreterThread::ConsumedCharacter
                                   consumed_since_last_quantifier) {
    switch (consumed_since_last_quantifier) {
      case InterpreterThread::ConsumedCharacter::DidConsume:
        DCHECK_LE(pc_last_input_index_[pc].having_consumed_character,
                  input_index_);
        pc_last_input_index_[pc].having_consumed_character = input_index_;
        break;
      case InterpreterThread::ConsumedCharacter::DidNotConsume:
        DCHECK_LE(pc_last_input_index_[pc].not_having_consumed_character,
                  input_index_);
        pc_last_input_index_[pc].not_having_consumed_character = input_index_;
        break;
    }
  }

  Isolate* const isolate_;

  const RegExp::CallOrigin call_origin_;

  DisallowGarbageCollection no_gc_;

  Tagged<TrustedByteArray> bytecode_object_;
  base::Vector<const RegExpInstruction> bytecode_;

  // Number of registers used per thread.
  const int register_count_per_match_;

  // Number of quantifiers in the regexp.
  int quantifier_count_;

  Tagged<String> input_object_;
  base::Vector<const Character> input_;
  int input_index_;

  // Global clock counting the total of executed instructions.
  uint64_t clock;

  // Stores the last input index at which a thread was activated for a given pc.
  // Two values are stored, depending on the value
  // consumed_since_last_quantifier of the thread.
  class LastInputIndex {
   public:
    LastInputIndex() : LastInputIndex(-1, -1) {}
    LastInputIndex(int having_consumed_character,
                   int not_having_consumed_character)
        : having_consumed_character(having_consumed_character),
          not_having_consumed_character(not_having_consumed_character) {}

    int having_consumed_character;
    int not_having_consumed_character;
  };

  // pc_last_input_index_[k] records the values of input_index_ the last
  // time a thread t such that t.pc == k was activated for both values of
  // consumed_since_last_quantifier. Thus pc_last_input_index.size() ==
  // bytecode.size(). See also `RunActiveThread`.
  base::Vector<LastInputIndex> pc_last_input_index_;

  // Active threads can potentially (but not necessarily) continue without
  // input.  Sorted from low to high priority.
  ZoneList<InterpreterThread> active_threads_;

  // The pc of a blocked thread points to an instruction that consumes a
  // character. Sorted from high to low priority (so the opposite of
  // `active_threads_`).
  ZoneList<InterpreterThread> blocked_threads_;

  // RecyclingZoneAllocator maintains a linked list through freed allocations
  // for reuse if possible.
  RecyclingZoneAllocator<int> register_array_allocator_;
  std::optional<RecyclingZoneAllocator<uint64_t>> quantifier_array_allocator_;
  std::optional<RecyclingZoneAllocator<uint64_t>>
      capture_clock_array_allocator_;

  // The register array of the best match found so far during the current
  // search.  If several threads ACCEPTed, then this will be the register array
  // of the accepting thread with highest priority.  Should be deallocated with
  // `register_array_allocator_`.
  std::optional<InterpreterThread> best_match_thread_;

  // Starting PC of each of the lookbehinds in the bytecode. Computed during the
  // NFA instantiation (see the constructor).
  ZoneList<int> lookbehind_pc_;

  // PC of the first FILTER_* instruction. Computed during the NFA instantiation
  // (see the constructor). May be empty if their are no such instructions (in
  // the case where there are no capture groups or quantifiers).
  std::optional<int> filter_groups_pc_;

  // Truth table for the lookbehinds. lookbehind_table_[k] indicates whether the
  // lookbehind of index k did complete a match on the current position.
  ZoneList<bool> lookbehind_table_;

  uint64_t memory_consumption_per_thread_;

  Zone* zone_;
};

}  // namespace

int ExperimentalRegExpInterpreter::FindMatches(
    Isolate* isolate, RegExp::CallOrigin call_origin,
    Tagged<TrustedByteArray> bytecode, int register_count_per_match,
    Tagged<String> input, int start_index, int32_t* output_registers,
    int output_register_count, Zone* zone) {
  DCHECK(input->IsFlat());
  DisallowGarbageCollection no_gc;

  if (input->GetFlatContent(no_gc).IsOneByte()) {
    NfaInterpreter<uint8_t> interpreter(isolate, call_origin, bytecode,
                                        register_count_per_match, input,
                                        start_index, zone);
    return interpreter.FindMatches(output_registers, output_register_count);
  } else {
    DCHECK(input->GetFlatContent(no_gc).IsTwoByte());
    NfaInterpreter<base::uc16> interpreter(isolate, call_origin, bytecode,
                                           register_count_per_match, input,
                                           start_index, zone);
    return interpreter.FindMatches(output_registers, output_register_count);
  }
}

}  // namespace internal
}  // namespace v8
