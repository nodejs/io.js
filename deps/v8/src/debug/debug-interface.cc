// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-interface.h"

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/base/utils/random-number-generator.h"
#include "src/codegen/compiler.h"
#include "src/codegen/script-details.h"
#include "src/date/date.h"
#include "src/debug/debug-coverage.h"
#include "src/debug/debug-evaluate.h"
#include "src/debug/debug-property-iterator.h"
#include "src/debug/debug-stack-trace-iterator.h"
#include "src/debug/debug.h"
#include "src/execution/vm-state-inl.h"
#include "src/heap/heap.h"
#include "src/objects/js-generator-inl.h"
#include "src/profiler/heap-profiler.h"
#include "src/strings/string-builder-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/debug/debug-wasm-objects-inl.h"
#include "src/wasm/wasm-disassembler.h"
#include "src/wasm/wasm-engine.h"
#endif  // V8_ENABLE_WEBASSEMBLY

// Has to be the last include (doesn't have include guards):
#include "src/api/api-macros.h"

namespace v8 {
namespace debug {

void SetContextId(Local<Context> context, int id) {
  auto v8_context = Utils::OpenDirectHandle(*context);
  DCHECK_NO_SCRIPT_NO_EXCEPTION(v8_context->GetIsolate());
  v8_context->set_debug_context_id(i::Smi::FromInt(id));
}

int GetContextId(Local<Context> context) {
  auto v8_context = Utils::OpenDirectHandle(*context);
  DCHECK_NO_SCRIPT_NO_EXCEPTION(v8_context->GetIsolate());
  i::Tagged<i::Object> value = v8_context->debug_context_id();
  return (IsSmi(value)) ? i::Smi::ToInt(value) : 0;
}

void SetInspector(Isolate* isolate, v8_inspector::V8Inspector* inspector) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  DCHECK_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  if (inspector == nullptr) {
    i_isolate->set_inspector(nullptr);
  } else {
    i_isolate->set_inspector(inspector);
  }
}

v8_inspector::V8Inspector* GetInspector(Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  DCHECK_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  return i_isolate->inspector();
}

namespace {

i::Handle<i::String> GetBigIntStringPresentationHandle(
    i::Isolate* i_isolate, i::DirectHandle<i::BigInt> i_bigint) {
  // For large BigInts computing the decimal string representation
  // can take a long time, so we go with hexadecimal in that case.
  int radix = (i_bigint->Words64Count() > 100 * 1000) ? 16 : 10;
  i::Handle<i::String> string_value =
      i::BigInt::ToString(i_isolate, i_bigint, radix, i::kDontThrow)
          .ToHandleChecked();
  if (radix == 16) {
    if (i_bigint->IsNegative()) {
      string_value =
          i_isolate->factory()
              ->NewConsString(
                  i_isolate->factory()->NewStringFromAsciiChecked("-0x"),
                  i_isolate->factory()->NewProperSubString(
                      string_value, 1, string_value->length() - 1))
              .ToHandleChecked();
    } else {
      string_value =
          i_isolate->factory()
              ->NewConsString(
                  i_isolate->factory()->NewStringFromAsciiChecked("0x"),
                  string_value)
              .ToHandleChecked();
    }
  }
  return string_value;
}

}  // namespace

Local<String> GetBigIntStringValue(Isolate* isolate, Local<BigInt> bigint) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  i::DirectHandle<i::BigInt> i_bigint = Utils::OpenDirectHandle(*bigint);

  i::Handle<i::String> string_value =
      GetBigIntStringPresentationHandle(i_isolate, i_bigint);
  return Utils::ToLocal(string_value);
}

Local<String> GetBigIntDescription(Isolate* isolate, Local<BigInt> bigint) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  i::DirectHandle<i::BigInt> i_bigint = Utils::OpenDirectHandle(*bigint);

  i::Handle<i::String> string_value =
      GetBigIntStringPresentationHandle(i_isolate, i_bigint);

  i::Handle<i::String> description =
      i_isolate->factory()
          ->NewConsString(
              string_value,
              i_isolate->factory()->LookupSingleCharacterStringFromCode('n'))
          .ToHandleChecked();
  return Utils::ToLocal(description);
}

Local<String> GetDateDescription(Local<Date> date) {
  auto receiver = Utils::OpenDirectHandle(*date);
  auto jsdate = i::Cast<i::JSDate>(receiver);
  i::Isolate* i_isolate = jsdate->GetIsolate();
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  auto buffer = i::ToDateString(jsdate->value(), i_isolate->date_cache(),
                                i::ToDateStringMode::kLocalDateAndTime);
  return Utils::ToLocal(i_isolate->factory()
                            ->NewStringFromUtf8(base::VectorOf(buffer))
                            .ToHandleChecked());
}

Local<String> GetFunctionDescription(Local<Function> function) {
  auto receiver = Utils::OpenHandle(*function);
  auto i_isolate = receiver->GetIsolate();
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  if (IsJSBoundFunction(*receiver)) {
    return Utils::ToLocal(
        i::JSBoundFunction::ToString(i::Cast<i::JSBoundFunction>(receiver)));
  }
  if (IsJSFunction(*receiver)) {
    auto js_function = i::Cast<i::JSFunction>(receiver);
#if V8_ENABLE_WEBASSEMBLY
    if (js_function->shared()->HasWasmExportedFunctionData()) {
      i::DirectHandle<i::WasmExportedFunctionData> function_data(
          js_function->shared()->wasm_exported_function_data(), i_isolate);
      int func_index = function_data->function_index();
      i::DirectHandle<i::WasmTrustedInstanceData> instance_data(
          function_data->instance_data(), i_isolate);
      if (instance_data->module()->origin == i::wasm::kWasmOrigin) {
        // For asm.js functions, we can still print the source
        // code (hopefully), so don't bother with them here.
        auto debug_name =
            i::GetWasmFunctionDebugName(i_isolate, instance_data, func_index);
        i::IncrementalStringBuilder builder(i_isolate);
        builder.AppendCStringLiteral("function ");
        builder.AppendString(debug_name);
        builder.AppendCStringLiteral("() { [native code] }");
        return Utils::ToLocal(builder.Finish().ToHandleChecked());
      }
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    return Utils::ToLocal(i::JSFunction::ToString(js_function));
  }
  return Utils::ToLocal(
      receiver->GetIsolate()->factory()->function_native_code_string());
}

void SetBreakOnNextFunctionCall(Isolate* isolate) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  DCHECK_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  i_isolate->debug()->SetBreakOnNextFunctionCall();
}

void ClearBreakOnNextFunctionCall(Isolate* isolate) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  DCHECK_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  i_isolate->debug()->ClearBreakOnNextFunctionCall();
}

MaybeLocal<Array> GetInternalProperties(Isolate* v8_isolate,
                                        Local<Value> value) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  i::Handle<i::Object> val = Utils::OpenHandle(*value);
  i::Handle<i::JSArray> result;
  if (!i::Runtime::GetInternalProperties(isolate, val).ToHandle(&result))
    return MaybeLocal<Array>();
  return Utils::ToLocal(result);
}

