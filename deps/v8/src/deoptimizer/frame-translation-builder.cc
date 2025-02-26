// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/frame-translation-builder.h"

#include <optional>

#include "src/base/vlq.h"
#include "src/deoptimizer/translated-state.h"
#include "src/objects/fixed-array-inl.h"

#ifdef V8_USE_ZLIB
#include "third_party/zlib/google/compression_utils_portable.h"
#endif  // V8_USE_ZLIB

namespace v8 {
namespace internal {

namespace {

class OperandBase {
 public:
  explicit OperandBase(uint32_t value) : value_(value) {}
  uint32_t value() const { return value_; }

 private:
  uint32_t value_;
};

class SmallUnsignedOperand : public OperandBase {
 public:
  explicit SmallUnsignedOperand(uint32_t value) : OperandBase(value) {
    DCHECK_LE(value, base::kDataMask);
  }
  void WriteVLQ(ZoneVector<uint8_t>* buffer) { buffer->push_back(value()); }
  bool IsSigned() const { return false; }
};

class UnsignedOperand : public OperandBase {
 public:
  explicit UnsignedOperand(int32_t value)
      : UnsignedOperand(static_cast<uint32_t>(value)) {
    DCHECK_GE(value, 0);
  }
  explicit UnsignedOperand(uint32_t value) : OperandBase(value) {}
  void WriteVLQ(ZoneVector<uint8_t>* buffer) {
    base::VLQEncodeUnsigned(
        [buffer](uint8_t value) { buffer->push_back(value); }, value());
  }
  bool IsSigned() const { return false; }
};

class SignedOperand : public OperandBase {
 public:
  explicit SignedOperand(int32_t value) : OperandBase(value) {}
  // Use UnsignedOperand for unsigned values.
  explicit SignedOperand(uint32_t value) = delete;
  void WriteVLQ(ZoneVector<uint8_t>* buffer) {
    base::VLQEncode(
        [buffer](uint8_t value) {
          buffer->push_back(value);
          return &buffer->back();
        },
        value());
  }
  bool IsSigned() const { return true; }
};

template <typename... T>
inline bool OperandsEqual(uint32_t* expected_operands, T... operands) {
  return (... && (*(expected_operands++) == operands.value()));
}

}  // namespace

template <typename... T>
void FrameTranslationBuilder::AddRawToContents(TranslationOpcode opcode,
                                               T... operands) {
  DCHECK_EQ(sizeof...(T), TranslationOpcodeOperandCount(opcode));
  DCHECK(!v8_flags.turbo_compress_frame_translations);
  contents_.push_back(static_cast<uint8_t>(opcode));
  (..., operands.WriteVLQ(&contents_));
}

template <typename... T>
void FrameTranslationBuilder::AddRawToContentsForCompression(
    TranslationOpcode opcode, T... operands) {
  DCHECK_EQ(sizeof...(T), TranslationOpcodeOperandCount(opcode));
  DCHECK(v8_flags.turbo_compress_frame_translations);
  contents_for_compression_.push_back(static_cast<uint8_t>(opcode));
  (..., contents_for_compression_.push_back(operands.value()));
}

template <typename... T>
void FrameTranslationBuilder::AddRawBegin(bool update_feedback, T... operands) {
  auto opcode = update_feedback ? TranslationOpcode::BEGIN_WITH_FEEDBACK
                                : TranslationOpcode::BEGIN_WITHOUT_FEEDBACK;
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    AddRawToContentsForCompression(opcode, operands...);
  } else {
    AddRawToContents(opcode, operands...);
#ifdef ENABLE_SLOW_DCHECKS
    if (v8_flags.enable_slow_asserts) {
      all_instructions_.emplace_back(opcode, operands...);
    }
#endif
  }
}

int FrameTranslationBuilder::BeginTranslation(int frame_count,
                                              int jsframe_count,
                                              bool update_feedback) {
  FinishPendingInstructionIfNeeded();
  int start_index = Size();
  int distance_from_last_start = 0;

  // We should reuse an existing basis translation if:
  // - we just finished writing the basis translation
  //   (match_previous_allowed_ is false), or
  // - the translation we just finished was moderately successful at reusing
  //   instructions from the basis translation. We'll define "moderately
  //   successful" as reusing more than 3/4 of the basis instructions.
  // Otherwise we should reset and write a new basis translation. At the
  // beginning, match_previous_allowed_ is initialized to true so that this
  // logic decides to start a new basis translation.
  if (!match_previous_allowed_ ||
      total_matching_instructions_in_current_translation_ >
          instruction_index_within_translation_ / 4 * 3) {
    // Use the existing basis translation.
    distance_from_last_start = start_index - index_of_basis_translation_start_;
    match_previous_allowed_ = true;
  } else {
    // Abandon the existing basis translation and write a new one.
    basis_instructions_.clear();
    index_of_basis_translation_start_ = start_index;
    match_previous_allowed_ = false;
  }

  total_matching_instructions_in_current_translation_ = 0;
  instruction_index_within_translation_ = 0;

  // BEGIN instructions can't be replaced by MATCH_PREVIOUS_TRANSLATION, so
  // use a special helper function rather than calling Add().
  AddRawBegin(update_feedback, UnsignedOperand(distance_from_last_start),
              SignedOperand(frame_count), SignedOperand(jsframe_count));
  return start_index;
}

void FrameTranslationBuilder::FinishPendingInstructionIfNeeded() {
  if (matching_instructions_count_) {
    total_matching_instructions_in_current_translation_ +=
        matching_instructions_count_;

    // There is a short form for the MATCH_PREVIOUS_TRANSLATION instruction
    // because it's the most common opcode: rather than spending a byte on the
    // opcode and a second byte on the operand, we can use only a single byte
    // which doesn't match any valid opcode.
    const int kMaxShortenableOperand =
        std::numeric_limits<uint8_t>::max() - kNumTranslationOpcodes;
    if (matching_instructions_count_ <= kMaxShortenableOperand) {
      contents_.push_back(kNumTranslationOpcodes +
                          matching_instructions_count_);
    } else {
      // The operand didn't fit in the opcode byte, so encode it normally.
      AddRawToContents(
          TranslationOpcode::MATCH_PREVIOUS_TRANSLATION,
          UnsignedOperand(static_cast<uint32_t>(matching_instructions_count_)));
    }
    matching_instructions_count_ = 0;
  }
}

template <typename... T>
void FrameTranslationBuilder::Add(TranslationOpcode opcode, T... operands) {
  DCHECK_EQ(sizeof...(T), TranslationOpcodeOperandCount(opcode));
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    AddRawToContentsForCompression(opcode, operands...);
    return;
  }
#ifdef ENABLE_SLOW_DCHECKS
  if (v8_flags.enable_slow_asserts) {
    all_instructions_.emplace_back(opcode, operands...);
  }
#endif
  if (match_previous_allowed_ &&
      instruction_index_within_translation_ < basis_instructions_.size() &&
      opcode ==
          basis_instructions_[instruction_index_within_translation_].opcode &&
      OperandsEqual(
          basis_instructions_[instruction_index_within_translation_].operands,
          operands...)) {
    ++matching_instructions_count_;
  } else {
    FinishPendingInstructionIfNeeded();
    AddRawToContents(opcode, operands...);
    if (!match_previous_allowed_) {
      // Include this instruction in basis_instructions_ so that future
      // translations can check whether they match with it.
      DCHECK_EQ(basis_instructions_.size(),
                instruction_index_within_translation_);
      basis_instructions_.emplace_back(opcode, operands...);
    }
  }
  ++instruction_index_within_translation_;
}

