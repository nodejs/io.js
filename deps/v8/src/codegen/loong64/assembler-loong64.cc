// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/loong64/assembler-loong64.h"

#if V8_TARGET_ARCH_LOONG64

#include "src/base/cpu.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/codegen/loong64/assembler-loong64-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/safepoint-table.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/objects/heap-number-inl.h"

namespace v8 {
namespace internal {

bool CpuFeatures::SupportsWasmSimd128() { return false; }

void CpuFeatures::ProbeImpl(bool cross_compile) {
  supported_ |= 1u << FPU;

  // Only use statically determined features for cross compile (snapshot).
  if (cross_compile) return;

#ifdef __loongarch__
  // Probe for additional features at runtime.
  base::CPU cpu;
  supported_ |= 1u << FPU;
#endif

  // Set a static value on whether Simd is supported.
  // This variable is only used for certain archs to query SupportWasmSimd128()
  // at runtime in builtins using an extern ref. Other callers should use
  // CpuFeatures::SupportWasmSimd128().
  CpuFeatures::supports_wasm_simd_128_ = CpuFeatures::SupportsWasmSimd128();
}

void CpuFeatures::PrintTarget() {}
void CpuFeatures::PrintFeatures() {}

int ToNumber(Register reg) {
  DCHECK(reg.is_valid());
  const int kNumbers[] = {
      0,   // zero_reg
      1,   // ra
      2,   // tp
      3,   // sp
      4,   // a0 v0
      5,   // a1 v1
      6,   // a2
      7,   // a3
      8,   // a4
      9,   // a5
      10,  // a6
      11,  // a7
      12,  // t0
      13,  // t1
      14,  // t2
      15,  // t3
      16,  // t4
      17,  // t5
      18,  // t6
      19,  // t7
      20,  // t8
      21,  // x_reg
      22,  // fp
      23,  // s0
      24,  // s1
      25,  // s2
      26,  // s3
      27,  // s4
      28,  // s5
      29,  // s6
      30,  // s7
      31,  // s8
  };
  return kNumbers[reg.code()];
}

Register ToRegister(int num) {
  DCHECK(num >= 0 && num < kNumRegisters);
  const Register kRegisters[] = {
      zero_reg, ra, tp, sp, a0, a1,    a2, a3, a4, a5, a6, a7, t0, t1, t2, t3,
      t4,       t5, t6, t7, t8, x_reg, fp, s0, s1, s2, s3, s4, s5, s6, s7, s8};
  return kRegisters[num];
}

// -----------------------------------------------------------------------------
// Implementation of RelocInfo.

const int RelocInfo::kApplyMask =
    RelocInfo::ModeMask(RelocInfo::NEAR_BUILTIN_ENTRY) |
    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
    RelocInfo::ModeMask(RelocInfo::RELATIVE_CODE_TARGET);

bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially coded.  Being
  // specially coded on LoongArch64 means that it is a lu12i_w/ori instruction,
  // and that is always the case inside code objects.
  return true;
}

bool RelocInfo::IsInConstantPool() { return false; }

uint32_t RelocInfo::wasm_call_tag() const {
  DCHECK(rmode_ == WASM_CALL || rmode_ == WASM_STUB_CALL);
  return static_cast<uint32_t>(
      Assembler::target_address_at(pc_, constant_pool_));
}

// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand.
// See assembler-loong64-inl.h for inlined constructors.

Operand::Operand(Handle<HeapObject> handle)
    : rm_(no_reg), rmode_(RelocInfo::FULL_EMBEDDED_OBJECT) {
  value_.immediate = static_cast<intptr_t>(handle.address());
}

Operand Operand::EmbeddedNumber(double value) {
  int32_t smi;
  if (DoubleToSmiInteger(value, &smi)) return Operand(Smi::FromInt(smi));
  Operand result(0, RelocInfo::FULL_EMBEDDED_OBJECT);
  result.is_heap_number_request_ = true;
  result.value_.heap_number_request = HeapNumberRequest(value);
  return result;
}

MemOperand::MemOperand(Register base, int32_t offset)
    : base_(base), index_(no_reg), offset_(offset) {}

MemOperand::MemOperand(Register base, Register index)
    : base_(base), index_(index), offset_(0) {}

void Assembler::AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate) {
  DCHECK_IMPLIES(isolate == nullptr, heap_number_requests_.empty());
  for (auto& request : heap_number_requests_) {
    Handle<HeapObject> object;
    object = isolate->factory()->NewHeapNumber<AllocationType::kOld>(
        request.heap_number());
    Address pc = reinterpret_cast<Address>(buffer_start_) + request.offset();
    EmbeddedObjectIndex index = AddEmbeddedObject(object);
    if (IsLu32i_d(instr_at(pc + 2 * kInstrSize))) {
      set_target_value_at(pc, static_cast<uint64_t>(index));
    } else {
      set_target_compressed_value_at(pc, static_cast<uint32_t>(index));
    }
  }
}

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

Assembler::Assembler(const AssemblerOptions& options,
                     std::unique_ptr<AssemblerBuffer> buffer)
    : AssemblerBase(options, std::move(buffer)),
      scratch_register_list_({t7, t6}),
      scratch_fpregister_list_({f31}) {
  reloc_info_writer.Reposition(buffer_start_ + buffer_->size(), pc_);

  last_trampoline_pool_end_ = 0;
  no_trampoline_pool_before_ = 0;
  trampoline_pool_blocked_nesting_ = 0;
  // We leave space (16 * kTrampolineSlotsSize)
  // for BlockTrampolinePoolScope buffer.
  next_buffer_check_ = v8_flags.force_long_branches
                           ? kMaxInt
                           : kMax16BranchOffset - kTrampolineSlotsSize * 16;
  internal_trampoline_exception_ = false;
  last_bound_pos_ = 0;

  trampoline_emitted_ = v8_flags.force_long_branches;
  unbound_labels_count_ = 0;
  block_buffer_growth_ = false;
}

void Assembler::GetCode(Isolate* isolate, CodeDesc* desc) {
  GetCode(isolate->main_thread_local_isolate(), desc);
}
void Assembler::GetCode(LocalIsolate* isolate, CodeDesc* desc,
                        SafepointTableBuilderBase* safepoint_table_builder,
                        int handler_table_offset) {
  // As a crutch to avoid having to add manual Align calls wherever we use a
  // raw workflow to create InstructionStream objects (mostly in tests), add
  // another Align call here. It does no harm - the end of the InstructionStream
  // object is aligned to the (larger) kCodeAlignment anyways.
  // TODO(jgruber): Consider moving responsibility for proper alignment to
  // metadata table builders (safepoint, handler, constant pool, code
  // comments).
  DataAlign(InstructionStream::kMetadataAlignment);

  // EmitForbiddenSlotInstruction(); TODO:LOONG64 why?

  int code_comments_size = WriteCodeComments();

  DCHECK(pc_ <= reloc_info_writer.pos());  // No overlap.

  AllocateAndInstallRequestedHeapNumbers(isolate);

  // Set up code descriptor.
  // TODO(jgruber): Reconsider how these offsets and sizes are maintained up to
  // this point to make CodeDesc initialization less fiddly.

  static constexpr int kConstantPoolSize = 0;
  const int instruction_size = pc_offset();
  const int code_comments_offset = instruction_size - code_comments_size;
  const int constant_pool_offset = code_comments_offset - kConstantPoolSize;
  const int handler_table_offset2 = (handler_table_offset == kNoHandlerTable)
                                        ? constant_pool_offset
                                        : handler_table_offset;
  const int safepoint_table_offset =
      (safepoint_table_builder == kNoSafepointTable)
          ? handler_table_offset2
          : safepoint_table_builder->safepoint_table_offset();
  const int reloc_info_offset =
      static_cast<int>(reloc_info_writer.pos() - buffer_->start());
  CodeDesc::Initialize(desc, this, safepoint_table_offset,
                       handler_table_offset2, constant_pool_offset,
                       code_comments_offset, reloc_info_offset);
}

void Assembler::Align(int m) {
  // If not, the loop below won't terminate.
  DCHECK(IsAligned(pc_offset(), kInstrSize));
  DCHECK(m >= kInstrSize && base::bits::IsPowerOfTwo(m));
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}

void Assembler::CodeTargetAlign() {
  // No advantage to aligning branch/call targets to more than
  // single instruction, that I am aware of.
  Align(4);
}

Register Assembler::GetRkReg(Instr instr) {
  return Register::from_code((instr & kRkFieldMask) >> kRkShift);
}

Register Assembler::GetRjReg(Instr instr) {
  return Register::from_code((instr & kRjFieldMask) >> kRjShift);
}

Register Assembler::GetRdReg(Instr instr) {
  return Register::from_code((instr & kRdFieldMask) >> kRdShift);
}

uint32_t Assembler::GetRk(Instr instr) {
  return (instr & kRkFieldMask) >> kRkShift;
}

uint32_t Assembler::GetRkField(Instr instr) { return instr & kRkFieldMask; }

uint32_t Assembler::GetRj(Instr instr) {
  return (instr & kRjFieldMask) >> kRjShift;
}

uint32_t Assembler::GetRjField(Instr instr) { return instr & kRjFieldMask; }

uint32_t Assembler::GetRd(Instr instr) {
  return (instr & kRdFieldMask) >> kRdShift;
}

uint32_t Assembler::GetRdField(Instr instr) { return instr & kRdFieldMask; }

uint32_t Assembler::GetSa2(Instr instr) {
  return (instr & kSa2FieldMask) >> kSaShift;
}

uint32_t Assembler::GetSa2Field(Instr instr) { return instr & kSa2FieldMask; }

uint32_t Assembler::GetSa3(Instr instr) {
  return (instr & kSa3FieldMask) >> kSaShift;
}

uint32_t Assembler::GetSa3Field(Instr instr) { return instr & kSa3FieldMask; }

// Labels refer to positions in the (to be) generated code.
// There are bound, linked, and unused labels.
//
// Bound labels refer to known positions in the already
// generated code. pos() is the position the label refers to.
//
// Linked labels refer to unknown positions in the code
// to be generated; pos() is the position of the last
// instruction using the label.

// The link chain is terminated by a value in the instruction of 0,
// which is an otherwise illegal value (branch 0 is inf loop).
// The instruction 16-bit offset field addresses 32-bit words, but in
// code is conv to an 18-bit value addressing bytes, hence the -4 value.