namespace {

using FlagFilter = std::function<bool(i::IsStaticFlag)>;
using VariableModeFilter = std::function<bool(i::VariableMode)>;
using ContextLocalIterator = std::function<void(
    i::VariableMode, i::Handle<i::String>, i::Handle<i::Object>)>;

void ForEachContextLocal(i::Isolate* isolate,
                         i::DirectHandle<i::Context> context,
                         const VariableModeFilter& var_mode_filter,
                         const FlagFilter& flag_filter,
                         const ContextLocalIterator& context_local_it) {
  DCHECK_NO_SCRIPT_NO_EXCEPTION(isolate);
  i::Handle<i::ScopeInfo> scope_info(context->scope_info(), isolate);
  for (auto it : i::ScopeInfo::IterateLocalNames(scope_info)) {
    i::Handle<i::String> name(it->name(), isolate);
    i::VariableMode mode = scope_info->ContextLocalMode(it->index());
    if (!var_mode_filter(mode)) {
      continue;
    }
    i::IsStaticFlag flag = scope_info->ContextLocalIsStaticFlag(it->index());
    if (!flag_filter(flag)) {
      continue;
    }
    int context_index = scope_info->ContextHeaderLength() + it->index();
    i::Handle<i::Object> slot_value(context->get(context_index), isolate);
    context_local_it(mode, name, slot_value);
  }
}

}  // namespace

bool GetPrivateMembers(Local<Context> context, Local<Object> object, int filter,
                       LocalVector<Value>* names_out,
                       LocalVector<Value>* values_out) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(context->GetIsolate());
  API_RCS_SCOPE(isolate, debug, GetPrivateMembers);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);

  bool include_methods =
      filter & static_cast<int>(PrivateMemberFilter::kPrivateMethods);
  bool include_fields =
      filter & static_cast<int>(PrivateMemberFilter::kPrivateFields);
  bool include_accessors =
      filter & static_cast<int>(PrivateMemberFilter::kPrivateAccessors);
  bool include_methods_or_accessors = include_methods || include_accessors;

  auto var_mode_filter =
      include_methods
          ? (include_accessors ? i::IsPrivateMethodOrAccessorVariableMode
                               : i::IsPrivateMethodVariableMode)
          : i::IsPrivateAccessorVariableMode;
  auto constexpr instance_filter = [](i::IsStaticFlag flag) {
    return flag == i::IsStaticFlag::kNotStatic;
  };
  auto constexpr static_filter = [](i::IsStaticFlag flag) {
    return flag == i::IsStaticFlag::kStatic;
  };

  i::Handle<i::JSReceiver> receiver = Utils::OpenHandle(*object);

  i::PropertyFilter key_filter =
      static_cast<i::PropertyFilter>(i::PropertyFilter::PRIVATE_NAMES_ONLY);
  i::Handle<i::FixedArray> keys;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, keys,
      i::KeyAccumulator::GetKeys(isolate, receiver,
                                 i::KeyCollectionMode::kOwnOnly, key_filter,
                                 i::GetKeysConversion::kConvertToString),
      false);

  // Estimate number of private fields and private instance methods/accessors.
  int private_entries_count = 0;
  auto count_private_entry =
      [&](i::VariableMode mode, i::DirectHandle<i::String>,
          i::DirectHandle<i::Object>) { private_entries_count++; };
  for (int i = 0; i < keys->length(); ++i) {
    // Exclude the private brand symbols.
    i::Handle<i::Symbol> key(i::Cast<i::Symbol>(keys->get(i)), isolate);
    if (key->is_private_brand()) {
      if (include_methods_or_accessors) {
        i::Handle<i::Object> value;
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, value, i::Object::GetProperty(isolate, receiver, key),
            false);

        i::DirectHandle<i::Context> value_context(i::Cast<i::Context>(*value),
                                                  isolate);
        ForEachContextLocal(isolate, value_context, var_mode_filter,
                            instance_filter, count_private_entry);
      }
    } else if (include_fields) {
      private_entries_count++;
    }
  }

  // Estimate number of static private methods/accessors for classes.
  bool has_static_private_methods_or_accessors = false;
  if (include_methods_or_accessors) {
    if (IsJSFunction(*receiver)) {
      i::DirectHandle<i::JSFunction> func(i::Cast<i::JSFunction>(*receiver),
                                          isolate);
      i::DirectHandle<i::SharedFunctionInfo> shared(func->shared(), isolate);
      if (shared->is_class_constructor() &&
          shared->has_static_private_methods_or_accessors()) {
        has_static_private_methods_or_accessors = true;
        i::DirectHandle<i::Context> func_context(func->context(), isolate);
        ForEachContextLocal(isolate, func_context, var_mode_filter,
                            static_filter, count_private_entry);
      }
    }
  }

  DCHECK(names_out->empty());
  names_out->reserve(private_entries_count);
  DCHECK(values_out->empty());
  values_out->reserve(private_entries_count);

  auto add_private_entry = [&](i::VariableMode mode, i::Handle<i::String> name,
                               i::Handle<i::Object> value) {
    DCHECK_IMPLIES(mode == i::VariableMode::kPrivateMethod,
                   IsJSFunction(*value));
    DCHECK_IMPLIES(mode != i::VariableMode::kPrivateMethod,
                   IsAccessorPair(*value));
    names_out->push_back(Utils::ToLocal(name));
    values_out->push_back(Utils::ToLocal(value));
  };
  if (has_static_private_methods_or_accessors) {
    i::DirectHandle<i::Context> receiver_context(
        i::Cast<i::JSFunction>(*receiver)->context(), isolate);
    ForEachContextLocal(isolate, receiver_context, var_mode_filter,
                        static_filter, add_private_entry);
  }

  for (int i = 0; i < keys->length(); ++i) {
    i::DirectHandle<i::Object> obj_key(keys->get(i), isolate);
    i::Handle<i::Symbol> key(i::Cast<i::Symbol>(*obj_key), isolate);
    CHECK(key->is_private_name());
    i::Handle<i::Object> value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, value, i::Object::GetProperty(isolate, receiver, key), false);
    if (key->is_private_brand()) {
      if (include_methods_or_accessors) {
        DCHECK(IsContext(*value));
        i::DirectHandle<i::Context> value_context(i::Cast<i::Context>(*value),
                                                  isolate);
        ForEachContextLocal(isolate, value_context, var_mode_filter,
                            instance_filter, add_private_entry);
      }
    } else if (include_fields) {  // Private fields
      i::DirectHandle<i::String> name(
          i::Cast<i::String>(i::Cast<i::Symbol>(*key)->description()), isolate);
      names_out->push_back(Utils::ToLocal(name));
      values_out->push_back(Utils::ToLocal(value));
    }
  }

  DCHECK_EQ(names_out->size(), values_out->size());
  DCHECK_LE(names_out->size(), private_entries_count);
  return true;
}

MaybeLocal<Context> GetCreationContext(Local<Object> value) {
  if (IsJSGlobalProxy(*Utils::OpenDirectHandle(*value))) {
    return MaybeLocal<Context>();
  }
  return value->GetCreationContext();
}

