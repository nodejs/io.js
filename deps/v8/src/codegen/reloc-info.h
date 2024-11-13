// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_RELOC_INFO_H_
#define V8_CODEGEN_RELOC_INFO_H_

#include "src/base/export-template.h"
#include "src/common/code-memory-access.h"
#include "src/common/globals.h"
#include "src/objects/code.h"
#include "src/objects/instruction-stream.h"

namespace v8 {
namespace internal {

class CodeReference;
class EmbeddedData;

// Specifies whether to perform icache flush operations on RelocInfo updates.
// If FLUSH_ICACHE_IF_NEEDED, the icache will always be flushed if an
// instruction was modified. If SKIP_ICACHE_FLUSH the flush will always be
// skipped (only use this if you will flush the icache manually before it is
// executed).
enum ICacheFlushMode { FLUSH_ICACHE_IF_NEEDED, SKIP_ICACHE_FLUSH };

namespace detail {
// -----------------------------------------------------------------------------
// Implementation of RelocInfoWriter and RelocIterator
//
// Relocation information is written backwards in memory, from high addresses
// towards low addresses, byte by byte.  Therefore, in the encodings listed
// below, the first byte listed it at the highest address, and successive
// bytes in the record are at progressively lower addresses.
//
// Encoding
//
// The most common modes are given single-byte encodings.  Also, it is
// easy to identify the type of reloc info and skip unwanted modes in
// an iteration.
//
// The encoding relies on the fact that there are fewer than 14
// different relocation modes using standard non-compact encoding.
//
// The first byte of a relocation record has a tag in its low 2 bits:
// Here are the record schemes, depending on the low tag and optional higher
// tags.
//
// Low tag:
//   00: embedded_object:      [6-bit pc delta] 00
//
//   01: code_target:          [6-bit pc delta] 01
//
//   10: wasm_stub_call:       [6-bit pc delta] 10
//
//   11: long_record           [6 bit reloc mode] 11
//                             followed by pc delta
//                             followed by optional data depending on type.
//
//  If a pc delta exceeds 6 bits, it is split into a remainder that fits into
//  6 bits and a part that does not. The latter is encoded as a long record
//  with PC_JUMP as pseudo reloc info mode. The former is encoded as part of
//  the following record in the usual way. The long pc jump record has variable
//  length:
//               pc-jump:        [PC_JUMP] 11
//                               1 [7 bits data]
//                                  ...
//                               0 [7 bits data]
//               (Bits 6..31 of pc delta, encoded with VLQ.)

constexpr int kTagBits = 2;
constexpr int kTagMask = (1 << kTagBits) - 1;
constexpr int kLongTagBits = 6;

constexpr int kEmbeddedObjectTag = 0;
constexpr int kCodeTargetTag = 1;
constexpr int kWasmStubCallTag = 2;
constexpr int kDefaultTag = 3;

constexpr int kSmallPCDeltaBits = kBitsPerByte - kTagBits;
constexpr int kSmallPCDeltaMask = (1 << kSmallPCDeltaBits) - 1;
}  // namespace detail

// -----------------------------------------------------------------------------
// Relocation information

// Relocation information consists of the address (pc) of the datum
// to which the relocation information applies, the relocation mode
// (rmode), and an optional data field. The relocation mode may be
// "descriptive" and not indicate a need for relocation, but simply
// describe a property of the datum. Such rmodes are useful for GC
// and nice disassembly output.

class RelocInfo {
 public:
  // The minimum size of a comment is equal to two bytes for the extra tagged
  // pc and kSystemPointerSize for the actual pointer to the comment.
  static constexpr int kMinRelocCommentSize = 2 + kSystemPointerSize;

  // The maximum size for a call instruction including pc-jump.
  static constexpr int kMaxCallSize = 6;

  // The maximum pc delta that will use the short encoding.
  static constexpr int kMaxSmallPCDelta = detail::kSmallPCDeltaMask;

  enum Mode : int8_t {
    // Please note the order is important (see IsRealRelocMode, IsGCRelocMode,
    // and IsShareableRelocMode predicates below).

    NO_INFO,  // Never recorded value. Most common one, hence value 0.

