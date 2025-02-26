// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/deoptimization-data.h"

#include <iomanip>

#include "src/deoptimizer/translated-state.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects/casting.h"
#include "src/objects/code.h"
#include "src/objects/deoptimization-data-inl.h"
#include "src/objects/shared-function-info.h"

#ifdef V8_USE_ZLIB
#include "third_party/zlib/google/compression_utils_portable.h"
#endif  // V8_USE_ZLIB

namespace v8 {
namespace internal {

DirectHandle<Object> DeoptimizationLiteral::Reify(Isolate* isolate) const {
  Validate();
  switch (kind_) {
    case DeoptimizationLiteralKind::kObject: {
      return object_;
    }
    case DeoptimizationLiteralKind::kNumber: {
      return isolate->factory()->NewNumber(number_);
    }
    case DeoptimizationLiteralKind::kSignedBigInt64: {
      return BigInt::FromInt64(isolate, int64_);
    }
    case DeoptimizationLiteralKind::kUnsignedBigInt64: {
      return BigInt::FromUint64(isolate, uint64_);
    }
    case DeoptimizationLiteralKind::kHoleNaN: {
      // Hole NaNs that made it to here represent the undefined value.
      return isolate->factory()->undefined_value();
    }
    case DeoptimizationLiteralKind::kWasmI31Ref:
    case DeoptimizationLiteralKind::kWasmInt32:
    case DeoptimizationLiteralKind::kWasmFloat32:
    case DeoptimizationLiteralKind::kWasmFloat64:
    case DeoptimizationLiteralKind::kInvalid: {
      UNREACHABLE();
    }
  }
  UNREACHABLE();
}

Handle<DeoptimizationData> DeoptimizationData::New(Isolate* isolate,
                                                   int deopt_entry_count) {
  return Cast<DeoptimizationData>(
      isolate->factory()->NewProtectedFixedArray(LengthFor(deopt_entry_count)));
}

Handle<DeoptimizationData> DeoptimizationData::New(LocalIsolate* isolate,
                                                   int deopt_entry_count) {
  return Cast<DeoptimizationData>(
      isolate->factory()->NewProtectedFixedArray(LengthFor(deopt_entry_count)));
}

Handle<DeoptimizationData> DeoptimizationData::Empty(Isolate* isolate) {
  return Cast<DeoptimizationData>(
      isolate->factory()->empty_protected_fixed_array());
}

Handle<DeoptimizationData> DeoptimizationData::Empty(LocalIsolate* isolate) {
  return Cast<DeoptimizationData>(
      isolate->factory()->empty_protected_fixed_array());
}

Tagged<SharedFunctionInfo> DeoptimizationData::GetInlinedFunction(int index) {
  if (index == -1) {
    return GetSharedFunctionInfo();
  } else {
    return Cast<i::SharedFunctionInfo>(LiteralArray()->get(index));
  }
}

#ifdef DEBUG
void DeoptimizationData::Verify(Handle<BytecodeArray> bytecode) const {
#ifdef V8_USE_ZLIB
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    return;
  }
#endif  // V8_USE_ZLIB
  for (int i = 0; i < DeoptCount(); ++i) {
    // Check the frame count and identify the bailout id of the top compilation
    // unit.
    int idx = TranslationIndex(i).value();
    DeoptimizationFrameTranslation::Iterator iterator(FrameTranslation(), idx);
    auto [frame_count, jsframe_count] = iterator.EnterBeginOpcode();
    DCHECK_GE(frame_count, jsframe_count);
    BytecodeOffset bailout = BytecodeOffset::None();
    bool first_frame = true;
    while (frame_count > 0) {
      TranslationOpcode frame = iterator.SeekNextFrame();
      frame_count--;
      if (IsTranslationJsFrameOpcode(frame)) {
        jsframe_count--;
        if (first_frame) {
          bailout = BytecodeOffset(iterator.NextOperand());
          first_frame = false;
          iterator.SkipOperands(TranslationOpcodeOperandCount(frame) - 1);
          continue;
        }
      }
      iterator.SkipOperands(TranslationOpcodeOperandCount(frame));
    }
    CHECK_EQ(frame_count, 0);
    CHECK_EQ(jsframe_count, 0);

    // Check the bytecode offset exists in the bytecode array
    if (bailout != BytecodeOffset::None()) {
#ifdef ENABLE_SLOW_DCHECKS
      interpreter::BytecodeArrayIterator bytecode_iterator(bytecode);
      while (bytecode_iterator.current_offset() < bailout.ToInt()) {
        bytecode_iterator.Advance();
        DCHECK_LE(bytecode_iterator.current_offset(), bailout.ToInt());
      }
#else
      DCHECK_GE(bailout.ToInt(), 0);
      DCHECK_LT(bailout.ToInt(), bytecode->length());
#endif  // ENABLE_SLOW_DCHECKS
    }
  }
}
#endif  // DEBUG

#ifdef ENABLE_DISASSEMBLER

namespace {
void print_pc(std::ostream& os, int pc) {
  if (pc == -1) {
    os << "NA";
  } else {
    os << std::hex << pc << std::dec;
  }
}
}  // namespace

void DeoptimizationData::PrintDeoptimizationData(std::ostream& os) const {
  if (length() == 0) {
    os << "Deoptimization Input Data invalidated by lazy deoptimization\n";
    return;
  }

  int const inlined_function_count = InlinedFunctionCount().value();
  os << "Inlined functions (count = " << inlined_function_count << ")\n";
  for (int id = 0; id < inlined_function_count; ++id) {
    Tagged<Object> info = LiteralArray()->get(id);
    os << " " << Brief(Cast<i::SharedFunctionInfo>(info)) << "\n";
  }
  os << "\n";
  int deopt_count = DeoptCount();
  os << "Deoptimization Input Data (deopt points = " << deopt_count << ")\n";
  if (0 != deopt_count) {
#ifdef DEBUG
    os << " index  bytecode-offset  node-id    pc";
#else   // DEBUG
    os << " index  bytecode-offset    pc";
#endif  // DEBUG
    if (v8_flags.print_code_verbose) os << "  commands";
    os << "\n";
  }
  for (int i = 0; i < deopt_count; i++) {
    os << std::setw(6) << i << "  " << std::setw(15)
       << GetBytecodeOffsetOrBuiltinContinuationId(i).ToInt() << "  "
#ifdef DEBUG
       << std::setw(7) << NodeId(i).value() << "  "
#endif  // DEBUG
       << std::setw(4);
    print_pc(os, Pc(i).value());
    os << std::setw(2) << "\n";

    if (v8_flags.print_code_verbose) {
      FrameTranslation()->PrintFrameTranslation(os, TranslationIndex(i).value(),
                                                ProtectedLiteralArray(),
                                                LiteralArray());
    }
  }
}

#endif  // ENABLE_DISASSEMBLER

DeoptTranslationIterator::DeoptTranslationIterator(
    base::Vector<const uint8_t> buffer, int index)
    : buffer_(buffer), index_(index) {
#ifdef V8_USE_ZLIB
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    const int size =
        base::ReadUnalignedValue<uint32_t>(reinterpret_cast<Address>(
            &buffer_[DeoptimizationFrameTranslation::kUncompressedSizeOffset]));
    uncompressed_contents_.insert(uncompressed_contents_.begin(), size, 0);

    uLongf uncompressed_size = size *
                               DeoptimizationFrameTranslation::
                                   kDeoptimizationFrameTranslationElementSize;

    CHECK_EQ(zlib_internal::UncompressHelper(
                 zlib_internal::ZRAW,
                 reinterpret_cast<Bytef*>(uncompressed_contents_.data()),
                 &uncompressed_size,
                 buffer_.begin() +
                     DeoptimizationFrameTranslation::kCompressedDataOffset,
                 buffer_.length()),
             Z_OK);
    DCHECK(index >= 0 && index < size);
    return;
  }
#endif  // V8_USE_ZLIB
  DCHECK(!v8_flags.turbo_compress_frame_translations);
  DCHECK(index >= 0 && index < buffer_.length());
  // Starting at a location other than a BEGIN would make
  // MATCH_PREVIOUS_TRANSLATION instructions not work.
  DCHECK(
      TranslationOpcodeIsBegin(static_cast<TranslationOpcode>(buffer_[index])));
}

DeoptimizationFrameTranslation::Iterator::Iterator(
    Tagged<DeoptimizationFrameTranslation> buffer, int index)
    : DeoptTranslationIterator(
          base::Vector<uint8_t>(buffer->begin(), buffer->length()), index) {}

int32_t DeoptTranslationIterator::NextOperand() {
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    return uncompressed_contents_[index_++];
  } else if (remaining_ops_to_use_from_previous_translation_) {
    int32_t value = base::VLQDecode(buffer_.begin(), &previous_index_);
    DCHECK_LT(previous_index_, index_);
    return value;
  } else {
    int32_t value = base::VLQDecode(buffer_.begin(), &index_);
    DCHECK_LE(index_, buffer_.length());
    return value;
  }
}

TranslationOpcode DeoptTranslationIterator::NextOpcodeAtPreviousIndex() {
  TranslationOpcode opcode =
      static_cast<TranslationOpcode>(buffer_[previous_index_++]);
  DCHECK_LT(static_cast<uint32_t>(opcode), kNumTranslationOpcodes);
  DCHECK_NE(opcode, TranslationOpcode::MATCH_PREVIOUS_TRANSLATION);
  DCHECK_LT(previous_index_, index_);
  return opcode;
}

uint32_t DeoptTranslationIterator::NextUnsignedOperandAtPreviousIndex() {
  uint32_t value = base::VLQDecodeUnsigned(buffer_.begin(), &previous_index_);
  DCHECK_LT(previous_index_, index_);
  return value;
}

