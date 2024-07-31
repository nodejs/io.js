// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_LOONG64_ASSEMBLER_LOONG64_H_
#define V8_CODEGEN_LOONG64_ASSEMBLER_LOONG64_H_

#include <stdio.h>

#include <memory>
#include <set>

#include "src/codegen/assembler.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/label.h"
#include "src/codegen/loong64/constants-loong64.h"
#include "src/codegen/loong64/register-loong64.h"
#include "src/codegen/machine-type.h"
#include "src/objects/contexts.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

class SafepointTableBuilder;

// -----------------------------------------------------------------------------
// Machine instruction Operands.
constexpr int kSmiShift = kSmiTagSize + kSmiShiftSize;
constexpr uint64_t kSmiShiftMask = (1UL << kSmiShift) - 1;
// Class Operand represents a shifter operand in data processing instructions.
class Operand {
 public:
  // Immediate.
  V8_INLINE explicit Operand(int64_t immediate,
                             RelocInfo::Mode rmode = RelocInfo::NO_INFO)
      : rm_(no_reg), rmode_(rmode) {
    value_.immediate = immediate;
  }
  V8_INLINE explicit Operand(const ExternalReference& f)
      : rm_(no_reg), rmode_(RelocInfo::EXTERNAL_REFERENCE) {
    value_.immediate = static_cast<int64_t>(f.address());
  }
  V8_INLINE explicit Operand(Tagged<Smi> value)
      : Operand(static_cast<intptr_t>(value.ptr())) {}

  explicit Operand(Handle<HeapObject> handle);

  static Operand EmbeddedNumber(double number);  // Smi or HeapNumber.

  // Register.
  V8_INLINE explicit Operand(Register rm) : rm_(rm) {}

  // Return true if this is a register operand.
  V8_INLINE bool is_reg() const;

  inline int64_t immediate() const;

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

  RelocInfo::Mode rmode() const { return rmode_; }

 private:
  Register rm_;
  union Value {
    Value() {}
    HeapNumberRequest heap_number_request;  // if is_heap_number_request_
    int64_t immediate;                      // otherwise
  } value_;                                 // valid if rm_ == no_reg
  bool is_heap_number_request_ = false;
  RelocInfo::Mode rmode_;

  friend class Assembler;
  friend class MacroAssembler;
};

// Class MemOperand represents a memory operand in load and store instructions.
// 1: base_reg + off_imm( si12 | si14<<2)
// 2: base_reg + offset_reg
class V8_EXPORT_PRIVATE MemOperand {
 public:
  explicit MemOperand(Register rj, int32_t offset = 0);
  explicit MemOperand(Register rj, Register offset = no_reg);
  Register base() const { return base_; }
  Register index() const { return index_; }
  int32_t offset() const { return offset_; }

  bool hasIndexReg() const { return index_ != no_reg; }

 private:
  Register base_;   // base
  Register index_;  // index
  int32_t offset_;  // offset

  friend class Assembler;
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

  virtual ~Assembler() {}

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

  // Unused on this architecture.
  void MaybeEmitOutOfLineConstantPool() {}

  // Loong64 uses BlockTrampolinePool to prevent generating trampoline inside a
  // continuous instruction block. In the destructor of BlockTrampolinePool, it
  // must check if it needs to generate trampoline immediately, if it does not
  // do this, the branch range will go beyond the max branch offset, that means
  // the pc_offset after call CheckTrampolinePool may have changed. So we use
  // pc_for_safepoint_ here for safepoint record.
  int pc_offset_for_safepoint() {
    return static_cast<int>(pc_for_safepoint_ - buffer_start_);
  }

  // TODO(LOONG_dev): LOONG64 Check this comment
  // Label operations & relative jumps (PPUM Appendix D).
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
  void bind(Label* L);  // Binds an unbound label L to current code position.

  enum OffsetSize : int {
    kOffset26 = 26,
    kOffset21 = 21,
    kOffset20 = 20,
    kOffset16 = 16
  };

  // Determines if Label is bound and near enough so that branch instruction
  // can be used to reach it, instead of jump instruction.
  // c means conditinal branch, a means always branch.
  bool is_near_c(Label* L);
  bool is_near(Label* L, OffsetSize bits);
  bool is_near_a(Label* L);

  int BranchOffset(Instr instr);

  // Returns the branch offset to the given label from the current code
  // position. Links the label to the current position if it is still unbound.
  // Manages the jump elimination optimization if the second parameter is true.
  int32_t branch_offset_helper(Label* L, OffsetSize bits);
  inline int32_t branch_offset(Label* L) {
    return branch_offset_helper(L, OffsetSize::kOffset16);
  }
  inline int32_t branch_offset21(Label* L) {
    return branch_offset_helper(L, OffsetSize::kOffset21);
  }
  inline int32_t branch_offset26(Label* L) {
    return branch_offset_helper(L, OffsetSize::kOffset26);
  }
  inline int32_t shifted_branch_offset(Label* L) {
    return branch_offset(L) >> 2;
  }
  inline int32_t shifted_branch_offset21(Label* L) {
    return branch_offset21(L) >> 2;
  }
  inline int32_t shifted_branch_offset26(Label* L) {
    return branch_offset26(L) >> 2;
  }
  uint64_t jump_address(Label* L);
  uint64_t jump_offset(Label* L);
  uint64_t branch_long_offset(Label* L);

  // Puts a labels target address at the given position.
  // The high 8 bits are set to zero.
  void label_at_put(Label* L, int at_offset);

