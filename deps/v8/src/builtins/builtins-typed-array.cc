// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/logging.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/common/message-template.h"
#include "src/logging/counters.h"
#include "src/objects/elements.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/objects-inl.h"
#include "third_party/simdutf/simdutf.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 22.2 TypedArray Objects

// ES6 section 22.2.3.1 get %TypedArray%.prototype.buffer
BUILTIN(TypedArrayPrototypeBuffer) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSTypedArray, typed_array,
                 "get %TypedArray%.prototype.buffer");
  return *typed_array->GetBuffer();
}

namespace {

int64_t CapRelativeIndex(DirectHandle<Object> num, int64_t minimum,
                         int64_t maximum) {
  if (V8_LIKELY(IsSmi(*num))) {
    int64_t relative = Smi::ToInt(*num);
    return relative < 0 ? std::max<int64_t>(relative + maximum, minimum)
                        : std::min<int64_t>(relative, maximum);
  } else {
    DCHECK(IsHeapNumber(*num));
    double relative = Cast<HeapNumber>(*num)->value();
    DCHECK(!std::isnan(relative));
    return static_cast<int64_t>(
        relative < 0 ? std::max<double>(relative + maximum, minimum)
                     : std::min<double>(relative, maximum));
  }
}

}  // namespace

BUILTIN(TypedArrayPrototypeCopyWithin) {
  HandleScope scope(isolate);

  DirectHandle<JSTypedArray> array;
  const char* method_name = "%TypedArray%.prototype.copyWithin";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array,
      JSTypedArray::Validate(isolate, args.receiver(), method_name));

  int64_t len = array->GetLength();
  int64_t to = 0;
  int64_t from = 0;
  int64_t final = len;

  if (V8_LIKELY(args.length() > 1)) {
    DirectHandle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(1)));
    to = CapRelativeIndex(num, 0, len);

    if (args.length() > 2) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
      from = CapRelativeIndex(num, 0, len);

      DirectHandle<Object> end = args.atOrUndefined(isolate, 3);
      if (!IsUndefined(*end, isolate)) {
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, num,
                                           Object::ToInteger(isolate, end));
        final = CapRelativeIndex(num, 0, len);
      }
    }
  }

  int64_t count = std::min<int64_t>(final - from, len - to);
  if (count <= 0) return *array;

  // TypedArray buffer may have been transferred/detached during parameter
  // processing above.
  if (V8_UNLIKELY(array->WasDetached())) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDetachedOperation,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  if (V8_UNLIKELY(array->is_backed_by_rab())) {
    bool out_of_bounds = false;
    int64_t new_len = array->GetLengthOrOutOfBounds(out_of_bounds);
    if (out_of_bounds) {
      const MessageTemplate message = MessageTemplate::kDetachedOperation;
      DirectHandle<String> operation =
          isolate->factory()->NewStringFromAsciiChecked(method_name);
      THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message, operation));
    }
    if (new_len < len) {
      // We don't need to account for growing, since we only copy an already
      // determined number of elements and growing won't change it. If to >
      // new_len or from > new_len, the count below will be < 0, so we don't
      // need to check them separately.
      if (final > new_len) {
        final = new_len;
      }
      count = std::min<int64_t>(final - from, new_len - to);
      if (count <= 0) {
        return *array;
      }
    }
  }

  // Ensure processed indexes are within array bounds
  DCHECK_GE(from, 0);
  DCHECK_LT(from, len);
  DCHECK_GE(to, 0);
  DCHECK_LT(to, len);
  DCHECK_GE(len - count, 0);

  size_t element_size = array->element_size();
  to = to * element_size;
  from = from * element_size;
  count = count * element_size;

  uint8_t* data = static_cast<uint8_t*>(array->DataPtr());
  if (array->buffer()->is_shared()) {
    base::Relaxed_Memmove(reinterpret_cast<base::Atomic8*>(data + to),
                          reinterpret_cast<base::Atomic8*>(data + from), count);
  } else {
    std::memmove(data + to, data + from, count);
  }

  return *array;
}

