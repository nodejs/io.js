// Copyright 2013 the V8 project authors. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#if V8_TARGET_ARCH_ARM64

#include "src/codegen/arm64/assembler-arm64.h"

#include "src/base/bits.h"
#include "src/base/cpu.h"
#include "src/base/small-vector.h"
#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/safepoint-table.h"
#include "src/execution/frame-constants.h"

namespace v8 {
namespace internal {

namespace {

#ifdef USE_SIMULATOR
unsigned SimulatorFeaturesFromCommandLine() {
  if (strcmp(v8_flags.sim_arm64_optional_features, "none") == 0) {
    return 0;
  }
  if (strcmp(v8_flags.sim_arm64_optional_features, "all") == 0) {
    return (1u << NUMBER_OF_CPU_FEATURES) - 1;
  }
  fprintf(
      stderr,
      "Error: unrecognised value for --sim-arm64-optional-features ('%s').\n",
      v8_flags.sim_arm64_optional_features.value());
  fprintf(stderr,
          "Supported values are:  none\n"
          "                       all\n");
  FATAL("sim-arm64-optional-features");
}
#endif  // USE_SIMULATOR

constexpr unsigned CpuFeaturesFromCompiler() {
  unsigned features = 0;
#if defined(__ARM_FEATURE_JCVT) && !defined(V8_TARGET_OS_IOS)
  features |= 1u << JSCVT;
#endif
#if defined(__ARM_FEATURE_DOTPROD)
  features |= 1u << DOTPROD;
#endif
#if defined(__ARM_FEATURE_ATOMICS)
  features |= 1u << LSE;
#endif
// There is no __ARM_FEATURE_PMULL macro; instead, __ARM_FEATURE_AES
// covers the FEAT_PMULL feature too.
#if defined(__ARM_FEATURE_AES)
  features |= 1u << PMULL1Q;
#endif
  return features;
}

constexpr unsigned CpuFeaturesFromTargetOS() {
  unsigned features = 0;
#if defined(V8_TARGET_OS_MACOS) && !defined(V8_TARGET_OS_IOS)
  // TODO(v8:13004): Detect if an iPhone is new enough to support jscvt, dotprot
  // and lse.
  features |= 1u << JSCVT;
  features |= 1u << DOTPROD;
  features |= 1u << LSE;
  features |= 1u << PMULL1Q;
#endif
  return features;
}

}  // namespace

// -----------------------------------------------------------------------------
// CpuFeatures implementation.
bool CpuFeatures::SupportsWasmSimd128() { return true; }

void CpuFeatures::ProbeImpl(bool cross_compile) {
  // Only use statically determined features for cross compile (snapshot).
  if (cross_compile) {
    supported_ |= CpuFeaturesFromCompiler();
    supported_ |= CpuFeaturesFromTargetOS();
    return;
  }

  // We used to probe for coherent cache support, but on older CPUs it
  // causes crashes (crbug.com/524337), and newer CPUs don't even have
  // the feature any more.

#ifdef USE_SIMULATOR
  supported_ |= SimulatorFeaturesFromCommandLine();
#else
  // Probe for additional features at runtime.
  base::CPU cpu;
  unsigned runtime = 0;
  if (cpu.has_jscvt()) {
    runtime |= 1u << JSCVT;
  }
  if (cpu.has_dot_prod()) {
    runtime |= 1u << DOTPROD;
  }
  if (cpu.has_lse()) {
    runtime |= 1u << LSE;
  }
  if (cpu.has_pmull1q()) {
    runtime |= 1u << PMULL1Q;
  }
  if (cpu.has_fp16()) {
    runtime |= 1u << FP16;
  }

  // Use the best of the features found by CPU detection and those inferred from
  // the build system.
  supported_ |= CpuFeaturesFromCompiler();
  supported_ |= runtime;
#endif  // USE_SIMULATOR

  // Set a static value on whether Simd is supported.
  // This variable is only used for certain archs to query SupportWasmSimd128()
  // at runtime in builtins using an extern ref. Other callers should use
  // CpuFeatures::SupportWasmSimd128().
  CpuFeatures::supports_wasm_simd_128_ = CpuFeatures::SupportsWasmSimd128();
}

void CpuFeatures::PrintTarget() {}
void CpuFeatures::PrintFeatures() {}

// -----------------------------------------------------------------------------
// CPURegList utilities.

CPURegister CPURegList::PopLowestIndex() {
  if (IsEmpty()) {
    return NoCPUReg;
  }
  int index = base::bits::CountTrailingZeros(list_);
  DCHECK((1LL << index) & list_);
  Remove(index);
  return CPURegister::Create(index, size_, type_);
}

CPURegister CPURegList::PopHighestIndex() {
  if (IsEmpty()) {
    return NoCPUReg;
  }
  int index = CountLeadingZeros(list_, kRegListSizeInBits);
  index = kRegListSizeInBits - 1 - index;
  DCHECK((1LL << index) & list_);
  Remove(index);
  return CPURegister::Create(index, size_, type_);
}

void CPURegList::Align() {
  // Use padreg, if necessary, to maintain stack alignment.
  if (Count() % 2 != 0) {
    if (IncludesAliasOf(padreg)) {
      Remove(padreg);
    } else {
      Combine(padreg);
    }
  }

  DCHECK_EQ(Count() % 2, 0);
}

CPURegList CPURegList::GetCalleeSaved(int size) {
  return CPURegList(CPURegister::kRegister, size, 19, 28);
}

CPURegList CPURegList::GetCalleeSavedV(int size) {
  return CPURegList(CPURegister::kVRegister, size, 8, 15);
}

CPURegList CPURegList::GetCallerSaved(int size) {
  // x18 is the platform register and is reserved for the use of platform ABIs.
  // Registers x0-x17 are caller-saved.
  CPURegList list = CPURegList(CPURegister::kRegister, size, 0, 17);
  return list;
}

CPURegList CPURegList::GetCallerSavedV(int size) {
  // Registers d0-d7 and d16-d31 are caller-saved.
  CPURegList list = CPURegList(CPURegister::kVRegister, size, 0, 7);
  list.Combine(CPURegList(CPURegister::kVRegister, size, 16, 31));
  return list;
}

// -----------------------------------------------------------------------------
// Implementation of RelocInfo

const int RelocInfo::kApplyMask =
    RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
    RelocInfo::ModeMask(RelocInfo::NEAR_BUILTIN_ENTRY) |
    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
    RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);

bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially coded. Being
  // specially coded on ARM64 means that it is an immediate branch.
  Instruction* instr = reinterpret_cast<Instruction*>(pc_);
  if (instr->IsLdrLiteralX()) {
    return false;
  } else {
    DCHECK(instr->IsBranchAndLink() || instr->IsUnconditionalBranch());
    return true;
  }
}

bool RelocInfo::IsInConstantPool() {
  Instruction* instr = reinterpret_cast<Instruction*>(pc_);
  DCHECK_IMPLIES(instr->IsLdrLiteralW(), COMPRESS_POINTERS_BOOL);
  return instr->IsLdrLiteralX() ||
         (COMPRESS_POINTERS_BOOL && instr->IsLdrLiteralW());
}

uint32_t RelocInfo::wasm_call_tag() const {
  DCHECK(rmode_ == WASM_CALL || rmode_ == WASM_STUB_CALL);
  Instruction* instr = reinterpret_cast<Instruction*>(pc_);
  if (instr->IsLdrLiteralX()) {
    return static_cast<uint32_t>(
        Memory<Address>(Assembler::target_pointer_address_at(pc_)));
  } else {
    DCHECK(instr->IsBranchAndLink() || instr->IsUnconditionalBranch());
    return static_cast<uint32_t>(instr->ImmPCOffset() / kInstrSize);
  }
}

bool AreAliased(const CPURegister& reg1, const CPURegister& reg2,
                const CPURegister& reg3, const CPURegister& reg4,
                const CPURegister& reg5, const CPURegister& reg6,
                const CPURegister& reg7, const CPURegister& reg8) {
  int number_of_valid_regs = 0;
  int number_of_valid_fpregs = 0;

  uint64_t unique_regs = 0;
  uint64_t unique_fpregs = 0;

  const CPURegister regs[] = {reg1, reg2, reg3, reg4, reg5, reg6, reg7, reg8};

  for (unsigned i = 0; i < arraysize(regs); i++) {
    if (regs[i].IsRegister()) {
      number_of_valid_regs++;
      unique_regs |= (uint64_t{1} << regs[i].code());
    } else if (regs[i].IsVRegister()) {
      number_of_valid_fpregs++;
      unique_fpregs |= (uint64_t{1} << regs[i].code());
    } else {
      DCHECK(!regs[i].is_valid());
    }
  }

  int number_of_unique_regs =
      CountSetBits(unique_regs, sizeof(unique_regs) * kBitsPerByte);
  int number_of_unique_fpregs =
      CountSetBits(unique_fpregs, sizeof(unique_fpregs) * kBitsPerByte);

  DCHECK(number_of_valid_regs >= number_of_unique_regs);
  DCHECK(number_of_valid_fpregs >= number_of_unique_fpregs);

  return (number_of_valid_regs != number_of_unique_regs) ||
         (number_of_valid_fpregs != number_of_unique_fpregs);
}

bool AreSameSizeAndType(const CPURegister& reg1, const CPURegister& reg2,
                        const CPURegister& reg3, const CPURegister& reg4,
                        const CPURegister& reg5, const CPURegister& reg6,
                        const CPURegister& reg7, const CPURegister& reg8) {
  DCHECK(reg1.is_valid());
  bool match = true;
  match &= !reg2.is_valid() || reg2.IsSameSizeAndType(reg1);
  match &= !reg3.is_valid() || reg3.IsSameSizeAndType(reg1);
  match &= !reg4.is_valid() || reg4.IsSameSizeAndType(reg1);
  match &= !reg5.is_valid() || reg5.IsSameSizeAndType(reg1);
  match &= !reg6.is_valid() || reg6.IsSameSizeAndType(reg1);
  match &= !reg7.is_valid() || reg7.IsSameSizeAndType(reg1);
  match &= !reg8.is_valid() || reg8.IsSameSizeAndType(reg1);
  return match;
}

bool AreSameFormat(const Register& reg1, const Register& reg2,
                   const Register& reg3, const Register& reg4) {
  DCHECK(reg1.is_valid());
  return (!reg2.is_valid() || reg2.IsSameSizeAndType(reg1)) &&
         (!reg3.is_valid() || reg3.IsSameSizeAndType(reg1)) &&
         (!reg4.is_valid() || reg4.IsSameSizeAndType(reg1));
}

bool AreSameFormat(const VRegister& reg1, const VRegister& reg2,
                   const VRegister& reg3, const VRegister& reg4) {
  DCHECK(reg1.is_valid());
  return (!reg2.is_valid() || reg2.IsSameFormat(reg1)) &&
         (!reg3.is_valid() || reg3.IsSameFormat(reg1)) &&
         (!reg4.is_valid() || reg4.IsSameFormat(reg1));
}

bool AreConsecutive(const CPURegister& reg1, const CPURegister& reg2,
                    const CPURegister& reg3, const CPURegister& reg4) {
  DCHECK(reg1.is_valid());

  if (!reg2.is_valid()) {
    DCHECK(!reg3.is_valid() && !reg4.is_valid());
    return true;
  } else if (reg2.code() != ((reg1.code() + 1) % (reg1.MaxCode() + 1))) {
    return false;
  }

  if (!reg3.is_valid()) {
    DCHECK(!reg4.is_valid());
    return true;
  } else if (reg3.code() != ((reg2.code() + 1) % (reg1.MaxCode() + 1))) {
    return false;
  }

  if (!reg4.is_valid()) {
    return true;
  } else if (reg4.code() != ((reg3.code() + 1) % (reg1.MaxCode() + 1))) {
    return false;
  }

  return true;
}

bool AreEven(const CPURegister& reg1, const CPURegister& reg2,
             const CPURegister& reg3, const CPURegister& reg4,
             const CPURegister& reg5, const CPURegister& reg6,
             const CPURegister& reg7, const CPURegister& reg8) {
  DCHECK(reg1.is_valid());
  bool even = reg1.IsEven();
  even &= !reg2.is_valid() || reg2.IsEven();
  even &= !reg3.is_valid() || reg3.IsEven();
  even &= !reg4.is_valid() || reg4.IsEven();
  even &= !reg5.is_valid() || reg5.IsEven();
  even &= !reg6.is_valid() || reg6.IsEven();
  even &= !reg7.is_valid() || reg7.IsEven();
  even &= !reg8.is_valid() || reg8.IsEven();
  return even;
}

bool Operand::NeedsRelocation(const Assembler* assembler) const {
  RelocInfo::Mode rmode = immediate_.rmode();

  if (RelocInfo::IsOnlyForSerializer(rmode)) {
    return assembler->options().record_reloc_info_for_serialization;
  }

  return !RelocInfo::IsNoInfo(rmode);
}

// Assembler
Assembler::Assembler(const MaybeAssemblerZone& zone,
                     const AssemblerOptions& options,
                     std::unique_ptr<AssemblerBuffer> buffer)
    : AssemblerBase(options, std::move(buffer)),
      zone_(zone),
      unresolved_branches_(zone_.get()),
      constpool_(this) {
  Reset();

#if defined(V8_OS_WIN)
  if (options.collect_win64_unwind_info) {
    xdata_encoder_ = std::make_unique<win64_unwindinfo::XdataEncoder>(*this);
  }
#endif
}

Assembler::~Assembler() {
  DCHECK(constpool_.IsEmpty());
  DCHECK_EQ(veneer_pool_blocked_nesting_, 0);
}

void Assembler::AbortedCodeGeneration() { constpool_.Clear(); }

void Assembler::Reset() {
#ifdef DEBUG
  DCHECK((pc_ >= buffer_start_) && (pc_ < buffer_start_ + buffer_->size()));
  DCHECK_EQ(veneer_pool_blocked_nesting_, 0);
  DCHECK(unresolved_branches_.empty());
  memset(buffer_start_, 0, pc_ - buffer_start_);
#endif
  pc_ = buffer_start_;
  reloc_info_writer.Reposition(buffer_start_ + buffer_->size(), pc_);
  constpool_.Clear();
  constpool_.SetNextCheckIn(ConstantPool::kCheckInterval);
  next_veneer_pool_check_ = kMaxInt;
}

#if defined(V8_OS_WIN)
win64_unwindinfo::BuiltinUnwindInfo Assembler::GetUnwindInfo() const {
  DCHECK(options().collect_win64_unwind_info);
  DCHECK_NOT_NULL(xdata_encoder_);
  return xdata_encoder_->unwinding_info();
}
#endif

void Assembler::AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate) {
  DCHECK_IMPLIES(isolate == nullptr, heap_number_requests_.empty());
  for (auto& request : heap_number_requests_) {
    Address pc = reinterpret_cast<Address>(buffer_start_) + request.offset();
    Handle<HeapObject> object =
        isolate->factory()->NewHeapNumber<AllocationType::kOld>(
            request.heap_number());
    EmbeddedObjectIndex index = AddEmbeddedObject(object);
    set_embedded_object_index_referenced_from(pc, index);
  }
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

  // Emit constant pool if necessary.
  ForceConstantPoolEmissionWithoutJump();
  DCHECK(constpool_.IsEmpty());

  int code_comments_size = WriteCodeComments();

  AllocateAndInstallRequestedHeapNumbers(isolate);

  // Set up code descriptor.
  // TODO(jgruber): Reconsider how these offsets and sizes are maintained up to
  // this point to make CodeDesc initialization less fiddly.

  static constexpr int kConstantPoolSize = 0;
  static constexpr int kBuiltinJumpTableInfoSize = 0;
  const int instruction_size = pc_offset();
  const int builtin_jump_table_info_offset =
      instruction_size - kBuiltinJumpTableInfoSize;
  const int code_comments_offset =
      builtin_jump_table_info_offset - code_comments_size;
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
                       code_comments_offset, builtin_jump_table_info_offset,
                       reloc_info_offset);
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
  // Preferred alignment of jump targets on some ARM chips.
#if !defined(V8_TARGET_OS_MACOS)
  Align(8);
#endif
}

void Assembler::CheckLabelLinkChain(Label const* label) {
#ifdef DEBUG
  if (label->is_linked()) {
    static const int kMaxLinksToCheck = 64;  // Avoid O(n2) behaviour.
    int links_checked = 0;
    int64_t linkoffset = label->pos();
    bool end_of_chain = false;
    while (!end_of_chain) {
      if (++links_checked > kMaxLinksToCheck) break;
      Instruction* link = InstructionAt(linkoffset);
      int64_t linkpcoffset = link->ImmPCOffset();
      int64_t prevlinkoffset = linkoffset + linkpcoffset;

      end_of_chain = (linkoffset == prevlinkoffset);
      linkoffset = linkoffset + linkpcoffset;
    }
  }
#endif
}

void Assembler::RemoveBranchFromLabelLinkChain(Instruction* branch,
                                               Label* label,
                                               Instruction* label_veneer) {
  DCHECK(label->is_linked());

  CheckLabelLinkChain(label);

  Instruction* link = InstructionAt(label->pos());
  Instruction* prev_link = link;
  Instruction* next_link;

  if (link != branch) {
    int i = static_cast<int>(InstructionOffset(branch));
    // Currently, we don't support adr instructions sharing labels with
    // branches in the link chain.
    DCHECK(branch_link_chain_back_edge_.contains(i));
    prev_link = InstructionAt(branch_link_chain_back_edge_.at(i));
    link = branch;
  }

  DCHECK(branch == link);
  next_link = branch->ImmPCOffsetTarget();

  if (branch == prev_link) {
    // The branch is the first instruction in the chain.
    if (branch == next_link) {
      // It is also the last instruction in the chain, so it is the only branch
      // currently referring to this label.
      //
      // Label -> this branch -> start
      label->Unuse();
    } else {
      // Label -> this branch -> 1+ branches -> start
      label->link_to(static_cast<int>(InstructionOffset(next_link)));
      branch_link_chain_back_edge_.erase(
          static_cast<int>(InstructionOffset(next_link)));
    }
  } else if (branch == next_link) {
    // The branch is the last (but not also the first) instruction in the chain.
    //
    // Label -> 1+ branches -> this branch -> start
    prev_link->SetImmPCOffsetTarget(zone(), options(), prev_link);
    branch_link_chain_back_edge_.erase(
        static_cast<int>(InstructionOffset(branch)));
  } else {
    // The branch is in the middle of the chain.
    //
    // Label -> 1+ branches -> this branch -> 1+ branches -> start
    int n = static_cast<int>(InstructionOffset(next_link));
    if (branch_link_chain_back_edge_.contains(n)) {
      // Update back edge such that the branch after this branch points to the
      // branch before it.
      branch_link_chain_back_edge_[n] =
          static_cast<int>(InstructionOffset(prev_link));
      branch_link_chain_back_edge_.erase(
          static_cast<int>(InstructionOffset(branch)));
    }

    if (prev_link->IsTargetInImmPCOffsetRange(next_link)) {
      prev_link->SetImmPCOffsetTarget(zone(), options(), next_link);
    } else if (label_veneer != nullptr) {
      // Use the veneer for all previous links in the chain.
      prev_link->SetImmPCOffsetTarget(zone(), options(), prev_link);

      bool end_of_chain = false;
      link = next_link;
      while (!end_of_chain) {
        next_link = link->ImmPCOffsetTarget();
        end_of_chain = (link == next_link);
        link->SetImmPCOffsetTarget(zone(), options(), label_veneer);
        // {link} is now resolved; remove it from {unresolved_branches_} so
        // we won't later try to process it again, which would fail because
        // by walking the chain of its label's unresolved branch instructions,
        // we won't find it: {prev_link} is now the end of that chain after
        // its update above.
        if (link->IsCondBranchImm() || link->IsCompareBranch()) {
          static_assert(Instruction::ImmBranchRange(CondBranchType) ==
                        Instruction::ImmBranchRange(CompareBranchType));
          int max_reachable_pc = static_cast<int>(InstructionOffset(link)) +
                                 Instruction::ImmBranchRange(CondBranchType);
          unresolved_branches_.erase(max_reachable_pc);
        } else if (link->IsTestBranch()) {
          // Add 1 to account for branch type tag bit.
          int max_reachable_pc = static_cast<int>(InstructionOffset(link)) +
                                 Instruction::ImmBranchRange(TestBranchType) +
                                 1;
          unresolved_branches_.erase(max_reachable_pc);
        } else {
          // Other branch types are not handled by veneers.
        }
        link = next_link;
      }
    } else {
      // The assert below will fire.
      // Some other work could be attempted to fix up the chain, but it would be
      // rather complicated. If we crash here, we may want to consider using an
      // other mechanism than a chain of branches.
      //
      // Note that this situation currently should not happen, as we always call
      // this function with a veneer to the target label.
      // However this could happen with a MacroAssembler in the following state:
      //    [previous code]
      //    B(label);
      //    [20KB code]
      //    Tbz(label);   // First tbz. Pointing to unconditional branch.
      //    [20KB code]
      //    Tbz(label);   // Second tbz. Pointing to the first tbz.
      //    [more code]
      // and this function is called to remove the first tbz from the label link
      // chain. Since tbz has a range of +-32KB, the second tbz cannot point to
      // the unconditional branch.
      CHECK(prev_link->IsTargetInImmPCOffsetRange(next_link));
      UNREACHABLE();
    }
  }

  CheckLabelLinkChain(label);
}

void Assembler::bind(Label* label) {
  // Bind label to the address at pc_. All instructions (most likely branches)
  // that are linked to this label will be updated to point to the newly-bound
  // label.

  DCHECK(!label->is_near_linked());
  DCHECK(!label->is_bound());

  DeleteUnresolvedBranchInfoForLabel(label);

  // If the label is linked, the link chain looks something like this:
  //
  // |--I----I-------I-------L
  // |---------------------->| pc_offset
  // |-------------->|         linkoffset = label->pos()
  //         |<------|         link->ImmPCOffset()
  // |------>|                 prevlinkoffset = linkoffset + link->ImmPCOffset()
  //
  // On each iteration, the last link is updated and then removed from the
  // chain until only one remains. At that point, the label is bound.
  //
  // If the label is not linked, no preparation is required before binding.
  while (label->is_linked()) {
    int linkoffset = label->pos();
    Instruction* link = InstructionAt(linkoffset);
    int prevlinkoffset = linkoffset + static_cast<int>(link->ImmPCOffset());

    CheckLabelLinkChain(label);

    DCHECK_GE(linkoffset, 0);
    DCHECK(linkoffset < pc_offset());
    DCHECK((linkoffset > prevlinkoffset) ||
           (linkoffset - prevlinkoffset == kStartOfLabelLinkChain));
    DCHECK_GE(prevlinkoffset, 0);

    // Update the link to point to the label.
    if (link->IsUnresolvedInternalReference()) {
      // Internal references do not get patched to an instruction but directly
      // to an address.
      internal_reference_positions_.push_back(linkoffset);
      memcpy(link, &pc_, kSystemPointerSize);
    } else {
      link->SetImmPCOffsetTarget(zone(), options(),
                                 reinterpret_cast<Instruction*>(pc_));

      // Discard back edge data for this link.
      branch_link_chain_back_edge_.erase(
          static_cast<int>(InstructionOffset(link)));
    }

    // Link the label to the previous link in the chain.
    if (linkoffset - prevlinkoffset == kStartOfLabelLinkChain) {
      // We hit kStartOfLabelLinkChain, so the chain is fully processed.
      label->Unuse();
    } else {
      // Update the label for the next iteration.
      label->link_to(prevlinkoffset);
    }
  }
  label->bind_to(pc_offset());

  DCHECK(label->is_bound());
  DCHECK(!label->is_linked());
}

int Assembler::LinkAndGetByteOffsetTo(Label* label) {
  DCHECK_EQ(sizeof(*pc_), 1);
  CheckLabelLinkChain(label);

  int offset;
  if (label->is_bound()) {
    // The label is bound, so it does not need to be updated. Referring
    // instructions must link directly to the label as they will not be
    // updated.
    //
    // In this case, label->pos() returns the offset of the label from the
    // start of the buffer.
    //
    // Note that offset can be zero for self-referential instructions. (This
    // could be useful for ADR, for example.)
    offset = label->pos() - pc_offset();
    DCHECK_LE(offset, 0);
  } else {
    if (label->is_linked()) {
      // The label is linked, so the referring instruction should be added onto
      // the end of the label's link chain.
      //
      // In this case, label->pos() returns the offset of the last linked
      // instruction from the start of the buffer.
      offset = label->pos() - pc_offset();
      DCHECK_NE(offset, kStartOfLabelLinkChain);
      // Note that the offset here needs to be PC-relative only so that the
      // first instruction in a buffer can link to an unbound label. Otherwise,
      // the offset would be 0 for this case, and 0 is reserved for
      // kStartOfLabelLinkChain.
    } else {
      // The label is unused, so it now becomes linked and the referring
      // instruction is at the start of the new link chain.
      offset = kStartOfLabelLinkChain;
    }
    // The instruction at pc is now the last link in the label's chain.
    label->link_to(pc_offset());
  }

  return offset;
}

