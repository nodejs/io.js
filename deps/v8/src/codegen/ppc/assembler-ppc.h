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
// Copyright 2014 the V8 project authors. All rights reserved.

// A light-weight PPC Assembler
// Generates user mode instructions for the PPC architecture up

#ifndef V8_CODEGEN_PPC_ASSEMBLER_PPC_H_
#define V8_CODEGEN_PPC_ASSEMBLER_PPC_H_

#include <stdio.h>

#include <memory>

#include "src/base/numbers/double.h"
#include "src/codegen/assembler.h"
#include "src/codegen/constant-pool.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/label.h"
#include "src/codegen/ppc/constants-ppc.h"
#include "src/codegen/ppc/register-ppc.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

class SafepointTableBuilder;

// -----------------------------------------------------------------------------
// Machine instruction Operands

// Class Operand represents a shifter operand in data processing instructions
class V8_EXPORT_PRIVATE Operand {
 public:
  // immediate
  V8_INLINE explicit Operand(intptr_t immediate,
                             RelocInfo::Mode rmode = RelocInfo::NO_INFO)
      : rmode_(rmode) {
    value_.immediate = immediate;
  }
  V8_INLINE static Operand Zero() { return Operand(static_cast<intptr_t>(0)); }
  V8_INLINE explicit Operand(const ExternalReference& f)
      : rmode_(RelocInfo::EXTERNAL_REFERENCE) {
    value_.immediate = static_cast<intptr_t>(f.address());
  }
  explicit Operand(Handle<HeapObject> handle);
  V8_INLINE explicit Operand(Tagged<Smi> value) : rmode_(RelocInfo::NO_INFO) {
    value_.immediate = static_cast<intptr_t>(value.ptr());
  }
  // rm
  V8_INLINE explicit Operand(Register rm);

  static Operand EmbeddedNumber(double number);  // Smi or HeapNumber.

  // Return true if this is a register operand.
  V8_INLINE bool is_reg() const { return rm_.is_valid(); }

  bool must_output_reloc_info(const Assembler* assembler) const;

  inline intptr_t immediate() const {
    DCHECK(IsImmediate());
    DCHECK(!IsHeapNumberRequest());
    return value_.immediate;
  }
  bool IsImmediate() const { return !rm_.is_valid(); }

  HeapNumberRequest heap_number_request() const {
    DCHECK(IsHeapNumberRequest());
    return value_.heap_number_request;
  }

  Register rm() const { return rm_; }

  bool IsHeapNumberRequest() const {
    DCHECK_IMPLIES(is_heap_number_request_, IsImmediate());
    DCHECK_IMPLIES(is_heap_number_request_,
                   rmode_ == RelocInfo::FULL_EMBEDDED_OBJECT ||
                       rmode_ == RelocInfo::CODE_TARGET);
    return is_heap_number_request_;
  }

 private:
  Register rm_ = no_reg;
  union Value {
    Value() {}
    HeapNumberRequest heap_number_request;  // if is_heap_number_request_
    intptr_t immediate;                     // otherwise
  } value_;                                 // valid if rm_ == no_reg
  bool is_heap_number_request_ = false;

  RelocInfo::Mode rmode_;

  friend class Assembler;
  friend class MacroAssembler;
};

// Class MemOperand represents a memory operand in load and store instructions
// On PowerPC we have base register + 16bit signed value
// Alternatively we can have a 16bit signed value immediate
class V8_EXPORT_PRIVATE MemOperand {
 public:
  explicit MemOperand(Register rn, int64_t offset = 0);

  explicit MemOperand(Register ra, Register rb);

  explicit MemOperand(Register ra, Register rb, int64_t offset);

  int64_t offset() const { return offset_; }

  // PowerPC - base register
  Register ra() const { return ra_; }

  Register rb() const { return rb_; }

 private:
  Register ra_;     // base
  int64_t offset_;  // offset
  Register rb_;     // index

  friend class Assembler;
};

class DeferredRelocInfo {
 public:
  DeferredRelocInfo() {}
  DeferredRelocInfo(int position, RelocInfo::Mode rmode, intptr_t data)
      : position_(position), rmode_(rmode), data_(data) {}

  int position() const { return position_; }
  RelocInfo::Mode rmode() const { return rmode_; }
  intptr_t data() const { return data_; }

 private:
  int position_;
  RelocInfo::Mode rmode_;
  intptr_t data_;
};

class Assembler : public AssemblerBase {
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

  void MaybeEmitOutOfLineConstantPool() { EmitConstantPool(); }

