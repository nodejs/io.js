// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2011 the V8 project authors. All rights reserved.

// A light-weight IA32 Assembler.

#ifndef V8_CODEGEN_IA32_ASSEMBLER_IA32_H_
#define V8_CODEGEN_IA32_ASSEMBLER_IA32_H_

#include <deque>
#include <memory>

#include "src/codegen/assembler.h"
#include "src/codegen/ia32/constants-ia32.h"
#include "src/codegen/ia32/fma-instr.h"
#include "src/codegen/ia32/register-ia32.h"
#include "src/codegen/ia32/sse-instr.h"
#include "src/codegen/label.h"
#include "src/execution/isolate.h"
#include "src/objects/smi.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class SafepointTableBuilder;

enum Condition : int {
  overflow = 0,
  no_overflow = 1,
  below = 2,
  above_equal = 3,
  equal = 4,
  not_equal = 5,
  below_equal = 6,
  above = 7,
  negative = 8,
  positive = 9,
  parity_even = 10,
  parity_odd = 11,
  less = 12,
  greater_equal = 13,
  less_equal = 14,
  greater = 15,

  // aliases
  carry = below,
  not_carry = above_equal,
  zero = equal,
  not_zero = not_equal,
  sign = negative,
  not_sign = positive,

  // Unified cross-platform condition names/aliases.
  kEqual = equal,
  kNotEqual = not_equal,
  kLessThan = less,
  kGreaterThan = greater,
  kLessThanEqual = less_equal,
  kGreaterThanEqual = greater_equal,
  kUnsignedLessThan = below,
  kUnsignedGreaterThan = above,
  kUnsignedLessThanEqual = below_equal,
  kUnsignedGreaterThanEqual = above_equal,
  kOverflow = overflow,
  kNoOverflow = no_overflow,
  kZero = equal,
  kNotZero = not_equal,
};

// Returns the equivalent of !cc.
inline Condition NegateCondition(Condition cc) {
  return static_cast<Condition>(cc ^ 1);
}

enum RoundingMode {
  kRoundToNearest = 0x0,
  kRoundDown = 0x1,
  kRoundUp = 0x2,
  kRoundToZero = 0x3
};

// -----------------------------------------------------------------------------
// Machine instruction Immediates

class Immediate {
 public:
  // Calls where x is an Address (uintptr_t) resolve to this overload.
  inline explicit Immediate(int x, RelocInfo::Mode rmode = RelocInfo::NO_INFO) {
    value_.immediate = x;
    rmode_ = rmode;
  }
  inline explicit Immediate(const ExternalReference& ext)
      : Immediate(ext.raw(), RelocInfo::EXTERNAL_REFERENCE) {}
  inline explicit Immediate(Handle<HeapObject> handle)
      : Immediate(handle.address(), RelocInfo::FULL_EMBEDDED_OBJECT) {}
  inline explicit Immediate(Tagged<Smi> value)
      : Immediate(static_cast<intptr_t>(value.ptr())) {}

  static Immediate EmbeddedNumber(double number);  // Smi or HeapNumber.

  static Immediate CodeRelativeOffset(Label* label) { return Immediate(label); }

  bool is_heap_number_request() const {
    DCHECK_IMPLIES(is_heap_number_request_,
                   rmode_ == RelocInfo::FULL_EMBEDDED_OBJECT ||
                       rmode_ == RelocInfo::CODE_TARGET);
    return is_heap_number_request_;
  }

  HeapNumberRequest heap_number_request() const {
    DCHECK(is_heap_number_request());
    return value_.heap_number_request;
  }

  int immediate() const {
    DCHECK(!is_heap_number_request());
    return value_.immediate;
  }

  bool is_embedded_object() const {
    return !is_heap_number_request() &&
           rmode() == RelocInfo::FULL_EMBEDDED_OBJECT;
  }

  Handle<HeapObject> embedded_object() const {
    return Handle<HeapObject>(reinterpret_cast<Address*>(immediate()));
  }

  bool is_external_reference() const {
    return rmode() == RelocInfo::EXTERNAL_REFERENCE;
  }

  ExternalReference external_reference() const {
    DCHECK(is_external_reference());
    return base::bit_cast<ExternalReference>(immediate());
  }

  bool is_zero() const {
    return RelocInfo::IsNoInfo(rmode_) && immediate() == 0;
  }
  bool is_int8() const {
    return RelocInfo::IsNoInfo(rmode_) && i::is_int8(immediate());
  }
  bool is_uint8() const {
    return RelocInfo::IsNoInfo(rmode_) && i::is_uint8(immediate());
  }
  bool is_int16() const {
    return RelocInfo::IsNoInfo(rmode_) && i::is_int16(immediate());
  }

  bool is_uint16() const {
    return RelocInfo::IsNoInfo(rmode_) && i::is_uint16(immediate());
  }

  RelocInfo::Mode rmode() const { return rmode_; }

 private:
  inline explicit Immediate(Label* value) {
    value_.immediate = reinterpret_cast<int32_t>(value);
    rmode_ = RelocInfo::INTERNAL_REFERENCE;
  }

  union Value {
    Value() {}
    HeapNumberRequest heap_number_request;
    int immediate;
  } value_;
  bool is_heap_number_request_ = false;
  RelocInfo::Mode rmode_;

  friend class Operand;
  friend class Assembler;
  friend class MacroAssembler;
};

// -----------------------------------------------------------------------------
// Machine instruction Operands

enum ScaleFactor {
  times_1 = 0,
  times_2 = 1,
  times_4 = 2,
  times_8 = 3,
  times_int_size = times_4,

  times_half_system_pointer_size = times_2,
  times_system_pointer_size = times_4,

  times_tagged_size = times_4,
};

class V8_EXPORT_PRIVATE Operand {
 public:
  // reg
  V8_INLINE explicit Operand(Register reg) { set_modrm(3, reg); }

  // XMM reg
  V8_INLINE explicit Operand(XMMRegister xmm_reg) {
    Register reg = Register::from_code(xmm_reg.code());
    set_modrm(3, reg);
  }

  // [disp/r]
  V8_INLINE explicit Operand(int32_t disp, RelocInfo::Mode rmode) {
    set_modrm(0, ebp);
    set_dispr(disp, rmode);
  }

  // [disp/r]
  V8_INLINE explicit Operand(Immediate imm) {
    set_modrm(0, ebp);
    set_dispr(imm.immediate(), imm.rmode_);
  }

  // [base + disp/r]
  explicit Operand(Register base, int32_t disp,
                   RelocInfo::Mode rmode = RelocInfo::NO_INFO);

  // [disp/r]
  explicit Operand(Label* label) {
    set_modrm(0, ebp);
    set_dispr(reinterpret_cast<intptr_t>(label), RelocInfo::INTERNAL_REFERENCE);
  }

  // [base + index*scale + disp/r]
  explicit Operand(Register base, Register index, ScaleFactor scale,
                   int32_t disp, RelocInfo::Mode rmode = RelocInfo::NO_INFO);

  // [index*scale + disp/r]
  explicit Operand(Register index, ScaleFactor scale, int32_t disp,
                   RelocInfo::Mode rmode = RelocInfo::NO_INFO);

  static Operand JumpTable(Register index, ScaleFactor scale, Label* table) {
    return Operand(index, scale, reinterpret_cast<int32_t>(table),
                   RelocInfo::INTERNAL_REFERENCE);
  }

  static Operand ForRegisterPlusImmediate(Register base, Immediate imm) {
    return Operand(base, imm.value_.immediate, imm.rmode_);
  }

  // Returns true if this Operand is a wrapper for the specified register.
  bool is_reg(Register reg) const { return is_reg(reg.code()); }
  bool is_reg(XMMRegister reg) const { return is_reg(reg.code()); }

  // Returns true if this Operand is a wrapper for one register.
  bool is_reg_only() const;

  // Asserts that this Operand is a wrapper for one register and returns the
  // register.
  Register reg() const;

  base::Vector<const uint8_t> encoded_bytes() const { return {buf_, len_}; }
  RelocInfo::Mode rmode() { return rmode_; }

 private:
  // Set the ModRM byte without an encoded 'reg' register. The
  // register is encoded later as part of the emit_operand operation.
  inline void set_modrm(int mod, Register rm) {
    DCHECK_EQ(mod & -4, 0);
    buf_[0] = mod << 6 | rm.code();
    len_ = 1;
  }

  inline void set_sib(ScaleFactor scale, Register index, Register base);
  inline void set_disp8(int8_t disp);
  inline void set_dispr(int32_t disp, RelocInfo::Mode rmode) {
    DCHECK(len_ == 1 || len_ == 2);
    Address p = reinterpret_cast<Address>(&buf_[len_]);
    WriteUnalignedValue(p, disp);
    len_ += sizeof(int32_t);
    rmode_ = rmode;
  }

  inline bool is_reg(int reg_code) const {
    return ((buf_[0] & 0xF8) == 0xC0)  // addressing mode is register only.
           && ((buf_[0] & 0x07) == reg_code);  // register codes match.
  }

  uint8_t buf_[6];
  // The number of bytes in buf_.
  uint8_t len_ = 0;
  // Only valid if len_ > 4.
  RelocInfo::Mode rmode_ = RelocInfo::NO_INFO;
};
ASSERT_TRIVIALLY_COPYABLE(Operand);
static_assert(sizeof(Operand) <= 2 * kSystemPointerSize,
              "Operand must be small enough to pass it by value");

