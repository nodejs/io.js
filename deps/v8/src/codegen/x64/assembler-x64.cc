// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/x64/assembler-x64.h"

#include <cstring>

#include "src/utils/utils.h"

#if V8_TARGET_ARCH_X64

#if V8_LIBC_MSVCRT
#include <intrin.h>  // _xgetbv()
#endif
#if V8_OS_DARWIN
#include <sys/sysctl.h>
#endif

#include "src/base/bits.h"
#include "src/base/cpu.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/flags/flags.h"
#include "src/init/v8.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Implementation of CpuFeatures

namespace {

#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64

V8_INLINE uint64_t xgetbv(unsigned int xcr) {
#if V8_LIBC_MSVCRT
  return _xgetbv(xcr);
#else
  unsigned eax, edx;
  // Check xgetbv; this uses a .byte sequence instead of the instruction
  // directly because older assemblers do not include support for xgetbv and
  // there is no easy way to conditionally compile based on the assembler
  // used.
  __asm__ volatile(".byte 0x0F, 0x01, 0xD0" : "=a"(eax), "=d"(edx) : "c"(xcr));
  return static_cast<uint64_t>(eax) | (static_cast<uint64_t>(edx) << 32);
#endif
}

bool OSHasAVXSupport() {
#if V8_OS_DARWIN
  // Mac OS X up to 10.9 has a bug where AVX transitions were indeed being
  // caused by ISRs, so we detect that here and disable AVX in that case.
  char buffer[128];
  size_t buffer_size = arraysize(buffer);
  int ctl_name[] = {CTL_KERN, KERN_OSRELEASE};
  if (sysctl(ctl_name, 2, buffer, &buffer_size, nullptr, 0) != 0) {
    FATAL("V8 failed to get kernel version");
  }
  // The buffer now contains a string of the form XX.YY.ZZ, where
  // XX is the major kernel version component.
  char* period_pos = strchr(buffer, '.');
  DCHECK_NOT_NULL(period_pos);
  *period_pos = '\0';
  long kernel_version_major = strtol(buffer, nullptr, 10);  // NOLINT
  if (kernel_version_major <= 13) return false;
#endif  // V8_OS_DARWIN
  // Check whether OS claims to support AVX.
  uint64_t feature_mask = xgetbv(0);  // XCR_XFEATURE_ENABLED_MASK
  return (feature_mask & 0x6) == 0x6;
}

#endif  // V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64

}  // namespace

bool CpuFeatures::SupportsWasmSimd128() {
#if V8_ENABLE_WEBASSEMBLY
  if (IsSupported(SSE4_1)) return true;
  if (v8_flags.wasm_simd_ssse3_codegen && IsSupported(SSSE3)) return true;
#endif  // V8_ENABLE_WEBASSEMBLY
  return false;
}

void CpuFeatures::ProbeImpl(bool cross_compile) {
  // Only use statically determined features for cross compile (snapshot).
  if (cross_compile) return;

#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
  base::CPU cpu;
  CHECK(cpu.has_sse2());  // SSE2 support is mandatory.
  CHECK(cpu.has_cmov());  // CMOV support is mandatory.

  if (cpu.has_sse42()) SetSupported(SSE4_2);
  if (cpu.has_sse41()) SetSupported(SSE4_1);
  if (cpu.has_ssse3()) SetSupported(SSSE3);
  if (cpu.has_sse3()) SetSupported(SSE3);
  if (cpu.has_avx() && cpu.has_osxsave() && OSHasAVXSupport()) {
    SetSupported(AVX);
    if (cpu.has_avx2()) SetSupported(AVX2);
    if (cpu.has_fma3()) SetSupported(FMA3);
  }

  // SAHF is not generally available in long mode.
  if (cpu.has_sahf() && v8_flags.enable_sahf) SetSupported(SAHF);
  if (cpu.has_bmi1() && v8_flags.enable_bmi1) SetSupported(BMI1);
  if (cpu.has_bmi2() && v8_flags.enable_bmi2) SetSupported(BMI2);
  if (cpu.has_lzcnt() && v8_flags.enable_lzcnt) SetSupported(LZCNT);
  if (cpu.has_popcnt() && v8_flags.enable_popcnt) SetSupported(POPCNT);
  if (strcmp(v8_flags.mcpu, "auto") == 0) {
    if (cpu.is_atom()) SetSupported(INTEL_ATOM);
  } else if (strcmp(v8_flags.mcpu, "atom") == 0) {
    SetSupported(INTEL_ATOM);
  }
  if (cpu.has_intel_jcc_erratum() && v8_flags.intel_jcc_erratum_mitigation)
    SetSupported(INTEL_JCC_ERRATUM_MITIGATION);

  // Ensure that supported cpu features make sense. E.g. it is wrong to support
  // AVX but not SSE4_2, if we have --enable-avx and --no-enable-sse4-2, the
  // code above would set AVX to supported, and SSE4_2 to unsupported, then the
  // checks below will set AVX to unsupported.
  if (!v8_flags.enable_sse3) SetUnsupported(SSE3);
  if (!v8_flags.enable_ssse3 || !IsSupported(SSE3)) SetUnsupported(SSSE3);
  if (!v8_flags.enable_sse4_1 || !IsSupported(SSSE3)) SetUnsupported(SSE4_1);
  if (!v8_flags.enable_sse4_2 || !IsSupported(SSE4_1)) SetUnsupported(SSE4_2);
  if (!v8_flags.enable_avx || !IsSupported(SSE4_2)) SetUnsupported(AVX);
  if (!v8_flags.enable_avx2 || !IsSupported(AVX)) SetUnsupported(AVX2);
  if (!v8_flags.enable_fma3 || !IsSupported(AVX)) SetUnsupported(FMA3);

  // Set a static value on whether Simd is supported.
  // This variable is only used for certain archs to query SupportWasmSimd128()
  // at runtime in builtins using an extern ref. Other callers should use
  // CpuFeatures::SupportWasmSimd128().
  CpuFeatures::supports_wasm_simd_128_ = CpuFeatures::SupportsWasmSimd128();

  if (cpu.has_cetss()) SetSupported(CETSS);
  // The static variable is used for codegen of certain CETSS instructions.
  CpuFeatures::supports_cetss_ = IsSupported(CETSS);
#endif  // V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
}

void CpuFeatures::PrintTarget() {}
void CpuFeatures::PrintFeatures() {
  printf(
      "SSE3=%d SSSE3=%d SSE4_1=%d SSE4_2=%d SAHF=%d AVX=%d AVX2=%d FMA3=%d "
      "BMI1=%d "
      "BMI2=%d "
      "LZCNT=%d "
      "POPCNT=%d ATOM=%d\n",
      CpuFeatures::IsSupported(SSE3), CpuFeatures::IsSupported(SSSE3),
      CpuFeatures::IsSupported(SSE4_1), CpuFeatures::IsSupported(SSE4_2),
      CpuFeatures::IsSupported(SAHF), CpuFeatures::IsSupported(AVX),
      CpuFeatures::IsSupported(AVX2), CpuFeatures::IsSupported(FMA3),
      CpuFeatures::IsSupported(BMI1), CpuFeatures::IsSupported(BMI2),
      CpuFeatures::IsSupported(LZCNT), CpuFeatures::IsSupported(POPCNT),
      CpuFeatures::IsSupported(INTEL_ATOM));
}

// -----------------------------------------------------------------------------
// Implementation of RelocInfo

uint32_t RelocInfo::wasm_call_tag() const {
  DCHECK(rmode_ == WASM_CALL || rmode_ == WASM_STUB_CALL);
  return ReadUnalignedValue<uint32_t>(pc_);
}

// -----------------------------------------------------------------------------
// Implementation of Operand

Operand::Operand(Operand operand, int32_t offset) {
  DCHECK_GE(operand.memory().len, 1);
  // Operand encodes REX ModR/M [SIB] [Disp].
  uint8_t modrm = operand.memory().buf[0];
  DCHECK_LT(modrm, 0xC0);  // Disallow mode 3 (register target).
  bool has_sib = ((modrm & 0x07) == 0x04);
  uint8_t mode = modrm & 0xC0;
  int disp_offset = has_sib ? 2 : 1;
  int base_reg = (has_sib ? operand.memory().buf[1] : modrm) & 0x07;
  // Mode 0 with rbp/r13 as ModR/M or SIB base register always has a 32-bit
  // displacement.
  bool is_baseless = (mode == 0) && (base_reg == 0x05);  // No base or RIP base.
  int32_t disp_value = 0;
  if (mode == 0x80 || is_baseless) {
    // Mode 2 or mode 0 with rbp/r13 as base: Word displacement.
    disp_value = ReadUnalignedValue<int32_t>(
        reinterpret_cast<Address>(&operand.memory().buf[disp_offset]));
  } else if (mode == 0x40) {
    // Mode 1: Byte displacement.
    disp_value = static_cast<signed char>(operand.memory().buf[disp_offset]);
  }

  // Write new operand with same registers, but with modified displacement.
  DCHECK(offset >= 0 ? disp_value + offset > disp_value
                     : disp_value + offset < disp_value);  // No overflow.
  disp_value += offset;
  memory_.rex = operand.memory().rex;
  if (!is_int8(disp_value) || is_baseless) {
    // Need 32 bits of displacement, mode 2 or mode 1 with register rbp/r13.
    memory_.buf[0] = (modrm & 0x3F) | (is_baseless ? 0x00 : 0x80);
    memory_.len = disp_offset + 4;
    WriteUnalignedValue(reinterpret_cast<Address>(&memory_.buf[disp_offset]),
                        disp_value);
  } else if (disp_value != 0 || (base_reg == 0x05)) {
    // Need 8 bits of displacement.
    memory_.buf[0] = (modrm & 0x3F) | 0x40;  // Mode 1.
    memory_.len = disp_offset + 1;
    memory_.buf[disp_offset] = static_cast<uint8_t>(disp_value);
  } else {
    // Need no displacement.
    memory_.buf[0] = (modrm & 0x3F);  // Mode 0.
    memory_.len = disp_offset;
  }
  if (has_sib) {
    memory_.buf[1] = operand.memory().buf[1];
  }
}

bool Operand::AddressUsesRegister(Register reg) const {
  DCHECK(!is_label_operand());
  int code = reg.code();
  DCHECK_NE(memory_.buf[0] & 0xC0, 0xC0);
  // Start with only low three bits of base register. Initial decoding
  // doesn't distinguish on the REX.B bit.
  int base_code = memory_.buf[0] & 0x07;
  if (base_code == rsp.code()) {
    // SIB byte present in buf_[1].
    // Check the index register from the SIB byte + REX.X prefix.
    int index_code =
        ((memory_.buf[1] >> 3) & 0x07) | ((memory_.rex & 0x02) << 2);
    // Index code (including REX.X) of 0x04 (rsp) means no index register.
    if (index_code != rsp.code() && index_code == code) return true;
    // Add REX.B to get the full base register code.
    base_code = (memory_.buf[1] & 0x07) | ((memory_.rex & 0x01) << 3);
    // A base register of 0x05 (rbp) with mod = 0 means no base register.
    if (base_code == rbp.code() && ((memory_.buf[0] & 0xC0) == 0)) return false;
    return code == base_code;
  } else {
    // A base register with low bits of 0x05 (rbp or r13) and mod = 0 means
    // no base register.
    if (base_code == rbp.code() && ((memory_.buf[0] & 0xC0) == 0)) return false;
    base_code |= ((memory_.rex & 0x01) << 3);
    return code == base_code;
  }
}

void Assembler::AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate) {
  DCHECK_IMPLIES(isolate == nullptr, heap_number_requests_.empty());
  for (auto& request : heap_number_requests_) {
    Address pc = reinterpret_cast<Address>(buffer_start_) + request.offset();
    Handle<HeapNumber> object =
        isolate->factory()->NewHeapNumber<AllocationType::kOld>(
            request.heap_number());
    WriteUnalignedValue(pc, object);
  }
}

// Partial Constant Pool.
bool ConstPool::AddSharedEntry(uint64_t data, int offset) {
  auto existing = entries_.find(data);
  if (existing == entries_.end()) {
    entries_.insert(std::make_pair(data, offset + kMoveImm64Offset));
    return false;
  }

  // Make sure this is called with strictly ascending offsets.
  DCHECK_GT(offset + kMoveImm64Offset, existing->second);

  entries_.insert(std::make_pair(data, offset + kMoveRipRelativeDispOffset));
  return true;
}

bool ConstPool::TryRecordEntry(intptr_t data, RelocInfo::Mode mode) {
  if (!v8_flags.partial_constant_pool) return false;
  DCHECK_WITH_MSG(
      v8_flags.text_is_readable,
      "The partial constant pool requires a readable .text section");
  if (!RelocInfo::IsShareableRelocMode(mode)) return false;

  // Currently, partial constant pool only handles the following kinds of
  // RelocInfo.
  if (mode != RelocInfo::NO_INFO && mode != RelocInfo::EXTERNAL_REFERENCE &&
      mode != RelocInfo::OFF_HEAP_TARGET)
    return false;

  uint64_t raw_data = static_cast<uint64_t>(data);
  int offset = assm_->pc_offset();
  return AddSharedEntry(raw_data, offset);
}

bool ConstPool::IsMoveRipRelative(Address instr) {
  return (ReadUnalignedValue<uint32_t>(instr) & kMoveRipRelativeMask) ==
         kMoveRipRelativeInstr;
}

void ConstPool::Clear() { entries_.clear(); }

void ConstPool::PatchEntries() {
  auto iter = entries_.begin();
  if (iter == entries_.end()) return;

  // Read off the first value/offset pair before starting the loop proper.
  std::pair<uint64_t, int> first_entry_of_range = *iter;
  while (++iter != entries_.end()) {
    // Check if we've entered a new set of values.
    if (first_entry_of_range.first != iter->first) {
      // Make sure that this iterator is both the (exclusive) end of the
      // previous value's equal range, and the start of this value's equal
      // range.
      DCHECK_EQ(entries_.equal_range(first_entry_of_range.first).second, iter);
      DCHECK_EQ(entries_.equal_range(iter->first).first, iter);
      first_entry_of_range = *iter;
      continue;
    }
    int constant_entry_offset = first_entry_of_range.second;

    DCHECK_GT(constant_entry_offset, 0);
    DCHECK_LT(constant_entry_offset, iter->second);
    int32_t disp32 =
        constant_entry_offset - (iter->second + kRipRelativeDispSize);
    Address disp_addr = assm_->addr_at(iter->second);

    // Check if the instruction is actually a rip-relative move.
    DCHECK(IsMoveRipRelative(disp_addr - kMoveRipRelativeDispOffset));
    // The displacement of the rip-relative move should be 0 before patching.
    DCHECK(ReadUnalignedValue<uint32_t>(disp_addr) == 0);
    WriteUnalignedValue(disp_addr, disp32);
  }
  Clear();
}

