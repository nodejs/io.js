// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/objects.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

#include "src/api/api-arguments-inl.h"
#include "src/api/api-natives.h"
#include "src/api/api.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/base/bits.h"
#include "src/base/debug/stack_trace.h"
#include "src/base/logging.h"
#include "src/base/overflowing-math.h"
#include "src/base/utils/random-number-generator.h"
#include "src/builtins/accessors.h"
#include "src/builtins/builtins.h"
#include "src/codegen/compiler.h"
#include "src/codegen/source-position-table.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/date/date.h"
#include "src/debug/debug.h"
#include "src/diagnostics/code-tracer.h"
#include "src/execution/arguments.h"
#include "src/execution/execution.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/execution/isolate-utils.h"
#include "src/execution/microtask-queue.h"
#include "src/execution/protectors-inl.h"
#include "src/heap/factory-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/local-factory-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/ic/ic.h"
#include "src/init/bootstrapper.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/allocation-site-scopes.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/arguments-inl.h"
#include "src/objects/bigint.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/cell-inl.h"
#include "src/objects/code-inl.h"
#include "src/objects/compilation-cache-table-inl.h"
#include "src/objects/debug-objects-inl.h"
#include "src/objects/dictionary.h"
#include "src/objects/elements.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/field-index-inl.h"
#include "src/objects/field-index.h"
#include "src/objects/field-type.h"
#include "src/objects/foreign.h"
#include "src/objects/free-space-inl.h"
#include "src/objects/function-kind.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/js-regexp-string-iterator.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/keys.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/lookup-inl.h"
#include "src/objects/map-inl.h"
#include "src/objects/map-updater.h"
#include "src/objects/map.h"
#include "src/objects/megadom-handler-inl.h"
#include "src/objects/microtask-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-body-descriptors-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/promise-inl.h"
#include "src/objects/promise.h"
#include "src/objects/property-descriptor-object-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/property-details.h"
#include "src/objects/prototype.h"
#include "src/objects/slots-atomic-inl.h"
#include "src/objects/string-comparator.h"
#include "src/objects/string-set-inl.h"
#include "src/objects/struct-inl.h"
#include "src/objects/template-objects-inl.h"
#include "src/objects/transitions-inl.h"
#include "src/parsing/preparse-data.h"
#include "src/regexp/regexp.h"
#include "src/roots/roots.h"
#include "src/snapshot/deserializer.h"
#include "src/strings/string-builder-inl.h"
#include "src/strings/string-search.h"
#include "src/strings/string-stream.h"
#include "src/strings/unicode-decoder.h"
#include "src/strings/unicode-inl.h"
#include "src/utils/hex-format.h"
#include "src/utils/identity-map.h"
#include "src/utils/ostreams.h"
#include "src/utils/sha-256.h"
#include "src/utils/utils-inl.h"
#include "src/zone/zone.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#ifdef V8_INTL_SUPPORT
#include "src/objects/js-break-iterator.h"
#include "src/objects/js-collator.h"
#include "src/objects/js-date-time-format.h"
#include "src/objects/js-list-format.h"
#include "src/objects/js-locale.h"
#include "src/objects/js-number-format.h"
#include "src/objects/js-plural-rules.h"
#include "src/objects/js-relative-time-format.h"
#include "src/objects/js-segment-iterator.h"
#include "src/objects/js-segmenter.h"
#include "src/objects/js-segments.h"
#endif  // V8_INTL_SUPPORT

namespace v8::internal {

ShouldThrow GetShouldThrow(Isolate* isolate, Maybe<ShouldThrow> should_throw) {
  if (should_throw.IsJust()) return should_throw.FromJust();

  LanguageMode mode = isolate->context()->scope_info()->language_mode();
  if (mode == LanguageMode::kStrict) return kThrowOnError;

  for (StackFrameIterator it(isolate); !it.done(); it.Advance()) {
    if (!it.frame()->is_java_script()) continue;

    // Get the language mode from closure.
    JavaScriptFrame* js_frame = static_cast<JavaScriptFrame*>(it.frame());
    std::vector<Tagged<SharedFunctionInfo>> functions;
    js_frame->GetFunctions(&functions);
    LanguageMode closure_language_mode = functions.back()->language_mode();
    if (closure_language_mode > mode) {
      mode = closure_language_mode;
    }
    break;
  }

  return is_sloppy(mode) ? kDontThrow : kThrowOnError;
}

bool ComparisonResultToBool(Operation op, ComparisonResult result) {
  switch (op) {
    case Operation::kLessThan:
      return result == ComparisonResult::kLessThan;
    case Operation::kLessThanOrEqual:
      return result == ComparisonResult::kLessThan ||
             result == ComparisonResult::kEqual;
    case Operation::kGreaterThan:
      return result == ComparisonResult::kGreaterThan;
    case Operation::kGreaterThanOrEqual:
      return result == ComparisonResult::kGreaterThan ||
             result == ComparisonResult::kEqual;
    default:
      break;
  }
  UNREACHABLE();
}

std::ostream& operator<<(std::ostream& os, InstanceType instance_type) {
  if (InstanceTypeChecker::IsJSApiObject(instance_type)) {
    return os << "[api object] "
              << static_cast<int16_t>(instance_type) -
                     i::Internals::kFirstJSApiObjectType;
  }
  switch (instance_type) {
#define WRITE_TYPE(TYPE) \
  case TYPE:             \
    return os << #TYPE;
    INSTANCE_TYPE_LIST(WRITE_TYPE)
#undef WRITE_TYPE
  }
  return os << "[unknown instance type " << static_cast<int16_t>(instance_type)
            << "]";
}

std::ostream& operator<<(std::ostream& os, PropertyCellType type) {
  switch (type) {
    case PropertyCellType::kUndefined:
      return os << "Undefined";
    case PropertyCellType::kConstant:
      return os << "Constant";
    case PropertyCellType::kConstantType:
      return os << "ConstantType";
    case PropertyCellType::kMutable:
      return os << "Mutable";
    case PropertyCellType::kInTransition:
      return os << "InTransition";
  }
  UNREACHABLE();
}

// static
Handle<FieldType> Object::OptimalType(Tagged<Object> obj, Isolate* isolate,
                                      Representation representation) {
  if (representation.IsNone()) return FieldType::None(isolate);
  if (v8_flags.track_field_types) {
    if (representation.IsHeapObject() && IsHeapObject(obj)) {
      // We can track only JavaScript objects with stable maps.
      DirectHandle<Map> map(Cast<HeapObject>(obj)->map(), isolate);
      if (map->is_stable() && IsJSReceiverMap(*map)) {
        return FieldType::Class(map, isolate);
      }
    }
  }
  return FieldType::Any(isolate);
}

Handle<UnionOf<JSAny, Hole>> Object::NewStorageFor(
    Isolate* isolate, Handle<UnionOf<JSAny, Hole>> object,
    Representation representation) {
  if (!representation.IsDouble()) return object;
  Handle<HeapNumber> result = isolate->factory()->NewHeapNumberWithHoleNaN();
  if (IsUninitialized(*object, isolate)) {
    result->set_value_as_bits(kHoleNanInt64);
  } else if (IsHeapNumber(*object)) {
    // Ensure that all bits of the double value are preserved.
    result->set_value_as_bits(Cast<HeapNumber>(*object)->value_as_bits());
  } else {
    result->set_value(Cast<Smi>(*object).value());
  }
  return result;
}

template <AllocationType allocation_type, typename IsolateT>
Handle<JSAny> Object::WrapForRead(IsolateT* isolate, Handle<JSAny> object,
                                  Representation representation) {
  DCHECK(!IsUninitialized(*object, isolate));
  if (!representation.IsDouble()) {
    DCHECK(Object::FitsRepresentation(*object, representation));
    return object;
  }
  return isolate->factory()->template NewHeapNumberFromBits<allocation_type>(
      Cast<HeapNumber>(*object)->value_as_bits());
}

template Handle<JSAny> Object::WrapForRead<AllocationType::kYoung>(
    Isolate* isolate, Handle<JSAny> object, Representation representation);
template Handle<JSAny> Object::WrapForRead<AllocationType::kOld>(
    LocalIsolate* isolate, Handle<JSAny> object, Representation representation);

MaybeHandle<JSReceiver> Object::ToObjectImpl(Isolate* isolate,
                                             DirectHandle<Object> object,
                                             const char* method_name) {
  DCHECK(!IsJSReceiver(*object));  // Use ToObject() for fast path.
  DirectHandle<Context> native_context = isolate->native_context();
  Handle<JSFunction> constructor;
  if (IsSmi(*object)) {
    constructor = handle(native_context->number_function(), isolate);
  } else {
    int constructor_function_index =
        Cast<HeapObject>(object)->map()->GetConstructorFunctionIndex();
    if (constructor_function_index == Map::kNoConstructorFunctionIndex) {
      if (method_name != nullptr) {
        THROW_NEW_ERROR(
            isolate, NewTypeError(MessageTemplate::kCalledOnNullOrUndefined,
                                  isolate->factory()->NewStringFromAsciiChecked(
                                      method_name)));
      }
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kUndefinedOrNullToObject));
    }
    constructor = handle(
        Cast<JSFunction>(native_context->get(constructor_function_index)),
        isolate);
  }
  Handle<JSObject> result = isolate->factory()->NewJSObject(constructor);
  Cast<JSPrimitiveWrapper>(result)->set_value(Cast<JSAny>(*object));
  return result;
}

// ES6 section 9.2.1.2, OrdinaryCallBindThis for sloppy callee.
// static
MaybeHandle<JSReceiver> Object::ConvertReceiver(Isolate* isolate,
                                                Handle<Object> object) {
  if (IsJSReceiver(*object)) return Cast<JSReceiver>(object);
  if (IsNullOrUndefined(*object, isolate)) {
    return isolate->global_proxy();
  }
  return Object::ToObject(isolate, object);
}

// static
MaybeHandle<Number> Object::ConvertToNumber(Isolate* isolate,
                                            Handle<Object> input) {
  while (true) {
    if (IsNumber(*input)) {
      return Cast<Number>(input);
    }
    if (IsString(*input)) {
      return String::ToNumber(isolate, Cast<String>(input));
    }
    if (IsOddball(*input)) {
      return Oddball::ToNumber(isolate, Cast<Oddball>(input));
    }
    if (IsSymbol(*input)) {
      THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kSymbolToNumber));
    }
    if (IsBigInt(*input)) {
      THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kBigIntToNumber));
    }
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, input,
        JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(input),
                                ToPrimitiveHint::kNumber));
  }
}

// static
MaybeHandle<Numeric> Object::ConvertToNumeric(Isolate* isolate,
                                              Handle<Object> input) {
  while (true) {
    if (IsNumber(*input)) {
      return Cast<Number>(input);
    }
    if (IsString(*input)) {
      return String::ToNumber(isolate, Cast<String>(input));
    }
    if (IsOddball(*input)) {
      return Oddball::ToNumber(isolate, Cast<Oddball>(input));
    }
    if (IsSymbol(*input)) {
      THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kSymbolToNumber));
    }
    if (IsBigInt(*input)) {
      return Cast<BigInt>(input);
    }
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, input,
        JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(input),
                                ToPrimitiveHint::kNumber));
  }
}

// static
MaybeHandle<Number> Object::ConvertToInteger(Isolate* isolate,
                                             Handle<Object> input) {
  ASSIGN_RETURN_ON_EXCEPTION(isolate, input, ConvertToNumber(isolate, input));
  if (IsSmi(*input)) return Cast<Smi>(input);
  return isolate->factory()->NewNumber(
      DoubleToInteger(Cast<HeapNumber>(*input)->value()));
}

// static
MaybeHandle<Number> Object::ConvertToInt32(Isolate* isolate,
                                           Handle<Object> input) {
  ASSIGN_RETURN_ON_EXCEPTION(isolate, input, ConvertToNumber(isolate, input));
  if (IsSmi(*input)) return Cast<Smi>(input);
  return isolate->factory()->NewNumberFromInt(
      DoubleToInt32(Cast<HeapNumber>(*input)->value()));
}

// static
MaybeHandle<Number> Object::ConvertToUint32(Isolate* isolate,
                                            Handle<Object> input) {
  ASSIGN_RETURN_ON_EXCEPTION(isolate, input, ConvertToNumber(isolate, input));
  if (IsSmi(*input))
    return handle(Smi::ToUint32Smi(Cast<Smi>(*input)), isolate);
  return isolate->factory()->NewNumberFromUint(
      DoubleToUint32(Cast<HeapNumber>(*input)->value()));
}

// static
MaybeHandle<Name> Object::ConvertToName(Isolate* isolate,
                                        Handle<Object> input) {
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, input,
      Object::ToPrimitive(isolate, input, ToPrimitiveHint::kString));
  if (IsName(*input)) return Cast<Name>(input);
  return ToString(isolate, input);
}

// ES6 7.1.14
// static
MaybeHandle<Object> Object::ConvertToPropertyKey(Isolate* isolate,
                                                 Handle<Object> value) {
  // 1. Let key be ToPrimitive(argument, hint String).
  MaybeHandle<Object> maybe_key =
      Object::ToPrimitive(isolate, value, ToPrimitiveHint::kString);
  // 2. ReturnIfAbrupt(key).
  Handle<Object> key;
  if (!maybe_key.ToHandle(&key)) return key;
  // 3. If Type(key) is Symbol, then return key.
  if (IsSymbol(*key)) return key;
  // 4. Return ToString(key).
  // Extending spec'ed behavior, we'd be happy to return an element index.
  if (IsSmi(*key)) return key;
  if (IsHeapNumber(*key)) {
    uint32_t uint_value;
    if (Object::ToArrayLength(*value, &uint_value) &&
        uint_value <= static_cast<uint32_t>(Smi::kMaxValue)) {
      return handle(Smi::FromInt(static_cast<int>(uint_value)), isolate);
    }
  }
  return Object::ToString(isolate, key);
}

// static
MaybeHandle<String> Object::ConvertToString(Isolate* isolate,
                                            Handle<Object> input) {
  while (true) {
    if (IsOddball(*input)) {
      return handle(Cast<Oddball>(input)->to_string(), isolate);
    }
    if (IsNumber(*input)) {
      return isolate->factory()->NumberToString(input);
    }
    if (IsSymbol(*input)) {
      THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kSymbolToString));
    }
    if (IsBigInt(*input)) {
      return BigInt::ToString(isolate, Cast<BigInt>(input));
    }
#if V8_ENABLE_WEBASSEMBLY
    // We generally don't let the WasmNull escape into the JavaScript world,
    // but some builtins may encounter it when called directly from Wasm code.
    if (IsWasmNull(*input)) {
      return isolate->factory()->null_string();
    }
#endif
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, input,
        JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(input),
                                ToPrimitiveHint::kString));
    // The previous isString() check happened in Object::ToString and thus we
    // put it at the end of the loop in this helper.
    if (IsString(*input)) {
      return Cast<String>(input);
    }
  }
}

namespace {

bool IsErrorObject(Isolate* isolate, Handle<Object> object) {
  if (!IsJSObject(*object)) return false;
  return ErrorUtils::HasErrorStackSymbolOwnProperty(isolate,
                                                    Cast<JSObject>(object));
}

Handle<String> AsStringOrEmpty(Isolate* isolate, Handle<Object> object) {
  return IsString(*object) ? Cast<String>(object)
                           : isolate->factory()->empty_string();
}

DirectHandle<String> NoSideEffectsErrorToString(Isolate* isolate,
                                                Handle<JSReceiver> error) {
  Handle<Name> name_key = isolate->factory()->name_string();
  Handle<Object> name = JSReceiver::GetDataProperty(isolate, error, name_key);
  Handle<String> name_str = AsStringOrEmpty(isolate, name);

  Handle<Name> msg_key = isolate->factory()->message_string();
  Handle<Object> msg = JSReceiver::GetDataProperty(isolate, error, msg_key);
  DirectHandle<String> msg_str = AsStringOrEmpty(isolate, msg);

  if (name_str->length() == 0) return msg_str;
  if (msg_str->length() == 0) return name_str;

  constexpr const char error_suffix[] = "<a very large string>";
  constexpr int error_suffix_size = sizeof(error_suffix);
  int suffix_size = std::min(error_suffix_size, msg_str->length());

  IncrementalStringBuilder builder(isolate);
  if (name_str->length() + suffix_size + 2 /* ": " */ > String::kMaxLength) {
    constexpr const char connector[] = "... : ";
    int connector_size = sizeof(connector);
    DirectHandle<String> truncated_name =
        isolate->factory()->NewProperSubString(
            name_str, 0,
            name_str->length() - error_suffix_size - connector_size);
    builder.AppendString(truncated_name);
    builder.AppendCStringLiteral(connector);
    builder.AppendCStringLiteral(error_suffix);
  } else {
    builder.AppendString(name_str);
    builder.AppendCStringLiteral(": ");
    if (builder.Length() + msg_str->length() <= String::kMaxLength) {
      builder.AppendString(msg_str);
    } else {
      builder.AppendCStringLiteral(error_suffix);
    }
  }

  return builder.Finish().ToHandleChecked();
}

}  // namespace

// static
MaybeDirectHandle<String> Object::NoSideEffectsToMaybeString(
    Isolate* isolate, DirectHandle<Object> input) {
  DisallowJavascriptExecution no_js(isolate);

  if (IsString(*input) || IsNumber(*input) || IsOddball(*input)) {
    return Object::ToString(isolate, input).ToHandleChecked();
  } else if (IsJSProxy(*input)) {
    DirectHandle<Object> currInput = input;
    do {
      Tagged<HeapObject> target = Cast<JSProxy>(currInput)->target(isolate);
      currInput = direct_handle(target, isolate);
    } while (IsJSProxy(*currInput));
    return NoSideEffectsToString(isolate, currInput);
  } else if (IsBigInt(*input)) {
    return BigInt::NoSideEffectsToString(isolate, Cast<BigInt>(input));
  } else if (IsJSFunctionOrBoundFunctionOrWrappedFunction(*input)) {
    // -- F u n c t i o n
    Handle<String> fun_str;
    if (IsJSBoundFunction(*input)) {
      fun_str = JSBoundFunction::ToString(Cast<JSBoundFunction>(input));
    } else if (IsJSWrappedFunction(*input)) {
      fun_str = JSWrappedFunction::ToString(Cast<JSWrappedFunction>(input));
    } else {
      DCHECK(IsJSFunction(*input));
      fun_str = JSFunction::ToString(Cast<JSFunction>(input));
    }

    if (fun_str->length() > 128) {
      IncrementalStringBuilder builder(isolate);
      builder.AppendString(isolate->factory()->NewSubString(fun_str, 0, 111));
      builder.AppendCStringLiteral("...<omitted>...");
      builder.AppendString(isolate->factory()->NewSubString(
          fun_str, fun_str->length() - 2, fun_str->length()));

      return builder.Finish().ToHandleChecked();
    }
    return fun_str;
  } else if (IsSymbol(*input)) {
    // -- S y m b o l
    DirectHandle<Symbol> symbol = Cast<Symbol>(input);

    if (symbol->is_private_name()) {
      return DirectHandle<String>(Cast<String>(symbol->description()), isolate);
    }

    IncrementalStringBuilder builder(isolate);
    builder.AppendCStringLiteral("Symbol(");
    if (IsString(symbol->description())) {
      Handle<String> description =
          handle(Cast<String>(symbol->description()), isolate);
      if (description->length() > 128) {
        builder.AppendString(
            isolate->factory()->NewSubString(description, 0, 56));
        builder.AppendCStringLiteral("...<omitted>...");
        builder.AppendString(isolate->factory()->NewSubString(
            description, description->length() - 56, description->length()));
      } else {
        builder.AppendString(description);
      }
    }
    builder.AppendCharacter(')');

    return builder.Finish().ToHandleChecked();
  } else if (IsJSReceiver(*input)) {
    // -- J S R e c e i v e r
    Handle<Object> indirect_input = indirect_handle(input, isolate);
    Handle<JSReceiver> receiver = Cast<JSReceiver>(indirect_input);
    DirectHandle<Object> to_string = JSReceiver::GetDataProperty(
        isolate, receiver, isolate->factory()->toString_string());

    if (IsErrorObject(isolate, indirect_input) ||
        *to_string == *isolate->error_to_string()) {
      // When internally formatting error objects, use a side-effects-free
      // version of Error.prototype.toString independent of the actually
      // installed toString method.
      return NoSideEffectsErrorToString(isolate, receiver);
    } else if (*to_string == *isolate->object_to_string()) {
      Handle<Object> ctor = JSReceiver::GetDataProperty(
          isolate, receiver, isolate->factory()->constructor_string());
      if (IsJSFunctionOrBoundFunctionOrWrappedFunction(*ctor)) {
        DirectHandle<String> ctor_name;
        if (IsJSBoundFunction(*ctor)) {
          ctor_name =
              JSBoundFunction::GetName(isolate, Cast<JSBoundFunction>(ctor))
                  .ToHandleChecked();
        } else if (IsJSFunction(*ctor)) {
          ctor_name = JSFunction::GetName(isolate, Cast<JSFunction>(ctor));
        }

        if (ctor_name->length() != 0) {
          IncrementalStringBuilder builder(isolate);
          builder.AppendCStringLiteral("#<");
          builder.AppendString(ctor_name);
          builder.AppendCharacter('>');

          return builder.Finish().ToHandleChecked();
        }
      }
    }
  }
  return {};
}

// static
DirectHandle<String> Object::NoSideEffectsToString(Isolate* isolate,
                                                   DirectHandle<Object> input) {
  DisallowJavascriptExecution no_js(isolate);

  // Try to convert input to a meaningful string.
  MaybeDirectHandle<String> maybe_string =
      NoSideEffectsToMaybeString(isolate, input);
  DirectHandle<String> string_handle;
  if (maybe_string.ToHandle(&string_handle)) {
    return string_handle;
  }

  // At this point, input is either none of the above or a JSReceiver.

  Handle<JSReceiver> receiver;
  if (IsJSReceiver(*input)) {
    receiver = indirect_handle(Cast<JSReceiver>(input), isolate);
  } else {
    // This is the only case where Object::ToObject throws.
    DCHECK(!IsSmi(*input));
    int constructor_function_index =
        Cast<HeapObject>(input)->map()->GetConstructorFunctionIndex();
    if (constructor_function_index == Map::kNoConstructorFunctionIndex) {
      return isolate->factory()->NewStringFromAsciiChecked("[object Unknown]");
    }

    receiver = Object::ToObjectImpl(isolate, input).ToHandleChecked();
  }

  DirectHandle<String> builtin_tag =
      direct_handle(receiver->class_name(), isolate);
  DirectHandle<Object> tag_obj = JSReceiver::GetDataProperty(
      isolate, receiver, isolate->factory()->to_string_tag_symbol());
  DirectHandle<String> tag =
      IsString(*tag_obj) ? Cast<String>(tag_obj) : builtin_tag;

  IncrementalStringBuilder builder(isolate);
  builder.AppendCStringLiteral("[object ");
  builder.AppendString(tag);
  builder.AppendCharacter(']');

  return builder.Finish().ToHandleChecked();
}

// static
MaybeHandle<Number> Object::ConvertToLength(Isolate* isolate,
                                            Handle<Object> input) {
  ASSIGN_RETURN_ON_EXCEPTION(isolate, input, ToNumber(isolate, input));
  if (IsSmi(*input)) {
    int value = std::max(Smi::ToInt(*input), 0);
    return handle(Smi::FromInt(value), isolate);
  }
  double len = DoubleToInteger(Cast<HeapNumber>(*input)->value());
  if (len <= 0.0) {
    return handle(Smi::zero(), isolate);
  } else if (len >= kMaxSafeInteger) {
    len = kMaxSafeInteger;
  }
  return isolate->factory()->NewNumber(len);
}

// static
MaybeHandle<Number> Object::ConvertToIndex(Isolate* isolate,
                                           Handle<Object> input,
                                           MessageTemplate error_index) {
  if (IsUndefined(*input, isolate)) return handle(Smi::zero(), isolate);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, input, ToNumber(isolate, input));
  if (IsSmi(*input) && Smi::ToInt(*input) >= 0) return Cast<Smi>(input);
  double len = DoubleToInteger(Object::NumberValue(Cast<Number>(*input)));
  Handle<Number> js_len = isolate->factory()->NewNumber(len);
  if (len < 0.0 || len > kMaxSafeInteger) {
    THROW_NEW_ERROR(isolate, NewRangeError(error_index, js_len));
  }
  return js_len;
}

template <typename IsolateT>
// static
bool Object::BooleanValue(Tagged<Object> obj, IsolateT* isolate) {
  if (IsSmi(obj)) return Smi::ToInt(obj) != 0;
  DCHECK(IsHeapObject(obj));
  if (IsBoolean(obj)) return IsTrue(obj, isolate);
  if (IsNullOrUndefined(obj, isolate)) return false;
#ifdef V8_ENABLE_WEBASSEMBLY
  if (IsWasmNull(obj)) return false;
#endif
  if (IsUndetectable(obj)) return false;  // Undetectable object is false.
  if (IsString(obj)) return Cast<String>(obj)->length() != 0;
  if (IsHeapNumber(obj)) return DoubleToBoolean(Cast<HeapNumber>(obj)->value());
  if (IsBigInt(obj)) return Cast<BigInt>(obj)->ToBoolean();
  return true;
}
template bool Object::BooleanValue(Tagged<Object>, Isolate*);
template bool Object::BooleanValue(Tagged<Object>, LocalIsolate*);

// static
Tagged<Object> Object::ToBoolean(Tagged<Object> obj, Isolate* isolate) {
  if (IsBoolean(obj)) return obj;
  return isolate->heap()->ToBoolean(Object::BooleanValue(obj, isolate));
}

namespace {

// TODO(bmeurer): Maybe we should introduce a marker interface Number,
// where we put all these methods at some point?
ComparisonResult StrictNumberCompare(double x, double y) {
  if (std::isnan(x) || std::isnan(y)) {
    return ComparisonResult::kUndefined;
  } else if (x < y) {
    return ComparisonResult::kLessThan;
  } else if (x > y) {
    return ComparisonResult::kGreaterThan;
  } else {
    return ComparisonResult::kEqual;
  }
}

// See Number case of ES6#sec-strict-equality-comparison
// Returns false if x or y is NaN, treats -0.0 as equal to 0.0.
bool StrictNumberEquals(double x, double y) {
  // Must check explicitly for NaN's on Windows, but -0 works fine.
  if (std::isnan(x) || std::isnan(y)) return false;
  return x == y;
}

bool StrictNumberEquals(const Tagged<Number> x, const Tagged<Number> y) {
  return StrictNumberEquals(Object::NumberValue(x), Object::NumberValue(y));
}

bool StrictNumberEquals(DirectHandle<Number> x, DirectHandle<Number> y) {
  return StrictNumberEquals(*x, *y);
}

ComparisonResult Reverse(ComparisonResult result) {
  if (result == ComparisonResult::kLessThan) {
    return ComparisonResult::kGreaterThan;
  }
  if (result == ComparisonResult::kGreaterThan) {
    return ComparisonResult::kLessThan;
  }
  return result;
}

}  // anonymous namespace

// static
Maybe<ComparisonResult> Object::Compare(Isolate* isolate, Handle<Object> x,
                                        Handle<Object> y) {
  // ES6 section 7.2.11 Abstract Relational Comparison step 3 and 4.
  if (!Object::ToPrimitive(isolate, x, ToPrimitiveHint::kNumber).ToHandle(&x) ||
      !Object::ToPrimitive(isolate, y, ToPrimitiveHint::kNumber).ToHandle(&y)) {
    return Nothing<ComparisonResult>();
  }
  if (IsString(*x) && IsString(*y)) {
    // ES6 section 7.2.11 Abstract Relational Comparison step 5.
    return Just(String::Compare(isolate, Cast<String>(x), Cast<String>(y)));
  }
  if (IsBigInt(*x) && IsString(*y)) {
    return BigInt::CompareToString(isolate, Cast<BigInt>(x), Cast<String>(y));
  }
  if (IsString(*x) && IsBigInt(*y)) {
    Maybe<ComparisonResult> maybe_result =
        BigInt::CompareToString(isolate, Cast<BigInt>(y), Cast<String>(x));
    ComparisonResult result;
    if (maybe_result.To(&result)) {
      return Just(Reverse(result));
    } else {
      return Nothing<ComparisonResult>();
    }
  }
  // ES6 section 7.2.11 Abstract Relational Comparison step 6.
  if (!Object::ToNumeric(isolate, x).ToHandle(&x) ||
      !Object::ToNumeric(isolate, y).ToHandle(&y)) {
    return Nothing<ComparisonResult>();
  }

  bool x_is_number = IsNumber(*x);
  bool y_is_number = IsNumber(*y);
  if (x_is_number && y_is_number) {
    return Just(
        StrictNumberCompare(Object::NumberValue(*x), Object::NumberValue(*y)));
  } else if (!x_is_number && !y_is_number) {
    return Just(BigInt::CompareToBigInt(Cast<BigInt>(x), Cast<BigInt>(y)));
  } else if (x_is_number) {
    return Just(Reverse(BigInt::CompareToNumber(Cast<BigInt>(y), x)));
  } else {
    return Just(BigInt::CompareToNumber(Cast<BigInt>(x), y));
  }
}

Maybe<bool> Object::Equals(Isolate* isolate, Handle<Object> x,
                           Handle<Object> y) {
  // This is the generic version of Abstract Equality Comparison. Must be in
  // sync with CodeStubAssembler::Equal.
  while (true) {
    if (IsNumber(*x)) {
      if (IsNumber(*y)) {
        return Just(StrictNumberEquals(Cast<Number>(*x), Cast<Number>(*y)));
      } else if (IsBoolean(*y)) {
        return Just(StrictNumberEquals(Cast<Number>(*x),
                                       Cast<Oddball>(y)->to_number()));
      } else if (IsString(*y)) {
        return Just(StrictNumberEquals(
            Cast<Number>(x), String::ToNumber(isolate, Cast<String>(y))));
      } else if (IsBigInt(*y)) {
        return Just(BigInt::EqualToNumber(Cast<BigInt>(y), Cast<Number>(x)));
      } else if (IsJSReceiver(*y)) {
        if (!JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(y))
                 .ToHandle(&y)) {
          return Nothing<bool>();
        }
      } else {
        return Just(false);
      }
    } else if (IsString(*x)) {
      if (IsString(*y)) {
        return Just(String::Equals(isolate, Cast<String>(x), Cast<String>(y)));
      } else if (IsNumber(*y)) {
        x = String::ToNumber(isolate, Cast<String>(x));
        return Just(StrictNumberEquals(Cast<Number>(*x), Cast<Number>(*y)));
      } else if (IsBoolean(*y)) {
        x = String::ToNumber(isolate, Cast<String>(x));
        return Just(StrictNumberEquals(Cast<Number>(*x),
                                       Cast<Oddball>(y)->to_number()));
      } else if (IsBigInt(*y)) {
        return BigInt::EqualToString(isolate, Cast<BigInt>(y), Cast<String>(x));
      } else if (IsJSReceiver(*y)) {
        if (!JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(y))
                 .ToHandle(&y)) {
          return Nothing<bool>();
        }
      } else {
        return Just(false);
      }
    } else if (IsBoolean(*x)) {
      if (IsOddball(*y)) {
        return Just(x.is_identical_to(y));
      } else if (IsNumber(*y)) {
        return Just(StrictNumberEquals(Cast<Oddball>(x)->to_number(),
                                       Cast<Number>(*y)));
      } else if (IsString(*y)) {
        y = String::ToNumber(isolate, Cast<String>(y));
        return Just(StrictNumberEquals(Cast<Oddball>(x)->to_number(),
                                       Cast<Number>(*y)));
      } else if (IsBigInt(*y)) {
        x = Oddball::ToNumber(isolate, Cast<Oddball>(x));
        return Just(BigInt::EqualToNumber(Cast<BigInt>(y), x));
      } else if (IsJSReceiver(*y)) {
        if (!JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(y))
                 .ToHandle(&y)) {
          return Nothing<bool>();
        }
        x = Oddball::ToNumber(isolate, Cast<Oddball>(x));
      } else {
        return Just(false);
      }
    } else if (IsSymbol(*x)) {
      if (IsSymbol(*y)) {
        return Just(x.is_identical_to(y));
      } else if (IsJSReceiver(*y)) {
        if (!JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(y))
                 .ToHandle(&y)) {
          return Nothing<bool>();
        }
      } else {
        return Just(false);
      }
    } else if (IsBigInt(*x)) {
      if (IsBigInt(*y)) {
        return Just(BigInt::EqualToBigInt(Cast<BigInt>(*x), Cast<BigInt>(*y)));
      }
      return Equals(isolate, y, x);
    } else if (IsJSReceiver(*x)) {
      if (IsJSReceiver(*y)) {
        return Just(x.is_identical_to(y));
      } else if (IsUndetectable(*y)) {
        return Just(IsUndetectable(*x));
      } else if (IsBoolean(*y)) {
        y = Oddball::ToNumber(isolate, Cast<Oddball>(y));
      } else if (!JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(x))
                      .ToHandle(&x)) {
        return Nothing<bool>();
      }
    } else {
      return Just(IsUndetectable(*x) && IsUndetectable(*y));
    }
  }
}