  inline void CheckTrampolinePoolQuick(int extra_space = 0) {
    if (pc_offset() >= next_trampoline_check_ - extra_space) {
      CheckTrampolinePool();
    }
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

  // Links a label at the current pc_offset().  If already bound, returns the
  // bound position.  If already linked, returns the position of the prior link.
  // Otherwise, returns the current pc_offset().
  int link(Label* L);

  // Determines if Label is bound and near enough so that a single
  // branch instruction can be used to reach it.
  bool is_near(Label* L, Condition cond);

  // Returns the branch offset to the given label from the current code position
  // Links the label to the current position if it is still unbound
  int branch_offset(Label* L) {
    if (L->is_unused() && !trampoline_emitted_) {
      TrackBranch();
    }
    return link(L) - pc_offset();
  }

  V8_INLINE static bool IsConstantPoolLoadStart(
      Address pc, ConstantPoolEntry::Access* access = nullptr);
  V8_INLINE static bool IsConstantPoolLoadEnd(
      Address pc, ConstantPoolEntry::Access* access = nullptr);
  V8_INLINE static int GetConstantPoolOffset(Address pc,
                                             ConstantPoolEntry::Access access,
                                             ConstantPoolEntry::Type type);
  V8_INLINE void PatchConstantPoolAccessInstruction(
      int pc_offset, int offset, ConstantPoolEntry::Access access,
      ConstantPoolEntry::Type type);

  // Return the address in the constant pool of the code target address used by
  // the branch/call instruction at pc, or the object in a mov.
  V8_INLINE static Address target_constant_pool_address_at(
      Address pc, Address constant_pool, ConstantPoolEntry::Access access,
      ConstantPoolEntry::Type type);

  // Read/Modify the code target address in the branch/call instruction at pc.
  // The isolate argument is unused (and may be nullptr) when skipping flushing.
  V8_INLINE static Address target_address_at(Address pc, Address constant_pool);
  V8_INLINE static void set_target_address_at(
      Address pc, Address constant_pool, Address target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  // Read/Modify the code target address in the branch/call instruction at pc.
  inline static Tagged_t target_compressed_address_at(Address pc,
                                                      Address constant_pool);
  inline static void set_target_compressed_address_at(
      Address pc, Address constant_pool, Tagged_t target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  inline Handle<Object> code_target_object_handle_at(Address pc,
                                                     Address constant_pool);
  inline Handle<HeapObject> compressed_embedded_object_handle_at(
      Address pc, Address constant_pool);

  // This sets the branch destination.
  // This is for calls and branches within generated code.
  inline static void deserialization_set_special_target_at(
      Address instruction_payload, Tagged<Code>, Address target);

  // Get the size of the special target encoded at 'instruction_payload'.
  inline static int deserialization_special_target_size(
      Address instruction_payload);

  // This sets the internal reference at the pc.
  inline static void deserialization_set_target_internal_reference_at(
      Address pc, Address target,
      RelocInfo::Mode mode = RelocInfo::INTERNAL_REFERENCE);

  // Read/modify the uint32 constant used at pc.
  static inline uint32_t uint32_constant_at(Address pc, Address constant_pool);
  static inline void set_uint32_constant_at(
      Address pc, Address constant_pool, uint32_t new_constant,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  // Here we are patching the address in the LUI/ORI instruction pair.
  // These values are used in the serialization process and must be zero for
  // PPC platform, as Code, Embedded Object or External-reference pointers
  // are split across two consecutive instructions and don't exist separately
  // in the code, so the serializer should not step forwards in memory after
  // a target is resolved and written.
  static constexpr int kSpecialTargetSize = 0;

// Number of instructions to load an address via a mov sequence.
#if V8_TARGET_ARCH_PPC64
  static constexpr int kMovInstructionsConstantPool = 1;
  static constexpr int kMovInstructionsNoConstantPool = 5;
#if defined(V8_PPC_TAGGING_OPT)
  static constexpr int kTaggedLoadInstructions = 1;
#else
  static constexpr int kTaggedLoadInstructions = 2;
#endif
#else
  static constexpr int kMovInstructionsConstantPool = 1;
  static constexpr int kMovInstructionsNoConstantPool = 2;
  static constexpr int kTaggedLoadInstructions = 1;
#endif
  static constexpr int kMovInstructions = V8_EMBEDDED_CONSTANT_POOL_BOOL
                                              ? kMovInstructionsConstantPool
                                              : kMovInstructionsNoConstantPool;

  static inline int encode_crbit(const CRegister& cr, enum CRBit crbit) {
    return ((cr.code() * CRWIDTH) + crbit);
  }

#define DECLARE_PPC_X_INSTRUCTIONS_A_FORM(name, instr_name, instr_value)    \
  inline void name(const Register rt, const Register ra, const Register rb, \
                   const RCBit rc = LeaveRC) {                              \
    x_form(instr_name, rt, ra, rb, rc);                                     \
  }

#define DECLARE_PPC_X_INSTRUCTIONS_B_FORM(name, instr_name, instr_value)    \
  inline void name(const Register ra, const Register rs, const Register rb, \
                   const RCBit rc = LeaveRC) {                              \
    x_form(instr_name, rs, ra, rb, rc);                                     \
  }

#define DECLARE_PPC_X_INSTRUCTIONS_C_FORM(name, instr_name, instr_value) \
  inline void name(const Register dst, const Register src,               \
                   const RCBit rc = LeaveRC) {                           \
    x_form(instr_name, src, dst, r0, rc);                                \
  }

#define DECLARE_PPC_X_INSTRUCTIONS_D_FORM(name, instr_name, instr_value) \
  template <class R>                                                     \
  inline void name(const R rt, const Register ra, const Register rb,     \
                   const RCBit rc = LeaveRC) {                           \
    x_form(instr_name, rt.code(), ra.code(), rb.code(), rc);             \
  }                                                                      \
  template <class R>                                                     \
  inline void name(const R dst, const MemOperand& src) {                 \
    name(dst, src.ra(), src.rb());                                       \
  }

#define DECLARE_PPC_X_INSTRUCTIONS_E_FORM(name, instr_name, instr_value) \
  inline void name(const Register dst, const Register src, const int sh, \
                   const RCBit rc = LeaveRC) {                           \
    x_form(instr_name, src.code(), dst.code(), sh, rc);                  \
  }

#define DECLARE_PPC_X_INSTRUCTIONS_F_FORM(name, instr_name, instr_value)    \
  inline void name(const Register src1, const Register src2,                \
                   const CRegister cr = cr7, const RCBit rc = LeaveRC) {    \
    x_form(instr_name, cr, src1, src2, rc);                                 \
  }                                                                         \
  inline void name##w(const Register src1, const Register src2,             \
                      const CRegister cr = cr7, const RCBit rc = LeaveRC) { \
    x_form(instr_name, cr.code() * B2, src1.code(), src2.code(), LeaveRC);  \
  }

#define DECLARE_PPC_X_INSTRUCTIONS_G_FORM(name, instr_name, instr_value) \
  inline void name(const Register dst, const Register src) {             \
    x_form(instr_name, src, dst, r0, LeaveRC);                           \
  }

#define DECLARE_PPC_X_INSTRUCTIONS_EH_S_FORM(name, instr_name, instr_value) \
  inline void name(const Register dst, const MemOperand& src) {             \
    x_form(instr_name, src.ra(), dst, src.rb(), SetEH);                     \
  }
#define DECLARE_PPC_X_INSTRUCTIONS_EH_L_FORM(name, instr_name, instr_value) \
  inline void name(const Register dst, const MemOperand& src) {             \
    x_form(instr_name, src.ra(), dst, src.rb(), SetEH);                     \
  }

  inline void x_form(Instr instr, int f1, int f2, int f3, int rc) {
    emit(instr | f1 * B21 | f2 * B16 | f3 * B11 | rc);
  }
  inline void x_form(Instr instr, Register rs, Register ra, Register rb,
                     RCBit rc) {
    emit(instr | rs.code() * B21 | ra.code() * B16 | rb.code() * B11 | rc);
  }
  inline void x_form(Instr instr, Register ra, Register rs, Register rb,
                     EHBit eh = SetEH) {
    emit(instr | rs.code() * B21 | ra.code() * B16 | rb.code() * B11 | eh);
  }
  inline void x_form(Instr instr, CRegister cr, Register s1, Register s2,
                     RCBit rc) {
#if V8_TARGET_ARCH_PPC64
    int L = 1;
#else
    int L = 0;
#endif
    emit(instr | cr.code() * B23 | L * B21 | s1.code() * B16 | s2.code() * B11 |
         rc);
  }

  PPC_X_OPCODE_A_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_A_FORM)
  PPC_X_OPCODE_B_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_B_FORM)
  PPC_X_OPCODE_C_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_C_FORM)
  PPC_X_OPCODE_D_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_D_FORM)
  PPC_X_OPCODE_E_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_E_FORM)
  PPC_X_OPCODE_F_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_F_FORM)
  PPC_X_OPCODE_G_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_G_FORM)
  PPC_X_OPCODE_EH_S_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_EH_S_FORM)
  PPC_X_OPCODE_EH_L_FORM_LIST(DECLARE_PPC_X_INSTRUCTIONS_EH_L_FORM)

  inline void notx(Register dst, Register src, RCBit rc = LeaveRC) {
    nor(dst, src, src, rc);
  }
  inline void lwax(Register rt, const MemOperand& src) {
#if V8_TARGET_ARCH_PPC64
    Register ra = src.ra();
    Register rb = src.rb();
    DCHECK(ra != r0);
    x_form(LWAX, rt, ra, rb, LeaveRC);
#else
    lwzx(rt, src);
#endif
  }
  inline void extsw(Register rs, Register ra, RCBit rc = LeaveRC) {
#if V8_TARGET_ARCH_PPC64
    emit(EXT2 | EXTSW | ra.code() * B21 | rs.code() * B16 | rc);
#else
    // nop on 32-bit
    DCHECK(rs == ra && rc == LeaveRC);
#endif
  }

#undef DECLARE_PPC_X_INSTRUCTIONS_A_FORM
#undef DECLARE_PPC_X_INSTRUCTIONS_B_FORM
#undef DECLARE_PPC_X_INSTRUCTIONS_C_FORM
#undef DECLARE_PPC_X_INSTRUCTIONS_D_FORM
#undef DECLARE_PPC_X_INSTRUCTIONS_E_FORM
#undef DECLARE_PPC_X_INSTRUCTIONS_F_FORM
#undef DECLARE_PPC_X_INSTRUCTIONS_G_FORM
#undef DECLARE_PPC_X_INSTRUCTIONS_EH_S_FORM
#undef DECLARE_PPC_X_INSTRUCTIONS_EH_L_FORM

#define DECLARE_PPC_XX2_VECTOR_INSTRUCTIONS(name, instr_name, instr_value) \
  inline void name(const Simd128Register rt, const Simd128Register rb) {   \
    xx2_form(instr_name, rt, rb);                                          \
  }
