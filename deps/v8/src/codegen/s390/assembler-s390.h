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

// A light-weight S390 Assembler
// Generates user mode instructions for z/Architecture

#ifndef V8_CODEGEN_S390_ASSEMBLER_S390_H_
#define V8_CODEGEN_S390_ASSEMBLER_S390_H_
#include <stdio.h>
#include <memory>
#if V8_HOST_ARCH_S390 && !V8_OS_ZOS
// elf.h include is required for auxv check for STFLE facility used
// for hardware detection, which is sensible only on s390 hosts.
#include <elf.h>
#endif

#include <fcntl.h>
#include <unistd.h>

#include "src/base/platform/platform.h"
#include "src/codegen/assembler.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/label.h"
#include "src/codegen/s390/constants-s390.h"
#include "src/codegen/s390/register-s390.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

class SafepointTableBuilder;

// -----------------------------------------------------------------------------
// Machine instruction Operands

// Class Operand represents a shifter operand in data processing instructions
// defining immediate numbers and masks
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

  static Operand EmbeddedNumber(double value);  // Smi or HeapNumber

  // Return true if this is a register operand.
  V8_INLINE bool is_reg() const { return rm_.is_valid(); }

  bool must_output_reloc_info(const Assembler* assembler) const;

  inline intptr_t immediate() const {
    DCHECK(!rm_.is_valid());
    DCHECK(!is_heap_number_request());
    return value_.immediate;
  }

  HeapNumberRequest heap_number_request() const {
    DCHECK(is_heap_number_request());
    return value_.heap_number_request;
  }

  inline void setBits(int n) {
    value_.immediate =
        (static_cast<uint32_t>(value_.immediate) << (32 - n)) >> (32 - n);
  }

  Register rm() const { return rm_; }

  bool is_heap_number_request() const {
    DCHECK_IMPLIES(is_heap_number_request_, !rm_.is_valid());
    DCHECK_IMPLIES(is_heap_number_request_,
                   rmode_ == RelocInfo::FULL_EMBEDDED_OBJECT ||
                       rmode_ == RelocInfo::CODE_TARGET);
    return is_heap_number_request_;
  }

  RelocInfo::Mode rmode() const { return rmode_; }

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

using Disp = int32_t;

// Class MemOperand represents a memory operand in load and store instructions
// On S390, we have various flavours of memory operands:
//   1) a base register + 16 bit unsigned displacement
//   2) a base register + index register + 16 bit unsigned displacement
//   3) a base register + index register + 20 bit signed displacement
class V8_EXPORT_PRIVATE MemOperand {
 public:
  explicit MemOperand(Register rx, Disp offset = 0);
  explicit MemOperand(Register rx, Register rb, Disp offset = 0);

  int32_t offset() const { return offset_; }
  uint32_t getDisplacement() const { return offset(); }

  // Base register
  Register rb() const {
    DCHECK(baseRegister != no_reg);
    return baseRegister;
  }

  Register getBaseRegister() const { return rb(); }

  // Index Register
  Register rx() const {
    DCHECK(indexRegister != no_reg);
    return indexRegister;
  }
  Register getIndexRegister() const { return rx(); }

 private:
  Register baseRegister;   // base
  Register indexRegister;  // index
  int32_t offset_;         // offset

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

  static RegList DefaultTmpList();
  static DoubleRegList DefaultFPTmpList();

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

  // Returns the branch offset to the given label from the current code position
  // Links the label to the current position if it is still unbound
  int branch_offset(Label* L) { return link(L) - pc_offset(); }

  void load_label_offset(Register r1, Label* L);

  // Read/Modify the code target address in the branch/call instruction at pc.
  // The isolate argument is unused (and may be nullptr) when skipping flushing.
  V8_INLINE static Address target_address_at(Address pc, Address constant_pool);

  // Read/Modify the code target address in the branch/call instruction at pc.
  inline static Tagged_t target_compressed_address_at(Address pc,
                                                      Address constant_pool);
  V8_INLINE static void set_target_address_at(
      Address pc, Address constant_pool, Address target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  inline static void set_target_compressed_address_at(
      Address pc, Address constant_pool, Tagged_t target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  inline Handle<Object> code_target_object_handle_at(Address pc);
  inline Handle<HeapObject> compressed_embedded_object_handle_at(
      Address pc, Address constant_pool);
  // This sets the branch destination.
  // This is for calls and branches within generated code.
  inline static void deserialization_set_special_target_at(
      Address instruction_payload, Tagged<Code> code, Address target);

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

  // Here we are patching the address in the IIHF/IILF instruction pair.
  // These values are used in the serialization process and must be zero for
  // S390 platform, as Code, Embedded Object or External-reference pointers
  // are split across two consecutive instructions and don't exist separately
  // in the code, so the serializer should not step forwards in memory after
  // a target is resolved and written.
  static constexpr int kSpecialTargetSize = 0;
// Number of bytes for instructions used to store pointer sized constant.
#if V8_TARGET_ARCH_S390X
  static constexpr int kBytesForPtrConstant = 12;  // IIHF + IILF
#else
  static constexpr int kBytesForPtrConstant = 6;  // IILF
#endif

  RegList* GetScratchRegisterList() { return &scratch_register_list_; }
  DoubleRegList* GetScratchDoubleRegisterList() {
    return &scratch_double_register_list_;
  }

  // ---------------------------------------------------------------------------
  // InstructionStream generation

  template <class T, int size, int lo, int hi>
  inline T getfield(T value) {
    DCHECK(lo < hi);
    DCHECK_GT(size, 0);
    int mask = hi - lo;
    int shift = size * 8 - hi;
    uint32_t mask_value = (mask == 32) ? 0xffffffff : (1 << mask) - 1;
    return (value & mask_value) << shift;
  }

#define DECLARE_S390_RIL_AB_INSTRUCTIONS(name, op_name, op_value) \
  template <class R1>                                             \
  inline void name(R1 r1, const Operand& i2) {                    \
    ril_format(op_name, r1.code(), i2.immediate());               \
  }
#define DECLARE_S390_RIL_C_INSTRUCTIONS(name, op_name, op_value) \
  inline void name(Condition m1, const Operand& i2) {            \
    ril_format(op_name, m1, i2.immediate());                     \
  }

  inline void ril_format(Opcode opcode, int f1, int f2) {
    uint32_t op1 = opcode >> 4;
    uint32_t op2 = opcode & 0xf;
    emit6bytes(
        getfield<uint64_t, 6, 0, 8>(op1) | getfield<uint64_t, 6, 8, 12>(f1) |
        getfield<uint64_t, 6, 12, 16>(op2) | getfield<uint64_t, 6, 16, 48>(f2));
  }
  S390_RIL_A_OPCODE_LIST(DECLARE_S390_RIL_AB_INSTRUCTIONS)
  S390_RIL_B_OPCODE_LIST(DECLARE_S390_RIL_AB_INSTRUCTIONS)
  S390_RIL_C_OPCODE_LIST(DECLARE_S390_RIL_C_INSTRUCTIONS)
#undef DECLARE_S390_RIL_AB_INSTRUCTIONS
#undef DECLARE_S390_RIL_C_INSTRUCTIONS

#define DECLARE_S390_RR_INSTRUCTIONS(name, op_name, op_value) \
  inline void name(Register r1, Register r2) {                \
    rr_format(op_name, r1.code(), r2.code());                 \
  }                                                           \
  inline void name(DoubleRegister r1, DoubleRegister r2) {    \
    rr_format(op_name, r1.code(), r2.code());                 \
  }                                                           \
  inline void name(Condition m1, Register r2) {               \
    rr_format(op_name, m1, r2.code());                        \
  }

  inline void rr_format(Opcode opcode, int f1, int f2) {
    emit2bytes(getfield<uint16_t, 2, 0, 8>(opcode) |
               getfield<uint16_t, 2, 8, 12>(f1) |
               getfield<uint16_t, 2, 12, 16>(f2));
  }
  S390_RR_OPCODE_LIST(DECLARE_S390_RR_INSTRUCTIONS)
#undef DECLARE_S390_RR_INSTRUCTIONS

#define DECLARE_S390_RRD_INSTRUCTIONS(name, op_name, op_value) \
  template <class R1, class R2, class R3>                      \
  inline void name(R1 r1, R3 r3, R2 r2) {                      \
    rrd_format(op_name, r1.code(), r3.code(), r2.code());      \
  }
  inline void rrd_format(Opcode opcode, int f1, int f2, int f3) {
    emit4bytes(getfield<uint32_t, 4, 0, 16>(opcode) |
               getfield<uint32_t, 4, 16, 20>(f1) |
               getfield<uint32_t, 4, 24, 28>(f2) |
               getfield<uint32_t, 4, 28, 32>(f3));
  }
  S390_RRD_OPCODE_LIST(DECLARE_S390_RRD_INSTRUCTIONS)
#undef DECLARE_S390_RRD_INSTRUCTIONS

#define DECLARE_S390_RRE_INSTRUCTIONS(name, op_name, op_value) \
  template <class R1, class R2>                                \
  inline void name(R1 r1, R2 r2) {                             \
    rre_format(op_name, r1.code(), r2.code());                 \
  }
  inline void rre_format(Opcode opcode, int f1, int f2) {
    emit4bytes(getfield<uint32_t, 4, 0, 16>(opcode) |
               getfield<uint32_t, 4, 24, 28>(f1) |
               getfield<uint32_t, 4, 28, 32>(f2));
  }
  S390_RRE_OPCODE_LIST(DECLARE_S390_RRE_INSTRUCTIONS)
  // Special format
  void lzdr(DoubleRegister r1) { rre_format(LZDR, r1.code(), 0); }
  void lzer(DoubleRegister r1) { rre_format(LZER, r1.code(), 0); }
#undef DECLARE_S390_RRE_INSTRUCTIONS

#define DECLARE_S390_RX_INSTRUCTIONS(name, op_name, op_value)            \
  template <class R1>                                                    \
  inline void name(R1 r1, Register x2, Register b2, const Operand& d2) { \
    rx_format(op_name, r1.code(), x2.code(), b2.code(), d2.immediate()); \
  }                                                                      \
  template <class R1>                                                    \
  inline void name(R1 r1, const MemOperand& opnd) {                      \
    name(r1, opnd.getIndexRegister(), opnd.getBaseRegister(),            \
         Operand(opnd.getDisplacement()));                               \
  }

  inline void rx_format(Opcode opcode, int f1, int f2, int f3, int f4) {
    DCHECK(is_uint8(opcode));
    DCHECK(is_uint12(f4));
    emit4bytes(
        getfield<uint32_t, 4, 0, 8>(opcode) | getfield<uint32_t, 4, 8, 12>(f1) |
        getfield<uint32_t, 4, 12, 16>(f2) | getfield<uint32_t, 4, 16, 20>(f3) |
        getfield<uint32_t, 4, 20, 32>(f4));
  }
  S390_RX_A_OPCODE_LIST(DECLARE_S390_RX_INSTRUCTIONS)

  void bc(Condition cond, const MemOperand& opnd) {
    bc(cond, opnd.getIndexRegister(), opnd.getBaseRegister(),
       Operand(opnd.getDisplacement()));
  }
  void bc(Condition cond, Register x2, Register b2, const Operand& d2) {
    rx_format(BC, cond, x2.code(), b2.code(), d2.immediate());
  }
#undef DECLARE_S390_RX_INSTRUCTIONS

#define DECLARE_S390_RXY_INSTRUCTIONS(name, op_name, op_value)            \
  template <class R1, class R2>                                           \
  inline void name(R1 r1, R2 r2, Register b2, const Operand& d2) {        \
    rxy_format(op_name, r1.code(), r2.code(), b2.code(), d2.immediate()); \
  }                                                                       \
  template <class R1>                                                     \
  inline void name(R1 r1, const MemOperand& opnd) {                       \
    name(r1, opnd.getIndexRegister(), opnd.getBaseRegister(),             \
         Operand(opnd.getDisplacement()));                                \
  }

  inline void rxy_format(Opcode opcode, int f1, int f2, int f3, int f4) {
    DCHECK(is_uint16(opcode));
    DCHECK(is_int20(f4));
    emit6bytes(getfield<uint64_t, 6, 0, 8>(opcode >> 8) |
               getfield<uint64_t, 6, 8, 12>(f1) |
               getfield<uint64_t, 6, 12, 16>(f2) |
               getfield<uint64_t, 6, 16, 20>(f3) |
               getfield<uint64_t, 6, 20, 32>(f4 & 0x0fff) |
               getfield<uint64_t, 6, 32, 40>(f4 >> 12) |
               getfield<uint64_t, 6, 40, 48>(opcode & 0x00ff));
  }
  S390_RXY_A_OPCODE_LIST(DECLARE_S390_RXY_INSTRUCTIONS)

  void pfd(Condition cond, const MemOperand& opnd) {
    pfd(cond, opnd.getIndexRegister(), opnd.getBaseRegister(),
        Operand(opnd.getDisplacement()));
  }
  void pfd(Condition cond, Register x2, Register b2, const Operand& d2) {
    rxy_format(PFD, cond, x2.code(), b2.code(), d2.immediate());
  }
#undef DECLARE_S390_RXY_INSTRUCTIONS

  inline void rsy_format(Opcode op, int f1, int f2, int f3, int f4) {
    DCHECK(is_int20(f4));
    DCHECK(is_uint16(op));
    uint64_t code =
        (getfield<uint64_t, 6, 0, 8>(op >> 8) |
         getfield<uint64_t, 6, 8, 12>(f1) | getfield<uint64_t, 6, 12, 16>(f2) |
         getfield<uint64_t, 6, 16, 20>(f3) |
         getfield<uint64_t, 6, 20, 32>(f4 & 0x0fff) |
         getfield<uint64_t, 6, 32, 40>(f4 >> 12) |
         getfield<uint64_t, 6, 40, 48>(op & 0xff));
    emit6bytes(code);
  }

#define DECLARE_S390_RSY_A_INSTRUCTIONS(name, op_name, op_value)            \
  void name(Register r1, Register r3, Register b2,                          \
            const Operand& d2 = Operand::Zero()) {                          \
    rsy_format(op_name, r1.code(), r3.code(), b2.code(), d2.immediate());   \
  }                                                                         \
  void name(Register r1, Register r3, Operand d2) { name(r1, r3, r0, d2); } \
  void name(Register r1, Register r3, const MemOperand& opnd) {             \
    name(r1, r3, opnd.getBaseRegister(), Operand(opnd.getDisplacement()));  \
  }
  S390_RSY_A_OPCODE_LIST(DECLARE_S390_RSY_A_INSTRUCTIONS)
#undef DECLARE_S390_RSY_A_INSTRUCTIONS

#define DECLARE_S390_RSY_B_INSTRUCTIONS(name, op_name, op_value)           \
  void name(Register r1, Condition m3, Register b2, const Operand& d2) {   \
    rsy_format(op_name, r1.code(), m3, b2.code(), d2.immediate());         \
  }                                                                        \
  void name(Register r1, Condition m3, const MemOperand& opnd) {           \
    name(r1, m3, opnd.getBaseRegister(), Operand(opnd.getDisplacement())); \
  }
  S390_RSY_B_OPCODE_LIST(DECLARE_S390_RSY_B_INSTRUCTIONS)
#undef DECLARE_S390_RSY_B_INSTRUCTIONS

  inline void rs_format(Opcode op, int f1, int f2, int f3, const int f4) {
    uint32_t code =
        getfield<uint32_t, 4, 0, 8>(op) | getfield<uint32_t, 4, 8, 12>(f1) |
        getfield<uint32_t, 4, 12, 16>(f2) | getfield<uint32_t, 4, 16, 20>(f3) |
        getfield<uint32_t, 4, 20, 32>(f4);
    emit4bytes(code);
  }

#define DECLARE_S390_RS_A_INSTRUCTIONS(name, op_name, op_value)            \
  void name(Register r1, Register r3, Register b2, const Operand& d2) {    \
    rs_format(op_name, r1.code(), r3.code(), b2.code(), d2.immediate());   \
  }                                                                        \
  void name(Register r1, Register r3, const MemOperand& opnd) {            \
    name(r1, r3, opnd.getBaseRegister(), Operand(opnd.getDisplacement())); \
  }
  S390_RS_A_OPCODE_LIST(DECLARE_S390_RS_A_INSTRUCTIONS)
#undef DECLARE_S390_RS_A_INSTRUCTIONS

#define DECLARE_S390_RS_B_INSTRUCTIONS(name, op_name, op_value)            \
  void name(Register r1, Condition m3, Register b2, const Operand& d2) {   \
    rs_format(op_name, r1.code(), m3, b2.code(), d2.immediate());          \
  }                                                                        \
  void name(Register r1, Condition m3, const MemOperand& opnd) {           \
    name(r1, m3, opnd.getBaseRegister(), Operand(opnd.getDisplacement())); \
  }
  S390_RS_B_OPCODE_LIST(DECLARE_S390_RS_B_INSTRUCTIONS)
#undef DECLARE_S390_RS_B_INSTRUCTIONS

#define DECLARE_S390_RS_SHIFT_FORMAT(name, opcode)                             \
  void name(Register r1, Register r2, const Operand& opnd = Operand::Zero()) { \
    rs_format(opcode, r1.code(), r0.code(), r2.code(), opnd.immediate());      \
  }                                                                            \
  void name(Register r1, const Operand& opnd) {                                \
    rs_format(opcode, r1.code(), r0.code(), r0.code(), opnd.immediate());      \
  }
  DECLARE_S390_RS_SHIFT_FORMAT(sll, SLL)
  DECLARE_S390_RS_SHIFT_FORMAT(srl, SRL)
  DECLARE_S390_RS_SHIFT_FORMAT(sla, SLA)
  DECLARE_S390_RS_SHIFT_FORMAT(sra, SRA)
  DECLARE_S390_RS_SHIFT_FORMAT(sldl, SLDL)
  DECLARE_S390_RS_SHIFT_FORMAT(srda, SRDA)
  DECLARE_S390_RS_SHIFT_FORMAT(srdl, SRDL)
#undef DECLARE_S390_RS_SHIFT_FORMAT

  inline void rxe_format(Opcode op, int f1, int f2, int f3, int f4,
                         int f5 = 0) {
    DCHECK(is_uint12(f4));
    DCHECK(is_uint16(op));
    uint64_t code =
        (getfield<uint64_t, 6, 0, 8>(op >> 8) |
         getfield<uint64_t, 6, 8, 12>(f1) | getfield<uint64_t, 6, 12, 16>(f2) |
         getfield<uint64_t, 6, 16, 20>(f3) |
         getfield<uint64_t, 6, 20, 32>(f4 & 0x0fff) |
         getfield<uint64_t, 6, 32, 36>(f5) |
         getfield<uint64_t, 6, 40, 48>(op & 0xff));
    emit6bytes(code);
  }

#define DECLARE_S390_RXE_INSTRUCTIONS(name, op_name, op_value)                \
  void name(Register r1, Register x2, Register b2, const Operand& d2,         \
            Condition m3 = static_cast<Condition>(0)) {                       \
    rxe_format(op_name, r1.code(), x2.code(), b2.code(), d2.immediate(), m3); \
  }                                                                           \
  template <class _R1Type>                                                    \
  void name(_R1Type r1, const MemOperand& opnd) {                             \
    name(Register::from_code(r1.code()), opnd.rx(), opnd.rb(),                \
         Operand(opnd.offset()));                                             \
  }
  S390_RXE_OPCODE_LIST(DECLARE_S390_RXE_INSTRUCTIONS)
#undef DECLARE_S390_RXE_INSTRUCTIONS

  inline void ri_format(Opcode opcode, int f1, int f2) {
    uint32_t op1 = opcode >> 4;
    uint32_t op2 = opcode & 0xf;
    emit4bytes(
        getfield<uint32_t, 4, 0, 8>(op1) | getfield<uint32_t, 4, 8, 12>(f1) |
        getfield<uint32_t, 4, 12, 16>(op2) | getfield<uint32_t, 4, 16, 32>(f2));
  }

#define DECLARE_S390_RI_A_INSTRUCTIONS(name, op_name, op_value)    \
  void name(Register r, const Operand& i2) {                       \
    DCHECK(is_uint12(op_name));                                    \
    DCHECK(is_uint16(i2.immediate()) || is_int16(i2.immediate())); \
    ri_format(op_name, r.code(), i2.immediate());                  \
  }
  S390_RI_A_OPCODE_LIST(DECLARE_S390_RI_A_INSTRUCTIONS)
#undef DECLARE_S390_RI_A_INSTRUCTIONS

#define DECLARE_S390_RI_B_INSTRUCTIONS(name, op_name, op_value)       \
  void name(Register r1, const Operand& imm) {                        \
    /* 2nd argument encodes # of halfwords, so divide by 2. */        \
    int16_t numHalfwords = static_cast<int16_t>(imm.immediate()) / 2; \
    Operand halfwordOp = Operand(numHalfwords);                       \
    halfwordOp.setBits(16);                                           \
    ri_format(op_name, r1.code(), halfwordOp.immediate());            \
  }
  S390_RI_B_OPCODE_LIST(DECLARE_S390_RI_B_INSTRUCTIONS)
#undef DECLARE_S390_RI_B_INSTRUCTIONS

#define DECLARE_S390_RI_C_INSTRUCTIONS(name, op_name, op_value) \
  void name(Condition m, const Operand& i2) {                   \
    DCHECK(is_uint12(op_name));                                 \
    DCHECK(is_uint4(m));                                        \
    DCHECK(op_name == BRC ? is_int16(i2.immediate())            \
                          : is_uint16(i2.immediate()));         \
    ri_format(op_name, m, i2.immediate());                      \
  }
  S390_RI_C_OPCODE_LIST(DECLARE_S390_RI_C_INSTRUCTIONS)
#undef DECLARE_S390_RI_C_INSTRUCTIONS

  inline void rrf_format(Opcode op, int f1, int f2, int f3, int f4) {
    uint32_t code =
        getfield<uint32_t, 4, 0, 16>(op) | getfield<uint32_t, 4, 16, 20>(f1) |
        getfield<uint32_t, 4, 20, 24>(f2) | getfield<uint32_t, 4, 24, 28>(f3) |
        getfield<uint32_t, 4, 28, 32>(f4);
    emit4bytes(code);
  }

#define DECLARE_S390_RRF_A_INSTRUCTIONS(name, op_name, op_value)   \
  void name(Register r1, Condition m4, Register r2, Register r3) { \
    rrf_format(op_name, r3.code(), m4, r1.code(), r2.code());      \
  }                                                                \
  void name(Register r1, Register r2, Register r3) {               \
    name(r1, Condition(0), r2, r3);                                \
  }
  S390_RRF_A_OPCODE_LIST(DECLARE_S390_RRF_A_INSTRUCTIONS)
#undef DECLARE_S390_RRF_A_INSTRUCTIONS

#define DECLARE_S390_RRF_B_INSTRUCTIONS(name, op_name, op_value)   \
  void name(Register r1, Condition m4, Register r2, Register r3) { \
    rrf_format(op_name, r3.code(), m4, r1.code(), r2.code());      \
  }                                                                \
  void name(Register r1, Register r2, Register r3) {               \
    name(r1, Condition(0), r2, r3);                                \
  }
  S390_RRF_B_OPCODE_LIST(DECLARE_S390_RRF_B_INSTRUCTIONS)
#undef DECLARE_S390_RRF_B_INSTRUCTIONS

#define DECLARE_S390_RRF_C_INSTRUCTIONS(name, op_name, op_value) \
  template <class R1, class R2>                                  \
  void name(Condition m3, Condition m4, R1 r1, R2 r2) {          \
    rrf_format(op_name, m3, m4, r1.code(), r2.code());           \
  }                                                              \
  template <class R1, class R2>                                  \
  void name(Condition m3, R1 r1, R2 r2) {                        \
    name(m3, Condition(0), r1, r2);                              \
  }
  S390_RRF_C_OPCODE_LIST(DECLARE_S390_RRF_C_INSTRUCTIONS)
#undef DECLARE_S390_RRF_C_INSTRUCTIONS

#define DECLARE_S390_RRF_D_INSTRUCTIONS(name, op_name, op_value) \
  template <class R1, class R2>                                  \
  void name(Condition m3, Condition m4, R1 r1, R2 r2) {          \
    rrf_format(op_name, m3, m4, r1.code(), r2.code());           \
  }                                                              \
  template <class R1, class R2>                                  \
  void name(Condition m3, R1 r1, R2 r2) {                        \
    name(m3, Condition(0), r1, r2);                              \
  }
  S390_RRF_D_OPCODE_LIST(DECLARE_S390_RRF_D_INSTRUCTIONS)
#undef DECLARE_S390_RRF_D_INSTRUCTIONS

#define DECLARE_S390_RRF_E_INSTRUCTIONS(name, op_name, op_value) \
  template <class M3, class M4, class R1, class R2>              \
  void name(M3 m3, M4 m4, R1 r1, R2 r2) {                        \
    rrf_format(op_name, m3, m4, r1.code(), r2.code());           \
  }                                                              \
  template <class M3, class R1, class R2>                        \
  void name(M3 m3, R1 r1, R2 r2) {                               \
    name(m3, Condition(0), r1, r2);                              \
  }
  S390_RRF_E_OPCODE_LIST(DECLARE_S390_RRF_E_INSTRUCTIONS)
#undef DECLARE_S390_RRF_E_INSTRUCTIONS

  inline void rsi_format(Opcode op, int f1, int f2, int f3) {
    DCHECK(is_uint8(op));
    DCHECK(is_uint16(f3) || is_int16(f3));
    uint32_t code =
        getfield<uint32_t, 4, 0, 8>(op) | getfield<uint32_t, 4, 8, 12>(f1) |
        getfield<uint32_t, 4, 12, 16>(f2) | getfield<uint32_t, 4, 16, 32>(f3);
    emit4bytes(code);
  }

#define DECLARE_S390_RSI_INSTRUCTIONS(name, op_name, op_value) \
  void name(Register r1, Register r3, const Operand& i2) {     \
    rsi_format(op_name, r1.code(), r3.code(), i2.immediate()); \
  }
  S390_RSI_OPCODE_LIST(DECLARE_S390_RSI_INSTRUCTIONS)
#undef DECLARE_S390_RSI_INSTRUCTIONS

  inline void rsl_format(Opcode op, uint16_t f1, int f2, int f3, int f4,
                         int f5) {
    DCHECK(is_uint16(op));
    uint64_t code =
        getfield<uint64_t, 6, 0, 8>(op >> 8) |
        getfield<uint64_t, 6, 8, 16>(f1) | getfield<uint64_t, 6, 16, 20>(f2) |
        getfield<uint64_t, 6, 20, 32>(f3) | getfield<uint64_t, 6, 32, 36>(f4) |
        getfield<uint64_t, 6, 36, 40>(f5) |
        getfield<uint64_t, 6, 40, 48>(op & 0x00FF);
    emit6bytes(code);
  }

#define DECLARE_S390_RSL_A_INSTRUCTIONS(name, op_name, op_value) \
  void name(const Operand& l1, Register b1, const Operand& d1) { \
    uint16_t L = static_cast<uint16_t>(l1.immediate() << 8);     \
    rsl_format(op_name, L, b1.code(), d1.immediate(), 0, 0);     \
  }
  S390_RSL_A_OPCODE_LIST(DECLARE_S390_RSL_A_INSTRUCTIONS)
#undef DECLARE_S390_RSL_A_INSTRUCTIONS

#define DECLARE_S390_RSL_B_INSTRUCTIONS(name, op_name, op_value)            \
  void name(const Operand& l2, Register b2, const Operand& d2, Register r1, \
            Condition m3) {                                                 \
    uint16_t L = static_cast<uint16_t>(l2.immediate());                     \
    rsl_format(op_name, L, b2.code(), d2.immediate(), r1.code(), m3);       \
  }
  S390_RSL_B_OPCODE_LIST(DECLARE_S390_RSL_B_INSTRUCTIONS)
#undef DECLARE_S390_RSL_B_INSTRUCTIONS

  inline void s_format(Opcode op, int f1, int f2) {
    DCHECK_NE(op & 0xff00, 0);
    DCHECK(is_uint12(f2));
    uint32_t code = getfield<uint32_t, 4, 0, 16>(op) |
                    getfield<uint32_t, 4, 16, 20>(f1) |
                    getfield<uint32_t, 4, 20, 32>(f2);
    emit4bytes(code);
  }

#define DECLARE_S390_S_INSTRUCTIONS(name, op_name, op_value) \
  void name(Register b1, const Operand& d2) {                \
    Opcode op = op_name;                                     \
    if ((op & 0xFF00) == 0) {                                \
      op = (Opcode)(op << 8);                                \
    }                                                        \
    s_format(op, b1.code(), d2.immediate());                 \
  }                                                          \
  void name(const MemOperand& opnd) {                        \
    Operand d2 = Operand(opnd.getDisplacement());            \
    name(opnd.getBaseRegister(), d2);                        \
  }
  S390_S_OPCODE_LIST(DECLARE_S390_S_INSTRUCTIONS)
#undef DECLARE_S390_S_INSTRUCTIONS

  inline void si_format(Opcode op, int f1, int f2, int f3) {
    uint32_t code =
        getfield<uint32_t, 4, 0, 8>(op) | getfield<uint32_t, 4, 8, 16>(f1) |
        getfield<uint32_t, 4, 16, 20>(f2) | getfield<uint32_t, 4, 20, 32>(f3);
    emit4bytes(code);
  }

#define DECLARE_S390_SI_INSTRUCTIONS(name, op_name, op_value)          \
  void name(const Operand& i2, Register b1, const Operand& d1) {       \
    si_format(op_name, i2.immediate(), b1.code(), d1.immediate());     \
  }                                                                    \
  void name(const MemOperand& opnd, const Operand& i2) {               \
    name(i2, opnd.getBaseRegister(), Operand(opnd.getDisplacement())); \
  }
  S390_SI_OPCODE_LIST(DECLARE_S390_SI_INSTRUCTIONS)
#undef DECLARE_S390_SI_INSTRUCTIONS

  inline void siy_format(Opcode op, int f1, int f2, int f3) {
    DCHECK(is_uint20(f3) || is_int20(f3));
    DCHECK(is_uint16(op));
    DCHECK(is_uint8(f1) || is_int8(f1));
    uint64_t code = getfield<uint64_t, 6, 0, 8>(op >> 8) |
                    getfield<uint64_t, 6, 8, 16>(f1) |
                    getfield<uint64_t, 6, 16, 20>(f2) |
                    getfield<uint64_t, 6, 20, 32>(f3) |
                    getfield<uint64_t, 6, 32, 40>(f3 >> 12) |
                    getfield<uint64_t, 6, 40, 48>(op & 0x00FF);
    emit6bytes(code);
  }

#define DECLARE_S390_SIY_INSTRUCTIONS(name, op_name, op_value)         \
  void name(const Operand& i2, Register b1, const Operand& d1) {       \
    siy_format(op_name, i2.immediate(), b1.code(), d1.immediate());    \
  }                                                                    \
  void name(const MemOperand& opnd, const Operand& i2) {               \
    name(i2, opnd.getBaseRegister(), Operand(opnd.getDisplacement())); \
  }
  S390_SIY_OPCODE_LIST(DECLARE_S390_SIY_INSTRUCTIONS)
#undef DECLARE_S390_SIY_INSTRUCTIONS

  inline void rrs_format(Opcode op, int f1, int f2, int f3, int f4, int f5) {
    DCHECK(is_uint12(f4));
    DCHECK(is_uint16(op));
    uint64_t code =
        getfield<uint64_t, 6, 0, 8>(op >> 8) |
        getfield<uint64_t, 6, 8, 12>(f1) | getfield<uint64_t, 6, 12, 16>(f2) |
        getfield<uint64_t, 6, 16, 20>(f3) | getfield<uint64_t, 6, 20, 32>(f4) |
        getfield<uint64_t, 6, 32, 36>(f5) |
        getfield<uint64_t, 6, 40, 48>(op & 0x00FF);
    emit6bytes(code);
  }

#define DECLARE_S390_RRS_INSTRUCTIONS(name, op_name, op_value)                 \
  void name(Register r1, Register r2, Register b4, const Operand& d4,          \
            Condition m3) {                                                    \
    rrs_format(op_name, r1.code(), r2.code(), b4.code(), d4.immediate(), m3);  \
  }                                                                            \
  void name(Register r1, Register r2, Condition m3, const MemOperand& opnd) {  \
    name(r1, r2, opnd.getBaseRegister(), Operand(opnd.getDisplacement()), m3); \
  }
  S390_RRS_OPCODE_LIST(DECLARE_S390_RRS_INSTRUCTIONS)
#undef DECLARE_S390_RRS_INSTRUCTIONS

  inline void ris_format(Opcode op, int f1, int f2, int f3, int f4, int f5) {
    DCHECK(is_uint12(f3));
    DCHECK(is_uint16(op));
    DCHECK(is_uint8(f5));
    uint64_t code =
        getfield<uint64_t, 6, 0, 8>(op >> 8) |
        getfield<uint64_t, 6, 8, 12>(f1) | getfield<uint64_t, 6, 12, 16>(f2) |
        getfield<uint64_t, 6, 16, 20>(f3) | getfield<uint64_t, 6, 20, 32>(f4) |
        getfield<uint64_t, 6, 32, 40>(f5) |
        getfield<uint64_t, 6, 40, 48>(op & 0x00FF);
    emit6bytes(code);
  }

#define DECLARE_S390_RIS_INSTRUCTIONS(name, op_name, op_value)                 \
  void name(Register r1, Condition m3, Register b4, const Operand& d4,         \
            const Operand& i2) {                                               \
    ris_format(op_name, r1.code(), m3, b4.code(), d4.immediate(),              \
               i2.immediate());                                                \
  }                                                                            \
  void name(Register r1, const Operand& i2, Condition m3,                      \
            const MemOperand& opnd) {                                          \
    name(r1, m3, opnd.getBaseRegister(), Operand(opnd.getDisplacement()), i2); \
  }
  S390_RIS_OPCODE_LIST(DECLARE_S390_RIS_INSTRUCTIONS)
#undef DECLARE_S390_RIS_INSTRUCTIONS

  inline void sil_format(Opcode op, int f1, int f2, int f3) {
    DCHECK(is_uint12(f2));
    DCHECK(is_uint16(op));
    DCHECK(is_uint16(f3));
    uint64_t code =
        getfield<uint64_t, 6, 0, 16>(op) | getfield<uint64_t, 6, 16, 20>(f1) |
        getfield<uint64_t, 6, 20, 32>(f2) | getfield<uint64_t, 6, 32, 48>(f3);
    emit6bytes(code);
  }

#define DECLARE_S390_SIL_INSTRUCTIONS(name, op_name, op_value)         \
  void name(Register b1, const Operand& d1, const Operand& i2) {       \
    sil_format(op_name, b1.code(), d1.immediate(), i2.immediate());    \
  }                                                                    \
  void name(const MemOperand& opnd, const Operand& i2) {               \
    name(opnd.getBaseRegister(), Operand(opnd.getDisplacement()), i2); \
  }
  S390_SIL_OPCODE_LIST(DECLARE_S390_SIL_INSTRUCTIONS)
#undef DECLARE_S390_SIL_INSTRUCTIONS

  inline void rie_d_format(Opcode opcode, int f1, int f2, int f3, int f4) {
    uint32_t op1 = opcode >> 8;
    uint32_t op2 = opcode & 0xff;
    uint64_t code =
        getfield<uint64_t, 6, 0, 8>(op1) | getfield<uint64_t, 6, 8, 12>(f1) |
        getfield<uint64_t, 6, 12, 16>(f2) | getfield<uint64_t, 6, 16, 32>(f3) |
        getfield<uint64_t, 6, 32, 40>(f4) | getfield<uint64_t, 6, 40, 48>(op2);
    emit6bytes(code);
  }

#define DECLARE_S390_RIE_D_INSTRUCTIONS(name, op_name, op_value)    \
  void name(Register r1, Register r3, const Operand& i2) {          \
    rie_d_format(op_name, r1.code(), r3.code(), i2.immediate(), 0); \
  }
  S390_RIE_D_OPCODE_LIST(DECLARE_S390_RIE_D_INSTRUCTIONS)
#undef DECLARE_S390_RIE_D_INSTRUCTIONS

  inline void rie_e_format(Opcode opcode, int f1, int f2, int f3) {
    uint32_t op1 = opcode >> 8;
    uint32_t op2 = opcode & 0xff;
    uint64_t code =
        getfield<uint64_t, 6, 0, 8>(op1) | getfield<uint64_t, 6, 8, 12>(f1) |
        getfield<uint64_t, 6, 12, 16>(f2) | getfield<uint64_t, 6, 16, 32>(f3) |
        getfield<uint64_t, 6, 40, 48>(op2);
    emit6bytes(code);
  }

#define DECLARE_S390_RIE_E_INSTRUCTIONS(name, op_name, op_value) \
  void name(Register r1, Register r3, const Operand& i2) {       \
    rie_e_format(op_name, r1.code(), r3.code(), i2.immediate()); \
  }
  S390_RIE_E_OPCODE_LIST(DECLARE_S390_RIE_E_INSTRUCTIONS)
#undef DECLARE_S390_RIE_E_INSTRUCTIONS

  inline void rie_f_format(Opcode opcode, int f1, int f2, int f3, int f4,
                           int f5) {
    uint32_t op1 = opcode >> 8;
    uint32_t op2 = opcode & 0xff;
    uint64_t code =
        getfield<uint64_t, 6, 0, 8>(op1) | getfield<uint64_t, 6, 8, 12>(f1) |
        getfield<uint64_t, 6, 12, 16>(f2) | getfield<uint64_t, 6, 16, 24>(f3) |
        getfield<uint64_t, 6, 24, 32>(f4) | getfield<uint64_t, 6, 32, 40>(f5) |
        getfield<uint64_t, 6, 40, 48>(op2);
    emit6bytes(code);
  }

#define DECLARE_S390_RIE_F_INSTRUCTIONS(name, op_name, op_value)        \
  void name(Register dst, Register src, const Operand& startBit,        \
            const Operand& endBit, const Operand& shiftAmt) {           \
    DCHECK(is_uint8(startBit.immediate()));                             \
    DCHECK(is_uint8(endBit.immediate()));                               \
    DCHECK(is_uint8(shiftAmt.immediate()));                             \
    rie_f_format(op_name, dst.code(), src.code(), startBit.immediate(), \
                 endBit.immediate(), shiftAmt.immediate());             \
  }
  S390_RIE_F_OPCODE_LIST(DECLARE_S390_RIE_F_INSTRUCTIONS)
#undef DECLARE_S390_RIE_F_INSTRUCTIONS

  inline void ss_a_format(Opcode op, int f1, int f2, int f3, int f4, int f5) {
    DCHECK(is_uint12(f5));
    DCHECK(is_uint12(f3));
    DCHECK(is_uint8(f1));
    DCHECK(is_uint8(op));
    uint64_t code =
        getfield<uint64_t, 6, 0, 8>(op) | getfield<uint64_t, 6, 8, 16>(f1) |
        getfield<uint64_t, 6, 16, 20>(f2) | getfield<uint64_t, 6, 20, 32>(f3) |
        getfield<uint64_t, 6, 32, 36>(f4) | getfield<uint64_t, 6, 36, 48>(f5);
    emit6bytes(code);
  }

#define DECLARE_S390_SS_A_INSTRUCTIONS(name, op_name, op_value)              \
  void name(Register b1, const Operand& d1, Register b2, const Operand& d2,  \
            const Operand& length) {                                         \
    ss_a_format(op_name, length.immediate(), b1.code(), d1.immediate(),      \
                b2.code(), d2.immediate());                                  \
  }                                                                          \
  void name(const MemOperand& opnd1, const MemOperand& opnd2,                \
            const Operand& length) {                                         \
    ss_a_format(op_name, length.immediate(), opnd1.getBaseRegister().code(), \
                opnd1.getDisplacement(), opnd2.getBaseRegister().code(),     \
                opnd2.getDisplacement());                                    \
  }
  S390_SS_A_OPCODE_LIST(DECLARE_S390_SS_A_INSTRUCTIONS)
#undef DECLARE_S390_SS_A_INSTRUCTIONS

  // Helper for unconditional branch to Label with update to save register
  void b(Register r, Label* l) {
    int32_t halfwords = branch_offset(l) / 2;
    brasl(r, Operand(halfwords));
  }

  // Conditional Branch Instruction - Generates either BRC / BRCL
  void branchOnCond(Condition c, int branch_offset, bool is_bound = false,
                    bool force_long_branch = false);

  // Helpers for conditional branch to Label
  void b(Condition cond, Label* l, Label::Distance dist = Label::kFar,
         bool force_long_branch = false) {
    branchOnCond(cond, branch_offset(l),
                 l->is_bound() || (dist == Label::kNear), force_long_branch);
  }

  void bc_short(Condition cond, Label* l, Label::Distance dist = Label::kFar) {
    b(cond, l, Label::kNear);
  }
  void bc_long(Condition cond, Label* l) { b(cond, l, Label::kFar, true); }
  // Helpers for conditional branch to Label
  void beq(Label* l, Label::Distance dist = Label::kFar) { b(eq, l, dist); }
  void bne(Label* l, Label::Distance dist = Label::kFar) { b(ne, l, dist); }
  void blt(Label* l, Label::Distance dist = Label::kFar) { b(lt, l, dist); }
  void ble(Label* l, Label::Distance dist = Label::kFar) { b(le, l, dist); }
  void bgt(Label* l, Label::Distance dist = Label::kFar) { b(gt, l, dist); }
  void bge(Label* l, Label::Distance dist = Label::kFar) { b(ge, l, dist); }
  void b(Label* l, Label::Distance dist = Label::kFar) { b(al, l, dist); }
  void jmp(Label* l, Label::Distance dist = Label::kFar) { b(al, l, dist); }
  void bunordered(Label* l, Label::Distance dist = Label::kFar) {
    b(unordered, l, dist);
  }
  void bordered(Label* l, Label::Distance dist = Label::kFar) {
    b(ordered, l, dist);
  }

  // Helpers for conditional indirect branch off register
  void b(Condition cond, Register r) { bcr(cond, r); }
  void beq(Register r) { b(eq, r); }
  void bne(Register r) { b(ne, r); }
  void blt(Register r) { b(lt, r); }
  void ble(Register r) { b(le, r); }
  void bgt(Register r) { b(gt, r); }
  void bge(Register r) { b(ge, r); }
  void b(Register r) { b(al, r); }
  void jmp(Register r) { b(al, r); }
  void bunordered(Register r) { b(unordered, r); }
  void bordered(Register r) { b(ordered, r); }

  // wrappers around asm instr
  void brxh(Register dst, Register inc, Label* L) {
    int offset_halfwords = branch_offset(L) / 2;
    CHECK(is_int16(offset_halfwords));
    brxh(dst, inc, Operand(offset_halfwords));
  }

  void brxhg(Register dst, Register inc, Label* L) {
    int offset_halfwords = branch_offset(L) / 2;
    CHECK(is_int16(offset_halfwords));
    brxhg(dst, inc, Operand(offset_halfwords));
  }

  template <class R1, class R2>
  void ledbr(R1 r1, R2 r2) {
    ledbra(Condition(0), Condition(0), r1, r2);
  }

  template <class R1, class R2>
  void cdfbr(R1 r1, R2 r2) {
    cdfbra(Condition(0), Condition(0), r1, r2);
  }

  template <class R1, class R2>
  void cdgbr(R1 r1, R2 r2) {
    cdgbra(Condition(0), Condition(0), r1, r2);
  }

  template <class R1, class R2>
  void cegbr(R1 r1, R2 r2) {
    cegbra(Condition(0), Condition(0), r1, r2);
  }

  template <class R1, class R2>
  void cgebr(Condition m3, R1 r1, R2 r2) {
    cgebra(m3, Condition(0), r1, r2);
  }

  template <class R1, class R2>
  void cgdbr(Condition m3, R1 r1, R2 r2) {
    cgdbra(m3, Condition(0), r1, r2);
  }

  template <class R1, class R2>
  void cfdbr(Condition m3, R1 r1, R2 r2) {
    cfdbra(m3, Condition(0), r1, r2);
  }

  template <class R1, class R2>
  void cfebr(Condition m3, R1 r1, R2 r2) {
    cfebra(m3, Condition(0), r1, r2);
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

  void breakpoint(bool do_print) {
    if (do_print) {
      PrintF("DebugBreak is inserted to %p\n", static_cast<void*>(pc_));
    }
#if V8_HOST_ARCH_64_BIT
    int64_t value = reinterpret_cast<uint64_t>(&v8::base::OS::DebugBreak);
    int32_t hi_32 = static_cast<int64_t>(value) >> 32;
    int32_t lo_32 = static_cast<int32_t>(value);

    iihf(r1, Operand(hi_32));
    iilf(r1, Operand(lo_32));
#else
    iilf(r1, Operand(reinterpret_cast<uint32_t>(&v8::base::OS::DebugBreak)));
#endif
    basr(r14, r1);
  }

  void call(Handle<Code> target, RelocInfo::Mode rmode);
  void jump(Handle<Code> target, RelocInfo::Mode rmode, Condition cond);

// S390 instruction generation
#define DECLARE_VRR_A_INSTRUCTIONS(name, opcode_name, opcode_value)           \
  void name(DoubleRegister v1, DoubleRegister v2, Condition m5, Condition m4, \
            Condition m3) {                                                   \
    uint64_t code = (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 |    \
                    (static_cast<uint64_t>(v1.code())) * B36 |                \
                    (static_cast<uint64_t>(v2.code())) * B32 |                \
                    (static_cast<uint64_t>(m5 & 0xF)) * B20 |                 \
                    (static_cast<uint64_t>(m4 & 0xF)) * B16 |                 \
                    (static_cast<uint64_t>(m3 & 0xF)) * B12 |                 \
                    (static_cast<uint64_t>(0)) * B8 |                         \
                    (static_cast<uint64_t>(opcode_value & 0x00FF));           \
    emit6bytes(code);                                                         \
  }
  S390_VRR_A_OPCODE_LIST(DECLARE_VRR_A_INSTRUCTIONS)
#undef DECLARE_VRR_A_INSTRUCTIONS

#define DECLARE_VRR_C_INSTRUCTIONS(name, opcode_name, opcode_value)        \
  void name(DoubleRegister v1, DoubleRegister v2, DoubleRegister v3,       \
            Condition m6, Condition m5, Condition m4) {                    \
    uint64_t code = (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 | \
                    (static_cast<uint64_t>(v1.code())) * B36 |             \
                    (static_cast<uint64_t>(v2.code())) * B32 |             \
                    (static_cast<uint64_t>(v3.code())) * B28 |             \
                    (static_cast<uint64_t>(m6 & 0xF)) * B20 |              \
                    (static_cast<uint64_t>(m5 & 0xF)) * B16 |              \
                    (static_cast<uint64_t>(m4 & 0xF)) * B12 |              \
                    (static_cast<uint64_t>(0)) * B8 |                      \
                    (static_cast<uint64_t>(opcode_value & 0x00FF));        \
    emit6bytes(code);                                                      \
  }
  S390_VRR_C_OPCODE_LIST(DECLARE_VRR_C_INSTRUCTIONS)
#undef DECLARE_VRR_C_INSTRUCTIONS

#define DECLARE_VRR_B_INSTRUCTIONS(name, opcode_name, opcode_value)        \
  void name(DoubleRegister v1, DoubleRegister v2, DoubleRegister v3,       \
            Condition m5, Condition m4) {                                  \
    uint64_t code = (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 | \
                    (static_cast<uint64_t>(v1.code())) * B36 |             \
                    (static_cast<uint64_t>(v2.code())) * B32 |             \
                    (static_cast<uint64_t>(v3.code())) * B28 |             \
                    (static_cast<uint64_t>(m5 & 0xF)) * B20 |              \
                    (static_cast<uint64_t>(m4 & 0xF)) * B12 |              \
                    (static_cast<uint64_t>(0)) * B8 |                      \
                    (static_cast<uint64_t>(opcode_value & 0x00FF));        \
    emit6bytes(code);                                                      \
  }
  S390_VRR_B_OPCODE_LIST(DECLARE_VRR_B_INSTRUCTIONS)
#undef DECLARE_VRR_B_INSTRUCTIONS

#define DECLARE_VRR_E_INSTRUCTIONS(name, opcode_name, opcode_value)        \
  void name(DoubleRegister v1, DoubleRegister v2, DoubleRegister v3,       \
            DoubleRegister v4, Condition m6, Condition m5) {               \
    uint64_t code = (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 | \
                    (static_cast<uint64_t>(v1.code())) * B36 |             \
                    (static_cast<uint64_t>(v2.code())) * B32 |             \
                    (static_cast<uint64_t>(v3.code())) * B28 |             \
                    (static_cast<uint64_t>(m6 & 0xF)) * B24 |              \
                    (static_cast<uint64_t>(m5 & 0xF)) * B16 |              \
                    (static_cast<uint64_t>(v4.code())) * B12 |             \
                    (static_cast<uint64_t>(0)) * B8 |                      \
                    (static_cast<uint64_t>(opcode_value & 0x00FF));        \
    emit6bytes(code);                                                      \
  }
  S390_VRR_E_OPCODE_LIST(DECLARE_VRR_E_INSTRUCTIONS)
#undef DECLARE_VRR_E_INSTRUCTIONS

#define DECLARE_VRR_F_INSTRUCTIONS(name, opcode_name, opcode_value)        \
  void name(DoubleRegister v1, Register r1, Register r2) {                 \
    uint64_t code = (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 | \
                    (static_cast<uint64_t>(v1.code())) * B36 |             \
                    (static_cast<uint64_t>(r1.code())) * B32 |             \
                    (static_cast<uint64_t>(r2.code())) * B28 |             \
                    (static_cast<uint64_t>(0)) * B8 |                      \
                    (static_cast<uint64_t>(opcode_value & 0x00FF));        \
    emit6bytes(code);                                                      \
  }
  S390_VRR_F_OPCODE_LIST(DECLARE_VRR_F_INSTRUCTIONS)
#undef DECLARE_VRR_E_INSTRUCTIONS

#define DECLARE_VRX_INSTRUCTIONS(name, opcode_name, opcode_value)       \
  void name(DoubleRegister v1, const MemOperand& opnd, Condition m3) {  \
    uint64_t code =                                                     \
        (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 |          \
        (static_cast<uint64_t>(v1.code())) * B36 |                      \
        (static_cast<uint64_t>(opnd.getIndexRegister().code())) * B32 | \
        (static_cast<uint64_t>(opnd.getBaseRegister().code())) * B28 |  \
        (static_cast<uint64_t>(opnd.getDisplacement())) * B16 |         \
        (static_cast<uint64_t>(m3 & 0xF)) * B12 |                       \
        (static_cast<uint64_t>(0)) * B8 |                               \
        (static_cast<uint64_t>(opcode_value & 0x00FF));                 \
    emit6bytes(code);                                                   \
  }
  S390_VRX_OPCODE_LIST(DECLARE_VRX_INSTRUCTIONS)
#undef DECLARE_VRX_INSTRUCTIONS

#define DECLARE_VRS_A_INSTRUCTIONS(name, opcode_name, opcode_value)       \
  void name(DoubleRegister v1, DoubleRegister v2, const MemOperand& opnd, \
            Condition m4 = Condition(0)) {                                \
    uint64_t code =                                                       \
        (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 |            \
        (static_cast<uint64_t>(v1.code())) * B36 |                        \
        (static_cast<uint64_t>(v2.code())) * B32 |                        \
        (static_cast<uint64_t>(opnd.getBaseRegister().code())) * B28 |    \
        (static_cast<uint64_t>(opnd.getDisplacement())) * B16 |           \
        (static_cast<uint64_t>(m4 & 0xF)) * B12 |                         \
        (static_cast<uint64_t>(0)) * B8 |                                 \
        (static_cast<uint64_t>(opcode_value & 0x00FF));                   \
    emit6bytes(code);                                                     \
  }
  S390_VRS_A_OPCODE_LIST(DECLARE_VRS_A_INSTRUCTIONS)
#undef DECLARE_VRS_A_INSTRUCTIONS

#define DECLARE_VRS_B_INSTRUCTIONS(name, opcode_name, opcode_value)    \
  void name(DoubleRegister v1, Register r1, const MemOperand& opnd,    \
            Condition m4 = Condition(0)) {                             \
    uint64_t code =                                                    \
        (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 |         \
        (static_cast<uint64_t>(v1.code())) * B36 |                     \
        (static_cast<uint64_t>(r1.code())) * B32 |                     \
        (static_cast<uint64_t>(opnd.getBaseRegister().code())) * B28 | \
        (static_cast<uint64_t>(opnd.getDisplacement())) * B16 |        \
        (static_cast<uint64_t>(m4 & 0xF)) * B12 |                      \
        (static_cast<uint64_t>(0)) * B8 |                              \
        (static_cast<uint64_t>(opcode_value & 0x00FF));                \
    emit6bytes(code);                                                  \
  }
  S390_VRS_B_OPCODE_LIST(DECLARE_VRS_B_INSTRUCTIONS)
#undef DECLARE_VRS_B_INSTRUCTIONS

#define DECLARE_VRS_C_INSTRUCTIONS(name, opcode_name, opcode_value)    \
  void name(Register r1, DoubleRegister v1, const MemOperand& opnd,    \
            Condition m4 = Condition(0)) {                             \
    uint64_t code =                                                    \
        (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 |         \
        (static_cast<uint64_t>(r1.code())) * B36 |                     \
        (static_cast<uint64_t>(v1.code())) * B32 |                     \
        (static_cast<uint64_t>(opnd.getBaseRegister().code())) * B28 | \
        (static_cast<uint64_t>(opnd.getDisplacement())) * B16 |        \
        (static_cast<uint64_t>(m4 & 0xF)) * B12 |                      \
        (static_cast<uint64_t>(0)) * B8 |                              \
        (static_cast<uint64_t>(opcode_value & 0x00FF));                \
    emit6bytes(code);                                                  \
  }
  S390_VRS_C_OPCODE_LIST(DECLARE_VRS_C_INSTRUCTIONS)
#undef DECLARE_VRS_C_INSTRUCTIONS

#define DECLARE_VRI_A_INSTRUCTIONS(name, opcode_name, opcode_value)        \
  void name(DoubleRegister v1, const Operand& i2, Condition m3) {          \
    uint64_t code = (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 | \
                    (static_cast<uint64_t>(v1.code())) * B36 |             \
                    (static_cast<uint32_t>(i2.immediate())) * B16 |        \
                    (static_cast<uint64_t>(m3 & 0xF)) * B12 |              \
                    (static_cast<uint64_t>(0)) * B8 |                      \
                    (static_cast<uint64_t>(opcode_value & 0x00FF));        \
    emit6bytes(code);                                                      \
  }
  S390_VRI_A_OPCODE_LIST(DECLARE_VRI_A_INSTRUCTIONS)
#undef DECLARE_VRI_A_INSTRUCTIONS

#define DECLARE_VRI_C_INSTRUCTIONS(name, opcode_name, opcode_value)        \
  void name(DoubleRegister v1, DoubleRegister v2, const Operand& i2,       \
            Condition m4) {                                                \
    uint64_t code = (static_cast<uint64_t>(opcode_value & 0xFF00)) * B32 | \
                    (static_cast<uint64_t>(v1.code())) * B36 |             \
                    (static_cast<uint64_t>(v2.code())) * B32 |             \
                    (static_cast<uint16_t>(i2.immediate())) * B16 |        \
                    (static_cast<uint64_t>(m4 & 0xF)) * B12 |              \
                    (static_cast<uint64_t>(0)) * B8 |                      \
                    (static_cast<uint64_t>(opcode_value & 0x00FF));        \
    emit6bytes(code);                                                      \
  }
  S390_VRI_C_OPCODE_LIST(DECLARE_VRI_C_INSTRUCTIONS)
#undef DECLARE_VRI_C_INSTRUCTIONS

  // Single Element format
  void vfa(DoubleRegister v1, DoubleRegister v2, DoubleRegister v3) {
    vfa(v1, v2, v3, static_cast<Condition>(0), static_cast<Condition>(8),
        static_cast<Condition>(3));
  }
  void vfs(DoubleRegister v1, DoubleRegister v2, DoubleRegister v3) {
    vfs(v1, v2, v3, static_cast<Condition>(0), static_cast<Condition>(8),
        static_cast<Condition>(3));
  }
  void vfm(DoubleRegister v1, DoubleRegister v2, DoubleRegister v3) {
    vfm(v1, v2, v3, static_cast<Condition>(0), static_cast<Condition>(8),
        static_cast<Condition>(3));
  }
  void vfd(DoubleRegister v1, DoubleRegister v2, DoubleRegister v3) {
    vfd(v1, v2, v3, static_cast<Condition>(0), static_cast<Condition>(8),
        static_cast<Condition>(3));
  }

  // Load Address Instructions
  void larl(Register r, Label* l);
  void lgrl(Register r, Label* l);

  // Exception-generating instructions and debugging support
  void stop(Condition cond = al, int32_t code = kDefaultStopCode,
            CRegister cr = cr7);

  void bkpt(uint32_t imm16);  // v5 and above

  // Different nop operations are used by the code generator to detect certain
  // states of the generated code.
  enum NopMarkerTypes {
    NON_MARKING_NOP = 0,
    GROUP_ENDING_NOP,
    DEBUG_BREAK_NOP,
#if V8_OS_ZOS
    BASR_CALL_TYPE_NOP,
    BRAS_CALL_TYPE_NOP,
    BRASL_CALL_TYPE_NOP,
#endif
    // IC markers.
    PROPERTY_ACCESS_INLINED,
    PROPERTY_ACCESS_INLINED_CONTEXT,
    PROPERTY_ACCESS_INLINED_CONTEXT_DONT_DELETE,
    // Helper values.
    LAST_CODE_MARKER,
    FIRST_IC_MARKER = PROPERTY_ACCESS_INLINED
  };

  void nop(int type = 0);  // 0 is the default non-marking type.

  void dumy(int r1, int x2, int b2, int d2);

  // Check the code size generated from label to here.
  int SizeOfCodeGeneratedSince(Label* label) {
    return pc_offset() - label->pos();
  }

  // Record a deoptimization reason that can be used by a log or cpu profiler.
  // Use --trace-deopt to enable.
  void RecordDeoptReason(DeoptimizeReason reason, uint32_t node_id,
                         SourcePosition position, int id);

  // Writes a single byte or word of data in the code stream.  Used
  // for inline tables, e.g., jump-tables.
  void db(uint8_t data);
  void dh(uint16_t data);
  void dd(uint32_t data);
  void dq(uint64_t data);
  void dp(uintptr_t data);

  // Read/patch instructions
  SixByteInstr instr_at(int pos) {
    return Instruction::InstructionBits(buffer_start_ + pos);
  }
  template <typename T>
  void instr_at_put(int pos, T instr) {
    Instruction::SetInstructionBits<T>(buffer_start_ + pos, instr);
  }

  // Decodes instruction at pos, and returns its length
  int32_t instr_length_at(int pos) {
    return Instruction::InstructionLength(buffer_start_ + pos);
  }

  static SixByteInstr instr_at(uint8_t* pc) {
    return Instruction::InstructionBits(pc);
  }

  static Condition GetCondition(Instr instr);

  static bool IsBranch(Instr instr);
#if V8_TARGET_ARCH_S390X
  static bool Is64BitLoadIntoIP(SixByteInstr instr1, SixByteInstr instr2);
#else
  static bool Is32BitLoadIntoIP(SixByteInstr instr);
#endif

  static bool IsCmpRegister(Instr instr);
  static bool IsCmpImmediate(Instr instr);
  static bool IsNop(SixByteInstr instr, int type = NON_MARKING_NOP);

  // The code currently calls CheckBuffer() too often. This has the side
  // effect of randomly growing the buffer in the middle of multi-instruction
  // sequences.
  //
  // This function allows outside callers to check and grow the buffer
  void EnsureSpaceFor(int space_needed);

  void EmitRelocations();
  void emit_label_addr(Label* label);

 public:
  uint8_t* buffer_pos() const { return buffer_start_; }

  // InstructionStream generation
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries.
  static constexpr int kGap = 32;
  static_assert(AssemblerBase::kMinimalBufferSize >= 2 * kGap);

 protected:
  int buffer_space() const { return reloc_info_writer.pos() - pc_; }

  // Decode instruction(s) at pos and return backchain to previous
  // label reference or kEndOfChain.
  int target_at(int pos);

  // Patch instruction(s) at pos to target target_pos (e.g. branch)
  void target_at_put(int pos, int target_pos, bool* is_branch = nullptr);

  // Record reloc info for current pc_
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);

 private:
  // Avoid overflows for displacements etc.
  static const int kMaximalBufferSize = 512 * MB;

  // Relocation info generation
  // Each relocation is encoded as a variable size value
  static constexpr int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  RelocInfoWriter reloc_info_writer;
  std::vector<DeferredRelocInfo> relocations_;

  // Scratch registers available for use by the Assembler.
  RegList scratch_register_list_;
  DoubleRegList scratch_double_register_list_;

  // The bound position, before this we cannot do instruction elimination.
  int last_bound_pos_;

  // Code emission
  void CheckBuffer() {
    if (buffer_space() <= kGap) {
      GrowBuffer();
    }
  }
  void GrowBuffer(int needed = 0);
  inline void TrackBranch();
  inline void UntrackBranch();

  // Helper to emit the binary encoding of a 2 byte instruction
  void emit2bytes(uint16_t x) {
    CheckBuffer();
#if V8_TARGET_LITTLE_ENDIAN
    // We need to emit instructions in big endian format as disassembler /
    // simulator require the first byte of the instruction in order to decode
    // the instruction length.  Swap the bytes.
    x = ((x & 0x00FF) << 8) | ((x & 0xFF00) >> 8);
#endif
    *reinterpret_cast<uint16_t*>(pc_) = x;
    pc_ += 2;
  }

  // Helper to emit the binary encoding of a 4 byte instruction
  void emit4bytes(uint32_t x) {
    CheckBuffer();
#if V8_TARGET_LITTLE_ENDIAN
    // We need to emit instructions in big endian format as disassembler /
    // simulator require the first byte of the instruction in order to decode
    // the instruction length.  Swap the bytes.
    x = ((x & 0x000000FF) << 24) | ((x & 0x0000FF00) << 8) |
        ((x & 0x00FF0000) >> 8) | ((x & 0xFF000000) >> 24);
#endif
    *reinterpret_cast<uint32_t*>(pc_) = x;
    pc_ += 4;
  }

  // Helper to emit the binary encoding of a 6 byte instruction
  void emit6bytes(uint64_t x) {
    CheckBuffer();
#if V8_TARGET_LITTLE_ENDIAN
    // We need to emit instructions in big endian format as disassembler /
    // simulator require the first byte of the instruction in order to decode
    // the instruction length.  Swap the bytes.
    x = (static_cast<uint64_t>(x & 0xFF) << 40) |
        (static_cast<uint64_t>((x >> 8) & 0xFF) << 32) |
        (static_cast<uint64_t>((x >> 16) & 0xFF) << 24) |
        (static_cast<uint64_t>((x >> 24) & 0xFF) << 16) |
        (static_cast<uint64_t>((x >> 32) & 0xFF) << 8) |
        (static_cast<uint64_t>((x >> 40) & 0xFF));
    x |= (*reinterpret_cast<uint64_t*>(pc_) >> 48) << 48;
#else
    // We need to pad two bytes of zeros in order to get the 6-bytes
    // stored from low address.
    x = x << 16;
    x |= *reinterpret_cast<uint64_t*>(pc_) & 0xFFFF;
#endif
    // It is safe to store 8-bytes, as CheckBuffer() guarantees we have kGap
    // space left over.
    *reinterpret_cast<uint64_t*>(pc_) = x;
    pc_ += 6;
  }

  // Labels
  void print(Label* L);
  int max_reach_from(int pos);
  void bind_to(Label* L, int pos);
  void next(Label* L);

  void AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate);

  int WriteCodeComments();

  friend class RegExpMacroAssemblerS390;
  friend class RelocInfo;
  friend class EnsureSpace;
  friend class UseScratchRegisterScope;
};

class EnsureSpace {
 public:
  explicit EnsureSpace(Assembler* assembler) { assembler->CheckBuffer(); }
};

class V8_EXPORT_PRIVATE V8_NODISCARD UseScratchRegisterScope {
 public:
  explicit UseScratchRegisterScope(Assembler* assembler)
      : assembler_(assembler),
        old_available_(*assembler->GetScratchRegisterList()),
        old_available_double_(*assembler->GetScratchDoubleRegisterList()) {}

  ~UseScratchRegisterScope() {
    *assembler_->GetScratchRegisterList() = old_available_;
    *assembler_->GetScratchDoubleRegisterList() = old_available_double_;
  }

  Register Acquire() {
    return assembler_->GetScratchRegisterList()->PopFirst();
  }

  DoubleRegister AcquireDouble() {
    return assembler_->GetScratchDoubleRegisterList()->PopFirst();
  }

  // Check if we have registers available to acquire.
  bool CanAcquire() const {
    return !assembler_->GetScratchRegisterList()->is_empty();
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
  void Include(DoubleRegList list) {
    DoubleRegList* available = assembler_->GetScratchDoubleRegisterList();
    DCHECK_NOT_NULL(available);
    DCHECK_EQ((*available & list).bits(), 0x0);
    *available = *available | list;
  }

  DoubleRegList AvailableDoubleRegList() {
    return *assembler_->GetScratchDoubleRegisterList();
  }
  void SetAvailableDoubleRegList(DoubleRegList available) {
    *assembler_->GetScratchDoubleRegisterList() = available;
  }
  RegList Available() { return *assembler_->GetScratchRegisterList(); }
  void SetAvailable(RegList available) {
    *assembler_->GetScratchRegisterList() = available;
  }

 private:
  friend class Assembler;
  friend class MacroAssembler;

  Assembler* assembler_;
  RegList old_available_;
  DoubleRegList old_available_double_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_S390_ASSEMBLER_S390_H_