  // Read/Modify the code target address in the branch/call instruction at pc.
  // The isolate argument is unused (and may be nullptr) when skipping flushing.
  static Address target_address_at(Address pc);
  static uint32_t target_compressed_address_at(Address pc);
  // On LOONG64 there is no Constant Pool so we skip that parameter.
  inline static Address target_address_at(Address pc, Address constant_pool) {
    return target_address_at(pc);
  }
  inline static Tagged_t target_compressed_address_at(Address pc,
                                                      Address constant_pool) {
    return target_compressed_address_at(pc);
  }
  inline static void set_target_address_at(
      Address pc, Address constant_pool, Address target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED) {
    set_target_value_at(pc, target, icache_flush_mode);
  }
  inline static void set_target_compressed_address_at(
      Address pc, Address constant_pool, Tagged_t target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED) {
    set_target_compressed_value_at(pc, target, icache_flush_mode);
  }

  inline Handle<Code> code_target_object_handle_at(Address pc,
                                                   Address constant_pool);

  // During code generation builtin targets in PC-relative call/jump
  // instructions are temporarily encoded as builtin ID until the generated
  // code is moved into the code space.
  static inline Builtin target_builtin_at(Address pc);

  static void set_target_value_at(
      Address pc, uint64_t target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);
  static void set_target_compressed_value_at(
      Address pc, uint32_t target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  static void JumpLabelToJumpRegister(Address pc);

  // This sets the branch destination (which gets loaded at the call address).
  // This is for calls and branches within generated code.  The serializer
  // has already deserialized the lui/ori instructions etc.
  inline static void deserialization_set_special_target_at(
      Address instruction_payload, Tagged<Code> code, Address target);

  // Get the size of the special target encoded at 'instruction_payload'.
  inline static int deserialization_special_target_size(
      Address instruction_payload);

  // This sets the internal reference at the pc.
  inline static void deserialization_set_target_internal_reference_at(
      Address pc, Address target,
      RelocInfo::Mode mode = RelocInfo::INTERNAL_REFERENCE);

  inline Handle<HeapObject> compressed_embedded_object_handle_at(
      Address pc, Address constant_pool);
  inline Handle<HeapObject> embedded_object_handle_at(Address pc,
                                                      Address constant_pool);

  // Here we are patching the address in the LUI/ORI instruction pair.
  // These values are used in the serialization process and must be zero for
  // LOONG platform, as InstructionStream, Embedded Object or External-reference
  // pointers are split across two consecutive instructions and don't exist
  // separately in the code, so the serializer should not step forwards in
  // memory after a target is resolved and written.
  static constexpr int kSpecialTargetSize = 0;

  // Number of consecutive instructions used to store 32bit/64bit constant.
  // This constant was used in RelocInfo::target_address_address() function
  // to tell serializer address of the instruction that follows
  // LUI/ORI instruction pair.
  // TODO(LOONG_dev): check this
  static constexpr int kInstructionsFor64BitConstant = 4;

  // Max offset for instructions with 16-bit offset field
  static constexpr int kMax16BranchOffset = (1 << (18 - 1)) - 1;

  // Max offset for instructions with 21-bit offset field
  static constexpr int kMax21BranchOffset = (1 << (23 - 1)) - 1;

  // Max offset for compact branch instructions with 26-bit offset field
  static constexpr int kMax26BranchOffset = (1 << (28 - 1)) - 1;

  static constexpr int kTrampolineSlotsSize = 2 * kInstrSize;

  RegList* GetScratchRegisterList() { return &scratch_register_list_; }

  DoubleRegList* GetScratchFPRegisterList() {
    return &scratch_fpregister_list_;
  }

  // ---------------------------------------------------------------------------
  // InstructionStream generation.

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
    FIRST_IC_MARKER = PROPERTY_ACCESS_INLINED,
  };

  // Type == 0 is the default non-marking nop. For LoongArch this is a
  // andi(zero_reg, zero_reg, 0).
  void nop(unsigned int type = 0) {
    DCHECK_LT(type, 32);
    andi(zero_reg, zero_reg, type);
  }

  // --------Branch-and-jump-instructions----------
  // We don't use likely variant of instructions.
  void b(int32_t offset);
  inline void b(Label* L) { b(shifted_branch_offset26(L)); }
  void bl(int32_t offset);
  inline void bl(Label* L) { bl(shifted_branch_offset26(L)); }

  void beq(Register rj, Register rd, int32_t offset);
  inline void beq(Register rj, Register rd, Label* L) {
    beq(rj, rd, shifted_branch_offset(L));
  }
  void bne(Register rj, Register rd, int32_t offset);
  inline void bne(Register rj, Register rd, Label* L) {
    bne(rj, rd, shifted_branch_offset(L));
  }
  void blt(Register rj, Register rd, int32_t offset);
  inline void blt(Register rj, Register rd, Label* L) {
    blt(rj, rd, shifted_branch_offset(L));
  }
  void bge(Register rj, Register rd, int32_t offset);
  inline void bge(Register rj, Register rd, Label* L) {
    bge(rj, rd, shifted_branch_offset(L));
  }
  void bltu(Register rj, Register rd, int32_t offset);
  inline void bltu(Register rj, Register rd, Label* L) {
    bltu(rj, rd, shifted_branch_offset(L));
  }
  void bgeu(Register rj, Register rd, int32_t offset);
  inline void bgeu(Register rj, Register rd, Label* L) {
    bgeu(rj, rd, shifted_branch_offset(L));
  }
  void beqz(Register rj, int32_t offset);
  inline void beqz(Register rj, Label* L) {
    beqz(rj, shifted_branch_offset21(L));
  }
  void bnez(Register rj, int32_t offset);
  inline void bnez(Register rj, Label* L) {
    bnez(rj, shifted_branch_offset21(L));
  }

