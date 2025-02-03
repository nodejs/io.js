// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SCRIPT_INL_H_
#define V8_OBJECTS_SCRIPT_INL_H_

#include "src/objects/managed.h"
#include "src/objects/objects.h"
#include "src/objects/script.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/smi-inl.h"
#include "src/objects/string-inl.h"
#include "src/objects/string.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/script-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(Script)

NEVER_READ_ONLY_SPACE_IMPL(Script)

#if V8_ENABLE_WEBASSEMBLY
ACCESSORS_CHECKED(Script, wasm_breakpoint_infos, Tagged<FixedArray>,
                  kEvalFromSharedOrWrappedArgumentsOffset,
                  this->type() == Type::kWasm)
ACCESSORS_CHECKED(Script, wasm_managed_native_module, Tagged<Object>,
                  kEvalFromPositionOffset, this->type() == Type::kWasm)
ACCESSORS_CHECKED(Script, wasm_weak_instance_list, Tagged<WeakArrayList>,
                  kInfosOffset, this->type() == Type::kWasm)
#define CHECK_SCRIPT_NOT_WASM this->type() != Type::kWasm
#else
#define CHECK_SCRIPT_NOT_WASM true
#endif  // V8_ENABLE_WEBASSEMBLY

Script::Type Script::type() const {
  Tagged<Smi> value = TaggedField<Smi, kScriptTypeOffset>::load(*this);
  return static_cast<Type>(value.value());
}
void Script::set_type(Type value) {
  TaggedField<Smi, kScriptTypeOffset>::store(
      *this, Smi::FromInt(static_cast<int>(value)));
}

ACCESSORS_CHECKED(Script, eval_from_shared_or_wrapped_arguments, Tagged<Object>,
                  kEvalFromSharedOrWrappedArgumentsOffset,
                  CHECK_SCRIPT_NOT_WASM)
SMI_ACCESSORS_CHECKED(Script, eval_from_position, kEvalFromPositionOffset,
                      CHECK_SCRIPT_NOT_WASM)
#undef CHECK_SCRIPT_NOT_WASM

ACCESSORS(Script, compiled_lazy_function_positions, Tagged<Object>,
          kCompiledLazyFunctionPositionsOffset)

bool Script::is_wrapped() const {
  return IsFixedArray(eval_from_shared_or_wrapped_arguments());
}

bool Script::has_eval_from_shared() const {
  return IsSharedFunctionInfo(eval_from_shared_or_wrapped_arguments());
}

void Script::set_eval_from_shared(Tagged<SharedFunctionInfo> shared,
                                  WriteBarrierMode mode) {
  DCHECK(!is_wrapped());
  set_eval_from_shared_or_wrapped_arguments(shared, mode);
}

Tagged<SharedFunctionInfo> Script::eval_from_shared() const {
  DCHECK(has_eval_from_shared());
  return Cast<SharedFunctionInfo>(eval_from_shared_or_wrapped_arguments());
}

void Script::set_wrapped_arguments(Tagged<FixedArray> value,
                                   WriteBarrierMode mode) {
  DCHECK(!has_eval_from_shared());
  set_eval_from_shared_or_wrapped_arguments(value, mode);
}

Tagged<FixedArray> Script::wrapped_arguments() const {
  DCHECK(is_wrapped());
  return Cast<FixedArray>(eval_from_shared_or_wrapped_arguments());
}