uint32_t DeoptTranslationIterator::NextOperandUnsigned() {
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    return uncompressed_contents_[index_++];
  } else if (remaining_ops_to_use_from_previous_translation_) {
    return NextUnsignedOperandAtPreviousIndex();
  } else {
    uint32_t value = base::VLQDecodeUnsigned(buffer_.begin(), &index_);
    DCHECK_LE(index_, buffer_.length());
    return value;
  }
}

TranslationOpcode DeoptTranslationIterator::NextOpcode() {
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    return static_cast<TranslationOpcode>(NextOperandUnsigned());
  }
  if (remaining_ops_to_use_from_previous_translation_) {
    --remaining_ops_to_use_from_previous_translation_;
  }
  if (remaining_ops_to_use_from_previous_translation_) {
    return NextOpcodeAtPreviousIndex();
  }
  CHECK_LT(index_, buffer_.length());
  uint8_t opcode_byte = buffer_[index_++];

  // If the opcode byte is greater than any valid opcode, then the opcode is
  // implicitly MATCH_PREVIOUS_TRANSLATION and the operand is the opcode byte
  // minus kNumTranslationOpcodes. This special-case encoding of the most common
  // opcode saves some memory.
  if (opcode_byte >= kNumTranslationOpcodes) {
    remaining_ops_to_use_from_previous_translation_ =
        opcode_byte - kNumTranslationOpcodes;
    opcode_byte =
        static_cast<uint8_t>(TranslationOpcode::MATCH_PREVIOUS_TRANSLATION);
  } else if (opcode_byte ==
             static_cast<uint8_t>(
                 TranslationOpcode::MATCH_PREVIOUS_TRANSLATION)) {
    remaining_ops_to_use_from_previous_translation_ = NextOperandUnsigned();
  }

  TranslationOpcode opcode = static_cast<TranslationOpcode>(opcode_byte);
  DCHECK_LE(index_, buffer_.length());
  DCHECK_LT(static_cast<uint32_t>(opcode), kNumTranslationOpcodes);
  if (TranslationOpcodeIsBegin(opcode)) {
    int temp_index = index_;
    // The first argument for BEGIN is the distance, in bytes, since the
    // previous BEGIN, or zero to indicate that MATCH_PREVIOUS_TRANSLATION will
    // not be used in this translation.
    uint32_t lookback_distance =
        base::VLQDecodeUnsigned(buffer_.begin(), &temp_index);
    if (lookback_distance) {
      previous_index_ = index_ - 1 - lookback_distance;
      DCHECK(TranslationOpcodeIsBegin(
          static_cast<TranslationOpcode>(buffer_[previous_index_])));
      // The previous BEGIN should specify zero as its lookback distance,
      // meaning it won't use MATCH_PREVIOUS_TRANSLATION.
      DCHECK_EQ(buffer_[previous_index_ + 1], 0);
    }
    ops_since_previous_index_was_updated_ = 1;
  } else if (opcode == TranslationOpcode::MATCH_PREVIOUS_TRANSLATION) {
    for (int i = 0; i < ops_since_previous_index_was_updated_; ++i) {
      SkipOpcodeAndItsOperandsAtPreviousIndex();
    }
    ops_since_previous_index_was_updated_ = 0;
    opcode = NextOpcodeAtPreviousIndex();
  } else {
    ++ops_since_previous_index_was_updated_;
  }
  return opcode;
}