void ChangeBreakOnException(Isolate* isolate, ExceptionBreakState type) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  DCHECK_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  i_isolate->debug()->ChangeBreakOnException(
      i::BreakCaughtException,
      type == BreakOnCaughtException || type == BreakOnAnyException);
  i_isolate->debug()->ChangeBreakOnException(
      i::BreakUncaughtException,
      type == BreakOnUncaughtException || type == BreakOnAnyException);
}

void SetBreakPointsActive(Isolate* v8_isolate, bool is_active) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  isolate->debug()->set_break_points_active(is_active);
}

void PrepareStep(Isolate* v8_isolate, StepAction action) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_BASIC(isolate);
  CHECK(isolate->debug()->CheckExecutionState());
  // Clear all current stepping setup.
  isolate->debug()->ClearStepping();
  // Prepare step.
  isolate->debug()->PrepareStep(static_cast<i::StepAction>(action));
}

bool PrepareRestartFrame(Isolate* v8_isolate, int callFrameOrdinal) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_BASIC(isolate);
  CHECK(isolate->debug()->CheckExecutionState());

  i::DebugStackTraceIterator it(isolate, callFrameOrdinal);
  if (it.Done() || !it.CanBeRestarted()) return false;

  // Clear all current stepping setup.
  isolate->debug()->ClearStepping();
  it.PrepareRestart();
  return true;
}

void ClearStepping(Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  // Clear all current stepping setup.
  isolate->debug()->ClearStepping();
}

void BreakRightNow(Isolate* v8_isolate,
                   base::EnumSet<debug::BreakReason> break_reasons) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_BASIC(isolate);
  isolate->debug()->HandleDebugBreak(i::kIgnoreIfAllFramesBlackboxed,
                                     break_reasons);
}

void SetTerminateOnResume(Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  isolate->debug()->SetTerminateOnResume();
}

bool CanBreakProgram(Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_BASIC(isolate);
  return !isolate->debug()->AllFramesOnStackAreBlackboxed();
}

size_t ScriptSource::Length() const {
  auto source = Utils::OpenDirectHandle(this);
  if (IsString(*source)) {
    return i::Cast<i::String>(source)->length();
  }
  return Size();
}

