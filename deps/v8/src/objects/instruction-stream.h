// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_INSTRUCTION_STREAM_H_
#define V8_OBJECTS_INSTRUCTION_STREAM_H_

#ifdef DEBUG
#include <set>
#endif

#include "src/codegen/code-desc.h"
#include "src/objects/trusted-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class Code;
class WritableJitAllocation;

// InstructionStream contains the instruction stream for V8-generated code
// objects.
//
// When V8_EXTERNAL_CODE_SPACE is enabled, InstructionStream objects are
// allocated in a separate pointer compression cage instead of the cage where
// all the other objects are allocated.
//
// An InstructionStream is a trusted object as it lives outside of the sandbox
// and contains trusted content (machine code). However, it is special in that
// it doesn't live in the trusted space but instead in the code space.
class InstructionStream : public TrustedObject {
 public:
  NEVER_READ_ONLY_SPACE

  // All InstructionStream objects have the following layout:
  //
  //  +--------------------------+
  //  |          header          |
  //  +--------------------------+  <-- body_start()
  //  |       instructions       |   == instruction_start()
  //  |           ...            |
  //  | padded to meta alignment |      see kMetadataAlignment
  //  +--------------------------+  <-- instruction_end()
  //  |         metadata         |   == metadata_start() (MS)
  //  |           ...            |
  //  |                          |  <-- MS + handler_table_offset()
  //  |                          |  <-- MS + constant_pool_offset()
  //  |                          |  <-- MS + code_comments_offset()
  //  |                          |  <-- MS + unwinding_info_offset()
  //  | padded to obj alignment  |
  //  +--------------------------+  <-- metadata_end() == body_end()
  //  | padded to kCodeAlignmentMinusCodeHeader
  //  +--------------------------+
  //
  // In other words, the variable-size 'body' consists of 'instructions' and
  // 'metadata'.

  // Constants for use in static asserts, stating whether the body is adjacent,
  // i.e. instructions and metadata areas are adjacent.
  static constexpr bool kOnHeapBodyIsContiguous = true;
  static constexpr bool kOffHeapBodyIsContiguous = false;
  static constexpr bool kBodyIsContiguous =
      kOnHeapBodyIsContiguous && kOffHeapBodyIsContiguous;

  inline Address instruction_start() const;

  // The metadata section is aligned to this value.
  static constexpr int kMetadataAlignment = kIntSize;

  // [code]: The associated Code object.
  //
  // Set to Smi::zero() during initialization. Heap iterators may see
  // InstructionStream objects in this state.
  inline Tagged<Code> code(AcquireLoadTag tag) const;
  inline void set_code(Tagged<Code> value, ReleaseStoreTag tag);
  inline Tagged<Object> raw_code(AcquireLoadTag tag) const;
  // Use when the InstructionStream may be uninitialized:
  inline bool TryGetCode(Tagged<Code>* code_out, AcquireLoadTag tag) const;
  inline bool TryGetCodeUnchecked(Tagged<Code>* code_out,
                                  AcquireLoadTag tag) const;

  inline Address constant_pool() const;

  // [relocation_info]: InstructionStream relocation information.
  inline Tagged<ByteArray> relocation_info() const;
  // Unchecked accessor to be used during GC.
  inline Tagged<ByteArray> unchecked_relocation_info() const;

  inline uint8_t* relocation_start() const;
  inline uint8_t* relocation_end() const;
  inline int relocation_size() const;

  // The size of the entire body section, containing instructions and inlined
  // metadata.
  DECL_PRIMITIVE_GETTER(body_size, uint32_t)
  inline Address body_end() const;

  static constexpr int TrailingPaddingSizeFor(uint32_t body_size) {
    return RoundUp<kCodeAlignment>(kHeaderSize + body_size) - kHeaderSize -
           body_size;
  }
  static constexpr int SizeFor(int body_size) {
    return kHeaderSize + body_size + TrailingPaddingSizeFor(body_size);
  }
  inline int Size() const;

  static inline Tagged<InstructionStream> FromTargetAddress(Address address);
  static inline Tagged<InstructionStream> FromEntryAddress(
      Address location_of_address);

