// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the
// distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

// A light-weight ARM Assembler
// Generates user mode instructions for the ARM architecture up to version 5

#ifndef V8_CODEGEN_ARM_ASSEMBLER_ARM_H_
#define V8_CODEGEN_ARM_ASSEMBLER_ARM_H_

#include <stdio.h>

#include <memory>

#include "src/base/numbers/double.h"
#include "src/base/small-vector.h"
#include "src/codegen/arm/constants-arm.h"
#include "src/codegen/arm/register-arm.h"
#include "src/codegen/assembler.h"
#include "src/codegen/constant-pool.h"
#include "src/codegen/machine-type.h"
#include "src/utils/boxed-float.h"
namespace v8 {
namespace internal {

class SafepointTableBuilder;

// Coprocessor number
enum Coprocessor {
  p0 = 0,
  p1 = 1,
  p2 = 2,
  p3 = 3,
  p4 = 4,
  p5 = 5,
  p6 = 6,
  p7 = 7,
  p8 = 8,
  p9 = 9,
  p10 = 10,
  p11 = 11,
  p12 = 12,
  p13 = 13,
  p14 = 14,
  p15 = 15
};

// -----------------------------------------------------------------------------
// Machine instruction Operands

// Class Operand represents a shifter operand in data processing instructions
class V8_EXPORT_PRIVATE Operand {
 public:
  // immediate
  V8_INLINE explicit Operand(int32_t immediate,
                             RelocInfo::Mode rmode = RelocInfo::NO_INFO)
      : rmode_(rmode) {
    value_.immediate = immediate;
  }
  V8_INLINE static Operand Zero();
  V8_INLINE explicit Operand(const ExternalReference& f);
  explicit Operand(Handle<HeapObject> handle);
  V8_INLINE explicit Operand(Tagged<Smi> value);

  // rm
  V8_INLINE explicit Operand(Register rm);

  // rm <shift_op> shift_imm
  explicit Operand(Register rm, ShiftOp shift_op, int shift_imm);
  V8_INLINE static Operand SmiUntag(Register rm) {
    return Operand(rm, ASR, kSmiTagSize);
  }
  V8_INLINE static Operand PointerOffsetFromSmiKey(Register key) {
    static_assert(kSmiTag == 0 && kSmiTagSize < kPointerSizeLog2);
    return Operand(key, LSL, kPointerSizeLog2 - kSmiTagSize);
  }
  V8_INLINE static Operand DoubleOffsetFromSmiKey(Register key) {
    static_assert(kSmiTag == 0 && kSmiTagSize < kDoubleSizeLog2);
    return Operand(key, LSL, kDoubleSizeLog2 - kSmiTagSize);
  }

  // rm <shift_op> rs
  explicit Operand(Register rm, ShiftOp shift_op, Register rs);

  static Operand EmbeddedNumber(double number);  // Smi or HeapNumber.

  // Return true if this is a register operand.
  bool IsRegister() const {
    return rm_.is_valid() && rs_ == no_reg && shift_op_ == LSL &&
           shift_imm_ == 0;
  }
  // Return true if this is a register operand shifted with an immediate.
  bool IsImmediateShiftedRegister() const {
    return rm_.is_valid() && !rs_.is_valid();
  }
  // Return true if this is a register operand shifted with a register.
  bool IsRegisterShiftedRegister() const {
    return rm_.is_valid() && rs_.is_valid();
  }

  // Return the number of actual instructions required to implement the given
  // instruction for this particular operand. This can be a single instruction,
  // if no load into a scratch register is necessary, or anything between 2 and
  // 4 instructions when we need to load from the constant pool (depending upon
  // whether the constant pool entry is in the small or extended section). If
  // the instruction this operand is used for is a MOV or MVN instruction the
  // actual instruction to use is required for this calculation. For other
  // instructions instr is ignored.
  //
  // The value returned is only valid as long as no entries are added to the
  // constant pool between this call and the actual instruction being emitted.
  int InstructionsRequired(const Assembler* assembler, Instr instr = 0) const;
  bool MustOutputRelocInfo(const Assembler* assembler) const;

  inline int32_t immediate() const {
    DCHECK(IsImmediate());
    DCHECK(!IsHeapNumberRequest());
    return value_.immediate;
  }
  bool IsImmediate() const { return !rm_.is_valid(); }

  HeapNumberRequest heap_number_request() const {
    DCHECK(IsHeapNumberRequest());
    return value_.heap_number_request;
  }
  bool IsHeapNumberRequest() const {
    DCHECK_IMPLIES(is_heap_number_request_, IsImmediate());
    DCHECK_IMPLIES(is_heap_number_request_,
                   rmode_ == RelocInfo::FULL_EMBEDDED_OBJECT ||
                       rmode_ == RelocInfo::CODE_TARGET);
    return is_heap_number_request_;
  }

  Register rm() const { return rm_; }
  Register rs() const { return rs_; }
  ShiftOp shift_op() const { return shift_op_; }

 private:
  Register rm_ = no_reg;
  Register rs_ = no_reg;
  ShiftOp shift_op_;
  int shift_imm_;  // valid if rm_ != no_reg && rs_ == no_reg
  union Value {
    Value() {}
    HeapNumberRequest heap_number_request;  // if is_heap_number_request_
    int32_t immediate;                      // otherwise
  } value_;                                 // valid if rm_ == no_reg
  bool is_heap_number_request_ = false;
  RelocInfo::Mode rmode_;

  friend class Assembler;
};

// Class MemOperand represents a memory operand in load and store instructions
class V8_EXPORT_PRIVATE MemOperand {
 public:
  // [rn +/- offset]      Offset/NegOffset
  // [rn +/- offset]!     PreIndex/NegPreIndex
  // [rn], +/- offset     PostIndex/NegPostIndex
  // offset is any signed 32-bit value; offset is first loaded to a scratch
  // register if it does not fit the addressing mode (12-bit unsigned and sign
  // bit)
  explicit MemOperand(Register rn, int32_t offset = 0, AddrMode am = Offset);

  // [rn +/- rm]          Offset/NegOffset
  // [rn +/- rm]!         PreIndex/NegPreIndex
  // [rn], +/- rm         PostIndex/NegPostIndex
  explicit MemOperand(Register rn, Register rm, AddrMode am = Offset);

  // [rn +/- rm <shift_op> shift_imm]      Offset/NegOffset
  // [rn +/- rm <shift_op> shift_imm]!     PreIndex/NegPreIndex
  // [rn], +/- rm <shift_op> shift_imm     PostIndex/NegPostIndex
  explicit MemOperand(Register rn, Register rm, ShiftOp shift_op, int shift_imm,
                      AddrMode am = Offset);
  V8_INLINE static MemOperand PointerAddressFromSmiKey(Register array,
                                                       Register key,
                                                       AddrMode am = Offset) {
    static_assert(kSmiTag == 0 && kSmiTagSize < kPointerSizeLog2);
    return MemOperand(array, key, LSL, kPointerSizeLog2 - kSmiTagSize, am);
  }

  bool IsImmediateOffset() const { return rm_ == no_reg; }

  void set_offset(int32_t offset) {
    DCHECK(IsImmediateOffset());
    offset_ = offset;
  }

  int32_t offset() const {
    DCHECK(IsImmediateOffset());
    return offset_;
  }

  Register rn() const { return rn_; }
  Register rm() const { return rm_; }
  AddrMode am() const { return am_; }

