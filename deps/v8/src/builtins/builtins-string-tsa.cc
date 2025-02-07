// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/codegen/turboshaft-builtins-assembler-inl.h"
#include "src/compiler/globals.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/string-view.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/objects/string.h"
#include "src/objects/tagged-field.h"

namespace v8::internal {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

using namespace compiler::turboshaft;  // NOLINT(build/namespaces)

template <typename Next>
class StringBuiltinsReducer : public Next {
 public:
  BUILTIN_REDUCER(StringBuiltins)

  void CopyStringCharacters(V<String> src_string, ConstOrV<WordPtr> src_begin,
                            String::Encoding src_encoding, V<String> dst_string,
                            ConstOrV<WordPtr> dst_begin,
                            String::Encoding dst_encoding,
                            ConstOrV<WordPtr> character_count) {
    bool src_one_byte = src_encoding == String::ONE_BYTE_ENCODING;
    bool dst_one_byte = dst_encoding == String::ONE_BYTE_ENCODING;
    __ CodeComment("CopyStringCharacters ",
                   src_one_byte ? "ONE_BYTE_ENCODING" : "TWO_BYTE_ENCODING",
                   " -> ",
                   dst_one_byte ? "ONE_BYTE_ENCODING" : "TWO_BYTE_ENCODING");

    const auto dst_rep = dst_one_byte ? MemoryRepresentation::Uint8()
                                      : MemoryRepresentation::Uint16();
    static_assert(OFFSET_OF_DATA_START(SeqOneByteString) ==
                  OFFSET_OF_DATA_START(SeqTwoByteString));
    const size_t data_offset = OFFSET_OF_DATA_START(SeqOneByteString);
    const int dst_stride = dst_one_byte ? 1 : 2;

    DisallowGarbageCollection no_gc;
    V<WordPtr> dst_begin_offset =
        __ WordPtrAdd(__ BitcastTaggedToWordPtr(dst_string),
                      __ WordPtrAdd(data_offset - kHeapObjectTag,
                                    __ WordPtrMul(dst_begin, dst_stride)));

    StringView src_view(no_gc, src_string, src_encoding, src_begin,
                        character_count);
    FOREACH(src_char, dst_offset,
            Zip(src_view, Sequence(dst_begin_offset, dst_stride))) {
#if DEBUG
      // Copying two-byte characters to one-byte is okay if callers have
      // checked that this loses no information.
      if (v8_flags.debug_code && !src_one_byte && dst_one_byte) {
        TSA_DCHECK(this, __ Uint32LessThanOrEqual(src_char, 0xFF));
      }
#endif
      __ Store(dst_offset, src_char, StoreOp::Kind::RawAligned(), dst_rep,
               compiler::kNoWriteBarrier);
    }
  }

  V<SeqOneByteString> AllocateSeqOneByteString(V<WordPtr> length) {
    __ CodeComment("AllocateSeqOneByteString");
    Label<SeqOneByteString> done(this);
    GOTO_IF(__ WordPtrEqual(length, 0), done,
            V<SeqOneByteString>::Cast(__ EmptyStringConstant()));

    V<WordPtr> object_size =
        __ WordPtrAdd(sizeof(SeqOneByteString),
                      __ WordPtrMul(length, sizeof(SeqOneByteString::Char)));
    V<WordPtr> aligned_size = __ AlignTagged(object_size);
    Uninitialized<SeqOneByteString> new_string =
        __ template Allocate<SeqOneByteString>(aligned_size,
                                               AllocationType::kYoung);
    __ InitializeField(new_string, AccessBuilderTS::ForMap(),
                       __ SeqOneByteStringMapConstant());

    __ InitializeField(new_string, AccessBuilderTS::ForStringLength(),
                       __ TruncateWordPtrToWord32(length));
    __ InitializeField(new_string, AccessBuilderTS::ForNameRawHashField(),
                       Name::kEmptyHashField);
    V<SeqOneByteString> string = __ FinishInitialization(std::move(new_string));
    // Clear padding.
    V<WordPtr> raw_padding_begin = __ WordPtrAdd(
        __ WordPtrAdd(__ BitcastTaggedToWordPtr(string), aligned_size),
        -kObjectAlignment - kHeapObjectTag);
    static_assert(kObjectAlignment ==
                  MemoryRepresentation::TaggedSigned().SizeInBytes());
    __ Store(raw_padding_begin, {}, __ SmiConstant(0),
             StoreOp::Kind::RawAligned(), MemoryRepresentation::TaggedSigned(),
             compiler::kNoWriteBarrier, 0, 0, true);
    GOTO(done, string);

    BIND(done, result);
    return result;
  }