const int kEndOfChain = 0;
// Determines the end of the Jump chain (a subset of the label link chain).
const int kEndOfJumpChain = 0;

bool Assembler::IsBranch(Instr instr) {
  uint32_t opcode = (instr >> 26) << 26;
  // Checks if the instruction is a branch.
  bool isBranch = opcode == BEQZ || opcode == BNEZ || opcode == BCZ ||
                  opcode == B || opcode == BL || opcode == BEQ ||
                  opcode == BNE || opcode == BLT || opcode == BGE ||
                  opcode == BLTU || opcode == BGEU;
  return isBranch;
}

bool Assembler::IsB(Instr instr) {
  uint32_t opcode = (instr >> 26) << 26;
  // Checks if the instruction is a b.
  bool isBranch = opcode == B || opcode == BL;
  return isBranch;
}

bool Assembler::IsBz(Instr instr) {
  uint32_t opcode = (instr >> 26) << 26;
  // Checks if the instruction is a branch.
  bool isBranch = opcode == BEQZ || opcode == BNEZ || opcode == BCZ;
  return isBranch;
}

bool Assembler::IsEmittedConstant(Instr instr) {
  // Add GetLabelConst function?
  uint32_t label_constant = instr & ~kImm16Mask;
  return label_constant == 0;  // Emitted label const in reg-exp engine.
}

bool Assembler::IsJ(Instr instr) {
  uint32_t opcode = (instr >> 26) << 26;
  // Checks if the instruction is a jump.
  return opcode == JIRL;
}

bool Assembler::IsLu12i_w(Instr instr) {
  uint32_t opcode = (instr >> 25) << 25;
  return opcode == LU12I_W;
}

bool Assembler::IsOri(Instr instr) {
  uint32_t opcode = (instr >> 22) << 22;
  return opcode == ORI;
}

bool Assembler::IsLu32i_d(Instr instr) {
  uint32_t opcode = (instr >> 25) << 25;
  return opcode == LU32I_D;
}

bool Assembler::IsLu52i_d(Instr instr) {
  uint32_t opcode = (instr >> 22) << 22;
  return opcode == LU52I_D;
}

bool Assembler::IsMov(Instr instr, Register rd, Register rj) {
  // Checks if the instruction is a OR with zero_reg argument (aka MOV).
  Instr instr1 =
      OR | zero_reg.code() << kRkShift | rj.code() << kRjShift | rd.code();
  return instr == instr1;
}

bool Assembler::IsPcAddi(Instr instr) {
  uint32_t opcode = (instr >> 25) << 25;
  return opcode == PCADDI;
}

bool Assembler::IsNop(Instr instr, unsigned int type) {
  // See Assembler::nop(type).
  DCHECK_LT(type, 32);

  Instr instr1 =
      ANDI | ((type & kImm12Mask) << kRkShift) | (zero_reg.code() << kRjShift);

  return instr == instr1;
}

static inline int32_t GetOffsetOfBranch(Instr instr,
                                        Assembler::OffsetSize bits) {
  int32_t result = 0;
  if (bits == 16) {
    result = (instr << 6) >> 16;
  } else if (bits == 21) {
    uint32_t low16 = instr << 6;
    low16 = low16 >> 16;
    low16 &= 0xffff;
    int32_t hi5 = (instr << 27) >> 11;
    result = hi5 | low16;
  } else {
    uint32_t low16 = instr << 6;
    low16 = low16 >> 16;
    low16 &= 0xffff;
    int32_t hi10 = (instr << 22) >> 6;
    result = hi10 | low16;
    DCHECK_EQ(bits, 26);
  }
  return result << 2;
}

static Assembler::OffsetSize OffsetSizeInBits(Instr instr) {
  if (Assembler::IsB(instr)) {
    return Assembler::OffsetSize::kOffset26;
  } else if (Assembler::IsBz(instr)) {
    return Assembler::OffsetSize::kOffset21;
  } else {
    DCHECK(Assembler::IsBranch(instr));
    return Assembler::OffsetSize::kOffset16;
  }
}

static inline int32_t AddBranchOffset(int pos, Instr instr) {
  Assembler::OffsetSize bits = OffsetSizeInBits(instr);

  int32_t imm = GetOffsetOfBranch(instr, bits);

  if (imm == kEndOfChain) {
    // EndOfChain sentinel is returned directly, not relative to pc or pos.
    return kEndOfChain;
  } else {
    // Handle the case that next branch position is 0.
    // TODO(LOONG_dev): Define -4 as a constant
    int32_t offset = pos + imm;
    return offset == 0 ? -4 : offset;
  }
}

int Assembler::target_at(int pos, bool is_internal) {
  if (is_internal) {
    int64_t* p = reinterpret_cast<int64_t*>(buffer_start_ + pos);
    int64_t address = *p;
    if (address == kEndOfJumpChain) {
      return kEndOfChain;
    } else {
      int64_t instr_address = reinterpret_cast<int64_t>(p);
      DCHECK(instr_address - address < INT_MAX);
      int delta = static_cast<int>(instr_address - address);
      DCHECK(pos > delta);
      return pos - delta;
    }
  }
  Instr instr = instr_at(pos);

  // TODO(LOONG_dev) remove after remove label_at_put?
  if ((instr & ~kImm16Mask) == 0) {
    // Emitted label constant, not part of a branch.
    if (instr == 0) {
      return kEndOfChain;
    } else {
      int32_t imm18 = ((instr & static_cast<int32_t>(kImm16Mask)) << 16) >> 14;
      return (imm18 + pos);
    }
  }

  // Check we have a branch, jump or pcaddi instruction.
  DCHECK(IsBranch(instr) || IsPcAddi(instr));
  // Do NOT change this to <<2. We rely on arithmetic shifts here, assuming
  // the compiler uses arithmetic shifts for signed integers.
  if (IsBranch(instr)) {
    return AddBranchOffset(pos, instr);
  } else if (IsPcAddi(instr)) {
    // see LoadLabelRelative
    int32_t si20;
    si20 = (instr >> kRjShift) & 0xfffff;
    if (si20 == kEndOfJumpChain) {
      // EndOfChain sentinel is returned directly, not relative to pc or pos.
      return kEndOfChain;
    }
    return pos + (si20 << 2);
  } else {
    UNREACHABLE();
  }
}

static inline Instr SetBranchOffset(int32_t pos, int32_t target_pos,
                                    Instr instr) {
  int32_t bits = OffsetSizeInBits(instr);
  int32_t imm = target_pos - pos;
  DCHECK_EQ(imm & 3, 0);
  imm >>= 2;

  DCHECK(is_intn(imm, bits));

  if (bits == 16) {
    const int32_t mask = ((1 << 16) - 1) << 10;
    instr &= ~mask;
    return instr | ((imm << 10) & mask);
  } else if (bits == 21) {
    const int32_t mask = 0x3fffc1f;
    instr &= ~mask;
    uint32_t low16 = (imm & kImm16Mask) << 10;
    int32_t hi5 = (imm >> 16) & 0x1f;
    return instr | low16 | hi5;
  } else {
    DCHECK_EQ(bits, 26);
    const int32_t mask = 0x3ffffff;
    instr &= ~mask;
    uint32_t low16 = (imm & kImm16Mask) << 10;
    int32_t hi10 = (imm >> 16) & 0x3ff;
    return instr | low16 | hi10;
  }
}

void Assembler::target_at_put(int pos, int target_pos, bool is_internal) {
  if (is_internal) {
    uint64_t imm = reinterpret_cast<uint64_t>(buffer_start_) + target_pos;
    *reinterpret_cast<uint64_t*>(buffer_start_ + pos) = imm;
    return;
  }
  Instr instr = instr_at(pos);
  if ((instr & ~kImm16Mask) == 0) {
    DCHECK(target_pos == kEndOfChain || target_pos >= 0);
    // Emitted label constant, not part of a branch.
    // Make label relative to Code pointer of generated Code object.
    instr_at_put(
        pos, target_pos + (InstructionStream::kHeaderSize - kHeapObjectTag));
    return;
  }

  if (IsPcAddi(instr)) {
    // For LoadLabelRelative function.
    int32_t imm = target_pos - pos;
    DCHECK_EQ(imm & 3, 0);
    DCHECK(is_int22(imm));
    uint32_t siMask = 0xfffff << kRjShift;
    uint32_t si20 = ((imm >> 2) << kRjShift) & siMask;
    instr = (instr & ~siMask) | si20;
    instr_at_put(pos, instr);
    return;
  }

  DCHECK(IsBranch(instr));
  instr = SetBranchOffset(pos, target_pos, instr);
  instr_at_put(pos, instr);
}

void Assembler::print(const Label* L) {
  if (L->is_unused()) {
    PrintF("unused label\n");
  } else if (L->is_bound()) {
    PrintF("bound label to %d\n", L->pos());
  } else if (L->is_linked()) {
    Label l;
    l.link_to(L->pos());
    PrintF("unbound label");
    while (l.is_linked()) {
      PrintF("@ %d ", l.pos());
      Instr instr = instr_at(l.pos());
      if ((instr & ~kImm16Mask) == 0) {
        PrintF("value\n");
      } else {
        PrintF("%d\n", instr);
      }
      next(&l, is_internal_reference(&l));
    }
  } else {
    PrintF("label in inconsistent state (pos = %d)\n", L->pos_);
  }
}

void Assembler::bind_to(Label* L, int pos) {
  DCHECK(0 <= pos && pos <= pc_offset());  // Must have valid binding position.
  int trampoline_pos = kInvalidSlotPos;
  bool is_internal = false;
  if (L->is_linked() && !trampoline_emitted_) {
    unbound_labels_count_--;
    if (!is_internal_reference(L)) {
      next_buffer_check_ += kTrampolineSlotsSize;
    }
  }

  while (L->is_linked()) {
    int fixup_pos = L->pos();
    int dist = pos - fixup_pos;
    is_internal = is_internal_reference(L);
    next(L, is_internal);  // Call next before overwriting link with target at
                           // fixup_pos.
    Instr instr = instr_at(fixup_pos);
    if (is_internal) {
      target_at_put(fixup_pos, pos, is_internal);
    } else {
      if (IsBranch(instr)) {
        int branch_offset = BranchOffset(instr);
        if (dist > branch_offset) {
          if (trampoline_pos == kInvalidSlotPos) {
            trampoline_pos = get_trampoline_entry(fixup_pos);
            CHECK_NE(trampoline_pos, kInvalidSlotPos);
          }
          CHECK((trampoline_pos - fixup_pos) <= branch_offset);
          target_at_put(fixup_pos, trampoline_pos, false);
          fixup_pos = trampoline_pos;
        }
        target_at_put(fixup_pos, pos, false);
      } else {
        DCHECK(IsJ(instr) || IsLu12i_w(instr) || IsEmittedConstant(instr) ||
               IsPcAddi(instr));
        target_at_put(fixup_pos, pos, false);
      }
    }
  }
  L->bind_to(pos);

  // Keep track of the last bound label so we don't eliminate any instructions
  // before a bound label.
  if (pos > last_bound_pos_) last_bound_pos_ = pos;
}