#define DECLARE_PPC_XX2_SCALAR_INSTRUCTIONS(name, instr_name, instr_value) \
  inline void name(const DoubleRegister rt, const DoubleRegister rb) {     \
    xx2_form(instr_name, rt, rb);                                          \
  }

  template <typename T>
  inline void xx2_form(Instr instr, T t, T b) {
    static_assert(std::is_same<T, Simd128Register>::value ||
                      std::is_same<T, DoubleRegister>::value,
                  "VSX only uses FP or Vector registers.");
    // Using FP (low VSR) registers.
    int BX = 0, TX = 0;
    // Using VR (high VSR) registers when Simd registers are used.
    if (std::is_same<T, Simd128Register>::value) {
      BX = TX = 1;
    }

    emit(instr | (t.code() & 0x1F) * B21 | (b.code() & 0x1F) * B11 | BX * B1 |
         TX);
  }

  PPC_XX2_OPCODE_VECTOR_A_FORM_LIST(DECLARE_PPC_XX2_VECTOR_INSTRUCTIONS)
  PPC_XX2_OPCODE_SCALAR_A_FORM_LIST(DECLARE_PPC_XX2_SCALAR_INSTRUCTIONS)
  PPC_XX2_OPCODE_B_FORM_LIST(DECLARE_PPC_XX2_VECTOR_INSTRUCTIONS)
#undef DECLARE_PPC_XX2_VECTOR_INSTRUCTIONS
#undef DECLARE_PPC_XX2_SCALAR_INSTRUCTIONS

#define DECLARE_PPC_XX3_VECTOR_INSTRUCTIONS_A_FORM(name, instr_name,     \
                                                   instr_value)          \
  inline void name(const Simd128Register rt, const Simd128Register ra,   \
                   const Simd128Register rb, const RCBit rc = LeaveRC) { \
    xx3_form(instr_name, rt, ra, rb, rc);                                \
  }
#define DECLARE_PPC_XX3_VECTOR_INSTRUCTIONS_B_FORM(name, instr_name,   \
                                                   instr_value)        \
  inline void name(const Simd128Register rt, const Simd128Register ra, \
                   const Simd128Register rb) {                         \
    xx3_form(instr_name, rt, ra, rb);                                  \
  }
#define DECLARE_PPC_XX3_SCALAR_INSTRUCTIONS(name, instr_name, instr_value) \
  inline void name(const DoubleRegister rt, const DoubleRegister ra,       \
                   const DoubleRegister rb) {                              \
    xx3_form(instr_name, rt, ra, rb);                                      \
  }

  inline void xx3_form(Instr instr, Simd128Register t, Simd128Register a,
                       Simd128Register b, int rc) {
    // Using VR (high VSR) registers.
    int AX = 1, BX = 1, TX = 1;

    emit(instr | (t.code() & 0x1F) * B21 | (a.code() & 0x1F) * B16 |
         (b.code() & 0x1F) * B11 | rc * B10 | AX * B2 | BX * B1 | TX);
  }

  template <typename T>
  inline void xx3_form(Instr instr, T t, T a, T b) {
    static_assert(std::is_same<T, Simd128Register>::value ||
                      std::is_same<T, DoubleRegister>::value,
                  "VSX only uses FP or Vector registers.");
    // Using FP (low VSR) registers.
    int AX = 0, BX = 0, TX = 0;
    // Using VR (high VSR) registers when Simd registers are used.
    if (std::is_same<T, Simd128Register>::value) {
      AX = BX = TX = 1;
    }

    emit(instr | (t.code() & 0x1F) * B21 | (a.code() & 0x1F) * B16 |
         (b.code() & 0x1F) * B11 | AX * B2 | BX * B1 | TX);
  }

  PPC_XX3_OPCODE_VECTOR_A_FORM_LIST(DECLARE_PPC_XX3_VECTOR_INSTRUCTIONS_A_FORM)
  PPC_XX3_OPCODE_VECTOR_B_FORM_LIST(DECLARE_PPC_XX3_VECTOR_INSTRUCTIONS_B_FORM)
  PPC_XX3_OPCODE_SCALAR_LIST(DECLARE_PPC_XX3_SCALAR_INSTRUCTIONS)
#undef DECLARE_PPC_XX3_VECTOR_INSTRUCTIONS_A_FORM
#undef DECLARE_PPC_XX3_VECTOR_INSTRUCTIONS_B_FORM
#undef DECLARE_PPC_XX3_SCALAR_INSTRUCTIONS

#define DECLARE_PPC_VX_INSTRUCTIONS_A_FORM(name, instr_name, instr_value) \
  inline void name(const Simd128Register rt, const Simd128Register rb,    \
                   const Operand& imm) {                                  \
    vx_form(instr_name, rt, rb, imm);                                     \
  }
#define DECLARE_PPC_VX_INSTRUCTIONS_B_FORM(name, instr_name, instr_value) \
  inline void name(const Simd128Register rt, const Simd128Register ra,    \
                   const Simd128Register rb) {                            \
    vx_form(instr_name, rt, ra, rb);                                      \
  }
#define DECLARE_PPC_VX_INSTRUCTIONS_C_FORM(name, instr_name, instr_value) \
  inline void name(const Simd128Register rt, const Simd128Register rb) {  \
    vx_form(instr_name, rt, rb);                                          \
  }
#define DECLARE_PPC_VX_INSTRUCTIONS_E_FORM(name, instr_name, instr_value) \
  inline void name(const Simd128Register rt, const Operand& imm) {        \
    vx_form(instr_name, rt, imm);                                         \
  }
#define DECLARE_PPC_VX_INSTRUCTIONS_F_FORM(name, instr_name, instr_value) \
  inline void name(const Register rt, const Simd128Register rb) {         \
    vx_form(instr_name, rt, rb);                                          \
  }
#define DECLARE_PPC_VX_INSTRUCTIONS_G_FORM(name, instr_name, instr_value) \
  inline void name(const Simd128Register rt, const Register rb,           \
                   const Operand& imm) {                                  \
    vx_form(instr_name, rt, rb, imm);                                     \
  }

  inline void vx_form(Instr instr, Simd128Register rt, Simd128Register rb,
                      const Operand& imm) {
    emit(instr | (rt.code() & 0x1F) * B21 | (imm.immediate() & 0x1F) * B16 |
         (rb.code() & 0x1F) * B11);
  }
  inline void vx_form(Instr instr, Simd128Register rt, Simd128Register ra,
                      Simd128Register rb) {
    emit(instr | (rt.code() & 0x1F) * B21 | ra.code() * B16 |
         (rb.code() & 0x1F) * B11);
  }
  inline void vx_form(Instr instr, Simd128Register rt, Simd128Register rb) {
    emit(instr | (rt.code() & 0x1F) * B21 | (rb.code() & 0x1F) * B11);
  }
  inline void vx_form(Instr instr, Simd128Register rt, const Operand& imm) {
    emit(instr | (rt.code() & 0x1F) * B21 | (imm.immediate() & 0x1F) * B16);
  }
  inline void vx_form(Instr instr, Register rt, Simd128Register rb) {
    emit(instr | (rt.code() & 0x1F) * B21 | (rb.code() & 0x1F) * B11);
  }
  inline void vx_form(Instr instr, Simd128Register rt, Register rb,
                      const Operand& imm) {
    emit(instr | (rt.code() & 0x1F) * B21 | (imm.immediate() & 0x1F) * B16 |
         (rb.code() & 0x1F) * B11);
  }

  PPC_VX_OPCODE_A_FORM_LIST(DECLARE_PPC_VX_INSTRUCTIONS_A_FORM)
  PPC_VX_OPCODE_B_FORM_LIST(DECLARE_PPC_VX_INSTRUCTIONS_B_FORM)
  PPC_VX_OPCODE_C_FORM_LIST(DECLARE_PPC_VX_INSTRUCTIONS_C_FORM)
  PPC_VX_OPCODE_D_FORM_LIST(
      DECLARE_PPC_VX_INSTRUCTIONS_C_FORM) /* OPCODE_D_FORM can use
                                             INSTRUCTIONS_C_FORM */
  PPC_VX_OPCODE_E_FORM_LIST(DECLARE_PPC_VX_INSTRUCTIONS_E_FORM)
  PPC_VX_OPCODE_F_FORM_LIST(DECLARE_PPC_VX_INSTRUCTIONS_F_FORM)
  PPC_VX_OPCODE_G_FORM_LIST(DECLARE_PPC_VX_INSTRUCTIONS_G_FORM)