  V<SeqTwoByteString> AllocateSeqTwoByteString(V<WordPtr> length) {
    __ CodeComment("AllocateSeqTwoByteString");
    Label<SeqTwoByteString> done(this);
    GOTO_IF(__ WordPtrEqual(length, 0), done,
            V<SeqTwoByteString>::Cast(__ EmptyStringConstant()));

    V<WordPtr> object_size =
        __ WordPtrAdd(sizeof(SeqTwoByteString),
                      __ WordPtrMul(length, sizeof(SeqTwoByteString::Char)));
    V<WordPtr> aligned_size = __ AlignTagged(object_size);
    Uninitialized<SeqTwoByteString> new_string =
        __ template Allocate<SeqTwoByteString>(aligned_size,
                                               AllocationType::kYoung);
    __ InitializeField(new_string, AccessBuilderTS::ForMap(),
                       __ SeqTwoByteStringMapConstant());

    __ InitializeField(new_string, AccessBuilderTS::ForStringLength(),
                       __ TruncateWordPtrToWord32(length));
    __ InitializeField(new_string, AccessBuilderTS::ForNameRawHashField(),
                       Name::kEmptyHashField);
    V<SeqTwoByteString> string = __ FinishInitialization(std::move(new_string));
    // Clear padding.
    V<WordPtr> raw_padding_begin = __ WordPtrAdd(
        __ WordPtrAdd(__ BitcastTaggedToWordPtr(string), aligned_size),
        -kObjectAlignment - kHeapObjectTag);
    static_assert(kObjectAlignment ==
                  MemoryRepresentation::TaggedSigned().SizeInBytes());
    __ Store(raw_padding_begin, {}, __ SmiConstant(0),
             StoreOp::Kind::RawAligned(), MemoryRepresentation::TaggedSigned(),
             compiler::kNoWriteBarrier, 0, 0, true);
    GOTO(done, string);

    BIND(done, result);
    return result;
  }
};

class StringBuiltinsAssemblerTS
    : public TurboshaftBuiltinsAssembler<StringBuiltinsReducer,
                                         NoFeedbackCollectorReducer> {
 public:
  using Base = TurboshaftBuiltinsAssembler;

  StringBuiltinsAssemblerTS(compiler::turboshaft::PipelineData* data,
                            compiler::turboshaft::Graph& graph,
                            Zone* phase_zone)
      : Base(data, graph, phase_zone) {}
  using Base::Asm;
};

#ifdef V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS

TS_BUILTIN(StringFromCodePointAt, StringBuiltinsAssemblerTS) {
  auto receiver = Parameter<String>(Descriptor::kReceiver);
  auto position = Parameter<WordPtr>(Descriptor::kPosition);

  // Load the character code at the {position} from the {receiver}.
  V<Word32> codepoint =
      LoadSurrogatePairAt(receiver, {}, position, UnicodeEncoding::UTF16);
  // Create a String from the UTF16 encoded code point
  V<String> result =
      StringFromSingleCodePoint(codepoint, UnicodeEncoding::UTF16);
  Return(result);
}

// ES6 #sec-string.fromcharcode
TS_BUILTIN(StringFromCharCode, StringBuiltinsAssemblerTS) {
  V<Context> context = Parameter<Context>(Descriptor::kContext);
  V<Word32> argc = Parameter<Word32>(Descriptor::kJSActualArgumentsCount);
  BuiltinArgumentsTS arguments(this, argc);

  V<WordPtr> character_count = arguments.GetLengthWithoutReceiver();
  // Check if we have exactly one argument (plus the implicit receiver), i.e.
  // if the parent frame is not an inlined arguments frame.
  IF (WordPtrEqual(arguments.GetLengthWithoutReceiver(), 1)) {
    // Single argument case, perform fast single character string cache lookup
    // for one-byte code units, or fall back to creating a single character
    // string on the fly otherwise.
    V<Object> code = arguments.AtIndex(0);
    V<Word32> code32 = TruncateTaggedToWord32(context, code);
    V<Word32> code16 = Word32BitwiseAnd(code32, String::kMaxUtf16CodeUnit);
    V<String> result = StringFromSingleCharCode(code16);
    PopAndReturn(arguments, result);
  } ELSE {
    Label<> contains_two_byte_characters(this);

    // Assume that the resulting string contains only one-byte characters.
    V<SeqOneByteString> one_byte_result =
        AllocateSeqOneByteString(character_count);

    ScopedVar<WordPtr> var_max_index(this, 0);
    // Iterate over the incoming arguments, converting them to 8-bit character
    // codes. Stop if any of the conversions generates a code that doesn't fit
    // in 8 bits.
    FOREACH(arg, arguments.Range()) {
      V<Word32> code32 = TruncateTaggedToWord32(context, arg);
      V<Word32> code16 = Word32BitwiseAnd(code32, String::kMaxUtf16CodeUnit);

      IF (UNLIKELY(Int32LessThan(String::kMaxOneByteCharCode, code16))) {
        // At least one of the characters in the string requires a 16-bit
        // representation.  Allocate a SeqTwoByteString to hold the resulting
        // string.
        V<SeqTwoByteString> two_byte_result =
            AllocateSeqTwoByteString(character_count);

        // Copy the characters that have already been put in the 8-bit string
        // into their corresponding positions in the new 16-bit string.
        CopyStringCharacters(one_byte_result, 0, String::ONE_BYTE_ENCODING,
                             two_byte_result, 0, String::TWO_BYTE_ENCODING,
                             var_max_index);

        // Write the character that caused the 8-bit to 16-bit fault.
        StoreElement(two_byte_result,
                     AccessBuilderTS::ForSeqTwoByteStringCharacter(),
                     var_max_index, code16);
        var_max_index = WordPtrAdd(var_max_index, 1);

        // Resume copying the passed-in arguments from the same place where the
        // 8-bit copy stopped, but this time copying over all of the characters
        // using a 16-bit representation.
        FOREACH(arg, arguments.Range(var_max_index)) {
          V<Word32> code32 = TruncateTaggedToWord32(context, arg);
          V<Word32> code16 =
              Word32BitwiseAnd(code32, String::kMaxUtf16CodeUnit);

          StoreElement(two_byte_result,
                       AccessBuilderTS::ForSeqTwoByteStringCharacter(),
                       var_max_index, code16);
          var_max_index = WordPtrAdd(var_max_index, 1);
        }
        PopAndReturn(arguments, two_byte_result);
      }

      // The {code16} fits into the SeqOneByteString {one_byte_result}.
      StoreElement(one_byte_result,
                   AccessBuilderTS::ForSeqOneByteStringCharacter(),
                   var_max_index, code16);
      var_max_index = WordPtrAdd(var_max_index, 1);
    }
    PopAndReturn(arguments, one_byte_result);
  }
}

#endif  // V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal
