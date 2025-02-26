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
// Copyright 2012 the V8 project authors. All rights reserved.

#include "src/codegen/assembler.h"

#ifdef V8_CODE_COMMENTS
#include <iomanip>
#endif
#include "src/base/vector.h"
#include "src/codegen/assembler-inl.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"  // For MemoryAllocator. TODO(jkummerow): Drop.
#include "src/snapshot/embedded/embedded-data.h"
#include "src/snapshot/snapshot.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

AssemblerOptions AssemblerOptions::Default(Isolate* isolate) {
  AssemblerOptions options;
  const bool serializer = isolate->serializer_enabled();
  const bool generating_embedded_builtin =
      isolate->IsGeneratingEmbeddedBuiltins();
  options.record_reloc_info_for_serialization = serializer;
  options.enable_root_relative_access =
      !serializer && !generating_embedded_builtin;
#ifdef USE_SIMULATOR
  // Even though the simulator is enabled, we may still need to generate code
  // that may need to run on both the simulator and real hardware. For example,
  // if we are cross-compiling and embedding a script into the snapshot, the
  // script will need to run on the host causing the embedded builtins to run in
  // the simulator. While the final cross-compiled V8 will not have a simulator.

  // So here we enable simulator specific code if not generating the snapshot or
  // if we are but we are targetting the simulator *only*.
  options.enable_simulator_code = !serializer || v8_flags.target_is_simulator;
#endif

#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_LOONG64 || \
    V8_TARGET_ARCH_RISCV64
  options.code_range_base = isolate->heap()->code_range_base();
#endif
  bool short_builtin_calls =
      isolate->is_short_builtin_calls_enabled() &&
      !generating_embedded_builtin &&
      (options.code_range_base != kNullAddress) &&
      // Serialization of NEAR_BUILTIN_ENTRY reloc infos is not supported yet.
      !serializer;
  if (short_builtin_calls) {
    options.builtin_call_jump_mode = BuiltinCallJumpMode::kPCRelative;
  }
  return options;
}

namespace {

class DefaultAssemblerBuffer : public AssemblerBuffer {
 public:
  explicit DefaultAssemblerBuffer(int size)
      : buffer_(base::OwnedVector<uint8_t>::NewForOverwrite(
            std::max(AssemblerBase::kMinimalBufferSize, size))) {
#ifdef DEBUG
    ZapCode(reinterpret_cast<Address>(buffer_.begin()), buffer_.size());
#endif
  }

  uint8_t* start() const override { return buffer_.begin(); }

  int size() const override { return static_cast<int>(buffer_.size()); }

  std::unique_ptr<AssemblerBuffer> Grow(int new_size) override {
    DCHECK_LT(size(), new_size);
    return std::make_unique<DefaultAssemblerBuffer>(new_size);
  }

 private:
  base::OwnedVector<uint8_t> buffer_;
};

class ExternalAssemblerBufferImpl : public AssemblerBuffer {
 public:
  ExternalAssemblerBufferImpl(uint8_t* start, int size)
      : start_(start), size_(size) {}

  uint8_t* start() const override { return start_; }

  int size() const override { return size_; }

  std::unique_ptr<AssemblerBuffer> Grow(int new_size) override {
    FATAL("Cannot grow external assembler buffer");
  }

  void* operator new(std::size_t count);
  void operator delete(void* ptr) noexcept;

 private:
  uint8_t* const start_;
  const int size_;
};

alignas(
    ExternalAssemblerBufferImpl) static thread_local char tls_singleton_storage
    [sizeof(ExternalAssemblerBufferImpl)];

static thread_local bool tls_singleton_taken{false};

void* ExternalAssemblerBufferImpl::operator new(std::size_t count) {
  DCHECK_EQ(count, sizeof(ExternalAssemblerBufferImpl));
  if (V8_LIKELY(!tls_singleton_taken)) {
    tls_singleton_taken = true;
    return tls_singleton_storage;
  }
  return ::operator new(count);
}

void ExternalAssemblerBufferImpl::operator delete(void* ptr) noexcept {
  if (V8_LIKELY(ptr == tls_singleton_storage)) {
    DCHECK(tls_singleton_taken);
    tls_singleton_taken = false;
    return;
  }
  ::operator delete(ptr);
}

}  // namespace

std::unique_ptr<AssemblerBuffer> ExternalAssemblerBuffer(void* start,
                                                         int size) {
  return std::make_unique<ExternalAssemblerBufferImpl>(
      reinterpret_cast<uint8_t*>(start), size);
}

std::unique_ptr<AssemblerBuffer> NewAssemblerBuffer(int size) {
  return std::make_unique<DefaultAssemblerBuffer>(size);
}

// -----------------------------------------------------------------------------
// Implementation of AssemblerBase

// static
constexpr int AssemblerBase::kMinimalBufferSize;

// static
constexpr int AssemblerBase::kDefaultBufferSize;

AssemblerBase::AssemblerBase(const AssemblerOptions& options,
                             std::unique_ptr<AssemblerBuffer> buffer)
    : buffer_(std::move(buffer)),
      options_(options),
      enabled_cpu_features_(0),
      predictable_code_size_(false),
      constant_pool_available_(false),
      jump_optimization_info_(nullptr) {
  if (!buffer_) buffer_ = NewAssemblerBuffer(kDefaultBufferSize);
  buffer_start_ = buffer_->start();
  pc_ = buffer_start_;
}

AssemblerBase::~AssemblerBase() = default;