bool operator!=(Operand op, XMMRegister r);

// -----------------------------------------------------------------------------
// A Displacement describes the 32bit immediate field of an instruction which
// may be used together with a Label in order to refer to a yet unknown code
// position. Displacements stored in the instruction stream are used to describe
// the instruction and to chain a list of instructions using the same Label.
// A Displacement contains 2 different fields:
//
// next field: position of next displacement in the chain (0 = end of list)
// type field: instruction type
//
// A next value of null (0) indicates the end of a chain (note that there can
// be no displacement at position zero, because there is always at least one
// instruction byte before the displacement).
//
// Displacement _data field layout
//
// |31.....2|1......0|
// [  next  |  type  |

class Displacement {
 public:
  enum Type { UNCONDITIONAL_JUMP, CODE_RELATIVE, OTHER, CODE_ABSOLUTE };

  int data() const { return data_; }
  Type type() const { return TypeField::decode(data_); }
  void next(Label* L) const {
    int n = NextField::decode(data_);
    n > 0 ? L->link_to(n) : L->Unuse();
  }
  void link_to(Label* L) { init(L, type()); }

  explicit Displacement(int data) { data_ = data; }

  Displacement(Label* L, Type type) { init(L, type); }

  void print() {
    PrintF("%s (%x) ", (type() == UNCONDITIONAL_JUMP ? "jmp" : "[other]"),
           NextField::decode(data_));
  }

 private:
  int data_;

  using TypeField = base::BitField<Type, 0, 2>;
  using NextField = base::BitField<int, 2, 32 - 2>;

  void init(Label* L, Type type);
};

class V8_EXPORT_PRIVATE Assembler : public AssemblerBase {
 private:
  // We check before assembling an instruction that there is sufficient
  // space to write an instruction and its relocation information.
  // The relocation writer's position must be kGap bytes above the end of
  // the generated instructions. This leaves enough space for the
  // longest possible ia32 instruction, 15 bytes, and the longest possible
  // relocation information encoding, RelocInfoWriter::kMaxLength == 16.
  // (There is a 15 byte limit on ia32 instruction length that rules out some
  // otherwise valid instructions.)
  // This allows for a single, fast space check per instruction.
  static constexpr int kGap = 32;
  static_assert(AssemblerBase::kMinimalBufferSize >= 2 * kGap);

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

  // GetCode emits any pending (non-emitted) code and fills the descriptor desc.
  static constexpr int kNoHandlerTable = 0;
  static constexpr SafepointTableBuilder* kNoSafepointTable = nullptr;
  void GetCode(LocalIsolate* isolate, CodeDesc* desc,
               SafepointTableBuilder* safepoint_table_builder,
               int handler_table_offset);

  // Convenience wrapper for allocating with an Isolate.
  void GetCode(Isolate* isolate, CodeDesc* desc);
  // Convenience wrapper for code without safepoint or handler tables.
  void GetCode(LocalIsolate* isolate, CodeDesc* desc) {
    GetCode(isolate, desc, kNoSafepointTable, kNoHandlerTable);
  }

  void FinalizeJumpOptimizationInfo();

  // Unused on this architecture.
  void MaybeEmitOutOfLineConstantPool() {}

