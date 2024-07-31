// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/reloc-info.h"

#include "src/base/vlq.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/code-reference.h"
#include "src/codegen/external-reference-encoder.h"
#include "src/codegen/reloc-info-inl.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/code-inl.h"
#include "src/snapshot/embedded/embedded-data-inl.h"

namespace v8 {
namespace internal {

using namespace detail;

uint32_t RelocInfoWriter::WriteLongPCJump(uint32_t pc_delta) {
  // Return if the pc_delta can fit in kSmallPCDeltaBits bits.
  // Otherwise write a variable length PC jump for the bits that do
  // not fit in the kSmallPCDeltaBits bits.
  if (is_uintn(pc_delta, kSmallPCDeltaBits)) return pc_delta;
  WriteMode(RelocInfo::PC_JUMP);
  uint32_t pc_jump = pc_delta >> kSmallPCDeltaBits;
  DCHECK_GT(pc_jump, 0);
  base::VLQEncodeUnsigned(
      [this](uint8_t byte) {
        *--pos_ = byte;
        return pos_;
      },
      pc_jump);
  // Return the remaining kSmallPCDeltaBits of the pc_delta.
  return pc_delta & kSmallPCDeltaMask;
}

void RelocInfoWriter::WriteShortTaggedPC(uint32_t pc_delta, int tag) {
  // Write a byte of tagged pc-delta, possibly preceded by an explicit pc-jump.
  pc_delta = WriteLongPCJump(pc_delta);
  *--pos_ = pc_delta << kTagBits | tag;
}

void RelocInfoWriter::WriteShortData(intptr_t data_delta) {
  *--pos_ = static_cast<uint8_t>(data_delta);
}

void RelocInfoWriter::WriteMode(RelocInfo::Mode rmode) {
  static_assert(RelocInfo::NUMBER_OF_MODES <= (1 << kLongTagBits));
  *--pos_ = static_cast<int>((rmode << kTagBits) | kDefaultTag);
}

void RelocInfoWriter::WriteModeAndPC(uint32_t pc_delta, RelocInfo::Mode rmode) {
  // Write two-byte tagged pc-delta, possibly preceded by var. length pc-jump.
  pc_delta = WriteLongPCJump(pc_delta);
  WriteMode(rmode);
  *--pos_ = pc_delta;
}

void RelocInfoWriter::WriteIntData(int number) {
  for (int i = 0; i < kIntSize; i++) {
    *--pos_ = static_cast<uint8_t>(number);
    // Signed right shift is arithmetic shift.  Tested in test-utils.cc.
    number = number >> kBitsPerByte;
  }
}

void RelocInfoWriter::Write(const RelocInfo* rinfo) {
  RelocInfo::Mode rmode = rinfo->rmode();
#ifdef DEBUG
  uint8_t* begin_pos = pos_;
#endif
  DCHECK(rinfo->rmode() < RelocInfo::NUMBER_OF_MODES);
  DCHECK_GE(rinfo->pc() - reinterpret_cast<Address>(last_pc_), 0);
  // Use unsigned delta-encoding for pc.
  uint32_t pc_delta =
      static_cast<uint32_t>(rinfo->pc() - reinterpret_cast<Address>(last_pc_));

  // The two most common modes are given small tags, and usually fit in a byte.
  if (rmode == RelocInfo::FULL_EMBEDDED_OBJECT) {
    WriteShortTaggedPC(pc_delta, kEmbeddedObjectTag);
  } else if (rmode == RelocInfo::CODE_TARGET) {
    WriteShortTaggedPC(pc_delta, kCodeTargetTag);
    DCHECK_LE(begin_pos - pos_, RelocInfo::kMaxCallSize);
  } else if (rmode == RelocInfo::WASM_STUB_CALL) {
    WriteShortTaggedPC(pc_delta, kWasmStubCallTag);
  } else {
    WriteModeAndPC(pc_delta, rmode);
    if (RelocInfo::IsDeoptReason(rmode)) {
      DCHECK_LT(rinfo->data(), 1 << kBitsPerByte);
      WriteShortData(rinfo->data());
    } else if (RelocInfo::IsConstPool(rmode) ||
               RelocInfo::IsVeneerPool(rmode) || RelocInfo::IsDeoptId(rmode) ||
               RelocInfo::IsDeoptPosition(rmode) ||
               RelocInfo::IsDeoptNodeId(rmode) ||
               RelocInfo::IsRelativeSwitchTableEntry(rmode)) {
      WriteIntData(static_cast<int>(rinfo->data()));
    }
  }
  last_pc_ = reinterpret_cast<uint8_t*>(rinfo->pc());
#ifdef DEBUG
  DCHECK_LE(begin_pos - pos_, kMaxSize);
#endif
}

template <typename RelocInfoT>
void RelocIteratorBase<RelocInfoT>::AdvanceReadInt() {
  int x = 0;
  for (int i = 0; i < kIntSize; i++) {
    x |= static_cast<int>(*--pos_) << i * kBitsPerByte;
  }
  rinfo_.data_ = x;
}

template <typename RelocInfoT>
void RelocIteratorBase<RelocInfoT>::AdvanceReadLongPCJump() {
  // Read the 32-kSmallPCDeltaBits most significant bits of the
  // pc jump as a VLQ encoded integer.
  uint32_t pc_jump = base::VLQDecodeUnsigned([this] { return *--pos_; });
  // The least significant kSmallPCDeltaBits bits will be added
  // later.
  rinfo_.pc_ += pc_jump << kSmallPCDeltaBits;
}

template <typename RelocInfoT>
inline void RelocIteratorBase<RelocInfoT>::ReadShortData() {
  uint8_t unsigned_b = *pos_;
  rinfo_.data_ = unsigned_b;
}

template <typename RelocInfoT>
void RelocIteratorBase<RelocInfoT>::next() {
  DCHECK(!done());
  // Basically, do the opposite of RelocInfoWriter::Write.
  // Reading of data is as far as possible avoided for unwanted modes,
  // but we must always update the pc.
  //
  // We exit this loop by returning when we find a mode we want.
  while (pos_ > end_) {
    int tag = AdvanceGetTag();
    if (tag == kEmbeddedObjectTag) {
      ReadShortTaggedPC();
      if (SetMode(RelocInfo::FULL_EMBEDDED_OBJECT)) return;
    } else if (tag == kCodeTargetTag) {
      ReadShortTaggedPC();
      if (SetMode(RelocInfo::CODE_TARGET)) return;
    } else if (tag == kWasmStubCallTag) {
      ReadShortTaggedPC();
      if (SetMode(RelocInfo::WASM_STUB_CALL)) return;
    } else {
      DCHECK_EQ(tag, kDefaultTag);
      RelocInfo::Mode rmode = GetMode();
      if (rmode == RelocInfo::PC_JUMP) {
        AdvanceReadLongPCJump();
      } else {
        AdvanceReadPC();
        if (RelocInfo::IsDeoptReason(rmode)) {
          Advance();
          if (SetMode(rmode)) {
            ReadShortData();
            return;
          }
        } else if (RelocInfo::IsConstPool(rmode) ||
                   RelocInfo::IsVeneerPool(rmode) ||
                   RelocInfo::IsDeoptId(rmode) ||
                   RelocInfo::IsDeoptPosition(rmode) ||
                   RelocInfo::IsDeoptNodeId(rmode) ||
                   RelocInfo::IsRelativeSwitchTableEntry(rmode)) {
          if (SetMode(rmode)) {
            AdvanceReadInt();
            return;
          }
          Advance(kIntSize);
        } else if (SetMode(static_cast<RelocInfo::Mode>(rmode))) {
          return;
        }
      }
    }
  }
  done_ = true;
}

RelocIterator::RelocIterator(Tagged<Code> code, int mode_mask)
    : RelocIterator(code->instruction_stream(), mode_mask) {}

RelocIterator::RelocIterator(Tagged<InstructionStream> istream, int mode_mask)
    : RelocIterator(
          istream->instruction_start(), istream->constant_pool(),
          // Use unchecked accessors since this can be called during GC
          istream->unchecked_relocation_info()->end(),
          istream->unchecked_relocation_info()->begin(), mode_mask) {}

RelocIterator::RelocIterator(const CodeReference code_reference)
    : RelocIterator(code_reference.instruction_start(),
                    code_reference.constant_pool(),
                    code_reference.relocation_end(),
                    code_reference.relocation_start(), kAllModesMask) {}

RelocIterator::RelocIterator(EmbeddedData* embedded_data, Tagged<Code> code,
                             int mode_mask)
    : RelocIterator(embedded_data->InstructionStartOf(code->builtin_id()),
                    code->constant_pool(), code->relocation_end(),
                    code->relocation_start(), mode_mask) {}

RelocIterator::RelocIterator(base::Vector<uint8_t> instructions,
                             base::Vector<const uint8_t> reloc_info,
                             Address const_pool, int mode_mask)
    : RelocIterator(reinterpret_cast<Address>(instructions.begin()), const_pool,
                    reloc_info.begin() + reloc_info.size(), reloc_info.begin(),
                    mode_mask) {}

RelocIterator::RelocIterator(Address pc, Address constant_pool,
                             const uint8_t* pos, const uint8_t* end,
                             int mode_mask)
    : RelocIteratorBase<RelocInfo>(
          RelocInfo(pc, RelocInfo::NO_INFO, 0, constant_pool), pos, end,
          mode_mask) {}

WritableRelocIterator::WritableRelocIterator(
    WritableJitAllocation& jit_allocation, Tagged<InstructionStream> istream,
    Address constant_pool, int mode_mask)
    : RelocIteratorBase<WritableRelocInfo>(
          WritableRelocInfo(jit_allocation, istream->instruction_start(),
                            RelocInfo::NO_INFO, 0, constant_pool),
          // Use unchecked accessors since this can be called during GC
          istream->unchecked_relocation_info()->end(),
          istream->unchecked_relocation_info()->begin(), mode_mask) {}

WritableRelocIterator::WritableRelocIterator(
    WritableJitAllocation& jit_allocation, base::Vector<uint8_t> instructions,
    base::Vector<const uint8_t> reloc_info, Address constant_pool,
    int mode_mask)
    : RelocIteratorBase<WritableRelocInfo>(
          WritableRelocInfo(jit_allocation,
                            reinterpret_cast<Address>(instructions.begin()),
                            RelocInfo::NO_INFO, 0, constant_pool),
          reloc_info.begin() + reloc_info.size(), reloc_info.begin(),
          mode_mask) {}

// -----------------------------------------------------------------------------
// Implementation of RelocInfo

// static
bool RelocInfo::OffHeapTargetIsCodedSpecially() {
#if defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_ARM64) || \
    defined(V8_TARGET_ARCH_X64)
  return false;
#elif defined(V8_TARGET_ARCH_IA32) || defined(V8_TARGET_ARCH_MIPS64) || \
    defined(V8_TARGET_ARCH_PPC) || defined(V8_TARGET_ARCH_PPC64) ||     \
    defined(V8_TARGET_ARCH_S390) || defined(V8_TARGET_ARCH_RISCV64) ||  \
    defined(V8_TARGET_ARCH_LOONG64) || defined(V8_TARGET_ARCH_RISCV32)
  return true;
#endif
}

Address RelocInfo::wasm_call_address() const {
  DCHECK_EQ(rmode_, WASM_CALL);
  return Assembler::target_address_at(pc_, constant_pool_);
}

void WritableRelocInfo::set_wasm_call_address(
    Address address, ICacheFlushMode icache_flush_mode) {
  DCHECK_EQ(rmode_, WASM_CALL);
  Assembler::set_target_address_at(pc_, constant_pool_, address,
                                   icache_flush_mode);
}

Address RelocInfo::wasm_stub_call_address() const {
  DCHECK_EQ(rmode_, WASM_STUB_CALL);
  return Assembler::target_address_at(pc_, constant_pool_);
}

void WritableRelocInfo::set_wasm_stub_call_address(
    Address address, ICacheFlushMode icache_flush_mode) {
  DCHECK_EQ(rmode_, WASM_STUB_CALL);
  Assembler::set_target_address_at(pc_, constant_pool_, address,
                                   icache_flush_mode);
}

void WritableRelocInfo::set_target_address(Address target,
                                           ICacheFlushMode icache_flush_mode) {
  DCHECK(IsCodeTargetMode(rmode_) || IsNearBuiltinEntry(rmode_) ||
         IsWasmCall(rmode_));
  Assembler::set_target_address_at(pc_, constant_pool_, target,
                                   icache_flush_mode);
}

void WritableRelocInfo::set_target_address(Tagged<InstructionStream> host,
                                           Address target,
                                           WriteBarrierMode write_barrier_mode,
                                           ICacheFlushMode icache_flush_mode) {
  set_target_address(target, icache_flush_mode);
  if (IsCodeTargetMode(rmode_) && !v8_flags.disable_write_barriers) {
    Tagged<InstructionStream> target_code =
        InstructionStream::FromTargetAddress(target);
    WriteBarrierForCode(host, this, target_code, write_barrier_mode);
  }
}

void RelocInfo::set_off_heap_target_address(Address target,
                                            ICacheFlushMode icache_flush_mode) {
  DCHECK(IsCodeTargetMode(rmode_));
  Assembler::set_target_address_at(pc_, constant_pool_, target,
                                   icache_flush_mode);
}

bool RelocInfo::HasTargetAddressAddress() const {
  // TODO(jgruber): Investigate whether WASM_CALL is still appropriate on
  // non-intel platforms now that wasm code is no longer on the heap.
#if defined(V8_TARGET_ARCH_IA32) || defined(V8_TARGET_ARCH_X64)
  static constexpr int kTargetAddressAddressModeMask =
      ModeMask(CODE_TARGET) | ModeMask(FULL_EMBEDDED_OBJECT) |
      ModeMask(COMPRESSED_EMBEDDED_OBJECT) | ModeMask(EXTERNAL_REFERENCE) |
      ModeMask(OFF_HEAP_TARGET) | ModeMask(WASM_CALL) |
      ModeMask(WASM_STUB_CALL);
#else
  static constexpr int kTargetAddressAddressModeMask =
      ModeMask(CODE_TARGET) | ModeMask(RELATIVE_CODE_TARGET) |
      ModeMask(FULL_EMBEDDED_OBJECT) | ModeMask(EXTERNAL_REFERENCE) |
      ModeMask(OFF_HEAP_TARGET) | ModeMask(WASM_CALL);
#endif
  return (ModeMask(rmode_) & kTargetAddressAddressModeMask) != 0;
}

#ifdef ENABLE_DISASSEMBLER
const char* RelocInfo::RelocModeName(RelocInfo::Mode rmode) {
  switch (rmode) {
    case NO_INFO:
      return "no reloc";
    case COMPRESSED_EMBEDDED_OBJECT:
      return "compressed embedded object";
    case FULL_EMBEDDED_OBJECT:
      return "full embedded object";
    case CODE_TARGET:
      return "code target";
    case RELATIVE_CODE_TARGET:
      return "relative code target";
    case EXTERNAL_REFERENCE:
      return "external reference";
    case INTERNAL_REFERENCE:
      return "internal reference";
    case INTERNAL_REFERENCE_ENCODED:
      return "encoded internal reference";
    case RELATIVE_SWITCH_TABLE_ENTRY:
      return "relative switch table entry";
    case OFF_HEAP_TARGET:
      return "off heap target";
    case NEAR_BUILTIN_ENTRY:
      return "near builtin entry";
    case DEOPT_SCRIPT_OFFSET:
      return "deopt script offset";
    case DEOPT_INLINING_ID:
      return "deopt inlining id";
    case DEOPT_REASON:
      return "deopt reason";
    case DEOPT_ID:
      return "deopt index";
    case DEOPT_NODE_ID:
      return "deopt node id";
    case CONST_POOL:
      return "constant pool";
    case VENEER_POOL:
      return "veneer pool";
    case WASM_CALL:
      return "internal wasm call";
    case WASM_STUB_CALL:
      return "wasm stub call";
    case NUMBER_OF_MODES:
    case PC_JUMP:
      UNREACHABLE();
  }
  return "unknown relocation type";
}

void RelocInfo::Print(Isolate* isolate, std::ostream& os) {
  os << reinterpret_cast<const void*>(pc_) << "  " << RelocModeName(rmode_);
  if (rmode_ == DEOPT_SCRIPT_OFFSET || rmode_ == DEOPT_INLINING_ID) {
    os << "  (" << data() << ")";
  } else if (rmode_ == DEOPT_REASON) {
    os << "  ("
       << DeoptimizeReasonToString(static_cast<DeoptimizeReason>(data_)) << ")";
  } else if (rmode_ == FULL_EMBEDDED_OBJECT) {
    os << "  (" << Brief(target_object(isolate)) << ")";
  } else if (rmode_ == COMPRESSED_EMBEDDED_OBJECT) {
    os << "  (" << Brief(target_object(isolate)) << " compressed)";
  } else if (rmode_ == EXTERNAL_REFERENCE) {
    if (isolate) {
      ExternalReferenceEncoder ref_encoder(isolate);
      os << " ("
         << ref_encoder.NameOfAddress(isolate, target_external_reference())
         << ") ";
    }
    os << " (" << reinterpret_cast<const void*>(target_external_reference())
       << ")";
  } else if (IsCodeTargetMode(rmode_)) {
    const Address code_target = target_address();
    Tagged<Code> target_code = Code::FromTargetAddress(code_target);
    os << " (" << CodeKindToString(target_code->kind());
    if (Builtins::IsBuiltin(target_code)) {
      os << " " << Builtins::name(target_code->builtin_id());
    }
    os << ")  (" << reinterpret_cast<const void*>(target_address()) << ")";
  } else if (IsConstPool(rmode_)) {
    os << " (size " << static_cast<int>(data_) << ")";
  } else if (IsWasmStubCall(rmode_)) {
    os << "  (";
    Address addr = target_address();
    if (isolate != nullptr) {
      Builtin builtin = OffHeapInstructionStream::TryLookupCode(isolate, addr);
      os << (Builtins::IsBuiltinId(builtin) ? Builtins::name(builtin)
                                            : "<UNRECOGNIZED>")
         << ")  (";
    }
    os << reinterpret_cast<const void*>(addr) << ")";
  }

  os << "\n";
}
#endif  // ENABLE_DISASSEMBLER

#ifdef VERIFY_HEAP
void RelocInfo::Verify(Isolate* isolate) {
  switch (rmode_) {
    case COMPRESSED_EMBEDDED_OBJECT:
      Object::VerifyPointer(isolate, target_object(isolate));
      break;
    case FULL_EMBEDDED_OBJECT:
      Object::VerifyAnyTagged(isolate, target_object(isolate));
      break;
    case CODE_TARGET:
    case RELATIVE_CODE_TARGET: {
      // convert inline target address to code object
      Address addr = target_address();
      CHECK_NE(addr, kNullAddress);
      // Check that we can find the right code object.
      Tagged<InstructionStream> code =
          InstructionStream::FromTargetAddress(addr);
      Tagged<Code> lookup_result =
          isolate->heap()->FindCodeForInnerPointer(addr);
      CHECK_EQ(code.address(), lookup_result->instruction_stream().address());
      break;
    }
    case INTERNAL_REFERENCE:
    case INTERNAL_REFERENCE_ENCODED: {
      Address target = target_internal_reference();
      Address pc = target_internal_reference_address();
      Tagged<Code> lookup_result = isolate->heap()->FindCodeForInnerPointer(pc);
      CHECK_GE(target, lookup_result->instruction_start());
      CHECK_LT(target, lookup_result->instruction_end());
      break;
    }
    case OFF_HEAP_TARGET: {
      Address addr = target_off_heap_target();
      CHECK_NE(addr, kNullAddress);
      CHECK(Builtins::IsBuiltinId(
          OffHeapInstructionStream::TryLookupCode(isolate, addr)));
      break;
    }
    case WASM_STUB_CALL:
    case NEAR_BUILTIN_ENTRY: {
      Address addr = target_address();
      CHECK_NE(addr, kNullAddress);
      CHECK(Builtins::IsBuiltinId(
          OffHeapInstructionStream::TryLookupCode(isolate, addr)));
      break;
    }
    case EXTERNAL_REFERENCE:
    case DEOPT_SCRIPT_OFFSET:
    case DEOPT_INLINING_ID:
    case DEOPT_REASON:
    case DEOPT_ID:
    case DEOPT_NODE_ID:
    case CONST_POOL:
    case VENEER_POOL:
    case WASM_CALL:
    case NO_INFO:
    case RELATIVE_SWITCH_TABLE_ENTRY:
      break;
    case NUMBER_OF_MODES:
    case PC_JUMP:
      UNREACHABLE();
  }
}
#endif  // VERIFY_HEAP

template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    RelocIteratorBase<RelocInfo>;
template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    RelocIteratorBase<WritableRelocInfo>;

}  // namespace internal
}  // namespace v8
