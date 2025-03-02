// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEOPTIMIZER_FRAME_TRANSLATION_BUILDER_H_
#define V8_DEOPTIMIZER_FRAME_TRANSLATION_BUILDER_H_

#include <optional>

#include "src/codegen/register.h"
#include "src/deoptimizer/translation-opcode.h"
#include "src/objects/deoptimization-data.h"
#include "src/zone/zone-containers.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/value-type.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

class LocalFactory;

class FrameTranslationBuilder {
 public:
  explicit FrameTranslationBuilder(Zone* zone)
      : contents_(zone),
        contents_for_compression_(zone),
        basis_instructions_(zone),
        zone_(zone) {}

  DirectHandle<DeoptimizationFrameTranslation> ToFrameTranslation(
      LocalFactory* factory);
  base::Vector<const uint8_t> ToFrameTranslationWasm();

  int BeginTranslation(int frame_count, int jsframe_count,
                       bool update_feedback);

  void BeginInterpretedFrame(BytecodeOffset bytecode_offset, int literal_id,
                             int bytecode_array_id, unsigned height,
                             int return_value_offset, int return_value_count);
  void BeginInlinedExtraArguments(int literal_id, unsigned height,
                                  uint32_t parameter_count);
  void BeginConstructCreateStubFrame(int literal_id, unsigned height);
  void BeginConstructInvokeStubFrame(int literal_id);
  void BeginBuiltinContinuationFrame(BytecodeOffset bailout_id, int literal_id,
                                     unsigned height);
#if V8_ENABLE_WEBASSEMBLY
  void BeginJSToWasmBuiltinContinuationFrame(
      BytecodeOffset bailout_id, int literal_id, unsigned height,
      std::optional<wasm::ValueKind> return_kind);
  void BeginWasmInlinedIntoJSFrame(BytecodeOffset bailout_id, int literal_id,
                                   unsigned height);
  void BeginLiftoffFrame(BytecodeOffset bailout_id, unsigned height,
                         uint32_t wasm_function_index);
#endif  // V8_ENABLE_WEBASSEMBLY
  void BeginJavaScriptBuiltinContinuationFrame(BytecodeOffset bailout_id,
                                               int literal_id, unsigned height);
  void BeginJavaScriptBuiltinContinuationWithCatchFrame(
      BytecodeOffset bailout_id, int literal_id, unsigned height);
  void ArgumentsElements(CreateArgumentsType type);
  void ArgumentsLength();
  void RestLength();
  void BeginCapturedObject(int length);
  void AddUpdateFeedback(int vector_literal, int slot);
  void DuplicateObject(int object_index);
  void StringConcat();
  void StoreRegister(TranslationOpcode opcode, Register reg);
  void StoreRegister(Register reg);
  void StoreInt32Register(Register reg);
  void StoreInt64Register(Register reg);
  void StoreIntPtrRegister(Register reg);
  void StoreSignedBigInt64Register(Register reg);
  void StoreUnsignedBigInt64Register(Register reg);
  void StoreUint32Register(Register reg);
  void StoreBoolRegister(Register reg);
  void StoreFloatRegister(FloatRegister reg);
  void StoreDoubleRegister(DoubleRegister reg);
  void StoreHoleyDoubleRegister(DoubleRegister reg);
  void StoreSimd128Register(Simd128Register reg);
  void StoreStackSlot(int index);
  void StoreInt32StackSlot(int index);
  void StoreInt64StackSlot(int index);
  void StoreIntPtrStackSlot(int index);
  void StoreSignedBigInt64StackSlot(int index);
  void StoreUnsignedBigInt64StackSlot(int index);
  void StoreUint32StackSlot(int index);
  void StoreBoolStackSlot(int index);
  void StoreFloatStackSlot(int index);
  void StoreDoubleStackSlot(int index);
  void StoreSimd128StackSlot(int index);
  void StoreHoleyDoubleStackSlot(int index);
  void StoreLiteral(int literal_id);
  void StoreOptimizedOut();
  void StoreJSFrameFunction();

 private:
  struct Instruction {
    template <typename... T>
    explicit Instruction(TranslationOpcode opcode, T... operands)
        : opcode(opcode),
          operands{operands.value()...}
#ifdef ENABLE_SLOW_DCHECKS
          ,
          is_operand_signed{operands.IsSigned()...}
#endif
    {
    }
    TranslationOpcode opcode;
    // The operands for the instruction. Signed values were static_casted to
    // unsigned.
    uint32_t operands[kMaxTranslationOperandCount];
#ifdef ENABLE_SLOW_DCHECKS
    bool is_operand_signed[kMaxTranslationOperandCount];
#endif
  };

  // Either adds the instruction or increments matching_instructions_count_,
  // depending on whether the instruction matches the corresponding instruction
  // from the previous translation.
  template <typename... T>
  void Add(TranslationOpcode opcode, T... operands);

  // Adds the instruction to contents_, without performing the other steps of
  // Add(). Requires !v8_flags.turbo_compress_frame_translations.
  template <typename... T>
  void AddRawToContents(TranslationOpcode opcode, T... operands);

  // Adds the instruction to contents_for_compression_, without performing the
  // other steps of Add(). Requires v8_flags.turbo_compress_frame_translations.
  template <typename... T>
  void AddRawToContentsForCompression(TranslationOpcode opcode, T... operands);

  // Adds a BEGIN instruction to contents_ or contents_for_compression_, but
  // does not update other state. Used by BeginTranslation.
  template <typename... T>
  void AddRawBegin(bool update_feedback, T... operands);

  int Size() const {
    return V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)
               ? static_cast<int>(contents_for_compression_.size())
               : static_cast<int>(contents_.size());
  }
  int SizeInBytes() const {
    return V8_UNLIKELY(v8_flags.turbo_compress_frame_translations)
               ? Size() * kInt32Size
               : Size();
  }

  Zone* zone() const { return zone_; }

  void FinishPendingInstructionIfNeeded();
  void ValidateBytes(DeoptTranslationIterator& iter) const;

  ZoneVector<uint8_t> contents_;
  ZoneVector<int32_t> contents_for_compression_;
  // If match_previous_allowed_ is false, then this vector contains the
  // instructions written so far in the current translation (since the last
  // BEGIN). If match_previous_allowed_ is true, then this vector contains the
  // instructions from the basis translation (the one written with
  // !match_previous_allowed_). This allows Add() to easily check whether a
  // newly added instruction matches the corresponding one from the basis
  // translation.
  ZoneVector<Instruction> basis_instructions_;
#ifdef ENABLE_SLOW_DCHECKS
  std::vector<Instruction> all_instructions_;
#endif
  Zone* const zone_;
  // How many consecutive instructions we've skipped writing because they match
  // the basis translation.
  size_t matching_instructions_count_ = 0;
  size_t total_matching_instructions_in_current_translation_ = 0;
  // The current index within basis_instructions_.
  size_t instruction_index_within_translation_ = 0;
  // The byte index within the contents_ array of the BEGIN instruction for the
  // basis translation (the most recent translation which was fully written out,
  // not using MATCH_PREVIOUS_TRANSLATION instructions).
  int index_of_basis_translation_start_ = 0;
  // Whether the builder can use MATCH_PREVIOUS_TRANSLATION in the current
  // translation.
  bool match_previous_allowed_ = true;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEOPTIMIZER_FRAME_TRANSLATION_BUILDER_H_