void Assembler::bind(Label* L) {
  DCHECK(!L->is_bound());  // Label can only be bound once.
  bind_to(L, pc_offset());
}

void Assembler::next(Label* L, bool is_internal) {
  DCHECK(L->is_linked());
  int link = target_at(L->pos(), is_internal);
  if (link == kEndOfChain) {
    L->Unuse();
  } else if (link == -4) {
    // Next position is pc_offset == 0
    L->link_to(0);
  } else {
    DCHECK_GE(link, 0);
    L->link_to(link);
  }
}

bool Assembler::is_near_c(Label* L) {
  DCHECK(L->is_bound());
  return pc_offset() - L->pos() < kMax16BranchOffset - 4 * kInstrSize;
}

bool Assembler::is_near(Label* L, OffsetSize bits) {
  DCHECK(L->is_bound());
  return ((pc_offset() - L->pos()) <
          (1 << (bits + 2 - 1)) - 1 - 5 * kInstrSize);
}

bool Assembler::is_near_a(Label* L) {
  DCHECK(L->is_bound());
  return pc_offset() - L->pos() <= kMax26BranchOffset - 4 * kInstrSize;
}

int Assembler::BranchOffset(Instr instr) {
  int bits = OffsetSize::kOffset16;

  uint32_t opcode = (instr >> 26) << 26;
  switch (opcode) {
    case B:
    case BL:
      bits = OffsetSize::kOffset26;
      break;
    case BNEZ:
    case BEQZ:
    case BCZ:
      bits = OffsetSize::kOffset21;
      break;
    case BNE:
    case BEQ:
    case BLT:
    case BGE:
    case BLTU:
    case BGEU:
    case JIRL:
      bits = OffsetSize::kOffset16;
      break;
    default:
      break;
  }

  return (1 << (bits + 2 - 1)) - 1;
}

// We have to use a temporary register for things that can be relocated even
// if they can be encoded in the LOONG's 16 bits of immediate-offset
// instruction space. There is no guarantee that the relocated location can be
// similarly encoded.
bool Assembler::MustUseReg(RelocInfo::Mode rmode) {
  return !RelocInfo::IsNoInfo(rmode);
}

void Assembler::GenB(Opcode opcode, Register rj, int32_t si21) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK((BEQZ == opcode || BNEZ == opcode) && is_int21(si21) && rj.is_valid());
  Instr instr = opcode | (si21 & kImm16Mask) << kRkShift |
                (rj.code() << kRjShift) | ((si21 & 0x1fffff) >> 16);
  emit(instr);
}

void Assembler::GenB(Opcode opcode, CFRegister cj, int32_t si21, bool isEq) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(BCZ == opcode && is_int21(si21));
  DCHECK(cj >= 0 && cj <= 7);
  int32_t sc = (isEq ? cj : cj + 8);
  Instr instr = opcode | (si21 & kImm16Mask) << kRkShift | (sc << kRjShift) |
                ((si21 & 0x1fffff) >> 16);
  emit(instr);
}

void Assembler::GenB(Opcode opcode, int32_t si26) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK((B == opcode || BL == opcode) && is_int26(si26));
  Instr instr =
      opcode | ((si26 & kImm16Mask) << kRkShift) | ((si26 & kImm26Mask) >> 16);
  emit(instr);
}

void Assembler::GenBJ(Opcode opcode, Register rj, Register rd, int32_t si16) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(is_int16(si16));
  Instr instr = opcode | ((si16 & kImm16Mask) << kRkShift) |
                (rj.code() << kRjShift) | rd.code();
  emit(instr);
}

void Assembler::GenCmp(Opcode opcode, FPUCondition cond, FPURegister fk,
                       FPURegister fj, CFRegister cd) {
  DCHECK(opcode == FCMP_COND_S || opcode == FCMP_COND_D);
  Instr instr = opcode | cond << kCondShift | (fk.code() << kFkShift) |
                (fj.code() << kFjShift) | cd;
  emit(instr);
}

void Assembler::GenSel(Opcode opcode, CFRegister ca, FPURegister fk,
                       FPURegister fj, FPURegister rd) {
  DCHECK((opcode == FSEL));
  Instr instr = opcode | ca << kCondShift | (fk.code() << kFkShift) |
                (fj.code() << kFjShift) | rd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, Register rj, Register rd,
                            bool rjrd) {
  DCHECK(rjrd);
  Instr instr = 0;
  instr = opcode | (rj.code() << kRjShift) | rd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, FPURegister fj, FPURegister fd) {
  Instr instr = opcode | (fj.code() << kFjShift) | fd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, Register rj, FPURegister fd) {
  DCHECK((opcode == MOVGR2FR_W) || (opcode == MOVGR2FR_D) ||
         (opcode == MOVGR2FRH_W));
  Instr instr = opcode | (rj.code() << kRjShift) | fd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, FPURegister fj, Register rd) {
  DCHECK((opcode == MOVFR2GR_S) || (opcode == MOVFR2GR_D) ||
         (opcode == MOVFRH2GR_S));
  Instr instr = opcode | (fj.code() << kFjShift) | rd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, Register rj, FPUControlRegister fd) {
  DCHECK((opcode == MOVGR2FCSR));
  Instr instr = opcode | (rj.code() << kRjShift) | fd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, FPUControlRegister fj, Register rd) {
  DCHECK((opcode == MOVFCSR2GR));
  Instr instr = opcode | (fj.code() << kFjShift) | rd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, FPURegister fj, CFRegister cd) {
  DCHECK((opcode == MOVFR2CF));
  Instr instr = opcode | (fj.code() << kFjShift) | cd;
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, CFRegister cj, FPURegister fd) {
  DCHECK((opcode == MOVCF2FR));
  Instr instr = opcode | cj << kFjShift | fd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, Register rj, CFRegister cd) {
  DCHECK((opcode == MOVGR2CF));
  Instr instr = opcode | (rj.code() << kRjShift) | cd;
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, CFRegister cj, Register rd) {
  DCHECK((opcode == MOVCF2GR));
  Instr instr = opcode | cj << kFjShift | rd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, Register rk, Register rj,
                            Register rd) {
  Instr instr =
      opcode | (rk.code() << kRkShift) | (rj.code() << kRjShift) | rd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, FPURegister fk, FPURegister fj,
                            FPURegister fd) {
  Instr instr =
      opcode | (fk.code() << kFkShift) | (fj.code() << kFjShift) | fd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, FPURegister fa, FPURegister fk,
                            FPURegister fj, FPURegister fd) {
  Instr instr = opcode | (fa.code() << kFaShift) | (fk.code() << kFkShift) |
                (fj.code() << kFjShift) | fd.code();
  emit(instr);
}

void Assembler::GenRegister(Opcode opcode, Register rk, Register rj,
                            FPURegister fd) {
  Instr instr =
      opcode | (rk.code() << kRkShift) | (rj.code() << kRjShift) | fd.code();
  emit(instr);
}

void Assembler::GenImm(Opcode opcode, int32_t bit3, Register rk, Register rj,
                       Register rd) {
  DCHECK(is_uint3(bit3));
  Instr instr = opcode | (bit3 & 0x7) << kSaShift | (rk.code() << kRkShift) |
                (rj.code() << kRjShift) | rd.code();
  emit(instr);
}

void Assembler::GenImm(Opcode opcode, int32_t bit6m, int32_t bit6l, Register rj,
                       Register rd) {
  DCHECK(is_uint6(bit6m) && is_uint6(bit6l));
  Instr instr = opcode | (bit6m & 0x3f) << 16 | (bit6l & 0x3f) << kRkShift |
                (rj.code() << kRjShift) | rd.code();
  emit(instr);
}

void Assembler::GenImm(Opcode opcode, int32_t bit20, Register rd) {
  //  DCHECK(is_uint20(bit20) || is_int20(bit20));
  Instr instr = opcode | (bit20 & 0xfffff) << kRjShift | rd.code();
  emit(instr);
}

void Assembler::GenImm(Opcode opcode, int32_t bit15) {
  DCHECK(is_uint15(bit15));
  Instr instr = opcode | (bit15 & 0x7fff);
  emit(instr);
}

void Assembler::GenImm(Opcode opcode, int32_t value, Register rj, Register rd,
                       int32_t value_bits) {
  DCHECK(value_bits == 6 || value_bits == 12 || value_bits == 14 ||
         value_bits == 16);
  uint32_t imm = value & 0x3f;
  if (value_bits == 12) {
    imm = value & kImm12Mask;
  } else if (value_bits == 14) {
    imm = value & 0x3fff;
  } else if (value_bits == 16) {
    imm = value & kImm16Mask;
  }
  Instr instr = opcode | imm << kRkShift | (rj.code() << kRjShift) | rd.code();
  emit(instr);
}

void Assembler::GenImm(Opcode opcode, int32_t bit12, Register rj,
                       FPURegister fd) {
  DCHECK(is_int12(bit12));
  Instr instr = opcode | ((bit12 & kImm12Mask) << kRkShift) |
                (rj.code() << kRjShift) | fd.code();
  emit(instr);
}

// Returns the next free trampoline entry.
int32_t Assembler::get_trampoline_entry(int32_t pos) {
  int32_t trampoline_entry = kInvalidSlotPos;
  if (!internal_trampoline_exception_) {
    if (trampoline_.start() > pos) {
      trampoline_entry = trampoline_.take_slot();
    }

    if (kInvalidSlotPos == trampoline_entry) {
      internal_trampoline_exception_ = true;
    }
  }
  return trampoline_entry;
}