  // Relocate the code by delta bytes.
  void Relocate(WritableJitAllocation& jit_allocation, intptr_t delta);

  static V8_INLINE Tagged<InstructionStream> Initialize(
      Tagged<HeapObject> self, Tagged<Map> map, uint32_t body_size,
      int constant_pool_offset, Tagged<ByteArray> reloc_info);
  V8_INLINE void Finalize(Tagged<Code> code, Tagged<ByteArray> reloc_info,
                          CodeDesc desc, Heap* heap);
  V8_INLINE bool IsFullyInitialized();

  DECL_CAST(InstructionStream)
  DECL_PRINTER(InstructionStream)
  DECL_VERIFIER(InstructionStream)

  // Layout description.
#define ISTREAM_FIELDS(V)                                                     \
  V(kCodeOffset, kCodePointerSize)                                            \
  V(kStartOfStrongFieldsOffset, 0)                                            \
  V(kRelocationInfoOffset, kTaggedSize)                                       \
  V(kEndOfStrongFieldsOffset, 0)                                              \
  /* Data or code not directly visited by GC directly starts here. */         \
  V(kDataStart, 0)                                                            \
  V(kBodySizeOffset, kUInt32Size)                                             \
  V(kConstantPoolOffsetOffset, V8_EMBEDDED_CONSTANT_POOL_BOOL ? kIntSize : 0) \
  V(kUnalignedSize, OBJECT_POINTER_PADDING(kUnalignedSize))                   \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(TrustedObject::kHeaderSize, ISTREAM_FIELDS)
#undef ISTREAM_FIELDS

  static_assert(kCodeAlignment >= kHeaderSize);
  // We do two things to ensure kCodeAlignment of the entry address:
  // 1) Add kCodeAlignmentMinusCodeHeader padding once in the beginning of every
  //    MemoryChunk.
  // 2) Round up all IStream allocations to a multiple of kCodeAlignment, see
  //    TrailingPaddingSizeFor.
  // Together, the IStream object itself will always start at offset
  // kCodeAlignmentMinusCodeHeader, which aligns the entry to kCodeAlignment.
  static constexpr int kCodeAlignmentMinusCodeHeader =
      kCodeAlignment - kHeaderSize;

  class BodyDescriptor;

 private:
  friend class Factory;

  class V8_NODISCARD WriteBarrierPromise {
   public:
    WriteBarrierPromise() = default;
    WriteBarrierPromise(WriteBarrierPromise&&) V8_NOEXCEPT = default;
    WriteBarrierPromise(const WriteBarrierPromise&) = delete;
    WriteBarrierPromise& operator=(const WriteBarrierPromise&) = delete;

#ifdef DEBUG
    void RegisterAddress(Address address);
    void ResolveAddress(Address address);
    ~WriteBarrierPromise();

   private:
    std::set<Address> delayed_write_barriers_;
#else
    void RegisterAddress(Address address) {}
    void ResolveAddress(Address address) {}
#endif
  };

  // Migrate code from desc without flushing the instruction cache. This
  // function will not trigger any write barriers and the caller needs to call
  // RelocateFromDescWriteBarriers afterwards. This is split into two functions,
  // since the former needs write access to executable memory and we need to
  // keep this critical section minimal since any memory write poses attack
  // surface for CFI and will require special validation.
  WriteBarrierPromise RelocateFromDesc(WritableJitAllocation& jit_allocation,
                                       Heap* heap, const CodeDesc& desc,
                                       Address constant_pool,
                                       const DisallowGarbageCollection& no_gc);
  void RelocateFromDescWriteBarriers(Heap* heap, const CodeDesc& desc,
                                     Address constant_pool,
                                     WriteBarrierPromise& promise,
                                     const DisallowGarbageCollection& no_gc);

  // Must be used when loading any of InstructionStream's tagged fields.
  static inline PtrComprCageBase main_cage_base();

  OBJECT_CONSTRUCTORS(InstructionStream, TrustedObject);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_INSTRUCTION_STREAM_H_