void Assembler::PatchConstPool() {
  // There is nothing to do if there are no pending entries.
  if (constpool_.IsEmpty()) {
    return;
  }
  constpool_.PatchEntries();
}

bool Assembler::UseConstPoolFor(RelocInfo::Mode rmode) {
  if (!v8_flags.partial_constant_pool) return false;
  return (rmode == RelocInfo::NO_INFO ||
          rmode == RelocInfo::EXTERNAL_REFERENCE ||
          rmode == RelocInfo::OFF_HEAP_TARGET);
}

// -----------------------------------------------------------------------------
// Implementation of Assembler.

Assembler::Assembler(const AssemblerOptions& options,
                     std::unique_ptr<AssemblerBuffer> buffer)
    : AssemblerBase(options, std::move(buffer)), constpool_(this) {
  reloc_info_writer.Reposition(buffer_start_ + buffer_->size(), pc_);
  if (CpuFeatures::IsSupported(SSE4_2)) {
    EnableCpuFeature(SSE4_1);
  }
  if (CpuFeatures::IsSupported(SSE4_1)) {
    EnableCpuFeature(SSSE3);
  }
  if (CpuFeatures::IsSupported(SSSE3)) {
    EnableCpuFeature(SSE3);
  }

#if defined(V8_OS_WIN_X64)
  if (options.collect_win64_unwind_info) {
    xdata_encoder_ = std::make_unique<win64_unwindinfo::XdataEncoder>(*this);
  }
#endif
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

  PatchConstPool();
  DCHECK(constpool_.IsEmpty());

  const int code_comments_size = WriteCodeComments();

  // At this point overflow() may be true, but the gap ensures
  // that we are still not overlapping instructions and relocation info.
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

void Assembler::FinalizeJumpOptimizationInfo() {
  // Collection stage
  auto jump_opt = jump_optimization_info();
  if (jump_opt && jump_opt->is_collecting()) {
    auto& dict = jump_opt->may_optimizable_farjmp;
    int num = static_cast<int>(jump_opt->farjmps.size());
    if (num && dict.empty()) {
      bool can_opt = false;
      for (int i = 0; i < num; i++) {
        auto jmp_info = jump_opt->farjmps[i];
        int disp = long_at(jmp_info.pos + jmp_info.opcode_size);
        if (is_int8(disp)) {
          jmp_info.distance = disp;
          dict[i] = jmp_info;
          can_opt = true;
        }
      }
      if (can_opt) {
        jump_opt->set_optimizable();
      }
    }
  }
}

#if defined(V8_OS_WIN_X64)
win64_unwindinfo::BuiltinUnwindInfo Assembler::GetUnwindInfo() const {
  DCHECK(options().collect_win64_unwind_info);
  DCHECK_NOT_NULL(xdata_encoder_);
  return xdata_encoder_->unwinding_info();
}
#endif

void Assembler::Align(int m) {
  DCHECK(base::bits::IsPowerOfTwo(m));
  int delta = (m - (pc_offset() & (m - 1))) & (m - 1);
  Nop(delta);
}

void Assembler::AlignForJCCErratum(int inst_size) {
  DCHECK(CpuFeatures::IsSupported(INTEL_JCC_ERRATUM_MITIGATION));
  // Code alignment can break jump optimization info, so we early return in this
  // case. This is because jump optimization will do the code generation twice:
  // the first run collects the optimizable far jumps and the second run
  // replaces them by near jumps. For example, if aaa is a far jump and bbb is
  // another instruction at the jump target, aaa will be recorded in
  // |jump_optimization_info|:
  //
  // ...aaa...bbb
  //       ^  ^
  //       |  jump target (start of a 32-byte boundary)
  //       |  pc_offset + 127
  //       pc_offset
  //
  // However, if bbb need to be aligned at the start of a 32-byte boundary,
  // the second run might crash because the distance is no longer a int8:
  //
  //   aaa......bbb
  //      ^     ^
  //      |     jump target (start of a 32-byte boundary)
  //      |     pc_offset + 127
  //      pc_offset - delta
  if (jump_optimization_info()) return;
  constexpr int kJCCErratumAlignment = 32;
  int delta = kJCCErratumAlignment - (pc_offset() & (kJCCErratumAlignment - 1));
  if (delta <= inst_size) Nop(delta);
}

void Assembler::CodeTargetAlign() {
  Align(16);  // Preferred alignment of jump targets on x64.
  auto jump_opt = jump_optimization_info();
  if (jump_opt && jump_opt->is_collecting()) {
    jump_opt->align_pos_size[pc_offset()] = 16;
  }
}

void Assembler::LoopHeaderAlign() {
  Align(64);  // Preferred alignment of loop header on x64.
  auto jump_opt = jump_optimization_info();
  if (jump_opt && jump_opt->is_collecting()) {
    jump_opt->align_pos_size[pc_offset()] = 64;
  }
}

bool Assembler::IsNop(Address addr) {
  uint8_t* a = reinterpret_cast<uint8_t*>(addr);
  while (*a == 0x66) a++;
  if (*a == 0x90) return true;
  if (a[0] == 0xF && a[1] == 0x1F) return true;
  return false;
}

void Assembler::bind_to(Label* L, int pos) {
  DCHECK(!L->is_bound());                  // Label may only be bound once.
  DCHECK(0 <= pos && pos <= pc_offset());  // Position must be valid.
  if (L->is_linked()) {
    int current = L->pos();
    int next = long_at(current);
    while (next != current) {
      if (current >= 4 && long_at(current - 4) == 0) {
        // Absolute address.
        intptr_t imm64 = reinterpret_cast<intptr_t>(buffer_start_ + pos);
        WriteUnalignedValue(addr_at(current - 4), imm64);
        internal_reference_positions_.push_back(current - 4);
      } else {
        // Relative address, relative to point after address.
        int imm32 = pos - (current + sizeof(int32_t));
        long_at_put(current, imm32);
      }
      current = next;
      next = long_at(next);
    }
    // Fix up last fixup on linked list.
    if (current >= 4 && long_at(current - 4) == 0) {
      // Absolute address.
      intptr_t imm64 = reinterpret_cast<intptr_t>(buffer_start_ + pos);
      WriteUnalignedValue(addr_at(current - 4), imm64);
      internal_reference_positions_.push_back(current - 4);
    } else {
      // Relative address, relative to point after address.
      int imm32 = pos - (current + sizeof(int32_t));
      long_at_put(current, imm32);
    }
  }
  while (L->is_near_linked()) {
    int fixup_pos = L->near_link_pos();
    int offset_to_next =
        static_cast<int>(*reinterpret_cast<int8_t*>(addr_at(fixup_pos)));
    DCHECK_LE(offset_to_next, 0);
    int disp = pos - (fixup_pos + sizeof(int8_t));
    CHECK(is_int8(disp));
    set_byte_at(fixup_pos, disp);
    if (offset_to_next < 0) {
      L->link_to(fixup_pos + offset_to_next, Label::kNear);
    } else {
      L->UnuseNear();
    }
  }

  // Optimization stage
  auto jump_opt = jump_optimization_info();
  if (jump_opt && jump_opt->is_optimizing()) {
    auto it = jump_opt->label_farjmp_maps.find(L);
    if (it != jump_opt->label_farjmp_maps.end()) {
      auto& pos_vector = it->second;
      for (auto fixup_pos : pos_vector) {
        int disp = pos - (fixup_pos + sizeof(int8_t));
        CHECK(is_int8(disp));
        set_byte_at(fixup_pos, disp);
      }
      jump_opt->label_farjmp_maps.erase(it);
    }
  }
  L->bind_to(pos);
}

void Assembler::bind(Label* L) { bind_to(L, pc_offset()); }

void Assembler::record_farjmp_position(Label* L, int pos) {
  auto& pos_vector = jump_optimization_info()->label_farjmp_maps[L];
  pos_vector.push_back(pos);
}

bool Assembler::is_optimizable_farjmp(int idx) {
  if (predictable_code_size()) return false;

  auto jump_opt = jump_optimization_info();
  CHECK(jump_opt->is_optimizing());

  auto& dict = jump_opt->may_optimizable_farjmp;
  if (dict.find(idx) != dict.end()) {
    auto record_jmp_info = dict[idx];

    int record_pos = record_jmp_info.pos;

    // 4 bytes for jmp rel32 operand.
    const int operand_size = 4;
    int record_dest = record_jmp_info.pos + record_jmp_info.opcode_size +
                      operand_size + record_jmp_info.distance;

    const int max_align_in_jmp_range =
        jump_opt->MaxAlignInRange(record_pos, record_dest);

    if (max_align_in_jmp_range == 0) {
      return true;
    }

    // ja rel32 -> ja rel8, the opcode size 2bytes -> 1byte
    // 0F 87 -> 77
    const int saved_opcode_size = record_jmp_info.opcode_size - 1;

    // jmp rel32 -> rel8, the operand size 4bytes -> 1byte
    constexpr int saved_operand_size = 4 - 1;

    // The shorter encoding may further decrease the base address of the
    // relative jump, while the jump target could stay in place because of
    // alignment.
    int cur_jmp_length_max_increase =
        (record_pos - pc_offset() + saved_opcode_size + saved_operand_size) %
        max_align_in_jmp_range;

    if (is_int8(record_jmp_info.distance + cur_jmp_length_max_increase)) {
      return true;
    }
  }
  return false;
}

void Assembler::GrowBuffer() {
  DCHECK(buffer_overflow());

  // Compute new buffer size.
  DCHECK_EQ(buffer_start_, buffer_->start());
  int old_size = buffer_->size();
  int new_size = 2 * old_size;

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
  MemMove(rc_delta + reloc_info_writer.pos(), reloc_info_writer.pos(),
          reloc_size);

  // Switch buffers.
  buffer_ = std::move(new_buffer);
  buffer_start_ = new_start;
  pc_ += pc_delta;
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // Relocate internal references.
  for (auto pos : internal_reference_positions_) {
    Address p = reinterpret_cast<Address>(buffer_start_ + pos);
    WriteUnalignedValue(p, ReadUnalignedValue<intptr_t>(p) + pc_delta);
  }

  DCHECK(!buffer_overflow());
}

void Assembler::emit_operand(int code, Operand adr) {
  // Redirect to {emit_label_operand} if {adr} contains a label.
  if (adr.is_label_operand()) {
    emit_label_operand(code, adr.label().label, adr.label().addend);
    return;
  }

  const size_t length = adr.memory().len;
  V8_ASSUME(1 <= length && length <= 6);

  // Compute the opcode extension to be encoded in the ModR/M byte.
  V8_ASSUME(0 <= code && code <= 7);
  DCHECK_EQ((adr.memory().buf[0] & 0x38), 0);
  uint8_t opcode_extension = code << 3;

  // Use an optimized routine for copying the 1-6 bytes into the assembler
  // buffer. We execute up to two read and write instructions, while also
  // minimizing the number of branches.
  Address src = reinterpret_cast<Address>(adr.memory().buf);
  Address dst = reinterpret_cast<Address>(pc_);
  if (length > 4) {
    // Length is 5 or 6.
    // Copy range [0, 3] and [len-2, len-1] (might overlap).
    uint32_t lower_four_bytes = ReadUnalignedValue<uint32_t>(src);
    lower_four_bytes |= opcode_extension;
    uint16_t upper_two_bytes = ReadUnalignedValue<uint16_t>(src + length - 2);
    WriteUnalignedValue<uint16_t>(dst + length - 2, upper_two_bytes);
    WriteUnalignedValue<uint32_t>(dst, lower_four_bytes);
  } else {
    // Length is in [1, 3].
    uint8_t first_byte = ReadUnalignedValue<uint8_t>(src);
    first_byte |= opcode_extension;
    if (length != 1) {
      // Copy bytes [len-2, len-1].
      uint16_t upper_two_bytes = ReadUnalignedValue<uint16_t>(src + length - 2);
      WriteUnalignedValue<uint16_t>(dst + length - 2, upper_two_bytes);
    }
    WriteUnalignedValue<uint8_t>(dst, first_byte);
  }

  pc_ += length;
}

void Assembler::emit_label_operand(int code, Label* label, int addend) {
  DCHECK(addend == 0 || (is_int8(addend) && label->is_bound()));
  V8_ASSUME(0 <= code && code <= 7);

  *pc_++ = 5 | (code << 3);
  if (label->is_bound()) {
    int offset = label->pos() - pc_offset() - sizeof(int32_t) + addend;
    DCHECK_GE(0, offset);
    emitl(offset);
  } else if (label->is_linked()) {
    emitl(label->pos());
    label->link_to(pc_offset() - sizeof(int32_t));
  } else {
    DCHECK(label->is_unused());
    int32_t current = pc_offset();
    emitl(current);
    label->link_to(current);
  }
}

// Assembler Instruction implementations.

void Assembler::arithmetic_op(uint8_t opcode, Register reg, Operand op,
                              int size) {
  EnsureSpace ensure_space(this);
  emit_rex(reg, op, size);
  emit(opcode);
  emit_operand(reg, op);
}

void Assembler::arithmetic_op(uint8_t opcode, Register reg, Register rm_reg,
                              int size) {
  EnsureSpace ensure_space(this);
  DCHECK_EQ(opcode & 0xC6, 2);
  if (rm_reg.low_bits() == 4) {  // Forces SIB byte.
    // Swap reg and rm_reg and change opcode operand order.
    emit_rex(rm_reg, reg, size);
    emit(opcode ^ 0x02);
    emit_modrm(rm_reg, reg);
  } else {
    emit_rex(reg, rm_reg, size);
    emit(opcode);
    emit_modrm(reg, rm_reg);
  }
}

void Assembler::arithmetic_op_16(uint8_t opcode, Register reg,
                                 Register rm_reg) {
  EnsureSpace ensure_space(this);
  DCHECK_EQ(opcode & 0xC6, 2);
  if (rm_reg.low_bits() == 4) {  // Forces SIB byte.
    // Swap reg and rm_reg and change opcode operand order.
    emit(0x66);
    emit_optional_rex_32(rm_reg, reg);
    emit(opcode ^ 0x02);
    emit_modrm(rm_reg, reg);
  } else {
    emit(0x66);
    emit_optional_rex_32(reg, rm_reg);
    emit(opcode);
    emit_modrm(reg, rm_reg);
  }
}

void Assembler::arithmetic_op_16(uint8_t opcode, Register reg, Operand rm_reg) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(reg, rm_reg);
  emit(opcode);
  emit_operand(reg, rm_reg);
}

void Assembler::arithmetic_op_8(uint8_t opcode, Register reg, Operand op) {
  EnsureSpace ensure_space(this);
  if (!reg.is_byte_register()) {
    emit_rex_32(reg, op);
  } else {
    emit_optional_rex_32(reg, op);
  }
  emit(opcode);
  emit_operand(reg, op);
}