uint64_t Assembler::jump_address(Label* L) {
  int64_t target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      L->link_to(pc_offset());
    } else {
      L->link_to(pc_offset());
      return kEndOfJumpChain;
    }
  }
  uint64_t imm = reinterpret_cast<uint64_t>(buffer_start_) + target_pos;
  DCHECK_EQ(imm & 3, 0);

  return imm;
}

uint64_t Assembler::branch_long_offset(Label* L) {
  int64_t target_pos;

  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      L->link_to(pc_offset());
    } else {
      L->link_to(pc_offset());
      return kEndOfJumpChain;
    }
  }
  int64_t offset = target_pos - pc_offset();
  DCHECK_EQ(offset & 3, 0);

  return static_cast<uint64_t>(offset);
}

int32_t Assembler::branch_offset_helper(Label* L, OffsetSize bits) {
  int32_t target_pos;

  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();
      L->link_to(pc_offset());
    } else {
      L->link_to(pc_offset());
      if (!trampoline_emitted_) {
        unbound_labels_count_++;
        next_buffer_check_ -= kTrampolineSlotsSize;
      }
      return kEndOfChain;
    }
  }

  int32_t offset = target_pos - pc_offset();
  DCHECK(is_intn(offset, bits + 2));
  DCHECK_EQ(offset & 3, 0);

  return offset;
}

void Assembler::label_at_put(Label* L, int at_offset) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
    instr_at_put(at_offset, target_pos + (InstructionStream::kHeaderSize -
                                          kHeapObjectTag));
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link.
      int32_t imm18 = target_pos - at_offset;
      DCHECK_EQ(imm18 & 3, 0);
      int32_t imm16 = imm18 >> 2;
      DCHECK(is_int16(imm16));
      instr_at_put(at_offset, (imm16 & kImm16Mask));
    } else {
      target_pos = kEndOfChain;
      instr_at_put(at_offset, 0);
      if (!trampoline_emitted_) {
        unbound_labels_count_++;
        next_buffer_check_ -= kTrampolineSlotsSize;
      }
    }
    L->link_to(at_offset);
  }
}

//------- Branch and jump instructions --------

void Assembler::b(int32_t offset) { GenB(B, offset); }

void Assembler::bl(int32_t offset) { GenB(BL, offset); }

void Assembler::beq(Register rj, Register rd, int32_t offset) {
  GenBJ(BEQ, rj, rd, offset);
}

void Assembler::bne(Register rj, Register rd, int32_t offset) {
  GenBJ(BNE, rj, rd, offset);
}

void Assembler::blt(Register rj, Register rd, int32_t offset) {
  GenBJ(BLT, rj, rd, offset);
}

void Assembler::bge(Register rj, Register rd, int32_t offset) {
  GenBJ(BGE, rj, rd, offset);
}

void Assembler::bltu(Register rj, Register rd, int32_t offset) {
  GenBJ(BLTU, rj, rd, offset);
}

void Assembler::bgeu(Register rj, Register rd, int32_t offset) {
  GenBJ(BGEU, rj, rd, offset);
}

void Assembler::beqz(Register rj, int32_t offset) { GenB(BEQZ, rj, offset); }
void Assembler::bnez(Register rj, int32_t offset) { GenB(BNEZ, rj, offset); }

void Assembler::jirl(Register rd, Register rj, int32_t offset) {
  GenBJ(JIRL, rj, rd, offset);
}

void Assembler::bceqz(CFRegister cj, int32_t si21) {
  GenB(BCZ, cj, si21, true);
}

void Assembler::bcnez(CFRegister cj, int32_t si21) {
  GenB(BCZ, cj, si21, false);
}

// -------Data-processing-instructions---------

// Arithmetic.
void Assembler::add_w(Register rd, Register rj, Register rk) {
  GenRegister(ADD_W, rk, rj, rd);
}

void Assembler::add_d(Register rd, Register rj, Register rk) {
  GenRegister(ADD_D, rk, rj, rd);
}

void Assembler::sub_w(Register rd, Register rj, Register rk) {
  GenRegister(SUB_W, rk, rj, rd);
}

void Assembler::sub_d(Register rd, Register rj, Register rk) {
  GenRegister(SUB_D, rk, rj, rd);
}

void Assembler::addi_w(Register rd, Register rj, int32_t si12) {
  GenImm(ADDI_W, si12, rj, rd, 12);
}

void Assembler::addi_d(Register rd, Register rj, int32_t si12) {
  GenImm(ADDI_D, si12, rj, rd, 12);
}

void Assembler::addu16i_d(Register rd, Register rj, int32_t si16) {
  GenImm(ADDU16I_D, si16, rj, rd, 16);
}

void Assembler::alsl_w(Register rd, Register rj, Register rk, int32_t sa2) {
  DCHECK(is_uint2(sa2 - 1));
  GenImm(ALSL_W, sa2 - 1, rk, rj, rd);
}

void Assembler::alsl_wu(Register rd, Register rj, Register rk, int32_t sa2) {
  DCHECK(is_uint2(sa2 - 1));
  GenImm(ALSL_WU, sa2 + 3, rk, rj, rd);
}

void Assembler::alsl_d(Register rd, Register rj, Register rk, int32_t sa2) {
  DCHECK(is_uint2(sa2 - 1));
  GenImm(ALSL_D, sa2 - 1, rk, rj, rd);
}

void Assembler::lu12i_w(Register rd, int32_t si20) {
  GenImm(LU12I_W, si20, rd);
}

void Assembler::lu32i_d(Register rd, int32_t si20) {
  GenImm(LU32I_D, si20, rd);
}

void Assembler::lu52i_d(Register rd, Register rj, int32_t si12) {
  GenImm(LU52I_D, si12, rj, rd, 12);
}

void Assembler::slt(Register rd, Register rj, Register rk) {
  GenRegister(SLT, rk, rj, rd);
}

void Assembler::sltu(Register rd, Register rj, Register rk) {
  GenRegister(SLTU, rk, rj, rd);
}

void Assembler::slti(Register rd, Register rj, int32_t si12) {
  GenImm(SLTI, si12, rj, rd, 12);
}

void Assembler::sltui(Register rd, Register rj, int32_t si12) {
  GenImm(SLTUI, si12, rj, rd, 12);
}

void Assembler::pcaddi(Register rd, int32_t si20) { GenImm(PCADDI, si20, rd); }

void Assembler::pcaddu12i(Register rd, int32_t si20) {
  GenImm(PCADDU12I, si20, rd);
}

void Assembler::pcaddu18i(Register rd, int32_t si20) {
  GenImm(PCADDU18I, si20, rd);
}

void Assembler::pcalau12i(Register rd, int32_t si20) {
  GenImm(PCALAU12I, si20, rd);
}

void Assembler::and_(Register rd, Register rj, Register rk) {
  GenRegister(AND, rk, rj, rd);
}

void Assembler::or_(Register rd, Register rj, Register rk) {
  GenRegister(OR, rk, rj, rd);
}

void Assembler::xor_(Register rd, Register rj, Register rk) {
  GenRegister(XOR, rk, rj, rd);
}

void Assembler::nor(Register rd, Register rj, Register rk) {
  GenRegister(NOR, rk, rj, rd);
}

void Assembler::andn(Register rd, Register rj, Register rk) {
  GenRegister(ANDN, rk, rj, rd);
}

void Assembler::orn(Register rd, Register rj, Register rk) {
  GenRegister(ORN, rk, rj, rd);
}

void Assembler::andi(Register rd, Register rj, int32_t ui12) {
  GenImm(ANDI, ui12, rj, rd, 12);
}

void Assembler::ori(Register rd, Register rj, int32_t ui12) {
  GenImm(ORI, ui12, rj, rd, 12);
}

void Assembler::xori(Register rd, Register rj, int32_t ui12) {
  GenImm(XORI, ui12, rj, rd, 12);
}

void Assembler::mul_w(Register rd, Register rj, Register rk) {
  GenRegister(MUL_W, rk, rj, rd);
}

void Assembler::mulh_w(Register rd, Register rj, Register rk) {
  GenRegister(MULH_W, rk, rj, rd);
}

void Assembler::mulh_wu(Register rd, Register rj, Register rk) {
  GenRegister(MULH_WU, rk, rj, rd);
}

void Assembler::mul_d(Register rd, Register rj, Register rk) {
  GenRegister(MUL_D, rk, rj, rd);
}

void Assembler::mulh_d(Register rd, Register rj, Register rk) {
  GenRegister(MULH_D, rk, rj, rd);
}

void Assembler::mulh_du(Register rd, Register rj, Register rk) {
  GenRegister(MULH_DU, rk, rj, rd);
}

void Assembler::mulw_d_w(Register rd, Register rj, Register rk) {
  GenRegister(MULW_D_W, rk, rj, rd);
}

void Assembler::mulw_d_wu(Register rd, Register rj, Register rk) {
  GenRegister(MULW_D_WU, rk, rj, rd);
}

void Assembler::div_w(Register rd, Register rj, Register rk) {
  GenRegister(DIV_W, rk, rj, rd);
}

void Assembler::mod_w(Register rd, Register rj, Register rk) {
  GenRegister(MOD_W, rk, rj, rd);
}

void Assembler::div_wu(Register rd, Register rj, Register rk) {
  GenRegister(DIV_WU, rk, rj, rd);
}

void Assembler::mod_wu(Register rd, Register rj, Register rk) {
  GenRegister(MOD_WU, rk, rj, rd);
}

void Assembler::div_d(Register rd, Register rj, Register rk) {
  GenRegister(DIV_D, rk, rj, rd);
}

void Assembler::mod_d(Register rd, Register rj, Register rk) {
  GenRegister(MOD_D, rk, rj, rd);
}

void Assembler::div_du(Register rd, Register rj, Register rk) {
  GenRegister(DIV_DU, rk, rj, rd);
}

void Assembler::mod_du(Register rd, Register rj, Register rk) {
  GenRegister(MOD_DU, rk, rj, rd);
}

// Shifts.
void Assembler::sll_w(Register rd, Register rj, Register rk) {
  GenRegister(SLL_W, rk, rj, rd);
}

void Assembler::srl_w(Register rd, Register rj, Register rk) {
  GenRegister(SRL_W, rk, rj, rd);
}