DirectHandle<DeoptimizationFrameTranslation>
FrameTranslationBuilder::ToFrameTranslation(LocalFactory* factory) {
#ifdef V8_USE_ZLIB
  if (V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)) {
    const int input_size = SizeInBytes();
    uLongf compressed_data_size = compressBound(input_size);

    ZoneVector<uint8_t> compressed_data(compressed_data_size, zone());

    CHECK_EQ(
        zlib_internal::CompressHelper(
            zlib_internal::ZRAW, compressed_data.data(), &compressed_data_size,
            reinterpret_cast<const Bytef*>(contents_for_compression_.data()),
            input_size, Z_DEFAULT_COMPRESSION, nullptr, nullptr),
        Z_OK);

    const int translation_array_size =
        static_cast<int>(compressed_data_size) +
        DeoptimizationFrameTranslation::kUncompressedSizeSize;
    DirectHandle<DeoptimizationFrameTranslation> result =
        factory->NewDeoptimizationFrameTranslation(translation_array_size);

    result->set_int(DeoptimizationFrameTranslation::kUncompressedSizeOffset,
                    Size());
    std::memcpy(
        result->begin() + DeoptimizationFrameTranslation::kCompressedDataOffset,
        compressed_data.data(), compressed_data_size);

    return result;
  }
