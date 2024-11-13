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

#ifndef V8_CODEGEN_ASSEMBLER_H_
#define V8_CODEGEN_ASSEMBLER_H_

#include <algorithm>
#include <forward_list>
#include <map>
#include <memory>
#include <ostream>
#include <type_traits>
#include <unordered_map>

#include "src/base/macros.h"
#include "src/base/memory.h"
#include "src/codegen/code-comments.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/label.h"
#include "src/codegen/reglist.h"
#include "src/codegen/reloc-info.h"
#include "src/common/globals.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/flags/flags.h"
#include "src/handles/handles.h"
#include "src/objects/objects.h"
#include "src/sandbox/indirect-pointer-tag.h"
#include "src/utils/ostreams.h"

namespace v8 {

// Forward declarations.
class ApiFunction;

namespace internal {

using base::Memory;
using base::ReadUnalignedValue;
using base::WriteUnalignedValue;

// Forward declarations.
class EmbeddedData;
class OffHeapInstructionStream;
class Isolate;
class SCTableReference;
class SourcePosition;
class StatsCounter;
class Label;

// -----------------------------------------------------------------------------
// Optimization for far-jmp like instructions that can be replaced by shorter.

struct JumpOptimizationInfo {
 public:
  struct JumpInfo {
    int pos;
    int opcode_size;
    // target_address-address_after_jmp_instr, 0 when distance not bind.
    int distance;
  };

  bool is_collecting() const { return stage == kCollection; }
  bool is_optimizing() const { return stage == kOptimization; }
  void set_optimizing() {
    DCHECK(is_optimizable());
    stage = kOptimization;
  }

  bool is_optimizable() const { return optimizable; }
  void set_optimizable() {
    DCHECK(is_collecting());
    optimizable = true;
  }

  int MaxAlignInRange(int from, int to) {
    int max_align = 0;

    auto it = align_pos_size.upper_bound(from);

    while (it != align_pos_size.end()) {
      if (it->first <= to) {
        max_align = std::max(max_align, it->second);
        it++;
      } else {
        break;
      }
    }
    return max_align;
  }

  // Debug
  void Print() {
    std::cout << "align_pos_size:" << std::endl;
    for (auto p : align_pos_size) {
      std::cout << "{" << p.first << "," << p.second << "}"
                << " ";
    }
    std::cout << std::endl;

    std::cout << "may_optimizable_farjmp:" << std::endl;

    for (auto p : may_optimizable_farjmp) {
      const auto& jmp_info = p.second;
      printf("{postion:%d, opcode_size:%d, distance:%d, dest:%d}\n",
             jmp_info.pos, jmp_info.opcode_size, jmp_info.distance,
             jmp_info.pos + jmp_info.opcode_size + 4 + jmp_info.distance);
    }
    std::cout << std::endl;
  }

  // Used to verify the instruction sequence is always the same in two stages.
  enum { kCollection, kOptimization } stage = kCollection;

  size_t hash_code = 0u;

  // {position: align_size}
  std::map<int, int> align_pos_size;

  int farjmp_num = 0;
  // For collecting stage, should contains all far jump information after
  // collecting.
  std::vector<JumpInfo> farjmps;

  bool optimizable = false;
  // {index: JumpInfo}
  std::map<int, JumpInfo> may_optimizable_farjmp;

  // For label binding.
  std::map<Label*, std::vector<int>> label_farjmp_maps;
};

class HeapNumberRequest {
 public:
  explicit HeapNumberRequest(double heap_number, int offset = -1);

  double heap_number() const { return value_; }

  // The code buffer offset at the time of the request.
  int offset() const {
    DCHECK_GE(offset_, 0);
    return offset_;
  }
  void set_offset(int offset) {
    DCHECK_LT(offset_, 0);
    offset_ = offset;
    DCHECK_GE(offset_, 0);
  }