// static
bool Object::StrictEquals(Tagged<Object> obj, Tagged<Object> that) {
  if (IsNumber(obj)) {
    if (!IsNumber(that)) return false;
    return StrictNumberEquals(Cast<Number>(obj), Cast<Number>(that));
  } else if (IsString(obj)) {
    if (!IsString(that)) return false;
    return Cast<String>(obj)->Equals(Cast<String>(that));
  } else if (IsBigInt(obj)) {
    if (!IsBigInt(that)) return false;
    return BigInt::EqualToBigInt(Cast<BigInt>(obj), Cast<BigInt>(that));
  }
  return obj == that;
}

// static
Handle<String> Object::TypeOf(Isolate* isolate, DirectHandle<Object> object) {
  if (IsNumber(*object)) return isolate->factory()->number_string();
  if (IsOddball(*object))
    return handle(Cast<Oddball>(*object)->type_of(), isolate);
  if (IsUndetectable(*object)) {
    return isolate->factory()->undefined_string();
  }
  if (IsString(*object)) return isolate->factory()->string_string();
  if (IsSymbol(*object)) return isolate->factory()->symbol_string();
  if (IsBigInt(*object)) return isolate->factory()->bigint_string();
  if (IsCallable(*object)) return isolate->factory()->function_string();
  return isolate->factory()->object_string();
}

// static
MaybeHandle<Object> Object::Add(Isolate* isolate, Handle<Object> lhs,
                                Handle<Object> rhs) {
  if (IsNumber(*lhs) && IsNumber(*rhs)) {
    return isolate->factory()->NewNumber(
        Object::NumberValue(Cast<Number>(*lhs)) +
        Object::NumberValue(Cast<Number>(*rhs)));
  } else if (IsString(*lhs) && IsString(*rhs)) {
    return isolate->factory()->NewConsString(Cast<String>(lhs),
                                             Cast<String>(rhs));
  }
  ASSIGN_RETURN_ON_EXCEPTION(isolate, lhs, Object::ToPrimitive(isolate, lhs));
  ASSIGN_RETURN_ON_EXCEPTION(isolate, rhs, Object::ToPrimitive(isolate, rhs));
  if (IsString(*lhs) || IsString(*rhs)) {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, rhs, Object::ToString(isolate, rhs));
    ASSIGN_RETURN_ON_EXCEPTION(isolate, lhs, Object::ToString(isolate, lhs));
    return isolate->factory()->NewConsString(Cast<String>(lhs),
                                             Cast<String>(rhs));
  }
  Handle<Number> lhs_number;
  Handle<Number> rhs_number;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, rhs_number,
                             Object::ToNumber(isolate, rhs));
  ASSIGN_RETURN_ON_EXCEPTION(isolate, lhs_number,
                             Object::ToNumber(isolate, lhs));
  return isolate->factory()->NewNumber(Object::NumberValue(*lhs_number) +
                                       Object::NumberValue(*rhs_number));
}

// static
MaybeHandle<Object> Object::OrdinaryHasInstance(Isolate* isolate,
                                                Handle<Object> callable,
                                                Handle<Object> object) {
  // The {callable} must have a [[Call]] internal method.
  if (!IsCallable(*callable)) return isolate->factory()->false_value();

  // Check if {callable} is a bound function, and if so retrieve its
  // [[BoundTargetFunction]] and use that instead of {callable}.
  if (IsJSBoundFunction(*callable)) {
    // Since there is a mutual recursion here, we might run out of stack
    // space for long chains of bound functions.
    STACK_CHECK(isolate, MaybeHandle<Object>());
    Handle<Object> bound_callable(
        Cast<JSBoundFunction>(callable)->bound_target_function(), isolate);
    return Object::InstanceOf(isolate, object, bound_callable);
  }

  // If {object} is not a receiver, return false.
  if (!IsJSReceiver(*object)) return isolate->factory()->false_value();

  // Get the "prototype" of {callable}; raise an error if it's not a receiver.
  Handle<Object> prototype;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, prototype,
      Object::GetProperty(isolate, callable,
                          isolate->factory()->prototype_string()));
  if (!IsJSReceiver(*prototype)) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kInstanceofNonobjectProto, prototype));
  }

  // Return whether or not {prototype} is in the prototype chain of {object}.
  Maybe<bool> result = JSReceiver::HasInPrototypeChain(
      isolate, Cast<JSReceiver>(object), prototype);
  if (result.IsNothing()) return MaybeHandle<Object>();
  return isolate->factory()->ToBoolean(result.FromJust());
}

// static
MaybeHandle<Object> Object::InstanceOf(Isolate* isolate, Handle<Object> object,
                                       Handle<Object> callable) {
  // The {callable} must be a receiver.
  if (!IsJSReceiver(*callable)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kNonObjectInInstanceOfCheck));
  }

  // Lookup the @@hasInstance method on {callable}.
  Handle<Object> inst_of_handler;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, inst_of_handler,
      Object::GetMethod(isolate, Cast<JSReceiver>(callable),
                        isolate->factory()->has_instance_symbol()));
  if (!IsUndefined(*inst_of_handler, isolate)) {
    // Call the {inst_of_handler} on the {callable}.
    Handle<Object> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result,
        Execution::Call(isolate, inst_of_handler, callable, 1, &object));
    return isolate->factory()->ToBoolean(
        Object::BooleanValue(*result, isolate));
  }

  // The {callable} must have a [[Call]] internal method.
  if (!IsCallable(*callable)) {
    THROW_NEW_ERROR(
        isolate, NewTypeError(MessageTemplate::kNonCallableInInstanceOfCheck));
  }

  // Fall back to OrdinaryHasInstance with {callable} and {object}.
  Handle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result, Object::OrdinaryHasInstance(isolate, callable, object));
  return result;
}

// static
MaybeHandle<Object> Object::GetMethod(Isolate* isolate,
                                      Handle<JSReceiver> receiver,
                                      Handle<Name> name) {
  Handle<Object> func;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, func,
                             JSReceiver::GetProperty(isolate, receiver, name));
  if (IsNullOrUndefined(*func, isolate)) {
    return isolate->factory()->undefined_value();
  }
  if (!IsCallable(*func)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kPropertyNotFunction,
                                          func, name, receiver));
  }
  return func;
}

namespace {

MaybeHandle<FixedArray> CreateListFromArrayLikeFastPath(
    Isolate* isolate, Handle<Object> object, ElementTypes element_types) {
  if (element_types == ElementTypes::kAll) {
    if (IsJSArray(*object)) {
      Handle<JSArray> array = Cast<JSArray>(object);
      uint32_t length;
      if (!array->HasArrayPrototype(isolate) ||
          !Object::ToUint32(array->length(), &length) ||
          !array->HasFastElements() ||
          !JSObject::PrototypeHasNoElements(isolate, *array)) {
        return MaybeHandle<FixedArray>();
      }
      return array->GetElementsAccessor()->CreateListFromArrayLike(
          isolate, array, length);
    } else if (IsJSTypedArray(*object)) {
      Handle<JSTypedArray> array = Cast<JSTypedArray>(object);
      size_t length = array->GetLength();
      if (array->IsDetachedOrOutOfBounds() ||
          length > static_cast<size_t>(FixedArray::kMaxLength)) {
        return MaybeHandle<FixedArray>();
      }
      static_assert(FixedArray::kMaxLength <=
                    std::numeric_limits<uint32_t>::max());
      return array->GetElementsAccessor()->CreateListFromArrayLike(
          isolate, array, static_cast<uint32_t>(length));
    }
  }
  return MaybeHandle<FixedArray>();
}

}  // namespace

// static
MaybeHandle<FixedArray> Object::CreateListFromArrayLike(
    Isolate* isolate, Handle<Object> object, ElementTypes element_types) {
  // Fast-path for JSArray and JSTypedArray.
  MaybeHandle<FixedArray> fast_result =
      CreateListFromArrayLikeFastPath(isolate, object, element_types);
  if (!fast_result.is_null()) return fast_result;
  // 1. ReturnIfAbrupt(object).
  // 2. (default elementTypes -- not applicable.)
  // 3. If Type(obj) is not Object, throw a TypeError exception.
  if (!IsJSReceiver(*object)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kCalledOnNonObject,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "CreateListFromArrayLike")));
  }

  // 4. Let len be ? ToLength(? Get(obj, "length")).
  Handle<JSReceiver> receiver = Cast<JSReceiver>(object);
  Handle<Object> raw_length_number;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, raw_length_number,
                             Object::GetLengthFromArrayLike(isolate, receiver));
  uint32_t len;
  if (!Object::ToUint32(*raw_length_number, &len) ||
      len > static_cast<uint32_t>(FixedArray::kMaxLength)) {
    THROW_NEW_ERROR(isolate,
                    NewRangeError(MessageTemplate::kInvalidArrayLength));
  }
  // 5. Let list be an empty List.
  Handle<FixedArray> list = isolate->factory()->NewFixedArray(len);
  // 6. Let index be 0.
  // 7. Repeat while index < len:
  for (uint32_t index = 0; index < len; ++index) {
    // 7a. Let indexName be ToString(index).
    // 7b. Let next be ? Get(obj, indexName).
    Handle<Object> next;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, next, JSReceiver::GetElement(isolate, receiver, index));
    switch (element_types) {
      case ElementTypes::kAll:
        // Nothing to do.
        break;
      case ElementTypes::kStringAndSymbol: {
        // 7c. If Type(next) is not an element of elementTypes, throw a
        //     TypeError exception.
        if (!IsName(*next)) {
          THROW_NEW_ERROR(
              isolate, NewTypeError(MessageTemplate::kNotPropertyName, next));
        }
        // 7d. Append next as the last element of list.
        // Internalize on the fly so we can use pointer identity later.
        next = isolate->factory()->InternalizeName(Cast<Name>(next));
        break;
      }
    }
    list->set(index, *next);
    // 7e. Set index to index + 1. (See loop header.)
  }
  // 8. Return list.
  return list;
}

// static
MaybeHandle<Object> Object::GetLengthFromArrayLike(Isolate* isolate,
                                                   Handle<JSReceiver> object) {
  Handle<Object> val;
  Handle<Name> key = isolate->factory()->length_string();
  ASSIGN_RETURN_ON_EXCEPTION(isolate, val,
                             JSReceiver::GetProperty(isolate, object, key));
  return Object::ToLength(isolate, val);
}

// static
MaybeHandle<Object> Object::GetProperty(LookupIterator* it,
                                        bool is_global_reference) {
  for (;; it->Next()) {
    switch (it->state()) {
      case LookupIterator::TRANSITION:
        UNREACHABLE();
      case LookupIterator::JSPROXY: {
        bool was_found;
        Handle<Object> receiver = it->GetReceiver();
        // In case of global IC, the receiver is the global object. Replace by
        // the global proxy.
        if (IsJSGlobalObject(*receiver)) {
          receiver = handle(Cast<JSGlobalObject>(*receiver)->global_proxy(),
                            it->isolate());
        }
        if (is_global_reference) {
          Maybe<bool> maybe = JSProxy::HasProperty(
              it->isolate(), it->GetHolder<JSProxy>(), it->GetName());
          if (maybe.IsNothing()) return {};
          if (!maybe.FromJust()) {
            it->NotFound();
            return it->isolate()->factory()->undefined_value();
          }
        }
        MaybeHandle<JSAny> result =
            JSProxy::GetProperty(it->isolate(), it->GetHolder<JSProxy>(),
                                 it->GetName(), receiver, &was_found);
        if (!was_found && !is_global_reference) it->NotFound();
        return result;
      }
      case LookupIterator::WASM_OBJECT:
        return it->isolate()->factory()->undefined_value();
      case LookupIterator::INTERCEPTOR: {
        bool done;
        Handle<JSAny> result;
        ASSIGN_RETURN_ON_EXCEPTION(
            it->isolate(), result,
            JSObject::GetPropertyWithInterceptor(it, &done));
        if (done) return result;
        continue;
      }
      case LookupIterator::ACCESS_CHECK:
        if (it->HasAccess()) continue;
        return JSObject::GetPropertyWithFailedAccessCheck(it);
      case LookupIterator::ACCESSOR:
        return GetPropertyWithAccessor(it);
      case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
        return it->isolate()->factory()->undefined_value();
      case LookupIterator::DATA:
        return it->GetDataValue();
      case LookupIterator::NOT_FOUND:
        if (it->IsPrivateName()) {
          auto private_symbol = Cast<Symbol>(it->name());
          Handle<String> name_string(
              Cast<String>(private_symbol->description()), it->isolate());
          if (private_symbol->is_private_brand()) {
            Handle<String> class_name =
                (name_string->length() == 0)
                    ? it->isolate()->factory()->anonymous_string()
                    : name_string;
            THROW_NEW_ERROR(
                it->isolate(),
                NewTypeError(MessageTemplate::kInvalidPrivateBrandInstance,
                             class_name));
          }
          THROW_NEW_ERROR(
              it->isolate(),
              NewTypeError(MessageTemplate::kInvalidPrivateMemberRead,
                           name_string));
        }

        return it->isolate()->factory()->undefined_value();
    }
    UNREACHABLE();
  }
}

// static
MaybeHandle<JSAny> JSProxy::GetProperty(Isolate* isolate,
                                        DirectHandle<JSProxy> proxy,
                                        Handle<Name> name,
                                        Handle<Object> receiver,
                                        bool* was_found) {
  *was_found = true;

  DCHECK(!name->IsPrivate());
  STACK_CHECK(isolate, kNullMaybeHandle);
  Handle<Name> trap_name = isolate->factory()->get_string();
  // 1. Assert: IsPropertyKey(P) is true.
  // 2. Let handler be the value of the [[ProxyHandler]] internal slot of O.
  Handle<Object> handler(proxy->handler(), isolate);
  // 3. If handler is null, throw a TypeError exception.
  // 4. Assert: Type(handler) is Object.
  if (proxy->IsRevoked()) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kProxyRevoked, trap_name));
  }
  // 5. Let target be the value of the [[ProxyTarget]] internal slot of O.
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  // 6. Let trap be ? GetMethod(handler, "get").
  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, trap,
      Object::GetMethod(isolate, Cast<JSReceiver>(handler), trap_name));
  // 7. If trap is undefined, then
  if (IsUndefined(*trap, isolate)) {
    // 7.a Return target.[[Get]](P, Receiver).
    PropertyKey key(isolate, name);
    LookupIterator it(isolate, receiver, key, target);
    MaybeHandle<JSAny> result = Cast<JSAny>(Object::GetProperty(&it));
    *was_found = it.IsFound();
    return result;
  }
  // 8. Let trapResult be ? Call(trap, handler, «target, P, Receiver»).
  Handle<Object> trap_result;
  Handle<Object> args[] = {target, name, receiver};
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, trap_result,
      Execution::Call(isolate, trap, handler, arraysize(args), args));

  MaybeHandle<JSAny> result =
      JSProxy::CheckGetSetTrapResult(isolate, name, target, trap_result, kGet);
  if (result.is_null()) {
    return result;
  }

  // 11. Return trap_result
  return Cast<JSAny>(trap_result);
}

// static
MaybeHandle<JSAny> JSProxy::CheckGetSetTrapResult(Isolate* isolate,
                                                  Handle<Name> name,
                                                  Handle<JSReceiver> target,
                                                  Handle<Object> trap_result,
                                                  AccessKind access_kind) {
  // 9. Let targetDesc be ? target.[[GetOwnProperty]](P).
  PropertyDescriptor target_desc;
  Maybe<bool> target_found =
      JSReceiver::GetOwnPropertyDescriptor(isolate, target, name, &target_desc);
  MAYBE_RETURN_NULL(target_found);
  // 10. If targetDesc is not undefined, then
  if (target_found.FromJust()) {
    // 10.a. If IsDataDescriptor(targetDesc) and targetDesc.[[Configurable]] is
    //       false and targetDesc.[[Writable]] is false, then
    // 10.a.i. If SameValue(trapResult, targetDesc.[[Value]]) is false,
    //        throw a TypeError exception.
    bool inconsistent = PropertyDescriptor::IsDataDescriptor(&target_desc) &&
                        !target_desc.configurable() &&
                        !target_desc.writable() &&
                        !Object::SameValue(*trap_result, *target_desc.value());
    if (inconsistent) {
      if (access_kind == kGet) {
        THROW_NEW_ERROR(
            isolate, NewTypeError(MessageTemplate::kProxyGetNonConfigurableData,
                                  name, target_desc.value(), trap_result));
      } else {
        isolate->Throw(*isolate->factory()->NewTypeError(
            MessageTemplate::kProxySetFrozenData, name));
        return {};
      }
    }
    // 10.b. If IsAccessorDescriptor(targetDesc) and targetDesc.[[Configurable]]
    //       is false and targetDesc.[[Get]] is undefined, then
    // 10.b.i. If trapResult is not undefined, throw a TypeError exception.
    if (access_kind == kGet) {
      inconsistent = PropertyDescriptor::IsAccessorDescriptor(&target_desc) &&
                     !target_desc.configurable() &&
                     IsUndefined(*target_desc.get(), isolate) &&
                     !IsUndefined(*trap_result, isolate);
    } else {
      inconsistent = PropertyDescriptor::IsAccessorDescriptor(&target_desc) &&
                     !target_desc.configurable() &&
                     IsUndefined(*target_desc.set(), isolate);
    }
    if (inconsistent) {
      if (access_kind == kGet) {
        THROW_NEW_ERROR(
            isolate,
            NewTypeError(MessageTemplate::kProxyGetNonConfigurableAccessor,
                         name, trap_result));
      } else {
        isolate->Throw(*isolate->factory()->NewTypeError(
            MessageTemplate::kProxySetFrozenAccessor, name));
        return {};
      }
    }
  }
  return isolate->factory()->undefined_value();
}

// static
bool Object::ToInt32(Tagged<Object> obj, int32_t* value) {
  if (IsSmi(obj)) {
    *value = Smi::ToInt(obj);
    return true;
  }
  if (IsHeapNumber(obj)) {
    double num = Cast<HeapNumber>(obj)->value();
    // Check range before conversion to avoid undefined behavior.
    if (num >= kMinInt && num <= kMaxInt && FastI2D(FastD2I(num)) == num) {
      *value = FastD2I(num);
      return true;
    }
  }
  return false;
}

// ES6 9.5.1
// static
MaybeHandle<HeapObject> JSProxy::GetPrototype(DirectHandle<JSProxy> proxy) {
  Isolate* isolate = proxy->GetIsolate();
  Handle<String> trap_name = isolate->factory()->getPrototypeOf_string();

  STACK_CHECK(isolate, MaybeHandle<HeapObject>());

  // 1. Let handler be the value of the [[ProxyHandler]] internal slot.
  // 2. If handler is null, throw a TypeError exception.
  // 3. Assert: Type(handler) is Object.
  // 4. Let target be the value of the [[ProxyTarget]] internal slot.
  if (proxy->IsRevoked()) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kProxyRevoked, trap_name));
  }
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  Handle<JSReceiver> handler(Cast<JSReceiver>(proxy->handler()), isolate);

  // 5. Let trap be ? GetMethod(handler, "getPrototypeOf").
  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, trap,
                             Object::GetMethod(isolate, handler, trap_name));
  // 6. If trap is undefined, then return target.[[GetPrototypeOf]]().
  if (IsUndefined(*trap, isolate)) {
    return JSReceiver::GetPrototype(isolate, target);
  }
  // 7. Let handlerProto be ? Call(trap, handler, «target»).
  Handle<Object> argv[] = {target};
  Handle<Object> handler_proto;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, handler_proto,
      Execution::Call(isolate, trap, handler, arraysize(argv), argv));
  // 8. If Type(handlerProto) is neither Object nor Null, throw a TypeError.
  if (!(IsJSReceiver(*handler_proto) || IsNull(*handler_proto, isolate))) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kProxyGetPrototypeOfInvalid));
  }
  // 9. Let extensibleTarget be ? IsExtensible(target).
  Maybe<bool> is_extensible = JSReceiver::IsExtensible(isolate, target);
  MAYBE_RETURN(is_extensible, MaybeHandle<HeapObject>());
  // 10. If extensibleTarget is true, return handlerProto.
  if (is_extensible.FromJust()) return Cast<HeapObject>(handler_proto);
  // 11. Let targetProto be ? target.[[GetPrototypeOf]]().
  Handle<HeapObject> target_proto;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, target_proto,
                             JSReceiver::GetPrototype(isolate, target));
  // 12. If SameValue(handlerProto, targetProto) is false, throw a TypeError.
  if (!Object::SameValue(*handler_proto, *target_proto)) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kProxyGetPrototypeOfNonExtensible));
  }
  // 13. Return handlerProto.
  return Cast<HeapObject>(handler_proto);
}

MaybeHandle<JSAny> Object::GetPropertyWithAccessor(LookupIterator* it) {
  Isolate* isolate = it->isolate();
  Handle<Object> structure = it->GetAccessors();
  Handle<Object> receiver = it->GetReceiver();
  // In case of global IC, the receiver is the global object. Replace by the
  // global proxy.
  if (IsJSGlobalObject(*receiver)) {
    receiver = handle(Cast<JSGlobalObject>(*receiver)->global_proxy(), isolate);
  }

  // We should never get here to initialize a const with the hole value since a
  // const declaration would conflict with the getter.
  DCHECK(!IsForeign(*structure));

  // API style callbacks.
  Handle<JSObject> holder = it->GetHolder<JSObject>();
  if (IsAccessorInfo(*structure)) {
    Handle<Name> name = it->GetName();
    auto info = Cast<AccessorInfo>(structure);

    if (!info->has_getter(isolate)) {
      return isolate->factory()->undefined_value();
    }

    if (info->is_sloppy() && !IsJSReceiver(*receiver)) {
      ASSIGN_RETURN_ON_EXCEPTION(isolate, receiver,
                                 Object::ConvertReceiver(isolate, receiver));
    }

    PropertyCallbackArguments args(isolate, info->data(), *receiver, *holder,
                                   Just(kDontThrow));
    Handle<JSAny> result = args.CallAccessorGetter(info, name);
    RETURN_EXCEPTION_IF_EXCEPTION(isolate);
    Handle<JSAny> reboxed_result = handle(*result, isolate);
    if (info->replace_on_access() && IsJSReceiver(*receiver)) {
      RETURN_ON_EXCEPTION(isolate,
                          Accessors::ReplaceAccessorWithDataProperty(
                              isolate, receiver, holder, name, result));
    }
    return reboxed_result;
  }

  auto accessor_pair = Cast<AccessorPair>(structure);
  // AccessorPair with 'cached' private property.
  if (it->TryLookupCachedProperty(accessor_pair)) {
    return Cast<JSAny>(Object::GetProperty(it));
  }

  // Regular accessor.
  Handle<Object> getter(accessor_pair->getter(), isolate);
  if (IsFunctionTemplateInfo(*getter)) {
    SaveAndSwitchContext save(isolate, holder->GetCreationContext().value());
    return Cast<JSAny>(Builtins::InvokeApiFunction(
        isolate, false, Cast<FunctionTemplateInfo>(getter), receiver, 0,
        nullptr, isolate->factory()->undefined_value()));
  } else if (IsCallable(*getter)) {
    // TODO(rossberg): nicer would be to cast to some JSCallable here...
    return Object::GetPropertyWithDefinedGetter(receiver,
                                                Cast<JSReceiver>(getter));
  }
  // Getter is not a function.
  return isolate->factory()->undefined_value();
}

Maybe<bool> Object::SetPropertyWithAccessor(
    LookupIterator* it, Handle<Object> value,
    Maybe<ShouldThrow> maybe_should_throw) {
  Isolate* isolate = it->isolate();
  Handle<Object> structure = it->GetAccessors();
  Handle<Object> receiver = it->GetReceiver();
  // In case of global IC, the receiver is the global object. Replace by the
  // global proxy.
  if (IsJSGlobalObject(*receiver)) {
    receiver = handle(Cast<JSGlobalObject>(*receiver)->global_proxy(), isolate);
  }

  // We should never get here to initialize a const with the hole value since a
  // const declaration would conflict with the setter.
  DCHECK(!IsForeign(*structure));

  // API style callbacks.
  DirectHandle<JSObject> holder = it->GetHolder<JSObject>();
  if (IsAccessorInfo(*structure)) {
    Handle<Name> name = it->GetName();
    auto info = Cast<AccessorInfo>(structure);

    if (!info->has_setter(isolate)) {
      // TODO(verwaest): We should not get here anymore once all AccessorInfos
      // are marked as special_data_property. They cannot both be writable and
      // not have a setter.
      return Just(true);
    }

    if (info->is_sloppy() && !IsJSReceiver(*receiver)) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, receiver, Object::ConvertReceiver(isolate, receiver),
          Nothing<bool>());
    }

    PropertyCallbackArguments args(isolate, info->data(), *receiver, *holder,
                                   maybe_should_throw);
    bool result = args.CallAccessorSetter(info, name, value);
    RETURN_VALUE_IF_EXCEPTION(isolate, Nothing<bool>());
    // Ensure the setter callback respects the "should throw" value - it's
    // allowed to fail without throwing only in case of kDontThrow.
    DCHECK_IMPLIES(!result,
                   GetShouldThrow(isolate, maybe_should_throw) == kDontThrow);
    return Just(result);
  }

  // Regular accessor.
  Handle<Object> setter(Cast<AccessorPair>(*structure)->setter(), isolate);
  if (IsFunctionTemplateInfo(*setter)) {
    SaveAndSwitchContext save(isolate, holder->GetCreationContext().value());
    Handle<Object> argv[] = {value};
    RETURN_ON_EXCEPTION_VALUE(
        isolate,
        Builtins::InvokeApiFunction(
            isolate, false, Cast<FunctionTemplateInfo>(setter), receiver,
            arraysize(argv), argv, isolate->factory()->undefined_value()),
        Nothing<bool>());
    return Just(true);
  } else if (IsCallable(*setter)) {
    // TODO(rossberg): nicer would be to cast to some JSCallable here...
    return SetPropertyWithDefinedSetter(receiver, Cast<JSReceiver>(setter),
                                        value, maybe_should_throw);
  }

  RETURN_FAILURE(isolate, GetShouldThrow(isolate, maybe_should_throw),
                 NewTypeError(MessageTemplate::kNoSetterInCallback,
                              it->GetName(), it->GetHolder<JSObject>()));
}

MaybeHandle<JSAny> Object::GetPropertyWithDefinedGetter(
    Handle<Object> receiver, Handle<JSReceiver> getter) {
  Isolate* isolate = getter->GetIsolate();

  // Platforms with simulators like arm/arm64 expose a funny issue. If the
  // simulator has a separate JS stack pointer from the C++ stack pointer, it
  // can miss C++ stack overflows in the stack guard at the start of JavaScript
  // functions. It would be very expensive to check the C++ stack pointer at
  // that location. The best solution seems to be to break the impasse by
  // adding checks at possible recursion points. What's more, we don't put
  // this stack check behind the USE_SIMULATOR define in order to keep
  // behavior the same between hardware and simulators.
  StackLimitCheck check(isolate);
  if (check.JsHasOverflowed()) {
    isolate->StackOverflow();
    return kNullMaybeHandle;
  }

  return Cast<JSAny>(Execution::Call(isolate, getter, receiver, 0, nullptr));
}

Maybe<bool> Object::SetPropertyWithDefinedSetter(
    Handle<Object> receiver, Handle<JSReceiver> setter, Handle<Object> value,
    Maybe<ShouldThrow> should_throw) {
  Isolate* isolate = setter->GetIsolate();

  Handle<Object> argv[] = {value};
  RETURN_ON_EXCEPTION_VALUE(
      isolate,
      Execution::Call(isolate, setter, receiver, arraysize(argv), argv),
      Nothing<bool>());
  return Just(true);
}

// static
Tagged<Map> Object::GetPrototypeChainRootMap(Tagged<Object> obj,
                                             Isolate* isolate) {
  DisallowGarbageCollection no_alloc;
  if (IsSmi(obj)) {
    Tagged<Context> native_context = isolate->context()->native_context();
    return native_context->number_function()->initial_map();
  }

  const Tagged<HeapObject> heap_object = Cast<HeapObject>(obj);
  return heap_object->map()->GetPrototypeChainRootMap(isolate);
}

// static
Tagged<Smi> Object::GetOrCreateHash(Tagged<Object> obj, Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  Tagged<Object> hash = Object::GetSimpleHash(obj);
  if (IsSmi(hash)) return Cast<Smi>(hash);

  DCHECK(IsJSReceiver(obj));
  return Cast<JSReceiver>(obj)->GetOrCreateIdentityHash(isolate);
}

// static
bool Object::SameValue(Tagged<Object> obj, Tagged<Object> other) {
  if (other == obj) return true;

  if (IsNumber(obj) && IsNumber(other)) {
    return SameNumberValue(Object::NumberValue(Cast<Number>(obj)),
                           Object::NumberValue(Cast<Number>(other)));
  }
  if (IsString(obj) && IsString(other)) {
    return Cast<String>(obj)->Equals(Cast<String>(other));
  }
  if (IsBigInt(obj) && IsBigInt(other)) {
    return BigInt::EqualToBigInt(Cast<BigInt>(obj), Cast<BigInt>(other));
  }
  return false;
}

// static
bool Object::SameValueZero(Tagged<Object> obj, Tagged<Object> other) {
  if (other == obj) return true;

  if (IsNumber(obj) && IsNumber(other)) {
    double this_value = Object::NumberValue(Cast<Number>(obj));
    double other_value = Object::NumberValue(Cast<Number>(other));
    // +0 == -0 is true
    return this_value == other_value ||
           (std::isnan(this_value) && std::isnan(other_value));
  }
  if (IsString(obj) && IsString(other)) {
    return Cast<String>(obj)->Equals(Cast<String>(other));
  }
  if (IsBigInt(obj) && IsBigInt(other)) {
    return BigInt::EqualToBigInt(Cast<BigInt>(obj), Cast<BigInt>(other));
  }
  return false;
}

MaybeHandle<Object> Object::ArraySpeciesConstructor(
    Isolate* isolate, Handle<Object> original_array) {
  Handle<Object> default_species = isolate->array_function();
  if (!v8_flags.builtin_subclassing) return default_species;
  if (IsJSArray(*original_array) &&
      Cast<JSArray>(original_array)->HasArrayPrototype(isolate) &&
      Protectors::IsArraySpeciesLookupChainIntact(isolate)) {
    return default_species;
  }
  Handle<Object> constructor = isolate->factory()->undefined_value();
  Maybe<bool> is_array = IsArray(original_array);
  MAYBE_RETURN_NULL(is_array);
  if (is_array.FromJust()) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, constructor,
        Object::GetProperty(isolate, original_array,
                            isolate->factory()->constructor_string()));
    if (IsConstructor(*constructor)) {
      Handle<NativeContext> constructor_context;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, constructor_context,
          JSReceiver::GetFunctionRealm(Cast<JSReceiver>(constructor)));
      if (*constructor_context != *isolate->native_context() &&
          *constructor == constructor_context->array_function()) {
        constructor = isolate->factory()->undefined_value();
      }
    }
    if (IsJSReceiver(*constructor)) {
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, constructor,
          JSReceiver::GetProperty(isolate, Cast<JSReceiver>(constructor),
                                  isolate->factory()->species_symbol()));
      if (IsNull(*constructor, isolate)) {
        constructor = isolate->factory()->undefined_value();
      }
    }
  }
  if (IsUndefined(*constructor, isolate)) {
    return default_species;
  } else {
    if (!IsConstructor(*constructor)) {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kSpeciesNotConstructor));
    }
    return constructor;
  }
}

// ES6 section 7.3.20 SpeciesConstructor ( O, defaultConstructor )
V8_WARN_UNUSED_RESULT MaybeHandle<Object> Object::SpeciesConstructor(
    Isolate* isolate, Handle<JSReceiver> recv,
    Handle<JSFunction> default_ctor) {
  Handle<Object> ctor_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, ctor_obj,
      JSObject::GetProperty(isolate, recv,
                            isolate->factory()->constructor_string()));

  if (IsUndefined(*ctor_obj, isolate)) return default_ctor;

  if (!IsJSReceiver(*ctor_obj)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kConstructorNotReceiver));
  }

  Handle<JSReceiver> ctor = Cast<JSReceiver>(ctor_obj);

  Handle<Object> species;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, species,
      JSObject::GetProperty(isolate, ctor,
                            isolate->factory()->species_symbol()));

  if (IsNullOrUndefined(*species, isolate)) {
    return default_ctor;
  }

  if (IsConstructor(*species)) return species;

  THROW_NEW_ERROR(isolate,
                  NewTypeError(MessageTemplate::kSpeciesNotConstructor));
}

// static
bool Object::IterationHasObservableEffects(Tagged<Object> obj) {
  DisallowGarbageCollection no_gc;
  // Check that this object is an array.
  if (!IsJSArray(obj)) return true;
  Tagged<JSArray> array = Cast<JSArray>(obj);

  // Check that we have the original ArrayPrototype.
  Tagged<Object> array_proto = array->map()->prototype();
  if (!IsJSObject(array_proto)) return true;
  Tagged<NativeContext> native_context = array->GetCreationContext().value();
  auto initial_array_prototype = native_context->initial_array_prototype();
  if (initial_array_prototype != array_proto) return true;

  Isolate* isolate = array->GetIsolate();
  // Check that the ArrayPrototype hasn't been modified in a way that would
  // affect iteration.
  if (!Protectors::IsArrayIteratorLookupChainIntact(isolate)) return true;

  // For FastPacked kinds, iteration will have the same effect as simply
  // accessing each property in order.
  ElementsKind array_kind = array->GetElementsKind();
  if (IsFastPackedElementsKind(array_kind)) return false;

  // For FastHoley kinds, an element access on a hole would cause a lookup on
  // the prototype. This could have different results if the prototype has been
  // changed.
  if (IsHoleyElementsKind(array_kind) &&
      Protectors::IsNoElementsIntact(isolate)) {
    return false;
  }
  return true;
}

