// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_PARALLEL_MOVE_H_
#define V8_WASM_BASELINE_PARALLEL_MOVE_H_

#include "src/wasm/baseline/liftoff-assembler.h"
#include "src/wasm/baseline/liftoff-register.h"
#include "src/wasm/wasm-value.h"

namespace v8::internal::wasm {

// ParallelMove is a utility class that encodes multiple moves from registers to
// registers (`RegisterMove`), constants to registers (`RegisterLoad` with
// `LoadKind::kConstant`), or stack slots to registers (other
// `RegisterLoad`s).
// It can handle cyclic moves, e.g., swaps between registers.
// The moves are typically prepared/encoded into an instance via the high-level
// entry point `Transfer`, which takes two Wasm value stack configurations
// (`VarState`) as input.
// Code is actually emitted to the underlying `LiftoffAssembler` only at the
// end via `Execute` or implicitly in the destructor.
class ParallelMove {
  using VarState = LiftoffAssembler::VarState;

  struct RegisterMove {
    LiftoffRegister src;
    ValueKind kind;
    constexpr RegisterMove(LiftoffRegister src, ValueKind kind)
        : src(src), kind(kind) {}
  };

  struct RegisterLoad {
    enum LoadKind : uint8_t {
      kNop,           // no-op, used for high fp of a fp pair.
      kConstant,      // load a constant value into a register.
      kStack,         // fill a register from a stack slot.
      kLowHalfStack,  // fill a register from the low half of a stack slot.
      kHighHalfStack  // fill a register from the high half of a stack slot.
    };

    LoadKind load_kind;
    ValueKind kind;
    // `value` stores the i32 constant value (sign-extended if `kind == kI64`),
    // or stack offset, depending on `load_kind`.
    int32_t value;

    // Named constructors.
    static RegisterLoad Const(ValueKind kind, int32_t constant) {
      V8_ASSUME(kind == kI32 || kind == kI64);
      return {kConstant, kind, constant};
    }
    static RegisterLoad Stack(int32_t offset, ValueKind kind) {
      return {kStack, kind, offset};
    }
    static RegisterLoad HalfStack(int32_t offset, RegPairHalf half) {
      return {half == kLowWord ? kLowHalfStack : kHighHalfStack, kI32, offset};
    }
    static RegisterLoad Nop() {
      // ValueKind does not matter.
      return {kNop, kI32, 0};
    }

   private:
    RegisterLoad(LoadKind load_kind, ValueKind kind, int32_t value)
        : load_kind(load_kind), kind(kind), value(value) {}
  };

 public:
  explicit inline ParallelMove(LiftoffAssembler* wasm_asm);
  ParallelMove(const ParallelMove&) = delete;
  ParallelMove& operator=(const ParallelMove&) = delete;
  V8_INLINE ~ParallelMove() { Execute(); }

  V8_INLINE void Execute() {
    // First, execute register moves. Then load constants and stack values into
    // registers.
    if (!move_dst_regs_.is_empty()) ExecuteMoves();
    DCHECK(move_dst_regs_.is_empty());
    if (!load_dst_regs_.is_empty()) ExecuteLoads();
    DCHECK(load_dst_regs_.is_empty());
    // Tell the compiler that the ParallelMove is empty after this, so it
    // can eliminate a second {Execute} in the destructor.
    bool all_done = move_dst_regs_.is_empty() && load_dst_regs_.is_empty();
    V8_ASSUME(all_done);
  }

  V8_INLINE void Transfer(const VarState& dst, const VarState& src) {
    DCHECK(CompatibleStackSlotTypes(dst.kind(), src.kind()));
    if (dst.is_stack()) {
      if (V8_UNLIKELY(!(src.is_stack() && src.offset() == dst.offset()))) {
        TransferToStack(dst.offset(), src);
      }
    } else if (dst.is_reg()) {
      LoadIntoRegister(dst.reg(), src);
    } else {
      DCHECK(dst.is_const());
      DCHECK_EQ(dst.i32_const(), src.i32_const());
    }
  }

  void TransferToStack(int dst_offset, const VarState& src);