 private:
  double value_;
  int offset_;
};

// -----------------------------------------------------------------------------
// Platform independent assembler base class.

enum class CodeObjectRequired { kNo, kYes };

enum class BuiltinCallJumpMode {
  // The builtin entry point address is embedded into the instruction stream as
  // an absolute address.
  kAbsolute,
  // Generate builtin calls/jumps using PC-relative instructions. This mode
  // assumes that the target is guaranteed to be within the
  // kMaxPCRelativeCodeRangeInMB distance.
  kPCRelative,
  // Generate builtin calls/jumps as an indirect instruction which loads the
  // target address from the builtins entry point table.
  kIndirect,
  // Same as kPCRelative but used only for generating embedded builtins.
  // Currently we use RelocInfo::RUNTIME_ENTRY for generating kPCRelative but
  // it's not supported yet for mksnapshot yet because of various reasons:
  // 1) we encode the target as an offset from the code range which is not
  // always available (32-bit architectures don't have it),
  // 2) serialization of RelocInfo::RUNTIME_ENTRY is not implemented yet.
  // TODO(v8:11527): Address the reasons above and remove the kForMksnapshot in
  // favor of kPCRelative or kIndirect.
  kForMksnapshot,
};

struct V8_EXPORT_PRIVATE AssemblerOptions {
  // Recording reloc info for external references and off-heap targets is
  // needed whenever code is serialized, e.g. into the snapshot or as a Wasm
  // module. This flag allows this reloc info to be disabled for code that
  // will not survive process destruction.
  bool record_reloc_info_for_serialization = true;
  // Recording reloc info can be disabled wholesale. This is needed when the
  // assembler is used on existing code directly (e.g. JumpTableAssembler)
  // without any buffer to hold reloc information.
  bool disable_reloc_info_for_patching = false;
  // Enables root-relative access to arbitrary untagged addresses (usually
  // external references). Only valid if code will not survive the process.
  bool enable_root_relative_access = false;
  // Enables specific assembler sequences only used for the simulator.
  bool enable_simulator_code = USE_SIMULATOR_BOOL;
  // Enables use of isolate-independent constants, indirected through the
  // root array.
  // (macro assembler feature).
  bool isolate_independent_code = false;

  // Defines how builtin calls and tail calls should be generated.
  BuiltinCallJumpMode builtin_call_jump_mode = BuiltinCallJumpMode::kAbsolute;
  // Mksnapshot ensures that the code range is small enough to guarantee that
  // PC-relative call/jump instructions can be used for builtin to builtin
  // calls/tail calls. The embedded builtins blob generator also ensures that.
  // However, there are serializer tests, where we force isolate creation at
  // runtime and at this point, Code space isn't restricted to a size s.t.
  // PC-relative calls may be used. So, we fall back to an indirect mode.
  // TODO(v8:11527): remove once kForMksnapshot is removed.
  bool use_pc_relative_calls_and_jumps_for_mksnapshot = false;

  // On some platforms, all code is created within a certain address range in
  // the process, and the base of this code range is configured here.
  Address code_range_base = 0;
  // Enables the collection of information useful for the generation of unwind
  // info. This is useful in some platform (Win64) where the unwind info depends
  // on a function prologue/epilogue.
  bool collect_win64_unwind_info = false;
  // Whether to emit code comments.
  bool emit_code_comments = v8_flags.code_comments;

  bool is_wasm = false;

  static AssemblerOptions Default(Isolate* isolate);
};

class AssemblerBuffer {
 public:
  virtual ~AssemblerBuffer() = default;
  virtual uint8_t* start() const = 0;
  virtual int size() const = 0;
  // Return a grown copy of this buffer. The contained data is uninitialized.
  // The data in {this} will still be read afterwards (until {this} is
  // destructed), but not written.
  virtual std::unique_ptr<AssemblerBuffer> Grow(int new_size)
      V8_WARN_UNUSED_RESULT = 0;
};

// Describes a HeapObject slot containing a pointer to another HeapObject. Such
// a slot can either contain a direct/tagged pointer, or an indirect pointer
// (i.e. an index into a pointer table, which then contains the actual pointer
// to the object) together with a specific IndirectPointerTag.
class SlotDescriptor {
 public:
  bool contains_direct_pointer() const {
    return indirect_pointer_tag_ == kIndirectPointerNullTag;
  }

  bool contains_indirect_pointer() const {
    return indirect_pointer_tag_ != kIndirectPointerNullTag;
  }

  IndirectPointerTag indirect_pointer_tag() const {
    DCHECK(contains_indirect_pointer());
    return indirect_pointer_tag_;
  }