// static
bool Object::IsCodeLike(Tagged<Object> obj, Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  return IsJSReceiver(obj) && Cast<JSReceiver>(obj)->IsCodeLike(isolate);
}

void ShortPrint(Tagged<Object> obj, FILE* out) {
  OFStream os(out);
  os << Brief(obj);
}

void ShortPrint(Tagged<Object> obj, StringStream* accumulator) {
  std::ostringstream os;
  os << Brief(obj);
  accumulator->Add(os.str().c_str());
}

void ShortPrint(Tagged<Object> obj, std::ostream& os) { os << Brief(obj); }

std::ostream& operator<<(std::ostream& os, Tagged<Object> obj) {
  ShortPrint(obj, os);
  return os;
}

std::ostream& operator<<(std::ostream& os, Object::Conversion kind) {
  switch (kind) {
    case Object::Conversion::kToNumber:
      return os << "ToNumber";
    case Object::Conversion::kToNumeric:
      return os << "ToNumeric";
  }
}

std::ostream& operator<<(std::ostream& os, const Brief& v) {
  Tagged<MaybeObject> maybe_object(v.value);
  Tagged<Smi> smi;
  Tagged<HeapObject> heap_object;
  if (maybe_object.ToSmi(&smi)) {
    Smi::SmiPrint(smi, os);
  } else if (maybe_object.IsCleared()) {
    os << "[cleared]";
  } else if (maybe_object.GetHeapObjectIfWeak(&heap_object)) {
    os << "[weak] ";
    heap_object->HeapObjectShortPrint(os);
  } else if (maybe_object.GetHeapObjectIfStrong(&heap_object)) {
    heap_object->HeapObjectShortPrint(os);
  } else {
    UNREACHABLE();
  }
  return os;
}

// static
void Smi::SmiPrint(Tagged<Smi> smi, std::ostream& os) { os << smi.value(); }

void Struct::BriefPrintDetails(std::ostream& os) {}

void Tuple2::BriefPrintDetails(std::ostream& os) {
  os << " " << Brief(value1()) << ", " << Brief(value2());
}

void MegaDomHandler::BriefPrintDetails(std::ostream& os) {
  os << " " << Brief(accessor(kAcquireLoad)) << ", " << Brief(context());
}

void ClassPositions::BriefPrintDetails(std::ostream& os) {
  os << " " << start() << ", " << end();
}

void CallableTask::BriefPrintDetails(std::ostream& os) {
  os << " callable=" << Brief(callable());
}

void HeapObject::Iterate(PtrComprCageBase cage_base, ObjectVisitor* v) {
  IterateFast<ObjectVisitor>(cage_base, v);
}

void HeapObject::IterateBody(PtrComprCageBase cage_base, ObjectVisitor* v) {
  Tagged<Map> m = map(cage_base);
  IterateBodyFast<ObjectVisitor>(m, SizeFromMap(m), v);
}

void HeapObject::IterateBody(Tagged<Map> map, int object_size,
                             ObjectVisitor* v) {
  IterateBodyFast<ObjectVisitor>(map, object_size, v);
}

int HeapObjectLayout::SizeFromMap(Tagged<Map> map) const {
  return Tagged<HeapObject>(this)->SizeFromMap(map);
}

int HeapObject::SizeFromMap(Tagged<Map> map) const {
  int instance_size = map->instance_size();
  if (instance_size != kVariableSizeSentinel) return instance_size;
  // Only inline the most frequent cases.
  InstanceType instance_type = map->instance_type();
  if (base::IsInRange(instance_type, FIRST_FIXED_ARRAY_TYPE,
                      LAST_FIXED_ARRAY_TYPE)) {
    return UncheckedCast<FixedArray>(*this)->AllocatedSize();
  }
#define CASE(TypeCamelCase, TYPE_UPPER_CASE)                     \
  if (instance_type == TYPE_UPPER_CASE##_TYPE) {                 \
    return UncheckedCast<TypeCamelCase>(*this)->AllocatedSize(); \
  }
  SIMPLE_HEAP_OBJECT_LIST2(CASE)
#undef CASE
  if (instance_type == SLOPPY_ARGUMENTS_ELEMENTS_TYPE) {
    return UncheckedCast<SloppyArgumentsElements>(*this)->AllocatedSize();
  }
  if (base::IsInRange(instance_type, FIRST_CONTEXT_TYPE, LAST_CONTEXT_TYPE)) {
    if (instance_type == NATIVE_CONTEXT_TYPE) return NativeContext::kSize;
    return Context::SizeFor(UncheckedCast<Context>(*this)->length());
  }
  if (instance_type == SEQ_ONE_BYTE_STRING_TYPE ||
      instance_type == INTERNALIZED_ONE_BYTE_STRING_TYPE ||
      instance_type == SHARED_SEQ_ONE_BYTE_STRING_TYPE) {
    // Strings may get concurrently truncated, hence we have to access its
    // length synchronized.
    return SeqOneByteString::SizeFor(
        UncheckedCast<SeqOneByteString>(*this)->length(kAcquireLoad));
  }
  if (instance_type == BYTECODE_ARRAY_TYPE) {
    return BytecodeArray::SizeFor(
        UncheckedCast<BytecodeArray>(*this)->length(kAcquireLoad));
  }
  if (instance_type == EXTERNAL_POINTER_ARRAY_TYPE) {
    return ExternalPointerArray::SizeFor(
        UncheckedCast<ExternalPointerArray>(*this)->length(kAcquireLoad));
  }
  if (instance_type == FREE_SPACE_TYPE) {
    return UncheckedCast<FreeSpace>(*this)->size(kRelaxedLoad);
  }
  if (instance_type == SEQ_TWO_BYTE_STRING_TYPE ||
      instance_type == INTERNALIZED_TWO_BYTE_STRING_TYPE ||
      instance_type == SHARED_SEQ_TWO_BYTE_STRING_TYPE) {
    // Strings may get concurrently truncated, hence we have to access its
    // length synchronized.
    return SeqTwoByteString::SizeFor(
        UncheckedCast<SeqTwoByteString>(*this)->length(kAcquireLoad));
  }
  if (instance_type == FIXED_DOUBLE_ARRAY_TYPE) {
    return UncheckedCast<FixedDoubleArray>(*this)->AllocatedSize();
  }
  if (instance_type == TRUSTED_FIXED_ARRAY_TYPE) {
    return UncheckedCast<TrustedFixedArray>(*this)->AllocatedSize();
  }
  if (instance_type == PROTECTED_FIXED_ARRAY_TYPE) {
    return UncheckedCast<ProtectedFixedArray>(*this)->AllocatedSize();
  }
  if (instance_type == TRUSTED_WEAK_FIXED_ARRAY_TYPE) {
    return UncheckedCast<TrustedWeakFixedArray>(*this)->AllocatedSize();
  }
  if (instance_type == TRUSTED_BYTE_ARRAY_TYPE) {
    return UncheckedCast<TrustedByteArray>(*this)->AllocatedSize();
  }
  if (instance_type == FEEDBACK_METADATA_TYPE) {
    return UncheckedCast<FeedbackMetadata>(*this)->AllocatedSize();
  }
  if (base::IsInRange(instance_type, FIRST_DESCRIPTOR_ARRAY_TYPE,
                      LAST_DESCRIPTOR_ARRAY_TYPE)) {
    return DescriptorArray::SizeFor(
        UncheckedCast<DescriptorArray>(*this)->number_of_all_descriptors());
  }
  if (base::IsInRange(instance_type, FIRST_WEAK_FIXED_ARRAY_TYPE,
                      LAST_WEAK_FIXED_ARRAY_TYPE)) {
    return UncheckedCast<WeakFixedArray>(*this)->AllocatedSize();
  }
  if (instance_type == WEAK_ARRAY_LIST_TYPE) {
    return WeakArrayList::SizeForCapacity(
        UncheckedCast<WeakArrayList>(*this)->capacity());
  }
  if (instance_type == SMALL_ORDERED_HASH_SET_TYPE) {
    return SmallOrderedHashSet::SizeFor(
        UncheckedCast<SmallOrderedHashSet>(*this)->Capacity());
  }
  if (instance_type == SMALL_ORDERED_HASH_MAP_TYPE) {
    return SmallOrderedHashMap::SizeFor(
        UncheckedCast<SmallOrderedHashMap>(*this)->Capacity());
  }
  if (instance_type == SMALL_ORDERED_NAME_DICTIONARY_TYPE) {
    return SmallOrderedNameDictionary::SizeFor(
        UncheckedCast<SmallOrderedNameDictionary>(*this)->Capacity());
  }
  if (instance_type == SWISS_NAME_DICTIONARY_TYPE) {
    return SwissNameDictionary::SizeFor(
        UncheckedCast<SwissNameDictionary>(*this)->Capacity());
  }
  if (instance_type == PROPERTY_ARRAY_TYPE) {
    return PropertyArray::SizeFor(
        UncheckedCast<PropertyArray>(*this)->length(kAcquireLoad));
  }
  if (instance_type == FEEDBACK_VECTOR_TYPE) {
    return FeedbackVector::SizeFor(
        UncheckedCast<FeedbackVector>(*this)->length());
  }
  if (instance_type == BIGINT_TYPE) {
    return BigInt::SizeFor(UncheckedCast<BigInt>(*this)->length());
  }
  if (instance_type == PREPARSE_DATA_TYPE) {
    Tagged<PreparseData> data = UncheckedCast<PreparseData>(*this);
    return PreparseData::SizeFor(data->data_length(), data->children_length());
  }
#define MAKE_TORQUE_SIZE_FOR(TYPE, TypeName)                \
  if (instance_type == TYPE) {                              \
    return UncheckedCast<TypeName>(*this)->AllocatedSize(); \
  }
  TORQUE_INSTANCE_TYPE_TO_BODY_DESCRIPTOR_LIST(MAKE_TORQUE_SIZE_FOR)
#undef MAKE_TORQUE_SIZE_FOR

  if (instance_type == INSTRUCTION_STREAM_TYPE) {
    return UncheckedCast<InstructionStream>(*this)->Size();
  }
  if (instance_type == COVERAGE_INFO_TYPE) {
    return CoverageInfo::SizeFor(
        UncheckedCast<CoverageInfo>(*this)->slot_count());
  }
#if V8_ENABLE_WEBASSEMBLY
  if (instance_type == WASM_TYPE_INFO_TYPE) {
    return WasmTypeInfo::SizeFor(
        UncheckedCast<WasmTypeInfo>(*this)->supertypes_length());
  }
  if (instance_type == WASM_STRUCT_TYPE) {
    return WasmStruct::GcSafeSize(map);
  }
  if (instance_type == WASM_ARRAY_TYPE) {
    return WasmArray::SizeFor(map, UncheckedCast<WasmArray>(*this)->length());
  }
  if (instance_type == WASM_NULL_TYPE) {
    return WasmNull::kSize;
  }
  if (instance_type == WASM_DISPATCH_TABLE_TYPE) {
    return WasmDispatchTable::SizeFor(
        UncheckedCast<WasmDispatchTable>(*this)->capacity());
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  DCHECK_EQ(instance_type, EMBEDDER_DATA_ARRAY_TYPE);
  return EmbedderDataArray::SizeFor(
      UncheckedCast<EmbedderDataArray>(*this)->length());
}

bool HeapObject::NeedsRehashing(PtrComprCageBase cage_base) const {
  return NeedsRehashing(map(cage_base)->instance_type());
}

bool HeapObject::NeedsRehashing(InstanceType instance_type) const {
  if (V8_EXTERNAL_CODE_SPACE_BOOL) {
    // Use map() only when it's guaranteed that it's not a InstructionStream
    // object.
    DCHECK_IMPLIES(instance_type != INSTRUCTION_STREAM_TYPE,
                   instance_type == map()->instance_type());
  } else {
    DCHECK_EQ(instance_type, map()->instance_type());
  }
  switch (instance_type) {
    case DESCRIPTOR_ARRAY_TYPE:
    case STRONG_DESCRIPTOR_ARRAY_TYPE:
      return Cast<DescriptorArray>(*this)->number_of_descriptors() > 1;
    case TRANSITION_ARRAY_TYPE:
      return Cast<TransitionArray>(*this)->number_of_entries() > 1;
    case ORDERED_HASH_MAP_TYPE:
    case ORDERED_HASH_SET_TYPE:
      return false;  // We'll rehash from the JSMap or JSSet referencing them.
    case NAME_DICTIONARY_TYPE:
    case NAME_TO_INDEX_HASH_TABLE_TYPE:
    case REGISTERED_SYMBOL_TABLE_TYPE:
    case GLOBAL_DICTIONARY_TYPE:
    case NUMBER_DICTIONARY_TYPE:
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
    case HASH_TABLE_TYPE:
    case SMALL_ORDERED_HASH_MAP_TYPE:
    case SMALL_ORDERED_HASH_SET_TYPE:
    case SMALL_ORDERED_NAME_DICTIONARY_TYPE:
    case SWISS_NAME_DICTIONARY_TYPE:
    case JS_MAP_TYPE:
    case JS_SET_TYPE:
      return true;
    default:
      return false;
  }
}

bool HeapObject::CanBeRehashed(PtrComprCageBase cage_base) const {
  DCHECK(NeedsRehashing(cage_base));
  switch (map(cage_base)->instance_type()) {
    case JS_MAP_TYPE:
    case JS_SET_TYPE:
      return true;
    case ORDERED_HASH_MAP_TYPE:
    case ORDERED_HASH_SET_TYPE:
      UNREACHABLE();  // We'll rehash from the JSMap or JSSet referencing them.
    case ORDERED_NAME_DICTIONARY_TYPE:
      return false;
    case NAME_DICTIONARY_TYPE:
    case NAME_TO_INDEX_HASH_TABLE_TYPE:
    case REGISTERED_SYMBOL_TABLE_TYPE:
    case GLOBAL_DICTIONARY_TYPE:
    case NUMBER_DICTIONARY_TYPE:
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
    case SWISS_NAME_DICTIONARY_TYPE:
      return true;
    case DESCRIPTOR_ARRAY_TYPE:
    case STRONG_DESCRIPTOR_ARRAY_TYPE:
      return true;
    case TRANSITION_ARRAY_TYPE:
      return true;
    case SMALL_ORDERED_HASH_MAP_TYPE:
      return Cast<SmallOrderedHashMap>(*this)->NumberOfElements() == 0;
    case SMALL_ORDERED_HASH_SET_TYPE:
      return Cast<SmallOrderedHashMap>(*this)->NumberOfElements() == 0;
    case SMALL_ORDERED_NAME_DICTIONARY_TYPE:
      return Cast<SmallOrderedNameDictionary>(*this)->NumberOfElements() == 0;
    default:
      return false;
  }
}

template <typename IsolateT>
void HeapObject::RehashBasedOnMap(IsolateT* isolate) {
  switch (map(isolate)->instance_type()) {
    case HASH_TABLE_TYPE:
      Cast<ObjectHashTable>(*this)->Rehash(isolate);
      break;
    case NAME_DICTIONARY_TYPE:
      Cast<NameDictionary>(*this)->Rehash(isolate);
      break;
    case NAME_TO_INDEX_HASH_TABLE_TYPE:
      Cast<NameToIndexHashTable>(*this)->Rehash(isolate);
      break;
    case REGISTERED_SYMBOL_TABLE_TYPE:
      Cast<RegisteredSymbolTable>(*this)->Rehash(isolate);
      break;
    case SWISS_NAME_DICTIONARY_TYPE:
      Cast<SwissNameDictionary>(*this)->Rehash(isolate);
      break;
    case GLOBAL_DICTIONARY_TYPE:
      Cast<GlobalDictionary>(*this)->Rehash(isolate);
      break;
    case NUMBER_DICTIONARY_TYPE:
      Cast<NumberDictionary>(*this)->Rehash(isolate);
      break;
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
      Cast<SimpleNumberDictionary>(*this)->Rehash(isolate);
      break;
    case DESCRIPTOR_ARRAY_TYPE:
    case STRONG_DESCRIPTOR_ARRAY_TYPE:
      DCHECK_LE(1, Cast<DescriptorArray>(*this)->number_of_descriptors());
      Cast<DescriptorArray>(*this)->Sort();
      break;
    case TRANSITION_ARRAY_TYPE:
      Cast<TransitionArray>(*this)->Sort();
      break;
    case SMALL_ORDERED_HASH_MAP_TYPE:
      DCHECK_EQ(0, Cast<SmallOrderedHashMap>(*this)->NumberOfElements());
      break;
    case SMALL_ORDERED_HASH_SET_TYPE:
      DCHECK_EQ(0, Cast<SmallOrderedHashSet>(*this)->NumberOfElements());
      break;
    case ORDERED_HASH_MAP_TYPE:
    case ORDERED_HASH_SET_TYPE:
      UNREACHABLE();  // We'll rehash from the JSMap or JSSet referencing them.
    case JS_MAP_TYPE: {
      Cast<JSMap>(*this)->Rehash(isolate->AsIsolate());
      break;
    }
    case JS_SET_TYPE: {
      Cast<JSSet>(*this)->Rehash(isolate->AsIsolate());
      break;
    }
    case SMALL_ORDERED_NAME_DICTIONARY_TYPE:
      DCHECK_EQ(0, Cast<SmallOrderedNameDictionary>(*this)->NumberOfElements());
      break;
    case INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case INTERNALIZED_TWO_BYTE_STRING_TYPE:
      // Rare case, rehash read-only space strings before they are sealed.
      DCHECK(ReadOnlyHeap::Contains(*this));
      Cast<String>(*this)->EnsureHash();
      break;
    default:
      // TODO(ishell): remove once b/326043780 is no longer an issue.
      isolate->AsIsolate()->PushParamsAndDie(
          reinterpret_cast<void*>(ptr()), reinterpret_cast<void*>(map().ptr()),
          reinterpret_cast<void*>(
              static_cast<uintptr_t>(map()->instance_type())));
      UNREACHABLE();
  }
}
template void HeapObject::RehashBasedOnMap(Isolate* isolate);
template void HeapObject::RehashBasedOnMap(LocalIsolate* isolate);

void DescriptorArray::GeneralizeAllFields(bool clear_constness) {
  int length = number_of_descriptors();
  for (InternalIndex i : InternalIndex::Range(length)) {
    PropertyDetails details = GetDetails(i);
    details = details.CopyWithRepresentation(Representation::Tagged());
    if (details.location() == PropertyLocation::kField) {
      // Since constness is not propagated across proto transitions we must
      // clear the flag here.
      if (clear_constness) {
        details = details.CopyWithConstness(PropertyConstness::kMutable);
      }
      DCHECK_EQ(PropertyKind::kData, details.kind());
      SetValue(i, FieldType::Any());
    }
    SetDetails(i, details);
  }
}

MaybeHandle<Object> Object::SetProperty(Isolate* isolate, Handle<Object> object,
                                        Handle<Name> name, Handle<Object> value,
                                        StoreOrigin store_origin,
                                        Maybe<ShouldThrow> should_throw) {
  LookupIterator it(isolate, object, name);
  MAYBE_RETURN_NULL(SetProperty(&it, value, store_origin, should_throw));
  return value;
}

Maybe<bool> Object::SetPropertyInternal(LookupIterator* it,
                                        Handle<Object> value,
                                        Maybe<ShouldThrow> should_throw,
                                        StoreOrigin store_origin, bool* found) {
  it->UpdateProtector();
  DCHECK(it->IsFound());

  // Make sure that the top context does not change when doing callbacks or
  // interceptor calls.
  AssertNoContextChange ncc(it->isolate());

  for (;; it->Next()) {
    switch (it->state()) {
      case LookupIterator::ACCESS_CHECK:
        if (it->HasAccess()) continue;
        // Check whether it makes sense to reuse the lookup iterator. Here it
        // might still call into setters up the prototype chain.
        return JSObject::SetPropertyWithFailedAccessCheck(it, value,
                                                          should_throw);

      case LookupIterator::JSPROXY: {
        Handle<Object> receiver = it->GetReceiver();
        // In case of global IC, the receiver is the global object. Replace by
        // the global proxy.
        if (IsJSGlobalObject(*receiver)) {
          receiver = handle(Cast<JSGlobalObject>(*receiver)->global_proxy(),
                            it->isolate());
        }
        return JSProxy::SetProperty(it->GetHolder<JSProxy>(), it->GetName(),
                                    value, receiver, should_throw);
      }

      case LookupIterator::WASM_OBJECT:
        RETURN_FAILURE(it->isolate(), kThrowOnError,
                       NewTypeError(MessageTemplate::kWasmObjectsAreOpaque));

      case LookupIterator::INTERCEPTOR: {
        if (it->HolderIsReceiverOrHiddenPrototype()) {
          InterceptorResult result;
          if (!JSObject::SetPropertyWithInterceptor(it, should_throw, value)
                   .To(&result)) {
            // An exception was thrown in the interceptor. Propagate.
            return Nothing<bool>();
          }
          switch (result) {
            case InterceptorResult::kFalse:
              return Just(false);
            case InterceptorResult::kTrue:
              return Just(true);
            case InterceptorResult::kNotIntercepted:
              // Proceed lookup.
              break;
          }
          // Assuming that the callback has side effects, we use
          // Object::SetSuperProperty() which works properly regardless on
          // whether the property was present on the receiver or not when
          // storing to the receiver.
          // Proceed lookup from the next state.
          it->Next();
        } else {
          Maybe<PropertyAttributes> maybe_attributes =
              JSObject::GetPropertyAttributesWithInterceptor(it);
          if (maybe_attributes.IsNothing()) return Nothing<bool>();
          if ((maybe_attributes.FromJust() & READ_ONLY) != 0) {
            return WriteToReadOnlyProperty(it, value, should_throw);
          }
          // At this point we might have called interceptor's query or getter
          // callback. Assuming that the callbacks have side effects, we use
          // Object::SetSuperProperty() which works properly regardless on
          // whether the property was present on the receiver or not when
          // storing to the receiver.
          if (maybe_attributes.FromJust() == ABSENT) {
            // Proceed lookup from the next state.
            it->Next();
          } else {
            // Finish lookup in order to make Object::SetSuperProperty() store
            // property to the receiver.
            it->NotFound();
          }
        }
        return Object::SetSuperProperty(it, value, store_origin, should_throw);
      }

      case LookupIterator::ACCESSOR: {
        if (it->IsReadOnly()) {
          return WriteToReadOnlyProperty(it, value, should_throw);
        }
        DirectHandle<Object> accessors = it->GetAccessors();
        if (IsAccessorInfo(*accessors) &&
            !it->HolderIsReceiverOrHiddenPrototype()) {
          *found = false;
          return Nothing<bool>();
        }
        return SetPropertyWithAccessor(it, value, should_throw);
      }
      case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND: {
        // IntegerIndexedElementSet converts value to a Number/BigInt prior to
        // the bounds check. The bounds check has already happened here, but
        // perform the possibly effectful ToNumber (or ToBigInt) operation
        // anyways.
        DirectHandle<JSTypedArray> holder = it->GetHolder<JSTypedArray>();
        Handle<Object> throwaway_value;
        if (holder->type() == kExternalBigInt64Array ||
            holder->type() == kExternalBigUint64Array) {
          ASSIGN_RETURN_ON_EXCEPTION_VALUE(
              it->isolate(), throwaway_value,
              BigInt::FromObject(it->isolate(), value), Nothing<bool>());
        } else {
          ASSIGN_RETURN_ON_EXCEPTION_VALUE(
              it->isolate(), throwaway_value,
              Object::ToNumber(it->isolate(), value), Nothing<bool>());
        }

        // FIXME: Throw a TypeError if the holder is detached here
        // (IntegerIndexedElementSpec step 5).

        // TODO(verwaest): Per spec, we should return false here (steps 6-9
        // in IntegerIndexedElementSpec), resulting in an exception being thrown
        // on OOB accesses in strict code. Historically, v8 has not done made
        // this change due to uncertainty about web compat. (v8:4901)
        return Just(true);
      }

      case LookupIterator::DATA:
        if (it->IsReadOnly()) {
          return WriteToReadOnlyProperty(it, value, should_throw);
        }
        if (it->HolderIsReceiverOrHiddenPrototype()) {
          return SetDataProperty(it, value);
        }
        [[fallthrough]];
      case LookupIterator::NOT_FOUND:
      case LookupIterator::TRANSITION:
        *found = false;
        return Nothing<bool>();
    }
    UNREACHABLE();
  }
}

bool Object::CheckContextualStoreToJSGlobalObject(
    LookupIterator* it, Maybe<ShouldThrow> should_throw) {
  Isolate* isolate = it->isolate();

  if (IsJSGlobalObject(*it->GetReceiver(), isolate) &&
      (GetShouldThrow(isolate, should_throw) == ShouldThrow::kThrowOnError)) {
    if (it->state() == LookupIterator::TRANSITION) {
      // The property cell that we have created is garbage because we are going
      // to throw now instead of putting it into the global dictionary. However,
      // the cell might already have been stored into the feedback vector, so
      // we must invalidate it nevertheless.
      it->transition_cell()->ClearAndInvalidate(ReadOnlyRoots(isolate));
    }
    isolate->Throw(*isolate->factory()->NewReferenceError(
        MessageTemplate::kNotDefined, it->GetName()));
    return false;
  }
  return true;
}

Maybe<bool> Object::SetProperty(LookupIterator* it, Handle<Object> value,
                                StoreOrigin store_origin,
                                Maybe<ShouldThrow> should_throw) {
  if (it->IsFound()) {
    bool found = true;
    Maybe<bool> result =
        SetPropertyInternal(it, value, should_throw, store_origin, &found);
    if (found) return result;
  }

  if (!CheckContextualStoreToJSGlobalObject(it, should_throw)) {
    return Nothing<bool>();
  }
  return AddDataProperty(it, value, NONE, should_throw, store_origin);
}

Maybe<bool> Object::SetSuperProperty(LookupIterator* it, Handle<Object> value,
                                     StoreOrigin store_origin,
                                     Maybe<ShouldThrow> should_throw) {
  Isolate* isolate = it->isolate();

  if (it->IsFound()) {
    bool found = true;
    Maybe<bool> result =
        SetPropertyInternal(it, value, should_throw, store_origin, &found);
    if (found) return result;
  }

  it->UpdateProtector();

  // The property either doesn't exist on the holder or exists there as a data
  // property.

  if (!IsJSReceiver(*it->GetReceiver())) {
    return WriteToReadOnlyProperty(it, value, should_throw);
  }
  Handle<JSReceiver> receiver = Cast<JSReceiver>(it->GetReceiver());

  // Note, the callers rely on the fact that this code is redoing the full own
  // lookup from scratch.
  LookupIterator own_lookup(isolate, receiver, it->GetKey(),
                            LookupIterator::OWN);
  for (;; own_lookup.Next()) {
    switch (own_lookup.state()) {
      case LookupIterator::ACCESS_CHECK:
        if (!own_lookup.HasAccess()) {
          return JSObject::SetPropertyWithFailedAccessCheck(&own_lookup, value,
                                                            should_throw);
        }
        continue;

      case LookupIterator::ACCESSOR:
        if (IsAccessorInfo(*own_lookup.GetAccessors())) {
          if (own_lookup.IsReadOnly()) {
            return WriteToReadOnlyProperty(&own_lookup, value, should_throw);
          }
          return Object::SetPropertyWithAccessor(&own_lookup, value,
                                                 should_throw);
        }
        [[fallthrough]];
      case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
        return RedefineIncompatibleProperty(isolate, it->GetName(), value,
                                            should_throw);

      case LookupIterator::DATA: {
        if (own_lookup.IsReadOnly()) {
          return WriteToReadOnlyProperty(&own_lookup, value, should_throw);
        }
        return SetDataProperty(&own_lookup, value);
      }

      case LookupIterator::INTERCEPTOR:
      case LookupIterator::JSPROXY: {
        PropertyDescriptor desc;
        Maybe<bool> owned =
            JSReceiver::GetOwnPropertyDescriptor(&own_lookup, &desc);
        MAYBE_RETURN(owned, Nothing<bool>());
        if (!owned.FromJust()) {
          // |own_lookup| might become outdated at this point anyway.
          // TODO(leszeks): Remove this restart since we don't really use the
          // lookup iterator after this.
          own_lookup.Restart();
          if (!CheckContextualStoreToJSGlobalObject(&own_lookup,
                                                    should_throw)) {
            return Nothing<bool>();
          }
          return JSReceiver::CreateDataProperty(isolate, receiver, it->GetKey(),
                                                value, should_throw);
        }
        if (PropertyDescriptor::IsAccessorDescriptor(&desc) ||
            !desc.writable()) {
          return RedefineIncompatibleProperty(isolate, it->GetName(), value,
                                              should_throw);
        }

        PropertyDescriptor value_desc;
        value_desc.set_value(Cast<JSAny>(value));
        return JSReceiver::DefineOwnProperty(isolate, receiver, it->GetName(),
                                             &value_desc, should_throw);
      }

      case LookupIterator::NOT_FOUND:
        if (!CheckContextualStoreToJSGlobalObject(&own_lookup, should_throw)) {
          return Nothing<bool>();
        }
        return AddDataProperty(&own_lookup, value, NONE, should_throw,
                               store_origin);

      case LookupIterator::WASM_OBJECT:
        RETURN_FAILURE(it->isolate(), kThrowOnError,
                       NewTypeError(MessageTemplate::kWasmObjectsAreOpaque));

      case LookupIterator::TRANSITION:
        UNREACHABLE();
    }
    UNREACHABLE();
  }
}

Maybe<bool> Object::CannotCreateProperty(Isolate* isolate,
                                         Handle<Object> receiver,
                                         Handle<Object> name,
                                         DirectHandle<Object> value,
                                         Maybe<ShouldThrow> should_throw) {
  RETURN_FAILURE(
      isolate, GetShouldThrow(isolate, should_throw),
      NewTypeError(MessageTemplate::kStrictCannotCreateProperty, name,
                   Object::TypeOf(isolate, receiver), receiver));
}

Maybe<bool> Object::WriteToReadOnlyProperty(
    LookupIterator* it, DirectHandle<Object> value,
    Maybe<ShouldThrow> maybe_should_throw) {
  ShouldThrow should_throw = GetShouldThrow(it->isolate(), maybe_should_throw);
  if (it->IsFound() && !it->HolderIsReceiver()) {
    // "Override mistake" attempted, record a use count to track this per
    // v8:8175
    v8::Isolate::UseCounterFeature feature =
        should_throw == kThrowOnError
            ? v8::Isolate::kAttemptOverrideReadOnlyOnPrototypeStrict
            : v8::Isolate::kAttemptOverrideReadOnlyOnPrototypeSloppy;
    it->isolate()->CountUsage(feature);
  }
  return WriteToReadOnlyProperty(it->isolate(), it->GetReceiver(),
                                 it->GetName(), value, should_throw);
}

Maybe<bool> Object::WriteToReadOnlyProperty(Isolate* isolate,
                                            Handle<Object> receiver,
                                            Handle<Object> name,
                                            DirectHandle<Object> value,
                                            ShouldThrow should_throw) {
  RETURN_FAILURE(isolate, should_throw,
                 NewTypeError(MessageTemplate::kStrictReadOnlyProperty, name,
                              Object::TypeOf(isolate, receiver), receiver));
}

Maybe<bool> Object::RedefineIncompatibleProperty(
    Isolate* isolate, Handle<Object> name, DirectHandle<Object> value,
    Maybe<ShouldThrow> should_throw) {
  RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                 NewTypeError(MessageTemplate::kRedefineDisallowed, name));
}

Maybe<bool> Object::SetDataProperty(LookupIterator* it, Handle<Object> value) {
  Isolate* isolate = it->isolate();
  DCHECK_IMPLIES(IsJSProxy(*it->GetReceiver(), isolate),
                 it->GetName()->IsPrivateName());
  DCHECK_IMPLIES(!it->IsElement() && it->GetName()->IsPrivateName(),
                 it->state() == LookupIterator::DATA);
  Handle<JSReceiver> receiver = Cast<JSReceiver>(it->GetReceiver());

  // Store on the holder which may be hidden behind the receiver.
  DCHECK(it->HolderIsReceiverOrHiddenPrototype());

  Handle<Object> to_assign = value;
  // Convert the incoming value to a number for storing into typed arrays.
  if (it->IsElement() && IsJSObject(*receiver, isolate) &&
      Cast<JSObject>(*receiver)->HasTypedArrayOrRabGsabTypedArrayElements(
          isolate)) {
    auto receiver_ta = Cast<JSTypedArray>(receiver);
    ElementsKind elements_kind = Cast<JSObject>(*receiver)->GetElementsKind();
    if (IsBigIntTypedArrayElementsKind(elements_kind)) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, to_assign,
                                       BigInt::FromObject(isolate, value),
                                       Nothing<bool>());
      if (V8_UNLIKELY(receiver_ta->IsDetachedOrOutOfBounds() ||
                      it->index() >= receiver_ta->GetLength())) {
        return Just(true);
      }
    } else if (!IsNumber(*value) && !IsUndefined(*value, isolate)) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, to_assign,
                                       Object::ToNumber(isolate, value),
                                       Nothing<bool>());
      if (V8_UNLIKELY(receiver_ta->IsDetachedOrOutOfBounds() ||
                      it->index() >= receiver_ta->GetLength())) {
        return Just(true);
      }
    }
  }

  DCHECK(!IsWasmObject(*receiver, isolate));
  if (V8_UNLIKELY(IsJSSharedStruct(*receiver, isolate) ||
                  IsJSSharedArray(*receiver, isolate))) {
    // Shared structs can only point to primitives or shared values.
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, to_assign, Object::Share(isolate, to_assign, kThrowOnError),
        Nothing<bool>());
    it->WriteDataValue(to_assign, false);
  } else {
    // Possibly migrate to the most up-to-date map that will be able to store
    // |value| under it->name().
    it->PrepareForDataProperty(to_assign);

    // Write the property value.
    it->WriteDataValue(to_assign, false);
  }

