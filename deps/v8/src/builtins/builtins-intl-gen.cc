// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/builtins/builtins-iterator-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/objects/js-list-format-inl.h"
#include "src/objects/js-list-format.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class IntlBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit IntlBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  void ListFormatCommon(TNode<Context> context, TNode<Int32T> argc,
                        Runtime::FunctionId format_func_id,
                        const char* method_name);

  TNode<JSArray> AllocateEmptyJSArray(TNode<Context> context);

  TNode<IntPtrT> PointerToSeqStringData(TNode<String> seq_string) {
    CSA_DCHECK(this,
               IsSequentialStringInstanceType(LoadInstanceType(seq_string)));
    static_assert(OFFSET_OF_DATA_START(SeqOneByteString) ==
                  OFFSET_OF_DATA_START(SeqTwoByteString));
    return IntPtrAdd(BitcastTaggedToWord(seq_string),
                     IntPtrConstant(OFFSET_OF_DATA_START(SeqOneByteString) -
                                    kHeapObjectTag));
  }

  TNode<Uint8T> GetChar(TNode<SeqOneByteString> seq_string, int index) {
    size_t effective_offset = OFFSET_OF_DATA_START(SeqOneByteString) +
                              sizeof(SeqOneByteString::Char) * index -
                              kHeapObjectTag;
    return Load<Uint8T>(seq_string, IntPtrConstant(effective_offset));
  }

  // Jumps to {target} if the first two characters of {seq_string} equal
  // {pattern} ignoring case.
  void JumpIfStartsWithIgnoreCase(TNode<SeqOneByteString> seq_string,
                                  const char* pattern, Label* target) {
    size_t effective_offset =
        OFFSET_OF_DATA_START(SeqOneByteString) - kHeapObjectTag;
    TNode<Uint16T> raw =
        Load<Uint16T>(seq_string, IntPtrConstant(effective_offset));
    DCHECK_EQ(strlen(pattern), 2);
#if V8_TARGET_BIG_ENDIAN
    int raw_pattern = (pattern[0] << 8) + pattern[1];
#else
    int raw_pattern = pattern[0] + (pattern[1] << 8);
#endif
    GotoIf(Word32Equal(Word32Or(raw, Int32Constant(0x2020)),
                       Int32Constant(raw_pattern)),
           target);
  }

  TNode<BoolT> IsNonAlpha(TNode<Uint8T> character) {
    return Uint32GreaterThan(
        Int32Sub(Word32Or(character, Int32Constant(0x20)), Int32Constant('a')),
        Int32Constant('z' - 'a'));
  }

  enum class ToLowerCaseKind {
    kToLowerCase,
    kToLocaleLowerCase,
  };
  void ToLowerCaseImpl(TNode<String> string, TNode<Object> maybe_locales,
                       TNode<Context> context, ToLowerCaseKind kind,
                       std::function<void(TNode<Object>)> ReturnFct);
};

TF_BUILTIN(StringToLowerCaseIntl, IntlBuiltinsAssembler) {
  const auto string = Parameter<String>(Descriptor::kString);
  ToLowerCaseImpl(string, TNode<Object>() /*maybe_locales*/, TNode<Context>(),
                  ToLowerCaseKind::kToLowerCase,
                  [this](TNode<Object> ret) { Return(ret); });
}

TF_BUILTIN(StringPrototypeToLowerCaseIntl, IntlBuiltinsAssembler) {
  auto maybe_string = Parameter<Object>(Descriptor::kReceiver);
  auto context = Parameter<Context>(Descriptor::kContext);

  TNode<String> string =
      ToThisString(context, maybe_string, "String.prototype.toLowerCase");

  Return(CallBuiltin(Builtin::kStringToLowerCaseIntl, context, string));
}

TF_BUILTIN(StringPrototypeToLocaleLowerCase, IntlBuiltinsAssembler) {
  TNode<Int32T> argc =
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  CodeStubArguments args(this, argc);
  TNode<Object> maybe_string = args.GetReceiver();
  TNode<Context> context = Parameter<Context>(Descriptor::kContext);
  TNode<Object> maybe_locales = args.GetOptionalArgumentValue(0);
  TNode<String> string =
      ToThisString(context, maybe_string, "String.prototype.toLocaleLowerCase");
  ToLowerCaseImpl(string, maybe_locales, context,
                  ToLowerCaseKind::kToLocaleLowerCase,
                  [&args](TNode<Object> ret) { args.PopAndReturn(ret); });
}