  bool OffsetIsUint12Encodable() const {
    return offset_ >= 0 ? is_uint12(offset_) : is_uint12(-offset_);
  }

 private:
  Register rn_;     // base
  Register rm_;     // register offset
  int32_t offset_;  // valid if rm_ == no_reg
  ShiftOp shift_op_;
  int shift_imm_;  // valid if rm_ != no_reg && rs_ == no_reg
  AddrMode am_;    // bits P, U, and W

  friend class Assembler;
};

// Class NeonMemOperand represents a memory operand in load and
// store NEON instructions
class V8_EXPORT_PRIVATE NeonMemOperand {
 public:
  // [rn {:align}]       Offset
  // [rn {:align}]!      PostIndex
  explicit NeonMemOperand(Register rn, AddrMode am = Offset, int align = 0);

  // [rn {:align}], rm   PostIndex
  explicit NeonMemOperand(Register rn, Register rm, int align = 0);

  Register rn() const { return rn_; }
  Register rm() const { return rm_; }
  int align() const { return align_; }

 private:
  void SetAlignment(int align);

  Register rn_;  // base
  Register rm_;  // register increment
  int align_;
};

// Class NeonListOperand represents a list of NEON registers
class NeonListOperand {
 public:
  explicit NeonListOperand(DoubleRegister base, int register_count = 1)
      : base_(base), register_count_(register_count) {}
  explicit NeonListOperand(QwNeonRegister q_reg)
      : base_(q_reg.low()), register_count_(2) {}
  DoubleRegister base() const { return base_; }
  int register_count() { return register_count_; }
  int length() const { return register_count_ - 1; }
  NeonListType type() const {
    switch (register_count_) {
      default:
        UNREACHABLE();
      // Fall through.
      case 1:
        return nlt_1;
      case 2:
        return nlt_2;
      case 3:
        return nlt_3;
      case 4:
        return nlt_4;
    }
  }

 private:
  DoubleRegister base_;
  int register_count_;
};

class V8_EXPORT_PRIVATE Assembler : public AssemblerBase {
 public:
  // Create an assembler. Instructions and relocation information are emitted
  // into a buffer, with the instructions starting from the beginning and the
  // relocation information starting from the end of the buffer. See CodeDesc
  // for a detailed comment on the layout (globals.h).
  //
  // If the provided buffer is nullptr, the assembler allocates and grows its
  // own buffer. Otherwise it takes ownership of the provided buffer.
  explicit Assembler(const AssemblerOptions&,
                     std::unique_ptr<AssemblerBuffer> = {});
  // For compatibility with assemblers that require a zone.
  Assembler(const MaybeAssemblerZone&, const AssemblerOptions& options,
            std::unique_ptr<AssemblerBuffer> buffer = {})
      : Assembler(options, std::move(buffer)) {}

  ~Assembler() override;

  static RegList DefaultTmpList();
  static VfpRegList DefaultFPTmpList();

  void AbortedCodeGeneration() override {
    pending_32_bit_constants_.clear();
    first_const_pool_32_use_ = -1;
    constant_pool_deadline_ = kMaxInt;
  }

  // GetCode emits any pending (non-emitted) code and fills the descriptor desc.
  static constexpr int kNoHandlerTable = 0;
  static constexpr SafepointTableBuilderBase* kNoSafepointTable = nullptr;
  void GetCode(LocalIsolate* isolate, CodeDesc* desc,
               SafepointTableBuilderBase* safepoint_table_builder,
               int handler_table_offset);

  // Convenience wrapper for allocating with an Isolate.
  void GetCode(Isolate* isolate, CodeDesc* desc);
  // Convenience wrapper for code without safepoint or handler tables.
  void GetCode(LocalIsolate* isolate, CodeDesc* desc) {
    GetCode(isolate, desc, kNoSafepointTable, kNoHandlerTable);
  }

  // Label operations & relative jumps (PPUM Appendix D)
  //
  // Takes a branch opcode (cc) and a label (L) and generates
  // either a backward branch or a forward branch and links it
  // to the label fixup chain. Usage:
  //
  // Label L;    // unbound label
  // j(cc, &L);  // forward branch to unbound label
  // bind(&L);   // bind label to the current pc
  // j(cc, &L);  // backward branch to bound label
  // bind(&L);   // illegal: a label may be bound only once
  //
  // Note: The same Label can be used for forward and backward branches
  // but it may be bound only once.

  void bind(Label* L);  // binds an unbound label L to the current code position

  // Returns the branch offset to the given label from the current code position
  // Links the label to the current position if it is still unbound
  // Manages the jump elimination optimization if the second parameter is true.
  int branch_offset(Label* L);

  // Returns true if the given pc address is the start of a constant pool load
  // instruction sequence.
  V8_INLINE static bool is_constant_pool_load(Address pc);

  // Return the address in the constant pool of the code target address used by
  // the branch/call instruction at pc, or the object in a mov.
  V8_INLINE static Address constant_pool_entry_address(Address pc,
                                                       Address constant_pool);