void Assembler::DeleteUnresolvedBranchInfoForLabelTraverse(Label* label) {
  DCHECK(label->is_linked());
  CheckLabelLinkChain(label);

  int link_offset = label->pos();
  int link_pcoffset;
  bool end_of_chain = false;

  while (!end_of_chain) {
    Instruction* link = InstructionAt(link_offset);
    int max_reachable_pc = static_cast<int>(InstructionOffset(link));

    // ADR instructions and unconditional branches are not handled by veneers.
    if (link->IsCondBranchImm() || link->IsCompareBranch()) {
      static_assert(Instruction::ImmBranchRange(CondBranchType) ==
                    Instruction::ImmBranchRange(CompareBranchType));
      max_reachable_pc += Instruction::ImmBranchRange(CondBranchType);
      unresolved_branches_.erase(max_reachable_pc);
      link_pcoffset = link->ImmCondBranch() * kInstrSize;
    } else if (link->IsTestBranch()) {
      // Add one to account for branch type tag bit.
      max_reachable_pc += Instruction::ImmBranchRange(TestBranchType) + 1;
      unresolved_branches_.erase(max_reachable_pc);
      link_pcoffset = link->ImmTestBranch() * kInstrSize;
    } else if (link->IsUncondBranchImm()) {
      link_pcoffset = link->ImmUncondBranch() * kInstrSize;
    } else {
      link_pcoffset = static_cast<int>(link->ImmPCOffset());
    }

    end_of_chain = (link_pcoffset == 0);
    link_offset = link_offset + link_pcoffset;
  }
}

void Assembler::DeleteUnresolvedBranchInfoForLabel(Label* label) {
  if (unresolved_branches_.empty()) {
    DCHECK_EQ(next_veneer_pool_check_, kMaxInt);
    return;
  }

  if (label->is_linked()) {
    // Branches to this label will be resolved when the label is bound, normally
    // just after all the associated info has been deleted.
    DeleteUnresolvedBranchInfoForLabelTraverse(label);
  }
  if (unresolved_branches_.empty()) {
    next_veneer_pool_check_ = kMaxInt;
  } else {
    next_veneer_pool_check_ =
        unresolved_branches_first_limit() - kVeneerDistanceCheckMargin;
  }
}

bool Assembler::IsConstantPoolAt(Instruction* instr) {
  // The constant pool marker is made of two instructions. These instructions
  // will never be emitted by the JIT, so checking for the first one is enough:
  // 0: ldr xzr, #<size of pool>
  bool result = instr->IsLdrLiteralX() && (instr->Rt() == kZeroRegCode);

  // It is still worth asserting the marker is complete.
  // 4: blr xzr
  DCHECK(!result || (instr->following()->IsBranchAndLinkToRegister() &&
                     instr->following()->Rn() == kZeroRegCode));

  return result;
}

int Assembler::ConstantPoolSizeAt(Instruction* instr) {
#ifdef USE_SIMULATOR
  // Assembler::debug() embeds constants directly into the instruction stream.
  // Although this is not a genuine constant pool, treat it like one to avoid
  // disassembling the constants.
  if ((instr->Mask(ExceptionMask) == HLT) &&
      (instr->ImmException() == kImmExceptionIsDebug)) {
    const char* message = reinterpret_cast<const char*>(
        instr->InstructionAtOffset(kDebugMessageOffset));
    int size = static_cast<int>(kDebugMessageOffset + strlen(message) + 1);
    return RoundUp(size, kInstrSize) / kInstrSize;
  }
  // Same for printf support, see MacroAssembler::CallPrintf().
  if ((instr->Mask(ExceptionMask) == HLT) &&
      (instr->ImmException() == kImmExceptionIsPrintf)) {
    return kPrintfLength / kInstrSize;
  }
#endif
  if (IsConstantPoolAt(instr)) {
    return instr->ImmLLiteral();
  } else {
    return -1;
  }
}

void Assembler::EmitPoolGuard() {
  // We must generate only one instruction as this is used in scopes that
  // control the size of the code generated.
  Emit(BLR | Rn(xzr));
}

void Assembler::StartBlockVeneerPool() { ++veneer_pool_blocked_nesting_; }

void Assembler::EndBlockVeneerPool() {
  if (--veneer_pool_blocked_nesting_ == 0) {
    // Check the veneer pool hasn't been blocked for too long.
    DCHECK(unresolved_branches_.empty() ||
           (pc_offset() < unresolved_branches_first_limit()));
  }
}

void Assembler::br(const Register& xn) {
  DCHECK(xn.Is64Bits());
  Emit(BR | Rn(xn));
}

void Assembler::blr(const Register& xn) {
  DCHECK(xn.Is64Bits());
  // The pattern 'blr xzr' is used as a guard to detect when execution falls
  // through the constant pool. It should not be emitted.
  DCHECK_NE(xn, xzr);
  Emit(BLR | Rn(xn));
}

void Assembler::ret(const Register& xn) {
  DCHECK(xn.Is64Bits());
  Emit(RET | Rn(xn));
}

void Assembler::b(int imm26) { Emit(B | ImmUncondBranch(imm26)); }

void Assembler::b(Label* label) {
  b(LinkAndGetBranchInstructionOffsetTo(label));
}

void Assembler::b(int imm19, Condition cond) {
  Emit(B_cond | ImmCondBranch(imm19) | cond);
}

void Assembler::b(Label* label, Condition cond) {
  b(LinkAndGetBranchInstructionOffsetTo(label), cond);
}

void Assembler::bl(int imm26) { Emit(BL | ImmUncondBranch(imm26)); }

void Assembler::bl(Label* label) {
  bl(LinkAndGetBranchInstructionOffsetTo(label));
}

void Assembler::cbz(const Register& rt, int imm19) {
  Emit(SF(rt) | CBZ | ImmCmpBranch(imm19) | Rt(rt));
}

void Assembler::cbz(const Register& rt, Label* label) {
  cbz(rt, LinkAndGetBranchInstructionOffsetTo(label));
}

void Assembler::cbnz(const Register& rt, int imm19) {
  Emit(SF(rt) | CBNZ | ImmCmpBranch(imm19) | Rt(rt));
}

void Assembler::cbnz(const Register& rt, Label* label) {
  cbnz(rt, LinkAndGetBranchInstructionOffsetTo(label));
}

void Assembler::tbz(const Register& rt, unsigned bit_pos, int imm14) {
  DCHECK(rt.Is64Bits() || (rt.Is32Bits() && (bit_pos < kWRegSizeInBits)));
  Emit(TBZ | ImmTestBranchBit(bit_pos) | ImmTestBranch(imm14) | Rt(rt));
}

void Assembler::tbz(const Register& rt, unsigned bit_pos, Label* label) {
  tbz(rt, bit_pos, LinkAndGetBranchInstructionOffsetTo(label));
}

void Assembler::tbnz(const Register& rt, unsigned bit_pos, int imm14) {
  DCHECK(rt.Is64Bits() || (rt.Is32Bits() && (bit_pos < kWRegSizeInBits)));
  Emit(TBNZ | ImmTestBranchBit(bit_pos) | ImmTestBranch(imm14) | Rt(rt));
}

void Assembler::tbnz(const Register& rt, unsigned bit_pos, Label* label) {
  tbnz(rt, bit_pos, LinkAndGetBranchInstructionOffsetTo(label));
}

void Assembler::adr(const Register& rd, int imm21) {
  DCHECK(rd.Is64Bits());
  Emit(ADR | ImmPCRelAddress(imm21) | Rd(rd));
}

void Assembler::adr(const Register& rd, Label* label) {
  adr(rd, LinkAndGetByteOffsetTo(label));
}

void Assembler::nop(NopMarkerTypes n) {
  DCHECK((FIRST_NOP_MARKER <= n) && (n <= LAST_NOP_MARKER));
  mov(Register::XRegFromCode(n), Register::XRegFromCode(n));
}

void Assembler::add(const Register& rd, const Register& rn,
                    const Operand& operand) {
  AddSub(rd, rn, operand, LeaveFlags, ADD);
}

void Assembler::adds(const Register& rd, const Register& rn,
                     const Operand& operand) {
  AddSub(rd, rn, operand, SetFlags, ADD);
}

void Assembler::cmn(const Register& rn, const Operand& operand) {
  Register zr = AppropriateZeroRegFor(rn);
  adds(zr, rn, operand);
}

void Assembler::sub(const Register& rd, const Register& rn,
                    const Operand& operand) {
  AddSub(rd, rn, operand, LeaveFlags, SUB);
}

void Assembler::subs(const Register& rd, const Register& rn,
                     const Operand& operand) {
  AddSub(rd, rn, operand, SetFlags, SUB);
}

void Assembler::cmp(const Register& rn, const Operand& operand) {
  Register zr = AppropriateZeroRegFor(rn);
  subs(zr, rn, operand);
}

void Assembler::neg(const Register& rd, const Operand& operand) {
  Register zr = AppropriateZeroRegFor(rd);
  sub(rd, zr, operand);
}

void Assembler::negs(const Register& rd, const Operand& operand) {
  Register zr = AppropriateZeroRegFor(rd);
  subs(rd, zr, operand);
}

void Assembler::adc(const Register& rd, const Register& rn,
                    const Operand& operand) {
  AddSubWithCarry(rd, rn, operand, LeaveFlags, ADC);
}

void Assembler::adcs(const Register& rd, const Register& rn,
                     const Operand& operand) {
  AddSubWithCarry(rd, rn, operand, SetFlags, ADC);
}

void Assembler::sbc(const Register& rd, const Register& rn,
                    const Operand& operand) {
  AddSubWithCarry(rd, rn, operand, LeaveFlags, SBC);
}

void Assembler::sbcs(const Register& rd, const Register& rn,
                     const Operand& operand) {
  AddSubWithCarry(rd, rn, operand, SetFlags, SBC);
}

void Assembler::ngc(const Register& rd, const Operand& operand) {
  Register zr = AppropriateZeroRegFor(rd);
  sbc(rd, zr, operand);
}

void Assembler::ngcs(const Register& rd, const Operand& operand) {
  Register zr = AppropriateZeroRegFor(rd);
  sbcs(rd, zr, operand);
}

// Logical instructions.
void Assembler::and_(const Register& rd, const Register& rn,
                     const Operand& operand) {
  Logical(rd, rn, operand, AND);
}

void Assembler::ands(const Register& rd, const Register& rn,
                     const Operand& operand) {
  Logical(rd, rn, operand, ANDS);
}

void Assembler::tst(const Register& rn, const Operand& operand) {
  ands(AppropriateZeroRegFor(rn), rn, operand);
}

void Assembler::bic(const Register& rd, const Register& rn,
                    const Operand& operand) {
  Logical(rd, rn, operand, BIC);
}

void Assembler::bics(const Register& rd, const Register& rn,
                     const Operand& operand) {
  Logical(rd, rn, operand, BICS);
}

void Assembler::orr(const Register& rd, const Register& rn,
                    const Operand& operand) {
  Logical(rd, rn, operand, ORR);
}

void Assembler::orn(const Register& rd, const Register& rn,
                    const Operand& operand) {
  Logical(rd, rn, operand, ORN);
}

void Assembler::eor(const Register& rd, const Register& rn,
                    const Operand& operand) {
  Logical(rd, rn, operand, EOR);
}

void Assembler::eon(const Register& rd, const Register& rn,
                    const Operand& operand) {
  Logical(rd, rn, operand, EON);
}

void Assembler::lslv(const Register& rd, const Register& rn,
                     const Register& rm) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(rd.SizeInBits() == rm.SizeInBits());
  Emit(SF(rd) | LSLV | Rm(rm) | Rn(rn) | Rd(rd));
}

void Assembler::lsrv(const Register& rd, const Register& rn,
                     const Register& rm) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(rd.SizeInBits() == rm.SizeInBits());
  Emit(SF(rd) | LSRV | Rm(rm) | Rn(rn) | Rd(rd));
}

void Assembler::asrv(const Register& rd, const Register& rn,
                     const Register& rm) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(rd.SizeInBits() == rm.SizeInBits());
  Emit(SF(rd) | ASRV | Rm(rm) | Rn(rn) | Rd(rd));
}

void Assembler::rorv(const Register& rd, const Register& rn,
                     const Register& rm) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(rd.SizeInBits() == rm.SizeInBits());
  Emit(SF(rd) | RORV | Rm(rm) | Rn(rn) | Rd(rd));
}

// Bitfield operations.
void Assembler::bfm(const Register& rd, const Register& rn, int immr,
                    int imms) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  Instr N = SF(rd) >> (kSFOffset - kBitfieldNOffset);
  Emit(SF(rd) | BFM | N | ImmR(immr, rd.SizeInBits()) |
       ImmS(imms, rn.SizeInBits()) | Rn(rn) | Rd(rd));
}

void Assembler::sbfm(const Register& rd, const Register& rn, int immr,
                     int imms) {
  DCHECK(rd.Is64Bits() || rn.Is32Bits());
  Instr N = SF(rd) >> (kSFOffset - kBitfieldNOffset);
  Emit(SF(rd) | SBFM | N | ImmR(immr, rd.SizeInBits()) |
       ImmS(imms, rn.SizeInBits()) | Rn(rn) | Rd(rd));
}

void Assembler::ubfm(const Register& rd, const Register& rn, int immr,
                     int imms) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  Instr N = SF(rd) >> (kSFOffset - kBitfieldNOffset);
  Emit(SF(rd) | UBFM | N | ImmR(immr, rd.SizeInBits()) |
       ImmS(imms, rn.SizeInBits()) | Rn(rn) | Rd(rd));
}

void Assembler::extr(const Register& rd, const Register& rn, const Register& rm,
                     int lsb) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(rd.SizeInBits() == rm.SizeInBits());
  Instr N = SF(rd) >> (kSFOffset - kBitfieldNOffset);
  Emit(SF(rd) | EXTR | N | Rm(rm) | ImmS(lsb, rn.SizeInBits()) | Rn(rn) |
       Rd(rd));
}

void Assembler::csel(const Register& rd, const Register& rn, const Register& rm,
                     Condition cond) {
  ConditionalSelect(rd, rn, rm, cond, CSEL);
}

void Assembler::csinc(const Register& rd, const Register& rn,
                      const Register& rm, Condition cond) {
  ConditionalSelect(rd, rn, rm, cond, CSINC);
}

void Assembler::csinv(const Register& rd, const Register& rn,
                      const Register& rm, Condition cond) {
  ConditionalSelect(rd, rn, rm, cond, CSINV);
}

void Assembler::csneg(const Register& rd, const Register& rn,
                      const Register& rm, Condition cond) {
  ConditionalSelect(rd, rn, rm, cond, CSNEG);
}

void Assembler::cset(const Register& rd, Condition cond) {
  DCHECK((cond != al) && (cond != nv));
  Register zr = AppropriateZeroRegFor(rd);
  csinc(rd, zr, zr, NegateCondition(cond));
}

void Assembler::csetm(const Register& rd, Condition cond) {
  DCHECK((cond != al) && (cond != nv));
  Register zr = AppropriateZeroRegFor(rd);
  csinv(rd, zr, zr, NegateCondition(cond));
}

void Assembler::cinc(const Register& rd, const Register& rn, Condition cond) {
  DCHECK((cond != al) && (cond != nv));
  csinc(rd, rn, rn, NegateCondition(cond));
}

void Assembler::cinv(const Register& rd, const Register& rn, Condition cond) {
  DCHECK((cond != al) && (cond != nv));
  csinv(rd, rn, rn, NegateCondition(cond));
}

void Assembler::cneg(const Register& rd, const Register& rn, Condition cond) {
  DCHECK((cond != al) && (cond != nv));
  csneg(rd, rn, rn, NegateCondition(cond));
}

void Assembler::ConditionalSelect(const Register& rd, const Register& rn,
                                  const Register& rm, Condition cond,
                                  ConditionalSelectOp op) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(rd.SizeInBits() == rm.SizeInBits());
  Emit(SF(rd) | op | Rm(rm) | Cond(cond) | Rn(rn) | Rd(rd));
}

void Assembler::ccmn(const Register& rn, const Operand& operand,
                     StatusFlags nzcv, Condition cond) {
  ConditionalCompare(rn, operand, nzcv, cond, CCMN);
}

void Assembler::ccmp(const Register& rn, const Operand& operand,
                     StatusFlags nzcv, Condition cond) {
  ConditionalCompare(rn, operand, nzcv, cond, CCMP);
}

void Assembler::DataProcessing3Source(const Register& rd, const Register& rn,
                                      const Register& rm, const Register& ra,
                                      DataProcessing3SourceOp op) {
  Emit(SF(rd) | op | Rm(rm) | Ra(ra) | Rn(rn) | Rd(rd));
}

void Assembler::mul(const Register& rd, const Register& rn,
                    const Register& rm) {
  DCHECK(AreSameSizeAndType(rd, rn, rm));
  Register zr = AppropriateZeroRegFor(rn);
  DataProcessing3Source(rd, rn, rm, zr, MADD);
}

void Assembler::madd(const Register& rd, const Register& rn, const Register& rm,
                     const Register& ra) {
  DCHECK(AreSameSizeAndType(rd, rn, rm, ra));
  DataProcessing3Source(rd, rn, rm, ra, MADD);
}

void Assembler::mneg(const Register& rd, const Register& rn,
                     const Register& rm) {
  DCHECK(AreSameSizeAndType(rd, rn, rm));
  Register zr = AppropriateZeroRegFor(rn);
  DataProcessing3Source(rd, rn, rm, zr, MSUB);
}

void Assembler::msub(const Register& rd, const Register& rn, const Register& rm,
                     const Register& ra) {
  DCHECK(AreSameSizeAndType(rd, rn, rm, ra));
  DataProcessing3Source(rd, rn, rm, ra, MSUB);
}

void Assembler::smaddl(const Register& rd, const Register& rn,
                       const Register& rm, const Register& ra) {
  DCHECK(rd.Is64Bits() && ra.Is64Bits());
  DCHECK(rn.Is32Bits() && rm.Is32Bits());
  DataProcessing3Source(rd, rn, rm, ra, SMADDL_x);
}

void Assembler::smsubl(const Register& rd, const Register& rn,
                       const Register& rm, const Register& ra) {
  DCHECK(rd.Is64Bits() && ra.Is64Bits());
  DCHECK(rn.Is32Bits() && rm.Is32Bits());
  DataProcessing3Source(rd, rn, rm, ra, SMSUBL_x);
}

void Assembler::umaddl(const Register& rd, const Register& rn,
                       const Register& rm, const Register& ra) {
  DCHECK(rd.Is64Bits() && ra.Is64Bits());
  DCHECK(rn.Is32Bits() && rm.Is32Bits());
  DataProcessing3Source(rd, rn, rm, ra, UMADDL_x);
}

void Assembler::umsubl(const Register& rd, const Register& rn,
                       const Register& rm, const Register& ra) {
  DCHECK(rd.Is64Bits() && ra.Is64Bits());
  DCHECK(rn.Is32Bits() && rm.Is32Bits());
  DataProcessing3Source(rd, rn, rm, ra, UMSUBL_x);
}

void Assembler::smull(const Register& rd, const Register& rn,
                      const Register& rm) {
  DCHECK(rd.Is64Bits());
  DCHECK(rn.Is32Bits() && rm.Is32Bits());
  DataProcessing3Source(rd, rn, rm, xzr, SMADDL_x);
}

void Assembler::smulh(const Register& rd, const Register& rn,
                      const Register& rm) {
  DCHECK(rd.Is64Bits());
  DCHECK(rn.Is64Bits());
  DCHECK(rm.Is64Bits());
  DataProcessing3Source(rd, rn, rm, xzr, SMULH_x);
}

void Assembler::umulh(const Register& rd, const Register& rn,
                      const Register& rm) {
  DCHECK(rd.Is64Bits());
  DCHECK(rn.Is64Bits());
  DCHECK(rm.Is64Bits());
  DataProcessing3Source(rd, rn, rm, xzr, UMULH_x);
}

void Assembler::sdiv(const Register& rd, const Register& rn,
                     const Register& rm) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(rd.SizeInBits() == rm.SizeInBits());
  Emit(SF(rd) | SDIV | Rm(rm) | Rn(rn) | Rd(rd));
}

void Assembler::udiv(const Register& rd, const Register& rn,
                     const Register& rm) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(rd.SizeInBits() == rm.SizeInBits());
  Emit(SF(rd) | UDIV | Rm(rm) | Rn(rn) | Rd(rd));
}

void Assembler::rbit(const Register& rd, const Register& rn) {
  DataProcessing1Source(rd, rn, RBIT);
}

void Assembler::rev16(const Register& rd, const Register& rn) {
  DataProcessing1Source(rd, rn, REV16);
}

void Assembler::rev32(const Register& rd, const Register& rn) {
  DCHECK(rd.Is64Bits());
  DataProcessing1Source(rd, rn, REV);
}

void Assembler::rev(const Register& rd, const Register& rn) {
  DataProcessing1Source(rd, rn, rd.Is64Bits() ? REV_x : REV_w);
}

void Assembler::clz(const Register& rd, const Register& rn) {
  DataProcessing1Source(rd, rn, CLZ);
}

void Assembler::cls(const Register& rd, const Register& rn) {
  DataProcessing1Source(rd, rn, CLS);
}

void Assembler::pacib1716() { Emit(PACIB1716); }
void Assembler::autib1716() { Emit(AUTIB1716); }
void Assembler::pacibsp() { Emit(PACIBSP); }
void Assembler::autibsp() { Emit(AUTIBSP); }

void Assembler::bti(BranchTargetIdentifier id) {
  SystemHint op;
  switch (id) {
    case BranchTargetIdentifier::kBti:
      op = BTI;
      break;
    case BranchTargetIdentifier::kBtiCall:
      op = BTI_c;
      break;
    case BranchTargetIdentifier::kBtiJump:
      op = BTI_j;
      break;
    case BranchTargetIdentifier::kBtiJumpCall:
      op = BTI_jc;
      break;
    case BranchTargetIdentifier::kNone:
    case BranchTargetIdentifier::kPacibsp:
      // We always want to generate a BTI instruction here, so disallow
      // skipping its generation or generating a PACIBSP instead.
      UNREACHABLE();
  }
  hint(op);
}

void Assembler::ldp(const CPURegister& rt, const CPURegister& rt2,
                    const MemOperand& src) {
  LoadStorePair(rt, rt2, src, LoadPairOpFor(rt, rt2));
}

void Assembler::stp(const CPURegister& rt, const CPURegister& rt2,
                    const MemOperand& dst) {
  LoadStorePair(rt, rt2, dst, StorePairOpFor(rt, rt2));

#if defined(V8_OS_WIN)
  if (xdata_encoder_ && rt == x29 && rt2 == lr && dst.base().IsSP()) {
    xdata_encoder_->onSaveFpLr();
  }
#endif
}

void Assembler::ldpsw(const Register& rt, const Register& rt2,
                      const MemOperand& src) {
  DCHECK(rt.Is64Bits());
  LoadStorePair(rt, rt2, src, LDPSW_x);
}

void Assembler::LoadStorePair(const CPURegister& rt, const CPURegister& rt2,
                              const MemOperand& addr, LoadStorePairOp op) {
  // 'rt' and 'rt2' can only be aliased for stores.
  DCHECK(((op & LoadStorePairLBit) == 0) || rt != rt2);
  DCHECK(AreSameSizeAndType(rt, rt2));
  DCHECK(IsImmLSPair(addr.offset(), CalcLSPairDataSize(op)));
  int offset = static_cast<int>(addr.offset());

  Instr memop = op | Rt(rt) | Rt2(rt2) | RnSP(addr.base()) |
                ImmLSPair(offset, CalcLSPairDataSize(op));

  Instr addrmodeop;
  if (addr.IsImmediateOffset()) {
    addrmodeop = LoadStorePairOffsetFixed;
  } else {
    // Pre-index and post-index modes.
    DCHECK_NE(rt, addr.base());
    DCHECK_NE(rt2, addr.base());
    DCHECK_NE(addr.offset(), 0);
    if (addr.IsPreIndex()) {
      addrmodeop = LoadStorePairPreIndexFixed;
    } else {
      DCHECK(addr.IsPostIndex());
      addrmodeop = LoadStorePairPostIndexFixed;
    }
  }
  Emit(addrmodeop | memop);
}

// Memory instructions.
void Assembler::ldrb(const Register& rt, const MemOperand& src) {
  LoadStore(rt, src, LDRB_w);
}

void Assembler::strb(const Register& rt, const MemOperand& dst) {
  LoadStore(rt, dst, STRB_w);
}