  // Read/Modify the code target in the branch/call instruction at pc.
  // The isolate argument is unused (and may be nullptr) when skipping flushing.
  inline static Address target_address_at(Address pc, Address constant_pool);
  inline static void set_target_address_at(
      Address pc, Address constant_pool, Address target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  // This sets the branch destination (which is in the instruction on x86).
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

  static constexpr int kSpecialTargetSize = kSystemPointerSize;

  // One byte opcode for test al, 0xXX.
  static constexpr uint8_t kTestAlByte = 0xA8;
  // One byte opcode for nop.
  static constexpr uint8_t kNopByte = 0x90;

  // One byte opcode for a short unconditional jump.
  static constexpr uint8_t kJmpShortOpcode = 0xEB;
  // One byte prefix for a short conditional jump.
  static constexpr uint8_t kJccShortPrefix = 0x70;
  static constexpr uint8_t kJncShortOpcode = kJccShortPrefix | not_carry;
  static constexpr uint8_t kJcShortOpcode = kJccShortPrefix | carry;
  static constexpr uint8_t kJnzShortOpcode = kJccShortPrefix | not_zero;
  static constexpr uint8_t kJzShortOpcode = kJccShortPrefix | zero;

  // ---------------------------------------------------------------------------
  // InstructionStream generation
  //
  // - function names correspond one-to-one to ia32 instruction mnemonics
  // - unless specified otherwise, instructions operate on 32bit operands
  // - instructions on 8bit (byte) operands/registers have a trailing '_b'
  // - instructions on 16bit (word) operands/registers have a trailing '_w'
  // - naming conflicts with C++ keywords are resolved via a trailing '_'

  // NOTE ON INTERFACE: Currently, the interface is not very consistent
  // in the sense that some operations (e.g. mov()) can be called in more
  // the one way to generate the same instruction: The Register argument
  // can in some cases be replaced with an Operand(Register) argument.
  // This should be cleaned up and made more orthogonal. The questions
  // is: should we always use Operands instead of Registers where an
  // Operand is possible, or should we have a Register (overloaded) form
  // instead? We must be careful to make sure that the selected instruction
  // is obvious from the parameters to avoid hard-to-find code generation
  // bugs.

  // Insert the smallest number of nop instructions
  // possible to align the pc offset to a multiple
  // of m. m must be a power of 2.
  void Align(int m);
  // Insert the smallest number of zero bytes possible to align the pc offset
  // to a mulitple of m. m must be a power of 2 (>= 2).
  void DataAlign(int m);
  void Nop(int bytes = 1);
  // Aligns code to something that's optimal for a jump target for the platform.
  void CodeTargetAlign();
  void LoopHeaderAlign() { CodeTargetAlign(); }

  // Stack
  void pushad();
  void popad();

  void pushfd();
  void popfd();

  void push(const Immediate& x);
  void push_imm32(int32_t imm32);
  void push(Register src);
  void push(Operand src);

  void pop(Register dst);
  void pop(Operand dst);

  void leave();

  // Moves
  void mov_b(Register dst, Register src) { mov_b(dst, Operand(src)); }
  void mov_b(Register dst, Operand src);
  void mov_b(Register dst, int8_t imm8) { mov_b(Operand(dst), imm8); }
  void mov_b(Operand dst, int8_t src) { mov_b(dst, Immediate(src)); }
  void mov_b(Operand dst, const Immediate& src);
  void mov_b(Operand dst, Register src);

  void mov_w(Register dst, Operand src);
  void mov_w(Operand dst, int16_t src) { mov_w(dst, Immediate(src)); }
  void mov_w(Operand dst, const Immediate& src);
  void mov_w(Operand dst, Register src);

  void mov(Register dst, int32_t imm32);
  void mov(Register dst, const Immediate& x);
  void mov(Register dst, Handle<HeapObject> handle);
  void mov(Register dst, Operand src);
  void mov(Register dst, Register src);
  void mov(Operand dst, const Immediate& x);
  void mov(Operand dst, Handle<HeapObject> handle);
  void mov(Operand dst, Register src);
  void mov(Operand dst, Address src, RelocInfo::Mode);

  void movsx_b(Register dst, Register src) { movsx_b(dst, Operand(src)); }
  void movsx_b(Register dst, Operand src);

  void movsx_w(Register dst, Register src) { movsx_w(dst, Operand(src)); }
  void movsx_w(Register dst, Operand src);

  void movzx_b(Register dst, Register src) { movzx_b(dst, Operand(src)); }
  void movzx_b(Register dst, Operand src);

  void movzx_w(Register dst, Register src) { movzx_w(dst, Operand(src)); }
  void movzx_w(Register dst, Operand src);

  void movq(XMMRegister dst, Operand src);
  void movq(Operand dst, XMMRegister src);

  // Conditional moves
  void cmov(Condition cc, Register dst, Register src) {
    cmov(cc, dst, Operand(src));
  }
  void cmov(Condition cc, Register dst, Operand src);

  // Flag management.
  void cld();

  // Repetitive string instructions.
  void rep_movs();
  void rep_stos();
  void stos();

  void xadd(Operand dst, Register src);
  void xadd_b(Operand dst, Register src);
  void xadd_w(Operand dst, Register src);

  // Exchange
  void xchg(Register dst, Register src);
  void xchg(Register dst, Operand src);
  void xchg_b(Register reg, Operand op);
  void xchg_w(Register reg, Operand op);

  // Lock prefix
  void lock();

  // CompareExchange
  void cmpxchg(Operand dst, Register src);
  void cmpxchg_b(Operand dst, Register src);
  void cmpxchg_w(Operand dst, Register src);
  void cmpxchg8b(Operand dst);

  // Memory Fence
  void mfence();
  void lfence();

  void pause();

  // Arithmetics
  void adc(Register dst, int32_t imm32);
  void adc(Register dst, Register src) { adc(dst, Operand(src)); }
  void adc(Register dst, Operand src);

  void add(Register dst, Register src) { add(dst, Operand(src)); }
  void add(Register dst, Operand src);
  void add(Operand dst, Register src);
  void add(Register dst, const Immediate& imm) { add(Operand(dst), imm); }
  void add(Operand dst, const Immediate& x);

  void and_(Register dst, int32_t imm32);
  void and_(Register dst, const Immediate& x);
  void and_(Register dst, Register src) { and_(dst, Operand(src)); }
  void and_(Register dst, Operand src);
  void and_(Operand dst, Register src);
  void and_(Operand dst, const Immediate& x);

  void cmpb(Register reg, Immediate imm8) {
    DCHECK(reg.is_byte_register());
    cmpb(Operand(reg), imm8);
  }
  void cmpb(Operand op, Immediate imm8);
  void cmpb(Register reg, Operand op);
  void cmpb(Operand op, Register reg);
  void cmpb(Register dst, Register src) { cmpb(Operand(dst), src); }
  void cmpb_al(Operand op);
  void cmpw_ax(Operand op);
  void cmpw(Operand dst, Immediate src);
  void cmpw(Register dst, Immediate src) { cmpw(Operand(dst), src); }
  void cmpw(Register dst, Operand src);
  void cmpw(Register dst, Register src) { cmpw(Operand(dst), src); }
  void cmpw(Operand dst, Register src);
  void cmp(Register reg, int32_t imm32);
  void cmp(Register reg, Handle<HeapObject> handle);
  void cmp(Register reg0, Register reg1) { cmp(reg0, Operand(reg1)); }
  void cmp(Register reg, Operand op);
  void cmp(Register reg, const Immediate& imm) { cmp(Operand(reg), imm); }
  void cmp(Operand op, Register reg);
  void cmp(Operand op, const Immediate& imm);
  void cmp(Operand op, Handle<HeapObject> handle);

  void dec_b(Register dst);
  void dec_b(Operand dst);

  void dec(Register dst);
  void dec(Operand dst);

  void cdq();

  void idiv(Register src) { idiv(Operand(src)); }
  void idiv(Operand src);
  void div(Register src) { div(Operand(src)); }
  void div(Operand src);

  // Signed multiply instructions.
  void imul(Register src);  // edx:eax = eax * src.
  void imul(Register dst, Register src) { imul(dst, Operand(src)); }
  void imul(Register dst, Operand src);                  // dst = dst * src.
  void imul(Register dst, Register src, int32_t imm32);  // dst = src * imm32.
  void imul(Register dst, Operand src, int32_t imm32);

  void inc(Register dst);
  void inc(Operand dst);

  void lea(Register dst, Operand src);
  void lea(Register dst, Register src, Label* lbl);

  // Unsigned multiply instruction.
  void mul(Register src);  // edx:eax = eax * reg.

  void neg(Register dst);
  void neg(Operand dst);

  void not_(Register dst);
  void not_(Operand dst);

  void or_(Register dst, int32_t imm32);
  void or_(Register dst, Register src) { or_(dst, Operand(src)); }
  void or_(Register dst, Operand src);
  void or_(Operand dst, Register src);
  void or_(Register dst, const Immediate& imm) { or_(Operand(dst), imm); }
  void or_(Operand dst, const Immediate& x);

  void rcl(Register dst, uint8_t imm8);
  void rcr(Register dst, uint8_t imm8);

  void rol(Register dst, uint8_t imm8) { rol(Operand(dst), imm8); }
  void rol(Operand dst, uint8_t imm8);
  void rol_cl(Register dst) { rol_cl(Operand(dst)); }
  void rol_cl(Operand dst);

  void ror(Register dst, uint8_t imm8) { ror(Operand(dst), imm8); }
  void ror(Operand dst, uint8_t imm8);
  void ror_cl(Register dst) { ror_cl(Operand(dst)); }
  void ror_cl(Operand dst);

  void sar(Register dst, uint8_t imm8) { sar(Operand(dst), imm8); }
  void sar(Operand dst, uint8_t imm8);
  void sar_cl(Register dst) { sar_cl(Operand(dst)); }
  void sar_cl(Operand dst);

  void sbb(Register dst, Register src) { sbb(dst, Operand(src)); }
  void sbb(Register dst, Operand src);

  void shl(Register dst, uint8_t imm8) { shl(Operand(dst), imm8); }
  void shl(Operand dst, uint8_t imm8);
  void shl_cl(Register dst) { shl_cl(Operand(dst)); }
  void shl_cl(Operand dst);
  void shld(Register dst, Register src, uint8_t shift);
  void shld_cl(Register dst, Register src);

  void shr(Register dst, uint8_t imm8) { shr(Operand(dst), imm8); }
  void shr(Operand dst, uint8_t imm8);
  void shr_cl(Register dst) { shr_cl(Operand(dst)); }
  void shr_cl(Operand dst);
  void shrd(Register dst, Register src, uint8_t shift);
  void shrd_cl(Register dst, Register src) { shrd_cl(Operand(dst), src); }
  void shrd_cl(Operand dst, Register src);

  void sub(Register dst, const Immediate& imm) { sub(Operand(dst), imm); }
  void sub(Operand dst, const Immediate& x);
  void sub(Register dst, Register src) { sub(dst, Operand(src)); }
  void sub(Register dst, Operand src);
  void sub(Operand dst, Register src);
  void sub_sp_32(uint32_t imm);

  void test(Register reg, const Immediate& imm);
  void test(Register reg0, Register reg1) { test(reg0, Operand(reg1)); }
  void test(Register reg, Operand op);
  void test(Operand op, const Immediate& imm);
  void test(Operand op, Register reg) { test(reg, op); }
  void test_b(Register reg, Operand op);
  void test_b(Register reg, Immediate imm8);
  void test_b(Operand op, Immediate imm8);
  void test_b(Operand op, Register reg) { test_b(reg, op); }
  void test_b(Register dst, Register src) { test_b(dst, Operand(src)); }
  void test_w(Register reg, Operand op);
  void test_w(Register reg, Immediate imm16);
  void test_w(Operand op, Immediate imm16);
  void test_w(Operand op, Register reg) { test_w(reg, op); }
  void test_w(Register dst, Register src) { test_w(dst, Operand(src)); }

  void xor_(Register dst, int32_t imm32);
  void xor_(Register dst, Register src) { xor_(dst, Operand(src)); }
  void xor_(Register dst, Operand src);
  void xor_(Operand dst, Register src);
  void xor_(Register dst, const Immediate& imm) { xor_(Operand(dst), imm); }
  void xor_(Operand dst, const Immediate& x);

  // Bit operations.
  void bswap(Register dst);
  void bt(Operand dst, Register src);
  void bts(Register dst, Register src) { bts(Operand(dst), src); }
  void bts(Operand dst, Register src);
  void bsr(Register dst, Register src) { bsr(dst, Operand(src)); }
  void bsr(Register dst, Operand src);
  void bsf(Register dst, Register src) { bsf(dst, Operand(src)); }
  void bsf(Register dst, Operand src);

  // Miscellaneous
  void hlt();
  void int3();
  void nop();
  void ret(int imm16);
  void ud2();

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

  // Calls
  void call(Label* L);
  void call(Address entry, RelocInfo::Mode rmode);
  void call(Register reg) { call(Operand(reg)); }
  void call(Operand adr);
  void call(Handle<Code> code, RelocInfo::Mode rmode);
  void wasm_call(Address address, RelocInfo::Mode rmode);

  // Jumps
  // unconditional jump to L
  void jmp(Label* L, Label::Distance distance = Label::kFar);
  void jmp(Address entry, RelocInfo::Mode rmode);
  void jmp(Register reg) { jmp(Operand(reg)); }
  void jmp(Operand adr);
  void jmp(Handle<Code> code, RelocInfo::Mode rmode);
  // Unconditional jump relative to the current address. Low-level routine,
  // use with caution!
  void jmp_rel(int offset);

  // Conditional jumps
  void j(Condition cc, Label* L, Label::Distance distance = Label::kFar);
  void j(Condition cc, uint8_t* entry, RelocInfo::Mode rmode);
  void j(Condition cc, Handle<Code> code,
         RelocInfo::Mode rmode = RelocInfo::CODE_TARGET);

  // Floating-point operations
  void fld(int i);
  void fstp(int i);

  void fld1();
  void fldz();
  void fldpi();
  void fldln2();

  void fld_s(Operand adr);
  void fld_d(Operand adr);

  void fstp_s(Operand adr);
  void fst_s(Operand adr);
  void fstp_d(Operand adr);
  void fst_d(Operand adr);

  void fild_s(Operand adr);
  void fild_d(Operand adr);

  void fist_s(Operand adr);

  void fistp_s(Operand adr);
  void fistp_d(Operand adr);

  // The fisttp instructions require SSE3.
  void fisttp_s(Operand adr);
  void fisttp_d(Operand adr);

  void fabs();
  void fchs();
  void fcos();
  void fsin();
  void fptan();
  void fyl2x();
  void f2xm1();
  void fscale();
  void fninit();

  void fadd(int i);
  void fadd_i(int i);
  void fsub(int i);
  void fsub_i(int i);
  void fmul(int i);
  void fmul_i(int i);
  void fdiv(int i);
  void fdiv_i(int i);

  void fisub_s(Operand adr);

  void faddp(int i = 1);
  void fsubp(int i = 1);
  void fsubrp(int i = 1);
  void fmulp(int i = 1);
  void fdivp(int i = 1);
  void fprem();
  void fprem1();

  void fxch(int i = 1);
  void fincstp();
  void ffree(int i = 0);

  void ftst();
  void fucomp(int i);
  void fucompp();
  void fucomi(int i);
  void fucomip();
  void fcompp();
  void fnstsw_ax();
  void fwait();
  void fnclex();

  void frndint();

  void sahf();
  void setcc(Condition cc, Register reg);

  void cpuid();

  // SSE instructions
  void addss(XMMRegister dst, XMMRegister src) { addss(dst, Operand(src)); }
  void addss(XMMRegister dst, Operand src);
  void subss(XMMRegister dst, XMMRegister src) { subss(dst, Operand(src)); }
  void subss(XMMRegister dst, Operand src);
  void mulss(XMMRegister dst, XMMRegister src) { mulss(dst, Operand(src)); }
  void mulss(XMMRegister dst, Operand src);
  void divss(XMMRegister dst, XMMRegister src) { divss(dst, Operand(src)); }
  void divss(XMMRegister dst, Operand src);
  void sqrtss(XMMRegister dst, XMMRegister src) { sqrtss(dst, Operand(src)); }
  void sqrtss(XMMRegister dst, Operand src);

  void ucomiss(XMMRegister dst, XMMRegister src) { ucomiss(dst, Operand(src)); }
  void ucomiss(XMMRegister dst, Operand src);
  void movaps(XMMRegister dst, XMMRegister src) { movaps(dst, Operand(src)); }
  void movaps(XMMRegister dst, Operand src);
  void movups(XMMRegister dst, XMMRegister src) { movups(dst, Operand(src)); }
  void movups(XMMRegister dst, Operand src);
  void movups(Operand dst, XMMRegister src);
  void shufps(XMMRegister dst, XMMRegister src, uint8_t imm8);
  void shufpd(XMMRegister dst, XMMRegister src, uint8_t imm8);

  void movhlps(XMMRegister dst, XMMRegister src);
  void movlhps(XMMRegister dst, XMMRegister src);
  void movlps(XMMRegister dst, Operand src);
  void movlps(Operand dst, XMMRegister src);
  void movhps(XMMRegister dst, Operand src);
  void movhps(Operand dst, XMMRegister src);

  void maxss(XMMRegister dst, XMMRegister src) { maxss(dst, Operand(src)); }
  void maxss(XMMRegister dst, Operand src);
  void minss(XMMRegister dst, XMMRegister src) { minss(dst, Operand(src)); }
  void minss(XMMRegister dst, Operand src);

  void haddps(XMMRegister dst, Operand src);
  void haddps(XMMRegister dst, XMMRegister src) { haddps(dst, Operand(src)); }
  void sqrtpd(XMMRegister dst, Operand src) {
    sse2_instr(dst, src, 0x66, 0x0F, 0x51);
  }
  void sqrtpd(XMMRegister dst, XMMRegister src) { sqrtpd(dst, Operand(src)); }

  void cmpps(XMMRegister dst, Operand src, uint8_t cmp);
  void cmpps(XMMRegister dst, XMMRegister src, uint8_t cmp) {
    cmpps(dst, Operand(src), cmp);
  }
  void cmppd(XMMRegister dst, Operand src, uint8_t cmp);
  void cmppd(XMMRegister dst, XMMRegister src, uint8_t cmp) {
    cmppd(dst, Operand(src), cmp);
  }

// Packed floating-point comparison operations.
#define PACKED_CMP_LIST(V) \
  V(cmpeq, 0x0)            \
  V(cmplt, 0x1)            \
  V(cmple, 0x2)            \
  V(cmpunord, 0x3)         \
  V(cmpneq, 0x4)

#define SSE_CMP_P(instr, imm8)                                            \
  void instr##ps(XMMRegister dst, XMMRegister src) {                      \
    cmpps(dst, Operand(src), imm8);                                       \
  }                                                                       \
  void instr##ps(XMMRegister dst, Operand src) { cmpps(dst, src, imm8); } \
  void instr##pd(XMMRegister dst, XMMRegister src) {                      \
    cmppd(dst, Operand(src), imm8);                                       \
  }                                                                       \
  void instr##pd(XMMRegister dst, Operand src) { cmppd(dst, src, imm8); }

  PACKED_CMP_LIST(SSE_CMP_P)
