// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DEOPTIMIZATION_DATA_H_
#define V8_OBJECTS_DEOPTIMIZATION_DATA_H_

#include <vector>

#include "src/objects/bytecode-array.h"
#include "src/objects/fixed-array.h"
#include "src/utils/boxed-float.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// This class holds data required during deoptimization. It does not have its
// own instance type.
class DeoptimizationLiteralArray : public TrustedWeakFixedArray {
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

  OBJECT_CONSTRUCTORS(DeoptimizationLiteralArray, TrustedWeakFixedArray);
};

enum class DeoptimizationLiteralKind {
  kObject,
  kNumber,
  kSignedBigInt64,
  kUnsignedBigInt64,
  kHoleNaN,
  kInvalid,

  // These kinds are used by wasm only (as unoptimized JS doesn't have these
  // types).
  kWasmI31Ref,
  kWasmInt32,
  kWasmFloat32,
  kWasmFloat64,
  kWasmInt64 = kSignedBigInt64,
};

// A deoptimization literal during code generation. For JS this is transformed
// into a heap object after code generation. For wasm the DeoptimizationLiteral
// is directly used by the deoptimizer.
class DeoptimizationLiteral {
 public:
  DeoptimizationLiteral()
      : kind_(DeoptimizationLiteralKind::kInvalid), object_() {}
  explicit DeoptimizationLiteral(Handle<Object> object)
      : kind_(DeoptimizationLiteralKind::kObject), object_(object) {
    CHECK(!object_.is_null());
  }
  explicit DeoptimizationLiteral(Float32 number)
      : kind_(DeoptimizationLiteralKind::kWasmFloat32), float32_(number) {}
  explicit DeoptimizationLiteral(Float64 number)
      : kind_(DeoptimizationLiteralKind::kWasmFloat64), float64_(number) {}
  explicit DeoptimizationLiteral(double number)
      : kind_(DeoptimizationLiteralKind::kNumber), number_(number) {}
  explicit DeoptimizationLiteral(int64_t signed_bigint64)
      : kind_(DeoptimizationLiteralKind::kSignedBigInt64),
        int64_(signed_bigint64) {}
  explicit DeoptimizationLiteral(uint64_t unsigned_bigint64)
      : kind_(DeoptimizationLiteralKind::kUnsignedBigInt64),
        uint64_(unsigned_bigint64) {}
  explicit DeoptimizationLiteral(int32_t int32)
      : kind_(DeoptimizationLiteralKind::kWasmInt32), int64_(int32) {}
  explicit DeoptimizationLiteral(Tagged<Smi> smi)
      : kind_(DeoptimizationLiteralKind::kWasmI31Ref), int64_(smi.value()) {}

  static DeoptimizationLiteral HoleNaN() {
    DeoptimizationLiteral literal;
    literal.kind_ = DeoptimizationLiteralKind::kHoleNaN;
    return literal;
  }

  Handle<Object> object() const { return object_; }

  bool operator==(const DeoptimizationLiteral& other) const {
    if (kind_ != other.kind_) {
      return false;
    }
    switch (kind_) {
      case DeoptimizationLiteralKind::kObject:
        return object_.equals(other.object_);
      case DeoptimizationLiteralKind::kNumber:
        return base::bit_cast<uint64_t>(number_) ==
               base::bit_cast<uint64_t>(other.number_);
      case DeoptimizationLiteralKind::kWasmI31Ref:
      case DeoptimizationLiteralKind::kWasmInt32:
      case DeoptimizationLiteralKind::kSignedBigInt64:
        return int64_ == other.int64_;
      case DeoptimizationLiteralKind::kUnsignedBigInt64:
        return uint64_ == other.uint64_;
      case DeoptimizationLiteralKind::kHoleNaN:
        return other.kind() == DeoptimizationLiteralKind::kHoleNaN;
      case DeoptimizationLiteralKind::kInvalid:
        return true;
      case DeoptimizationLiteralKind::kWasmFloat32:
        return float32_.get_bits() == other.float32_.get_bits();
      case DeoptimizationLiteralKind::kWasmFloat64:
        return float64_.get_bits() == other.float64_.get_bits();
    }
    UNREACHABLE();
  }