#if VERIFY_HEAP
  if (v8_flags.verify_heap) {
    receiver->HeapObjectVerify(isolate);
  }
#endif
  return Just(true);
}

Maybe<bool> Object::AddDataProperty(LookupIterator* it,
                                    DirectHandle<Object> value,
                                    PropertyAttributes attributes,
                                    Maybe<ShouldThrow> should_throw,
                                    StoreOrigin store_origin,
                                    EnforceDefineSemantics semantics) {
  if (!IsJSReceiver(*it->GetReceiver())) {
    return CannotCreateProperty(it->isolate(), it->GetReceiver(), it->GetName(),
                                value, should_throw);
  }

  // Private symbols should be installed on JSProxy using
  // JSProxy::SetPrivateSymbol.
  if (IsJSProxy(*it->GetReceiver()) && it->GetName()->IsPrivate() &&
      !it->GetName()->IsPrivateName()) {
    RETURN_FAILURE(it->isolate(), GetShouldThrow(it->isolate(), should_throw),
                   NewTypeError(MessageTemplate::kProxyPrivate));
  }

  DCHECK_NE(LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND, it->state());

  Handle<JSReceiver> receiver = it->GetStoreTarget<JSReceiver>();
  DCHECK_IMPLIES(IsJSProxy(*receiver), it->GetName()->IsPrivateName());
  DCHECK_IMPLIES(IsJSProxy(*receiver),
                 it->state() == LookupIterator::NOT_FOUND);

  // If the receiver is a JSGlobalProxy, store on the prototype (JSGlobalObject)
  // instead. If the prototype is Null, the proxy is detached.
  if (IsJSGlobalProxy(*receiver)) return Just(true);

  Isolate* isolate = it->isolate();

  if (it->ExtendingNonExtensible(receiver)) {
    bool is_shared_object = IsAlwaysSharedSpaceJSObject(*receiver);
    RETURN_FAILURE(
        isolate, GetShouldThrow(it->isolate(), should_throw),
        NewTypeError(
            semantics == EnforceDefineSemantics::kDefine
                ? (is_shared_object
                       ? MessageTemplate::kDefineDisallowedFixedLayout
                       : MessageTemplate::kDefineDisallowed)
                : (is_shared_object ? MessageTemplate::kObjectFixedLayout
                                    : MessageTemplate::kObjectNotExtensible),
            it->GetName()));
  }

  if (it->IsElement(*receiver)) {
    if (IsJSArray(*receiver)) {
      Handle<JSArray> array = Cast<JSArray>(receiver);
      if (JSArray::WouldChangeReadOnlyLength(array, it->array_index())) {
        RETURN_FAILURE(isolate, GetShouldThrow(it->isolate(), should_throw),
                       NewTypeError(MessageTemplate::kStrictReadOnlyProperty,
                                    isolate->factory()->length_string(),
                                    Object::TypeOf(isolate, array), array));
      }
    }

    Handle<JSObject> receiver_obj = Cast<JSObject>(receiver);
    MAYBE_RETURN(JSObject::AddDataElement(receiver_obj, it->array_index(),
                                          value, attributes),
                 Nothing<bool>());
    JSObject::ValidateElements(*receiver_obj);
    return Just(true);
  }

  return Object::TransitionAndWriteDataProperty(it, value, attributes,
                                                should_throw, store_origin);
}

// static
Maybe<bool> Object::TransitionAndWriteDataProperty(
    LookupIterator* it, DirectHandle<Object> value,
    PropertyAttributes attributes, Maybe<ShouldThrow> should_throw,
    StoreOrigin store_origin) {
  Handle<JSReceiver> receiver = it->GetStoreTarget<JSReceiver>();
  it->UpdateProtector();
  // Migrate to the most up-to-date map that will be able to store |value|
  // under it->name() with |attributes|.
  it->PrepareTransitionToDataProperty(receiver, value, attributes,
                                      store_origin);
  DCHECK_EQ(LookupIterator::TRANSITION, it->state());
  it->ApplyTransitionToDataProperty(receiver);

  // Write the property value.
  it->WriteDataValue(value, true);

#if VERIFY_HEAP
  if (v8_flags.verify_heap) {
    receiver->HeapObjectVerify(it->isolate());
  }
#endif

  return Just(true);
}
// static
MaybeHandle<Object> Object::ShareSlow(Isolate* isolate,
                                      Handle<HeapObject> value,
                                      ShouldThrow throw_if_cannot_be_shared) {
  // Use Object::Share() if value might already be shared.
  DCHECK(!IsShared(*value));

  SharedObjectSafePublishGuard publish_guard;

  if (IsString(*value)) {
    return String::Share(isolate, Cast<String>(value));
  }

  if (IsHeapNumber(*value)) {
    uint64_t bits = Cast<HeapNumber>(*value)->value_as_bits();
    return isolate->factory()
        ->NewHeapNumberFromBits<AllocationType::kSharedOld>(bits);
  }

  if (throw_if_cannot_be_shared == kThrowOnError) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kCannotBeShared, value));
  }
  return MaybeHandle<Object>();
}

namespace {

template <class T>
int AppendUniqueCallbacks(Isolate* isolate, DirectHandle<ArrayList> callbacks,
                          Handle<typename T::Array> array,
                          int valid_descriptors) {
  int nof_callbacks = callbacks->length();

  // Fill in new callback descriptors.  Process the callbacks from
  // back to front so that the last callback with a given name takes
  // precedence over previously added callbacks with that name.
  for (int i = nof_callbacks - 1; i >= 0; i--) {
    Handle<AccessorInfo> entry(Cast<AccessorInfo>(callbacks->get(i)), isolate);
    Handle<Name> key(Cast<Name>(entry->name()), isolate);
    DCHECK(IsUniqueName(*key));
    // Check if a descriptor with this name already exists before writing.
    if (!T::Contains(key, entry, valid_descriptors, array)) {
      T::Insert(key, entry, valid_descriptors, array);
      valid_descriptors++;
    }
  }

  return valid_descriptors;
}

struct FixedArrayAppender {
  using Array = FixedArray;
  static bool Contains(DirectHandle<Name> key, DirectHandle<AccessorInfo> entry,
                       int valid_descriptors, DirectHandle<FixedArray> array) {
    for (int i = 0; i < valid_descriptors; i++) {
      if (*key == Cast<AccessorInfo>(array->get(i))->name()) return true;
    }
    return false;
  }
  static void Insert(DirectHandle<Name> key, DirectHandle<AccessorInfo> entry,
                     int valid_descriptors, DirectHandle<FixedArray> array) {
    DisallowGarbageCollection no_gc;
    array->set(valid_descriptors, *entry);
  }
};

}  // namespace

int AccessorInfo::AppendUnique(Isolate* isolate, Handle<Object> descriptors,
                               Handle<FixedArray> array,
                               int valid_descriptors) {
  auto callbacks = Cast<ArrayList>(descriptors);
  DCHECK_GE(array->length(), callbacks->length() + valid_descriptors);
  return AppendUniqueCallbacks<FixedArrayAppender>(isolate, callbacks, array,
                                                   valid_descriptors);
}

void JSProxy::Revoke(DirectHandle<JSProxy> proxy) {
  Isolate* isolate = proxy->GetIsolate();
  // ES#sec-proxy-revocation-functions
  if (!proxy->IsRevoked()) {
    // 5. Set p.[[ProxyTarget]] to null.
    proxy->set_target(ReadOnlyRoots(isolate).null_value());
    // 6. Set p.[[ProxyHandler]] to null.
    proxy->set_handler(ReadOnlyRoots(isolate).null_value());
  }
  DCHECK(proxy->IsRevoked());
}

// static
Maybe<bool> JSProxy::IsArray(Handle<JSProxy> proxy) {
  Isolate* isolate = proxy->GetIsolate();
  Handle<JSReceiver> object = Cast<JSReceiver>(proxy);
  for (int i = 0; i < JSProxy::kMaxIterationLimit; i++) {
    proxy = Cast<JSProxy>(object);
    if (proxy->IsRevoked()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyRevoked,
          isolate->factory()->NewStringFromAsciiChecked("IsArray")));
      return Nothing<bool>();
    }
    object = handle(Cast<JSReceiver>(proxy->target()), isolate);
    if (IsJSArray(*object)) return Just(true);
    if (!IsJSProxy(*object)) return Just(false);
  }

  // Too deep recursion, throw a RangeError.
  isolate->StackOverflow();
  return Nothing<bool>();
}

Maybe<bool> JSProxy::HasProperty(Isolate* isolate, DirectHandle<JSProxy> proxy,
                                 Handle<Name> name) {
  DCHECK(!name->IsPrivate());
  STACK_CHECK(isolate, Nothing<bool>());
  // 1. (Assert)
  // 2. Let handler be the value of the [[ProxyHandler]] internal slot of O.
  Handle<Object> handler(proxy->handler(), isolate);
  // 3. If handler is null, throw a TypeError exception.
  // 4. Assert: Type(handler) is Object.
  if (proxy->IsRevoked()) {
    isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kProxyRevoked, isolate->factory()->has_string()));
    return Nothing<bool>();
  }
  // 5. Let target be the value of the [[ProxyTarget]] internal slot of O.
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  // 6. Let trap be ? GetMethod(handler, "has").
  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap,
      Object::GetMethod(isolate, Cast<JSReceiver>(handler),
                        isolate->factory()->has_string()),
      Nothing<bool>());
  // 7. If trap is undefined, then
  if (IsUndefined(*trap, isolate)) {
    // 7a. Return target.[[HasProperty]](P).
    return JSReceiver::HasProperty(isolate, target, name);
  }
  // 8. Let booleanTrapResult be ToBoolean(? Call(trap, handler, «target, P»)).
  Handle<Object> trap_result_obj;
  Handle<Object> args[] = {target, name};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap_result_obj,
      Execution::Call(isolate, trap, handler, arraysize(args), args),
      Nothing<bool>());
  bool boolean_trap_result = Object::BooleanValue(*trap_result_obj, isolate);
  // 9. If booleanTrapResult is false, then:
  if (!boolean_trap_result) {
    MAYBE_RETURN(JSProxy::CheckHasTrap(isolate, name, target), Nothing<bool>());
  }
  // 10. Return booleanTrapResult.
  return Just(boolean_trap_result);
}

Maybe<bool> JSProxy::CheckHasTrap(Isolate* isolate, Handle<Name> name,
                                  Handle<JSReceiver> target) {
  // 9a. Let targetDesc be ? target.[[GetOwnProperty]](P).
  PropertyDescriptor target_desc;
  Maybe<bool> target_found =
      JSReceiver::GetOwnPropertyDescriptor(isolate, target, name, &target_desc);
  MAYBE_RETURN(target_found, Nothing<bool>());
  // 9b. If targetDesc is not undefined, then:
  if (target_found.FromJust()) {
    // 9b i. If targetDesc.[[Configurable]] is false, throw a TypeError
    //       exception.
    if (!target_desc.configurable()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyHasNonConfigurable, name));
      return Nothing<bool>();
    }
    // 9b ii. Let extensibleTarget be ? IsExtensible(target).
    Maybe<bool> extensible_target = JSReceiver::IsExtensible(isolate, target);
    MAYBE_RETURN(extensible_target, Nothing<bool>());
    // 9b iii. If extensibleTarget is false, throw a TypeError exception.
    if (!extensible_target.FromJust()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyHasNonExtensible, name));
      return Nothing<bool>();
    }
  }
  return Just(true);
}

Maybe<bool> JSProxy::SetProperty(DirectHandle<JSProxy> proxy, Handle<Name> name,
                                 Handle<Object> value, Handle<Object> receiver,
                                 Maybe<ShouldThrow> should_throw) {
  DCHECK(!name->IsPrivate());
  Isolate* isolate = proxy->GetIsolate();
  STACK_CHECK(isolate, Nothing<bool>());
  Factory* factory = isolate->factory();
  Handle<String> trap_name = factory->set_string();

  if (proxy->IsRevoked()) {
    isolate->Throw(
        *factory->NewTypeError(MessageTemplate::kProxyRevoked, trap_name));
    return Nothing<bool>();
  }
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  Handle<JSReceiver> handler(Cast<JSReceiver>(proxy->handler()), isolate);

  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap, Object::GetMethod(isolate, handler, trap_name),
      Nothing<bool>());
  if (IsUndefined(*trap, isolate)) {
    PropertyKey key(isolate, name);
    LookupIterator it(isolate, receiver, key, target);

    return Object::SetSuperProperty(&it, value, StoreOrigin::kMaybeKeyed,
                                    should_throw);
  }

  Handle<Object> trap_result;
  Handle<Object> args[] = {target, name, value, receiver};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap_result,
      Execution::Call(isolate, trap, handler, arraysize(args), args),
      Nothing<bool>());
  if (!Object::BooleanValue(*trap_result, isolate)) {
    RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                   NewTypeError(MessageTemplate::kProxyTrapReturnedFalsishFor,
                                trap_name, name));
  }

  MaybeHandle<Object> result =
      JSProxy::CheckGetSetTrapResult(isolate, name, target, value, kSet);

  if (result.is_null()) {
    return Nothing<bool>();
  }
  return Just(true);
}

Maybe<bool> JSProxy::DeletePropertyOrElement(DirectHandle<JSProxy> proxy,
                                             Handle<Name> name,
                                             LanguageMode language_mode) {
  DCHECK(!name->IsPrivate());
  ShouldThrow should_throw =
      is_sloppy(language_mode) ? kDontThrow : kThrowOnError;
  Isolate* isolate = proxy->GetIsolate();
  STACK_CHECK(isolate, Nothing<bool>());
  Factory* factory = isolate->factory();
  Handle<String> trap_name = factory->deleteProperty_string();

  if (proxy->IsRevoked()) {
    isolate->Throw(
        *factory->NewTypeError(MessageTemplate::kProxyRevoked, trap_name));
    return Nothing<bool>();
  }
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  Handle<JSReceiver> handler(Cast<JSReceiver>(proxy->handler()), isolate);

  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap, Object::GetMethod(isolate, handler, trap_name),
      Nothing<bool>());
  if (IsUndefined(*trap, isolate)) {
    return JSReceiver::DeletePropertyOrElement(target, name, language_mode);
  }

  Handle<Object> trap_result;
  Handle<Object> args[] = {target, name};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap_result,
      Execution::Call(isolate, trap, handler, arraysize(args), args),
      Nothing<bool>());
  if (!Object::BooleanValue(*trap_result, isolate)) {
    RETURN_FAILURE(isolate, should_throw,
                   NewTypeError(MessageTemplate::kProxyTrapReturnedFalsishFor,
                                trap_name, name));
  }

  // Enforce the invariant.
  return JSProxy::CheckDeleteTrap(isolate, name, target);
}

Maybe<bool> JSProxy::CheckDeleteTrap(Isolate* isolate, Handle<Name> name,
                                     Handle<JSReceiver> target) {
  // 10. Let targetDesc be ? target.[[GetOwnProperty]](P).
  PropertyDescriptor target_desc;
  Maybe<bool> target_found =
      JSReceiver::GetOwnPropertyDescriptor(isolate, target, name, &target_desc);
  MAYBE_RETURN(target_found, Nothing<bool>());
  // 11. If targetDesc is undefined, return true.
  if (target_found.FromJust()) {
    // 12. If targetDesc.[[Configurable]] is false, throw a TypeError exception.
    if (!target_desc.configurable()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyDeletePropertyNonConfigurable, name));
      return Nothing<bool>();
    }
    // 13. Let extensibleTarget be ? IsExtensible(target).
    Maybe<bool> extensible_target = JSReceiver::IsExtensible(isolate, target);
    MAYBE_RETURN(extensible_target, Nothing<bool>());
    // 14. If extensibleTarget is false, throw a TypeError exception.
    if (!extensible_target.FromJust()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyDeletePropertyNonExtensible, name));
      return Nothing<bool>();
    }
  }
  return Just(true);
}

// static
MaybeHandle<JSProxy> JSProxy::New(Isolate* isolate, Handle<Object> target,
                                  Handle<Object> handler) {
  if (!IsJSReceiver(*target)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kProxyNonObject));
  }
  if (!IsJSReceiver(*handler)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kProxyNonObject));
  }
  return isolate->factory()->NewJSProxy(Cast<JSReceiver>(target),
                                        Cast<JSReceiver>(handler));
}

Maybe<PropertyAttributes> JSProxy::GetPropertyAttributes(LookupIterator* it) {
  PropertyDescriptor desc;
  Maybe<bool> found = JSProxy::GetOwnPropertyDescriptor(
      it->isolate(), it->GetHolder<JSProxy>(), it->GetName(), &desc);
  MAYBE_RETURN(found, Nothing<PropertyAttributes>());
  if (!found.FromJust()) return Just(ABSENT);
  return Just(desc.ToAttributes());
}

// TODO(jkummerow): Consider unification with FastAsArrayLength() in
// accessors.cc.
bool PropertyKeyToArrayLength(DirectHandle<Object> value, uint32_t* length) {
  DCHECK(IsNumber(*value) || IsName(*value));
  if (Object::ToArrayLength(*value, length)) return true;
  if (IsString(*value)) return Cast<String>(*value)->AsArrayIndex(length);
  return false;
}

bool PropertyKeyToArrayIndex(DirectHandle<Object> index_obj, uint32_t* output) {
  return PropertyKeyToArrayLength(index_obj, output) && *output != kMaxUInt32;
}

// ES6 9.4.2.1
// static
Maybe<bool> JSArray::DefineOwnProperty(Isolate* isolate, Handle<JSArray> o,
                                       Handle<Object> name,
                                       PropertyDescriptor* desc,
                                       Maybe<ShouldThrow> should_throw) {
  if (IsName(*name)) {
    name = isolate->factory()->InternalizeName(Cast<Name>(name));
  }

  // 1. Assert: IsPropertyKey(P) is true. ("P" is |name|.)
  // 2. If P is "length", then:
  if (*name == ReadOnlyRoots(isolate).length_string()) {
    // 2a. Return ArraySetLength(A, Desc).
    return ArraySetLength(isolate, o, desc, should_throw);
  }
  // 3. Else if P is an array index, then:
  uint32_t index = 0;
  if (PropertyKeyToArrayIndex(name, &index)) {
    // 3a. Let oldLenDesc be OrdinaryGetOwnProperty(A, "length").
    PropertyDescriptor old_len_desc;
    Maybe<bool> success = GetOwnPropertyDescriptor(
        isolate, o, isolate->factory()->length_string(), &old_len_desc);
    // 3b. (Assert)
    DCHECK(success.FromJust());
    USE(success);
    // 3c. Let oldLen be oldLenDesc.[[Value]].
    uint32_t old_len = 0;
    CHECK(Object::ToArrayLength(*old_len_desc.value(), &old_len));
    // 3d. Let index be ToUint32(P).
    // (Already done above.)
    // 3e. (Assert)
    // 3f. If index >= oldLen and oldLenDesc.[[Writable]] is false,
    //     return false.
    if (index >= old_len && old_len_desc.has_writable() &&
        !old_len_desc.writable()) {
      RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                     NewTypeError(MessageTemplate::kDefineDisallowed, name));
    }
    // 3g. Let succeeded be OrdinaryDefineOwnProperty(A, P, Desc).
    Maybe<bool> succeeded =
        OrdinaryDefineOwnProperty(isolate, o, name, desc, should_throw);
    // 3h. Assert: succeeded is not an abrupt completion.
    //     In our case, if should_throw == kThrowOnError, it can be!
    // 3i. If succeeded is false, return false.
    if (succeeded.IsNothing() || !succeeded.FromJust()) return succeeded;
    // 3j. If index >= oldLen, then:
    if (index >= old_len) {
      // 3j i. Set oldLenDesc.[[Value]] to index + 1.
      old_len_desc.set_value(isolate->factory()->NewNumberFromUint(index + 1));
      // 3j ii. Let succeeded be
      //        OrdinaryDefineOwnProperty(A, "length", oldLenDesc).
      succeeded = OrdinaryDefineOwnProperty(isolate, o,
                                            isolate->factory()->length_string(),
                                            &old_len_desc, should_throw);
      // 3j iii. Assert: succeeded is true.
      DCHECK(succeeded.FromJust());
      USE(succeeded);
    }
    // 3k. Return true.
    return Just(true);
  }

  // 4. Return OrdinaryDefineOwnProperty(A, P, Desc).
  return OrdinaryDefineOwnProperty(isolate, o, name, desc, should_throw);
}

// Part of ES6 9.4.2.4 ArraySetLength.
// static
bool JSArray::AnythingToArrayLength(Isolate* isolate,
                                    Handle<Object> length_object,
                                    uint32_t* output) {
  // Fast path: check numbers and strings that can be converted directly
  // and unobservably.
  if (Object::ToArrayLength(*length_object, output)) return true;
  if (IsString(*length_object) &&
      Cast<String>(length_object)->AsArrayIndex(output)) {
    return true;
  }
  // Slow path: follow steps in ES6 9.4.2.4 "ArraySetLength".
  // 3. Let newLen be ToUint32(Desc.[[Value]]).
  Handle<Number> uint32_v;
  if (!Object::ToUint32(isolate, length_object).ToHandle(&uint32_v)) {
    // 4. ReturnIfAbrupt(newLen).
    return false;
  }
  // 5. Let numberLen be ToNumber(Desc.[[Value]]).
  Handle<Number> number_v;
  if (!Object::ToNumber(isolate, length_object).ToHandle(&number_v)) {
    // 6. ReturnIfAbrupt(newLen).
    return false;
  }
  // 7. If newLen != numberLen, throw a RangeError exception.
  if (Object::NumberValue(*uint32_v) != Object::NumberValue(*number_v)) {
    DirectHandle<Object> exception =
        isolate->factory()->NewRangeError(MessageTemplate::kInvalidArrayLength);
    isolate->Throw(*exception);
    return false;
  }
  CHECK(Object::ToArrayLength(*uint32_v, output));
  return true;
}

// ES6 9.4.2.4
// static
Maybe<bool> JSArray::ArraySetLength(Isolate* isolate, Handle<JSArray> a,
                                    PropertyDescriptor* desc,
                                    Maybe<ShouldThrow> should_throw) {
  // 1. If the [[Value]] field of Desc is absent, then
  if (!desc->has_value()) {
    // 1a. Return OrdinaryDefineOwnProperty(A, "length", Desc).
    return OrdinaryDefineOwnProperty(
        isolate, a, isolate->factory()->length_string(), desc, should_throw);
  }
  // 2. Let newLenDesc be a copy of Desc.
  // (Actual copying is not necessary.)
  PropertyDescriptor* new_len_desc = desc;
  // 3. - 7. Convert Desc.[[Value]] to newLen.
  uint32_t new_len = 0;
  if (!AnythingToArrayLength(isolate, desc->value(), &new_len)) {
    DCHECK(isolate->has_exception());
    return Nothing<bool>();
  }
  // 8. Set newLenDesc.[[Value]] to newLen.
  // (Done below, if needed.)
  // 9. Let oldLenDesc be OrdinaryGetOwnProperty(A, "length").
  PropertyDescriptor old_len_desc;
  Maybe<bool> success = GetOwnPropertyDescriptor(
      isolate, a, isolate->factory()->length_string(), &old_len_desc);
  // 10. (Assert)
  DCHECK(success.FromJust());
  USE(success);
  // 11. Let oldLen be oldLenDesc.[[Value]].
  uint32_t old_len = 0;
  CHECK(Object::ToArrayLength(*old_len_desc.value(), &old_len));
  // 12. If newLen >= oldLen, then
  if (new_len >= old_len) {
    // 8. Set newLenDesc.[[Value]] to newLen.
    // 12a. Return OrdinaryDefineOwnProperty(A, "length", newLenDesc).
    new_len_desc->set_value(isolate->factory()->NewNumberFromUint(new_len));
    return OrdinaryDefineOwnProperty(isolate, a,
                                     isolate->factory()->length_string(),
                                     new_len_desc, should_throw);
  }
  // 13. If oldLenDesc.[[Writable]] is false, return false.
  if (!old_len_desc.writable() ||
      // Also handle the {configurable: true} and enumerable changes
      // since we later use JSArray::SetLength instead of
      // OrdinaryDefineOwnProperty to change the length,
      // and it doesn't have access to the descriptor anymore.
      new_len_desc->configurable() ||
      (new_len_desc->has_enumerable() &&
       (old_len_desc.enumerable() != new_len_desc->enumerable()))) {
    RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                   NewTypeError(MessageTemplate::kRedefineDisallowed,
                                isolate->factory()->length_string()));
  }
  // 14. If newLenDesc.[[Writable]] is absent or has the value true,
  // let newWritable be true.
  bool new_writable = false;
  if (!new_len_desc->has_writable() || new_len_desc->writable()) {
    new_writable = true;
  } else {
    // 15. Else,
    // 15a. Need to defer setting the [[Writable]] attribute to false in case
    //      any elements cannot be deleted.
    // 15b. Let newWritable be false. (It's initialized as "false" anyway.)
    // 15c. Set newLenDesc.[[Writable]] to true.
    // (Not needed.)
  }
  // Most of steps 16 through 19 is implemented by JSArray::SetLength.
  MAYBE_RETURN(JSArray::SetLength(a, new_len), Nothing<bool>());
  // Steps 19d-ii, 20.
  if (!new_writable) {
    PropertyDescriptor readonly;
    readonly.set_writable(false);
    success = OrdinaryDefineOwnProperty(isolate, a,
                                        isolate->factory()->length_string(),
                                        &readonly, should_throw);
    DCHECK(success.FromJust());
    USE(success);
  }
  uint32_t actual_new_len = 0;
  CHECK(Object::ToArrayLength(a->length(), &actual_new_len));
  // Steps 19d-v, 21. Return false if there were non-deletable elements.
  bool result = actual_new_len == new_len;
  if (!result) {
    RETURN_FAILURE(
        isolate, GetShouldThrow(isolate, should_throw),
        NewTypeError(MessageTemplate::kStrictDeleteProperty,
                     isolate->factory()->NewNumberFromUint(actual_new_len - 1),
                     a));
  }
  return Just(result);
}

// ES6 9.5.6
// static
Maybe<bool> JSProxy::DefineOwnProperty(Isolate* isolate, Handle<JSProxy> proxy,
                                       Handle<Object> key,
                                       PropertyDescriptor* desc,
                                       Maybe<ShouldThrow> should_throw) {
  STACK_CHECK(isolate, Nothing<bool>());
  if (IsSymbol(*key) && Cast<Symbol>(key)->IsPrivate()) {
    DCHECK(!Cast<Symbol>(key)->IsPrivateName());
    return JSProxy::SetPrivateSymbol(isolate, proxy, Cast<Symbol>(key), desc,
                                     should_throw);
  }
  Handle<String> trap_name = isolate->factory()->defineProperty_string();
  // 1. Assert: IsPropertyKey(P) is true.
  DCHECK(IsName(*key) || IsNumber(*key));
  // 2. Let handler be the value of the [[ProxyHandler]] internal slot of O.
  Handle<Object> handler(proxy->handler(), isolate);
  // 3. If handler is null, throw a TypeError exception.
  // 4. Assert: Type(handler) is Object.
  if (proxy->IsRevoked()) {
    isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kProxyRevoked, trap_name));
    return Nothing<bool>();
  }
  // 5. Let target be the value of the [[ProxyTarget]] internal slot of O.
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  // 6. Let trap be ? GetMethod(handler, "defineProperty").
  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap,
      Object::GetMethod(isolate, Cast<JSReceiver>(handler), trap_name),
      Nothing<bool>());
  // 7. If trap is undefined, then:
  if (IsUndefined(*trap, isolate)) {
    // 7a. Return target.[[DefineOwnProperty]](P, Desc).
    return JSReceiver::DefineOwnProperty(isolate, target, key, desc,
                                         should_throw);
  }
  // 8. Let descObj be FromPropertyDescriptor(Desc).
  Handle<Object> desc_obj = desc->ToObject(isolate);
  // 9. Let booleanTrapResult be
  //    ToBoolean(? Call(trap, handler, «target, P, descObj»)).
  Handle<Name> property_name =
      IsName(*key) ? Cast<Name>(key)
                   : Cast<Name>(isolate->factory()->NumberToString(key));
  // Do not leak private property names.
  DCHECK(!property_name->IsPrivate());
  Handle<Object> trap_result_obj;
  Handle<Object> args[] = {target, property_name, desc_obj};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap_result_obj,
      Execution::Call(isolate, trap, handler, arraysize(args), args),
      Nothing<bool>());
  // 10. If booleanTrapResult is false, return false.
  if (!Object::BooleanValue(*trap_result_obj, isolate)) {
    RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                   NewTypeError(MessageTemplate::kProxyTrapReturnedFalsishFor,
                                trap_name, property_name));
  }
  // 11. Let targetDesc be ? target.[[GetOwnProperty]](P).
  PropertyDescriptor target_desc;
  Maybe<bool> target_found =
      JSReceiver::GetOwnPropertyDescriptor(isolate, target, key, &target_desc);
  MAYBE_RETURN(target_found, Nothing<bool>());
  // 12. Let extensibleTarget be ? IsExtensible(target).
  Maybe<bool> maybe_extensible = JSReceiver::IsExtensible(isolate, target);
  MAYBE_RETURN(maybe_extensible, Nothing<bool>());
  bool extensible_target = maybe_extensible.FromJust();
  // 13. If Desc has a [[Configurable]] field and if Desc.[[Configurable]]
  //     is false, then:
  // 13a. Let settingConfigFalse be true.
  // 14. Else let settingConfigFalse be false.
  bool setting_config_false = desc->has_configurable() && !desc->configurable();
  // 15. If targetDesc is undefined, then
  if (!target_found.FromJust()) {
    // 15a. If extensibleTarget is false, throw a TypeError exception.
    if (!extensible_target) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyDefinePropertyNonExtensible, property_name));
      return Nothing<bool>();
    }
    // 15b. If settingConfigFalse is true, throw a TypeError exception.
    if (setting_config_false) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyDefinePropertyNonConfigurable, property_name));
      return Nothing<bool>();
    }
  } else {
    // 16. Else targetDesc is not undefined,
    // 16a. If IsCompatiblePropertyDescriptor(extensibleTarget, Desc,
    //      targetDesc) is false, throw a TypeError exception.
    Maybe<bool> valid = IsCompatiblePropertyDescriptor(
        isolate, extensible_target, desc, &target_desc, property_name,
        Just(kDontThrow));
    MAYBE_RETURN(valid, Nothing<bool>());
    if (!valid.FromJust()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyDefinePropertyIncompatible, property_name));
      return Nothing<bool>();
    }
    // 16b. If settingConfigFalse is true and targetDesc.[[Configurable]] is
    //      true, throw a TypeError exception.
    if (setting_config_false && target_desc.configurable()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyDefinePropertyNonConfigurable, property_name));
      return Nothing<bool>();
    }
    // 16c. If IsDataDescriptor(targetDesc) is true,
    // targetDesc.[[Configurable]] is
    //       false, and targetDesc.[[Writable]] is true, then
    if (PropertyDescriptor::IsDataDescriptor(&target_desc) &&
        !target_desc.configurable() && target_desc.writable()) {
      // 16c i. If Desc has a [[Writable]] field and Desc.[[Writable]] is false,
      // throw a TypeError exception.
      if (desc->has_writable() && !desc->writable()) {
        isolate->Throw(*isolate->factory()->NewTypeError(
            MessageTemplate::kProxyDefinePropertyNonConfigurableWritable,
            property_name));
        return Nothing<bool>();
      }
    }
  }
  // 17. Return true.
  return Just(true);
}