#undef SSE_CMP_P

  // SSE2 instructions
  void cvttss2si(Register dst, Operand src);
  void cvttss2si(Register dst, XMMRegister src) {
    cvttss2si(dst, Operand(src));
  }
  void cvttsd2si(Register dst, Operand src);
  void cvttsd2si(Register dst, XMMRegister src) {
    cvttsd2si(dst, Operand(src));
  }
  void cvtsd2si(Register dst, XMMRegister src);

  void cvtsi2ss(XMMRegister dst, Register src) { cvtsi2ss(dst, Operand(src)); }
  void cvtsi2ss(XMMRegister dst, Operand src);
  void cvtsi2sd(XMMRegister dst, Register src) { cvtsi2sd(dst, Operand(src)); }
  void cvtsi2sd(XMMRegister dst, Operand src);
  void cvtss2sd(XMMRegister dst, Operand src);
  void cvtss2sd(XMMRegister dst, XMMRegister src) {
    cvtss2sd(dst, Operand(src));
  }
  void cvtdq2pd(XMMRegister dst, XMMRegister src);
  void cvtpd2ps(XMMRegister dst, XMMRegister src);
  void cvttps2dq(XMMRegister dst, XMMRegister src) {
    cvttps2dq(dst, Operand(src));
  }
  void cvttps2dq(XMMRegister dst, Operand src);
  void cvttpd2dq(XMMRegister dst, XMMRegister src);

  void ucomisd(XMMRegister dst, XMMRegister src) { ucomisd(dst, Operand(src)); }
  void ucomisd(XMMRegister dst, Operand src);

  void roundss(XMMRegister dst, XMMRegister src, RoundingMode mode);
  void roundsd(XMMRegister dst, XMMRegister src, RoundingMode mode);

  void movapd(XMMRegister dst, XMMRegister src) { movapd(dst, Operand(src)); }
  void movapd(XMMRegister dst, Operand src) {
    sse2_instr(dst, src, 0x66, 0x0F, 0x28);
  }
  void movupd(XMMRegister dst, Operand src) {
    sse2_instr(dst, src, 0x66, 0x0F, 0x10);
  }

  void movmskpd(Register dst, XMMRegister src);
  void movmskps(Register dst, XMMRegister src);

  void pmovmskb(Register dst, XMMRegister src);

  void cmpltsd(XMMRegister dst, XMMRegister src);

  void movdqa(XMMRegister dst, Operand src);
  void movdqa(Operand dst, XMMRegister src);
  void movdqa(XMMRegister dst, XMMRegister src);
  void movdqu(XMMRegister dst, Operand src);
  void movdqu(Operand dst, XMMRegister src);
  void movdqu(XMMRegister dst, XMMRegister src);
  void movdq(bool aligned, XMMRegister dst, Operand src) {
    if (aligned) {
      movdqa(dst, src);
    } else {
      movdqu(dst, src);
    }
  }

  void movd(XMMRegister dst, Register src) { movd(dst, Operand(src)); }
  void movd(XMMRegister dst, Operand src);
  void movd(Register dst, XMMRegister src) { movd(Operand(dst), src); }
  void movd(Operand dst, XMMRegister src);
  void movsd(XMMRegister dst, XMMRegister src) { movsd(dst, Operand(src)); }
  void movsd(XMMRegister dst, Operand src);
  void movsd(Operand dst, XMMRegister src);

  void movss(XMMRegister dst, Operand src);
  void movss(Operand dst, XMMRegister src);
  void movss(XMMRegister dst, XMMRegister src) { movss(dst, Operand(src)); }

  void extractps(Operand dst, XMMRegister src, uint8_t imm8);
  void extractps(Register dst, XMMRegister src, uint8_t imm8);

  void pcmpgtq(XMMRegister dst, XMMRegister src);

  void psllw(XMMRegister reg, uint8_t shift);
  void pslld(XMMRegister reg, uint8_t shift);
  void psrlw(XMMRegister reg, uint8_t shift);
  void psrld(XMMRegister reg, uint8_t shift);
  void psraw(XMMRegister reg, uint8_t shift);
  void psrad(XMMRegister reg, uint8_t shift);
  void psllq(XMMRegister reg, uint8_t shift);
  void psrlq(XMMRegister reg, uint8_t shift);

  void pshufhw(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
    pshufhw(dst, Operand(src), shuffle);
  }
  void pshufhw(XMMRegister dst, Operand src, uint8_t shuffle);
  void pshuflw(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
    pshuflw(dst, Operand(src), shuffle);
  }
  void pshuflw(XMMRegister dst, Operand src, uint8_t shuffle);
  void pshufd(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
    pshufd(dst, Operand(src), shuffle);
  }
  void pshufd(XMMRegister dst, Operand src, uint8_t shuffle);

  void pblendw(XMMRegister dst, XMMRegister src, uint8_t mask) {
    pblendw(dst, Operand(src), mask);
  }
  void pblendw(XMMRegister dst, Operand src, uint8_t mask);

  void palignr(XMMRegister dst, XMMRegister src, uint8_t mask) {
    palignr(dst, Operand(src), mask);
  }
  void palignr(XMMRegister dst, Operand src, uint8_t mask);

  void pextrb(Register dst, XMMRegister src, uint8_t offset) {
    pextrb(Operand(dst), src, offset);
  }
  void pextrb(Operand dst, XMMRegister src, uint8_t offset);
  // SSE3 instructions
  void movddup(XMMRegister dst, Operand src);
  void movddup(XMMRegister dst, XMMRegister src) { movddup(dst, Operand(src)); }
  void movshdup(XMMRegister dst, XMMRegister src);

  // Use SSE4_1 encoding for pextrw reg, xmm, imm8 for consistency
  void pextrw(Register dst, XMMRegister src, uint8_t offset) {
    pextrw(Operand(dst), src, offset);
  }
  void pextrw(Operand dst, XMMRegister src, uint8_t offset);
  void pextrd(Register dst, XMMRegister src, uint8_t offset) {
    pextrd(Operand(dst), src, offset);
  }
  void pextrd(Operand dst, XMMRegister src, uint8_t offset);

  void insertps(XMMRegister dst, XMMRegister src, uint8_t offset) {
    insertps(dst, Operand(src), offset);
  }
  void insertps(XMMRegister dst, Operand src, uint8_t offset);
  void pinsrb(XMMRegister dst, Register src, uint8_t offset) {
    pinsrb(dst, Operand(src), offset);
  }
  void pinsrb(XMMRegister dst, Operand src, uint8_t offset);
  void pinsrw(XMMRegister dst, Register src, uint8_t offset) {
    pinsrw(dst, Operand(src), offset);
  }
  void pinsrw(XMMRegister dst, Operand src, uint8_t offset);
  void pinsrd(XMMRegister dst, Register src, uint8_t offset) {
    pinsrd(dst, Operand(src), offset);
  }
  void pinsrd(XMMRegister dst, Operand src, uint8_t offset);

  void roundps(XMMRegister dst, XMMRegister src, RoundingMode mode);
  void roundpd(XMMRegister dst, XMMRegister src, RoundingMode mode);

  // AVX instructions
  void vaddss(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vaddss(dst, src1, Operand(src2));
  }
  void vaddss(XMMRegister dst, XMMRegister src1, Operand src2) {
    vss(0x58, dst, src1, src2);
  }
  void vsubss(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vsubss(dst, src1, Operand(src2));
  }
  void vsubss(XMMRegister dst, XMMRegister src1, Operand src2) {
    vss(0x5c, dst, src1, src2);
  }
  void vmulss(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vmulss(dst, src1, Operand(src2));
  }
  void vmulss(XMMRegister dst, XMMRegister src1, Operand src2) {
    vss(0x59, dst, src1, src2);
  }
  void vdivss(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vdivss(dst, src1, Operand(src2));
  }
  void vdivss(XMMRegister dst, XMMRegister src1, Operand src2) {
    vss(0x5e, dst, src1, src2);
  }
  void vmaxss(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vmaxss(dst, src1, Operand(src2));
  }
  void vmaxss(XMMRegister dst, XMMRegister src1, Operand src2) {
    vss(0x5f, dst, src1, src2);
  }
  void vminss(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vminss(dst, src1, Operand(src2));
  }
  void vminss(XMMRegister dst, XMMRegister src1, Operand src2) {
    vss(0x5d, dst, src1, src2);
  }
  void vsqrtss(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vsqrtss(dst, src1, Operand(src2));
  }
  void vsqrtss(XMMRegister dst, XMMRegister src1, Operand src2) {
    vss(0x51, dst, src1, src2);
  }
  void vss(uint8_t op, XMMRegister dst, XMMRegister src1, Operand src2);

  void vhaddps(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vhaddps(dst, src1, Operand(src2));
  }
  void vhaddps(XMMRegister dst, XMMRegister src1, Operand src2) {
    vinstr(0x7C, dst, src1, src2, kF2, k0F, kWIG);
  }
  void vsqrtpd(XMMRegister dst, XMMRegister src) { vsqrtpd(dst, Operand(src)); }
  void vsqrtpd(XMMRegister dst, Operand src) {
    vinstr(0x51, dst, xmm0, src, k66, k0F, kWIG);
  }
  void vmovss(Operand dst, XMMRegister src) {
    vinstr(0x11, src, xmm0, dst, kF3, k0F, kWIG);
  }
  void vmovss(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vinstr(0x10, dst, src1, src2, kF3, k0F, kWIG);
  }
  void vmovss(XMMRegister dst, Operand src) {
    vinstr(0x10, dst, xmm0, src, kF3, k0F, kWIG);
  }
  void vmovsd(Operand dst, XMMRegister src) {
    vinstr(0x11, src, xmm0, dst, kF2, k0F, kWIG);
  }
  void vmovsd(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vinstr(0x10, dst, src1, src2, kF2, k0F, kWIG);
  }
  void vmovsd(XMMRegister dst, Operand src) {
    vinstr(0x10, dst, xmm0, src, kF2, k0F, kWIG);
  }

  void vextractps(Operand dst, XMMRegister src, uint8_t imm8);

  void vpcmpgtq(XMMRegister dst, XMMRegister src1, XMMRegister src2);

  void vmovaps(XMMRegister dst, XMMRegister src) { vmovaps(dst, Operand(src)); }
  void vmovaps(XMMRegister dst, Operand src) { vps(0x28, dst, xmm0, src); }
  void vmovapd(XMMRegister dst, XMMRegister src) { vmovapd(dst, Operand(src)); }
  void vmovapd(XMMRegister dst, Operand src) { vpd(0x28, dst, xmm0, src); }
  void vmovups(Operand dst, XMMRegister src) { vps(0x11, src, xmm0, dst); }
  void vmovups(XMMRegister dst, XMMRegister src) { vmovups(dst, Operand(src)); }
  void vmovups(XMMRegister dst, Operand src) { vps(0x10, dst, xmm0, src); }
  void vmovupd(XMMRegister dst, Operand src) { vpd(0x10, dst, xmm0, src); }
  void vshufps(XMMRegister dst, XMMRegister src1, XMMRegister src2,
               uint8_t imm8) {
    vshufps(dst, src1, Operand(src2), imm8);
  }
  void vshufps(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t imm8);
  void vshufpd(XMMRegister dst, XMMRegister src1, XMMRegister src2,
               uint8_t imm8) {
    vshufpd(dst, src1, Operand(src2), imm8);
  }
  void vshufpd(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t imm8);

  void vmovhlps(XMMRegister dst, XMMRegister src1, XMMRegister src2);
  void vmovlhps(XMMRegister dst, XMMRegister src1, XMMRegister src2);
  void vmovlps(XMMRegister dst, XMMRegister src1, Operand src2);
  void vmovlps(Operand dst, XMMRegister src);
  void vmovhps(XMMRegister dst, XMMRegister src1, Operand src2);
  void vmovhps(Operand dst, XMMRegister src);

  void vpsllw(XMMRegister dst, XMMRegister src, uint8_t imm8);
  void vpslld(XMMRegister dst, XMMRegister src, uint8_t imm8);
  void vpsllq(XMMRegister dst, XMMRegister src, uint8_t imm8);
  void vpsrlw(XMMRegister dst, XMMRegister src, uint8_t imm8);
  void vpsrld(XMMRegister dst, XMMRegister src, uint8_t imm8);
  void vpsraw(XMMRegister dst, XMMRegister src, uint8_t imm8);
  void vpsrad(XMMRegister dst, XMMRegister src, uint8_t imm8);
  void vpsrlq(XMMRegister dst, XMMRegister src, uint8_t imm8);

  void vpshufhw(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
    vpshufhw(dst, Operand(src), shuffle);
  }
  void vpshufhw(XMMRegister dst, Operand src, uint8_t shuffle);
  void vpshuflw(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
    vpshuflw(dst, Operand(src), shuffle);
  }
  void vpshuflw(XMMRegister dst, Operand src, uint8_t shuffle);
  void vpshufd(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
    vpshufd(dst, Operand(src), shuffle);
  }
  void vpshufd(XMMRegister dst, Operand src, uint8_t shuffle);

  void vblendvps(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister mask);
  void vblendvpd(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister mask);
  void vpblendvb(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister mask);

  void vpblendw(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                uint8_t mask) {
    vpblendw(dst, src1, Operand(src2), mask);
  }
  void vpblendw(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t mask);

  void vpalignr(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                uint8_t mask) {
    vpalignr(dst, src1, Operand(src2), mask);
  }
  void vpalignr(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t mask);

  void vpextrb(Register dst, XMMRegister src, uint8_t offset) {
    vpextrb(Operand(dst), src, offset);
  }
  void vpextrb(Operand dst, XMMRegister src, uint8_t offset);
  void vpextrw(Register dst, XMMRegister src, uint8_t offset) {
    vpextrw(Operand(dst), src, offset);
  }
  void vpextrw(Operand dst, XMMRegister src, uint8_t offset);
  void vpextrd(Register dst, XMMRegister src, uint8_t offset) {
    vpextrd(Operand(dst), src, offset);
  }
  void vpextrd(Operand dst, XMMRegister src, uint8_t offset);

  void vinsertps(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                 uint8_t offset) {
    vinsertps(dst, src1, Operand(src2), offset);
  }
  void vinsertps(XMMRegister dst, XMMRegister src1, Operand src2,
                 uint8_t offset);
  void vpinsrb(XMMRegister dst, XMMRegister src1, Register src2,
               uint8_t offset) {
    vpinsrb(dst, src1, Operand(src2), offset);
  }
  void vpinsrb(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t offset);
  void vpinsrw(XMMRegister dst, XMMRegister src1, Register src2,
               uint8_t offset) {
    vpinsrw(dst, src1, Operand(src2), offset);
  }
  void vpinsrw(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t offset);
  void vpinsrd(XMMRegister dst, XMMRegister src1, Register src2,
               uint8_t offset) {
    vpinsrd(dst, src1, Operand(src2), offset);
  }
  void vpinsrd(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t offset);

  void vroundsd(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                RoundingMode mode);
  void vroundss(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                RoundingMode mode);
  void vroundps(XMMRegister dst, XMMRegister src, RoundingMode mode);
  void vroundpd(XMMRegister dst, XMMRegister src, RoundingMode mode);

  void vcvtdq2pd(XMMRegister dst, XMMRegister src) {
    vinstr(0xE6, dst, xmm0, src, kF3, k0F, kWIG);
  }
  void vcvtpd2ps(XMMRegister dst, XMMRegister src) {
    vinstr(0x5A, dst, xmm0, src, k66, k0F, kWIG);
  }
  void vcvttps2dq(XMMRegister dst, XMMRegister src) {
    vcvttps2dq(dst, Operand(src));
  }
  void vcvttps2dq(XMMRegister dst, Operand src) {
    vinstr(0x5B, dst, xmm0, src, kF3, k0F, kWIG);
  }
  void vcvttpd2dq(XMMRegister dst, XMMRegister src) {
    vinstr(0xE6, dst, xmm0, src, k66, k0F, kWIG);
  }
  void vcvttsd2si(Register dst, XMMRegister src) {
    XMMRegister idst = XMMRegister::from_code(dst.code());
    vinstr(0x2c, idst, xmm0, src, kF2, k0F, kW0);
  }
  void vcvttsd2si(Register dst, Operand src) {
    XMMRegister idst = XMMRegister::from_code(dst.code());
    vinstr(0x2c, idst, xmm0, src, kF2, k0F, kW0);
  }
  void vcvtss2sd(XMMRegister dst, XMMRegister src1, XMMRegister src2) {
    vinstr(0x5a, dst, src1, src2, kF3, k0F, kWIG);
  }
  void vcvtss2sd(XMMRegister dst, XMMRegister src1, Operand src2) {
    vinstr(0x5a, dst, src1, src2, kF3, k0F, kWIG);
  }
  void vcvttss2si(Register dst, XMMRegister src) {
    XMMRegister idst = XMMRegister::from_code(dst.code());
    vinstr(0x2c, idst, xmm0, src, kF3, k0F, kW0);
  }
  void vcvttss2si(Register dst, Operand src) {
    XMMRegister idst = XMMRegister::from_code(dst.code());
    vinstr(0x2c, idst, xmm0, src, kF3, k0F, kW0);
  }

  void vmovddup(XMMRegister dst, Operand src) {
    vinstr(0x12, dst, xmm0, src, kF2, k0F, kWIG);
  }
  void vmovddup(XMMRegister dst, XMMRegister src) {
    vmovddup(dst, Operand(src));
  }
  void vmovshdup(XMMRegister dst, XMMRegister src) {
    vinstr(0x16, dst, xmm0, src, kF3, k0F, kWIG);
  }
  void vbroadcastss(XMMRegister dst, XMMRegister src) {
    vinstr(0x18, dst, xmm0, src, k66, k0F38, kW0, AVX2);
  }
  void vbroadcastss(XMMRegister dst, Operand src) {
    vinstr(0x18, dst, xmm0, src, k66, k0F38, kW0);
  }
  void vmovdqa(XMMRegister dst, Operand src) {
    vinstr(0x6F, dst, xmm0, src, k66, k0F, kWIG);
  }
  void vmovdqa(XMMRegister dst, XMMRegister src) {
    vinstr(0x6F, dst, xmm0, src, k66, k0F, kWIG);
  }
  void vmovdqu(XMMRegister dst, Operand src) {
    vinstr(0x6F, dst, xmm0, src, kF3, k0F, kWIG);
  }
  void vmovdqu(Operand dst, XMMRegister src) {
    vinstr(0x7F, src, xmm0, dst, kF3, k0F, kWIG);
  }
  void vmovd(XMMRegister dst, Register src) { vmovd(dst, Operand(src)); }
  void vmovd(XMMRegister dst, Operand src) {
    vinstr(0x6E, dst, xmm0, src, k66, k0F, kWIG);
  }
  void vmovd(Register dst, XMMRegister src) { movd(Operand(dst), src); }
  void vmovd(Operand dst, XMMRegister src) {
    vinstr(0x7E, src, xmm0, dst, k66, k0F, kWIG);
  }

  void vmovmskpd(Register dst, XMMRegister src);
  void vmovmskps(Register dst, XMMRegister src);

  void vpmovmskb(Register dst, XMMRegister src);

  void vucomisd(XMMRegister dst, XMMRegister src) {
    vinstr(0x2E, dst, xmm0, src, k66, k0F, kWIG);
  }
  void vucomisd(XMMRegister dst, Operand src) {
    vinstr(0x2E, dst, xmm0, src, k66, k0F, kWIG);
  }
  void vucomiss(XMMRegister dst, XMMRegister src) {
    vinstr(0x2E, dst, xmm0, src, kNoPrefix, k0F, kWIG);
  }
  void vucomiss(XMMRegister dst, Operand src) {
    vinstr(0x2E, dst, xmm0, src, kNoPrefix, k0F, kWIG);
  }

  // BMI instruction
  void andn(Register dst, Register src1, Register src2) {
    andn(dst, src1, Operand(src2));
  }
  void andn(Register dst, Register src1, Operand src2) {
    bmi1(0xf2, dst, src1, src2);
  }
  void bextr(Register dst, Register src1, Register src2) {
    bextr(dst, Operand(src1), src2);
  }
  void bextr(Register dst, Operand src1, Register src2) {
    bmi1(0xf7, dst, src2, src1);
  }
  void blsi(Register dst, Register src) { blsi(dst, Operand(src)); }
  void blsi(Register dst, Operand src) { bmi1(0xf3, ebx, dst, src); }
  void blsmsk(Register dst, Register src) { blsmsk(dst, Operand(src)); }
  void blsmsk(Register dst, Operand src) { bmi1(0xf3, edx, dst, src); }
  void blsr(Register dst, Register src) { blsr(dst, Operand(src)); }
  void blsr(Register dst, Operand src) { bmi1(0xf3, ecx, dst, src); }
  void tzcnt(Register dst, Register src) { tzcnt(dst, Operand(src)); }
  void tzcnt(Register dst, Operand src);

  void lzcnt(Register dst, Register src) { lzcnt(dst, Operand(src)); }
  void lzcnt(Register dst, Operand src);

  void popcnt(Register dst, Register src) { popcnt(dst, Operand(src)); }
  void popcnt(Register dst, Operand src);

  void bzhi(Register dst, Register src1, Register src2) {
    bzhi(dst, Operand(src1), src2);
  }
  void bzhi(Register dst, Operand src1, Register src2) {
    bmi2(kNoPrefix, 0xf5, dst, src2, src1);
  }
  void mulx(Register dst1, Register dst2, Register src) {
    mulx(dst1, dst2, Operand(src));
  }
  void mulx(Register dst1, Register dst2, Operand src) {
    bmi2(kF2, 0xf6, dst1, dst2, src);
  }
  void pdep(Register dst, Register src1, Register src2) {
    pdep(dst, src1, Operand(src2));
  }
  void pdep(Register dst, Register src1, Operand src2) {
    bmi2(kF2, 0xf5, dst, src1, src2);
  }
  void pext(Register dst, Register src1, Register src2) {
    pext(dst, src1, Operand(src2));
  }
  void pext(Register dst, Register src1, Operand src2) {
    bmi2(kF3, 0xf5, dst, src1, src2);
  }
  void sarx(Register dst, Register src1, Register src2) {
    sarx(dst, Operand(src1), src2);
  }
  void sarx(Register dst, Operand src1, Register src2) {
    bmi2(kF3, 0xf7, dst, src2, src1);
  }
  void shlx(Register dst, Register src1, Register src2) {
    shlx(dst, Operand(src1), src2);
  }
  void shlx(Register dst, Operand src1, Register src2) {
    bmi2(k66, 0xf7, dst, src2, src1);
  }
  void shrx(Register dst, Register src1, Register src2) {
    shrx(dst, Operand(src1), src2);
  }
  void shrx(Register dst, Operand src1, Register src2) {
    bmi2(kF2, 0xf7, dst, src2, src1);
  }
  void rorx(Register dst, Register src, uint8_t imm8) {
    rorx(dst, Operand(src), imm8);
  }
  void rorx(Register dst, Operand src, uint8_t imm8);

  // Implementation of packed single-precision floating-point SSE instructions.
  void ps(uint8_t op, XMMRegister dst, Operand src);
  // Implementation of packed double-precision floating-point SSE instructions.
  void pd(uint8_t op, XMMRegister dst, Operand src);