  V8_INLINE void LoadIntoRegister(LiftoffRegister dst, const VarState& src) {
    if (src.is_reg()) {
      DCHECK_EQ(dst.reg_class(), src.reg_class());
      if (dst != src.reg()) MoveRegister(dst, src.reg(), src.kind());
    } else if (src.is_stack()) {
      LoadStackSlot(dst, src.offset(), src.kind());
    } else {
      DCHECK(src.is_const());
      LoadConstant(dst, src.kind(), src.i32_const());
    }
  }

  void LoadI64HalfIntoRegister(LiftoffRegister dst, const VarState& src,
                               RegPairHalf half) {
    // Use CHECK such that the remaining code is statically dead if
    // {kNeedI64RegPair} is false.
    CHECK(kNeedI64RegPair);
    DCHECK_EQ(kI64, src.kind());
    switch (src.loc()) {
      case VarState::kStack:
        LoadI64HalfStackSlot(dst, src.offset(), half);
        break;
      case VarState::kRegister: {
        LiftoffRegister src_half =
            half == kLowWord ? src.reg().low() : src.reg().high();
        if (dst != src_half) MoveRegister(dst, src_half, kI32);
        break;
      }
      case VarState::kIntConst:
        int32_t value = src.i32_const();
        // The high word is the sign extension of the low word.
        if (half == kHighWord) value = value >> 31;
        LoadConstant(dst, kI32, value);
        break;
    }
  }

  void MoveRegister(LiftoffRegister dst, LiftoffRegister src, ValueKind kind) {
    DCHECK_NE(dst, src);
    DCHECK_EQ(dst.reg_class(), src.reg_class());
    DCHECK_EQ(reg_class_for(kind), src.reg_class());
    if (src.is_gp_pair()) {
      DCHECK_EQ(kI64, kind);
      if (dst.low() != src.low()) MoveRegister(dst.low(), src.low(), kI32);
      if (dst.high() != src.high()) MoveRegister(dst.high(), src.high(), kI32);
      return;
    }
    if (src.is_fp_pair()) {
      DCHECK_EQ(kS128, kind);
      if (dst.low() != src.low()) {
        MoveRegister(dst.low(), src.low(), kF64);
        MoveRegister(dst.high(), src.high(), kF64);
      }
      return;
    }
    if (move_dst_regs_.has(dst)) {
      DCHECK_EQ(register_move(dst)->src, src);
      // Check for compatible value kinds.
      // - references can occur with mixed kRef / kRefNull kinds.
      // - FP registers can only occur with f32 / f64 / s128 kinds (mixed kinds
      //   only if they hold the initial zero value).
      // - others must match exactly.
      DCHECK_EQ(is_object_reference(register_move(dst)->kind),
                is_object_reference(kind));
      DCHECK_EQ(dst.is_fp(), register_move(dst)->kind == kF32 ||
                                 register_move(dst)->kind == kF64 ||
                                 register_move(dst)->kind == kS128);
      if (!is_object_reference(kind) && !dst.is_fp()) {
        DCHECK_EQ(register_move(dst)->kind, kind);
      }
      // Potentially upgrade an existing `kF32` move to a `kF64` move.
      if (kind == kF64) register_move(dst)->kind = kF64;
      return;
    }
    move_dst_regs_.set(dst);
    ++*src_reg_use_count(src);
    *register_move(dst) = {src, kind};
  }

  // Note: {constant} will be sign-extended if {kind == kI64}.
  void LoadConstant(LiftoffRegister dst, ValueKind kind, int32_t constant) {
    DCHECK(!load_dst_regs_.has(dst));
    load_dst_regs_.set(dst);
    if (dst.is_gp_pair()) {
      DCHECK_EQ(kI64, kind);
      *register_load(dst.low()) = RegisterLoad::Const(kI32, constant);
      // The high word is either 0 or 0xffffffff.
      *register_load(dst.high()) = RegisterLoad::Const(kI32, constant >> 31);
    } else {
      *register_load(dst) = RegisterLoad::Const(kind, constant);
    }
  }

