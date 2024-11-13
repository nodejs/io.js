// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CONTEXTS_INL_H_
#define V8_OBJECTS_CONTEXTS_INL_H_

#include "src/common/globals.h"
#include "src/heap/heap-write-barrier.h"
#include "src/objects/casting.h"
#include "src/objects/contexts.h"
#include "src/objects/dictionary-inl.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/map-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/ordered-hash-table-inl.h"
#include "src/objects/regexp-match-info.h"
#include "src/objects/scope-info.h"
#include "src/objects/shared-function-info.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/contexts-tq-inl.inc"

OBJECT_CONSTRUCTORS_IMPL(ScriptContextTable, ScriptContextTable::Super)

RELEASE_ACQUIRE_SMI_ACCESSORS(ScriptContextTable, length, kLengthOffset)
ACCESSORS(ScriptContextTable, names_to_context_index,
          Tagged<NameToIndexHashTable>, kNamesToContextIndexOffset)

Tagged<Context> ScriptContextTable::get(int i) const {
  DCHECK_LT(i, length(kAcquireLoad));
  return Super::get(i);
}

Tagged<Context> ScriptContextTable::get(int i, AcquireLoadTag tag) const {
  DCHECK_LT(i, length(tag));
  return Super::get(i, tag);
}

TQ_OBJECT_CONSTRUCTORS_IMPL(Context)
NEVER_READ_ONLY_SPACE_IMPL(Context)

RELAXED_SMI_ACCESSORS(Context, length, kLengthOffset)

Tagged<Object> Context::get(int index) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return get(cage_base, index);
}

Tagged<Object> Context::get(PtrComprCageBase cage_base, int index) const {
  DCHECK_LT(static_cast<unsigned int>(index),
            static_cast<unsigned int>(length(kRelaxedLoad)));
  return TaggedField<Object>::Relaxed_Load(cage_base, *this,
                                           OffsetOfElementAt(index));
}

void Context::set(int index, Tagged<Object> value, WriteBarrierMode mode) {
  DCHECK_LT(static_cast<unsigned int>(index),
            static_cast<unsigned int>(length(kRelaxedLoad)));
  const int offset = OffsetOfElementAt(index);
  RELAXED_WRITE_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);
}

Tagged<Object> Context::get(int index, AcquireLoadTag tag) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return get(cage_base, index, tag);
}

Tagged<Object> Context::get(PtrComprCageBase cage_base, int index,
                            AcquireLoadTag) const {
  DCHECK_LT(static_cast<unsigned int>(index),
            static_cast<unsigned int>(length(kRelaxedLoad)));
  return ACQUIRE_READ_FIELD(*this, OffsetOfElementAt(index));
}

void Context::set(int index, Tagged<Object> value, WriteBarrierMode mode,
                  ReleaseStoreTag) {
  DCHECK_LT(static_cast<unsigned int>(index),
            static_cast<unsigned int>(length(kRelaxedLoad)));
  const int offset = OffsetOfElementAt(index);
  RELEASE_WRITE_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);
}

void NativeContext::set(int index, Tagged<Object> value, WriteBarrierMode mode,
                        ReleaseStoreTag tag) {
  Context::set(index, value, mode, tag);
}

ACCESSORS(Context, scope_info, Tagged<ScopeInfo>, kScopeInfoOffset)

Tagged<Object> Context::unchecked_previous() const {
  return get(PREVIOUS_INDEX);
}

Tagged<Context> Context::previous() const {
  Tagged<Object> result = get(PREVIOUS_INDEX);
  DCHECK(IsBootstrappingOrValidParentContext(result, *this));
  return UncheckedCast<Context>(result);
}
void Context::set_previous(Tagged<Context> context, WriteBarrierMode mode) {
  set(PREVIOUS_INDEX, context, mode);
}

Tagged<Object> Context::next_context_link() const {
  return get(Context::NEXT_CONTEXT_LINK);
}

bool Context::has_extension() const {
  return scope_info()->HasContextExtensionSlot() && !IsUndefined(extension());
}