    CODE_TARGET,
    // TODO(ishell): rename to NEAR_CODE_TARGET.
    RELATIVE_CODE_TARGET,  // LAST_CODE_TARGET_MODE
    COMPRESSED_EMBEDDED_OBJECT,
    FULL_EMBEDDED_OBJECT,  // LAST_GCED_ENUM

    WASM_CALL,  // FIRST_SHAREABLE_RELOC_MODE
    WASM_STUB_CALL,
    WASM_CANONICAL_SIG_ID,

    EXTERNAL_REFERENCE,  // The address of an external C++ function.
    INTERNAL_REFERENCE,  // An address inside the same function.

    // The relative address (target-table) in the switch table.
    RELATIVE_SWITCH_TABLE_ENTRY,

    // Encoded internal reference, used only on RISCV64, RISCV32, MIPS64
    // and PPC.
    INTERNAL_REFERENCE_ENCODED,

    // An off-heap instruction stream target. See http://goo.gl/Z2HUiM.
    // TODO(ishell): rename to BUILTIN_ENTRY.
    OFF_HEAP_TARGET,  // FIRST_BUILTIN_ENTRY_MODE
    // An un-embedded off-heap instruction stream target.
    // See http://crbug.com/v8/11527 for details.
    NEAR_BUILTIN_ENTRY,  // LAST_BUILTIN_ENTRY_MODE

    // Marks constant and veneer pools. Only used on ARM and ARM64.
    // They use a custom noncompact encoding.
    CONST_POOL,
    VENEER_POOL,

    DEOPT_SCRIPT_OFFSET,
    DEOPT_INLINING_ID,  // Deoptimization source position.
    DEOPT_REASON,       // Deoptimization reason index.
    DEOPT_ID,           // Deoptimization inlining id.
    DEOPT_NODE_ID,      // Id of the node that caused deoptimization. This
                        // information is only recorded in debug builds.

    // This is not an actual reloc mode, but used to encode a long pc jump that
    // cannot be encoded as part of another record.
    PC_JUMP,

    // Pseudo-types
    NUMBER_OF_MODES,

    LAST_CODE_TARGET_MODE = RELATIVE_CODE_TARGET,
    FIRST_REAL_RELOC_MODE = CODE_TARGET,
    LAST_REAL_RELOC_MODE = VENEER_POOL,
    FIRST_EMBEDDED_OBJECT_RELOC_MODE = COMPRESSED_EMBEDDED_OBJECT,
    LAST_EMBEDDED_OBJECT_RELOC_MODE = FULL_EMBEDDED_OBJECT,
    LAST_GCED_ENUM = LAST_EMBEDDED_OBJECT_RELOC_MODE,
    FIRST_BUILTIN_ENTRY_MODE = OFF_HEAP_TARGET,
    LAST_BUILTIN_ENTRY_MODE = NEAR_BUILTIN_ENTRY,
    FIRST_SHAREABLE_RELOC_MODE = WASM_CALL,
  };

  static_assert(NUMBER_OF_MODES <= kBitsPerInt);

  RelocInfo() = default;

  RelocInfo(Address pc, Mode rmode, intptr_t data,
            Address constant_pool = kNullAddress)
      : pc_(pc), rmode_(rmode), data_(data), constant_pool_(constant_pool) {
    DCHECK_IMPLIES(!COMPRESS_POINTERS_BOOL,
                   rmode != COMPRESSED_EMBEDDED_OBJECT);
  }

  // Convenience ctor.
  RelocInfo(Address pc, Mode rmode) : RelocInfo(pc, rmode, 0) {}