void IntlBuiltinsAssembler::ToLowerCaseImpl(
    TNode<String> string, TNode<Object> maybe_locales, TNode<Context> context,
    ToLowerCaseKind kind, std::function<void(TNode<Object>)> ReturnFct) {
  Label call_c(this), return_string(this), runtime(this, Label::kDeferred);

  // Unpack strings if possible, and bail to runtime unless we get a one-byte
  // flat string.
  ToDirectStringAssembler to_direct(
      state(), string, ToDirectStringAssembler::kDontUnpackSlicedStrings);
  to_direct.TryToDirect(&runtime);

  if (kind == ToLowerCaseKind::kToLocaleLowerCase) {
    Label fast(this), check_locale(this);
    // Check for fast locales.
    GotoIf(IsUndefined(maybe_locales), &fast);
    // Passing a Smi as locales requires performing a ToObject conversion
    // followed by reading the length property and the "indexed" properties of
    // it until a valid locale is found.
    GotoIf(TaggedIsSmi(maybe_locales), &runtime);
    GotoIfNot(IsString(CAST(maybe_locales)), &runtime);
    GotoIfNot(IsSeqOneByteString(CAST(maybe_locales)), &runtime);
    TNode<SeqOneByteString> locale = CAST(maybe_locales);
    TNode<Uint32T> locale_length = LoadStringLengthAsWord32(locale);
    GotoIf(Int32LessThan(locale_length, Int32Constant(2)), &runtime);
    GotoIf(IsNonAlpha(GetChar(locale, 0)), &runtime);
    GotoIf(IsNonAlpha(GetChar(locale, 1)), &runtime);
    GotoIf(Word32Equal(locale_length, Int32Constant(2)), &check_locale);
    GotoIf(Word32NotEqual(locale_length, Int32Constant(5)), &runtime);
    GotoIf(Word32NotEqual(GetChar(locale, 2), Int32Constant('-')), &runtime);
    GotoIf(IsNonAlpha(GetChar(locale, 3)), &runtime);
    GotoIf(IsNonAlpha(GetChar(locale, 4)), &runtime);
    Goto(&check_locale);

    Bind(&check_locale);
    JumpIfStartsWithIgnoreCase(locale, "az", &runtime);
    JumpIfStartsWithIgnoreCase(locale, "el", &runtime);
    JumpIfStartsWithIgnoreCase(locale, "lt", &runtime);
    JumpIfStartsWithIgnoreCase(locale, "tr", &runtime);
    Goto(&fast);

    Bind(&fast);
  }

  // Early exit on empty string.
  const TNode<Uint32T> length = LoadStringLengthAsWord32(string);
  GotoIf(Word32Equal(length, Uint32Constant(0)), &return_string);

  const TNode<Int32T> instance_type = to_direct.instance_type();
  CSA_DCHECK(this,
             Word32BinaryNot(IsIndirectStringInstanceType(instance_type)));

  GotoIfNot(IsOneByteStringInstanceType(instance_type), &runtime);

  // For short strings, do the conversion in CSA through the lookup table.

  const TNode<String> dst = AllocateSeqOneByteString(length);

  const int kMaxShortStringLength = 24;  // Determined empirically.
  GotoIf(Uint32GreaterThan(length, Uint32Constant(kMaxShortStringLength)),
         &call_c);

  {
    const TNode<IntPtrT> dst_ptr = PointerToSeqStringData(dst);
    TVARIABLE(IntPtrT, var_cursor, IntPtrConstant(0));

    const TNode<IntPtrT> start_address =
        ReinterpretCast<IntPtrT>(to_direct.PointerToData(&call_c));
    const TNode<IntPtrT> end_address =
        Signed(IntPtrAdd(start_address, ChangeUint32ToWord(length)));

    const TNode<ExternalReference> to_lower_table_addr =
        ExternalConstant(ExternalReference::intl_to_latin1_lower_table());

    TVARIABLE(Word32T, var_did_change, Int32Constant(0));

    VariableList push_vars({&var_cursor, &var_did_change}, zone());
    BuildFastLoop<IntPtrT>(
        push_vars, start_address, end_address,
        [&](TNode<IntPtrT> current) {
          TNode<Uint8T> c = Load<Uint8T>(current);
          TNode<Uint8T> lower =
              Load<Uint8T>(to_lower_table_addr, ChangeInt32ToIntPtr(c));
          StoreNoWriteBarrier(MachineRepresentation::kWord8, dst_ptr,
                              var_cursor.value(), lower);

          var_did_change =
              Word32Or(Word32NotEqual(c, lower), var_did_change.value());

          Increment(&var_cursor);
        },
        kCharSize, LoopUnrollingMode::kNo, IndexAdvanceMode::kPost);

    // Return the original string if it remained unchanged in order to preserve
    // e.g. internalization and private symbols (such as the preserved object
    // hash) on the source string.
    GotoIfNot(var_did_change.value(), &return_string);

    ReturnFct(dst);
  }

  // Call into C for case conversion. The signature is:
  // String ConvertOneByteToLower(String src, String dst);
  BIND(&call_c);
  {
    const TNode<String> src = to_direct.string();

    const TNode<ExternalReference> function_addr =
        ExternalConstant(ExternalReference::intl_convert_one_byte_to_lower());

    MachineType type_tagged = MachineType::AnyTagged();

    const TNode<String> result = CAST(CallCFunction(
        function_addr, type_tagged, std::make_pair(type_tagged, src),
        std::make_pair(type_tagged, dst)));

    ReturnFct(result);
  }

  BIND(&return_string);
  ReturnFct(string);

  BIND(&runtime);
  if (kind == ToLowerCaseKind::kToLocaleLowerCase) {
    ReturnFct(CallRuntime(Runtime::kStringToLocaleLowerCase, context, string,
                          maybe_locales));
  } else {
    DCHECK_EQ(kind, ToLowerCaseKind::kToLowerCase);
    ReturnFct(CallRuntime(Runtime::kStringToLowerCaseIntl, NoContextConstant(),
                          string));
  }
}