void Assembler::sra_w(Register rd, Register rj, Register rk) {
  GenRegister(SRA_W, rk, rj, rd);
}

void Assembler::rotr_w(Register rd, Register rj, Register rk) {
  GenRegister(ROTR_W, rk, rj, rd);
}

void Assembler::slli_w(Register rd, Register rj, int32_t ui5) {
  DCHECK(is_uint5(ui5));
  GenImm(SLLI_W, ui5 + 0x20, rj, rd, 6);
}

void Assembler::srli_w(Register rd, Register rj, int32_t ui5) {
  DCHECK(is_uint5(ui5));
  GenImm(SRLI_W, ui5 + 0x20, rj, rd, 6);
}

void Assembler::srai_w(Register rd, Register rj, int32_t ui5) {
  DCHECK(is_uint5(ui5));
  GenImm(SRAI_W, ui5 + 0x20, rj, rd, 6);
}

void Assembler::rotri_w(Register rd, Register rj, int32_t ui5) {
  DCHECK(is_uint5(ui5));
  GenImm(ROTRI_W, ui5 + 0x20, rj, rd, 6);
}

void Assembler::sll_d(Register rd, Register rj, Register rk) {
  GenRegister(SLL_D, rk, rj, rd);
}

void Assembler::srl_d(Register rd, Register rj, Register rk) {
  GenRegister(SRL_D, rk, rj, rd);
}

void Assembler::sra_d(Register rd, Register rj, Register rk) {
  GenRegister(SRA_D, rk, rj, rd);
}

void Assembler::rotr_d(Register rd, Register rj, Register rk) {
  GenRegister(ROTR_D, rk, rj, rd);
}

void Assembler::slli_d(Register rd, Register rj, int32_t ui6) {
  GenImm(SLLI_D, ui6, rj, rd, 6);
}

void Assembler::srli_d(Register rd, Register rj, int32_t ui6) {
  GenImm(SRLI_D, ui6, rj, rd, 6);
}

void Assembler::srai_d(Register rd, Register rj, int32_t ui6) {
  GenImm(SRAI_D, ui6, rj, rd, 6);
}

void Assembler::rotri_d(Register rd, Register rj, int32_t ui6) {
  GenImm(ROTRI_D, ui6, rj, rd, 6);
}

// Bit twiddling.
void Assembler::ext_w_b(Register rd, Register rj) {
  GenRegister(EXT_W_B, rj, rd);
}

void Assembler::ext_w_h(Register rd, Register rj) {
  GenRegister(EXT_W_H, rj, rd);
}

void Assembler::clo_w(Register rd, Register rj) { GenRegister(CLO_W, rj, rd); }

void Assembler::clz_w(Register rd, Register rj) { GenRegister(CLZ_W, rj, rd); }

void Assembler::cto_w(Register rd, Register rj) { GenRegister(CTO_W, rj, rd); }

void Assembler::ctz_w(Register rd, Register rj) { GenRegister(CTZ_W, rj, rd); }

void Assembler::clo_d(Register rd, Register rj) { GenRegister(CLO_D, rj, rd); }

void Assembler::clz_d(Register rd, Register rj) { GenRegister(CLZ_D, rj, rd); }

void Assembler::cto_d(Register rd, Register rj) { GenRegister(CTO_D, rj, rd); }

void Assembler::ctz_d(Register rd, Register rj) { GenRegister(CTZ_D, rj, rd); }

void Assembler::bytepick_w(Register rd, Register rj, Register rk, int32_t sa2) {
  DCHECK(is_uint2(sa2));
  GenImm(BYTEPICK_W, sa2, rk, rj, rd);
}

void Assembler::bytepick_d(Register rd, Register rj, Register rk, int32_t sa3) {
  GenImm(BYTEPICK_D, sa3, rk, rj, rd);
}

void Assembler::revb_2h(Register rd, Register rj) {
  GenRegister(REVB_2H, rj, rd);
}

void Assembler::revb_4h(Register rd, Register rj) {
  GenRegister(REVB_4H, rj, rd);
}

void Assembler::revb_2w(Register rd, Register rj) {
  GenRegister(REVB_2W, rj, rd);
}

void Assembler::revb_d(Register rd, Register rj) {
  GenRegister(REVB_D, rj, rd);
}

void Assembler::revh_2w(Register rd, Register rj) {
  GenRegister(REVH_2W, rj, rd);
}

void Assembler::revh_d(Register rd, Register rj) {
  GenRegister(REVH_D, rj, rd);
}

void Assembler::bitrev_4b(Register rd, Register rj) {
  GenRegister(BITREV_4B, rj, rd);
}

void Assembler::bitrev_8b(Register rd, Register rj) {
  GenRegister(BITREV_8B, rj, rd);
}

void Assembler::bitrev_w(Register rd, Register rj) {
  GenRegister(BITREV_W, rj, rd);
}

void Assembler::bitrev_d(Register rd, Register rj) {
  GenRegister(BITREV_D, rj, rd);
}

void Assembler::bstrins_w(Register rd, Register rj, int32_t msbw,
                          int32_t lsbw) {
  DCHECK(is_uint5(msbw) && is_uint5(lsbw));
  GenImm(BSTR_W, msbw + 0x20, lsbw, rj, rd);
}

void Assembler::bstrins_d(Register rd, Register rj, int32_t msbd,
                          int32_t lsbd) {
  GenImm(BSTRINS_D, msbd, lsbd, rj, rd);
}

void Assembler::bstrpick_w(Register rd, Register rj, int32_t msbw,
                           int32_t lsbw) {
  DCHECK(is_uint5(msbw) && is_uint5(lsbw));
  GenImm(BSTR_W, msbw + 0x20, lsbw + 0x20, rj, rd);
}

void Assembler::bstrpick_d(Register rd, Register rj, int32_t msbd,
                           int32_t lsbd) {
  GenImm(BSTRPICK_D, msbd, lsbd, rj, rd);
}

void Assembler::maskeqz(Register rd, Register rj, Register rk) {
  GenRegister(MASKEQZ, rk, rj, rd);
}

void Assembler::masknez(Register rd, Register rj, Register rk) {
  GenRegister(MASKNEZ, rk, rj, rd);
}

// Memory-instructions
void Assembler::ld_b(Register rd, Register rj, int32_t si12) {
  GenImm(LD_B, si12, rj, rd, 12);
}

void Assembler::ld_h(Register rd, Register rj, int32_t si12) {
  GenImm(LD_H, si12, rj, rd, 12);
}

void Assembler::ld_w(Register rd, Register rj, int32_t si12) {
  GenImm(LD_W, si12, rj, rd, 12);
}

void Assembler::ld_d(Register rd, Register rj, int32_t si12) {
  GenImm(LD_D, si12, rj, rd, 12);
}

void Assembler::ld_bu(Register rd, Register rj, int32_t si12) {
  GenImm(LD_BU, si12, rj, rd, 12);
}

void Assembler::ld_hu(Register rd, Register rj, int32_t si12) {
  GenImm(LD_HU, si12, rj, rd, 12);
}

void Assembler::ld_wu(Register rd, Register rj, int32_t si12) {
  GenImm(LD_WU, si12, rj, rd, 12);
}

void Assembler::st_b(Register rd, Register rj, int32_t si12) {
  GenImm(ST_B, si12, rj, rd, 12);
}

void Assembler::st_h(Register rd, Register rj, int32_t si12) {
  GenImm(ST_H, si12, rj, rd, 12);
}

void Assembler::st_w(Register rd, Register rj, int32_t si12) {
  GenImm(ST_W, si12, rj, rd, 12);
}

void Assembler::st_d(Register rd, Register rj, int32_t si12) {
  GenImm(ST_D, si12, rj, rd, 12);
}

void Assembler::ldx_b(Register rd, Register rj, Register rk) {
  GenRegister(LDX_B, rk, rj, rd);
}

void Assembler::ldx_h(Register rd, Register rj, Register rk) {
  GenRegister(LDX_H, rk, rj, rd);
}

void Assembler::ldx_w(Register rd, Register rj, Register rk) {
  GenRegister(LDX_W, rk, rj, rd);
}

void Assembler::ldx_d(Register rd, Register rj, Register rk) {
  GenRegister(LDX_D, rk, rj, rd);
}

void Assembler::ldx_bu(Register rd, Register rj, Register rk) {
  GenRegister(LDX_BU, rk, rj, rd);
}

void Assembler::ldx_hu(Register rd, Register rj, Register rk) {
  GenRegister(LDX_HU, rk, rj, rd);
}

void Assembler::ldx_wu(Register rd, Register rj, Register rk) {
  GenRegister(LDX_WU, rk, rj, rd);
}

void Assembler::stx_b(Register rd, Register rj, Register rk) {
  GenRegister(STX_B, rk, rj, rd);
}

void Assembler::stx_h(Register rd, Register rj, Register rk) {
  GenRegister(STX_H, rk, rj, rd);
}

void Assembler::stx_w(Register rd, Register rj, Register rk) {
  GenRegister(STX_W, rk, rj, rd);
}

void Assembler::stx_d(Register rd, Register rj, Register rk) {
  GenRegister(STX_D, rk, rj, rd);
}

void Assembler::ldptr_w(Register rd, Register rj, int32_t si14) {
  DCHECK(is_int16(si14) && ((si14 & 0x3) == 0));
  GenImm(LDPTR_W, si14 >> 2, rj, rd, 14);
}

void Assembler::ldptr_d(Register rd, Register rj, int32_t si14) {
  DCHECK(is_int16(si14) && ((si14 & 0x3) == 0));
  GenImm(LDPTR_D, si14 >> 2, rj, rd, 14);
}

void Assembler::stptr_w(Register rd, Register rj, int32_t si14) {
  DCHECK(is_int16(si14) && ((si14 & 0x3) == 0));
  GenImm(STPTR_W, si14 >> 2, rj, rd, 14);
}

void Assembler::stptr_d(Register rd, Register rj, int32_t si14) {
  DCHECK(is_int16(si14) && ((si14 & 0x3) == 0));
  GenImm(STPTR_D, si14 >> 2, rj, rd, 14);
}

void Assembler::amswap_w(Register rd, Register rk, Register rj) {
  GenRegister(AMSWAP_W, rk, rj, rd);
}