DeoptimizationFrameTranslation::FrameCount
DeoptTranslationIterator::EnterBeginOpcode() {
  TranslationOpcode opcode = NextOpcode();
  DCHECK(TranslationOpcodeIsBegin(opcode));
  USE(opcode);
  NextOperand();  // Skip lookback distance.
  int frame_count = NextOperand();
  int jsframe_count = NextOperand();
  return {frame_count, jsframe_count};
}

TranslationOpcode DeoptTranslationIterator::SeekNextJSFrame() {
  while (HasNextOpcode()) {
    TranslationOpcode opcode = NextOpcode();
    DCHECK(!TranslationOpcodeIsBegin(opcode));
    if (IsTranslationJsFrameOpcode(opcode)) {
      return opcode;
    } else {
      // Skip over operands to advance to the next opcode.
      SkipOperands(TranslationOpcodeOperandCount(opcode));
    }
  }
  UNREACHABLE();
}

TranslationOpcode DeoptTranslationIterator::SeekNextFrame() {
  while (HasNextOpcode()) {
    TranslationOpcode opcode = NextOpcode();
    DCHECK(!TranslationOpcodeIsBegin(opcode));
    if (IsTranslationFrameOpcode(opcode)) {
      return opcode;
    } else {
      // Skip over operands to advance to the next opcode.
      SkipOperands(TranslationOpcodeOperandCount(opcode));
    }
  }
  UNREACHABLE();
}

bool DeoptTranslationIterator::HasNextOpcode() const {
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    return index_ < static_cast<int>(uncompressed_contents_.size());
  } else {
    return index_ < buffer_.length() ||
           remaining_ops_to_use_from_previous_translation_ > 1;
  }
}

void DeoptTranslationIterator::SkipOpcodeAndItsOperandsAtPreviousIndex() {
  TranslationOpcode opcode = NextOpcodeAtPreviousIndex();
  for (int count = TranslationOpcodeOperandCount(opcode); count != 0; --count) {
    NextUnsignedOperandAtPreviousIndex();
  }
}

#ifdef ENABLE_DISASSEMBLER

void DeoptimizationFrameTranslation::PrintFrameTranslation(
    std::ostream& os, int index,
    Tagged<ProtectedDeoptimizationLiteralArray> protected_literal_array,
    Tagged<DeoptimizationLiteralArray> literal_array) const {
  DisallowGarbageCollection gc_oh_noes;

  DeoptimizationFrameTranslation::Iterator iterator(this, index);
  TranslationOpcode first_opcode = iterator.NextOpcode();
  DCHECK(TranslationOpcodeIsBegin(first_opcode));
  os << first_opcode << " ";
  DeoptimizationFrameTranslationPrintSingleOpcode(
      os, first_opcode, iterator, protected_literal_array, literal_array);
  while (iterator.HasNextOpcode()) {
    TranslationOpcode opcode = iterator.NextOpcode();
    if (TranslationOpcodeIsBegin(opcode)) {
      break;
    }
    os << opcode << " ";
    DeoptimizationFrameTranslationPrintSingleOpcode(
        os, opcode, iterator, protected_literal_array, literal_array);
  }
}

#endif  // ENABLE_DISASSEMBLER

}  // namespace internal
}  // namespace v8