#define PACKED_OP_LIST(V) \
  V(unpckl, 0x14)         \
  V(and, 0x54)            \
  V(andn, 0x55)           \
  V(or, 0x56)             \
  V(xor, 0x57)            \
  V(add, 0x58)            \
  V(mul, 0x59)            \
  V(sub, 0x5c)            \
  V(min, 0x5d)            \
  V(div, 0x5e)            \
  V(max, 0x5f)

#define SSE_PACKED_OP_DECLARE(name, opcode)                             \
  void name##ps(XMMRegister dst, XMMRegister src) {                     \
    ps(opcode, dst, Operand(src));                                      \
  }                                                                     \
  void name##ps(XMMRegister dst, Operand src) { ps(opcode, dst, src); } \
  void name##pd(XMMRegister dst, XMMRegister src) {                     \
    pd(opcode, dst, Operand(src));                                      \
  }                                                                     \
  void name##pd(XMMRegister dst, Operand src) { pd(opcode, dst, src); }

  PACKED_OP_LIST(SSE_PACKED_OP_DECLARE)
#undef SSE_PACKED_OP_DECLARE

#define AVX_PACKED_OP_DECLARE(name, opcode)                               \
  void v##name##ps(XMMRegister dst, XMMRegister src1, XMMRegister src2) { \
    vps(opcode, dst, src1, Operand(src2));                                \
  }                                                                       \
  void v##name##ps(XMMRegister dst, XMMRegister src1, Operand src2) {     \
    vps(opcode, dst, src1, src2);                                         \
  }                                                                       \
  void v##name##pd(XMMRegister dst, XMMRegister src1, XMMRegister src2) { \
    vpd(opcode, dst, src1, Operand(src2));                                \
  }                                                                       \
  void v##name##pd(XMMRegister dst, XMMRegister src1, Operand src2) {     \
    vpd(opcode, dst, src1, src2);                                         \
  }

  PACKED_OP_LIST(AVX_PACKED_OP_DECLARE)