  static SlotDescriptor ForDirectPointerSlot() {
    return SlotDescriptor(kIndirectPointerNullTag);
  }

  static SlotDescriptor ForIndirectPointerSlot(IndirectPointerTag tag) {
    return SlotDescriptor(tag);
  }

  static SlotDescriptor ForTrustedPointerSlot(IndirectPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
    return ForIndirectPointerSlot(tag);
#else
    return ForDirectPointerSlot();
#endif
  }

  static SlotDescriptor ForCodePointerSlot() {
    return ForTrustedPointerSlot(kCodeIndirectPointerTag);
  }

 private:
  SlotDescriptor(IndirectPointerTag tag) : indirect_pointer_tag_(tag) {}

  // If the tag is null, this object describes a direct pointer slot.
  IndirectPointerTag indirect_pointer_tag_;
};

// Allocate an AssemblerBuffer which uses an existing buffer. This buffer cannot
// grow, so it must be large enough for all code emitted by the Assembler.
V8_EXPORT_PRIVATE
std::unique_ptr<AssemblerBuffer> ExternalAssemblerBuffer(void* buffer,
                                                         int size);

// Allocate a new growable AssemblerBuffer with a given initial size.
V8_EXPORT_PRIVATE
std::unique_ptr<AssemblerBuffer> NewAssemblerBuffer(int size);

class V8_EXPORT_PRIVATE AssemblerBase : public Malloced {
 public:
  AssemblerBase(const AssemblerOptions& options,
                std::unique_ptr<AssemblerBuffer>);
  virtual ~AssemblerBase();

  const AssemblerOptions& options() const { return options_; }

  bool predictable_code_size() const { return predictable_code_size_; }
  void set_predictable_code_size(bool value) { predictable_code_size_ = value; }

  uint64_t enabled_cpu_features() const { return enabled_cpu_features_; }
  void set_enabled_cpu_features(uint64_t features) {
    enabled_cpu_features_ = features;
  }
  // Features are usually enabled by CpuFeatureScope, which also asserts that
  // the features are supported before they are enabled.
  // IMPORTANT:  IsEnabled() should only be used by DCHECKs. For real feature
  // detection, use IsSupported().
  bool IsEnabled(CpuFeature f) {
    return (enabled_cpu_features_ & (static_cast<uint64_t>(1) << f)) != 0;
  }
  void EnableCpuFeature(CpuFeature f) {
    enabled_cpu_features_ |= (static_cast<uint64_t>(1) << f);
  }

  bool is_constant_pool_available() const {
    if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
      // We need to disable constant pool here for embeded builtins
      // because the metadata section is not adjacent to instructions
      return constant_pool_available_ && !options().isolate_independent_code;
    } else {
      // Embedded constant pool not supported on this architecture.
      UNREACHABLE();
    }
  }

  JumpOptimizationInfo* jump_optimization_info() {
    return jump_optimization_info_;
  }
  void set_jump_optimization_info(JumpOptimizationInfo* jump_opt) {
    jump_optimization_info_ = jump_opt;
  }

  void FinalizeJumpOptimizationInfo() {}

  // Overwrite a host NaN with a quiet target NaN.  Used by mksnapshot for
  // cross-snapshotting.
  static void QuietNaN(Tagged<HeapObject> nan) {}

  int pc_offset() const { return static_cast<int>(pc_ - buffer_start_); }

  int pc_offset_for_safepoint() {
#if defined(V8_TARGET_ARCH_MIPS64) || defined(V8_TARGET_ARCH_LOONG64)
    // MIPS and LOONG need to use their own implementation to avoid trampoline's
    // influence.
    UNREACHABLE();
#else
    return pc_offset();
#endif
  }

  uint8_t* buffer_start() const { return buffer_->start(); }
  int buffer_size() const { return buffer_->size(); }
  int instruction_size() const { return pc_offset(); }

  std::unique_ptr<AssemblerBuffer> ReleaseBuffer() {
    std::unique_ptr<AssemblerBuffer> buffer = std::move(buffer_);
    DCHECK_NULL(buffer_);
    // Reset fields to prevent accidental further modifications of the buffer.
    buffer_start_ = nullptr;
    pc_ = nullptr;
    return buffer;
  }

  // This function is called when code generation is aborted, so that
  // the assembler could clean up internal data structures.
  virtual void AbortedCodeGeneration() {}