Tagged<HeapObject> Context::extension() const {
  DCHECK(scope_info()->HasContextExtensionSlot());
  return Cast<HeapObject>(get(EXTENSION_INDEX));
}

Tagged<NativeContext> Context::native_context() const {
  return this->map()->native_context();
}

bool Context::IsFunctionContext() const {
  return map()->instance_type() == FUNCTION_CONTEXT_TYPE;
}

bool Context::IsCatchContext() const {
  return map()->instance_type() == CATCH_CONTEXT_TYPE;
}

bool Context::IsWithContext() const {
  return map()->instance_type() == WITH_CONTEXT_TYPE;
}

bool Context::IsDebugEvaluateContext() const {
  return map()->instance_type() == DEBUG_EVALUATE_CONTEXT_TYPE;
}

bool Context::IsAwaitContext() const {
  return map()->instance_type() == AWAIT_CONTEXT_TYPE;
}

bool Context::IsBlockContext() const {
  return map()->instance_type() == BLOCK_CONTEXT_TYPE;
}

bool Context::IsModuleContext() const {
  return map()->instance_type() == MODULE_CONTEXT_TYPE;
}

bool Context::IsEvalContext() const {
  return map()->instance_type() == EVAL_CONTEXT_TYPE;
}

bool Context::IsScriptContext() const {
  return map()->instance_type() == SCRIPT_CONTEXT_TYPE;
}

bool Context::HasSameSecurityTokenAs(Tagged<Context> that) const {
  return this->native_context()->security_token() ==
         that->native_context()->security_token();
}

bool Context::IsDetached() const { return global_object()->IsDetached(); }

#define NATIVE_CONTEXT_FIELD_ACCESSORS(index, type, name)         \
  void Context::set_##name(Tagged<UNPAREN(type)> value) {         \
    DCHECK(IsNativeContext(*this));                               \
    set(index, value, UPDATE_WRITE_BARRIER, kReleaseStore);       \
  }                                                               \
  bool Context::is_##name(Tagged<UNPAREN(type)> value) const {    \
    DCHECK(IsNativeContext(*this));                               \
    return Cast<UNPAREN(type)>(get(index)) == value;              \
  }                                                               \
  Tagged<UNPAREN(type)> Context::name() const {                   \
    DCHECK(IsNativeContext(*this));                               \
    return Cast<UNPAREN(type)>(get(index));                       \
  }                                                               \
  Tagged<UNPAREN(type)> Context::name(AcquireLoadTag tag) const { \
    DCHECK(IsNativeContext(*this));                               \
    return Cast<UNPAREN(type)>(get(index, tag));                  \
  }
NATIVE_CONTEXT_FIELDS(NATIVE_CONTEXT_FIELD_ACCESSORS)
#undef NATIVE_CONTEXT_FIELD_ACCESSORS

#define CHECK_FOLLOWS2(v1, v2) static_assert((v1 + 1) == (v2))
#define CHECK_FOLLOWS4(v1, v2, v3, v4) \
  CHECK_FOLLOWS2(v1, v2);              \
  CHECK_FOLLOWS2(v2, v3);              \
  CHECK_FOLLOWS2(v3, v4)