void Assembler::ldrsb(const Register& rt, const MemOperand& src) {
  LoadStore(rt, src, rt.Is64Bits() ? LDRSB_x : LDRSB_w);
}

void Assembler::ldrh(const Register& rt, const MemOperand& src) {
  LoadStore(rt, src, LDRH_w);
}

void Assembler::strh(const Register& rt, const MemOperand& dst) {
  LoadStore(rt, dst, STRH_w);
}

void Assembler::ldrsh(const Register& rt, const MemOperand& src) {
  LoadStore(rt, src, rt.Is64Bits() ? LDRSH_x : LDRSH_w);
}

void Assembler::ldr(const CPURegister& rt, const MemOperand& src) {
  LoadStore(rt, src, LoadOpFor(rt));
}

void Assembler::str(const CPURegister& rt, const MemOperand& src) {
  LoadStore(rt, src, StoreOpFor(rt));
}

void Assembler::ldrsw(const Register& rt, const MemOperand& src) {
  DCHECK(rt.Is64Bits());
  LoadStore(rt, src, LDRSW_x);
}

void Assembler::ldr_pcrel(const CPURegister& rt, int imm19) {
  // The pattern 'ldr xzr, #offset' is used to indicate the beginning of a
  // constant pool. It should not be emitted.
  DCHECK(!rt.IsZero());
  Emit(LoadLiteralOpFor(rt) | ImmLLiteral(imm19) | Rt(rt));
}

Operand Operand::EmbeddedNumber(double number) {
  int32_t smi;
  if (DoubleToSmiInteger(number, &smi)) {
    return Operand(Immediate(Smi::FromInt(smi)));
  }
  return EmbeddedHeapNumber(number);
}

Operand Operand::EmbeddedHeapNumber(double number) {
  Operand result(0, RelocInfo::FULL_EMBEDDED_OBJECT);
  result.heap_number_request_.emplace(number);
  DCHECK(result.IsHeapNumberRequest());
  return result;
}

void Assembler::ldr(const CPURegister& rt, const Operand& operand) {
  if (operand.IsHeapNumberRequest()) {
    BlockPoolsScope no_pool_before_ldr_of_heap_number_request(this);
    RequestHeapNumber(operand.heap_number_request());
    ldr(rt, operand.immediate_for_heap_number_request());
  } else {
    ldr(rt, operand.immediate());
  }
}

void Assembler::ldr(const CPURegister& rt, const Immediate& imm) {
  BlockPoolsScope no_pool_before_ldr_pcrel_instr(this);
  RecordRelocInfo(imm.rmode(), imm.value());
  // The load will be patched when the constpool is emitted, patching code
  // expect a load literal with offset 0.
  ldr_pcrel(rt, 0);
}

void Assembler::ldar(const Register& rt, const Register& rn) {
  DCHECK(rn.Is64Bits());
  LoadStoreAcquireReleaseOp op = rt.Is32Bits() ? LDAR_w : LDAR_x;
  Emit(op | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::ldaxr(const Register& rt, const Register& rn) {
  DCHECK(rn.Is64Bits());
  LoadStoreAcquireReleaseOp op = rt.Is32Bits() ? LDAXR_w : LDAXR_x;
  Emit(op | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::stlr(const Register& rt, const Register& rn) {
  DCHECK(rn.Is64Bits());
  LoadStoreAcquireReleaseOp op = rt.Is32Bits() ? STLR_w : STLR_x;
  Emit(op | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::stlxr(const Register& rs, const Register& rt,
                      const Register& rn) {
  DCHECK(rn.Is64Bits());
  DCHECK(rs != rt && rs != rn);
  LoadStoreAcquireReleaseOp op = rt.Is32Bits() ? STLXR_w : STLXR_x;
  Emit(op | Rs(rs) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::ldarb(const Register& rt, const Register& rn) {
  DCHECK(rt.Is32Bits());
  DCHECK(rn.Is64Bits());
  Emit(LDAR_b | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::ldaxrb(const Register& rt, const Register& rn) {
  DCHECK(rt.Is32Bits());
  DCHECK(rn.Is64Bits());
  Emit(LDAXR_b | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::stlrb(const Register& rt, const Register& rn) {
  DCHECK(rt.Is32Bits());
  DCHECK(rn.Is64Bits());
  Emit(STLR_b | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::stlxrb(const Register& rs, const Register& rt,
                       const Register& rn) {
  DCHECK(rs.Is32Bits());
  DCHECK(rt.Is32Bits());
  DCHECK(rn.Is64Bits());
  DCHECK(rs != rt && rs != rn);
  Emit(STLXR_b | Rs(rs) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::ldarh(const Register& rt, const Register& rn) {
  DCHECK(rt.Is32Bits());
  DCHECK(rn.Is64Bits());
  Emit(LDAR_h | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::ldaxrh(const Register& rt, const Register& rn) {
  DCHECK(rt.Is32Bits());
  DCHECK(rn.Is64Bits());
  Emit(LDAXR_h | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::stlrh(const Register& rt, const Register& rn) {
  DCHECK(rt.Is32Bits());
  DCHECK(rn.Is64Bits());
  Emit(STLR_h | Rs(x31) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

void Assembler::stlxrh(const Register& rs, const Register& rt,
                       const Register& rn) {
  DCHECK(rs.Is32Bits());
  DCHECK(rt.Is32Bits());
  DCHECK(rn.Is64Bits());
  DCHECK(rs != rt && rs != rn);
  Emit(STLXR_h | Rs(rs) | Rt2(x31) | RnSP(rn) | Rt(rt));
}

#define COMPARE_AND_SWAP_W_X_LIST(V) \
  V(cas, CAS)                        \
  V(casa, CASA)                      \
  V(casl, CASL)                      \
  V(casal, CASAL)

#define DEFINE_ASM_FUNC(FN, OP)                                     \
  void Assembler::FN(const Register& rs, const Register& rt,        \
                     const MemOperand& src) {                       \
    DCHECK(IsEnabled(LSE));                                         \
    DCHECK(src.IsImmediateOffset() && (src.offset() == 0));         \
    LoadStoreAcquireReleaseOp op = rt.Is64Bits() ? OP##_x : OP##_w; \
    Emit(op | Rs(rs) | Rt(rt) | Rt2_mask | RnSP(src.base()));       \
  }
COMPARE_AND_SWAP_W_X_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

#define COMPARE_AND_SWAP_W_LIST(V) \
  V(casb, CASB)                    \
  V(casab, CASAB)                  \
  V(caslb, CASLB)                  \
  V(casalb, CASALB)                \
  V(cash, CASH)                    \
  V(casah, CASAH)                  \
  V(caslh, CASLH)                  \
  V(casalh, CASALH)

#define DEFINE_ASM_FUNC(FN, OP)                               \
  void Assembler::FN(const Register& rs, const Register& rt,  \
                     const MemOperand& src) {                 \
    DCHECK(IsEnabled(LSE));                                   \
    DCHECK(src.IsImmediateOffset() && (src.offset() == 0));   \
    Emit(OP | Rs(rs) | Rt(rt) | Rt2_mask | RnSP(src.base())); \
  }
COMPARE_AND_SWAP_W_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

#define COMPARE_AND_SWAP_PAIR_LIST(V) \
  V(casp, CASP)                       \
  V(caspa, CASPA)                     \
  V(caspl, CASPL)                     \
  V(caspal, CASPAL)

#define DEFINE_ASM_FUNC(FN, OP)                                     \
  void Assembler::FN(const Register& rs, const Register& rs1,       \
                     const Register& rt, const Register& rt1,       \
                     const MemOperand& src) {                       \
    DCHECK(IsEnabled(LSE));                                         \
    DCHECK(src.IsImmediateOffset() && (src.offset() == 0));         \
    DCHECK(AreEven(rs, rt));                                        \
    DCHECK(AreConsecutive(rs, rs1));                                \
    DCHECK(AreConsecutive(rt, rt1));                                \
    DCHECK(AreSameFormat(rs, rs1, rt, rt1));                        \
    LoadStoreAcquireReleaseOp op = rt.Is64Bits() ? OP##_x : OP##_w; \
    Emit(op | Rs(rs) | Rt(rt) | Rt2_mask | RnSP(src.base()));       \
  }
COMPARE_AND_SWAP_PAIR_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

// These macros generate all the variations of the atomic memory operations,
// e.g. ldadd, ldadda, ldaddb, staddl, etc.
// For a full list of the methods with comments, see the assembler header file.

#define ATOMIC_MEMORY_SIMPLE_OPERATION_LIST(V, DEF) \
  V(DEF, add, LDADD)                                \
  V(DEF, clr, LDCLR)                                \
  V(DEF, eor, LDEOR)                                \
  V(DEF, set, LDSET)                                \
  V(DEF, smax, LDSMAX)                              \
  V(DEF, smin, LDSMIN)                              \
  V(DEF, umax, LDUMAX)                              \
  V(DEF, umin, LDUMIN)

#define ATOMIC_MEMORY_STORE_MODES(V, NAME, OP) \
  V(NAME, OP##_x, OP##_w)                      \
  V(NAME##l, OP##L_x, OP##L_w)                 \
  V(NAME##b, OP##B, OP##B)                     \
  V(NAME##lb, OP##LB, OP##LB)                  \
  V(NAME##h, OP##H, OP##H)                     \
  V(NAME##lh, OP##LH, OP##LH)

#define ATOMIC_MEMORY_LOAD_MODES(V, NAME, OP) \
  ATOMIC_MEMORY_STORE_MODES(V, NAME, OP)      \
  V(NAME##a, OP##A_x, OP##A_w)                \
  V(NAME##al, OP##AL_x, OP##AL_w)             \
  V(NAME##ab, OP##AB, OP##AB)                 \
  V(NAME##alb, OP##ALB, OP##ALB)              \
  V(NAME##ah, OP##AH, OP##AH)                 \
  V(NAME##alh, OP##ALH, OP##ALH)

#define DEFINE_ASM_LOAD_FUNC(FN, OP_X, OP_W)                     \
  void Assembler::ld##FN(const Register& rs, const Register& rt, \
                         const MemOperand& src) {                \
    DCHECK(IsEnabled(LSE));                                      \
    DCHECK(src.IsImmediateOffset() && (src.offset() == 0));      \
    AtomicMemoryOp op = rt.Is64Bits() ? OP_X : OP_W;             \
    Emit(op | Rs(rs) | Rt(rt) | RnSP(src.base()));               \
  }
#define DEFINE_ASM_STORE_FUNC(FN, OP_X, OP_W)                         \
  void Assembler::st##FN(const Register& rs, const MemOperand& src) { \
    DCHECK(IsEnabled(LSE));                                           \
    ld##FN(rs, AppropriateZeroRegFor(rs), src);                       \
  }

ATOMIC_MEMORY_SIMPLE_OPERATION_LIST(ATOMIC_MEMORY_LOAD_MODES,
                                    DEFINE_ASM_LOAD_FUNC)
ATOMIC_MEMORY_SIMPLE_OPERATION_LIST(ATOMIC_MEMORY_STORE_MODES,
                                    DEFINE_ASM_STORE_FUNC)

#define DEFINE_ASM_SWP_FUNC(FN, OP_X, OP_W)                  \
  void Assembler::FN(const Register& rs, const Register& rt, \
                     const MemOperand& src) {                \
    DCHECK(IsEnabled(LSE));                                  \
    DCHECK(src.IsImmediateOffset() && (src.offset() == 0));  \
    AtomicMemoryOp op = rt.Is64Bits() ? OP_X : OP_W;         \
    Emit(op | Rs(rs) | Rt(rt) | RnSP(src.base()));           \
  }

ATOMIC_MEMORY_LOAD_MODES(DEFINE_ASM_SWP_FUNC, swp, SWP)

#undef DEFINE_ASM_LOAD_FUNC
#undef DEFINE_ASM_STORE_FUNC
#undef DEFINE_ASM_SWP_FUNC

void Assembler::sdot(const VRegister& vd, const VRegister& vn,
                     const VRegister& vm) {
  DCHECK(IsEnabled(DOTPROD));
  DCHECK((vn.Is16B() && vd.Is4S()) || (vn.Is8B() && vd.Is2S()));
  DCHECK(AreSameFormat(vn, vm));
  Emit(VFormat(vd) | NEON_SDOT | Rm(vm) | Rn(vn) | Rd(vd));
}

void Assembler::NEON3DifferentL(const VRegister& vd, const VRegister& vn,
                                const VRegister& vm, NEON3DifferentOp vop) {
  DCHECK(AreSameFormat(vn, vm));
  DCHECK((vn.Is1H() && vd.Is1S()) || (vn.Is1S() && vd.Is1D()) ||
         (vn.Is8B() && vd.Is8H()) || (vn.Is4H() && vd.Is4S()) ||
         (vn.Is2S() && vd.Is2D()) || (vn.Is16B() && vd.Is8H()) ||
         (vn.Is8H() && vd.Is4S()) || (vn.Is4S() && vd.Is2D()));
  Instr format, op = vop;
  if (vd.IsScalar()) {
    op |= NEON_Q | NEONScalar;
    format = SFormat(vn);
  } else {
    format = VFormat(vn);
  }
  Emit(format | op | Rm(vm) | Rn(vn) | Rd(vd));
}

void Assembler::NEON3DifferentW(const VRegister& vd, const VRegister& vn,
                                const VRegister& vm, NEON3DifferentOp vop) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK((vm.Is8B() && vd.Is8H()) || (vm.Is4H() && vd.Is4S()) ||
         (vm.Is2S() && vd.Is2D()) || (vm.Is16B() && vd.Is8H()) ||
         (vm.Is8H() && vd.Is4S()) || (vm.Is4S() && vd.Is2D()));
  Emit(VFormat(vm) | vop | Rm(vm) | Rn(vn) | Rd(vd));
}

void Assembler::NEON3DifferentHN(const VRegister& vd, const VRegister& vn,
                                 const VRegister& vm, NEON3DifferentOp vop) {
  DCHECK(AreSameFormat(vm, vn));
  DCHECK((vd.Is8B() && vn.Is8H()) || (vd.Is4H() && vn.Is4S()) ||
         (vd.Is2S() && vn.Is2D()) || (vd.Is16B() && vn.Is8H()) ||
         (vd.Is8H() && vn.Is4S()) || (vd.Is4S() && vn.Is2D()));
  Emit(VFormat(vd) | vop | Rm(vm) | Rn(vn) | Rd(vd));
}

#define NEON_3DIFF_LONG_LIST(V)                                                \
  V(saddl, NEON_SADDL, vn.IsVector() && vn.IsD())                              \
  V(saddl2, NEON_SADDL2, vn.IsVector() && vn.IsQ())                            \
  V(sabal, NEON_SABAL, vn.IsVector() && vn.IsD())                              \
  V(sabal2, NEON_SABAL2, vn.IsVector() && vn.IsQ())                            \
  V(uabal, NEON_UABAL, vn.IsVector() && vn.IsD())                              \
  V(uabal2, NEON_UABAL2, vn.IsVector() && vn.IsQ())                            \
  V(sabdl, NEON_SABDL, vn.IsVector() && vn.IsD())                              \
  V(sabdl2, NEON_SABDL2, vn.IsVector() && vn.IsQ())                            \
  V(uabdl, NEON_UABDL, vn.IsVector() && vn.IsD())                              \
  V(uabdl2, NEON_UABDL2, vn.IsVector() && vn.IsQ())                            \
  V(smlal, NEON_SMLAL, vn.IsVector() && vn.IsD())                              \
  V(smlal2, NEON_SMLAL2, vn.IsVector() && vn.IsQ())                            \
  V(umlal, NEON_UMLAL, vn.IsVector() && vn.IsD())                              \
  V(umlal2, NEON_UMLAL2, vn.IsVector() && vn.IsQ())                            \
  V(smlsl, NEON_SMLSL, vn.IsVector() && vn.IsD())                              \
  V(smlsl2, NEON_SMLSL2, vn.IsVector() && vn.IsQ())                            \
  V(umlsl, NEON_UMLSL, vn.IsVector() && vn.IsD())                              \
  V(umlsl2, NEON_UMLSL2, vn.IsVector() && vn.IsQ())                            \
  V(smull, NEON_SMULL, vn.IsVector() && vn.IsD())                              \
  V(smull2, NEON_SMULL2, vn.IsVector() && vn.IsQ())                            \
  V(umull, NEON_UMULL, vn.IsVector() && vn.IsD())                              \
  V(umull2, NEON_UMULL2, vn.IsVector() && vn.IsQ())                            \
  V(ssubl, NEON_SSUBL, vn.IsVector() && vn.IsD())                              \
  V(ssubl2, NEON_SSUBL2, vn.IsVector() && vn.IsQ())                            \
  V(uaddl, NEON_UADDL, vn.IsVector() && vn.IsD())                              \
  V(uaddl2, NEON_UADDL2, vn.IsVector() && vn.IsQ())                            \
  V(usubl, NEON_USUBL, vn.IsVector() && vn.IsD())                              \
  V(usubl2, NEON_USUBL2, vn.IsVector() && vn.IsQ())                            \
  V(sqdmlal, NEON_SQDMLAL, vn.Is1H() || vn.Is1S() || vn.Is4H() || vn.Is2S())   \
  V(sqdmlal2, NEON_SQDMLAL2, vn.Is1H() || vn.Is1S() || vn.Is8H() || vn.Is4S()) \
  V(sqdmlsl, NEON_SQDMLSL, vn.Is1H() || vn.Is1S() || vn.Is4H() || vn.Is2S())   \
  V(sqdmlsl2, NEON_SQDMLSL2, vn.Is1H() || vn.Is1S() || vn.Is8H() || vn.Is4S()) \
  V(sqdmull, NEON_SQDMULL, vn.Is1H() || vn.Is1S() || vn.Is4H() || vn.Is2S())   \
  V(sqdmull2, NEON_SQDMULL2, vn.Is1H() || vn.Is1S() || vn.Is8H() || vn.Is4S())

#define DEFINE_ASM_FUNC(FN, OP, AS)                            \
  void Assembler::FN(const VRegister& vd, const VRegister& vn, \
                     const VRegister& vm) {                    \
    DCHECK(AS);                                                \
    NEON3DifferentL(vd, vn, vm, OP);                           \
  }
NEON_3DIFF_LONG_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

#define NEON_3DIFF_HN_LIST(V)        \
  V(addhn, NEON_ADDHN, vd.IsD())     \
  V(addhn2, NEON_ADDHN2, vd.IsQ())   \
  V(raddhn, NEON_RADDHN, vd.IsD())   \
  V(raddhn2, NEON_RADDHN2, vd.IsQ()) \
  V(subhn, NEON_SUBHN, vd.IsD())     \
  V(subhn2, NEON_SUBHN2, vd.IsQ())   \
  V(rsubhn, NEON_RSUBHN, vd.IsD())   \
  V(rsubhn2, NEON_RSUBHN2, vd.IsQ())

#define DEFINE_ASM_FUNC(FN, OP, AS)                            \
  void Assembler::FN(const VRegister& vd, const VRegister& vn, \
                     const VRegister& vm) {                    \
    DCHECK(AS);                                                \
    NEON3DifferentHN(vd, vn, vm, OP);                          \
  }
NEON_3DIFF_HN_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

void Assembler::NEONPerm(const VRegister& vd, const VRegister& vn,
                         const VRegister& vm, NEONPermOp op) {
  DCHECK(AreSameFormat(vd, vn, vm));
  DCHECK(!vd.Is1D());
  Emit(VFormat(vd) | op | Rm(vm) | Rn(vn) | Rd(vd));
}

void Assembler::trn1(const VRegister& vd, const VRegister& vn,
                     const VRegister& vm) {
  NEONPerm(vd, vn, vm, NEON_TRN1);
}

void Assembler::trn2(const VRegister& vd, const VRegister& vn,
                     const VRegister& vm) {
  NEONPerm(vd, vn, vm, NEON_TRN2);
}

void Assembler::uzp1(const VRegister& vd, const VRegister& vn,
                     const VRegister& vm) {
  NEONPerm(vd, vn, vm, NEON_UZP1);
}

void Assembler::uzp2(const VRegister& vd, const VRegister& vn,
                     const VRegister& vm) {
  NEONPerm(vd, vn, vm, NEON_UZP2);
}

void Assembler::zip1(const VRegister& vd, const VRegister& vn,
                     const VRegister& vm) {
  NEONPerm(vd, vn, vm, NEON_ZIP1);
}

void Assembler::zip2(const VRegister& vd, const VRegister& vn,
                     const VRegister& vm) {
  NEONPerm(vd, vn, vm, NEON_ZIP2);
}

void Assembler::NEONShiftImmediate(const VRegister& vd, const VRegister& vn,
                                   NEONShiftImmediateOp op, int immh_immb) {
  DCHECK(AreSameFormat(vd, vn));
  Instr q, scalar;
  if (vn.IsScalar()) {
    q = NEON_Q;
    scalar = NEONScalar;
  } else {
    q = vd.IsD() ? 0 : NEON_Q;
    scalar = 0;
  }
  Emit(q | op | scalar | immh_immb | Rn(vn) | Rd(vd));
}

void Assembler::NEONShiftLeftImmediate(const VRegister& vd, const VRegister& vn,
                                       int shift, NEONShiftImmediateOp op) {
  int laneSizeInBits = vn.LaneSizeInBits();
  DCHECK((shift >= 0) && (shift < laneSizeInBits));
  NEONShiftImmediate(vd, vn, op, (laneSizeInBits + shift) << 16);
}

void Assembler::NEONShiftRightImmediate(const VRegister& vd,
                                        const VRegister& vn, int shift,
                                        NEONShiftImmediateOp op) {
  int laneSizeInBits = vn.LaneSizeInBits();
  DCHECK((shift >= 1) && (shift <= laneSizeInBits));
  NEONShiftImmediate(vd, vn, op, ((2 * laneSizeInBits) - shift) << 16);
}

void Assembler::NEONShiftImmediateL(const VRegister& vd, const VRegister& vn,
                                    int shift, NEONShiftImmediateOp op) {
  int laneSizeInBits = vn.LaneSizeInBits();
  DCHECK((shift >= 0) && (shift < laneSizeInBits));
  int immh_immb = (laneSizeInBits + shift) << 16;

  DCHECK((vn.Is8B() && vd.Is8H()) || (vn.Is4H() && vd.Is4S()) ||
         (vn.Is2S() && vd.Is2D()) || (vn.Is16B() && vd.Is8H()) ||
         (vn.Is8H() && vd.Is4S()) || (vn.Is4S() && vd.Is2D()));
  Instr q;
  q = vn.IsD() ? 0 : NEON_Q;
  Emit(q | op | immh_immb | Rn(vn) | Rd(vd));
}

void Assembler::NEONShiftImmediateN(const VRegister& vd, const VRegister& vn,
                                    int shift, NEONShiftImmediateOp op) {
  Instr q, scalar;
  int laneSizeInBits = vd.LaneSizeInBits();
  DCHECK((shift >= 1) && (shift <= laneSizeInBits));
  int immh_immb = (2 * laneSizeInBits - shift) << 16;

  if (vn.IsScalar()) {
    DCHECK((vd.Is1B() && vn.Is1H()) || (vd.Is1H() && vn.Is1S()) ||
           (vd.Is1S() && vn.Is1D()));
    q = NEON_Q;
    scalar = NEONScalar;
  } else {
    DCHECK((vd.Is8B() && vn.Is8H()) || (vd.Is4H() && vn.Is4S()) ||
           (vd.Is2S() && vn.Is2D()) || (vd.Is16B() && vn.Is8H()) ||
           (vd.Is8H() && vn.Is4S()) || (vd.Is4S() && vn.Is2D()));
    scalar = 0;
    q = vd.IsD() ? 0 : NEON_Q;
  }
  Emit(q | op | scalar | immh_immb | Rn(vn) | Rd(vd));
}

void Assembler::shl(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftLeftImmediate(vd, vn, shift, NEON_SHL);
}

void Assembler::sli(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftLeftImmediate(vd, vn, shift, NEON_SLI);
}

void Assembler::sqshl(const VRegister& vd, const VRegister& vn, int shift) {
  NEONShiftLeftImmediate(vd, vn, shift, NEON_SQSHL_imm);
}

void Assembler::sqshlu(const VRegister& vd, const VRegister& vn, int shift) {
  NEONShiftLeftImmediate(vd, vn, shift, NEON_SQSHLU);
}

void Assembler::uqshl(const VRegister& vd, const VRegister& vn, int shift) {
  NEONShiftLeftImmediate(vd, vn, shift, NEON_UQSHL_imm);
}

void Assembler::sshll(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsD());
  NEONShiftImmediateL(vd, vn, shift, NEON_SSHLL);
}

void Assembler::sshll2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsQ());
  NEONShiftImmediateL(vd, vn, shift, NEON_SSHLL);
}

void Assembler::sxtl(const VRegister& vd, const VRegister& vn) {
  sshll(vd, vn, 0);
}

void Assembler::sxtl2(const VRegister& vd, const VRegister& vn) {
  sshll2(vd, vn, 0);
}

void Assembler::ushll(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsD());
  NEONShiftImmediateL(vd, vn, shift, NEON_USHLL);
}

void Assembler::ushll2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsQ());
  NEONShiftImmediateL(vd, vn, shift, NEON_USHLL);
}

void Assembler::uxtl(const VRegister& vd, const VRegister& vn) {
  ushll(vd, vn, 0);
}

void Assembler::uxtl2(const VRegister& vd, const VRegister& vn) {
  ushll2(vd, vn, 0);
}

void Assembler::sri(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_SRI);
}

void Assembler::sshr(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_SSHR);
}

void Assembler::ushr(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_USHR);
}

void Assembler::srshr(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_SRSHR);
}

void Assembler::urshr(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_URSHR);
}

void Assembler::ssra(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_SSRA);
}

void Assembler::usra(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_USRA);
}

void Assembler::srsra(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_SRSRA);
}

void Assembler::ursra(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEONShiftRightImmediate(vd, vn, shift, NEON_URSRA);
}

void Assembler::shrn(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsD());
  NEONShiftImmediateN(vd, vn, shift, NEON_SHRN);
}

void Assembler::shrn2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsQ());
  NEONShiftImmediateN(vd, vn, shift, NEON_SHRN);
}

void Assembler::rshrn(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsD());
  NEONShiftImmediateN(vd, vn, shift, NEON_RSHRN);
}

void Assembler::rshrn2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsQ());
  NEONShiftImmediateN(vd, vn, shift, NEON_RSHRN);
}

void Assembler::sqshrn(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsD() || (vn.IsScalar() && vd.IsScalar()));
  NEONShiftImmediateN(vd, vn, shift, NEON_SQSHRN);
}

void Assembler::sqshrn2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsQ());
  NEONShiftImmediateN(vd, vn, shift, NEON_SQSHRN);
}

void Assembler::sqrshrn(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsD() || (vn.IsScalar() && vd.IsScalar()));
  NEONShiftImmediateN(vd, vn, shift, NEON_SQRSHRN);
}

void Assembler::sqrshrn2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsQ());
  NEONShiftImmediateN(vd, vn, shift, NEON_SQRSHRN);
}

void Assembler::sqshrun(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsD() || (vn.IsScalar() && vd.IsScalar()));
  NEONShiftImmediateN(vd, vn, shift, NEON_SQSHRUN);
}

void Assembler::sqshrun2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsQ());
  NEONShiftImmediateN(vd, vn, shift, NEON_SQSHRUN);
}

void Assembler::sqrshrun(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsD() || (vn.IsScalar() && vd.IsScalar()));
  NEONShiftImmediateN(vd, vn, shift, NEON_SQRSHRUN);
}

void Assembler::sqrshrun2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsQ());
  NEONShiftImmediateN(vd, vn, shift, NEON_SQRSHRUN);
}