void Assembler::arithmetic_op_8(uint8_t opcode, Register reg, Register rm_reg) {
  EnsureSpace ensure_space(this);
  DCHECK_EQ(opcode & 0xC6, 2);
  if (rm_reg.low_bits() == 4) {  // Forces SIB byte.
    // Swap reg and rm_reg and change opcode operand order.
    if (!rm_reg.is_byte_register() || !reg.is_byte_register()) {
      // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
      emit_rex_32(rm_reg, reg);
    }
    emit(opcode ^ 0x02);
    emit_modrm(rm_reg, reg);
  } else {
    if (!reg.is_byte_register() || !rm_reg.is_byte_register()) {
      // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
      emit_rex_32(reg, rm_reg);
    }
    emit(opcode);
    emit_modrm(reg, rm_reg);
  }
}

void Assembler::immediate_arithmetic_op(uint8_t subcode, Register dst,
                                        Immediate src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  if (is_int8(src.value_) && RelocInfo::IsNoInfo(src.rmode_)) {
    emit(0x83);
    emit_modrm(subcode, dst);
    emit(src.value_);
  } else if (dst == rax) {
    emit(0x05 | (subcode << 3));
    emit(src);
  } else {
    emit(0x81);
    emit_modrm(subcode, dst);
    emit(src);
  }
}

void Assembler::immediate_arithmetic_op(uint8_t subcode, Operand dst,
                                        Immediate src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  if (is_int8(src.value_) && RelocInfo::IsNoInfo(src.rmode_)) {
    emit(0x83);
    emit_operand(subcode, dst);
    emit(src.value_);
  } else {
    emit(0x81);
    emit_operand(subcode, dst);
    emit(src);
  }
}

void Assembler::immediate_arithmetic_op_16(uint8_t subcode, Register dst,
                                           Immediate src) {
  EnsureSpace ensure_space(this);
  emit(0x66);  // Operand size override prefix.
  emit_optional_rex_32(dst);
  if (is_int8(src.value_)) {
    emit(0x83);
    emit_modrm(subcode, dst);
    emit(src.value_);
  } else if (dst == rax) {
    emit(0x05 | (subcode << 3));
    emitw(src.value_);
  } else {
    emit(0x81);
    emit_modrm(subcode, dst);
    emitw(src.value_);
  }
}

void Assembler::immediate_arithmetic_op_16(uint8_t subcode, Operand dst,
                                           Immediate src) {
  EnsureSpace ensure_space(this);
  emit(0x66);  // Operand size override prefix.
  emit_optional_rex_32(dst);
  if (is_int8(src.value_)) {
    emit(0x83);
    emit_operand(subcode, dst);
    emit(src.value_);
  } else {
    emit(0x81);
    emit_operand(subcode, dst);
    emitw(src.value_);
  }
}

void Assembler::immediate_arithmetic_op_8(uint8_t subcode, Operand dst,
                                          Immediate src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst);
  DCHECK(is_int8(src.value_) || is_uint8(src.value_));
  emit(0x80);
  emit_operand(subcode, dst);
  emit(src.value_);
}

void Assembler::immediate_arithmetic_op_8(uint8_t subcode, Register dst,
                                          Immediate src) {
  EnsureSpace ensure_space(this);
  if (!dst.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(dst);
  }
  DCHECK(is_int8(src.value_) || is_uint8(src.value_));
  if (dst == rax) {
    emit(0x04 | (subcode << 3));
    emit(src.value_);
  } else {
    emit(0x80);
    emit_modrm(subcode, dst);
    emit(src.value_);
  }
}

void Assembler::shift(Register dst, Immediate shift_amount, int subcode,
                      int size) {
  EnsureSpace ensure_space(this);
  DCHECK(size == kInt64Size ? is_uint6(shift_amount.value_)
                            : is_uint5(shift_amount.value_));
  if (shift_amount.value_ == 1) {
    emit_rex(dst, size);
    emit(0xD1);
    emit_modrm(subcode, dst);
  } else {
    emit_rex(dst, size);
    emit(0xC1);
    emit_modrm(subcode, dst);
    emit(shift_amount.value_);
  }
}

void Assembler::shift(Operand dst, Immediate shift_amount, int subcode,
                      int size) {
  EnsureSpace ensure_space(this);
  DCHECK(size == kInt64Size ? is_uint6(shift_amount.value_)
                            : is_uint5(shift_amount.value_));
  if (shift_amount.value_ == 1) {
    emit_rex(dst, size);
    emit(0xD1);
    emit_operand(subcode, dst);
  } else {
    emit_rex(dst, size);
    emit(0xC1);
    emit_operand(subcode, dst);
    emit(shift_amount.value_);
  }
}

void Assembler::shift(Register dst, int subcode, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xD3);
  emit_modrm(subcode, dst);
}

void Assembler::shift(Operand dst, int subcode, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xD3);
  emit_operand(subcode, dst);
}

void Assembler::bswapl(Register dst) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst);
  emit(0x0F);
  emit(0xC8 + dst.low_bits());
}

void Assembler::bswapq(Register dst) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst);
  emit(0x0F);
  emit(0xC8 + dst.low_bits());
}

void Assembler::btq(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0xA3);
  emit_operand(src, dst);
}

void Assembler::btsq(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0xAB);
  emit_operand(src, dst);
}

void Assembler::btsq(Register dst, Immediate imm8) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst);
  emit(0x0F);
  emit(0xBA);
  emit_modrm(0x5, dst);
  emit(imm8.value_);
}

void Assembler::btrq(Register dst, Immediate imm8) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst);
  emit(0x0F);
  emit(0xBA);
  emit_modrm(0x6, dst);
  emit(imm8.value_);
}

void Assembler::bsrl(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBD);
  emit_modrm(dst, src);
}

void Assembler::bsrl(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBD);
  emit_operand(dst, src);
}

void Assembler::bsrq(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBD);
  emit_modrm(dst, src);
}

void Assembler::bsrq(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBD);
  emit_operand(dst, src);
}

void Assembler::bsfl(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBC);
  emit_modrm(dst, src);
}

void Assembler::bsfl(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBC);
  emit_operand(dst, src);
}

void Assembler::bsfq(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBC);
  emit_modrm(dst, src);
}

void Assembler::bsfq(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBC);
  emit_operand(dst, src);
}

void Assembler::pblendw(XMMRegister dst, Operand src, uint8_t mask) {
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x0E);
  emit(mask);
}

void Assembler::pblendw(XMMRegister dst, XMMRegister src, uint8_t mask) {
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x0E);
  emit(mask);
}

void Assembler::palignr(XMMRegister dst, Operand src, uint8_t mask) {
  ssse3_instr(dst, src, 0x66, 0x0F, 0x3A, 0x0F);
  emit(mask);
}

void Assembler::palignr(XMMRegister dst, XMMRegister src, uint8_t mask) {
  ssse3_instr(dst, src, 0x66, 0x0F, 0x3A, 0x0F);
  emit(mask);
}

void Assembler::call(Label* L) {
  EnsureSpace ensure_space(this);
  // 1110 1000 #32-bit disp.
  emit(0xE8);
  if (L->is_bound()) {
    int offset = L->pos() - pc_offset() - sizeof(int32_t);
    DCHECK_LE(offset, 0);
    emitl(offset);
  } else if (L->is_linked()) {
    emitl(L->pos());
    L->link_to(pc_offset() - sizeof(int32_t));
  } else {
    DCHECK(L->is_unused());
    int32_t current = pc_offset();
    emitl(current);
    L->link_to(current);
  }
}

void Assembler::call(Handle<Code> target, RelocInfo::Mode rmode) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  EnsureSpace ensure_space(this);
  // 1110 1000 #32-bit disp.
  emit(0xE8);
  RecordRelocInfo(rmode);
  int code_target_index = AddCodeTarget(target);
  emitl(code_target_index);
}

void Assembler::near_call(intptr_t disp, RelocInfo::Mode rmode) {
  EnsureSpace ensure_space(this);
  // 1110 1000 #32-bit disp.
  emit(0xE8);
  DCHECK(is_int32(disp));
  RecordRelocInfo(rmode);
  emitl(static_cast<int32_t>(disp));
}

void Assembler::near_jmp(intptr_t disp, RelocInfo::Mode rmode) {
  EnsureSpace ensure_space(this);
  // 1110 1001 #32-bit disp.
  emit(0xE9);
  DCHECK(is_int32(disp));
  if (!RelocInfo::IsNoInfo(rmode)) RecordRelocInfo(rmode);
  emitl(static_cast<int32_t>(disp));
}

void Assembler::near_j(Condition cc, intptr_t disp, RelocInfo::Mode rmode) {
  EnsureSpace ensure_space(this);
  // 0000 1111 1000 tttn #32-bit disp.
  emit(0x0F);
  emit(0x80 | cc);
  DCHECK(is_int32(disp));
  if (!RelocInfo::IsNoInfo(rmode)) RecordRelocInfo(rmode);
  emitl(static_cast<int32_t>(disp));
}

void Assembler::call(Register adr) {
  EnsureSpace ensure_space(this);
  // Opcode: FF /2 r64.
  emit_optional_rex_32(adr);
  emit(0xFF);
  emit_modrm(0x2, adr);
}

void Assembler::call(Operand op) {
  EnsureSpace ensure_space(this);
  // Opcode: FF /2 m64.
  emit_optional_rex_32(op);
  emit(0xFF);
  emit_operand(0x2, op);
}

void Assembler::clc() {
  EnsureSpace ensure_space(this);
  emit(0xF8);
}

void Assembler::cld() {
  EnsureSpace ensure_space(this);
  emit(0xFC);
}

void Assembler::cdq() {
  EnsureSpace ensure_space(this);
  emit(0x99);
}

void Assembler::cmovq(Condition cc, Register dst, Register src) {
  EnsureSpace ensure_space(this);
  // Opcode: REX.W 0f 40 + cc /r.
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x40 + cc);
  emit_modrm(dst, src);
}

void Assembler::cmovq(Condition cc, Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  // Opcode: REX.W 0f 40 + cc /r.
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x40 + cc);
  emit_operand(dst, src);
}

void Assembler::cmovl(Condition cc, Register dst, Register src) {
  EnsureSpace ensure_space(this);
  // Opcode: 0f 40 + cc /r.
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x40 + cc);
  emit_modrm(dst, src);
}

void Assembler::cmovl(Condition cc, Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  // Opcode: 0f 40 + cc /r.
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x40 + cc);
  emit_operand(dst, src);
}

void Assembler::cmpb_al(Immediate imm8) {
  DCHECK(is_int8(imm8.value_) || is_uint8(imm8.value_));
  EnsureSpace ensure_space(this);
  emit(0x3C);
  emit(imm8.value_);
}

void Assembler::lock() {
  EnsureSpace ensure_space(this);
  emit(0xF0);
}

void Assembler::xaddb(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_8(src, dst);
  emit(0x0F);
  emit(0xC0);
  emit_operand(src, dst);
}

void Assembler::xaddw(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(src, dst);
  emit(0x0F);
  emit(0xC1);
  emit_operand(src, dst);
}

void Assembler::xaddl(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(src, dst);
  emit(0x0F);
  emit(0xC1);
  emit_operand(src, dst);
}

void Assembler::xaddq(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex(src, dst, kInt64Size);
  emit(0x0F);
  emit(0xC1);
  emit_operand(src, dst);
}

void Assembler::cmpxchgb(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  if (!src.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(src, dst);
  } else {
    emit_optional_rex_32(src, dst);
  }
  emit(0x0F);
  emit(0xB0);
  emit_operand(src, dst);
}

void Assembler::cmpxchgw(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(src, dst);
  emit(0x0F);
  emit(0xB1);
  emit_operand(src, dst);
}

void Assembler::emit_cmpxchg(Operand dst, Register src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(src, dst, size);
  emit(0x0F);
  emit(0xB1);
  emit_operand(src, dst);
}

void Assembler::mfence() {
  EnsureSpace ensure_space(this);
  emit(0x0F);
  emit(0xAE);
  emit(0xF0);
}

void Assembler::lfence() {
  EnsureSpace ensure_space(this);
  emit(0x0F);
  emit(0xAE);
  emit(0xE8);
}

void Assembler::cpuid() {
  EnsureSpace ensure_space(this);
  emit(0x0F);
  emit(0xA2);
}

void Assembler::cqo() {
  EnsureSpace ensure_space(this);
  emit_rex_64();
  emit(0x99);
}

void Assembler::emit_dec(Register dst, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xFF);
  emit_modrm(0x1, dst);
}

void Assembler::emit_dec(Operand dst, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xFF);
  emit_operand(1, dst);
}

void Assembler::decb(Register dst) {
  EnsureSpace ensure_space(this);
  if (!dst.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(dst);
  }
  emit(0xFE);
  emit_modrm(0x1, dst);
}

void Assembler::decb(Operand dst) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst);
  emit(0xFE);
  emit_operand(1, dst);
}

void Assembler::hlt() {
  EnsureSpace ensure_space(this);
  emit(0xF4);
}

void Assembler::endbr64() {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit(0x0f);
  emit(0x1e);
  emit(0xfa);
}

void Assembler::emit_idiv(Register src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(src, size);
  emit(0xF7);
  emit_modrm(0x7, src);
}

void Assembler::emit_div(Register src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(src, size);
  emit(0xF7);
  emit_modrm(0x6, src);
}

void Assembler::emit_imul(Register src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(src, size);
  emit(0xF7);
  emit_modrm(0x5, src);
}

void Assembler::emit_imul(Operand src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(src, size);
  emit(0xF7);
  emit_operand(0x5, src);
}

void Assembler::emit_imul(Register dst, Register src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, src, size);
  emit(0x0F);
  emit(0xAF);
  emit_modrm(dst, src);
}

void Assembler::emit_imul(Register dst, Operand src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, src, size);
  emit(0x0F);
  emit(0xAF);
  emit_operand(dst, src);
}

void Assembler::emit_imul(Register dst, Register src, Immediate imm, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, src, size);
  if (is_int8(imm.value_)) {
    emit(0x6B);
    emit_modrm(dst, src);
    emit(imm.value_);
  } else {
    emit(0x69);
    emit_modrm(dst, src);
    emitl(imm.value_);
  }
}

void Assembler::emit_imul(Register dst, Operand src, Immediate imm, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, src, size);
  if (is_int8(imm.value_)) {
    emit(0x6B);
    emit_operand(dst, src);
    emit(imm.value_);
  } else {
    emit(0x69);
    emit_operand(dst, src);
    emitl(imm.value_);
  }
}

void Assembler::emit_inc(Register dst, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xFF);
  emit_modrm(0x0, dst);
}

void Assembler::emit_inc(Operand dst, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xFF);
  emit_operand(0, dst);
}