void Assembler::amswap_d(Register rd, Register rk, Register rj) {
  GenRegister(AMSWAP_D, rk, rj, rd);
}

void Assembler::amadd_w(Register rd, Register rk, Register rj) {
  GenRegister(AMADD_W, rk, rj, rd);
}

void Assembler::amadd_d(Register rd, Register rk, Register rj) {
  GenRegister(AMADD_D, rk, rj, rd);
}

void Assembler::amand_w(Register rd, Register rk, Register rj) {
  GenRegister(AMAND_W, rk, rj, rd);
}

void Assembler::amand_d(Register rd, Register rk, Register rj) {
  GenRegister(AMAND_D, rk, rj, rd);
}

void Assembler::amor_w(Register rd, Register rk, Register rj) {
  GenRegister(AMOR_W, rk, rj, rd);
}

void Assembler::amor_d(Register rd, Register rk, Register rj) {
  GenRegister(AMOR_D, rk, rj, rd);
}

void Assembler::amxor_w(Register rd, Register rk, Register rj) {
  GenRegister(AMXOR_W, rk, rj, rd);
}

void Assembler::amxor_d(Register rd, Register rk, Register rj) {
  GenRegister(AMXOR_D, rk, rj, rd);
}

void Assembler::ammax_w(Register rd, Register rk, Register rj) {
  GenRegister(AMMAX_W, rk, rj, rd);
}

void Assembler::ammax_d(Register rd, Register rk, Register rj) {
  GenRegister(AMMAX_D, rk, rj, rd);
}

void Assembler::ammin_w(Register rd, Register rk, Register rj) {
  GenRegister(AMMIN_W, rk, rj, rd);
}

void Assembler::ammin_d(Register rd, Register rk, Register rj) {
  GenRegister(AMMIN_D, rk, rj, rd);
}

void Assembler::ammax_wu(Register rd, Register rk, Register rj) {
  GenRegister(AMMAX_WU, rk, rj, rd);
}

void Assembler::ammax_du(Register rd, Register rk, Register rj) {
  GenRegister(AMMAX_DU, rk, rj, rd);
}

void Assembler::ammin_wu(Register rd, Register rk, Register rj) {
  GenRegister(AMMIN_WU, rk, rj, rd);
}

void Assembler::ammin_du(Register rd, Register rk, Register rj) {
  GenRegister(AMMIN_DU, rk, rj, rd);
}

void Assembler::amswap_db_w(Register rd, Register rk, Register rj) {
  GenRegister(AMSWAP_DB_W, rk, rj, rd);
}

void Assembler::amswap_db_d(Register rd, Register rk, Register rj) {
  GenRegister(AMSWAP_DB_D, rk, rj, rd);
}

void Assembler::amadd_db_w(Register rd, Register rk, Register rj) {
  GenRegister(AMADD_DB_W, rk, rj, rd);
}

void Assembler::amadd_db_d(Register rd, Register rk, Register rj) {
  GenRegister(AMADD_DB_D, rk, rj, rd);
}

void Assembler::amand_db_w(Register rd, Register rk, Register rj) {
  GenRegister(AMAND_DB_W, rk, rj, rd);
}

void Assembler::amand_db_d(Register rd, Register rk, Register rj) {
  GenRegister(AMAND_DB_D, rk, rj, rd);
}

void Assembler::amor_db_w(Register rd, Register rk, Register rj) {
  GenRegister(AMOR_DB_W, rk, rj, rd);
}

void Assembler::amor_db_d(Register rd, Register rk, Register rj) {
  GenRegister(AMOR_DB_D, rk, rj, rd);
}

void Assembler::amxor_db_w(Register rd, Register rk, Register rj) {
  GenRegister(AMXOR_DB_W, rk, rj, rd);
}

void Assembler::amxor_db_d(Register rd, Register rk, Register rj) {
  GenRegister(AMXOR_DB_D, rk, rj, rd);
}

void Assembler::ammax_db_w(Register rd, Register rk, Register rj) {
  GenRegister(AMMAX_DB_W, rk, rj, rd);
}

void Assembler::ammax_db_d(Register rd, Register rk, Register rj) {
  GenRegister(AMMAX_DB_D, rk, rj, rd);
}

void Assembler::ammin_db_w(Register rd, Register rk, Register rj) {
  GenRegister(AMMIN_DB_W, rk, rj, rd);
}

void Assembler::ammin_db_d(Register rd, Register rk, Register rj) {
  GenRegister(AMMIN_DB_D, rk, rj, rd);
}

void Assembler::ammax_db_wu(Register rd, Register rk, Register rj) {
  GenRegister(AMMAX_DB_WU, rk, rj, rd);
}

void Assembler::ammax_db_du(Register rd, Register rk, Register rj) {
  GenRegister(AMMAX_DB_DU, rk, rj, rd);
}

void Assembler::ammin_db_wu(Register rd, Register rk, Register rj) {
  GenRegister(AMMIN_DB_WU, rk, rj, rd);
}

void Assembler::ammin_db_du(Register rd, Register rk, Register rj) {
  GenRegister(AMMIN_DB_DU, rk, rj, rd);
}

void Assembler::ll_w(Register rd, Register rj, int32_t si14) {
  DCHECK(is_int16(si14) && ((si14 & 0x3) == 0));
  GenImm(LL_W, si14 >> 2, rj, rd, 14);
}

void Assembler::ll_d(Register rd, Register rj, int32_t si14) {
  DCHECK(is_int16(si14) && ((si14 & 0x3) == 0));
  GenImm(LL_D, si14 >> 2, rj, rd, 14);
}

void Assembler::sc_w(Register rd, Register rj, int32_t si14) {
  DCHECK(is_int16(si14) && ((si14 & 0x3) == 0));
  GenImm(SC_W, si14 >> 2, rj, rd, 14);
}

void Assembler::sc_d(Register rd, Register rj, int32_t si14) {
  DCHECK(is_int16(si14) && ((si14 & 0x3) == 0));
  GenImm(SC_D, si14 >> 2, rj, rd, 14);
}

void Assembler::dbar(int32_t hint) { GenImm(DBAR, hint); }

void Assembler::ibar(int32_t hint) { GenImm(IBAR, hint); }

// Break instruction.
void Assembler::break_(uint32_t code, bool break_as_stop) {
  DCHECK(
      (break_as_stop && code <= kMaxStopCode && code > kMaxWatchpointCode) ||
      (!break_as_stop && (code > kMaxStopCode || code <= kMaxWatchpointCode)));
  GenImm(BREAK, code);
}

void Assembler::stop(uint32_t code) {
  DCHECK_GT(code, kMaxWatchpointCode);
  DCHECK_LE(code, kMaxStopCode);
#if defined(V8_HOST_ARCH_LOONG64)
  break_(0x4321);
#else  // V8_HOST_ARCH_LOONG64
  break_(code, true);
#endif
}

void Assembler::fadd_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FADD_S, fk, fj, fd);
}

void Assembler::fadd_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FADD_D, fk, fj, fd);
}

void Assembler::fsub_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FSUB_S, fk, fj, fd);
}

void Assembler::fsub_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FSUB_D, fk, fj, fd);
}

void Assembler::fmul_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMUL_S, fk, fj, fd);
}

void Assembler::fmul_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMUL_D, fk, fj, fd);
}

void Assembler::fdiv_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FDIV_S, fk, fj, fd);
}

void Assembler::fdiv_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FDIV_D, fk, fj, fd);
}

void Assembler::fmadd_s(FPURegister fd, FPURegister fj, FPURegister fk,
                        FPURegister fa) {
  GenRegister(FMADD_S, fa, fk, fj, fd);
}

void Assembler::fmadd_d(FPURegister fd, FPURegister fj, FPURegister fk,
                        FPURegister fa) {
  GenRegister(FMADD_D, fa, fk, fj, fd);
}

void Assembler::fmsub_s(FPURegister fd, FPURegister fj, FPURegister fk,
                        FPURegister fa) {
  GenRegister(FMSUB_S, fa, fk, fj, fd);
}

void Assembler::fmsub_d(FPURegister fd, FPURegister fj, FPURegister fk,
                        FPURegister fa) {
  GenRegister(FMSUB_D, fa, fk, fj, fd);
}

void Assembler::fnmadd_s(FPURegister fd, FPURegister fj, FPURegister fk,
                         FPURegister fa) {
  GenRegister(FNMADD_S, fa, fk, fj, fd);
}

void Assembler::fnmadd_d(FPURegister fd, FPURegister fj, FPURegister fk,
                         FPURegister fa) {
  GenRegister(FNMADD_D, fa, fk, fj, fd);
}

void Assembler::fnmsub_s(FPURegister fd, FPURegister fj, FPURegister fk,
                         FPURegister fa) {
  GenRegister(FNMSUB_S, fa, fk, fj, fd);
}

void Assembler::fnmsub_d(FPURegister fd, FPURegister fj, FPURegister fk,
                         FPURegister fa) {
  GenRegister(FNMSUB_D, fa, fk, fj, fd);
}

void Assembler::fmax_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMAX_S, fk, fj, fd);
}

void Assembler::fmax_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMAX_D, fk, fj, fd);
}

void Assembler::fmin_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMIN_S, fk, fj, fd);
}

void Assembler::fmin_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMIN_D, fk, fj, fd);
}

void Assembler::fmaxa_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMAXA_S, fk, fj, fd);
}

void Assembler::fmaxa_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMAXA_D, fk, fj, fd);
}

void Assembler::fmina_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMINA_S, fk, fj, fd);
}

void Assembler::fmina_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FMINA_D, fk, fj, fd);
}

void Assembler::fabs_s(FPURegister fd, FPURegister fj) {
  GenRegister(FABS_S, fj, fd);
}

void Assembler::fabs_d(FPURegister fd, FPURegister fj) {
  GenRegister(FABS_D, fj, fd);
}

void Assembler::fneg_s(FPURegister fd, FPURegister fj) {
  GenRegister(FNEG_S, fj, fd);
}

void Assembler::fneg_d(FPURegister fd, FPURegister fj) {
  GenRegister(FNEG_D, fj, fd);
}

void Assembler::fsqrt_s(FPURegister fd, FPURegister fj) {
  GenRegister(FSQRT_S, fj, fd);
}

void Assembler::fsqrt_d(FPURegister fd, FPURegister fj) {
  GenRegister(FSQRT_D, fj, fd);
}