void Assembler::uqshrn(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsD() || (vn.IsScalar() && vd.IsScalar()));
  NEONShiftImmediateN(vd, vn, shift, NEON_UQSHRN);
}

void Assembler::uqshrn2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsQ());
  NEONShiftImmediateN(vd, vn, shift, NEON_UQSHRN);
}

void Assembler::uqrshrn(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vd.IsD() || (vn.IsScalar() && vd.IsScalar()));
  NEONShiftImmediateN(vd, vn, shift, NEON_UQRSHRN);
}

void Assembler::uqrshrn2(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK(vn.IsVector() && vd.IsQ());
  NEONShiftImmediateN(vd, vn, shift, NEON_UQRSHRN);
}

void Assembler::uaddw(const VRegister& vd, const VRegister& vn,
                      const VRegister& vm) {
  DCHECK(vm.IsD());
  NEON3DifferentW(vd, vn, vm, NEON_UADDW);
}

void Assembler::uaddw2(const VRegister& vd, const VRegister& vn,
                       const VRegister& vm) {
  DCHECK(vm.IsQ());
  NEON3DifferentW(vd, vn, vm, NEON_UADDW2);
}

void Assembler::saddw(const VRegister& vd, const VRegister& vn,
                      const VRegister& vm) {
  DCHECK(vm.IsD());
  NEON3DifferentW(vd, vn, vm, NEON_SADDW);
}

void Assembler::saddw2(const VRegister& vd, const VRegister& vn,
                       const VRegister& vm) {
  DCHECK(vm.IsQ());
  NEON3DifferentW(vd, vn, vm, NEON_SADDW2);
}

void Assembler::usubw(const VRegister& vd, const VRegister& vn,
                      const VRegister& vm) {
  DCHECK(vm.IsD());
  NEON3DifferentW(vd, vn, vm, NEON_USUBW);
}

void Assembler::usubw2(const VRegister& vd, const VRegister& vn,
                       const VRegister& vm) {
  DCHECK(vm.IsQ());
  NEON3DifferentW(vd, vn, vm, NEON_USUBW2);
}

void Assembler::ssubw(const VRegister& vd, const VRegister& vn,
                      const VRegister& vm) {
  DCHECK(vm.IsD());
  NEON3DifferentW(vd, vn, vm, NEON_SSUBW);
}

void Assembler::ssubw2(const VRegister& vd, const VRegister& vn,
                       const VRegister& vm) {
  DCHECK(vm.IsQ());
  NEON3DifferentW(vd, vn, vm, NEON_SSUBW2);
}

void Assembler::mov(const Register& rd, const Register& rm) {
  // Moves involving the stack pointer are encoded as add immediate with
  // second operand of zero. Otherwise, orr with first operand zr is
  // used.
  if (rd.IsSP() || rm.IsSP()) {
    add(rd, rm, 0);
  } else {
    orr(rd, AppropriateZeroRegFor(rd), rm);
  }
}

void Assembler::ins(const VRegister& vd, int vd_index, const Register& rn) {
  // We support vd arguments of the form vd.VxT() or vd.T(), where x is the
  // number of lanes, and T is b, h, s or d.
  int lane_size = vd.LaneSizeInBytes();
  NEONFormatField format;
  switch (lane_size) {
    case 1:
      format = NEON_16B;
      DCHECK(rn.IsW());
      break;
    case 2:
      format = NEON_8H;
      DCHECK(rn.IsW());
      break;
    case 4:
      format = NEON_4S;
      DCHECK(rn.IsW());
      break;
    default:
      DCHECK_EQ(lane_size, 8);
      DCHECK(rn.IsX());
      format = NEON_2D;
      break;
  }

  DCHECK((0 <= vd_index) &&
         (vd_index < LaneCountFromFormat(static_cast<VectorFormat>(format))));
  Emit(NEON_INS_GENERAL | ImmNEON5(format, vd_index) | Rn(rn) | Rd(vd));
}

void Assembler::mov(const Register& rd, const VRegister& vn, int vn_index) {
  DCHECK_GE(vn.SizeInBytes(), 4);
  umov(rd, vn, vn_index);
}

void Assembler::smov(const Register& rd, const VRegister& vn, int vn_index) {
  // We support vn arguments of the form vn.VxT() or vn.T(), where x is the
  // number of lanes, and T is b, h, s.
  int lane_size = vn.LaneSizeInBytes();
  NEONFormatField format;
  Instr q = 0;
  switch (lane_size) {
    case 1:
      format = NEON_16B;
      break;
    case 2:
      format = NEON_8H;
      break;
    default:
      DCHECK_EQ(lane_size, 4);
      DCHECK(rd.IsX());
      format = NEON_4S;
      break;
  }
  q = rd.IsW() ? 0 : NEON_Q;
  DCHECK((0 <= vn_index) &&
         (vn_index < LaneCountFromFormat(static_cast<VectorFormat>(format))));
  Emit(q | NEON_SMOV | ImmNEON5(format, vn_index) | Rn(vn) | Rd(rd));
}

void Assembler::cls(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(!vd.Is1D() && !vd.Is2D());
  Emit(VFormat(vn) | NEON_CLS | Rn(vn) | Rd(vd));
}

void Assembler::clz(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(!vd.Is1D() && !vd.Is2D());
  Emit(VFormat(vn) | NEON_CLZ | Rn(vn) | Rd(vd));
}

void Assembler::cnt(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(vd.Is8B() || vd.Is16B());
  Emit(VFormat(vn) | NEON_CNT | Rn(vn) | Rd(vd));
}

void Assembler::rev16(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(vd.Is8B() || vd.Is16B());
  Emit(VFormat(vn) | NEON_REV16 | Rn(vn) | Rd(vd));
}

void Assembler::rev32(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(vd.Is8B() || vd.Is16B() || vd.Is4H() || vd.Is8H());
  Emit(VFormat(vn) | NEON_REV32 | Rn(vn) | Rd(vd));
}

void Assembler::rev64(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(!vd.Is1D() && !vd.Is2D());
  Emit(VFormat(vn) | NEON_REV64 | Rn(vn) | Rd(vd));
}

void Assembler::ursqrte(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(vd.Is2S() || vd.Is4S());
  Emit(VFormat(vn) | NEON_URSQRTE | Rn(vn) | Rd(vd));
}

void Assembler::urecpe(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(vd.Is2S() || vd.Is4S());
  Emit(VFormat(vn) | NEON_URECPE | Rn(vn) | Rd(vd));
}

void Assembler::NEONAddlp(const VRegister& vd, const VRegister& vn,
                          NEON2RegMiscOp op) {
  DCHECK((op == NEON_SADDLP) || (op == NEON_UADDLP) || (op == NEON_SADALP) ||
         (op == NEON_UADALP));

  DCHECK((vn.Is8B() && vd.Is4H()) || (vn.Is4H() && vd.Is2S()) ||
         (vn.Is2S() && vd.Is1D()) || (vn.Is16B() && vd.Is8H()) ||
         (vn.Is8H() && vd.Is4S()) || (vn.Is4S() && vd.Is2D()));
  Emit(VFormat(vn) | op | Rn(vn) | Rd(vd));
}

void Assembler::saddlp(const VRegister& vd, const VRegister& vn) {
  NEONAddlp(vd, vn, NEON_SADDLP);
}

void Assembler::uaddlp(const VRegister& vd, const VRegister& vn) {
  NEONAddlp(vd, vn, NEON_UADDLP);
}

void Assembler::sadalp(const VRegister& vd, const VRegister& vn) {
  NEONAddlp(vd, vn, NEON_SADALP);
}

void Assembler::uadalp(const VRegister& vd, const VRegister& vn) {
  NEONAddlp(vd, vn, NEON_UADALP);
}

void Assembler::NEONAcrossLanesL(const VRegister& vd, const VRegister& vn,
                                 NEONAcrossLanesOp op) {
  DCHECK((vn.Is8B() && vd.Is1H()) || (vn.Is16B() && vd.Is1H()) ||
         (vn.Is4H() && vd.Is1S()) || (vn.Is8H() && vd.Is1S()) ||
         (vn.Is4S() && vd.Is1D()));
  Emit(VFormat(vn) | op | Rn(vn) | Rd(vd));
}

void Assembler::saddlv(const VRegister& vd, const VRegister& vn) {
  NEONAcrossLanesL(vd, vn, NEON_SADDLV);
}

void Assembler::uaddlv(const VRegister& vd, const VRegister& vn) {
  NEONAcrossLanesL(vd, vn, NEON_UADDLV);
}

void Assembler::NEONAcrossLanes(const VRegister& vd, const VRegister& vn,
                                NEONAcrossLanesOp op) {
  DCHECK((vn.Is8B() && vd.Is1B()) || (vn.Is16B() && vd.Is1B()) ||
         (vn.Is4H() && vd.Is1H()) || (vn.Is8H() && vd.Is1H()) ||
         (vn.Is4S() && vd.Is1S()));
  if ((op & NEONAcrossLanesFPFMask) == NEONAcrossLanesFPFixed) {
    Emit(FPFormat(vn) | op | Rn(vn) | Rd(vd));
  } else {
    Emit(VFormat(vn) | op | Rn(vn) | Rd(vd));
  }
}

#define NEON_ACROSSLANES_LIST(V)      \
  V(fmaxv, NEON_FMAXV, vd.Is1S())     \
  V(fminv, NEON_FMINV, vd.Is1S())     \
  V(fmaxnmv, NEON_FMAXNMV, vd.Is1S()) \
  V(fminnmv, NEON_FMINNMV, vd.Is1S()) \
  V(addv, NEON_ADDV, true)            \
  V(smaxv, NEON_SMAXV, true)          \
  V(sminv, NEON_SMINV, true)          \
  V(umaxv, NEON_UMAXV, true)          \
  V(uminv, NEON_UMINV, true)

#define DEFINE_ASM_FUNC(FN, OP, AS)                              \
  void Assembler::FN(const VRegister& vd, const VRegister& vn) { \
    DCHECK(AS);                                                  \
    NEONAcrossLanes(vd, vn, OP);                                 \
  }
NEON_ACROSSLANES_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

void Assembler::mov(const VRegister& vd, int vd_index, const Register& rn) {
  ins(vd, vd_index, rn);
}

void Assembler::umov(const Register& rd, const VRegister& vn, int vn_index) {
  // We support vn arguments of the form vn.VxT() or vn.T(), where x is the
  // number of lanes, and T is b, h, s or d.
  int lane_size = vn.LaneSizeInBytes();
  NEONFormatField format;
  Instr q = 0;
  switch (lane_size) {
    case 1:
      format = NEON_16B;
      DCHECK(rd.IsW());
      break;
    case 2:
      format = NEON_8H;
      DCHECK(rd.IsW());
      break;
    case 4:
      format = NEON_4S;
      DCHECK(rd.IsW());
      break;
    default:
      DCHECK_EQ(lane_size, 8);
      DCHECK(rd.IsX());
      format = NEON_2D;
      q = NEON_Q;
      break;
  }

  DCHECK((0 <= vn_index) &&
         (vn_index < LaneCountFromFormat(static_cast<VectorFormat>(format))));
  Emit(q | NEON_UMOV | ImmNEON5(format, vn_index) | Rn(vn) | Rd(rd));
}

void Assembler::mov(const VRegister& vd, const VRegister& vn, int vn_index) {
  DCHECK(vd.IsScalar());
  dup(vd, vn, vn_index);
}

void Assembler::dup(const VRegister& vd, const Register& rn) {
  DCHECK(!vd.Is1D());
  DCHECK_EQ(vd.Is2D(), rn.IsX());
  Instr q = vd.IsD() ? 0 : NEON_Q;
  Emit(q | NEON_DUP_GENERAL | ImmNEON5(VFormat(vd), 0) | Rn(rn) | Rd(vd));
}

void Assembler::ins(const VRegister& vd, int vd_index, const VRegister& vn,
                    int vn_index) {
  DCHECK(AreSameFormat(vd, vn));
  // We support vd arguments of the form vd.VxT() or vd.T(), where x is the
  // number of lanes, and T is b, h, s or d.
  int lane_size = vd.LaneSizeInBytes();
  NEONFormatField format;
  switch (lane_size) {
    case 1:
      format = NEON_16B;
      break;
    case 2:
      format = NEON_8H;
      break;
    case 4:
      format = NEON_4S;
      break;
    default:
      DCHECK_EQ(lane_size, 8);
      format = NEON_2D;
      break;
  }

  DCHECK((0 <= vd_index) &&
         (vd_index < LaneCountFromFormat(static_cast<VectorFormat>(format))));
  DCHECK((0 <= vn_index) &&
         (vn_index < LaneCountFromFormat(static_cast<VectorFormat>(format))));
  Emit(NEON_INS_ELEMENT | ImmNEON5(format, vd_index) |
       ImmNEON4(format, vn_index) | Rn(vn) | Rd(vd));
}

void Assembler::NEONTable(const VRegister& vd, const VRegister& vn,
                          const VRegister& vm, NEONTableOp op) {
  DCHECK(vd.Is16B() || vd.Is8B());
  DCHECK(vn.Is16B());
  DCHECK(AreSameFormat(vd, vm));
  Emit(op | (vd.IsQ() ? NEON_Q : 0) | Rm(vm) | Rn(vn) | Rd(vd));
}

void Assembler::tbl(const VRegister& vd, const VRegister& vn,
                    const VRegister& vm) {
  NEONTable(vd, vn, vm, NEON_TBL_1v);
}

void Assembler::tbl(const VRegister& vd, const VRegister& vn,
                    const VRegister& vn2, const VRegister& vm) {
  USE(vn2);
  DCHECK(AreSameFormat(vn, vn2));
  DCHECK(AreConsecutive(vn, vn2));
  NEONTable(vd, vn, vm, NEON_TBL_2v);
}

void Assembler::tbl(const VRegister& vd, const VRegister& vn,
                    const VRegister& vn2, const VRegister& vn3,
                    const VRegister& vm) {
  USE(vn2);
  USE(vn3);
  DCHECK(AreSameFormat(vn, vn2, vn3));
  DCHECK(AreConsecutive(vn, vn2, vn3));
  NEONTable(vd, vn, vm, NEON_TBL_3v);
}

void Assembler::tbl(const VRegister& vd, const VRegister& vn,
                    const VRegister& vn2, const VRegister& vn3,
                    const VRegister& vn4, const VRegister& vm) {
  USE(vn2);
  USE(vn3);
  USE(vn4);
  DCHECK(AreSameFormat(vn, vn2, vn3, vn4));
  DCHECK(AreConsecutive(vn, vn2, vn3, vn4));
  NEONTable(vd, vn, vm, NEON_TBL_4v);
}

void Assembler::tbx(const VRegister& vd, const VRegister& vn,
                    const VRegister& vm) {
  NEONTable(vd, vn, vm, NEON_TBX_1v);
}

void Assembler::tbx(const VRegister& vd, const VRegister& vn,
                    const VRegister& vn2, const VRegister& vm) {
  USE(vn2);
  DCHECK(AreSameFormat(vn, vn2));
  DCHECK(AreConsecutive(vn, vn2));
  NEONTable(vd, vn, vm, NEON_TBX_2v);
}

void Assembler::tbx(const VRegister& vd, const VRegister& vn,
                    const VRegister& vn2, const VRegister& vn3,
                    const VRegister& vm) {
  USE(vn2);
  USE(vn3);
  DCHECK(AreSameFormat(vn, vn2, vn3));
  DCHECK(AreConsecutive(vn, vn2, vn3));
  NEONTable(vd, vn, vm, NEON_TBX_3v);
}

void Assembler::tbx(const VRegister& vd, const VRegister& vn,
                    const VRegister& vn2, const VRegister& vn3,
                    const VRegister& vn4, const VRegister& vm) {
  USE(vn2);
  USE(vn3);
  USE(vn4);
  DCHECK(AreSameFormat(vn, vn2, vn3, vn4));
  DCHECK(AreConsecutive(vn, vn2, vn3, vn4));
  NEONTable(vd, vn, vm, NEON_TBX_4v);
}

void Assembler::mov(const VRegister& vd, int vd_index, const VRegister& vn,
                    int vn_index) {
  ins(vd, vd_index, vn, vn_index);
}

void Assembler::mvn(const Register& rd, const Operand& operand) {
  orn(rd, AppropriateZeroRegFor(rd), operand);
}

void Assembler::mrs(const Register& rt, SystemRegister sysreg) {
  DCHECK(rt.Is64Bits());
  Emit(MRS | ImmSystemRegister(sysreg) | Rt(rt));
}

void Assembler::msr(SystemRegister sysreg, const Register& rt) {
  DCHECK(rt.Is64Bits());
  Emit(MSR | Rt(rt) | ImmSystemRegister(sysreg));
}

void Assembler::hint(SystemHint code) { Emit(HINT | ImmHint(code) | Rt(xzr)); }

// NEON structure loads and stores.
Instr Assembler::LoadStoreStructAddrModeField(const MemOperand& addr) {
  Instr addr_field = RnSP(addr.base());

  if (addr.IsPostIndex()) {
    static_assert(NEONLoadStoreMultiStructPostIndex ==
                      static_cast<NEONLoadStoreMultiStructPostIndexOp>(
                          NEONLoadStoreSingleStructPostIndex),
                  "Opcodes must match for NEON post index memop.");

    addr_field |= NEONLoadStoreMultiStructPostIndex;
    if (addr.offset() == 0) {
      addr_field |= RmNot31(addr.regoffset());
    } else {
      // The immediate post index addressing mode is indicated by rm = 31.
      // The immediate is implied by the number of vector registers used.
      addr_field |= (0x1F << Rm_offset);
    }
  } else {
    DCHECK(addr.IsImmediateOffset() && (addr.offset() == 0));
  }
  return addr_field;
}

void Assembler::LoadStoreStructVerify(const VRegister& vt,
                                      const MemOperand& addr, Instr op) {
#ifdef DEBUG
  // Assert that addressing mode is either offset (with immediate 0), post
  // index by immediate of the size of the register list, or post index by a
  // value in a core register.
  if (addr.IsImmediateOffset()) {
    DCHECK_EQ(addr.offset(), 0);
  } else {
    int offset = vt.SizeInBytes();
    switch (op) {
      case NEON_LD1_1v:
      case NEON_ST1_1v:
        offset *= 1;
        break;
      case NEONLoadStoreSingleStructLoad1:
      case NEONLoadStoreSingleStructStore1:
      case NEON_LD1R:
        offset = (offset / vt.LaneCount()) * 1;
        break;

      case NEON_LD1_2v:
      case NEON_ST1_2v:
      case NEON_LD2:
      case NEON_ST2:
        offset *= 2;
        break;
      case NEONLoadStoreSingleStructLoad2:
      case NEONLoadStoreSingleStructStore2:
      case NEON_LD2R:
        offset = (offset / vt.LaneCount()) * 2;
        break;

      case NEON_LD1_3v:
      case NEON_ST1_3v:
      case NEON_LD3:
      case NEON_ST3:
        offset *= 3;
        break;
      case NEONLoadStoreSingleStructLoad3:
      case NEONLoadStoreSingleStructStore3:
      case NEON_LD3R:
        offset = (offset / vt.LaneCount()) * 3;
        break;

      case NEON_LD1_4v:
      case NEON_ST1_4v:
      case NEON_LD4:
      case NEON_ST4:
        offset *= 4;
        break;
      case NEONLoadStoreSingleStructLoad4:
      case NEONLoadStoreSingleStructStore4:
      case NEON_LD4R:
        offset = (offset / vt.LaneCount()) * 4;
        break;
      default:
        UNREACHABLE();
    }
    DCHECK(addr.regoffset() != NoReg || addr.offset() == offset);
  }
#else
  USE(vt);
  USE(addr);
  USE(op);
#endif
}

void Assembler::LoadStoreStruct(const VRegister& vt, const MemOperand& addr,
                                NEONLoadStoreMultiStructOp op) {
  LoadStoreStructVerify(vt, addr, op);
  DCHECK(vt.IsVector() || vt.Is1D());
  Emit(op | LoadStoreStructAddrModeField(addr) | LSVFormat(vt) | Rt(vt));
}

void Assembler::LoadStoreStructSingleAllLanes(const VRegister& vt,
                                              const MemOperand& addr,
                                              NEONLoadStoreSingleStructOp op) {
  LoadStoreStructVerify(vt, addr, op);
  Emit(op | LoadStoreStructAddrModeField(addr) | LSVFormat(vt) | Rt(vt));
}

void Assembler::ld1(const VRegister& vt, const MemOperand& src) {
  LoadStoreStruct(vt, src, NEON_LD1_1v);
}

void Assembler::ld1(const VRegister& vt, const VRegister& vt2,
                    const MemOperand& src) {
  USE(vt2);
  DCHECK(AreSameFormat(vt, vt2));
  DCHECK(AreConsecutive(vt, vt2));
  LoadStoreStruct(vt, src, NEON_LD1_2v);
}

void Assembler::ld1(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  DCHECK(AreSameFormat(vt, vt2, vt3));
  DCHECK(AreConsecutive(vt, vt2, vt3));
  LoadStoreStruct(vt, src, NEON_LD1_3v);
}

void Assembler::ld1(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const VRegister& vt4,
                    const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  USE(vt4);
  DCHECK(AreSameFormat(vt, vt2, vt3, vt4));
  DCHECK(AreConsecutive(vt, vt2, vt3, vt4));
  LoadStoreStruct(vt, src, NEON_LD1_4v);
}

void Assembler::ld2(const VRegister& vt, const VRegister& vt2,
                    const MemOperand& src) {
  USE(vt2);
  DCHECK(AreSameFormat(vt, vt2));
  DCHECK(AreConsecutive(vt, vt2));
  LoadStoreStruct(vt, src, NEON_LD2);
}

void Assembler::ld2(const VRegister& vt, const VRegister& vt2, int lane,
                    const MemOperand& src) {
  USE(vt2);
  DCHECK(AreSameFormat(vt, vt2));
  DCHECK(AreConsecutive(vt, vt2));
  LoadStoreStructSingle(vt, lane, src, NEONLoadStoreSingleStructLoad2);
}

