// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DEOPTIMIZATION_DATA_H_
#define V8_OBJECTS_DEOPTIMIZATION_DATA_H_

#include <vector>

#include "src/objects/bytecode-array.h"
#include "src/objects/fixed-array.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// This class holds data required during deoptimization. It does not have its
// own instance type.
class DeoptimizationLiteralArray : public WeakFixedArray {
 public:
  // Getters for literals. These include runtime checks that the pointer was not
  // cleared, if the literal was held weakly.
  inline Tagged<Object> get(int index) const;
  inline Tagged<Object> get(PtrComprCageBase cage_base, int index) const;

  // TODO(jgruber): Swap these names around. It's confusing that this
  // WeakFixedArray subclass redefines `get` with different semantics.
  inline Tagged<MaybeObject> get_raw(int index) const;

  // Setter for literals. This will set the object as strong or weak depending
  // on InstructionStream::IsWeakObjectInOptimizedCode.
  inline void set(int index, Tagged<Object> value);

  DECL_CAST(DeoptimizationLiteralArray)

  OBJECT_CONSTRUCTORS(DeoptimizationLiteralArray, WeakFixedArray);
};

// The DeoptimizationFrameTranslation is the on-heap representation of
// translations created during code generation in a (zone-allocated)
// DeoptimizationFrameTranslationBuilder. The translation specifies how to
// transform an optimized frame back into one or more unoptimized frames.
enum class TranslationOpcode;
class DeoptimizationFrameTranslation : public ByteArray {
 public:
  DECL_CAST(DeoptimizationFrameTranslation)

  struct FrameCount {
    int total_frame_count;
    int js_frame_count;
  };

  class Iterator;

#ifdef V8_USE_ZLIB
  // Constants describing compressed DeoptimizationFrameTranslation layout. Only
  // relevant if
  // --turbo-compress-frame-translation is enabled.
  static constexpr int kUncompressedSizeOffset = 0;
  static constexpr int kUncompressedSizeSize = kInt32Size;
  static constexpr int kCompressedDataOffset =
      kUncompressedSizeOffset + kUncompressedSizeSize;
  static constexpr int kDeoptimizationFrameTranslationElementSize = kInt32Size;
#endif  // V8_USE_ZLIB

#ifdef ENABLE_DISASSEMBLER
  void PrintFrameTranslation(
      std::ostream& os, int index,
      Tagged<DeoptimizationLiteralArray> literal_array) const;
#endif

  OBJECT_CONSTRUCTORS(DeoptimizationFrameTranslation, ByteArray);
};

class DeoptimizationFrameTranslation::Iterator {
 public:
  Iterator(Tagged<DeoptimizationFrameTranslation> buffer, int index);

  int32_t NextOperand();

  uint32_t NextOperandUnsigned();

  DeoptimizationFrameTranslation::FrameCount EnterBeginOpcode();

  TranslationOpcode NextOpcode();

  TranslationOpcode SeekNextJSFrame();
  TranslationOpcode SeekNextFrame();

  bool HasNextOpcode() const;

  void SkipOperands(int n) {
    for (int i = 0; i < n; i++) NextOperand();
  }

 private:
  TranslationOpcode NextOpcodeAtPreviousIndex();
  uint32_t NextUnsignedOperandAtPreviousIndex();
  void SkipOpcodeAndItsOperandsAtPreviousIndex();

  std::vector<int32_t> uncompressed_contents_;
  Tagged<DeoptimizationFrameTranslation> buffer_;
  int index_;

  // This decrementing counter indicates how many more times to read operations
  // from the previous translation before continuing to move the index forward.
  int remaining_ops_to_use_from_previous_translation_ = 0;

  // An index into buffer_ for operations starting at a previous BEGIN, which
  // can be used to read operations referred to by MATCH_PREVIOUS_TRANSLATION.
  int previous_index_ = 0;

  // When starting a new MATCH_PREVIOUS_TRANSLATION operation, we'll need to
  // advance the previous_index_ by this many steps.
  int ops_since_previous_index_was_updated_ = 0;
};

// DeoptimizationData is a fixed array used to hold the deoptimization data for
// optimized code.  It also contains information about functions that were
// inlined.  If N different functions were inlined then the first N elements of
// the literal array will contain these functions.
//
// It can be empty.
class DeoptimizationData : public TrustedFixedArray {
 public:
  // Layout description.  Indices in the array.
  static const int kFrameTranslationIndex = 0;
  static const int kInlinedFunctionCountIndex = 1;
  static const int kLiteralArrayIndex = 2;
  static const int kOsrBytecodeOffsetIndex = 3;
  static const int kOsrPcOffsetIndex = 4;
  static const int kOptimizationIdIndex = 5;
  static const int kSharedFunctionInfoIndex = 6;
  static const int kInliningPositionsIndex = 7;
  static const int kDeoptExitStartIndex = 8;
  static const int kEagerDeoptCountIndex = 9;
  static const int kLazyDeoptCountIndex = 10;
  static const int kFirstDeoptEntryIndex = 11;

