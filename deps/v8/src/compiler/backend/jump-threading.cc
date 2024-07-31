// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/jump-threading.h"
#include "src/compiler/backend/code-generator-impl.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(...)                                    \
  do {                                                \
    if (v8_flags.trace_turbo_jt) PrintF(__VA_ARGS__); \
  } while (false)

namespace {

struct JumpThreadingState {
  bool forwarded;
  ZoneVector<RpoNumber>& result;
  ZoneStack<RpoNumber>& stack;

  void Clear(size_t count) { result.assign(count, unvisited()); }
  void PushIfUnvisited(RpoNumber num) {
    if (result[num.ToInt()] == unvisited()) {
      stack.push(num);
      result[num.ToInt()] = onstack();
    }
  }
  void Forward(RpoNumber to) {
    RpoNumber from = stack.top();
    RpoNumber to_to = result[to.ToInt()];
    bool pop = true;
    if (to == from) {
      TRACE("  xx %d\n", from.ToInt());
      result[from.ToInt()] = from;
    } else if (to_to == unvisited()) {
      TRACE("  fw %d -> %d (recurse)\n", from.ToInt(), to.ToInt());
      stack.push(to);
      result[to.ToInt()] = onstack();
      pop = false;  // recurse.
    } else if (to_to == onstack()) {
      TRACE("  fw %d -> %d (cycle)\n", from.ToInt(), to.ToInt());
      result[from.ToInt()] = to;  // break the cycle.
      forwarded = true;
    } else {
      TRACE("  fw %d -> %d (forward)\n", from.ToInt(), to.ToInt());
      result[from.ToInt()] = to_to;  // forward the block.
      forwarded = true;
    }
    if (pop) stack.pop();
  }
  RpoNumber unvisited() { return RpoNumber::FromInt(-1); }
  RpoNumber onstack() { return RpoNumber::FromInt(-2); }
};

struct GapJumpRecord {
  explicit GapJumpRecord(Zone* zone) : zone_(zone), gap_jump_records_(zone) {}

  struct Record {
    RpoNumber block;
    Instruction* instr;
  };

  struct RpoNumberHash {
    std::size_t operator()(const RpoNumber& key) const {
      return std::hash<int>()(key.ToInt());
    }
  };

  bool CanForwardGapJump(Instruction* instr, RpoNumber instr_block,
                         RpoNumber target_block, RpoNumber* forward_to) {
    DCHECK_EQ(instr->arch_opcode(), kArchJmp);
    bool can_forward = false;
    auto search = gap_jump_records_.find(target_block);
    if (search != gap_jump_records_.end()) {
      for (Record& record : search->second) {
        Instruction* record_instr = record.instr;
        DCHECK_EQ(record_instr->arch_opcode(), kArchJmp);
        bool is_same_instr = true;
        for (int i = Instruction::FIRST_GAP_POSITION;
             i <= Instruction::LAST_GAP_POSITION; i++) {
          Instruction::GapPosition pos =
              static_cast<Instruction::GapPosition>(i);
          ParallelMove* record_move = record_instr->GetParallelMove(pos);
          ParallelMove* instr_move = instr->GetParallelMove(pos);
          if (record_move == nullptr && instr_move == nullptr) continue;
          if (((record_move == nullptr) != (instr_move == nullptr)) ||
              !record_move->Equals(*instr_move)) {
            is_same_instr = false;
            break;
          }
        }
        if (is_same_instr) {
          // Found an instruction same as the recorded one.
          *forward_to = record.block;
          can_forward = true;
          break;
        }
      }
      if (!can_forward) {
        // No recorded instruction has been found for this target block,
        // so create a new record with the given instruction.
        search->second.push_back({instr_block, instr});
      }
    } else {
      // This is the first explored gap jump to target block.
      auto ins =
          gap_jump_records_.insert({target_block, ZoneVector<Record>(zone_)});
      if (ins.second) {
        ins.first->second.reserve(4);
        ins.first->second.push_back({instr_block, instr});
      }
    }
    return can_forward;
  }