  static constexpr bool IsRealRelocMode(Mode mode) {
    return mode >= FIRST_REAL_RELOC_MODE && mode <= LAST_REAL_RELOC_MODE;
  }
  // Is the relocation mode affected by GC?
  static constexpr bool IsGCRelocMode(Mode mode) {
    return mode <= LAST_GCED_ENUM;
  }
  static constexpr bool IsShareableRelocMode(Mode mode) {
    return mode == RelocInfo::NO_INFO ||
           mode >= RelocInfo::FIRST_SHAREABLE_RELOC_MODE;
  }
  static constexpr bool IsCodeTarget(Mode mode) { return mode == CODE_TARGET; }
  static constexpr bool IsCodeTargetMode(Mode mode) {
    return mode <= LAST_CODE_TARGET_MODE;
  }
  static constexpr bool IsRelativeCodeTarget(Mode mode) {
    return mode == RELATIVE_CODE_TARGET;
  }
  static constexpr bool IsFullEmbeddedObject(Mode mode) {
    return mode == FULL_EMBEDDED_OBJECT;
  }
  static constexpr bool IsCompressedEmbeddedObject(Mode mode) {
    return COMPRESS_POINTERS_BOOL && mode == COMPRESSED_EMBEDDED_OBJECT;
  }
  static constexpr bool IsEmbeddedObjectMode(Mode mode) {
    return base::IsInRange(mode, FIRST_EMBEDDED_OBJECT_RELOC_MODE,
                           LAST_EMBEDDED_OBJECT_RELOC_MODE);
  }
  static constexpr bool IsWasmCall(Mode mode) { return mode == WASM_CALL; }
  static constexpr bool IsWasmStubCall(Mode mode) {
    return mode == WASM_STUB_CALL;
  }
  static constexpr bool IsWasmCanonicalSigId(Mode mode) {
    return mode == WASM_CANONICAL_SIG_ID;
  }
  static constexpr bool IsConstPool(Mode mode) { return mode == CONST_POOL; }
  static constexpr bool IsVeneerPool(Mode mode) { return mode == VENEER_POOL; }
  static constexpr bool IsDeoptPosition(Mode mode) {
    return mode == DEOPT_SCRIPT_OFFSET || mode == DEOPT_INLINING_ID;
  }
  static constexpr bool IsDeoptReason(Mode mode) {
    return mode == DEOPT_REASON;
  }
  static constexpr bool IsDeoptId(Mode mode) { return mode == DEOPT_ID; }
  static constexpr bool IsDeoptNodeId(Mode mode) {
    return mode == DEOPT_NODE_ID;
  }
  static constexpr bool IsExternalReference(Mode mode) {
    return mode == EXTERNAL_REFERENCE;
  }
  static constexpr bool IsInternalReference(Mode mode) {
    return mode == INTERNAL_REFERENCE;
  }
  static constexpr bool IsRelativeSwitchTableEntry(Mode mode) {
    return mode == RELATIVE_SWITCH_TABLE_ENTRY;
  }
  static constexpr bool IsInternalReferenceEncoded(Mode mode) {
    return mode == INTERNAL_REFERENCE_ENCODED;
  }
  static constexpr bool IsOffHeapTarget(Mode mode) {
    return mode == OFF_HEAP_TARGET;
  }
  static constexpr bool IsNearBuiltinEntry(Mode mode) {
    return mode == NEAR_BUILTIN_ENTRY;
  }
  static constexpr bool IsBuiltinEntryMode(Mode mode) {
    return base::IsInRange(mode, FIRST_BUILTIN_ENTRY_MODE,
                           LAST_BUILTIN_ENTRY_MODE);
  }
  static constexpr bool IsNoInfo(Mode mode) { return mode == NO_INFO; }

  static bool IsOnlyForSerializer(Mode mode) {
#ifdef V8_TARGET_ARCH_IA32
    // On ia32, inlined off-heap trampolines must be relocated.
    DCHECK_NE((kApplyMask & ModeMask(OFF_HEAP_TARGET)), 0);
    DCHECK_EQ((kApplyMask & ModeMask(EXTERNAL_REFERENCE)), 0);
    return mode == EXTERNAL_REFERENCE;
#else
    DCHECK_EQ((kApplyMask & ModeMask(OFF_HEAP_TARGET)), 0);
    DCHECK_EQ((kApplyMask & ModeMask(EXTERNAL_REFERENCE)), 0);
    return mode == EXTERNAL_REFERENCE || mode == OFF_HEAP_TARGET;
#endif
  }

  static bool IsOnlyForDisassembler(Mode mode) {
    return mode == RELATIVE_SWITCH_TABLE_ENTRY;
  }

  static constexpr int ModeMask(Mode mode) { return 1 << mode; }

  // Accessors
  Address pc() const { return pc_; }
  Mode rmode() const { return rmode_; }
  intptr_t data() const { return data_; }

  // Is the pointer this relocation info refers to coded like a plain pointer
  // or is it strange in some way (e.g. relative or patched into a series of
  // instructions).
  bool IsCodedSpecially();