  // Offsets of deopt entry elements relative to the start of the entry.
  static const int kBytecodeOffsetRawOffset = 0;
  static const int kTranslationIndexOffset = 1;
  static const int kPcOffset = 2;
#ifdef DEBUG
  static const int kNodeIdOffset = 3;
  static const int kDeoptEntrySize = 4;
#else   // DEBUG
  static const int kDeoptEntrySize = 3;
#endif  // DEBUG

// Simple element accessors.
#define DECL_ELEMENT_ACCESSORS(name, type) \
  inline type name() const;                \
  inline void Set##name(type value);

  DECL_ELEMENT_ACCESSORS(FrameTranslation,
                         Tagged<DeoptimizationFrameTranslation>)
  DECL_ELEMENT_ACCESSORS(InlinedFunctionCount, Tagged<Smi>)
  DECL_ELEMENT_ACCESSORS(LiteralArray, Tagged<DeoptimizationLiteralArray>)
  DECL_ELEMENT_ACCESSORS(OsrBytecodeOffset, Tagged<Smi>)
  DECL_ELEMENT_ACCESSORS(OsrPcOffset, Tagged<Smi>)
  DECL_ELEMENT_ACCESSORS(OptimizationId, Tagged<Smi>)
  DECL_ELEMENT_ACCESSORS(SharedFunctionInfo, Tagged<Object>)
  DECL_ELEMENT_ACCESSORS(InliningPositions, Tagged<PodArray<InliningPosition>>)
  DECL_ELEMENT_ACCESSORS(DeoptExitStart, Tagged<Smi>)
  DECL_ELEMENT_ACCESSORS(EagerDeoptCount, Tagged<Smi>)
  DECL_ELEMENT_ACCESSORS(LazyDeoptCount, Tagged<Smi>)

#undef DECL_ELEMENT_ACCESSORS

// Accessors for elements of the ith deoptimization entry.
#define DECL_ENTRY_ACCESSORS(name, type) \
  inline type name(int i) const;         \
  inline void Set##name(int i, type value);

  DECL_ENTRY_ACCESSORS(BytecodeOffsetRaw, Tagged<Smi>)
  DECL_ENTRY_ACCESSORS(TranslationIndex, Tagged<Smi>)
  DECL_ENTRY_ACCESSORS(Pc, Tagged<Smi>)
#ifdef DEBUG
  DECL_ENTRY_ACCESSORS(NodeId, Tagged<Smi>)
#endif  // DEBUG

#undef DECL_ENTRY_ACCESSORS

  // In case the innermost frame is a builtin continuation stub, then this field
  // actually contains the builtin id. See uses of
  // `Builtins::GetBuiltinFromBytecodeOffset`.
  // TODO(olivf): Add some validation that callers do not misinterpret the
  // result.
  inline BytecodeOffset GetBytecodeOffsetOrBuiltinContinuationId(int i) const;

  inline void SetBytecodeOffset(int i, BytecodeOffset value);

  inline int DeoptCount() const;

  static const int kNotInlinedIndex = -1;

  // Returns the inlined function at the given position in LiteralArray, or the
  // outer function if index == kNotInlinedIndex.
  Tagged<class SharedFunctionInfo> GetInlinedFunction(int index);

  // Allocates a DeoptimizationData.
  static Handle<DeoptimizationData> New(Isolate* isolate,
                                        int deopt_entry_count);
  static Handle<DeoptimizationData> New(LocalIsolate* isolate,
                                        int deopt_entry_count);

  // Return an empty DeoptimizationData.
  V8_EXPORT_PRIVATE static Handle<DeoptimizationData> Empty(Isolate* isolate);
  V8_EXPORT_PRIVATE static Handle<DeoptimizationData> Empty(
      LocalIsolate* isolate);

  DECL_CAST(DeoptimizationData)

#ifdef DEBUG
  void Verify(Handle<BytecodeArray> bytecode) const;
#endif
#ifdef ENABLE_DISASSEMBLER
  void PrintDeoptimizationData(std::ostream& os) const;
#endif

 private:
  static int IndexForEntry(int i) {
    return kFirstDeoptEntryIndex + (i * kDeoptEntrySize);
  }

  static int LengthFor(int entry_count) { return IndexForEntry(entry_count); }

  OBJECT_CONSTRUCTORS(DeoptimizationData, TrustedFixedArray);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DEOPTIMIZATION_DATA_H_