// static
Maybe<bool> JSProxy::SetPrivateSymbol(Isolate* isolate, Handle<JSProxy> proxy,
                                      Handle<Symbol> private_name,
                                      PropertyDescriptor* desc,
                                      Maybe<ShouldThrow> should_throw) {
  // Despite the generic name, this can only add private data properties.
  if (!PropertyDescriptor::IsDataDescriptor(desc) ||
      desc->ToAttributes() != DONT_ENUM) {
    RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                   NewTypeError(MessageTemplate::kProxyPrivate));
  }
  DCHECK(proxy->map()->is_dictionary_map());
  Handle<Object> value =
      desc->has_value() ? desc->value()
                        : Cast<Object>(isolate->factory()->undefined_value());

  LookupIterator it(isolate, proxy, private_name, proxy);

  if (it.IsFound()) {
    DCHECK_EQ(LookupIterator::DATA, it.state());
    DCHECK_EQ(DONT_ENUM, it.property_attributes());
    // We are not tracking constness for private symbols added to JSProxy
    // objects.
    DCHECK_EQ(PropertyConstness::kMutable, it.property_details().constness());
    it.WriteDataValue(value, false);
    return Just(true);
  }

  PropertyDetails details(PropertyKind::kData, DONT_ENUM,
                          PropertyConstness::kMutable);
  if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    Handle<SwissNameDictionary> dict(proxy->property_dictionary_swiss(),
                                     isolate);
    Handle<SwissNameDictionary> result =
        SwissNameDictionary::Add(isolate, dict, private_name, value, details);
    if (!dict.is_identical_to(result)) proxy->SetProperties(*result);
  } else {
    Handle<NameDictionary> dict(proxy->property_dictionary(), isolate);
    Handle<NameDictionary> result =
        NameDictionary::Add(isolate, dict, private_name, value, details);
    if (!dict.is_identical_to(result)) proxy->SetProperties(*result);
  }
  return Just(true);
}

// ES6 9.5.5
// static
Maybe<bool> JSProxy::GetOwnPropertyDescriptor(Isolate* isolate,
                                              DirectHandle<JSProxy> proxy,
                                              Handle<Name> name,
                                              PropertyDescriptor* desc) {
  DCHECK(!name->IsPrivate());
  STACK_CHECK(isolate, Nothing<bool>());

  Handle<String> trap_name =
      isolate->factory()->getOwnPropertyDescriptor_string();
  // 1. (Assert)
  // 2. Let handler be the value of the [[ProxyHandler]] internal slot of O.
  Handle<Object> handler(proxy->handler(), isolate);
  // 3. If handler is null, throw a TypeError exception.
  // 4. Assert: Type(handler) is Object.
  if (proxy->IsRevoked()) {
    isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kProxyRevoked, trap_name));
    return Nothing<bool>();
  }
  // 5. Let target be the value of the [[ProxyTarget]] internal slot of O.
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  // 6. Let trap be ? GetMethod(handler, "getOwnPropertyDescriptor").
  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap,
      Object::GetMethod(isolate, Cast<JSReceiver>(handler), trap_name),
      Nothing<bool>());
  // 7. If trap is undefined, then
  if (IsUndefined(*trap, isolate)) {
    // 7a. Return target.[[GetOwnProperty]](P).
    return JSReceiver::GetOwnPropertyDescriptor(isolate, target, name, desc);
  }
  // 8. Let trapResultObj be ? Call(trap, handler, «target, P»).
  Handle<JSAny> trap_result_obj;
  Handle<Object> args[] = {target, name};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap_result_obj,
      Cast<JSAny>(
          Execution::Call(isolate, trap, handler, arraysize(args), args)),
      Nothing<bool>());
  // 9. If Type(trapResultObj) is neither Object nor Undefined, throw a
  //    TypeError exception.
  if (!IsJSReceiver(*trap_result_obj) &&
      !IsUndefined(*trap_result_obj, isolate)) {
    isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kProxyGetOwnPropertyDescriptorInvalid, name));
    return Nothing<bool>();
  }
  // 10. Let targetDesc be ? target.[[GetOwnProperty]](P).
  PropertyDescriptor target_desc;
  Maybe<bool> found =
      JSReceiver::GetOwnPropertyDescriptor(isolate, target, name, &target_desc);
  MAYBE_RETURN(found, Nothing<bool>());
  // 11. If trapResultObj is undefined, then
  if (IsUndefined(*trap_result_obj, isolate)) {
    // 11a. If targetDesc is undefined, return undefined.
    if (!found.FromJust()) return Just(false);
    // 11b. If targetDesc.[[Configurable]] is false, throw a TypeError
    //      exception.
    if (!target_desc.configurable()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyGetOwnPropertyDescriptorUndefined, name));
      return Nothing<bool>();
    }
    // 11c. Let extensibleTarget be ? IsExtensible(target).
    Maybe<bool> extensible_target = JSReceiver::IsExtensible(isolate, target);
    MAYBE_RETURN(extensible_target, Nothing<bool>());
    // 11d. (Assert)
    // 11e. If extensibleTarget is false, throw a TypeError exception.
    if (!extensible_target.FromJust()) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyGetOwnPropertyDescriptorNonExtensible, name));
      return Nothing<bool>();
    }
    // 11f. Return undefined.
    return Just(false);
  }
  // 12. Let extensibleTarget be ? IsExtensible(target).
  Maybe<bool> extensible_target = JSReceiver::IsExtensible(isolate, target);
  MAYBE_RETURN(extensible_target, Nothing<bool>());
  // 13. Let resultDesc be ? ToPropertyDescriptor(trapResultObj).
  if (!PropertyDescriptor::ToPropertyDescriptor(isolate, trap_result_obj,
                                                desc)) {
    DCHECK(isolate->has_exception());
    return Nothing<bool>();
  }
  // 14. Call CompletePropertyDescriptor(resultDesc).
  PropertyDescriptor::CompletePropertyDescriptor(isolate, desc);
  // 15. Let valid be IsCompatiblePropertyDescriptor (extensibleTarget,
  //     resultDesc, targetDesc).
  Maybe<bool> valid = IsCompatiblePropertyDescriptor(
      isolate, extensible_target.FromJust(), desc, &target_desc, name,
      Just(kDontThrow));
  MAYBE_RETURN(valid, Nothing<bool>());
  // 16. If valid is false, throw a TypeError exception.
  if (!valid.FromJust()) {
    isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kProxyGetOwnPropertyDescriptorIncompatible, name));
    return Nothing<bool>();
  }
  // 17. If resultDesc.[[Configurable]] is false, then
  if (!desc->configurable()) {
    // 17a. If targetDesc is undefined or targetDesc.[[Configurable]] is true:
    if (target_desc.is_empty() || target_desc.configurable()) {
      // 17a i. Throw a TypeError exception.
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kProxyGetOwnPropertyDescriptorNonConfigurable,
          name));
      return Nothing<bool>();
    }
    // 17b. If resultDesc has a [[Writable]] field and resultDesc.[[Writable]]
    // is false, then
    if (desc->has_writable() && !desc->writable()) {
      // 17b i. If targetDesc.[[Writable]] is true, throw a TypeError exception.
      if (target_desc.writable()) {
        isolate->Throw(*isolate->factory()->NewTypeError(
            MessageTemplate::
                kProxyGetOwnPropertyDescriptorNonConfigurableWritable,
            name));
        return Nothing<bool>();
      }
    }
  }
  // 18. Return resultDesc.
  return Just(true);
}

Maybe<bool> JSProxy::PreventExtensions(DirectHandle<JSProxy> proxy,
                                       ShouldThrow should_throw) {
  Isolate* isolate = proxy->GetIsolate();
  STACK_CHECK(isolate, Nothing<bool>());
  Factory* factory = isolate->factory();
  Handle<String> trap_name = factory->preventExtensions_string();

  if (proxy->IsRevoked()) {
    isolate->Throw(
        *factory->NewTypeError(MessageTemplate::kProxyRevoked, trap_name));
    return Nothing<bool>();
  }
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  Handle<JSReceiver> handler(Cast<JSReceiver>(proxy->handler()), isolate);

  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap, Object::GetMethod(isolate, handler, trap_name),
      Nothing<bool>());
  if (IsUndefined(*trap, isolate)) {
    return JSReceiver::PreventExtensions(isolate, target, should_throw);
  }

  Handle<Object> trap_result;
  Handle<Object> args[] = {target};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap_result,
      Execution::Call(isolate, trap, handler, arraysize(args), args),
      Nothing<bool>());
  if (!Object::BooleanValue(*trap_result, isolate)) {
    RETURN_FAILURE(
        isolate, should_throw,
        NewTypeError(MessageTemplate::kProxyTrapReturnedFalsish, trap_name));
  }

  // Enforce the invariant.
  Maybe<bool> target_result = JSReceiver::IsExtensible(isolate, target);
  MAYBE_RETURN(target_result, Nothing<bool>());
  if (target_result.FromJust()) {
    isolate->Throw(*factory->NewTypeError(
        MessageTemplate::kProxyPreventExtensionsExtensible));
    return Nothing<bool>();
  }
  return Just(true);
}

Maybe<bool> JSProxy::IsExtensible(DirectHandle<JSProxy> proxy) {
  Isolate* isolate = proxy->GetIsolate();
  STACK_CHECK(isolate, Nothing<bool>());
  Factory* factory = isolate->factory();
  Handle<String> trap_name = factory->isExtensible_string();

  if (proxy->IsRevoked()) {
    isolate->Throw(
        *factory->NewTypeError(MessageTemplate::kProxyRevoked, trap_name));
    return Nothing<bool>();
  }
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  Handle<JSReceiver> handler(Cast<JSReceiver>(proxy->handler()), isolate);

  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap, Object::GetMethod(isolate, handler, trap_name),
      Nothing<bool>());
  if (IsUndefined(*trap, isolate)) {
    return JSReceiver::IsExtensible(isolate, target);
  }

  Handle<Object> trap_result;
  Handle<Object> args[] = {target};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap_result,
      Execution::Call(isolate, trap, handler, arraysize(args), args),
      Nothing<bool>());

  // Enforce the invariant.
  Maybe<bool> target_result = JSReceiver::IsExtensible(isolate, target);
  MAYBE_RETURN(target_result, Nothing<bool>());
  if (target_result.FromJust() != Object::BooleanValue(*trap_result, isolate)) {
    isolate->Throw(
        *factory->NewTypeError(MessageTemplate::kProxyIsExtensibleInconsistent,
                               factory->ToBoolean(target_result.FromJust())));
    return Nothing<bool>();
  }
  return target_result;
}

Handle<DescriptorArray> DescriptorArray::CopyUpTo(
    Isolate* isolate, DirectHandle<DescriptorArray> desc, int enumeration_index,
    int slack) {
  return DescriptorArray::CopyUpToAddAttributes(isolate, desc,
                                                enumeration_index, NONE, slack);
}

Handle<DescriptorArray> DescriptorArray::CopyUpToAddAttributes(
    Isolate* isolate, DirectHandle<DescriptorArray> source_handle,
    int enumeration_index, PropertyAttributes attributes, int slack) {
  if (enumeration_index + slack == 0) {
    return isolate->factory()->empty_descriptor_array();
  }

  int size = enumeration_index;
  Handle<DescriptorArray> copy_handle =
      DescriptorArray::Allocate(isolate, size, slack);

  DisallowGarbageCollection no_gc;
  Tagged<DescriptorArray> source = *source_handle;
  Tagged<DescriptorArray> copy = *copy_handle;

  if (attributes != NONE) {
    for (InternalIndex i : InternalIndex::Range(size)) {
      Tagged<MaybeObject> value_or_field_type = source->GetValue(i);
      Tagged<Name> key = source->GetKey(i);
      PropertyDetails details = source->GetDetails(i);
      // Bulk attribute changes never affect private properties.
      if (!key->IsPrivate()) {
        int mask = DONT_DELETE | DONT_ENUM;
        // READ_ONLY is an invalid attribute for JS setters/getters.
        Tagged<HeapObject> heap_object;
        if (details.kind() != PropertyKind::kAccessor ||
            !(value_or_field_type.GetHeapObjectIfStrong(&heap_object) &&
              IsAccessorPair(heap_object))) {
          mask |= READ_ONLY;
        }
        details = details.CopyAddAttributes(
            static_cast<PropertyAttributes>(attributes & mask));
      }
      copy->Set(i, key, value_or_field_type, details);
    }
  } else {
    for (InternalIndex i : InternalIndex::Range(size)) {
      copy->CopyFrom(i, source);
    }
  }

  if (source->number_of_descriptors() != enumeration_index) copy->Sort();

  return copy_handle;
}

bool DescriptorArray::IsEqualUpTo(Tagged<DescriptorArray> desc,
                                  int nof_descriptors) {
  for (InternalIndex i : InternalIndex::Range(nof_descriptors)) {
    if (GetKey(i) != desc->GetKey(i) || GetValue(i) != desc->GetValue(i)) {
      return false;
    }
    PropertyDetails details = GetDetails(i);
    PropertyDetails other_details = desc->GetDetails(i);
    if (details.kind() != other_details.kind() ||
        details.location() != other_details.location() ||
        !details.representation().Equals(other_details.representation())) {
      return false;
    }
  }
  return true;
}

// static
Handle<WeakArrayList> PrototypeUsers::Add(Isolate* isolate,
                                          Handle<WeakArrayList> array,
                                          DirectHandle<Map> value,
                                          int* assigned_index) {
  int length = array->length();
  if (length == 0) {
    // Uninitialized WeakArrayList; need to initialize empty_slot_index.
    array = WeakArrayList::EnsureSpace(isolate, array, kFirstIndex + 1);
    set_empty_slot_index(*array, kNoEmptySlotsMarker);
    array->Set(kFirstIndex, MakeWeak(*value));
    array->set_length(kFirstIndex + 1);
    if (assigned_index != nullptr) *assigned_index = kFirstIndex;
    return array;
  }

  // If the array has unfilled space at the end, use it.
  if (!array->IsFull()) {
    array->Set(length, MakeWeak(*value));
    array->set_length(length + 1);
    if (assigned_index != nullptr) *assigned_index = length;
    return array;
  }

  // If there are empty slots, use one of them.
  int empty_slot = Smi::ToInt(empty_slot_index(*array));

  if (empty_slot == kNoEmptySlotsMarker) {
    // GCs might have cleared some references, rescan the array for empty slots.
    PrototypeUsers::ScanForEmptySlots(*array);
    empty_slot = Smi::ToInt(empty_slot_index(*array));
  }

  if (empty_slot != kNoEmptySlotsMarker) {
    DCHECK_GE(empty_slot, kFirstIndex);
    CHECK_LT(empty_slot, array->length());
    int next_empty_slot = array->Get(empty_slot).ToSmi().value();

    array->Set(empty_slot, MakeWeak(*value));
    if (assigned_index != nullptr) *assigned_index = empty_slot;

    set_empty_slot_index(*array, next_empty_slot);
    return array;
  } else {
    DCHECK_EQ(empty_slot, kNoEmptySlotsMarker);
  }

  // Array full and no empty slots. Grow the array.
  array = WeakArrayList::EnsureSpace(isolate, array, length + 1);
  array->Set(length, MakeWeak(*value));
  array->set_length(length + 1);
  if (assigned_index != nullptr) *assigned_index = length;
  return array;
}

// static
void PrototypeUsers::ScanForEmptySlots(Tagged<WeakArrayList> array) {
  for (int i = kFirstIndex; i < array->length(); i++) {
    if (array->Get(i).IsCleared()) {
      PrototypeUsers::MarkSlotEmpty(array, i);
    }
  }
}

Tagged<WeakArrayList> PrototypeUsers::Compact(DirectHandle<WeakArrayList> array,
                                              Heap* heap,
                                              CompactionCallback callback,
                                              AllocationType allocation) {
  if (array->length() == 0) {
    return *array;
  }
  int new_length = kFirstIndex + array->CountLiveWeakReferences();
  if (new_length == array->length()) {
    return *array;
  }

  DirectHandle<WeakArrayList> new_array = WeakArrayList::EnsureSpace(
      heap->isolate(),
      handle(ReadOnlyRoots(heap).empty_weak_array_list(), heap->isolate()),
      new_length, allocation);
  // Allocation might have caused GC and turned some of the elements into
  // cleared weak heap objects. Count the number of live objects again.
  int copy_to = kFirstIndex;
  for (int i = kFirstIndex; i < array->length(); i++) {
    Tagged<MaybeObject> element = array->Get(i);
    Tagged<HeapObject> value;
    if (element.GetHeapObjectIfWeak(&value)) {
      callback(value, i, copy_to);
      new_array->Set(copy_to++, element);
    } else {
      DCHECK(element.IsCleared() || element.IsSmi());
    }
  }
  new_array->set_length(copy_to);
  set_empty_slot_index(*new_array, kNoEmptySlotsMarker);
  return *new_array;
}

template <typename IsolateT>
Handle<DescriptorArray> DescriptorArray::Allocate(IsolateT* isolate,
                                                  int nof_descriptors,
                                                  int slack,
                                                  AllocationType allocation) {
  return nof_descriptors + slack == 0
             ? isolate->factory()->empty_descriptor_array()
             : isolate->factory()->NewDescriptorArray(nof_descriptors, slack,
                                                      allocation);
}
template Handle<DescriptorArray> DescriptorArray::Allocate(
    Isolate* isolate, int nof_descriptors, int slack,
    AllocationType allocation);
template Handle<DescriptorArray> DescriptorArray::Allocate(
    LocalIsolate* isolate, int nof_descriptors, int slack,
    AllocationType allocation);

void DescriptorArray::Initialize(Tagged<EnumCache> empty_enum_cache,
                                 Tagged<HeapObject> undefined_value,
                                 int nof_descriptors, int slack,
                                 uint32_t raw_gc_state) {
  DCHECK_GE(nof_descriptors, 0);
  DCHECK_GE(slack, 0);
  DCHECK_LE(nof_descriptors + slack, kMaxNumberOfDescriptors);
  set_number_of_all_descriptors(nof_descriptors + slack);
  set_number_of_descriptors(nof_descriptors);
  set_raw_gc_state(raw_gc_state, kRelaxedStore);
  set_enum_cache(empty_enum_cache, SKIP_WRITE_BARRIER);
  MemsetTagged(GetDescriptorSlot(0), undefined_value,
               number_of_all_descriptors() * kEntrySize);
}

void DescriptorArray::ClearEnumCache() {
  set_enum_cache(GetReadOnlyRoots().empty_enum_cache(), SKIP_WRITE_BARRIER);
}

void DescriptorArray::Replace(InternalIndex index, Descriptor* descriptor) {
  descriptor->SetSortedKeyIndex(GetSortedKeyIndex(index.as_int()));
  Set(index, descriptor);
}

// static
void DescriptorArray::InitializeOrChangeEnumCache(
    DirectHandle<DescriptorArray> descriptors, Isolate* isolate,
    DirectHandle<FixedArray> keys, DirectHandle<FixedArray> indices,
    AllocationType allocation_if_initialize) {
  Tagged<EnumCache> enum_cache = descriptors->enum_cache();
  if (enum_cache == ReadOnlyRoots(isolate).empty_enum_cache()) {
    enum_cache = *isolate->factory()->NewEnumCache(keys, indices,
                                                   allocation_if_initialize);
    descriptors->set_enum_cache(enum_cache);
  } else {
    enum_cache->set_keys(*keys);
    enum_cache->set_indices(*indices);
  }
}

void DescriptorArray::CopyFrom(InternalIndex index,
                               Tagged<DescriptorArray> src) {
  PropertyDetails details = src->GetDetails(index);
  Set(index, src->GetKey(index), src->GetValue(index), details);
}

void DescriptorArray::Sort() {
  // In-place heap sort.
  const int len = number_of_descriptors();
  // Reset sorting since the descriptor array might contain invalid pointers.
  for (int i = 0; i < len; ++i) SetSortedKey(i, i);
  // Bottom-up max-heap construction.
  // Index of the last node with children.
  int max_parent_index = (len / 2) - 1;
  for (int i = max_parent_index; i >= 0; --i) {
    int parent_index = i;
    const uint32_t parent_hash = GetSortedKey(i)->hash();
    while (parent_index <= max_parent_index) {
      int child_index = 2 * parent_index + 1;
      uint32_t child_hash = GetSortedKey(child_index)->hash();
      if (child_index + 1 < len) {
        uint32_t right_child_hash = GetSortedKey(child_index + 1)->hash();
        if (right_child_hash > child_hash) {
          child_index++;
          child_hash = right_child_hash;
        }
      }
      if (child_hash <= parent_hash) break;
      SwapSortedKeys(parent_index, child_index);
      // Now element at child_index could be < its children.
      parent_index = child_index;  // parent_hash remains correct.
    }
  }

  // Extract elements and create sorted array.
  for (int i = len - 1; i > 0; --i) {
    // Put max element at the back of the array.
    SwapSortedKeys(0, i);
    // Shift down the new top element.
    int parent_index = 0;
    const uint32_t parent_hash = GetSortedKey(parent_index)->hash();
    max_parent_index = (i / 2) - 1;
    while (parent_index <= max_parent_index) {
      int child_index = parent_index * 2 + 1;
      uint32_t child_hash = GetSortedKey(child_index)->hash();
      if (child_index + 1 < i) {
        uint32_t right_child_hash = GetSortedKey(child_index + 1)->hash();
        if (right_child_hash > child_hash) {
          child_index++;
          child_hash = right_child_hash;
        }
      }
      if (child_hash <= parent_hash) break;
      SwapSortedKeys(parent_index, child_index);
      parent_index = child_index;
    }
  }
  DCHECK(IsSortedNoDuplicates());
}

void DescriptorArray::CheckNameCollisionDuringInsertion(Descriptor* desc,
                                                        uint32_t desc_hash,
                                                        int insertion_index) {
  DCHECK_GE(insertion_index, 0);
  DCHECK_LE(insertion_index, number_of_all_descriptors());

  if (insertion_index <= 0) return;

  for (int i = insertion_index; i > 0; --i) {
    Tagged<Name> current_key = GetSortedKey(i - 1);
    if (current_key->hash() != desc_hash) return;
    CHECK(current_key != *desc->GetKey());
  }
}

Handle<AccessorPair> AccessorPair::Copy(Isolate* isolate,
                                        DirectHandle<AccessorPair> pair) {
  Handle<AccessorPair> copy = isolate->factory()->NewAccessorPair();
  DisallowGarbageCollection no_gc;
  Tagged<AccessorPair> raw_src = *pair;
  Tagged<AccessorPair> raw_copy = *copy;
  raw_copy->set_getter(raw_src->getter());
  raw_copy->set_setter(raw_src->setter());
  return copy;
}

Handle<JSAny> AccessorPair::GetComponent(
    Isolate* isolate, Handle<NativeContext> native_context,
    DirectHandle<AccessorPair> accessor_pair, AccessorComponent component) {
  Handle<Object> accessor(accessor_pair->get(component), isolate);
  if (IsFunctionTemplateInfo(*accessor)) {
    // TODO(v8:5962): pass the right name here: "get "/"set " + prop.
    Handle<JSFunction> function =
        ApiNatives::InstantiateFunction(isolate, native_context,
                                        Cast<FunctionTemplateInfo>(accessor))
            .ToHandleChecked();
    accessor_pair->set(component, *function, kReleaseStore);
    return function;
  }
  if (IsNull(*accessor, isolate)) {
    return isolate->factory()->undefined_value();
  }
  return Cast<JSAny>(accessor);
}

#ifdef DEBUG
bool DescriptorArray::IsEqualTo(Tagged<DescriptorArray> other) {
  if (number_of_all_descriptors() != other->number_of_all_descriptors()) {
    return false;
  }
  for (InternalIndex i : InternalIndex::Range(number_of_descriptors())) {
    if (GetKey(i) != other->GetKey(i)) return false;
    if (GetDetails(i).AsSmi() != other->GetDetails(i).AsSmi()) return false;
    if (GetValue(i) != other->GetValue(i)) return false;
  }
  return true;
}
#endif

// static
MaybeHandle<String> Name::ToFunctionName(Isolate* isolate, Handle<Name> name) {
  if (IsString(*name)) return Cast<String>(name);
  // ES6 section 9.2.11 SetFunctionName, step 4.
  Handle<Object> description(Cast<Symbol>(name)->description(), isolate);
  if (IsUndefined(*description, isolate)) {
    return isolate->factory()->empty_string();
  }
  IncrementalStringBuilder builder(isolate);
  builder.AppendCharacter('[');
  builder.AppendString(Cast<String>(description));
  builder.AppendCharacter(']');
  return indirect_handle(builder.Finish(), isolate);
}

// static
MaybeHandle<String> Name::ToFunctionName(Isolate* isolate, Handle<Name> name,
                                         DirectHandle<String> prefix) {
  Handle<String> name_string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, name_string,
                             ToFunctionName(isolate, name));
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(prefix);
  builder.AppendCharacter(' ');
  builder.AppendString(name_string);
  return indirect_handle(builder.Finish(), isolate);
}

void Relocatable::PostGarbageCollectionProcessing(Isolate* isolate) {
  Relocatable* current = isolate->relocatable_top();
  while (current != nullptr) {
    current->PostGarbageCollection();
    current = current->prev_;
  }
}

// Reserve space for statics needing saving and restoring.
int Relocatable::ArchiveSpacePerThread() { return sizeof(Relocatable*); }

// Archive statics that are thread-local.
char* Relocatable::ArchiveState(Isolate* isolate, char* to) {
  *reinterpret_cast<Relocatable**>(to) = isolate->relocatable_top();
  isolate->set_relocatable_top(nullptr);
  return to + ArchiveSpacePerThread();
}

// Restore statics that are thread-local.
char* Relocatable::RestoreState(Isolate* isolate, char* from) {
  isolate->set_relocatable_top(*reinterpret_cast<Relocatable**>(from));
  return from + ArchiveSpacePerThread();
}

char* Relocatable::Iterate(RootVisitor* v, char* thread_storage) {
  Relocatable* top = *reinterpret_cast<Relocatable**>(thread_storage);
  Iterate(v, top);
  return thread_storage + ArchiveSpacePerThread();
}

void Relocatable::Iterate(Isolate* isolate, RootVisitor* v) {
  Iterate(v, isolate->relocatable_top());
}

void Relocatable::Iterate(RootVisitor* v, Relocatable* top) {
  Relocatable* current = top;
  while (current != nullptr) {
    current->IterateInstance(v);
    current = current->prev_;
  }
}

namespace {

template <typename sinkchar>
void WriteFixedArrayToFlat(Tagged<FixedArray> fixed_array, int length,
                           Tagged<String> separator, sinkchar* sink,
                           int sink_length) {
  DisallowGarbageCollection no_gc;
  CHECK_GT(length, 0);
  CHECK_LE(length, fixed_array->length());
#ifdef DEBUG
  sinkchar* sink_end = sink + sink_length;
#endif

  const int separator_length = separator->length();
  const bool use_one_byte_separator_fast_path =
      separator_length == 1 && sizeof(sinkchar) == 1 &&
      StringShape(separator).IsSequentialOneByte();
  uint8_t separator_one_char;
  if (use_one_byte_separator_fast_path) {
    CHECK(StringShape(separator).IsSequentialOneByte());
    CHECK_EQ(separator->length(), 1);
    separator_one_char = Cast<SeqOneByteString>(separator)->GetChars(no_gc)[0];
  }

  uint32_t num_separators = 0;
  uint32_t repeat_last = 0;
  for (int i = 0; i < length; i++) {
    Tagged<Object> element = fixed_array->get(i);
    const bool element_is_special = IsSmi(element);

    // If element is a positive Smi, it represents the number of separators to
    // write. If it is a negative Smi, it reprsents the number of times the last
    // string is repeated.
    if (V8_UNLIKELY(element_is_special)) {
      int count;
      CHECK(Object::ToInt32(element, &count));
      if (count > 0) {
        num_separators = count;
        //  Verify that Smis (number of separators) only occur when necessary:
        //    1) at the beginning
        //    2) at the end
        //    3) when the number of separators > 1
        //      - It is assumed that consecutive Strings will have one
        //      separator,
        //        so there is no need for a Smi.
        DCHECK(i == 0 || i == length - 1 || num_separators > 1);
      } else {
        repeat_last = -count;
        // Repeat is only possible when the previous element is not special.
        DCHECK_GT(i, 0);
        DCHECK(IsString(fixed_array->get(i - 1)));
      }
    }

    // Write separator(s) if necessary.
    if (num_separators > 0 && separator_length > 0) {
      // TODO(pwong): Consider doubling strategy employed by runtime-strings.cc
      //              WriteRepeatToFlat().
      // Fast path for single character, single byte separators.
      if (use_one_byte_separator_fast_path) {
        DCHECK_LE(sink + num_separators, sink_end);
        memset(sink, separator_one_char, num_separators);
        DCHECK_EQ(separator_length, 1);
        sink += num_separators;
      } else {
        for (uint32_t j = 0; j < num_separators; j++) {
          DCHECK_LE(sink + separator_length, sink_end);
          String::WriteToFlat(separator, sink, 0, separator_length);
          sink += separator_length;
        }
      }
      num_separators = 0;
    }

    // Repeat the last written string |repeat_last| times (including
    // separators).
    if (V8_UNLIKELY(repeat_last > 0)) {
      Tagged<Object> last_element = fixed_array->get(i - 1);
      int string_length = Cast<String>(last_element)->length();
      // The implemented logic requires that string length is > 0. Empty strings
      // are handled by repeating the separator (positive smi in the fixed
      // array) already.
      DCHECK_GT(string_length, 0);
      int length_with_sep = string_length + separator_length;
      // Only copy separators between elements, not at the start or beginning.
      sinkchar* copy_end =
          sink + (length_with_sep * repeat_last) - separator_length;
      int copy_length = length_with_sep;
      while (sink < copy_end - copy_length) {
        DCHECK_LE(sink + copy_length, sink_end);
        memcpy(sink, sink - copy_length, copy_length * sizeof(sinkchar));
        sink += copy_length;
        copy_length *= 2;
      }
      int remaining = static_cast<int>(copy_end - sink);
      if (remaining > 0) {
        DCHECK_LE(sink + remaining, sink_end);
        memcpy(sink, sink - remaining - separator_length,
               remaining * sizeof(sinkchar));
        sink += remaining;
      }
      repeat_last = 0;
      num_separators = 1;
    }

    if (V8_LIKELY(!element_is_special)) {
      DCHECK(IsString(element));
      Tagged<String> string = Cast<String>(element);
      const int string_length = string->length();

      DCHECK(string_length == 0 || sink < sink_end);
      String::WriteToFlat(string, sink, 0, string_length);
      sink += string_length;

      // Next string element, needs at least one separator preceding it.
      num_separators = 1;
    }
  }

  // Verify we have written to the end of the sink.
  DCHECK_EQ(sink, sink_end);
}

}  // namespace

// static
Address JSArray::ArrayJoinConcatToSequentialString(Isolate* isolate,
                                                   Address raw_fixed_array,
                                                   intptr_t length,
                                                   Address raw_separator,
                                                   Address raw_dest) {
  DisallowGarbageCollection no_gc;
  DisallowJavascriptExecution no_js(isolate);
  Tagged<FixedArray> fixed_array =
      Cast<FixedArray>(Tagged<Object>(raw_fixed_array));
  Tagged<String> separator = Cast<String>(Tagged<Object>(raw_separator));
  Tagged<String> dest = Cast<String>(Tagged<Object>(raw_dest));
  DCHECK(IsFixedArray(fixed_array));
  DCHECK(StringShape(dest).IsSequentialOneByte() ||
         StringShape(dest).IsSequentialTwoByte());

  if (StringShape(dest).IsSequentialOneByte()) {
    WriteFixedArrayToFlat(fixed_array, static_cast<int>(length), separator,
                          Cast<SeqOneByteString>(dest)->GetChars(no_gc),
                          dest->length());
  } else {
    DCHECK(StringShape(dest).IsSequentialTwoByte());
    WriteFixedArrayToFlat(fixed_array, static_cast<int>(length), separator,
                          Cast<SeqTwoByteString>(dest)->GetChars(no_gc),
                          dest->length());
  }
  return dest.ptr();
}

uint32_t StringHasher::MakeArrayIndexHash(uint32_t value, int length) {
  // For array indexes mix the length into the hash as an array index could
  // be zero.
  DCHECK_GT(length, 0);
  DCHECK_LE(length, String::kMaxArrayIndexSize);
  DCHECK(TenToThe(String::kMaxCachedArrayIndexLength) <
         (1 << String::kArrayIndexValueBits));

  value <<= String::ArrayIndexValueBits::kShift;
  value |= length << String::ArrayIndexLengthBits::kShift;

  DCHECK(String::IsIntegerIndex(value));
  DCHECK_EQ(length <= String::kMaxCachedArrayIndexLength,
            Name::ContainsCachedArrayIndex(value));
  return value;
}

void Oddball::Initialize(Isolate* isolate, DirectHandle<Oddball> oddball,
                         const char* to_string, Handle<Number> to_number,
                         const char* type_of, uint8_t kind) {
  DirectHandle<String> internalized_to_string =
      isolate->factory()->InternalizeUtf8String(to_string);
  DirectHandle<String> internalized_type_of =
      isolate->factory()->InternalizeUtf8String(type_of);
  if (IsHeapNumber(*to_number)) {
    oddball->set_to_number_raw_as_bits(
        Cast<HeapNumber>(to_number)->value_as_bits());
  } else {
    oddball->set_to_number_raw(Object::NumberValue(*to_number));
  }
  oddball->set_to_number(*to_number);
  oddball->set_to_string(*internalized_to_string);
  oddball->set_type_of(*internalized_type_of);
  oddball->set_kind(kind);
}

// static
int Script::GetEvalPosition(Isolate* isolate, DirectHandle<Script> script) {
  DCHECK(script->compilation_type() == Script::CompilationType::kEval);
  int position = script->eval_from_position();
  if (position < 0) {
    // Due to laziness, the position may not have been translated from code
    // offset yet, which would be encoded as negative integer. In that case,
    // translate and set the position.
    if (!script->has_eval_from_shared()) {
      position = 0;
    } else {
      Handle<SharedFunctionInfo> shared =
          handle(script->eval_from_shared(), isolate);
      SharedFunctionInfo::EnsureSourcePositionsAvailable(isolate, shared);
      position =
          shared->abstract_code(isolate)->SourcePosition(isolate, -position);
    }
    DCHECK_GE(position, 0);
    script->set_eval_from_position(position);
  }
  return position;
}