size_t ScriptSource::Size() const {
#if V8_ENABLE_WEBASSEMBLY
  MemorySpan<const uint8_t> wasm_bytecode;
  if (WasmBytecode().To(&wasm_bytecode)) {
    return wasm_bytecode.size();
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  auto source = Utils::OpenDirectHandle(this);
  if (!IsString(*source)) return 0;
  auto string = i::Cast<i::String>(source);
  return string->length() * (string->IsTwoByteRepresentation() ? 2 : 1);
}

MaybeLocal<String> ScriptSource::JavaScriptCode() const {
  i::Handle<i::HeapObject> source = Utils::OpenHandle(this);
  if (!IsString(*source)) return MaybeLocal<String>();
  return Utils::ToLocal(i::Cast<i::String>(source));
}

#if V8_ENABLE_WEBASSEMBLY
Maybe<MemorySpan<const uint8_t>> ScriptSource::WasmBytecode() const {
  auto source = Utils::OpenDirectHandle(this);
  if (!IsForeign(*source)) return Nothing<MemorySpan<const uint8_t>>();
  base::Vector<const uint8_t> wire_bytes =
      i::Cast<i::Managed<i::wasm::NativeModule>>(*source)->raw()->wire_bytes();
  return Just(MemorySpan<const uint8_t>{wire_bytes.begin(), wire_bytes.size()});
}
#endif  // V8_ENABLE_WEBASSEMBLY

Isolate* Script::GetIsolate() const {
  return reinterpret_cast<Isolate*>(
      Utils::OpenDirectHandle(this)->GetIsolate());
}

ScriptOriginOptions Script::OriginOptions() const {
  return Utils::OpenDirectHandle(this)->origin_options();
}

bool Script::WasCompiled() const {
  return Utils::OpenDirectHandle(this)->compilation_state() ==
         i::Script::CompilationState::kCompiled;
}

bool Script::IsEmbedded() const {
  auto script = Utils::OpenDirectHandle(this);
  return script->context_data() ==
         script->GetReadOnlyRoots().uninitialized_symbol();
}

int Script::Id() const { return Utils::OpenDirectHandle(this)->id(); }

int Script::StartLine() const {
  return Utils::OpenDirectHandle(this)->line_offset();
}

int Script::StartColumn() const {
  return Utils::OpenDirectHandle(this)->column_offset();
}

int Script::EndLine() const {
  i::DirectHandle<i::Script> script = Utils::OpenDirectHandle(this);
#if V8_ENABLE_WEBASSEMBLY
  if (script->type() == i::Script::Type::kWasm) return 0;
#endif  // V8_ENABLE_WEBASSEMBLY
  if (!IsString(script->source())) {
    return script->line_offset();
  }
  i::Isolate* isolate = script->GetIsolate();
  i::HandleScope scope(isolate);
  i::Script::PositionInfo info;
  i::Script::GetPositionInfo(
      script, i::Cast<i::String>(script->source())->length(), &info);
  return info.line;
}

int Script::EndColumn() const {
  i::DirectHandle<i::Script> script = Utils::OpenDirectHandle(this);
#if V8_ENABLE_WEBASSEMBLY
  if (script->type() == i::Script::Type::kWasm) {
    return script->wasm_native_module()->wire_bytes().length();
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  if (!IsString(script->source())) {
    return script->column_offset();
  }
  i::Isolate* isolate = script->GetIsolate();
  i::HandleScope scope(isolate);
  i::Script::PositionInfo info;
  i::Script::GetPositionInfo(
      script, i::Cast<i::String>(script->source())->length(), &info);
  return info.column;
}

MaybeLocal<String> Script::Name() const {
  auto script = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = script->GetIsolate();
  i::DirectHandle<i::Object> value(script->name(), isolate);
  if (!IsString(*value)) return MaybeLocal<String>();
  return Utils::ToLocal(i::Cast<i::String>(value));
}

MaybeLocal<String> Script::SourceURL() const {
  auto script = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = script->GetIsolate();
  i::DirectHandle<i::PrimitiveHeapObject> value(script->source_url(), isolate);
  if (!IsString(*value)) return MaybeLocal<String>();
  return Utils::ToLocal(i::Cast<i::String>(value));
}

MaybeLocal<String> Script::SourceMappingURL() const {
  auto script = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = script->GetIsolate();
  i::DirectHandle<i::Object> value(script->source_mapping_url(), isolate);
  if (!IsString(*value)) return MaybeLocal<String>();
  return Utils::ToLocal(i::Cast<i::String>(value));
}

MaybeLocal<String> Script::GetSha256Hash() const {
  i::DirectHandle<i::Script> script = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = script->GetIsolate();
  i::Handle<i::String> value =
      i::Script::GetScriptHash(isolate, script, /* forceForInspector: */ true);
  return Utils::ToLocal(value);
}

Maybe<int> Script::ContextId() const {
  auto script = Utils::OpenDirectHandle(this);
  i::Tagged<i::Object> value = script->context_data();
  if (IsSmi(value)) return Just(i::Smi::ToInt(value));
  return Nothing<int>();
}

Local<ScriptSource> Script::Source() const {
  auto script = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = script->GetIsolate();
#if V8_ENABLE_WEBASSEMBLY
  if (script->type() == i::Script::Type::kWasm) {
    i::DirectHandle<i::Object> wasm_native_module(
        script->wasm_managed_native_module(), isolate);
    return Utils::Convert<i::Object, ScriptSource>(wasm_native_module);
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  i::DirectHandle<i::PrimitiveHeapObject> source(script->source(), isolate);
  return Utils::Convert<i::PrimitiveHeapObject, ScriptSource>(source);
}

#if V8_ENABLE_WEBASSEMBLY
bool Script::IsWasm() const {
  return Utils::OpenDirectHandle(this)->type() == i::Script::Type::kWasm;
}
#endif  // V8_ENABLE_WEBASSEMBLY

bool Script::IsModule() const {
  return Utils::OpenDirectHandle(this)->origin_options().IsModule();
}

namespace {

int GetSmiValue(i::DirectHandle<i::FixedArray> array, int index) {
  return i::Smi::ToInt(array->get(index));
}

bool CompareBreakLocation(const i::BreakLocation& loc1,
                          const i::BreakLocation& loc2) {
  return loc1.position() < loc2.position();
}

}  // namespace

bool Script::GetPossibleBreakpoints(
    const Location& start, const Location& end, bool restrict_to_function,
    std::vector<BreakLocation>* locations) const {
  CHECK(!start.IsEmpty());
  i::Handle<i::Script> script = Utils::OpenHandle(this);
#if V8_ENABLE_WEBASSEMBLY
  if (script->type() == i::Script::Type::kWasm) {
    i::wasm::NativeModule* native_module = script->wasm_native_module();
    return i::WasmScript::GetPossibleBreakpoints(native_module, start, end,
                                                 locations);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  i::Isolate* isolate = script->GetIsolate();

  int start_offset, end_offset;
  if (!GetSourceOffset(start, GetSourceOffsetMode::kClamp).To(&start_offset)) {
    return false;
  }
  if (end.IsEmpty()) {
    end_offset = std::numeric_limits<int>::max();
  } else if (!GetSourceOffset(end, GetSourceOffsetMode::kClamp)
                  .To(&end_offset)) {
    return false;
  }
  if (start_offset >= end_offset) return true;

  std::vector<i::BreakLocation> v8_locations;
  if (!isolate->debug()->GetPossibleBreakpoints(
          script, start_offset, end_offset, restrict_to_function,
          &v8_locations)) {
    return false;
  }

  std::sort(v8_locations.begin(), v8_locations.end(), CompareBreakLocation);
  for (const auto& v8_location : v8_locations) {
    Location location = GetSourceLocation(v8_location.position());
    locations->emplace_back(location.GetLineNumber(),
                            location.GetColumnNumber(), v8_location.type());
  }
  return true;
}

Maybe<int> Script::GetSourceOffset(const Location& location,
                                   GetSourceOffsetMode mode) const {
  i::DirectHandle<i::Script> script = Utils::OpenDirectHandle(this);
#if V8_ENABLE_WEBASSEMBLY
  if (script->type() == i::Script::Type::kWasm) {
    return location.GetLineNumber() == 0 ? Just(location.GetColumnNumber())
                                         : Nothing<int>();
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  int line = location.GetLineNumber();
  int column = location.GetColumnNumber();
  if (!script->HasSourceURLComment()) {
    // Line/column number for inline <script>s with sourceURL annotation
    // are supposed to be related to the <script> tag, otherwise they
    // are relative to the parent file. Keep this in sync with the logic
    // in GetSourceLocation() below.
    line -= script->line_offset();
    if (line == 0) column -= script->column_offset();
  }

  i::Script::InitLineEnds(script->GetIsolate(), script);
  auto line_ends = i::Cast<i::FixedArray>(
      i::direct_handle(script->line_ends(), script->GetIsolate()));
  if (line < 0) {
    if (mode == GetSourceOffsetMode::kClamp) {
      return Just(0);
    }
    return Nothing<int>();
  }
  if (line >= line_ends->length()) {
    if (mode == GetSourceOffsetMode::kClamp) {
      return Just(GetSmiValue(line_ends, line_ends->length() - 1));
    }
    return Nothing<int>();
  }
  if (column < 0) {
    if (mode != GetSourceOffsetMode::kClamp) {
      return Nothing<int>();
    }
    column = 0;
  }
  int offset = column;
  if (line > 0) {
    int prev_line_end_offset = GetSmiValue(line_ends, line - 1);
    offset += prev_line_end_offset + 1;
  }
  int line_end_offset = GetSmiValue(line_ends, line);
  if (offset > line_end_offset) {
    // Be permissive with columns that don't exist,
    // as long as they are clearly within the range
    // of the script.
    if (line < line_ends->length() - 1 || mode == GetSourceOffsetMode::kClamp) {
      return Just(line_end_offset);
    }
    return Nothing<int>();
  }
  return Just(offset);
}

Location Script::GetSourceLocation(int offset) const {
  i::DirectHandle<i::Script> script = Utils::OpenDirectHandle(this);
  i::Script::PositionInfo info;
  i::Script::GetPositionInfo(script, offset, &info);
  if (script->HasSourceURLComment()) {
    // Line/column number for inline <script>s with sourceURL annotation
    // are supposed to be related to the <script> tag, otherwise they
    // are relative to the parent file. Keep this in sync with the logic
    // in GetSourceOffset() above.
    info.line -= script->line_offset();
    if (info.line == 0) info.column -= script->column_offset();
  }
  return Location(info.line, info.column);
}

bool Script::SetScriptSource(Local<String> newSource, bool preview,
                             bool allow_top_frame_live_editing,
                             LiveEditResult* result) const {
  i::Handle<i::Script> script = Utils::OpenHandle(this);
  i::Isolate* isolate = script->GetIsolate();
  return isolate->debug()->SetScriptSource(
      script, Utils::OpenHandle(*newSource), preview,
      allow_top_frame_live_editing, result);
}

bool Script::SetBreakpoint(Local<String> condition, Location* location,
                           BreakpointId* id) const {
  i::Handle<i::Script> script = Utils::OpenHandle(this);
  i::Isolate* isolate = script->GetIsolate();
  int offset;
  if (!GetSourceOffset(*location).To(&offset)) {
    return false;
  }
  if (!isolate->debug()->SetBreakPointForScript(
          script, Utils::OpenHandle(*condition), &offset, id)) {
    return false;
  }
  *location = GetSourceLocation(offset);
  return true;
}

bool Script::SetInstrumentationBreakpoint(BreakpointId* id) const {
  i::Handle<i::Script> script = Utils::OpenHandle(this);
  i::Isolate* isolate = script->GetIsolate();
#if V8_ENABLE_WEBASSEMBLY
  if (script->type() == i::Script::Type::kWasm) {
    isolate->debug()->SetInstrumentationBreakpointForWasmScript(script, id);
    return true;
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  i::SharedFunctionInfo::ScriptIterator it(isolate, *script);
  for (i::Tagged<i::SharedFunctionInfo> sfi = it.Next(); !sfi.is_null();
       sfi = it.Next()) {
    if (sfi->is_toplevel()) {
      return isolate->debug()->SetBreakpointForFunction(
          handle(sfi, isolate), isolate->factory()->empty_string(), id,
          internal::Debug::kInstrumentation);
    }
  }
  return false;
}

#if V8_ENABLE_WEBASSEMBLY
void Script::RemoveWasmBreakpoint(BreakpointId id) {
  i::DirectHandle<i::Script> script = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = script->GetIsolate();
  isolate->debug()->RemoveBreakpointForWasmScript(script, id);
}
#endif  //  V8_ENABLE_WEBASSEMBLY

void RemoveBreakpoint(Isolate* v8_isolate, BreakpointId id) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::HandleScope handle_scope(isolate);
  isolate->debug()->RemoveBreakpoint(id);
}

Platform* GetCurrentPlatform() { return i::V8::GetCurrentPlatform(); }

void ForceGarbageCollection(Isolate* isolate, StackState embedder_stack_state) {
  i::EmbedderStackStateScope stack_scope(
      reinterpret_cast<i::Isolate*>(isolate)->heap(),
      i::EmbedderStackStateOrigin::kImplicitThroughTask, embedder_stack_state);
  isolate->LowMemoryNotification();
}

#if V8_ENABLE_WEBASSEMBLY
WasmScript* WasmScript::Cast(Script* script) {
  CHECK(script->IsWasm());
  return static_cast<WasmScript*>(script);
}

WasmScript::DebugSymbolsType WasmScript::GetDebugSymbolType() const {
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());
  switch (script->wasm_native_module()->module()->debug_symbols.type) {
    case i::wasm::WasmDebugSymbols::Type::None:
      return WasmScript::DebugSymbolsType::None;
    case i::wasm::WasmDebugSymbols::Type::EmbeddedDWARF:
      return WasmScript::DebugSymbolsType::EmbeddedDWARF;
    case i::wasm::WasmDebugSymbols::Type::ExternalDWARF:
      return WasmScript::DebugSymbolsType::ExternalDWARF;
    case i::wasm::WasmDebugSymbols::Type::SourceMap:
      return WasmScript::DebugSymbolsType::SourceMap;
  }
}

MemorySpan<const char> WasmScript::ExternalSymbolsURL() const {
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());

  const i::wasm::WasmDebugSymbols& symbols =
      script->wasm_native_module()->module()->debug_symbols;
  if (symbols.external_url.is_empty()) return {};

  internal::wasm::ModuleWireBytes wire_bytes(
      script->wasm_native_module()->wire_bytes());
  i::wasm::WasmName external_url =
      wire_bytes.GetNameOrNull(symbols.external_url);
  return {external_url.data(), external_url.size()};
}

int WasmScript::NumFunctions() const {
  i::DisallowGarbageCollection no_gc;
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());
  i::wasm::NativeModule* native_module = script->wasm_native_module();
  const i::wasm::WasmModule* module = native_module->module();
  DCHECK_GE(i::kMaxInt, module->functions.size());
  return static_cast<int>(module->functions.size());
}

int WasmScript::NumImportedFunctions() const {
  i::DisallowGarbageCollection no_gc;
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());
  i::wasm::NativeModule* native_module = script->wasm_native_module();
  const i::wasm::WasmModule* module = native_module->module();
  DCHECK_GE(i::kMaxInt, module->num_imported_functions);
  return static_cast<int>(module->num_imported_functions);
}

std::pair<int, int> WasmScript::GetFunctionRange(int function_index) const {
  i::DisallowGarbageCollection no_gc;
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());
  i::wasm::NativeModule* native_module = script->wasm_native_module();
  const i::wasm::WasmModule* module = native_module->module();
  DCHECK_LE(0, function_index);
  DCHECK_GT(module->functions.size(), function_index);
  const i::wasm::WasmFunction& func = module->functions[function_index];
  DCHECK_GE(i::kMaxInt, func.code.offset());
  DCHECK_GE(i::kMaxInt, func.code.end_offset());
  return std::make_pair(static_cast<int>(func.code.offset()),
                        static_cast<int>(func.code.end_offset()));
}