#endif
  DCHECK(!v8_flags.turbo_compress_frame_translations);
  FinishPendingInstructionIfNeeded();
  DirectHandle<DeoptimizationFrameTranslation> result =
      factory->NewDeoptimizationFrameTranslation(SizeInBytes());
  if (SizeInBytes() == 0) return result;
  memcpy(result->begin(), contents_.data(), contents_.size() * sizeof(uint8_t));
#ifdef ENABLE_SLOW_DCHECKS
  DeoptimizationFrameTranslation::Iterator iter(*result, 0);
  ValidateBytes(iter);
#endif
  return result;
}

base::Vector<const uint8_t> FrameTranslationBuilder::ToFrameTranslationWasm() {
  DCHECK(!v8_flags.turbo_compress_frame_translations);
  FinishPendingInstructionIfNeeded();
  base::Vector<const uint8_t> result = base::VectorOf(contents_);
#ifdef ENABLE_SLOW_DCHECKS
  DeoptTranslationIterator iter(result, 0);
  ValidateBytes(iter);
#endif
  return result;
}

void FrameTranslationBuilder::ValidateBytes(
    DeoptTranslationIterator& iter) const {
#ifdef ENABLE_SLOW_DCHECKS
  if (v8_flags.enable_slow_asserts) {
    // Check that we can read back all of the same content we intended to write.
    for (size_t i = 0; i < all_instructions_.size(); ++i) {
      CHECK(iter.HasNextOpcode());
      const Instruction& instruction = all_instructions_[i];
      CHECK_EQ(instruction.opcode, iter.NextOpcode());
      for (int j = 0; j < TranslationOpcodeOperandCount(instruction.opcode);
           ++j) {
        uint32_t operand = instruction.is_operand_signed[j]
                               ? iter.NextOperand()
                               : iter.NextOperandUnsigned();
        CHECK_EQ(instruction.operands[j], operand);
      }
    }
  }
#endif
}

void FrameTranslationBuilder::BeginBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::BUILTIN_CONTINUATION_FRAME;
  Add(opcode, SignedOperand(bytecode_offset.ToInt()), SignedOperand(literal_id),
      UnsignedOperand(height));
}

#if V8_ENABLE_WEBASSEMBLY
void FrameTranslationBuilder::BeginJSToWasmBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height,
    std::optional<wasm::ValueKind> return_kind) {
  auto opcode = TranslationOpcode::JS_TO_WASM_BUILTIN_CONTINUATION_FRAME;
  Add(opcode, SignedOperand(bytecode_offset.ToInt()), SignedOperand(literal_id),
      UnsignedOperand(height),
      SignedOperand(return_kind ? static_cast<int>(return_kind.value())
                                : kNoWasmReturnKind));
}

void FrameTranslationBuilder::BeginWasmInlinedIntoJSFrame(
    BytecodeOffset bailout_id, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::WASM_INLINED_INTO_JS_FRAME;
  Add(opcode, SignedOperand(bailout_id.ToInt()), SignedOperand(literal_id),
      UnsignedOperand(height));
}

void FrameTranslationBuilder::BeginLiftoffFrame(BytecodeOffset bailout_id,
                                                unsigned height,
                                                uint32_t wasm_function_index) {
  auto opcode = TranslationOpcode::LIFTOFF_FRAME;
  Add(opcode, SignedOperand(bailout_id.ToInt()), UnsignedOperand(height),
      UnsignedOperand(wasm_function_index));
}
#endif  // V8_ENABLE_WEBASSEMBLY

void FrameTranslationBuilder::BeginJavaScriptBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::JAVASCRIPT_BUILTIN_CONTINUATION_FRAME;
  Add(opcode, SignedOperand(bytecode_offset.ToInt()), SignedOperand(literal_id),
      UnsignedOperand(height));
}

void FrameTranslationBuilder::BeginJavaScriptBuiltinContinuationWithCatchFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode =
      TranslationOpcode::JAVASCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME;
  Add(opcode, SignedOperand(bytecode_offset.ToInt()), SignedOperand(literal_id),
      UnsignedOperand(height));
}