void Assembler::ld2r(const VRegister& vt, const VRegister& vt2,
                     const MemOperand& src) {
  USE(vt2);
  DCHECK(AreSameFormat(vt, vt2));
  DCHECK(AreConsecutive(vt, vt2));
  LoadStoreStructSingleAllLanes(vt, src, NEON_LD2R);
}

void Assembler::ld3(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  DCHECK(AreSameFormat(vt, vt2, vt3));
  DCHECK(AreConsecutive(vt, vt2, vt3));
  LoadStoreStruct(vt, src, NEON_LD3);
}

void Assembler::ld3(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, int lane, const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  DCHECK(AreSameFormat(vt, vt2, vt3));
  DCHECK(AreConsecutive(vt, vt2, vt3));
  LoadStoreStructSingle(vt, lane, src, NEONLoadStoreSingleStructLoad3);
}

void Assembler::ld3r(const VRegister& vt, const VRegister& vt2,
                     const VRegister& vt3, const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  DCHECK(AreSameFormat(vt, vt2, vt3));
  DCHECK(AreConsecutive(vt, vt2, vt3));
  LoadStoreStructSingleAllLanes(vt, src, NEON_LD3R);
}

void Assembler::ld4(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const VRegister& vt4,
                    const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  USE(vt4);
  DCHECK(AreSameFormat(vt, vt2, vt3, vt4));
  DCHECK(AreConsecutive(vt, vt2, vt3, vt4));
  LoadStoreStruct(vt, src, NEON_LD4);
}

void Assembler::ld4(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const VRegister& vt4, int lane,
                    const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  USE(vt4);
  DCHECK(AreSameFormat(vt, vt2, vt3, vt4));
  DCHECK(AreConsecutive(vt, vt2, vt3, vt4));
  LoadStoreStructSingle(vt, lane, src, NEONLoadStoreSingleStructLoad4);
}

void Assembler::ld4r(const VRegister& vt, const VRegister& vt2,
                     const VRegister& vt3, const VRegister& vt4,
                     const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  USE(vt4);
  DCHECK(AreSameFormat(vt, vt2, vt3, vt4));
  DCHECK(AreConsecutive(vt, vt2, vt3, vt4));
  LoadStoreStructSingleAllLanes(vt, src, NEON_LD4R);
}

void Assembler::st1(const VRegister& vt, const MemOperand& src) {
  LoadStoreStruct(vt, src, NEON_ST1_1v);
}

void Assembler::st1(const VRegister& vt, const VRegister& vt2,
                    const MemOperand& src) {
  USE(vt2);
  DCHECK(AreSameFormat(vt, vt2));
  DCHECK(AreConsecutive(vt, vt2));
  LoadStoreStruct(vt, src, NEON_ST1_2v);
}

void Assembler::st1(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  DCHECK(AreSameFormat(vt, vt2, vt3));
  DCHECK(AreConsecutive(vt, vt2, vt3));
  LoadStoreStruct(vt, src, NEON_ST1_3v);
}

void Assembler::st1(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const VRegister& vt4,
                    const MemOperand& src) {
  USE(vt2);
  USE(vt3);
  USE(vt4);
  DCHECK(AreSameFormat(vt, vt2, vt3, vt4));
  DCHECK(AreConsecutive(vt, vt2, vt3, vt4));
  LoadStoreStruct(vt, src, NEON_ST1_4v);
}

void Assembler::st2(const VRegister& vt, const VRegister& vt2,
                    const MemOperand& dst) {
  USE(vt2);
  DCHECK(AreSameFormat(vt, vt2));
  DCHECK(AreConsecutive(vt, vt2));
  LoadStoreStruct(vt, dst, NEON_ST2);
}

void Assembler::st2(const VRegister& vt, const VRegister& vt2, int lane,
                    const MemOperand& dst) {
  USE(vt2);
  DCHECK(AreSameFormat(vt, vt2));
  DCHECK(AreConsecutive(vt, vt2));
  LoadStoreStructSingle(vt, lane, dst, NEONLoadStoreSingleStructStore2);
}

void Assembler::st3(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const MemOperand& dst) {
  USE(vt2);
  USE(vt3);
  DCHECK(AreSameFormat(vt, vt2, vt3));
  DCHECK(AreConsecutive(vt, vt2, vt3));
  LoadStoreStruct(vt, dst, NEON_ST3);
}

void Assembler::st3(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, int lane, const MemOperand& dst) {
  USE(vt2);
  USE(vt3);
  DCHECK(AreSameFormat(vt, vt2, vt3));
  DCHECK(AreConsecutive(vt, vt2, vt3));
  LoadStoreStructSingle(vt, lane, dst, NEONLoadStoreSingleStructStore3);
}

void Assembler::st4(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const VRegister& vt4,
                    const MemOperand& dst) {
  USE(vt2);
  USE(vt3);
  USE(vt4);
  DCHECK(AreSameFormat(vt, vt2, vt3, vt4));
  DCHECK(AreConsecutive(vt, vt2, vt3, vt4));
  LoadStoreStruct(vt, dst, NEON_ST4);
}

void Assembler::st4(const VRegister& vt, const VRegister& vt2,
                    const VRegister& vt3, const VRegister& vt4, int lane,
                    const MemOperand& dst) {
  USE(vt2);
  USE(vt3);
  USE(vt4);
  DCHECK(AreSameFormat(vt, vt2, vt3, vt4));
  DCHECK(AreConsecutive(vt, vt2, vt3, vt4));
  LoadStoreStructSingle(vt, lane, dst, NEONLoadStoreSingleStructStore4);
}

void Assembler::LoadStoreStructSingle(const VRegister& vt, uint32_t lane,
                                      const MemOperand& addr,
                                      NEONLoadStoreSingleStructOp op) {
  LoadStoreStructVerify(vt, addr, op);

  // We support vt arguments of the form vt.VxT() or vt.T(), where x is the
  // number of lanes, and T is b, h, s or d.
  unsigned lane_size = vt.LaneSizeInBytes();
  DCHECK_LT(lane, kQRegSize / lane_size);

  // Lane size is encoded in the opcode field. Lane index is encoded in the Q,
  // S and size fields.
  lane *= lane_size;

  // Encodings for S[0]/D[0] and S[2]/D[1] are distinguished using the least-
  // significant bit of the size field, so we increment lane here to account for
  // that.
  if (lane_size == 8) lane++;

  Instr size = (lane << NEONLSSize_offset) & NEONLSSize_mask;
  Instr s = (lane << (NEONS_offset - 2)) & NEONS_mask;
  Instr q = (lane << (NEONQ_offset - 3)) & NEONQ_mask;

  Instr instr = op;
  switch (lane_size) {
    case 1:
      instr |= NEONLoadStoreSingle_b;
      break;
    case 2:
      instr |= NEONLoadStoreSingle_h;
      break;
    case 4:
      instr |= NEONLoadStoreSingle_s;
      break;
    default:
      DCHECK_EQ(lane_size, 8U);
      instr |= NEONLoadStoreSingle_d;
  }

  Emit(instr | LoadStoreStructAddrModeField(addr) | q | size | s | Rt(vt));
}

void Assembler::ld1(const VRegister& vt, int lane, const MemOperand& src) {
  LoadStoreStructSingle(vt, lane, src, NEONLoadStoreSingleStructLoad1);
}

void Assembler::ld1r(const VRegister& vt, const MemOperand& src) {
  LoadStoreStructSingleAllLanes(vt, src, NEON_LD1R);
}

void Assembler::st1(const VRegister& vt, int lane, const MemOperand& dst) {
  LoadStoreStructSingle(vt, lane, dst, NEONLoadStoreSingleStructStore1);
}

void Assembler::dmb(BarrierDomain domain, BarrierType type) {
  Emit(DMB | ImmBarrierDomain(domain) | ImmBarrierType(type));
}

void Assembler::dsb(BarrierDomain domain, BarrierType type) {
  Emit(DSB | ImmBarrierDomain(domain) | ImmBarrierType(type));
}

void Assembler::isb() {
  Emit(ISB | ImmBarrierDomain(FullSystem) | ImmBarrierType(BarrierAll));
}

void Assembler::csdb() { hint(CSDB); }

void Assembler::fmov(const VRegister& vd, double imm) {
  if (vd.IsScalar()) {
    DCHECK(vd.Is1D());
    Emit(FMOV_d_imm | Rd(vd) | ImmFP(imm));
  } else {
    DCHECK(vd.Is2D());
    Instr op = NEONModifiedImmediate_MOVI | NEONModifiedImmediateOpBit;
    Emit(NEON_Q | op | ImmNEONFP(imm) | NEONCmode(0xF) | Rd(vd));
  }
}

void Assembler::fmov(const VRegister& vd, float imm) {
  if (vd.IsScalar()) {
    DCHECK(vd.Is1S());
    Emit(FMOV_s_imm | Rd(vd) | ImmFP(imm));
  } else {
    DCHECK(vd.Is2S() || vd.Is4S());
    Instr op = NEONModifiedImmediate_MOVI;
    Instr q = vd.Is4S() ? NEON_Q : 0;
    Emit(q | op | ImmNEONFP(imm) | NEONCmode(0xF) | Rd(vd));
  }
}

void Assembler::fmov(const Register& rd, const VRegister& fn) {
  DCHECK_EQ(rd.SizeInBits(), fn.SizeInBits());
  FPIntegerConvertOp op = rd.Is32Bits() ? FMOV_ws : FMOV_xd;
  Emit(op | Rd(rd) | Rn(fn));
}

void Assembler::fmov(const VRegister& vd, const Register& rn) {
  DCHECK_EQ(vd.SizeInBits(), rn.SizeInBits());
  FPIntegerConvertOp op = vd.Is32Bits() ? FMOV_sw : FMOV_dx;
  Emit(op | Rd(vd) | Rn(rn));
}

void Assembler::fmov(const VRegister& vd, const VRegister& vn) {
  DCHECK_EQ(vd.SizeInBits(), vn.SizeInBits());
  Emit(FPType(vd) | FMOV | Rd(vd) | Rn(vn));
}

void Assembler::fmov(const VRegister& vd, int index, const Register& rn) {
  DCHECK((index == 1) && vd.Is1D() && rn.IsX());
  USE(index);
  Emit(FMOV_d1_x | Rd(vd) | Rn(rn));
}

void Assembler::fmov(const Register& rd, const VRegister& vn, int index) {
  DCHECK((index == 1) && vn.Is1D() && rd.IsX());
  USE(index);
  Emit(FMOV_x_d1 | Rd(rd) | Rn(vn));
}

void Assembler::fmadd(const VRegister& fd, const VRegister& fn,
                      const VRegister& fm, const VRegister& fa) {
  FPDataProcessing3Source(fd, fn, fm, fa, fd.Is32Bits() ? FMADD_s : FMADD_d);
}

void Assembler::fmsub(const VRegister& fd, const VRegister& fn,
                      const VRegister& fm, const VRegister& fa) {
  FPDataProcessing3Source(fd, fn, fm, fa, fd.Is32Bits() ? FMSUB_s : FMSUB_d);
}

void Assembler::fnmadd(const VRegister& fd, const VRegister& fn,
                       const VRegister& fm, const VRegister& fa) {
  FPDataProcessing3Source(fd, fn, fm, fa, fd.Is32Bits() ? FNMADD_s : FNMADD_d);
}

void Assembler::fnmsub(const VRegister& fd, const VRegister& fn,
                       const VRegister& fm, const VRegister& fa) {
  FPDataProcessing3Source(fd, fn, fm, fa, fd.Is32Bits() ? FNMSUB_s : FNMSUB_d);
}

void Assembler::fnmul(const VRegister& vd, const VRegister& vn,
                      const VRegister& vm) {
  DCHECK(AreSameSizeAndType(vd, vn, vm));
  Instr op = vd.Is1S() ? FNMUL_s : FNMUL_d;
  Emit(FPType(vd) | op | Rm(vm) | Rn(vn) | Rd(vd));
}

void Assembler::fcmp(const VRegister& fn, const VRegister& fm) {
  DCHECK_EQ(fn.SizeInBits(), fm.SizeInBits());
  Emit(FPType(fn) | FCMP | Rm(fm) | Rn(fn));
}

void Assembler::fcmp(const VRegister& fn, double value) {
  USE(value);
  // Although the fcmp instruction can strictly only take an immediate value of
  // +0.0, we don't need to check for -0.0 because the sign of 0.0 doesn't
  // affect the result of the comparison.
  DCHECK_EQ(value, 0.0);
  Emit(FPType(fn) | FCMP_zero | Rn(fn));
}

void Assembler::fccmp(const VRegister& fn, const VRegister& fm,
                      StatusFlags nzcv, Condition cond) {
  DCHECK_EQ(fn.SizeInBits(), fm.SizeInBits());
  Emit(FPType(fn) | FCCMP | Rm(fm) | Cond(cond) | Rn(fn) | Nzcv(nzcv));
}

void Assembler::fcsel(const VRegister& fd, const VRegister& fn,
                      const VRegister& fm, Condition cond) {
  DCHECK_EQ(fd.SizeInBits(), fn.SizeInBits());
  DCHECK_EQ(fd.SizeInBits(), fm.SizeInBits());
  Emit(FPType(fd) | FCSEL | Rm(fm) | Cond(cond) | Rn(fn) | Rd(fd));
}

void Assembler::NEONFPConvertToInt(const Register& rd, const VRegister& vn,
                                   Instr op) {
  Emit(SF(rd) | FPType(vn) | op | Rn(vn) | Rd(rd));
}

void Assembler::NEONFPConvertToInt(const VRegister& vd, const VRegister& vn,
                                   Instr op) {
  if (vn.IsScalar()) {
    DCHECK((vd.Is1S() && vn.Is1S()) || (vd.Is1D() && vn.Is1D()));
    op |= NEON_Q | NEONScalar;
  }
  Emit(FPFormat(vn) | op | Rn(vn) | Rd(vd));
}

void Assembler::fcvt(const VRegister& vd, const VRegister& vn) {
  FPDataProcessing1SourceOp op;
  if (vd.Is1D()) {
    DCHECK(vn.Is1S() || vn.Is1H());
    op = vn.Is1S() ? FCVT_ds : FCVT_dh;
  } else if (vd.Is1S()) {
    DCHECK(vn.Is1D() || vn.Is1H());
    op = vn.Is1D() ? FCVT_sd : FCVT_sh;
  } else {
    DCHECK(vd.Is1H());
    DCHECK(vn.Is1D() || vn.Is1S());
    op = vn.Is1D() ? FCVT_hd : FCVT_hs;
  }
  FPDataProcessing1Source(vd, vn, op);
}

void Assembler::fcvtl(const VRegister& vd, const VRegister& vn) {
  DCHECK((vd.Is4S() && vn.Is4H()) || (vd.Is2D() && vn.Is2S()));
  Instr format = vd.Is2D() ? (1 << NEONSize_offset) : 0;
  Emit(format | NEON_FCVTL | Rn(vn) | Rd(vd));
}

void Assembler::fcvtl2(const VRegister& vd, const VRegister& vn) {
  DCHECK((vd.Is4S() && vn.Is8H()) || (vd.Is2D() && vn.Is4S()));
  Instr format = vd.Is2D() ? (1 << NEONSize_offset) : 0;
  Emit(NEON_Q | format | NEON_FCVTL | Rn(vn) | Rd(vd));
}

void Assembler::fcvtn(const VRegister& vd, const VRegister& vn) {
  DCHECK((vn.Is4S() && vd.Is4H()) || (vn.Is2D() && vd.Is2S()));
  Instr format = vn.Is2D() ? (1 << NEONSize_offset) : 0;
  Emit(format | NEON_FCVTN | Rn(vn) | Rd(vd));
}

void Assembler::fcvtn2(const VRegister& vd, const VRegister& vn) {
  DCHECK((vn.Is4S() && vd.Is8H()) || (vn.Is2D() && vd.Is4S()));
  Instr format = vn.Is2D() ? (1 << NEONSize_offset) : 0;
  Emit(NEON_Q | format | NEON_FCVTN | Rn(vn) | Rd(vd));
}

void Assembler::fcvtxn(const VRegister& vd, const VRegister& vn) {
  Instr format = 1 << NEONSize_offset;
  if (vd.IsScalar()) {
    DCHECK(vd.Is1S() && vn.Is1D());
    Emit(format | NEON_FCVTXN_scalar | Rn(vn) | Rd(vd));
  } else {
    DCHECK(vd.Is2S() && vn.Is2D());
    Emit(format | NEON_FCVTXN | Rn(vn) | Rd(vd));
  }
}

void Assembler::fcvtxn2(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.Is4S() && vn.Is2D());
  Instr format = 1 << NEONSize_offset;
  Emit(NEON_Q | format | NEON_FCVTXN | Rn(vn) | Rd(vd));
}

void Assembler::fjcvtzs(const Register& rd, const VRegister& vn) {
  DCHECK(rd.IsW() && vn.Is1D());
  Emit(FJCVTZS | Rn(vn) | Rd(rd));
}

#define NEON_FP2REGMISC_FCVT_LIST(V) \
  V(fcvtnu, NEON_FCVTNU, FCVTNU)     \
  V(fcvtns, NEON_FCVTNS, FCVTNS)     \
  V(fcvtpu, NEON_FCVTPU, FCVTPU)     \
  V(fcvtps, NEON_FCVTPS, FCVTPS)     \
  V(fcvtmu, NEON_FCVTMU, FCVTMU)     \
  V(fcvtms, NEON_FCVTMS, FCVTMS)     \
  V(fcvtau, NEON_FCVTAU, FCVTAU)     \
  V(fcvtas, NEON_FCVTAS, FCVTAS)

#define DEFINE_ASM_FUNCS(FN, VEC_OP, SCA_OP)                     \
  void Assembler::FN(const Register& rd, const VRegister& vn) {  \
    NEONFPConvertToInt(rd, vn, SCA_OP);                          \
  }                                                              \
  void Assembler::FN(const VRegister& vd, const VRegister& vn) { \
    NEONFPConvertToInt(vd, vn, VEC_OP);                          \
  }
NEON_FP2REGMISC_FCVT_LIST(DEFINE_ASM_FUNCS)
#undef DEFINE_ASM_FUNCS

void Assembler::scvtf(const VRegister& vd, const VRegister& vn, int fbits) {
  DCHECK_GE(fbits, 0);
  if (fbits == 0) {
    NEONFP2RegMisc(vd, vn, NEON_SCVTF, 0.0);
  } else {
    DCHECK(vd.Is1D() || vd.Is1S() || vd.Is2D() || vd.Is2S() || vd.Is4S());
    NEONShiftRightImmediate(vd, vn, fbits, NEON_SCVTF_imm);
  }
}

void Assembler::ucvtf(const VRegister& vd, const VRegister& vn, int fbits) {
  DCHECK_GE(fbits, 0);
  if (fbits == 0) {
    NEONFP2RegMisc(vd, vn, NEON_UCVTF, 0.0);
  } else {
    DCHECK(vd.Is1D() || vd.Is1S() || vd.Is2D() || vd.Is2S() || vd.Is4S());
    NEONShiftRightImmediate(vd, vn, fbits, NEON_UCVTF_imm);
  }
}

void Assembler::scvtf(const VRegister& vd, const Register& rn, int fbits) {
  DCHECK_GE(fbits, 0);
  if (fbits == 0) {
    Emit(SF(rn) | FPType(vd) | SCVTF | Rn(rn) | Rd(vd));
  } else {
    Emit(SF(rn) | FPType(vd) | SCVTF_fixed | FPScale(64 - fbits) | Rn(rn) |
         Rd(vd));
  }
}

void Assembler::ucvtf(const VRegister& fd, const Register& rn, int fbits) {
  DCHECK_GE(fbits, 0);
  if (fbits == 0) {
    Emit(SF(rn) | FPType(fd) | UCVTF | Rn(rn) | Rd(fd));
  } else {
    Emit(SF(rn) | FPType(fd) | UCVTF_fixed | FPScale(64 - fbits) | Rn(rn) |
         Rd(fd));
  }
}

void Assembler::NEON3Same(const VRegister& vd, const VRegister& vn,
                          const VRegister& vm, NEON3SameOp vop) {
  DCHECK(AreSameFormat(vd, vn, vm));
  DCHECK(vd.IsVector() || !vd.IsQ());

  Instr format, op = vop;
  if (vd.IsScalar()) {
    op |= NEON_Q | NEONScalar;
    format = SFormat(vd);
  } else {
    format = VFormat(vd);
  }

  Emit(format | op | Rm(vm) | Rn(vn) | Rd(vd));
}

void Assembler::NEONFP3Same(const VRegister& vd, const VRegister& vn,
                            const VRegister& vm, Instr op) {
  DCHECK(AreSameFormat(vd, vn, vm));
  if (vd.Is4H() || vd.Is8H()) {
    op |= NEON_sz;
    op ^= NEON3SameHPMask;
  }
  Emit(FPFormat(vd) | op | Rm(vm) | Rn(vn) | Rd(vd));
}

#define NEON_FP2REGMISC_LIST(V)                 \
  V(fabs, NEON_FABS, FABS)                      \
  V(fneg, NEON_FNEG, FNEG)                      \
  V(fsqrt, NEON_FSQRT, FSQRT)                   \
  V(frintn, NEON_FRINTN, FRINTN)                \
  V(frinta, NEON_FRINTA, FRINTA)                \
  V(frintp, NEON_FRINTP, FRINTP)                \
  V(frintm, NEON_FRINTM, FRINTM)                \
  V(frintx, NEON_FRINTX, FRINTX)                \
  V(frintz, NEON_FRINTZ, FRINTZ)                \
  V(frinti, NEON_FRINTI, FRINTI)                \
  V(frsqrte, NEON_FRSQRTE, NEON_FRSQRTE_scalar) \
  V(frecpe, NEON_FRECPE, NEON_FRECPE_scalar)

#define DEFINE_ASM_FUNC(FN, VEC_OP, SCA_OP)                      \
  void Assembler::FN(const VRegister& vd, const VRegister& vn) { \
    if (vd.IsScalar()) {                                         \
      DCHECK(vd.Is1S() || vd.Is1D());                            \
      NEONFP2RegMisc(vd, vn, SCA_OP);                            \
    } else {                                                     \
      NEONFP2RegMisc(vd, vn, VEC_OP, 0.0);                       \
    }                                                            \
  }
NEON_FP2REGMISC_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

void Assembler::shll(const VRegister& vd, const VRegister& vn, int shift) {
  DCHECK((vd.Is8H() && vn.Is8B() && shift == 8) ||
         (vd.Is4S() && vn.Is4H() && shift == 16) ||
         (vd.Is2D() && vn.Is2S() && shift == 32));
  USE(shift);
  Emit(VFormat(vn) | NEON_SHLL | Rn(vn) | Rd(vd));
}

void Assembler::shll2(const VRegister& vd, const VRegister& vn, int shift) {
  USE(shift);
  DCHECK((vd.Is8H() && vn.Is16B() && shift == 8) ||
         (vd.Is4S() && vn.Is8H() && shift == 16) ||
         (vd.Is2D() && vn.Is4S() && shift == 32));
  Emit(VFormat(vn) | NEON_SHLL | Rn(vn) | Rd(vd));
}

void Assembler::NEONFP2RegMisc(const VRegister& vd, const VRegister& vn,
                               NEON2RegMiscOp vop, double value) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK_EQ(value, 0.0);
  USE(value);

  Instr op = vop;
  if (vd.IsScalar()) {
    DCHECK(vd.Is1S() || vd.Is1D());
    op |= NEON_Q | NEONScalar;
  } else if (vd.Is4H() || vd.Is8H()) {
    op |= NEON_sz | NEON2RegMiscHPFixed;
  } else {
    DCHECK(vd.Is2S() || vd.Is2D() || vd.Is4S());
  }

  Emit(FPFormat(vd) | op | Rn(vn) | Rd(vd));
}

void Assembler::fcmeq(const VRegister& vd, const VRegister& vn, double value) {
  NEONFP2RegMisc(vd, vn, NEON_FCMEQ_zero, value);
}

void Assembler::fcmge(const VRegister& vd, const VRegister& vn, double value) {
  NEONFP2RegMisc(vd, vn, NEON_FCMGE_zero, value);
}

void Assembler::fcmgt(const VRegister& vd, const VRegister& vn, double value) {
  NEONFP2RegMisc(vd, vn, NEON_FCMGT_zero, value);
}

void Assembler::fcmle(const VRegister& vd, const VRegister& vn, double value) {
  NEONFP2RegMisc(vd, vn, NEON_FCMLE_zero, value);
}

void Assembler::fcmlt(const VRegister& vd, const VRegister& vn, double value) {
  NEONFP2RegMisc(vd, vn, NEON_FCMLT_zero, value);
}