  Zone* zone_;
  ZoneUnorderedMap<RpoNumber, ZoneVector<Record>, RpoNumberHash>
      gap_jump_records_;
};

}  // namespace

bool JumpThreading::ComputeForwarding(Zone* local_zone,
                                      ZoneVector<RpoNumber>* result,
                                      InstructionSequence* code,
                                      bool frame_at_start) {
  ZoneStack<RpoNumber> stack(local_zone);
  JumpThreadingState state = {false, *result, stack};
  state.Clear(code->InstructionBlockCount());
  RpoNumber empty_deconstruct_frame_return_block = RpoNumber::Invalid();
  int32_t empty_deconstruct_frame_return_size;
  RpoNumber empty_no_deconstruct_frame_return_block = RpoNumber::Invalid();
  int32_t empty_no_deconstruct_frame_return_size;
  GapJumpRecord record(local_zone);

  // Iterate over the blocks forward, pushing the blocks onto the stack.
  for (auto const instruction_block : code->instruction_blocks()) {
    RpoNumber current = instruction_block->rpo_number();
    state.PushIfUnvisited(current);

    // Process the stack, which implements DFS through empty blocks.
    while (!state.stack.empty()) {
      InstructionBlock* block = code->InstructionBlockAt(state.stack.top());
      // Process the instructions in a block up to a non-empty instruction.
      TRACE("jt [%d] B%d\n", static_cast<int>(stack.size()),
            block->rpo_number().ToInt());
      RpoNumber fw = block->rpo_number();
      for (int i = block->code_start(); i < block->code_end(); ++i) {
        Instruction* instr = code->InstructionAt(i);
        if (!instr->AreMovesRedundant()) {
          TRACE("  parallel move");
          // can't skip instructions with non redundant moves, except when we
          // can forward to a block with identical gap-moves.
          if (instr->arch_opcode() == kArchJmp) {
            TRACE(" jmp");
            RpoNumber forward_to;
            if ((frame_at_start || !(block->must_deconstruct_frame() ||
                                     block->must_construct_frame())) &&
                record.CanForwardGapJump(instr, block->rpo_number(),
                                         code->InputRpo(instr, 0),
                                         &forward_to)) {
              DCHECK(forward_to.IsValid());
              fw = forward_to;
              TRACE("\n  merge B%d into B%d", block->rpo_number().ToInt(),
                    forward_to.ToInt());
            }
          }
          TRACE("\n");
        } else if (FlagsModeField::decode(instr->opcode()) != kFlags_none) {
          // can't skip instructions with flags continuations.
          TRACE("  flags\n");
        } else if (instr->IsNop()) {
          // skip nops.
          TRACE("  nop\n");
          continue;
        } else if (instr->arch_opcode() == kArchJmp) {
          // try to forward the jump instruction.
          TRACE("  jmp\n");
          // if this block deconstructs the frame, we can't forward it.
          // TODO(mtrofin): we can still forward if we end up building
          // the frame at start. So we should move the decision of whether
          // to build a frame or not in the register allocator, and trickle it
          // here and to the code generator.
          if (frame_at_start || !(block->must_deconstruct_frame() ||
                                  block->must_construct_frame())) {
            fw = code->InputRpo(instr, 0);
          }
        } else if (instr->IsRet()) {
          TRACE("  ret\n");
          CHECK_IMPLIES(block->must_construct_frame(),
                        block->must_deconstruct_frame());
          // Only handle returns with immediate/constant operands, since
          // they must always be the same for all returns in a function.
          // Dynamic return values might use different registers at
          // different return sites and therefore cannot be shared.
          if (instr->InputAt(0)->IsImmediate()) {
            int32_t return_size =
                ImmediateOperand::cast(instr->InputAt(0))->inline_int32_value();
            // Instructions can be shared only for blocks that share
            // the same |must_deconstruct_frame| attribute.
            if (block->must_deconstruct_frame()) {
              if (empty_deconstruct_frame_return_block ==
                  RpoNumber::Invalid()) {
                empty_deconstruct_frame_return_block = block->rpo_number();
                empty_deconstruct_frame_return_size = return_size;
              } else if (empty_deconstruct_frame_return_size == return_size) {
                fw = empty_deconstruct_frame_return_block;
                block->clear_must_deconstruct_frame();
              }
            } else {
              if (empty_no_deconstruct_frame_return_block ==
                  RpoNumber::Invalid()) {
                empty_no_deconstruct_frame_return_block = block->rpo_number();
                empty_no_deconstruct_frame_return_size = return_size;
              } else if (empty_no_deconstruct_frame_return_size ==
                         return_size) {
                fw = empty_no_deconstruct_frame_return_block;
              }
            }
          }
        } else {
          // can't skip other instructions.
          TRACE("  other\n");
        }
        break;
      }
      state.Forward(fw);
    }
  }

#ifdef DEBUG
  for (RpoNumber num : *result) {
    DCHECK(num.IsValid());
  }
#endif

  if (v8_flags.trace_turbo_jt) {
    for (int i = 0; i < static_cast<int>(result->size()); i++) {
      TRACE("B%d ", i);
      int to = (*result)[i].ToInt();
      if (i != to) {
        TRACE("-> B%d\n", to);
      } else {
        TRACE("\n");
      }
    }
  }

  return state.forwarded;
}