void Assembler::int3() {
  EnsureSpace ensure_space(this);
  emit(0xCC);
}

void Assembler::j(Condition cc, Label* L, Label::Distance distance) {
  EnsureSpace ensure_space(this);
  DCHECK(is_uint4(cc));
  if (L->is_bound()) {
    const int short_size = 2;
    const int long_size = 6;
    int offs = L->pos() - pc_offset();
    DCHECK_LE(offs, 0);
    // Determine whether we can use 1-byte offsets for backwards branches,
    // which have a max range of 128 bytes.

    // We also need to check predictable_code_size() flag here, because on x64,
    // when the full code generator recompiles code for debugging, some places
    // need to be padded out to a certain size. The debugger is keeping track of
    // how often it did this so that it can adjust return addresses on the
    // stack, but if the size of jump instructions can also change, that's not
    // enough and the calculated offsets would be incorrect.
    if (is_int8(offs - short_size) && !predictable_code_size()) {
      // 0111 tttn #8-bit disp.
      emit(0x70 | cc);
      emit((offs - short_size) & 0xFF);
    } else {
      // 0000 1111 1000 tttn #32-bit disp.
      emit(0x0F);
      emit(0x80 | cc);
      emitl(offs - long_size);
    }
  } else if (distance == Label::kNear) {
    // 0111 tttn #8-bit disp
    emit(0x70 | cc);
    uint8_t disp = 0x00;
    if (L->is_near_linked()) {
      int offset = L->near_link_pos() - pc_offset();
      DCHECK(is_int8(offset));
      disp = static_cast<uint8_t>(offset & 0xFF);
    }
    L->link_to(pc_offset(), Label::kNear);
    emit(disp);
  } else {
    auto jump_opt = jump_optimization_info();
    if (V8_UNLIKELY(jump_opt)) {
      if (jump_opt->is_optimizing() &&
          is_optimizable_farjmp(jump_opt->farjmp_num++)) {
        // 0111 tttn #8-bit disp
        emit(0x70 | cc);
        record_farjmp_position(L, pc_offset());
        emit(0);
        return;
      }
      if (jump_opt->is_collecting()) {
        jump_opt->farjmps.push_back({pc_offset(), 2, 0});
      }
    }
    if (L->is_linked()) {
      // 0000 1111 1000 tttn #32-bit disp.
      emit(0x0F);
      emit(0x80 | cc);
      emitl(L->pos());
      L->link_to(pc_offset() - sizeof(int32_t));
    } else {
      // If this fires a near label is reused for a far jump, missing an
      // optimization opportunity.
      DCHECK(!L->is_near_linked());
      DCHECK(L->is_unused());
      emit(0x0F);
      emit(0x80 | cc);
      int32_t current = pc_offset();
      emitl(current);
      L->link_to(current);
    }
  }
}

void Assembler::j(Condition cc, Address entry, RelocInfo::Mode rmode) {
  DCHECK(RelocInfo::IsWasmStubCall(rmode));
  EnsureSpace ensure_space(this);
  DCHECK(is_uint4(cc));
  emit(0x0F);
  emit(0x80 | cc);
  RecordRelocInfo(rmode);
  emitl(static_cast<int32_t>(entry));
}

void Assembler::j(Condition cc, Handle<Code> target, RelocInfo::Mode rmode) {
  EnsureSpace ensure_space(this);
  DCHECK(is_uint4(cc));
  // 0000 1111 1000 tttn #32-bit disp.
  emit(0x0F);
  emit(0x80 | cc);
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  RecordRelocInfo(rmode);
  int code_target_index = AddCodeTarget(target);
  emitl(code_target_index);
}

void Assembler::jmp_rel(int32_t offset) {
  EnsureSpace ensure_space(this);
  // The offset is encoded relative to the next instruction.
  constexpr int32_t kShortJmpDisplacement = 1 + sizeof(int8_t);
  constexpr int32_t kNearJmpDisplacement = 1 + sizeof(int32_t);
  DCHECK_LE(std::numeric_limits<int32_t>::min() + kNearJmpDisplacement, offset);
  if (is_int8(offset - kShortJmpDisplacement) && !predictable_code_size()) {
    // 0xEB #8-bit disp.
    emit(0xEB);
    emit(offset - kShortJmpDisplacement);
  } else {
    // 0xE9 #32-bit disp.
    emit(0xE9);
    emitl(offset - kNearJmpDisplacement);
  }
}

void Assembler::jmp(Label* L, Label::Distance distance) {
  const int long_size = sizeof(int32_t);

  if (L->is_bound()) {
    int offset = L->pos() - pc_offset();
    DCHECK_LE(offset, 0);  // backward jump.
    jmp_rel(offset);
    return;
  }

  EnsureSpace ensure_space(this);
  if (distance == Label::kNear) {
    emit(0xEB);
    uint8_t disp = 0x00;
    if (L->is_near_linked()) {
      int offset = L->near_link_pos() - pc_offset();
      DCHECK(is_int8(offset));
      disp = static_cast<uint8_t>(offset & 0xFF);
    }
    L->link_to(pc_offset(), Label::kNear);
    emit(disp);
  } else {
    auto jump_opt = jump_optimization_info();
    if (V8_UNLIKELY(jump_opt)) {
      if (jump_opt->is_optimizing() &&
          is_optimizable_farjmp(jump_opt->farjmp_num++)) {
        emit(0xEB);
        record_farjmp_position(L, pc_offset());
        emit(0);
        return;
      }
      if (jump_opt->is_collecting()) {
        jump_opt->farjmps.push_back({pc_offset(), 1, 0});
      }
    }
    if (L->is_linked()) {
      // 1110 1001 #32-bit disp.
      emit(0xE9);
      emitl(L->pos());
      L->link_to(pc_offset() - long_size);
    } else {
      // 1110 1001 #32-bit disp.
      // If this fires a near label is reused for a far jump, missing an
      // optimization opportunity.
      DCHECK(!L->is_near_linked());
      DCHECK(L->is_unused());
      emit(0xE9);
      int32_t current = pc_offset();
      emitl(current);
      L->link_to(current);
    }
  }
}

void Assembler::jmp(Handle<Code> target, RelocInfo::Mode rmode) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  EnsureSpace ensure_space(this);
  // 1110 1001 #32-bit disp.
  emit(0xE9);
  RecordRelocInfo(rmode);
  int code_target_index = AddCodeTarget(target);
  emitl(code_target_index);
}

#ifdef V8_ENABLE_CET_IBT

void Assembler::jmp(Register target, bool notrack) {
  EnsureSpace ensure_space(this);
  if (notrack) {
    emit(0x3e);
  }
  // Opcode FF/4 r64.
  emit_optional_rex_32(target);
  emit(0xFF);
  emit_modrm(0x4, target);
}

void Assembler::jmp(Operand src, bool notrack) {
  EnsureSpace ensure_space(this);
  if (notrack) {
    emit(0x3e);
  }
  // Opcode FF/4 m64.
  emit_optional_rex_32(src);
  emit(0xFF);
  emit_operand(0x4, src);
}

#else  // V8_ENABLE_CET_IBT

void Assembler::jmp(Register target) {
  EnsureSpace ensure_space(this);
  // Opcode FF/4 r64.
  emit_optional_rex_32(target);
  emit(0xFF);
  emit_modrm(0x4, target);
}

void Assembler::jmp(Operand src) {
  EnsureSpace ensure_space(this);
  // Opcode FF/4 m64.
  emit_optional_rex_32(src);
  emit(0xFF);
  emit_operand(0x4, src);
}

#endif

void Assembler::emit_lea(Register dst, Operand src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, src, size);
  emit(0x8D);
  emit_operand(dst, src);
}

void Assembler::load_rax(Address value, RelocInfo::Mode mode) {
  EnsureSpace ensure_space(this);
  emit(0x48);  // REX.W
  emit(0xA1);
  emit(Immediate64(value, mode));
}

void Assembler::load_rax(ExternalReference ref) {
  load_rax(ref.address(), RelocInfo::EXTERNAL_REFERENCE);
}

void Assembler::leave() {
  EnsureSpace ensure_space(this);
  emit(0xC9);
}

void Assembler::movb(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  if (!dst.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(dst, src);
  } else {
    emit_optional_rex_32(dst, src);
  }
  emit(0x8A);
  emit_operand(dst, src);
}

void Assembler::movb(Register dst, Immediate imm) {
  EnsureSpace ensure_space(this);
  if (!dst.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(dst);
  }
  emit(0xB0 + dst.low_bits());
  emit(imm.value_);
}

void Assembler::movb(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  if (!src.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(src, dst);
  } else {
    emit_optional_rex_32(src, dst);
  }
  emit(0x88);
  emit_operand(src, dst);
}

void Assembler::movb(Operand dst, Immediate imm) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst);
  emit(0xC6);
  emit_operand(0x0, dst);
  emit(static_cast<uint8_t>(imm.value_));
}

void Assembler::movw(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x8B);
  emit_operand(dst, src);
}

void Assembler::movw(Operand dst, Register src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(src, dst);
  emit(0x89);
  emit_operand(src, dst);
}

void Assembler::movw(Operand dst, Immediate imm) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst);
  emit(0xC7);
  emit_operand(0x0, dst);
  emit(static_cast<uint8_t>(imm.value_ & 0xFF));
  emit(static_cast<uint8_t>(imm.value_ >> 8));
}

void Assembler::emit_mov(Register dst, Operand src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, src, size);
  emit(0x8B);
  emit_operand(dst, src);
}

void Assembler::emit_mov(Register dst, Register src, int size) {
  EnsureSpace ensure_space(this);
  if (src.low_bits() == 4) {
    emit_rex(src, dst, size);
    emit(0x89);
    emit_modrm(src, dst);
  } else {
    emit_rex(dst, src, size);
    emit(0x8B);
    emit_modrm(dst, src);
  }

#if defined(V8_OS_WIN_X64)
  if (xdata_encoder_ && dst == rbp && src == rsp) {
    xdata_encoder_->onMovRbpRsp();
  }
#endif
}

void Assembler::emit_mov(Operand dst, Register src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(src, dst, size);
  emit(0x89);
  emit_operand(src, dst);
}

void Assembler::emit_mov(Register dst, Immediate value, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  if (size == kInt64Size) {
    emit(0xC7);
    emit_modrm(0x0, dst);
  } else {
    DCHECK_EQ(size, kInt32Size);
    emit(0xB8 + dst.low_bits());
  }
  emit(value);
}

void Assembler::emit_mov(Operand dst, Immediate value, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xC7);
  emit_operand(0x0, dst);
  emit(value);
}

void Assembler::emit_mov(Register dst, Immediate64 value, int size) {
  DCHECK_EQ(size, kInt64Size);
  if (constpool_.TryRecordEntry(value.value_, value.rmode_)) {
    // Emit rip-relative move with offset = 0
    Label label;
    emit_mov(dst, Operand(&label, 0), size);
    bind(&label);
  } else {
    EnsureSpace ensure_space(this);
    emit_rex(dst, size);
    emit(0xB8 | dst.low_bits());
    emit(value);
  }
}

void Assembler::movq_imm64(Register dst, int64_t value) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, kInt64Size);
  emit(0xB8 | dst.low_bits());
  emitq(static_cast<uint64_t>(value));
}

void Assembler::movq_heap_number(Register dst, double value) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, kInt64Size);
  emit(0xB8 | dst.low_bits());
  RequestHeapNumber(HeapNumberRequest(value));
  emit(Immediate64(kNullAddress, RelocInfo::FULL_EMBEDDED_OBJECT));
}

// Loads the ip-relative location of the src label into the target location
// (as a 32-bit offset sign extended to 64-bit).
void Assembler::movl(Operand dst, Label* src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst);
  emit(0xC7);
  emit_operand(0, dst);
  if (src->is_bound()) {
    int offset = src->pos() - pc_offset() - sizeof(int32_t);
    DCHECK_LE(offset, 0);
    emitl(offset);
  } else if (src->is_linked()) {
    emitl(src->pos());
    src->link_to(pc_offset() - sizeof(int32_t));
  } else {
    DCHECK(src->is_unused());
    int32_t current = pc_offset();
    emitl(current);
    src->link_to(current);
  }
}

void Assembler::movsxbl(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  if (!src.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(dst, src);
  } else {
    emit_optional_rex_32(dst, src);
  }
  emit(0x0F);
  emit(0xBE);
  emit_modrm(dst, src);
}

void Assembler::movsxbl(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBE);
  emit_operand(dst, src);
}

void Assembler::movsxbq(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBE);
  emit_operand(dst, src);
}

void Assembler::movsxbq(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBE);
  emit_modrm(dst, src);
}

void Assembler::movsxwl(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBF);
  emit_modrm(dst, src);
}

void Assembler::movsxwl(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBF);
  emit_operand(dst, src);
}

void Assembler::movsxwq(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBF);
  emit_operand(dst, src);
}

void Assembler::movsxwq(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBF);
  emit_modrm(dst, src);
}

void Assembler::movsxlq(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x63);
  emit_modrm(dst, src);
}

void Assembler::movsxlq(Register dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(dst, src);
  emit(0x63);
  emit_operand(dst, src);
}

void Assembler::emit_movzxb(Register dst, Operand src, int size) {
  EnsureSpace ensure_space(this);
  // 32 bit operations zero the top 32 bits of 64 bit registers.  Therefore
  // there is no need to make this a 64 bit operation.
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xB6);
  emit_operand(dst, src);
}

void Assembler::emit_movzxb(Register dst, Register src, int size) {
  EnsureSpace ensure_space(this);
  // 32 bit operations zero the top 32 bits of 64 bit registers.  Therefore
  // there is no need to make this a 64 bit operation.
  if (!src.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(dst, src);
  } else {
    emit_optional_rex_32(dst, src);
  }
  emit(0x0F);
  emit(0xB6);
  emit_modrm(dst, src);
}

void Assembler::emit_movzxw(Register dst, Operand src, int size) {
  EnsureSpace ensure_space(this);
  // 32 bit operations zero the top 32 bits of 64 bit registers.  Therefore
  // there is no need to make this a 64 bit operation.
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xB7);
  emit_operand(dst, src);
}

void Assembler::emit_movzxw(Register dst, Register src, int size) {
  EnsureSpace ensure_space(this);
  // 32 bit operations zero the top 32 bits of 64 bit registers.  Therefore
  // there is no need to make this a 64 bit operation.
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xB7);
  emit_modrm(dst, src);
}

void Assembler::repmovsb() {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit(0xA4);
}

void Assembler::repmovsw() {
  EnsureSpace ensure_space(this);
  emit(0x66);  // Operand size override.
  emit(0xF3);
  emit(0xA4);
}

void Assembler::emit_repmovs(int size) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex(size);
  emit(0xA5);
}