void Assembler::frecpx(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsScalar());
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(vd.Is1S() || vd.Is1D());
  Emit(FPFormat(vd) | NEON_FRECPX_scalar | Rn(vn) | Rd(vd));
}

void Assembler::fcvtzs(const Register& rd, const VRegister& vn, int fbits) {
  DCHECK(vn.Is1S() || vn.Is1D());
  DCHECK((fbits >= 0) && (fbits <= rd.SizeInBits()));
  if (fbits == 0) {
    Emit(SF(rd) | FPType(vn) | FCVTZS | Rn(vn) | Rd(rd));
  } else {
    Emit(SF(rd) | FPType(vn) | FCVTZS_fixed | FPScale(64 - fbits) | Rn(vn) |
         Rd(rd));
  }
}

void Assembler::fcvtzs(const VRegister& vd, const VRegister& vn, int fbits) {
  DCHECK_GE(fbits, 0);
  if (fbits == 0) {
    NEONFP2RegMisc(vd, vn, NEON_FCVTZS, 0.0);
  } else {
    DCHECK(vd.Is1D() || vd.Is1S() || vd.Is2D() || vd.Is2S() || vd.Is4S());
    NEONShiftRightImmediate(vd, vn, fbits, NEON_FCVTZS_imm);
  }
}

void Assembler::fcvtzu(const Register& rd, const VRegister& vn, int fbits) {
  DCHECK(vn.Is1S() || vn.Is1D());
  DCHECK((fbits >= 0) && (fbits <= rd.SizeInBits()));
  if (fbits == 0) {
    Emit(SF(rd) | FPType(vn) | FCVTZU | Rn(vn) | Rd(rd));
  } else {
    Emit(SF(rd) | FPType(vn) | FCVTZU_fixed | FPScale(64 - fbits) | Rn(vn) |
         Rd(rd));
  }
}

void Assembler::fcvtzu(const VRegister& vd, const VRegister& vn, int fbits) {
  DCHECK_GE(fbits, 0);
  if (fbits == 0) {
    NEONFP2RegMisc(vd, vn, NEON_FCVTZU, 0.0);
  } else {
    DCHECK(vd.Is1D() || vd.Is1S() || vd.Is2D() || vd.Is2S() || vd.Is4S());
    NEONShiftRightImmediate(vd, vn, fbits, NEON_FCVTZU_imm);
  }
}

void Assembler::NEONFP2RegMisc(const VRegister& vd, const VRegister& vn,
                               Instr op) {
  DCHECK(AreSameFormat(vd, vn));
  Emit(FPFormat(vd) | op | Rn(vn) | Rd(vd));
}

void Assembler::NEON2RegMisc(const VRegister& vd, const VRegister& vn,
                             NEON2RegMiscOp vop, int value) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK_EQ(value, 0);
  USE(value);

  Instr format, op = vop;
  if (vd.IsScalar()) {
    op |= NEON_Q | NEONScalar;
    format = SFormat(vd);
  } else {
    format = VFormat(vd);
  }

  Emit(format | op | Rn(vn) | Rd(vd));
}

void Assembler::cmeq(const VRegister& vd, const VRegister& vn, int value) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEON2RegMisc(vd, vn, NEON_CMEQ_zero, value);
}

void Assembler::cmge(const VRegister& vd, const VRegister& vn, int value) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEON2RegMisc(vd, vn, NEON_CMGE_zero, value);
}

void Assembler::cmgt(const VRegister& vd, const VRegister& vn, int value) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEON2RegMisc(vd, vn, NEON_CMGT_zero, value);
}

void Assembler::cmle(const VRegister& vd, const VRegister& vn, int value) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEON2RegMisc(vd, vn, NEON_CMLE_zero, value);
}

void Assembler::cmlt(const VRegister& vd, const VRegister& vn, int value) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEON2RegMisc(vd, vn, NEON_CMLT_zero, value);
}

#define NEON_3SAME_LIST(V)                                         \
  V(add, NEON_ADD, vd.IsVector() || vd.Is1D())                     \
  V(addp, NEON_ADDP, vd.IsVector() || vd.Is1D())                   \
  V(sub, NEON_SUB, vd.IsVector() || vd.Is1D())                     \
  V(cmeq, NEON_CMEQ, vd.IsVector() || vd.Is1D())                   \
  V(cmge, NEON_CMGE, vd.IsVector() || vd.Is1D())                   \
  V(cmgt, NEON_CMGT, vd.IsVector() || vd.Is1D())                   \
  V(cmhi, NEON_CMHI, vd.IsVector() || vd.Is1D())                   \
  V(cmhs, NEON_CMHS, vd.IsVector() || vd.Is1D())                   \
  V(cmtst, NEON_CMTST, vd.IsVector() || vd.Is1D())                 \
  V(sshl, NEON_SSHL, vd.IsVector() || vd.Is1D())                   \
  V(ushl, NEON_USHL, vd.IsVector() || vd.Is1D())                   \
  V(srshl, NEON_SRSHL, vd.IsVector() || vd.Is1D())                 \
  V(urshl, NEON_URSHL, vd.IsVector() || vd.Is1D())                 \
  V(sqdmulh, NEON_SQDMULH, vd.IsLaneSizeH() || vd.IsLaneSizeS())   \
  V(sqrdmulh, NEON_SQRDMULH, vd.IsLaneSizeH() || vd.IsLaneSizeS()) \
  V(shadd, NEON_SHADD, vd.IsVector() && !vd.IsLaneSizeD())         \
  V(uhadd, NEON_UHADD, vd.IsVector() && !vd.IsLaneSizeD())         \
  V(srhadd, NEON_SRHADD, vd.IsVector() && !vd.IsLaneSizeD())       \
  V(urhadd, NEON_URHADD, vd.IsVector() && !vd.IsLaneSizeD())       \
  V(shsub, NEON_SHSUB, vd.IsVector() && !vd.IsLaneSizeD())         \
  V(uhsub, NEON_UHSUB, vd.IsVector() && !vd.IsLaneSizeD())         \
  V(smax, NEON_SMAX, vd.IsVector() && !vd.IsLaneSizeD())           \
  V(smaxp, NEON_SMAXP, vd.IsVector() && !vd.IsLaneSizeD())         \
  V(smin, NEON_SMIN, vd.IsVector() && !vd.IsLaneSizeD())           \
  V(sminp, NEON_SMINP, vd.IsVector() && !vd.IsLaneSizeD())         \
  V(umax, NEON_UMAX, vd.IsVector() && !vd.IsLaneSizeD())           \
  V(umaxp, NEON_UMAXP, vd.IsVector() && !vd.IsLaneSizeD())         \
  V(umin, NEON_UMIN, vd.IsVector() && !vd.IsLaneSizeD())           \
  V(uminp, NEON_UMINP, vd.IsVector() && !vd.IsLaneSizeD())         \
  V(saba, NEON_SABA, vd.IsVector() && !vd.IsLaneSizeD())           \
  V(sabd, NEON_SABD, vd.IsVector() && !vd.IsLaneSizeD())           \
  V(uaba, NEON_UABA, vd.IsVector() && !vd.IsLaneSizeD())           \
  V(uabd, NEON_UABD, vd.IsVector() && !vd.IsLaneSizeD())           \
  V(mla, NEON_MLA, vd.IsVector() && !vd.IsLaneSizeD())             \
  V(mls, NEON_MLS, vd.IsVector() && !vd.IsLaneSizeD())             \
  V(mul, NEON_MUL, vd.IsVector() && !vd.IsLaneSizeD())             \
  V(and_, NEON_AND, vd.Is8B() || vd.Is16B())                       \
  V(orr, NEON_ORR, vd.Is8B() || vd.Is16B())                        \
  V(orn, NEON_ORN, vd.Is8B() || vd.Is16B())                        \
  V(eor, NEON_EOR, vd.Is8B() || vd.Is16B())                        \
  V(bic, NEON_BIC, vd.Is8B() || vd.Is16B())                        \
  V(bit, NEON_BIT, vd.Is8B() || vd.Is16B())                        \
  V(bif, NEON_BIF, vd.Is8B() || vd.Is16B())                        \
  V(bsl, NEON_BSL, vd.Is8B() || vd.Is16B())                        \
  V(pmul, NEON_PMUL, vd.Is8B() || vd.Is16B())                      \
  V(uqadd, NEON_UQADD, true)                                       \
  V(sqadd, NEON_SQADD, true)                                       \
  V(uqsub, NEON_UQSUB, true)                                       \
  V(sqsub, NEON_SQSUB, true)                                       \
  V(sqshl, NEON_SQSHL, true)                                       \
  V(uqshl, NEON_UQSHL, true)                                       \
  V(sqrshl, NEON_SQRSHL, true)                                     \
  V(uqrshl, NEON_UQRSHL, true)

#define DEFINE_ASM_FUNC(FN, OP, AS)                            \
  void Assembler::FN(const VRegister& vd, const VRegister& vn, \
                     const VRegister& vm) {                    \
    DCHECK(AS);                                                \
    NEON3Same(vd, vn, vm, OP);                                 \
  }
NEON_3SAME_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

#define NEON_FP3SAME_LIST_V2(V)                 \
  V(fadd, NEON_FADD, FADD)                      \
  V(fsub, NEON_FSUB, FSUB)                      \
  V(fmul, NEON_FMUL, FMUL)                      \
  V(fdiv, NEON_FDIV, FDIV)                      \
  V(fmax, NEON_FMAX, FMAX)                      \
  V(fmaxnm, NEON_FMAXNM, FMAXNM)                \
  V(fmin, NEON_FMIN, FMIN)                      \
  V(fminnm, NEON_FMINNM, FMINNM)                \
  V(fmulx, NEON_FMULX, NEON_FMULX_scalar)       \
  V(frecps, NEON_FRECPS, NEON_FRECPS_scalar)    \
  V(frsqrts, NEON_FRSQRTS, NEON_FRSQRTS_scalar) \
  V(fabd, NEON_FABD, NEON_FABD_scalar)          \
  V(fmla, NEON_FMLA, 0)                         \
  V(fmls, NEON_FMLS, 0)                         \
  V(facge, NEON_FACGE, NEON_FACGE_scalar)       \
  V(facgt, NEON_FACGT, NEON_FACGT_scalar)       \
  V(fcmeq, NEON_FCMEQ, NEON_FCMEQ_scalar)       \
  V(fcmge, NEON_FCMGE, NEON_FCMGE_scalar)       \
  V(fcmgt, NEON_FCMGT, NEON_FCMGT_scalar)       \
  V(faddp, NEON_FADDP, 0)                       \
  V(fmaxp, NEON_FMAXP, 0)                       \
  V(fminp, NEON_FMINP, 0)                       \
  V(fmaxnmp, NEON_FMAXNMP, 0)                   \
  V(fminnmp, NEON_FMINNMP, 0)

#define DEFINE_ASM_FUNC(FN, VEC_OP, SCA_OP)                                  \
  void Assembler::FN(const VRegister& vd, const VRegister& vn,               \
                     const VRegister& vm) {                                  \
    Instr op;                                                                \
    if ((SCA_OP != 0) && vd.IsScalar()) {                                    \
      DCHECK(vd.Is1S() || vd.Is1D());                                        \
      op = SCA_OP;                                                           \
    } else {                                                                 \
      DCHECK(vd.IsVector());                                                 \
      DCHECK(vd.Is2S() || vd.Is2D() || vd.Is4S() || vd.Is4H() || vd.Is8H()); \
      op = VEC_OP;                                                           \
    }                                                                        \
    NEONFP3Same(vd, vn, vm, op);                                             \
  }
NEON_FP3SAME_LIST_V2(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

void Assembler::addp(const VRegister& vd, const VRegister& vn) {
  DCHECK((vd.Is1D() && vn.Is2D()));
  Emit(SFormat(vd) | NEON_ADDP_scalar | Rn(vn) | Rd(vd));
}

void Assembler::faddp(const VRegister& vd, const VRegister& vn) {
  DCHECK((vd.Is1S() && vn.Is2S()) || (vd.Is1D() && vn.Is2D()));
  Emit(FPFormat(vd) | NEON_FADDP_scalar | Rn(vn) | Rd(vd));
}

void Assembler::fmaxp(const VRegister& vd, const VRegister& vn) {
  DCHECK((vd.Is1S() && vn.Is2S()) || (vd.Is1D() && vn.Is2D()));
  Emit(FPFormat(vd) | NEON_FMAXP_scalar | Rn(vn) | Rd(vd));
}

void Assembler::fminp(const VRegister& vd, const VRegister& vn) {
  DCHECK((vd.Is1S() && vn.Is2S()) || (vd.Is1D() && vn.Is2D()));
  Emit(FPFormat(vd) | NEON_FMINP_scalar | Rn(vn) | Rd(vd));
}

void Assembler::fmaxnmp(const VRegister& vd, const VRegister& vn) {
  DCHECK((vd.Is1S() && vn.Is2S()) || (vd.Is1D() && vn.Is2D()));
  Emit(FPFormat(vd) | NEON_FMAXNMP_scalar | Rn(vn) | Rd(vd));
}

void Assembler::fminnmp(const VRegister& vd, const VRegister& vn) {
  DCHECK((vd.Is1S() && vn.Is2S()) || (vd.Is1D() && vn.Is2D()));
  Emit(FPFormat(vd) | NEON_FMINNMP_scalar | Rn(vn) | Rd(vd));
}

void Assembler::orr(const VRegister& vd, const int imm8, const int left_shift) {
  NEONModifiedImmShiftLsl(vd, imm8, left_shift, NEONModifiedImmediate_ORR);
}

void Assembler::mov(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  if (vd.IsD()) {
    orr(vd.V8B(), vn.V8B(), vn.V8B());
  } else {
    DCHECK(vd.IsQ());
    orr(vd.V16B(), vn.V16B(), vn.V16B());
  }
}

void Assembler::bic(const VRegister& vd, const int imm8, const int left_shift) {
  NEONModifiedImmShiftLsl(vd, imm8, left_shift, NEONModifiedImmediate_BIC);
}

void Assembler::movi(const VRegister& vd, const uint64_t imm, Shift shift,
                     const int shift_amount) {
  DCHECK((shift == LSL) || (shift == MSL));
  if (vd.Is2D() || vd.Is1D()) {
    DCHECK_EQ(shift_amount, 0);
    int imm8 = 0;
    for (int i = 0; i < 8; ++i) {
      int byte = (imm >> (i * 8)) & 0xFF;
      DCHECK((byte == 0) || (byte == 0xFF));
      if (byte == 0xFF) {
        imm8 |= (1 << i);
      }
    }
    Instr q = vd.Is2D() ? NEON_Q : 0;
    Emit(q | NEONModImmOp(1) | NEONModifiedImmediate_MOVI |
         ImmNEONabcdefgh(imm8) | NEONCmode(0xE) | Rd(vd));
  } else if (shift == LSL) {
    DCHECK(is_uint8(imm));
    NEONModifiedImmShiftLsl(vd, static_cast<int>(imm), shift_amount,
                            NEONModifiedImmediate_MOVI);
  } else {
    DCHECK(is_uint8(imm));
    NEONModifiedImmShiftMsl(vd, static_cast<int>(imm), shift_amount,
                            NEONModifiedImmediate_MOVI);
  }
}

void Assembler::mvn(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  if (vd.IsD()) {
    not_(vd.V8B(), vn.V8B());
  } else {
    DCHECK(vd.IsQ());
    not_(vd.V16B(), vn.V16B());
  }
}

void Assembler::mvni(const VRegister& vd, const int imm8, Shift shift,
                     const int shift_amount) {
  DCHECK((shift == LSL) || (shift == MSL));
  if (shift == LSL) {
    NEONModifiedImmShiftLsl(vd, imm8, shift_amount, NEONModifiedImmediate_MVNI);
  } else {
    NEONModifiedImmShiftMsl(vd, imm8, shift_amount, NEONModifiedImmediate_MVNI);
  }
}

void Assembler::NEONFPByElement(const VRegister& vd, const VRegister& vn,
                                const VRegister& vm, int vm_index,
                                NEONByIndexedElementOp vop) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK((vd.Is2S() && vm.Is1S()) || (vd.Is4S() && vm.Is1S()) ||
         (vd.Is1S() && vm.Is1S()) || (vd.Is2D() && vm.Is1D()) ||
         (vd.Is1D() && vm.Is1D()));
  DCHECK((vm.Is1S() && (vm_index < 4)) || (vm.Is1D() && (vm_index < 2)));

  Instr op = vop;
  int index_num_bits = vm.Is1S() ? 2 : 1;
  if (vd.IsScalar()) {
    op |= NEON_Q | NEONScalar;
  }

  Emit(FPFormat(vd) | op | ImmNEONHLM(vm_index, index_num_bits) | Rm(vm) |
       Rn(vn) | Rd(vd));
}

void Assembler::NEONByElement(const VRegister& vd, const VRegister& vn,
                              const VRegister& vm, int vm_index,
                              NEONByIndexedElementOp vop) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK((vd.Is4H() && vm.Is1H()) || (vd.Is8H() && vm.Is1H()) ||
         (vd.Is1H() && vm.Is1H()) || (vd.Is2S() && vm.Is1S()) ||
         (vd.Is4S() && vm.Is1S()) || (vd.Is1S() && vm.Is1S()));
  DCHECK((vm.Is1H() && (vm.code() < 16) && (vm_index < 8)) ||
         (vm.Is1S() && (vm_index < 4)));

  Instr format, op = vop;
  int index_num_bits = vm.Is1H() ? 3 : 2;
  if (vd.IsScalar()) {
    op |= NEONScalar | NEON_Q;
    format = SFormat(vn);
  } else {
    format = VFormat(vn);
  }
  Emit(format | op | ImmNEONHLM(vm_index, index_num_bits) | Rm(vm) | Rn(vn) |
       Rd(vd));
}

void Assembler::NEONByElementL(const VRegister& vd, const VRegister& vn,
                               const VRegister& vm, int vm_index,
                               NEONByIndexedElementOp vop) {
  DCHECK((vd.Is4S() && vn.Is4H() && vm.Is1H()) ||
         (vd.Is4S() && vn.Is8H() && vm.Is1H()) ||
         (vd.Is1S() && vn.Is1H() && vm.Is1H()) ||
         (vd.Is2D() && vn.Is2S() && vm.Is1S()) ||
         (vd.Is2D() && vn.Is4S() && vm.Is1S()) ||
         (vd.Is1D() && vn.Is1S() && vm.Is1S()));

  DCHECK((vm.Is1H() && (vm.code() < 16) && (vm_index < 8)) ||
         (vm.Is1S() && (vm_index < 4)));

  Instr format, op = vop;
  int index_num_bits = vm.Is1H() ? 3 : 2;
  if (vd.IsScalar()) {
    op |= NEONScalar | NEON_Q;
    format = SFormat(vn);
  } else {
    format = VFormat(vn);
  }
  Emit(format | op | ImmNEONHLM(vm_index, index_num_bits) | Rm(vm) | Rn(vn) |
       Rd(vd));
}

#define NEON_BYELEMENT_LIST(V)              \
  V(mul, NEON_MUL_byelement, vn.IsVector()) \
  V(mla, NEON_MLA_byelement, vn.IsVector()) \
  V(mls, NEON_MLS_byelement, vn.IsVector()) \
  V(sqdmulh, NEON_SQDMULH_byelement, true)  \
  V(sqrdmulh, NEON_SQRDMULH_byelement, true)

#define DEFINE_ASM_FUNC(FN, OP, AS)                            \
  void Assembler::FN(const VRegister& vd, const VRegister& vn, \
                     const VRegister& vm, int vm_index) {      \
    DCHECK(AS);                                                \
    NEONByElement(vd, vn, vm, vm_index, OP);                   \
  }
NEON_BYELEMENT_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

#define NEON_FPBYELEMENT_LIST(V) \
  V(fmul, NEON_FMUL_byelement)   \
  V(fmla, NEON_FMLA_byelement)   \
  V(fmls, NEON_FMLS_byelement)   \
  V(fmulx, NEON_FMULX_byelement)

#define DEFINE_ASM_FUNC(FN, OP)                                \
  void Assembler::FN(const VRegister& vd, const VRegister& vn, \
                     const VRegister& vm, int vm_index) {      \
    NEONFPByElement(vd, vn, vm, vm_index, OP);                 \
  }
NEON_FPBYELEMENT_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

#define NEON_BYELEMENT_LONG_LIST(V)                              \
  V(sqdmull, NEON_SQDMULL_byelement, vn.IsScalar() || vn.IsD())  \
  V(sqdmull2, NEON_SQDMULL_byelement, vn.IsVector() && vn.IsQ()) \
  V(sqdmlal, NEON_SQDMLAL_byelement, vn.IsScalar() || vn.IsD())  \
  V(sqdmlal2, NEON_SQDMLAL_byelement, vn.IsVector() && vn.IsQ()) \
  V(sqdmlsl, NEON_SQDMLSL_byelement, vn.IsScalar() || vn.IsD())  \
  V(sqdmlsl2, NEON_SQDMLSL_byelement, vn.IsVector() && vn.IsQ()) \
  V(smull, NEON_SMULL_byelement, vn.IsVector() && vn.IsD())      \
  V(smull2, NEON_SMULL_byelement, vn.IsVector() && vn.IsQ())     \
  V(umull, NEON_UMULL_byelement, vn.IsVector() && vn.IsD())      \
  V(umull2, NEON_UMULL_byelement, vn.IsVector() && vn.IsQ())     \
  V(smlal, NEON_SMLAL_byelement, vn.IsVector() && vn.IsD())      \
  V(smlal2, NEON_SMLAL_byelement, vn.IsVector() && vn.IsQ())     \
  V(umlal, NEON_UMLAL_byelement, vn.IsVector() && vn.IsD())      \
  V(umlal2, NEON_UMLAL_byelement, vn.IsVector() && vn.IsQ())     \
  V(smlsl, NEON_SMLSL_byelement, vn.IsVector() && vn.IsD())      \
  V(smlsl2, NEON_SMLSL_byelement, vn.IsVector() && vn.IsQ())     \
  V(umlsl, NEON_UMLSL_byelement, vn.IsVector() && vn.IsD())      \
  V(umlsl2, NEON_UMLSL_byelement, vn.IsVector() && vn.IsQ())

#define DEFINE_ASM_FUNC(FN, OP, AS)                            \
  void Assembler::FN(const VRegister& vd, const VRegister& vn, \
                     const VRegister& vm, int vm_index) {      \
    DCHECK(AS);                                                \
    NEONByElementL(vd, vn, vm, vm_index, OP);                  \
  }
NEON_BYELEMENT_LONG_LIST(DEFINE_ASM_FUNC)
#undef DEFINE_ASM_FUNC

void Assembler::suqadd(const VRegister& vd, const VRegister& vn) {
  NEON2RegMisc(vd, vn, NEON_SUQADD);
}

void Assembler::usqadd(const VRegister& vd, const VRegister& vn) {
  NEON2RegMisc(vd, vn, NEON_USQADD);
}

void Assembler::abs(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEON2RegMisc(vd, vn, NEON_ABS);
}

void Assembler::sqabs(const VRegister& vd, const VRegister& vn) {
  NEON2RegMisc(vd, vn, NEON_SQABS);
}

void Assembler::neg(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsVector() || vd.Is1D());
  NEON2RegMisc(vd, vn, NEON_NEG);
}

void Assembler::sqneg(const VRegister& vd, const VRegister& vn) {
  NEON2RegMisc(vd, vn, NEON_SQNEG);
}

void Assembler::NEONXtn(const VRegister& vd, const VRegister& vn,
                        NEON2RegMiscOp vop) {
  Instr format, op = vop;
  if (vd.IsScalar()) {
    DCHECK((vd.Is1B() && vn.Is1H()) || (vd.Is1H() && vn.Is1S()) ||
           (vd.Is1S() && vn.Is1D()));
    op |= NEON_Q | NEONScalar;
    format = SFormat(vd);
  } else {
    DCHECK((vd.Is8B() && vn.Is8H()) || (vd.Is4H() && vn.Is4S()) ||
           (vd.Is2S() && vn.Is2D()) || (vd.Is16B() && vn.Is8H()) ||
           (vd.Is8H() && vn.Is4S()) || (vd.Is4S() && vn.Is2D()));
    format = VFormat(vd);
  }
  Emit(format | op | Rn(vn) | Rd(vd));
}

void Assembler::xtn(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsVector() && vd.IsD());
  NEONXtn(vd, vn, NEON_XTN);
}

void Assembler::xtn2(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsVector() && vd.IsQ());
  NEONXtn(vd, vn, NEON_XTN);
}

void Assembler::sqxtn(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsScalar() || vd.IsD());
  NEONXtn(vd, vn, NEON_SQXTN);
}

void Assembler::sqxtn2(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsVector() && vd.IsQ());
  NEONXtn(vd, vn, NEON_SQXTN);
}