// ES#sec-%typedarray%.prototype.fill
BUILTIN(TypedArrayPrototypeFill) {
  HandleScope scope(isolate);

  // 1. Let O be the this value.
  // 2. Let taRecord be ? ValidateTypedArray(O, seq-cst).
  DirectHandle<JSTypedArray> array;
  const char* method_name = "%TypedArray%.prototype.fill";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array,
      JSTypedArray::Validate(isolate, args.receiver(), method_name));
  ElementsKind kind = array->GetElementsKind();

  // 3. Let len be TypedArrayLength(taRecord).
  int64_t len = array->GetLength();

  DirectHandle<Object> obj_value = args.atOrUndefined(isolate, 1);
  if (IsBigIntTypedArrayElementsKind(kind)) {
    // 4. If O.[[ContentType]] is bigint, set value to ? ToBigInt(value).
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, obj_value,
                                       BigInt::FromObject(isolate, obj_value));
  } else {
    // 5. Otherwise, set value to ? ToNumber(value).
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, obj_value,
                                       Object::ToNumber(isolate, obj_value));
  }

  int64_t start = 0;
  int64_t end = len;

  if (args.length() > 2) {
    // 6. Let relativeStart be ? ToIntegerOrInfinity(start).
    DirectHandle<Object> num = args.atOrUndefined(isolate, 2);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, num,
                                       Object::ToInteger(isolate, num));

    // 7. If relativeStart = -∞, let startIndex be 0.
    // 8. Else if relativeStart < 0, let startIndex be max(len + relativeStart,
    //    0).
    // 9. Else, let startIndex be min(relativeStart, len).
    start = CapRelativeIndex(num, 0, len);

    // 10. If end is undefined, let relativeEnd be len; else let relativeEnd be
    //     ? ToIntegerOrInfinity(end).
    num = args.atOrUndefined(isolate, 3);
    if (!IsUndefined(*num, isolate)) {
      // 11. If relativeEnd = -∞, let endIndex be 0.
      // 12. Else if relativeEnd < 0, let endIndex be max(len + relativeEnd, 0).
      // 13. Else, let endIndex be min(relativeEnd, len).
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, num,
                                         Object::ToInteger(isolate, num));
      end = CapRelativeIndex(num, 0, len);
    }
  }

  // 14. Set taRecord to MakeTypedArrayWithBufferWitnessRecord(O, seq-cst).
  // 15. If IsTypedArrayOutOfBounds(taRecord) is true, throw a TypeError
  // exception.
  if (V8_UNLIKELY(array->WasDetached())) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDetachedOperation,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  if (V8_UNLIKELY(array->IsVariableLength())) {
    if (array->IsOutOfBounds()) {
      const MessageTemplate message = MessageTemplate::kDetachedOperation;
      DirectHandle<String> operation =
          isolate->factory()->NewStringFromAsciiChecked(method_name);
      THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message, operation));
    }
    // 16. Set len to TypedArrayLength(taRecord).
    // 17. Set endIndex to min(endIndex, len).
    end = std::min(end, static_cast<int64_t>(array->GetLength()));
  }

  int64_t count = end - start;
  if (count <= 0) return *array;

  // Ensure processed indexes are within array bounds
  DCHECK_GE(start, 0);
  DCHECK_LT(start, len);
  DCHECK_GE(end, 0);
  DCHECK_LE(end, len);
  DCHECK_LE(count, len);

  // 19. Repeat, while k < endIndex,
  //  a. Let Pk be ! ToString(𝔽(k)).
  //   b. Perform ! Set(O, Pk, value, true).
  //   c. Set k to k + 1.
  // 20. Return O.
  RETURN_RESULT_OR_FAILURE(isolate, ElementsAccessor::ForKind(kind)->Fill(
                                        array, obj_value, start, end));
}

