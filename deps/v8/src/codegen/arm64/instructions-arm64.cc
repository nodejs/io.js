// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM64

#include "src/codegen/arm64/instructions-arm64.h"

#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/common/code-memory-access-inl.h"

namespace v8 {
namespace internal {

void Instruction::SetInstructionBits(Instr new_instr,
                                     WritableJitAllocation* jit_allocation) {
  // Usually this is aligned, but when de/serializing that's not guaranteed.
  if (jit_allocation) {
    jit_allocation->WriteUnalignedValue(reinterpret_cast<Address>(this),
                                        new_instr);
  } else {
    base::WriteUnalignedValue(reinterpret_cast<Address>(this), new_instr);
  }
}

bool Instruction::IsLoad() const {
  if (Mask(LoadStoreAnyFMask) != LoadStoreAnyFixed) {
    return false;
  }

  if (Mask(LoadStorePairAnyFMask) == LoadStorePairAnyFixed) {
    return Mask(LoadStorePairLBit) != 0;
  } else {
    LoadStoreOp op = static_cast<LoadStoreOp>(Mask(LoadStoreMask));
    switch (op) {
      case LDRB_w:
      case LDRH_w:
      case LDR_w:
      case LDR_x:
      case LDRSB_w:
      case LDRSB_x:
      case LDRSH_w:
      case LDRSH_x:
      case LDRSW_x:
      case LDR_b:
      case LDR_h:
      case LDR_s:
      case LDR_d:
      case LDR_q:
        return true;
      default:
        return false;
    }
  }
}

bool Instruction::IsStore() const {
  if (Mask(LoadStoreAnyFMask) != LoadStoreAnyFixed) {
    return false;
  }

  if (Mask(LoadStorePairAnyFMask) == LoadStorePairAnyFixed) {
    return Mask(LoadStorePairLBit) == 0;
  } else {
    LoadStoreOp op = static_cast<LoadStoreOp>(Mask(LoadStoreMask));
    switch (op) {
      case STRB_w:
      case STRH_w:
      case STR_w:
      case STR_x:
      case STR_b:
      case STR_h:
      case STR_s:
      case STR_d:
      case STR_q:
        return true;
      default:
        return false;
    }
  }
}

static uint64_t RotateRight(uint64_t value, unsigned int rotate,
                            unsigned int width) {
  DCHECK_LE(width, 64);
  rotate &= 63;
  if (rotate == 0) return value;
  return ((value & ((1ULL << rotate) - 1ULL)) << (width - rotate)) |
         (value >> rotate);
}

static uint64_t RepeatBitsAcrossReg(unsigned reg_size, uint64_t value,
                                    unsigned width) {
  DCHECK((width == 2) || (width == 4) || (width == 8) || (width == 16) ||
         (width == 32));
  DCHECK((reg_size == kWRegSizeInBits) || (reg_size == kXRegSizeInBits));
  uint64_t result = value & ((1ULL << width) - 1ULL);
  for (unsigned i = width; i < reg_size; i *= 2) {
    result |= (result << i);
  }
  return result;
}

// Logical immediates can't encode zero, so a return value of zero is used to
// indicate a failure case. Specifically, where the constraints on imm_s are not
// met.
uint64_t Instruction::ImmLogical() {
  unsigned reg_size = SixtyFourBits() ? kXRegSizeInBits : kWRegSizeInBits;
  int32_t n = BitN();
  int32_t imm_s = ImmSetBits();
  int32_t imm_r = ImmRotate();

  // An integer is constructed from the n, imm_s and imm_r bits according to
  // the following table:
  //
  //  N   imms    immr    size        S             R
  //  1  ssssss  rrrrrr    64    UInt(ssssss)  UInt(rrrrrr)
  //  0  0sssss  xrrrrr    32    UInt(sssss)   UInt(rrrrr)
  //  0  10ssss  xxrrrr    16    UInt(ssss)    UInt(rrrr)
  //  0  110sss  xxxrrr     8    UInt(sss)     UInt(rrr)
  //  0  1110ss  xxxxrr     4    UInt(ss)      UInt(rr)
  //  0  11110s  xxxxxr     2    UInt(s)       UInt(r)
  // (s bits must not be all set)
  //
  // A pattern is constructed of size bits, where the least significant S+1
  // bits are set. The pattern is rotated right by R, and repeated across a
  // 32 or 64-bit value, depending on destination register width.
  //

  if (n == 1) {
    if (imm_s == 0x3F) {
      return 0;
    }
    uint64_t bits = (1ULL << (imm_s + 1)) - 1;
    return RotateRight(bits, imm_r, 64);
  } else {
    if ((imm_s >> 1) == 0x1F) {
      return 0;
    }
    for (int width = 0x20; width >= 0x2; width >>= 1) {
      if ((imm_s & width) == 0) {
        int mask = width - 1;
        if ((imm_s & mask) == mask) {
          return 0;
        }
        uint64_t bits = (1ULL << ((imm_s & mask) + 1)) - 1;
        return RepeatBitsAcrossReg(
            reg_size, RotateRight(bits, imm_r & mask, width), width);
      }
    }
  }
  UNREACHABLE();
}

uint32_t Instruction::ImmNEONabcdefgh() const {
  return ImmNEONabc() << 5 | ImmNEONdefgh();
}

float Instruction::ImmFP32() { return Imm8ToFP32(ImmFP()); }

double Instruction::ImmFP64() { return Imm8ToFP64(ImmFP()); }

float Instruction::ImmNEONFP32() const { return Imm8ToFP32(ImmNEONabcdefgh()); }

double Instruction::ImmNEONFP64() const {
  return Imm8ToFP64(ImmNEONabcdefgh());
}

unsigned CalcLSDataSizeLog2(LoadStoreOp op) {
  DCHECK_EQ(LSSize_offset + LSSize_width, kInstrSize * 8);
  unsigned size_log2 = static_cast<Instr>(op) >> LSSize_offset;
  if ((op & LSVector_mask) != 0) {
    // Vector register memory operations encode the access size in the "size"
    // and "opc" fields.
    if (size_log2 == 0 && ((op & LSOpc_mask) >> LSOpc_offset) >= 2) {
      size_log2 = kQRegSizeLog2;
    }
  }
  return size_log2;
}

unsigned CalcLSPairDataSize(LoadStorePairOp op) {
  static_assert(kXRegSize == kDRegSize, "X and D registers must be same size.");
  static_assert(kWRegSize == kSRegSize, "W and S registers must be same size.");
  switch (op) {
    case STP_q:
    case LDP_q:
      return kQRegSizeLog2;
    case STP_x:
    case LDP_x:
    case STP_d:
    case LDP_d:
      return kXRegSizeLog2;
    default:
      return kWRegSizeLog2;
  }
}

int64_t Instruction::ImmPCOffset() {
  int64_t offset;
  if (IsPCRelAddressing()) {
    // PC-relative addressing. Only ADR is supported.
    offset = ImmPCRel();
  } else if (BranchType() != UnknownBranchType) {
    // All PC-relative branches.
    // Relative branch offsets are instruction-size-aligned.
    offset = ImmBranch() * kInstrSize;
  } else if (IsUnresolvedInternalReference()) {
    // Internal references are always word-aligned.
    offset = ImmUnresolvedInternalReference() * kInstrSize;
  } else {
    // Load literal (offset from PC).
    DCHECK(IsLdrLiteral());
    // The offset is always shifted by 2 bits, even for loads to 64-bits
    // registers.
    offset = ImmLLiteral() * kInstrSize;
  }
  return offset;
}

Instruction* Instruction::ImmPCOffsetTarget() {
  return InstructionAtOffset(ImmPCOffset());
}

bool Instruction::IsTargetInImmPCOffsetRange(Instruction* target) {
  return IsValidImmPCOffset(BranchType(), DistanceTo(target));
}

void Instruction::SetImmPCOffsetTarget(Zone* zone, AssemblerOptions options,
                                       Instruction* target) {
  if (IsPCRelAddressing()) {
    SetPCRelImmTarget(zone, options, target);
  } else if (IsCondBranchImm()) {
    SetBranchImmTarget<CondBranchType>(target);
  } else if (IsUncondBranchImm()) {
    SetBranchImmTarget<UncondBranchType>(target);
  } else if (IsCompareBranch()) {
    SetBranchImmTarget<CompareBranchType>(target);
  } else if (IsTestBranch()) {
    SetBranchImmTarget<TestBranchType>(target);
  } else if (IsUnresolvedInternalReference()) {
    SetUnresolvedInternalReferenceImmTarget(zone, options, target);
  } else {
    // Load literal (offset from PC).
    SetImmLLiteral(target);
  }
}

void Instruction::SetPCRelImmTarget(Zone* zone, AssemblerOptions options,
                                    Instruction* target) {
  // ADRP is not supported, so 'this' must point to an ADR instruction.
  DCHECK(IsAdr());

  ptrdiff_t target_offset = DistanceTo(target);
  Instr imm;
  if (Instruction::IsValidPCRelOffset(target_offset)) {
    imm = Assembler::ImmPCRelAddress(static_cast<int>(target_offset));
    SetInstructionBits(Mask(~ImmPCRel_mask) | imm);
  } else {
    PatchingAssembler patcher(zone, options, reinterpret_cast<uint8_t*>(this),
                              PatchingAssembler::kAdrFarPatchableNInstrs);
    patcher.PatchAdrFar(target_offset);
  }
}

void Instruction::SetUnresolvedInternalReferenceImmTarget(
    Zone* zone, AssemblerOptions options, Instruction* target) {
  DCHECK(IsUnresolvedInternalReference());
  DCHECK(IsAligned(DistanceTo(target), kInstrSize));
  DCHECK(is_int32(DistanceTo(target) >> kInstrSizeLog2));
  int32_t target_offset =
      static_cast<int32_t>(DistanceTo(target) >> kInstrSizeLog2);
  uint32_t high16 = unsigned_bitextract_32(31, 16, target_offset);
  uint32_t low16 = unsigned_bitextract_32(15, 0, target_offset);

  PatchingAssembler patcher(zone, options, reinterpret_cast<uint8_t*>(this), 2);
  patcher.brk(high16);
  patcher.brk(low16);
}

void Instruction::SetImmLLiteral(Instruction* source) {
  DCHECK(IsLdrLiteral());
  DCHECK(IsAligned(DistanceTo(source), kInstrSize));
  DCHECK(Assembler::IsImmLLiteral(DistanceTo(source)));
  Instr imm = Assembler::ImmLLiteral(
      static_cast<int>(DistanceTo(source) >> kLoadLiteralScaleLog2));
  Instr mask = ImmLLiteral_mask;

  SetInstructionBits(Mask(~mask) | imm);
}

NEONFormatDecoder::NEONFormatDecoder(const Instruction* instr) {
  instrbits_ = instr->InstructionBits();
  SetFormatMaps(IntegerFormatMap());
}

NEONFormatDecoder::NEONFormatDecoder(const Instruction* instr,
                                     const NEONFormatMap* format) {
  instrbits_ = instr->InstructionBits();
  SetFormatMaps(format);
}

NEONFormatDecoder::NEONFormatDecoder(const Instruction* instr,
                                     const NEONFormatMap* format0,
                                     const NEONFormatMap* format1) {
  instrbits_ = instr->InstructionBits();
  SetFormatMaps(format0, format1);
}

NEONFormatDecoder::NEONFormatDecoder(const Instruction* instr,
                                     const NEONFormatMap* format0,
                                     const NEONFormatMap* format1,
                                     const NEONFormatMap* format2) {
  instrbits_ = instr->InstructionBits();
  SetFormatMaps(format0, format1, format2);
}

void NEONFormatDecoder::SetFormatMaps(const NEONFormatMap* format0,
                                      const NEONFormatMap* format1,
                                      const NEONFormatMap* format2) {
  DCHECK_NOT_NULL(format0);
  formats_[0] = format0;
  formats_[1] = (format1 == nullptr) ? formats_[0] : format1;
  formats_[2] = (format2 == nullptr) ? formats_[1] : format2;
  // Support four parameters form (e.i. ld4r)
  // to avoid using positional arguments in DisassemblingDecoder.
  // See: https://crbug.com/v8/10365
  formats_[3] = formats_[2];
}

void NEONFormatDecoder::SetFormatMap(unsigned index,
                                     const NEONFormatMap* format) {
  DCHECK_LT(index, arraysize(formats_));
  DCHECK_NOT_NULL(format);
  formats_[index] = format;
}

const char* NEONFormatDecoder::SubstitutePlaceholders(const char* string) {
  return Substitute(string, kPlaceholder, kPlaceholder, kPlaceholder,
                    kPlaceholder);
}

const char* NEONFormatDecoder::Substitute(const char* string,
                                          SubstitutionMode mode0,
                                          SubstitutionMode mode1,
                                          SubstitutionMode mode2,
                                          SubstitutionMode mode3) {
  snprintf(form_buffer_, sizeof(form_buffer_), string, GetSubstitute(0, mode0),
           GetSubstitute(1, mode1), GetSubstitute(2, mode2),
           GetSubstitute(3, mode3));
  return form_buffer_;
}

const char* NEONFormatDecoder::Mnemonic(const char* mnemonic) {
  if ((instrbits_ & NEON_Q) != 0) {
    snprintf(mne_buffer_, sizeof(mne_buffer_), "%s2", mnemonic);
    return mne_buffer_;
  }
  return mnemonic;
}

VectorFormat NEONFormatDecoder::GetVectorFormat(int format_index) {
  return GetVectorFormat(formats_[format_index]);
}

VectorFormat NEONFormatDecoder::GetVectorFormat(
    const NEONFormatMap* format_map) {
  static const VectorFormat vform[] = {
      kFormatUndefined, kFormat8B, kFormat16B, kFormat4H, kFormat8H,
      kFormat2S,        kFormat4S, kFormat1D,  kFormat2D, kFormatB,
      kFormatH,         kFormatS,  kFormatD};
  DCHECK_LT(GetNEONFormat(format_map), arraysize(vform));
  return vform[GetNEONFormat(format_map)];
}

const char* NEONFormatDecoder::GetSubstitute(int index, SubstitutionMode mode) {
  if (mode == kFormat) {
    return NEONFormatAsString(GetNEONFormat(formats_[index]));
  }
  DCHECK_EQ(mode, kPlaceholder);
  return NEONFormatAsPlaceholder(GetNEONFormat(formats_[index]));
}

NEONFormat NEONFormatDecoder::GetNEONFormat(const NEONFormatMap* format_map) {
  return format_map->map[PickBits(format_map->bits)];
}

const char* NEONFormatDecoder::NEONFormatAsString(NEONFormat format) {
  static const char* formats[] = {"undefined", "8b", "16b", "4h", "8h",
                                  "2s",        "4s", "1d",  "2d", "b",
                                  "h",         "s",  "d"};
  DCHECK_LT(format, arraysize(formats));
  return formats[format];
}

const char* NEONFormatDecoder::NEONFormatAsPlaceholder(NEONFormat format) {
  DCHECK((format == NF_B) || (format == NF_H) || (format == NF_S) ||
         (format == NF_D) || (format == NF_UNDEF));
  static const char* formats[] = {
      "undefined", "undefined", "undefined", "undefined", "undefined",
      "undefined", "undefined", "undefined", "undefined", "'B",
      "'H",        "'S",        "'D"};
  return formats[format];
}

uint8_t NEONFormatDecoder::PickBits(const uint8_t bits[]) {
  uint8_t result = 0;
  for (unsigned b = 0; b < kNEONFormatMaxBits; b++) {
    if (bits[b] == 0) break;
    result <<= 1;
    result |= ((instrbits_ & (1 << bits[b])) == 0) ? 0 : 1;
  }
  return result;
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64
