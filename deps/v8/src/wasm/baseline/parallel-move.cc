// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/baseline/parallel-move.h"

#include "src/wasm/baseline/liftoff-assembler-inl.h"

namespace v8::internal::wasm {

void ParallelMove::TransferToStack(int dst_offset, const VarState& src) {
  switch (src.loc()) {
    case VarState::kStack:
      if (src.offset() != dst_offset) {
        asm_->MoveStackValue(dst_offset, src.offset(), src.kind());
      }
      break;
    case VarState::kRegister:
      asm_->Spill(dst_offset, src.reg(), src.kind());
      break;
    case VarState::kIntConst:
      asm_->Spill(dst_offset, src.constant());
      break;
  }
}

void ParallelMove::ExecuteMoves() {
  // Execute all moves whose {dst} is not being used as src in another move.
  // If any src count drops to zero, also (transitively) execute the
  // corresponding move to that register.
  for (LiftoffRegister dst : move_dst_regs_) {
    // Check if already handled via transitivity in {ClearExecutedMove}.
    if (!move_dst_regs_.has(dst)) continue;
    if (*src_reg_use_count(dst)) continue;
    ExecuteMove(dst);
  }

  // All remaining moves are parts of a cycle. Just spill the first one, then
  // process all remaining moves in that cycle. Repeat for all cycles.
  while (!move_dst_regs_.is_empty()) {
    // TODO(clemensb): Use an unused register if available.
    LiftoffRegister dst = move_dst_regs_.GetFirstRegSet();
    RegisterMove* move = register_move(dst);
    last_spill_offset_ += LiftoffAssembler::SlotSizeForType(move->kind);
    LiftoffRegister spill_reg = move->src;
    asm_->Spill(last_spill_offset_, spill_reg, move->kind);
    // Remember to reload into the destination register later.
    LoadStackSlot(dst, last_spill_offset_, move->kind);
    ClearExecutedMove(dst);
  }
}

void ParallelMove::ExecuteLoads() {
  for (LiftoffRegister dst : load_dst_regs_) {
    RegisterLoad* load = register_load(dst);
    switch (load->load_kind) {
      case RegisterLoad::kNop:
        break;
      case RegisterLoad::kConstant:
        asm_->LoadConstant(dst, load->kind == kI64
                                    ? WasmValue(int64_t{load->value})
                                    : WasmValue(int32_t{load->value}));
        break;
      case RegisterLoad::kStack:
        if (kNeedS128RegPair && load->kind == kS128) {
          asm_->Fill(LiftoffRegister::ForFpPair(dst.fp()), load->value,
                     load->kind);
        } else {
          asm_->Fill(dst, load->value, load->kind);
        }
        break;
      case RegisterLoad::kLowHalfStack:
        // Half of a register pair, {dst} must be a gp register.
        asm_->FillI64Half(dst.gp(), load->value, kLowWord);
        break;
      case RegisterLoad::kHighHalfStack:
        // Half of a register pair, {dst} must be a gp register.
        asm_->FillI64Half(dst.gp(), load->value, kHighWord);
        break;
    }
  }
  load_dst_regs_ = {};
}

}  // namespace v8::internal::wasm