  void jirl(Register rd, Register rj, int32_t offset);

  void bceqz(CFRegister cj, int32_t si21);
  inline void bceqz(CFRegister cj, Label* L) {
    bceqz(cj, shifted_branch_offset21(L));
  }
  void bcnez(CFRegister cj, int32_t si21);
  inline void bcnez(CFRegister cj, Label* L) {
    bcnez(cj, shifted_branch_offset21(L));
  }

  // -------Data-processing-instructions---------

  // Arithmetic.
  void add_w(Register rd, Register rj, Register rk);
  void add_d(Register rd, Register rj, Register rk);
  void sub_w(Register rd, Register rj, Register rk);
  void sub_d(Register rd, Register rj, Register rk);

  void addi_w(Register rd, Register rj, int32_t si12);
  void addi_d(Register rd, Register rj, int32_t si12);

  void addu16i_d(Register rd, Register rj, int32_t si16);

  void alsl_w(Register rd, Register rj, Register rk, int32_t sa2);
  void alsl_wu(Register rd, Register rj, Register rk, int32_t sa2);
  void alsl_d(Register rd, Register rj, Register rk, int32_t sa2);

  void lu12i_w(Register rd, int32_t si20);
  void lu32i_d(Register rd, int32_t si20);
  void lu52i_d(Register rd, Register rj, int32_t si12);

  void slt(Register rd, Register rj, Register rk);
  void sltu(Register rd, Register rj, Register rk);
  void slti(Register rd, Register rj, int32_t si12);
  void sltui(Register rd, Register rj, int32_t si12);

  void pcaddi(Register rd, int32_t si20);
  void pcaddu12i(Register rd, int32_t si20);
  void pcaddu18i(Register rd, int32_t si20);
  void pcalau12i(Register rd, int32_t si20);

  void and_(Register rd, Register rj, Register rk);
  void or_(Register rd, Register rj, Register rk);
  void xor_(Register rd, Register rj, Register rk);
  void nor(Register rd, Register rj, Register rk);
  void andn(Register rd, Register rj, Register rk);
  void orn(Register rd, Register rj, Register rk);

  void andi(Register rd, Register rj, int32_t ui12);
  void ori(Register rd, Register rj, int32_t ui12);
  void xori(Register rd, Register rj, int32_t ui12);

  void mul_w(Register rd, Register rj, Register rk);
  void mulh_w(Register rd, Register rj, Register rk);
  void mulh_wu(Register rd, Register rj, Register rk);
  void mul_d(Register rd, Register rj, Register rk);
  void mulh_d(Register rd, Register rj, Register rk);
  void mulh_du(Register rd, Register rj, Register rk);

  void mulw_d_w(Register rd, Register rj, Register rk);
  void mulw_d_wu(Register rd, Register rj, Register rk);

  void div_w(Register rd, Register rj, Register rk);
  void mod_w(Register rd, Register rj, Register rk);
  void div_wu(Register rd, Register rj, Register rk);
  void mod_wu(Register rd, Register rj, Register rk);
  void div_d(Register rd, Register rj, Register rk);
  void mod_d(Register rd, Register rj, Register rk);
  void div_du(Register rd, Register rj, Register rk);
  void mod_du(Register rd, Register rj, Register rk);

  // Shifts.
  void sll_w(Register rd, Register rj, Register rk);
  void srl_w(Register rd, Register rj, Register rk);
  void sra_w(Register rd, Register rj, Register rk);
  void rotr_w(Register rd, Register rj, Register rk);

  void slli_w(Register rd, Register rj, int32_t ui5);
  void srli_w(Register rd, Register rj, int32_t ui5);
  void srai_w(Register rd, Register rj, int32_t ui5);
  void rotri_w(Register rd, Register rj, int32_t ui5);

  void sll_d(Register rd, Register rj, Register rk);
  void srl_d(Register rd, Register rj, Register rk);
  void sra_d(Register rd, Register rj, Register rk);
  void rotr_d(Register rd, Register rj, Register rk);

  void slli_d(Register rd, Register rj, int32_t ui6);
  void srli_d(Register rd, Register rj, int32_t ui6);
  void srai_d(Register rd, Register rj, int32_t ui6);
  void rotri_d(Register rd, Register rj, int32_t ui6);

  // Bit twiddling.
  void ext_w_b(Register rd, Register rj);
  void ext_w_h(Register rd, Register rj);

  void clo_w(Register rd, Register rj);
  void clz_w(Register rd, Register rj);
  void cto_w(Register rd, Register rj);
  void ctz_w(Register rd, Register rj);
  void clo_d(Register rd, Register rj);
  void clz_d(Register rd, Register rj);
  void cto_d(Register rd, Register rj);
  void ctz_d(Register rd, Register rj);

  void bytepick_w(Register rd, Register rj, Register rk, int32_t sa2);
  void bytepick_d(Register rd, Register rj, Register rk, int32_t sa3);

  void revb_2h(Register rd, Register rj);
  void revb_4h(Register rd, Register rj);
  void revb_2w(Register rd, Register rj);
  void revb_d(Register rd, Register rj);

  void revh_2w(Register rd, Register rj);
  void revh_d(Register rd, Register rj);

  void bitrev_4b(Register rd, Register rj);
  void bitrev_8b(Register rd, Register rj);

  void bitrev_w(Register rd, Register rj);
  void bitrev_d(Register rd, Register rj);

  void bstrins_w(Register rd, Register rj, int32_t msbw, int32_t lsbw);
  void bstrins_d(Register rd, Register rj, int32_t msbd, int32_t lsbd);

