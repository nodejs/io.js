// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-string-gen.h"

#include "src/base/strings.h"
#include "src/builtins/builtins-regexp-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/execution/protectors.h"
#include "src/heap/factory-inl.h"
#include "src/heap/heap-inl.h"
#include "src/logging/counters.h"
#include "src/objects/instance-type.h"
#include "src/objects/objects.h"
#include "src/objects/property-cell.h"

namespace v8 {
namespace internal {

TNode<RawPtrT> StringBuiltinsAssembler::DirectStringData(
    TNode<String> string, TNode<Word32T> string_instance_type) {
  // Compute the effective offset of the first character.
  TVARIABLE(RawPtrT, var_data);
  Label if_sequential(this), if_external(this), if_join(this);
  Branch(Word32Equal(Word32And(string_instance_type,
                               Int32Constant(kStringRepresentationMask)),
                     Int32Constant(kSeqStringTag)),
         &if_sequential, &if_external);

  BIND(&if_sequential);
  {
    static_assert(OFFSET_OF_DATA_START(SeqOneByteString) ==
                  OFFSET_OF_DATA_START(SeqTwoByteString));
    var_data = RawPtrAdd(ReinterpretCast<RawPtrT>(BitcastTaggedToWord(string)),
                         IntPtrConstant(OFFSET_OF_DATA_START(SeqOneByteString) -
                                        kHeapObjectTag));
    Goto(&if_join);
  }

  BIND(&if_external);
  {
    var_data = LoadExternalStringResourceDataPtr(CAST(string));
    Goto(&if_join);
  }

  BIND(&if_join);
  return var_data.value();
}

template <typename SubjectChar, typename PatternChar>
TNode<IntPtrT> StringBuiltinsAssembler::CallSearchStringRaw(
    const TNode<RawPtrT> subject_ptr, const TNode<IntPtrT> subject_length,
    const TNode<RawPtrT> search_ptr, const TNode<IntPtrT> search_length,
    const TNode<IntPtrT> start_position) {
  const TNode<ExternalReference> function_addr = ExternalConstant(
      ExternalReference::search_string_raw<SubjectChar, PatternChar>());
  const TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address(isolate()));

  MachineType type_ptr = MachineType::Pointer();
  MachineType type_intptr = MachineType::IntPtr();

  const TNode<IntPtrT> result = UncheckedCast<IntPtrT>(CallCFunction(
      function_addr, type_intptr, std::make_pair(type_ptr, isolate_ptr),
      std::make_pair(type_ptr, subject_ptr),
      std::make_pair(type_intptr, subject_length),
      std::make_pair(type_ptr, search_ptr),
      std::make_pair(type_intptr, search_length),
      std::make_pair(type_intptr, start_position)));