String::LineEndsVector Script::GetLineEnds(Isolate* isolate,
                                           DirectHandle<Script> script) {
  DCHECK(!script->has_line_ends());
  Tagged<Object> src_obj = script->source();
  if (IsString(src_obj)) {
    Handle<String> src(Cast<String>(src_obj), isolate);
    return String::CalculateLineEndsVector(isolate, src, true);
  }

  return String::LineEndsVector();
}

template <typename IsolateT>
// static
void Script::InitLineEndsInternal(IsolateT* isolate,
                                  DirectHandle<Script> script) {
  DCHECK(!script->has_line_ends());
  DCHECK(script->CanHaveLineEnds());
  Tagged<Object> src_obj = script->source();
  if (!IsString(src_obj)) {
    DCHECK(IsUndefined(src_obj, isolate));
    script->set_line_ends(ReadOnlyRoots(isolate).empty_fixed_array());
  } else {
    DCHECK(IsString(src_obj));
    Handle<String> src(Cast<String>(src_obj), isolate);
    DirectHandle<FixedArray> array =
        String::CalculateLineEnds(isolate, src, true);
    script->set_line_ends(*array);
  }
  DCHECK(IsFixedArray(script->line_ends()));
  DCHECK(script->has_line_ends());
}

void Script::SetSource(Isolate* isolate, DirectHandle<Script> script,
                       DirectHandle<String> source) {
  script->set_source(*source);
  if (isolate->NeedsSourcePositions()) InitLineEnds(isolate, script);
}

template EXPORT_TEMPLATE_DEFINE(
    V8_EXPORT_PRIVATE) void Script::InitLineEndsInternal(Isolate* isolate,
                                                         DirectHandle<Script>
                                                             script);
template EXPORT_TEMPLATE_DEFINE(
    V8_EXPORT_PRIVATE) void Script::InitLineEndsInternal(LocalIsolate* isolate,
                                                         DirectHandle<Script>
                                                             script);

bool Script::GetPositionInfo(DirectHandle<Script> script, int position,
                             PositionInfo* info, OffsetFlag offset_flag) {
#if V8_ENABLE_WEBASSEMBLY
  // For wasm, we do not create an artificial line_ends array, but do the
  // translation directly.
#ifdef DEBUG
  if (script->type() == Type::kWasm) {
    DCHECK(script->has_line_ends());
    DCHECK_EQ(Cast<FixedArray>(script->line_ends())->length(), 0);
  }
#endif  // DEBUG
#endif  // V8_ENABLE_WEBASSEMBLY
  InitLineEnds(script->GetIsolate(), script);
  return script->GetPositionInfo(position, info, offset_flag);
}

bool Script::IsSubjectToDebugging() const {
  switch (type()) {
    case Type::kNormal:
#if V8_ENABLE_WEBASSEMBLY
    case Type::kWasm:
#endif  // V8_ENABLE_WEBASSEMBLY
      return true;
    case Type::kNative:
    case Type::kInspector:
    case Type::kExtension:
      return false;
  }
  UNREACHABLE();
}

bool Script::IsUserJavaScript() const {
  return type() == Script::Type::kNormal;
}

#if V8_ENABLE_WEBASSEMBLY
bool Script::ContainsAsmModule() {
  DisallowGarbageCollection no_gc;
  SharedFunctionInfo::ScriptIterator iter(this->GetIsolate(), *this);
  for (Tagged<SharedFunctionInfo> sfi = iter.Next(); !sfi.is_null();
       sfi = iter.Next()) {
    if (sfi->HasAsmWasmData()) return true;
  }
  return false;
}
#endif  // V8_ENABLE_WEBASSEMBLY

namespace {

template <typename Char>
bool GetPositionInfoSlowImpl(base::Vector<Char> source, int position,
                             Script::PositionInfo* info) {
  DCHECK(DisallowPositionInfoSlow::IsAllowed());
  if (position < 0) {
    position = 0;
  }
  int line = 0;
  const auto begin = std::cbegin(source);
  const auto end = std::cend(source);
  for (auto line_begin = begin; line_begin < end;) {
    const auto line_end = std::find(line_begin, end, '\n');
    if (position <= (line_end - begin)) {
      info->line = line;
      info->column = static_cast<int>((begin + position) - line_begin);
      info->line_start = static_cast<int>(line_begin - begin);
      info->line_end = static_cast<int>(line_end - begin);
      return true;
    }
    ++line;
    line_begin = line_end + 1;
  }
  return false;
}
bool GetPositionInfoSlow(const Tagged<Script> script, int position,
                         const DisallowGarbageCollection& no_gc,
                         Script::PositionInfo* info) {
  if (!IsString(script->source())) {
    return false;
  }
  auto source = Cast<String>(script->source());
  const auto flat = source->GetFlatContent(no_gc);
  return flat.IsOneByte()
             ? GetPositionInfoSlowImpl(flat.ToOneByteVector(), position, info)
             : GetPositionInfoSlowImpl(flat.ToUC16Vector(), position, info);
}

int GetLineEnd(const String::LineEndsVector& vector, int line) {
  return vector[line];
}

int GetLineEnd(const Tagged<FixedArray>& array, int line) {
  return Smi::ToInt(array->get(line));
}

int GetLength(const String::LineEndsVector& vector) {
  return static_cast<int>(vector.size());
}

int GetLength(const Tagged<FixedArray>& array) { return array->length(); }

template <typename LineEndsContainer>
bool GetLineEndsContainerPositionInfo(const LineEndsContainer& ends,
                                      int position, Script::PositionInfo* info,
                                      const DisallowGarbageCollection& no_gc) {
  const int ends_len = GetLength(ends);
  if (ends_len == 0) return false;

  // Return early on invalid positions. Negative positions behave as if 0 was
  // passed, and positions beyond the end of the script return as failure.
  if (position < 0) {
    position = 0;
  } else if (position > GetLineEnd(ends, ends_len - 1)) {
    return false;
  }

  // Determine line number by doing a binary search on the line ends array.
  if (GetLineEnd(ends, 0) >= position) {
    info->line = 0;
    info->line_start = 0;
    info->column = position;
  } else {
    int left = 0;
    int right = ends_len - 1;

    while (right > 0) {
      DCHECK_LE(left, right);
      const int mid = left + (right - left) / 2;
      if (position > GetLineEnd(ends, mid)) {
        left = mid + 1;
      } else if (position <= GetLineEnd(ends, mid - 1)) {
        right = mid - 1;
      } else {
        info->line = mid;
        break;
      }
    }
    DCHECK(GetLineEnd(ends, info->line) >= position &&
           GetLineEnd(ends, info->line - 1) < position);
    info->line_start = GetLineEnd(ends, info->line - 1) + 1;
    info->column = position - info->line_start;
  }

  return true;
}

}  // namespace

void Script::AddPositionInfoOffset(PositionInfo* info,
                                   OffsetFlag offset_flag) const {
  // Add offsets if requested.
  if (offset_flag == OffsetFlag::kWithOffset) {
    if (info->line == 0) {
      info->column += column_offset();
    }
    info->line += line_offset();
  } else {
    DCHECK_EQ(offset_flag, OffsetFlag::kNoOffset);
  }
}

template <typename LineEndsContainer>
bool Script::GetPositionInfoInternal(
    const LineEndsContainer& ends, int position, Script::PositionInfo* info,
    const DisallowGarbageCollection& no_gc) const {
  if (!GetLineEndsContainerPositionInfo(ends, position, info, no_gc))
    return false;

  // Line end is position of the linebreak character.
  info->line_end = GetLineEnd(ends, info->line);
  if (info->line_end > 0) {
    DCHECK(IsString(source()));
    Tagged<String> src = Cast<String>(source());
    if (src->length() >= info->line_end &&
        src->Get(info->line_end - 1) == '\r') {
      info->line_end--;
    }
  }

  return true;
}

template bool Script::GetPositionInfoInternal<String::LineEndsVector>(
    const String::LineEndsVector& ends, int position,
    Script::PositionInfo* info, const DisallowGarbageCollection& no_gc) const;
template bool Script::GetPositionInfoInternal<Tagged<FixedArray>>(
    const Tagged<FixedArray>& ends, int position, Script::PositionInfo* info,
    const DisallowGarbageCollection& no_gc) const;

bool Script::GetPositionInfo(int position, PositionInfo* info,
                             OffsetFlag offset_flag) const {
  DisallowGarbageCollection no_gc;

#if V8_ENABLE_WEBASSEMBLY
  // For wasm, we use the byte offset as the column.
  if (type() == Script::Type::kWasm) {
    DCHECK_LE(0, position);
    wasm::NativeModule* native_module = wasm_native_module();
    const wasm::WasmModule* module = native_module->module();
    if (module->functions.empty()) return false;
    info->line = 0;
    info->column = position;
    info->line_start = module->functions[0].code.offset();
    info->line_end = module->functions.back().code.end_offset();
    return true;
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  if (!has_line_ends()) {
    // Slow mode: we do not have line_ends. We have to iterate through source.
    if (!GetPositionInfoSlow(*this, position, no_gc, info)) {
      return false;
    }
  } else {
    DCHECK(has_line_ends());
    Tagged<FixedArray> ends = Cast<FixedArray>(line_ends());

    if (!GetPositionInfoInternal(ends, position, info, no_gc)) return false;
  }

  AddPositionInfoOffset(info, offset_flag);

  return true;
}

bool Script::GetPositionInfoWithLineEnds(
    int position, PositionInfo* info, const String::LineEndsVector& line_ends,
    OffsetFlag offset_flag) const {
  DisallowGarbageCollection no_gc;
  if (!GetPositionInfoInternal(line_ends, position, info, no_gc)) return false;

  AddPositionInfoOffset(info, offset_flag);

  return true;
}

bool Script::GetLineColumnWithLineEnds(
    int position, int& line, int& column,
    const String::LineEndsVector& line_ends) {
  DisallowGarbageCollection no_gc;
  PositionInfo info;
  if (!GetLineEndsContainerPositionInfo(line_ends, position, &info, no_gc)) {
    line = -1;
    column = -1;
    return false;
  }

  line = info.line;
  column = info.column;

  return true;
}

int Script::GetColumnNumber(DirectHandle<Script> script, int code_pos) {
  PositionInfo info;
  GetPositionInfo(script, code_pos, &info);
  return info.column;
}

int Script::GetColumnNumber(int code_pos) const {
  PositionInfo info;
  GetPositionInfo(code_pos, &info);
  return info.column;
}

int Script::GetLineNumber(DirectHandle<Script> script, int code_pos) {
  PositionInfo info;
  GetPositionInfo(script, code_pos, &info);
  return info.line;
}

int Script::GetLineNumber(int code_pos) const {
  PositionInfo info;
  GetPositionInfo(code_pos, &info);
  return info.line;
}

Tagged<Object> Script::GetNameOrSourceURL() {
  // Keep in sync with ScriptNameOrSourceURL in messages.js.
  if (!IsUndefined(source_url())) return source_url();
  return name();
}

// static
Handle<String> Script::GetScriptHash(Isolate* isolate,
                                     DirectHandle<Script> script,
                                     bool forceForInspector) {
  if (script->origin_options().IsOpaque() && !forceForInspector) {
    return isolate->factory()->empty_string();
  }

  PtrComprCageBase cage_base(isolate);
  {
    Tagged<Object> maybe_source_hash = script->source_hash(cage_base);
    if (IsString(maybe_source_hash, cage_base)) {
      Handle<String> precomputed(Cast<String>(maybe_source_hash), isolate);
      if (precomputed->length() > 0) {
        return precomputed;
      }
    }
  }

  DirectHandle<String> src_text;
  {
    Tagged<Object> maybe_script_source = script->source(cage_base);

    if (!IsString(maybe_script_source, cage_base)) {
      return isolate->factory()->empty_string();
    }
    src_text = direct_handle(Cast<String>(maybe_script_source), isolate);
  }

  char formatted_hash[kSizeOfFormattedSha256Digest];

  std::unique_ptr<char[]> string_val = src_text->ToCString();
  size_t len = strlen(string_val.get());
  uint8_t hash[kSizeOfSha256Digest];
  SHA256_hash(string_val.get(), len, hash);
  FormatBytesToHex(formatted_hash, kSizeOfFormattedSha256Digest, hash,
                   kSizeOfSha256Digest);
  formatted_hash[kSizeOfSha256Digest * 2] = '\0';

  Handle<String> result =
      isolate->factory()->NewStringFromAsciiChecked(formatted_hash);
  script->set_source_hash(*result);
  return result;
}

template <typename IsolateT>
MaybeHandle<SharedFunctionInfo> Script::FindSharedFunctionInfo(
    DirectHandle<Script> script, IsolateT* isolate,
    FunctionLiteral* function_literal) {
  DCHECK(function_literal->shared_function_info().is_null());
  int function_literal_id = function_literal->function_literal_id();
  CHECK_NE(function_literal_id, kInvalidInfoId);
  // If this check fails, the problem is most probably the function id
  // renumbering done by AstFunctionLiteralIdReindexer; in particular, that
  // AstTraversalVisitor doesn't recurse properly in the construct which
  // triggers the mismatch.
  CHECK_LT(function_literal_id, script->infos()->length());
  Tagged<MaybeObject> shared = script->infos()->get(function_literal_id);
  Tagged<HeapObject> heap_object;
  if (!shared.GetHeapObject(&heap_object) ||
      IsUndefined(heap_object, isolate)) {
    return MaybeHandle<SharedFunctionInfo>();
  }
  Handle<SharedFunctionInfo> result(Cast<SharedFunctionInfo>(heap_object),
                                    isolate);
  function_literal->set_shared_function_info(result);
  return result;
}
template MaybeHandle<SharedFunctionInfo> Script::FindSharedFunctionInfo(
    DirectHandle<Script> script, Isolate* isolate,
    FunctionLiteral* function_literal);
template MaybeHandle<SharedFunctionInfo> Script::FindSharedFunctionInfo(
    DirectHandle<Script> script, LocalIsolate* isolate,
    FunctionLiteral* function_literal);

Script::Iterator::Iterator(Isolate* isolate)
    : iterator_(isolate->heap()->script_list()) {}

Tagged<Script> Script::Iterator::Next() {
  Tagged<Object> o = iterator_.Next();
  if (o != Tagged<Object>()) {
    return Cast<Script>(o);
  }
  return Script();
}

// static
void JSArray::Initialize(DirectHandle<JSArray> array, int capacity,
                         int length) {
  DCHECK_GE(capacity, 0);
  array->GetIsolate()->factory()->NewJSArrayStorage(
      array, length, capacity,
      ArrayStorageAllocationMode::INITIALIZE_ARRAY_ELEMENTS_WITH_HOLE);
}

Maybe<bool> JSArray::SetLength(Handle<JSArray> array, uint32_t new_length) {
  if (array->SetLengthWouldNormalize(new_length)) {
    JSObject::NormalizeElements(array);
  }
  return array->GetElementsAccessor()->SetLength(array, new_length);
}

// ES6: 9.5.2 [[SetPrototypeOf]] (V)
// static
Maybe<bool> JSProxy::SetPrototype(Isolate* isolate, DirectHandle<JSProxy> proxy,
                                  Handle<Object> value, bool from_javascript,
                                  ShouldThrow should_throw) {
  STACK_CHECK(isolate, Nothing<bool>());
  Handle<Name> trap_name = isolate->factory()->setPrototypeOf_string();
  // 1. Assert: Either Type(V) is Object or Type(V) is Null.
  DCHECK(IsJSReceiver(*value) || IsNull(*value, isolate));
  // 2. Let handler be the value of the [[ProxyHandler]] internal slot of O.
  Handle<Object> handler(proxy->handler(), isolate);
  // 3. If handler is null, throw a TypeError exception.
  // 4. Assert: Type(handler) is Object.
  if (proxy->IsRevoked()) {
    isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kProxyRevoked, trap_name));
    return Nothing<bool>();
  }
  // 5. Let target be the value of the [[ProxyTarget]] internal slot.
  Handle<JSReceiver> target(Cast<JSReceiver>(proxy->target()), isolate);
  // 6. Let trap be ? GetMethod(handler, "getPrototypeOf").
  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap,
      Object::GetMethod(isolate, Cast<JSReceiver>(handler), trap_name),
      Nothing<bool>());
  // 7. If trap is undefined, then return target.[[SetPrototypeOf]]().
  if (IsUndefined(*trap, isolate)) {
    return JSReceiver::SetPrototype(isolate, target, value, from_javascript,
                                    should_throw);
  }
  // 8. Let booleanTrapResult be ToBoolean(? Call(trap, handler, «target, V»)).
  Handle<Object> argv[] = {target, value};
  Handle<Object> trap_result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, trap_result,
      Execution::Call(isolate, trap, handler, arraysize(argv), argv),
      Nothing<bool>());
  bool bool_trap_result = Object::BooleanValue(*trap_result, isolate);
  // 9. If booleanTrapResult is false, return false.
  if (!bool_trap_result) {
    RETURN_FAILURE(
        isolate, should_throw,
        NewTypeError(MessageTemplate::kProxyTrapReturnedFalsish, trap_name));
  }
  // 10. Let extensibleTarget be ? IsExtensible(target).
  Maybe<bool> is_extensible = JSReceiver::IsExtensible(isolate, target);
  if (is_extensible.IsNothing()) return Nothing<bool>();
  // 11. If extensibleTarget is true, return true.
  if (is_extensible.FromJust()) {
    if (bool_trap_result) return Just(true);
    RETURN_FAILURE(
        isolate, should_throw,
        NewTypeError(MessageTemplate::kProxyTrapReturnedFalsish, trap_name));
  }
  // 12. Let targetProto be ? target.[[GetPrototypeOf]]().
  Handle<Object> target_proto;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, target_proto,
                                   JSReceiver::GetPrototype(isolate, target),
                                   Nothing<bool>());
  // 13. If SameValue(V, targetProto) is false, throw a TypeError exception.
  if (bool_trap_result && !Object::SameValue(*value, *target_proto)) {
    isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kProxySetPrototypeOfNonExtensible));
    return Nothing<bool>();
  }
  // 14. Return true.
  return Just(true);
}

bool JSArray::SetLengthWouldNormalize(uint32_t new_length) {
  if (!HasFastElements()) return false;
  uint32_t capacity = static_cast<uint32_t>(elements()->length());
  uint32_t new_capacity;
  return JSArray::SetLengthWouldNormalize(GetHeap(), new_length) &&
         ShouldConvertToSlowElements(*this, capacity, new_length - 1,
                                     &new_capacity);
}

void AllocationSite::ResetPretenureDecision() {
  set_pretenure_decision(kUndecided);
  set_memento_found_count(0);
  set_memento_create_count(0);
}

AllocationType AllocationSite::GetAllocationType() const {
  PretenureDecision mode = pretenure_decision();
  // Zombie objects "decide" to be untenured.
  return mode == kTenure ? AllocationType::kOld : AllocationType::kYoung;
}

bool AllocationSite::IsNested() {
  DCHECK(v8_flags.trace_track_allocation_sites);
  Tagged<Object> current = boilerplate()->GetHeap()->allocation_sites_list();
  while (IsAllocationSite(current)) {
    Tagged<AllocationSite> current_site = Cast<AllocationSite>(current);
    if (current_site->nested_site() == *this) {
      return true;
    }
    current = current_site->weak_next();
  }
  return false;
}

bool AllocationSite::ShouldTrack(ElementsKind from, ElementsKind to) {
  if (!V8_ALLOCATION_SITE_TRACKING_BOOL) return false;
  return IsMoreGeneralElementsKindTransition(from, to);
}

const char* AllocationSite::PretenureDecisionName(PretenureDecision decision) {
  switch (decision) {
    case kUndecided:
      return "undecided";
    case kDontTenure:
      return "don't tenure";
    case kMaybeTenure:
      return "maybe tenure";
    case kTenure:
      return "tenure";
    case kZombie:
      return "zombie";
    default:
      UNREACHABLE();
  }
}

// static
bool JSArray::MayHaveReadOnlyLength(Tagged<Map> js_array_map) {
  DCHECK(IsJSArrayMap(js_array_map));
  if (js_array_map->is_dictionary_map()) return true;

  // Fast path: "length" is the first fast property of arrays with non
  // dictionary properties. Since it's not configurable, it's guaranteed to be
  // the first in the descriptor array.
  InternalIndex first(0);
  DCHECK(js_array_map->instance_descriptors()->GetKey(first) ==
         js_array_map->GetReadOnlyRoots().length_string());
  return js_array_map->instance_descriptors()->GetDetails(first).IsReadOnly();
}

bool JSArray::HasReadOnlyLength(Handle<JSArray> array) {
  Tagged<Map> map = array->map();

  // If map guarantees that there can't be a read-only length, we are done.
  if (!MayHaveReadOnlyLength(map)) return false;

  // Look at the object.
  Isolate* isolate = array->GetIsolate();
  LookupIterator it(isolate, array, isolate->factory()->length_string(), array,
                    LookupIterator::OWN_SKIP_INTERCEPTOR);
  CHECK_EQ(LookupIterator::ACCESSOR, it.state());
  return it.IsReadOnly();
}

bool JSArray::WouldChangeReadOnlyLength(Handle<JSArray> array, uint32_t index) {
  uint32_t length = 0;
  CHECK(Object::ToArrayLength(array->length(), &length));
  if (length <= index) return HasReadOnlyLength(array);
  return false;
}

const char* Symbol::PrivateSymbolToName() const {
  ReadOnlyRoots roots = GetReadOnlyRoots();
#define SYMBOL_CHECK_AND_PRINT(_, name) \
  if (this == roots.name()) return #name;
  PRIVATE_SYMBOL_LIST_GENERATOR(SYMBOL_CHECK_AND_PRINT, /* not used */)
#undef SYMBOL_CHECK_AND_PRINT
  return "UNKNOWN";
}

v8::Promise::PromiseState JSPromise::status() const {
  int value = flags() & StatusBits::kMask;
  DCHECK(value == 0 || value == 1 || value == 2);
  return static_cast<v8::Promise::PromiseState>(value);
}

void JSPromise::set_status(Promise::PromiseState status) {
  int value = flags() & ~StatusBits::kMask;
  set_flags(value | status);
}

// static
const char* JSPromise::Status(v8::Promise::PromiseState status) {
  switch (status) {
    case v8::Promise::kFulfilled:
      return "fulfilled";
    case v8::Promise::kPending:
      return "pending";
    case v8::Promise::kRejected:
      return "rejected";
  }
  UNREACHABLE();
}

int JSPromise::async_task_id() const {
  return AsyncTaskIdBits::decode(flags());
}

void JSPromise::set_async_task_id(int id) {
  set_flags(AsyncTaskIdBits::update(flags(), id));
}

// static
Handle<Object> JSPromise::Fulfill(DirectHandle<JSPromise> promise,
                                  DirectHandle<Object> value) {
  Isolate* const isolate = promise->GetIsolate();

#ifdef V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
  if (isolate->HasContextPromiseHooks()) {
    isolate->raw_native_context()->RunPromiseHook(
        PromiseHookType::kResolve, promise,
        isolate->factory()->undefined_value());
  }
#endif

  // 1. Assert: The value of promise.[[PromiseState]] is "pending".
  CHECK_EQ(Promise::kPending, promise->status());

  // 2. Let reactions be promise.[[PromiseFulfillReactions]].
  DirectHandle<Object> reactions(promise->reactions(), isolate);

  // 3. Set promise.[[PromiseResult]] to value.
  // 4. Set promise.[[PromiseFulfillReactions]] to undefined.
  // 5. Set promise.[[PromiseRejectReactions]] to undefined.
  promise->set_reactions_or_result(Cast<JSAny>(*value));

  // 6. Set promise.[[PromiseState]] to "fulfilled".
  promise->set_status(Promise::kFulfilled);

  // 7. Return TriggerPromiseReactions(reactions, value).
  return TriggerPromiseReactions(isolate, reactions, value,
                                 PromiseReaction::kFulfill);
}

static void MoveMessageToPromise(Isolate* isolate, Handle<JSPromise> promise) {
  if (!isolate->has_pending_message()) return;

  if (isolate->debug()->is_active()) {
    Handle<Object> message = handle(isolate->pending_message(), isolate);
    Handle<Symbol> key = isolate->factory()->promise_debug_message_symbol();
    Object::SetProperty(isolate, promise, key, message,
                        StoreOrigin::kMaybeKeyed,
                        Just(ShouldThrow::kThrowOnError))
        .Assert();
  }

  // The message object for a rejected promise was only stored for this purpose.
  // Clear it, otherwise we might leak memory.
  isolate->clear_pending_message();
}

// static
Handle<Object> JSPromise::Reject(Handle<JSPromise> promise,
                                 Handle<Object> reason, bool debug_event) {
  Isolate* const isolate = promise->GetIsolate();
  DCHECK(
      !reinterpret_cast<v8::Isolate*>(isolate)->GetCurrentContext().IsEmpty());

  MoveMessageToPromise(isolate, promise);

  if (debug_event) isolate->debug()->OnPromiseReject(promise, reason);
  isolate->RunAllPromiseHooks(PromiseHookType::kResolve, promise,
                              isolate->factory()->undefined_value());

  // 1. Assert: The value of promise.[[PromiseState]] is "pending".
  CHECK_EQ(Promise::kPending, promise->status());

  // 2. Let reactions be promise.[[PromiseRejectReactions]].
  DirectHandle<Object> reactions(promise->reactions(), isolate);

  // 3. Set promise.[[PromiseResult]] to reason.
  // 4. Set promise.[[PromiseFulfillReactions]] to undefined.
  // 5. Set promise.[[PromiseRejectReactions]] to undefined.
  promise->set_reactions_or_result(Cast<JSAny>(*reason));

  // 6. Set promise.[[PromiseState]] to "rejected".
  promise->set_status(Promise::kRejected);

  // 7. If promise.[[PromiseIsHandled]] is false, perform
  //    HostPromiseRejectionTracker(promise, "reject").
  if (!promise->has_handler()) {
    isolate->ReportPromiseReject(promise, reason, kPromiseRejectWithNoHandler);
  }

  // 8. Return TriggerPromiseReactions(reactions, reason).
  return TriggerPromiseReactions(isolate, reactions, reason,
                                 PromiseReaction::kReject);
}

// https://tc39.es/ecma262/#sec-promise-resolve-functions
// static
MaybeHandle<Object> JSPromise::Resolve(Handle<JSPromise> promise,
                                       Handle<Object> resolution) {
  Isolate* const isolate = promise->GetIsolate();
  DCHECK(
      !reinterpret_cast<v8::Isolate*>(isolate)->GetCurrentContext().IsEmpty());

  isolate->RunPromiseHook(PromiseHookType::kResolve, promise,
                          isolate->factory()->undefined_value());

  // 7. If SameValue(resolution, promise) is true, then
  if (promise.is_identical_to(resolution)) {
    // a. Let selfResolutionError be a newly created TypeError object.
    Handle<Object> self_resolution_error = isolate->factory()->NewTypeError(
        MessageTemplate::kPromiseCyclic, resolution);
    // b. Return RejectPromise(promise, selfResolutionError).
    return Reject(promise, self_resolution_error);
  }

  // 8. If Type(resolution) is not Object, then
  if (!IsJSReceiver(*resolution)) {
    // a. Return FulfillPromise(promise, resolution).
    return Fulfill(promise, resolution);
  }

  // 9. Let then be Get(resolution, "then").
  MaybeHandle<Object> then;
  Handle<JSReceiver> receiver(Cast<JSReceiver>(resolution));

  // Make sure a lookup of "then" on any JSPromise whose [[Prototype]] is the
  // initial %PromisePrototype% yields the initial method. In addition this
  // protector also guards the negative lookup of "then" on the intrinsic
  // %ObjectPrototype%, meaning that such lookups are guaranteed to yield
  // undefined without triggering any side-effects.
  if (IsJSPromise(*receiver) &&
      isolate->IsInAnyContext(receiver->map()->prototype(),
                              Context::PROMISE_PROTOTYPE_INDEX) &&
      Protectors::IsPromiseThenLookupChainIntact(isolate)) {
    // We can skip the "then" lookup on {resolution} if its [[Prototype]]
    // is the (initial) Promise.prototype and the Promise#then protector
    // is intact, as that guards the lookup path for the "then" property
    // on JSPromise instances which have the (initial) %PromisePrototype%.
    then = isolate->promise_then();
  } else {
    then = JSReceiver::GetProperty(isolate, receiver,
                                   isolate->factory()->then_string());
  }

  // 10. If then is an abrupt completion, then
  Handle<Object> then_action;
  if (!then.ToHandle(&then_action)) {
    // The "then" lookup can cause termination.
    if (!isolate->is_catchable_by_javascript(isolate->exception())) {
      return kNullMaybeHandle;
    }

    // a. Return RejectPromise(promise, then.[[Value]]).
    Handle<Object> reason(isolate->exception(), isolate);
    isolate->clear_exception();
    return Reject(promise, reason, false);
  }

  // 11. Let thenAction be then.[[Value]].
  // 12. If IsCallable(thenAction) is false, then
  if (!IsCallable(*then_action)) {
    // a. Return FulfillPromise(promise, resolution).
    return Fulfill(promise, resolution);
  }

  // 13. Let job be NewPromiseResolveThenableJob(promise, resolution,
  //                                             thenAction).
  Handle<NativeContext> then_context;
  if (!JSReceiver::GetContextForMicrotask(Cast<JSReceiver>(then_action))
           .ToHandle(&then_context)) {
    then_context = isolate->native_context();
  }

  DirectHandle<PromiseResolveThenableJobTask> task =
      isolate->factory()->NewPromiseResolveThenableJobTask(
          promise, Cast<JSReceiver>(resolution), Cast<JSReceiver>(then_action),
          then_context);
  if (isolate->debug()->is_active() && IsJSPromise(*resolution)) {
    // Mark the dependency of the new {promise} on the {resolution}.
    Object::SetProperty(isolate, resolution,
                        isolate->factory()->promise_handled_by_symbol(),
                        promise)
        .Check();
  }
  MicrotaskQueue* microtask_queue = then_context->microtask_queue();
  if (microtask_queue) microtask_queue->EnqueueMicrotask(*task);

  // 15. Return undefined.
  return isolate->factory()->undefined_value();
}