  void bstrpick_w(Register rd, Register rj, int32_t msbw, int32_t lsbw);
  void bstrpick_d(Register rd, Register rj, int32_t msbd, int32_t lsbd);

  void maskeqz(Register rd, Register rj, Register rk);
  void masknez(Register rd, Register rj, Register rk);

  // Memory-instructions
  void ld_b(Register rd, Register rj, int32_t si12);
  void ld_h(Register rd, Register rj, int32_t si12);
  void ld_w(Register rd, Register rj, int32_t si12);
  void ld_d(Register rd, Register rj, int32_t si12);
  void ld_bu(Register rd, Register rj, int32_t si12);
  void ld_hu(Register rd, Register rj, int32_t si12);
  void ld_wu(Register rd, Register rj, int32_t si12);
  void st_b(Register rd, Register rj, int32_t si12);
  void st_h(Register rd, Register rj, int32_t si12);
  void st_w(Register rd, Register rj, int32_t si12);
  void st_d(Register rd, Register rj, int32_t si12);

  void ldx_b(Register rd, Register rj, Register rk);
  void ldx_h(Register rd, Register rj, Register rk);
  void ldx_w(Register rd, Register rj, Register rk);
  void ldx_d(Register rd, Register rj, Register rk);
  void ldx_bu(Register rd, Register rj, Register rk);
  void ldx_hu(Register rd, Register rj, Register rk);
  void ldx_wu(Register rd, Register rj, Register rk);
  void stx_b(Register rd, Register rj, Register rk);
  void stx_h(Register rd, Register rj, Register rk);
  void stx_w(Register rd, Register rj, Register rk);
  void stx_d(Register rd, Register rj, Register rk);

  void ldptr_w(Register rd, Register rj, int32_t si14);
  void ldptr_d(Register rd, Register rj, int32_t si14);
  void stptr_w(Register rd, Register rj, int32_t si14);
  void stptr_d(Register rd, Register rj, int32_t si14);

  void amswap_w(Register rd, Register rk, Register rj);
  void amswap_d(Register rd, Register rk, Register rj);
  void amadd_w(Register rd, Register rk, Register rj);
  void amadd_d(Register rd, Register rk, Register rj);
  void amand_w(Register rd, Register rk, Register rj);
  void amand_d(Register rd, Register rk, Register rj);
  void amor_w(Register rd, Register rk, Register rj);
  void amor_d(Register rd, Register rk, Register rj);
  void amxor_w(Register rd, Register rk, Register rj);
  void amxor_d(Register rd, Register rk, Register rj);
  void ammax_w(Register rd, Register rk, Register rj);
  void ammax_d(Register rd, Register rk, Register rj);
  void ammin_w(Register rd, Register rk, Register rj);
  void ammin_d(Register rd, Register rk, Register rj);
  void ammax_wu(Register rd, Register rk, Register rj);
  void ammax_du(Register rd, Register rk, Register rj);
  void ammin_wu(Register rd, Register rk, Register rj);
  void ammin_du(Register rd, Register rk, Register rj);

  void amswap_db_w(Register rd, Register rk, Register rj);
  void amswap_db_d(Register rd, Register rk, Register rj);
  void amadd_db_w(Register rd, Register rk, Register rj);
  void amadd_db_d(Register rd, Register rk, Register rj);
  void amand_db_w(Register rd, Register rk, Register rj);
  void amand_db_d(Register rd, Register rk, Register rj);
  void amor_db_w(Register rd, Register rk, Register rj);
  void amor_db_d(Register rd, Register rk, Register rj);
  void amxor_db_w(Register rd, Register rk, Register rj);
  void amxor_db_d(Register rd, Register rk, Register rj);
  void ammax_db_w(Register rd, Register rk, Register rj);
  void ammax_db_d(Register rd, Register rk, Register rj);
  void ammin_db_w(Register rd, Register rk, Register rj);
  void ammin_db_d(Register rd, Register rk, Register rj);
  void ammax_db_wu(Register rd, Register rk, Register rj);
  void ammax_db_du(Register rd, Register rk, Register rj);
  void ammin_db_wu(Register rd, Register rk, Register rj);
  void ammin_db_du(Register rd, Register rk, Register rj);

  void ll_w(Register rd, Register rj, int32_t si14);
  void ll_d(Register rd, Register rj, int32_t si14);
  void sc_w(Register rd, Register rj, int32_t si14);
  void sc_d(Register rd, Register rj, int32_t si14);

  void dbar(int32_t hint);
  void ibar(int32_t hint);

  // Break instruction
  void break_(uint32_t code, bool break_as_stop = false);
  void stop(uint32_t code = kMaxStopCode);

  // Arithmetic.
  void fadd_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fadd_d(FPURegister fd, FPURegister fj, FPURegister fk);
  void fsub_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fsub_d(FPURegister fd, FPURegister fj, FPURegister fk);
  void fmul_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fmul_d(FPURegister fd, FPURegister fj, FPURegister fk);
  void fdiv_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fdiv_d(FPURegister fd, FPURegister fj, FPURegister fk);

  void fmadd_s(FPURegister fd, FPURegister fj, FPURegister fk, FPURegister fa);
  void fmadd_d(FPURegister fd, FPURegister fj, FPURegister fk, FPURegister fa);
  void fmsub_s(FPURegister fd, FPURegister fj, FPURegister fk, FPURegister fa);
  void fmsub_d(FPURegister fd, FPURegister fj, FPURegister fk, FPURegister fa);
  void fnmadd_s(FPURegister fd, FPURegister fj, FPURegister fk, FPURegister fa);
  void fnmadd_d(FPURegister fd, FPURegister fj, FPURegister fk, FPURegister fa);
  void fnmsub_s(FPURegister fd, FPURegister fj, FPURegister fk, FPURegister fa);
  void fnmsub_d(FPURegister fd, FPURegister fj, FPURegister fk, FPURegister fa);