void Assembler::frecip_s(FPURegister fd, FPURegister fj) {
  GenRegister(FRECIP_S, fj, fd);
}

void Assembler::frecip_d(FPURegister fd, FPURegister fj) {
  GenRegister(FRECIP_D, fj, fd);
}

void Assembler::frsqrt_s(FPURegister fd, FPURegister fj) {
  GenRegister(FRSQRT_S, fj, fd);
}

void Assembler::frsqrt_d(FPURegister fd, FPURegister fj) {
  GenRegister(FRSQRT_D, fj, fd);
}

void Assembler::fscaleb_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FSCALEB_S, fk, fj, fd);
}

void Assembler::fscaleb_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FSCALEB_D, fk, fj, fd);
}

void Assembler::flogb_s(FPURegister fd, FPURegister fj) {
  GenRegister(FLOGB_S, fj, fd);
}

void Assembler::flogb_d(FPURegister fd, FPURegister fj) {
  GenRegister(FLOGB_D, fj, fd);
}

void Assembler::fcopysign_s(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FCOPYSIGN_S, fk, fj, fd);
}

void Assembler::fcopysign_d(FPURegister fd, FPURegister fj, FPURegister fk) {
  GenRegister(FCOPYSIGN_D, fk, fj, fd);
}

void Assembler::fclass_s(FPURegister fd, FPURegister fj) {
  GenRegister(FCLASS_S, fj, fd);
}

void Assembler::fclass_d(FPURegister fd, FPURegister fj) {
  GenRegister(FCLASS_D, fj, fd);
}

void Assembler::fcmp_cond_s(FPUCondition cc, FPURegister fj, FPURegister fk,
                            CFRegister cd) {
  GenCmp(FCMP_COND_S, cc, fk, fj, cd);
}

void Assembler::fcmp_cond_d(FPUCondition cc, FPURegister fj, FPURegister fk,
                            CFRegister cd) {
  GenCmp(FCMP_COND_D, cc, fk, fj, cd);
}

void Assembler::fcvt_s_d(FPURegister fd, FPURegister fj) {
  GenRegister(FCVT_S_D, fj, fd);
}

void Assembler::fcvt_d_s(FPURegister fd, FPURegister fj) {
  GenRegister(FCVT_D_S, fj, fd);
}

void Assembler::ffint_s_w(FPURegister fd, FPURegister fj) {
  GenRegister(FFINT_S_W, fj, fd);
}

void Assembler::ffint_s_l(FPURegister fd, FPURegister fj) {
  GenRegister(FFINT_S_L, fj, fd);
}

void Assembler::ffint_d_w(FPURegister fd, FPURegister fj) {
  GenRegister(FFINT_D_W, fj, fd);
}

void Assembler::ffint_d_l(FPURegister fd, FPURegister fj) {
  GenRegister(FFINT_D_L, fj, fd);
}

void Assembler::ftint_w_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINT_W_S, fj, fd);
}

void Assembler::ftint_w_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINT_W_D, fj, fd);
}

void Assembler::ftint_l_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINT_L_S, fj, fd);
}

void Assembler::ftint_l_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINT_L_D, fj, fd);
}

void Assembler::ftintrm_w_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRM_W_S, fj, fd);
}

void Assembler::ftintrm_w_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRM_W_D, fj, fd);
}

void Assembler::ftintrm_l_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRM_L_S, fj, fd);
}

void Assembler::ftintrm_l_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRM_L_D, fj, fd);
}

void Assembler::ftintrp_w_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRP_W_S, fj, fd);
}

void Assembler::ftintrp_w_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRP_W_D, fj, fd);
}

void Assembler::ftintrp_l_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRP_L_S, fj, fd);
}

void Assembler::ftintrp_l_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRP_L_D, fj, fd);
}

void Assembler::ftintrz_w_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRZ_W_S, fj, fd);
}

void Assembler::ftintrz_w_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRZ_W_D, fj, fd);
}

void Assembler::ftintrz_l_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRZ_L_S, fj, fd);
}

void Assembler::ftintrz_l_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRZ_L_D, fj, fd);
}

void Assembler::ftintrne_w_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRNE_W_S, fj, fd);
}

void Assembler::ftintrne_w_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRNE_W_D, fj, fd);
}

void Assembler::ftintrne_l_s(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRNE_L_S, fj, fd);
}

void Assembler::ftintrne_l_d(FPURegister fd, FPURegister fj) {
  GenRegister(FTINTRNE_L_D, fj, fd);
}

void Assembler::frint_s(FPURegister fd, FPURegister fj) {
  GenRegister(FRINT_S, fj, fd);
}

void Assembler::frint_d(FPURegister fd, FPURegister fj) {
  GenRegister(FRINT_D, fj, fd);
}

void Assembler::fmov_s(FPURegister fd, FPURegister fj) {
  GenRegister(FMOV_S, fj, fd);
}

void Assembler::fmov_d(FPURegister fd, FPURegister fj) {
  GenRegister(FMOV_D, fj, fd);
}

void Assembler::fsel(CFRegister ca, FPURegister fd, FPURegister fj,
                     FPURegister fk) {
  GenSel(FSEL, ca, fk, fj, fd);
}

void Assembler::movgr2fr_w(FPURegister fd, Register rj) {
  GenRegister(MOVGR2FR_W, rj, fd);
}

void Assembler::movgr2fr_d(FPURegister fd, Register rj) {
  GenRegister(MOVGR2FR_D, rj, fd);
}

void Assembler::movgr2frh_w(FPURegister fd, Register rj) {
  GenRegister(MOVGR2FRH_W, rj, fd);
}

void Assembler::movfr2gr_s(Register rd, FPURegister fj) {
  GenRegister(MOVFR2GR_S, fj, rd);
}

void Assembler::movfr2gr_d(Register rd, FPURegister fj) {
  GenRegister(MOVFR2GR_D, fj, rd);
}

void Assembler::movfrh2gr_s(Register rd, FPURegister fj) {
  GenRegister(MOVFRH2GR_S, fj, rd);
}

void Assembler::movgr2fcsr(Register rj, FPUControlRegister fcsr) {
  GenRegister(MOVGR2FCSR, rj, fcsr);
}

void Assembler::movfcsr2gr(Register rd, FPUControlRegister fcsr) {
  GenRegister(MOVFCSR2GR, fcsr, rd);
}

void Assembler::movfr2cf(CFRegister cd, FPURegister fj) {
  GenRegister(MOVFR2CF, fj, cd);
}

void Assembler::movcf2fr(FPURegister fd, CFRegister cj) {
  GenRegister(MOVCF2FR, cj, fd);
}

void Assembler::movgr2cf(CFRegister cd, Register rj) {
  GenRegister(MOVGR2CF, rj, cd);
}

void Assembler::movcf2gr(Register rd, CFRegister cj) {
  GenRegister(MOVCF2GR, cj, rd);
}

void Assembler::fld_s(FPURegister fd, Register rj, int32_t si12) {
  GenImm(FLD_S, si12, rj, fd);
}

void Assembler::fld_d(FPURegister fd, Register rj, int32_t si12) {
  GenImm(FLD_D, si12, rj, fd);
}

void Assembler::fst_s(FPURegister fd, Register rj, int32_t si12) {
  GenImm(FST_S, si12, rj, fd);
}

void Assembler::fst_d(FPURegister fd, Register rj, int32_t si12) {
  GenImm(FST_D, si12, rj, fd);
}

void Assembler::fldx_s(FPURegister fd, Register rj, Register rk) {
  GenRegister(FLDX_S, rk, rj, fd);
}

void Assembler::fldx_d(FPURegister fd, Register rj, Register rk) {
  GenRegister(FLDX_D, rk, rj, fd);
}

void Assembler::fstx_s(FPURegister fd, Register rj, Register rk) {
  GenRegister(FSTX_S, rk, rj, fd);
}

void Assembler::fstx_d(FPURegister fd, Register rj, Register rk) {
  GenRegister(FSTX_D, rk, rj, fd);
}