  // The static pendant to IsCodedSpecially, just for off-heap targets. Used
  // during deserialization, when we don't actually have a RelocInfo handy.
  static bool OffHeapTargetIsCodedSpecially();

  // If true, the pointer this relocation info refers to is an entry in the
  // constant pool, otherwise the pointer is embedded in the instruction stream.
  bool IsInConstantPool();

  Address wasm_call_address() const;
  Address wasm_stub_call_address() const;
  V8_EXPORT_PRIVATE uint32_t wasm_canonical_sig_id() const;

  uint32_t wasm_call_tag() const;

  void set_off_heap_target_address(
      Address target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  // this relocation applies to;
  // can only be called if IsCodeTarget(rmode_)
  V8_INLINE Address target_address();
  // Cage base value is used for decompressing compressed embedded references.
  V8_INLINE Tagged<HeapObject> target_object(PtrComprCageBase cage_base);

  V8_INLINE Handle<HeapObject> target_object_handle(Assembler* origin);

  // Decodes builtin ID encoded as a PC-relative offset. This encoding is used
  // during code generation of call/jump with NEAR_BUILTIN_ENTRY.
  V8_INLINE Builtin target_builtin_at(Assembler* origin);
  V8_INLINE Address target_off_heap_target();

  // Returns the address of the constant pool entry where the target address
  // is held.  This should only be called if IsInConstantPool returns true.
  V8_INLINE Address constant_pool_entry_address();

  // Read the address of the word containing the target_address in an
  // instruction stream.  What this means exactly is architecture-independent.
  // The only architecture-independent user of this function is the serializer.
  // The serializer uses it to find out how many raw bytes of instruction to
  // output before the next target.  Architecture-independent code shouldn't
  // dereference the pointer it gets back from this.
  V8_INLINE Address target_address_address();
  bool HasTargetAddressAddress() const;

  // This indicates how much space a target takes up when deserializing a code
  // stream.  For most architectures this is just the size of a pointer.  For
  // an instruction like movw/movt where the target bits are mixed into the
  // instruction bits the size of the target will be zero, indicating that the
  // serializer should not step forwards in memory after a target is resolved
  // and written.  In this case the target_address_address function above
  // should return the end of the instructions to be patched, allowing the
  // deserializer to deserialize the instructions as raw bytes and put them in
  // place, ready to be patched with the target.
  V8_INLINE int target_address_size();

  // Read the reference in the instruction this relocation
  // applies to; can only be called if rmode_ is EXTERNAL_REFERENCE.
  V8_INLINE Address target_external_reference();

  // Read the reference in the instruction this relocation
  // applies to; can only be called if rmode_ is INTERNAL_REFERENCE.
  V8_INLINE Address target_internal_reference();

  // Return the reference address this relocation applies to;
  // can only be called if rmode_ is INTERNAL_REFERENCE.
  V8_INLINE Address target_internal_reference_address();

  template <typename ObjectVisitor>
  void Visit(Tagged<InstructionStream> host, ObjectVisitor* visitor) {
    Mode mode = rmode();
    if (IsEmbeddedObjectMode(mode)) {
      visitor->VisitEmbeddedPointer(host, this);
    } else if (IsCodeTargetMode(mode)) {
      visitor->VisitCodeTarget(host, this);
    } else if (IsExternalReference(mode)) {
      visitor->VisitExternalReference(host, this);
    } else if (IsInternalReference(mode) || IsInternalReferenceEncoded(mode)) {
      visitor->VisitInternalReference(host, this);
    } else if (IsBuiltinEntryMode(mode)) {
      visitor->VisitOffHeapTarget(host, this);
    }
  }

#ifdef ENABLE_DISASSEMBLER
  // Printing
  static const char* RelocModeName(Mode rmode);
  void Print(Isolate* isolate, std::ostream& os);
#endif  // ENABLE_DISASSEMBLER
#ifdef VERIFY_HEAP
  void Verify(Isolate* isolate);
#endif

  static const int kApplyMask;  // Modes affected by apply.  Depends on arch.

  static constexpr int AllRealModesMask() {
    constexpr Mode kFirstUnrealRelocMode =
        static_cast<Mode>(RelocInfo::LAST_REAL_RELOC_MODE + 1);
    return (ModeMask(kFirstUnrealRelocMode) - 1) &
           ~(ModeMask(RelocInfo::FIRST_REAL_RELOC_MODE) - 1);
  }

  static int EmbeddedObjectModeMask() {
    return ModeMask(RelocInfo::FULL_EMBEDDED_OBJECT) |
           ModeMask(RelocInfo::COMPRESSED_EMBEDDED_OBJECT);
  }

  // In addition to modes covered by the apply mask (which is applied at GC
  // time, among others), this covers all modes that are relocated by
  // InstructionStream::CopyFromNoFlush after code generation.
  static int PostCodegenRelocationMask() {
    return ModeMask(RelocInfo::CODE_TARGET) |
           ModeMask(RelocInfo::COMPRESSED_EMBEDDED_OBJECT) |
           ModeMask(RelocInfo::FULL_EMBEDDED_OBJECT) |
           ModeMask(RelocInfo::NEAR_BUILTIN_ENTRY) |
           ModeMask(RelocInfo::WASM_STUB_CALL) |
           ModeMask(RelocInfo::RELATIVE_CODE_TARGET) | kApplyMask;
  }

 protected:
  // On ARM/ARM64, note that pc_ is the address of the instruction referencing
  // the constant pool and not the address of the constant pool entry.
  Address pc_;
  Mode rmode_;
  intptr_t data_ = 0;
  Address constant_pool_ = kNullAddress;

  template <typename RelocIteratorType>
  friend class RelocIteratorBase;
};

class WritableRelocInfo : public RelocInfo {
 public:
  WritableRelocInfo(WritableJitAllocation& jit_allocation, Address pc,
                    Mode rmode)
      : RelocInfo(pc, rmode) {}
  WritableRelocInfo(WritableJitAllocation& jit_allocation, Address pc,
                    Mode rmode, intptr_t data, Address constant_pool)
      : RelocInfo(pc, rmode, data, constant_pool) {}