  void fmax_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fmax_d(FPURegister fd, FPURegister fj, FPURegister fk);
  void fmin_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fmin_d(FPURegister fd, FPURegister fj, FPURegister fk);

  void fmaxa_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fmaxa_d(FPURegister fd, FPURegister fj, FPURegister fk);
  void fmina_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fmina_d(FPURegister fd, FPURegister fj, FPURegister fk);

  void fabs_s(FPURegister fd, FPURegister fj);
  void fabs_d(FPURegister fd, FPURegister fj);
  void fneg_s(FPURegister fd, FPURegister fj);
  void fneg_d(FPURegister fd, FPURegister fj);

  void fsqrt_s(FPURegister fd, FPURegister fj);
  void fsqrt_d(FPURegister fd, FPURegister fj);
  void frecip_s(FPURegister fd, FPURegister fj);
  void frecip_d(FPURegister fd, FPURegister fj);
  void frsqrt_s(FPURegister fd, FPURegister fj);
  void frsqrt_d(FPURegister fd, FPURegister fj);

  void fscaleb_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fscaleb_d(FPURegister fd, FPURegister fj, FPURegister fk);
  void flogb_s(FPURegister fd, FPURegister fj);
  void flogb_d(FPURegister fd, FPURegister fj);
  void fcopysign_s(FPURegister fd, FPURegister fj, FPURegister fk);
  void fcopysign_d(FPURegister fd, FPURegister fj, FPURegister fk);

  void fclass_s(FPURegister fd, FPURegister fj);
  void fclass_d(FPURegister fd, FPURegister fj);

  void fcmp_cond_s(FPUCondition cc, FPURegister fj, FPURegister fk,
                   CFRegister cd);
  void fcmp_cond_d(FPUCondition cc, FPURegister fj, FPURegister fk,
                   CFRegister cd);

  void fcvt_s_d(FPURegister fd, FPURegister fj);
  void fcvt_d_s(FPURegister fd, FPURegister fj);

  void ffint_s_w(FPURegister fd, FPURegister fj);
  void ffint_s_l(FPURegister fd, FPURegister fj);
  void ffint_d_w(FPURegister fd, FPURegister fj);
  void ffint_d_l(FPURegister fd, FPURegister fj);
  void ftint_w_s(FPURegister fd, FPURegister fj);
  void ftint_w_d(FPURegister fd, FPURegister fj);
  void ftint_l_s(FPURegister fd, FPURegister fj);
  void ftint_l_d(FPURegister fd, FPURegister fj);

  void ftintrm_w_s(FPURegister fd, FPURegister fj);
  void ftintrm_w_d(FPURegister fd, FPURegister fj);
  void ftintrm_l_s(FPURegister fd, FPURegister fj);
  void ftintrm_l_d(FPURegister fd, FPURegister fj);
  void ftintrp_w_s(FPURegister fd, FPURegister fj);
  void ftintrp_w_d(FPURegister fd, FPURegister fj);
  void ftintrp_l_s(FPURegister fd, FPURegister fj);
  void ftintrp_l_d(FPURegister fd, FPURegister fj);
  void ftintrz_w_s(FPURegister fd, FPURegister fj);
  void ftintrz_w_d(FPURegister fd, FPURegister fj);
  void ftintrz_l_s(FPURegister fd, FPURegister fj);
  void ftintrz_l_d(FPURegister fd, FPURegister fj);
  void ftintrne_w_s(FPURegister fd, FPURegister fj);
  void ftintrne_w_d(FPURegister fd, FPURegister fj);
  void ftintrne_l_s(FPURegister fd, FPURegister fj);
  void ftintrne_l_d(FPURegister fd, FPURegister fj);

  void frint_s(FPURegister fd, FPURegister fj);
  void frint_d(FPURegister fd, FPURegister fj);

  void fmov_s(FPURegister fd, FPURegister fj);
  void fmov_d(FPURegister fd, FPURegister fj);

  void fsel(CFRegister ca, FPURegister fd, FPURegister fj, FPURegister fk);

  void movgr2fr_w(FPURegister fd, Register rj);
  void movgr2fr_d(FPURegister fd, Register rj);
  void movgr2frh_w(FPURegister fd, Register rj);

  void movfr2gr_s(Register rd, FPURegister fj);
  void movfr2gr_d(Register rd, FPURegister fj);
  void movfrh2gr_s(Register rd, FPURegister fj);

  void movgr2fcsr(Register rj, FPUControlRegister fcsr = FCSR0);
  void movfcsr2gr(Register rd, FPUControlRegister fcsr = FCSR0);

  void movfr2cf(CFRegister cd, FPURegister fj);
  void movcf2fr(FPURegister fd, CFRegister cj);

  void movgr2cf(CFRegister cd, Register rj);
  void movcf2gr(Register rd, CFRegister cj);

  void fld_s(FPURegister fd, Register rj, int32_t si12);
  void fld_d(FPURegister fd, Register rj, int32_t si12);
  void fst_s(FPURegister fd, Register rj, int32_t si12);
  void fst_d(FPURegister fd, Register rj, int32_t si12);