void Assembler::repstosl() {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit(0xAB);
}

void Assembler::repstosq() {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64();
  emit(0xAB);
}

void Assembler::mull(Register src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(src);
  emit(0xF7);
  emit_modrm(0x4, src);
}

void Assembler::mull(Operand src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(src);
  emit(0xF7);
  emit_operand(0x4, src);
}

void Assembler::mulq(Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(src);
  emit(0xF7);
  emit_modrm(0x4, src);
}

void Assembler::mulq(Operand src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(src);
  emit(0xF7);
  emit_operand(0x4, src);
}

void Assembler::negb(Register reg) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_8(reg);
  emit(0xF6);
  emit_modrm(0x3, reg);
}

void Assembler::negw(Register reg) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(reg);
  emit(0xF7);
  emit_modrm(0x3, reg);
}

void Assembler::negl(Register reg) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(reg);
  emit(0xF7);
  emit_modrm(0x3, reg);
}

void Assembler::negq(Register reg) {
  EnsureSpace ensure_space(this);
  emit_rex_64(reg);
  emit(0xF7);
  emit_modrm(0x3, reg);
}

void Assembler::negb(Operand op) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(op);
  emit(0xF6);
  emit_operand(0x3, op);
}

void Assembler::negw(Operand op) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(op);
  emit(0xF7);
  emit_operand(0x3, op);
}

void Assembler::negl(Operand op) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(op);
  emit(0xF7);
  emit_operand(0x3, op);
}

void Assembler::negq(Operand op) {
  EnsureSpace ensure_space(this);
  emit_rex_64(op);
  emit(0xF7);
  emit_operand(0x3, op);
}

void Assembler::nop() {
  EnsureSpace ensure_space(this);
  emit(0x90);
}

void Assembler::emit_not(Register dst, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xF7);
  emit_modrm(0x2, dst);
}

void Assembler::emit_not(Operand dst, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, size);
  emit(0xF7);
  emit_operand(2, dst);
}

void Assembler::Nop(int n) {
  DCHECK_LE(0, n);
  // The recommended muti-byte sequences of NOP instructions from the Intel 64
  // and IA-32 Architectures Software Developer's Manual.
  //
  // Len Assembly                                    Byte Sequence
  // 2   66 NOP                                      66 90H
  // 3   NOP DWORD ptr [EAX]                         0F 1F 00H
  // 4   NOP DWORD ptr [EAX + 00H]                   0F 1F 40 00H
  // 5   NOP DWORD ptr [EAX + EAX*1 + 00H]           0F 1F 44 00 00H
  // 6   66 NOP DWORD ptr [EAX + EAX*1 + 00H]        66 0F 1F 44 00 00H
  // 7   NOP DWORD ptr [EAX + 00000000H]             0F 1F 80 00 00 00 00H
  // 8   NOP DWORD ptr [EAX + EAX*1 + 00000000H]     0F 1F 84 00 00 00 00 00H
  // 9   66 NOP DWORD ptr [EAX + EAX*1 + 00000000H]  66 0F 1F 84 00 00 00 00 00H

  constexpr const char* kNopSequences =
      "\x66\x90"                               // length 1 (@1) / 2 (@0)
      "\x0F\x1F\x00"                           // length 3 (@2)
      "\x0F\x1F\x40\x00"                       // length 4 (@5)
      "\x66\x0F\x1F\x44\x00\x00"               // length 5 (@10) / 6 (@9)
      "\x0F\x1F\x80\x00\x00\x00\x00"           // length 7 (@15)
      "\x66\x0F\x1F\x84\x00\x00\x00\x00\x00";  // length 8 (@23) / 9 (@22)
  constexpr int8_t kNopOffsets[10] = {0, 1, 0, 2, 5, 10, 9, 15, 23, 22};

  do {
    EnsureSpace ensure_space(this);
    int nop_bytes = std::min(n, 9);
    const char* sequence = kNopSequences + kNopOffsets[nop_bytes];
    memcpy(pc_, sequence, nop_bytes);
    pc_ += nop_bytes;
    n -= nop_bytes;
  } while (n);
}

void Assembler::emit_trace_instruction(Immediate markid) {
  EnsureSpace ensure_space(this);
  if (v8_flags.wasm_trace_native != nullptr &&
      !strcmp(v8_flags.wasm_trace_native, "cpuid")) {
    // This is the optionally selected cpuid sequence which computes a magic
    // number based upon the markid. The low 16 bits of the magic number are
    // 0x4711 and the high 16 bits are the low 16 bits of the markid. This
    // magic number gets moved into the eax register.
    uint32_t magic_num = 0x4711 | (static_cast<uint32_t>(markid.value_) << 16);

    pushq(rax);
    pushq(rbx);
    pushq(rcx);
    pushq(rdx);
    movl(rax, Immediate(magic_num));
    cpuid();
    popq(rdx);
    popq(rcx);
    popq(rbx);
    popq(rax);
  } else {
    // This is the default triple-nop sequence, an sscmark. The markid is moved
    // into the ebx register and then the triple-nop sequence is executed. The
    // three nops are prefixed by prefix.64 and prefix.67. The entire sequence
    // becomes "prefix.64 prefix.67 nop nop nop".
    pushq(rbx);
    movl(rbx, markid);
    emit(0x64);
    emit(0x67);
    nop();
    nop();
    nop();
    popq(rbx);
  }
}

void Assembler::popq(Register dst) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst);
  emit(0x58 | dst.low_bits());
}

void Assembler::popq(Operand dst) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst);
  emit(0x8F);
  emit_operand(0, dst);
}

void Assembler::popfq() {
  EnsureSpace ensure_space(this);
  emit(0x9D);
}

void Assembler::pushq(Register src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(src);
  emit(0x50 | src.low_bits());

#if defined(V8_OS_WIN_X64)
  if (xdata_encoder_ && src == rbp) {
    xdata_encoder_->onPushRbp();
  }
#endif
}

void Assembler::pushq(Operand src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(src);
  emit(0xFF);
  emit_operand(6, src);
}

void Assembler::pushq(Immediate value) {
  EnsureSpace ensure_space(this);
  if (is_int8(value.value_)) {
    emit(0x6A);
    emit(value.value_);  // Emit low byte of value.
  } else {
    emit(0x68);
    emitl(value.value_);
  }
}

void Assembler::pushq_imm32(int32_t imm32) {
  EnsureSpace ensure_space(this);
  emit(0x68);
  emitl(imm32);
}

void Assembler::pushfq() {
  EnsureSpace ensure_space(this);
  emit(0x9C);
}

void Assembler::incsspq(Register number_of_words) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(number_of_words);
  emit(0x0F);
  emit(0xAE);
  emit(0xE8 | number_of_words.low_bits());
}

void Assembler::ret(int imm16) {
  EnsureSpace ensure_space(this);
  DCHECK(is_uint16(imm16));
  if (imm16 == 0) {
    emit(0xC3);
  } else {
    emit(0xC2);
    emit(imm16 & 0xFF);
    emit((imm16 >> 8) & 0xFF);
  }
}

void Assembler::ud2() {
  EnsureSpace ensure_space(this);
  emit(0x0F);
  emit(0x0B);
}

void Assembler::setcc(Condition cc, Register reg) {
  EnsureSpace ensure_space(this);
  DCHECK(is_uint4(cc));
  if (!reg.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(reg);
  }
  emit(0x0F);
  emit(0x90 | cc);
  emit_modrm(0x0, reg);
}

void Assembler::shld(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0xA5);
  emit_modrm(src, dst);
}

void Assembler::shrd(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0xAD);
  emit_modrm(src, dst);
}

void Assembler::xchgb(Register reg, Operand op) {
  EnsureSpace ensure_space(this);
  if (!reg.is_byte_register()) {
    // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
    emit_rex_32(reg, op);
  } else {
    emit_optional_rex_32(reg, op);
  }
  emit(0x86);
  emit_operand(reg, op);
}

void Assembler::xchgw(Register reg, Operand op) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(reg, op);
  emit(0x87);
  emit_operand(reg, op);
}

void Assembler::emit_xchg(Register dst, Register src, int size) {
  EnsureSpace ensure_space(this);
  if (src == rax || dst == rax) {  // Single-byte encoding
    Register other = src == rax ? dst : src;
    emit_rex(other, size);
    emit(0x90 | other.low_bits());
  } else if (dst.low_bits() == 4) {
    emit_rex(dst, src, size);
    emit(0x87);
    emit_modrm(dst, src);
  } else {
    emit_rex(src, dst, size);
    emit(0x87);
    emit_modrm(src, dst);
  }
}

void Assembler::emit_xchg(Register dst, Operand src, int size) {
  EnsureSpace ensure_space(this);
  emit_rex(dst, src, size);
  emit(0x87);
  emit_operand(dst, src);
}

void Assembler::store_rax(Address dst, RelocInfo::Mode mode) {
  EnsureSpace ensure_space(this);
  emit(0x48);  // REX.W
  emit(0xA3);
  emit(Immediate64(dst, mode));
}

void Assembler::store_rax(ExternalReference ref) {
  store_rax(ref.address(), RelocInfo::EXTERNAL_REFERENCE);
}

void Assembler::sub_sp_32(uint32_t imm) {
  emit_rex_64();
  emit(0x81);  // using a literal 32-bit immediate.
  emit_modrm(0x5, rsp);
  emitl(imm);
}

void Assembler::testb(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  emit_test(dst, src, sizeof(int8_t));
}

void Assembler::testb(Register reg, Immediate mask) {
  DCHECK(is_int8(mask.value_) || is_uint8(mask.value_));
  emit_test(reg, mask, sizeof(int8_t));
}

void Assembler::testb(Operand op, Immediate mask) {
  DCHECK(is_int8(mask.value_) || is_uint8(mask.value_));
  emit_test(op, mask, sizeof(int8_t));
}

void Assembler::testb(Operand op, Register reg) {
  emit_test(op, reg, sizeof(int8_t));
}

void Assembler::testw(Register dst, Register src) {
  emit_test(dst, src, sizeof(uint16_t));
}

void Assembler::testw(Register reg, Immediate mask) {
  emit_test(reg, mask, sizeof(int16_t));
}

void Assembler::testw(Operand op, Immediate mask) {
  emit_test(op, mask, sizeof(int16_t));
}

void Assembler::testw(Operand op, Register reg) {
  emit_test(op, reg, sizeof(int16_t));
}

void Assembler::emit_test(Register dst, Register src, int size) {
  EnsureSpace ensure_space(this);
  if (src.low_bits() == 4) std::swap(dst, src);
  if (size == sizeof(int16_t)) {
    emit(0x66);
    size = sizeof(int32_t);
  }
  bool byte_operand = size == sizeof(int8_t);
  if (byte_operand) {
    size = sizeof(int32_t);
    if (!src.is_byte_register() || !dst.is_byte_register()) {
      emit_rex_32(dst, src);
    }
  } else {
    emit_rex(dst, src, size);
  }
  emit(byte_operand ? 0x84 : 0x85);
  emit_modrm(dst, src);
}

void Assembler::emit_test(Register reg, Immediate mask, int size) {
  if (is_uint8(mask.value_)) {
    size = sizeof(int8_t);
  } else if (is_uint16(mask.value_)) {
    size = sizeof(int16_t);
  }
  EnsureSpace ensure_space(this);
  bool half_word = size == sizeof(int16_t);
  if (half_word) {
    emit(0x66);
    size = sizeof(int32_t);
  }
  bool byte_operand = size == sizeof(int8_t);
  if (byte_operand) {
    size = sizeof(int32_t);
    if (!reg.is_byte_register()) emit_rex_32(reg);
  } else {
    emit_rex(reg, size);
  }
  if (reg == rax) {
    emit(byte_operand ? 0xA8 : 0xA9);
  } else {
    emit(byte_operand ? 0xF6 : 0xF7);
    emit_modrm(0x0, reg);
  }
  if (byte_operand) {
    emit(mask.value_);
  } else if (half_word) {
    emitw(mask.value_);
  } else {
    emit(mask);
  }
}

void Assembler::emit_test(Operand op, Immediate mask, int size) {
  if (is_uint8(mask.value_)) {
    size = sizeof(int8_t);
  } else if (is_uint16(mask.value_)) {
    size = sizeof(int16_t);
  }
  EnsureSpace ensure_space(this);
  bool half_word = size == sizeof(int16_t);
  if (half_word) {
    emit(0x66);
    size = sizeof(int32_t);
  }
  bool byte_operand = size == sizeof(int8_t);
  if (byte_operand) {
    size = sizeof(int32_t);
  }
  emit_rex(rax, op, size);
  emit(byte_operand ? 0xF6 : 0xF7);
  emit_operand(rax, op);  // Operation code 0
  if (byte_operand) {
    emit(mask.value_);
  } else if (half_word) {
    emitw(mask.value_);
  } else {
    emit(mask);
  }
}

void Assembler::emit_test(Operand op, Register reg, int size) {
  EnsureSpace ensure_space(this);
  if (size == sizeof(int16_t)) {
    emit(0x66);
    size = sizeof(int32_t);
  }
  bool byte_operand = size == sizeof(int8_t);
  if (byte_operand) {
    size = sizeof(int32_t);
    if (!reg.is_byte_register()) {
      // Register is not one of al, bl, cl, dl.  Its encoding needs REX.
      emit_rex_32(reg, op);
    } else {
      emit_optional_rex_32(reg, op);
    }
  } else {
    emit_rex(reg, op, size);
  }
  emit(byte_operand ? 0x84 : 0x85);
  emit_operand(reg, op);
}

// FPU instructions.

void Assembler::fld(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xD9, 0xC0, i);
}

void Assembler::fld1() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xE8);
}

void Assembler::fldz() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xEE);
}

void Assembler::fldpi() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xEB);
}

void Assembler::fldln2() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xED);
}

void Assembler::fld_s(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xD9);
  emit_operand(0, adr);
}

void Assembler::fld_d(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDD);
  emit_operand(0, adr);
}

void Assembler::fstp_s(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xD9);
  emit_operand(3, adr);
}

void Assembler::fstp_d(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDD);
  emit_operand(3, adr);
}

void Assembler::fstp(int index) {
  DCHECK(is_uint3(index));
  EnsureSpace ensure_space(this);
  emit_farith(0xDD, 0xD8, index);
}

void Assembler::fild_s(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDB);
  emit_operand(0, adr);
}

void Assembler::fild_d(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDF);
  emit_operand(5, adr);
}

void Assembler::fistp_s(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDB);
  emit_operand(3, adr);
}

void Assembler::fisttp_s(Operand adr) {
  DCHECK(IsEnabled(SSE3));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDB);
  emit_operand(1, adr);
}

void Assembler::fisttp_d(Operand adr) {
  DCHECK(IsEnabled(SSE3));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDD);
  emit_operand(1, adr);
}