DEF_GETTER(Script, infos, Tagged<WeakFixedArray>) {
#if V8_ENABLE_WEBASSEMBLY
  if (type() == Type::kWasm) {
    return ReadOnlyRoots(GetHeap()).empty_weak_fixed_array();
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return TaggedField<WeakFixedArray, kInfosOffset>::load(*this);
}

void Script::set_infos(Tagged<WeakFixedArray> value, WriteBarrierMode mode) {
#if V8_ENABLE_WEBASSEMBLY
  DCHECK_NE(Type::kWasm, type());
#endif  // V8_ENABLE_WEBASSEMBLY
  TaggedField<WeakFixedArray, kInfosOffset>::store(*this, value);
  CONDITIONAL_WRITE_BARRIER(*this, kInfosOffset, value, mode);
}

#if V8_ENABLE_WEBASSEMBLY
bool Script::has_wasm_breakpoint_infos() const {
  return type() == Type::kWasm && wasm_breakpoint_infos()->length() > 0;
}

wasm::NativeModule* Script::wasm_native_module() const {
  return Cast<Managed<wasm::NativeModule>>(wasm_managed_native_module())->raw();
}

bool Script::break_on_entry() const { return BreakOnEntryBit::decode(flags()); }

void Script::set_break_on_entry(bool value) {
  set_flags(BreakOnEntryBit::update(flags(), value));
}
#endif  // V8_ENABLE_WEBASSEMBLY

uint32_t Script::flags() const {
  // Use a relaxed load since background compile threads read the
  // {compilation_type()} while the foreground thread might update e.g. the
  // {origin_options}.
  return TaggedField<Smi>::Relaxed_Load(*this, kFlagsOffset).value();
}

void Script::set_flags(uint32_t new_flags) {
  DCHECK(is_int31(new_flags));
  TaggedField<Smi>::Relaxed_Store(*this, kFlagsOffset, Smi::FromInt(new_flags));
}

Script::CompilationType Script::compilation_type() const {
  return CompilationTypeBit::decode(flags());
}
void Script::set_compilation_type(CompilationType type) {
  set_flags(CompilationTypeBit::update(flags(), type));
}
Script::CompilationState Script::compilation_state() {
  return CompilationStateBit::decode(flags());
}
void Script::set_compilation_state(CompilationState state) {
  set_flags(CompilationStateBit::update(flags(), state));
}

bool Script::produce_compile_hints() const {
  return ProduceCompileHintsBit::decode(flags());
}

void Script::set_produce_compile_hints(bool produce_compile_hints) {
  set_flags(ProduceCompileHintsBit::update(flags(), produce_compile_hints));
}

bool Script::deserialized() const { return DeserializedBit::decode(flags()); }

void Script::set_deserialized(bool value) {
  set_flags(DeserializedBit::update(flags(), value));
}

bool Script::is_repl_mode() const { return IsReplModeBit::decode(flags()); }

void Script::set_is_repl_mode(bool value) {
  set_flags(IsReplModeBit::update(flags(), value));
}

ScriptOriginOptions Script::origin_options() {
  return ScriptOriginOptions(OriginOptionsBits::decode(flags()));
}
void Script::set_origin_options(ScriptOriginOptions origin_options) {
  DCHECK(!(origin_options.Flags() & ~((1 << OriginOptionsBits::kSize) - 1)));
  set_flags(OriginOptionsBits::update(flags(), origin_options.Flags()));
}

bool Script::HasValidSource() {
  Tagged<Object> src = this->source();
  if (!IsString(src)) return true;
  Tagged<String> src_str = Cast<String>(src);
  if (!StringShape(src_str).IsExternal()) return true;
  if (src_str->IsOneByteRepresentation()) {
    return Cast<ExternalOneByteString>(src)->resource() != nullptr;
  } else if (src_str->IsTwoByteRepresentation()) {
    return Cast<ExternalTwoByteString>(src)->resource() != nullptr;
  }
  return true;
}

bool Script::has_line_ends() const { return line_ends() != Smi::zero(); }

bool Script::CanHaveLineEnds() const {
#if V8_ENABLE_WEBASSEMBLY
  return type() != Script::Type::kWasm;
#else
  return true;
#endif  // V8_ENABLE_WEBASSEMBLY
}

// static
void Script::InitLineEnds(Isolate* isolate, DirectHandle<Script> script) {
  if (script->has_line_ends()) return;
  Script::InitLineEndsInternal(isolate, script);
}
// static
void Script::InitLineEnds(LocalIsolate* isolate, DirectHandle<Script> script) {
  if (script->has_line_ends()) return;
  Script::InitLineEndsInternal(isolate, script);
}

bool Script::HasSourceURLComment() const {
  return IsString(source_url()) && Cast<String>(source_url())->length() != 0;
}

bool Script::IsMaybeUnfinalized(Isolate* isolate) const {
  // TODO(v8:12051): A more robust detection, e.g. with a dedicated sentinel
  // value.
  return IsUndefined(source(), isolate) ||
         Cast<String>(source())->length() == 0;
}

Tagged<Script> Script::GetEvalOrigin() {
  DisallowGarbageCollection no_gc;
  Tagged<Script> origin_script = *this;
  while (origin_script->has_eval_from_shared()) {
    Tagged<HeapObject> maybe_script =
        origin_script->eval_from_shared()->script();
    CHECK(IsScript(maybe_script));
    origin_script = Cast<Script>(maybe_script);
  }
  return origin_script;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SCRIPT_INL_H_