void AssemblerBase::Print(Isolate* isolate) {
  StdoutStream os;
  v8::internal::Disassembler::Decode(isolate, os, buffer_start_, pc_);
}

// -----------------------------------------------------------------------------
// Implementation of CpuFeatureScope

#ifdef DEBUG
CpuFeatureScope::CpuFeatureScope(AssemblerBase* assembler, CpuFeature f,
                                 CheckPolicy check)
    : assembler_(assembler) {
  DCHECK_IMPLIES(check == kCheckSupported, CpuFeatures::IsSupported(f));
  old_enabled_ = assembler_->enabled_cpu_features();
  assembler_->EnableCpuFeature(f);
}

CpuFeatureScope::~CpuFeatureScope() {
  assembler_->set_enabled_cpu_features(old_enabled_);
}
#endif

bool CpuFeatures::initialized_ = false;
bool CpuFeatures::supports_wasm_simd_128_ = false;
bool CpuFeatures::supports_cetss_ = false;
unsigned CpuFeatures::supported_ = 0;
unsigned CpuFeatures::icache_line_size_ = 0;
unsigned CpuFeatures::dcache_line_size_ = 0;

HeapNumberRequest::HeapNumberRequest(double heap_number, int offset)
    : offset_(offset) {
  value_ = heap_number;
  DCHECK(!IsSmiDouble(value_));
}

// Platform specific but identical code for all the platforms.

void Assembler::RecordDeoptReason(DeoptimizeReason reason, uint32_t node_id,
                                  SourcePosition position, int id) {
  static_assert(RelocInfoWriter::kMaxSize * 2 <= kGap);
  {
    EnsureSpace space(this);
    DCHECK(position.IsKnown());
    RecordRelocInfo(RelocInfo::DEOPT_SCRIPT_OFFSET, position.ScriptOffset());
    RecordRelocInfo(RelocInfo::DEOPT_INLINING_ID, position.InliningId());
  }
  {
    EnsureSpace space(this);
    RecordRelocInfo(RelocInfo::DEOPT_REASON, static_cast<int>(reason));
    RecordRelocInfo(RelocInfo::DEOPT_ID, id);
  }
#ifdef DEBUG
  EnsureSpace space(this);
  RecordRelocInfo(RelocInfo::DEOPT_NODE_ID, node_id);
#endif  // DEBUG
}

void Assembler::DataAlign(int m) {
  DCHECK(m >= 2 && base::bits::IsPowerOfTwo(m));
  while ((pc_offset() & (m - 1)) != 0) {
    // Pad with 0xcc (= int3 on ia32 and x64); the primary motivation is that
    // the disassembler expects to find valid instructions, but this is also
    // nice from a security point of view.
    db(0xcc);
  }
}

void AssemblerBase::RequestHeapNumber(HeapNumberRequest request) {
  request.set_offset(pc_offset());
  heap_number_requests_.push_front(request);
}

int AssemblerBase::AddCodeTarget(IndirectHandle<Code> target) {
  int current = static_cast<int>(code_targets_.size());
  if (current > 0 && !target.is_null() &&
      code_targets_.back().address() == target.address()) {
    // Optimization if we keep jumping to the same code target.
    return current - 1;
  } else {
    code_targets_.push_back(target);
    return current;
  }
}

IndirectHandle<Code> AssemblerBase::GetCodeTarget(
    intptr_t code_target_index) const {
  DCHECK_LT(static_cast<size_t>(code_target_index), code_targets_.size());
  return code_targets_[code_target_index];
}

AssemblerBase::EmbeddedObjectIndex AssemblerBase::AddEmbeddedObject(
    IndirectHandle<HeapObject> object) {
  EmbeddedObjectIndex current = embedded_objects_.size();
  // Do not deduplicate invalid handles, they are to heap object requests.
  if (!object.is_null()) {
    auto entry = embedded_objects_map_.find(object);
    if (entry != embedded_objects_map_.end()) {
      return entry->second;
    }
    embedded_objects_map_[object] = current;
  }
  embedded_objects_.push_back(object);
  return current;
}

IndirectHandle<HeapObject> AssemblerBase::GetEmbeddedObject(
    EmbeddedObjectIndex index) const {
  DCHECK_LT(index, embedded_objects_.size());
  return embedded_objects_[index];
}

int Assembler::WriteCodeComments() {
  if (!v8_flags.code_comments) return 0;
  CHECK_IMPLIES(code_comments_writer_.entry_count() > 0,
                options().emit_code_comments);
  if (code_comments_writer_.entry_count() == 0) return 0;
  int offset = pc_offset();
  code_comments_writer_.Emit(this);
  int size = pc_offset() - offset;
  DCHECK_EQ(size, code_comments_writer_.section_size());
  return size;
}

#ifdef V8_CODE_COMMENTS
int Assembler::CodeComment::depth() const { return assembler_->comment_depth_; }
void Assembler::CodeComment::Open(const std::string& comment,
                                  const SourceLocation& loc) {
  std::stringstream sstream;
  sstream << std::setfill(' ') << std::setw(depth() * kIndentWidth + 2);
  sstream << "[ " << comment;
  assembler_->comment_depth_++;
  assembler_->RecordComment(sstream.str(), loc);
}

void Assembler::CodeComment::Close() {
  assembler_->comment_depth_--;
  std::string comment = "]";
  comment.insert(0, depth() * kIndentWidth, ' ');
  DCHECK_LE(0, depth());
  // Don't record source information for the closed comment.
  assembler_->RecordComment(comment, SourceLocation());
}
#endif

}  // namespace internal
}  // namespace v8