  void LoadStackSlot(LiftoffRegister dst, int stack_offset, ValueKind kind) {
    V8_ASSUME(stack_offset > 0);
    if (load_dst_regs_.has(dst)) {
      // It can happen that we spilled the same register to different stack
      // slots, and then we reload them later into the same dst register.
      // In that case, it is enough to load one of the stack slots.
      return;
    }
    load_dst_regs_.set(dst);
    // Make sure that we only spill to positions after this stack offset to
    // avoid overwriting the content.
    if (stack_offset > last_spill_offset_) {
      last_spill_offset_ = stack_offset;
    }
    if (dst.is_gp_pair()) {
      DCHECK_EQ(kI64, kind);
      *register_load(dst.low()) =
          RegisterLoad::HalfStack(stack_offset, kLowWord);
      *register_load(dst.high()) =
          RegisterLoad::HalfStack(stack_offset, kHighWord);
    } else if (dst.is_fp_pair()) {
      DCHECK_EQ(kS128, kind);
      // Only need register_load for low_gp since we load 128 bits at one go.
      // Both low and high need to be set in load_dst_regs_ but when iterating
      // over it, both low and high will be cleared, so we won't load twice.
      *register_load(dst.low()) = RegisterLoad::Stack(stack_offset, kind);
      *register_load(dst.high()) = RegisterLoad::Nop();
    } else {
      *register_load(dst) = RegisterLoad::Stack(stack_offset, kind);
    }
  }

  void LoadI64HalfStackSlot(LiftoffRegister dst, int offset, RegPairHalf half) {
    if (load_dst_regs_.has(dst)) {
      // It can happen that we spilled the same register to different stack
      // slots, and then we reload them later into the same dst register.
      // In that case, it is enough to load one of the stack slots.
      return;
    }
    load_dst_regs_.set(dst);
    *register_load(dst) = RegisterLoad::HalfStack(offset, half);
  }

 private:
  using MovesStorage =
      std::aligned_storage<kAfterMaxLiftoffRegCode * sizeof(RegisterMove),
                           alignof(RegisterMove)>::type;
  using LoadsStorage =
      std::aligned_storage<kAfterMaxLiftoffRegCode * sizeof(RegisterLoad),
                           alignof(RegisterLoad)>::type;

  ASSERT_TRIVIALLY_COPYABLE(RegisterMove);
  ASSERT_TRIVIALLY_COPYABLE(RegisterLoad);

  MovesStorage register_moves_;  // uninitialized
  LoadsStorage register_loads_;  // uninitialized
  int src_reg_use_count_[kAfterMaxLiftoffRegCode] = {0};
  LiftoffRegList move_dst_regs_;
  LiftoffRegList load_dst_regs_;
  LiftoffAssembler* const asm_;
  // Cache the last spill offset in case we need to spill for resolving move
  // cycles.
  int last_spill_offset_;

  RegisterMove* register_move(LiftoffRegister reg) {
    return reinterpret_cast<RegisterMove*>(&register_moves_) +
           reg.liftoff_code();
  }
  RegisterLoad* register_load(LiftoffRegister reg) {
    return reinterpret_cast<RegisterLoad*>(&register_loads_) +
           reg.liftoff_code();
  }
  int* src_reg_use_count(LiftoffRegister reg) {
    return src_reg_use_count_ + reg.liftoff_code();
  }

  void ExecuteMove(LiftoffRegister dst) {
    RegisterMove* move = register_move(dst);
    DCHECK_EQ(0, *src_reg_use_count(dst));
    asm_->Move(dst, move->src, move->kind);
    ClearExecutedMove(dst);
  }

  void ClearExecutedMove(LiftoffRegister dst) {
    DCHECK(move_dst_regs_.has(dst));
    move_dst_regs_.clear(dst);
    RegisterMove* move = register_move(dst);
    DCHECK_LT(0, *src_reg_use_count(move->src));
    if (--*src_reg_use_count(move->src)) return;
    // src count dropped to zero. If this is a destination register, execute
    // that move now.
    if (!move_dst_regs_.has(move->src)) return;
    ExecuteMove(move->src);
  }

  V8_NOINLINE V8_PRESERVE_MOST void ExecuteMoves();

  V8_NOINLINE V8_PRESERVE_MOST void ExecuteLoads();
};

}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_PARALLEL_MOVE_H_