BUILTIN(TypedArrayPrototypeIncludes) {
  HandleScope scope(isolate);

  DirectHandle<JSTypedArray> array;
  const char* method_name = "%TypedArray%.prototype.includes";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array,
      JSTypedArray::Validate(isolate, args.receiver(), method_name));

  if (args.length() < 2) return ReadOnlyRoots(isolate).false_value();

  int64_t len = array->GetLength();
  if (len == 0) return ReadOnlyRoots(isolate).false_value();

  int64_t index = 0;
  if (args.length() > 2) {
    DirectHandle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    index = CapRelativeIndex(num, 0, len);
  }

  DirectHandle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<bool> result =
      elements->IncludesValue(isolate, array, search_element, index, len);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

BUILTIN(TypedArrayPrototypeIndexOf) {
  HandleScope scope(isolate);

  DirectHandle<JSTypedArray> array;
  const char* method_name = "%TypedArray%.prototype.indexOf";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array,
      JSTypedArray::Validate(isolate, args.receiver(), method_name));

  int64_t len = array->GetLength();
  if (len == 0) return Smi::FromInt(-1);

  int64_t index = 0;
  if (args.length() > 2) {
    DirectHandle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    index = CapRelativeIndex(num, 0, len);
  }

  if (V8_UNLIKELY(array->WasDetached())) return Smi::FromInt(-1);

  if (V8_UNLIKELY(array->IsVariableLength() && array->IsOutOfBounds())) {
    return Smi::FromInt(-1);
  }

  DirectHandle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<int64_t> result =
      elements->IndexOfValue(isolate, array, search_element, index, len);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return *isolate->factory()->NewNumberFromInt64(result.FromJust());
}

BUILTIN(TypedArrayPrototypeLastIndexOf) {
  HandleScope scope(isolate);

  DirectHandle<JSTypedArray> array;
  const char* method_name = "%TypedArray%.prototype.lastIndexOf";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array,
      JSTypedArray::Validate(isolate, args.receiver(), method_name));

  int64_t len = array->GetLength();
  if (len == 0) return Smi::FromInt(-1);

  int64_t index = len - 1;
  if (args.length() > 2) {
    DirectHandle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    // Set a negative value (-1) for returning -1 if num is negative and
    // len + num is still negative. Upper bound is len - 1.
    index = std::min<int64_t>(CapRelativeIndex(num, -1, len), len - 1);
  }

  if (index < 0) return Smi::FromInt(-1);

  if (V8_UNLIKELY(array->WasDetached())) return Smi::FromInt(-1);
  if (V8_UNLIKELY(array->IsVariableLength() && array->IsOutOfBounds())) {
    return Smi::FromInt(-1);
  }

  DirectHandle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<int64_t> result =
      elements->LastIndexOfValue(array, search_element, index);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return *isolate->factory()->NewNumberFromInt64(result.FromJust());
}

BUILTIN(TypedArrayPrototypeReverse) {
  HandleScope scope(isolate);

  DirectHandle<JSTypedArray> array;
  const char* method_name = "%TypedArray%.prototype.reverse";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array,
      JSTypedArray::Validate(isolate, args.receiver(), method_name));

  ElementsAccessor* elements = array->GetElementsAccessor();
  elements->Reverse(*array);
  return *array;
}