int WasmScript::GetContainingFunction(int byte_offset) const {
  i::DisallowGarbageCollection no_gc;
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());
  i::wasm::NativeModule* native_module = script->wasm_native_module();
  const i::wasm::WasmModule* module = native_module->module();
  DCHECK_LE(0, byte_offset);

  return i::wasm::GetContainingWasmFunction(module, byte_offset);
}

void WasmScript::Disassemble(DisassemblyCollector* collector,
                             std::vector<int>* function_body_offsets) {
  i::DisallowGarbageCollection no_gc;
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());
  i::wasm::NativeModule* native_module = script->wasm_native_module();
  const i::wasm::WasmModule* module = native_module->module();
  i::wasm::ModuleWireBytes wire_bytes(native_module->wire_bytes());
  i::wasm::Disassemble(module, wire_bytes, native_module->GetNamesProvider(),
                       collector, function_body_offsets);
}

void Disassemble(base::Vector<const uint8_t> wire_bytes,
                 DisassemblyCollector* collector,
                 std::vector<int>* function_body_offsets) {
  i::wasm::Disassemble(wire_bytes, collector, function_body_offsets);
}

uint32_t WasmScript::GetFunctionHash(int function_index) {
  i::DisallowGarbageCollection no_gc;
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());
  i::wasm::NativeModule* native_module = script->wasm_native_module();
  const i::wasm::WasmModule* module = native_module->module();
  DCHECK_LE(0, function_index);
  DCHECK_GT(module->functions.size(), function_index);
  const i::wasm::WasmFunction& func = module->functions[function_index];
  i::wasm::ModuleWireBytes wire_bytes(native_module->wire_bytes());
  base::Vector<const uint8_t> function_bytes =
      wire_bytes.GetFunctionBytes(&func);
  // TODO(herhut): Maybe also take module, name and signature into account.
  return i::StringHasher::HashSequentialString(function_bytes.begin(),
                                               function_bytes.length(), 0);
}

int WasmScript::CodeOffset() const {
  auto script = Utils::OpenDirectHandle(this);
  DCHECK_EQ(i::Script::Type::kWasm, script->type());
  i::wasm::NativeModule* native_module = script->wasm_native_module();
  const i::wasm::WasmModule* module = native_module->module();

  // If the module contains at least one function, the code offset must have
  // been initialized, and it cannot be zero.
  DCHECK_IMPLIES(module->num_declared_functions > 0,
                 module->code.offset() != 0);
  return module->code.offset();
}
#endif  // V8_ENABLE_WEBASSEMBLY

Location::Location(int line_number, int column_number)
    : line_number_(line_number),
      column_number_(column_number),
      is_empty_(false) {}