  // Apply a relocation by delta bytes. When the code object is moved, PC
  // relative addresses have to be updated as well as absolute addresses
  // inside the code (internal references).
  // Do not forget to flush the icache afterwards!
  V8_INLINE void apply(intptr_t delta);

  void set_wasm_call_address(Address);
  void set_wasm_stub_call_address(Address);
  void set_wasm_canonical_sig_id(uint32_t);

  void set_target_address(
      Tagged<InstructionStream> host, Address target,
      WriteBarrierMode write_barrier_mode = UPDATE_WRITE_BARRIER,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);
  // Use this overload only when an InstructionStream host is not available.
  void set_target_address(Address target, ICacheFlushMode icache_flush_mode =
                                              FLUSH_ICACHE_IF_NEEDED);

  V8_INLINE void set_target_object(
      Tagged<InstructionStream> host, Tagged<HeapObject> target,
      WriteBarrierMode write_barrier_mode = UPDATE_WRITE_BARRIER,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);
  // Use this overload only when an InstructionStream host is not available.
  V8_INLINE void set_target_object(
      Tagged<HeapObject> target,
      ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);

  V8_INLINE void set_target_external_reference(
      Address, ICacheFlushMode icache_flush_mode = FLUSH_ICACHE_IF_NEEDED);
};

// RelocInfoWriter serializes a stream of relocation info. It writes towards
// lower addresses.
class RelocInfoWriter {
 public:
  RelocInfoWriter() : pos_(nullptr), last_pc_(nullptr) {}

  RelocInfoWriter(const RelocInfoWriter&) = delete;
  RelocInfoWriter& operator=(const RelocInfoWriter&) = delete;

  uint8_t* pos() const { return pos_; }
  uint8_t* last_pc() const { return last_pc_; }

  void Write(const RelocInfo* rinfo);

  // Update the state of the stream after reloc info buffer
  // and/or code is moved while the stream is active.
  void Reposition(uint8_t* pos, uint8_t* pc) {
    pos_ = pos;
    last_pc_ = pc;
  }

  // Max size (bytes) of a written RelocInfo. Longest encoding is
  // ExtraTag, VariableLengthPCJump, ExtraTag, pc_delta, data_delta.
  static constexpr int kMaxSize = 1 + 4 + 1 + 1 + kSystemPointerSize;

 private:
  inline uint32_t WriteLongPCJump(uint32_t pc_delta);

  inline void WriteShortTaggedPC(uint32_t pc_delta, int tag);
  inline void WriteShortData(intptr_t data_delta);