void IntlBuiltinsAssembler::ListFormatCommon(TNode<Context> context,
                                             TNode<Int32T> argc,
                                             Runtime::FunctionId format_func_id,
                                             const char* method_name) {
  CodeStubArguments args(this, argc);

  // Label has_list(this);
  // 1. Let lf be this value.
  // 2. If Type(lf) is not Object, throw a TypeError exception.
  TNode<Object> receiver = args.GetReceiver();

  // 3. If lf does not have an [[InitializedListFormat]] internal slot, throw a
  // TypeError exception.
  ThrowIfNotInstanceType(context, receiver, JS_LIST_FORMAT_TYPE, method_name);
  TNode<JSListFormat> list_format = CAST(receiver);

  TNode<Object> list = args.GetOptionalArgumentValue(0);
  {
    // 4. Let stringList be ? StringListFromIterable(list).
    TNode<Object> string_list =
        CallBuiltin(Builtin::kStringListFromIterable, context, list);

    // 6. Return ? FormatList(lf, stringList).
    args.PopAndReturn(
        CallRuntime(format_func_id, context, list_format, string_list));
  }
}

TNode<JSArray> IntlBuiltinsAssembler::AllocateEmptyJSArray(
    TNode<Context> context) {
  return CodeStubAssembler::AllocateJSArray(
      PACKED_ELEMENTS,
      LoadJSArrayElementsMap(PACKED_ELEMENTS, LoadNativeContext(context)),
      IntPtrConstant(0), SmiConstant(0));
}

TF_BUILTIN(ListFormatPrototypeFormat, IntlBuiltinsAssembler) {
  ListFormatCommon(
      Parameter<Context>(Descriptor::kContext),
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount),
      Runtime::kFormatList, "Intl.ListFormat.prototype.format");
}

TF_BUILTIN(ListFormatPrototypeFormatToParts, IntlBuiltinsAssembler) {
  ListFormatCommon(
      Parameter<Context>(Descriptor::kContext),
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount),
      Runtime::kFormatListToParts, "Intl.ListFormat.prototype.formatToParts");
}

}  // namespace internal
}  // namespace v8