Location::Location()
    : line_number_(Function::kLineOffsetNotFound),
      column_number_(Function::kLineOffsetNotFound),
      is_empty_(true) {}

int Location::GetLineNumber() const {
  DCHECK(!IsEmpty());
  return line_number_;
}

int Location::GetColumnNumber() const {
  DCHECK(!IsEmpty());
  return column_number_;
}

bool Location::IsEmpty() const { return is_empty_; }

void GetLoadedScripts(Isolate* v8_isolate,
                      std::vector<v8::Global<Script>>& scripts) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  {
    i::DisallowGarbageCollection no_gc;
    i::Script::Iterator iterator(isolate);
    for (i::Tagged<i::Script> script = iterator.Next(); !script.is_null();
         script = iterator.Next()) {
#if V8_ENABLE_WEBASSEMBLY
      if (script->type() != i::Script::Type::kNormal &&
          script->type() != i::Script::Type::kWasm) {
        continue;
      }
#else
      if (script->type() != i::Script::Type::kNormal) continue;
#endif  // V8_ENABLE_WEBASSEMBLY
      if (!script->HasValidSource()) continue;
      i::HandleScope handle_scope(isolate);
      i::DirectHandle<i::Script> script_handle(script, isolate);
      scripts.emplace_back(v8_isolate, ToApiHandle<Script>(script_handle));
    }
  }
}

MaybeLocal<UnboundScript> CompileInspectorScript(Isolate* v8_isolate,
                                                 Local<String> source) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  v8::Local<v8::Context> context = Utils::ToLocal(isolate->native_context());
  PREPARE_FOR_DEBUG_INTERFACE_EXECUTION_WITH_ISOLATE(isolate, context,
                                                     UnboundScript);
  i::Handle<i::String> str = Utils::OpenHandle(*source);
  i::Handle<i::SharedFunctionInfo> result;
  {
    i::AlignedCachedData* cached_data = nullptr;
    ScriptCompiler::CompilationDetails compilation_details;
    i::MaybeHandle<i::SharedFunctionInfo> maybe_function_info =
        i::Compiler::GetSharedFunctionInfoForScriptWithCachedData(
            isolate, str, i::ScriptDetails(), cached_data,
            ScriptCompiler::kNoCompileOptions,
            ScriptCompiler::kNoCacheBecauseInspector,
            i::v8_flags.expose_inspector_scripts ? i::NOT_NATIVES_CODE
                                                 : i::INSPECTOR_CODE,
            &compilation_details);
    has_exception = !maybe_function_info.ToHandle(&result);
    RETURN_ON_FAILED_EXECUTION(UnboundScript);
  }
  RETURN_ESCAPED(ToApiHandle<UnboundScript>(result));
}

#if V8_ENABLE_WEBASSEMBLY
void EnterDebuggingForIsolate(Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::wasm::GetWasmEngine()->EnterDebuggingForIsolate(isolate);
}

void LeaveDebuggingForIsolate(Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::wasm::GetWasmEngine()->LeaveDebuggingForIsolate(isolate);
}
#endif  // V8_ENABLE_WEBASSEMBLY

void SetDebugDelegate(Isolate* v8_isolate, DebugDelegate* delegate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  isolate->debug()->SetDebugDelegate(delegate);
}

void SetAsyncEventDelegate(Isolate* v8_isolate, AsyncEventDelegate* delegate) {
  reinterpret_cast<i::Isolate*>(v8_isolate)->set_async_event_delegate(delegate);
}

void ResetBlackboxedStateCache(Isolate* v8_isolate, Local<Script> script) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  i::DisallowGarbageCollection no_gc;
  i::SharedFunctionInfo::ScriptIterator iter(isolate,
                                             *Utils::OpenDirectHandle(*script));
  for (i::Tagged<i::SharedFunctionInfo> info = iter.Next(); !info.is_null();
       info = iter.Next()) {
    if (auto debug_info = isolate->debug()->TryGetDebugInfo(info)) {
      debug_info.value()->set_computed_debug_is_blackboxed(false);
    }
  }
}

int EstimatedValueSize(Isolate* v8_isolate, Local<Value> value) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  auto object = Utils::OpenDirectHandle(*value);
  if (IsSmi(*object)) return i::kTaggedSize;
  CHECK(IsHeapObject(*object));
  return i::Cast<i::HeapObject>(object)->Size();
}

void AccessorPair::CheckCast(Value* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsAccessorPair(*obj), "v8::debug::AccessorPair::Cast",
                  "Value is not a v8::debug::AccessorPair");
}

#if V8_ENABLE_WEBASSEMBLY
void WasmValueObject::CheckCast(Value* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsWasmValueObject(*obj),
                  "v8::debug::WasmValueObject::Cast",
                  "Value is not a v8::debug::WasmValueObject");
}

bool WasmValueObject::IsWasmValueObject(Local<Value> that) {
  auto obj = Utils::OpenDirectHandle(*that);
  return i::IsWasmValueObject(*obj);
}

Local<String> WasmValueObject::type() const {
  auto object = i::Cast<i::WasmValueObject>(Utils::OpenDirectHandle(this));
  i::Isolate* isolate = object->GetIsolate();
  i::DirectHandle<i::String> type(object->type(), isolate);
  return Utils::ToLocal(type);
}
#endif  // V8_ENABLE_WEBASSEMBLY

Local<Function> GetBuiltin(Isolate* v8_isolate, Builtin requested_builtin) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  i::HandleScope handle_scope(isolate);

  CHECK_EQ(requested_builtin, kStringToLowerCase);
  i::Builtin builtin = i::Builtin::kStringPrototypeToLocaleLowerCase;

  i::Factory* factory = isolate->factory();
  i::Handle<i::String> name = isolate->factory()->empty_string();
  i::Handle<i::NativeContext> context(isolate->native_context());
  i::Handle<i::SharedFunctionInfo> info =
      factory->NewSharedFunctionInfoForBuiltin(name, builtin);
  info->set_language_mode(i::LanguageMode::kStrict);
  i::Handle<i::JSFunction> fun =
      i::Factory::JSFunctionBuilder{isolate, info, context}
          .set_map(isolate->strict_function_without_prototype_map())
          .Build();

  fun->shared()->set_internal_formal_parameter_count(i::JSParameterCount(0));
  fun->shared()->set_length(0);
  return Utils::ToLocal(handle_scope.CloseAndEscape(fun));
}

void SetConsoleDelegate(Isolate* v8_isolate, ConsoleDelegate* delegate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  DCHECK_NO_SCRIPT_NO_EXCEPTION(isolate);
  if (delegate == nullptr) {
    isolate->set_console_delegate(nullptr);
  } else {
    isolate->set_console_delegate(delegate);
  }
}

ConsoleCallArguments::ConsoleCallArguments(
    const v8::FunctionCallbackInfo<v8::Value>& info)
    : isolate_(info.GetIsolate()),
      values_(info.values_),
      length_(info.Length()) {}