void Assembler::fist_s(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDB);
  emit_operand(2, adr);
}

void Assembler::fistp_d(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDF);
  emit_operand(7, adr);
}

void Assembler::fabs() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xE1);
}

void Assembler::fchs() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xE0);
}

void Assembler::fcos() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xFF);
}

void Assembler::fsin() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xFE);
}

void Assembler::fptan() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xF2);
}

void Assembler::fyl2x() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xF1);
}

void Assembler::f2xm1() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xF0);
}

void Assembler::fscale() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xFD);
}

void Assembler::fninit() {
  EnsureSpace ensure_space(this);
  emit(0xDB);
  emit(0xE3);
}

void Assembler::fadd(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDC, 0xC0, i);
}

void Assembler::fsub(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDC, 0xE8, i);
}

void Assembler::fisub_s(Operand adr) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(adr);
  emit(0xDA);
  emit_operand(4, adr);
}

void Assembler::fmul(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDC, 0xC8, i);
}

void Assembler::fdiv(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDC, 0xF8, i);
}

void Assembler::faddp(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDE, 0xC0, i);
}

void Assembler::fsubp(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDE, 0xE8, i);
}

void Assembler::fsubrp(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDE, 0xE0, i);
}

void Assembler::fmulp(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDE, 0xC8, i);
}

void Assembler::fdivp(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDE, 0xF8, i);
}

void Assembler::fprem() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xF8);
}

void Assembler::fprem1() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xF5);
}

void Assembler::fxch(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xD9, 0xC8, i);
}

void Assembler::fincstp() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xF7);
}

void Assembler::ffree(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDD, 0xC0, i);
}

void Assembler::ftst() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xE4);
}

void Assembler::fucomp(int i) {
  EnsureSpace ensure_space(this);
  emit_farith(0xDD, 0xE8, i);
}

void Assembler::fucompp() {
  EnsureSpace ensure_space(this);
  emit(0xDA);
  emit(0xE9);
}

void Assembler::fucomi(int i) {
  EnsureSpace ensure_space(this);
  emit(0xDB);
  emit(0xE8 + i);
}

void Assembler::fucomip() {
  EnsureSpace ensure_space(this);
  emit(0xDF);
  emit(0xE9);
}

void Assembler::fcompp() {
  EnsureSpace ensure_space(this);
  emit(0xDE);
  emit(0xD9);
}

void Assembler::fnstsw_ax() {
  EnsureSpace ensure_space(this);
  emit(0xDF);
  emit(0xE0);
}

void Assembler::fwait() {
  EnsureSpace ensure_space(this);
  emit(0x9B);
}

void Assembler::frndint() {
  EnsureSpace ensure_space(this);
  emit(0xD9);
  emit(0xFC);
}

void Assembler::fnclex() {
  EnsureSpace ensure_space(this);
  emit(0xDB);
  emit(0xE2);
}

void Assembler::sahf() {
  // TODO(X64): Test for presence. Not all 64-bit intel CPU's have sahf
  // in 64-bit mode. Test CpuID.
  DCHECK(IsEnabled(SAHF));
  EnsureSpace ensure_space(this);
  emit(0x9E);
}

void Assembler::emit_farith(int b1, int b2, int i) {
  DCHECK(is_uint8(b1) && is_uint8(b2));  // wrong opcode
  DCHECK(is_uint3(i));                   // illegal stack offset
  emit(b1);
  emit(b2 + i);
}

// SSE 2 operations.

void Assembler::movd(XMMRegister dst, Register src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x6E);
  emit_sse_operand(dst, src);
}

void Assembler::movd(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x6E);
  emit_sse_operand(dst, src);
}

void Assembler::movd(Register dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(src, dst);
  emit(0x0F);
  emit(0x7E);
  emit_sse_operand(src, dst);
}

void Assembler::movq(XMMRegister dst, Register src) {
  // Mixing AVX and non-AVX is expensive, catch those cases
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x6E);
  emit_sse_operand(dst, src);
}

void Assembler::movq(XMMRegister dst, Operand src) {
  // Mixing AVX and non-AVX is expensive, catch those cases
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x6E);
  emit_sse_operand(dst, src);
}

void Assembler::movq(Register dst, XMMRegister src) {
  // Mixing AVX and non-AVX is expensive, catch those cases
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0x7E);
  emit_sse_operand(src, dst);
}

void Assembler::movq(XMMRegister dst, XMMRegister src) {
  // Mixing AVX and non-AVX is expensive, catch those cases
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  if (dst.low_bits() == 4) {
    // Avoid unnecessary SIB byte.
    emit(0xF3);
    emit_optional_rex_32(dst, src);
    emit(0x0F);
    emit(0x7E);
    emit_sse_operand(dst, src);
  } else {
    emit(0x66);
    emit_optional_rex_32(src, dst);
    emit(0x0F);
    emit(0xD6);
    emit_sse_operand(src, dst);
  }
}

void Assembler::movdqa(Operand dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0x7F);
  emit_sse_operand(src, dst);
}

void Assembler::movdqa(XMMRegister dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::movdqa(XMMRegister dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0x7F);
  emit_sse_operand(src, dst);
}

void Assembler::movdqu(Operand dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0x7F);
  emit_sse_operand(src, dst);
}

void Assembler::movdqu(XMMRegister dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::movdqu(XMMRegister dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::pinsrw(XMMRegister dst, Register src, uint8_t imm8) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC4);
  emit_sse_operand(dst, src);
  emit(imm8);
}

void Assembler::pinsrw(XMMRegister dst, Operand src, uint8_t imm8) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC4);
  emit_sse_operand(dst, src);
  emit(imm8);
}

void Assembler::pextrq(Register dst, XMMRegister src, int8_t imm8) {
  DCHECK(IsEnabled(SSE4_1));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(src, dst);
  emit(0x0F);
  emit(0x3A);
  emit(0x16);
  emit_sse_operand(src, dst);
  emit(imm8);
}

void Assembler::pinsrq(XMMRegister dst, Register src, uint8_t imm8) {
  DCHECK(IsEnabled(SSE4_1));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x3A);
  emit(0x22);
  emit_sse_operand(dst, src);
  emit(imm8);
}

void Assembler::pinsrq(XMMRegister dst, Operand src, uint8_t imm8) {
  DCHECK(IsEnabled(SSE4_1));
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x3A);
  emit(0x22);
  emit_sse_operand(dst, src);
  emit(imm8);
}

void Assembler::pinsrd(XMMRegister dst, Register src, uint8_t imm8) {
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x22, imm8);
}

void Assembler::pinsrd(XMMRegister dst, Operand src, uint8_t imm8) {
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x22);
  emit(imm8);
}

void Assembler::pinsrb(XMMRegister dst, Register src, uint8_t imm8) {
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x20, imm8);
}

void Assembler::pinsrb(XMMRegister dst, Operand src, uint8_t imm8) {
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x20);
  emit(imm8);
}

void Assembler::insertps(XMMRegister dst, XMMRegister src, uint8_t imm8) {
  DCHECK(is_uint8(imm8));
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x21);
  emit(imm8);
}

void Assembler::insertps(XMMRegister dst, Operand src, uint8_t imm8) {
  DCHECK(is_uint8(imm8));
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x21);
  emit(imm8);
}

void Assembler::movsd(Operand dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);  // double
  emit_optional_rex_32(src, dst);
  emit(0x0F);
  emit(0x11);  // store
  emit_sse_operand(src, dst);
}

void Assembler::movsd(XMMRegister dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);  // double
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x10);  // load
  emit_sse_operand(dst, src);
}

void Assembler::movsd(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);  // double
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x10);  // load
  emit_sse_operand(dst, src);
}

void Assembler::movaps(XMMRegister dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  if (src.low_bits() == 4) {
    // Try to avoid an unnecessary SIB byte.
    emit_optional_rex_32(src, dst);
    emit(0x0F);
    emit(0x29);
    emit_sse_operand(src, dst);
  } else {
    emit_optional_rex_32(dst, src);
    emit(0x0F);
    emit(0x28);
    emit_sse_operand(dst, src);
  }
}

void Assembler::movaps(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x28);
  emit_sse_operand(dst, src);
}

void Assembler::shufps(XMMRegister dst, XMMRegister src, uint8_t imm8) {
  DCHECK(is_uint8(imm8));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC6);
  emit_sse_operand(dst, src);
  emit(imm8);
}

void Assembler::movapd(XMMRegister dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  if (src.low_bits() == 4) {
    // Try to avoid an unnecessary SIB byte.
    emit(0x66);
    emit_optional_rex_32(src, dst);
    emit(0x0F);
    emit(0x29);
    emit_sse_operand(src, dst);
  } else {
    emit(0x66);
    emit_optional_rex_32(dst, src);
    emit(0x0F);
    emit(0x28);
    emit_sse_operand(dst, src);
  }
}

void Assembler::movupd(XMMRegister dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x10);
  emit_sse_operand(dst, src);
}

void Assembler::movupd(Operand dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(src, dst);
  emit(0x0F);
  emit(0x11);
  emit_sse_operand(src, dst);
}

void Assembler::ucomiss(XMMRegister dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2E);
  emit_sse_operand(dst, src);
}

void Assembler::ucomiss(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2E);
  emit_sse_operand(dst, src);
}

void Assembler::movss(XMMRegister dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);  // single
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x10);  // load
  emit_sse_operand(dst, src);
}

void Assembler::movss(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);  // single
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x10);  // load
  emit_sse_operand(dst, src);
}

void Assembler::movss(Operand src, XMMRegister dst) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);  // single
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x11);  // store
  emit_sse_operand(dst, src);
}

void Assembler::movlps(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x12);
  emit_sse_operand(dst, src);
}

void Assembler::movlps(Operand src, XMMRegister dst) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x13);
  emit_sse_operand(dst, src);
}

void Assembler::movhps(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x16);
  emit_sse_operand(dst, src);
}

void Assembler::movhps(Operand src, XMMRegister dst) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x17);
  emit_sse_operand(dst, src);
}

void Assembler::cmpps(XMMRegister dst, XMMRegister src, int8_t cmp) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC2);
  emit_sse_operand(dst, src);
  emit(cmp);
}

void Assembler::cmpps(XMMRegister dst, Operand src, int8_t cmp) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC2);
  emit_sse_operand(dst, src);
  emit(cmp);
}

void Assembler::cmppd(XMMRegister dst, XMMRegister src, int8_t cmp) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC2);
  emit_sse_operand(dst, src);
  emit(cmp);
}

void Assembler::cmppd(XMMRegister dst, Operand src, int8_t cmp) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC2);
  emit_sse_operand(dst, src);
  emit(cmp);
}

void Assembler::cvtdq2pd(XMMRegister dst, XMMRegister src) {
  sse2_instr(dst, src, 0xF3, 0x0F, 0xE6);
}

void Assembler::cvttss2si(Register dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2C);
  emit_operand(dst, src);
}

void Assembler::cvttss2si(Register dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2C);
  emit_sse_operand(dst, src);
}

void Assembler::cvttsd2si(Register dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2C);
  emit_operand(dst, src);
}

void Assembler::cvttsd2si(Register dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2C);
  emit_sse_operand(dst, src);
}

void Assembler::cvttss2siq(Register dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2C);
  emit_sse_operand(dst, src);
}

void Assembler::cvttss2siq(Register dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2C);
  emit_sse_operand(dst, src);
}

void Assembler::cvttsd2siq(Register dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2C);
  emit_sse_operand(dst, src);
}

void Assembler::cvttsd2siq(Register dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2C);
  emit_sse_operand(dst, src);
}

void Assembler::cvttps2dq(XMMRegister dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x5B);
  emit_sse_operand(dst, src);
}

void Assembler::cvttps2dq(XMMRegister dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x5B);
  emit_sse_operand(dst, src);
}

void Assembler::cvtlsi2sd(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2A);
  emit_sse_operand(dst, src);
}

void Assembler::cvtlsi2sd(XMMRegister dst, Register src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2A);
  emit_sse_operand(dst, src);
}

void Assembler::cvtlsi2ss(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2A);
  emit_sse_operand(dst, src);
}

void Assembler::cvtlsi2ss(XMMRegister dst, Register src) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2A);
  emit_sse_operand(dst, src);
}

void Assembler::cvtqsi2ss(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2A);
  emit_sse_operand(dst, src);
}

void Assembler::cvtqsi2ss(XMMRegister dst, Register src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2A);
  emit_sse_operand(dst, src);
}

void Assembler::cvtqsi2sd(XMMRegister dst, Operand src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2A);
  emit_sse_operand(dst, src);
}

void Assembler::cvtqsi2sd(XMMRegister dst, Register src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2A);
  emit_sse_operand(dst, src);
}

void Assembler::cvtsd2si(Register dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x2D);
  emit_sse_operand(dst, src);
}

void Assembler::cvtsd2siq(Register dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0x2D);
  emit_sse_operand(dst, src);
}

void Assembler::haddps(XMMRegister dst, XMMRegister src) {
  DCHECK(IsEnabled(SSE3));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x7C);
  emit_sse_operand(dst, src);
}

void Assembler::haddps(XMMRegister dst, Operand src) {
  DCHECK(IsEnabled(SSE3));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x7C);
  emit_sse_operand(dst, src);
}

void Assembler::cmpeqss(XMMRegister dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC2);
  emit_sse_operand(dst, src);
  emit(0x00);  // EQ == 0
}

void Assembler::cmpeqsd(XMMRegister dst, XMMRegister src) {
  DCHECK(!IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC2);
  emit_sse_operand(dst, src);
  emit(0x00);  // EQ == 0
}

void Assembler::cmpltsd(XMMRegister dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xC2);
  emit_sse_operand(dst, src);
  emit(0x01);  // LT == 1
}

void Assembler::roundss(XMMRegister dst, XMMRegister src, RoundingMode mode) {
  DCHECK(!IsEnabled(AVX));
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x0A);
  // Mask precision exception.
  emit(static_cast<uint8_t>(mode) | 0x8);
}

void Assembler::roundss(XMMRegister dst, Operand src, RoundingMode mode) {
  DCHECK(!IsEnabled(AVX));
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x0A);
  // Mask precision exception.
  emit(static_cast<uint8_t>(mode) | 0x8);
}

void Assembler::roundsd(XMMRegister dst, XMMRegister src, RoundingMode mode) {
  DCHECK(!IsEnabled(AVX));
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x0B);
  // Mask precision exception.
  emit(static_cast<uint8_t>(mode) | 0x8);
}

void Assembler::roundsd(XMMRegister dst, Operand src, RoundingMode mode) {
  DCHECK(!IsEnabled(AVX));
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x0B);
  // Mask precision exception.
  emit(static_cast<uint8_t>(mode) | 0x8);
}

