// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_ASSEMBLER_ARM64_H_
#define V8_CODEGEN_ARM64_ASSEMBLER_ARM64_H_

#include <deque>
#include <map>
#include <memory>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "src/codegen/arm64/constants-arm64.h"
#include "src/codegen/arm64/instructions-arm64.h"
#include "src/codegen/arm64/register-arm64.h"
#include "src/codegen/assembler.h"
#include "src/codegen/constant-pool.h"
#include "src/common/globals.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"

// Windows arm64 SDK defines mvn to NEON intrinsic neon_not which will not
// be used here.
#if defined(V8_OS_WIN) && defined(mvn)
#undef mvn
#endif

#if defined(V8_OS_WIN)
#include "src/diagnostics/unwinding-info-win64.h"
#endif  // V8_OS_WIN

namespace v8 {
namespace internal {

class SafepointTableBuilder;

// -----------------------------------------------------------------------------
// Immediates.
class Immediate {
 public:
  template <typename T>
  inline explicit Immediate(
      Handle<T> handle, RelocInfo::Mode mode = RelocInfo::FULL_EMBEDDED_OBJECT);

  // This is allowed to be an implicit constructor because Immediate is
  // a wrapper class that doesn't normally perform any type conversion.
  template <typename T>
  inline Immediate(T value);  // NOLINT(runtime/explicit)

  template <typename T>
  inline Immediate(T value, RelocInfo::Mode rmode);

  int64_t value() const { return value_; }
  RelocInfo::Mode rmode() const { return rmode_; }

 private:
  int64_t value_;
  RelocInfo::Mode rmode_;
};

// -----------------------------------------------------------------------------
// Operands.
constexpr int kSmiShift = kSmiTagSize + kSmiShiftSize;
constexpr uint64_t kSmiShiftMask = (1ULL << kSmiShift) - 1;

// Represents an operand in a machine instruction.
class Operand {
  // TODO(all): If necessary, study more in details which methods
  // TODO(all): should be inlined or not.
 public:
  // rm, {<shift> {#<shift_amount>}}
  // where <shift> is one of {LSL, LSR, ASR, ROR}.
  //       <shift_amount> is uint6_t.
  // This is allowed to be an implicit constructor because Operand is
  // a wrapper class that doesn't normally perform any type conversion.
  inline Operand(Register reg, Shift shift = LSL,
                 unsigned shift_amount = 0);  // NOLINT(runtime/explicit)

  // rm, <extend> {#<shift_amount>}
  // where <extend> is one of {UXTB, UXTH, UXTW, UXTX, SXTB, SXTH, SXTW, SXTX}.
  //       <shift_amount> is uint2_t.
  inline Operand(Register reg, Extend extend, unsigned shift_amount = 0);

  static Operand EmbeddedNumber(double number);  // Smi or HeapNumber.
  static Operand EmbeddedHeapNumber(double number);

  inline bool IsHeapNumberRequest() const;
  inline HeapNumberRequest heap_number_request() const;
  inline Immediate immediate_for_heap_number_request() const;

  // Implicit constructor for all int types, ExternalReference, and Smi.
  template <typename T>
  inline Operand(T t);  // NOLINT(runtime/explicit)

  // Implicit constructor for int types.
  template <typename T>
  inline Operand(T t, RelocInfo::Mode rmode);

  inline bool IsImmediate() const;
  inline bool IsShiftedRegister() const;
  inline bool IsExtendedRegister() const;
  inline bool IsZero() const;

  // This returns an LSL shift (<= 4) operand as an equivalent extend operand,
  // which helps in the encoding of instructions that use the stack pointer.
  inline Operand ToExtendedRegister() const;

  // Returns new Operand adapted for using with W registers.
  inline Operand ToW() const;

  inline Immediate immediate() const;
  inline int64_t ImmediateValue() const;
  inline RelocInfo::Mode ImmediateRMode() const;
  inline Register reg() const;
  inline Shift shift() const;
  inline Extend extend() const;
  inline unsigned shift_amount() const;

  // Relocation information.
  bool NeedsRelocation(const Assembler* assembler) const;

 private:
  std::optional<HeapNumberRequest> heap_number_request_;
  Immediate immediate_;
  Register reg_;
  Shift shift_;
  Extend extend_;
  unsigned shift_amount_;
};

// MemOperand represents a memory operand in a load or store instruction.
class MemOperand {
 public:
  inline MemOperand();
  inline explicit MemOperand(Register base, int64_t offset = 0,
                             AddrMode addrmode = Offset);
  inline explicit MemOperand(Register base, Register regoffset,
                             Shift shift = LSL, unsigned shift_amount = 0);
  inline explicit MemOperand(Register base, Register regoffset, Extend extend,
                             unsigned shift_amount = 0);
  inline explicit MemOperand(Register base, const Operand& offset,
                             AddrMode addrmode = Offset);

  const Register& base() const { return base_; }
  const Register& regoffset() const { return regoffset_; }
  int64_t offset() const { return offset_; }
  AddrMode addrmode() const { return addrmode_; }
  Shift shift() const { return shift_; }
  Extend extend() const { return extend_; }
  unsigned shift_amount() const { return shift_amount_; }
  inline bool IsImmediateOffset() const;
  inline bool IsRegisterOffset() const;
  inline bool IsPreIndex() const;
  inline bool IsPostIndex() const;

 private:
  Register base_;
  Register regoffset_;
  int64_t offset_;
  AddrMode addrmode_;
  Shift shift_;
  Extend extend_;
  unsigned shift_amount_;
};

class AssemblerZone {
 public:
  explicit AssemblerZone(const MaybeAssemblerZone& zone)
      // Create a fresh Zone unless one is already provided.
      : maybe_local_zone_(
            std::holds_alternative<Zone*>(zone)
                ? std::nullopt
                : std::make_optional<Zone>(std::get<AccountingAllocator*>(zone),
                                           ZONE_NAME)),
        zone_(std::holds_alternative<Zone*>(zone)
                  ? std::get<Zone*>(zone)
                  : &maybe_local_zone_.value()) {}

  Zone* get() const { return zone_; }

 private:
  std::optional<Zone> maybe_local_zone_ = std::nullopt;
  Zone* zone_;
};

// -----------------------------------------------------------------------------
// Assembler.

class V8_EXPORT_PRIVATE Assembler : public AssemblerBase {
 public:
  // Create an assembler. Instructions and relocation information are emitted
  // into a buffer, with the instructions starting from the beginning and the
  // relocation information starting from the end of the buffer. See CodeDesc
  // for a detailed comment on the layout (globals.h).
  //
  // When available, a zone should be provided for the assembler to manage
  // temporary state, as long as the assembler does not outlive it. An
  // AccountingAllocator can be provided instead.
  //
  // If the provided buffer is nullptr, the assembler allocates and grows its
  // own buffer. Otherwise it takes ownership of the provided buffer.
  Assembler(const MaybeAssemblerZone&, const AssemblerOptions&,
            std::unique_ptr<AssemblerBuffer> = {});

  ~Assembler() override;

  Zone* zone() const { return zone_.get(); }

  void AbortedCodeGeneration() override;

  // System functions ---------------------------------------------------------
  // Start generating code from the beginning of the buffer, discarding any code
  // and data that has already been emitted into the buffer.
  //
  // In order to avoid any accidental transfer of state, Reset DCHECKs that the
  // constant pool is not blocked.
  void Reset();

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

  inline void Unreachable();

  // Label --------------------------------------------------------------------
  // Bind a label to the current pc. Note that labels can only be bound once,
  // and if labels are linked to other instructions, they _must_ be bound
  // before they go out of scope.
  void bind(Label* label);

  // RelocInfo and pools ------------------------------------------------------

  // Record relocation information for current pc_.
  enum ConstantPoolMode { NEEDS_POOL_ENTRY, NO_POOL_ENTRY };
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0,
                       ConstantPoolMode constant_pool_mode = NEEDS_POOL_ENTRY);

  // Generate a B immediate instruction with the corresponding relocation info.
  // 'offset' is the immediate to encode in the B instruction (so it is the
  // difference between the target and the PC of the instruction, divided by
  // the instruction size).
  void near_jump(int offset, RelocInfo::Mode rmode);
  // Generate a BL immediate instruction with the corresponding relocation info.
  // As for near_jump, 'offset' is the immediate to encode in the BL
  // instruction.
  void near_call(int offset, RelocInfo::Mode rmode);
  // Generate a BL immediate instruction with the corresponding relocation info
  // for the input HeapNumberRequest.
  void near_call(HeapNumberRequest request);

  // Return the address in the constant pool of the code target address used by
  // the branch/call instruction at pc.
  inline static Address target_pointer_address_at(Address pc);

  // Read/Modify the code target address in the branch/call instruction at pc.
  // The isolate argument is unused (and may be nullptr) when skipping flushing.
  inline static Address target_address_at(Address pc, Address constant_pool);