namespace {

std::vector<std::tuple<const char*, size_t, simdutf::base64_options>>
SimdutfBase64OptionsVector() {
  return {{"base64", 6, simdutf::base64_options::base64_default},
          {"base64url", 9, simdutf::base64_options::base64_url}};
}

template <typename T>
Maybe<T> MapOptionToEnum(
    Isolate* isolate, DirectHandle<String> option_string,
    const std::vector<std::tuple<const char*, size_t, T>>& allowed_options) {
  option_string = String::Flatten(isolate, option_string);

  {
    DisallowGarbageCollection no_gc;
    String::FlatContent option_content = option_string->GetFlatContent(no_gc);

    if (option_content.IsOneByte()) {
      const unsigned char* option_string_to_compare =
          option_content.ToOneByteVector().data();
      size_t length = option_content.ToOneByteVector().size();

      for (auto& [str_val, str_size, enum_val] : allowed_options) {
        if (str_size == length &&
            CompareCharsEqual(option_string_to_compare, str_val, str_size)) {
          return Just<T>(enum_val);
        }
      }
    } else {
      const base::uc16* option_string_to_compare =
          option_content.ToUC16Vector().data();
      size_t length = option_content.ToUC16Vector().size();

      for (auto& [str_val, str_size, enum_val] : allowed_options) {
        if (str_size == length &&
            CompareCharsEqual(option_string_to_compare, str_val, str_size)) {
          return Just<T>(enum_val);
        }
      }
    }
  }

  isolate->Throw(*isolate->factory()->NewTypeError(
      MessageTemplate::kInvalidOption, option_string));
  return Nothing<T>();
}

MessageTemplate ToMessageTemplate(simdutf::error_code error) {
  switch (error) {
    case simdutf::error_code::INVALID_BASE64_CHARACTER:
      return MessageTemplate::kInvalidBase64Character;
    case simdutf::error_code::BASE64_INPUT_REMAINDER:
      return MessageTemplate::kBase64InputRemainder;
    case simdutf::error_code::BASE64_EXTRA_BITS:
      return MessageTemplate::kBase64ExtraBits;
    default:
      UNREACHABLE();
  }
}

template <typename T>
Maybe<simdutf::result> ArrayBufferFromBase64(
    Isolate* isolate, T input_vector, size_t input_length,
    size_t& output_length, simdutf::base64_options alphabet,
    simdutf::last_chunk_handling_options last_chunk_handling,
    DirectHandle<JSArrayBuffer>& buffer) {
  const char method_name[] = "Uint8Array.fromBase64";

  output_length = simdutf::maximal_binary_length_from_base64(
      reinterpret_cast<const T>(input_vector), input_length);
  std::unique_ptr<char[]> output = std::make_unique<char[]>(output_length);
  simdutf::result simd_result = simdutf::base64_to_binary_safe(
      reinterpret_cast<const T>(input_vector), input_length, output.get(),
      output_length, alphabet, last_chunk_handling);

  {
    AllowGarbageCollection gc;
    MaybeDirectHandle<JSArrayBuffer> result_buffer =
        isolate->factory()->NewJSArrayBufferAndBackingStore(
            output_length, InitializedFlag::kUninitialized);
    if (!result_buffer.ToHandle(&buffer)) {
      isolate->Throw(*isolate->factory()->NewRangeError(
          MessageTemplate::kOutOfMemory,
          isolate->factory()->NewStringFromAsciiChecked(method_name)));
      return Nothing<simdutf::result>();
    }

    memcpy(buffer->backing_store(), output.get(), output_length);
  }
  return Just<simdutf::result>(simd_result);
}

}  // namespace