void Assembler::sqxtun(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsScalar() || vd.IsD());
  NEONXtn(vd, vn, NEON_SQXTUN);
}

void Assembler::sqxtun2(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsVector() && vd.IsQ());
  NEONXtn(vd, vn, NEON_SQXTUN);
}

void Assembler::uqxtn(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsScalar() || vd.IsD());
  NEONXtn(vd, vn, NEON_UQXTN);
}

void Assembler::uqxtn2(const VRegister& vd, const VRegister& vn) {
  DCHECK(vd.IsVector() && vd.IsQ());
  NEONXtn(vd, vn, NEON_UQXTN);
}

// NEON NOT and RBIT are distinguised by bit 22, the bottom bit of "size".
void Assembler::not_(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(vd.Is8B() || vd.Is16B());
  Emit(VFormat(vd) | NEON_RBIT_NOT | Rn(vn) | Rd(vd));
}

void Assembler::rbit(const VRegister& vd, const VRegister& vn) {
  DCHECK(AreSameFormat(vd, vn));
  DCHECK(vd.Is8B() || vd.Is16B());
  Emit(VFormat(vn) | (1 << NEONSize_offset) | NEON_RBIT_NOT | Rn(vn) | Rd(vd));
}

void Assembler::ext(const VRegister& vd, const VRegister& vn,
                    const VRegister& vm, int index) {
  DCHECK(AreSameFormat(vd, vn, vm));
  DCHECK(vd.Is8B() || vd.Is16B());
  DCHECK((0 <= index) && (index < vd.LaneCount()));
  Emit(VFormat(vd) | NEON_EXT | Rm(vm) | ImmNEONExt(index) | Rn(vn) | Rd(vd));
}

void Assembler::dup(const VRegister& vd, const VRegister& vn, int vn_index) {
  Instr q, scalar;

  // We support vn arguments of the form vn.VxT() or vn.T(), where x is the
  // number of lanes, and T is b, h, s or d.
  int lane_size = vn.LaneSizeInBytes();
  NEONFormatField format;
  switch (lane_size) {
    case 1:
      format = NEON_16B;
      break;
    case 2:
      format = NEON_8H;
      break;
    case 4:
      format = NEON_4S;
      break;
    default:
      DCHECK_EQ(lane_size, 8);
      format = NEON_2D;
      break;
  }

  if (vd.IsScalar()) {
    q = NEON_Q;
    scalar = NEONScalar;
  } else {
    DCHECK(!vd.Is1D());
    q = vd.IsD() ? 0 : NEON_Q;
    scalar = 0;
  }
  Emit(q | scalar | NEON_DUP_ELEMENT | ImmNEON5(format, vn_index) | Rn(vn) |
       Rd(vd));
}

void Assembler::dcptr(Label* label) {
  BlockPoolsScope no_pool_inbetween(this);
  RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE);
  if (label->is_bound()) {
    // The label is bound, so it does not need to be updated and the internal
    // reference should be emitted.
    //
    // In this case, label->pos() returns the offset of the label from the
    // start of the buffer.
    internal_reference_positions_.push_back(pc_offset());
    dc64(reinterpret_cast<uintptr_t>(buffer_start_ + label->pos()));
  } else {
    int32_t offset;
    if (label->is_linked()) {
      // The label is linked, so the internal reference should be added
      // onto the end of the label's link chain.
      //
      // In this case, label->pos() returns the offset of the last linked
      // instruction from the start of the buffer.
      offset = label->pos() - pc_offset();
      DCHECK_NE(offset, kStartOfLabelLinkChain);
    } else {
      // The label is unused, so it now becomes linked and the internal
      // reference is at the start of the new link chain.
      offset = kStartOfLabelLinkChain;
    }
    // The instruction at pc is now the last link in the label's chain.
    label->link_to(pc_offset());

    // Traditionally the offset to the previous instruction in the chain is
    // encoded in the instruction payload (e.g. branch range) but internal
    // references are not instructions so while unbound they are encoded as
    // two consecutive brk instructions. The two 16-bit immediates are used
    // to encode the offset.
    offset >>= kInstrSizeLog2;
    DCHECK(is_int32(offset));
    uint32_t high16 = unsigned_bitextract_32(31, 16, offset);
    uint32_t low16 = unsigned_bitextract_32(15, 0, offset);

    brk(high16);
    brk(low16);
  }
}

// Below, a difference in case for the same letter indicates a
// negated bit. If b is 1, then B is 0.
uint32_t Assembler::FPToImm8(double imm) {
  uint64_t bits = base::bit_cast<uint64_t>(imm);
  DCHECK(IsImmFP64(bits));
  // bits: aBbb.bbbb.bbcd.efgh.0000.0000.0000.0000
  //       0000.0000.0000.0000.0000.0000.0000.0000
  // bit7: a000.0000
  uint64_t bit7 = ((bits >> 63) & 0x1) << 7;
  // bit6: 0b00.0000
  uint64_t bit6 = ((bits >> 61) & 0x1) << 6;
  // bit5_to_0: 00cd.efgh
  uint64_t bit5_to_0 = (bits >> 48) & 0x3F;

  return static_cast<uint32_t>(bit7 | bit6 | bit5_to_0);
}

Instr Assembler::ImmFP(double imm) { return FPToImm8(imm) << ImmFP_offset; }
Instr Assembler::ImmNEONFP(double imm) {
  return ImmNEONabcdefgh(FPToImm8(imm));
}

// InstructionStream generation helpers.
void Assembler::MoveWide(const Register& rd, uint64_t imm, int shift,
                         MoveWideImmediateOp mov_op) {
  // Ignore the top 32 bits of an immediate if we're moving to a W register.
  if (rd.Is32Bits()) {
    // Check that the top 32 bits are zero (a positive 32-bit number) or top
    // 33 bits are one (a negative 32-bit number, sign extended to 64 bits).
    DCHECK(((imm >> kWRegSizeInBits) == 0) ||
           ((imm >> (kWRegSizeInBits - 1)) == 0x1FFFFFFFF));
    imm &= kWRegMask;
  }

  if (shift >= 0) {
    // Explicit shift specified.
    DCHECK((shift == 0) || (shift == 16) || (shift == 32) || (shift == 48));
    DCHECK(rd.Is64Bits() || (shift == 0) || (shift == 16));
    shift /= 16;
  } else {
    // Calculate a new immediate and shift combination to encode the immediate
    // argument.
    shift = 0;
    if ((imm & ~0xFFFFULL) == 0) {
      // Nothing to do.
    } else if ((imm & ~(0xFFFFULL << 16)) == 0) {
      imm >>= 16;
      shift = 1;
    } else if ((imm & ~(0xFFFFULL << 32)) == 0) {
      DCHECK(rd.Is64Bits());
      imm >>= 32;
      shift = 2;
    } else if ((imm & ~(0xFFFFULL << 48)) == 0) {
      DCHECK(rd.Is64Bits());
      imm >>= 48;
      shift = 3;
    }
  }

  DCHECK(is_uint16(imm));

  Emit(SF(rd) | MoveWideImmediateFixed | mov_op | Rd(rd) |
       ImmMoveWide(static_cast<int>(imm)) | ShiftMoveWide(shift));
}

void Assembler::AddSub(const Register& rd, const Register& rn,
                       const Operand& operand, FlagsUpdate S, AddSubOp op) {
  DCHECK_EQ(rd.SizeInBits(), rn.SizeInBits());
  DCHECK(!operand.NeedsRelocation(this));
  if (operand.IsImmediate()) {
    int64_t immediate = operand.ImmediateValue();
    DCHECK(IsImmAddSub(immediate));
    Instr dest_reg = (S == SetFlags) ? Rd(rd) : RdSP(rd);
    Emit(SF(rd) | AddSubImmediateFixed | op | Flags(S) |
         ImmAddSub(static_cast<int>(immediate)) | dest_reg | RnSP(rn));
  } else if (operand.IsShiftedRegister()) {
    DCHECK_EQ(operand.reg().SizeInBits(), rd.SizeInBits());
    DCHECK_NE(operand.shift(), ROR);

    // For instructions of the form:
    //   add/sub   wsp, <Wn>, <Wm> [, LSL #0-3 ]
    //   add/sub   <Wd>, wsp, <Wm> [, LSL #0-3 ]
    //   add/sub   wsp, wsp, <Wm> [, LSL #0-3 ]
    //   adds/subs <Wd>, wsp, <Wm> [, LSL #0-3 ]
    // or their 64-bit register equivalents, convert the operand from shifted to
    // extended register mode, and emit an add/sub extended instruction.
    if (rn.IsSP() || rd.IsSP()) {
      DCHECK(!(rd.IsSP() && (S == SetFlags)));
      DataProcExtendedRegister(rd, rn, operand.ToExtendedRegister(), S,
                               AddSubExtendedFixed | op);
    } else {
      DataProcShiftedRegister(rd, rn, operand, S, AddSubShiftedFixed | op);
    }
  } else {
    DCHECK(operand.IsExtendedRegister());
    DataProcExtendedRegister(rd, rn, operand, S, AddSubExtendedFixed | op);
  }
}

void Assembler::AddSubWithCarry(const Register& rd, const Register& rn,
                                const Operand& operand, FlagsUpdate S,
                                AddSubWithCarryOp op) {
  DCHECK_EQ(rd.SizeInBits(), rn.SizeInBits());
  DCHECK_EQ(rd.SizeInBits(), operand.reg().SizeInBits());
  DCHECK(operand.IsShiftedRegister() && (operand.shift_amount() == 0));
  DCHECK(!operand.NeedsRelocation(this));
  Emit(SF(rd) | op | Flags(S) | Rm(operand.reg()) | Rn(rn) | Rd(rd));
}

void Assembler::hlt(int code) {
  DCHECK(is_uint16(code));
  Emit(HLT | ImmException(code));
}

void Assembler::brk(int code) {
  DCHECK(is_uint16(code));
  Emit(BRK | ImmException(code));
}

void Assembler::EmitStringData(const char* string) {
  size_t len = strlen(string) + 1;
  DCHECK_LE(RoundUp(len, kInstrSize), static_cast<size_t>(kGap));
  EmitData(string, static_cast<int>(len));
  // Pad with nullptr characters until pc_ is aligned.
  const char pad[] = {'\0', '\0', '\0', '\0'};
  static_assert(sizeof(pad) == kInstrSize,
                "Size of padding must match instruction size.");
  EmitData(pad, RoundUp(pc_offset(), kInstrSize) - pc_offset());
}

void Assembler::debug(const char* message, uint32_t code, Instr params) {
  if (options().enable_simulator_code) {
    size_t size_of_debug_sequence =
        4 * kInstrSize + RoundUp<kInstrSize>(strlen(message) + 1);

    // The arguments to the debug marker need to be contiguous in memory, so
    // make sure we don't try to emit pools.
    BlockPoolsScope scope(this, size_of_debug_sequence);

    Label start;
    bind(&start);

    // Refer to instructions-arm64.h for a description of the marker and its
    // arguments.
    hlt(kImmExceptionIsDebug);
    DCHECK_EQ(SizeOfCodeGeneratedSince(&start), kDebugCodeOffset);
    dc32(code);
    DCHECK_EQ(SizeOfCodeGeneratedSince(&start), kDebugParamsOffset);
    dc32(params);
    DCHECK_EQ(SizeOfCodeGeneratedSince(&start), kDebugMessageOffset);
    EmitStringData(message);
    hlt(kImmExceptionIsUnreachable);
    DCHECK_EQ(SizeOfCodeGeneratedSince(&start), size_of_debug_sequence);

    return;
  }

  if (params & BREAK) {
    brk(0);
  }
}

void Assembler::Logical(const Register& rd, const Register& rn,
                        const Operand& operand, LogicalOp op) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  DCHECK(!operand.NeedsRelocation(this));
  if (operand.IsImmediate()) {
    int64_t immediate = operand.ImmediateValue();
    unsigned reg_size = rd.SizeInBits();

    DCHECK_NE(immediate, 0);
    DCHECK_NE(immediate, -1);
    DCHECK(rd.Is64Bits() || is_uint32(immediate));

    // If the operation is NOT, invert the operation and immediate.
    if ((op & NOT) == NOT) {
      op = static_cast<LogicalOp>(op & ~NOT);
      immediate = rd.Is64Bits() ? ~immediate : (~immediate & kWRegMask);
    }

    unsigned n, imm_s, imm_r;
    if (IsImmLogical(immediate, reg_size, &n, &imm_s, &imm_r)) {
      // Immediate can be encoded in the instruction.
      LogicalImmediate(rd, rn, n, imm_s, imm_r, op);
    } else {
      // This case is handled in the macro assembler.
      UNREACHABLE();
    }
  } else {
    DCHECK(operand.IsShiftedRegister());
    DCHECK(operand.reg().SizeInBits() == rd.SizeInBits());
    Instr dp_op = static_cast<Instr>(op | LogicalShiftedFixed);
    DataProcShiftedRegister(rd, rn, operand, LeaveFlags, dp_op);
  }
}

void Assembler::LogicalImmediate(const Register& rd, const Register& rn,
                                 unsigned n, unsigned imm_s, unsigned imm_r,
                                 LogicalOp op) {
  unsigned reg_size = rd.SizeInBits();
  Instr dest_reg = (op == ANDS) ? Rd(rd) : RdSP(rd);
  Emit(SF(rd) | LogicalImmediateFixed | op | BitN(n, reg_size) |
       ImmSetBits(imm_s, reg_size) | ImmRotate(imm_r, reg_size) | dest_reg |
       Rn(rn));
}

void Assembler::ConditionalCompare(const Register& rn, const Operand& operand,
                                   StatusFlags nzcv, Condition cond,
                                   ConditionalCompareOp op) {
  Instr ccmpop;
  DCHECK(!operand.NeedsRelocation(this));
  if (operand.IsImmediate()) {
    int64_t immediate = operand.ImmediateValue();
    DCHECK(IsImmConditionalCompare(immediate));
    ccmpop = ConditionalCompareImmediateFixed | op |
             ImmCondCmp(static_cast<unsigned>(immediate));
  } else {
    DCHECK(operand.IsShiftedRegister() && (operand.shift_amount() == 0));
    ccmpop = ConditionalCompareRegisterFixed | op | Rm(operand.reg());
  }
  Emit(SF(rn) | ccmpop | Cond(cond) | Rn(rn) | Nzcv(nzcv));
}

void Assembler::DataProcessing1Source(const Register& rd, const Register& rn,
                                      DataProcessing1SourceOp op) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  Emit(SF(rn) | op | Rn(rn) | Rd(rd));
}

void Assembler::FPDataProcessing1Source(const VRegister& vd,
                                        const VRegister& vn,
                                        FPDataProcessing1SourceOp op) {
  Emit(FPType(vn) | op | Rn(vn) | Rd(vd));
}

void Assembler::FPDataProcessing2Source(const VRegister& fd,
                                        const VRegister& fn,
                                        const VRegister& fm,
                                        FPDataProcessing2SourceOp op) {
  DCHECK(fd.SizeInBits() == fn.SizeInBits());
  DCHECK(fd.SizeInBits() == fm.SizeInBits());
  Emit(FPType(fd) | op | Rm(fm) | Rn(fn) | Rd(fd));
}

void Assembler::FPDataProcessing3Source(const VRegister& fd,
                                        const VRegister& fn,
                                        const VRegister& fm,
                                        const VRegister& fa,
                                        FPDataProcessing3SourceOp op) {
  DCHECK(AreSameSizeAndType(fd, fn, fm, fa));
  Emit(FPType(fd) | op | Rm(fm) | Rn(fn) | Rd(fd) | Ra(fa));
}

void Assembler::NEONModifiedImmShiftLsl(const VRegister& vd, const int imm8,
                                        const int left_shift,
                                        NEONModifiedImmediateOp op) {
  DCHECK(vd.Is8B() || vd.Is16B() || vd.Is4H() || vd.Is8H() || vd.Is2S() ||
         vd.Is4S());
  DCHECK((left_shift == 0) || (left_shift == 8) || (left_shift == 16) ||
         (left_shift == 24));
  DCHECK(is_uint8(imm8));

  int cmode_1, cmode_2, cmode_3;
  if (vd.Is8B() || vd.Is16B()) {
    DCHECK_EQ(op, NEONModifiedImmediate_MOVI);
    cmode_1 = 1;
    cmode_2 = 1;
    cmode_3 = 1;
  } else {
    cmode_1 = (left_shift >> 3) & 1;
    cmode_2 = left_shift >> 4;
    cmode_3 = 0;
    if (vd.Is4H() || vd.Is8H()) {
      DCHECK((left_shift == 0) || (left_shift == 8));
      cmode_3 = 1;
    }
  }
  int cmode = (cmode_3 << 3) | (cmode_2 << 2) | (cmode_1 << 1);

  Instr q = vd.IsQ() ? NEON_Q : 0;

  Emit(q | op | ImmNEONabcdefgh(imm8) | NEONCmode(cmode) | Rd(vd));
}

void Assembler::NEONModifiedImmShiftMsl(const VRegister& vd, const int imm8,
                                        const int shift_amount,
                                        NEONModifiedImmediateOp op) {
  DCHECK(vd.Is2S() || vd.Is4S());
  DCHECK((shift_amount == 8) || (shift_amount == 16));
  DCHECK(is_uint8(imm8));

  int cmode_0 = (shift_amount >> 4) & 1;
  int cmode = 0xC | cmode_0;

  Instr q = vd.IsQ() ? NEON_Q : 0;

  Emit(q | op | ImmNEONabcdefgh(imm8) | NEONCmode(cmode) | Rd(vd));
}

void Assembler::EmitShift(const Register& rd, const Register& rn, Shift shift,
                          unsigned shift_amount) {
  switch (shift) {
    case LSL:
      lsl(rd, rn, shift_amount);
      break;
    case LSR:
      lsr(rd, rn, shift_amount);
      break;
    case ASR:
      asr(rd, rn, shift_amount);
      break;
    case ROR:
      ror(rd, rn, shift_amount);
      break;
    default:
      UNREACHABLE();
  }
}

void Assembler::EmitExtendShift(const Register& rd, const Register& rn,
                                Extend extend, unsigned left_shift) {
  DCHECK(rd.SizeInBits() >= rn.SizeInBits());
  unsigned reg_size = rd.SizeInBits();
  // Use the correct size of register.
  Register rn_ = Register::Create(rn.code(), rd.SizeInBits());
  // Bits extracted are high_bit:0.
  unsigned high_bit = (8 << (extend & 0x3)) - 1;
  // Number of bits left in the result that are not introduced by the shift.
  unsigned non_shift_bits = (reg_size - left_shift) & (reg_size - 1);

  if ((non_shift_bits > high_bit) || (non_shift_bits == 0)) {
    switch (extend) {
      case UXTB:
      case UXTH:
      case UXTW:
        ubfm(rd, rn_, non_shift_bits, high_bit);
        break;
      case SXTB:
      case SXTH:
      case SXTW:
        sbfm(rd, rn_, non_shift_bits, high_bit);
        break;
      case UXTX:
      case SXTX: {
        DCHECK_EQ(rn.SizeInBits(), kXRegSizeInBits);
        // Nothing to extend. Just shift.
        lsl(rd, rn_, left_shift);
        break;
      }
      default:
        UNREACHABLE();
    }
  } else {
    // No need to extend as the extended bits would be shifted away.
    lsl(rd, rn_, left_shift);
  }
}

void Assembler::DataProcShiftedRegister(const Register& rd, const Register& rn,
                                        const Operand& operand, FlagsUpdate S,
                                        Instr op) {
  DCHECK(operand.IsShiftedRegister());
  DCHECK(rn.Is64Bits() || (rn.Is32Bits() && is_uint5(operand.shift_amount())));
  DCHECK(!operand.NeedsRelocation(this));
  Emit(SF(rd) | op | Flags(S) | ShiftDP(operand.shift()) |
       ImmDPShift(operand.shift_amount()) | Rm(operand.reg()) | Rn(rn) |
       Rd(rd));
}

void Assembler::DataProcExtendedRegister(const Register& rd, const Register& rn,
                                         const Operand& operand, FlagsUpdate S,
                                         Instr op) {
  DCHECK(!operand.NeedsRelocation(this));
  Instr dest_reg = (S == SetFlags) ? Rd(rd) : RdSP(rd);
  Emit(SF(rd) | op | Flags(S) | Rm(operand.reg()) |
       ExtendMode(operand.extend()) | ImmExtendShift(operand.shift_amount()) |
       dest_reg | RnSP(rn));
}

void Assembler::LoadStore(const CPURegister& rt, const MemOperand& addr,
                          LoadStoreOp op) {
  Instr memop = op | Rt(rt) | RnSP(addr.base());

  if (addr.IsImmediateOffset()) {
    unsigned size_log2 = CalcLSDataSizeLog2(op);
    int offset = static_cast<int>(addr.offset());
    if (IsImmLSScaled(addr.offset(), size_log2)) {
      LoadStoreScaledImmOffset(memop, offset, size_log2);
    } else {
      DCHECK(IsImmLSUnscaled(addr.offset()));
      LoadStoreUnscaledImmOffset(memop, offset);
    }
  } else if (addr.IsRegisterOffset()) {
    Extend ext = addr.extend();
    Shift shift = addr.shift();
    unsigned shift_amount = addr.shift_amount();

    // LSL is encoded in the option field as UXTX.
    if (shift == LSL) {
      ext = UXTX;
    }

    // Shifts are encoded in one bit, indicating a left shift by the memory
    // access size.
    DCHECK(shift_amount == 0 || shift_amount == CalcLSDataSizeLog2(op));
    Emit(LoadStoreRegisterOffsetFixed | memop | Rm(addr.regoffset()) |
         ExtendMode(ext) | ImmShiftLS((shift_amount > 0) ? 1 : 0));
  } else {
    // Pre-index and post-index modes.
    DCHECK(IsImmLSUnscaled(addr.offset()));
    DCHECK_NE(rt, addr.base());
    int offset = static_cast<int>(addr.offset());
    if (addr.IsPreIndex()) {
      Emit(LoadStorePreIndexFixed | memop | ImmLS(offset));
    } else {
      DCHECK(addr.IsPostIndex());
      Emit(LoadStorePostIndexFixed | memop | ImmLS(offset));
    }
  }
}

void Assembler::pmull(const VRegister& vd, const VRegister& vn,
                      const VRegister& vm) {
  DCHECK(AreSameFormat(vn, vm));
  DCHECK((vn.Is8B() && vd.Is8H()) || (vn.Is1D() && vd.Is1Q()));
  DCHECK(IsEnabled(PMULL1Q) || vd.Is8H());
  Emit(VFormat(vn) | NEON_PMULL | Rm(vm) | Rn(vn) | Rd(vd));
}

void Assembler::pmull2(const VRegister& vd, const VRegister& vn,
                       const VRegister& vm) {
  DCHECK(AreSameFormat(vn, vm));
  DCHECK((vn.Is16B() && vd.Is8H()) || (vn.Is2D() && vd.Is1Q()));
  DCHECK(IsEnabled(PMULL1Q) || vd.Is8H());
  Emit(VFormat(vn) | NEON_PMULL2 | Rm(vm) | Rn(vn) | Rd(vd));
}

bool Assembler::IsImmLSPair(int64_t offset, unsigned size) {
  bool offset_is_size_multiple =
      (static_cast<int64_t>(static_cast<uint64_t>(offset >> size) << size) ==
       offset);
  return offset_is_size_multiple && is_int7(offset >> size);
}

bool Assembler::IsImmLLiteral(int64_t offset) {
  int inst_size = static_cast<int>(kInstrSizeLog2);
  bool offset_is_inst_multiple =
      (static_cast<int64_t>(static_cast<uint64_t>(offset >> inst_size)
                            << inst_size) == offset);
  DCHECK_GT(offset, 0);
  offset >>= kLoadLiteralScaleLog2;
  return offset_is_inst_multiple && is_intn(offset, ImmLLiteral_width);
}