#undef AVX_PACKED_OP_DECLARE
#undef PACKED_OP_LIST

  void vps(uint8_t op, XMMRegister dst, XMMRegister src1, Operand src2);
  void vpd(uint8_t op, XMMRegister dst, XMMRegister src1, Operand src2);

  void vcmpps(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t cmp);
  void vcmppd(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t cmp);

#define AVX_CMP_P(instr, imm8)                                             \
  void v##instr##ps(XMMRegister dst, XMMRegister src1, XMMRegister src2) { \
    vcmpps(dst, src1, Operand(src2), imm8);                                \
  }                                                                        \
  void v##instr##ps(XMMRegister dst, XMMRegister src1, Operand src2) {     \
    vcmpps(dst, src1, src2, imm8);                                         \
  }                                                                        \
  void v##instr##pd(XMMRegister dst, XMMRegister src1, XMMRegister src2) { \
    vcmppd(dst, src1, Operand(src2), imm8);                                \
  }                                                                        \
  void v##instr##pd(XMMRegister dst, XMMRegister src1, Operand src2) {     \
    vcmppd(dst, src1, src2, imm8);                                         \
  }

  PACKED_CMP_LIST(AVX_CMP_P)
  // vcmpgeps/vcmpgepd only in AVX.
  AVX_CMP_P(cmpge, 0xd)