void JumpThreading::ApplyForwarding(Zone* local_zone,
                                    ZoneVector<RpoNumber> const& result,
                                    InstructionSequence* code) {
  if (!v8_flags.turbo_jt) return;

  // Skip empty blocks except for the first block.
  int ao = 0;
  for (auto const block : code->ao_blocks()) {
    RpoNumber block_rpo = block->rpo_number();
    int block_num = block_rpo.ToInt();
    RpoNumber result_rpo = result[block_num];
    bool skip = block_rpo != RpoNumber::FromInt(0) && result_rpo != block_rpo;

    if (result_rpo != block_rpo) {
      // We need the handler information to be propagated, so that branch
      // targets are annotated as necessary for control flow integrity
      // checks (when enabled).
      if (code->InstructionBlockAt(block_rpo)->IsHandler()) {
        code->InstructionBlockAt(result_rpo)->MarkHandler();
      }
    }

    if (skip) {
      for (int i = block->code_start(); i < block->code_end(); ++i) {
        Instruction* instr = code->InstructionAt(i);
        DCHECK_NE(FlagsModeField::decode(instr->opcode()), kFlags_branch);
        if (instr->arch_opcode() == kArchJmp ||
            instr->arch_opcode() == kArchRet) {
          // Overwrite a redundant jump with a nop.
          TRACE("jt-fw nop @%d\n", i);
          instr->OverwriteWithNop();
          // Eliminate all the ParallelMoves.
          for (int i = Instruction::FIRST_GAP_POSITION;
               i <= Instruction::LAST_GAP_POSITION; i++) {
            Instruction::GapPosition pos =
                static_cast<Instruction::GapPosition>(i);
            ParallelMove* instr_move = instr->GetParallelMove(pos);
            if (instr_move != nullptr) {
              instr_move->Eliminate();
            }
          }
          // If this block was marked as a handler, it can be unmarked now.
          code->InstructionBlockAt(block_rpo)->UnmarkHandler();
          code->InstructionBlockAt(block_rpo)->set_omitted_by_jump_threading();
        }
      }
    }

    // Renumber the blocks so that IsNextInAssemblyOrder() will return true,
    // even if there are skipped blocks in-between.
    block->set_ao_number(RpoNumber::FromInt(ao));
    if (!skip) ao++;
  }

  // Patch RPO immediates.
  InstructionSequence::RpoImmediates& rpo_immediates = code->rpo_immediates();
  for (size_t i = 0; i < rpo_immediates.size(); i++) {
    RpoNumber rpo = rpo_immediates[i];
    if (rpo.IsValid()) {
      RpoNumber fw = result[rpo.ToInt()];
      if (fw != rpo) rpo_immediates[i] = fw;
    }
  }
}

#undef TRACE

}  // namespace compiler
}  // namespace internal
}  // namespace v8