ConsoleCallArguments::ConsoleCallArguments(
    internal::Isolate* isolate, const internal::BuiltinArguments& args)
    : isolate_(reinterpret_cast<v8::Isolate*>(isolate)),
      values_(args.length() > 1 ? args.address_of_first_argument() : nullptr),
      length_(args.length() - 1) {}

v8::Local<v8::Message> CreateMessageFromException(
    Isolate* v8_isolate, v8::Local<v8::Value> v8_error) {
  i::Handle<i::Object> obj = Utils::OpenHandle(*v8_error);
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
  i::HandleScope scope(isolate);
  return Utils::MessageToLocal(
      scope.CloseAndEscape(isolate->CreateMessageFromException(obj)));
}

MaybeLocal<Script> GeneratorObject::Script() {
  auto obj = Utils::OpenDirectHandle(this);
  i::Tagged<i::Object> maybe_script = obj->function()->shared()->script();
  if (!IsScript(maybe_script)) return {};
  i::Isolate* isolate = obj->GetIsolate();
  i::DirectHandle<i::Script> script(i::Cast<i::Script>(maybe_script), isolate);
  return ToApiHandle<v8::debug::Script>(script);
}

Local<Function> GeneratorObject::Function() {
  auto obj = Utils::OpenDirectHandle(this);
  return Utils::ToLocal(handle(obj->function(), obj->GetIsolate()));
}

Location GeneratorObject::SuspendedLocation() {
  auto obj = Utils::OpenDirectHandle(this);
  CHECK(obj->is_suspended());
  i::Tagged<i::Object> maybe_script = obj->function()->shared()->script();
  if (!IsScript(maybe_script)) return Location();
  i::Isolate* isolate = obj->GetIsolate();
  i::DirectHandle<i::Script> script(i::Cast<i::Script>(maybe_script), isolate);
  i::Script::PositionInfo info;
  i::SharedFunctionInfo::EnsureSourcePositionsAvailable(
      isolate, i::handle(obj->function()->shared(), isolate));
  i::Script::GetPositionInfo(script, obj->source_position(), &info);
  return Location(info.line, info.column);
}

bool GeneratorObject::IsSuspended() {
  return Utils::OpenDirectHandle(this)->is_suspended();
}

v8::Local<GeneratorObject> GeneratorObject::Cast(v8::Local<v8::Value> value) {
  CHECK(value->IsGeneratorObject());
  return ToApiHandle<GeneratorObject>(Utils::OpenHandle(*value));
}

MaybeLocal<Value> CallFunctionOn(Local<Context> context,
                                 Local<Function> function, Local<Value> recv,
                                 int argc, Global<Value> argv[],
                                 bool throw_on_side_effect) {
  auto isolate = reinterpret_cast<i::Isolate*>(context->GetIsolate());
  PREPARE_FOR_DEBUG_INTERFACE_EXECUTION_WITH_ISOLATE(isolate, context, Value);
  auto self = Utils::OpenHandle(*function);
  auto recv_obj = Utils::OpenHandle(*recv);
  static_assert(sizeof(v8::Global<v8::Value>) == sizeof(i::Handle<i::Object>));
  auto args = reinterpret_cast<i::Handle<i::Object>*>(argv);
  // Disable breaks in side-effect free mode.
  i::DisableBreak disable_break_scope(isolate->debug(), throw_on_side_effect);
  if (throw_on_side_effect) {
    isolate->debug()->StartSideEffectCheckMode();
  }
  Local<Value> result;
  has_exception = !ToLocal<Value>(
      i::Execution::Call(isolate, self, recv_obj, argc, args), &result);
  if (throw_on_side_effect) {
    isolate->debug()->StopSideEffectCheckMode();
  }
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}

MaybeLocal<v8::Value> EvaluateGlobal(v8::Isolate* isolate,
                                     v8::Local<v8::String> source,
                                     EvaluateGlobalMode mode, bool repl) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::Local<v8::Context> context = Utils::ToLocal(i_isolate->native_context());
  PREPARE_FOR_DEBUG_INTERFACE_EXECUTION_WITH_ISOLATE(i_isolate, context, Value);
  i::REPLMode repl_mode = repl ? i::REPLMode::kYes : i::REPLMode::kNo;
  Local<Value> result;
  has_exception = !ToLocal<Value>(
      i::DebugEvaluate::Global(i_isolate, Utils::OpenHandle(*source), mode,
                               repl_mode),
      &result);
  RETURN_ON_FAILED_EXECUTION(Value);
  RETURN_ESCAPED(result);
}

void GlobalLexicalScopeNames(v8::Local<v8::Context> v8_context,
                             std::vector<v8::Global<v8::String>>* names) {
  auto context = Utils::OpenDirectHandle(*v8_context);
  i::Isolate* isolate = context->GetIsolate();
  i::DirectHandle<i::ScriptContextTable> table(
      context->native_context()->script_context_table(), isolate);
  for (int i = 0; i < table->length(kAcquireLoad); i++) {
    i::DirectHandle<i::Context> script_context(table->get(i), isolate);
    DCHECK(script_context->IsScriptContext());
    i::Handle<i::ScopeInfo> scope_info(script_context->scope_info(), isolate);
    for (auto it : i::ScopeInfo::IterateLocalNames(scope_info)) {
      if (i::ScopeInfo::VariableIsSynthetic(it->name())) continue;
      names->emplace_back(reinterpret_cast<Isolate*>(isolate),
                          Utils::ToLocal(handle(it->name(), isolate)));
    }
  }
}

void SetReturnValue(v8::Isolate* v8_isolate, v8::Local<v8::Value> value) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  isolate->debug()->set_return_value(*Utils::OpenDirectHandle(*value));
}

int64_t GetNextRandomInt64(v8::Isolate* v8_isolate) {
  return reinterpret_cast<i::Isolate*>(v8_isolate)
      ->random_number_generator()
      ->NextInt64();
}

int GetDebuggingId(v8::Local<v8::Function> function) {
  auto callable = v8::Utils::OpenDirectHandle(*function);
  if (!IsJSFunction(*callable)) return i::DebugInfo::kNoDebuggingId;
  auto func = i::Cast<i::JSFunction>(callable);
  int id = func->GetIsolate()->debug()->GetFunctionDebuggingId(func);
  DCHECK_NE(i::DebugInfo::kNoDebuggingId, id);
  return id;
}

bool SetFunctionBreakpoint(v8::Local<v8::Function> function,
                           v8::Local<v8::String> condition, BreakpointId* id) {
  auto callable = Utils::OpenDirectHandle(*function);
  if (!IsJSFunction(*callable)) return false;
  auto jsfunction = i::Cast<i::JSFunction>(callable);
  i::Isolate* isolate = jsfunction->GetIsolate();
  i::DirectHandle<i::String> condition_string =
      condition.IsEmpty()
          ? i::DirectHandle<i::String>(isolate->factory()->empty_string())
          : Utils::OpenDirectHandle(*condition);
  return isolate->debug()->SetBreakpointForFunction(
      handle(jsfunction->shared(), isolate), condition_string, id);
}