  return result;
}
TNode<IntPtrT> StringBuiltinsAssembler::SearchOneByteStringInTwoByteString(
    const TNode<RawPtrT> subject_ptr, const TNode<IntPtrT> subject_length,
    const TNode<RawPtrT> search_ptr, const TNode<IntPtrT> search_length,
    const TNode<IntPtrT> start_position) {
  return CallSearchStringRaw<const base::uc16, const uint8_t>(
      subject_ptr, subject_length, search_ptr, search_length, start_position);
}
TNode<IntPtrT> StringBuiltinsAssembler::SearchOneByteStringInOneByteString(
    const TNode<RawPtrT> subject_ptr, const TNode<IntPtrT> subject_length,
    const TNode<RawPtrT> search_ptr, const TNode<IntPtrT> search_length,
    const TNode<IntPtrT> start_position) {
  return CallSearchStringRaw<const uint8_t, const uint8_t>(
      subject_ptr, subject_length, search_ptr, search_length, start_position);
}
TNode<IntPtrT> StringBuiltinsAssembler::SearchTwoByteStringInTwoByteString(
    const TNode<RawPtrT> subject_ptr, const TNode<IntPtrT> subject_length,
    const TNode<RawPtrT> search_ptr, const TNode<IntPtrT> search_length,
    const TNode<IntPtrT> start_position) {
  return CallSearchStringRaw<const base::uc16, const base::uc16>(
      subject_ptr, subject_length, search_ptr, search_length, start_position);
}
TNode<IntPtrT> StringBuiltinsAssembler::SearchTwoByteStringInOneByteString(
    const TNode<RawPtrT> subject_ptr, const TNode<IntPtrT> subject_length,
    const TNode<RawPtrT> search_ptr, const TNode<IntPtrT> search_length,
    const TNode<IntPtrT> start_position) {
  return CallSearchStringRaw<const uint8_t, const base::uc16>(
      subject_ptr, subject_length, search_ptr, search_length, start_position);
}
TNode<IntPtrT> StringBuiltinsAssembler::SearchOneByteInOneByteString(
    const TNode<RawPtrT> subject_ptr, const TNode<IntPtrT> subject_length,
    const TNode<RawPtrT> search_ptr, const TNode<IntPtrT> start_position) {
  const TNode<RawPtrT> subject_start_ptr =
      RawPtrAdd(subject_ptr, start_position);
  const TNode<IntPtrT> search_byte =
      ChangeInt32ToIntPtr(Load<Uint8T>(search_ptr));
  const TNode<UintPtrT> search_length =
      Unsigned(IntPtrSub(subject_length, start_position));
  const TNode<ExternalReference> memchr =
      ExternalConstant(ExternalReference::libc_memchr_function());
  const TNode<RawPtrT> result_address = UncheckedCast<RawPtrT>(
      CallCFunction(memchr, MachineType::Pointer(),
                    std::make_pair(MachineType::Pointer(), subject_start_ptr),
                    std::make_pair(MachineType::IntPtr(), search_byte),
                    std::make_pair(MachineType::UintPtr(), search_length)));
  return Select<IntPtrT>(
      WordEqual(result_address, IntPtrConstant(0)),
      [=] { return IntPtrConstant(-1); },
      [=] {
        return IntPtrAdd(RawPtrSub(result_address, subject_start_ptr),
                         start_position);
      });
}

void StringBuiltinsAssembler::GenerateStringEqual(TNode<String> left,
                                                  TNode<String> right,
                                                  TNode<IntPtrT> length) {
  TVARIABLE(String, var_left, left);
  TVARIABLE(String, var_right, right);
  Label if_equal(this), if_notequal(this), if_indirect(this, Label::kDeferred),
      start(this, {&var_left, &var_right});

  // Callers must handle the case where {lhs} and {rhs} refer to the same
  // String object.
  CSA_DCHECK(this, TaggedNotEqual(left, right));

  CSA_DCHECK(this, IntPtrEqual(LoadStringLengthAsWord(left), length));
  CSA_DCHECK(this, IntPtrEqual(LoadStringLengthAsWord(right), length));

  Goto(&start);
  BIND(&start);
  TNode<String> lhs = var_left.value();
  TNode<String> rhs = var_right.value();

  TNode<Uint16T> lhs_instance_type = LoadInstanceType(lhs);
  TNode<Uint16T> rhs_instance_type = LoadInstanceType(rhs);

  StringEqual_Core(lhs, lhs_instance_type, rhs, rhs_instance_type, length,
                   &if_equal, &if_notequal, &if_indirect);

  BIND(&if_indirect);
  {
    Label restart(this, {&var_left, &var_right});
    // Try to unwrap indirect strings, restart the above attempt on success.
    MaybeDerefIndirectStrings(&var_left, lhs_instance_type, &var_right,
                              rhs_instance_type, &restart);

    TailCallRuntime(Runtime::kStringEqual, NoContextConstant(), lhs, rhs);

    BIND(&restart);
    GotoIf(TaggedEqual(var_left.value(), var_right.value()), &if_equal);
    Goto(&start);
  }

  BIND(&if_equal);
  Return(TrueConstant());

  BIND(&if_notequal);
  Return(FalseConstant());
}

void StringBuiltinsAssembler::StringEqual_Core(
    TNode<String> lhs, TNode<Word32T> lhs_instance_type, TNode<String> rhs,
    TNode<Word32T> rhs_instance_type, TNode<IntPtrT> length, Label* if_equal,
    Label* if_not_equal, Label* if_indirect) {
  CSA_DCHECK(this, WordEqual(LoadStringLengthAsWord(lhs), length));
  CSA_DCHECK(this, WordEqual(LoadStringLengthAsWord(rhs), length));

  // Callers must handle the case where {lhs} and {rhs} refer to the same
  // String object.
  CSA_DCHECK(this, TaggedNotEqual(lhs, rhs));

  // Combine the instance types into a single 16-bit value, so we can check
  // both of them at once.
  TNode<Word32T> both_instance_types = Word32Or(
      lhs_instance_type, Word32Shl(rhs_instance_type, Int32Constant(8)));

  // Check if both {lhs} and {rhs} are internalized. Since we already know
  // that they're not the same object, they're not equal in that case.
  int const kBothInternalizedMask =
      kIsNotInternalizedMask | (kIsNotInternalizedMask << 8);
  int const kBothInternalizedTag = kInternalizedTag | (kInternalizedTag << 8);
  GotoIf(Word32Equal(Word32And(both_instance_types,
                               Int32Constant(kBothInternalizedMask)),
                     Int32Constant(kBothInternalizedTag)),
         if_not_equal);

  // Check if both {lhs} and {rhs} are direct strings, and that in case of
  // ExternalStrings the data pointer is cached.
  static_assert(kUncachedExternalStringTag != 0);
  static_assert(kIsIndirectStringTag != 0);
  int const kBothDirectStringMask =
      kIsIndirectStringMask | kUncachedExternalStringMask |
      ((kIsIndirectStringMask | kUncachedExternalStringMask) << 8);
  GotoIfNot(Word32Equal(Word32And(both_instance_types,
                                  Int32Constant(kBothDirectStringMask)),
                        Int32Constant(0)),
            if_indirect);

  Label if_skip_fast_case(this), if_fast_case(this), if_oneonebytestring(this),
      if_twotwobytestring(this), if_onetwobytestring(this),
      if_twoonebytestring(this);

  // Dispatch based on the {lhs} and {rhs} string encoding.
  int const kBothStringEncodingMask =
      kStringEncodingMask | (kStringEncodingMask << 8);
  int const kBothExternalStringTag =
      kExternalStringTag | (kExternalStringTag << 8);
  int const kOneOneByteStringTag = kOneByteStringTag | (kOneByteStringTag << 8);
  int const kTwoTwoByteStringTag = kTwoByteStringTag | (kTwoByteStringTag << 8);
  int const kOneTwoByteStringTag = kOneByteStringTag | (kTwoByteStringTag << 8);

  TNode<Word32T> masked_instance_types =
      Word32And(both_instance_types, Int32Constant(kBothStringEncodingMask));
  TNode<Word32T> both_are_one_byte =
      Word32Equal(masked_instance_types, Int32Constant(kOneOneByteStringTag));
  TNode<Word32T> both_are_two_byte =
      Word32Equal(masked_instance_types, Int32Constant(kTwoTwoByteStringTag));

  // If both strings are not external we know that their payload length is
  // kTagged sized. When they have the same type we can compare in chunks. The
  // padding bytes are set to zero.
  GotoIf(Word32And(both_instance_types, Int32Constant(kBothExternalStringTag)),
         &if_skip_fast_case);
  TVARIABLE(IntPtrT, byte_length, length);
  GotoIf(both_are_one_byte, &if_fast_case);
  byte_length = WordShl(byte_length.value(), IntPtrConstant(1));
  Branch(both_are_two_byte, &if_fast_case, &if_skip_fast_case);
  BIND(&if_fast_case);
  StringEqual_FastLoop(lhs, lhs_instance_type, rhs, rhs_instance_type,
                       byte_length.value(), if_equal, if_not_equal);

  BIND(&if_skip_fast_case);
  GotoIf(both_are_one_byte, &if_oneonebytestring);
  GotoIf(both_are_two_byte, &if_twotwobytestring);
  Branch(
      Word32Equal(masked_instance_types, Int32Constant(kOneTwoByteStringTag)),
      &if_onetwobytestring, &if_twoonebytestring);

  BIND(&if_oneonebytestring);
  StringEqual_Loop(lhs, lhs_instance_type, MachineType::Uint8(), rhs,
                   rhs_instance_type, MachineType::Uint8(), length, if_equal,
                   if_not_equal);

  BIND(&if_twotwobytestring);
  StringEqual_Loop(lhs, lhs_instance_type, MachineType::Uint16(), rhs,
                   rhs_instance_type, MachineType::Uint16(), length, if_equal,
                   if_not_equal);

  BIND(&if_onetwobytestring);
  StringEqual_Loop(lhs, lhs_instance_type, MachineType::Uint8(), rhs,
                   rhs_instance_type, MachineType::Uint16(), length, if_equal,
                   if_not_equal);

  BIND(&if_twoonebytestring);
  StringEqual_Loop(lhs, lhs_instance_type, MachineType::Uint16(), rhs,
                   rhs_instance_type, MachineType::Uint8(), length, if_equal,
                   if_not_equal);
}

void StringBuiltinsAssembler::StringEqual_FastLoop(
    TNode<String> lhs, TNode<Word32T> lhs_instance_type, TNode<String> rhs,
    TNode<Word32T> rhs_instance_type, TNode<IntPtrT> byte_length,
    Label* if_equal, Label* if_not_equal) {
  TNode<RawPtrT> lhs_data = DirectStringData(lhs, lhs_instance_type);
  TNode<RawPtrT> rhs_data = DirectStringData(rhs, rhs_instance_type);

  const int kChunk = kTaggedSize;
  static_assert(kObjectAlignment % kChunk == 0);
  // Round up the byte_length to `ceiling(length / kChunk) * kChunk`
  TNode<IntPtrT> rounded_up_len = UncheckedCast<IntPtrT>(WordAnd(
      UncheckedCast<WordT>(IntPtrAdd(byte_length, IntPtrConstant(kChunk - 1))),
      UncheckedCast<WordT>(IntPtrConstant(~(kChunk - 1)))));
  TNode<RawPtrT> lhs_end = RawPtrAdd(lhs_data, rounded_up_len);

#ifdef ENABLE_SLOW_DCHECKS
  // The padding must be zeroed for chunked comparison to be correct. This loop
  // checks all bytes being 0 from byte_length up to rounded_up_len.
  // If we ever stop zeroing the padding, GenerateStringRelationalComparison
  // below will also need to be updated.
  {
    TVARIABLE(IntPtrT, var_padding_offset, byte_length);
    Label loop(this, &var_padding_offset), loop_end(this);
    Goto(&loop);
    BIND(&loop);
    {
      GotoIf(WordEqual(var_padding_offset.value(), rounded_up_len), &loop_end);

      // Load the next byte
      TNode<Word32T> lhs_value = UncheckedCast<Word32T>(Load(
          MachineType::Uint8(), lhs_data,
          WordShl(var_padding_offset.value(),
                  ElementSizeLog2Of(MachineType::Uint8().representation()))));
      TNode<Word32T> rhs_value = UncheckedCast<Word32T>(Load(
          MachineType::Uint8(), rhs_data,
          WordShl(var_padding_offset.value(),
                  ElementSizeLog2Of(MachineType::Uint8().representation()))));

      // Check the padding is zero.
      CSA_CHECK(this, Word32Equal(lhs_value, Int32Constant(0)));
      CSA_CHECK(this, Word32Equal(rhs_value, Int32Constant(0)));

      // Advance to next byte.
      var_padding_offset =
          IntPtrAdd(var_padding_offset.value(), IntPtrConstant(1));
      Goto(&loop);
    }
    BIND(&loop_end);
  }
#endif  // ENABLE_SLOW_DCHECKS

  // Compare strings in chunks of either 4 or 8 bytes, depending on the
  // alignment of allocations.
  static_assert(kChunk == ElementSizeInBytes(MachineRepresentation::kWord64) ||
                kChunk == ElementSizeInBytes(MachineRepresentation::kWord32));
  TVARIABLE(RawPtrT, rhs_ptr, rhs_data);
  VariableList vars({&rhs_ptr}, zone());

  if (kChunk == ElementSizeInBytes(MachineRepresentation::kWord64)) {
    BuildFastLoop<RawPtrT>(
        vars, lhs_data, lhs_end,
        [&](TNode<RawPtrT> lhs_ptr) {
          TNode<Word64T> lhs_value = Load<Uint64T>(lhs_ptr);
          TNode<Word64T> rhs_value = Load<Uint64T>(rhs_ptr.value());
          GotoIf(Word64NotEqual(lhs_value, rhs_value), if_not_equal);

          // Advance {rhs_ptr} to next characters. {lhs_ptr} will be
          // advanced along loop's {var_index}.
          Increment(&rhs_ptr, kChunk);
        },
        kChunk, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
  } else {
    BuildFastLoop<RawPtrT>(
        vars, lhs_data, lhs_end,
        [&](TNode<RawPtrT> lhs_ptr) {
          TNode<Word32T> lhs_value = Load<Uint32T>(lhs_ptr);
          TNode<Word32T> rhs_value = Load<Uint32T>(rhs_ptr.value());
          GotoIf(Word32NotEqual(lhs_value, rhs_value), if_not_equal);

          // Advance {rhs_ptr} to next characters. {lhs_ptr} will be
          // advanced along loop's {var_index}.
          Increment(&rhs_ptr, kChunk);
        },
        kChunk, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
  }
  Goto(if_equal);
}

void StringBuiltinsAssembler::StringEqual_Loop(
    TNode<String> lhs, TNode<Word32T> lhs_instance_type, MachineType lhs_type,
    TNode<String> rhs, TNode<Word32T> rhs_instance_type, MachineType rhs_type,
    TNode<IntPtrT> length, Label* if_equal, Label* if_not_equal) {
  Comment("StringEqual_Loop");
  CSA_DCHECK(this, WordEqual(LoadStringLengthAsWord(lhs), length));
  CSA_DCHECK(this, WordEqual(LoadStringLengthAsWord(rhs), length));

  // Compute the effective offset of the first character.
  TNode<RawPtrT> lhs_data = DirectStringData(lhs, lhs_instance_type);
  TNode<RawPtrT> rhs_data = DirectStringData(rhs, rhs_instance_type);
  TNode<RawPtrT> lhs_end =
      RawPtrAdd(lhs_data, WordShl(length, IntPtrConstant(ElementSizeLog2Of(
                                              lhs_type.representation()))));
  TVARIABLE(RawPtrT, rhs_ptr, rhs_data);
  VariableList vars({&rhs_ptr}, zone());

  // Loop over the {lhs} and {rhs} strings to see if they are equal.
  BuildFastLoop<RawPtrT>(
      vars, lhs_data, lhs_end,
      [&](TNode<RawPtrT> lhs_ptr) {
        TNode<Word32T> lhs_value =
            UncheckedCast<Word32T>(Load(lhs_type, lhs_ptr));
        TNode<Word32T> rhs_value =
            UncheckedCast<Word32T>(Load(rhs_type, rhs_ptr.value()));

        // Check if the characters match.
        GotoIf(Word32NotEqual(lhs_value, rhs_value), if_not_equal);

        // Advance {rhs_ptr} to next characters. {lhs_ptr} will be
        // advanced along loop's {var_index}.
        Increment(&rhs_ptr, ElementSizeInBytes(rhs_type.representation()));
      },
      ElementSizeInBytes(lhs_type.representation()), LoopUnrollingMode::kNo,
      IndexAdvanceMode::kPost);

  // All characters are checked and no difference was found, so the strings
  // are equal.
  Goto(if_equal);
}

TNode<String> StringBuiltinsAssembler::StringFromSingleUTF16EncodedCodePoint(
    TNode<Int32T> codepoint) {
  TVARIABLE(String, var_result, EmptyStringConstant());

  Label if_isword16(this), if_isword32(this), return_result(this);

  Branch(Uint32LessThan(codepoint, Int32Constant(0x10000)), &if_isword16,
         &if_isword32);

  BIND(&if_isword16);
  {
    var_result = StringFromSingleCharCode(codepoint);
    Goto(&return_result);
  }

  BIND(&if_isword32);
  {
    TNode<String> value = AllocateSeqTwoByteString(2);
    StoreNoWriteBarrier(
        MachineRepresentation::kWord32, value,
        IntPtrConstant(OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag),
        codepoint);
    var_result = value;
    Goto(&return_result);
  }

  BIND(&return_result);
  return var_result.value();
}

TNode<String> StringBuiltinsAssembler::AllocateConsString(TNode<Uint32T> length,
                                                          TNode<String> left,
                                                          TNode<String> right) {
  // Added string can be a cons string.
  Comment("Allocating ConsString");
  TVARIABLE(String, first, left);
  TNode<Int32T> left_instance_type = LoadInstanceType(left);
  Label handle_right(this);
  static_assert(base::bits::CountPopulation(kThinStringTagBit) == 1);
  GotoIfNot(IsSetWord32(left_instance_type, kThinStringTagBit), &handle_right);
  {
    first = LoadObjectField<String>(left, offsetof(ThinString, actual_));
    Goto(&handle_right);
  }

  BIND(&handle_right);
  TVARIABLE(String, second, right);
  TNode<Int32T> right_instance_type = LoadInstanceType(right);
  Label allocate(this);
  GotoIfNot(IsSetWord32(right_instance_type, kThinStringTagBit), &allocate);
  {
    second = LoadObjectField<String>(right, offsetof(ThinString, actual_));
    Goto(&allocate);
  }

  BIND(&allocate);
  // Determine the resulting ConsString map to use depending on whether
  // any of {left} or {right} has two byte encoding.
  static_assert(kOneByteStringTag != 0);
  static_assert(kTwoByteStringTag == 0);
  TNode<Int32T> combined_instance_type =
      Word32And(left_instance_type, right_instance_type);
  TNode<Map> result_map = CAST(Select<Object>(
      IsSetWord32(combined_instance_type, kStringEncodingMask),
      [=] { return ConsOneByteStringMapConstant(); },
      [=] { return ConsTwoByteStringMapConstant(); }));
  TNode<HeapObject> result = AllocateInNewSpace(sizeof(ConsString));
  StoreMapNoWriteBarrier(result, result_map);
  StoreObjectFieldNoWriteBarrier(result, offsetof(ConsString, length_), length);
  StoreObjectFieldNoWriteBarrier(result, offsetof(ConsString, raw_hash_field_),
                                 Int32Constant(String::kEmptyHashField));
  StoreObjectFieldNoWriteBarrier(result, offsetof(ConsString, first_),
                                 first.value());
  StoreObjectFieldNoWriteBarrier(result, offsetof(ConsString, second_),
                                 second.value());
  return CAST(result);
}

TNode<String> StringBuiltinsAssembler::StringAdd(
    TNode<ContextOrEmptyContext> context, TNode<String> left,
    TNode<String> right) {
  CSA_DCHECK(this, IsZeroOrContext(context));

  TVARIABLE(String, result);
  Label check_right(this), runtime(this, Label::kDeferred), cons(this),
      done(this, &result);

  TNode<Uint32T> left_length = LoadStringLengthAsWord32(left);
  GotoIfNot(Word32Equal(left_length, Uint32Constant(0)), &check_right);
  result = right;
  Goto(&done);

  BIND(&check_right);
  TNode<Uint32T> right_length = LoadStringLengthAsWord32(right);
  GotoIfNot(Word32Equal(right_length, Uint32Constant(0)), &cons);
  result = left;
  Goto(&done);

  BIND(&cons);
  {
    TNode<Uint32T> new_length = Uint32Add(left_length, right_length);

    // If new length is greater than String::kMaxLength, goto runtime to
    // throw. Note: we also need to invalidate the string length protector, so
    // can't just throw here directly.
    GotoIf(Uint32GreaterThan(new_length, Uint32Constant(String::kMaxLength)),
           &runtime);

    TVARIABLE(String, var_left, left);
    TVARIABLE(String, var_right, right);
    Label non_cons(this, {&var_left, &var_right});
    Label slow(this, Label::kDeferred);
    GotoIf(Uint32LessThan(new_length, Uint32Constant(ConsString::kMinLength)),
           &non_cons);

    result =
        AllocateConsString(new_length, var_left.value(), var_right.value());
    Goto(&done);

    BIND(&non_cons);

    Comment("Full string concatenate");
    TNode<Int32T> left_instance_type = LoadInstanceType(var_left.value());
    TNode<Int32T> right_instance_type = LoadInstanceType(var_right.value());
    // Compute intersection and difference of instance types.

    TNode<Int32T> ored_instance_types =
        Word32Or(left_instance_type, right_instance_type);
    TNode<Word32T> xored_instance_types =
        Word32Xor(left_instance_type, right_instance_type);

    // Check if both strings have the same encoding and both are sequential.
    GotoIf(IsSetWord32(xored_instance_types, kStringEncodingMask), &runtime);
    GotoIf(IsSetWord32(ored_instance_types, kStringRepresentationMask), &slow);

    TNode<IntPtrT> word_left_length = Signed(ChangeUint32ToWord(left_length));
    TNode<IntPtrT> word_right_length = Signed(ChangeUint32ToWord(right_length));

    Label two_byte(this);
    GotoIf(Word32Equal(Word32And(ored_instance_types,
                                 Int32Constant(kStringEncodingMask)),
                       Int32Constant(kTwoByteStringTag)),
           &two_byte);
    // One-byte sequential string case
    result = AllocateSeqOneByteString(new_length);
    CopyStringCharacters(var_left.value(), result.value(), IntPtrConstant(0),
                         IntPtrConstant(0), word_left_length,
                         String::ONE_BYTE_ENCODING, String::ONE_BYTE_ENCODING);
    CopyStringCharacters(var_right.value(), result.value(), IntPtrConstant(0),
                         word_left_length, word_right_length,
                         String::ONE_BYTE_ENCODING, String::ONE_BYTE_ENCODING);
    Goto(&done);

    BIND(&two_byte);
    {
      // Two-byte sequential string case
      result = AllocateSeqTwoByteString(new_length);
      CopyStringCharacters(var_left.value(), result.value(), IntPtrConstant(0),
                           IntPtrConstant(0), word_left_length,
                           String::TWO_BYTE_ENCODING,
                           String::TWO_BYTE_ENCODING);
      CopyStringCharacters(var_right.value(), result.value(), IntPtrConstant(0),
                           word_left_length, word_right_length,
                           String::TWO_BYTE_ENCODING,
                           String::TWO_BYTE_ENCODING);
      Goto(&done);
    }

    BIND(&slow);
    {
      // Try to unwrap indirect strings, restart the above attempt on success.
      MaybeDerefIndirectStrings(&var_left, left_instance_type, &var_right,
                                right_instance_type, &non_cons);
      Goto(&runtime);
    }
  }
  BIND(&runtime);
  {
    result = CAST(CallRuntime(Runtime::kStringAdd, context, left, right));
    Goto(&done);
  }

  BIND(&done);
  return result.value();
}

void StringBuiltinsAssembler::BranchIfCanDerefIndirectString(
    TNode<String> string, TNode<Int32T> instance_type, Label* can_deref,
    Label* cannot_deref) {
  TNode<Int32T> representation =
      Word32And(instance_type, Int32Constant(kStringRepresentationMask));
  GotoIf(Word32Equal(representation, Int32Constant(kThinStringTag)), can_deref);
  GotoIf(Word32NotEqual(representation, Int32Constant(kConsStringTag)),
         cannot_deref);
  // Cons string.
  TNode<String> rhs =
      LoadObjectField<String>(string, offsetof(ConsString, second_));
  GotoIf(IsEmptyString(rhs), can_deref);
  Goto(cannot_deref);
}

void StringBuiltinsAssembler::DerefIndirectString(TVariable<String>* var_string,
                                                  TNode<Int32T> instance_type) {
#ifdef DEBUG
  Label can_deref(this), cannot_deref(this);
  BranchIfCanDerefIndirectString(var_string->value(), instance_type, &can_deref,
                                 &cannot_deref);
  BIND(&cannot_deref);
  DebugBreak();  // Should be able to dereference string.
  Goto(&can_deref);
  BIND(&can_deref);
#endif  // DEBUG

  static_assert(static_cast<int>(offsetof(ThinString, actual_)) ==
                static_cast<int>(offsetof(ConsString, first_)));
  *var_string = LoadObjectField<String>(var_string->value(),
                                        offsetof(ThinString, actual_));
}

void StringBuiltinsAssembler::MaybeDerefIndirectString(
    TVariable<String>* var_string, TNode<Int32T> instance_type,
    Label* did_deref, Label* cannot_deref) {
  Label deref(this);
  BranchIfCanDerefIndirectString(var_string->value(), instance_type, &deref,
                                 cannot_deref);

  BIND(&deref);
  {
    DerefIndirectString(var_string, instance_type);
    Goto(did_deref);
  }
}

void StringBuiltinsAssembler::MaybeDerefIndirectStrings(
    TVariable<String>* var_left, TNode<Int32T> left_instance_type,
    TVariable<String>* var_right, TNode<Int32T> right_instance_type,
    Label* did_something) {
  Label did_nothing_left(this), did_something_left(this),
      didnt_do_anything(this);
  MaybeDerefIndirectString(var_left, left_instance_type, &did_something_left,
                           &did_nothing_left);

  BIND(&did_something_left);
  {
    MaybeDerefIndirectString(var_right, right_instance_type, did_something,
                             did_something);
  }

  BIND(&did_nothing_left);
  {
    MaybeDerefIndirectString(var_right, right_instance_type, did_something,
                             &didnt_do_anything);
  }

  BIND(&didnt_do_anything);
  // Fall through if neither string was an indirect string.
}

TNode<String> StringBuiltinsAssembler::DerefIndirectString(
    TNode<String> string, TNode<Int32T> instance_type, Label* cannot_deref) {
  Label deref(this);
  BranchIfCanDerefIndirectString(string, instance_type, &deref, cannot_deref);
  BIND(&deref);
  static_assert(static_cast<int>(offsetof(ThinString, actual_)) ==
                static_cast<int>(offsetof(ConsString, first_)));
  return LoadObjectField<String>(string, offsetof(ThinString, actual_));
}

TF_BUILTIN(StringAdd_CheckNone, StringBuiltinsAssembler) {
  auto left = Parameter<String>(Descriptor::kLeft);
  auto right = Parameter<String>(Descriptor::kRight);
  TNode<ContextOrEmptyContext> context =
      UncheckedParameter<ContextOrEmptyContext>(Descriptor::kContext);
  CSA_DCHECK(this, IsZeroOrContext(context));
  Return(StringAdd(context, left, right));
}

TF_BUILTIN(SubString, StringBuiltinsAssembler) {
  auto string = Parameter<String>(Descriptor::kString);
  auto from = Parameter<Smi>(Descriptor::kFrom);
  auto to = Parameter<Smi>(Descriptor::kTo);
  Return(SubString(string, SmiUntag(from), SmiUntag(to)));
}

void StringBuiltinsAssembler::GenerateStringRelationalComparison(
    TNode<String> left, TNode<String> right, StringComparison op) {
  TVARIABLE(String, var_left, left);
  TVARIABLE(String, var_right, right);

  Label if_less(this), if_equal(this), if_greater(this);
  Label restart(this, {&var_left, &var_right});
  Goto(&restart);
  BIND(&restart);

  TNode<String> lhs = var_left.value();
  TNode<String> rhs = var_right.value();
  // Fast check to see if {lhs} and {rhs} refer to the same String object.
  GotoIf(TaggedEqual(lhs, rhs), &if_equal);

  // Load instance types of {lhs} and {rhs}.
  TNode<Uint16T> lhs_instance_type = LoadInstanceType(lhs);
  TNode<Uint16T> rhs_instance_type = LoadInstanceType(rhs);

  // Combine the instance types into a single 16-bit value, so we can check
  // both of them at once.
  TNode<Int32T> both_instance_types = Word32Or(
      lhs_instance_type, Word32Shl(rhs_instance_type, Int32Constant(8)));

  // Check that both {lhs} and {rhs} are flat one-byte strings.
  int const kBothSeqOneByteStringMask =
      kStringEncodingMask | kStringRepresentationMask |
      ((kStringEncodingMask | kStringRepresentationMask) << 8);
  int const kBothSeqOneByteStringTag =
      kOneByteStringTag | kSeqStringTag |
      ((kOneByteStringTag | kSeqStringTag) << 8);
  Label if_bothonebyteseqstrings(this), if_notbothonebyteseqstrings(this);
  Branch(Word32Equal(Word32And(both_instance_types,
                               Int32Constant(kBothSeqOneByteStringMask)),
                     Int32Constant(kBothSeqOneByteStringTag)),
         &if_bothonebyteseqstrings, &if_notbothonebyteseqstrings);

  BIND(&if_bothonebyteseqstrings);
  {
    TNode<IntPtrT> lhs_length = LoadStringLengthAsWord(lhs);
    TNode<IntPtrT> rhs_length = LoadStringLengthAsWord(rhs);

    TNode<IntPtrT> length = IntPtrMin(lhs_length, rhs_length);

    // Loop over the {lhs} and {rhs} strings to see if they are equal.
    constexpr int kBeginOffset =
        OFFSET_OF_DATA_START(SeqOneByteString) - kHeapObjectTag;
    TNode<IntPtrT> begin = IntPtrConstant(kBeginOffset);
    TNode<IntPtrT> end = IntPtrAdd(begin, length);
    TVARIABLE(IntPtrT, var_offset, begin);
    Label chunk_loop(this, &var_offset), char_loop(this, &var_offset);
    Label if_done(this);

    // Unrolled first iteration.
    GotoIf(IntPtrEqual(length, IntPtrConstant(0)), &if_done);

    constexpr int kChunkSize = kTaggedSize;
    static_assert(
        kChunkSize == ElementSizeInBytes(MachineRepresentation::kWord64) ||
        kChunkSize == ElementSizeInBytes(MachineRepresentation::kWord32));
    if (kChunkSize == ElementSizeInBytes(MachineRepresentation::kWord32)) {
      TNode<Uint32T> lhs_chunk =
          Load<Uint32T>(lhs, IntPtrConstant(kBeginOffset));
      TNode<Uint32T> rhs_chunk =
          Load<Uint32T>(rhs, IntPtrConstant(kBeginOffset));
      GotoIf(Word32NotEqual(lhs_chunk, rhs_chunk), &char_loop);
    } else {
      TNode<Uint64T> lhs_chunk =
          Load<Uint64T>(lhs, IntPtrConstant(kBeginOffset));
      TNode<Uint64T> rhs_chunk =
          Load<Uint64T>(rhs, IntPtrConstant(kBeginOffset));
      GotoIf(Word64NotEqual(lhs_chunk, rhs_chunk), &char_loop);
    }

    var_offset = IntPtrConstant(OFFSET_OF_DATA_START(SeqOneByteString) -
                                kHeapObjectTag + kChunkSize);

    Goto(&chunk_loop);

    // Try skipping over chunks of kChunkSize identical characters.
    // This depends on padding (between strings' lengths and the actual end
    // of the heap object) being zeroed out.
    BIND(&chunk_loop);
    {
      GotoIf(IntPtrGreaterThanOrEqual(var_offset.value(), end), &if_done);

      if (kChunkSize == ElementSizeInBytes(MachineRepresentation::kWord32)) {
        TNode<Uint32T> lhs_chunk = Load<Uint32T>(lhs, var_offset.value());
        TNode<Uint32T> rhs_chunk = Load<Uint32T>(rhs, var_offset.value());
        GotoIf(Word32NotEqual(lhs_chunk, rhs_chunk), &char_loop);
      } else {
        TNode<Uint64T> lhs_chunk = Load<Uint64T>(lhs, var_offset.value());
        TNode<Uint64T> rhs_chunk = Load<Uint64T>(rhs, var_offset.value());
        GotoIf(Word64NotEqual(lhs_chunk, rhs_chunk), &char_loop);
      }

      var_offset = IntPtrAdd(var_offset.value(), IntPtrConstant(kChunkSize));
      Goto(&chunk_loop);
    }

    BIND(&char_loop);
    {
      GotoIf(WordEqual(var_offset.value(), end), &if_done);

      TNode<Uint8T> lhs_char = Load<Uint8T>(lhs, var_offset.value());
      TNode<Uint8T> rhs_char = Load<Uint8T>(rhs, var_offset.value());

      Label if_charsdiffer(this);
      GotoIf(Word32NotEqual(lhs_char, rhs_char), &if_charsdiffer);

      var_offset = IntPtrAdd(var_offset.value(), IntPtrConstant(1));
      Goto(&char_loop);

      BIND(&if_charsdiffer);
      Branch(Uint32LessThan(lhs_char, rhs_char), &if_less, &if_greater);
    }

    BIND(&if_done);
    {
      // All characters up to the min length are equal, decide based on
      // string length.
      GotoIf(IntPtrEqual(lhs_length, rhs_length), &if_equal);
      Branch(IntPtrLessThan(lhs_length, rhs_length), &if_less, &if_greater);
    }
  }

  BIND(&if_notbothonebyteseqstrings);
  {
    // Try to unwrap indirect strings, restart the above attempt on success.
    MaybeDerefIndirectStrings(&var_left, lhs_instance_type, &var_right,
                              rhs_instance_type, &restart);
    // TODO(bmeurer): Add support for two byte string relational comparisons.
    switch (op) {
      case StringComparison::kLessThan:
        TailCallRuntime(Runtime::kStringLessThan, NoContextConstant(), lhs,
                        rhs);
        break;
      case StringComparison::kLessThanOrEqual:
        TailCallRuntime(Runtime::kStringLessThanOrEqual, NoContextConstant(),
                        lhs, rhs);
        break;
      case StringComparison::kGreaterThan:
        TailCallRuntime(Runtime::kStringGreaterThan, NoContextConstant(), lhs,
                        rhs);
        break;
      case StringComparison::kGreaterThanOrEqual:
        TailCallRuntime(Runtime::kStringGreaterThanOrEqual, NoContextConstant(),
                        lhs, rhs);
        break;
      case StringComparison::kCompare:
        TailCallRuntime(Runtime::kStringCompare, NoContextConstant(), lhs, rhs);
        break;
    }
  }

  BIND(&if_less);
  switch (op) {
    case StringComparison::kLessThan:
    case StringComparison::kLessThanOrEqual:
      Return(TrueConstant());
      break;

    case StringComparison::kGreaterThan:
    case StringComparison::kGreaterThanOrEqual:
      Return(FalseConstant());
      break;

    case StringComparison::kCompare:
      Return(SmiConstant(-1));
      break;
  }

  BIND(&if_equal);
  switch (op) {
    case StringComparison::kLessThan:
    case StringComparison::kGreaterThan:
      Return(FalseConstant());
      break;

    case StringComparison::kLessThanOrEqual:
    case StringComparison::kGreaterThanOrEqual:
      Return(TrueConstant());
      break;

    case StringComparison::kCompare:
      Return(SmiConstant(0));
      break;
  }

  BIND(&if_greater);
  switch (op) {
    case StringComparison::kLessThan:
    case StringComparison::kLessThanOrEqual:
      Return(FalseConstant());
      break;

    case StringComparison::kGreaterThan:
    case StringComparison::kGreaterThanOrEqual:
      Return(TrueConstant());
      break;

    case StringComparison::kCompare:
      Return(SmiConstant(1));
      break;
  }
}

TF_BUILTIN(StringEqual, StringBuiltinsAssembler) {
  auto left = Parameter<String>(Descriptor::kLeft);
  auto right = Parameter<String>(Descriptor::kRight);
  auto length = UncheckedParameter<IntPtrT>(Descriptor::kLength);
  // Callers must handle the case where {lhs} and {rhs} refer to the same
  // String object.
  CSA_DCHECK(this, TaggedNotEqual(left, right));
  GenerateStringEqual(left, right, length);
}

TF_BUILTIN(StringLessThan, StringBuiltinsAssembler) {
  auto left = Parameter<String>(Descriptor::kLeft);
  auto right = Parameter<String>(Descriptor::kRight);
  GenerateStringRelationalComparison(left, right, StringComparison::kLessThan);
}

TF_BUILTIN(StringLessThanOrEqual, StringBuiltinsAssembler) {
  auto left = Parameter<String>(Descriptor::kLeft);
  auto right = Parameter<String>(Descriptor::kRight);
  GenerateStringRelationalComparison(left, right,
                                     StringComparison::kLessThanOrEqual);
}

TF_BUILTIN(StringGreaterThan, StringBuiltinsAssembler) {
  auto left = Parameter<String>(Descriptor::kLeft);
  auto right = Parameter<String>(Descriptor::kRight);
  GenerateStringRelationalComparison(left, right,
                                     StringComparison::kGreaterThan);
}

TF_BUILTIN(StringCompare, StringBuiltinsAssembler) {
  auto left = Parameter<String>(Descriptor::kLeft);
  auto right = Parameter<String>(Descriptor::kRight);
  GenerateStringRelationalComparison(left, right, StringComparison::kCompare);
}

TF_BUILTIN(StringGreaterThanOrEqual, StringBuiltinsAssembler) {
  auto left = Parameter<String>(Descriptor::kLeft);
  auto right = Parameter<String>(Descriptor::kRight);
  GenerateStringRelationalComparison(left, right,
                                     StringComparison::kGreaterThanOrEqual);
}

TF_BUILTIN(StringFromCodePointAt, StringBuiltinsAssembler) {
  auto receiver = Parameter<String>(Descriptor::kReceiver);
  auto position = UncheckedParameter<IntPtrT>(Descriptor::kPosition);

  // TODO(sigurds) Figure out if passing length as argument pays off.
  TNode<IntPtrT> length = LoadStringLengthAsWord(receiver);
  // Load the character code at the {position} from the {receiver}.
  TNode<Int32T> code =
      LoadSurrogatePairAt(receiver, length, position, UnicodeEncoding::UTF16);
  // Create a String from the UTF16 encoded code point
  TNode<String> result = StringFromSingleUTF16EncodedCodePoint(code);
  Return(result);
}

// -----------------------------------------------------------------------------
// ES6 section 21.1 String Objects

// ES6 #sec-string.fromcharcode
TF_BUILTIN(StringFromCharCode, StringBuiltinsAssembler) {
  // TODO(ishell): use constants from Descriptor once the JSFunction linkage
  // arguments are reordered.
  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  auto context = Parameter<Context>(Descriptor::kContext);

  CodeStubArguments arguments(this, argc);
  TNode<Uint32T> unsigned_argc =
      Unsigned(TruncateIntPtrToInt32(arguments.GetLengthWithoutReceiver()));
  // Check if we have exactly one argument (plus the implicit receiver), i.e.
  // if the parent frame is not an inlined arguments frame.
  Label if_oneargument(this), if_notoneargument(this);
  Branch(IntPtrEqual(arguments.GetLengthWithoutReceiver(), IntPtrConstant(1)),
         &if_oneargument, &if_notoneargument);

  BIND(&if_oneargument);
  {
    // Single argument case, perform fast single character string cache lookup
    // for one-byte code units, or fall back to creating a single character
    // string on the fly otherwise.
    TNode<Object> code = arguments.AtIndex(0);
    TNode<Word32T> code32 = TruncateTaggedToWord32(context, code);
    TNode<Int32T> code16 =
        Signed(Word32And(code32, Int32Constant(String::kMaxUtf16CodeUnit)));
    TNode<String> result = StringFromSingleCharCode(code16);
    arguments.PopAndReturn(result);
  }

  TNode<Word32T> code16;
  BIND(&if_notoneargument);
  {
    Label two_byte(this);
    // Assume that the resulting string contains only one-byte characters.
    TNode<String> one_byte_result = AllocateSeqOneByteString(unsigned_argc);

    TVARIABLE(IntPtrT, var_max_index, IntPtrConstant(0));

    // Iterate over the incoming arguments, converting them to 8-bit character
    // codes. Stop if any of the conversions generates a code that doesn't fit
    // in 8 bits.
    CodeStubAssembler::VariableList vars({&var_max_index}, zone());
    arguments.ForEach(vars, [&](TNode<Object> arg) {
      TNode<Word32T> code32 = TruncateTaggedToWord32(context, arg);
      code16 = Word32And(code32, Int32Constant(String::kMaxUtf16CodeUnit));

      GotoIf(
          Int32GreaterThan(code16, Int32Constant(String::kMaxOneByteCharCode)),
          &two_byte);

      // The {code16} fits into the SeqOneByteString {one_byte_result}.
      TNode<IntPtrT> offset = ElementOffsetFromIndex(
          var_max_index.value(), UINT8_ELEMENTS,
          OFFSET_OF_DATA_START(SeqOneByteString) - kHeapObjectTag);
      StoreNoWriteBarrier(MachineRepresentation::kWord8, one_byte_result,
                          offset, code16);
      var_max_index = IntPtrAdd(var_max_index.value(), IntPtrConstant(1));
    });
    arguments.PopAndReturn(one_byte_result);

    BIND(&two_byte);

    // At least one of the characters in the string requires a 16-bit
    // representation.  Allocate a SeqTwoByteString to hold the resulting
    // string.
    TNode<String> two_byte_result = AllocateSeqTwoByteString(unsigned_argc);

    // Copy the characters that have already been put in the 8-bit string into
    // their corresponding positions in the new 16-bit string.
    TNode<IntPtrT> zero = IntPtrConstant(0);
    CopyStringCharacters(one_byte_result, two_byte_result, zero, zero,
                         var_max_index.value(), String::ONE_BYTE_ENCODING,
                         String::TWO_BYTE_ENCODING);

    // Write the character that caused the 8-bit to 16-bit fault.
    TNode<IntPtrT> max_index_offset = ElementOffsetFromIndex(
        var_max_index.value(), UINT16_ELEMENTS,
        OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag);
    StoreNoWriteBarrier(MachineRepresentation::kWord16, two_byte_result,
                        max_index_offset, code16);
    var_max_index = IntPtrAdd(var_max_index.value(), IntPtrConstant(1));

    // Resume copying the passed-in arguments from the same place where the
    // 8-bit copy stopped, but this time copying over all of the characters
    // using a 16-bit representation.
    arguments.ForEach(
        vars,
        [&](TNode<Object> arg) {
          TNode<Word32T> code32 = TruncateTaggedToWord32(context, arg);
          TNode<Word32T> code16 =
              Word32And(code32, Int32Constant(String::kMaxUtf16CodeUnit));

          TNode<IntPtrT> offset = ElementOffsetFromIndex(
              var_max_index.value(), UINT16_ELEMENTS,
              OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag);
          StoreNoWriteBarrier(MachineRepresentation::kWord16, two_byte_result,
                              offset, code16);
          var_max_index = IntPtrAdd(var_max_index.value(), IntPtrConstant(1));
        },
        var_max_index.value());

    arguments.PopAndReturn(two_byte_result);
  }
}

void StringBuiltinsAssembler::MaybeCallFunctionAtSymbol(
    const TNode<Context> context, const TNode<Object> object,
    const TNode<Object> maybe_string, Handle<Symbol> symbol,
    DescriptorIndexNameValue additional_property_to_check,
    const NodeFunction0& regexp_call, const NodeFunction1& generic_call) {
  Label out(this), no_protector(this), object_is_heapobject(this);
  Label get_property_lookup(this);

  // The protector guarantees that that the Number and String wrapper
  // prototypes do not contain Symbol.{matchAll|replace|split} (aka.
  // @@matchAll, @@replace @@split).
  GotoIf(IsNumberStringNotRegexpLikeProtectorCellInvalid(), &no_protector);
  // Smi is safe thanks to the protector.
  GotoIf(TaggedIsSmi(object), &out);
  // String is safe thanks to the protector.
  GotoIf(IsString(CAST(object)), &out);
  // HeapNumber is safe thanks to the protector.
  Branch(IsHeapNumber(CAST(object)), &out, &object_is_heapobject);

  BIND(&no_protector);
  // Smis have to go through the GetProperty lookup in case Number.prototype or
  // Object.prototype was modified.
  Branch(TaggedIsSmi(object), &get_property_lookup, &object_is_heapobject);

  // Take the fast path for RegExps.
  // There's two conditions: {object} needs to be a fast regexp, and
  // {maybe_string} must be a string (we can't call ToString on the fast path
  // since it may mutate {object}).
  {
    Label stub_call(this), slow_lookup(this);

    BIND(&object_is_heapobject);
    TNode<HeapObject> heap_object = CAST(object);

    GotoIf(TaggedIsSmi(maybe_string), &slow_lookup);
    GotoIfNot(IsString(CAST(maybe_string)), &slow_lookup);

    // Note we don't run a full (= permissive) check here, because passing the
    // check implies calling the fast variants of target builtins, which assume
    // we've already made their appropriate fast path checks. This is not the
    // case though; e.g.: some of the target builtins access flag getters.
    // TODO(jgruber): Handle slow flag accesses on the fast path and make this
    // permissive.
    RegExpBuiltinsAssembler regexp_asm(state());
    regexp_asm.BranchIfFastRegExp(
        context, heap_object, LoadMap(heap_object),
        PrototypeCheckAssembler::kCheckPrototypePropertyConstness,
        additional_property_to_check, &stub_call, &slow_lookup);

    BIND(&stub_call);
    // TODO(jgruber): Add a no-JS scope once it exists.
    regexp_call();

    BIND(&slow_lookup);
    // Special case null and undefined to skip the property lookup.
    Branch(IsNullOrUndefined(heap_object), &out, &get_property_lookup);
  }

  // Fall back to a slow lookup of {heap_object[symbol]}.
  //
  // The spec uses GetMethod({heap_object}, {symbol}), which has a few quirks:
  // * null values are turned into undefined, and
  // * an exception is thrown if the value is not undefined, null, or callable.
  // We handle the former by jumping to {out} for null values as well, while
  // the latter is already handled by the Call({maybe_func}) operation.

  BIND(&get_property_lookup);
  const TNode<Object> maybe_func = GetProperty(context, object, symbol);
  GotoIf(IsUndefined(maybe_func), &out);
  GotoIf(IsNull(maybe_func), &out);

  // Attempt to call the function.
  generic_call(maybe_func);

  BIND(&out);
}

const TNode<Smi> StringBuiltinsAssembler::IndexOfDollarChar(
    const TNode<Context> context, const TNode<String> string) {
  const TNode<String> dollar_string = HeapConstantNoHole(
      isolate()->factory()->LookupSingleCharacterStringFromCode('$'));
  const TNode<Smi> dollar_ix = CAST(CallBuiltin(
      Builtin::kStringIndexOf, context, string, dollar_string, SmiConstant(0)));
  return dollar_ix;
}

TNode<String> StringBuiltinsAssembler::GetSubstitution(
    TNode<Context> context, TNode<String> subject_string,
    TNode<Smi> match_start_index, TNode<Smi> match_end_index,
    TNode<String> replace_string) {
  CSA_DCHECK(this, TaggedIsPositiveSmi(match_start_index));
  CSA_DCHECK(this, TaggedIsPositiveSmi(match_end_index));

  TVARIABLE(String, var_result, replace_string);
  Label runtime(this), out(this);

  // In this primitive implementation we simply look for the next '$' char in
  // {replace_string}. If it doesn't exist, we can simply return
  // {replace_string} itself. If it does, then we delegate to
  // String::GetSubstitution, passing in the index of the first '$' to avoid
  // repeated scanning work.
  // TODO(jgruber): Possibly extend this in the future to handle more complex
  // cases without runtime calls.

  const TNode<Smi> dollar_index = IndexOfDollarChar(context, replace_string);
  Branch(SmiIsNegative(dollar_index), &out, &runtime);

  BIND(&runtime);
  {
    CSA_DCHECK(this, TaggedIsPositiveSmi(dollar_index));

    const TNode<Object> matched =
        CallBuiltin(Builtin::kStringSubstring, context, subject_string,
                    SmiUntag(match_start_index), SmiUntag(match_end_index));
    const TNode<String> replacement_string = CAST(
        CallRuntime(Runtime::kGetSubstitution, context, matched, subject_string,
                    match_start_index, replace_string, dollar_index));
    var_result = replacement_string;

    Goto(&out);
  }

  BIND(&out);
  return var_result.value();
}

// ES6 #sec-string.prototype.replace
TF_BUILTIN(StringPrototypeReplace, StringBuiltinsAssembler) {
  Label out(this);

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto search = Parameter<Object>(Descriptor::kSearch);
  const auto replace = Parameter<Object>(Descriptor::kReplace);
  auto context = Parameter<Context>(Descriptor::kContext);

  const TNode<Smi> smi_zero = SmiConstant(0);

  RequireObjectCoercible(context, receiver, "String.prototype.replace");

  // Redirect to replacer method if {search[@@replace]} is not undefined.
  {
    Label next(this);

    MaybeCallFunctionAtSymbol(
        context, search, receiver, isolate()->factory()->replace_symbol(),
        DescriptorIndexNameValue{
            JSRegExp::kSymbolReplaceFunctionDescriptorIndex,
            RootIndex::kreplace_symbol, Context::REGEXP_REPLACE_FUNCTION_INDEX},
        [=]() {
          Return(CallBuiltin(Builtin::kRegExpReplace, context, search, receiver,
                             replace));
        },
        [=](TNode<Object> fn) {
          Return(Call(context, fn, search, receiver, replace));
        });
    Goto(&next);

    BIND(&next);
  }

  // Convert {receiver} and {search} to strings.

  const TNode<String> subject_string = ToString_Inline(context, receiver);
  const TNode<String> search_string = ToString_Inline(context, search);

  const TNode<IntPtrT> subject_length = LoadStringLengthAsWord(subject_string);
  const TNode<IntPtrT> search_length = LoadStringLengthAsWord(search_string);

  // Fast-path single-char {search}, long cons {receiver}, and simple string
  // {replace}.
  {
    Label next(this);

    GotoIfNot(WordEqual(search_length, IntPtrConstant(1)), &next);
    GotoIfNot(IntPtrGreaterThan(subject_length, IntPtrConstant(0xFF)), &next);
    GotoIf(TaggedIsSmi(replace), &next);
    GotoIfNot(IsString(CAST(replace)), &next);

    TNode<String> replace_string = CAST(replace);
    const TNode<Uint16T> subject_instance_type =
        LoadInstanceType(subject_string);
    GotoIfNot(IsConsStringInstanceType(subject_instance_type), &next);

    GotoIf(TaggedIsPositiveSmi(IndexOfDollarChar(context, replace_string)),
           &next);

    // Searching by traversing a cons string tree and replace with cons of
    // slices works only when the replaced string is a single character, being
    // replaced by a simple string and only pays off for long strings.
    // TODO(jgruber): Reevaluate if this is still beneficial.
    // TODO(jgruber): TailCallRuntime when it correctly handles adapter frames.
    Return(CallRuntime(Runtime::kStringReplaceOneCharWithString, context,
                       subject_string, search_string, replace_string));

    BIND(&next);
  }

  // TODO(jgruber): Extend StringIndexOf to handle two-byte strings and
  // longer substrings - we can handle up to 8 chars (one-byte) / 4 chars
  // (2-byte).

  const TNode<Smi> match_start_index =
      CAST(CallBuiltin(Builtin::kStringIndexOf, context, subject_string,
                       search_string, smi_zero));

  // Early exit if no match found.
  {
    Label next(this), return_subject(this);

    GotoIfNot(SmiIsNegative(match_start_index), &next);

    // The spec requires to perform ToString(replace) if the {replace} is not
    // callable even if we are going to exit here.
    // Since ToString() being applied to Smi does not have side effects for
    // numbers we can skip it.
    GotoIf(TaggedIsSmi(replace), &return_subject);
    GotoIf(IsCallableMap(LoadMap(CAST(replace))), &return_subject);

    // TODO(jgruber): Could introduce ToStringSideeffectsStub which only
    // performs observable parts of ToString.
    ToString_Inline(context, replace);
    Goto(&return_subject);

    BIND(&return_subject);
    Return(subject_string);

    BIND(&next);
  }

  const TNode<Smi> match_end_index =
      SmiAdd(match_start_index, SmiFromIntPtr(search_length));

  TVARIABLE(String, var_result, EmptyStringConstant());

  // Compute the prefix.
  {
    Label next(this);

    GotoIf(SmiEqual(match_start_index, smi_zero), &next);
    const TNode<String> prefix =
        CAST(CallBuiltin(Builtin::kStringSubstring, context, subject_string,
                         IntPtrConstant(0), SmiUntag(match_start_index)));
    var_result = prefix;

    Goto(&next);
    BIND(&next);
  }

  // Compute the string to replace with.

  Label if_iscallablereplace(this), if_notcallablereplace(this);
  GotoIf(TaggedIsSmi(replace), &if_notcallablereplace);
  Branch(IsCallableMap(LoadMap(CAST(replace))), &if_iscallablereplace,
         &if_notcallablereplace);

  BIND(&if_iscallablereplace);
  {
    const TNode<Object> replacement =
        Call(context, replace, UndefinedConstant(), search_string,
             match_start_index, subject_string);
    const TNode<String> replacement_string =
        ToString_Inline(context, replacement);
    var_result = CAST(CallBuiltin(Builtin::kStringAdd_CheckNone, context,
                                  var_result.value(), replacement_string));
    Goto(&out);
  }

  BIND(&if_notcallablereplace);
  {
    const TNode<String> replace_string = ToString_Inline(context, replace);
    const TNode<Object> replacement =
        GetSubstitution(context, subject_string, match_start_index,
                        match_end_index, replace_string);
    var_result = CAST(CallBuiltin(Builtin::kStringAdd_CheckNone, context,
                                  var_result.value(), replacement));
    Goto(&out);
  }

  BIND(&out);
  {
    const TNode<Object> suffix =
        CallBuiltin(Builtin::kStringSubstring, context, subject_string,
                    SmiUntag(match_end_index), subject_length);
    const TNode<Object> result = CallBuiltin(
        Builtin::kStringAdd_CheckNone, context, var_result.value(), suffix);
    Return(result);
  }
}

// ES #sec-string.prototype.matchAll
TF_BUILTIN(StringPrototypeMatchAll, StringBuiltinsAssembler) {
  char const* method_name = "String.prototype.matchAll";

  auto context = Parameter<Context>(Descriptor::kContext);
  auto maybe_regexp = Parameter<Object>(Descriptor::kRegexp);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  TNode<NativeContext> native_context = LoadNativeContext(context);

  // 1. Let O be ? RequireObjectCoercible(this value).
  RequireObjectCoercible(context, receiver, method_name);

  RegExpMatchAllAssembler regexp_asm(state());
  {
    Label fast(this), slow(this, Label::kDeferred),
        throw_exception(this, Label::kDeferred),
        throw_flags_exception(this, Label::kDeferred), next(this);

    // 2. If regexp is neither undefined nor null, then
    //   a. Let isRegExp be ? IsRegExp(regexp).
    //   b. If isRegExp is true, then
    //     i. Let flags be ? Get(regexp, "flags").
    //    ii. Perform ? RequireObjectCoercible(flags).
    //   iii. If ? ToString(flags) does not contain "g", throw a
    //        TypeError exception.
    GotoIf(TaggedIsSmi(maybe_regexp), &next);
    TNode<HeapObject> heap_maybe_regexp = CAST(maybe_regexp);
    regexp_asm.BranchIfFastRegExpForMatch(context, heap_maybe_regexp, &fast,
                                          &slow);

    BIND(&fast);
    {
      TNode<BoolT> is_global = regexp_asm.FlagGetter(context, heap_maybe_regexp,
                                                     JSRegExp::kGlobal, true);
      Branch(is_global, &next, &throw_exception);
    }

    BIND(&slow);
    {
      GotoIfNot(regexp_asm.IsRegExp(native_context, heap_maybe_regexp), &next);

      TNode<Object> flags = GetProperty(context, heap_maybe_regexp,
                                        isolate()->factory()->flags_string());
      // TODO(syg): Implement a RequireObjectCoercible with more flexible error
      // messages.
      GotoIf(IsNullOrUndefined(flags), &throw_flags_exception);

      TNode<String> flags_string = ToString_Inline(context, flags);
      TNode<String> global_char_string = StringConstant("g");
      TNode<Smi> global_ix =
          CAST(CallBuiltin(Builtin::kStringIndexOf, context, flags_string,
                           global_char_string, SmiConstant(0)));
      Branch(SmiEqual(global_ix, SmiConstant(-1)), &throw_exception, &next);
    }

    BIND(&throw_exception);
    ThrowTypeError(context, MessageTemplate::kRegExpGlobalInvokedOnNonGlobal,
                   method_name);

    BIND(&throw_flags_exception);
    ThrowTypeError(context,
                   MessageTemplate::kStringMatchAllNullOrUndefinedFlags);

    BIND(&next);
  }
  //   a. Let matcher be ? GetMethod(regexp, @@matchAll).
  //   b. If matcher is not undefined, then
  //     i. Return ? Call(matcher, regexp, « O »).
  auto if_regexp_call = [&] {
    // MaybeCallFunctionAtSymbol guarantees fast path is chosen only if
    // maybe_regexp is a fast regexp and receiver is a string.
    TNode<String> s = CAST(receiver);

    Return(
        RegExpPrototypeMatchAllImpl(context, native_context, maybe_regexp, s));
  };
  auto if_generic_call = [=](TNode<Object> fn) {
    Return(Call(context, fn, maybe_regexp, receiver));
  };
  MaybeCallFunctionAtSymbol(
      context, maybe_regexp, receiver, isolate()->factory()->match_all_symbol(),
      DescriptorIndexNameValue{JSRegExp::kSymbolMatchAllFunctionDescriptorIndex,
                               RootIndex::kmatch_all_symbol,
                               Context::REGEXP_MATCH_ALL_FUNCTION_INDEX},
      if_regexp_call, if_generic_call);

  // 3. Let S be ? ToString(O).
  TNode<String> s = ToString_Inline(context, receiver);

  // 4. Let rx be ? RegExpCreate(R, "g").
  TNode<Object> rx = regexp_asm.RegExpCreate(context, native_context,
                                             maybe_regexp, StringConstant("g"));

  // 5. Return ? Invoke(rx, @@matchAll, « S »).
  TNode<Object> match_all_func =
      GetProperty(context, rx, isolate()->factory()->match_all_symbol());
  Return(Call(context, match_all_func, rx, s));
}

TNode<JSArray> StringBuiltinsAssembler::StringToArray(
    TNode<NativeContext> context, TNode<String> subject_string,
    TNode<Smi> subject_length, TNode<Number> limit_number) {
  CSA_DCHECK(this, SmiGreaterThan(subject_length, SmiConstant(0)));

  Label done(this), call_runtime(this, Label::kDeferred),
      fill_thehole_and_call_runtime(this, Label::kDeferred);
  TVARIABLE(JSArray, result_array);

  TNode<Uint16T> instance_type = LoadInstanceType(subject_string);
  GotoIfNot(IsOneByteStringInstanceType(instance_type), &call_runtime);

  // Try to use cached one byte characters.
  {
    TNode<Smi> length_smi =
        Select<Smi>(TaggedIsSmi(limit_number),
                    [=] { return SmiMin(CAST(limit_number), subject_length); },
                    [=] { return subject_length; });
    TNode<IntPtrT> length = SmiToIntPtr(length_smi);

    ToDirectStringAssembler to_direct(state(), subject_string);
    to_direct.TryToDirect(&call_runtime);

    // The extracted direct string may be two-byte even though the wrapping
    // string is one-byte.
    GotoIfNot(IsOneByteStringInstanceType(to_direct.instance_type()),
              &call_runtime);

    TNode<FixedArray> elements =
        CAST(AllocateFixedArray(PACKED_ELEMENTS, length));
    // Don't allocate anything while {string_data} is live!
    TNode<RawPtrT> string_data =
        to_direct.PointerToData(&fill_thehole_and_call_runtime);
    TNode<IntPtrT> string_data_offset = to_direct.offset();
    TNode<FixedArray> cache = SingleCharacterStringTableConstant();

    BuildFastLoop<IntPtrT>(
        IntPtrConstant(0), length,
        [&](TNode<IntPtrT> index) {
          // TODO(jkummerow): Implement a CSA version of
          // DisallowGarbageCollection and use that to guard
          // ToDirectStringAssembler.PointerToData().
          CSA_DCHECK(this, WordEqual(to_direct.PointerToData(&call_runtime),
                                     string_data));
          TNode<Int32T> char_code =
              UncheckedCast<Int32T>(Load(MachineType::Uint8(), string_data,
                                         IntPtrAdd(index, string_data_offset)));
          TNode<UintPtrT> code_index = ChangeUint32ToWord(char_code);
          TNode<Object> entry = LoadFixedArrayElement(cache, code_index);

          CSA_DCHECK(this, Word32BinaryNot(IsUndefined(entry)));

          StoreFixedArrayElement(elements, index, entry);
        },
        1, LoopUnrollingMode::kNo, IndexAdvanceMode::kPost);

    TNode<Map> array_map = LoadJSArrayElementsMap(PACKED_ELEMENTS, context);
    result_array = AllocateJSArray(array_map, elements, length_smi);
    Goto(&done);

    BIND(&fill_thehole_and_call_runtime);
    {
      FillFixedArrayWithValue(PACKED_ELEMENTS, elements, IntPtrConstant(0),
                              length, RootIndex::kTheHoleValue);
      Goto(&call_runtime);
    }
  }

  BIND(&call_runtime);
  {
    result_array = CAST(CallRuntime(Runtime::kStringToArray, context,
                                    subject_string, limit_number));
    Goto(&done);
  }

  BIND(&done);
  return result_array.value();
}

// ES6 section 21.1.3.19 String.prototype.split ( separator, limit )
TF_BUILTIN(StringPrototypeSplit, StringBuiltinsAssembler) {
  const int kSeparatorArg = 0;
  const int kLimitArg = 1;

  const TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  TNode<Object> receiver = args.GetReceiver();
  const TNode<Object> separator = args.GetOptionalArgumentValue(kSeparatorArg);
  const TNode<Object> limit = args.GetOptionalArgumentValue(kLimitArg);
  auto context = Parameter<NativeContext>(Descriptor::kContext);

  TNode<Smi> smi_zero = SmiConstant(0);

  RequireObjectCoercible(context, receiver, "String.prototype.split");

  // Redirect to splitter method if {separator[@@split]} is not undefined.

  MaybeCallFunctionAtSymbol(
      context, separator, receiver, isolate()->factory()->split_symbol(),
      DescriptorIndexNameValue{JSRegExp::kSymbolSplitFunctionDescriptorIndex,
                               RootIndex::ksplit_symbol,
                               Context::REGEXP_SPLIT_FUNCTION_INDEX},
      [&]() {
        args.PopAndReturn(CallBuiltin(Builtin::kRegExpSplit, context, separator,
                                      receiver, limit));
      },
      [&](TNode<Object> fn) {
        args.PopAndReturn(Call(context, fn, separator, receiver, limit));
      });

  // String and integer conversions.

  TNode<String> subject_string = ToString_Inline(context, receiver);
  TNode<Number> limit_number = Select<Number>(
      IsUndefined(limit), [=] { return NumberConstant(kMaxUInt32); },
      [=] { return ToUint32(context, limit); });
  const TNode<String> separator_string = ToString_Inline(context, separator);

  Label return_empty_array(this);

  // Shortcut for {limit} == 0.
  GotoIf(TaggedEqual(limit_number, smi_zero), &return_empty_array);

  // ECMA-262 says that if {separator} is undefined, the result should
  // be an array of size 1 containing the entire string.
  {
    Label next(this);
    GotoIfNot(IsUndefined(separator), &next);

    const ElementsKind kind = PACKED_ELEMENTS;
    const TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> array_map = LoadJSArrayElementsMap(kind, native_context);

    TNode<Smi> length = SmiConstant(1);
    TNode<IntPtrT> capacity = IntPtrConstant(1);
    TNode<JSArray> result = AllocateJSArray(kind, array_map, capacity, length);

    TNode<FixedArray> fixed_array = CAST(LoadElements(result));
    StoreFixedArrayElement(fixed_array, 0, subject_string);

    args.PopAndReturn(result);

    BIND(&next);
  }

  // If the separator string is empty then return the elements in the subject.
  {
    Label next(this);
    GotoIfNot(SmiEqual(LoadStringLengthAsSmi(separator_string), smi_zero),
              &next);

    TNode<Smi> subject_length = LoadStringLengthAsSmi(subject_string);
    GotoIf(SmiEqual(subject_length, smi_zero), &return_empty_array);

    args.PopAndReturn(
        StringToArray(context, subject_string, subject_length, limit_number));

    BIND(&next);
  }

  const TNode<Object> result =
      CallRuntime(Runtime::kStringSplit, context, subject_string,
                  separator_string, limit_number);
  args.PopAndReturn(result);

  BIND(&return_empty_array);
  {
    const ElementsKind kind = PACKED_ELEMENTS;
    const TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> array_map = LoadJSArrayElementsMap(kind, native_context);

    TNode<Smi> length = smi_zero;
    TNode<IntPtrT> capacity = IntPtrConstant(0);
    TNode<JSArray> result_array =
        AllocateJSArray(kind, array_map, capacity, length);

    args.PopAndReturn(result_array);
  }
}

TF_BUILTIN(StringSubstring, StringBuiltinsAssembler) {
  auto string = Parameter<String>(Descriptor::kString);
  auto from = UncheckedParameter<IntPtrT>(Descriptor::kFrom);
  auto to = UncheckedParameter<IntPtrT>(Descriptor::kTo);

  Return(SubString(string, from, to));
}


// Return the |word32| codepoint at {index}. Supports SeqStrings and
// ExternalStrings.
// TODO(v8:9880): Use UintPtrT here.
TNode<Int32T> StringBuiltinsAssembler::LoadSurrogatePairAt(
    TNode<String> string, TNode<IntPtrT> length, TNode<IntPtrT> index,
    UnicodeEncoding encoding) {
  Label handle_surrogate_pair(this), return_result(this);
  TVARIABLE(Int32T, var_result);
  TVARIABLE(Int32T, var_trail);
  var_result = StringCharCodeAt(string, Unsigned(index));
  var_trail = Int32Constant(0);

  GotoIf(Word32NotEqual(Word32And(var_result.value(), Int32Constant(0xFC00)),
                        Int32Constant(0xD800)),
         &return_result);
  TNode<IntPtrT> next_index = IntPtrAdd(index, IntPtrConstant(1));

  GotoIfNot(IntPtrLessThan(next_index, length), &return_result);
  var_trail = StringCharCodeAt(string, Unsigned(next_index));
  Branch(Word32Equal(Word32And(var_trail.value(), Int32Constant(0xFC00)),
                     Int32Constant(0xDC00)),
         &handle_surrogate_pair, &return_result);

  BIND(&handle_surrogate_pair);
  {
    TNode<Int32T> lead = var_result.value();
    TNode<Int32T> trail = var_trail.value();

    // Check that this path is only taken if a surrogate pair is found
    CSA_SLOW_DCHECK(this,
                    Uint32GreaterThanOrEqual(lead, Int32Constant(0xD800)));
    CSA_SLOW_DCHECK(this, Uint32LessThan(lead, Int32Constant(0xDC00)));
    CSA_SLOW_DCHECK(this,
                    Uint32GreaterThanOrEqual(trail, Int32Constant(0xDC00)));
    CSA_SLOW_DCHECK(this, Uint32LessThan(trail, Int32Constant(0xE000)));

    switch (encoding) {
      case UnicodeEncoding::UTF16:
        var_result = Word32Or(
// Need to swap the order for big-endian platforms
#if V8_TARGET_BIG_ENDIAN
            Word32Shl(lead, Int32Constant(16)), trail);
#else
            Word32Shl(trail, Int32Constant(16)), lead);
#endif
        break;

      case UnicodeEncoding::UTF32: {
        // Convert UTF16 surrogate pair into |word32| code point, encoded as
        // UTF32.
        TNode<Int32T> surrogate_offset =
            Int32Constant(0x10000 - (0xD800 << 10) - 0xDC00);

        // (lead << 10) + trail + SURROGATE_OFFSET
        var_result = Int32Add(Word32Shl(lead, Int32Constant(10)),
                              Int32Add(trail, surrogate_offset));
        break;
      }
    }
    Goto(&return_result);
  }

  BIND(&return_result);
  return var_result.value();
}

TNode<BoolT> StringBuiltinsAssembler::HasUnpairedSurrogate(TNode<String> string,
                                                           Label* if_indirect) {
  TNode<Uint16T> instance_type = LoadInstanceType(string);
  CSA_DCHECK(this, Word32Equal(Word32And(instance_type,
                                         Int32Constant(kStringEncodingMask)),
                               Int32Constant(kTwoByteStringTag)));
  GotoIfNot(Word32Equal(Word32And(instance_type,
                                  Int32Constant(kIsIndirectStringMask |
                                                kUncachedExternalStringMask)),
                        Int32Constant(0)),
            if_indirect);

  TNode<RawPtrT> string_data = DirectStringData(string, instance_type);
  TNode<IntPtrT> length = LoadStringLengthAsWord(string);

  const TNode<ExternalReference> has_unpaired_surrogate =
      ExternalConstant(ExternalReference::has_unpaired_surrogate());
  return UncheckedCast<BoolT>(
      CallCFunction(has_unpaired_surrogate, MachineType::Uint32(),
                    std::make_pair(MachineType::Pointer(), string_data),
                    std::make_pair(MachineType::IntPtr(), length)));
}

void StringBuiltinsAssembler::ReplaceUnpairedSurrogates(TNode<String> source,
                                                        TNode<String> dest,
                                                        Label* if_indirect) {
  TNode<Uint16T> source_instance_type = LoadInstanceType(source);
  CSA_DCHECK(this, Word32Equal(Word32And(source_instance_type,
                                         Int32Constant(kStringEncodingMask)),
                               Int32Constant(kTwoByteStringTag)));
  GotoIfNot(Word32Equal(Word32And(source_instance_type,
                                  Int32Constant(kIsIndirectStringMask |
                                                kUncachedExternalStringMask)),
                        Int32Constant(0)),
            if_indirect);

  TNode<RawPtrT> source_data = DirectStringData(source, source_instance_type);
  // The destination string is a freshly allocated SeqString, and so is always
  // direct.
  TNode<Uint16T> dest_instance_type = LoadInstanceType(dest);
  CSA_DCHECK(this, Word32Equal(Word32And(dest_instance_type,
                                         Int32Constant(kStringEncodingMask)),
                               Int32Constant(kTwoByteStringTag)));
  TNode<RawPtrT> dest_data = DirectStringData(dest, dest_instance_type);
  TNode<IntPtrT> length = LoadStringLengthAsWord(source);
  CSA_DCHECK(this, IntPtrEqual(length, LoadStringLengthAsWord(dest)));

  const TNode<ExternalReference> replace_unpaired_surrogates =
      ExternalConstant(ExternalReference::replace_unpaired_surrogates());
  CallCFunction(replace_unpaired_surrogates, MachineType::Pointer(),
                std::make_pair(MachineType::Pointer(), source_data),
                std::make_pair(MachineType::Pointer(), dest_data),
                std::make_pair(MachineType::IntPtr(), length));
}

void StringBuiltinsAssembler::BranchIfStringPrimitiveWithNoCustomIteration(
    TNode<Object> object, TNode<Context> context, Label* if_true,
    Label* if_false) {
  GotoIf(TaggedIsSmi(object), if_false);
  GotoIfNot(IsString(CAST(object)), if_false);

  // Check that the String iterator hasn't been modified in a way that would
  // affect iteration.
  TNode<PropertyCell> protector_cell = StringIteratorProtectorConstant();
  DCHECK(i::IsPropertyCell(isolate()->heap()->string_iterator_protector()));
  Branch(
      TaggedEqual(LoadObjectField(protector_cell, PropertyCell::kValueOffset),
                  SmiConstant(Protectors::kProtectorValid)),
      if_true, if_false);
}

// Instantiate template due to shared library requirements.
template V8_EXPORT_PRIVATE void StringBuiltinsAssembler::CopyStringCharacters(
    TNode<String> from_string, TNode<String> to_string,
    TNode<IntPtrT> from_index, TNode<IntPtrT> to_index,
    TNode<IntPtrT> character_count, String::Encoding from_encoding,
    String::Encoding to_encoding);

template V8_EXPORT_PRIVATE void StringBuiltinsAssembler::CopyStringCharacters(
    TNode<RawPtrT> from_string, TNode<String> to_string,
    TNode<IntPtrT> from_index, TNode<IntPtrT> to_index,
    TNode<IntPtrT> character_count, String::Encoding from_encoding,
    String::Encoding to_encoding);

template <typename T>
void StringBuiltinsAssembler::CopyStringCharacters(
    TNode<T> from_string, TNode<String> to_string, TNode<IntPtrT> from_index,
    TNode<IntPtrT> to_index, TNode<IntPtrT> character_count,
    String::Encoding from_encoding, String::Encoding to_encoding) {
  // from_string could be either a String or a RawPtrT in the case we pass in
  // faked sequential strings when handling external subject strings.
  bool from_one_byte = from_encoding == String::ONE_BYTE_ENCODING;
  bool to_one_byte = to_encoding == String::ONE_BYTE_ENCODING;
  Comment("CopyStringCharacters ",
          from_one_byte ? "ONE_BYTE_ENCODING" : "TWO_BYTE_ENCODING", " -> ",
          to_one_byte ? "ONE_BYTE_ENCODING" : "TWO_BYTE_ENCODING");

  ElementsKind from_kind = from_one_byte ? UINT8_ELEMENTS : UINT16_ELEMENTS;
  ElementsKind to_kind = to_one_byte ? UINT8_ELEMENTS : UINT16_ELEMENTS;
  static_assert(OFFSET_OF_DATA_START(SeqOneByteString) ==
                OFFSET_OF_DATA_START(SeqTwoByteString));
  int header_size = OFFSET_OF_DATA_START(SeqOneByteString) - kHeapObjectTag;
  TNode<IntPtrT> from_offset =
      ElementOffsetFromIndex(from_index, from_kind, header_size);
  TNode<IntPtrT> to_offset =
      ElementOffsetFromIndex(to_index, to_kind, header_size);
  TNode<IntPtrT> byte_count =
      ElementOffsetFromIndex(character_count, from_kind);
  TNode<IntPtrT> limit_offset = IntPtrAdd(from_offset, byte_count);

  // Prepare the fast loop.
  MachineType type =
      from_one_byte ? MachineType::Uint8() : MachineType::Uint16();
  MachineRepresentation rep = to_one_byte ? MachineRepresentation::kWord8
                                          : MachineRepresentation::kWord16;
  int from_increment = 1 << ElementsKindToShiftSize(from_kind);
  int to_increment = 1 << ElementsKindToShiftSize(to_kind);

  TVARIABLE(IntPtrT, current_to_offset, to_offset);
  VariableList vars({&current_to_offset}, zone());
  int to_index_constant = 0, from_index_constant = 0;
  bool index_same = (from_encoding == to_encoding) &&
                    (from_index == to_index ||
                     (TryToInt32Constant(from_index, &from_index_constant) &&
                      TryToInt32Constant(to_index, &to_index_constant) &&
                      from_index_constant == to_index_constant));
  BuildFastLoop<IntPtrT>(
      vars, from_offset, limit_offset,
      [&](TNode<IntPtrT> offset) {
        compiler::Node* value = Load(type, from_string, offset);
#if DEBUG
        // Copying two-byte characters to one-byte is okay if callers have
        // checked that this loses no information.
        if (v8_flags.debug_code && !from_one_byte && to_one_byte) {
          CSA_DCHECK(this, Uint32LessThanOrEqual(UncheckedCast<Uint32T>(value),
                                                 Uint32Constant(0xFF)));
        }
#endif
        StoreNoWriteBarrier(rep, to_string,
                            index_same ? offset : current_to_offset.value(),
                            value);
        if (!index_same) {
          Increment(&current_to_offset, to_increment);
        }
      },
      from_increment, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
}

// A wrapper around CopyStringCharacters which determines the correct string
// encoding, allocates a corresponding sequential string, and then copies the
// given character range using CopyStringCharacters.
// |from_string| must be a sequential string.
// 0 <= |from_index| <= |from_index| + |character_count| < from_string.length.
template <typename T>
TNode<String> StringBuiltinsAssembler::AllocAndCopyStringCharacters(
    TNode<T> from, TNode<Int32T> from_instance_type, TNode<IntPtrT> from_index,
    TNode<IntPtrT> character_count) {
  Label end(this), one_byte_sequential(this), two_byte_sequential(this);
  TVARIABLE(String, var_result);

  Branch(IsOneByteStringInstanceType(from_instance_type), &one_byte_sequential,
         &two_byte_sequential);

  // The subject string is a sequential one-byte string.
  BIND(&one_byte_sequential);
  {
    TNode<String> result = AllocateSeqOneByteString(
        Unsigned(TruncateIntPtrToInt32(character_count)));
    CopyStringCharacters<T>(from, result, from_index, IntPtrConstant(0),
                            character_count, String::ONE_BYTE_ENCODING,
                            String::ONE_BYTE_ENCODING);
    var_result = result;
    Goto(&end);
  }

  // The subject string is a sequential two-byte string.
  BIND(&two_byte_sequential);
  {
    // Check if the to-be-copied range happens to contain only one-byte
    // characters, and copy it to a one-byte string if so.
    // If the range is long enough, we check 8 characters at a time, to reduce
    // the amount of branching.
    // For a more readable version of this logic, see {StringFromTwoByteSlice}
    // in wasm.tq.
    TNode<IntPtrT> start_offset = ElementOffsetFromIndex(
        from_index, UINT16_ELEMENTS,
        OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag);
    TNode<IntPtrT> end_offset = IntPtrAdd(
        start_offset, ElementOffsetFromIndex(character_count, UINT16_ELEMENTS));
    TNode<IntPtrT> eight_char_loop_end = IntPtrSub(
        end_offset, ElementOffsetFromIndex(IntPtrConstant(8), UINT16_ELEMENTS));

    TVARIABLE(IntPtrT, var_cursor, start_offset);
    TNode<RawPtrT> raw_from;
    if constexpr (std::is_same_v<T, RawPtrT>) {
      raw_from = from;
    } else {
      raw_from = ReinterpretCast<RawPtrT>(BitcastTaggedToWord(from));
    }
    Label first_loop(this, &var_cursor), second_loop(this, &var_cursor);
    Label twobyte(this);
    Branch(IntPtrLessThanOrEqual(start_offset, eight_char_loop_end),
           &first_loop, &second_loop);
    BIND(&first_loop);
    {
      TNode<RawPtrT> chunk = RawPtrAdd(raw_from, var_cursor.value());
      TNode<Uint32T> c1 = Load<Uint16T>(chunk);
      TNode<Uint32T> c2 = Load<Uint16T>(chunk, IntPtrConstant(2));
      TNode<Uint32T> bits = Word32Or(c1, c2);
      TNode<Uint32T> c3 = Load<Uint16T>(chunk, IntPtrConstant(4));
      bits = Word32Or(bits, c3);
      TNode<Uint32T> c4 = Load<Uint16T>(chunk, IntPtrConstant(6));
      bits = Word32Or(bits, c4);
      TNode<Uint32T> c5 = Load<Uint16T>(chunk, IntPtrConstant(8));
      bits = Word32Or(bits, c5);
      TNode<Uint32T> c6 = Load<Uint16T>(chunk, IntPtrConstant(10));
      bits = Word32Or(bits, c6);
      TNode<Uint32T> c7 = Load<Uint16T>(chunk, IntPtrConstant(12));
      bits = Word32Or(bits, c7);
      TNode<Uint32T> c8 = Load<Uint16T>(chunk, IntPtrConstant(14));
      bits = Word32Or(bits, c8);
      GotoIf(Uint32GreaterThan(bits, Uint32Constant(0xFF)), &twobyte);
      Increment(&var_cursor, 8 * sizeof(uint16_t));
      Branch(IntPtrLessThanOrEqual(var_cursor.value(), eight_char_loop_end),
             &first_loop, &second_loop);
    }

    BIND(&second_loop);
    TVARIABLE(Uint32T, var_bits, Uint32Constant(0));
    VariableList vars({&var_bits}, zone());
    FastLoopBody<IntPtrT> one_char_loop = [&](TNode<IntPtrT> offset) {
      TNode<Uint32T> c = Load<Uint16T>(from, offset);
      var_bits = Word32Or(var_bits.value(), c);
    };
    BuildFastLoop<IntPtrT>(vars, var_cursor, var_cursor.value(), end_offset,
                           one_char_loop, sizeof(uint16_t),
                           LoopUnrollingMode::kNo, IndexAdvanceMode::kPost);
    GotoIf(Uint32GreaterThan(var_bits.value(), Uint32Constant(0xFF)), &twobyte);
    // Fallthrough: only one-byte characters in the to-be-copied range.
    {
      TNode<String> result = AllocateSeqOneByteString(
          Unsigned(TruncateIntPtrToInt32(character_count)));
      CopyStringCharacters<T>(from, result, from_index, IntPtrConstant(0),
                              character_count, String::TWO_BYTE_ENCODING,
                              String::ONE_BYTE_ENCODING);
      var_result = result;
      Goto(&end);
    }

    BIND(&twobyte);
    {
      TNode<String> result = AllocateSeqTwoByteString(
          Unsigned(TruncateIntPtrToInt32(character_count)));
      CopyStringCharacters<T>(from, result, from_index, IntPtrConstant(0),
                              character_count, String::TWO_BYTE_ENCODING,
                              String::TWO_BYTE_ENCODING);
      var_result = result;
      Goto(&end);
    }
  }

  BIND(&end);
  return var_result.value();
}

// TODO(v8:9880): Use UintPtrT here.
TNode<String> StringBuiltinsAssembler::SubString(TNode<String> string,
                                                 TNode<IntPtrT> from,
                                                 TNode<IntPtrT> to) {
  TVARIABLE(String, var_result);
  ToDirectStringAssembler to_direct(state(), string);
  Label end(this), runtime(this);

  const TNode<IntPtrT> substr_length = IntPtrSub(to, from);
  const TNode<IntPtrT> string_length = LoadStringLengthAsWord(string);

  // Begin dispatching based on substring length.

  Label original_string_or_invalid_length(this);
  GotoIf(UintPtrGreaterThanOrEqual(substr_length, string_length),
         &original_string_or_invalid_length);

  // A real substring (substr_length < string_length).
  Label empty(this);
  GotoIf(IntPtrEqual(substr_length, IntPtrConstant(0)), &empty);

  Label single_char(this);
  GotoIf(IntPtrEqual(substr_length, IntPtrConstant(1)), &single_char);

  // Deal with different string types: update the index if necessary
  // and extract the underlying string.

  TNode<String> direct_string = to_direct.TryToDirect(&runtime);
  TNode<IntPtrT> offset = IntPtrAdd(from, to_direct.offset());
  const TNode<Int32T> instance_type = to_direct.instance_type();

  // The subject string can only be external or sequential string of either
  // encoding at this point.
  Label external_string(this);
  {
    if (v8_flags.string_slices) {
      Label next(this);

      // Short slice.  Copy instead of slicing.
      GotoIf(IntPtrLessThan(substr_length,
                            IntPtrConstant(SlicedString::kMinLength)),
             &next);

      // Allocate new sliced string.
      Label one_byte_slice(this), two_byte_slice(this);
      Branch(IsOneByteStringInstanceType(to_direct.instance_type()),
             &one_byte_slice, &two_byte_slice);

      BIND(&one_byte_slice);
      {
        var_result = AllocateSlicedOneByteString(
            Unsigned(TruncateIntPtrToInt32(substr_length)), direct_string,
            SmiTag(offset));
        Goto(&end);
      }

      BIND(&two_byte_slice);
      {
        var_result = AllocateSlicedTwoByteString(
            Unsigned(TruncateIntPtrToInt32(substr_length)), direct_string,
            SmiTag(offset));
        Goto(&end);
      }

      BIND(&next);
    }

    // The subject string can only be external or sequential string of either
    // encoding at this point.
    GotoIf(to_direct.is_external(), &external_string);

    var_result = AllocAndCopyStringCharacters(direct_string, instance_type,
                                              offset, substr_length);
    Goto(&end);
  }

  // Handle external string.
  BIND(&external_string);
  {
    const TNode<RawPtrT> fake_sequential_string =
        to_direct.PointerToString(&runtime);

    var_result = AllocAndCopyStringCharacters(
        fake_sequential_string, instance_type, offset, substr_length);

    Goto(&end);
  }

  BIND(&empty);
  {
    var_result = EmptyStringConstant();
    Goto(&end);
  }

  // Substrings of length 1 are generated through CharCodeAt and FromCharCode.
  BIND(&single_char);
  {
    TNode<Int32T> char_code = StringCharCodeAt(string, Unsigned(from));
    var_result = StringFromSingleCharCode(char_code);
    Goto(&end);
  }

  BIND(&original_string_or_invalid_length);
  {
    CSA_DCHECK(this, IntPtrEqual(substr_length, string_length));

    // Equal length - check if {from, to} == {0, str.length}.
    GotoIf(UintPtrGreaterThan(from, IntPtrConstant(0)), &runtime);

    // Return the original string (substr_length == string_length).
    var_result = string;
    Goto(&end);
  }

  // Fall back to a runtime call.
  BIND(&runtime);
  {
    var_result =
        CAST(CallRuntime(Runtime::kStringSubstring, NoContextConstant(), string,
                         SmiTag(from), SmiTag(to)));
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

}  // namespace internal
}  // namespace v8