// Test if a given value can be encoded in the immediate field of a logical
// instruction.
// If it can be encoded, the function returns true, and values pointed to by n,
// imm_s and imm_r are updated with immediates encoded in the format required
// by the corresponding fields in the logical instruction.
// If it can not be encoded, the function returns false, and the values pointed
// to by n, imm_s and imm_r are undefined.
bool Assembler::IsImmLogical(uint64_t value, unsigned width, unsigned* n,
                             unsigned* imm_s, unsigned* imm_r) {
  DCHECK((n != nullptr) && (imm_s != nullptr) && (imm_r != nullptr));
  DCHECK((width == kWRegSizeInBits) || (width == kXRegSizeInBits));

  bool negate = false;

  // Logical immediates are encoded using parameters n, imm_s and imm_r using
  // the following table:
  //
  //    N   imms    immr    size        S             R
  //    1  ssssss  rrrrrr    64    UInt(ssssss)  UInt(rrrrrr)
  //    0  0sssss  xrrrrr    32    UInt(sssss)   UInt(rrrrr)
  //    0  10ssss  xxrrrr    16    UInt(ssss)    UInt(rrrr)
  //    0  110sss  xxxrrr     8    UInt(sss)     UInt(rrr)
  //    0  1110ss  xxxxrr     4    UInt(ss)      UInt(rr)
  //    0  11110s  xxxxxr     2    UInt(s)       UInt(r)
  // (s bits must not be all set)
  //
  // A pattern is constructed of size bits, where the least significant S+1 bits
  // are set. The pattern is rotated right by R, and repeated across a 32 or
  // 64-bit value, depending on destination register width.
  //
  // Put another way: the basic format of a logical immediate is a single
  // contiguous stretch of 1 bits, repeated across the whole word at intervals
  // given by a power of 2. To identify them quickly, we first locate the
  // lowest stretch of 1 bits, then the next 1 bit above that; that combination
  // is different for every logical immediate, so it gives us all the
  // information we need to identify the only logical immediate that our input
  // could be, and then we simply check if that's the value we actually have.
  //
  // (The rotation parameter does give the possibility of the stretch of 1 bits
  // going 'round the end' of the word. To deal with that, we observe that in
  // any situation where that happens the bitwise NOT of the value is also a
  // valid logical immediate. So we simply invert the input whenever its low bit
  // is set, and then we know that the rotated case can't arise.)

  if (value & 1) {
    // If the low bit is 1, negate the value, and set a flag to remember that we
    // did (so that we can adjust the return values appropriately).
    negate = true;
    value = ~value;
  }

  if (width == kWRegSizeInBits) {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.

    // The most-significant 32 bits may not be zero (ie. negate is true) so
    // shift the value left before duplicating it.
    value <<= kWRegSizeInBits;
    value |= value >> kWRegSizeInBits;
  }

  // The basic analysis idea: imagine our input word looks like this.
  //
  //    0011111000111110001111100011111000111110001111100011111000111110
  //                                                          c  b    a
  //                                                          |<--d-->|
  //
  // We find the lowest set bit (as an actual power-of-2 value, not its index)
  // and call it a. Then we add a to our original number, which wipes out the
  // bottommost stretch of set bits and replaces it with a 1 carried into the
  // next zero bit. Then we look for the new lowest set bit, which is in
  // position b, and subtract it, so now our number is just like the original
  // but with the lowest stretch of set bits completely gone. Now we find the
  // lowest set bit again, which is position c in the diagram above. Then we'll
  // measure the distance d between bit positions a and c (using CLZ), and that
  // tells us that the only valid logical immediate that could possibly be equal
  // to this number is the one in which a stretch of bits running from a to just
  // below b is replicated every d bits.
  uint64_t a = LargestPowerOf2Divisor(value);
  uint64_t value_plus_a = value + a;
  uint64_t b = LargestPowerOf2Divisor(value_plus_a);
  uint64_t value_plus_a_minus_b = value_plus_a - b;
  uint64_t c = LargestPowerOf2Divisor(value_plus_a_minus_b);

  int d, clz_a, out_n;
  uint64_t mask;

  if (c != 0) {
    // The general case, in which there is more than one stretch of set bits.
    // Compute the repeat distance d, and set up a bitmask covering the basic
    // unit of repetition (i.e. a word with the bottom d bits set). Also, in all
    // of these cases the N bit of the output will be zero.
    clz_a = CountLeadingZeros(a, kXRegSizeInBits);
    int clz_c = CountLeadingZeros(c, kXRegSizeInBits);
    d = clz_a - clz_c;
    mask = ((uint64_t{1} << d) - 1);
    out_n = 0;
  } else {
    // Handle degenerate cases.
    //
    // If any of those 'find lowest set bit' operations didn't find a set bit at
    // all, then the word will have been zero thereafter, so in particular the
    // last lowest_set_bit operation will have returned zero. So we can test for
    // all the special case conditions in one go by seeing if c is zero.
    if (a == 0) {
      // The input was zero (or all 1 bits, which will come to here too after we
      // inverted it at the start of the function), for which we just return
      // false.
      return false;
    } else {
      // Otherwise, if c was zero but a was not, then there's just one stretch
      // of set bits in our word, meaning that we have the trivial case of
      // d == 64 and only one 'repetition'. Set up all the same variables as in
      // the general case above, and set the N bit in the output.
      clz_a = CountLeadingZeros(a, kXRegSizeInBits);
      d = 64;
      mask = ~uint64_t{0};
      out_n = 1;
    }
  }

  // If the repeat period d is not a power of two, it can't be encoded.
  if (!base::bits::IsPowerOfTwo(d)) {
    return false;
  }

  if (((b - a) & ~mask) != 0) {
    // If the bit stretch (b - a) does not fit within the mask derived from the
    // repeat period, then fail.
    return false;
  }

  // The only possible option is b - a repeated every d bits. Now we're going to
  // actually construct the valid logical immediate derived from that
  // specification, and see if it equals our original input.
  //
  // To repeat a value every d bits, we multiply it by a number of the form
  // (1 + 2^d + 2^(2d) + ...), i.e. 0x0001000100010001 or similar. These can
  // be derived using a table lookup on CLZ(d).
  static const uint64_t multipliers[] = {
      0x0000000000000001UL, 0x0000000100000001UL, 0x0001000100010001UL,
      0x0101010101010101UL, 0x1111111111111111UL, 0x5555555555555555UL,
  };
  int multiplier_idx = CountLeadingZeros(d, kXRegSizeInBits) - 57;
  // Ensure that the index to the multipliers array is within bounds.
  DCHECK((multiplier_idx >= 0) &&
         (static_cast<size_t>(multiplier_idx) < arraysize(multipliers)));
  uint64_t multiplier = multipliers[multiplier_idx];
  uint64_t candidate = (b - a) * multiplier;

  if (value != candidate) {
    // The candidate pattern doesn't match our input value, so fail.
    return false;
  }

  // We have a match! This is a valid logical immediate, so now we have to
  // construct the bits and pieces of the instruction encoding that generates
  // it.

  // Count the set bits in our basic stretch. The special case of clz(0) == -1
  // makes the answer come out right for stretches that reach the very top of
  // the word (e.g. numbers like 0xFFFFC00000000000).
  int clz_b = (b == 0) ? -1 : CountLeadingZeros(b, kXRegSizeInBits);
  int s = clz_a - clz_b;

  // Decide how many bits to rotate right by, to put the low bit of that basic
  // stretch in position a.
  int r;
  if (negate) {
    // If we inverted the input right at the start of this function, here's
    // where we compensate: the number of set bits becomes the number of clear
    // bits, and the rotation count is based on position b rather than position
    // a (since b is the location of the 'lowest' 1 bit after inversion).
    s = d - s;
    r = (clz_b + 1) & (d - 1);
  } else {
    r = (clz_a + 1) & (d - 1);
  }

  // Now we're done, except for having to encode the S output in such a way that
  // it gives both the number of set bits and the length of the repeated
  // segment. The s field is encoded like this:
  //
  //     imms    size        S
  //    ssssss    64    UInt(ssssss)
  //    0sssss    32    UInt(sssss)
  //    10ssss    16    UInt(ssss)
  //    110sss     8    UInt(sss)
  //    1110ss     4    UInt(ss)
  //    11110s     2    UInt(s)
  //
  // So we 'or' (-d * 2) with our computed s to form imms.
  *n = out_n;
  *imm_s = ((-d * 2) | (s - 1)) & 0x3F;
  *imm_r = r;

  return true;
}

bool Assembler::IsImmFP32(uint32_t bits) {
  // Valid values will have the form:
  // aBbb.bbbc.defg.h000.0000.0000.0000.0000
  // bits[19..0] are cleared.
  if ((bits & 0x7FFFF) != 0) {
    return false;
  }

  // bits[29..25] are all set or all cleared.
  uint32_t b_pattern = (bits >> 16) & 0x3E00;
  if (b_pattern != 0 && b_pattern != 0x3E00) {
    return false;
  }

  // bit[30] and bit[29] are opposite.
  if (((bits ^ (bits << 1)) & 0x40000000) == 0) {
    return false;
  }

  return true;
}

bool Assembler::IsImmFP64(uint64_t bits) {
  // Valid values will have the form:
  // aBbb.bbbb.bbcd.efgh.0000.0000.0000.0000
  // 0000.0000.0000.0000.0000.0000.0000.0000
  // bits[47..0] are cleared.
  if ((bits & 0xFFFFFFFFFFFFL) != 0) {
    return false;
  }

  // bits[61..54] are all set or all cleared.
  uint32_t b_pattern = (bits >> 48) & 0x3FC0;
  if (b_pattern != 0 && b_pattern != 0x3FC0) {
    return false;
  }

  // bit[62] and bit[61] are opposite.
  if (((bits ^ (bits << 1)) & 0x4000000000000000L) == 0) {
    return false;
  }

  return true;
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
  memmove(new_start, buffer_start_, pc_offset());
  memmove(reloc_info_writer.pos() + rc_delta, reloc_info_writer.pos(),
          reloc_size);

  // Switch buffers.
  buffer_ = std::move(new_buffer);
  buffer_start_ = new_start;
  pc_ += pc_delta;
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // None of our relocation types are pc relative pointing outside the code
  // buffer nor pc absolute pointing inside the code buffer, so there is no need
  // to relocate any emitted relocation entries.

  // Relocate internal references.
  for (auto pos : internal_reference_positions_) {
    Address address = reinterpret_cast<intptr_t>(buffer_start_) + pos;
    intptr_t internal_ref = ReadUnalignedValue<intptr_t>(address);
    internal_ref += pc_delta;
    WriteUnalignedValue<intptr_t>(address, internal_ref);
  }

  // Pending relocation entries are also relative, no need to relocate.
}

void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data,
                                ConstantPoolMode constant_pool_mode) {
  if (rmode == RelocInfo::INTERNAL_REFERENCE ||
      rmode == RelocInfo::CONST_POOL || rmode == RelocInfo::VENEER_POOL ||
      rmode == RelocInfo::DEOPT_SCRIPT_OFFSET ||
      rmode == RelocInfo::DEOPT_INLINING_ID ||
      rmode == RelocInfo::DEOPT_REASON || rmode == RelocInfo::DEOPT_ID ||
      rmode == RelocInfo::DEOPT_NODE_ID) {
    // Adjust code for new modes.
    DCHECK(RelocInfo::IsDeoptReason(rmode) || RelocInfo::IsDeoptId(rmode) ||
           RelocInfo::IsDeoptNodeId(rmode) ||
           RelocInfo::IsDeoptPosition(rmode) ||
           RelocInfo::IsInternalReference(rmode) ||
           RelocInfo::IsConstPool(rmode) || RelocInfo::IsVeneerPool(rmode));
    // These modes do not need an entry in the constant pool.
  } else if (constant_pool_mode == NEEDS_POOL_ENTRY) {
    if (RelocInfo::IsEmbeddedObjectMode(rmode)) {
      Handle<HeapObject> handle(reinterpret_cast<Address*>(data));
      data = AddEmbeddedObject(handle);
    }
    if (rmode == RelocInfo::COMPRESSED_EMBEDDED_OBJECT) {
      if (constpool_.RecordEntry(static_cast<uint32_t>(data), rmode) ==
          RelocInfoStatus::kMustOmitForDuplicate) {
        return;
      }
    } else {
      if (constpool_.RecordEntry(static_cast<uint64_t>(data), rmode) ==
          RelocInfoStatus::kMustOmitForDuplicate) {
        return;
      }
    }
  }
  // For modes that cannot use the constant pool, a different sequence of
  // instructions will be emitted by this function's caller.

  if (!ShouldRecordRelocInfo(rmode)) return;

  // Callers should ensure that constant pool emission is blocked until the
  // instruction the reloc info is associated with has been emitted.
  DCHECK(constpool_.IsBlocked());

  // We do not try to reuse pool constants.
  RelocInfo rinfo(reinterpret_cast<Address>(pc_), rmode, data);
  DCHECK_GE(buffer_space(), kMaxRelocSize);  // too late to grow buffer here
  reloc_info_writer.Write(&rinfo);
}

void Assembler::near_jump(int offset, RelocInfo::Mode rmode) {
  BlockPoolsScope no_pool_before_b_instr(this);
  if (!RelocInfo::IsNoInfo(rmode))
    RecordRelocInfo(rmode, offset, NO_POOL_ENTRY);
  b(offset);
}

void Assembler::near_call(int offset, RelocInfo::Mode rmode) {
  BlockPoolsScope no_pool_before_bl_instr(this);
  if (!RelocInfo::IsNoInfo(rmode))
    RecordRelocInfo(rmode, offset, NO_POOL_ENTRY);
  bl(offset);
}

void Assembler::near_call(HeapNumberRequest request) {
  BlockPoolsScope no_pool_before_bl_instr(this);
  RequestHeapNumber(request);
  EmbeddedObjectIndex index = AddEmbeddedObject(Handle<Code>());
  RecordRelocInfo(RelocInfo::CODE_TARGET, index, NO_POOL_ENTRY);
  DCHECK(is_int32(index));
  bl(static_cast<int>(index));
}

// Constant Pool

void ConstantPool::EmitPrologue(Alignment require_alignment) {
  // Recorded constant pool size is expressed in number of 32-bits words,
  // and includes prologue and alignment, but not the jump around the pool
  // and the size of the marker itself.
  const int marker_size = 1;
  int word_count =
      ComputeSize(Jump::kOmitted, require_alignment) / kInt32Size - marker_size;
  assm_->Emit(LDR_x_lit | Assembler::ImmLLiteral(word_count) |
              Assembler::Rt(xzr));
  assm_->EmitPoolGuard();
}

int ConstantPool::PrologueSize(Jump require_jump) const {
  // Prologue is:
  //   b   over  ;; if require_jump
  //   ldr xzr, #pool_size
  //   blr xzr
  int prologue_size = require_jump == Jump::kRequired ? kInstrSize : 0;
  prologue_size += 2 * kInstrSize;
  return prologue_size;
}

void ConstantPool::SetLoadOffsetToConstPoolEntry(int load_offset,
                                                 Instruction* entry_offset,
                                                 const ConstantPoolKey& key) {
  Instruction* instr = assm_->InstructionAt(load_offset);
  // Instruction to patch must be 'ldr rd, [pc, #offset]' with offset == 0.
  DCHECK(instr->IsLdrLiteral() && instr->ImmLLiteral() == 0);
  instr->SetImmPCOffsetTarget(assm_->zone(), assm_->options(), entry_offset);
}

void ConstantPool::Check(Emission force_emit, Jump require_jump,
                         size_t margin) {
  // Some short sequence of instruction must not be broken up by constant pool
  // emission, such sequences are protected by a ConstPool::BlockScope.
  if (IsBlocked()) {
    // Something is wrong if emission is forced and blocked at the same time.
    DCHECK_EQ(force_emit, Emission::kIfNeeded);
    return;
  }

  // We emit a constant pool only if :
  //  * it is not empty
  //  * emission is forced by parameter force_emit (e.g. at function end).
  //  * emission is mandatory or opportune according to {ShouldEmitNow}.
  if (!IsEmpty() && (force_emit == Emission::kForced ||
                     ShouldEmitNow(require_jump, margin))) {
    // Emit veneers for branches that would go out of range during emission of
    // the constant pool.
    int worst_case_size = ComputeSize(Jump::kRequired, Alignment::kRequired);
    assm_->CheckVeneerPool(false, require_jump == Jump::kRequired,
                           assm_->kVeneerDistanceMargin + worst_case_size +
                               static_cast<int>(margin));

    // Check that the code buffer is large enough before emitting the constant
    // pool (this includes the gap to the relocation information).
    int needed_space = worst_case_size + assm_->kGap;
    while (assm_->buffer_space() <= needed_space) {
      assm_->GrowBuffer();
    }

    EmitAndClear(require_jump);
  }
  // Since a constant pool is (now) empty, move the check offset forward by
  // the standard interval.
  SetNextCheckIn(ConstantPool::kCheckInterval);
}

// Pool entries are accessed with pc relative load therefore this cannot be more
// than 1 * MB. Since constant pool emission checks are interval based, and we
// want to keep entries close to the code, we try to emit every 64KB.
const size_t ConstantPool::kMaxDistToPool32 = 1 * MB;
const size_t ConstantPool::kMaxDistToPool64 = 1 * MB;
const size_t ConstantPool::kCheckInterval = 128 * kInstrSize;
const size_t ConstantPool::kApproxDistToPool32 = 64 * KB;
const size_t ConstantPool::kApproxDistToPool64 = kApproxDistToPool32;

const size_t ConstantPool::kOpportunityDistToPool32 = 64 * KB;
const size_t ConstantPool::kOpportunityDistToPool64 = 64 * KB;
const size_t ConstantPool::kApproxMaxEntryCount = 512;

intptr_t Assembler::MaxPCOffsetAfterVeneerPoolIfEmittedNow(size_t margin) {
  // Account for the branch and guard around the veneers.
  static constexpr int kBranchSizeInBytes = kInstrSize;
  static constexpr int kGuardSizeInBytes = kInstrSize;
  const size_t max_veneer_size_in_bytes =
      unresolved_branches_.size() * kVeneerCodeSize;
  return static_cast<intptr_t>(pc_offset() + kBranchSizeInBytes +
                               kGuardSizeInBytes + max_veneer_size_in_bytes +
                               margin);
}

void Assembler::RecordVeneerPool(int location_offset, int size) {
  Assembler::BlockPoolsScope block_pools(this, PoolEmissionCheck::kSkip);
  RelocInfo rinfo(reinterpret_cast<Address>(buffer_start_) + location_offset,
                  RelocInfo::VENEER_POOL, static_cast<intptr_t>(size));
  reloc_info_writer.Write(&rinfo);
}

void Assembler::EmitVeneers(bool force_emit, bool need_protection,
                            size_t margin) {
  ASM_CODE_COMMENT(this);
  BlockPoolsScope scope(this, PoolEmissionCheck::kSkip);

  // The exact size of the veneer pool must be recorded (see the comment at the
  // declaration site of RecordConstPool()), but computing the number of
  // veneers that will be generated is not obvious. So instead we remember the
  // current position and will record the size after the pool has been
  // generated.
  Label size_check;
  bind(&size_check);
  int veneer_pool_relocinfo_loc = pc_offset();

  Label end;
  if (need_protection) {
    b(&end);
  }

  EmitVeneersGuard();

  // We only emit veneers if needed (unless emission is forced), i.e. when the
  // max-reachable-pc of the branch has been exhausted by the current codegen
  // state. Specifically, we emit when the max-reachable-pc of the branch <= the
  // max-pc-after-veneers (over-approximated).
  const intptr_t max_pc_after_veneers =
      MaxPCOffsetAfterVeneerPoolIfEmittedNow(margin);

  {
    // The `unresolved_branches_` map is sorted by max-reachable-pc in ascending
    // order.
    auto it = unresolved_branches_.begin();
    while (it != unresolved_branches_.end()) {
      const int max_reachable_pc = it->first & ~1;
      if (!force_emit && max_reachable_pc > max_pc_after_veneers) break;

      // Found a task. We'll emit a veneer for this.

      // Calculate the branch location from the maximum reachable PC. Only
      // B.cond, CB[N]Z and TB[N]Z are veneered, and the first two branch types
      // have the same range. The LSB (branch type tag bit) is set for TB[N]Z,
      // clear otherwise.
      int pc_offset = it->first;
      if (pc_offset & 1) {
        pc_offset -= (Instruction::ImmBranchRange(TestBranchType) + 1);
      } else {
        static_assert(Instruction::ImmBranchRange(CondBranchType) ==
                      Instruction::ImmBranchRange(CompareBranchType));
        pc_offset -= Instruction::ImmBranchRange(CondBranchType);
      }
#ifdef DEBUG
      Label veneer_size_check;
      bind(&veneer_size_check);
#endif
      Label* label = it->second;
      Instruction* veneer = reinterpret_cast<Instruction*>(pc_);
      Instruction* branch = InstructionAt(pc_offset);
      RemoveBranchFromLabelLinkChain(branch, label, veneer);
      branch->SetImmPCOffsetTarget(zone(), options(), veneer);
      b(label);  // This may end up pointing at yet another veneer later on.
      DCHECK_EQ(SizeOfCodeGeneratedSince(&veneer_size_check),
                static_cast<uint64_t>(kVeneerCodeSize));
      it = unresolved_branches_.erase(it);
    }
  }

  // Update next_veneer_pool_check_ (tightly coupled with unresolved_branches_).
  // This must happen after the calls to {RemoveBranchFromLabelLinkChain},
  // because that function can resolve additional branches.
  if (unresolved_branches_.empty()) {
    next_veneer_pool_check_ = kMaxInt;
  } else {
    next_veneer_pool_check_ =
        unresolved_branches_first_limit() - kVeneerDistanceCheckMargin;
  }

  // Record the veneer pool size.
  int pool_size = static_cast<int>(SizeOfCodeGeneratedSince(&size_check));
  RecordVeneerPool(veneer_pool_relocinfo_loc, pool_size);

  bind(&end);
}

void Assembler::CheckVeneerPool(bool force_emit, bool require_jump,
                                size_t margin) {
  // There is nothing to do if there are no pending veneer pool entries.
  if (unresolved_branches_.empty()) {
    DCHECK_EQ(next_veneer_pool_check_, kMaxInt);
    return;
  }

  DCHECK(pc_offset() < unresolved_branches_first_limit());

  // Some short sequence of instruction mustn't be broken up by veneer pool
  // emission, such sequences are protected by calls to BlockVeneerPoolFor and
  // BlockVeneerPoolScope.
  if (is_veneer_pool_blocked()) {
    DCHECK(!force_emit);
    return;
  }

  if (!require_jump) {
    // Prefer emitting veneers protected by an existing instruction.
    margin *= kVeneerNoProtectionFactor;
  }
  if (force_emit || ShouldEmitVeneers(margin)) {
    EmitVeneers(force_emit, require_jump, margin);
  } else {
    next_veneer_pool_check_ =
        unresolved_branches_first_limit() - kVeneerDistanceCheckMargin;
  }
}

int Assembler::buffer_space() const {
  return static_cast<int>(reloc_info_writer.pos() - pc_);
}

void Assembler::RecordConstPool(int size) {
  // We only need this for debugger support, to correctly compute offsets in the
  // code.
  Assembler::BlockPoolsScope block_pools(this);
  RecordRelocInfo(RelocInfo::CONST_POOL, static_cast<intptr_t>(size));
}

void PatchingAssembler::PatchAdrFar(int64_t target_offset) {
  // The code at the current instruction should be:
  //   adr  rd, 0
  //   nop  (adr_far)
  //   nop  (adr_far)
  //   movz scratch, 0

  // Verify the expected code.
  Instruction* expected_adr = InstructionAt(0);
  CHECK(expected_adr->IsAdr() && (expected_adr->ImmPCRel() == 0));
  int rd_code = expected_adr->Rd();
  for (int i = 0; i < kAdrFarPatchableNNops; ++i) {
    CHECK(InstructionAt((i + 1) * kInstrSize)->IsNop(ADR_FAR_NOP));
  }
  Instruction* expected_movz =
      InstructionAt((kAdrFarPatchableNInstrs - 1) * kInstrSize);
  CHECK(expected_movz->IsMovz() && (expected_movz->ImmMoveWide() == 0) &&
        (expected_movz->ShiftMoveWide() == 0));
  int scratch_code = expected_movz->Rd();

  // Patch to load the correct address.
  Register rd = Register::XRegFromCode(rd_code);
  Register scratch = Register::XRegFromCode(scratch_code);
  // Addresses are only 48 bits.
  adr(rd, target_offset & 0xFFFF);
  movz(scratch, (target_offset >> 16) & 0xFFFF, 16);
  movk(scratch, (target_offset >> 32) & 0xFFFF, 32);
  DCHECK_EQ(target_offset >> 48, 0);
  add(rd, rd, scratch);
}

void PatchingAssembler::PatchSubSp(uint32_t immediate) {
  // The code at the current instruction should be:
  //   sub sp, sp, #0

  // Verify the expected code.
  Instruction* expected_adr = InstructionAt(0);
  CHECK(expected_adr->IsAddSubImmediate());
  sub(sp, sp, immediate);
}

#undef NEON_3DIFF_LONG_LIST
#undef NEON_3DIFF_HN_LIST
#undef NEON_ACROSSLANES_LIST
#undef NEON_FP2REGMISC_FCVT_LIST
#undef NEON_FP2REGMISC_LIST
#undef NEON_3SAME_LIST
#undef NEON_FP3SAME_LIST_V2
#undef NEON_BYELEMENT_LIST
#undef NEON_FPBYELEMENT_LIST
#undef NEON_BYELEMENT_LONG_LIST

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64