void Assembler::AdjustBaseAndOffset(MemOperand* src) {
  // is_int12 must be passed a signed value, hence the static cast below.
  if ((!src->hasIndexReg() && is_int12(src->offset())) || src->hasIndexReg()) {
    return;
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  if (is_uint12(static_cast<int32_t>(src->offset()))) {
    ori(scratch, zero_reg, src->offset() & kImm12Mask);
  } else {
    lu12i_w(scratch, src->offset() >> 12 & 0xfffff);
    if (src->offset() & kImm12Mask) {
      ori(scratch, scratch, src->offset() & kImm12Mask);
    }
  }
  src->index_ = scratch;
  src->offset_ = 0;
}

int Assembler::RelocateInternalReference(RelocInfo::Mode rmode, Address pc,
                                         intptr_t pc_delta) {
  DCHECK(RelocInfo::IsInternalReference(rmode));
  int64_t* p = reinterpret_cast<int64_t*>(pc);
  if (*p == kEndOfJumpChain) {
    return 0;  // Number of instructions patched.
  }
  *p += pc_delta;
  return 2;  // Number of instructions patched.
}

void Assembler::RelocateRelativeReference(RelocInfo::Mode rmode, Address pc,
                                          intptr_t pc_delta) {
  DCHECK(RelocInfo::IsRelativeCodeTarget(rmode) ||
         RelocInfo::IsNearBuiltinEntry(rmode));
  Instr instr = instr_at(pc);
  int32_t offset = instr & kImm26Mask;
  offset = (((offset & 0x3ff) << 22 >> 6) | ((offset >> 10) & kImm16Mask)) << 2;
  offset -= pc_delta;
  uint32_t* p = reinterpret_cast<uint32_t*>(pc);
  offset >>= 2;
  offset = ((offset & kImm16Mask) << kRkShift) | ((offset & kImm26Mask) >> 16);
  *p = (instr & ~kImm26Mask) | offset;
  return;
}

void Assembler::GrowBuffer() {
  // Compute new buffer size.
  int old_size = buffer_->size();
  int new_size = std::min(2 * old_size, old_size + 1 * MB);

  // Some internal data structures overflow for very large buffers,
  // they must ensure that kMaximalBufferSize is not too large.
  if (new_size > kMaximalBufferSize) {
    V8::FatalProcessOutOfMemory(nullptr, "Assembler::GrowBuffer");
  }

  // Set up new buffer.
  std::unique_ptr<AssemblerBuffer> new_buffer = buffer_->Grow(new_size);
  DCHECK_EQ(new_size, new_buffer->size());
  uint8_t* new_start = new_buffer->start();

  // Copy the data.
  intptr_t pc_delta = new_start - buffer_start_;
  intptr_t rc_delta = (new_start + new_size) - (buffer_start_ + old_size);
  size_t reloc_size = (buffer_start_ + old_size) - reloc_info_writer.pos();
  MemMove(new_start, buffer_start_, pc_offset());
  MemMove(reloc_info_writer.pos() + rc_delta, reloc_info_writer.pos(),
          reloc_size);

  // Switch buffers.
  buffer_ = std::move(new_buffer);
  buffer_start_ = new_start;
  pc_ += pc_delta;
  pc_for_safepoint_ += pc_delta;
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // None of our relocation types are pc relative pointing outside the code
  // buffer nor pc absolute pointing inside the code buffer, so there is no need
  // to relocate any emitted relocation entries.

  // Relocate internal references.
  for (auto pos : internal_reference_positions_) {
    Address address = reinterpret_cast<intptr_t>(buffer_start_) + pos;
    intptr_t internal_ref = ReadUnalignedValue<intptr_t>(address);
    if (internal_ref != kEndOfJumpChain) {
      internal_ref += pc_delta;
      WriteUnalignedValue<intptr_t>(address, internal_ref);
    }
  }
}

void Assembler::db(uint8_t data) {
  if (!is_buffer_growth_blocked()) {
    CheckBuffer();
  }
  *reinterpret_cast<uint8_t*>(pc_) = data;
  pc_ += sizeof(uint8_t);
}

void Assembler::dd(uint32_t data) {
  if (!is_buffer_growth_blocked()) {
    CheckBuffer();
  }
  *reinterpret_cast<uint32_t*>(pc_) = data;
  pc_ += sizeof(uint32_t);
}

void Assembler::dq(uint64_t data) {
  if (!is_buffer_growth_blocked()) {
    CheckBuffer();
  }
  *reinterpret_cast<uint64_t*>(pc_) = data;
  pc_ += sizeof(uint64_t);
}

void Assembler::dd(Label* label) {
  if (!is_buffer_growth_blocked()) {
    CheckBuffer();
  }
  uint64_t data;
  if (label->is_bound()) {
    data = reinterpret_cast<uint64_t>(buffer_start_ + label->pos());
  } else {
    data = jump_address(label);
    unbound_labels_count_++;
    internal_reference_positions_.insert(label->pos());
  }
  RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE);
  EmitHelper(data);
}

void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  if (!ShouldRecordRelocInfo(rmode)) return;
  // We do not try to reuse pool constants.
  RelocInfo rinfo(reinterpret_cast<Address>(pc_), rmode, data);
  DCHECK_GE(buffer_space(), kMaxRelocSize);  // Too late to grow buffer here.
  reloc_info_writer.Write(&rinfo);
}

void Assembler::BlockTrampolinePoolFor(int instructions) {
  CheckTrampolinePoolQuick(instructions);
  BlockTrampolinePoolBefore(pc_offset() + instructions * kInstrSize);
}

void Assembler::CheckTrampolinePool() {
  // Some small sequences of instructions must not be broken up by the
  // insertion of a trampoline pool; such sequences are protected by setting
  // either trampoline_pool_blocked_nesting_ or no_trampoline_pool_before_,
  // which are both checked here. Also, recursive calls to CheckTrampolinePool
  // are blocked by trampoline_pool_blocked_nesting_.
  if ((trampoline_pool_blocked_nesting_ > 0) ||
      (pc_offset() < no_trampoline_pool_before_)) {
    // Emission is currently blocked; make sure we try again as soon as
    // possible.
    if (trampoline_pool_blocked_nesting_ > 0) {
      next_buffer_check_ = pc_offset() + kInstrSize;
    } else {
      next_buffer_check_ = no_trampoline_pool_before_;
    }
    return;
  }

  DCHECK(!trampoline_emitted_);
  DCHECK_GE(unbound_labels_count_, 0);
  if (unbound_labels_count_ > 0) {
    // First we emit jump (2 instructions), then we emit trampoline pool.
    {
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Label after_pool;
      b(&after_pool);
      nop();  // TODO(LOONG_dev): remove this

      int pool_start = pc_offset();
      for (int i = 0; i < unbound_labels_count_; i++) {
        {
          b(&after_pool);
          nop();  // TODO(LOONG_dev): remove this
        }
      }
      nop();
      trampoline_ = Trampoline(pool_start, unbound_labels_count_);
      bind(&after_pool);

      trampoline_emitted_ = true;
      // As we are only going to emit trampoline once, we need to prevent any
      // further emission.
      next_buffer_check_ = kMaxInt;
    }
  } else {
    // Number of branches to unbound label at this point is zero, so we can
    // move next buffer check to maximum.
    next_buffer_check_ =
        pc_offset() + kMax16BranchOffset - kTrampolineSlotsSize * 16;
  }
  return;
}

Address Assembler::target_address_at(Address pc) {
  Instr instr0 = instr_at(pc);
  if (IsB(instr0)) {
    int32_t offset = instr0 & kImm26Mask;
    offset = (((offset & 0x3ff) << 22 >> 6) | ((offset >> 10) & kImm16Mask))
             << 2;
    return pc + offset;
  }
  Instr instr1 = instr_at(pc + 1 * kInstrSize);
  Instr instr2 = instr_at(pc + 2 * kInstrSize);

  // Interpret 3 instructions for address generated by li: See listing in
  // Assembler::set_target_address_at() just below.
  DCHECK((IsLu12i_w(instr0) && (IsOri(instr1)) && (IsLu32i_d(instr2))));

  // Assemble the 48 bit value.
  uint64_t hi20 = ((uint64_t)(instr2 >> 5) & 0xfffff) << 32;
  uint64_t mid20 = ((uint64_t)(instr0 >> 5) & 0xfffff) << 12;
  uint64_t low12 = ((uint64_t)(instr1 >> 10) & 0xfff);
  int64_t addr = static_cast<int64_t>(hi20 | mid20 | low12);

  // Sign extend to get canonical address.
  addr = (addr << 16) >> 16;
  return static_cast<Address>(addr);
}

uint32_t Assembler::target_compressed_address_at(Address pc) {
  Instr instr0 = instr_at(pc);
  Instr instr1 = instr_at(pc + 1 * kInstrSize);

  // Interpret 2 instructions for address generated by li: See listing in
  // Assembler::set_target_compressed_value_at just below.
  DCHECK((IsLu12i_w(instr0) && (IsOri(instr1))));

  // Assemble the 32 bit value.
  uint32_t hi20 = ((uint32_t)(instr0 >> 5) & 0xfffff) << 12;
  uint32_t low12 = ((uint32_t)(instr1 >> 10) & 0xfff);
  uint32_t addr = static_cast<uint32_t>(hi20 | low12);

  return addr;
}

// On loong64, a target address is stored in a 3-instruction sequence:
//    0: lu12i_w(rd, (j.imm64_ >> 12) & kImm20Mask);
//    1: ori(rd, rd, j.imm64_  & kImm12Mask);
//    2: lu32i_d(rd, (j.imm64_ >> 32) & kImm20Mask);
//
// Patching the address must replace all the lui & ori instructions,
// and flush the i-cache.
//
void Assembler::set_target_value_at(Address pc, uint64_t target,
                                    ICacheFlushMode icache_flush_mode) {
  // There is an optimization where only 3 instructions are used to load address
  // in code on LOONG64 because only 48-bits of address is effectively used.
  // It relies on fact the upper [63:48] bits are not used for virtual address
  // translation and they have to be set according to value of bit 47 in order
  // get canonical address.
#ifdef DEBUG
  // Check we have the result from a li macro-instruction.
  Instr instr0 = instr_at(pc);
  Instr instr1 = instr_at(pc + kInstrSize);
  Instr instr2 = instr_at(pc + kInstrSize * 2);
  DCHECK((IsLu12i_w(instr0) && IsOri(instr1) && IsLu32i_d(instr2)) ||
         IsB(instr0));
#endif

  Instr instr = instr_at(pc);
  uint32_t* p = reinterpret_cast<uint32_t*>(pc);
  if (IsB(instr)) {
    int32_t offset = (target - pc) >> 2;
    CHECK(is_int26(offset));
    offset =
        ((offset & kImm16Mask) << kRkShift) | ((offset & kImm26Mask) >> 16);
    *p = (instr & ~kImm26Mask) | offset;
    if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
      FlushInstructionCache(pc, kInstrSize);
    }
    return;
  }
  uint32_t rd_code = GetRd(instr);

  // Must use 3 instructions to insure patchable code.
  // lu12i_w rd, middle-20.
  // ori rd, rd, low-12.
  // lu32i_d rd, high-20.
  *p = LU12I_W | (((target >> 12) & 0xfffff) << kRjShift) | rd_code;
  *(p + 1) =
      ORI | (target & 0xfff) << kRkShift | (rd_code << kRjShift) | rd_code;
  *(p + 2) = LU32I_D | (((target >> 32) & 0xfffff) << kRjShift) | rd_code;

  if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
    FlushInstructionCache(pc, 3 * kInstrSize);
  }
}

void Assembler::set_target_compressed_value_at(
    Address pc, uint32_t target, ICacheFlushMode icache_flush_mode) {
#ifdef DEBUG
  // Check we have the result from a li macro-instruction.
  Instr instr0 = instr_at(pc);
  Instr instr1 = instr_at(pc + kInstrSize);
  DCHECK(IsLu12i_w(instr0) && IsOri(instr1));
#endif

  Instr instr = instr_at(pc);
  uint32_t* p = reinterpret_cast<uint32_t*>(pc);
  uint32_t rd_code = GetRd(instr);

  // Must use 2 instructions to insure patchable code.
  // lu12i_w rd, high-20.
  // ori rd, rd, low-12.
  *p = LU12I_W | (((target >> 12) & 0xfffff) << kRjShift) | rd_code;
  *(p + 1) =
      ORI | (target & 0xfff) << kRkShift | (rd_code << kRjShift) | rd_code;

  if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
    FlushInstructionCache(pc, 2 * kInstrSize);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_LOONG64