void Assembler::roundps(XMMRegister dst, XMMRegister src, RoundingMode mode) {
  DCHECK(!IsEnabled(AVX));
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x08);
  // Mask precision exception.
  emit(static_cast<uint8_t>(mode) | 0x8);
}

void Assembler::roundpd(XMMRegister dst, XMMRegister src, RoundingMode mode) {
  DCHECK(!IsEnabled(AVX));
  sse4_instr(dst, src, 0x66, 0x0F, 0x3A, 0x09);
  // Mask precision exception.
  emit(static_cast<uint8_t>(mode) | 0x8);
}

void Assembler::movmskpd(Register dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x50);
  emit_sse_operand(dst, src);
}

void Assembler::movmskps(Register dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x50);
  emit_sse_operand(dst, src);
}

void Assembler::pmovmskb(Register dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xD7);
  emit_sse_operand(dst, src);
}

// AVX instructions
#define VMOV_DUP(SIMDRegister, length)                            \
  void Assembler::vmovddup(SIMDRegister dst, SIMDRegister src) {  \
    DCHECK(IsEnabled(AVX));                                       \
    EnsureSpace ensure_space(this);                               \
    emit_vex_prefix(dst, xmm0, src, k##length, kF2, k0F, kWIG);   \
    emit(0x12);                                                   \
    emit_sse_operand(dst, src);                                   \
  }                                                               \
                                                                  \
  void Assembler::vmovddup(SIMDRegister dst, Operand src) {       \
    DCHECK(IsEnabled(AVX));                                       \
    EnsureSpace ensure_space(this);                               \
    emit_vex_prefix(dst, xmm0, src, k##length, kF2, k0F, kWIG);   \
    emit(0x12);                                                   \
    emit_sse_operand(dst, src);                                   \
  }                                                               \
                                                                  \
  void Assembler::vmovshdup(SIMDRegister dst, SIMDRegister src) { \
    DCHECK(IsEnabled(AVX));                                       \
    EnsureSpace ensure_space(this);                               \
    emit_vex_prefix(dst, xmm0, src, k##length, kF3, k0F, kWIG);   \
    emit(0x16);                                                   \
    emit_sse_operand(dst, src);                                   \
  }
VMOV_DUP(XMMRegister, L128)
VMOV_DUP(YMMRegister, L256)
#undef VMOV_DUP

#define BROADCAST(suffix, SIMDRegister, length, opcode)                   \
  void Assembler::vbroadcast##suffix(SIMDRegister dst, Operand src) {     \
    DCHECK(IsEnabled(AVX));                                               \
    EnsureSpace ensure_space(this);                                       \
    emit_vex_prefix(dst, xmm0, src, k##length, k66, k0F38, kW0);          \
    emit(0x##opcode);                                                     \
    emit_sse_operand(dst, src);                                           \
  }                                                                       \
  void Assembler::vbroadcast##suffix(SIMDRegister dst, XMMRegister src) { \
    DCHECK(IsEnabled(AVX2));                                              \
    EnsureSpace ensure_space(this);                                       \
    emit_vex_prefix(dst, xmm0, src, k##length, k66, k0F38, kW0);          \
    emit(0x##opcode);                                                     \
    emit_sse_operand(dst, src);                                           \
  }
BROADCAST(ss, XMMRegister, L128, 18)
BROADCAST(ss, YMMRegister, L256, 18)
BROADCAST(sd, YMMRegister, L256, 19)
#undef BROADCAST

void Assembler::vinserti128(YMMRegister dst, YMMRegister src1, XMMRegister src2,
                            uint8_t imm8) {
  DCHECK(IsEnabled(AVX2));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL256, k66, k0F3A, kW0);
  emit(0x38);
  emit_sse_operand(dst, src2);
  emit(imm8);
}

void Assembler::vperm2f128(YMMRegister dst, YMMRegister src1, YMMRegister src2,
                           uint8_t imm8) {
  DCHECK(IsEnabled(AVX));
  DCHECK(is_uint8(imm8));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL256, k66, k0F3A, kW0);
  emit(0x06);
  emit_sse_operand(dst, src2);
  emit(imm8);
}

void Assembler::vextractf128(XMMRegister dst, YMMRegister src, uint8_t imm8) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(src, xmm0, dst, kL256, k66, k0F3A, kW0);
  emit(0x19);
  emit_sse_operand(src, dst);
  emit(imm8);
}

void Assembler::fma_instr(uint8_t op, XMMRegister dst, XMMRegister src1,
                          XMMRegister src2, VectorLength l, SIMDPrefix pp,
                          LeadingOpcode m, VexW w) {
  DCHECK(IsEnabled(FMA3));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, l, pp, m, w);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::fma_instr(uint8_t op, XMMRegister dst, XMMRegister src1,
                          Operand src2, VectorLength l, SIMDPrefix pp,
                          LeadingOpcode m, VexW w) {
  DCHECK(IsEnabled(FMA3));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, l, pp, m, w);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::vmovd(XMMRegister dst, Register src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  XMMRegister isrc = XMMRegister::from_code(src.code());
  emit_vex_prefix(dst, xmm0, isrc, kL128, k66, k0F, kW0);
  emit(0x6E);
  emit_sse_operand(dst, src);
}

void Assembler::vmovd(XMMRegister dst, Operand src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, xmm0, src, kL128, k66, k0F, kW0);
  emit(0x6E);
  emit_sse_operand(dst, src);
}

void Assembler::vmovd(Register dst, XMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  XMMRegister idst = XMMRegister::from_code(dst.code());
  emit_vex_prefix(src, xmm0, idst, kL128, k66, k0F, kW0);
  emit(0x7E);
  emit_sse_operand(src, dst);
}

void Assembler::vmovq(XMMRegister dst, Register src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  XMMRegister isrc = XMMRegister::from_code(src.code());
  emit_vex_prefix(dst, xmm0, isrc, kL128, k66, k0F, kW1);
  emit(0x6E);
  emit_sse_operand(dst, src);
}

void Assembler::vmovq(XMMRegister dst, Operand src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, xmm0, src, kL128, k66, k0F, kW1);
  emit(0x6E);
  emit_sse_operand(dst, src);
}

void Assembler::vmovq(Register dst, XMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  XMMRegister idst = XMMRegister::from_code(dst.code());
  emit_vex_prefix(src, xmm0, idst, kL128, k66, k0F, kW1);
  emit(0x7E);
  emit_sse_operand(src, dst);
}

void Assembler::vmovdqa(XMMRegister dst, Operand src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, xmm0, src, kL128, k66, k0F, kWIG);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::vmovdqa(XMMRegister dst, XMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, xmm0, src, kL128, k66, k0F, kWIG);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::vmovdqa(YMMRegister dst, Operand src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, ymm0, src, kL256, k66, k0F, kWIG);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::vmovdqa(YMMRegister dst, YMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, ymm0, src, kL256, k66, k0F, kWIG);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::vmovdqu(XMMRegister dst, Operand src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, xmm0, src, kL128, kF3, k0F, kWIG);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::vmovdqu(Operand dst, XMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(src, xmm0, dst, kL128, kF3, k0F, kWIG);
  emit(0x7F);
  emit_sse_operand(src, dst);
}

void Assembler::vmovdqu(XMMRegister dst, XMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(src, xmm0, dst, kL128, kF3, k0F, kWIG);
  emit(0x7F);
  emit_sse_operand(src, dst);
}

void Assembler::vmovdqu(YMMRegister dst, Operand src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, ymm0, src, kL256, kF3, k0F, kWIG);
  emit(0x6F);
  emit_sse_operand(dst, src);
}

void Assembler::vmovdqu(Operand dst, YMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(src, ymm0, dst, kL256, kF3, k0F, kWIG);
  emit(0x7F);
  emit_sse_operand(src, dst);
}

void Assembler::vmovdqu(YMMRegister dst, YMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(src, ymm0, dst, kL256, kF3, k0F, kWIG);
  emit(0x7F);
  emit_sse_operand(src, dst);
}

void Assembler::vmovlps(XMMRegister dst, XMMRegister src1, Operand src2) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL128, kNoPrefix, k0F, kWIG);
  emit(0x12);
  emit_sse_operand(dst, src2);
}

void Assembler::vmovlps(Operand dst, XMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(src, xmm0, dst, kL128, kNoPrefix, k0F, kWIG);
  emit(0x13);
  emit_sse_operand(src, dst);
}

void Assembler::vmovhps(XMMRegister dst, XMMRegister src1, Operand src2) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL128, kNoPrefix, k0F, kWIG);
  emit(0x16);
  emit_sse_operand(dst, src2);
}

void Assembler::vmovhps(Operand dst, XMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(src, xmm0, dst, kL128, kNoPrefix, k0F, kWIG);
  emit(0x17);
  emit_sse_operand(src, dst);
}

void Assembler::vinstr(uint8_t op, XMMRegister dst, XMMRegister src1,
                       XMMRegister src2, SIMDPrefix pp, LeadingOpcode m, VexW w,
                       CpuFeature feature) {
  DCHECK(IsEnabled(feature));
  DCHECK(feature == AVX || feature == AVX2);
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kLIG, pp, m, w);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::vinstr(uint8_t op, XMMRegister dst, XMMRegister src1,
                       Operand src2, SIMDPrefix pp, LeadingOpcode m, VexW w,
                       CpuFeature feature) {
  DCHECK(IsEnabled(feature));
  DCHECK(feature == AVX || feature == AVX2);
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kLIG, pp, m, w);
  emit(op);
  emit_sse_operand(dst, src2);
}

template <typename Reg1, typename Reg2, typename Op>
void Assembler::vinstr(uint8_t op, Reg1 dst, Reg2 src1, Op src2, SIMDPrefix pp,
                       LeadingOpcode m, VexW w, CpuFeature feature) {
  DCHECK(IsEnabled(feature));
  DCHECK(feature == AVX || feature == AVX2);
  DCHECK(
      (std::is_same_v<Reg1, YMMRegister> || std::is_same_v<Reg2, YMMRegister>));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL256, pp, m, w);
  emit(op);
  emit_sse_operand(dst, src2);
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Assembler::vinstr(
    uint8_t op, YMMRegister dst, YMMRegister src1, YMMRegister src2,
    SIMDPrefix pp, LeadingOpcode m, VexW w, CpuFeature feature);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Assembler::vinstr(
    uint8_t op, YMMRegister dst, XMMRegister src1, XMMRegister src2,
    SIMDPrefix pp, LeadingOpcode m, VexW w, CpuFeature feature);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Assembler::vinstr(
    uint8_t op, YMMRegister dst, YMMRegister src1, Operand src2, SIMDPrefix pp,
    LeadingOpcode m, VexW w, CpuFeature feature);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Assembler::vinstr(
    uint8_t op, YMMRegister dst, YMMRegister src1, XMMRegister src2,
    SIMDPrefix pp, LeadingOpcode m, VexW w, CpuFeature feature);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Assembler::vinstr(
    uint8_t op, YMMRegister dst, XMMRegister src1, Operand src2, SIMDPrefix pp,
    LeadingOpcode m, VexW w, CpuFeature feature);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void Assembler::vinstr(
    uint8_t op, YMMRegister dst, XMMRegister src1, YMMRegister src2,
    SIMDPrefix pp, LeadingOpcode m, VexW w, CpuFeature feature);

void Assembler::vps(uint8_t op, XMMRegister dst, XMMRegister src1,
                    XMMRegister src2) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL128, kNoPrefix, k0F, kWIG);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::vps(uint8_t op, YMMRegister dst, YMMRegister src1,
                    YMMRegister src2) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL256, kNoPrefix, k0F, kWIG);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::vps(uint8_t op, XMMRegister dst, XMMRegister src1,
                    Operand src2) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL128, kNoPrefix, k0F, kWIG);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::vps(uint8_t op, YMMRegister dst, YMMRegister src1,
                    Operand src2) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL256, kNoPrefix, k0F, kWIG);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::vps(uint8_t op, XMMRegister dst, XMMRegister src1,
                    XMMRegister src2, uint8_t imm8) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL128, kNoPrefix, k0F, kWIG);
  emit(op);
  emit_sse_operand(dst, src2);
  emit(imm8);
}

void Assembler::vps(uint8_t op, YMMRegister dst, YMMRegister src1,
                    YMMRegister src2, uint8_t imm8) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kL256, kNoPrefix, k0F, kWIG);
  emit(op);
  emit_sse_operand(dst, src2);
  emit(imm8);
}

#define VPD(DSTRegister, SRCRegister, length)                        \
  void Assembler::vpd(uint8_t op, DSTRegister dst, SRCRegister src1, \
                      SRCRegister src2) {                            \
    DCHECK(IsEnabled(AVX));                                          \
    EnsureSpace ensure_space(this);                                  \
    emit_vex_prefix(dst, src1, src2, k##length, k66, k0F, kWIG);     \
    emit(op);                                                        \
    emit_sse_operand(dst, src2);                                     \
  }                                                                  \
                                                                     \
  void Assembler::vpd(uint8_t op, DSTRegister dst, SRCRegister src1, \
                      Operand src2) {                                \
    DCHECK(IsEnabled(AVX));                                          \
    EnsureSpace ensure_space(this);                                  \
    emit_vex_prefix(dst, src1, src2, k##length, k66, k0F, kWIG);     \
    emit(op);                                                        \
    emit_sse_operand(dst, src2);                                     \
  }
VPD(XMMRegister, XMMRegister, L128)
VPD(XMMRegister, YMMRegister, L256)
VPD(YMMRegister, YMMRegister, L256)
#undef VPD

void Assembler::vucomiss(XMMRegister dst, XMMRegister src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, xmm0, src, kLIG, kNoPrefix, k0F, kWIG);
  emit(0x2E);
  emit_sse_operand(dst, src);
}

void Assembler::vucomiss(XMMRegister dst, Operand src) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, xmm0, src, kLIG, kNoPrefix, k0F, kWIG);
  emit(0x2E);
  emit_sse_operand(dst, src);
}

void Assembler::vpmovmskb(Register dst, XMMRegister src) {
  XMMRegister idst = XMMRegister::from_code(dst.code());
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(idst, xmm0, src, kL128, k66, k0F, kWIG);
  emit(0xD7);
  emit_sse_operand(idst, src);
}

void Assembler::vss(uint8_t op, XMMRegister dst, XMMRegister src1,
                    XMMRegister src2) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kLIG, kF3, k0F, kWIG);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::vss(uint8_t op, XMMRegister dst, XMMRegister src1,
                    Operand src2) {
  DCHECK(IsEnabled(AVX));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, src1, src2, kLIG, kF3, k0F, kWIG);
  emit(op);
  emit_sse_operand(dst, src2);
}

void Assembler::bmi1q(uint8_t op, Register reg, Register vreg, Register rm) {
  DCHECK(IsEnabled(BMI1));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(reg, vreg, rm, kLZ, kNoPrefix, k0F38, kW1);
  emit(op);
  emit_modrm(reg, rm);
}