  // Debugging
  void Print(Isolate* isolate);

  // Record an inline code comment that can be used by a disassembler.
  // Use --code-comments to enable.
  V8_INLINE void RecordComment(
      const char* comment,
      const SourceLocation& loc = SourceLocation::Current()) {
    // Set explicit dependency on --code-comments for dead-code elimination in
    // release builds.
    if (!v8_flags.code_comments) return;
    if (options().emit_code_comments) {
      std::string comment_str(comment);
      if (loc.FileName()) {
        comment_str += " - " + loc.ToString();
      }
      code_comments_writer_.Add(pc_offset(), comment_str);
    }
  }

  V8_INLINE void RecordComment(
      std::string comment,
      const SourceLocation& loc = SourceLocation::Current()) {
    // Set explicit dependency on --code-comments for dead-code elimination in
    // release builds.
    if (!v8_flags.code_comments) return;
    if (options().emit_code_comments) {
      std::string comment_str(comment);
      if (loc.FileName()) {
        comment_str += " - " + loc.ToString();
      }
      code_comments_writer_.Add(pc_offset(), comment_str);
    }
  }

#ifdef V8_CODE_COMMENTS
  class CodeComment {
   public:
    // `comment` can either be a value convertible to std::string, or a function
    // that returns a value convertible to std::string which is invoked lazily
    // when code comments are enabled.
    template <typename CommentGen>
    V8_NODISCARD CodeComment(
        Assembler* assembler, CommentGen&& comment,
        const SourceLocation& loc = SourceLocation::Current())
        : assembler_(assembler) {
      if (!v8_flags.code_comments) return;
      if constexpr (std::is_invocable_v<CommentGen>) {
        Open(comment(), loc);
      } else {
        Open(comment, loc);
      }
    }
    ~CodeComment() {
      if (!v8_flags.code_comments) return;
      Close();
    }
    static const int kIndentWidth = 2;

   private:
    int depth() const;
    void Open(const std::string& comment, const SourceLocation& loc);
    void Close();
    Assembler* assembler_;
  };
#else  // V8_CODE_COMMENTS
  class CodeComment {
    V8_NODISCARD CodeComment(Assembler*, const std::string&) {}
  };
#endif

  // The minimum buffer size. Should be at least two times the platform-specific
  // {Assembler::kGap}.
  static constexpr int kMinimalBufferSize = 128;

  // The default buffer size used if we do not know the final size of the
  // generated code.
  static constexpr int kDefaultBufferSize = 4 * KB;

 protected:
  // Add 'target' to the {code_targets_} vector, if necessary, and return the
  // offset at which it is stored.
  int AddCodeTarget(Handle<Code> target);
  Handle<Code> GetCodeTarget(intptr_t code_target_index) const;

  // Add 'object' to the {embedded_objects_} vector and return the index at
  // which it is stored.
  using EmbeddedObjectIndex = size_t;
  EmbeddedObjectIndex AddEmbeddedObject(Handle<HeapObject> object);
  Handle<HeapObject> GetEmbeddedObject(EmbeddedObjectIndex index) const;

  // The buffer into which code and relocation info are generated.
  std::unique_ptr<AssemblerBuffer> buffer_;
  // Cached from {buffer_->start()}, for faster access.
  uint8_t* buffer_start_;
  std::forward_list<HeapNumberRequest> heap_number_requests_;
  // The program counter, which points into the buffer above and moves forward.
  // TODO(jkummerow): This should probably have type {Address}.
  uint8_t* pc_;

  void set_constant_pool_available(bool available) {
    if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
      constant_pool_available_ = available;
    } else {
      // Embedded constant pool not supported on this architecture.
      UNREACHABLE();
    }
  }

  // {RequestHeapNumber} records the need for a future heap number allocation,
  // code stub generation or string allocation. After code assembly, each
  // platform's {Assembler::AllocateAndInstallRequestedHeapNumbers} will
  // allocate these objects and place them where they are expected (determined
  // by the pc offset associated with each request).
  void RequestHeapNumber(HeapNumberRequest request);