  // Read/Modify the code target address in the branch/call instruction at pc.
  // The isolate argument is unused (and may be nullptr) when skipping flushing.
  V8_INLINE static Address target_address_at(Address pc, Address constant_pool);
  V8_INLINE static void set_target_address_at(
      Address pc, Address constant_pool, Address target,
      WritableJitAllocation* jit_allocation,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  // Get the size of the special target encoded at 'location'.
  inline static int deserialization_special_target_size(Address location);

  // This sets the internal reference at the pc.
  inline static void deserialization_set_target_internal_reference_at(
      Address pc, Address target, WritableJitAllocation& jit_allocation,
      RelocInfo::Mode mode = RelocInfo::INTERNAL_REFERENCE);

  // Read/modify the uint32 constant used at pc.
  static inline uint32_t uint32_constant_at(Address pc, Address constant_pool);
  static inline void set_uint32_constant_at(
      Address pc, Address constant_pool, uint32_t new_constant,
      WritableJitAllocation* jit_allocation,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  // Here we are patching the address in the constant pool, not the actual call
  // instruction.  The address in the constant pool is the same size as a
  // pointer.
  static constexpr int kSpecialTargetSize = kPointerSize;

  RegList* GetScratchRegisterList() { return &scratch_register_list_; }
  VfpRegList* GetScratchVfpRegisterList() {
    return &scratch_vfp_register_list_;
  }

  // ---------------------------------------------------------------------------
  // InstructionStream generation

  // Insert the smallest number of nop instructions
  // possible to align the pc offset to a multiple
  // of m. m must be a power of 2 (>= 4).
  void Align(int m);
  // Insert the smallest number of zero bytes possible to align the pc offset
  // to a mulitple of m. m must be a power of 2 (>= 2).
  void DataAlign(int m);
  // Aligns code to something that's optimal for a jump target for the platform.
  void CodeTargetAlign();
  void LoopHeaderAlign() { CodeTargetAlign(); }

  // Branch instructions
  void b(int branch_offset, Condition cond = al,
         RelocInfo::Mode rmode = RelocInfo::NO_INFO);
  void bl(int branch_offset, Condition cond = al,
          RelocInfo::Mode rmode = RelocInfo::NO_INFO);
  void blx(int branch_offset);                     // v5 and above
  void blx(Register target, Condition cond = al);  // v5 and above
  void bx(Register target, Condition cond = al);   // v5 and above, plus v4t

  // Convenience branch instructions using labels
  void b(Label* L, Condition cond = al);
  void b(Condition cond, Label* L) { b(L, cond); }
  void bl(Label* L, Condition cond = al);
  void bl(Condition cond, Label* L) { bl(L, cond); }
  void blx(Label* L);  // v5 and above

  // Data-processing instructions

  void and_(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
            Condition cond = al);
  void and_(Register dst, Register src1, Register src2, SBit s = LeaveCC,
            Condition cond = al);

  void eor(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);
  void eor(Register dst, Register src1, Register src2, SBit s = LeaveCC,
           Condition cond = al);

  void sub(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);
  void sub(Register dst, Register src1, Register src2, SBit s = LeaveCC,
           Condition cond = al);

  void rsb(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);

  void add(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);
  void add(Register dst, Register src1, Register src2, SBit s = LeaveCC,
           Condition cond = al);

  void adc(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);

  void sbc(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);

  void rsc(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);

  void tst(Register src1, const Operand& src2, Condition cond = al);
  void tst(Register src1, Register src2, Condition cond = al);

  void teq(Register src1, const Operand& src2, Condition cond = al);

  void cmp(Register src1, const Operand& src2, Condition cond = al);
  void cmp(Register src1, Register src2, Condition cond = al);

  void cmp_raw_immediate(Register src1, int raw_immediate, Condition cond = al);

  void cmn(Register src1, const Operand& src2, Condition cond = al);

  void orr(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);
  void orr(Register dst, Register src1, Register src2, SBit s = LeaveCC,
           Condition cond = al);

  void mov(Register dst, const Operand& src, SBit s = LeaveCC,
           Condition cond = al);
  void mov(Register dst, Register src, SBit s = LeaveCC, Condition cond = al);

  // Load the position of the label relative to the generated code object
  // pointer in a register.
  void mov_label_offset(Register dst, Label* label);

  // ARMv7 instructions for loading a 32 bit immediate in two instructions.
  // The constant for movw and movt should be in the range 0-0xffff.
  void movw(Register reg, uint32_t immediate, Condition cond = al);
  void movt(Register reg, uint32_t immediate, Condition cond = al);

  void bic(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);

  void mvn(Register dst, const Operand& src, SBit s = LeaveCC,
           Condition cond = al);

  // Shift instructions

  void asr(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);

  void lsl(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);

  void lsr(Register dst, Register src1, const Operand& src2, SBit s = LeaveCC,
           Condition cond = al);

  // Multiply instructions

  void mla(Register dst, Register src1, Register src2, Register srcA,
           SBit s = LeaveCC, Condition cond = al);

  void mls(Register dst, Register src1, Register src2, Register srcA,
           Condition cond = al);

  void sdiv(Register dst, Register src1, Register src2, Condition cond = al);

  void udiv(Register dst, Register src1, Register src2, Condition cond = al);

  void mul(Register dst, Register src1, Register src2, SBit s = LeaveCC,
           Condition cond = al);

  void smmla(Register dst, Register src1, Register src2, Register srcA,
             Condition cond = al);

  void smmul(Register dst, Register src1, Register src2, Condition cond = al);

  void smlal(Register dstL, Register dstH, Register src1, Register src2,
             SBit s = LeaveCC, Condition cond = al);

  void smull(Register dstL, Register dstH, Register src1, Register src2,
             SBit s = LeaveCC, Condition cond = al);

  void umlal(Register dstL, Register dstH, Register src1, Register src2,
             SBit s = LeaveCC, Condition cond = al);

  void umull(Register dstL, Register dstH, Register src1, Register src2,
             SBit s = LeaveCC, Condition cond = al);

  // Miscellaneous arithmetic instructions

  void clz(Register dst, Register src, Condition cond = al);  // v5 and above

  // Saturating instructions. v6 and above.

  // Unsigned saturate.
  //
  // Saturate an optionally shifted signed value to an unsigned range.
  //
  //   usat dst, #satpos, src
  //   usat dst, #satpos, src, lsl #sh
  //   usat dst, #satpos, src, asr #sh
  //
  // Register dst will contain:
  //
  //   0,                 if s < 0
  //   (1 << satpos) - 1, if s > ((1 << satpos) - 1)
  //   s,                 otherwise
  //
  // where s is the contents of src after shifting (if used.)
  void usat(Register dst, int satpos, const Operand& src, Condition cond = al);

  // Bitfield manipulation instructions. v7 and above.

  void ubfx(Register dst, Register src, int lsb, int width,
            Condition cond = al);

  void sbfx(Register dst, Register src, int lsb, int width,
            Condition cond = al);

  void bfc(Register dst, int lsb, int width, Condition cond = al);

  void bfi(Register dst, Register src, int lsb, int width, Condition cond = al);

  void pkhbt(Register dst, Register src1, const Operand& src2,
             Condition cond = al);

  void pkhtb(Register dst, Register src1, const Operand& src2,
             Condition cond = al);

  void sxtb(Register dst, Register src, int rotate = 0, Condition cond = al);
  void sxtab(Register dst, Register src1, Register src2, int rotate = 0,
             Condition cond = al);
  void sxth(Register dst, Register src, int rotate = 0, Condition cond = al);
  void sxtah(Register dst, Register src1, Register src2, int rotate = 0,
             Condition cond = al);

  void uxtb(Register dst, Register src, int rotate = 0, Condition cond = al);
  void uxtab(Register dst, Register src1, Register src2, int rotate = 0,
             Condition cond = al);
  void uxtb16(Register dst, Register src, int rotate = 0, Condition cond = al);
  void uxth(Register dst, Register src, int rotate = 0, Condition cond = al);
  void uxtah(Register dst, Register src1, Register src2, int rotate = 0,
             Condition cond = al);

  // Reverse the bits in a register.
  void rbit(Register dst, Register src, Condition cond = al);
  void rev(Register dst, Register src, Condition cond = al);

  // Status register access instructions

  void mrs(Register dst, SRegister s, Condition cond = al);
  void msr(SRegisterFieldMask fields, const Operand& src, Condition cond = al);

  // Load/Store instructions
  void ldr(Register dst, const MemOperand& src, Condition cond = al);
  void str(Register src, const MemOperand& dst, Condition cond = al);
  void ldrb(Register dst, const MemOperand& src, Condition cond = al);
  void strb(Register src, const MemOperand& dst, Condition cond = al);
  void ldrh(Register dst, const MemOperand& src, Condition cond = al);
  void strh(Register src, const MemOperand& dst, Condition cond = al);
  void ldrsb(Register dst, const MemOperand& src, Condition cond = al);
  void ldrsh(Register dst, const MemOperand& src, Condition cond = al);
  void ldrd(Register dst1, Register dst2, const MemOperand& src,
            Condition cond = al);
  void strd(Register src1, Register src2, const MemOperand& dst,
            Condition cond = al);

  // Load literal from a pc relative address.
  void ldr_pcrel(Register dst, int imm12, Condition cond = al);

  // Load/Store exclusive instructions
  void ldrex(Register dst, Register src, Condition cond = al);
  void strex(Register src1, Register src2, Register dst, Condition cond = al);
  void ldrexb(Register dst, Register src, Condition cond = al);
  void strexb(Register src1, Register src2, Register dst, Condition cond = al);
  void ldrexh(Register dst, Register src, Condition cond = al);
  void strexh(Register src1, Register src2, Register dst, Condition cond = al);
  void ldrexd(Register dst1, Register dst2, Register src, Condition cond = al);
  void strexd(Register res, Register src1, Register src2, Register dst,
              Condition cond = al);

  // Preload instructions
  void pld(const MemOperand& address);

  // Load/Store multiple instructions
  void ldm(BlockAddrMode am, Register base, RegList dst, Condition cond = al);
  void stm(BlockAddrMode am, Register base, RegList src, Condition cond = al);

  // Exception-generating instructions and debugging support
  void stop(Condition cond = al, int32_t code = kDefaultStopCode);

  void bkpt(uint32_t imm16);  // v5 and above
  void svc(uint32_t imm24, Condition cond = al);

  // Synchronization instructions.
  // On ARMv6, an equivalent CP15 operation will be used.
  void dmb(BarrierOption option);
  void dsb(BarrierOption option);
  void isb(BarrierOption option);

  // Conditional speculation barrier.
  void csdb();

  // Coprocessor instructions

  void cdp(Coprocessor coproc, int opcode_1, CRegister crd, CRegister crn,
           CRegister crm, int opcode_2, Condition cond = al);

  void cdp2(Coprocessor coproc, int opcode_1, CRegister crd, CRegister crn,
            CRegister crm,
            int opcode_2);  // v5 and above

  void mcr(Coprocessor coproc, int opcode_1, Register rd, CRegister crn,
           CRegister crm, int opcode_2 = 0, Condition cond = al);

  void mcr2(Coprocessor coproc, int opcode_1, Register rd, CRegister crn,
            CRegister crm,
            int opcode_2 = 0);  // v5 and above

  void mrc(Coprocessor coproc, int opcode_1, Register rd, CRegister crn,
           CRegister crm, int opcode_2 = 0, Condition cond = al);

  void mrc2(Coprocessor coproc, int opcode_1, Register rd, CRegister crn,
            CRegister crm,
            int opcode_2 = 0);  // v5 and above

  void ldc(Coprocessor coproc, CRegister crd, const MemOperand& src,
           LFlag l = Short, Condition cond = al);
  void ldc(Coprocessor coproc, CRegister crd, Register base, int option,
           LFlag l = Short, Condition cond = al);

  void ldc2(Coprocessor coproc, CRegister crd, const MemOperand& src,
            LFlag l = Short);  // v5 and above
  void ldc2(Coprocessor coproc, CRegister crd, Register base, int option,
            LFlag l = Short);  // v5 and above

  // Support for VFP.
  // All these APIs support S0 to S31 and D0 to D31.

  void vldr(const DwVfpRegister dst, const Register base, int offset,
            const Condition cond = al);
  void vldr(const DwVfpRegister dst, const MemOperand& src,
            const Condition cond = al);

  void vldr(const SwVfpRegister dst, const Register base, int offset,
            const Condition cond = al);
  void vldr(const SwVfpRegister dst, const MemOperand& src,
            const Condition cond = al);

  void vstr(const DwVfpRegister src, const Register base, int offset,
            const Condition cond = al);
  void vstr(const DwVfpRegister src, const MemOperand& dst,
            const Condition cond = al);

  void vstr(const SwVfpRegister src, const Register base, int offset,
            const Condition cond = al);
  void vstr(const SwVfpRegister src, const MemOperand& dst,
            const Condition cond = al);

  void vldm(BlockAddrMode am, Register base, DwVfpRegister first,
            DwVfpRegister last, Condition cond = al);

  void vstm(BlockAddrMode am, Register base, DwVfpRegister first,
            DwVfpRegister last, Condition cond = al);

  void vldm(BlockAddrMode am, Register base, SwVfpRegister first,
            SwVfpRegister last, Condition cond = al);

  void vstm(BlockAddrMode am, Register base, SwVfpRegister first,
            SwVfpRegister last, Condition cond = al);

  void vmov(const SwVfpRegister dst, Float32 imm);
  void vmov(const DwVfpRegister dst, base::Double imm,
            const Register extra_scratch = no_reg);
  void vmov(const SwVfpRegister dst, const SwVfpRegister src,
            const Condition cond = al);
  void vmov(const DwVfpRegister dst, const DwVfpRegister src,
            const Condition cond = al);
  void vmov(const DwVfpRegister dst, const Register src1, const Register src2,
            const Condition cond = al);
  void vmov(const Register dst1, const Register dst2, const DwVfpRegister src,
            const Condition cond = al);
  void vmov(const SwVfpRegister dst, const Register src,
            const Condition cond = al);
  void vmov(const Register dst, const SwVfpRegister src,
            const Condition cond = al);
  void vcvt_f64_s32(const DwVfpRegister dst, const SwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_f32_s32(const SwVfpRegister dst, const SwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_f64_u32(const DwVfpRegister dst, const SwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_f32_u32(const SwVfpRegister dst, const SwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_s32_f32(const SwVfpRegister dst, const SwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_u32_f32(const SwVfpRegister dst, const SwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_s32_f64(const SwVfpRegister dst, const DwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_u32_f64(const SwVfpRegister dst, const DwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_f64_f32(const DwVfpRegister dst, const SwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_f32_f64(const SwVfpRegister dst, const DwVfpRegister src,
                    VFPConversionMode mode = kDefaultRoundToZero,
                    const Condition cond = al);
  void vcvt_f64_s32(const DwVfpRegister dst, int fraction_bits,
                    const Condition cond = al);

  void vmrs(const Register dst, const Condition cond = al);
  void vmsr(const Register dst, const Condition cond = al);

  void vneg(const DwVfpRegister dst, const DwVfpRegister src,
            const Condition cond = al);
  void vneg(const SwVfpRegister dst, const SwVfpRegister src,
            const Condition cond = al);
  void vabs(const DwVfpRegister dst, const DwVfpRegister src,
            const Condition cond = al);
  void vabs(const SwVfpRegister dst, const SwVfpRegister src,
            const Condition cond = al);
  void vadd(const DwVfpRegister dst, const DwVfpRegister src1,
            const DwVfpRegister src2, const Condition cond = al);
  void vadd(const SwVfpRegister dst, const SwVfpRegister src1,
            const SwVfpRegister src2, const Condition cond = al);
  void vsub(const DwVfpRegister dst, const DwVfpRegister src1,
            const DwVfpRegister src2, const Condition cond = al);
  void vsub(const SwVfpRegister dst, const SwVfpRegister src1,
            const SwVfpRegister src2, const Condition cond = al);
  void vmul(const DwVfpRegister dst, const DwVfpRegister src1,
            const DwVfpRegister src2, const Condition cond = al);
  void vmul(const SwVfpRegister dst, const SwVfpRegister src1,
            const SwVfpRegister src2, const Condition cond = al);
  void vmla(const DwVfpRegister dst, const DwVfpRegister src1,
            const DwVfpRegister src2, const Condition cond = al);
  void vmla(const SwVfpRegister dst, const SwVfpRegister src1,
            const SwVfpRegister src2, const Condition cond = al);
  void vmls(const DwVfpRegister dst, const DwVfpRegister src1,
            const DwVfpRegister src2, const Condition cond = al);
  void vmls(const SwVfpRegister dst, const SwVfpRegister src1,
            const SwVfpRegister src2, const Condition cond = al);
  void vdiv(const DwVfpRegister dst, const DwVfpRegister src1,
            const DwVfpRegister src2, const Condition cond = al);
  void vdiv(const SwVfpRegister dst, const SwVfpRegister src1,
            const SwVfpRegister src2, const Condition cond = al);
  void vcmp(const DwVfpRegister src1, const DwVfpRegister src2,
            const Condition cond = al);
  void vcmp(const SwVfpRegister src1, const SwVfpRegister src2,
            const Condition cond = al);
  void vcmp(const DwVfpRegister src1, const double src2,
            const Condition cond = al);
  void vcmp(const SwVfpRegister src1, const float src2,
            const Condition cond = al);

  void vmaxnm(const DwVfpRegister dst, const DwVfpRegister src1,
              const DwVfpRegister src2);
  void vmaxnm(const SwVfpRegister dst, const SwVfpRegister src1,
              const SwVfpRegister src2);
  void vminnm(const DwVfpRegister dst, const DwVfpRegister src1,
              const DwVfpRegister src2);
  void vminnm(const SwVfpRegister dst, const SwVfpRegister src1,
              const SwVfpRegister src2);

  // VSEL supports cond in {eq, ne, ge, lt, gt, le, vs, vc}.
  void vsel(const Condition cond, const DwVfpRegister dst,
            const DwVfpRegister src1, const DwVfpRegister src2);
  void vsel(const Condition cond, const SwVfpRegister dst,
            const SwVfpRegister src1, const SwVfpRegister src2);

  void vsqrt(const DwVfpRegister dst, const DwVfpRegister src,
             const Condition cond = al);
  void vsqrt(const SwVfpRegister dst, const SwVfpRegister src,
             const Condition cond = al);

  // ARMv8 rounding instructions (Scalar).
  void vrinta(const SwVfpRegister dst, const SwVfpRegister src);
  void vrinta(const DwVfpRegister dst, const DwVfpRegister src);
  void vrintn(const SwVfpRegister dst, const SwVfpRegister src);
  void vrintn(const DwVfpRegister dst, const DwVfpRegister src);
  void vrintm(const SwVfpRegister dst, const SwVfpRegister src);
  void vrintm(const DwVfpRegister dst, const DwVfpRegister src);
  void vrintp(const SwVfpRegister dst, const SwVfpRegister src);
  void vrintp(const DwVfpRegister dst, const DwVfpRegister src);
  void vrintz(const SwVfpRegister dst, const SwVfpRegister src,
              const Condition cond = al);
  void vrintz(const DwVfpRegister dst, const DwVfpRegister src,
              const Condition cond = al);

  // Support for NEON.

  // All these APIs support D0 to D31 and Q0 to Q15.
  void vld1(NeonSize size, const NeonListOperand& dst,
            const NeonMemOperand& src);
  // vld1s(ingle element to one lane).
  void vld1s(NeonSize size, const NeonListOperand& dst, uint8_t index,
             const NeonMemOperand& src);
  void vld1r(NeonSize size, const NeonListOperand& dst,
             const NeonMemOperand& src);
  void vst1(NeonSize size, const NeonListOperand& src,
            const NeonMemOperand& dst);
  // vst1s(single element from one lane).
  void vst1s(NeonSize size, const NeonListOperand& src, uint8_t index,
             const NeonMemOperand& dst);
  // dt represents the narrower type
  void vmovl(NeonDataType dt, QwNeonRegister dst, DwVfpRegister src);
  // dst_dt represents the narrower type, src_dt represents the src type.
  void vqmovn(NeonDataType dst_dt, NeonDataType src_dt, DwVfpRegister dst,
              QwNeonRegister src);

  // Only unconditional core <-> scalar moves are currently supported.
  void vmov(NeonDataType dt, DwVfpRegister dst, int index, Register src);
  void vmov(NeonDataType dt, Register dst, DwVfpRegister src, int index);

  void vmov(DwVfpRegister dst, uint64_t imm);
  void vmov(QwNeonRegister dst, uint64_t imm);
  void vmov(QwNeonRegister dst, QwNeonRegister src);
  void vdup(NeonSize size, QwNeonRegister dst, Register src);
  void vdup(NeonSize size, QwNeonRegister dst, DwVfpRegister src, int index);
  void vdup(NeonSize size, DwVfpRegister dst, DwVfpRegister src, int index);

  void vcvt_f32_s32(QwNeonRegister dst, QwNeonRegister src);
  void vcvt_f32_u32(QwNeonRegister dst, QwNeonRegister src);
  void vcvt_s32_f32(QwNeonRegister dst, QwNeonRegister src);
  void vcvt_u32_f32(QwNeonRegister dst, QwNeonRegister src);

  void vmvn(QwNeonRegister dst, QwNeonRegister src);
  void vswp(DwVfpRegister dst, DwVfpRegister src);
  void vswp(QwNeonRegister dst, QwNeonRegister src);
  void vabs(QwNeonRegister dst, QwNeonRegister src);
  void vabs(NeonSize size, QwNeonRegister dst, QwNeonRegister src);
  void vneg(QwNeonRegister dst, QwNeonRegister src);
  void vneg(NeonSize size, QwNeonRegister dst, QwNeonRegister src);

  void vand(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vbic(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void veor(DwVfpRegister dst, DwVfpRegister src1, DwVfpRegister src2);
  void veor(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vbsl(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vorr(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vorn(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vadd(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vadd(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vqadd(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
             QwNeonRegister src2);
  void vsub(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vsub(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vqsub(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
             QwNeonRegister src2);
  void vmlal(NeonDataType size, QwNeonRegister dst, DwVfpRegister src1,
             DwVfpRegister src2);
  void vmul(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vmul(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vmull(NeonDataType size, QwNeonRegister dst, DwVfpRegister src1,
             DwVfpRegister src2);
  void vmin(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vmin(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vmax(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vmax(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vpadd(DwVfpRegister dst, DwVfpRegister src1, DwVfpRegister src2);
  void vpadd(NeonSize size, DwVfpRegister dst, DwVfpRegister src1,
             DwVfpRegister src2);
  void vpmin(NeonDataType dt, DwVfpRegister dst, DwVfpRegister src1,
             DwVfpRegister src2);
  void vpmax(NeonDataType dt, DwVfpRegister dst, DwVfpRegister src1,
             DwVfpRegister src2);

  void vpadal(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src);
  void vpaddl(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src);
  void vqrdmulh(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
                QwNeonRegister src2);

  // ARMv8 rounding instructions (NEON).
  void vrintm(NeonDataType dt, const QwNeonRegister dst,
              const QwNeonRegister src);
  void vrintn(NeonDataType dt, const QwNeonRegister dst,
              const QwNeonRegister src);
  void vrintp(NeonDataType dt, const QwNeonRegister dst,
              const QwNeonRegister src);
  void vrintz(NeonDataType dt, const QwNeonRegister dst,
              const QwNeonRegister src);

  void vshl(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src, int shift);
  void vshl(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src,
            QwNeonRegister shift);
  void vshr(NeonDataType dt, DwVfpRegister dst, DwVfpRegister src, int shift);
  void vshr(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src, int shift);
  void vsli(NeonSize size, DwVfpRegister dst, DwVfpRegister src, int shift);
  void vsri(NeonSize size, DwVfpRegister dst, DwVfpRegister src, int shift);
  void vsra(NeonDataType size, DwVfpRegister dst, DwVfpRegister src, int imm);

  // vrecpe and vrsqrte only support floating point lanes.
  void vrecpe(QwNeonRegister dst, QwNeonRegister src);
  void vrsqrte(QwNeonRegister dst, QwNeonRegister src);
  void vrecps(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vrsqrts(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vtst(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vceq(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vceq(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vceq(NeonSize size, QwNeonRegister dst, QwNeonRegister src, int value);
  void vcge(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vcge(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vcgt(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void vcgt(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
            QwNeonRegister src2);
  void vclt(NeonSize size, QwNeonRegister dst, QwNeonRegister src, int value);
  void vrhadd(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
              QwNeonRegister src2);
  void vext(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2,
            int bytes);
  void vzip(NeonSize size, DwVfpRegister src1, DwVfpRegister src2);
  void vzip(NeonSize size, QwNeonRegister src1, QwNeonRegister src2);
  void vuzp(NeonSize size, DwVfpRegister src1, DwVfpRegister src2);
  void vuzp(NeonSize size, QwNeonRegister src1, QwNeonRegister src2);
  void vrev16(NeonSize size, QwNeonRegister dst, QwNeonRegister src);
  void vrev32(NeonSize size, QwNeonRegister dst, QwNeonRegister src);
  void vrev64(NeonSize size, QwNeonRegister dst, QwNeonRegister src);
  void vtrn(NeonSize size, DwVfpRegister src1, DwVfpRegister src2);
  void vtrn(NeonSize size, QwNeonRegister src1, QwNeonRegister src2);
  void vtbl(DwVfpRegister dst, const NeonListOperand& list,
            DwVfpRegister index);
  void vtbx(DwVfpRegister dst, const NeonListOperand& list,
            DwVfpRegister index);

  void vcnt(QwNeonRegister dst, QwNeonRegister src);

  // Pseudo instructions

  // Different nop operations are used by the code generator to detect certain
  // states of the generated code.
  enum NopMarkerTypes {
    NON_MARKING_NOP = 0,
    DEBUG_BREAK_NOP,
    // IC markers.
    PROPERTY_ACCESS_INLINED,
    PROPERTY_ACCESS_INLINED_CONTEXT,
    PROPERTY_ACCESS_INLINED_CONTEXT_DONT_DELETE,
    // Helper values.
    LAST_CODE_MARKER,
    FIRST_IC_MARKER = PROPERTY_ACCESS_INLINED
  };

  void nop(int type = 0);  // 0 is the default non-marking type.

  void push(Register src, Condition cond = al) {
    str(src, MemOperand(sp, 4, NegPreIndex), cond);
  }

  void pop(Register dst, Condition cond = al) {
    ldr(dst, MemOperand(sp, 4, PostIndex), cond);
  }

  void pop();

  void vpush(QwNeonRegister src, Condition cond = al) {
    vstm(db_w, sp, src.low(), src.high(), cond);
  }

  void vpush(DwVfpRegister src, Condition cond = al) {
    vstm(db_w, sp, src, src, cond);
  }

  void vpush(SwVfpRegister src, Condition cond = al) {
    vstm(db_w, sp, src, src, cond);
  }

  void vpop(DwVfpRegister dst, Condition cond = al) {
    vldm(ia_w, sp, dst, dst, cond);
  }

  // Jump unconditionally to given label.
  void jmp(Label* L) { b(L, al); }

  // Check the code size generated from label to here.
  int SizeOfCodeGeneratedSince(Label* label) {
    return pc_offset() - label->pos();
  }

  // Check the number of instructions generated from label to here.
  int InstructionsGeneratedSince(Label* label) {
    return SizeOfCodeGeneratedSince(label) / kInstrSize;
  }

  // Check whether an immediate fits an addressing mode 1 instruction.
  static bool ImmediateFitsAddrMode1Instruction(int32_t imm32);

  // Check whether an immediate fits an addressing mode 2 instruction.
  bool ImmediateFitsAddrMode2Instruction(int32_t imm32);

  // Class for scoping postponing the constant pool generation.
  class V8_NODISCARD BlockConstPoolScope {
   public:
    explicit BlockConstPoolScope(Assembler* assem) : assem_(assem) {
      assem_->StartBlockConstPool();
    }
    ~BlockConstPoolScope() { assem_->EndBlockConstPool(); }

   private:
    Assembler* const assem_;

    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockConstPoolScope);
  };

  // Unused on this architecture.
  void MaybeEmitOutOfLineConstantPool() {}

  // Record a deoptimization reason that can be used by a log or cpu profiler.
  // Use --trace-deopt to enable.
  void RecordDeoptReason(DeoptimizeReason reason, uint32_t node_id,
                         SourcePosition position, int id);

  // Record the emission of a constant pool.
  //
  // The emission of constant pool depends on the size of the code generated and
  // the number of RelocInfo recorded.
  // The Debug mechanism needs to map code offsets between two versions of a
  // function, compiled with and without debugger support (see for example
  // Debug::PrepareForBreakPoints()).
  // Compiling functions with debugger support generates additional code
  // (DebugCodegen::GenerateSlot()). This may affect the emission of the
  // constant pools and cause the version of the code with debugger support to
  // have constant pools generated in different places.
  // Recording the position and size of emitted constant pools allows to
  // correctly compute the offset mappings between the different versions of a
  // function in all situations.
  //
  // The parameter indicates the size of the constant pool (in bytes), including
  // the marker and branch over the data.
  void RecordConstPool(int size);

  // Writes a single byte or word of data in the code stream.  Used
  // for inline tables, e.g., jump-tables. CheckConstantPool() should be
  // called before any use of db/dd/dq/dp to ensure that constant pools
  // are not emitted as part of the tables generated.
  void db(uint8_t data);
  void dd(uint32_t data);
  void dq(uint64_t data);
  void dp(uintptr_t data) { dd(data); }

  // Read/patch instructions
  Instr instr_at(int pos) {
    return *reinterpret_cast<Instr*>(buffer_start_ + pos);
  }
  void instr_at_put(int pos, Instr instr) {
    *reinterpret_cast<Instr*>(buffer_start_ + pos) = instr;
  }
  static Instr instr_at(Address pc) { return *reinterpret_cast<Instr*>(pc); }
  static void instr_at_put(Address pc, Instr instr) {
    *reinterpret_cast<Instr*>(pc) = instr;
  }
  static Condition GetCondition(Instr instr);
  static bool IsLdrRegisterImmediate(Instr instr);
  static bool IsVldrDRegisterImmediate(Instr instr);
  static int GetLdrRegisterImmediateOffset(Instr instr);
  static int GetVldrDRegisterImmediateOffset(Instr instr);
  static Instr SetLdrRegisterImmediateOffset(Instr instr, int offset);
  static Instr SetVldrDRegisterImmediateOffset(Instr instr, int offset);
  static bool IsStrRegisterImmediate(Instr instr);
  static Instr SetStrRegisterImmediateOffset(Instr instr, int offset);
  static bool IsAddRegisterImmediate(Instr instr);
  static Instr SetAddRegisterImmediateOffset(Instr instr, int offset);
  static Register GetRd(Instr instr);
  static Register GetRn(Instr instr);
  static Register GetRm(Instr instr);
  static bool IsPush(Instr instr);
  static bool IsPop(Instr instr);
  static bool IsStrRegFpOffset(Instr instr);
  static bool IsLdrRegFpOffset(Instr instr);
  static bool IsStrRegFpNegOffset(Instr instr);
  static bool IsLdrRegFpNegOffset(Instr instr);
  static bool IsLdrPcImmediateOffset(Instr instr);
  static bool IsBOrBlPcImmediateOffset(Instr instr);
  static bool IsVldrDPcImmediateOffset(Instr instr);
  static bool IsBlxReg(Instr instr);
  static bool IsBlxIp(Instr instr);
  static bool IsTstImmediate(Instr instr);
  static bool IsCmpRegister(Instr instr);
  static bool IsCmpImmediate(Instr instr);
  static Register GetCmpImmediateRegister(Instr instr);
  static int GetCmpImmediateRawImmediate(Instr instr);
  static bool IsNop(Instr instr, int type = NON_MARKING_NOP);
  static bool IsMovImmed(Instr instr);
  static bool IsOrrImmed(Instr instr);
  static bool IsMovT(Instr instr);
  static Instr GetMovTPattern();
  static bool IsMovW(Instr instr);
  static Instr GetMovWPattern();
  static Instr EncodeMovwImmediate(uint32_t immediate);
  static Instr PatchMovwImmediate(Instr instruction, uint32_t immediate);
  static int DecodeShiftImm(Instr instr);
  static Instr PatchShiftImm(Instr instr, int immed);

  // Constants are accessed via pc relative addressing, which can reach −4095 to
  // 4095 for integer PC-relative loads, and −1020 to 1020 for floating-point
  // PC-relative loads, thereby defining a maximum distance between the
  // instruction and the accessed constant. Additionally, PC-relative loads
  // start at a delta from the actual load instruction's PC, so we can add this
  // on to the (positive) distance.
  static constexpr int kMaxDistToPcRelativeConstant =
      4095 + Instruction::kPcLoadDelta;
  // The constant pool needs to be jumped over, and has a marker, so the actual
  // distance from the instruction and start of the constant pool has to include
  // space for these two instructions.
  static constexpr int kMaxDistToIntPool =
      kMaxDistToPcRelativeConstant - 2 * kInstrSize;
  // Experimentally derived as sufficient for ~95% of compiles.
  static constexpr int kTypicalNumPending32Constants = 32;
  // The maximum number of pending constants is reached by a sequence of only
  // constant loads, which limits it to the number of constant loads that can
  // fit between the first constant load and the distance to the constant pool.
  static constexpr int kMaxNumPending32Constants =
      kMaxDistToIntPool / kInstrSize;

  // Postpone the generation of the constant pool for the specified number of
  // instructions.
  void BlockConstPoolFor(int instructions);

  // Check if is time to emit a constant pool.
  void CheckConstPool(bool force_emit, bool require_jump);

  V8_INLINE void MaybeCheckConstPool() {
    if (V8_UNLIKELY(pc_offset() >= constant_pool_deadline_)) {
      CheckConstPool(false, true);
    }
  }

  // Move a 32-bit immediate into a register, potentially via the constant pool.
  void Move32BitImmediate(Register rd, const Operand& x, Condition cond = al);

  // Get the code target object for a pc-relative call or jump.
  V8_INLINE Handle<Code> relative_code_target_object_handle_at(
      Address pc_) const;

 protected:
  int buffer_space() const { return reloc_info_writer.pos() - pc_; }

  // Decode branch instruction at pos and return branch target pos
  int target_at(int pos);

  // Patch branch instruction at pos to branch to given branch target pos
  void target_at_put(int pos, int target_pos);

  // Prevent contant pool emission until EndBlockConstPool is called.
  // Calls to this function can be nested but must be followed by an equal
  // number of call to EndBlockConstpool.
  void StartBlockConstPool() {
    if (const_pool_blocked_nesting_++ == 0) {
      // Prevent constant pool checks happening by resetting the deadline.
      constant_pool_deadline_ = kMaxInt;
    }
  }

  // Resume constant pool emission. Needs to be called as many times as
  // StartBlockConstPool to have an effect.
  void EndBlockConstPool() {
    if (--const_pool_blocked_nesting_ == 0) {
      if (first_const_pool_32_use_ >= 0) {
#ifdef DEBUG
        // Check the constant pool hasn't been blocked for too long.
        DCHECK_LE(pc_offset(), first_const_pool_32_use_ + kMaxDistToIntPool);
#endif
        // Reset the constant pool check back to the deadline.
        constant_pool_deadline_ = first_const_pool_32_use_ + kCheckPoolDeadline;
      }
    }
  }

  bool is_const_pool_blocked() const {
    return (const_pool_blocked_nesting_ > 0) ||
           (pc_offset() < no_const_pool_before_);
  }

  bool has_pending_constants() const {
    bool result = !pending_32_bit_constants_.empty();
    DCHECK_EQ(result, first_const_pool_32_use_ != -1);
    return result;
  }

  bool VfpRegisterIsAvailable(DwVfpRegister reg) {
    DCHECK(reg.is_valid());
    return IsEnabled(VFP32DREGS) ||
           (reg.code() < LowDwVfpRegister::kNumRegisters);
  }

  bool VfpRegisterIsAvailable(QwNeonRegister reg) {
    DCHECK(reg.is_valid());
    return IsEnabled(VFP32DREGS) ||
           (reg.code() < LowDwVfpRegister::kNumRegisters / 2);
  }

  inline void emit(Instr x);

  // InstructionStream generation
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries.
  static constexpr int kGap = 32;
  static_assert(AssemblerBase::kMinimalBufferSize >= 2 * kGap);

  // Relocation info generation
  // Each relocation is encoded as a variable size value
  static constexpr int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  RelocInfoWriter reloc_info_writer;

  // ConstantPoolEntry records are used during code generation as temporary
  // containers for constants and code target addresses until they are emitted
  // to the constant pool. These records are temporarily stored in a separate
  // buffer until a constant pool is emitted.
  // If every instruction in a long sequence is accessing the pool, we need one
  // pending relocation entry per instruction.

  // The buffers of pending constant pool entries.
  base::SmallVector<ConstantPoolEntry, kTypicalNumPending32Constants>
      pending_32_bit_constants_;

  // Scratch registers available for use by the Assembler.
  RegList scratch_register_list_;
  VfpRegList scratch_vfp_register_list_;

 private:
  // Avoid overflows for displacements etc.
  static const int kMaximalBufferSize = 512 * MB;

  // Constant pool generation
  // Pools are emitted in the instruction stream, preferably after unconditional
  // jumps or after returns from functions (in dead code locations).
  // If a long code sequence does not contain unconditional jumps, it is
  // necessary to emit the constant pool before the pool gets too far from the
  // location it is accessed from. In this case, we emit a jump over the emitted
  // constant pool.
  // Constants in the pool may be addresses of functions that gets relocated;
  // if so, a relocation info entry is associated to the constant pool entry.

  // Repeated checking whether the constant pool should be emitted is rather
  // expensive. Instead, we check once a deadline is hit; the deadline being
  // when there is a possibility that MaybeCheckConstPool won't be called before
  // kMaxDistToIntPoolWithHeader is exceeded. Since MaybeCheckConstPool is
  // called in CheckBuffer, this means that kGap is an upper bound on this
  // check. Use 2 * kGap just to give it some slack around BlockConstPoolScopes.
  static constexpr int kCheckPoolDeadline = kMaxDistToIntPool - 2 * kGap;

  // pc offset of the upcoming constant pool deadline. Equivalent to
  // first_const_pool_32_use_ + kCheckPoolDeadline.
  int constant_pool_deadline_;

  // Emission of the constant pool may be blocked in some code sequences.
  int const_pool_blocked_nesting_;  // Block emission if this is not zero.
  int no_const_pool_before_;        // Block emission before this pc offset.

  // Keep track of the first instruction requiring a constant pool entry
  // since the previous constant pool was emitted.
  int first_const_pool_32_use_;

  // The bound position, before this we cannot do instruction elimination.
  int last_bound_pos_;

  V8_INLINE void CheckBuffer();
  void GrowBuffer();

  // Instruction generation
  void AddrMode1(Instr instr, Register rd, Register rn, const Operand& x);
  // Attempt to encode operand |x| for instruction |instr| and return true on
  // success. The result will be encoded in |instr| directly. This method may
  // change the opcode if deemed beneficial, for instance, MOV may be turned
  // into MVN, ADD into SUB, AND into BIC, ...etc.  The only reason this method
  // may fail is that the operand is an immediate that cannot be encoded.
  bool AddrMode1TryEncodeOperand(Instr* instr, const Operand& x);

  void AddrMode2(Instr instr, Register rd, const MemOperand& x);
  void AddrMode3(Instr instr, Register rd, const MemOperand& x);
  void AddrMode4(Instr instr, Register rn, RegList rl);
  void AddrMode5(Instr instr, CRegister crd, const MemOperand& x);

  // Labels
  void print(const Label* L);
  void bind_to(Label* L, int pos);
  void next(Label* L);

  // Record reloc info for current pc_
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);
  void ConstantPoolAddEntry(int position, RelocInfo::Mode rmode,
                            intptr_t value);
  void AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate);

  int WriteCodeComments();

  friend class RelocInfo;
  friend class BlockConstPoolScope;
  friend class EnsureSpace;
  friend class UseScratchRegisterScope;
};

class EnsureSpace {
 public:
  V8_INLINE explicit EnsureSpace(Assembler* assembler);
};

class PatchingAssembler : public Assembler {
 public:
  PatchingAssembler(const AssemblerOptions& options, uint8_t* address,
                    int instructions);
  ~PatchingAssembler();

  void Emit(Address addr);
  void PadWithNops();
};

// This scope utility allows scratch registers to be managed safely. The
// Assembler's GetScratchRegisterList() is used as a pool of scratch
// registers. These registers can be allocated on demand, and will be returned
// at the end of the scope.
//
// When the scope ends, the Assembler's list will be restored to its original
// state, even if the list is modified by some other means. Note that this scope
// can be nested but the destructors need to run in the opposite order as the
// constructors. We do not have assertions for this.
class V8_EXPORT_PRIVATE V8_NODISCARD UseScratchRegisterScope {
 public:
  explicit UseScratchRegisterScope(Assembler* assembler)
      : assembler_(assembler),
        old_available_(*assembler->GetScratchRegisterList()),
        old_available_vfp_(*assembler->GetScratchVfpRegisterList()) {}

  ~UseScratchRegisterScope() {
    *assembler_->GetScratchRegisterList() = old_available_;
    *assembler_->GetScratchVfpRegisterList() = old_available_vfp_;
  }

  // Take a register from the list and return it.
  Register Acquire() {
    return assembler_->GetScratchRegisterList()->PopFirst();
  }
  SwVfpRegister AcquireS() { return AcquireVfp<SwVfpRegister>(); }
  LowDwVfpRegister AcquireLowD() { return AcquireVfp<LowDwVfpRegister>(); }
  DwVfpRegister AcquireD() {
    DwVfpRegister reg = AcquireVfp<DwVfpRegister>();
    DCHECK(assembler_->VfpRegisterIsAvailable(reg));
    return reg;
  }
  QwNeonRegister AcquireQ() {
    QwNeonRegister reg = AcquireVfp<QwNeonRegister>();
    DCHECK(assembler_->VfpRegisterIsAvailable(reg));
    return reg;
  }

  // Check if we have registers available to acquire.
  bool CanAcquire() const {
    return !assembler_->GetScratchRegisterList()->is_empty();
  }
  bool CanAcquireS() const { return CanAcquireVfp<SwVfpRegister>(); }
  bool CanAcquireD() const { return CanAcquireVfp<DwVfpRegister>(); }
  bool CanAcquireQ() const { return CanAcquireVfp<QwNeonRegister>(); }

  RegList Available() { return *assembler_->GetScratchRegisterList(); }
  void SetAvailable(RegList available) {
    *assembler_->GetScratchRegisterList() = available;
  }

  VfpRegList AvailableVfp() { return *assembler_->GetScratchVfpRegisterList(); }
  void SetAvailableVfp(VfpRegList available) {
    *assembler_->GetScratchVfpRegisterList() = available;
  }

  void Include(const Register& reg1, const Register& reg2 = no_reg) {
    RegList* available = assembler_->GetScratchRegisterList();
    DCHECK_NOT_NULL(available);
    DCHECK(!available->has(reg1));
    DCHECK(!available->has(reg2));
    available->set(reg1);
    available->set(reg2);
  }
  void Include(RegList list) {
    RegList* available = assembler_->GetScratchRegisterList();
    DCHECK_NOT_NULL(available);
    *available = *available | list;
  }
  void Include(VfpRegList list) {
    VfpRegList* available = assembler_->GetScratchVfpRegisterList();
    DCHECK_NOT_NULL(available);
    DCHECK_EQ((*available & list), 0x0);
    *available = *available | list;
  }
  void Exclude(const Register& reg1, const Register& reg2 = no_reg) {
    RegList* available = assembler_->GetScratchRegisterList();
    DCHECK_NOT_NULL(available);
    DCHECK(available->has(reg1));
    DCHECK_IMPLIES(reg2.is_valid(), available->has(reg2));
    available->clear(RegList{reg1, reg2});
  }
  void Exclude(VfpRegList list) {
    VfpRegList* available = assembler_->GetScratchVfpRegisterList();
    DCHECK_NOT_NULL(available);
    DCHECK_EQ((*available | list), *available);
    *available = *available & ~list;
  }

 private:
  friend class Assembler;
  friend class MacroAssembler;

  template <typename T>
  bool CanAcquireVfp() const;

  template <typename T>
  T AcquireVfp();

  Assembler* assembler_;
  // Available scratch registers at the start of this scope.
  RegList old_available_;
  VfpRegList old_available_vfp_;
};

// Helper struct for load lane and store lane to indicate which opcode to use
// and what memory size to be encoded in the opcode, and the new lane index.
class LoadStoreLaneParams {
 public:
  bool low_op;
  NeonSize sz;
  uint8_t laneidx;
  // The register mapping on ARM (1 Q to 2 D), means that loading/storing high
  // lanes of a Q register is equivalent to loading/storing the high D reg,
  // modulo number of lanes in a D reg. This constructor decides, based on the
  // laneidx and load/store size, whether the low or high D reg is accessed, and
  // what the new lane index is.
  LoadStoreLaneParams(MachineRepresentation rep, uint8_t laneidx);

 private:
  LoadStoreLaneParams(uint8_t laneidx, NeonSize sz, int lanes)
      : low_op(laneidx < lanes), sz(sz), laneidx(laneidx % lanes) {}
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM_ASSEMBLER_ARM_H_