// static
Handle<Object> JSPromise::TriggerPromiseReactions(
    Isolate* isolate, DirectHandle<Object> reactions,
    DirectHandle<Object> argument, PromiseReaction::Type type) {
  CHECK(IsSmi(*reactions) || IsPromiseReaction(*reactions));

  // We need to reverse the {reactions} here, since we record them
  // on the JSPromise in the reverse order.
  {
    DisallowGarbageCollection no_gc;
    Tagged<UnionOf<Smi, PromiseReaction>> current =
        Cast<UnionOf<Smi, PromiseReaction>>(*reactions);
    Tagged<UnionOf<Smi, PromiseReaction>> reversed = Smi::zero();
    while (!IsSmi(current)) {
      Tagged<UnionOf<Smi, PromiseReaction>> next =
          Cast<PromiseReaction>(current)->next();
      Cast<PromiseReaction>(current)->set_next(reversed);
      reversed = current;
      current = next;
    }
    reactions = direct_handle(reversed, isolate);
  }

  // Morph the {reactions} into PromiseReactionJobTasks
  // and push them onto the microtask queue.
  while (!IsSmi(*reactions)) {
    auto task = Cast<HeapObject>(reactions);
    auto reaction = Cast<PromiseReaction>(task);
    reactions = direct_handle(reaction->next(), isolate);

    // According to HTML, we use the context of the appropriate handler as the
    // context of the microtask. See step 3 of HTML's EnqueueJob:
    // https://html.spec.whatwg.org/C/#enqueuejob(queuename,-job,-arguments)
    DirectHandle<NativeContext> handler_context;

    DirectHandle<UnionOf<Undefined, JSCallable>> primary_handler;
    DirectHandle<UnionOf<Undefined, JSCallable>> secondary_handler;
    if (type == PromiseReaction::kFulfill) {
      primary_handler = direct_handle(reaction->fulfill_handler(), isolate);
      secondary_handler = direct_handle(reaction->reject_handler(), isolate);
    } else {
      primary_handler = direct_handle(reaction->reject_handler(), isolate);
      secondary_handler = direct_handle(reaction->fulfill_handler(), isolate);
    }

    bool has_handler_context = false;
    if (IsJSReceiver(*primary_handler)) {
      has_handler_context =
          JSReceiver::GetContextForMicrotask(Cast<JSReceiver>(primary_handler))
              .ToHandle(&handler_context);
    }
    if (!has_handler_context && IsJSReceiver(*secondary_handler)) {
      has_handler_context = JSReceiver::GetContextForMicrotask(
                                Cast<JSReceiver>(secondary_handler))
                                .ToHandle(&handler_context);
    }
    if (!has_handler_context) handler_context = isolate->native_context();

    static_assert(
        static_cast<int>(PromiseReaction::kSize) ==
        static_cast<int>(
            PromiseReactionJobTask::kSizeOfAllPromiseReactionJobTasks));
    if (type == PromiseReaction::kFulfill) {
      task->set_map(
          ReadOnlyRoots(isolate).promise_fulfill_reaction_job_task_map(),
          kReleaseStore);
      Cast<PromiseFulfillReactionJobTask>(task)->set_argument(*argument);
      Cast<PromiseFulfillReactionJobTask>(task)->set_context(*handler_context);
      static_assert(
          static_cast<int>(PromiseReaction::kFulfillHandlerOffset) ==
          static_cast<int>(PromiseFulfillReactionJobTask::kHandlerOffset));
      static_assert(
          static_cast<int>(PromiseReaction::kPromiseOrCapabilityOffset) ==
          static_cast<int>(
              PromiseFulfillReactionJobTask::kPromiseOrCapabilityOffset));
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
      static_assert(
          static_cast<int>(
              PromiseReaction::kContinuationPreservedEmbedderDataOffset) ==
          static_cast<int>(PromiseFulfillReactionJobTask::
                               kContinuationPreservedEmbedderDataOffset));
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
    } else {
      DisallowGarbageCollection no_gc;
      task->set_map(
          ReadOnlyRoots(isolate).promise_reject_reaction_job_task_map(),
          kReleaseStore);
      Cast<PromiseRejectReactionJobTask>(task)->set_argument(*argument);
      Cast<PromiseRejectReactionJobTask>(task)->set_context(*handler_context);
      Cast<PromiseRejectReactionJobTask>(task)->set_handler(*primary_handler);
      static_assert(
          static_cast<int>(PromiseReaction::kPromiseOrCapabilityOffset) ==
          static_cast<int>(
              PromiseRejectReactionJobTask::kPromiseOrCapabilityOffset));
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
      static_assert(
          static_cast<int>(
              PromiseReaction::kContinuationPreservedEmbedderDataOffset) ==
          static_cast<int>(PromiseRejectReactionJobTask::
                               kContinuationPreservedEmbedderDataOffset));
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
    }

    MicrotaskQueue* microtask_queue = handler_context->microtask_queue();
    if (microtask_queue) {
      microtask_queue->EnqueueMicrotask(*Cast<PromiseReactionJobTask>(task));
    }
  }

  return isolate->factory()->undefined_value();
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::IteratePrefix(ObjectVisitor* v) {
  BodyDescriptorBase::IteratePointers(*this, 0, kElementsStartOffset, v);
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::IterateElements(ObjectVisitor* v) {
  BodyDescriptorBase::IteratePointers(*this, kElementsStartOffset,
                                      SizeFor(length()), v);
}

template <typename Derived, typename Shape>
template <typename IsolateT>
Handle<Derived> HashTable<Derived, Shape>::New(
    IsolateT* isolate, int at_least_space_for, AllocationType allocation,
    MinimumCapacity capacity_option) {
  DCHECK_LE(0, at_least_space_for);
  DCHECK_IMPLIES(capacity_option == USE_CUSTOM_MINIMUM_CAPACITY,
                 base::bits::IsPowerOfTwo(at_least_space_for));

  int capacity = (capacity_option == USE_CUSTOM_MINIMUM_CAPACITY)
                     ? at_least_space_for
                     : ComputeCapacity(at_least_space_for);
  if (capacity > HashTable::kMaxCapacity) {
    isolate->FatalProcessOutOfHeapMemory("invalid table size");
  }
  return NewInternal(isolate, capacity, allocation);
}

template <typename Derived, typename Shape>
template <typename IsolateT>
Handle<Derived> HashTable<Derived, Shape>::NewInternal(
    IsolateT* isolate, int capacity, AllocationType allocation) {
  auto* factory = isolate->factory();
  int length = EntryToIndex(InternalIndex(capacity));
  Handle<FixedArray> array = factory->NewFixedArrayWithMap(
      Derived::GetMap(ReadOnlyRoots(isolate)), length, allocation);
  Handle<Derived> table = Cast<Derived>(array);
  DisallowGarbageCollection no_gc;
  Tagged<Derived> raw_table = *table;
  raw_table->SetNumberOfElements(0);
  raw_table->SetNumberOfDeletedElements(0);
  raw_table->SetCapacity(capacity);
  return table;
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::Rehash(PtrComprCageBase cage_base,
                                       Tagged<Derived> new_table) {
  DisallowGarbageCollection no_gc;
  WriteBarrierMode mode = new_table->GetWriteBarrierMode(no_gc);

  DCHECK_LT(NumberOfElements(), new_table->Capacity());

  // Copy prefix to new array.
  for (int i = kPrefixStartIndex; i < kElementsStartIndex; i++) {
    new_table->set(i, get(i), mode);
  }

  // Rehash the elements.
  ReadOnlyRoots roots = GetReadOnlyRoots(cage_base);
  for (InternalIndex i : this->IterateEntries()) {
    uint32_t from_index = EntryToIndex(i);
    Tagged<Object> k = this->get(from_index);
    if (!IsKey(roots, k)) continue;
    uint32_t hash = TodoShape::HashForObject(roots, k);
    uint32_t insertion_index =
        EntryToIndex(new_table->FindInsertionEntry(cage_base, roots, hash));
    new_table->set_key(insertion_index, get(from_index), mode);
    for (int j = 1; j < TodoShape::kEntrySize; j++) {
      new_table->set(insertion_index + j, get(from_index + j), mode);
    }
  }
  new_table->SetNumberOfElements(NumberOfElements());
  new_table->SetNumberOfDeletedElements(0);
}

template <typename Derived, typename Shape>
InternalIndex HashTable<Derived, Shape>::EntryForProbe(ReadOnlyRoots roots,
                                                       Tagged<Object> k,
                                                       int probe,
                                                       InternalIndex expected) {
  uint32_t hash = TodoShape::HashForObject(roots, k);
  uint32_t capacity = this->Capacity();
  InternalIndex entry = FirstProbe(hash, capacity);
  for (int i = 1; i < probe; i++) {
    if (entry == expected) return expected;
    entry = NextProbe(entry, i, capacity);
  }
  return entry;
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::Swap(InternalIndex entry1, InternalIndex entry2,
                                     WriteBarrierMode mode) {
  int index1 = EntryToIndex(entry1);
  int index2 = EntryToIndex(entry2);
  Tagged<Object> temp[TodoShape::kEntrySize];
  Derived* self = static_cast<Derived*>(this);
  for (int j = 0; j < TodoShape::kEntrySize; j++) {
    temp[j] = get(index1 + j);
  }
  self->set_key(index1, get(index2), mode);
  for (int j = 1; j < TodoShape::kEntrySize; j++) {
    set(index1 + j, get(index2 + j), mode);
  }
  self->set_key(index2, temp[0], mode);
  for (int j = 1; j < TodoShape::kEntrySize; j++) {
    set(index2 + j, temp[j], mode);
  }
}

template <typename Derived, typename Shape>
void HashTable<Derived, Shape>::Rehash(PtrComprCageBase cage_base) {
  DisallowGarbageCollection no_gc;
  WriteBarrierMode mode = GetWriteBarrierMode(no_gc);
  ReadOnlyRoots roots = EarlyGetReadOnlyRoots();
  uint32_t capacity = Capacity();
  bool done = false;
  for (int probe = 1; !done; probe++) {
    // All elements at entries given by one of the first _probe_ probes
    // are placed correctly. Other elements might need to be moved.
    done = true;
    for (InternalIndex current(0); current.raw_value() < capacity;
         /* {current} is advanced manually below, when appropriate.*/) {
      Tagged<Object> current_key = KeyAt(cage_base, current);
      if (!IsKey(roots, current_key)) {
        ++current;  // Advance to next entry.
        continue;
      }
      InternalIndex target = EntryForProbe(roots, current_key, probe, current);
      if (current == target) {
        ++current;  // Advance to next entry.
        continue;
      }
      Tagged<Object> target_key = KeyAt(cage_base, target);
      if (!IsKey(roots, target_key) ||
          EntryForProbe(roots, target_key, probe, target) != target) {
        // Put the current element into the correct position.
        Swap(current, target, mode);
        // The other element will be processed on the next iteration,
        // so don't advance {current} here!
      } else {
        // The place for the current element is occupied. Leave the element
        // for the next probe.
        done = false;
        ++current;  // Advance to next entry.
      }
    }
  }
  // Wipe deleted entries.
  Tagged<Object> the_hole = roots.the_hole_value();
  Tagged<HeapObject> undefined = roots.undefined_value();
  Derived* self = static_cast<Derived*>(this);
  for (InternalIndex current : InternalIndex::Range(capacity)) {
    if (KeyAt(cage_base, current) == the_hole) {
      self->set_key(EntryToIndex(current) + kEntryKeyIndex, undefined,
                    SKIP_WRITE_BARRIER);
    }
  }
  SetNumberOfDeletedElements(0);
}

template <typename Derived, typename Shape>
template <typename IsolateT>
Handle<Derived> HashTable<Derived, Shape>::EnsureCapacity(
    IsolateT* isolate, Handle<Derived> table, int n,
    AllocationType allocation) {
  if (table->HasSufficientCapacityToAdd(n)) return table;

  int capacity = table->Capacity();
  int new_nof = table->NumberOfElements() + n;

  bool should_pretenure = allocation == AllocationType::kOld ||
                          ((capacity > kMinCapacityForPretenure) &&
                           !Heap::InYoungGeneration(*table));
  Handle<Derived> new_table = HashTable::New(
      isolate, new_nof,
      should_pretenure ? AllocationType::kOld : AllocationType::kYoung);

  table->Rehash(isolate, *new_table);
  return new_table;
}

template <typename Derived, typename Shape>
bool HashTable<Derived, Shape>::HasSufficientCapacityToAdd(
    int number_of_additional_elements) {
  return HasSufficientCapacityToAdd(Capacity(), NumberOfElements(),
                                    NumberOfDeletedElements(),
                                    number_of_additional_elements);
}

// static
template <typename Derived, typename Shape>
bool HashTable<Derived, Shape>::HasSufficientCapacityToAdd(
    int capacity, int number_of_elements, int number_of_deleted_elements,
    int number_of_additional_elements) {
  int nof = number_of_elements + number_of_additional_elements;
  // Return true if:
  //   50% is still free after adding number_of_additional_elements elements and
  //   at most 50% of the free elements are deleted elements.
  if ((nof < capacity) &&
      ((number_of_deleted_elements <= (capacity - nof) / 2))) {
    int needed_free = nof / 2;
    if (nof + needed_free <= capacity) return true;
  }
  return false;
}

// static
template <typename Derived, typename Shape>
int HashTable<Derived, Shape>::ComputeCapacityWithShrink(
    int current_capacity, int at_least_room_for) {
  // Shrink to fit the number of elements if only a quarter of the
  // capacity is filled with elements.
  if (at_least_room_for > (current_capacity / 4)) return current_capacity;
  // Recalculate the smaller capacity actually needed.
  int new_capacity = ComputeCapacity(at_least_room_for);
  DCHECK_GE(new_capacity, at_least_room_for);
  // Don't go lower than room for {kMinShrinkCapacity} elements.
  if (new_capacity < Derived::kMinShrinkCapacity) return current_capacity;
  return new_capacity;
}

// static
template <typename Derived, typename Shape>
Handle<Derived> HashTable<Derived, Shape>::Shrink(Isolate* isolate,
                                                  Handle<Derived> table,
                                                  int additional_capacity) {
  int new_capacity = ComputeCapacityWithShrink(
      table->Capacity(), table->NumberOfElements() + additional_capacity);
  if (new_capacity == table->Capacity()) return table;
  DCHECK_GE(new_capacity, Derived::kMinShrinkCapacity);

  bool pretenure = (new_capacity > kMinCapacityForPretenure) &&
                   !Heap::InYoungGeneration(*table);
  Handle<Derived> new_table =
      HashTable::New(isolate, new_capacity,
                     pretenure ? AllocationType::kOld : AllocationType::kYoung,
                     USE_CUSTOM_MINIMUM_CAPACITY);

  table->Rehash(isolate, *new_table);
  return new_table;
}

template <typename Derived, typename Shape>
InternalIndex HashTable<Derived, Shape>::FindInsertionEntry(
    PtrComprCageBase cage_base, ReadOnlyRoots roots, uint32_t hash) {
  uint32_t capacity = Capacity();
  uint32_t count = 1;
  // EnsureCapacity will guarantee the hash table is never full.
  for (InternalIndex entry = FirstProbe(hash, capacity);;
       entry = NextProbe(entry, count++, capacity)) {
    if (!IsKey(roots, KeyAt(cage_base, entry))) return entry;
  }
}

std::optional<Tagged<PropertyCell>>
GlobalDictionary::TryFindPropertyCellForConcurrentLookupIterator(
    Isolate* isolate, DirectHandle<Name> name, RelaxedLoadTag tag) {
  // This reimplements HashTable::FindEntry for use in a concurrent setting.
  // 1) Atomic loads.
  // 2) IsPendingAllocation checks.
  // 3) Return the PropertyCell value instead of the InternalIndex to avoid a
  //   repeated load (unsafe with concurrent modifications).

  DisallowGarbageCollection no_gc;
  PtrComprCageBase cage_base{isolate};
  ReadOnlyRoots roots(isolate);
  const int32_t hash = TodoShape::Hash(roots, name);
  const uint32_t capacity = Capacity();
  uint32_t count = 1;
  Tagged<Object> undefined = roots.undefined_value();
  Tagged<Object> the_hole = roots.the_hole_value();
  // EnsureCapacity will guarantee the hash table is never full.
  for (InternalIndex entry = FirstProbe(hash, capacity);;
       entry = NextProbe(entry, count++, capacity)) {
    Tagged<Object> element = KeyAt(cage_base, entry, kRelaxedLoad);
    if (isolate->heap()->IsPendingAllocation(element)) return {};
    if (element == undefined) return {};
    if (TodoShape::kMatchNeedsHoleCheck && element == the_hole) continue;
    if (!TodoShape::IsMatch(name, element)) continue;
    CHECK(IsPropertyCell(element, cage_base));
    return Cast<PropertyCell>(element);
  }
}

Handle<StringSet> StringSet::New(Isolate* isolate) {
  return HashTable::New(isolate, 0);
}

Handle<StringSet> StringSet::Add(Isolate* isolate, Handle<StringSet> stringset,
                                 DirectHandle<String> name) {
  if (!stringset->Has(isolate, name)) {
    stringset = EnsureCapacity(isolate, stringset);
    uint32_t hash = TodoShape::Hash(ReadOnlyRoots(isolate), *name);
    InternalIndex entry = stringset->FindInsertionEntry(isolate, hash);
    stringset->set(EntryToIndex(entry), *name);
    stringset->ElementAdded();
  }
  return stringset;
}

bool StringSet::Has(Isolate* isolate, DirectHandle<String> name) {
  return FindEntry(isolate, *name).is_found();
}

Handle<RegisteredSymbolTable> RegisteredSymbolTable::Add(
    Isolate* isolate, Handle<RegisteredSymbolTable> table,
    IndirectHandle<String> key, DirectHandle<Symbol> symbol) {
  // Validate that the key is absent.
  SLOW_DCHECK(table->FindEntry(isolate, key).is_not_found());

  table = EnsureCapacity(isolate, table);
  uint32_t hash = TodoShape::Hash(ReadOnlyRoots(isolate), key);
  InternalIndex entry = table->FindInsertionEntry(isolate, hash);
  table->set(EntryToIndex(entry), *key);
  table->set(EntryToValueIndex(entry), *symbol);
  table->ElementAdded();
  return table;
}

template <typename Derived, typename Shape>
template <typename IsolateT>
Handle<Derived> BaseNameDictionary<Derived, Shape>::New(
    IsolateT* isolate, int at_least_space_for, AllocationType allocation,
    MinimumCapacity capacity_option) {
  DCHECK_LE(0, at_least_space_for);
  Handle<Derived> dict = Dictionary<Derived, Shape>::New(
      isolate, at_least_space_for, allocation, capacity_option);
  dict->SetHash(PropertyArray::kNoHashSentinel);
  dict->set_next_enumeration_index(PropertyDetails::kInitialIndex);
  return dict;
}

template <typename IsolateT>
Handle<NameDictionary> NameDictionary::New(IsolateT* isolate,
                                           int at_least_space_for,
                                           AllocationType allocation,
                                           MinimumCapacity capacity_option) {
  Handle<NameDictionary> dict =
      BaseNameDictionary<NameDictionary, NameDictionaryShape>::New(
          isolate, at_least_space_for, allocation, capacity_option);
  dict->set_flags(kFlagsDefault);
  return dict;
}

template <typename Derived, typename Shape>
int BaseNameDictionary<Derived, Shape>::NextEnumerationIndex(
    Isolate* isolate, Handle<Derived> dictionary) {
  int index = dictionary->next_enumeration_index();
  // Check whether the next enumeration index is valid.
  if (!PropertyDetails::IsValidIndex(index)) {
    // If not, we generate new indices for the properties.
    DirectHandle<FixedArray> iteration_order =
        IterationIndices(isolate, dictionary);
    int length = iteration_order->length();
    DCHECK_LE(length, dictionary->NumberOfElements());

    // Iterate over the dictionary using the enumeration order and update
    // the dictionary with new enumeration indices.
    for (int i = 0; i < length; i++) {
      InternalIndex internal_index(Smi::ToInt(iteration_order->get(i)));
      DCHECK(dictionary->IsKey(dictionary->GetReadOnlyRoots(),
                               dictionary->KeyAt(isolate, internal_index)));

      int enum_index = PropertyDetails::kInitialIndex + i;

      PropertyDetails details = dictionary->DetailsAt(internal_index);
      PropertyDetails new_details = details.set_index(enum_index);
      dictionary->DetailsAtPut(internal_index, new_details);
    }

    index = PropertyDetails::kInitialIndex + length;
  }

  // Don't update the next enumeration index here, since we might be looking at
  // an immutable empty dictionary.
  return index;
}

template <typename Derived, typename Shape>
Handle<Derived> Dictionary<Derived, Shape>::DeleteEntry(
    Isolate* isolate, Handle<Derived> dictionary, InternalIndex entry) {
  DCHECK(TodoShape::kEntrySize != 3 ||
         dictionary->DetailsAt(entry).IsConfigurable());
  dictionary->ClearEntry(entry);
  dictionary->ElementRemoved();
  return Shrink(isolate, dictionary);
}

template <typename Derived, typename Shape>
Handle<Derived> Dictionary<Derived, Shape>::AtPut(Isolate* isolate,
                                                  Handle<Derived> dictionary,
                                                  Key key, Handle<Object> value,
                                                  PropertyDetails details) {
  InternalIndex entry = dictionary->FindEntry(isolate, key);

  // If the entry is present set the value;
  if (entry.is_not_found()) {
    return Derived::Add(isolate, dictionary, key, value, details);
  }

  // We don't need to copy over the enumeration index.
  dictionary->ValueAtPut(entry, *value);
  if (TodoShape::kEntrySize == 3) dictionary->DetailsAtPut(entry, details);
  return dictionary;
}

template <typename Derived, typename Shape>
void Dictionary<Derived, Shape>::UncheckedAtPut(Isolate* isolate,
                                                Handle<Derived> dictionary,
                                                Key key, Handle<Object> value,
                                                PropertyDetails details) {
  InternalIndex entry = dictionary->FindEntry(isolate, key);

  // If the entry is present set the value;
  if (entry.is_not_found()) {
    Derived::UncheckedAdd(isolate, dictionary, key, value, details);
  } else {
    // We don't need to copy over the enumeration index.
    dictionary->ValueAtPut(entry, *value);
    if (TodoShape::kEntrySize == 3) dictionary->DetailsAtPut(entry, details);
  }
}

template <typename Derived, typename Shape>
template <typename IsolateT>
Handle<Derived>
BaseNameDictionary<Derived, Shape>::AddNoUpdateNextEnumerationIndex(
    IsolateT* isolate, Handle<Derived> dictionary, Key key,
    Handle<Object> value, PropertyDetails details, InternalIndex* entry_out) {
  // Insert element at empty or deleted entry.
  return Dictionary<Derived, Shape>::Add(isolate, dictionary, key, value,
                                         details, entry_out);
}

template <typename Derived, typename Shape>
Handle<Derived> BaseNameDictionary<Derived, Shape>::Add(
    Isolate* isolate, Handle<Derived> dictionary, Key key, Handle<Object> value,
    PropertyDetails details, InternalIndex* entry_out) {
  // Insert element at empty or deleted entry
  DCHECK_EQ(0, details.dictionary_index());
  // Assign an enumeration index to the property and update
  // SetNextEnumerationIndex.
  int index = Derived::NextEnumerationIndex(isolate, dictionary);
  details = details.set_index(index);
  dictionary = AddNoUpdateNextEnumerationIndex(isolate, dictionary, key, value,
                                               details, entry_out);
  // Update enumeration index here in order to avoid potential modification of
  // the canonical empty dictionary which lives in read only space.
  dictionary->set_next_enumeration_index(index + 1);
  return dictionary;
}

template <typename Derived, typename Shape>
template <typename IsolateT, AllocationType key_allocation>
Handle<Derived> Dictionary<Derived, Shape>::Add(IsolateT* isolate,
                                                Handle<Derived> dictionary,
                                                Key key,
                                                DirectHandle<Object> value,
                                                PropertyDetails details,
                                                InternalIndex* entry_out) {
  ReadOnlyRoots roots(isolate);
  uint32_t hash = TodoShape::Hash(roots, key);
  // Validate that the key is absent.
  SLOW_DCHECK(dictionary->FindEntry(isolate, key).is_not_found());
  // Check whether the dictionary should be extended.
  dictionary = Derived::EnsureCapacity(isolate, dictionary);

  // Compute the key object.
  DirectHandle<Object> k =
      TodoShape::template AsHandle<key_allocation>(isolate, key);

  InternalIndex entry = dictionary->FindInsertionEntry(isolate, roots, hash);
  dictionary->SetEntry(entry, *k, *value, details);
  DCHECK(IsNumber(dictionary->KeyAt(isolate, entry)) ||
         IsUniqueName(TodoShape::Unwrap(dictionary->KeyAt(isolate, entry))));
  dictionary->ElementAdded();
  if (entry_out) *entry_out = entry;
  return dictionary;
}

template <typename Derived, typename Shape>
template <typename IsolateT, AllocationType key_allocation>
void Dictionary<Derived, Shape>::UncheckedAdd(IsolateT* isolate,
                                              Handle<Derived> dictionary,
                                              Key key,
                                              DirectHandle<Object> value,
                                              PropertyDetails details) {
  ReadOnlyRoots roots(isolate);
  uint32_t hash = TodoShape::Hash(roots, key);
  // Validate that the key is absent and we capacity is sufficient.
  SLOW_DCHECK(dictionary->FindEntry(isolate, key).is_not_found());
  DCHECK(dictionary->HasSufficientCapacityToAdd(1));

  // Compute the key object.
  DirectHandle<Object> k =
      TodoShape::template AsHandle<key_allocation>(isolate, key);

  InternalIndex entry = dictionary->FindInsertionEntry(isolate, roots, hash);
  dictionary->SetEntry(entry, *k, *value, details);
  DCHECK(IsNumber(dictionary->KeyAt(isolate, entry)) ||
         IsUniqueName(TodoShape::Unwrap(dictionary->KeyAt(isolate, entry))));
}

template <typename Derived, typename Shape>
Handle<Derived> Dictionary<Derived, Shape>::ShallowCopy(
    Isolate* isolate, Handle<Derived> dictionary, AllocationType allocation) {
  return Cast<Derived>(isolate->factory()->CopyFixedArrayWithMap(
      dictionary, Derived::GetMap(ReadOnlyRoots(isolate)), allocation));
}

// static
Handle<SimpleNumberDictionary> SimpleNumberDictionary::Set(
    Isolate* isolate, Handle<SimpleNumberDictionary> dictionary, uint32_t key,
    Handle<Object> value) {
  return AtPut(isolate, dictionary, key, value, PropertyDetails::Empty());
}

void NumberDictionary::UpdateMaxNumberKey(uint32_t key,
                                          Handle<JSObject> dictionary_holder) {
  DisallowGarbageCollection no_gc;
  // If the dictionary requires slow elements an element has already
  // been added at a high index.
  if (requires_slow_elements()) return;
  // Check if this index is high enough that we should require slow
  // elements.
  if (key > kRequiresSlowElementsLimit) {
    if (!dictionary_holder.is_null()) {
      dictionary_holder->RequireSlowElements(*this);
    }
    set_requires_slow_elements();
    return;
  }
  // Update max key value.
  Tagged<Object> max_index_object = get(kMaxNumberKeyIndex);
  if (!IsSmi(max_index_object) || max_number_key() < key) {
    FixedArray::set(kMaxNumberKeyIndex,
                    Smi::FromInt(key << kRequiresSlowElementsTagSize));
  }
}

Handle<NumberDictionary> NumberDictionary::Set(
    Isolate* isolate, Handle<NumberDictionary> dictionary, uint32_t key,
    Handle<Object> value, Handle<JSObject> dictionary_holder,
    PropertyDetails details) {
  // We could call Set with empty dictionaries. UpdateMaxNumberKey doesn't
  // expect empty dictionaries so make sure to call AtPut that correctly handles
  // them by creating new dictionary when required.
  Handle<NumberDictionary> new_dictionary =
      AtPut(isolate, dictionary, key, value, details);
  new_dictionary->UpdateMaxNumberKey(key, dictionary_holder);
  return new_dictionary;
}

// static
void NumberDictionary::UncheckedSet(Isolate* isolate,
                                    Handle<NumberDictionary> dictionary,
                                    uint32_t key, Handle<Object> value) {
  UncheckedAtPut(isolate, dictionary, key, value, PropertyDetails::Empty());
}

void NumberDictionary::CopyValuesTo(Tagged<FixedArray> elements) {
  ReadOnlyRoots roots = GetReadOnlyRoots();
  int pos = 0;
  DisallowGarbageCollection no_gc;
  WriteBarrierMode mode = elements->GetWriteBarrierMode(no_gc);
  for (InternalIndex i : this->IterateEntries()) {
    Tagged<Object> k;
    if (this->ToKey(roots, i, &k)) {
      elements->set(pos++, this->ValueAt(i), mode);
    }
  }
  DCHECK_EQ(pos, elements->length());
}

template <typename Derived, typename Shape>
int Dictionary<Derived, Shape>::NumberOfEnumerableProperties() {
  ReadOnlyRoots roots = this->GetReadOnlyRoots();
  int result = 0;
  for (InternalIndex i : this->IterateEntries()) {
    Tagged<Object> k;
    if (!this->ToKey(roots, i, &k)) continue;
    if (Object::FilterKey(k, ENUMERABLE_STRINGS)) continue;
    PropertyDetails details = this->DetailsAt(i);
    PropertyAttributes attr = details.attributes();
    if ((int{attr} & ONLY_ENUMERABLE) == 0) result++;
  }
  return result;
}

template <typename Derived, typename Shape>
Handle<FixedArray> BaseNameDictionary<Derived, Shape>::IterationIndices(
    Isolate* isolate, Handle<Derived> dictionary) {
  Handle<FixedArray> array =
      isolate->factory()->NewFixedArray(dictionary->NumberOfElements());
  ReadOnlyRoots roots(isolate);
  int array_size = 0;
  {
    DisallowGarbageCollection no_gc;
    Tagged<Derived> raw_dictionary = *dictionary;
    for (InternalIndex i : dictionary->IterateEntries()) {
      Tagged<Object> k;
      if (!raw_dictionary->ToKey(roots, i, &k)) continue;
      array->set(array_size++, Smi::FromInt(i.as_int()));
    }

    // The global dictionary doesn't track its deletion count, so we may iterate
    // fewer entries than the count of elements claimed by the dictionary.
    if (std::is_same<Derived, GlobalDictionary>::value) {
      DCHECK_LE(array_size, dictionary->NumberOfElements());
    } else {
      DCHECK_EQ(array_size, dictionary->NumberOfElements());
    }

    EnumIndexComparator<Derived> cmp(raw_dictionary);
    // Use AtomicSlot wrapper to ensure that std::sort uses atomic load and
    // store operations that are safe for concurrent marking.
    AtomicSlot start(array->RawFieldOfFirstElement());
    std::sort(start, start + array_size, cmp);
  }
  return FixedArray::RightTrimOrEmpty(isolate, array, array_size);
}

// Backwards lookup (slow).
template <typename Derived, typename Shape>
Tagged<Object> Dictionary<Derived, Shape>::SlowReverseLookup(
    Tagged<Object> value) {
  Tagged<Derived> dictionary = Cast<Derived>(*this);
  ReadOnlyRoots roots = dictionary->GetReadOnlyRoots();
  for (InternalIndex i : dictionary->IterateEntries()) {
    Tagged<Object> k;
    if (!dictionary->ToKey(roots, i, &k)) continue;
    Tagged<Object> e = dictionary->ValueAt(i);
    if (e == value) return k;
  }
  return roots.undefined_value();
}

template <typename Derived, typename Shape>
void ObjectHashTableBase<Derived, Shape>::FillEntriesWithHoles(
    Handle<Derived> table) {
  auto roots = table->GetReadOnlyRoots();
  int length = table->length();
  for (int i = Derived::EntryToIndex(InternalIndex(0)); i < length; i++) {
    table->set_the_hole(roots, i);
  }
}

template <typename Derived, typename Shape>
Tagged<Object> ObjectHashTableBase<Derived, Shape>::Lookup(
    PtrComprCageBase cage_base, Handle<Object> key, int32_t hash) {
  DisallowGarbageCollection no_gc;
  ReadOnlyRoots roots = this->GetReadOnlyRoots(cage_base);
  DCHECK(this->IsKey(roots, *key));

  InternalIndex entry = this->FindEntry(cage_base, roots, key, hash);
  if (entry.is_not_found()) return roots.the_hole_value();
  return this->get(Derived::EntryToIndex(entry) + 1);
}

// The implementation should be in sync with
// CodeStubAssembler::NameToIndexHashTableLookup.
int NameToIndexHashTable::Lookup(Handle<Name> key) {
  DisallowGarbageCollection no_gc;
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  ReadOnlyRoots roots = this->GetReadOnlyRoots(cage_base);

  InternalIndex entry = this->FindEntry(cage_base, roots, key, key->hash());
  if (entry.is_not_found()) return -1;
  return Cast<Smi>(this->get(EntryToValueIndex(entry))).value();
}

template <typename Derived, typename Shape>
Tagged<Object> ObjectHashTableBase<Derived, Shape>::Lookup(Handle<Object> key) {
  DisallowGarbageCollection no_gc;

  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  ReadOnlyRoots roots = this->GetReadOnlyRoots(cage_base);
  DCHECK(this->IsKey(roots, *key));

  // If the object does not have an identity hash, it was never used as a key.
  Tagged<Object> hash = Object::GetHash(*key);
  if (IsUndefined(hash, roots)) {
    return roots.the_hole_value();
  }
  return Lookup(cage_base, key, Smi::ToInt(hash));
}

template <typename Derived, typename Shape>
Tagged<Object> ObjectHashTableBase<Derived, Shape>::Lookup(Handle<Object> key,
                                                           int32_t hash) {
  return Lookup(GetPtrComprCageBase(*this), key, hash);
}

template <typename Derived, typename Shape>
Tagged<Object> ObjectHashTableBase<Derived, Shape>::ValueAt(
    InternalIndex entry) {
  return this->get(EntryToValueIndex(entry));
}

Tagged<Object> RegisteredSymbolTable::ValueAt(InternalIndex entry) {
  return this->get(EntryToValueIndex(entry));
}

Tagged<Object> NameToIndexHashTable::ValueAt(InternalIndex entry) {
  return this->get(EntryToValueIndex(entry));
}

int NameToIndexHashTable::IndexAt(InternalIndex entry) {
  Tagged<Object> value = ValueAt(entry);
  if (IsSmi(value)) {
    int index = Smi::ToInt(value);
    DCHECK_LE(0, index);
    return index;
  }
  return -1;
}

template <typename Derived, typename Shape>
Handle<Derived> ObjectHashTableBase<Derived, Shape>::Put(Handle<Derived> table,
                                                         Handle<Object> key,
                                                         Handle<Object> value) {
  Isolate* isolate = Heap::FromWritableHeapObject(*table)->isolate();
  DCHECK(table->IsKey(ReadOnlyRoots(isolate), *key));
  DCHECK(!IsTheHole(*value, ReadOnlyRoots(isolate)));

  // Make sure the key object has an identity hash code.
  int32_t hash = Object::GetOrCreateHash(*key, isolate).value();

  return ObjectHashTableBase<Derived, Shape>::Put(isolate, table, key, value,
                                                  hash);
}

namespace {

template <typename T>
void RehashObjectHashTableAndGCIfNeeded(Isolate* isolate, Handle<T> table) {
  // Rehash if more than 33% of the entries are deleted entries.
  // TODO(verwaest): Consider to shrink the fixed array in place.
  if ((table->NumberOfDeletedElements() << 1) > table->NumberOfElements()) {
    table->Rehash(isolate);
  }
  // If we're out of luck, we didn't get a GC recently, and so rehashing
  // isn't enough to avoid a crash.
  if (!table->HasSufficientCapacityToAdd(1)) {
    int nof = table->NumberOfElements() + 1;
    int capacity = T::ComputeCapacity(nof * 2);
    if (capacity > T::kMaxCapacity) {
      for (size_t i = 0; i < 2; ++i) {
        isolate->heap()->CollectAllGarbage(
            GCFlag::kNoFlags, GarbageCollectionReason::kFullHashtable);
      }
      table->Rehash(isolate);
    }
  }
}

}  // namespace

template <typename Derived, typename Shape>
Handle<Derived> ObjectHashTableBase<Derived, Shape>::Put(
    Isolate* isolate, Handle<Derived> table, Handle<Object> key,
    DirectHandle<Object> value, int32_t hash) {
  ReadOnlyRoots roots(isolate);
  DCHECK(table->IsKey(roots, *key));
  DCHECK(!IsTheHole(*value, roots));

  InternalIndex entry = table->FindEntry(isolate, roots, key, hash);

  // Key is already in table, just overwrite value.
  if (entry.is_found()) {
    table->set(Derived::EntryToValueIndex(entry), *value);
    return table;
  }

  RehashObjectHashTableAndGCIfNeeded(isolate, table);

  // Check whether the hash table should be extended.
  table = Derived::EnsureCapacity(isolate, table);
  table->AddEntry(table->FindInsertionEntry(isolate, hash), *key, *value);
  return table;
}

template <typename Derived, typename Shape>
Handle<Derived> ObjectHashTableBase<Derived, Shape>::Remove(
    Isolate* isolate, Handle<Derived> table, Handle<Object> key,
    bool* was_present) {
  DCHECK(table->IsKey(table->GetReadOnlyRoots(), *key));

  Tagged<Object> hash = Object::GetHash(*key);
  if (IsUndefined(hash)) {
    *was_present = false;
    return table;
  }

  return Remove(isolate, table, key, was_present, Smi::ToInt(hash));
}

template <typename Derived, typename Shape>
Handle<Derived> ObjectHashTableBase<Derived, Shape>::Remove(
    Isolate* isolate, Handle<Derived> table, Handle<Object> key,
    bool* was_present, int32_t hash) {
  ReadOnlyRoots roots = table->GetReadOnlyRoots();
  DCHECK(table->IsKey(roots, *key));

  InternalIndex entry = table->FindEntry(isolate, roots, key, hash);
  if (entry.is_not_found()) {
    *was_present = false;
    return table;
  }

  *was_present = true;
  table->RemoveEntry(entry);
  return Derived::Shrink(isolate, table);
}

template <typename Derived, typename Shape>
void ObjectHashTableBase<Derived, Shape>::AddEntry(InternalIndex entry,
                                                   Tagged<Object> key,
                                                   Tagged<Object> value) {
  Derived* self = static_cast<Derived*>(this);
  self->set_key(Derived::EntryToIndex(entry), key);
  self->set(Derived::EntryToValueIndex(entry), value);
  self->ElementAdded();
}

template <typename Derived, typename Shape>
void ObjectHashTableBase<Derived, Shape>::RemoveEntry(InternalIndex entry) {
  auto roots = this->GetReadOnlyRoots();
  this->set_the_hole(roots, Derived::EntryToIndex(entry));
  this->set_the_hole(roots, Derived::EntryToValueIndex(entry));
  this->ElementRemoved();
}

template <typename Derived, int N>
std::array<Tagged<Object>, N> ObjectMultiHashTableBase<Derived, N>::Lookup(
    Handle<Object> key) {
  return Lookup(GetPtrComprCageBase(*this), key);
}

template <typename Derived, int N>
std::array<Tagged<Object>, N> ObjectMultiHashTableBase<Derived, N>::Lookup(
    PtrComprCageBase cage_base, Handle<Object> key) {
  DisallowGarbageCollection no_gc;

  ReadOnlyRoots roots = this->GetReadOnlyRoots(cage_base);
  DCHECK(this->IsKey(roots, *key));

  Tagged<Object> hash_obj = Object::GetHash(*key);
  if (IsUndefined(hash_obj, roots)) {
    return {roots.the_hole_value(), roots.the_hole_value()};
  }
  int32_t hash = Smi::ToInt(hash_obj);

  InternalIndex entry = this->FindEntry(cage_base, roots, key, hash);
  if (entry.is_not_found()) {
    return {roots.the_hole_value(), roots.the_hole_value()};
  }

  int start_index = this->EntryToIndex(entry) +
                    ObjectMultiHashTableShape<N>::kEntryValueIndex;
  std::array<Tagged<Object>, N> values;
  for (int i = 0; i < N; i++) {
    values[i] = this->get(start_index + i);
    DCHECK(!IsTheHole(values[i]));
  }
  return values;
}

// static
template <typename Derived, int N>
Handle<Derived> ObjectMultiHashTableBase<Derived, N>::Put(
    Isolate* isolate, Handle<Derived> table, Handle<Object> key,
    const std::array<Handle<Object>, N>& values) {
  ReadOnlyRoots roots(isolate);
  DCHECK(table->IsKey(roots, *key));

  int32_t hash = Object::GetOrCreateHash(*key, isolate).value();
  InternalIndex entry = table->FindEntry(isolate, roots, key, hash);

  // Overwrite values if entry is found.
  if (entry.is_found()) {
    table->SetEntryValues(entry, values);
    return table;
  }

  RehashObjectHashTableAndGCIfNeeded(isolate, table);

  // Check whether the hash table should be extended.
  table = Derived::EnsureCapacity(isolate, table);
  entry = table->FindInsertionEntry(isolate, hash);
  table->set(Derived::EntryToIndex(entry), *key);
  table->SetEntryValues(entry, values);
  return table;
}

template <typename Derived, int N>
void ObjectMultiHashTableBase<Derived, N>::SetEntryValues(
    InternalIndex entry, const std::array<Handle<Object>, N>& values) {
  int start_index = EntryToValueIndexStart(entry);
  for (int i = 0; i < N; i++) {
    this->set(start_index + i, *values[i]);
  }
}

Handle<ObjectHashSet> ObjectHashSet::Add(Isolate* isolate,
                                         Handle<ObjectHashSet> set,
                                         Handle<Object> key) {
  int32_t hash = Object::GetOrCreateHash(*key, isolate).value();
  if (!set->Has(isolate, key, hash)) {
    set = EnsureCapacity(isolate, set);
    InternalIndex entry = set->FindInsertionEntry(isolate, hash);
    set->set(EntryToIndex(entry), *key);
    set->ElementAdded();
  }
  return set;
}

void JSSet::Initialize(DirectHandle<JSSet> set, Isolate* isolate) {
  DirectHandle<OrderedHashSet> table = isolate->factory()->NewOrderedHashSet();
  set->set_table(*table);
}

void JSSet::Clear(Isolate* isolate, DirectHandle<JSSet> set) {
  Handle<OrderedHashSet> table(Cast<OrderedHashSet>(set->table()), isolate);
  table = OrderedHashSet::Clear(isolate, table);
  set->set_table(*table);
}

void JSSet::Rehash(Isolate* isolate) {
  Handle<OrderedHashSet> table_handle(Cast<OrderedHashSet>(table()), isolate);
  DirectHandle<OrderedHashSet> new_table =
      OrderedHashSet::Rehash(isolate, table_handle).ToHandleChecked();
  set_table(*new_table);
}

void JSMap::Initialize(DirectHandle<JSMap> map, Isolate* isolate) {
  DirectHandle<OrderedHashMap> table = isolate->factory()->NewOrderedHashMap();
  map->set_table(*table);
}

void JSMap::Clear(Isolate* isolate, DirectHandle<JSMap> map) {
  Handle<OrderedHashMap> table(Cast<OrderedHashMap>(map->table()), isolate);
  table = OrderedHashMap::Clear(isolate, table);
  map->set_table(*table);
}

void JSMap::Rehash(Isolate* isolate) {
  Handle<OrderedHashMap> table_handle(Cast<OrderedHashMap>(table()), isolate);
  DirectHandle<OrderedHashMap> new_table =
      OrderedHashMap::Rehash(isolate, table_handle).ToHandleChecked();
  set_table(*new_table);
}

void JSWeakCollection::Initialize(
    DirectHandle<JSWeakCollection> weak_collection, Isolate* isolate) {
  DirectHandle<EphemeronHashTable> table = EphemeronHashTable::New(isolate, 0);
  weak_collection->set_table(*table);
}

void JSWeakCollection::Set(DirectHandle<JSWeakCollection> weak_collection,
                           Handle<Object> key, DirectHandle<Object> value,
                           int32_t hash) {
  DCHECK(IsJSReceiver(*key) || IsSymbol(*key));
  Handle<EphemeronHashTable> table(
      Cast<EphemeronHashTable>(weak_collection->table()),
      weak_collection->GetIsolate());
  DCHECK(table->IsKey(weak_collection->GetReadOnlyRoots(), *key));
  DirectHandle<EphemeronHashTable> new_table = EphemeronHashTable::Put(
      weak_collection->GetIsolate(), table, key, value, hash);
  weak_collection->set_table(*new_table);
  if (*table != *new_table) {
    // Zap the old table since we didn't record slots for its elements.
    EphemeronHashTable::FillEntriesWithHoles(table);
  }
}

bool JSWeakCollection::Delete(DirectHandle<JSWeakCollection> weak_collection,
                              Handle<Object> key, int32_t hash) {
  DCHECK(IsJSReceiver(*key) || IsSymbol(*key));
  Handle<EphemeronHashTable> table(
      Cast<EphemeronHashTable>(weak_collection->table()),
      weak_collection->GetIsolate());
  DCHECK(table->IsKey(weak_collection->GetReadOnlyRoots(), *key));
  bool was_present = false;
  DirectHandle<EphemeronHashTable> new_table = EphemeronHashTable::Remove(
      weak_collection->GetIsolate(), table, key, &was_present, hash);
  weak_collection->set_table(*new_table);
  if (*table != *new_table) {
    // Zap the old table since we didn't record slots for its elements.
    EphemeronHashTable::FillEntriesWithHoles(table);
  }
  return was_present;
}

Handle<JSArray> JSWeakCollection::GetEntries(
    DirectHandle<JSWeakCollection> holder, int max_entries) {
  Isolate* isolate = holder->GetIsolate();
  DirectHandle<EphemeronHashTable> table(
      Cast<EphemeronHashTable>(holder->table()), isolate);
  if (max_entries == 0 || max_entries > table->NumberOfElements()) {
    max_entries = table->NumberOfElements();
  }
  int values_per_entry = IsJSWeakMap(*holder) ? 2 : 1;
  DirectHandle<FixedArray> entries =
      isolate->factory()->NewFixedArray(max_entries * values_per_entry);
  // Recompute max_values because GC could have removed elements from the table.
  if (max_entries > table->NumberOfElements()) {
    max_entries = table->NumberOfElements();
  }

  {
    DisallowGarbageCollection no_gc;
    ReadOnlyRoots roots = ReadOnlyRoots(isolate);
    int count = 0;
    for (int i = 0;
         count / values_per_entry < max_entries && i < table->Capacity(); i++) {
      Tagged<Object> key;
      if (table->ToKey(roots, InternalIndex(i), &key)) {
        entries->set(count++, key);
        if (values_per_entry > 1) {
          Tagged<Object> value = table->Lookup(handle(key, isolate));
          entries->set(count++, value);
        }
      }
    }
    DCHECK_EQ(max_entries * values_per_entry, count);
  }
  return isolate->factory()->NewJSArrayWithElements(entries);
}

void JSDisposableStackBase::InitializeJSDisposableStackBase(
    Isolate* isolate, DirectHandle<JSDisposableStackBase> disposable_stack) {
  DirectHandle<FixedArray> array = isolate->factory()->NewFixedArray(0);
  disposable_stack->set_stack(*array);
  disposable_stack->set_length(0);
  disposable_stack->set_state(DisposableStackState::kPending);
}

void PropertyCell::ClearAndInvalidate(ReadOnlyRoots roots) {
  DCHECK(!IsPropertyCellHole(value(), roots));
  PropertyDetails details = property_details();
  details = details.set_cell_type(PropertyCellType::kConstant);
  Transition(details, roots.property_cell_hole_value_handle());
  // TODO(11527): pass Isolate as an argument.
  Isolate* isolate = GetIsolateFromWritableObject(*this);
  DependentCode::DeoptimizeDependencyGroups(
      isolate, *this, DependentCode::kPropertyCellChangedGroup);
}

// static
Handle<PropertyCell> PropertyCell::InvalidateAndReplaceEntry(
    Isolate* isolate, DirectHandle<GlobalDictionary> dictionary,
    InternalIndex entry, PropertyDetails new_details,
    DirectHandle<Object> new_value) {
  DirectHandle<PropertyCell> cell(dictionary->CellAt(entry), isolate);
  DirectHandle<Name> name(cell->name(), isolate);
  DCHECK(cell->property_details().IsConfigurable());
  DCHECK(!IsAnyHole(cell->value(), isolate));

  // Swap with a new property cell.
  Handle<PropertyCell> new_cell =
      isolate->factory()->NewPropertyCell(name, new_details, new_value);
  dictionary->ValueAtPut(entry, *new_cell);

  cell->ClearAndInvalidate(ReadOnlyRoots(isolate));
  return new_cell;
}

static bool RemainsConstantType(Tagged<PropertyCell> cell,
                                Tagged<Object> value) {
  DisallowGarbageCollection no_gc;
  // TODO(dcarney): double->smi and smi->double transition from kConstant
  if (IsSmi(cell->value()) && IsSmi(value)) {
    return true;
  } else if (IsHeapObject(cell->value()) && IsHeapObject(value)) {
    Tagged<Map> map = Cast<HeapObject>(value)->map();
    return Cast<HeapObject>(cell->value())->map() == map && map->is_stable();
  }
  return false;
}

// static
PropertyCellType PropertyCell::InitialType(Isolate* isolate,
                                           Tagged<Object> value) {
  return IsUndefined(value, isolate) ? PropertyCellType::kUndefined
                                     : PropertyCellType::kConstant;
}

// static
PropertyCellType PropertyCell::UpdatedType(Isolate* isolate,
                                           Tagged<PropertyCell> cell,
                                           Tagged<Object> value,
                                           PropertyDetails details) {
  DisallowGarbageCollection no_gc;
  DCHECK(!IsAnyHole(value, isolate));
  DCHECK(!IsAnyHole(cell->value(), isolate));
  switch (details.cell_type()) {
    case PropertyCellType::kUndefined:
      return PropertyCellType::kConstant;
    case PropertyCellType::kConstant:
      if (value == cell->value()) return PropertyCellType::kConstant;
      [[fallthrough]];
    case PropertyCellType::kConstantType:
      if (RemainsConstantType(cell, value)) {
        return PropertyCellType::kConstantType;
      }
      [[fallthrough]];
    case PropertyCellType::kMutable:
      return PropertyCellType::kMutable;
    case PropertyCellType::kInTransition:
      UNREACHABLE();
  }
}

Handle<PropertyCell> PropertyCell::PrepareForAndSetValue(
    Isolate* isolate, DirectHandle<GlobalDictionary> dictionary,
    InternalIndex entry, DirectHandle<Object> value, PropertyDetails details) {
  DCHECK(!IsAnyHole(*value, isolate));
  Tagged<PropertyCell> raw_cell = dictionary->CellAt(entry);
  CHECK(!IsAnyHole(raw_cell->value(), isolate));
  const PropertyDetails original_details = raw_cell->property_details();
  // Data accesses could be cached in ics or optimized code.
  bool invalidate = original_details.kind() == PropertyKind::kData &&
                    details.kind() == PropertyKind::kAccessor;
  int index = original_details.dictionary_index();
  DCHECK_LT(0, index);
  details = details.set_index(index);

  PropertyCellType new_type =
      UpdatedType(isolate, raw_cell, *value, original_details);
  details = details.set_cell_type(new_type);

  Handle<PropertyCell> cell(raw_cell, isolate);

  if (invalidate) {
    cell = PropertyCell::InvalidateAndReplaceEntry(isolate, dictionary, entry,
                                                   details, value);
  } else {
    cell->Transition(details, value);
    // Deopt when transitioning from a constant type or when making a writable
    // property read-only. Making a read-only property writable again is not
    // interesting because Turbofan does not currently rely on read-only unless
    // the property is also configurable, in which case it will stay read-only
    // forever.
    if (original_details.cell_type() != new_type ||
        (!original_details.IsReadOnly() && details.IsReadOnly())) {
      DependentCode::DeoptimizeDependencyGroups(
          isolate, *cell, DependentCode::kPropertyCellChangedGroup);
    }
  }
  return cell;
}

// static
void PropertyCell::InvalidateProtector() {
  if (value() != Smi::FromInt(Protectors::kProtectorInvalid)) {
    DCHECK_EQ(value(), Smi::FromInt(Protectors::kProtectorValid));
    set_value(Smi::FromInt(Protectors::kProtectorInvalid), kReleaseStore);
    // TODO(11527): pass Isolate as an argument.
    Isolate* isolate = GetIsolateFromWritableObject(*this);
    DependentCode::DeoptimizeDependencyGroups(
        isolate, *this, DependentCode::kPropertyCellChangedGroup);
  }
}

// static
bool PropertyCell::CheckDataIsCompatible(PropertyDetails details,
                                         Tagged<Object> value) {
  DisallowGarbageCollection no_gc;
  PropertyCellType cell_type = details.cell_type();
  CHECK_NE(cell_type, PropertyCellType::kInTransition);
  if (IsPropertyCellHole(value)) {
    CHECK_EQ(cell_type, PropertyCellType::kConstant);
  } else {
    CHECK_EQ(IsAccessorInfo(value) || IsAccessorPair(value),
             details.kind() == PropertyKind::kAccessor);
    DCHECK_IMPLIES(cell_type == PropertyCellType::kUndefined,
                   IsUndefined(value));
  }
  return true;
}

#ifdef DEBUG
bool PropertyCell::CanTransitionTo(PropertyDetails new_details,
                                   Tagged<Object> new_value) const {
  // Extending the implementation of PropertyCells with additional states
  // and/or transitions likely requires changes to PropertyCellData::Serialize.
  DisallowGarbageCollection no_gc;
  DCHECK(CheckDataIsCompatible(new_details, new_value));
  switch (property_details().cell_type()) {
    case PropertyCellType::kUndefined:
      return new_details.cell_type() != PropertyCellType::kUndefined;
    case PropertyCellType::kConstant:
      return !IsPropertyCellHole(value()) &&
             new_details.cell_type() != PropertyCellType::kUndefined;
    case PropertyCellType::kConstantType:
      return new_details.cell_type() == PropertyCellType::kConstantType ||
             new_details.cell_type() == PropertyCellType::kMutable ||
             (new_details.cell_type() == PropertyCellType::kConstant &&
              IsPropertyCellHole(new_value));
    case PropertyCellType::kMutable:
      return new_details.cell_type() == PropertyCellType::kMutable ||
             (new_details.cell_type() == PropertyCellType::kConstant &&
              IsPropertyCellHole(new_value));
    case PropertyCellType::kInTransition:
      UNREACHABLE();
  }
}
#endif  // DEBUG

int JSGeneratorObject::code_offset() const {
  DCHECK(IsSmi(input_or_debug_pos()));
  int code_offset = Smi::ToInt(input_or_debug_pos());

  // The stored bytecode offset is relative to a different base than what
  // is used in the source position table, hence the subtraction.
  code_offset -= BytecodeArray::kHeaderSize - kHeapObjectTag;
  return code_offset;
}

int JSGeneratorObject::source_position() const {
  CHECK(is_suspended());
  DCHECK(function()->shared()->HasBytecodeArray());
  Isolate* isolate = GetIsolate();
  DCHECK(function()
             ->shared()
             ->GetBytecodeArray(isolate)
             ->HasSourcePositionTable());
  Tagged<BytecodeArray> bytecode =
      function()->shared()->GetBytecodeArray(isolate);
  return bytecode->SourcePosition(code_offset());
}

// static
Tagged<AccessCheckInfo> AccessCheckInfo::Get(Isolate* isolate,
                                             DirectHandle<JSObject> receiver) {
  DisallowGarbageCollection no_gc;
  DCHECK(receiver->map()->is_access_check_needed());
  Tagged<Object> maybe_constructor = receiver->map()->GetConstructor();
  if (IsFunctionTemplateInfo(maybe_constructor)) {
    Tagged<Object> data_obj =
        Cast<FunctionTemplateInfo>(maybe_constructor)->GetAccessCheckInfo();
    if (IsUndefined(data_obj, isolate)) return AccessCheckInfo();
    return Cast<AccessCheckInfo>(data_obj);
  }
  // Might happen for a detached context.
  if (!IsJSFunction(maybe_constructor)) return AccessCheckInfo();
  Tagged<JSFunction> constructor = Cast<JSFunction>(maybe_constructor);
  // Might happen for the debug context.
  if (!constructor->shared()->IsApiFunction()) return AccessCheckInfo();

  Tagged<Object> data_obj =
      constructor->shared()->api_func_data()->GetAccessCheckInfo();
  if (IsUndefined(data_obj, isolate)) return AccessCheckInfo();

  return Cast<AccessCheckInfo>(data_obj);
}

Address Smi::LexicographicCompare(Isolate* isolate, Tagged<Smi> x,
                                  Tagged<Smi> y) {
  DisallowGarbageCollection no_gc;
  DisallowJavascriptExecution no_js(isolate);

  int x_value = Smi::ToInt(x);
  int y_value = Smi::ToInt(y);

  // If the integers are equal so are the string representations.
  if (x_value == y_value) return Smi::FromInt(0).ptr();

  // If one of the integers is zero the normal integer order is the
  // same as the lexicographic order of the string representations.
  if (x_value == 0 || y_value == 0) {
    return Smi::FromInt(x_value < y_value ? -1 : 1).ptr();
  }

  // If only one of the integers is negative the negative number is
  // smallest because the char code of '-' is less than the char code
  // of any digit.  Otherwise, we make both values positive.

  // Use unsigned values otherwise the logic is incorrect for -MIN_INT on
  // architectures using 32-bit Smis.
  uint32_t x_scaled = x_value;
  uint32_t y_scaled = y_value;
  if (x_value < 0) {
    if (y_value >= 0) {
      return Smi::FromInt(-1).ptr();
    } else {
      y_scaled = base::NegateWithWraparound(y_value);
    }
    x_scaled = base::NegateWithWraparound(x_value);
  } else if (y_value < 0) {
    return Smi::FromInt(1).ptr();
  }

  // clang-format off
  static const uint32_t kPowersOf10[] = {
      1,                 10,                100,         1000,
      10 * 1000,         100 * 1000,        1000 * 1000, 10 * 1000 * 1000,
      100 * 1000 * 1000, 1000 * 1000 * 1000};
  // clang-format on

  // If the integers have the same number of decimal digits they can be
  // compared directly as the numeric order is the same as the
  // lexicographic order.  If one integer has fewer digits, it is scaled
  // by some power of 10 to have the same number of digits as the longer
  // integer.  If the scaled integers are equal it means the shorter
  // integer comes first in the lexicographic order.

  // From http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10
  int x_log2 = 31 - base::bits::CountLeadingZeros(x_scaled);
  int x_log10 = ((x_log2 + 1) * 1233) >> 12;
  x_log10 -= x_scaled < kPowersOf10[x_log10];

  int y_log2 = 31 - base::bits::CountLeadingZeros(y_scaled);
  int y_log10 = ((y_log2 + 1) * 1233) >> 12;
  y_log10 -= y_scaled < kPowersOf10[y_log10];

  int tie = 0;

  if (x_log10 < y_log10) {
    // X has fewer digits.  We would like to simply scale up X but that
    // might overflow, e.g when comparing 9 with 1_000_000_000, 9 would
    // be scaled up to 9_000_000_000. So we scale up by the next
    // smallest power and scale down Y to drop one digit. It is OK to
    // drop one digit from the longer integer since the final digit is
    // past the length of the shorter integer.
    x_scaled *= kPowersOf10[y_log10 - x_log10 - 1];
    y_scaled /= 10;
    tie = -1;
  } else if (y_log10 < x_log10) {
    y_scaled *= kPowersOf10[x_log10 - y_log10 - 1];
    x_scaled /= 10;
    tie = 1;
  }

  if (x_scaled < y_scaled) return Smi::FromInt(-1).ptr();
  if (x_scaled > y_scaled) return Smi::FromInt(1).ptr();
  return Smi::FromInt(tie).ptr();
}

void JSFinalizationRegistry::RemoveCellFromUnregisterTokenMap(
    Isolate* isolate, Address raw_finalization_registry,
    Address raw_weak_cell) {
  DisallowGarbageCollection no_gc;
  Tagged<JSFinalizationRegistry> finalization_registry =
      Cast<JSFinalizationRegistry>(Tagged<Object>(raw_finalization_registry));
  Tagged<WeakCell> weak_cell = Cast<WeakCell>(Tagged<Object>(raw_weak_cell));
  DCHECK(!IsUndefined(weak_cell->unregister_token(), isolate));
  Tagged<Undefined> undefined = ReadOnlyRoots(isolate).undefined_value();

  // Remove weak_cell from the linked list of other WeakCells with the same
  // unregister token and remove its unregister token from key_map if necessary
  // without shrinking it. Since shrinking may allocate, it is performed by the
  // caller after looping, or on exception.
  if (IsUndefined(weak_cell->key_list_prev(), isolate)) {
    Tagged<SimpleNumberDictionary> key_map =
        Cast<SimpleNumberDictionary>(finalization_registry->key_map());
    Tagged<HeapObject> unregister_token = weak_cell->unregister_token();
    uint32_t key = Smi::ToInt(Object::GetHash(unregister_token));
    InternalIndex entry = key_map->FindEntry(isolate, key);
    DCHECK(entry.is_found());

    if (IsUndefined(weak_cell->key_list_next(), isolate)) {
      // weak_cell is the only one associated with its key; remove the key
      // from the hash table.
      key_map->ClearEntry(entry);
      key_map->ElementRemoved();
    } else {
      // weak_cell is the list head for its key; we need to change the value
      // of the key in the hash table.
      Tagged<WeakCell> next = Cast<WeakCell>(weak_cell->key_list_next());
      DCHECK_EQ(next->key_list_prev(), weak_cell);
      next->set_key_list_prev(undefined);
      key_map->ValueAtPut(entry, next);
    }
  } else {
    // weak_cell is somewhere in the middle of its key list.
    Tagged<WeakCell> prev = Cast<WeakCell>(weak_cell->key_list_prev());
    prev->set_key_list_next(weak_cell->key_list_next());
    if (!IsUndefined(weak_cell->key_list_next())) {
      Tagged<WeakCell> next = Cast<WeakCell>(weak_cell->key_list_next());
      next->set_key_list_prev(weak_cell->key_list_prev());
    }
  }

  // weak_cell is now removed from the unregister token map, so clear its
  // unregister token-related fields.
  weak_cell->set_unregister_token(undefined);
  weak_cell->set_key_list_prev(undefined);
  weak_cell->set_key_list_next(undefined);
}

// static
bool MapWord::IsMapOrForwarded(Tagged<Map> map) {
  MapWord map_word = map->map_word(kRelaxedLoad);
  if (map_word.IsForwardingAddress()) {
    // During GC we can't access forwarded maps without synchronization.
    return true;
  }
  // The meta map might be moved away by GC too but we can read instance
  // type from both old and new location as it can't change.
  return InstanceTypeChecker::IsMap(map_word.ToMap()->instance_type());
}

// Force instantiation of template instances class.
// Please note this list is compiler dependent.
// Keep this at the end of this file

#define EXTERN_DEFINE_HASH_TABLE(DERIVED, SHAPE)                            \
  template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)                  \
      HashTable<DERIVED, SHAPE>;                                            \
                                                                            \
  template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) Handle<DERIVED>        \
  HashTable<DERIVED, SHAPE>::New(Isolate*, int, AllocationType,             \
                                 MinimumCapacity);                          \
  template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) Handle<DERIVED>        \
  HashTable<DERIVED, SHAPE>::New(LocalIsolate*, int, AllocationType,        \
                                 MinimumCapacity);                          \
                                                                            \
  template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) Handle<DERIVED>        \
  HashTable<DERIVED, SHAPE>::EnsureCapacity(Isolate*, Handle<DERIVED>, int, \
                                            AllocationType);                \
  template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) Handle<DERIVED>        \
  HashTable<DERIVED, SHAPE>::EnsureCapacity(LocalIsolate*, Handle<DERIVED>, \
                                            int, AllocationType);