  inline void WriteMode(RelocInfo::Mode rmode);
  inline void WriteModeAndPC(uint32_t pc_delta, RelocInfo::Mode rmode);
  inline void WriteIntData(int data_delta);

  uint8_t* pos_;
  uint8_t* last_pc_;
};

// A RelocIterator iterates over relocation information.
// Typical use:
//
//   for (RelocIterator it(code); !it.done(); it.next()) {
//     // do something with it.rinfo() here
//   }
//
// A mask can be specified to skip unwanted modes.
template <typename RelocInfoT>
class RelocIteratorBase {
 public:
  static constexpr int kAllModesMask = -1;

  RelocIteratorBase(RelocIteratorBase&&) V8_NOEXCEPT = default;
  RelocIteratorBase(const RelocIteratorBase&) = delete;
  RelocIteratorBase& operator=(const RelocIteratorBase&) = delete;

  bool done() const { return done_; }
  void next();

  // The returned pointer is valid until the next call to next().
  RelocInfoT* rinfo() {
    DCHECK(!done());
    return &rinfo_;
  }

 protected:
  V8_INLINE RelocIteratorBase(RelocInfoT reloc_info, const uint8_t* pos,
                              const uint8_t* end, int mode_mask);

  // Used for efficiently skipping unwanted modes.
  bool SetMode(RelocInfo::Mode mode) {
    if ((mode_mask_ & (1 << mode)) == 0) return false;
    rinfo_.rmode_ = mode;
    return true;
  }

  RelocInfo::Mode GetMode() const {
    return static_cast<RelocInfo::Mode>((*pos_ >> detail::kTagBits) &
                                        ((1 << detail::kLongTagBits) - 1));
  }

  void Advance(int bytes = 1) { pos_ -= bytes; }
  int AdvanceGetTag() { return *--pos_ & detail::kTagMask; }
  void AdvanceReadLongPCJump();
  void AdvanceReadPC() { rinfo_.pc_ += *--pos_; }
  void AdvanceReadInt();

  void ReadShortTaggedPC() { rinfo_.pc_ += *pos_ >> detail::kTagBits; }
  void ReadShortData();

  const uint8_t* pos_;
  const uint8_t* const end_;
  RelocInfoT rinfo_;
  bool done_ = false;
  const int mode_mask_;
};

extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
    RelocIteratorBase<RelocInfo>;
extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
    RelocIteratorBase<WritableRelocInfo>;

class V8_EXPORT_PRIVATE RelocIterator : public RelocIteratorBase<RelocInfo> {
 public:
  // Prefer using this ctor when possible:
  explicit RelocIterator(Tagged<InstructionStream> istream, int mode_mask);
  // Convenience wrapper.
  explicit RelocIterator(Tagged<Code> code, int mode_mask = kAllModesMask);

  // For Wasm.
  explicit RelocIterator(base::Vector<uint8_t> instructions,
                         base::Vector<const uint8_t> reloc_info,
                         Address const_pool, int mode_mask = kAllModesMask);
  // For the disassembler.
  explicit RelocIterator(const CodeReference code_reference);
  // For FinalizeEmbeddedCodeTargets when creating embedded builtins.
  explicit RelocIterator(EmbeddedData* embedded_data, Tagged<Code> code,
                         int mode_mask);

  RelocIterator(RelocIterator&&) V8_NOEXCEPT = default;
  RelocIterator(const RelocIterator&) = delete;
  RelocIterator& operator=(const RelocIterator&) = delete;

 private:
  RelocIterator(Address pc, Address constant_pool, const uint8_t* pos,
                const uint8_t* end, int mode_mask);
};

class V8_EXPORT_PRIVATE WritableRelocIterator
    : public RelocIteratorBase<WritableRelocInfo> {
 public:
  // Constructor for iterating InstructionStreams.
  WritableRelocIterator(WritableJitAllocation& jit_allocation,
                        Tagged<InstructionStream> istream,
                        Address constant_pool, int mode_mask);
  // Constructor for iterating Wasm code.
  WritableRelocIterator(WritableJitAllocation& jit_allocation,
                        base::Vector<uint8_t> instructions,
                        base::Vector<const uint8_t> reloc_info,
                        Address constant_pool, int mode_mask = kAllModesMask);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RELOC_INFO_H_