  bool ShouldRecordRelocInfo(RelocInfo::Mode rmode) const {
    DCHECK(!RelocInfo::IsNoInfo(rmode));
    if (options().disable_reloc_info_for_patching) return false;
    if (RelocInfo::IsOnlyForSerializer(rmode) &&
        !options().record_reloc_info_for_serialization &&
        !v8_flags.debug_code) {
      return false;
    }
    if (RelocInfo::IsOnlyForDisassembler(rmode)) {
#ifdef ENABLE_DISASSEMBLER
      return true;
#else
      return false;
#endif  // ENABLE_DISASSEMBLER
    }
    return true;
  }

  CodeCommentsWriter code_comments_writer_;

 private:
  // Before we copy code into the code space, we sometimes cannot encode
  // call/jump code targets as we normally would, as the difference between the
  // instruction's location in the temporary buffer and the call target is not
  // guaranteed to fit in the instruction's offset field. We keep track of the
  // code handles we encounter in calls in this vector, and encode the index of
  // the code handle in the vector instead.
  std::vector<Handle<Code>> code_targets_;

  // If an assembler needs a small number to refer to a heap object handle
  // (for example, because there are only 32bit available on a 64bit arch), the
  // assembler adds the object into this vector using AddEmbeddedObject, and
  // may then refer to the heap object using the handle's index in this vector.
  std::vector<Handle<HeapObject>> embedded_objects_;

  // Embedded objects are deduplicated based on handle location. This is a
  // compromise that is almost as effective as deduplication based on actual
  // heap object addresses maintains GC safety.
  std::unordered_map<Handle<HeapObject>, EmbeddedObjectIndex,
                     Handle<HeapObject>::hash, Handle<HeapObject>::equal_to>
      embedded_objects_map_;

  const AssemblerOptions options_;
  uint64_t enabled_cpu_features_;
  bool predictable_code_size_;

  // Indicates whether the constant pool can be accessed, which is only possible
  // if the pp register points to the current code object's constant pool.
  bool constant_pool_available_;

  JumpOptimizationInfo* jump_optimization_info_;

#ifdef V8_CODE_COMMENTS
  int comment_depth_ = 0;
#endif

  // Constant pool.
  friend class FrameAndConstantPoolScope;
  friend class ConstantPoolUnavailableScope;
};

// Enable a specified feature within a scope.
class V8_EXPORT_PRIVATE V8_NODISCARD CpuFeatureScope {
 public:
  enum CheckPolicy {
    kCheckSupported,
    kDontCheckSupported,
  };

#ifdef DEBUG
  CpuFeatureScope(AssemblerBase* assembler, CpuFeature f,
                  CheckPolicy check = kCheckSupported);
  ~CpuFeatureScope();

 private:
  AssemblerBase* assembler_;
  uint64_t old_enabled_;
#else
  CpuFeatureScope(AssemblerBase* assembler, CpuFeature f,
                  CheckPolicy check = kCheckSupported) {}
  ~CpuFeatureScope() {
    // Define a destructor to avoid unused variable warnings.
  }
#endif
};

#ifdef V8_CODE_COMMENTS
#if V8_SUPPORTS_SOURCE_LOCATION
// We'll get the function name from the source location, no need to pass it in.
#define ASM_CODE_COMMENT(asm) ASM_CODE_COMMENT_STRING(asm, "")
#else
#define ASM_CODE_COMMENT(asm) ASM_CODE_COMMENT_STRING(asm, __func__)
#endif
#define ASM_CODE_COMMENT_STRING(asm, comment) \
  AssemblerBase::CodeComment UNIQUE_IDENTIFIER(asm_code_comment)(asm, comment)
#else
#define ASM_CODE_COMMENT(asm)
#define ASM_CODE_COMMENT_STRING(asm, ...)
#endif

// Use this macro to mark functions that are only defined if
// V8_ENABLE_DEBUG_CODE is set, and are a no-op otherwise.
// Use like:
//   void AssertMyCondition() NOOP_UNLESS_DEBUG_CODE;
#ifdef V8_ENABLE_DEBUG_CODE
#define NOOP_UNLESS_DEBUG_CODE
#else
#define NOOP_UNLESS_DEBUG_CODE                                        \
  { static_assert(v8_flags.debug_code.value() == false); }            \
  /* Dummy static_assert to swallow the semicolon after this macro */ \
  static_assert(true)
#endif

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_ASSEMBLER_H_