#undef DECLARE_PPC_VX_INSTRUCTIONS_A_FORM
#undef DECLARE_PPC_VX_INSTRUCTIONS_B_FORM
#undef DECLARE_PPC_VX_INSTRUCTIONS_C_FORM
#undef DECLARE_PPC_VX_INSTRUCTIONS_E_FORM
#undef DECLARE_PPC_VX_INSTRUCTIONS_F_FORM
#undef DECLARE_PPC_VX_INSTRUCTIONS_G_FORM

#define DECLARE_PPC_VA_INSTRUCTIONS_A_FORM(name, instr_name, instr_value) \
  inline void name(const Simd128Register rt, const Simd128Register ra,    \
                   const Simd128Register rb, const Simd128Register rc) {  \
    va_form(instr_name, rt, ra, rb, rc);                                  \
  }

  inline void va_form(Instr instr, Simd128Register rt, Simd128Register ra,
                      Simd128Register rb, Simd128Register rc) {
    emit(instr | (rt.code() & 0x1F) * B21 | (ra.code() & 0x1F) * B16 |
         (rb.code() & 0x1F) * B11 | (rc.code() & 0x1F) * B6);
  }

  PPC_VA_OPCODE_A_FORM_LIST(DECLARE_PPC_VA_INSTRUCTIONS_A_FORM)
#undef DECLARE_PPC_VA_INSTRUCTIONS_A_FORM

#define DECLARE_PPC_VC_INSTRUCTIONS(name, instr_name, instr_value)       \
  inline void name(const Simd128Register rt, const Simd128Register ra,   \
                   const Simd128Register rb, const RCBit rc = LeaveRC) { \
    vc_form(instr_name, rt, ra, rb, rc);                                 \
  }

  inline void vc_form(Instr instr, Simd128Register rt, Simd128Register ra,
                      Simd128Register rb, int rc) {
    emit(instr | (rt.code() & 0x1F) * B21 | (ra.code() & 0x1F) * B16 |
         (rb.code() & 0x1F) * B11 | rc * B10);
  }

  PPC_VC_OPCODE_LIST(DECLARE_PPC_VC_INSTRUCTIONS)
#undef DECLARE_PPC_VC_INSTRUCTIONS

#define DECLARE_PPC_PREFIX_INSTRUCTIONS_TYPE_00(name, instr_name, instr_value) \
  inline void name(const Operand& imm, const PRBit pr = LeavePR) {             \
    prefix_form(instr_name, imm, pr);                                          \
  }
#define DECLARE_PPC_PREFIX_INSTRUCTIONS_TYPE_10(name, instr_name, instr_value) \
  inline void name(const Operand& imm, const PRBit pr = LeavePR) {             \
    prefix_form(instr_name, imm, pr);                                          \
  }
  inline void prefix_form(Instr instr, const Operand& imm, int pr) {
    emit_prefix(instr | pr * B20 | (imm.immediate() & kImm18Mask));
  }
  PPC_PREFIX_OPCODE_TYPE_00_LIST(DECLARE_PPC_PREFIX_INSTRUCTIONS_TYPE_00)
  PPC_PREFIX_OPCODE_TYPE_10_LIST(DECLARE_PPC_PREFIX_INSTRUCTIONS_TYPE_10)