// https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.frombase64
BUILTIN(Uint8ArrayFromBase64) {
  HandleScope scope(isolate);

  // 1. If string is not a String, throw a TypeError exception.
  DirectHandle<Object> input = args.atOrUndefined(isolate, 1);
  if (!IsString(*input)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kArgumentIsNonString,
                              isolate->factory()->input_string()));
  }

  DirectHandle<String> input_string =
      String::Flatten(isolate, Cast<String>(input));

  // 2. Let opts be ? GetOptionsObject(options).
  DirectHandle<Object> options = args.atOrUndefined(isolate, 2);

  // 3. Let alphabet be ? Get(opts, "alphabet").
  DirectHandle<Object> opt_alphabet;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, opt_alphabet,
      JSObject::ReadFromOptionsBag(
          options, isolate->factory()->alphabet_string(), isolate));

  // 4. If alphabet is undefined, set alphabet to "base64".
  simdutf::base64_options alphabet;
  if (IsUndefined(*opt_alphabet)) {
    alphabet = simdutf::base64_default;
  } else if (!IsString(*opt_alphabet)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kInvalidOption, opt_alphabet));
  } else {
    // 5. If alphabet is neither "base64" nor "base64url", throw a TypeError
    // exception.
    DirectHandle<String> alphabet_string = Cast<String>(opt_alphabet);

    MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, alphabet,
        MapOptionToEnum(isolate, alphabet_string,
                        SimdutfBase64OptionsVector()));
  }

  // 6. Let lastChunkHandling be ? Get(opts, "lastChunkHandling").
  DirectHandle<Object> opt_last_chunk_handling;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, opt_last_chunk_handling,
      JSObject::ReadFromOptionsBag(
          options, isolate->factory()->last_chunk_handling_string(), isolate));

  // 7. If lastChunkHandling is undefined, set lastChunkHandling to "loose".
  simdutf::last_chunk_handling_options last_chunk_handling;
  if (IsUndefined(*opt_last_chunk_handling)) {
    last_chunk_handling = simdutf::last_chunk_handling_options::loose;
  } else if (!IsString(*opt_last_chunk_handling)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kInvalidOption, opt_last_chunk_handling));
  } else {
    // 8. If lastChunkHandling is not one of "loose", "strict", or
    // "stop-before-partial", throw a TypeError exception.
    DirectHandle<String> last_chunk_handling_string =
        Cast<String>(opt_last_chunk_handling);

    std::vector<
        std::tuple<const char*, size_t, simdutf::last_chunk_handling_options>>
        last_chunk_handling_vector = {
            {"loose", 5, simdutf::last_chunk_handling_options::loose},
            {"strict", 6, simdutf::last_chunk_handling_options::strict},
            {"stop-before-partial", 19,
             simdutf::last_chunk_handling_options::stop_before_partial}};
    MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, last_chunk_handling,
        MapOptionToEnum(isolate, last_chunk_handling_string,
                        last_chunk_handling_vector));
  }

  // 9. Let result be ? FromBase64(string, alphabet, lastChunkHandling).
  size_t input_length;
  size_t output_length;
  simdutf::result simd_result;
  DirectHandle<JSArrayBuffer> buffer;
  {
    DisallowGarbageCollection no_gc;
    String::FlatContent input_content = input_string->GetFlatContent(no_gc);
    if (input_content.IsOneByte()) {
      const unsigned char* input_vector =
          input_content.ToOneByteVector().data();
      input_length = input_content.ToOneByteVector().size();
      MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, simd_result,
          ArrayBufferFromBase64(isolate,
                                reinterpret_cast<const char*>(input_vector),
                                input_length, output_length, alphabet,
                                last_chunk_handling, buffer));
    } else {
      const base::uc16* input_vector = input_content.ToUC16Vector().data();
      input_length = input_content.ToUC16Vector().size();
      MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, simd_result,
          ArrayBufferFromBase64(isolate,
                                reinterpret_cast<const char16_t*>(input_vector),
                                input_length, output_length, alphabet,
                                last_chunk_handling, buffer));
    }
  }

  // 10. If result.[[Error]] is not none, then
  //    a. Throw result.[[Error]].
  if (simd_result.error != simdutf::error_code::SUCCESS) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewSyntaxError(ToMessageTemplate(simd_result.error)));
  }

  // 11. Let resultLength be the length of result.[[Bytes]].
  // 12. Let ta be ? AllocateTypedArray("Uint8Array", %Uint8Array%,
  // "%Uint8Array.prototype%", resultLength).
  // 13. Set the value at each index of
  // ta.[[ViewedArrayBuffer]].[[ArrayBufferData]] to the value at the
  // corresponding index of result.[[Bytes]].
  // 14. Return ta.
  DirectHandle<JSTypedArray> result_typed_array =
      isolate->factory()->NewJSTypedArray(kExternalUint8Array, buffer, 0,
                                          output_length);
  return *result_typed_array;
}