#undef AVX_CMP_P
#undef PACKED_CMP_LIST

// Other SSE and AVX instructions
#define DECLARE_SSE_UNOP_AND_AVX(instruction, escape, opcode)       \
  void instruction(XMMRegister dst, XMMRegister src) {              \
    instruction(dst, Operand(src));                                 \
  }                                                                 \
  void instruction(XMMRegister dst, Operand src) {                  \
    sse_instr(dst, src, 0x##escape, 0x##opcode);                    \
  }                                                                 \
  void v##instruction(XMMRegister dst, XMMRegister src) {           \
    v##instruction(dst, Operand(src));                              \
  }                                                                 \
  void v##instruction(XMMRegister dst, Operand src) {               \
    vinstr(0x##opcode, dst, xmm0, src, kNoPrefix, k##escape, kWIG); \
  }

  SSE_UNOP_INSTRUCTION_LIST(DECLARE_SSE_UNOP_AND_AVX)
#undef DECLARE_SSE_UNOP_AND_AVX

#define DECLARE_SSE2_INSTRUCTION(instruction, prefix, escape, opcode) \
  void instruction(XMMRegister dst, XMMRegister src) {                \
    instruction(dst, Operand(src));                                   \
  }                                                                   \
  void instruction(XMMRegister dst, Operand src) {                    \
    sse2_instr(dst, src, 0x##prefix, 0x##escape, 0x##opcode);         \
  }

  SSE2_INSTRUCTION_LIST(DECLARE_SSE2_INSTRUCTION)
  SSE2_INSTRUCTION_LIST_SD(DECLARE_SSE2_INSTRUCTION)
#undef DECLARE_SSE2_INSTRUCTION

#define DECLARE_SSE2_AVX_INSTRUCTION(instruction, prefix, escape, opcode)    \
  void v##instruction(XMMRegister dst, XMMRegister src1, XMMRegister src2) { \
    v##instruction(dst, src1, Operand(src2));                                \
  }                                                                          \
  void v##instruction(XMMRegister dst, XMMRegister src1, Operand src2) {     \
    vinstr(0x##opcode, dst, src1, src2, k##prefix, k##escape, kW0);          \
  }

  SSE2_INSTRUCTION_LIST(DECLARE_SSE2_AVX_INSTRUCTION)
  SSE2_INSTRUCTION_LIST_SD(DECLARE_SSE2_AVX_INSTRUCTION)
#undef DECLARE_SSE2_AVX_INSTRUCTION

#define DECLARE_SSSE3_INSTRUCTION(instruction, prefix, escape1, escape2,     \
                                  opcode)                                    \
  void instruction(XMMRegister dst, XMMRegister src) {                       \
    instruction(dst, Operand(src));                                          \
  }                                                                          \
  void instruction(XMMRegister dst, Operand src) {                           \
    ssse3_instr(dst, src, 0x##prefix, 0x##escape1, 0x##escape2, 0x##opcode); \
  }

  SSSE3_INSTRUCTION_LIST(DECLARE_SSSE3_INSTRUCTION)
  SSSE3_UNOP_INSTRUCTION_LIST(DECLARE_SSSE3_INSTRUCTION)
#undef DECLARE_SSSE3_INSTRUCTION

#define DECLARE_SSE4_INSTRUCTION(instruction, prefix, escape1, escape2,     \
                                 opcode)                                    \
  void instruction(XMMRegister dst, XMMRegister src) {                      \
    instruction(dst, Operand(src));                                         \
  }                                                                         \
  void instruction(XMMRegister dst, Operand src) {                          \
    sse4_instr(dst, src, 0x##prefix, 0x##escape1, 0x##escape2, 0x##opcode); \
  }

  SSE4_INSTRUCTION_LIST(DECLARE_SSE4_INSTRUCTION)
  SSE4_RM_INSTRUCTION_LIST(DECLARE_SSE4_INSTRUCTION)
  DECLARE_SSE4_INSTRUCTION(blendvps, 66, 0F, 38, 14)
  DECLARE_SSE4_INSTRUCTION(blendvpd, 66, 0F, 38, 15)
  DECLARE_SSE4_INSTRUCTION(pblendvb, 66, 0F, 38, 10)
#undef DECLARE_SSE4_INSTRUCTION

#define DECLARE_SSE34_AVX_INSTRUCTION(instruction, prefix, escape1, escape2,  \
                                      opcode)                                 \
  void v##instruction(XMMRegister dst, XMMRegister src1, XMMRegister src2) {  \
    v##instruction(dst, src1, Operand(src2));                                 \
  }                                                                           \
  void v##instruction(XMMRegister dst, XMMRegister src1, Operand src2) {      \
    vinstr(0x##opcode, dst, src1, src2, k##prefix, k##escape1##escape2, kW0); \
  }

  SSSE3_INSTRUCTION_LIST(DECLARE_SSE34_AVX_INSTRUCTION)
  SSE4_INSTRUCTION_LIST(DECLARE_SSE34_AVX_INSTRUCTION)
#undef DECLARE_SSE34_AVX_INSTRUCTION

#define DECLARE_SSE4_AVX_RM_INSTRUCTION(instruction, prefix, escape1, escape2, \
                                        opcode)                                \
  void v##instruction(XMMRegister dst, XMMRegister src) {                      \
    v##instruction(dst, Operand(src));                                         \
  }                                                                            \
  void v##instruction(XMMRegister dst, Operand src) {                          \
    vinstr(0x##opcode, dst, xmm0, src, k##prefix, k##escape1##escape2, kW0);   \
  }

  SSSE3_UNOP_INSTRUCTION_LIST(DECLARE_SSE4_AVX_RM_INSTRUCTION)
  SSE4_RM_INSTRUCTION_LIST(DECLARE_SSE4_AVX_RM_INSTRUCTION)
#undef DECLARE_SSE4_AVX_RM_INSTRUCTION

  // AVX2 instructions