PostponeInterruptsScope::PostponeInterruptsScope(v8::Isolate* isolate)
    : scope_(
          new i::PostponeInterruptsScope(reinterpret_cast<i::Isolate*>(isolate),
                                         i::StackGuard::API_INTERRUPT)) {}

PostponeInterruptsScope::~PostponeInterruptsScope() = default;

DisableBreakScope::DisableBreakScope(v8::Isolate* isolate)
    : scope_(std::make_unique<i::DisableBreak>(
          reinterpret_cast<i::Isolate*>(isolate)->debug())) {}

DisableBreakScope::~DisableBreakScope() = default;

int Coverage::BlockData::StartOffset() const { return block_->start; }

int Coverage::BlockData::EndOffset() const { return block_->end; }

uint32_t Coverage::BlockData::Count() const { return block_->count; }

int Coverage::FunctionData::StartOffset() const { return function_->start; }

int Coverage::FunctionData::EndOffset() const { return function_->end; }

uint32_t Coverage::FunctionData::Count() const { return function_->count; }

MaybeLocal<String> Coverage::FunctionData::Name() const {
  return ToApiHandle<String>(function_->name);
}

size_t Coverage::FunctionData::BlockCount() const {
  return function_->blocks.size();
}

bool Coverage::FunctionData::HasBlockCoverage() const {
  return function_->has_block_coverage;
}

Coverage::BlockData Coverage::FunctionData::GetBlockData(size_t i) const {
  return BlockData(&function_->blocks.at(i), coverage_);
}

Local<Script> Coverage::ScriptData::GetScript() const {
  return ToApiHandle<Script>(script_->script);
}

size_t Coverage::ScriptData::FunctionCount() const {
  return script_->functions.size();
}

Coverage::FunctionData Coverage::ScriptData::GetFunctionData(size_t i) const {
  return FunctionData(&script_->functions.at(i), coverage_);
}

Coverage::ScriptData::ScriptData(size_t index,
                                 std::shared_ptr<i::Coverage> coverage)
    : script_(&coverage->at(index)), coverage_(std::move(coverage)) {}

size_t Coverage::ScriptCount() const { return coverage_->size(); }

Coverage::ScriptData Coverage::GetScriptData(size_t i) const {
  return ScriptData(i, coverage_);
}

Coverage Coverage::CollectPrecise(Isolate* isolate) {
  return Coverage(
      i::Coverage::CollectPrecise(reinterpret_cast<i::Isolate*>(isolate)));
}

Coverage Coverage::CollectBestEffort(Isolate* isolate) {
  return Coverage(
      i::Coverage::CollectBestEffort(reinterpret_cast<i::Isolate*>(isolate)));
}

void Coverage::SelectMode(Isolate* isolate, CoverageMode mode) {
  i::Coverage::SelectMode(reinterpret_cast<i::Isolate*>(isolate), mode);
}

MaybeLocal<v8::Value> EphemeronTable::Get(v8::Isolate* isolate,
                                          v8::Local<v8::Value> key) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  auto self = i::Cast<i::EphemeronHashTable>(Utils::OpenDirectHandle(this));
  i::Handle<i::Object> internal_key = Utils::OpenHandle(*key);
  DCHECK(IsJSReceiver(*internal_key));

  i::DirectHandle<i::Object> value(self->Lookup(internal_key), i_isolate);

  if (IsTheHole(*value)) return {};
  return Utils::ToLocal(value);
}

Local<EphemeronTable> EphemeronTable::Set(v8::Isolate* isolate,
                                          v8::Local<v8::Value> key,
                                          v8::Local<v8::Value> value) {
  auto self = i::Cast<i::EphemeronHashTable>(Utils::OpenHandle(this));
  i::Handle<i::Object> internal_key = Utils::OpenHandle(*key);
  i::Handle<i::Object> internal_value = Utils::OpenHandle(*value);
  DCHECK(IsJSReceiver(*internal_key));

  i::DirectHandle<i::EphemeronHashTable> result(
      i::EphemeronHashTable::Put(self, internal_key, internal_value));

  return ToApiHandle<EphemeronTable>(result);
}

Local<EphemeronTable> EphemeronTable::New(v8::Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
  i::DirectHandle<i::EphemeronHashTable> table =
      i::EphemeronHashTable::New(i_isolate, 0);
  return ToApiHandle<EphemeronTable>(table);
}

EphemeronTable* EphemeronTable::Cast(v8::Value* value) {
  return static_cast<EphemeronTable*>(value);
}

Local<Value> AccessorPair::getter() {
  auto accessors = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = accessors->GetIsolate();
  i::DirectHandle<i::Object> getter(accessors->getter(), isolate);
  return Utils::ToLocal(getter);
}

Local<Value> AccessorPair::setter() {
  auto accessors = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = accessors->GetIsolate();
  i::DirectHandle<i::Object> setter(accessors->setter(), isolate);
  return Utils::ToLocal(setter);
}

bool AccessorPair::IsAccessorPair(Local<Value> that) {
  return i::IsAccessorPair(*Utils::OpenDirectHandle(*that));
}

MaybeLocal<Message> GetMessageFromPromise(Local<Promise> p) {
  i::Handle<i::JSPromise> promise = Utils::OpenHandle(*p);
  i::Isolate* isolate = promise->GetIsolate();

  i::Handle<i::Symbol> key = isolate->factory()->promise_debug_message_symbol();
  i::Handle<i::Object> maybeMessage =
      i::JSReceiver::GetDataProperty(isolate, promise, key);

  if (!IsJSMessageObject(*maybeMessage, isolate)) return MaybeLocal<Message>();
  return ToApiHandle<Message>(i::Cast<i::JSMessageObject>(maybeMessage));
}

void RecordAsyncStackTaggingCreateTaskCall(v8::Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  isolate->CountUsage(v8::Isolate::kAsyncStackTaggingCreateTaskCall);
}

void NotifyDebuggerPausedEventSent(v8::Isolate* v8_isolate) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  isolate->debug()->NotifyDebuggerPausedEventSent();
}

std::unique_ptr<PropertyIterator> PropertyIterator::Create(
    Local<Context> context, Local<Object> object, bool skip_indices) {
  internal::Isolate* isolate =
      reinterpret_cast<i::Isolate*>(context->GetIsolate());
  if (isolate->is_execution_terminating()) {
    return nullptr;
  }
  CallDepthScope<false> call_depth_scope(isolate, context);

  return i::DebugPropertyIterator::Create(isolate, Utils::OpenHandle(*object),
                                          skip_indices);
}

}  // namespace debug

namespace internal {

Maybe<bool> DebugPropertyIterator::Advance() {
  if (isolate_->is_execution_terminating()) {
    return Nothing<bool>();
  }
  Local<v8::Context> context =
      Utils::ToLocal(handle(isolate_->context()->native_context(), isolate_));
  CallDepthScope<false> call_depth_scope(isolate_, context);

  if (!AdvanceInternal()) {
    DCHECK(isolate_->has_exception());
    return Nothing<bool>();
  }
  return Just(true);
}

}  // namespace internal
}  // namespace v8

#include "src/api/api-macros-undef.h"