  void fldx_s(FPURegister fd, Register rj, Register rk);
  void fldx_d(FPURegister fd, Register rj, Register rk);
  void fstx_s(FPURegister fd, Register rj, Register rk);
  void fstx_d(FPURegister fd, Register rj, Register rk);

  // Check the code size generated from label to here.
  int SizeOfCodeGeneratedSince(Label* label) {
    return pc_offset() - label->pos();
  }

  // Check the number of instructions generated from label to here.
  int InstructionsGeneratedSince(Label* label) {
    return SizeOfCodeGeneratedSince(label) / kInstrSize;
  }

  // Class for scoping postponing the trampoline pool generation.
  class V8_NODISCARD BlockTrampolinePoolScope {
   public:
    explicit BlockTrampolinePoolScope(Assembler* assem) : assem_(assem) {
      assem_->StartBlockTrampolinePool();
    }
    ~BlockTrampolinePoolScope() { assem_->EndBlockTrampolinePool(); }

   private:
    Assembler* assem_;

    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockTrampolinePoolScope);
  };

  // Class for postponing the assembly buffer growth. Typically used for
  // sequences of instructions that must be emitted as a unit, before
  // buffer growth (and relocation) can occur.
  // This blocking scope is not nestable.
  class V8_NODISCARD BlockGrowBufferScope {
   public:
    explicit BlockGrowBufferScope(Assembler* assem) : assem_(assem) {
      assem_->StartBlockGrowBuffer();
    }
    ~BlockGrowBufferScope() { assem_->EndBlockGrowBuffer(); }

   private:
    Assembler* assem_;

    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockGrowBufferScope);
  };

  // Record a deoptimization reason that can be used by a log or cpu profiler.
  // Use --trace-deopt to enable.
  void RecordDeoptReason(DeoptimizeReason reason, uint32_t node_id,
                         SourcePosition position, int id);

  static int RelocateInternalReference(RelocInfo::Mode rmode, Address pc,
                                       intptr_t pc_delta);
  static void RelocateRelativeReference(RelocInfo::Mode rmode, Address pc,
                                        intptr_t pc_delta);

  // Writes a single byte or word of data in the code stream.  Used for
  // inline tables, e.g., jump-tables.
  void db(uint8_t data);
  void dd(uint32_t data);
  void dq(uint64_t data);
  void dp(uintptr_t data) { dq(data); }
  void dd(Label* label);

  // Postpone the generation of the trampoline pool for the specified number of
  // instructions.
  void BlockTrampolinePoolFor(int instructions);

  // Check if there is less than kGap bytes available in the buffer.
  // If this is the case, we need to grow the buffer before emitting
  // an instruction or relocation information.
  inline bool overflow() const { return pc_ >= reloc_info_writer.pos() - kGap; }

  // Get the number of bytes available in the buffer.
  inline intptr_t available_space() const {
    return reloc_info_writer.pos() - pc_;
  }

  // Read/patch instructions.
  static Instr instr_at(Address pc) { return *reinterpret_cast<Instr*>(pc); }
  static void instr_at_put(Address pc, Instr instr) {
    *reinterpret_cast<Instr*>(pc) = instr;
  }
  Instr instr_at(int pos) {
    return *reinterpret_cast<Instr*>(buffer_start_ + pos);
  }
  void instr_at_put(int pos, Instr instr) {
    *reinterpret_cast<Instr*>(buffer_start_ + pos) = instr;
  }

  // Check if an instruction is a branch of some kind.
  static bool IsBranch(Instr instr);
  static bool IsB(Instr instr);
  static bool IsBz(Instr instr);
  static bool IsNal(Instr instr);

  static bool IsBeq(Instr instr);
  static bool IsBne(Instr instr);

  static bool IsJump(Instr instr);
  static bool IsMov(Instr instr, Register rd, Register rs);
  static bool IsPcAddi(Instr instr);

  static bool IsJ(Instr instr);
  static bool IsLu12i_w(Instr instr);
  static bool IsOri(Instr instr);
  static bool IsLu32i_d(Instr instr);
  static bool IsLu52i_d(Instr instr);

  static bool IsNop(Instr instr, unsigned int type);

  static Register GetRjReg(Instr instr);
  static Register GetRkReg(Instr instr);
  static Register GetRdReg(Instr instr);

  static uint32_t GetRj(Instr instr);
  static uint32_t GetRjField(Instr instr);
  static uint32_t GetRk(Instr instr);
  static uint32_t GetRkField(Instr instr);
  static uint32_t GetRd(Instr instr);
  static uint32_t GetRdField(Instr instr);
  static uint32_t GetSa2(Instr instr);
  static uint32_t GetSa3(Instr instr);
  static uint32_t GetSa2Field(Instr instr);
  static uint32_t GetSa3Field(Instr instr);
  static uint32_t GetOpcodeField(Instr instr);
  static uint32_t GetFunction(Instr instr);
  static uint32_t GetFunctionField(Instr instr);
  static uint32_t GetImmediate16(Instr instr);
  static uint32_t GetLabelConst(Instr instr);

  static bool IsAddImmediate(Instr instr);
  static Instr SetAddImmediateOffset(Instr instr, int16_t offset);

  static bool IsAndImmediate(Instr instr);
  static bool IsEmittedConstant(Instr instr);

  void CheckTrampolinePool();

  // Get the code target object for a pc-relative call or jump.
  V8_INLINE Handle<Code> relative_code_target_object_handle_at(
      Address pc_) const;

  inline int UnboundLabelsCount() { return unbound_labels_count_; }

 protected:
  // Helper function for memory load/store.
  void AdjustBaseAndOffset(MemOperand* src);

  inline static void set_target_internal_reference_encoded_at(Address pc,
                                                              Address target);

  int64_t buffer_space() const { return reloc_info_writer.pos() - pc_; }

  // Decode branch instruction at pos and return branch target pos.
  int target_at(int pos, bool is_internal);

  // Patch branch instruction at pos to branch to given branch target pos.
  void target_at_put(int pos, int target_pos, bool is_internal);

  // Say if we need to relocate with this mode.
  bool MustUseReg(RelocInfo::Mode rmode);

  // Record reloc info for current pc_.
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);

  // Block the emission of the trampoline pool before pc_offset.
  void BlockTrampolinePoolBefore(int pc_offset) {
    if (no_trampoline_pool_before_ < pc_offset)
      no_trampoline_pool_before_ = pc_offset;
  }

  void StartBlockTrampolinePool() { trampoline_pool_blocked_nesting_++; }

  void EndBlockTrampolinePool() {
    trampoline_pool_blocked_nesting_--;
    if (trampoline_pool_blocked_nesting_ == 0) {
      CheckTrampolinePoolQuick(1);
    }
  }

  bool is_trampoline_pool_blocked() const {
    return trampoline_pool_blocked_nesting_ > 0;
  }

  bool has_exception() const { return internal_trampoline_exception_; }

  bool is_trampoline_emitted() const { return trampoline_emitted_; }

  // Temporarily block automatic assembly buffer growth.
  void StartBlockGrowBuffer() {
    DCHECK(!block_buffer_growth_);
    block_buffer_growth_ = true;
  }

  void EndBlockGrowBuffer() {
    DCHECK(block_buffer_growth_);
    block_buffer_growth_ = false;
  }

  bool is_buffer_growth_blocked() const { return block_buffer_growth_; }

  void CheckTrampolinePoolQuick(int extra_instructions = 0) {
    if (pc_offset() >= next_buffer_check_ - extra_instructions * kInstrSize) {
      CheckTrampolinePool();
    }
  }

  void set_pc_for_safepoint() { pc_for_safepoint_ = pc_; }

 private:
  // Avoid overflows for displacements etc.
  static const int kMaximalBufferSize = 512 * MB;

  // Buffer size and constant pool distance are checked together at regular
  // intervals of kBufferCheckInterval emitted bytes.
  static constexpr int kBufferCheckInterval = 1 * KB / 2;

  // InstructionStream generation.
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries.
  static constexpr int kGap = 64;
  static_assert(AssemblerBase::kMinimalBufferSize >= 2 * kGap);

  // Repeated checking whether the trampoline pool should be emitted is rather
  // expensive. By default we only check again once a number of instructions
  // has been generated.
  static constexpr int kCheckConstIntervalInst = 32;
  static constexpr int kCheckConstInterval =
      kCheckConstIntervalInst * kInstrSize;

  int next_buffer_check_;  // pc offset of next buffer check.

  // Emission of the trampoline pool may be blocked in some code sequences.
  int trampoline_pool_blocked_nesting_;  // Block emission if this is not zero.
  int no_trampoline_pool_before_;  // Block emission before this pc offset.

  // Keep track of the last emitted pool to guarantee a maximal distance.
  int last_trampoline_pool_end_;  // pc offset of the end of the last pool.

  // Automatic growth of the assembly buffer may be blocked for some sequences.
  bool block_buffer_growth_;  // Block growth when true.

  // Relocation information generation.
  // Each relocation is encoded as a variable size value.
  static constexpr int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  RelocInfoWriter reloc_info_writer;

  // The bound position, before this we cannot do instruction elimination.
  int last_bound_pos_;

  // InstructionStream emission.
  inline void CheckBuffer();
  void GrowBuffer();
  inline void emit(Instr x);
  inline void emit(uint64_t x);
  template <typename T>
  inline void EmitHelper(T x);
  inline void EmitHelper(Instr x);

  void GenB(Opcode opcode, Register rj, int32_t si21);  // opcode:6
  void GenB(Opcode opcode, CFRegister cj, int32_t si21, bool isEq);
  void GenB(Opcode opcode, int32_t si26);
  void GenBJ(Opcode opcode, Register rj, Register rd, int32_t si16);
  void GenCmp(Opcode opcode, FPUCondition cond, FPURegister fk, FPURegister fj,
              CFRegister cd);
  void GenSel(Opcode opcode, CFRegister ca, FPURegister fk, FPURegister fj,
              FPURegister rd);

  void GenRegister(Opcode opcode, Register rj, Register rd, bool rjrd = true);
  void GenRegister(Opcode opcode, FPURegister fj, FPURegister fd);
  void GenRegister(Opcode opcode, Register rj, FPURegister fd);
  void GenRegister(Opcode opcode, FPURegister fj, Register rd);
  void GenRegister(Opcode opcode, Register rj, FPUControlRegister fd);
  void GenRegister(Opcode opcode, FPUControlRegister fj, Register rd);
  void GenRegister(Opcode opcode, FPURegister fj, CFRegister cd);
  void GenRegister(Opcode opcode, CFRegister cj, FPURegister fd);
  void GenRegister(Opcode opcode, Register rj, CFRegister cd);
  void GenRegister(Opcode opcode, CFRegister cj, Register rd);

  void GenRegister(Opcode opcode, Register rk, Register rj, Register rd);
  void GenRegister(Opcode opcode, FPURegister fk, FPURegister fj,
                   FPURegister fd);

  void GenRegister(Opcode opcode, FPURegister fa, FPURegister fk,
                   FPURegister fj, FPURegister fd);
  void GenRegister(Opcode opcode, Register rk, Register rj, FPURegister fd);

  void GenImm(Opcode opcode, int32_t bit3, Register rk, Register rj,
              Register rd);
  void GenImm(Opcode opcode, int32_t bit6m, int32_t bit6l, Register rj,
              Register rd);
  void GenImm(Opcode opcode, int32_t bit20, Register rd);
  void GenImm(Opcode opcode, int32_t bit15);
  void GenImm(Opcode opcode, int32_t value, Register rj, Register rd,
              int32_t value_bits);  // 6 | 12 | 14 | 16
  void GenImm(Opcode opcode, int32_t bit12, Register rj, FPURegister fd);

  // Labels.
  void print(const Label* L);
  void bind_to(Label* L, int pos);
  void next(Label* L, bool is_internal);

  // One trampoline consists of:
  // - space for trampoline slots,
  // - space for labels.
  //
  // Space for trampoline slots is equal to slot_count * 2 * kInstrSize.
  // Space for trampoline slots precedes space for labels. Each label is of one
  // instruction size, so total amount for labels is equal to
  // label_count *  kInstrSize.
  class Trampoline {
   public:
    Trampoline() {
      start_ = 0;
      next_slot_ = 0;
      free_slot_count_ = 0;
      end_ = 0;
    }
    Trampoline(int start, int slot_count) {
      start_ = start;
      next_slot_ = start;
      free_slot_count_ = slot_count;
      end_ = start + slot_count * kTrampolineSlotsSize;
    }
    int start() { return start_; }
    int end() { return end_; }
    int take_slot() {
      int trampoline_slot = kInvalidSlotPos;
      if (free_slot_count_ <= 0) {
        // We have run out of space on trampolines.
        // Make sure we fail in debug mode, so we become aware of each case
        // when this happens.
        DCHECK(0);
        // Internal exception will be caught.
      } else {
        trampoline_slot = next_slot_;
        free_slot_count_--;
        next_slot_ += kTrampolineSlotsSize;
      }
      return trampoline_slot;
    }

   private:
    int start_;
    int end_;
    int next_slot_;
    int free_slot_count_;
  };

  int32_t get_trampoline_entry(int32_t pos);
  int unbound_labels_count_;
  // After trampoline is emitted, long branches are used in generated code for
  // the forward branches whose target offsets could be beyond reach of branch
  // instruction. We use this information to trigger different mode of
  // branch instruction generation, where we use jump instructions rather
  // than regular branch instructions.
  bool trampoline_emitted_;
  static constexpr int kInvalidSlotPos = -1;

  // Internal reference positions, required for unbounded internal reference
  // labels.
  std::set<int64_t> internal_reference_positions_;
  bool is_internal_reference(Label* L) {
    return internal_reference_positions_.find(L->pos()) !=
           internal_reference_positions_.end();
  }

  void EmittedCompactBranchInstruction() { prev_instr_compact_branch_ = true; }
  void ClearCompactBranchState() { prev_instr_compact_branch_ = false; }
  bool prev_instr_compact_branch_ = false;

  Trampoline trampoline_;
  bool internal_trampoline_exception_;

  // Keep track of the last Call's position to ensure that safepoint can get the
  // correct information even if there is a trampoline immediately after the
  // Call.
  uint8_t* pc_for_safepoint_;

  RegList scratch_register_list_;

  DoubleRegList scratch_fpregister_list_;

 private:
  void AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate);

  int WriteCodeComments();

  friend class RegExpMacroAssemblerLOONG64;
  friend class RelocInfo;
  friend class BlockTrampolinePoolScope;
  friend class EnsureSpace;
};