void FrameTranslationBuilder::BeginConstructCreateStubFrame(int literal_id,
                                                            unsigned height) {
  auto opcode = TranslationOpcode::CONSTRUCT_CREATE_STUB_FRAME;
  Add(opcode, SignedOperand(literal_id), UnsignedOperand(height));
}

void FrameTranslationBuilder::BeginConstructInvokeStubFrame(int literal_id) {
  auto opcode = TranslationOpcode::CONSTRUCT_INVOKE_STUB_FRAME;
  Add(opcode, SignedOperand(literal_id));
}

void FrameTranslationBuilder::BeginInlinedExtraArguments(
    int literal_id, unsigned height, uint32_t parameter_count) {
  auto opcode = TranslationOpcode::INLINED_EXTRA_ARGUMENTS;
  Add(opcode, SignedOperand(literal_id), UnsignedOperand(height),
      UnsignedOperand(parameter_count));
}

void FrameTranslationBuilder::BeginInterpretedFrame(
    BytecodeOffset bytecode_offset, int literal_id, int bytecode_array_id,
    unsigned height, int return_value_offset, int return_value_count) {
  if (return_value_count == 0) {
    auto opcode = TranslationOpcode::INTERPRETED_FRAME_WITHOUT_RETURN;
    Add(opcode, SignedOperand(bytecode_offset.ToInt()),
        SignedOperand(literal_id), SignedOperand(bytecode_array_id),
        UnsignedOperand(height));
  } else {
    auto opcode = TranslationOpcode::INTERPRETED_FRAME_WITH_RETURN;
    Add(opcode, SignedOperand(bytecode_offset.ToInt()),
        SignedOperand(literal_id), SignedOperand(bytecode_array_id),
        UnsignedOperand(height), SignedOperand(return_value_offset),
        SignedOperand(return_value_count));
  }
}

void FrameTranslationBuilder::ArgumentsElements(CreateArgumentsType type) {
  auto opcode = TranslationOpcode::ARGUMENTS_ELEMENTS;
  Add(opcode, SignedOperand(static_cast<uint8_t>(type)));
}

void FrameTranslationBuilder::ArgumentsLength() {
  auto opcode = TranslationOpcode::ARGUMENTS_LENGTH;
  Add(opcode);
}

void FrameTranslationBuilder::RestLength() {
  auto opcode = TranslationOpcode::REST_LENGTH;
  Add(opcode);
}

void FrameTranslationBuilder::BeginCapturedObject(int length) {
  auto opcode = TranslationOpcode::CAPTURED_OBJECT;
  Add(opcode, SignedOperand(length));
}

void FrameTranslationBuilder::DuplicateObject(int object_index) {
  auto opcode = TranslationOpcode::DUPLICATED_OBJECT;
  Add(opcode, SignedOperand(object_index));
}

void FrameTranslationBuilder::StringConcat() {
  auto opcode = TranslationOpcode::STRING_CONCAT;
  Add(opcode);
}

void FrameTranslationBuilder::StoreRegister(TranslationOpcode opcode,
                                            Register reg) {
  static_assert(Register::kNumRegisters - 1 <= base::kDataMask);
  Add(opcode, SmallUnsignedOperand(static_cast<uint8_t>(reg.code())));
}

void FrameTranslationBuilder::StoreRegister(Register reg) {
  auto opcode = TranslationOpcode::REGISTER;
  StoreRegister(opcode, reg);
}

void FrameTranslationBuilder::StoreInt32Register(Register reg) {
  auto opcode = TranslationOpcode::INT32_REGISTER;
  StoreRegister(opcode, reg);
}

void FrameTranslationBuilder::StoreIntPtrRegister(Register reg) {
  auto opcode = (kSystemPointerSize == 4) ? TranslationOpcode::INT32_REGISTER
                                          : TranslationOpcode::INT64_REGISTER;
  StoreRegister(opcode, reg);
}

void FrameTranslationBuilder::StoreInt64Register(Register reg) {
  auto opcode = TranslationOpcode::INT64_REGISTER;
  StoreRegister(opcode, reg);
}

void FrameTranslationBuilder::StoreSignedBigInt64Register(Register reg) {
  auto opcode = TranslationOpcode::SIGNED_BIGINT64_REGISTER;
  StoreRegister(opcode, reg);
}