#define AVX2_INSTRUCTION(instr, prefix, escape1, escape2, opcode)           \
  void instr(XMMRegister dst, XMMRegister src) {                            \
    vinstr(0x##opcode, dst, xmm0, src, k##prefix, k##escape1##escape2, kW0, \
           AVX2);                                                           \
  }                                                                         \
  void instr(XMMRegister dst, Operand src) {                                \
    vinstr(0x##opcode, dst, xmm0, src, k##prefix, k##escape1##escape2, kW0, \
           AVX2);                                                           \
  }
  AVX2_BROADCAST_LIST(AVX2_INSTRUCTION)
#undef AVX2_INSTRUCTION

#define FMA(instr, length, prefix, escape1, escape2, extension, opcode) \
  void instr(XMMRegister dst, XMMRegister src1, XMMRegister src2) {     \
    vinstr(0x##opcode, dst, src1, src2, k##length, k##prefix,           \
           k##escape1##escape2, k##extension, FMA3);                    \
  }                                                                     \
  void instr(XMMRegister dst, XMMRegister src1, Operand src2) {         \
    vinstr(0x##opcode, dst, src1, src2, k##length, k##prefix,           \
           k##escape1##escape2, k##extension, FMA3);                    \
  }
  FMA_INSTRUCTION_LIST(FMA)
#undef FMA

  // Prefetch src position into cache level.
  // Level 1, 2 or 3 specifies CPU cache level. Level 0 specifies a
  // non-temporal
  void prefetch(Operand src, int level);
  // TODO(lrn): Need SFENCE for movnt?

  // Check the code size generated from label to here.
  int SizeOfCodeGeneratedSince(Label* label) {
    return pc_offset() - label->pos();
  }

  // Record a deoptimization reason that can be used by a log or cpu profiler.
  // Use --trace-deopt to enable.
  void RecordDeoptReason(DeoptimizeReason reason, uint32_t node_id,
                         SourcePosition position, int id);

  // Writes a single byte or word of data in the code stream.  Used for
  // inline tables, e.g., jump-tables.
  void db(uint8_t data);
  void dd(uint32_t data);
  void dq(uint64_t data);
  void dp(uintptr_t data) { dd(data); }
  void dd(Label* label);

  // Check if there is less than kGap bytes available in the buffer.
  // If this is the case, we need to grow the buffer before emitting
  // an instruction or relocation information.
  inline bool buffer_overflow() const {
    return pc_ >= reloc_info_writer.pos() - kGap;
  }

  // Get the number of bytes available in the buffer.
  inline int available_space() const { return reloc_info_writer.pos() - pc_; }

  static bool IsNop(Address addr);

  int relocation_writer_size() {
    return (buffer_start_ + buffer_->size()) - reloc_info_writer.pos();
  }

  // Avoid overflows for displacements etc.
  static constexpr int kMaximalBufferSize = 512 * MB;

  uint8_t byte_at(int pos) { return buffer_start_[pos]; }
  void set_byte_at(int pos, uint8_t value) { buffer_start_[pos] = value; }

 protected:
  void emit_sse_operand(XMMRegister reg, Operand adr);
  void emit_sse_operand(XMMRegister dst, XMMRegister src);
  void emit_sse_operand(Register dst, XMMRegister src);
  void emit_sse_operand(XMMRegister dst, Register src);

  Address addr_at(int pos) {
    DCHECK_GE(pos, 0);
    DCHECK_LT(pos, pc_offset());
    return reinterpret_cast<Address>(buffer_start_ + pos);
  }

 private:
  uint32_t long_at(int pos) {
    return ReadUnalignedValue<uint32_t>(addr_at(pos));
  }
  void long_at_put(int pos, uint32_t x) {
    WriteUnalignedValue(addr_at(pos), x);
  }

  // code emission
  void GrowBuffer();
  inline void emit(uint32_t x);
  inline void emit(Handle<HeapObject> handle);
  inline void emit(uint32_t x, RelocInfo::Mode rmode);
  inline void emit(Handle<Code> code, RelocInfo::Mode rmode);
  inline void emit(const Immediate& x);
  inline void emit_b(Immediate x);
  inline void emit_w(const Immediate& x);
  inline void emit_q(uint64_t x);

  // Emit the code-object-relative offset of the label's position
  inline void emit_code_relative_offset(Label* label);

  // instruction generation
  void emit_arith_b(int op1, int op2, Register dst, int imm8);

  // Emit a basic arithmetic instruction (i.e. first byte of the family is 0x81)
  // with a given destination expression and an immediate operand.  It attempts
  // to use the shortest encoding possible.
  // sel specifies the /n in the modrm byte (see the Intel PRM).
  void emit_arith(int sel, Operand dst, const Immediate& x);

  void emit_operand(int code, Operand adr);
  void emit_operand(Register reg, Operand adr);
  void emit_operand(XMMRegister reg, Operand adr);

  void emit_label(Label* label);

  void emit_farith(int b1, int b2, int i);

  // Emit vex prefix
  enum SIMDPrefix { kNoPrefix = 0x0, k66 = 0x1, kF3 = 0x2, kF2 = 0x3 };
  enum VectorLength { kL128 = 0x0, kL256 = 0x4, kLIG = kL128, kLZ = kL128 };
  enum VexW { kW0 = 0x0, kW1 = 0x80, kWIG = kW0 };
  enum LeadingOpcode { k0F = 0x1, k0F38 = 0x2, k0F3A = 0x3 };
  inline void emit_vex_prefix(XMMRegister v, VectorLength l, SIMDPrefix pp,
                              LeadingOpcode m, VexW w);
  inline void emit_vex_prefix(Register v, VectorLength l, SIMDPrefix pp,
                              LeadingOpcode m, VexW w);

  // labels
  void print(const Label* L);
  void bind_to(Label* L, int pos);

  // displacements
  inline Displacement disp_at(Label* L);
  inline void disp_at_put(Label* L, Displacement disp);
  inline void emit_disp(Label* L, Displacement::Type type);
  inline void emit_near_disp(Label* L);

  void sse_instr(XMMRegister dst, Operand src, uint8_t prefix, uint8_t opcode);
  void sse2_instr(XMMRegister dst, Operand src, uint8_t prefix, uint8_t escape,
                  uint8_t opcode);
  void ssse3_instr(XMMRegister dst, Operand src, uint8_t prefix,
                   uint8_t escape1, uint8_t escape2, uint8_t opcode);
  void sse4_instr(XMMRegister dst, Operand src, uint8_t prefix, uint8_t escape1,
                  uint8_t escape2, uint8_t opcode);
  void vinstr(uint8_t op, XMMRegister dst, XMMRegister src1, XMMRegister src2,
              SIMDPrefix pp, LeadingOpcode m, VexW w, CpuFeature = AVX);
  void vinstr(uint8_t op, XMMRegister dst, XMMRegister src1, Operand src2,
              SIMDPrefix pp, LeadingOpcode m, VexW w, CpuFeature = AVX);
  void vinstr(uint8_t op, XMMRegister dst, XMMRegister src1, XMMRegister src2,
              VectorLength l, SIMDPrefix pp, LeadingOpcode m, VexW w,
              CpuFeature = AVX);
  void vinstr(uint8_t op, XMMRegister dst, XMMRegister src1, Operand src2,
              VectorLength l, SIMDPrefix pp, LeadingOpcode m, VexW w,
              CpuFeature = AVX);
  // Most BMI instructions are similar.
  void bmi1(uint8_t op, Register reg, Register vreg, Operand rm);
  void bmi2(SIMDPrefix pp, uint8_t op, Register reg, Register vreg, Operand rm);
  void fma_instr(uint8_t op, XMMRegister dst, XMMRegister src1,
                 XMMRegister src2, VectorLength l, SIMDPrefix pp,
                 LeadingOpcode m, VexW w);
  void fma_instr(uint8_t op, XMMRegister dst, XMMRegister src1, Operand src2,
                 VectorLength l, SIMDPrefix pp, LeadingOpcode m, VexW w);

  // record reloc info for current pc_
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);

  // record the position of jmp/jcc instruction
  void record_farjmp_position(Label* L, int pos);

  bool is_optimizable_farjmp(int idx);

  void AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate);

  int WriteCodeComments();

  friend class EnsureSpace;

  // Internal reference positions, required for (potential) patching in
  // GrowBuffer(); contains only those internal references whose labels
  // are already bound.
  std::deque<int> internal_reference_positions_;

  // code generation
  RelocInfoWriter reloc_info_writer;
};

// Helper class that ensures that there is enough space for generating
// instructions and relocation information.  The constructor makes
// sure that there is enough space and (in debug mode) the destructor
// checks that we did not generate too much.
class EnsureSpace {
 public:
  explicit V8_INLINE EnsureSpace(Assembler* assembler) : assembler_(assembler) {
    if (V8_UNLIKELY(assembler_->buffer_overflow())) assembler_->GrowBuffer();
#ifdef DEBUG
    space_before_ = assembler->available_space();
#endif
  }

#ifdef DEBUG
  ~EnsureSpace() {
    int bytes_generated = space_before_ - assembler_->available_space();
    DCHECK(bytes_generated < assembler_->kGap);
  }
#endif

 private:
  Assembler* const assembler_;
#ifdef DEBUG
  int space_before_;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_IA32_ASSEMBLER_IA32_H_