class EnsureSpace {
 public:
  explicit inline EnsureSpace(Assembler* assembler);
};

class V8_EXPORT_PRIVATE V8_NODISCARD UseScratchRegisterScope {
 public:
  explicit UseScratchRegisterScope(Assembler* assembler)
      : available_(assembler->GetScratchRegisterList()),
        availablefp_(assembler->GetScratchFPRegisterList()),
        old_available_(*available_),
        old_availablefp_(*availablefp_) {}

  ~UseScratchRegisterScope() {
    *available_ = old_available_;
    *availablefp_ = old_availablefp_;
  }

  Register Acquire() {
    return available_->PopFirst();
  }

  DoubleRegister AcquireFp() {
    return availablefp_->PopFirst();
  }

  bool hasAvailable() const { return !available_->is_empty(); }

  bool hasAvailableFp() const { return !availablefp_->is_empty(); }

  void Include(const RegList& list) { *available_ |= list; }
  void IncludeFp(const DoubleRegList& list) { *availablefp_ |= list; }
  void Exclude(const RegList& list) { available_->clear(list); }
  void ExcludeFp(const DoubleRegList& list) { availablefp_->clear(list); }
  void Include(const Register& reg1, const Register& reg2 = no_reg) {
    RegList list({reg1, reg2});
    Include(list);
  }
  void IncludeFp(const DoubleRegister& reg1,
                 const DoubleRegister& reg2 = no_dreg) {
    DoubleRegList list({reg1, reg2});
    IncludeFp(list);
  }
  void Exclude(const Register& reg1, const Register& reg2 = no_reg) {
    RegList list({reg1, reg2});
    Exclude(list);
  }
  void ExcludeFp(const DoubleRegister& reg1,
                 const DoubleRegister& reg2 = no_dreg) {
    DoubleRegList list({reg1, reg2});
    ExcludeFp(list);
  }

 private:
  RegList* available_;
  DoubleRegList* availablefp_;
  RegList old_available_;
  DoubleRegList old_availablefp_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_LOONG64_ASSEMBLER_LOONG64_H_