// https://tc39.es/proposal-arraybuffer-base64/spec/#sec-uint8array.prototype.tobase64
BUILTIN(Uint8ArrayToBase64) {
  HandleScope scope(isolate);
  const char method_name[] = "Uint8Array.prototype.toBase64";

  // 1. Let O be the this value.
  // 2. Perform ? ValidateUint8Array(O).
  CHECK_RECEIVER(JSTypedArray, uint8array, method_name);
  if (uint8array->GetElementsKind() != ElementsKind::UINT8_ELEMENTS) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  // 3. Let opts be ? GetOptionsObject(options).
  DirectHandle<Object> options = args.atOrUndefined(isolate, 1);

  // 4. Let alphabet be ? Get(opts, "alphabet").
  DirectHandle<Object> opt_alphabet;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, opt_alphabet,
      JSObject::ReadFromOptionsBag(
          options, isolate->factory()->alphabet_string(), isolate));

  // 5. If alphabet is undefined, set alphabet to "base64".
  simdutf::base64_options alphabet;
  if (IsUndefined(*opt_alphabet)) {
    alphabet = simdutf::base64_options::base64_default;
  } else if (!IsString(*opt_alphabet)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kInvalidOption, opt_alphabet));
  } else {
    // 6. If alphabet is neither "base64" nor "base64url", throw a TypeError
    // exception.
    DirectHandle<String> alphabet_string = Cast<String>(opt_alphabet);

    MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, alphabet,
        MapOptionToEnum(isolate, alphabet_string,
                        SimdutfBase64OptionsVector()));
  }

  // 7. Let omitPadding be ToBoolean(? Get(opts, "omitPadding")).
  DirectHandle<Object> omit_padding_object;
  bool omit_padding = false;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, omit_padding_object,
      JSObject::ReadFromOptionsBag(
          options, isolate->factory()->NewStringFromAsciiChecked("omitPadding"),
          isolate));

  if (Object::BooleanValue(*omit_padding_object, isolate)) {
    omit_padding = true;
  }

  if (uint8array->IsDetachedOrOutOfBounds()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDetachedOperation,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  if (alphabet == simdutf::base64_options::base64_default &&
      omit_padding == true) {
    alphabet = simdutf::base64_options::base64_default_no_padding;
  } else if (alphabet == simdutf::base64_options::base64_url &&
             omit_padding == false) {
    alphabet = simdutf::base64_options::base64_url_with_padding;
  }

  size_t output_length =
      simdutf::base64_length_from_binary(uint8array->byte_length(), alphabet);

  if (output_length == 0) {
    return *isolate->factory()->empty_string();
  }

  Handle<SeqOneByteString> output;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, output,
      isolate->factory()->NewRawOneByteString(static_cast<int>(output_length)));
  {
    DisallowGarbageCollection no_gc;
    // 8. Let toEncode be ? GetUint8ArrayBytes(O).
    // 9. If alphabet is "base64", then
    //    a. Let outAscii be the sequence of code points which results from
    //    encoding toEncode according to the base64 encoding specified in
    //    section 4 of RFC 4648. Padding is included if and only if omitPadding
    //    is false.
    // 10. Else,
    //    a. Assert: alphabet is "base64url".
    //    b. Let outAscii be the sequence of code points which results from
    //    encoding toEncode according to the base64url encoding specified in
    //    section 5 of RFC 4648. Padding is included if and only if omitPadding
    //    is false.
    // 11. Return CodePointsToString(outAscii).

    // TODO(rezvan): Make sure to add a path for SharedArrayBuffers when
    // simdutf library got updated. Also, add a test for it.
    size_t simd_result_size = simdutf::binary_to_base64(
        std::bit_cast<const char*>(uint8array->GetBuffer()->backing_store()),
        uint8array->byte_length(),
        reinterpret_cast<char*>(output->GetChars(no_gc)), alphabet);
    DCHECK_EQ(simd_result_size, output_length);
    USE(simd_result_size);
  }

  // output_length is the correct length of the output, with padding or
  // without padding, so we do not need to modify the output based on
  // padding here.
  return *output;
}

}  // namespace internal
}  // namespace v8