  Handle<Object> Reify(Isolate* isolate) const;

#if V8_ENABLE_WEBASSEMBLY
  Float64 GetFloat64() const {
    DCHECK_EQ(kind_, DeoptimizationLiteralKind::kWasmFloat64);
    return float64_;
  }

  Float32 GetFloat32() const {
    DCHECK_EQ(kind_, DeoptimizationLiteralKind::kWasmFloat32);
    return float32_;
  }

  int64_t GetInt64() const {
    DCHECK_EQ(kind_, DeoptimizationLiteralKind::kWasmInt64);
    return int64_;
  }

  int32_t GetInt32() const {
    DCHECK_EQ(kind_, DeoptimizationLiteralKind::kWasmInt32);
    return static_cast<int32_t>(int64_);
  }

  Tagged<Smi> GetSmi() const {
    DCHECK_EQ(kind_, DeoptimizationLiteralKind::kWasmI31Ref);
    return Smi::FromInt(static_cast<int>(int64_));
  }
#endif

  void Validate() const {
    CHECK_NE(kind_, DeoptimizationLiteralKind::kInvalid);
  }

  DeoptimizationLiteralKind kind() const {
    Validate();
    return kind_;
  }

 private:
  DeoptimizationLiteralKind kind_;

  union {
    Handle<Object> object_;
    double number_;
    Float32 float32_;
    Float64 float64_;
    int64_t int64_;
    uint64_t uint64_;
  };
};

// The DeoptimizationFrameTranslation is the on-heap representation of
// translations created during code generation in a (zone-allocated)
// DeoptimizationFrameTranslationBuilder. The translation specifies how to
// transform an optimized frame back into one or more unoptimized frames.
enum class TranslationOpcode;
class DeoptimizationFrameTranslation : public TrustedByteArray {
 public:
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

  inline uint32_t get_int(int offset) const;
  inline void set_int(int offset, uint32_t value);

#ifdef ENABLE_DISASSEMBLER
  void PrintFrameTranslation(
      std::ostream& os, int index,
      Tagged<DeoptimizationLiteralArray> literal_array) const;
#endif

  OBJECT_CONSTRUCTORS(DeoptimizationFrameTranslation, TrustedByteArray);
};

class DeoptTranslationIterator {
 public:
  DeoptTranslationIterator(base::Vector<const uint8_t> buffer, int index);

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
  const base::Vector<const uint8_t> buffer_;
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

// Iterator over the deoptimization values. The iterator is not GC-safe.
class DeoptimizationFrameTranslation::Iterator
    : public DeoptTranslationIterator {
 public:
  Iterator(Tagged<DeoptimizationFrameTranslation> buffer, int index);
  DisallowGarbageCollection no_gc_;
};

// DeoptimizationData is a fixed array used to hold the deoptimization data for
// optimized code.  It also contains information about functions that were
// inlined.  If N different functions were inlined then the first N elements of
// the literal array will contain these functions.
//
// It can be empty.
class DeoptimizationData : public ProtectedFixedArray {
 public:
  // Layout description.  Indices in the array.
  static const int kFrameTranslationIndex = 0;
  static const int kInlinedFunctionCountIndex = 1;
  static const int kLiteralArrayIndex = 2;
  static const int kOsrBytecodeOffsetIndex = 3;
  static const int kOsrPcOffsetIndex = 4;
  static const int kOptimizationIdIndex = 5;
  static const int kSharedFunctionInfoWrapperIndex = 6;
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
  DECL_ELEMENT_ACCESSORS(SharedFunctionInfoWrapper, Tagged<Object>)
  DECL_ELEMENT_ACCESSORS(InliningPositions,
                         Tagged<TrustedPodArray<InliningPosition>>)
  DECL_ELEMENT_ACCESSORS(DeoptExitStart, Tagged<Smi>)
  DECL_ELEMENT_ACCESSORS(EagerDeoptCount, Tagged<Smi>)
  DECL_ELEMENT_ACCESSORS(LazyDeoptCount, Tagged<Smi>)

#undef DECL_ELEMENT_ACCESSORS

  inline Tagged<Object> SharedFunctionInfo() const;

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

  OBJECT_CONSTRUCTORS(DeoptimizationData, ProtectedFixedArray);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DEOPTIMIZATION_DATA_H_