  // Read/Modify the code target address in the branch/call instruction at pc.
  inline static Tagged_t target_compressed_address_at(Address pc,
                                                      Address constant_pool);
  inline static void set_target_address_at(
      Address pc, Address constant_pool, Address target,
      WritableJitAllocation* jit_allocation,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  inline static void set_target_compressed_address_at(
      Address pc, Address constant_pool, Tagged_t target,
      WritableJitAllocation* jit_allocation,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  // Returns the handle for the code object called at 'pc'.
  // This might need to be temporarily encoded as an offset into code_targets_.
  inline Handle<Code> code_target_object_handle_at(Address pc);
  inline EmbeddedObjectIndex embedded_object_index_referenced_from(Address pc);
  inline void set_embedded_object_index_referenced_from(
      Address p, EmbeddedObjectIndex index);
  // Returns the handle for the heap object referenced at 'pc'.
  inline Handle<HeapObject> target_object_handle_at(Address pc);

  // During code generation builtin targets in PC-relative call/jump
  // instructions are temporarily encoded as builtin ID until the generated
  // code is moved into the code space.
  static inline Builtin target_builtin_at(Address pc);

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
      WritableJitAllocation* jit_allocation = nullptr,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  // This value is used in the serialization process and must be zero for
  // ARM64, as the code target is split across multiple instructions and does
  // not exist separately in the code, so the serializer should not step
  // forwards in memory after a target is resolved and written.
  static constexpr int kSpecialTargetSize = 0;

  // Size of the generated code in bytes
  uint64_t SizeOfGeneratedCode() const {
    DCHECK((pc_ >= buffer_start_) && (pc_ < (buffer_start_ + buffer_->size())));
    return pc_ - buffer_start_;
  }

  // Return the code size generated from label to the current position.
  uint64_t SizeOfCodeGeneratedSince(const Label* label) {
    DCHECK(label->is_bound());
    DCHECK_GE(pc_offset(), label->pos());
    DCHECK_LT(pc_offset(), buffer_->size());
    return pc_offset() - label->pos();
  }

  // Return the number of instructions generated from label to the
  // current position.
  uint64_t InstructionsGeneratedSince(const Label* label) {
    return SizeOfCodeGeneratedSince(label) / kInstrSize;
  }

  static bool IsConstantPoolAt(Instruction* instr);
  static int ConstantPoolSizeAt(Instruction* instr);
  // See Assembler::CheckConstPool for more info.
  void EmitPoolGuard();

  // Prevent veneer pool emission until EndBlockVeneerPool is called.
  // Call to this function can be nested but must be followed by an equal
  // number of calls to EndBlockConstpool.
  void StartBlockVeneerPool();

  // Resume constant pool emission. Need to be called as many time as
  // StartBlockVeneerPool to have an effect.
  void EndBlockVeneerPool();

  bool is_veneer_pool_blocked() const {
    return veneer_pool_blocked_nesting_ > 0;
  }

  // Record a deoptimization reason that can be used by a log or cpu profiler.
  // Use --trace-deopt to enable.
  void RecordDeoptReason(DeoptimizeReason reason, uint32_t node_id,
                         SourcePosition position, int id);

  int buffer_space() const;

  // Record the emission of a constant pool.
  //
  // The emission of constant and veneer pools depends on the size of the code
  // generated and the number of RelocInfo recorded.
  // The Debug mechanism needs to map code offsets between two versions of a
  // function, compiled with and without debugger support (see for example
  // Debug::PrepareForBreakPoints()).
  // Compiling functions with debugger support generates additional code
  // (DebugCodegen::GenerateSlot()). This may affect the emission of the pools
  // and cause the version of the code with debugger support to have pools
  // generated in different places.
  // Recording the position and size of emitted pools allows to correctly
  // compute the offset mappings between the different versions of a function in
  // all situations.
  //
  // The parameter indicates the size of the pool (in bytes), including
  // the marker and branch over the data.
  void RecordConstPool(int size);

  // Instruction set functions ------------------------------------------------

  // Branch / Jump instructions.
  // For branches offsets are scaled, i.e. in instructions not in bytes.
  // Branch to register.
  void br(const Register& xn);

  // Branch-link to register.
  void blr(const Register& xn);

  // Branch to register with return hint.
  void ret(const Register& xn = lr);

  // Unconditional branch to label.
  void b(Label* label);

  // Conditional branch to label.
  void b(Label* label, Condition cond);

  // Unconditional branch to PC offset.
  void b(int imm26);

  // Conditional branch to PC offset.
  void b(int imm19, Condition cond);

  // Branch-link to label / pc offset.
  void bl(Label* label);
  void bl(int imm26);

  // Compare and branch to label / pc offset if zero.
  void cbz(const Register& rt, Label* label);
  void cbz(const Register& rt, int imm19);

  // Compare and branch to label / pc offset if not zero.
  void cbnz(const Register& rt, Label* label);
  void cbnz(const Register& rt, int imm19);

  // Test bit and branch to label / pc offset if zero.
  void tbz(const Register& rt, unsigned bit_pos, Label* label);
  void tbz(const Register& rt, unsigned bit_pos, int imm14);

  // Test bit and branch to label / pc offset if not zero.
  void tbnz(const Register& rt, unsigned bit_pos, Label* label);
  void tbnz(const Register& rt, unsigned bit_pos, int imm14);

  // Address calculation instructions.
  // Calculate a PC-relative address. Unlike for branches the offset in adr is
  // unscaled (i.e. the result can be unaligned).
  void adr(const Register& rd, Label* label);
  void adr(const Register& rd, int imm21);

  // Data Processing instructions.
  // Add.
  void add(const Register& rd, const Register& rn, const Operand& operand);

  // Add and update status flags.
  void adds(const Register& rd, const Register& rn, const Operand& operand);

  // Compare negative.
  void cmn(const Register& rn, const Operand& operand);

  // Subtract.
  void sub(const Register& rd, const Register& rn, const Operand& operand);

  // Subtract and update status flags.
  void subs(const Register& rd, const Register& rn, const Operand& operand);

  // Compare.
  void cmp(const Register& rn, const Operand& operand);

  // Negate.
  void neg(const Register& rd, const Operand& operand);

  // Negate and update status flags.
  void negs(const Register& rd, const Operand& operand);

  // Add with carry bit.
  void adc(const Register& rd, const Register& rn, const Operand& operand);

  // Add with carry bit and update status flags.
  void adcs(const Register& rd, const Register& rn, const Operand& operand);

  // Subtract with carry bit.
  void sbc(const Register& rd, const Register& rn, const Operand& operand);

  // Subtract with carry bit and update status flags.
  void sbcs(const Register& rd, const Register& rn, const Operand& operand);

  // Negate with carry bit.
  void ngc(const Register& rd, const Operand& operand);

  // Negate with carry bit and update status flags.
  void ngcs(const Register& rd, const Operand& operand);

  // Logical instructions.
  // Bitwise and (A & B).
  void and_(const Register& rd, const Register& rn, const Operand& operand);

  // Bitwise and (A & B) and update status flags.
  void ands(const Register& rd, const Register& rn, const Operand& operand);

  // Bit test, and set flags.
  void tst(const Register& rn, const Operand& operand);

  // Bit clear (A & ~B).
  void bic(const Register& rd, const Register& rn, const Operand& operand);

  // Bit clear (A & ~B) and update status flags.
  void bics(const Register& rd, const Register& rn, const Operand& operand);

  // Bitwise and.
  void and_(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Bit clear immediate.
  void bic(const VRegister& vd, const int imm8, const int left_shift = 0);

  // Bit clear.
  void bic(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Bitwise insert if false.
  void bif(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Bitwise insert if true.
  void bit(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Bitwise select.
  void bsl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Polynomial multiply.
  void pmul(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Vector move immediate.
  void movi(const VRegister& vd, const uint64_t imm, Shift shift = LSL,
            const int shift_amount = 0);

  // Bitwise not.
  void mvn(const VRegister& vd, const VRegister& vn);

  // Vector move inverted immediate.
  void mvni(const VRegister& vd, const int imm8, Shift shift = LSL,
            const int shift_amount = 0);

  // Signed saturating accumulate of unsigned value.
  void suqadd(const VRegister& vd, const VRegister& vn);

  // Unsigned saturating accumulate of signed value.
  void usqadd(const VRegister& vd, const VRegister& vn);

  // Absolute value.
  void abs(const VRegister& vd, const VRegister& vn);

  // Signed saturating absolute value.
  void sqabs(const VRegister& vd, const VRegister& vn);

  // Negate.
  void neg(const VRegister& vd, const VRegister& vn);

  // Signed saturating negate.
  void sqneg(const VRegister& vd, const VRegister& vn);

  // Bitwise not.
  void not_(const VRegister& vd, const VRegister& vn);

  // Extract narrow.
  void xtn(const VRegister& vd, const VRegister& vn);

  // Extract narrow (second part).
  void xtn2(const VRegister& vd, const VRegister& vn);

  // Signed saturating extract narrow.
  void sqxtn(const VRegister& vd, const VRegister& vn);

  // Signed saturating extract narrow (second part).
  void sqxtn2(const VRegister& vd, const VRegister& vn);

  // Unsigned saturating extract narrow.
  void uqxtn(const VRegister& vd, const VRegister& vn);

  // Unsigned saturating extract narrow (second part).
  void uqxtn2(const VRegister& vd, const VRegister& vn);

  // Signed saturating extract unsigned narrow.
  void sqxtun(const VRegister& vd, const VRegister& vn);

  // Signed saturating extract unsigned narrow (second part).
  void sqxtun2(const VRegister& vd, const VRegister& vn);

  // Move register to register.
  void mov(const VRegister& vd, const VRegister& vn);

  // Bitwise not or.
  void orn(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Bitwise exclusive or.
  void eor(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Bitwise or (A | B).
  void orr(const Register& rd, const Register& rn, const Operand& operand);

  // Bitwise or.
  void orr(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Bitwise or immediate.
  void orr(const VRegister& vd, const int imm8, const int left_shift = 0);

  // Bitwise nor (A | ~B).
  void orn(const Register& rd, const Register& rn, const Operand& operand);

  // Bitwise eor/xor (A ^ B).
  void eor(const Register& rd, const Register& rn, const Operand& operand);

  // Bitwise enor/xnor (A ^ ~B).
  void eon(const Register& rd, const Register& rn, const Operand& operand);

  // Logical shift left variable.
  void lslv(const Register& rd, const Register& rn, const Register& rm);

  // Logical shift right variable.
  void lsrv(const Register& rd, const Register& rn, const Register& rm);

  // Arithmetic shift right variable.
  void asrv(const Register& rd, const Register& rn, const Register& rm);

  // Rotate right variable.
  void rorv(const Register& rd, const Register& rn, const Register& rm);

  // Bitfield instructions.
  // Bitfield move.
  void bfm(const Register& rd, const Register& rn, int immr, int imms);

  // Signed bitfield move.
  void sbfm(const Register& rd, const Register& rn, int immr, int imms);

  // Unsigned bitfield move.
  void ubfm(const Register& rd, const Register& rn, int immr, int imms);

  // Bfm aliases.
  // Bitfield insert.
  void bfi(const Register& rd, const Register& rn, int lsb, int width) {
    DCHECK_GE(width, 1);
    DCHECK(lsb + width <= rn.SizeInBits());
    bfm(rd, rn, (rd.SizeInBits() - lsb) & (rd.SizeInBits() - 1), width - 1);
  }

  // Bitfield extract and insert low.
  void bfxil(const Register& rd, const Register& rn, int lsb, int width) {
    DCHECK_GE(width, 1);
    DCHECK(lsb + width <= rn.SizeInBits());
    bfm(rd, rn, lsb, lsb + width - 1);
  }

  // Sbfm aliases.
  // Arithmetic shift right.
  void asr(const Register& rd, const Register& rn, int shift) {
    DCHECK(shift < rd.SizeInBits());
    sbfm(rd, rn, shift, rd.SizeInBits() - 1);
  }

  // Signed bitfield insert in zero.
  void sbfiz(const Register& rd, const Register& rn, int lsb, int width) {
    DCHECK_GE(width, 1);
    DCHECK(lsb + width <= rn.SizeInBits());
    sbfm(rd, rn, (rd.SizeInBits() - lsb) & (rd.SizeInBits() - 1), width - 1);
  }

  // Signed bitfield extract.
  void sbfx(const Register& rd, const Register& rn, int lsb, int width) {
    DCHECK_GE(width, 1);
    DCHECK(lsb + width <= rn.SizeInBits());
    sbfm(rd, rn, lsb, lsb + width - 1);
  }

  // Signed extend byte.
  void sxtb(const Register& rd, const Register& rn) { sbfm(rd, rn, 0, 7); }

  // Signed extend halfword.
  void sxth(const Register& rd, const Register& rn) { sbfm(rd, rn, 0, 15); }

  // Signed extend word.
  void sxtw(const Register& rd, const Register& rn) { sbfm(rd, rn, 0, 31); }

  // Ubfm aliases.
  // Logical shift left.
  void lsl(const Register& rd, const Register& rn, int shift) {
    int reg_size = rd.SizeInBits();
    DCHECK(shift < reg_size);
    ubfm(rd, rn, (reg_size - shift) % reg_size, reg_size - shift - 1);
  }

  // Logical shift right.
  void lsr(const Register& rd, const Register& rn, int shift) {
    DCHECK(shift < rd.SizeInBits());
    ubfm(rd, rn, shift, rd.SizeInBits() - 1);
  }

  // Unsigned bitfield insert in zero.
  void ubfiz(const Register& rd, const Register& rn, int lsb, int width) {
    DCHECK_GE(width, 1);
    DCHECK(lsb + width <= rn.SizeInBits());
    ubfm(rd, rn, (rd.SizeInBits() - lsb) & (rd.SizeInBits() - 1), width - 1);
  }

  // Unsigned bitfield extract.
  void ubfx(const Register& rd, const Register& rn, int lsb, int width) {
    DCHECK_GE(width, 1);
    DCHECK(lsb + width <= rn.SizeInBits());
    ubfm(rd, rn, lsb, lsb + width - 1);
  }

  // Unsigned extend byte.
  void uxtb(const Register& rd, const Register& rn) { ubfm(rd, rn, 0, 7); }

  // Unsigned extend halfword.
  void uxth(const Register& rd, const Register& rn) { ubfm(rd, rn, 0, 15); }

  // Unsigned extend word.
  void uxtw(const Register& rd, const Register& rn) { ubfm(rd, rn, 0, 31); }

  // Extract.
  void extr(const Register& rd, const Register& rn, const Register& rm,
            int lsb);

  // Conditional select: rd = cond ? rn : rm.
  void csel(const Register& rd, const Register& rn, const Register& rm,
            Condition cond);

  // Conditional select increment: rd = cond ? rn : rm + 1.
  void csinc(const Register& rd, const Register& rn, const Register& rm,
             Condition cond);

  // Conditional select inversion: rd = cond ? rn : ~rm.
  void csinv(const Register& rd, const Register& rn, const Register& rm,
             Condition cond);

  // Conditional select negation: rd = cond ? rn : -rm.
  void csneg(const Register& rd, const Register& rn, const Register& rm,
             Condition cond);

  // Conditional set: rd = cond ? 1 : 0.
  void cset(const Register& rd, Condition cond);

  // Conditional set minus: rd = cond ? -1 : 0.
  void csetm(const Register& rd, Condition cond);

  // Conditional increment: rd = cond ? rn + 1 : rn.
  void cinc(const Register& rd, const Register& rn, Condition cond);

  // Conditional invert: rd = cond ? ~rn : rn.
  void cinv(const Register& rd, const Register& rn, Condition cond);

  // Conditional negate: rd = cond ? -rn : rn.
  void cneg(const Register& rd, const Register& rn, Condition cond);

  // Extr aliases.
  void ror(const Register& rd, const Register& rs, unsigned shift) {
    extr(rd, rs, rs, shift);
  }

  // Conditional comparison.
  // Conditional compare negative.
  void ccmn(const Register& rn, const Operand& operand, StatusFlags nzcv,
            Condition cond);

  // Conditional compare.
  void ccmp(const Register& rn, const Operand& operand, StatusFlags nzcv,
            Condition cond);

  // Multiplication.
  // 32 x 32 -> 32-bit and 64 x 64 -> 64-bit multiply.
  void mul(const Register& rd, const Register& rn, const Register& rm);

  // 32 + 32 x 32 -> 32-bit and 64 + 64 x 64 -> 64-bit multiply accumulate.
  void madd(const Register& rd, const Register& rn, const Register& rm,
            const Register& ra);

  // -(32 x 32) -> 32-bit and -(64 x 64) -> 64-bit multiply.
  void mneg(const Register& rd, const Register& rn, const Register& rm);

  // 32 - 32 x 32 -> 32-bit and 64 - 64 x 64 -> 64-bit multiply subtract.
  void msub(const Register& rd, const Register& rn, const Register& rm,
            const Register& ra);

  // 32 x 32 -> 64-bit multiply.
  void smull(const Register& rd, const Register& rn, const Register& rm);

  // Xd = bits<127:64> of Xn * Xm, signed.
  void smulh(const Register& rd, const Register& rn, const Register& rm);

  // Xd = bits<127:64> of Xn * Xm, unsigned.
  void umulh(const Register& rd, const Register& rn, const Register& rm);

  // Signed 32 x 32 -> 64-bit multiply and accumulate.
  void smaddl(const Register& rd, const Register& rn, const Register& rm,
              const Register& ra);

  // Unsigned 32 x 32 -> 64-bit multiply and accumulate.
  void umaddl(const Register& rd, const Register& rn, const Register& rm,
              const Register& ra);

  // Signed 32 x 32 -> 64-bit multiply and subtract.
  void smsubl(const Register& rd, const Register& rn, const Register& rm,
              const Register& ra);

  // Unsigned 32 x 32 -> 64-bit multiply and subtract.
  void umsubl(const Register& rd, const Register& rn, const Register& rm,
              const Register& ra);

  // Signed integer divide.
  void sdiv(const Register& rd, const Register& rn, const Register& rm);

  // Unsigned integer divide.
  void udiv(const Register& rd, const Register& rn, const Register& rm);

  // Bit count, bit reverse and endian reverse.
  void rbit(const Register& rd, const Register& rn);
  void rev16(const Register& rd, const Register& rn);
  void rev32(const Register& rd, const Register& rn);
  void rev(const Register& rd, const Register& rn);
  void clz(const Register& rd, const Register& rn);
  void cls(const Register& rd, const Register& rn);

  // Pointer Authentication InstructionStream for Instruction address, using key
  // B, with address in x17 and modifier in x16 [Armv8.3].
  void pacib1716();

  // Pointer Authentication InstructionStream for Instruction address, using key
  // B, with address in LR and modifier in SP [Armv8.3].
  void pacibsp();

  // Authenticate Instruction address, using key B, with address in x17 and
  // modifier in x16 [Armv8.3].
  void autib1716();

  // Authenticate Instruction address, using key B, with address in LR and
  // modifier in SP [Armv8.3].
  void autibsp();

  // Memory instructions.

  // Load integer or FP register.
  void ldr(const CPURegister& rt, const MemOperand& src);

  // Store integer or FP register.
  void str(const CPURegister& rt, const MemOperand& dst);

  // Load word with sign extension.
  void ldrsw(const Register& rt, const MemOperand& src);

  // Load byte.
  void ldrb(const Register& rt, const MemOperand& src);

  // Store byte.
  void strb(const Register& rt, const MemOperand& dst);

  // Load byte with sign extension.
  void ldrsb(const Register& rt, const MemOperand& src);

  // Load half-word.
  void ldrh(const Register& rt, const MemOperand& src);

  // Store half-word.
  void strh(const Register& rt, const MemOperand& dst);

  // Load half-word with sign extension.
  void ldrsh(const Register& rt, const MemOperand& src);

  // Load integer or FP register pair.
  void ldp(const CPURegister& rt, const CPURegister& rt2,
           const MemOperand& src);

  // Store integer or FP register pair.
  void stp(const CPURegister& rt, const CPURegister& rt2,
           const MemOperand& dst);

  // Load word pair with sign extension.
  void ldpsw(const Register& rt, const Register& rt2, const MemOperand& src);

  // Load literal to register from a pc relative address.
  void ldr_pcrel(const CPURegister& rt, int imm19);

  // Load literal to register.
  void ldr(const CPURegister& rt, const Immediate& imm);
  void ldr(const CPURegister& rt, const Operand& operand);

  // Load-acquire word.
  void ldar(const Register& rt, const Register& rn);

  // Load-acquire exclusive word.
  void ldaxr(const Register& rt, const Register& rn);

  // Store-release word.
  void stlr(const Register& rt, const Register& rn);

  // Store-release exclusive word.
  void stlxr(const Register& rs, const Register& rt, const Register& rn);

  // Load-acquire byte.
  void ldarb(const Register& rt, const Register& rn);

  // Load-acquire exclusive byte.
  void ldaxrb(const Register& rt, const Register& rn);

  // Store-release byte.
  void stlrb(const Register& rt, const Register& rn);

  // Store-release exclusive byte.
  void stlxrb(const Register& rs, const Register& rt, const Register& rn);

  // Load-acquire half-word.
  void ldarh(const Register& rt, const Register& rn);

  // Load-acquire exclusive half-word.
  void ldaxrh(const Register& rt, const Register& rn);

  // Store-release half-word.
  void stlrh(const Register& rt, const Register& rn);

  // Store-release exclusive half-word.
  void stlxrh(const Register& rs, const Register& rt, const Register& rn);

  // Compare and Swap word or doubleword in memory [Armv8.1].
  void cas(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap word or doubleword in memory, with Load-acquire semantics
  // [Armv8.1].
  void casa(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap word or doubleword in memory, with Store-release semantics
  // [Armv8.1].
  void casl(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap word or doubleword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1].
  void casal(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap byte in memory [Armv8.1].
  void casb(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap byte in memory, with Load-acquire semantics [Armv8.1].
  void casab(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap byte in memory, with Store-release semantics [Armv8.1].
  void caslb(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap byte in memory, with Load-acquire and Store-release
  // semantics [Armv8.1].
  void casalb(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap halfword in memory [Armv8.1].
  void cash(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap halfword in memory, with Load-acquire semantics [Armv8.1].
  void casah(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap halfword in memory, with Store-release semantics
  // [Armv8.1].
  void caslh(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap halfword in memory, with Load-acquire and Store-release
  // semantics [Armv8.1].
  void casalh(const Register& rs, const Register& rt, const MemOperand& src);

  // Compare and Swap Pair of words or doublewords in memory [Armv8.1].
  void casp(const Register& rs, const Register& rs2, const Register& rt,
            const Register& rt2, const MemOperand& src);

  // Compare and Swap Pair of words or doublewords in memory, with Load-acquire
  // semantics [Armv8.1].
  void caspa(const Register& rs, const Register& rs2, const Register& rt,
             const Register& rt2, const MemOperand& src);

  // Compare and Swap Pair of words or doublewords in memory, with Store-release
  // semantics [Armv8.1].
  void caspl(const Register& rs, const Register& rs2, const Register& rt,
             const Register& rt2, const MemOperand& src);

  // Compare and Swap Pair of words or doublewords in memory, with Load-acquire
  // and Store-release semantics [Armv8.1].
  void caspal(const Register& rs, const Register& rs2, const Register& rt,
              const Register& rt2, const MemOperand& src);

  // Atomic add on byte in memory [Armv8.1]
  void ldaddb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on byte in memory, with Load-acquire semantics [Armv8.1]
  void ldaddab(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on byte in memory, with Store-release semantics [Armv8.1]
  void ldaddlb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on byte in memory, with Load-acquire and Store-release semantics
  // [Armv8.1]
  void ldaddalb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on halfword in memory [Armv8.1]
  void ldaddh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on halfword in memory, with Load-acquire semantics [Armv8.1]
  void ldaddah(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on halfword in memory, with Store-release semantics [Armv8.1]
  void ldaddlh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on halfword in memory, with Load-acquire and Store-release
  // semantics [Armv8.1]
  void ldaddalh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on word or doubleword in memory [Armv8.1]
  void ldadd(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on word or doubleword in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldadda(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on word or doubleword in memory, with Store-release semantics
  // [Armv8.1]
  void ldaddl(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on word or doubleword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldaddal(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on byte in memory [Armv8.1]
  void ldclrb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on byte in memory, with Load-acquire semantics [Armv8.1]
  void ldclrab(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on byte in memory, with Store-release semantics [Armv8.1]
  void ldclrlb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on byte in memory, with Load-acquire and Store-release
  // semantics [Armv8.1]
  void ldclralb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on halfword in memory [Armv8.1]
  void ldclrh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on halfword in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldclrah(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on halfword in memory, with Store-release semantics
  // [Armv8.1]
  void ldclrlh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on halfword in memory, with Load-acquire and Store-release
  // semantics [Armv8.1]
  void ldclralh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on word or doubleword in memory [Armv8.1]
  void ldclr(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on word or doubleword in memory, with Load-acquire
  // semantics [Armv8.1]
  void ldclra(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on word or doubleword in memory, with Store-release
  // semantics [Armv8.1]
  void ldclrl(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit clear on word or doubleword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldclral(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on byte in memory [Armv8.1]
  void ldeorb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on byte in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldeorab(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on byte in memory, with Store-release semantics
  // [Armv8.1]
  void ldeorlb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on byte in memory, with Load-acquire and Store-release
  // semantics [Armv8.1]
  void ldeoralb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on halfword in memory [Armv8.1]
  void ldeorh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on halfword in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldeorah(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on halfword in memory, with Store-release semantics
  // [Armv8.1]
  void ldeorlh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on halfword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldeoralh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on word or doubleword in memory [Armv8.1]
  void ldeor(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on word or doubleword in memory, with Load-acquire
  // semantics [Armv8.1]
  void ldeora(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on word or doubleword in memory, with Store-release
  // semantics [Armv8.1]
  void ldeorl(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic exclusive OR on word or doubleword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldeoral(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on byte in memory [Armv8.1]
  void ldsetb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on byte in memory, with Load-acquire semantics [Armv8.1]
  void ldsetab(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on byte in memory, with Store-release semantics [Armv8.1]
  void ldsetlb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on byte in memory, with Load-acquire and Store-release
  // semantics [Armv8.1]
  void ldsetalb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on halfword in memory [Armv8.1]
  void ldseth(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on halfword in memory, with Load-acquire semantics [Armv8.1]
  void ldsetah(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on halfword in memory, with Store-release semantics
  // [Armv8.1]
  void ldsetlh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on halfword in memory, with Load-acquire and Store-release
  // semantics [Armv8.1]
  void ldsetalh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on word or doubleword in memory [Armv8.1]
  void ldset(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on word or doubleword in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldseta(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on word or doubleword in memory, with Store-release
  // semantics [Armv8.1]
  void ldsetl(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic bit set on word or doubleword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldsetal(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on byte in memory [Armv8.1]
  void ldsmaxb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on byte in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldsmaxab(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on byte in memory, with Store-release semantics
  // [Armv8.1]
  void ldsmaxlb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on byte in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldsmaxalb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on halfword in memory [Armv8.1]
  void ldsmaxh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on halfword in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldsmaxah(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on halfword in memory, with Store-release semantics
  // [Armv8.1]
  void ldsmaxlh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on halfword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldsmaxalh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on word or doubleword in memory [Armv8.1]
  void ldsmax(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on word or doubleword in memory, with Load-acquire
  // semantics [Armv8.1]
  void ldsmaxa(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on word or doubleword in memory, with Store-release
  // semantics [Armv8.1]
  void ldsmaxl(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed maximum on word or doubleword in memory, with Load-acquire
  // and Store-release semantics [Armv8.1]
  void ldsmaxal(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on byte in memory [Armv8.1]
  void ldsminb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on byte in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldsminab(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on byte in memory, with Store-release semantics
  // [Armv8.1]
  void ldsminlb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on byte in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldsminalb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on halfword in memory [Armv8.1]
  void ldsminh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on halfword in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldsminah(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on halfword in memory, with Store-release semantics
  // [Armv8.1]
  void ldsminlh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on halfword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldsminalh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on word or doubleword in memory [Armv8.1]
  void ldsmin(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on word or doubleword in memory, with Load-acquire
  // semantics [Armv8.1]
  void ldsmina(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on word or doubleword in memory, with Store-release
  // semantics [Armv8.1]
  void ldsminl(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic signed minimum on word or doubleword in memory, with Load-acquire
  // and Store-release semantics [Armv8.1]
  void ldsminal(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on byte in memory [Armv8.1]
  void ldumaxb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on byte in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldumaxab(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on byte in memory, with Store-release semantics
  // [Armv8.1]
  void ldumaxlb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on byte in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldumaxalb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on halfword in memory [Armv8.1]
  void ldumaxh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on halfword in memory, with Load-acquire semantics
  // [Armv8.1]
  void ldumaxah(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on halfword in memory, with Store-release semantics
  // [Armv8.1]
  void ldumaxlh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on halfword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void ldumaxalh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on word or doubleword in memory [Armv8.1]
  void ldumax(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on word or doubleword in memory, with Load-acquire
  // semantics [Armv8.1]
  void ldumaxa(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on word or doubleword in memory, with Store-release
  // semantics [Armv8.1]
  void ldumaxl(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned maximum on word or doubleword in memory, with Load-acquire
  // and Store-release semantics [Armv8.1]
  void ldumaxal(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on byte in memory [Armv8.1]
  void lduminb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on byte in memory, with Load-acquire semantics
  // [Armv8.1]
  void lduminab(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on byte in memory, with Store-release semantics
  // [Armv8.1]
  void lduminlb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on byte in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void lduminalb(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on halfword in memory [Armv8.1]
  void lduminh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on halfword in memory, with Load-acquire semantics
  // [Armv8.1]
  void lduminah(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on halfword in memory, with Store-release semantics
  // [Armv8.1]
  void lduminlh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on halfword in memory, with Load-acquire and
  // Store-release semantics [Armv8.1]
  void lduminalh(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on word or doubleword in memory [Armv8.1]
  void ldumin(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on word or doubleword in memory, with Load-acquire
  // semantics [Armv8.1]
  void ldumina(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on word or doubleword in memory, with Store-release
  // semantics [Armv8.1]
  void lduminl(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic unsigned minimum on word or doubleword in memory, with Load-acquire
  // and Store-release semantics [Armv8.1]
  void lduminal(const Register& rs, const Register& rt, const MemOperand& src);

  // Atomic add on byte in memory, without return. [Armv8.1]
  void staddb(const Register& rs, const MemOperand& src);

  // Atomic add on byte in memory, with Store-release semantics and without
  // return. [Armv8.1]
  void staddlb(const Register& rs, const MemOperand& src);

  // Atomic add on halfword in memory, without return. [Armv8.1]
  void staddh(const Register& rs, const MemOperand& src);

  // Atomic add on halfword in memory, with Store-release semantics and without
  // return. [Armv8.1]
  void staddlh(const Register& rs, const MemOperand& src);

  // Atomic add on word or doubleword in memory, without return. [Armv8.1]
  void stadd(const Register& rs, const MemOperand& src);

  // Atomic add on word or doubleword in memory, with Store-release semantics
  // and without return. [Armv8.1]
  void staddl(const Register& rs, const MemOperand& src);

  // Atomic bit clear on byte in memory, without return. [Armv8.1]
  void stclrb(const Register& rs, const MemOperand& src);

  // Atomic bit clear on byte in memory, with Store-release semantics and
  // without return. [Armv8.1]
  void stclrlb(const Register& rs, const MemOperand& src);

  // Atomic bit clear on halfword in memory, without return. [Armv8.1]
  void stclrh(const Register& rs, const MemOperand& src);

  // Atomic bit clear on halfword in memory, with Store-release semantics and
  // without return. [Armv8.1]
  void stclrlh(const Register& rs, const MemOperand& src);

  // Atomic bit clear on word or doubleword in memory, without return. [Armv8.1]
  void stclr(const Register& rs, const MemOperand& src);

  // Atomic bit clear on word or doubleword in memory, with Store-release
  // semantics and without return. [Armv8.1]
  void stclrl(const Register& rs, const MemOperand& src);

  // Atomic exclusive OR on byte in memory, without return. [Armv8.1]
  void steorb(const Register& rs, const MemOperand& src);

  // Atomic exclusive OR on byte in memory, with Store-release semantics and
  // without return. [Armv8.1]
  void steorlb(const Register& rs, const MemOperand& src);

  // Atomic exclusive OR on halfword in memory, without return. [Armv8.1]
  void steorh(const Register& rs, const MemOperand& src);

  // Atomic exclusive OR on halfword in memory, with Store-release semantics
  // and without return. [Armv8.1]
  void steorlh(const Register& rs, const MemOperand& src);

  // Atomic exclusive OR on word or doubleword in memory, without return.
  // [Armv8.1]
  void steor(const Register& rs, const MemOperand& src);

  // Atomic exclusive OR on word or doubleword in memory, with Store-release
  // semantics and without return. [Armv8.1]
  void steorl(const Register& rs, const MemOperand& src);

  // Atomic bit set on byte in memory, without return. [Armv8.1]
  void stsetb(const Register& rs, const MemOperand& src);

  // Atomic bit set on byte in memory, with Store-release semantics and without
  // return. [Armv8.1]
  void stsetlb(const Register& rs, const MemOperand& src);

  // Atomic bit set on halfword in memory, without return. [Armv8.1]
  void stseth(const Register& rs, const MemOperand& src);

  // Atomic bit set on halfword in memory, with Store-release semantics and
  // without return. [Armv8.1]
  void stsetlh(const Register& rs, const MemOperand& src);

  // Atomic bit set on word or doubleword in memory, without return. [Armv8.1]
  void stset(const Register& rs, const MemOperand& src);

  // Atomic bit set on word or doubleword in memory, with Store-release
  // semantics and without return. [Armv8.1]
  void stsetl(const Register& rs, const MemOperand& src);

  // Atomic signed maximum on byte in memory, without return. [Armv8.1]
  void stsmaxb(const Register& rs, const MemOperand& src);

  // Atomic signed maximum on byte in memory, with Store-release semantics and
  // without return. [Armv8.1]
  void stsmaxlb(const Register& rs, const MemOperand& src);

  // Atomic signed maximum on halfword in memory, without return. [Armv8.1]
  void stsmaxh(const Register& rs, const MemOperand& src);

  // Atomic signed maximum on halfword in memory, with Store-release semantics
  // and without return. [Armv8.1]
  void stsmaxlh(const Register& rs, const MemOperand& src);

  // Atomic signed maximum on word or doubleword in memory, without return.
  // [Armv8.1]
  void stsmax(const Register& rs, const MemOperand& src);

  // Atomic signed maximum on word or doubleword in memory, with Store-release
  // semantics and without return. [Armv8.1]
  void stsmaxl(const Register& rs, const MemOperand& src);

  // Atomic signed minimum on byte in memory, without return. [Armv8.1]
  void stsminb(const Register& rs, const MemOperand& src);

  // Atomic signed minimum on byte in memory, with Store-release semantics and
  // without return. [Armv8.1]
  void stsminlb(const Register& rs, const MemOperand& src);

  // Atomic signed minimum on halfword in memory, without return. [Armv8.1]
  void stsminh(const Register& rs, const MemOperand& src);

  // Atomic signed minimum on halfword in memory, with Store-release semantics
  // and without return. [Armv8.1]
  void stsminlh(const Register& rs, const MemOperand& src);

  // Atomic signed minimum on word or doubleword in memory, without return.
  // [Armv8.1]
  void stsmin(const Register& rs, const MemOperand& src);

  // Atomic signed minimum on word or doubleword in memory, with Store-release
  // semantics and without return. semantics [Armv8.1]
  void stsminl(const Register& rs, const MemOperand& src);

  // Atomic unsigned maximum on byte in memory, without return. [Armv8.1]
  void stumaxb(const Register& rs, const MemOperand& src);

  // Atomic unsigned maximum on byte in memory, with Store-release semantics and
  // without return. [Armv8.1]
  void stumaxlb(const Register& rs, const MemOperand& src);

  // Atomic unsigned maximum on halfword in memory, without return. [Armv8.1]
  void stumaxh(const Register& rs, const MemOperand& src);

  // Atomic unsigned maximum on halfword in memory, with Store-release semantics
  // and without return. [Armv8.1]
  void stumaxlh(const Register& rs, const MemOperand& src);

  // Atomic unsigned maximum on word or doubleword in memory, without return.
  // [Armv8.1]
  void stumax(const Register& rs, const MemOperand& src);

  // Atomic unsigned maximum on word or doubleword in memory, with Store-release
  // semantics and without return. [Armv8.1]
  void stumaxl(const Register& rs, const MemOperand& src);

  // Atomic unsigned minimum on byte in memory, without return. [Armv8.1]
  void stuminb(const Register& rs, const MemOperand& src);

  // Atomic unsigned minimum on byte in memory, with Store-release semantics and
  // without return. [Armv8.1]
  void stuminlb(const Register& rs, const MemOperand& src);

  // Atomic unsigned minimum on halfword in memory, without return. [Armv8.1]
  void stuminh(const Register& rs, const MemOperand& src);

  // Atomic unsigned minimum on halfword in memory, with Store-release semantics
  // and without return. [Armv8.1]
  void stuminlh(const Register& rs, const MemOperand& src);

  // Atomic unsigned minimum on word or doubleword in memory, without return.
  // [Armv8.1]
  void stumin(const Register& rs, const MemOperand& src);

  // Atomic unsigned minimum on word or doubleword in memory, with Store-release
  // semantics and without return. [Armv8.1]
  void stuminl(const Register& rs, const MemOperand& src);

  // Swap byte in memory [Armv8.1]
  void swpb(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap byte in memory, with Load-acquire semantics [Armv8.1]
  void swpab(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap byte in memory, with Store-release semantics [Armv8.1]
  void swplb(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap byte in memory, with Load-acquire and Store-release semantics
  // [Armv8.1]
  void swpalb(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap halfword in memory [Armv8.1]
  void swph(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap halfword in memory, with Load-acquire semantics [Armv8.1]
  void swpah(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap halfword in memory, with Store-release semantics [Armv8.1]
  void swplh(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap halfword in memory, with Load-acquire and Store-release semantics
  // [Armv8.1]
  void swpalh(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap word or doubleword in memory [Armv8.1]
  void swp(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap word or doubleword in memory, with Load-acquire semantics [Armv8.1]
  void swpa(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap word or doubleword in memory, with Store-release semantics [Armv8.1]
  void swpl(const Register& rs, const Register& rt, const MemOperand& src);

  // Swap word or doubleword in memory, with Load-acquire and Store-release
  // semantics [Armv8.1]
  void swpal(const Register& rs, const Register& rt, const MemOperand& src);

  // Move instructions. The default shift of -1 indicates that the move
  // instruction will calculate an appropriate 16-bit immediate and left shift
  // that is equal to the 64-bit immediate argument. If an explicit left shift
  // is specified (0, 16, 32 or 48), the immediate must be a 16-bit value.
  //
  // For movk, an explicit shift can be used to indicate which half word should
  // be overwritten, eg. movk(x0, 0, 0) will overwrite the least-significant
  // half word with zero, whereas movk(x0, 0, 48) will overwrite the
  // most-significant.

  // Move and keep.
  void movk(const Register& rd, uint64_t imm, int shift = -1) {
    MoveWide(rd, imm, shift, MOVK);
  }

  // Move with non-zero.
  void movn(const Register& rd, uint64_t imm, int shift = -1) {
    MoveWide(rd, imm, shift, MOVN);
  }

  // Move with zero.
  void movz(const Register& rd, uint64_t imm, int shift = -1) {
    MoveWide(rd, imm, shift, MOVZ);
  }

  // Misc instructions.
  // Monitor debug-mode breakpoint.
  void brk(int code);

  // Halting debug-mode breakpoint.
  void hlt(int code);

  // Move register to register.
  void mov(const Register& rd, const Register& rn);

  // Move NOT(operand) to register.
  void mvn(const Register& rd, const Operand& operand);

  // System instructions.
  // Move to register from system register.
  void mrs(const Register& rt, SystemRegister sysreg);

  // Move from register to system register.
  void msr(SystemRegister sysreg, const Register& rt);

  // System hint.
  void hint(SystemHint code);

  // Data memory barrier
  void dmb(BarrierDomain domain, BarrierType type);

  // Data synchronization barrier
  void dsb(BarrierDomain domain, BarrierType type);

  // Instruction synchronization barrier
  void isb();

  // Conditional speculation barrier.
  void csdb();

  // Branch target identification.
  void bti(BranchTargetIdentifier id);

  // No-op.
  void nop() { hint(NOP); }

  // Different nop operations are used by the code generator to detect certain
  // states of the generated code.
  enum NopMarkerTypes {
    DEBUG_BREAK_NOP,
    INTERRUPT_CODE_NOP,
    ADR_FAR_NOP,
    FIRST_NOP_MARKER = DEBUG_BREAK_NOP,
    LAST_NOP_MARKER = ADR_FAR_NOP
  };

  void nop(NopMarkerTypes n);

  // Add.
  void add(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned halving add.
  void uhadd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Subtract.
  void sub(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed halving add.
  void shadd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Multiply by scalar element.
  void mul(const VRegister& vd, const VRegister& vn, const VRegister& vm,
           int vm_index);

  // Multiply-add by scalar element.
  void mla(const VRegister& vd, const VRegister& vn, const VRegister& vm,
           int vm_index);

  // Multiply-subtract by scalar element.
  void mls(const VRegister& vd, const VRegister& vn, const VRegister& vm,
           int vm_index);

  // Signed long multiply-add by scalar element.
  void smlal(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             int vm_index);

  // Signed long multiply-add by scalar element (second part).
  void smlal2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
              int vm_index);

  // Unsigned long multiply-add by scalar element.
  void umlal(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             int vm_index);

  // Unsigned long multiply-add by scalar element (second part).
  void umlal2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
              int vm_index);

  // Signed long multiply-sub by scalar element.
  void smlsl(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             int vm_index);

  // Signed long multiply-sub by scalar element (second part).
  void smlsl2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
              int vm_index);

  // Unsigned long multiply-sub by scalar element.
  void umlsl(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             int vm_index);

  // Unsigned long multiply-sub by scalar element (second part).
  void umlsl2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
              int vm_index);

  // Signed long multiply by scalar element.
  void smull(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             int vm_index);

  // Signed long multiply by scalar element (second part).
  void smull2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
              int vm_index);

  // Unsigned long multiply by scalar element.
  void umull(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             int vm_index);

  // Unsigned long multiply by scalar element (second part).
  void umull2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
              int vm_index);

  // Add narrow returning high half.
  void addhn(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Add narrow returning high half (second part).
  void addhn2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating double long multiply by element.
  void sqdmull(const VRegister& vd, const VRegister& vn, const VRegister& vm,
               int vm_index);

  // Signed saturating double long multiply by element (second part).
  void sqdmull2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
                int vm_index);

  // Signed saturating doubling long multiply-add by element.
  void sqdmlal(const VRegister& vd, const VRegister& vn, const VRegister& vm,
               int vm_index);

  // Signed saturating doubling long multiply-add by element (second part).
  void sqdmlal2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
                int vm_index);

  // Signed saturating doubling long multiply-sub by element.
  void sqdmlsl(const VRegister& vd, const VRegister& vn, const VRegister& vm,
               int vm_index);

  // Signed saturating doubling long multiply-sub by element (second part).
  void sqdmlsl2(const VRegister& vd, const VRegister& vn, const VRegister& vm,
                int vm_index);

  // Compare bitwise to zero.
  void cmeq(const VRegister& vd, const VRegister& vn, int value);

  // Compare signed greater than or equal to zero.
  void cmge(const VRegister& vd, const VRegister& vn, int value);

  // Compare signed greater than zero.
  void cmgt(const VRegister& vd, const VRegister& vn, int value);

  // Compare signed less than or equal to zero.
  void cmle(const VRegister& vd, const VRegister& vn, int value);

  // Compare signed less than zero.
  void cmlt(const VRegister& vd, const VRegister& vn, int value);

  // Unsigned rounding halving add.
  void urhadd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Compare equal.
  void cmeq(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Compare signed greater than or equal.
  void cmge(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Compare signed greater than.
  void cmgt(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Compare unsigned higher.
  void cmhi(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Compare unsigned higher or same.
  void cmhs(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Compare bitwise test bits nonzero.
  void cmtst(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed shift left by register.
  void sshl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned shift left by register.
  void ushl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating doubling long multiply-subtract.
  void sqdmlsl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating doubling long multiply-subtract (second part).
  void sqdmlsl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating doubling long multiply.
  void sqdmull(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating doubling long multiply (second part).
  void sqdmull2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating doubling multiply returning high half.
  void sqdmulh(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating rounding doubling multiply returning high half.
  void sqrdmulh(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating doubling multiply element returning high half.
  void sqdmulh(const VRegister& vd, const VRegister& vn, const VRegister& vm,
               int vm_index);

  // Signed saturating rounding doubling multiply element returning high half.
  void sqrdmulh(const VRegister& vd, const VRegister& vn, const VRegister& vm,
                int vm_index);

  // Unsigned long multiply long.
  void umull(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned long multiply (second part).
  void umull2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Rounding add narrow returning high half.
  void raddhn(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Subtract narrow returning high half.
  void subhn(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Subtract narrow returning high half (second part).
  void subhn2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Rounding add narrow returning high half (second part).
  void raddhn2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Rounding subtract narrow returning high half.
  void rsubhn(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Rounding subtract narrow returning high half (second part).
  void rsubhn2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating shift left by register.
  void sqshl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned saturating shift left by register.
  void uqshl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed rounding shift left by register.
  void srshl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned rounding shift left by register.
  void urshl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating rounding shift left by register.
  void sqrshl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned saturating rounding shift left by register.
  void uqrshl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed absolute difference.
  void sabd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned absolute difference and accumulate.
  void uaba(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Shift left by immediate and insert.
  void sli(const VRegister& vd, const VRegister& vn, int shift);

  // Shift right by immediate and insert.
  void sri(const VRegister& vd, const VRegister& vn, int shift);

  // Signed maximum.
  void smax(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed pairwise maximum.
  void smaxp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Add across vector.
  void addv(const VRegister& vd, const VRegister& vn);

  // Signed add long across vector.
  void saddlv(const VRegister& vd, const VRegister& vn);

  // Unsigned add long across vector.
  void uaddlv(const VRegister& vd, const VRegister& vn);

  // FP maximum number across vector.
  void fmaxnmv(const VRegister& vd, const VRegister& vn);

  // FP maximum across vector.
  void fmaxv(const VRegister& vd, const VRegister& vn);

  // FP minimum number across vector.
  void fminnmv(const VRegister& vd, const VRegister& vn);

  // FP minimum across vector.
  void fminv(const VRegister& vd, const VRegister& vn);

  // Signed maximum across vector.
  void smaxv(const VRegister& vd, const VRegister& vn);

  // Signed minimum.
  void smin(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed minimum pairwise.
  void sminp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed minimum across vector.
  void sminv(const VRegister& vd, const VRegister& vn);

  // Signed dot product
  void sdot(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // One-element structure store from one register.
  void st1(const VRegister& vt, const MemOperand& src);

  // One-element structure store from two registers.
  void st1(const VRegister& vt, const VRegister& vt2, const MemOperand& src);

  // One-element structure store from three registers.
  void st1(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const MemOperand& src);

  // One-element structure store from four registers.
  void st1(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, const MemOperand& src);

  // One-element single structure store from one lane.
  void st1(const VRegister& vt, int lane, const MemOperand& src);

  // Two-element structure store from two registers.
  void st2(const VRegister& vt, const VRegister& vt2, const MemOperand& src);

  // Two-element single structure store from two lanes.
  void st2(const VRegister& vt, const VRegister& vt2, int lane,
           const MemOperand& src);

  // Three-element structure store from three registers.
  void st3(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const MemOperand& src);

  // Three-element single structure store from three lanes.
  void st3(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           int lane, const MemOperand& src);

  // Four-element structure store from four registers.
  void st4(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, const MemOperand& src);

  // Four-element single structure store from four lanes.
  void st4(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, int lane, const MemOperand& src);

  // Unsigned add long.
  void uaddl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned add long (second part).
  void uaddl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned add wide.
  void uaddw(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned add wide (second part).
  void uaddw2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed add long.
  void saddl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed add long (second part).
  void saddl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed add wide.
  void saddw(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed add wide (second part).
  void saddw2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned subtract long.
  void usubl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned subtract long (second part).
  void usubl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned subtract wide.
  void usubw(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed subtract long.
  void ssubl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed subtract long (second part).
  void ssubl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed integer subtract wide.
  void ssubw(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed integer subtract wide (second part).
  void ssubw2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned subtract wide (second part).
  void usubw2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned maximum.
  void umax(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned pairwise maximum.
  void umaxp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned maximum across vector.
  void umaxv(const VRegister& vd, const VRegister& vn);

  // Unsigned minimum.
  void umin(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned pairwise minimum.
  void uminp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned minimum across vector.
  void uminv(const VRegister& vd, const VRegister& vn);

  // Transpose vectors (primary).
  void trn1(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Transpose vectors (secondary).
  void trn2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unzip vectors (primary).
  void uzp1(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unzip vectors (secondary).
  void uzp2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Zip vectors (primary).
  void zip1(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Zip vectors (secondary).
  void zip2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed shift right by immediate.
  void sshr(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned shift right by immediate.
  void ushr(const VRegister& vd, const VRegister& vn, int shift);

  // Signed rounding shift right by immediate.
  void srshr(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned rounding shift right by immediate.
  void urshr(const VRegister& vd, const VRegister& vn, int shift);

  // Signed shift right by immediate and accumulate.
  void ssra(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned shift right by immediate and accumulate.
  void usra(const VRegister& vd, const VRegister& vn, int shift);

  // Signed rounding shift right by immediate and accumulate.
  void srsra(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned rounding shift right by immediate and accumulate.
  void ursra(const VRegister& vd, const VRegister& vn, int shift);

  // Shift right narrow by immediate.
  void shrn(const VRegister& vd, const VRegister& vn, int shift);

  // Shift right narrow by immediate (second part).
  void shrn2(const VRegister& vd, const VRegister& vn, int shift);

  // Rounding shift right narrow by immediate.
  void rshrn(const VRegister& vd, const VRegister& vn, int shift);

  // Rounding shift right narrow by immediate (second part).
  void rshrn2(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned saturating shift right narrow by immediate.
  void uqshrn(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned saturating shift right narrow by immediate (second part).
  void uqshrn2(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned saturating rounding shift right narrow by immediate.
  void uqrshrn(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned saturating rounding shift right narrow by immediate (second part).
  void uqrshrn2(const VRegister& vd, const VRegister& vn, int shift);

  // Signed saturating shift right narrow by immediate.
  void sqshrn(const VRegister& vd, const VRegister& vn, int shift);

  // Signed saturating shift right narrow by immediate (second part).
  void sqshrn2(const VRegister& vd, const VRegister& vn, int shift);

  // Signed saturating rounded shift right narrow by immediate.
  void sqrshrn(const VRegister& vd, const VRegister& vn, int shift);

  // Signed saturating rounded shift right narrow by immediate (second part).
  void sqrshrn2(const VRegister& vd, const VRegister& vn, int shift);

  // Signed saturating shift right unsigned narrow by immediate.
  void sqshrun(const VRegister& vd, const VRegister& vn, int shift);

  // Signed saturating shift right unsigned narrow by immediate (second part).
  void sqshrun2(const VRegister& vd, const VRegister& vn, int shift);

  // Signed sat rounded shift right unsigned narrow by immediate.
  void sqrshrun(const VRegister& vd, const VRegister& vn, int shift);

  // Signed sat rounded shift right unsigned narrow by immediate (second part).
  void sqrshrun2(const VRegister& vd, const VRegister& vn, int shift);

  // FP reciprocal step.
  void frecps(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP reciprocal estimate.
  void frecpe(const VRegister& vd, const VRegister& vn);

  // FP reciprocal square root estimate.
  void frsqrte(const VRegister& vd, const VRegister& vn);

  // FP reciprocal square root step.
  void frsqrts(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed absolute difference and accumulate long.
  void sabal(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed absolute difference and accumulate long (second part).
  void sabal2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned absolute difference and accumulate long.
  void uabal(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned absolute difference and accumulate long (second part).
  void uabal2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed absolute difference long.
  void sabdl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed absolute difference long (second part).
  void sabdl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned absolute difference long.
  void uabdl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned absolute difference long (second part).
  void uabdl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Polynomial multiply long.
  void pmull(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Polynomial multiply long (second part).
  void pmull2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed long multiply-add.
  void smlal(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed long multiply-add (second part).
  void smlal2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned long multiply-add.
  void umlal(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned long multiply-add (second part).
  void umlal2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed long multiply-sub.
  void smlsl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed long multiply-sub (second part).
  void smlsl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned long multiply-sub.
  void umlsl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned long multiply-sub (second part).
  void umlsl2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed long multiply.
  void smull(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed long multiply (second part).
  void smull2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating doubling long multiply-add.
  void sqdmlal(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating doubling long multiply-add (second part).
  void sqdmlal2(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned absolute difference.
  void uabd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed absolute difference and accumulate.
  void saba(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP instructions.
  // Move immediate to FP register.
  void fmov(const VRegister& fd, double imm);
  void fmov(const VRegister& fd, float imm);

  // Move FP register to register.
  void fmov(const Register& rd, const VRegister& fn);

  // Move register to FP register.
  void fmov(const VRegister& fd, const Register& rn);

  // Move FP register to FP register.
  void fmov(const VRegister& fd, const VRegister& fn);

  // Move 64-bit register to top half of 128-bit FP register.
  void fmov(const VRegister& vd, int index, const Register& rn);

  // Move top half of 128-bit FP register to 64-bit register.
  void fmov(const Register& rd, const VRegister& vn, int index);

  // FP add.
  void fadd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP subtract.
  void fsub(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP multiply.
  void fmul(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP compare equal to zero.
  void fcmeq(const VRegister& vd, const VRegister& vn, double imm);

  // FP greater than zero.
  void fcmgt(const VRegister& vd, const VRegister& vn, double imm);

  // FP greater than or equal to zero.
  void fcmge(const VRegister& vd, const VRegister& vn, double imm);

  // FP less than or equal to zero.
  void fcmle(const VRegister& vd, const VRegister& vn, double imm);

  // FP less than to zero.
  void fcmlt(const VRegister& vd, const VRegister& vn, double imm);

  // FP absolute difference.
  void fabd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP pairwise add vector.
  void faddp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP pairwise add scalar.
  void faddp(const VRegister& vd, const VRegister& vn);

  // FP pairwise maximum scalar.
  void fmaxp(const VRegister& vd, const VRegister& vn);

  // FP pairwise maximum number scalar.
  void fmaxnmp(const VRegister& vd, const VRegister& vn);

  // FP pairwise minimum number scalar.
  void fminnmp(const VRegister& vd, const VRegister& vn);

  // FP vector multiply accumulate.
  void fmla(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP vector multiply subtract.
  void fmls(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP vector multiply extended.
  void fmulx(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP absolute greater than or equal.
  void facge(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP absolute greater than.
  void facgt(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP multiply by element.
  void fmul(const VRegister& vd, const VRegister& vn, const VRegister& vm,
            int vm_index);

  // FP fused multiply-add to accumulator by element.
  void fmla(const VRegister& vd, const VRegister& vn, const VRegister& vm,
            int vm_index);

  // FP fused multiply-sub from accumulator by element.
  void fmls(const VRegister& vd, const VRegister& vn, const VRegister& vm,
            int vm_index);

  // FP multiply extended by element.
  void fmulx(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             int vm_index);

  // FP compare equal.
  void fcmeq(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP greater than.
  void fcmgt(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP greater than or equal.
  void fcmge(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP pairwise maximum vector.
  void fmaxp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP pairwise minimum vector.
  void fminp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP pairwise minimum scalar.
  void fminp(const VRegister& vd, const VRegister& vn);

  // FP pairwise maximum number vector.
  void fmaxnmp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP pairwise minimum number vector.
  void fminnmp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP fused multiply-add.
  void fmadd(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             const VRegister& va);

  // FP fused multiply-subtract.
  void fmsub(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             const VRegister& va);

  // FP fused multiply-add and negate.
  void fnmadd(const VRegister& vd, const VRegister& vn, const VRegister& vm,
              const VRegister& va);

  // FP fused multiply-subtract and negate.
  void fnmsub(const VRegister& vd, const VRegister& vn, const VRegister& vm,
              const VRegister& va);

  // FP multiply-negate scalar.
  void fnmul(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP reciprocal exponent scalar.
  void frecpx(const VRegister& vd, const VRegister& vn);

  // FP divide.
  void fdiv(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP maximum.
  void fmax(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP minimum.
  void fmin(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP maximum.
  void fmaxnm(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP minimum.
  void fminnm(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // FP absolute.
  void fabs(const VRegister& vd, const VRegister& vn);

  // FP negate.
  void fneg(const VRegister& vd, const VRegister& vn);

  // FP square root.
  void fsqrt(const VRegister& vd, const VRegister& vn);

  // FP round to integer nearest with ties to away.
  void frinta(const VRegister& vd, const VRegister& vn);

  // FP round to integer, implicit rounding.
  void frinti(const VRegister& vd, const VRegister& vn);

  // FP round to integer toward minus infinity.
  void frintm(const VRegister& vd, const VRegister& vn);

  // FP round to integer nearest with ties to even.
  void frintn(const VRegister& vd, const VRegister& vn);

  // FP round to integer towards plus infinity.
  void frintp(const VRegister& vd, const VRegister& vn);

  // FP round to integer, exact, implicit rounding.
  void frintx(const VRegister& vd, const VRegister& vn);

  // FP round to integer towards zero.
  void frintz(const VRegister& vd, const VRegister& vn);

  // FP compare registers.
  void fcmp(const VRegister& vn, const VRegister& vm);

  // FP compare immediate.
  void fcmp(const VRegister& vn, double value);

  // FP conditional compare.
  void fccmp(const VRegister& vn, const VRegister& vm, StatusFlags nzcv,
             Condition cond);

  // FP conditional select.
  void fcsel(const VRegister& vd, const VRegister& vn, const VRegister& vm,
             Condition cond);

  // Common FP Convert functions.
  void NEONFPConvertToInt(const Register& rd, const VRegister& vn, Instr op);
  void NEONFPConvertToInt(const VRegister& vd, const VRegister& vn, Instr op);

  // FP convert between precisions.
  void fcvt(const VRegister& vd, const VRegister& vn);

  // FP convert to higher precision.
  void fcvtl(const VRegister& vd, const VRegister& vn);

  // FP convert to higher precision (second part).
  void fcvtl2(const VRegister& vd, const VRegister& vn);

  // FP convert to lower precision.
  void fcvtn(const VRegister& vd, const VRegister& vn);

  // FP convert to lower prevision (second part).
  void fcvtn2(const VRegister& vd, const VRegister& vn);

  // FP convert to lower precision, rounding to odd.
  void fcvtxn(const VRegister& vd, const VRegister& vn);

  // FP convert to lower precision, rounding to odd (second part).
  void fcvtxn2(const VRegister& vd, const VRegister& vn);

  // FP convert to signed integer, nearest with ties to away.
  void fcvtas(const Register& rd, const VRegister& vn);

  // FP convert to unsigned integer, nearest with ties to away.
  void fcvtau(const Register& rd, const VRegister& vn);

  // FP convert to signed integer, nearest with ties to away.
  void fcvtas(const VRegister& vd, const VRegister& vn);

  // FP convert to unsigned integer, nearest with ties to away.
  void fcvtau(const VRegister& vd, const VRegister& vn);

  // FP convert to signed integer, round towards -infinity.
  void fcvtms(const Register& rd, const VRegister& vn);

  // FP convert to unsigned integer, round towards -infinity.
  void fcvtmu(const Register& rd, const VRegister& vn);

  // FP convert to signed integer, round towards -infinity.
  void fcvtms(const VRegister& vd, const VRegister& vn);

  // FP convert to unsigned integer, round towards -infinity.
  void fcvtmu(const VRegister& vd, const VRegister& vn);

  // FP convert to signed integer, nearest with ties to even.
  void fcvtns(const Register& rd, const VRegister& vn);

  // FP JavaScript convert to signed integer, rounding toward zero [Armv8.3].
  void fjcvtzs(const Register& rd, const VRegister& vn);

  // FP convert to unsigned integer, nearest with ties to even.
  void fcvtnu(const Register& rd, const VRegister& vn);

  // FP convert to signed integer, nearest with ties to even.
  void fcvtns(const VRegister& rd, const VRegister& vn);

  // FP convert to unsigned integer, nearest with ties to even.
  void fcvtnu(const VRegister& rd, const VRegister& vn);

  // FP convert to signed integer or fixed-point, round towards zero.
  void fcvtzs(const Register& rd, const VRegister& vn, int fbits = 0);

  // FP convert to unsigned integer or fixed-point, round towards zero.
  void fcvtzu(const Register& rd, const VRegister& vn, int fbits = 0);

  // FP convert to signed integer or fixed-point, round towards zero.
  void fcvtzs(const VRegister& vd, const VRegister& vn, int fbits = 0);

  // FP convert to unsigned integer or fixed-point, round towards zero.
  void fcvtzu(const VRegister& vd, const VRegister& vn, int fbits = 0);

  // FP convert to signed integer, round towards +infinity.
  void fcvtps(const Register& rd, const VRegister& vn);

  // FP convert to unsigned integer, round towards +infinity.
  void fcvtpu(const Register& rd, const VRegister& vn);

  // FP convert to signed integer, round towards +infinity.
  void fcvtps(const VRegister& vd, const VRegister& vn);

  // FP convert to unsigned integer, round towards +infinity.
  void fcvtpu(const VRegister& vd, const VRegister& vn);

  // Convert signed integer or fixed point to FP.
  void scvtf(const VRegister& fd, const Register& rn, int fbits = 0);

  // Convert unsigned integer or fixed point to FP.
  void ucvtf(const VRegister& fd, const Register& rn, int fbits = 0);

  // Convert signed integer or fixed-point to FP.
  void scvtf(const VRegister& fd, const VRegister& vn, int fbits = 0);

  // Convert unsigned integer or fixed-point to FP.
  void ucvtf(const VRegister& fd, const VRegister& vn, int fbits = 0);

  // Extract vector from pair of vectors.
  void ext(const VRegister& vd, const VRegister& vn, const VRegister& vm,
           int index);

  // Duplicate vector element to vector or scalar.
  void dup(const VRegister& vd, const VRegister& vn, int vn_index);

  // Duplicate general-purpose register to vector.
  void dup(const VRegister& vd, const Register& rn);

  // Insert vector element from general-purpose register.
  void ins(const VRegister& vd, int vd_index, const Register& rn);

  // Move general-purpose register to a vector element.
  void mov(const VRegister& vd, int vd_index, const Register& rn);

  // Unsigned move vector element to general-purpose register.
  void umov(const Register& rd, const VRegister& vn, int vn_index);

  // Move vector element to general-purpose register.
  void mov(const Register& rd, const VRegister& vn, int vn_index);

  // Move vector element to scalar.
  void mov(const VRegister& vd, const VRegister& vn, int vn_index);

  // Insert vector element from another vector element.
  void ins(const VRegister& vd, int vd_index, const VRegister& vn,
           int vn_index);

  // Move vector element to another vector element.
  void mov(const VRegister& vd, int vd_index, const VRegister& vn,
           int vn_index);

  // Signed move vector element to general-purpose register.
  void smov(const Register& rd, const VRegister& vn, int vn_index);

  // One-element structure load to one register.
  void ld1(const VRegister& vt, const MemOperand& src);

  // One-element structure load to two registers.
  void ld1(const VRegister& vt, const VRegister& vt2, const MemOperand& src);

  // One-element structure load to three registers.
  void ld1(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const MemOperand& src);

  // One-element structure load to four registers.
  void ld1(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, const MemOperand& src);

  // One-element single structure load to one lane.
  void ld1(const VRegister& vt, int lane, const MemOperand& src);

  // One-element single structure load to all lanes.
  void ld1r(const VRegister& vt, const MemOperand& src);

  // Two-element structure load.
  void ld2(const VRegister& vt, const VRegister& vt2, const MemOperand& src);

  // Two-element single structure load to one lane.
  void ld2(const VRegister& vt, const VRegister& vt2, int lane,
           const MemOperand& src);

  // Two-element single structure load to all lanes.
  void ld2r(const VRegister& vt, const VRegister& vt2, const MemOperand& src);

  // Three-element structure load.
  void ld3(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const MemOperand& src);

  // Three-element single structure load to one lane.
  void ld3(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           int lane, const MemOperand& src);

  // Three-element single structure load to all lanes.
  void ld3r(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
            const MemOperand& src);

  // Four-element structure load.
  void ld4(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, const MemOperand& src);

  // Four-element single structure load to one lane.
  void ld4(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, int lane, const MemOperand& src);

  // Four-element single structure load to all lanes.
  void ld4r(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
            const VRegister& vt4, const MemOperand& src);

  // Count leading sign bits.
  void cls(const VRegister& vd, const VRegister& vn);

  // Count leading zero bits (vector).
  void clz(const VRegister& vd, const VRegister& vn);

  // Population count per byte.
  void cnt(const VRegister& vd, const VRegister& vn);

  // Reverse bit order.
  void rbit(const VRegister& vd, const VRegister& vn);

  // Reverse elements in 16-bit halfwords.
  void rev16(const VRegister& vd, const VRegister& vn);

  // Reverse elements in 32-bit words.
  void rev32(const VRegister& vd, const VRegister& vn);

  // Reverse elements in 64-bit doublewords.
  void rev64(const VRegister& vd, const VRegister& vn);

  // Unsigned reciprocal square root estimate.
  void ursqrte(const VRegister& vd, const VRegister& vn);

  // Unsigned reciprocal estimate.
  void urecpe(const VRegister& vd, const VRegister& vn);

  // Signed pairwise long add and accumulate.
  void sadalp(const VRegister& vd, const VRegister& vn);

  // Signed pairwise long add.
  void saddlp(const VRegister& vd, const VRegister& vn);

  // Unsigned pairwise long add.
  void uaddlp(const VRegister& vd, const VRegister& vn);

  // Unsigned pairwise long add and accumulate.
  void uadalp(const VRegister& vd, const VRegister& vn);

  // Shift left by immediate.
  void shl(const VRegister& vd, const VRegister& vn, int shift);

  // Signed saturating shift left by immediate.
  void sqshl(const VRegister& vd, const VRegister& vn, int shift);

  // Signed saturating shift left unsigned by immediate.
  void sqshlu(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned saturating shift left by immediate.
  void uqshl(const VRegister& vd, const VRegister& vn, int shift);

  // Signed shift left long by immediate.
  void sshll(const VRegister& vd, const VRegister& vn, int shift);

  // Signed shift left long by immediate (second part).
  void sshll2(const VRegister& vd, const VRegister& vn, int shift);

  // Signed extend long.
  void sxtl(const VRegister& vd, const VRegister& vn);

  // Signed extend long (second part).
  void sxtl2(const VRegister& vd, const VRegister& vn);

  // Unsigned shift left long by immediate.
  void ushll(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned shift left long by immediate (second part).
  void ushll2(const VRegister& vd, const VRegister& vn, int shift);

  // Shift left long by element size.
  void shll(const VRegister& vd, const VRegister& vn, int shift);

  // Shift left long by element size (second part).
  void shll2(const VRegister& vd, const VRegister& vn, int shift);

  // Unsigned extend long.
  void uxtl(const VRegister& vd, const VRegister& vn);

  // Unsigned extend long (second part).
  void uxtl2(const VRegister& vd, const VRegister& vn);

  // Signed rounding halving add.
  void srhadd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned halving sub.
  void uhsub(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed halving sub.
  void shsub(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned saturating add.
  void uqadd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating add.
  void sqadd(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Unsigned saturating subtract.
  void uqsub(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Signed saturating subtract.
  void sqsub(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Add pairwise.
  void addp(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Add pair of elements scalar.
  void addp(const VRegister& vd, const VRegister& vn);

  // Multiply-add to accumulator.
  void mla(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Multiply-subtract to accumulator.
  void mls(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Multiply.
  void mul(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Table lookup from one register.
  void tbl(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Table lookup from two registers.
  void tbl(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vm);

  // Table lookup from three registers.
  void tbl(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vn3, const VRegister& vm);

  // Table lookup from four registers.
  void tbl(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vn3, const VRegister& vn4, const VRegister& vm);

  // Table lookup extension from one register.
  void tbx(const VRegister& vd, const VRegister& vn, const VRegister& vm);

  // Table lookup extension from two registers.
  void tbx(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vm);

  // Table lookup extension from three registers.
  void tbx(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vn3, const VRegister& vm);

  // Table lookup extension from four registers.
  void tbx(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vn3, const VRegister& vn4, const VRegister& vm);

  // Instruction functions used only for test, debug, and patching.
  // Emit raw instructions in the instruction stream.
  void dci(Instr raw_inst) { Emit(raw_inst); }

  // Emit 8 bits of data in the instruction stream.
  void dc8(uint8_t data) { EmitData(&data, sizeof(data)); }

  // Emit 32 bits of data in the instruction stream.
  void dc32(uint32_t data) { EmitData(&data, sizeof(data)); }

  // Emit 64 bits of data in the instruction stream.
  void dc64(uint64_t data) { EmitData(&data, sizeof(data)); }

  // Emit an address in the instruction stream.
  void dcptr(Label* label);

  // Copy a string into the instruction stream, including the terminating
  // nullptr character. The instruction pointer (pc_) is then aligned correctly
  // for subsequent instructions.
  void EmitStringData(const char* string);

  // Pseudo-instructions ------------------------------------------------------

  // Parameters are described in arm64/instructions-arm64.h.
  void debug(const char* message, uint32_t code, Instr params = BREAK);

  // Required by V8.
  void db(uint8_t data) { dc8(data); }
  void dd(uint32_t data) {
    BlockPoolsScope no_pool_scope(this);
    dc32(data);
  }
  void dq(uint64_t data) {
    BlockPoolsScope no_pool_scope(this);
    dc64(data);
  }
  void dp(uintptr_t data) {
    BlockPoolsScope no_pool_scope(this);
    dc64(data);
  }

  // InstructionStream generation helpers
  // --------------------------------------------------

  Instruction* pc() const { return Instruction::Cast(pc_); }

  Instruction* InstructionAt(ptrdiff_t offset) const {
    return reinterpret_cast<Instruction*>(buffer_start_ + offset);
  }

  ptrdiff_t InstructionOffset(Instruction* instr) const {
    return reinterpret_cast<uint8_t*>(instr) - buffer_start_;
  }

  // Register encoding.
  static Instr Rd(CPURegister rd) {
    DCHECK_NE(rd.code(), kSPRegInternalCode);
    return rd.code() << Rd_offset;
  }

  static Instr Rn(CPURegister rn) {
    DCHECK_NE(rn.code(), kSPRegInternalCode);
    return rn.code() << Rn_offset;
  }

  static Instr Rm(CPURegister rm) {
    DCHECK_NE(rm.code(), kSPRegInternalCode);
    return rm.code() << Rm_offset;
  }

  static Instr RmNot31(CPURegister rm) {
    DCHECK_NE(rm.code(), kSPRegInternalCode);
    DCHECK(!rm.IsZero());
    return Rm(rm);
  }

  static Instr Ra(CPURegister ra) {
    DCHECK_NE(ra.code(), kSPRegInternalCode);
    return ra.code() << Ra_offset;
  }

  static Instr Rt(CPURegister rt) {
    DCHECK_NE(rt.code(), kSPRegInternalCode);
    return rt.code() << Rt_offset;
  }

  static Instr Rt2(CPURegister rt2) {
    DCHECK_NE(rt2.code(), kSPRegInternalCode);
    return rt2.code() << Rt2_offset;
  }

  static Instr Rs(CPURegister rs) {
    DCHECK_NE(rs.code(), kSPRegInternalCode);
    return rs.code() << Rs_offset;
  }

  // These encoding functions allow the stack pointer to be encoded, and
  // disallow the zero register.
  static Instr RdSP(Register rd) {
    DCHECK(!rd.IsZero());
    return (rd.code() & kRegCodeMask) << Rd_offset;
  }

  static Instr RnSP(Register rn) {
    DCHECK(!rn.IsZero());
    return (rn.code() & kRegCodeMask) << Rn_offset;
  }

  // Flags encoding.
  inline static Instr Flags(FlagsUpdate S);
  inline static Instr Cond(Condition cond);

  // PC-relative address encoding.
  inline static Instr ImmPCRelAddress(int imm21);

  // Branch encoding.
  inline static Instr ImmUncondBranch(int imm26);
  inline static Instr ImmCondBranch(int imm19);
  inline static Instr ImmCmpBranch(int imm19);
  inline static Instr ImmTestBranch(int imm14);
  inline static Instr ImmTestBranchBit(unsigned bit_pos);

  // Data Processing encoding.
  inline static Instr SF(Register rd);
  inline static Instr ImmAddSub(int imm);
  inline static Instr ImmS(unsigned imms, unsigned reg_size);
  inline static Instr ImmR(unsigned immr, unsigned reg_size);
  inline static Instr ImmSetBits(unsigned imms, unsigned reg_size);
  inline static Instr ImmRotate(unsigned immr, unsigned reg_size);
  inline static Instr ImmLLiteral(int imm19);
  inline static Instr BitN(unsigned bitn, unsigned reg_size);
  inline static Instr ShiftDP(Shift shift);
  inline static Instr ImmDPShift(unsigned amount);
  inline static Instr ExtendMode(Extend extend);
  inline static Instr ImmExtendShift(unsigned left_shift);
  inline static Instr ImmCondCmp(unsigned imm);
  inline static Instr Nzcv(StatusFlags nzcv);

  static constexpr bool IsImmAddSub(int64_t immediate) {
    return is_uint12(immediate) ||
           (is_uint12(immediate >> 12) && ((immediate & 0xFFF) == 0));
  }

  static constexpr bool IsImmConditionalCompare(int64_t immediate) {
    return is_uint5(immediate);
  }

  static bool IsImmLogical(uint64_t value, unsigned width, unsigned* n,
                           unsigned* imm_s, unsigned* imm_r);

  // MemOperand offset encoding.
  inline static Instr ImmLSUnsigned(int imm12);
  inline static Instr ImmLS(int imm9);
  inline static Instr ImmLSPair(int imm7, unsigned size);
  inline static Instr ImmShiftLS(unsigned shift_amount);
  inline static Instr ImmException(int imm16);
  inline static Instr ImmSystemRegister(int imm15);
  inline static Instr ImmHint(int imm7);
  inline static Instr ImmBarrierDomain(int imm2);
  inline static Instr ImmBarrierType(int imm2);
  inline static unsigned CalcLSDataSizeLog2(LoadStoreOp op);

  // Instruction bits for vector format in data processing operations.
  static Instr VFormat(VRegister vd) {
    if (vd.Is64Bits()) {
      switch (vd.LaneCount()) {
        case 1:
          return NEON_1D;
        case 2:
          return NEON_2S;
        case 4:
          return NEON_4H;
        case 8:
          return NEON_8B;
        default:
          UNREACHABLE();
      }
    } else {
      DCHECK(vd.Is128Bits());
      switch (vd.LaneCount()) {
        case 2:
          return NEON_2D;
        case 4:
          return NEON_4S;
        case 8:
          return NEON_8H;
        case 16:
          return NEON_16B;
        default:
          UNREACHABLE();
      }
    }
  }

  // Instruction bits for vector format in floating point data processing
  // operations.
  static Instr FPFormat(VRegister vd) {
    if (vd.LaneCount() == 1) {
      // Floating point scalar formats.
      DCHECK(vd.Is32Bits() || vd.Is64Bits());
      return vd.Is64Bits() ? FP64 : FP32;
    }

    // Two lane floating point vector formats.
    if (vd.LaneCount() == 2) {
      DCHECK(vd.Is64Bits() || vd.Is128Bits());
      return vd.Is128Bits() ? NEON_FP_2D : NEON_FP_2S;
    }

    // Four lane floating point vector formats.
    if (vd.LaneCount() == 4) {
      DCHECK(vd.Is64Bits() || vd.Is128Bits());
      return vd.Is128Bits() ? NEON_FP_4S : NEON_FP_4H;
    }

    // Eight lane floating point vector format.
    DCHECK((vd.LaneCount() == 8) && vd.Is128Bits());
    return NEON_FP_8H;
  }

  // Instruction bits for vector format in load and store operations.
  static Instr LSVFormat(VRegister vd) {
    if (vd.Is64Bits()) {
      switch (vd.LaneCount()) {
        case 1:
          return LS_NEON_1D;
        case 2:
          return LS_NEON_2S;
        case 4:
          return LS_NEON_4H;
        case 8:
          return LS_NEON_8B;
        default:
          UNREACHABLE();
      }
    } else {
      DCHECK(vd.Is128Bits());
      switch (vd.LaneCount()) {
        case 2:
          return LS_NEON_2D;
        case 4:
          return LS_NEON_4S;
        case 8:
          return LS_NEON_8H;
        case 16:
          return LS_NEON_16B;
        default:
          UNREACHABLE();
      }
    }
  }

  // Instruction bits for scalar format in data processing operations.
  static Instr SFormat(VRegister vd) {
    DCHECK(vd.IsScalar());
    switch (vd.SizeInBytes()) {
      case 1:
        return NEON_B;
      case 2:
        return NEON_H;
      case 4:
        return NEON_S;
      case 8:
        return NEON_D;
      default:
        UNREACHABLE();
    }
  }

  static Instr ImmNEONHLM(int index, int num_bits) {
    int h, l, m;
    if (num_bits == 3) {
      DCHECK(is_uint3(index));
      h = (index >> 2) & 1;
      l = (index >> 1) & 1;
      m = (index >> 0) & 1;
    } else if (num_bits == 2) {
      DCHECK(is_uint2(index));
      h = (index >> 1) & 1;
      l = (index >> 0) & 1;
      m = 0;
    } else {
      DCHECK(is_uint1(index) && (num_bits == 1));
      h = (index >> 0) & 1;
      l = 0;
      m = 0;
    }
    return (h << NEONH_offset) | (l << NEONL_offset) | (m << NEONM_offset);
  }

  static Instr ImmNEONExt(int imm4) {
    DCHECK(is_uint4(imm4));
    return imm4 << ImmNEONExt_offset;
  }

  static Instr ImmNEON5(Instr format, int index) {
    DCHECK(is_uint4(index));
    int s = LaneSizeInBytesLog2FromFormat(static_cast<VectorFormat>(format));
    int imm5 = (index << (s + 1)) | (1 << s);
    return imm5 << ImmNEON5_offset;
  }

  static Instr ImmNEON4(Instr format, int index) {
    DCHECK(is_uint4(index));
    int s = LaneSizeInBytesLog2FromFormat(static_cast<VectorFormat>(format));
    int imm4 = index << s;
    return imm4 << ImmNEON4_offset;
  }

  static Instr ImmNEONabcdefgh(int imm8) {
    DCHECK(is_uint8(imm8));
    Instr instr;
    instr = ((imm8 >> 5) & 7) << ImmNEONabc_offset;
    instr |= (imm8 & 0x1f) << ImmNEONdefgh_offset;
    return instr;
  }

  static Instr NEONCmode(int cmode) {
    DCHECK(is_uint4(cmode));
    return cmode << NEONCmode_offset;
  }

  static Instr NEONModImmOp(int op) {
    DCHECK(is_uint1(op));
    return op << NEONModImmOp_offset;
  }

  static constexpr bool IsImmLSUnscaled(int64_t offset) {
    return is_int9(offset);
  }
  static constexpr bool IsImmLSScaled(int64_t offset, unsigned size_log2) {
    bool offset_is_size_multiple =
        (static_cast<int64_t>(static_cast<uint64_t>(offset >> size_log2)
                              << size_log2) == offset);
    return offset_is_size_multiple && is_uint12(offset >> size_log2);
  }
  static bool IsImmLLiteral(int64_t offset);

  // Move immediates encoding.
  inline static Instr ImmMoveWide(int imm);
  inline static Instr ShiftMoveWide(int shift);

  // FP Immediates.
  static Instr ImmFP(double imm);
  static Instr ImmNEONFP(double imm);
  inline static Instr FPScale(unsigned scale);

  // FP register type.
  inline static Instr FPType(VRegister fd);

  // Unused on this architecture.
  void MaybeEmitOutOfLineConstantPool() {}

  void ForceConstantPoolEmissionWithoutJump() {
    constpool_.Check(Emission::kForced, Jump::kOmitted);
  }
  void ForceConstantPoolEmissionWithJump() {
    constpool_.Check(Emission::kForced, Jump::kRequired);
  }
  // Check if the const pool needs to be emitted while pretending that {margin}
  // more bytes of instructions have already been emitted.
  void EmitConstPoolWithJumpIfNeeded(size_t margin = 0) {
    if (constpool_.IsEmpty()) return;
    constpool_.Check(Emission::kIfNeeded, Jump::kRequired, margin);
  }

  // Used by veneer checks below - returns the max (= overapproximated) pc
  // offset after the veneer pool, if the veneer pool were to be emitted
  // immediately.
  intptr_t MaxPCOffsetAfterVeneerPoolIfEmittedNow(size_t margin);
  // Returns true if we should emit a veneer as soon as possible for a branch
  // which can at most reach to specified pc.
  bool ShouldEmitVeneer(int max_reachable_pc, size_t margin) {
    return max_reachable_pc < MaxPCOffsetAfterVeneerPoolIfEmittedNow(margin);
  }
  bool ShouldEmitVeneers(size_t margin = kVeneerDistanceMargin) {
    return ShouldEmitVeneer(unresolved_branches_first_limit(), margin);
  }

  // The code size generated for a veneer. Currently one branch
  // instruction. This is for code size checking purposes, and can be extended
  // in the future for example if we decide to add nops between the veneers.
  static constexpr int kVeneerCodeSize = 1 * kInstrSize;

  void RecordVeneerPool(int location_offset, int size);
  // Emits veneers for branches that are approaching their maximum range.
  // If need_protection is true, the veneers are protected by a branch jumping
  // over the code.
  void EmitVeneers(bool force_emit, bool need_protection,
                   size_t margin = kVeneerDistanceMargin);
  void EmitVeneersGuard() { EmitPoolGuard(); }
  // Checks whether veneers need to be emitted at this point.
  // If force_emit is set, a veneer is generated for *all* unresolved branches.
  void CheckVeneerPool(bool force_emit, bool require_jump,
                       size_t margin = kVeneerDistanceMargin);

  using BlockConstPoolScope = ConstantPool::BlockScope;

  class V8_NODISCARD BlockPoolsScope {
   public:
    // Block veneer and constant pool. Emits pools if necessary to ensure that
    // {margin} more bytes can be emitted without triggering pool emission.
    explicit BlockPoolsScope(Assembler* assem, size_t margin = 0)
        : assem_(assem), block_const_pool_(assem, margin) {
      assem_->CheckVeneerPool(false, true, margin);
      assem_->StartBlockVeneerPool();
    }

    BlockPoolsScope(Assembler* assem, PoolEmissionCheck check)
        : assem_(assem), block_const_pool_(assem, check) {
      assem_->StartBlockVeneerPool();
    }
    ~BlockPoolsScope() { assem_->EndBlockVeneerPool(); }

   private:
    Assembler* assem_;
    BlockConstPoolScope block_const_pool_;
    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockPoolsScope);
  };

#if defined(V8_OS_WIN)
  win64_unwindinfo::XdataEncoder* GetXdataEncoder() {
    return xdata_encoder_.get();
  }

  win64_unwindinfo::BuiltinUnwindInfo GetUnwindInfo() const;
#endif

 protected:
  inline const Register& AppropriateZeroRegFor(const CPURegister& reg) const;

  void LoadStore(const CPURegister& rt, const MemOperand& addr, LoadStoreOp op);
  inline void LoadStoreScaledImmOffset(Instr memop, int offset, unsigned size);
  inline void LoadStoreUnscaledImmOffset(Instr memop, int offset);
  inline void LoadStoreWRegOffset(Instr memop, const Register& regoffset);
  void LoadStorePair(const CPURegister& rt, const CPURegister& rt2,
                     const MemOperand& addr, LoadStorePairOp op);
  void LoadStoreStruct(const VRegister& vt, const MemOperand& addr,
                       NEONLoadStoreMultiStructOp op);
  void LoadStoreStruct1(const VRegister& vt, int reg_count,
                        const MemOperand& addr);
  void LoadStoreStructSingle(const VRegister& vt, uint32_t lane,
                             const MemOperand& addr,
                             NEONLoadStoreSingleStructOp op);
  void LoadStoreStructSingleAllLanes(const VRegister& vt,
                                     const MemOperand& addr,
                                     NEONLoadStoreSingleStructOp op);
  void LoadStoreStructVerify(const VRegister& vt, const MemOperand& addr,
                             Instr op);

  static bool IsImmLSPair(int64_t offset, unsigned size);

  void Logical(const Register& rd, const Register& rn, const Operand& operand,
               LogicalOp op);
  void LogicalImmediate(const Register& rd, const Register& rn, unsigned n,
                        unsigned imm_s, unsigned imm_r, LogicalOp op);

  void ConditionalCompare(const Register& rn, const Operand& operand,
                          StatusFlags nzcv, Condition cond,
                          ConditionalCompareOp op);

  void AddSubWithCarry(const Register& rd, const Register& rn,
                       const Operand& operand, FlagsUpdate S,
                       AddSubWithCarryOp op);

  // Functions for emulating operands not directly supported by the instruction
  // set.
  void EmitShift(const Register& rd, const Register& rn, Shift shift,
                 unsigned amount);
  void EmitExtendShift(const Register& rd, const Register& rn, Extend extend,
                       unsigned left_shift);

  void AddSub(const Register& rd, const Register& rn, const Operand& operand,
              FlagsUpdate S, AddSubOp op);

  inline void DataProcPlainRegister(const Register& rd, const Register& rn,
                                    const Register& rm, Instr op);
  inline void CmpPlainRegister(const Register& rn, const Register& rm);
  inline void DataProcImmediate(const Register& rd, const Register& rn,
                                int immediate, Instr op);

  static bool IsImmFP32(uint32_t bits);
  static bool IsImmFP64(uint64_t bits);

  // Find an appropriate LoadStoreOp or LoadStorePairOp for the specified
  // registers. Only simple loads are supported; sign- and zero-extension (such
  // as in LDPSW_x or LDRB_w) are not supported.
  static inline LoadStoreOp LoadOpFor(const CPURegister& rt);
  static inline LoadStorePairOp LoadPairOpFor(const CPURegister& rt,
                                              const CPURegister& rt2);
  static inline LoadStoreOp StoreOpFor(const CPURegister& rt);
  static inline LoadStorePairOp StorePairOpFor(const CPURegister& rt,
                                               const CPURegister& rt2);
  static inline LoadLiteralOp LoadLiteralOpFor(const CPURegister& rt);

  // Remove the specified branch from the unbound label link chain.
  // If available, a veneer for this label can be used for other branches in the
  // chain if the link chain cannot be fixed up without this branch.
  void RemoveBranchFromLabelLinkChain(Instruction* branch, Label* label,
                                      Instruction* label_veneer = nullptr);

 private:
  static uint32_t FPToImm8(double imm);

  // Instruction helpers.
  void MoveWide(const Register& rd, uint64_t imm, int shift,
                MoveWideImmediateOp mov_op);
  void DataProcShiftedRegister(const Register& rd, const Register& rn,
                               const Operand& operand, FlagsUpdate S, Instr op);
  void DataProcExtendedRegister(const Register& rd, const Register& rn,
                                const Operand& operand, FlagsUpdate S,
                                Instr op);
  void ConditionalSelect(const Register& rd, const Register& rn,
                         const Register& rm, Condition cond,
                         ConditionalSelectOp op);
  void DataProcessing1Source(const Register& rd, const Register& rn,
                             DataProcessing1SourceOp op);
  void DataProcessing3Source(const Register& rd, const Register& rn,
                             const Register& rm, const Register& ra,
                             DataProcessing3SourceOp op);
  void FPDataProcessing1Source(const VRegister& fd, const VRegister& fn,
                               FPDataProcessing1SourceOp op);
  void FPDataProcessing2Source(const VRegister& fd, const VRegister& fn,
                               const VRegister& fm,
                               FPDataProcessing2SourceOp op);
  void FPDataProcessing3Source(const VRegister& fd, const VRegister& fn,
                               const VRegister& fm, const VRegister& fa,
                               FPDataProcessing3SourceOp op);
  void NEONAcrossLanesL(const VRegister& vd, const VRegister& vn,
                        NEONAcrossLanesOp op);
  void NEONAcrossLanes(const VRegister& vd, const VRegister& vn,
                       NEONAcrossLanesOp op);
  void NEONModifiedImmShiftLsl(const VRegister& vd, const int imm8,
                               const int left_shift,
                               NEONModifiedImmediateOp op);
  void NEONModifiedImmShiftMsl(const VRegister& vd, const int imm8,
                               const int shift_amount,
                               NEONModifiedImmediateOp op);
  void NEON3Same(const VRegister& vd, const VRegister& vn, const VRegister& vm,
                 NEON3SameOp vop);
  void NEONFP3Same(const VRegister& vd, const VRegister& vn,
                   const VRegister& vm, Instr op);
  void NEON3DifferentL(const VRegister& vd, const VRegister& vn,
                       const VRegister& vm, NEON3DifferentOp vop);
  void NEON3DifferentW(const VRegister& vd, const VRegister& vn,
                       const VRegister& vm, NEON3DifferentOp vop);
  void NEON3DifferentHN(const VRegister& vd, const VRegister& vn,
                        const VRegister& vm, NEON3DifferentOp vop);
  void NEONFP2RegMisc(const VRegister& vd, const VRegister& vn,
                      NEON2RegMiscOp vop, double value);
  void NEON2RegMisc(const VRegister& vd, const VRegister& vn,
                    NEON2RegMiscOp vop, int value = 0);
  void NEONFP2RegMisc(const VRegister& vd, const VRegister& vn, Instr op);
  void NEONAddlp(const VRegister& vd, const VRegister& vn, NEON2RegMiscOp op);
  void NEONPerm(const VRegister& vd, const VRegister& vn, const VRegister& vm,
                NEONPermOp op);
  void NEONFPByElement(const VRegister& vd, const VRegister& vn,
                       const VRegister& vm, int vm_index,
                       NEONByIndexedElementOp op);
  void NEONByElement(const VRegister& vd, const VRegister& vn,
                     const VRegister& vm, int vm_index,
                     NEONByIndexedElementOp op);
  void NEONByElementL(const VRegister& vd, const VRegister& vn,
                      const VRegister& vm, int vm_index,
                      NEONByIndexedElementOp op);
  void NEONShiftImmediate(const VRegister& vd, const VRegister& vn,
                          NEONShiftImmediateOp op, int immh_immb);
  void NEONShiftLeftImmediate(const VRegister& vd, const VRegister& vn,
                              int shift, NEONShiftImmediateOp op);
  void NEONShiftRightImmediate(const VRegister& vd, const VRegister& vn,
                               int shift, NEONShiftImmediateOp op);
  void NEONShiftImmediateL(const VRegister& vd, const VRegister& vn, int shift,
                           NEONShiftImmediateOp op);
  void NEONShiftImmediateN(const VRegister& vd, const VRegister& vn, int shift,
                           NEONShiftImmediateOp op);
  void NEONXtn(const VRegister& vd, const VRegister& vn, NEON2RegMiscOp vop);
  void NEONTable(const VRegister& vd, const VRegister& vn, const VRegister& vm,
                 NEONTableOp op);

  Instr LoadStoreStructAddrModeField(const MemOperand& addr);

  // Label helpers.

  // Return an offset for a label-referencing instruction, typically a branch.
  int LinkAndGetByteOffsetTo(Label* label);

  // This is the same as LinkAndGetByteOffsetTo, but return an offset
  // suitable for fields that take instruction offsets: branches.
  inline int LinkAndGetBranchInstructionOffsetTo(Label* label);

  static constexpr int kStartOfLabelLinkChain = 0;

  // Verify that a label's link chain is intact.
  void CheckLabelLinkChain(Label const* label);

  // Emit the instruction at pc_.
  void Emit(Instr instruction) {
    static_assert(sizeof(*pc_) == 1);
    static_assert(sizeof(instruction) == kInstrSize);
    DCHECK_LE(pc_ + sizeof(instruction), buffer_start_ + buffer_->size());

    memcpy(pc_, &instruction, sizeof(instruction));
    pc_ += sizeof(instruction);
    CheckBuffer();
  }

  // Emit data inline in the instruction stream.
  void EmitData(void const* data, unsigned size) {
    DCHECK_EQ(sizeof(*pc_), 1);
    DCHECK_LE(pc_ + size, buffer_start_ + buffer_->size());

    // TODO(all): Somehow register we have some data here. Then we can
    // disassemble it correctly.
    memcpy(pc_, data, size);
    pc_ += size;
    CheckBuffer();
  }

  void GrowBuffer();

  void CheckBufferSpace() {
    DCHECK_LT(pc_, buffer_start_ + buffer_->size());
    if (V8_UNLIKELY(buffer_space() < kGap)) {
      GrowBuffer();
    }
  }

  void CheckBuffer() {
    CheckBufferSpace();
    if (pc_offset() >= next_veneer_pool_check_) {
      CheckVeneerPool(false, true);
    }
    constpool_.MaybeCheck();
  }

  // Emission of the veneer pools may be blocked in some code sequences.
  int veneer_pool_blocked_nesting_ = 0;  // Block emission if this is not zero.

  // Relocation info generation
  // Each relocation is encoded as a variable size value
  static constexpr int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  RelocInfoWriter reloc_info_writer;

  // Internal reference positions, required for (potential) patching in
  // GrowBuffer(); contains only those internal references whose labels
  // are already bound.
  std::deque<int> internal_reference_positions_;

 protected:
  // InstructionStream generation
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries, and debug strings encoded in the instruction
  // stream.
  static constexpr int kGap = 64;
  static_assert(AssemblerBase::kMinimalBufferSize >= 2 * kGap);

 public:
#ifdef DEBUG
  // Functions used for testing.
  size_t GetConstantPoolEntriesSizeForTesting() const {
    // Do not include branch over the pool.
    return constpool_.Entry32Count() * kInt32Size +
           constpool_.Entry64Count() * kInt64Size;
  }

  static size_t GetCheckConstPoolIntervalForTesting() {
    return ConstantPool::kCheckInterval;
  }

  static size_t GetApproxMaxDistToConstPoolForTesting() {
    return ConstantPool::kApproxDistToPool64;
  }
#endif

 protected:
  const AssemblerZone zone_;

  // Information about unresolved (forward) branches.
  // The Assembler is only allowed to delete out-of-date information from here
  // after a label is bound. The MacroAssembler uses this information to
  // generate veneers.
  //
  // The first member of the pair (max_pc) is the maximum offset that the branch
  // can reach in the buffer, with the bottom bit set to indicate a
  // test-and-branch instruction. This bit is used to help in calculating the
  // address of the branch, ie.
  //
  //   branch_addr = { max_pc - 2^21,     if max_pc<0> == 0 (B.cond, CB[N]Z)
  //                 { max_pc - 2^16 - 1, if max_pc<0> == 1 (TB[N]Z)
  //
  // The second member is a pointer to the Label targetted by the branch.
  //
  // The map is sorted according to the reachable offset, max_pc, allowing to
  // check easily when veneers need to be emitted.
  // Note that the maximum reachable offset (first member of the pairs) should
  // always be positive but has the same type as the return value for
  // pc_offset() for convenience.
  ZoneAbslBTreeMap<int, Label*> unresolved_branches_;

  // Back edge offsets for the link chain - the forward edge is stored in the
  // generated code. This is used to accelerate removing branches from the
  // link chain when emitting veneers.
  absl::flat_hash_map<int, int> branch_link_chain_back_edge_;

  // We generate a veneer for a branch if we reach within this distance of the
  // limit of the range.
  static constexpr int kVeneerDistanceMargin = 1 * KB;
  // The factor of 2 is a finger in the air guess. With a default margin of
  // 1KB, that leaves us an addional 256 instructions to avoid generating a
  // protective branch.
  static constexpr int kVeneerNoProtectionFactor = 2;
  static constexpr int kVeneerDistanceCheckMargin =
      kVeneerNoProtectionFactor * kVeneerDistanceMargin;
  int unresolved_branches_first_limit() const {
    DCHECK(!unresolved_branches_.empty());

    // Mask branch type tag bit.
    return unresolved_branches_.begin()->first & ~1;
  }

  // This PC-offset of the next veneer pool check helps reduce the overhead
  // of checking for veneer pools.
  // It is maintained to the closest unresolved branch limit minus the maximum
  // veneer margin (or kMaxInt if there are no unresolved branches).
  int next_veneer_pool_check_;

#if defined(V8_OS_WIN)
  std::unique_ptr<win64_unwindinfo::XdataEncoder> xdata_encoder_;
#endif

 private:
  // Avoid overflows for displacements etc.
  static const int kMaximalBufferSize = 512 * MB;

  // If a veneer is emitted for a branch instruction, that instruction must be
  // removed from the associated label's link chain so that the assembler does
  // not later attempt (likely unsuccessfully) to patch it to branch directly to
  // the label.
  void DeleteUnresolvedBranchInfoForLabel(Label* label);
  // This function deletes the information related to the label by traversing
  // the label chain, and for each PC-relative instruction in the chain checking
  // if pending unresolved information exists. Its complexity is proportional to
  // the length of the label chain.
  void DeleteUnresolvedBranchInfoForLabelTraverse(Label* label);

  void AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate);

  int WriteCodeComments();

  // The pending constant pool.
  ConstantPool constpool_;

  friend class EnsureSpace;
  friend class ConstantPool;
};

class PatchingAssembler : public Assembler {
 public:
  // Create an Assembler with a buffer starting at 'start'.
  // The buffer size is
  //   size of instructions to patch + kGap
  // Where kGap is the distance from which the Assembler tries to grow the
  // buffer.
  // If more or fewer instructions than expected are generated or if some
  // relocation information takes space in the buffer, the PatchingAssembler
  // will crash trying to grow the buffer.
  // Note that the instruction cache will not be flushed.
  PatchingAssembler(Zone* zone, const AssemblerOptions& options, uint8_t* start,
                    unsigned count)
      : Assembler(zone, options,
                  ExternalAssemblerBuffer(start, count * kInstrSize + kGap)),
        block_constant_pool_emission_scope(this) {}

  ~PatchingAssembler() {
    // Verify we have generated the number of instruction we expected.
    DCHECK_EQ(pc_offset() + kGap, buffer_->size());
  }

  // See definition of PatchAdrFar() for details.
  static constexpr int kAdrFarPatchableNNops = 2;
  static constexpr int kAdrFarPatchableNInstrs = kAdrFarPatchableNNops + 2;
  void PatchAdrFar(int64_t target_offset);
  void PatchSubSp(uint32_t immediate);

 private:
  BlockPoolsScope block_constant_pool_emission_scope;
};

class EnsureSpace {
 public:
  explicit V8_INLINE EnsureSpace(Assembler* assembler);

 private:
  Assembler::BlockPoolsScope block_pools_scope_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM64_ASSEMBLER_ARM64_H_