int Context::FunctionMapIndex(LanguageMode language_mode, FunctionKind kind,
                              bool has_shared_name) {
  if (IsClassConstructor(kind)) {
    // Like the strict function map, but with no 'name' accessor. 'name'
    // needs to be the last property and it is added during instantiation,
    // in case a static property with the same name exists"
    return CLASS_FUNCTION_MAP_INDEX;
  }

  int base = 0;
  if (IsGeneratorFunction(kind)) {
    CHECK_FOLLOWS2(GENERATOR_FUNCTION_MAP_INDEX,
                   GENERATOR_FUNCTION_WITH_NAME_MAP_INDEX);
    CHECK_FOLLOWS2(ASYNC_GENERATOR_FUNCTION_MAP_INDEX,
                   ASYNC_GENERATOR_FUNCTION_WITH_NAME_MAP_INDEX);

    base = IsAsyncFunction(kind) ? ASYNC_GENERATOR_FUNCTION_MAP_INDEX
                                 : GENERATOR_FUNCTION_MAP_INDEX;

  } else if (IsAsyncFunction(kind) || IsModuleWithTopLevelAwait(kind)) {
    CHECK_FOLLOWS2(ASYNC_FUNCTION_MAP_INDEX,
                   ASYNC_FUNCTION_WITH_NAME_MAP_INDEX);

    base = ASYNC_FUNCTION_MAP_INDEX;

  } else if (IsStrictFunctionWithoutPrototype(kind)) {
    CHECK_FOLLOWS2(STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX,
                   METHOD_WITH_NAME_MAP_INDEX);

    base = STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX;

  } else {
    CHECK_FOLLOWS2(SLOPPY_FUNCTION_MAP_INDEX,
                   SLOPPY_FUNCTION_WITH_NAME_MAP_INDEX);
    CHECK_FOLLOWS2(STRICT_FUNCTION_MAP_INDEX,
                   STRICT_FUNCTION_WITH_NAME_MAP_INDEX);

    base = is_strict(language_mode) ? STRICT_FUNCTION_MAP_INDEX
                                    : SLOPPY_FUNCTION_MAP_INDEX;
  }
  int offset = static_cast<int>(!has_shared_name);
  DCHECK_EQ(0, offset & ~1);

  return base + offset;
}

#undef CHECK_FOLLOWS2
#undef CHECK_FOLLOWS4

Tagged<Map> Context::GetInitialJSArrayMap(ElementsKind kind) const {
  DCHECK(IsNativeContext(*this));
  if (!IsFastElementsKind(kind)) return Map();
  DisallowGarbageCollection no_gc;
  Tagged<Object> const initial_js_array_map = get(Context::ArrayMapIndex(kind));
  DCHECK(!IsUndefined(initial_js_array_map));
  return Cast<Map>(initial_js_array_map);
}

EXTERNAL_POINTER_ACCESSORS(NativeContext, microtask_queue, MicrotaskQueue*,
                           kMicrotaskQueueOffset,
                           kNativeContextMicrotaskQueueTag)

void NativeContext::synchronized_set_script_context_table(
    Tagged<ScriptContextTable> script_context_table) {
  set(SCRIPT_CONTEXT_TABLE_INDEX, script_context_table, UPDATE_WRITE_BARRIER,
      kReleaseStore);
}

Tagged<ScriptContextTable> NativeContext::synchronized_script_context_table()
    const {
  return Cast<ScriptContextTable>(
      get(SCRIPT_CONTEXT_TABLE_INDEX, kAcquireLoad));
}

Tagged<Map> NativeContext::TypedArrayElementsKindToCtorMap(
    ElementsKind element_kind) const {
  int ctor_index = Context::FIRST_FIXED_TYPED_ARRAY_FUN_INDEX + element_kind -
                   ElementsKind::FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND;
  Tagged<Map> map = Cast<Map>(Cast<JSFunction>(get(ctor_index))->initial_map());
  DCHECK_EQ(map->elements_kind(), element_kind);
  DCHECK(InstanceTypeChecker::IsJSTypedArray(map));
  return map;
}

Tagged<Map> NativeContext::TypedArrayElementsKindToRabGsabCtorMap(
    ElementsKind element_kind) const {
  int ctor_index = Context::FIRST_RAB_GSAB_TYPED_ARRAY_MAP_INDEX +
                   element_kind -
                   ElementsKind::FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND;
  Tagged<Map> map = Cast<Map>(get(ctor_index));
  DCHECK_EQ(map->elements_kind(),
            GetCorrespondingRabGsabElementsKind(element_kind));
  DCHECK(InstanceTypeChecker::IsJSTypedArray(map));
  return map;
}

OBJECT_CONSTRUCTORS_IMPL(NativeContext, Context)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_CONTEXTS_INL_H_