#undef DECLARE_PPC_PREFIX_INSTRUCTIONS_TYPE_00
#undef DECLARE_PPC_PREFIX_INSTRUCTIONS_TYPE_10

  RegList* GetScratchRegisterList() { return &scratch_register_list_; }
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
  void bclr(BOfield bo, int condition_bit, LKBit lk);
  void blr();
  void bc(int branch_offset, BOfield bo, int condition_bit, LKBit lk = LeaveLK);
  void b(int branch_offset, LKBit lk);

  void bcctr(BOfield bo, int condition_bit, LKBit lk);
  void bctr();
  void bctrl();

  // Convenience branch instructions using labels
  void b(Label* L, LKBit lk = LeaveLK) { b(branch_offset(L), lk); }

  inline CRegister cmpi_optimization(CRegister cr) {
    // Check whether the branch is preceded by an optimizable cmpi against 0.
    // The cmpi can be deleted if it is also preceded by an instruction that
    // sets the register used by the compare and supports a dot form.
    unsigned int sradi_mask = kOpcodeMask | kExt2OpcodeVariant2Mask;
    unsigned int srawi_mask = kOpcodeMask | kExt2OpcodeMask;
    int pos = pc_offset();
    int cmpi_pos = pc_offset() - kInstrSize;

    if (cmpi_pos > 0 && optimizable_cmpi_pos_ == cmpi_pos &&
        cmpi_cr_.code() == cr.code() && last_bound_pos_ != pos) {
      int xpos = cmpi_pos - kInstrSize;
      int xinstr = instr_at(xpos);
      int cmpi_ra = (instr_at(cmpi_pos) & 0x1f0000) >> 16;
      // ra is at the same bit position for the three cases below.
      int ra = (xinstr & 0x1f0000) >> 16;
      if (cmpi_ra == ra) {
        if ((xinstr & sradi_mask) == (EXT2 | SRADIX)) {
          cr = cr0;
          instr_at_put(xpos, xinstr | SetRC);
          pc_ -= kInstrSize;
        } else if ((xinstr & srawi_mask) == (EXT2 | SRAWIX)) {
          cr = cr0;
          instr_at_put(xpos, xinstr | SetRC);
          pc_ -= kInstrSize;
        } else if ((xinstr & kOpcodeMask) == ANDIx) {
          cr = cr0;
          pc_ -= kInstrSize;
          // nothing to do here since andi. records.
        }
        // didn't match one of the above, must keep cmpwi.
      }
    }
    return cr;
  }

  void bc_short(Condition cond, Label* L, CRegister cr = cr7,
                LKBit lk = LeaveLK) {
    DCHECK(cond != al);
    DCHECK(cr.code() >= 0 && cr.code() <= 7);

    cr = cmpi_optimization(cr);

    int b_offset = branch_offset(L);

    switch (cond) {
      case eq:
        bc(b_offset, BT, encode_crbit(cr, CR_EQ), lk);
        break;
      case ne:
        bc(b_offset, BF, encode_crbit(cr, CR_EQ), lk);
        break;
      case gt:
        bc(b_offset, BT, encode_crbit(cr, CR_GT), lk);
        break;
      case le:
        bc(b_offset, BF, encode_crbit(cr, CR_GT), lk);
        break;
      case lt:
        bc(b_offset, BT, encode_crbit(cr, CR_LT), lk);
        break;
      case ge:
        bc(b_offset, BF, encode_crbit(cr, CR_LT), lk);
        break;
      case unordered:
        bc(b_offset, BT, encode_crbit(cr, CR_FU), lk);
        break;
      case ordered:
        bc(b_offset, BF, encode_crbit(cr, CR_FU), lk);
        break;
      case overflow:
        bc(b_offset, BT, encode_crbit(cr, CR_SO), lk);
        break;
      case nooverflow:
        bc(b_offset, BF, encode_crbit(cr, CR_SO), lk);
        break;
      default:
        UNIMPLEMENTED();
    }
  }

  void bclr(Condition cond, CRegister cr = cr7, LKBit lk = LeaveLK) {
    DCHECK(cond != al);
    DCHECK(cr.code() >= 0 && cr.code() <= 7);

    cr = cmpi_optimization(cr);

    switch (cond) {
      case eq:
        bclr(BT, encode_crbit(cr, CR_EQ), lk);
        break;
      case ne:
        bclr(BF, encode_crbit(cr, CR_EQ), lk);
        break;
      case gt:
        bclr(BT, encode_crbit(cr, CR_GT), lk);
        break;
      case le:
        bclr(BF, encode_crbit(cr, CR_GT), lk);
        break;
      case lt:
        bclr(BT, encode_crbit(cr, CR_LT), lk);
        break;
      case ge:
        bclr(BF, encode_crbit(cr, CR_LT), lk);
        break;
      case unordered:
        bclr(BT, encode_crbit(cr, CR_FU), lk);
        break;
      case ordered:
        bclr(BF, encode_crbit(cr, CR_FU), lk);
        break;
      case overflow:
        bclr(BT, encode_crbit(cr, CR_SO), lk);
        break;
      case nooverflow:
        bclr(BF, encode_crbit(cr, CR_SO), lk);
        break;
      default:
        UNIMPLEMENTED();
    }
  }

  void isel(Register rt, Register ra, Register rb, int cb);
  void isel(Condition cond, Register rt, Register ra, Register rb,
            CRegister cr = cr7) {
    DCHECK(cond != al);
    DCHECK(cr.code() >= 0 && cr.code() <= 7);

    cr = cmpi_optimization(cr);

    switch (cond) {
      case eq:
        isel(rt, ra, rb, encode_crbit(cr, CR_EQ));
        break;
      case ne:
        isel(rt, rb, ra, encode_crbit(cr, CR_EQ));
        break;
      case gt:
        isel(rt, ra, rb, encode_crbit(cr, CR_GT));
        break;
      case le:
        isel(rt, rb, ra, encode_crbit(cr, CR_GT));
        break;
      case lt:
        isel(rt, ra, rb, encode_crbit(cr, CR_LT));
        break;
      case ge:
        isel(rt, rb, ra, encode_crbit(cr, CR_LT));
        break;
      case unordered:
        isel(rt, ra, rb, encode_crbit(cr, CR_FU));
        break;
      case ordered:
        isel(rt, rb, ra, encode_crbit(cr, CR_FU));
        break;
      case overflow:
        isel(rt, ra, rb, encode_crbit(cr, CR_SO));
        break;
      case nooverflow:
        isel(rt, rb, ra, encode_crbit(cr, CR_SO));
        break;
      default:
        UNIMPLEMENTED();
    }
  }

  void b(Condition cond, Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    if (cond == al) {
      b(L, lk);
      return;
    }

    if ((L->is_bound() && is_near(L, cond))) {
      bc_short(cond, L, cr, lk);
      return;
    }

    Label skip;
    Condition neg_cond = NegateCondition(cond);
    bc_short(neg_cond, &skip, cr);
    b(L, lk);
    bind(&skip);
  }

  void bne(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(ne, L, cr, lk);
  }
  void beq(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(eq, L, cr, lk);
  }
  void blt(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(lt, L, cr, lk);
  }
  void bge(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(ge, L, cr, lk);
  }
  void ble(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(le, L, cr, lk);
  }
  void bgt(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(gt, L, cr, lk);
  }
  void bunordered(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(unordered, L, cr, lk);
  }
  void bordered(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(ordered, L, cr, lk);
  }
  void boverflow(Label* L, CRegister cr = cr0, LKBit lk = LeaveLK) {
    b(overflow, L, cr, lk);
  }
  void bnooverflow(Label* L, CRegister cr = cr0, LKBit lk = LeaveLK) {
    b(nooverflow, L, cr, lk);
  }

  // Decrement CTR; branch if CTR != 0
  void bdnz(Label* L, LKBit lk = LeaveLK) {
    bc(branch_offset(L), DCBNZ, 0, lk);
  }

  // Data-processing instructions

  void sub(Register dst, Register src1, Register src2, OEBit s = LeaveOE,
           RCBit r = LeaveRC);

  void subc(Register dst, Register src1, Register src2, OEBit s = LeaveOE,
            RCBit r = LeaveRC);
  void sube(Register dst, Register src1, Register src2, OEBit s = LeaveOE,
            RCBit r = LeaveRC);

  void subfic(Register dst, Register src, const Operand& imm);

  void add(Register dst, Register src1, Register src2, OEBit s = LeaveOE,
           RCBit r = LeaveRC);

  void addc(Register dst, Register src1, Register src2, OEBit o = LeaveOE,
            RCBit r = LeaveRC);
  void adde(Register dst, Register src1, Register src2, OEBit o = LeaveOE,
            RCBit r = LeaveRC);
  void addze(Register dst, Register src1, OEBit o = LeaveOE, RCBit r = LeaveRC);

  void mullw(Register dst, Register src1, Register src2, OEBit o = LeaveOE,
             RCBit r = LeaveRC);

  void mulhw(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void mulhwu(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void mulhd(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void mulhdu(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void mulli(Register dst, Register src, const Operand& imm);

  void divw(Register dst, Register src1, Register src2, OEBit o = LeaveOE,
            RCBit r = LeaveRC);
  void divwu(Register dst, Register src1, Register src2, OEBit o = LeaveOE,
             RCBit r = LeaveRC);

  void addi(Register dst, Register src, const Operand& imm);
  void addis(Register dst, Register src, const Operand& imm);
  void addic(Register dst, Register src, const Operand& imm);

  void andi(Register ra, Register rs, const Operand& imm);
  void andis(Register ra, Register rs, const Operand& imm);
  void ori(Register dst, Register src, const Operand& imm);
  void oris(Register dst, Register src, const Operand& imm);
  void xori(Register dst, Register src, const Operand& imm);
  void xoris(Register ra, Register rs, const Operand& imm);
  void cmpi(Register src1, const Operand& src2, CRegister cr = cr7);
  void cmpli(Register src1, const Operand& src2, CRegister cr = cr7);
  void cmpwi(Register src1, const Operand& src2, CRegister cr = cr7);
  void cmplwi(Register src1, const Operand& src2, CRegister cr = cr7);
  void li(Register dst, const Operand& src);
  void lis(Register dst, const Operand& imm);
  void mr(Register dst, Register src);

  void lbz(Register dst, const MemOperand& src);
  void lhz(Register dst, const MemOperand& src);
  void lha(Register dst, const MemOperand& src);
  void lwz(Register dst, const MemOperand& src);
  void lwzu(Register dst, const MemOperand& src);
  void lwa(Register dst, const MemOperand& src);
  void stb(Register dst, const MemOperand& src);
  void sth(Register dst, const MemOperand& src);
  void stw(Register dst, const MemOperand& src);
  void stwu(Register dst, const MemOperand& src);
  void neg(Register rt, Register ra, OEBit o = LeaveOE, RCBit c = LeaveRC);

#if V8_TARGET_ARCH_PPC64
  void ld(Register rd, const MemOperand& src);
  void ldu(Register rd, const MemOperand& src);
  void std(Register rs, const MemOperand& src);
  void stdu(Register rs, const MemOperand& src);
  void rldic(Register dst, Register src, int sh, int mb, RCBit r = LeaveRC);
  void rldicl(Register dst, Register src, int sh, int mb, RCBit r = LeaveRC);
  void rldcl(Register ra, Register rs, Register rb, int mb, RCBit r = LeaveRC);
  void rldicr(Register dst, Register src, int sh, int me, RCBit r = LeaveRC);
  void rldimi(Register dst, Register src, int sh, int mb, RCBit r = LeaveRC);
  void sldi(Register dst, Register src, const Operand& val, RCBit rc = LeaveRC);
  void srdi(Register dst, Register src, const Operand& val, RCBit rc = LeaveRC);
  void clrrdi(Register dst, Register src, const Operand& val,
              RCBit rc = LeaveRC);
  void clrldi(Register dst, Register src, const Operand& val,
              RCBit rc = LeaveRC);
  void sradi(Register ra, Register rs, int sh, RCBit r = LeaveRC);
  void rotld(Register ra, Register rs, Register rb, RCBit r = LeaveRC);
  void rotldi(Register ra, Register rs, int sh, RCBit r = LeaveRC);
  void rotrdi(Register ra, Register rs, int sh, RCBit r = LeaveRC);
  void mulld(Register dst, Register src1, Register src2, OEBit o = LeaveOE,
             RCBit r = LeaveRC);
  void divd(Register dst, Register src1, Register src2, OEBit o = LeaveOE,
            RCBit r = LeaveRC);
  void divdu(Register dst, Register src1, Register src2, OEBit o = LeaveOE,
             RCBit r = LeaveRC);
#endif

  void rlwinm(Register ra, Register rs, int sh, int mb, int me,
              RCBit rc = LeaveRC);
  void rlwimi(Register ra, Register rs, int sh, int mb, int me,
              RCBit rc = LeaveRC);
  void rlwnm(Register ra, Register rs, Register rb, int mb, int me,
             RCBit rc = LeaveRC);
  void slwi(Register dst, Register src, const Operand& val, RCBit rc = LeaveRC);
  void srwi(Register dst, Register src, const Operand& val, RCBit rc = LeaveRC);
  void clrrwi(Register dst, Register src, const Operand& val,
              RCBit rc = LeaveRC);
  void clrlwi(Register dst, Register src, const Operand& val,
              RCBit rc = LeaveRC);
  void rotlw(Register ra, Register rs, Register rb, RCBit r = LeaveRC);
  void rotlwi(Register ra, Register rs, int sh, RCBit r = LeaveRC);
  void rotrwi(Register ra, Register rs, int sh, RCBit r = LeaveRC);

  void subi(Register dst, Register src1, const Operand& src2);

  void mov(Register dst, const Operand& src);
  void bitwise_mov(Register dst, intptr_t value);
  void bitwise_mov32(Register dst, int32_t value);
  void bitwise_add32(Register dst, Register src, int32_t value);

  // Patch the offset to the return address after Call.
  void patch_pc_address(Register dst, int pc_offset, int return_address_offset);

  // Load the position of the label relative to the generated code object
  // pointer in a register.
  void mov_label_offset(Register dst, Label* label);

  // dst = base + label position + delta
  void add_label_offset(Register dst, Register base, Label* label,
                        int delta = 0);

  // Load the address of the label in a register and associate with an
  // internal reference relocation.
  void mov_label_addr(Register dst, Label* label);

  // Emit the address of the label (i.e. a jump table entry) and associate with
  // an internal reference relocation.
  void emit_label_addr(Label* label);

  // Multiply instructions
  void mul(Register dst, Register src1, Register src2, OEBit s = LeaveOE,
           RCBit r = LeaveRC);

  // Miscellaneous arithmetic instructions

  // Special register access
  void crxor(int bt, int ba, int bb);
  void crclr(int bt) { crxor(bt, bt, bt); }
  void creqv(int bt, int ba, int bb);
  void crset(int bt) { creqv(bt, bt, bt); }
  void mflr(Register dst);
  void mtlr(Register src);
  void mtctr(Register src);
  void mtxer(Register src);
  void mcrfs(CRegister cr, FPSCRBit bit);
  void mfcr(Register dst);
  void mtcrf(Register src, uint8_t FXM);
#if V8_TARGET_ARCH_PPC64
  void mffprd(Register dst, DoubleRegister src);
  void mffprwz(Register dst, DoubleRegister src);
  void mtfprd(DoubleRegister dst, Register src);
  void mtfprwz(DoubleRegister dst, Register src);
  void mtfprwa(DoubleRegister dst, Register src);
#endif

  // Exception-generating instructions and debugging support
  void stop(Condition cond = al, int32_t code = kDefaultStopCode,
            CRegister cr = cr7);

  void bkpt(uint32_t imm16);  // v5 and above

  void dcbf(Register ra, Register rb);
  void sync();
  void lwsync();
  void icbi(Register ra, Register rb);
  void isync();

  // Support for floating point
  void lfd(const DoubleRegister frt, const MemOperand& src);
  void lfdu(const DoubleRegister frt, const MemOperand& src);
  void lfs(const DoubleRegister frt, const MemOperand& src);
  void lfsu(const DoubleRegister frt, const MemOperand& src);
  void stfd(const DoubleRegister frs, const MemOperand& src);
  void stfdu(const DoubleRegister frs, const MemOperand& src);
  void stfs(const DoubleRegister frs, const MemOperand& src);
  void stfsu(const DoubleRegister frs, const MemOperand& src);

  void fadd(const DoubleRegister frt, const DoubleRegister fra,
            const DoubleRegister frb, RCBit rc = LeaveRC);
  void fsub(const DoubleRegister frt, const DoubleRegister fra,
            const DoubleRegister frb, RCBit rc = LeaveRC);
  void fdiv(const DoubleRegister frt, const DoubleRegister fra,
            const DoubleRegister frb, RCBit rc = LeaveRC);
  void fmul(const DoubleRegister frt, const DoubleRegister fra,
            const DoubleRegister frc, RCBit rc = LeaveRC);
  void fcmpu(const DoubleRegister fra, const DoubleRegister frb,
             CRegister cr = cr7);
  void fmr(const DoubleRegister frt, const DoubleRegister frb,
           RCBit rc = LeaveRC);
  void fctiwz(const DoubleRegister frt, const DoubleRegister frb);
  void fctiw(const DoubleRegister frt, const DoubleRegister frb);
  void fctiwuz(const DoubleRegister frt, const DoubleRegister frb);
  void frin(const DoubleRegister frt, const DoubleRegister frb,
            RCBit rc = LeaveRC);
  void friz(const DoubleRegister frt, const DoubleRegister frb,
            RCBit rc = LeaveRC);
  void frip(const DoubleRegister frt, const DoubleRegister frb,
            RCBit rc = LeaveRC);
  void frim(const DoubleRegister frt, const DoubleRegister frb,
            RCBit rc = LeaveRC);
  void frsp(const DoubleRegister frt, const DoubleRegister frb,
            RCBit rc = LeaveRC);
  void fcfid(const DoubleRegister frt, const DoubleRegister frb,
             RCBit rc = LeaveRC);
  void fcfidu(const DoubleRegister frt, const DoubleRegister frb,
              RCBit rc = LeaveRC);
  void fcfidus(const DoubleRegister frt, const DoubleRegister frb,
               RCBit rc = LeaveRC);
  void fcfids(const DoubleRegister frt, const DoubleRegister frb,
              RCBit rc = LeaveRC);
  void fctid(const DoubleRegister frt, const DoubleRegister frb,
             RCBit rc = LeaveRC);
  void fctidz(const DoubleRegister frt, const DoubleRegister frb,
              RCBit rc = LeaveRC);
  void fctidu(const DoubleRegister frt, const DoubleRegister frb,
              RCBit rc = LeaveRC);
  void fctiduz(const DoubleRegister frt, const DoubleRegister frb,
               RCBit rc = LeaveRC);
  void fsel(const DoubleRegister frt, const DoubleRegister fra,
            const DoubleRegister frc, const DoubleRegister frb,
            RCBit rc = LeaveRC);
  void fneg(const DoubleRegister frt, const DoubleRegister frb,
            RCBit rc = LeaveRC);
  void mtfsb0(FPSCRBit bit, RCBit rc = LeaveRC);
  void mtfsb1(FPSCRBit bit, RCBit rc = LeaveRC);
  void mtfsfi(int bf, int immediate, RCBit rc = LeaveRC);
  void mffs(const DoubleRegister frt, RCBit rc = LeaveRC);
  void mtfsf(const DoubleRegister frb, bool L = 1, int FLM = 0, bool W = 0,
             RCBit rc = LeaveRC);
  void fsqrt(const DoubleRegister frt, const DoubleRegister frb,
             RCBit rc = LeaveRC);
  void fabs(const DoubleRegister frt, const DoubleRegister frb,
            RCBit rc = LeaveRC);
  void fmadd(const DoubleRegister frt, const DoubleRegister fra,
             const DoubleRegister frc, const DoubleRegister frb,
             RCBit rc = LeaveRC);
  void fmsub(const DoubleRegister frt, const DoubleRegister fra,
             const DoubleRegister frc, const DoubleRegister frb,
             RCBit rc = LeaveRC);
  void fcpsgn(const DoubleRegister frt, const DoubleRegister fra,
              const DoubleRegister frc, RCBit rc = LeaveRC);

  // Vector instructions
  void mfvsrd(const Register ra, const Simd128Register r);
  void mfvsrwz(const Register ra, const Simd128Register r);
  void mtvsrd(const Simd128Register rt, const Register ra);
  void mtvsrdd(const Simd128Register rt, const Register ra, const Register rb);
  void lxvd(const Simd128Register rt, const MemOperand& src);
  void lxvx(const Simd128Register rt, const MemOperand& src);
  void lxsdx(const Simd128Register rt, const MemOperand& src);
  void lxsibzx(const Simd128Register rt, const MemOperand& src);
  void lxsihzx(const Simd128Register rt, const MemOperand& src);
  void lxsiwzx(const Simd128Register rt, const MemOperand& src);
  void stxsdx(const Simd128Register rs, const MemOperand& dst);
  void stxsibx(const Simd128Register rs, const MemOperand& dst);
  void stxsihx(const Simd128Register rs, const MemOperand& dst);
  void stxsiwx(const Simd128Register rs, const MemOperand& dst);
  void stxvd(const Simd128Register rt, const MemOperand& dst);
  void stxvx(const Simd128Register rt, const MemOperand& dst);
  void xxspltib(const Simd128Register rt, const Operand& imm);

  // Prefixed instructioons.
  void paddi(Register dst, Register src, const Operand& imm);
  void pli(Register dst, const Operand& imm);
  void psubi(Register dst, Register src, const Operand& imm);
  void plbz(Register dst, const MemOperand& src);
  void plhz(Register dst, const MemOperand& src);
  void plha(Register dst, const MemOperand& src);
  void plwz(Register dst, const MemOperand& src);
  void plwa(Register dst, const MemOperand& src);
  void pld(Register dst, const MemOperand& src);
  void plfs(DoubleRegister dst, const MemOperand& src);
  void plfd(DoubleRegister dst, const MemOperand& src);
  void pstb(Register src, const MemOperand& dst);
  void psth(Register src, const MemOperand& dst);
  void pstw(Register src, const MemOperand& dst);
  void pstd(Register src, const MemOperand& dst);
  void pstfs(const DoubleRegister src, const MemOperand& dst);
  void pstfd(const DoubleRegister src, const MemOperand& dst);

  // Pseudo instructions

  // Different nop operations are used by the code generator to detect certain
  // states of the generated code.
  enum NopMarkerTypes {
    NON_MARKING_NOP = 0,
    GROUP_ENDING_NOP,
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

  void push(Register src) {
#if V8_TARGET_ARCH_PPC64
    stdu(src, MemOperand(sp, -kSystemPointerSize));
#else
    stwu(src, MemOperand(sp, -kSystemPointerSize));
#endif
  }

  void pop(Register dst) {
#if V8_TARGET_ARCH_PPC64
    ld(dst, MemOperand(sp));
#else
    lwz(dst, MemOperand(sp));
#endif
    addi(sp, sp, Operand(kSystemPointerSize));
  }

  void pop() { addi(sp, sp, Operand(kSystemPointerSize)); }

  // Jump unconditionally to given label.
  void jmp(Label* L) { b(L); }

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

  // Class for scoping disabling constant pool entry merging
  class V8_NODISCARD BlockConstantPoolEntrySharingScope {
   public:
    explicit BlockConstantPoolEntrySharingScope(Assembler* assem)
        : assem_(assem) {
      assem_->StartBlockConstantPoolEntrySharing();
    }
    ~BlockConstantPoolEntrySharingScope() {
      assem_->EndBlockConstantPoolEntrySharing();
    }

   private:
    Assembler* assem_;

    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockConstantPoolEntrySharingScope);
  };

  // Record a deoptimization reason that can be used by a log or cpu profiler.
  // Use --trace-deopt to enable.
  void RecordDeoptReason(DeoptimizeReason reason, uint32_t node_id,
                         SourcePosition position, int id);

  // Writes a single byte or word of data in the code stream.  Used
  // for inline tables, e.g., jump-tables.
  void db(uint8_t data);
  void dd(uint32_t data);
  void dq(uint64_t data);
  void dp(uintptr_t data);

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

  static bool IsLis(Instr instr);
  static bool IsLi(Instr instr);
  static bool IsAddic(Instr instr);
  static bool IsOri(Instr instr);

  static bool IsBranch(Instr instr);
  static Register GetRA(Instr instr);
  static Register GetRB(Instr instr);
#if V8_TARGET_ARCH_PPC64
  static bool Is64BitLoadIntoR12(Instr instr1, Instr instr2, Instr instr3,
                                 Instr instr4, Instr instr5);
#else
  static bool Is32BitLoadIntoR12(Instr instr1, Instr instr2);
#endif

  static bool IsCmpRegister(Instr instr);
  static bool IsCmpImmediate(Instr instr);
  static bool IsRlwinm(Instr instr);
  static bool IsAndi(Instr instr);
#if V8_TARGET_ARCH_PPC64
  static bool IsRldicl(Instr instr);
#endif
  static bool IsCrSet(Instr instr);
  static Register GetCmpImmediateRegister(Instr instr);
  static int GetCmpImmediateRawImmediate(Instr instr);
  static bool IsNop(Instr instr, int type = NON_MARKING_NOP);

  // Postpone the generation of the trampoline pool for the specified number of
  // instructions.
  void BlockTrampolinePoolFor(int instructions);
  void CheckTrampolinePool();

  // For mov.  Return the number of actual instructions required to
  // load the operand into a register.  This can be anywhere from
  // one (constant pool small section) to five instructions (full
  // 64-bit sequence).
  //
  // The value returned is only valid as long as no entries are added to the
  // constant pool between this call and the actual instruction being emitted.
  int instructions_required_for_mov(Register dst, const Operand& src) const;

  // Decide between using the constant pool vs. a mov immediate sequence.
  bool use_constant_pool_for_mov(Register dst, const Operand& src,
                                 bool canOptimize) const;

  // The code currently calls CheckBuffer() too often. This has the side
  // effect of randomly growing the buffer in the middle of multi-instruction
  // sequences.
  //
  // This function allows outside callers to check and grow the buffer
  void EnsureSpaceFor(int space_needed);

  int EmitConstantPool() { return constant_pool_builder_.Emit(this); }

  bool ConstantPoolAccessIsInOverflow() const {
    return constant_pool_builder_.NextAccess(ConstantPoolEntry::INTPTR) ==
           ConstantPoolEntry::OVERFLOWED;
  }

  Label* ConstantPoolPosition() {
    return constant_pool_builder_.EmittedPosition();
  }

  void EmitRelocations();

 protected:
  int buffer_space() const { return reloc_info_writer.pos() - pc_; }

  // Decode instruction(s) at pos and return backchain to previous
  // label reference or kEndOfChain.
  int target_at(int pos);

  // Patch instruction(s) at pos to target target_pos (e.g. branch)
  void target_at_put(int pos, int target_pos, bool* is_branch = nullptr);

  // Record reloc info for current pc_
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);
  ConstantPoolEntry::Access ConstantPoolAddEntry(RelocInfo::Mode rmode,
                                                 intptr_t value) {
    bool sharing_ok =
        RelocInfo::IsNoInfo(rmode) ||
        (!options().record_reloc_info_for_serialization &&
         RelocInfo::IsShareableRelocMode(rmode) &&
         !is_constant_pool_entry_sharing_blocked() &&
         // TODO(johnyan): make the following rmode shareable
         !RelocInfo::IsWasmCall(rmode) && !RelocInfo::IsWasmStubCall(rmode));
    return constant_pool_builder_.AddEntry(pc_offset(), value, sharing_ok);
  }
  ConstantPoolEntry::Access ConstantPoolAddEntry(base::Double value) {
    return constant_pool_builder_.AddEntry(pc_offset(), value);
  }

  // Block the emission of the trampoline pool before pc_offset.
  void BlockTrampolinePoolBefore(int pc_offset) {
    if (no_trampoline_pool_before_ < pc_offset)
      no_trampoline_pool_before_ = pc_offset;
  }

  void StartBlockTrampolinePool() { trampoline_pool_blocked_nesting_++; }
  void EndBlockTrampolinePool() {
    int count = --trampoline_pool_blocked_nesting_;
    if (count == 0) CheckTrampolinePoolQuick();
  }
  bool is_trampoline_pool_blocked() const {
    return trampoline_pool_blocked_nesting_ > 0;
  }

  void StartBlockConstantPoolEntrySharing() {
    constant_pool_entry_sharing_blocked_nesting_++;
  }
  void EndBlockConstantPoolEntrySharing() {
    constant_pool_entry_sharing_blocked_nesting_--;
  }
  bool is_constant_pool_entry_sharing_blocked() const {
    return constant_pool_entry_sharing_blocked_nesting_ > 0;
  }

  bool has_exception() const { return internal_trampoline_exception_; }

  bool is_trampoline_emitted() const { return trampoline_emitted_; }

  // InstructionStream generation
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries.
  static constexpr int kGap = 32;
  static_assert(AssemblerBase::kMinimalBufferSize >= 2 * kGap);

  RelocInfoWriter reloc_info_writer;

 private:
  // Avoid overflows for displacements etc.
  static const int kMaximalBufferSize = 512 * MB;

  // Repeated checking whether the trampoline pool should be emitted is rather
  // expensive. By default we only check again once a number of instructions
  // has been generated.
  int next_trampoline_check_;  // pc offset of next buffer check.

  // Emission of the trampoline pool may be blocked in some code sequences.
  int trampoline_pool_blocked_nesting_;  // Block emission if this is not zero.
  int no_trampoline_pool_before_;  // Block emission before this pc offset.

  // Do not share constant pool entries.
  int constant_pool_entry_sharing_blocked_nesting_;

  // Relocation info generation
  // Each relocation is encoded as a variable size value
  static constexpr int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  std::vector<DeferredRelocInfo> relocations_;

  // Scratch registers available for use by the Assembler.
  RegList scratch_register_list_;

  // The bound position, before this we cannot do instruction elimination.
  int last_bound_pos_;
  // Optimizable cmpi information.
  int optimizable_cmpi_pos_;
  CRegister cmpi_cr_ = CRegister::no_reg();

  ConstantPoolBuilder constant_pool_builder_;

  void CheckBuffer() {
    if (buffer_space() <= kGap) {
      GrowBuffer();
    }
  }

  void GrowBuffer(int needed = 0);
  // Code emission
  void emit(Instr x) {
    CheckBuffer();
    *reinterpret_cast<Instr*>(pc_) = x;
    pc_ += kInstrSize;
    CheckTrampolinePoolQuick();
  }

  void emit_prefix(Instr x) {
    // Prefixed instructions cannot cross 64-byte boundaries. Add a nop if the
    // boundary will be crossed mid way.
    // Code is set to be 64-byte aligned on PPC64 after relocation (look for
    // kCodeAlignment). We use pc_offset() instead of pc_ as current pc_
    // alignment could be different after relocation.
    if (((pc_offset() + sizeof(Instr)) & 63) == 0) {
      nop();
    }
    // Do not emit trampoline pool in between prefix and suffix.
    CHECK(is_trampoline_pool_blocked());
    emit(x);
  }

  void TrackBranch() {
    DCHECK(!trampoline_emitted_);
    int count = tracked_branch_count_++;
    if (count == 0) {
      // We leave space (kMaxBlockTrampolineSectionSize)
      // for BlockTrampolinePoolScope buffer.
      next_trampoline_check_ =
          pc_offset() + kMaxCondBranchReach - kMaxBlockTrampolineSectionSize;
    } else {
      next_trampoline_check_ -= kTrampolineSlotsSize;
    }
  }

  inline void UntrackBranch();
  // Instruction generation
  void a_form(Instr instr, DoubleRegister frt, DoubleRegister fra,
              DoubleRegister frb, RCBit r);
  void d_form(Instr instr, Register rt, Register ra, const intptr_t val,
              bool signed_disp);
  void xo_form(Instr instr, Register rt, Register ra, Register rb, OEBit o,
               RCBit r);
  void md_form(Instr instr, Register ra, Register rs, int shift, int maskbit,
               RCBit r);
  void mds_form(Instr instr, Register ra, Register rs, Register rb, int maskbit,
                RCBit r);

  // Labels
  void print(Label* L);
  int max_reach_from(int pos);
  void bind_to(Label* L, int pos);
  void next(Label* L);

  class Trampoline {
   public:
    Trampoline() {
      next_slot_ = 0;
      free_slot_count_ = 0;
    }
    Trampoline(int start, int slot_count) {
      next_slot_ = start;
      free_slot_count_ = slot_count;
    }
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
    int next_slot_;
    int free_slot_count_;
  };

  int32_t get_trampoline_entry();
  int tracked_branch_count_;
  // If trampoline is emitted, generated code is becoming large. As
  // this is already a slow case which can possibly break our code
  // generation for the extreme case, we use this information to
  // trigger different mode of branch instruction generation, where we
  // no longer use a single branch instruction.
  bool trampoline_emitted_;
  static constexpr int kTrampolineSlotsSize = kInstrSize;
  static constexpr int kMaxCondBranchReach = (1 << (16 - 1)) - 1;
  static constexpr int kMaxBlockTrampolineSectionSize = 64 * kInstrSize;
  static constexpr int kInvalidSlotPos = -1;

  Trampoline trampoline_;
  bool internal_trampoline_exception_;

  void AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate);

  int WriteCodeComments();

  friend class RegExpMacroAssemblerPPC;
  friend class RelocInfo;
  friend class BlockTrampolinePoolScope;
  friend class EnsureSpace;
  friend class UseScratchRegisterScope;
};

class EnsureSpace {
 public:
  explicit EnsureSpace(Assembler* assembler) { assembler->CheckBuffer(); }
};

class PatchingAssembler : public Assembler {
 public:
  PatchingAssembler(const AssemblerOptions& options, uint8_t* address,
                    int instructions);
  ~PatchingAssembler();
};

class V8_EXPORT_PRIVATE V8_NODISCARD UseScratchRegisterScope {
 public:
  explicit UseScratchRegisterScope(Assembler* assembler)
      : assembler_(assembler),
        old_available_(*assembler->GetScratchRegisterList()) {}

  ~UseScratchRegisterScope() {
    *assembler_->GetScratchRegisterList() = old_available_;
  }

  Register Acquire() {
    return assembler_->GetScratchRegisterList()->PopFirst();
  }

  // Check if we have registers available to acquire.
  bool CanAcquire() const {
    return !assembler_->GetScratchRegisterList()->is_empty();
  }

 private:
  friend class Assembler;
  friend class MacroAssembler;

  Assembler* assembler_;
  RegList old_available_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_PPC_ASSEMBLER_PPC_H_