void Assembler::bmi1q(uint8_t op, Register reg, Register vreg, Operand rm) {
  DCHECK(IsEnabled(BMI1));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(reg, vreg, rm, kLZ, kNoPrefix, k0F38, kW1);
  emit(op);
  emit_operand(reg, rm);
}

void Assembler::bmi1l(uint8_t op, Register reg, Register vreg, Register rm) {
  DCHECK(IsEnabled(BMI1));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(reg, vreg, rm, kLZ, kNoPrefix, k0F38, kW0);
  emit(op);
  emit_modrm(reg, rm);
}

void Assembler::bmi1l(uint8_t op, Register reg, Register vreg, Operand rm) {
  DCHECK(IsEnabled(BMI1));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(reg, vreg, rm, kLZ, kNoPrefix, k0F38, kW0);
  emit(op);
  emit_operand(reg, rm);
}

void Assembler::tzcntq(Register dst, Register src) {
  DCHECK(IsEnabled(BMI1));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBC);
  emit_modrm(dst, src);
}

void Assembler::tzcntq(Register dst, Operand src) {
  DCHECK(IsEnabled(BMI1));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBC);
  emit_operand(dst, src);
}

void Assembler::tzcntl(Register dst, Register src) {
  DCHECK(IsEnabled(BMI1));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBC);
  emit_modrm(dst, src);
}

void Assembler::tzcntl(Register dst, Operand src) {
  DCHECK(IsEnabled(BMI1));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBC);
  emit_operand(dst, src);
}

void Assembler::lzcntq(Register dst, Register src) {
  DCHECK(IsEnabled(LZCNT));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBD);
  emit_modrm(dst, src);
}

void Assembler::lzcntq(Register dst, Operand src) {
  DCHECK(IsEnabled(LZCNT));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xBD);
  emit_operand(dst, src);
}

void Assembler::lzcntl(Register dst, Register src) {
  DCHECK(IsEnabled(LZCNT));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBD);
  emit_modrm(dst, src);
}

void Assembler::lzcntl(Register dst, Operand src) {
  DCHECK(IsEnabled(LZCNT));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xBD);
  emit_operand(dst, src);
}

void Assembler::popcntq(Register dst, Register src) {
  DCHECK(IsEnabled(POPCNT));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xB8);
  emit_modrm(dst, src);
}

void Assembler::popcntq(Register dst, Operand src) {
  DCHECK(IsEnabled(POPCNT));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_rex_64(dst, src);
  emit(0x0F);
  emit(0xB8);
  emit_operand(dst, src);
}

void Assembler::popcntl(Register dst, Register src) {
  DCHECK(IsEnabled(POPCNT));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xB8);
  emit_modrm(dst, src);
}

void Assembler::popcntl(Register dst, Operand src) {
  DCHECK(IsEnabled(POPCNT));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xB8);
  emit_operand(dst, src);
}

void Assembler::bmi2q(SIMDPrefix pp, uint8_t op, Register reg, Register vreg,
                      Register rm) {
  DCHECK(IsEnabled(BMI2));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(reg, vreg, rm, kLZ, pp, k0F38, kW1);
  emit(op);
  emit_modrm(reg, rm);
}

void Assembler::bmi2q(SIMDPrefix pp, uint8_t op, Register reg, Register vreg,
                      Operand rm) {
  DCHECK(IsEnabled(BMI2));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(reg, vreg, rm, kLZ, pp, k0F38, kW1);
  emit(op);
  emit_operand(reg, rm);
}

void Assembler::bmi2l(SIMDPrefix pp, uint8_t op, Register reg, Register vreg,
                      Register rm) {
  DCHECK(IsEnabled(BMI2));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(reg, vreg, rm, kLZ, pp, k0F38, kW0);
  emit(op);
  emit_modrm(reg, rm);
}

void Assembler::bmi2l(SIMDPrefix pp, uint8_t op, Register reg, Register vreg,
                      Operand rm) {
  DCHECK(IsEnabled(BMI2));
  EnsureSpace ensure_space(this);
  emit_vex_prefix(reg, vreg, rm, kLZ, pp, k0F38, kW0);
  emit(op);
  emit_operand(reg, rm);
}

void Assembler::rorxq(Register dst, Register src, uint8_t imm8) {
  DCHECK(IsEnabled(BMI2));
  DCHECK(is_uint8(imm8));
  Register vreg = Register::from_code(0);  // VEX.vvvv unused
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, vreg, src, kLZ, kF2, k0F3A, kW1);
  emit(0xF0);
  emit_modrm(dst, src);
  emit(imm8);
}

void Assembler::rorxq(Register dst, Operand src, uint8_t imm8) {
  DCHECK(IsEnabled(BMI2));
  DCHECK(is_uint8(imm8));
  Register vreg = Register::from_code(0);  // VEX.vvvv unused
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, vreg, src, kLZ, kF2, k0F3A, kW1);
  emit(0xF0);
  emit_operand(dst, src);
  emit(imm8);
}

void Assembler::rorxl(Register dst, Register src, uint8_t imm8) {
  DCHECK(IsEnabled(BMI2));
  DCHECK(is_uint8(imm8));
  Register vreg = Register::from_code(0);  // VEX.vvvv unused
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, vreg, src, kLZ, kF2, k0F3A, kW0);
  emit(0xF0);
  emit_modrm(dst, src);
  emit(imm8);
}

void Assembler::rorxl(Register dst, Operand src, uint8_t imm8) {
  DCHECK(IsEnabled(BMI2));
  DCHECK(is_uint8(imm8));
  Register vreg = Register::from_code(0);  // VEX.vvvv unused
  EnsureSpace ensure_space(this);
  emit_vex_prefix(dst, vreg, src, kLZ, kF2, k0F3A, kW0);
  emit(0xF0);
  emit_operand(dst, src);
  emit(imm8);
}

void Assembler::pause() {
  emit(0xF3);
  emit(0x90);
}

void Assembler::movups(XMMRegister dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  if (src.low_bits() == 4) {
    // Try to avoid an unnecessary SIB byte.
    emit_optional_rex_32(src, dst);
    emit(0x0F);
    emit(0x11);
    emit_sse_operand(src, dst);
  } else {
    emit_optional_rex_32(dst, src);
    emit(0x0F);
    emit(0x10);
    emit_sse_operand(dst, src);
  }
}

void Assembler::movups(XMMRegister dst, Operand src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x10);
  emit_sse_operand(dst, src);
}

void Assembler::movups(Operand dst, XMMRegister src) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(src, dst);
  emit(0x0F);
  emit(0x11);
  emit_sse_operand(src, dst);
}

void Assembler::sse_instr(XMMRegister dst, XMMRegister src, uint8_t escape,
                          uint8_t opcode) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(escape);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::sse_instr(XMMRegister dst, Operand src, uint8_t escape,
                          uint8_t opcode) {
  EnsureSpace ensure_space(this);
  emit_optional_rex_32(dst, src);
  emit(escape);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::sse2_instr(XMMRegister dst, XMMRegister src, uint8_t prefix,
                           uint8_t escape, uint8_t opcode) {
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::sse2_instr(XMMRegister dst, Operand src, uint8_t prefix,
                           uint8_t escape, uint8_t opcode) {
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::ssse3_instr(XMMRegister dst, XMMRegister src, uint8_t prefix,
                            uint8_t escape1, uint8_t escape2, uint8_t opcode) {
  DCHECK(IsEnabled(SSSE3));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::ssse3_instr(XMMRegister dst, Operand src, uint8_t prefix,
                            uint8_t escape1, uint8_t escape2, uint8_t opcode) {
  DCHECK(IsEnabled(SSSE3));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::sse4_instr(XMMRegister dst, Register src, uint8_t prefix,
                           uint8_t escape1, uint8_t escape2, uint8_t opcode,
                           int8_t imm8) {
  DCHECK(is_uint8(imm8));
  DCHECK(IsEnabled(SSE4_1));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(dst, src);
  emit(imm8);
}

void Assembler::sse4_instr(XMMRegister dst, XMMRegister src, uint8_t prefix,
                           uint8_t escape1, uint8_t escape2, uint8_t opcode) {
  DCHECK(IsEnabled(SSE4_1));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::sse4_instr(XMMRegister dst, Operand src, uint8_t prefix,
                           uint8_t escape1, uint8_t escape2, uint8_t opcode) {
  DCHECK(IsEnabled(SSE4_1));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::sse4_instr(Register dst, XMMRegister src, uint8_t prefix,
                           uint8_t escape1, uint8_t escape2, uint8_t opcode,
                           int8_t imm8) {
  DCHECK(is_uint8(imm8));
  DCHECK(IsEnabled(SSE4_1));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(src, dst);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(src, dst);
  emit(imm8);
}

void Assembler::sse4_instr(Operand dst, XMMRegister src, uint8_t prefix,
                           uint8_t escape1, uint8_t escape2, uint8_t opcode,
                           int8_t imm8) {
  DCHECK(is_uint8(imm8));
  DCHECK(IsEnabled(SSE4_1));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(src, dst);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(src, dst);
  emit(imm8);
}

void Assembler::sse4_2_instr(XMMRegister dst, XMMRegister src, uint8_t prefix,
                             uint8_t escape1, uint8_t escape2, uint8_t opcode) {
  DCHECK(IsEnabled(SSE4_2));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::sse4_2_instr(XMMRegister dst, Operand src, uint8_t prefix,
                             uint8_t escape1, uint8_t escape2, uint8_t opcode) {
  DCHECK(IsEnabled(SSE4_2));
  EnsureSpace ensure_space(this);
  emit(prefix);
  emit_optional_rex_32(dst, src);
  emit(escape1);
  emit(escape2);
  emit(opcode);
  emit_sse_operand(dst, src);
}

void Assembler::lddqu(XMMRegister dst, Operand src) {
  DCHECK(IsEnabled(SSE3));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0xF0);
  emit_sse_operand(dst, src);
}

void Assembler::movddup(XMMRegister dst, XMMRegister src) {
  DCHECK(IsEnabled(SSE3));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x12);
  emit_sse_operand(dst, src);
}

void Assembler::movddup(XMMRegister dst, Operand src) {
  DCHECK(IsEnabled(SSE3));
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x12);
  emit_sse_operand(dst, src);
}

void Assembler::movshdup(XMMRegister dst, XMMRegister src) {
  DCHECK(IsEnabled(SSE3));
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x16);
  emit_sse_operand(dst, src);
}

void Assembler::psrldq(XMMRegister dst, uint8_t shift) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst);
  emit(0x0F);
  emit(0x73);
  emit_sse_operand(dst);
  emit(shift);
}

void Assembler::pshufhw(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x70);
  emit_sse_operand(dst, src);
  emit(shuffle);
}

void Assembler::pshufhw(XMMRegister dst, Operand src, uint8_t shuffle) {
  EnsureSpace ensure_space(this);
  emit(0xF3);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x70);
  emit_sse_operand(dst, src);
  emit(shuffle);
}

void Assembler::pshuflw(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x70);
  emit_sse_operand(dst, src);
  emit(shuffle);
}

void Assembler::pshuflw(XMMRegister dst, Operand src, uint8_t shuffle) {
  EnsureSpace ensure_space(this);
  emit(0xF2);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x70);
  emit_sse_operand(dst, src);
  emit(shuffle);
}

void Assembler::pshufd(XMMRegister dst, XMMRegister src, uint8_t shuffle) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x70);
  emit_sse_operand(dst, src);
  emit(shuffle);
}

void Assembler::pshufd(XMMRegister dst, Operand src, uint8_t shuffle) {
  EnsureSpace ensure_space(this);
  emit(0x66);
  emit_optional_rex_32(dst, src);
  emit(0x0F);
  emit(0x70);
  emit_sse_operand(dst, src);
  emit(shuffle);
}

void Assembler::emit_sse_operand(XMMRegister reg, Operand adr) {
  Register ireg = Register::from_code(reg.code());
  emit_operand(ireg, adr);
}

void Assembler::emit_sse_operand(Register reg, Operand adr) {
  emit_operand(reg, adr);
}

void Assembler::emit_sse_operand(XMMRegister dst, XMMRegister src) {
  emit(0xC0 | (dst.low_bits() << 3) | src.low_bits());
}

void Assembler::emit_sse_operand(XMMRegister dst, Register src) {
  emit(0xC0 | (dst.low_bits() << 3) | src.low_bits());
}

void Assembler::emit_sse_operand(Register dst, XMMRegister src) {
  emit(0xC0 | (dst.low_bits() << 3) | src.low_bits());
}

void Assembler::emit_sse_operand(XMMRegister dst) {
  emit(0xD8 | dst.low_bits());
}

void Assembler::db(uint8_t data) {
  EnsureSpace ensure_space(this);
  emit(data);
}

void Assembler::dd(uint32_t data) {
  EnsureSpace ensure_space(this);
  emitl(data);
}

void Assembler::dq(uint64_t data) {
  EnsureSpace ensure_space(this);
  emitq(data);
}

void Assembler::dq(Label* label) {
  EnsureSpace ensure_space(this);
  if (label->is_bound()) {
    internal_reference_positions_.push_back(pc_offset());
    emit(Immediate64(reinterpret_cast<Address>(buffer_start_) + label->pos(),
                     RelocInfo::INTERNAL_REFERENCE));
  } else {
    RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE);
    emitl(0);  // Zero for the first 32bit marks it as 64bit absolute address.
    if (label->is_linked()) {
      emitl(label->pos());
      label->link_to(pc_offset() - sizeof(int32_t));
    } else {
      DCHECK(label->is_unused());
      int32_t current = pc_offset();
      emitl(current);
      label->link_to(current);
    }
  }
}

void Assembler::WriteBuiltinJumpTableEntry(Label* label, const int table_pos) {
  EnsureSpace ensure_space(this);
  CHECK(label->is_bound());
  int32_t value = label->pos() - table_pos;
  RecordRelocInfo(RelocInfo::RELATIVE_SWITCH_TABLE_ENTRY, label->pos());
  emitl(value);
}

// Relocation information implementations.

void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  if (!ShouldRecordRelocInfo(rmode)) return;
  RelocInfo rinfo(reinterpret_cast<Address>(pc_), rmode, data);
  reloc_info_writer.Write(&rinfo);
}

const int RelocInfo::kApplyMask =
    RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
    RelocInfo::ModeMask(RelocInfo::NEAR_BUILTIN_ENTRY) |
    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
    RelocInfo::ModeMask(RelocInfo::WASM_CALL) |
    RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);

bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially coded.  Being
  // specially coded on x64 means that it is a relative 32 bit address, as used
  // by branch instructions.
  return (1 << rmode_) & kApplyMask;
}

bool RelocInfo::IsInConstantPool() { return false; }

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64