void FrameTranslationBuilder::StoreUnsignedBigInt64Register(Register reg) {
  auto opcode = TranslationOpcode::UNSIGNED_BIGINT64_REGISTER;
  StoreRegister(opcode, reg);
}

void FrameTranslationBuilder::StoreUint32Register(Register reg) {
  auto opcode = TranslationOpcode::UINT32_REGISTER;
  StoreRegister(opcode, reg);
}

void FrameTranslationBuilder::StoreBoolRegister(Register reg) {
  auto opcode = TranslationOpcode::BOOL_REGISTER;
  StoreRegister(opcode, reg);
}

void FrameTranslationBuilder::StoreFloatRegister(FloatRegister reg) {
  static_assert(FloatRegister::kNumRegisters - 1 <= base::kDataMask);
  auto opcode = TranslationOpcode::FLOAT_REGISTER;
  Add(opcode, SmallUnsignedOperand(static_cast<uint8_t>(reg.code())));
}

void FrameTranslationBuilder::StoreDoubleRegister(DoubleRegister reg) {
  static_assert(DoubleRegister::kNumRegisters - 1 <= base::kDataMask);
  auto opcode = TranslationOpcode::DOUBLE_REGISTER;
  Add(opcode, SmallUnsignedOperand(static_cast<uint8_t>(reg.code())));
}

void FrameTranslationBuilder::StoreHoleyDoubleRegister(DoubleRegister reg) {
  static_assert(DoubleRegister::kNumRegisters - 1 <= base::kDataMask);
  auto opcode = TranslationOpcode::HOLEY_DOUBLE_REGISTER;
  Add(opcode, SmallUnsignedOperand(static_cast<uint8_t>(reg.code())));
}

void FrameTranslationBuilder::StoreSimd128Register(Simd128Register reg) {
  static_assert(DoubleRegister::kNumRegisters - 1 <= base::kDataMask);
  auto opcode = TranslationOpcode::SIMD128_REGISTER;
  Add(opcode, SmallUnsignedOperand(static_cast<uint8_t>(reg.code())));
}

void FrameTranslationBuilder::StoreStackSlot(int index) {
  auto opcode = TranslationOpcode::TAGGED_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreInt32StackSlot(int index) {
  auto opcode = TranslationOpcode::INT32_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreIntPtrStackSlot(int index) {
  auto opcode = (kSystemPointerSize == 4) ? TranslationOpcode::INT32_STACK_SLOT
                                          : TranslationOpcode::INT64_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreInt64StackSlot(int index) {
  auto opcode = TranslationOpcode::INT64_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreSignedBigInt64StackSlot(int index) {
  auto opcode = TranslationOpcode::SIGNED_BIGINT64_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreUnsignedBigInt64StackSlot(int index) {
  auto opcode = TranslationOpcode::UNSIGNED_BIGINT64_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreUint32StackSlot(int index) {
  auto opcode = TranslationOpcode::UINT32_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreBoolStackSlot(int index) {
  auto opcode = TranslationOpcode::BOOL_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreFloatStackSlot(int index) {
  auto opcode = TranslationOpcode::FLOAT_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreDoubleStackSlot(int index) {
  auto opcode = TranslationOpcode::DOUBLE_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreSimd128StackSlot(int index) {
  auto opcode = TranslationOpcode::SIMD128_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreHoleyDoubleStackSlot(int index) {
  auto opcode = TranslationOpcode::HOLEY_DOUBLE_STACK_SLOT;
  Add(opcode, SignedOperand(index));
}

void FrameTranslationBuilder::StoreLiteral(int literal_id) {
  auto opcode = TranslationOpcode::LITERAL;
  DCHECK_GE(literal_id, 0);
  Add(opcode, SignedOperand(literal_id));
}

void FrameTranslationBuilder::StoreOptimizedOut() {
  auto opcode = TranslationOpcode::OPTIMIZED_OUT;
  Add(opcode);
}

void FrameTranslationBuilder::AddUpdateFeedback(int vector_literal, int slot) {
  auto opcode = TranslationOpcode::UPDATE_FEEDBACK;
  Add(opcode, SignedOperand(vector_literal), SignedOperand(slot));
}

void FrameTranslationBuilder::StoreJSFrameFunction() {
  StoreStackSlot((StandardFrameConstants::kCallerPCOffset -
                  StandardFrameConstants::kFunctionOffset) /
                 kSystemPointerSize);
}

}  // namespace internal
}  // namespace v8