#define EXTERN_DEFINE_OBJECT_BASE_HASH_TABLE(DERIVED, SHAPE) \
  EXTERN_DEFINE_HASH_TABLE(DERIVED, SHAPE)                   \
  template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)   \
      ObjectHashTableBase<DERIVED, SHAPE>;

#define EXTERN_DEFINE_MULTI_OBJECT_BASE_HASH_TABLE(DERIVED, N)    \
  EXTERN_DEFINE_HASH_TABLE(DERIVED, ObjectMultiHashTableShape<N>) \
  template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)        \
      ObjectMultiHashTableBase<DERIVED, N>;

#define EXTERN_DEFINE_DICTIONARY(DERIVED, SHAPE)                              \
  EXTERN_DEFINE_HASH_TABLE(DERIVED, SHAPE)                                    \
  template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)                    \
      Dictionary<DERIVED, SHAPE>;                                             \
                                                                              \
  template V8_EXPORT_PRIVATE Handle<DERIVED> Dictionary<DERIVED, SHAPE>::Add( \
      Isolate* isolate, Handle<DERIVED>, Key, DirectHandle<Object>,           \
      PropertyDetails, InternalIndex*);                                       \
  template V8_EXPORT_PRIVATE Handle<DERIVED> Dictionary<DERIVED, SHAPE>::Add( \
      LocalIsolate* isolate, Handle<DERIVED>, Key, DirectHandle<Object>,      \
      PropertyDetails, InternalIndex*);

#define EXTERN_DEFINE_BASE_NAME_DICTIONARY(DERIVED, SHAPE)                     \
  EXTERN_DEFINE_DICTIONARY(DERIVED, SHAPE)                                     \
  template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)                     \
      BaseNameDictionary<DERIVED, SHAPE>;                                      \
                                                                               \
  template V8_EXPORT_PRIVATE Handle<DERIVED>                                   \
  BaseNameDictionary<DERIVED, SHAPE>::New(Isolate*, int, AllocationType,       \
                                          MinimumCapacity);                    \
  template V8_EXPORT_PRIVATE Handle<DERIVED>                                   \
  BaseNameDictionary<DERIVED, SHAPE>::New(LocalIsolate*, int, AllocationType,  \
                                          MinimumCapacity);                    \
                                                                               \
  template Handle<DERIVED>                                                     \
  BaseNameDictionary<DERIVED, SHAPE>::AddNoUpdateNextEnumerationIndex(         \
      Isolate* isolate, Handle<DERIVED>, Key, Handle<Object>, PropertyDetails, \
      InternalIndex*);                                                         \
  template Handle<DERIVED>                                                     \
  BaseNameDictionary<DERIVED, SHAPE>::AddNoUpdateNextEnumerationIndex(         \
      LocalIsolate* isolate, Handle<DERIVED>, Key, Handle<Object>,             \
      PropertyDetails, InternalIndex*);

EXTERN_DEFINE_HASH_TABLE(StringSet, StringSetShape)
EXTERN_DEFINE_HASH_TABLE(CompilationCacheTable, CompilationCacheShape)
EXTERN_DEFINE_HASH_TABLE(ObjectHashSet, ObjectHashSetShape)
EXTERN_DEFINE_HASH_TABLE(NameToIndexHashTable, NameToIndexShape)
EXTERN_DEFINE_HASH_TABLE(RegisteredSymbolTable, RegisteredSymbolTableShape)

EXTERN_DEFINE_OBJECT_BASE_HASH_TABLE(ObjectHashTable, ObjectHashTableShape)
EXTERN_DEFINE_OBJECT_BASE_HASH_TABLE(EphemeronHashTable, ObjectHashTableShape)

EXTERN_DEFINE_MULTI_OBJECT_BASE_HASH_TABLE(ObjectTwoHashTable, 2)

EXTERN_DEFINE_DICTIONARY(SimpleNumberDictionary, SimpleNumberDictionaryShape)
EXTERN_DEFINE_DICTIONARY(NumberDictionary, NumberDictionaryShape)

template V8_EXPORT_PRIVATE void
Dictionary<NumberDictionary, NumberDictionaryShape>::UncheckedAdd<
    Isolate, AllocationType::kSharedOld>(Isolate*, Handle<NumberDictionary>,
                                         uint32_t, DirectHandle<Object>,
                                         PropertyDetails);

EXTERN_DEFINE_BASE_NAME_DICTIONARY(NameDictionary, NameDictionaryShape)
template V8_EXPORT_PRIVATE Handle<NameDictionary> NameDictionary::New(
    Isolate*, int, AllocationType, MinimumCapacity);
template V8_EXPORT_PRIVATE Handle<NameDictionary> NameDictionary::New(
    LocalIsolate*, int, AllocationType, MinimumCapacity);

EXTERN_DEFINE_BASE_NAME_DICTIONARY(GlobalDictionary, GlobalDictionaryShape)

#undef EXTERN_DEFINE_HASH_TABLE
#undef EXTERN_DEFINE_OBJECT_BASE_HASH_TABLE
#undef EXTERN_DEFINE_DICTIONARY
#undef EXTERN_DEFINE_BASE_NAME_DICTIONARY

}  // namespace v8::internal
