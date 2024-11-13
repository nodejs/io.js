// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/logging.h"
#include "src/codegen/assembler-inl.h"
#include "src/common/globals.h"
#include "src/date/date.h"
#include "src/diagnostics/disasm.h"
#include "src/diagnostics/disassembler.h"
#include "src/heap/combined-heap.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/init/bootstrapper.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/arguments-inl.h"
#include "src/objects/bigint.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/cell-inl.h"
#include "src/objects/code-inl.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/debug-objects-inl.h"
#include "src/objects/elements.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/embedder-data-slot-inl.h"
#include "src/objects/feedback-cell-inl.h"
#include "src/objects/field-type.h"
#include "src/objects/foreign-inl.h"
#include "src/objects/free-space-inl.h"
#include "src/objects/function-kind.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "src/objects/js-disposable-stack.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/objects/trusted-object.h"
#include "src/objects/turbofan-types-inl.h"
#include "src/objects/turboshaft-types-inl.h"
#include "src/roots/roots.h"
#ifdef V8_INTL_SUPPORT
#include "src/objects/js-break-iterator-inl.h"
#include "src/objects/js-collator-inl.h"
#endif  // V8_INTL_SUPPORT
#include "src/objects/js-collection-inl.h"
#ifdef V8_INTL_SUPPORT
#include "src/objects/js-date-time-format-inl.h"
#include "src/objects/js-display-names-inl.h"
#include "src/objects/js-duration-format-inl.h"
#endif  // V8_INTL_SUPPORT
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/js-iterator-helpers-inl.h"
#ifdef V8_INTL_SUPPORT
#include "src/objects/js-list-format-inl.h"
#include "src/objects/js-locale-inl.h"
#include "src/objects/js-number-format-inl.h"
#include "src/objects/js-plural-rules-inl.h"
#endif  // V8_INTL_SUPPORT
#include "src/objects/js-regexp-inl.h"
#include "src/objects/js-regexp-string-iterator-inl.h"
#include "src/objects/js-shadow-realm-inl.h"
#ifdef V8_INTL_SUPPORT
#include "src/objects/js-relative-time-format-inl.h"
#include "src/objects/js-segment-iterator-inl.h"
#include "src/objects/js-segmenter-inl.h"
#include "src/objects/js-segments-inl.h"
#endif  // V8_INTL_SUPPORT
#include "src/objects/hole-inl.h"
#include "src/objects/js-raw-json-inl.h"
#include "src/objects/js-shared-array-inl.h"
#include "src/objects/js-struct-inl.h"
#include "src/objects/js-temporal-objects-inl.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/maybe-object.h"
#include "src/objects/megadom-handler-inl.h"
#include "src/objects/microtask-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/oddball-inl.h"
#include "src/objects/promise-inl.h"
#include "src/objects/property-descriptor-object-inl.h"
#include "src/objects/struct-inl.h"
#include "src/objects/swiss-name-dictionary-inl.h"
#include "src/objects/synthetic-module-inl.h"
#include "src/objects/template-objects-inl.h"
#include "src/objects/torque-defined-classes-inl.h"
#include "src/objects/transitions-inl.h"
#include "src/regexp/regexp.h"
#include "src/sandbox/js-dispatch-table-inl.h"
#include "src/utils/ostreams.h"
#include "torque-generated/class-verifiers.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/base/strings.h"
#include "src/debug/debug-wasm-objects-inl.h"
#include "src/wasm/wasm-objects-inl.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

// Heap Verification Overview
// --------------------------
// - Each InstanceType has a separate XXXVerify method which checks an object's
//   integrity in isolation.
// - --verify-heap will iterate over all gc spaces and call ObjectVerify() on
//   every encountered tagged pointer.
// - Verification should be pushed down to the specific instance type if its
//   integrity is independent of an outer object.
// - In cases where the InstanceType is too generic (e.g. FixedArray) the
//   XXXVerify of the outer method has to do recursive verification.
// - If the corresponding objects have inheritence the parent's Verify method
//   is called as well.
// - For any field containing pointes VerifyPointer(...) should be called.
//
// Caveats
// -------
// - Assume that any of the verify methods is incomplete!
// - Some integrity checks are only partially done due to objects being in
//   partially initialized states when a gc happens, for instance when outer
//   objects are allocted before inner ones.
//

#ifdef VERIFY_HEAP

#define USE_TORQUE_VERIFIER(Class)                                \
  void Class::Class##Verify(Isolate* isolate) {                   \
    TorqueGeneratedClassVerifiers::Class##Verify(*this, isolate); \
  }

// static
void Object::ObjectVerify(Tagged<Object> obj, Isolate* isolate) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kObjectVerify);
  if (IsSmi(obj)) {
    Smi::SmiVerify(Cast<Smi>(obj), isolate);
  } else {
    Cast<HeapObject>(obj)->HeapObjectVerify(isolate);
  }
  PtrComprCageBase cage_base(isolate);
  CHECK(!IsConstructor(obj, cage_base) || IsCallable(obj, cage_base));
}

void Object::VerifyPointer(Isolate* isolate, Tagged<Object> p) {
  if (IsHeapObject(p)) {
    HeapObject::VerifyHeapPointer(isolate, p);
  } else {
    CHECK(IsSmi(p));
  }
}

void Object::VerifyAnyTagged(Isolate* isolate, Tagged<Object> p) {
  if (IsHeapObject(p)) {
    if (V8_EXTERNAL_CODE_SPACE_BOOL) {
      CHECK(IsValidHeapObject(isolate->heap(), Cast<HeapObject>(p)));
    } else {
      HeapObject::VerifyHeapPointer(isolate, p);
    }
  } else {
    CHECK(IsSmi(p));
  }
}

void Object::VerifyMaybeObjectPointer(Isolate* isolate, Tagged<MaybeObject> p) {
  Tagged<HeapObject> heap_object;
  if (p.GetHeapObject(&heap_object)) {
    HeapObject::VerifyHeapPointer(isolate, heap_object);
  } else {
    CHECK(p.IsSmi() || p.IsCleared() || MapWord::IsPacked(p.ptr()));
  }
}

// static
void Smi::SmiVerify(Tagged<Smi> obj, Isolate* isolate) {
  CHECK(IsSmi(obj));
  CHECK(!IsCallable(obj));
  CHECK(!IsConstructor(obj));
}

// static
void TaggedIndex::TaggedIndexVerify(Tagged<TaggedIndex> obj, Isolate* isolate) {
  CHECK(IsTaggedIndex(obj));
}

void HeapObject::HeapObjectVerify(Isolate* isolate) {
  CHECK(IsHeapObject(*this));
  PtrComprCageBase cage_base(isolate);
  Object::VerifyPointer(isolate, map(cage_base));
  CHECK(IsMap(map(cage_base), cage_base));

  CHECK(CheckRequiredAlignment(isolate));

  // Only TrustedObjects live in trusted space. See also TrustedObjectVerify.
  CHECK_IMPLIES(!IsTrustedObject(*this) && !IsFreeSpaceOrFiller(*this),
                !IsTrustedSpaceObject(*this));

  switch (map(cage_base)->instance_type()) {
#define STRING_TYPE_CASE(TYPE, size, name, CamelName) case TYPE:
    STRING_TYPE_LIST(STRING_TYPE_CASE)
#undef STRING_TYPE_CASE
    if (IsConsString(*this, cage_base)) {
      Cast<ConsString>(*this)->ConsStringVerify(isolate);
    } else if (IsSlicedString(*this, cage_base)) {
      Cast<SlicedString>(*this)->SlicedStringVerify(isolate);
    } else if (IsThinString(*this, cage_base)) {
      Cast<ThinString>(*this)->ThinStringVerify(isolate);
    } else if (IsSeqString(*this, cage_base)) {
      Cast<SeqString>(*this)->SeqStringVerify(isolate);
    } else if (IsExternalString(*this, cage_base)) {
      Cast<ExternalString>(*this)->ExternalStringVerify(isolate);
    } else {
      Cast<String>(*this)->StringVerify(isolate);
    }
    break;
    // FixedArray types
    case HASH_TABLE_TYPE:
    case ORDERED_HASH_MAP_TYPE:
    case ORDERED_HASH_SET_TYPE:
    case ORDERED_NAME_DICTIONARY_TYPE:
    case NAME_TO_INDEX_HASH_TABLE_TYPE:
    case REGISTERED_SYMBOL_TABLE_TYPE:
    case NAME_DICTIONARY_TYPE:
    case GLOBAL_DICTIONARY_TYPE:
    case NUMBER_DICTIONARY_TYPE:
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
    case EPHEMERON_HASH_TABLE_TYPE:
      Cast<FixedArray>(*this)->FixedArrayVerify(isolate);
      break;
    case AWAIT_CONTEXT_TYPE:
    case BLOCK_CONTEXT_TYPE:
    case CATCH_CONTEXT_TYPE:
    case DEBUG_EVALUATE_CONTEXT_TYPE:
    case EVAL_CONTEXT_TYPE:
    case FUNCTION_CONTEXT_TYPE:
    case MODULE_CONTEXT_TYPE:
    case SCRIPT_CONTEXT_TYPE:
    case WITH_CONTEXT_TYPE:
      Cast<Context>(*this)->ContextVerify(isolate);
      break;
    case NATIVE_CONTEXT_TYPE:
      Cast<NativeContext>(*this)->NativeContextVerify(isolate);
      break;
    case FEEDBACK_METADATA_TYPE:
      Cast<FeedbackMetadata>(*this)->FeedbackMetadataVerify(isolate);
      break;
    case TRANSITION_ARRAY_TYPE:
      Cast<TransitionArray>(*this)->TransitionArrayVerify(isolate);
      break;

    case INSTRUCTION_STREAM_TYPE:
      Cast<InstructionStream>(*this)->InstructionStreamVerify(isolate);
      break;
    case JS_API_OBJECT_TYPE:
    case JS_ARRAY_ITERATOR_PROTOTYPE_TYPE:
    case JS_CONTEXT_EXTENSION_OBJECT_TYPE:
    case JS_ERROR_TYPE:
    case JS_ITERATOR_PROTOTYPE_TYPE:
    case JS_MAP_ITERATOR_PROTOTYPE_TYPE:
    case JS_OBJECT_PROTOTYPE_TYPE:
    case JS_PROMISE_PROTOTYPE_TYPE:
    case JS_REG_EXP_PROTOTYPE_TYPE:
    case JS_SET_ITERATOR_PROTOTYPE_TYPE:
    case JS_SET_PROTOTYPE_TYPE:
    case JS_SPECIAL_API_OBJECT_TYPE:
    case JS_STRING_ITERATOR_PROTOTYPE_TYPE:
    case JS_TYPED_ARRAY_PROTOTYPE_TYPE:
      Cast<JSObject>(*this)->JSObjectVerify(isolate);
      break;
#if V8_ENABLE_WEBASSEMBLY
    case WASM_TRUSTED_INSTANCE_DATA_TYPE:
      Cast<WasmTrustedInstanceData>(*this)->WasmTrustedInstanceDataVerify(
          isolate);
      break;
    case WASM_DISPATCH_TABLE_TYPE:
      Cast<WasmDispatchTable>(*this)->WasmDispatchTableVerify(isolate);
      break;
    case WASM_VALUE_OBJECT_TYPE:
      Cast<WasmValueObject>(*this)->WasmValueObjectVerify(isolate);
      break;
    case WASM_EXCEPTION_PACKAGE_TYPE:
      Cast<WasmExceptionPackage>(*this)->WasmExceptionPackageVerify(isolate);
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case JS_SET_KEY_VALUE_ITERATOR_TYPE:
    case JS_SET_VALUE_ITERATOR_TYPE:
      Cast<JSSetIterator>(*this)->JSSetIteratorVerify(isolate);
      break;
    case JS_MAP_KEY_ITERATOR_TYPE:
    case JS_MAP_KEY_VALUE_ITERATOR_TYPE:
    case JS_MAP_VALUE_ITERATOR_TYPE:
      Cast<JSMapIterator>(*this)->JSMapIteratorVerify(isolate);
      break;
    case FILLER_TYPE:
      break;
    case CODE_TYPE:
      Cast<Code>(*this)->CodeVerify(isolate);
      break;
    case CODE_WRAPPER_TYPE:
      Cast<CodeWrapper>(*this)->CodeWrapperVerify(isolate);
      break;

#define MAKE_TORQUE_CASE(Name, TYPE)          \
  case TYPE:                                  \
    Cast<Name>(*this)->Name##Verify(isolate); \
    break;
      // Every class that has its fields defined in a .tq file and corresponds
      // to exactly one InstanceType value is included in the following list.
      TORQUE_INSTANCE_CHECKERS_SINGLE_FULLY_DEFINED(MAKE_TORQUE_CASE)
      TORQUE_INSTANCE_CHECKERS_MULTIPLE_FULLY_DEFINED(MAKE_TORQUE_CASE)
#undef MAKE_TORQUE_CASE

    case ALLOCATION_SITE_TYPE:
      Cast<AllocationSite>(*this)->AllocationSiteVerify(isolate);
      break;

    case LOAD_HANDLER_TYPE:
      Cast<LoadHandler>(*this)->LoadHandlerVerify(isolate);
      break;

    case STORE_HANDLER_TYPE:
      Cast<StoreHandler>(*this)->StoreHandlerVerify(isolate);
      break;

    case BIG_INT_BASE_TYPE:
      Cast<BigIntBase>(*this)->BigIntBaseVerify(isolate);
      break;

    case JS_CLASS_CONSTRUCTOR_TYPE:
    case JS_PROMISE_CONSTRUCTOR_TYPE:
    case JS_REG_EXP_CONSTRUCTOR_TYPE:
    case JS_ARRAY_CONSTRUCTOR_TYPE:
#define TYPED_ARRAY_CONSTRUCTORS_SWITCH(Type, type, TYPE, Ctype) \
  case TYPE##_TYPED_ARRAY_CONSTRUCTOR_TYPE:
      TYPED_ARRAYS(TYPED_ARRAY_CONSTRUCTORS_SWITCH)
#undef TYPED_ARRAY_CONSTRUCTORS_SWITCH
      Cast<JSFunction>(*this)->JSFunctionVerify(isolate);
      break;
    case JS_LAST_DUMMY_API_OBJECT_TYPE:
      UNREACHABLE();
  }
}

// static
void HeapObject::VerifyHeapPointer(Isolate* isolate, Tagged<Object> p) {
  CHECK(IsHeapObject(p));
  // If you crashed here and {isolate->is_shared()}, there is a bug causing the
  // host of {p} to point to a non-shared object.
  CHECK(IsValidHeapObject(isolate->heap(), Cast<HeapObject>(p)));
  CHECK_IMPLIES(V8_EXTERNAL_CODE_SPACE_BOOL, !IsInstructionStream(p));
}

// static
void HeapObject::VerifyCodePointer(Isolate* isolate, Tagged<Object> p) {
  CHECK(IsHeapObject(p));
  CHECK(IsValidCodeObject(isolate->heap(), Cast<HeapObject>(p)));
  PtrComprCageBase cage_base(isolate);
  CHECK(IsInstructionStream(Cast<HeapObject>(p), cage_base));
}

void Name::NameVerify(Isolate* isolate) {
  PrimitiveHeapObjectVerify(isolate);
  CHECK(IsName(this));
}

void Symbol::SymbolVerify(Isolate* isolate) {
  NameVerify(isolate);
  CHECK(IsSymbol(this));
  uint32_t hash;
  const bool has_hash = TryGetHash(&hash);
  CHECK(has_hash);
  CHECK_GT(hash, 0);
  CHECK(IsUndefined(description(), isolate) || IsString(description()));
  CHECK_IMPLIES(IsPrivateName(), IsPrivate());
  CHECK_IMPLIES(IsPrivateBrand(), IsPrivateName());
}

void BytecodeArray::BytecodeArrayVerify(Isolate* isolate) {
  ExposedTrustedObjectVerify(isolate);

  {
    CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));
    CHECK_LE(0, length());
    CHECK_LE(length(), kMaxLength);
  }
  {
    auto o = constant_pool();
    Object::VerifyPointer(isolate, o);
    CHECK(IsTrustedFixedArray(o));
  }
  {
    auto o = handler_table();
    Object::VerifyPointer(isolate, o);
    CHECK(IsTrustedByteArray(o));
  }
  {
    auto o = wrapper();
    Object::VerifyPointer(isolate, o);
    CHECK(IsBytecodeWrapper(o));
    // Our wrapper must point back to us.
    CHECK_EQ(o->bytecode(isolate), *this);
  }
  {
    // Use the raw accessor here as source positions may not be available.
    auto o = raw_source_position_table(kAcquireLoad);
    Object::VerifyPointer(isolate, o);
    CHECK(o == Smi::zero() || IsTrustedByteArray(o));
  }

  for (int i = 0; i < constant_pool()->length(); ++i) {
    // No ThinStrings in the constant pool.
    CHECK(!IsThinString(constant_pool()->get(i), isolate));
  }

  // TODO(oth): Walk bytecodes and immediate values to validate sanity.
  // - All bytecodes are known and well formed.
  // - Jumps must go to new instructions starts.
  // - No Illegal bytecodes.
  // - No consecutive sequences of prefix Wide / ExtraWide.
}

void BytecodeWrapper::BytecodeWrapperVerify(Isolate* isolate) {
  if (!this->has_bytecode()) return;
  auto bytecode = this->bytecode(isolate);
  Object::VerifyPointer(isolate, bytecode);
  CHECK_EQ(bytecode->wrapper(), *this);
}

bool JSObject::ElementsAreSafeToExamine(PtrComprCageBase cage_base) const {
  // If a GC was caused while constructing this object, the elements
  // pointer may point to a one pointer filler map.
  return elements(cage_base) !=
         GetReadOnlyRoots(cage_base).one_pointer_filler_map();
}

namespace {

void VerifyJSObjectElements(Isolate* isolate, Tagged<JSObject> object) {
  // Only TypedArrays can have these specialized elements.
  if (IsJSTypedArray(object)) {
    // TODO(bmeurer,v8:4153): Fix CreateTypedArray to either not instantiate
    // the object or propertly initialize it on errors during construction.
    /* CHECK(object->HasTypedArrayOrRabGsabTypedArrayElements()); */
    return;
  }
  CHECK(!IsByteArray(object->elements()));

  if (object->HasDoubleElements()) {
    if (object->elements()->length() > 0) {
      CHECK(IsFixedDoubleArray(object->elements()));
    }
    return;
  }

  if (object->HasSloppyArgumentsElements()) {
    CHECK(IsSloppyArgumentsElements(object->elements()));
    return;
  }

  Tagged<FixedArray> elements = Cast<FixedArray>(object->elements());
  if (object->HasSmiElements()) {
    // We might have a partially initialized backing store, in which case we
    // allow the hole + smi values.
    for (int i = 0; i < elements->length(); i++) {
      Tagged<Object> value = elements->get(i);
      CHECK(IsSmi(value) || IsTheHole(value, isolate));
    }
  } else if (object->HasObjectElements()) {
    for (int i = 0; i < elements->length(); i++) {
      Tagged<Object> element = elements->get(i);
      CHECK(!HasWeakHeapObjectTag(element));
    }
  }
}
}  // namespace

void JSObject::JSObjectVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSObjectVerify(*this, isolate);
  VerifyHeapPointer(isolate, elements());

  CHECK_IMPLIES(HasSloppyArgumentsElements(), IsJSArgumentsObject(*this));
  if (HasFastProperties()) {
    int actual_unused_property_fields = map()->GetInObjectProperties() +
                                        property_array()->length() -
                                        map()->NextFreePropertyIndex();
    if (map()->UnusedPropertyFields() != actual_unused_property_fields) {
      // There are two reasons why this can happen:
      // - in the middle of StoreTransitionStub when the new extended backing
      //   store is already set into the object and the allocation of the
      //   HeapNumber triggers GC while the map isn't updated yet.
      // - deletion of the last property can leave additional backing store
      //   capacity behind.
      CHECK_GT(actual_unused_property_fields, map()->UnusedPropertyFields());
      int delta = actual_unused_property_fields - map()->UnusedPropertyFields();
      CHECK_EQ(0, delta % JSObject::kFieldsAdded);
    }
    Tagged<DescriptorArray> descriptors = map()->instance_descriptors(isolate);
    bool is_transitionable_fast_elements_kind =
        IsTransitionableFastElementsKind(map()->elements_kind());

    for (InternalIndex i : map()->IterateOwnDescriptors()) {
      PropertyDetails details = descriptors->GetDetails(i);
      if (details.location() == PropertyLocation::kField) {
        DCHECK_EQ(PropertyKind::kData, details.kind());
        Representation r = details.representation();
        FieldIndex index = FieldIndex::ForDetails(map(), details);
        if (COMPRESS_POINTERS_BOOL && index.is_inobject()) {
          VerifyObjectField(isolate, index.offset());
        }
        Tagged<Object> value = RawFastPropertyAt(index);
        if (r.IsDouble()) DCHECK(IsHeapNumber(value));
        if (IsUninitialized(value, isolate)) continue;
        if (r.IsSmi()) DCHECK(IsSmi(value));
        if (r.IsHeapObject()) DCHECK(IsHeapObject(value));
        Tagged<FieldType> field_type = descriptors->GetFieldType(i);
        bool type_is_none = IsNone(field_type);
        bool type_is_any = IsAny(field_type);
        if (r.IsNone()) {
          CHECK(type_is_none);
        } else if (r.IsHeapObject()) {
          CHECK(!type_is_none);
          if (!type_is_any) {
            CHECK_IMPLIES(FieldType::NowStable(field_type),
                          map()->is_deprecated() ||
                              FieldType::NowContains(field_type, value));
          }
        } else {
          CHECK(type_is_any);
        }
        CHECK_IMPLIES(is_transitionable_fast_elements_kind,
                      Map::IsMostGeneralFieldType(r, field_type));
      }
    }

    if (map()->EnumLength() != kInvalidEnumCacheSentinel) {
      Tagged<EnumCache> enum_cache = descriptors->enum_cache();
      Tagged<FixedArray> keys = enum_cache->keys();
      Tagged<FixedArray> indices = enum_cache->indices();
      CHECK_LE(map()->EnumLength(), keys->length());
      CHECK_IMPLIES(indices != ReadOnlyRoots(isolate).empty_fixed_array(),
                    keys->length() == indices->length());
    }
  }

  // If a GC was caused while constructing this object, the elements
  // pointer may point to a one pointer filler map.
  if (ElementsAreSafeToExamine(isolate)) {
    CHECK_EQ((map()->has_fast_smi_or_object_elements() ||
              map()->has_any_nonextensible_elements() ||
              (elements() == GetReadOnlyRoots().empty_fixed_array()) ||
              HasFastStringWrapperElements()),
             (elements()->map() == GetReadOnlyRoots().fixed_array_map() ||
              elements()->map() == GetReadOnlyRoots().fixed_cow_array_map()));
    CHECK_EQ(map()->has_fast_object_elements(), HasObjectElements());
    VerifyJSObjectElements(isolate, *this);
  }
}

void Map::MapVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::MapVerify(*this, isolate);
  Heap* heap = isolate->heap();
  CHECK(!ObjectInYoungGeneration(*this));
  CHECK(FIRST_TYPE <= instance_type() && instance_type() <= LAST_TYPE);
  CHECK(instance_size() == kVariableSizeSentinel ||
        (kTaggedSize <= instance_size() &&
         static_cast<size_t>(instance_size()) < heap->Capacity()));
  if (IsContextMap(*this)) {
    // The map for the NativeContext is allocated before the NativeContext
    // itself, so it may happen that during a GC the native_context() is still
    // null.
    CHECK(IsNull(native_context_or_null()) ||
          IsNativeContext(native_context_or_null()));
    // The context's meta map is tied to the same native context.
    CHECK_EQ(native_context_or_null(), map()->native_context_or_null());
  } else {
    if (IsUndefined(GetBackPointer(), isolate)) {
      // Root maps must not have descriptors in the descriptor array that do not
      // belong to the map.
      CHECK_EQ(NumberOfOwnDescriptors(),
               instance_descriptors(isolate)->number_of_descriptors());
    } else {
      // If there is a parent map it must be non-stable.
      Tagged<Map> parent = Cast<Map>(GetBackPointer());
      CHECK(!parent->is_stable());
      Tagged<DescriptorArray> descriptors = instance_descriptors(isolate);
      if (!is_deprecated() && !parent->is_deprecated()) {
        CHECK_EQ(IsInobjectSlackTrackingInProgress(),
                 parent->IsInobjectSlackTrackingInProgress());
      }
      if (descriptors == parent->instance_descriptors(isolate)) {
        if (NumberOfOwnDescriptors() == parent->NumberOfOwnDescriptors() + 1) {
          // Descriptors sharing through property transitions takes over
          // ownership from the parent map.
          CHECK(!parent->owns_descriptors());
        } else {
          CHECK_EQ(NumberOfOwnDescriptors(), parent->NumberOfOwnDescriptors());
          // Descriptors sharing through special transitions properly takes over
          // ownership from the parent map unless it uses the canonical empty
          // descriptor array.
          if (descriptors != ReadOnlyRoots(isolate).empty_descriptor_array()) {
            CHECK_IMPLIES(owns_descriptors(), !parent->owns_descriptors());
            CHECK_IMPLIES(parent->owns_descriptors(), !owns_descriptors());
          }
        }
      }
    }
  }
  SLOW_DCHECK(instance_descriptors(isolate)->IsSortedNoDuplicates());
  SLOW_DCHECK(TransitionsAccessor(isolate, *this).IsSortedNoDuplicates());
  SLOW_DCHECK(
      TransitionsAccessor(isolate, *this).IsConsistentWithBackPointers());
  // Only JSFunction maps have has_prototype_slot() bit set and constructible
  // JSFunction objects must have prototype slot.
  CHECK_IMPLIES(has_prototype_slot(), IsJSFunctionMap(*this));

  if (InstanceTypeChecker::IsNativeContextSpecific(instance_type())) {
    // Native context-specific objects must have their own contextful meta map
    // modulo the following exceptions.
    if (instance_type() == NATIVE_CONTEXT_TYPE ||
        instance_type() == JS_GLOBAL_PROXY_TYPE) {
      // 1) Diring creation of the NativeContext the native context field might
      //    be not be initialized yet.
      // 2) The same applies to the placeholder JSGlobalProxy object created by
      //    Factory::NewUninitializedJSGlobalProxy.
      CHECK(IsNull(map()->native_context_or_null()) ||
            IsNativeContext(map()->native_context_or_null()));

    } else if (instance_type() == JS_SPECIAL_API_OBJECT_TYPE) {
      // 3) Remote Api objects' maps have the RO meta map (and thus are not
      //    tied to any native context) while all the other Api objects are
      //    tied to a native context.
      CHECK_IMPLIES(map() != GetReadOnlyRoots().meta_map(),
                    IsNativeContext(map()->native_context_or_null()));

    } else {
      // For all the other objects native context specific objects the
      // native context field must already be initialized.
      CHECK(IsNativeContext(map()->native_context_or_null()));
    }
  } else if (InstanceTypeChecker::IsAlwaysSharedSpaceJSObject(
                 instance_type())) {
    // Shared objects' maps must use the RO meta map.
    CHECK_EQ(map(), GetReadOnlyRoots().meta_map());
  }

  if (IsJSObjectMap(*this)) {
    int header_end_offset = JSObject::GetHeaderSize(*this);
    int inobject_fields_start_offset = GetInObjectPropertyOffset(0);
    // Ensure that embedder fields are located exactly between header and
    // inobject properties.
    CHECK_EQ(header_end_offset, JSObject::GetEmbedderFieldsStartOffset(*this));
    CHECK_EQ(header_end_offset +
                 JSObject::GetEmbedderFieldCount(*this) * kEmbedderDataSlotSize,
             inobject_fields_start_offset);

    if (IsJSSharedStructMap(*this) || IsJSSharedArrayMap(*this) ||
        IsJSAtomicsMutex(*this) || IsJSAtomicsCondition(*this)) {
      if (COMPRESS_POINTERS_IN_MULTIPLE_CAGES_BOOL) {
        // TODO(v8:14089): Verify what should be checked in this configuration
        // and again merge with the else-branch below.
        // CHECK(InSharedHeap());
        CHECK(IsUndefined(GetBackPointer(), isolate));
        // Object maybe_cell = prototype_validity_cell(kRelaxedLoad);
        // if (maybe_cell.IsCell()) CHECK(maybe_cell.InSharedHeap());
        CHECK(!is_extensible());
        CHECK(!is_prototype_map());
        CHECK(OnlyHasSimpleProperties());
        // CHECK(instance_descriptors(isolate).InSharedHeap());
        if (IsJSSharedArrayMap(*this)) {
          CHECK(has_shared_array_elements());
        }
      } else {
        CHECK(InAnySharedSpace(*this));
        CHECK(IsUndefined(GetBackPointer(), isolate));
        Tagged<Object> maybe_cell = prototype_validity_cell(kRelaxedLoad);
        if (IsCell(maybe_cell)) CHECK(InAnySharedSpace(Cast<Cell>(maybe_cell)));
        CHECK(!is_extensible());
        CHECK(!is_prototype_map());
        CHECK(OnlyHasSimpleProperties());
        CHECK(InAnySharedSpace(instance_descriptors(isolate)));
        if (IsJSSharedArrayMap(*this)) {
          CHECK(has_shared_array_elements());
        }
      }
    }

    // Check constuctor value in JSFunction's maps.
    if (IsJSFunctionMap(*this) && !IsMap(constructor_or_back_pointer())) {
      Tagged<Object> maybe_constructor = constructor_or_back_pointer();
      // Constructor field might still contain a tuple if this map used to
      // have non-instance prototype earlier.
      CHECK_IMPLIES(has_non_instance_prototype(), IsTuple2(maybe_constructor));
      if (IsTuple2(maybe_constructor)) {
        Tagged<Tuple2> tuple = Cast<Tuple2>(maybe_constructor);
        // Unwrap the {constructor, non-instance_prototype} pair.
        maybe_constructor = tuple->value1();
        CHECK(!IsJSReceiver(tuple->value2()));
      }
      CHECK(IsJSFunction(maybe_constructor) ||
            IsFunctionTemplateInfo(maybe_constructor) ||
            // The above check might fail until empty function setup is done.
            IsUndefined(isolate->raw_native_context()->get(
                Context::EMPTY_FUNCTION_INDEX)));
    }
  }

  if (!may_have_interesting_properties()) {
    CHECK(!has_named_interceptor());
    CHECK(!is_dictionary_map());
    CHECK(!is_access_check_needed());
    Tagged<DescriptorArray> const descriptors = instance_descriptors(isolate);
    for (InternalIndex i : IterateOwnDescriptors()) {
      CHECK(!descriptors->GetKey(i)->IsInteresting(isolate));
    }
  }
  CHECK_IMPLIES(has_named_interceptor(), may_have_interesting_properties());
  CHECK_IMPLIES(is_dictionary_map(), may_have_interesting_properties());
  CHECK_IMPLIES(is_dictionary_map(), owns_descriptors());
  CHECK_IMPLIES(is_access_check_needed(), may_have_interesting_properties());
  CHECK_IMPLIES(
      IsJSObjectMap(*this) && !CanHaveFastTransitionableElementsKind(),
      IsDictionaryElementsKind(elements_kind()) ||
          IsTerminalElementsKind(elements_kind()) ||
          IsAnyHoleyNonextensibleElementsKind(elements_kind()) ||
          IsSharedArrayElementsKind(elements_kind()));
  CHECK_IMPLIES(is_deprecated(), !is_stable());
  if (is_prototype_map()) {
    CHECK(prototype_info() == Smi::zero() || IsPrototypeInfo(prototype_info()));
  }
}

void Map::DictionaryMapVerify(Isolate* isolate) {
  MapVerify(isolate);
  CHECK(is_dictionary_map());
  CHECK_EQ(kInvalidEnumCacheSentinel, EnumLength());
  CHECK_EQ(ReadOnlyRoots(isolate).empty_descriptor_array(),
           instance_descriptors(isolate));
  CHECK_EQ(0, UnusedPropertyFields());
  CHECK_EQ(Map::GetVisitorId(*this), visitor_id());
}

void EmbedderDataArray::EmbedderDataArrayVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::EmbedderDataArrayVerify(*this, isolate);
  EmbedderDataSlot start(*this, 0);
  EmbedderDataSlot end(*this, length());
  for (EmbedderDataSlot slot = start; slot < end; ++slot) {
    Tagged<Object> e = slot.load_tagged();
    Object::VerifyPointer(isolate, e);
  }
}

void FixedArrayBase::FixedArrayBaseVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));
}

void FixedArray::FixedArrayVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));

  for (int i = 0; i < length(); ++i) {
    Object::VerifyPointer(isolate, get(i));
  }

  if (*this == ReadOnlyRoots(isolate).empty_fixed_array()) {
    CHECK_EQ(length(), 0);
    CHECK_EQ(map(), ReadOnlyRoots(isolate).fixed_array_map());
  }
}

void TrustedFixedArray::TrustedFixedArrayVerify(Isolate* isolate) {
  TrustedObjectVerify(isolate);
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));

  for (int i = 0; i < length(); ++i) {
    Object::VerifyPointer(isolate, get(i));
  }
}

void ProtectedFixedArray::ProtectedFixedArrayVerify(Isolate* isolate) {
  TrustedObjectVerify(isolate);

  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));

  for (int i = 0; i < length(); ++i) {
    Tagged<Object> element = get(i);
    CHECK(IsSmi(element) || IsTrustedObject(element));
    Object::VerifyPointer(isolate, element);
  }
}

void RegExpMatchInfo::RegExpMatchInfoVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kCapacityOffset)));
  CHECK_GE(capacity(), kMinCapacity);
  CHECK_LE(capacity(), kMaxCapacity);
  CHECK_GE(number_of_capture_registers(), kMinCapacity);
  CHECK_LE(number_of_capture_registers(), capacity());
  CHECK(IsString(last_subject()));
  Object::VerifyPointer(isolate, last_input());
  for (int i = 0; i < capacity(); ++i) {
    CHECK(IsSmi(get(i)));
  }
}

void ClosureFeedbackCellArray::ClosureFeedbackCellArrayVerify(
    Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kCapacityOffset)));
  for (int i = 0; i < length(); ++i) {
    Object::VerifyPointer(isolate, get(i));
  }
}

void WeakFixedArray::WeakFixedArrayVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));
  for (int i = 0; i < length(); i++) {
    Object::VerifyMaybeObjectPointer(isolate, get(i));
  }
}

void TrustedWeakFixedArray::TrustedWeakFixedArrayVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));
  for (int i = 0; i < length(); i++) {
    Object::VerifyMaybeObjectPointer(isolate, get(i));
  }
}

void ScriptContextTable::ScriptContextTableVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kCapacityOffset)));
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));
  int len = length(kAcquireLoad);
  CHECK_LE(0, len);
  CHECK_LE(len, capacity());
  CHECK(IsNameToIndexHashTable(names_to_context_index()));
  for (int i = 0; i < len; ++i) {
    Tagged<Context> o = get(i);
    Object::VerifyPointer(isolate, o);
    CHECK(IsContext(o));
    CHECK(o->IsScriptContext());
  }
}

void ArrayList::ArrayListVerify(Isolate* isolate) {
  CHECK_LE(0, length());
  CHECK_LE(length(), capacity());
  CHECK_IMPLIES(capacity() == 0,
                *this == ReadOnlyRoots(isolate).empty_array_list());
  for (int i = 0; i < capacity(); ++i) {
    Object::VerifyPointer(isolate, get(i));
  }
}

void PropertyArray::PropertyArrayVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::PropertyArrayVerify(*this, isolate);
  if (length() == 0) {
    CHECK_EQ(*this, ReadOnlyRoots(isolate).empty_property_array());
    return;
  }
  // There are no empty PropertyArrays.
  CHECK_LT(0, length());
  for (int i = 0; i < length(); i++) {
    Tagged<Object> e = get(i);
    Object::VerifyPointer(isolate, e);
  }
}

void ByteArray::ByteArrayVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));
}

void TrustedByteArray::TrustedByteArrayVerify(Isolate* isolate) {
  TrustedObjectVerify(isolate);
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));
}

void ExternalPointerArray::ExternalPointerArrayVerify(Isolate* isolate) {
  FixedArrayBase::FixedArrayBaseVerify(isolate);
}

void FixedDoubleArray::FixedDoubleArrayVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kLengthOffset)));
  for (int i = 0; i < length(); i++) {
    if (!is_the_hole(i)) {
      uint64_t value = get_representation(i);
      uint64_t unexpected =
          base::bit_cast<uint64_t>(std::numeric_limits<double>::quiet_NaN()) &
          uint64_t{0x7FF8000000000000};
      // Create implementation specific sNaN by inverting relevant bit.
      unexpected ^= uint64_t{0x0008000000000000};
      CHECK((value & uint64_t{0x7FF8000000000000}) != unexpected ||
            (value & uint64_t{0x0007FFFFFFFFFFFF}) == uint64_t{0});
    }
  }
}

void Context::ContextVerify(Isolate* isolate) {
  if (has_extension()) VerifyExtensionSlot(extension());
  TorqueGeneratedClassVerifiers::ContextVerify(*this, isolate);
  for (int i = 0; i < length(); i++) {
    VerifyObjectField(isolate, OffsetOfElementAt(i));
  }
  if (IsScriptContext()) {
    Tagged<Object> side_data = get(CONST_TRACKING_LET_SIDE_DATA_INDEX);
    CHECK(IsFixedArray(side_data));
    Tagged<FixedArray> side_data_array = Cast<FixedArray>(side_data);
    if (v8_flags.const_tracking_let) {
      for (int i = 0; i < side_data_array->length(); i++) {
        Tagged<Object> element = side_data_array->get(i);
        if (IsSmi(element)) {
          CHECK(element == ConstTrackingLetCell::kConstMarker ||
                element == ConstTrackingLetCell::kNonConstMarker);
        } else {
          // The slot contains `undefined` before the variable is initialized.
          CHECK(IsUndefined(element) || IsConstTrackingLetCell(element));
        }
      }
    } else {
      CHECK_EQ(0, side_data_array->length());
    }
  }
}

void NativeContext::NativeContextVerify(Isolate* isolate) {
  ContextVerify(isolate);
  CHECK(retained_maps() == Smi::zero() || IsWeakArrayList(retained_maps()));
  CHECK_EQ(length(), NativeContext::NATIVE_CONTEXT_SLOTS);
  CHECK_EQ(kVariableSizeSentinel, map()->instance_size());
}

void FeedbackMetadata::FeedbackMetadataVerify(Isolate* isolate) {
  if (slot_count() == 0 && create_closure_slot_count() == 0) {
    CHECK_EQ(ReadOnlyRoots(isolate).empty_feedback_metadata(), *this);
  } else {
    FeedbackMetadataIterator iter(*this);
    while (iter.HasNext()) {
      iter.Next();
      FeedbackSlotKind kind = iter.kind();
      CHECK_NE(FeedbackSlotKind::kInvalid, kind);
      CHECK_GT(kFeedbackSlotKindCount, kind);
    }
  }
}

void DescriptorArray::DescriptorArrayVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::DescriptorArrayVerify(*this, isolate);
  if (number_of_all_descriptors() == 0) {
    CHECK_EQ(ReadOnlyRoots(isolate).empty_descriptor_array(), *this);
    CHECK_EQ(0, number_of_all_descriptors());
    CHECK_EQ(0, number_of_descriptors());
    CHECK_EQ(ReadOnlyRoots(isolate).empty_enum_cache(), enum_cache());
  } else {
    CHECK_LT(0, number_of_all_descriptors());
    CHECK_LE(number_of_descriptors(), number_of_all_descriptors());

    // Check that properties with private symbols names are non-enumerable, and
    // that fields are in order.
    int expected_field_index = 0;
    for (InternalIndex descriptor :
         InternalIndex::Range(number_of_descriptors())) {
      Tagged<Object> key =
          *(GetDescriptorSlot(descriptor.as_int()) + kEntryKeyIndex);
      // number_of_descriptors() may be out of sync with the actual descriptors
      // written during descriptor array construction.
      if (IsUndefined(key, isolate)) continue;
      PropertyDetails details = GetDetails(descriptor);
      if (Cast<Name>(key)->IsPrivate()) {
        CHECK_NE(details.attributes() & DONT_ENUM, 0);
      }
      Tagged<MaybeObject> value = GetValue(descriptor);
      if (details.location() == PropertyLocation::kField) {
        CHECK_EQ(details.field_index(), expected_field_index);
        CHECK(value == FieldType::None() || value == FieldType::Any() ||
              IsMap(value.GetHeapObjectAssumeWeak()));
        expected_field_index += details.field_width_in_words();
      } else {
        CHECK(!value.IsWeakOrCleared());
        CHECK(!IsMap(Cast<Object>(value)));
      }
    }
  }
}

void TransitionArray::TransitionArrayVerify(Isolate* isolate) {
  WeakFixedArrayVerify(isolate);
  CHECK_LE(LengthFor(number_of_transitions()), length());

  ReadOnlyRoots roots(isolate);
  Tagged<Map> owner;

  // Check all entries have the same owner
  for (int i = 0; i < number_of_transitions(); ++i) {
    Tagged<Map> target = GetTarget(i);
    Tagged<Map> parent = Cast<Map>(target->constructor_or_back_pointer());
    if (owner.is_null()) {
      parent = owner;
    } else {
      CHECK_EQ(parent, owner);
    }
  }
  // Check all entries have the same owner
  if (HasPrototypeTransitions()) {
    Tagged<WeakFixedArray> proto_trans = GetPrototypeTransitions();
    int length = TransitionArray::NumberOfPrototypeTransitions(proto_trans);
    for (int i = 0; i < length; ++i) {
      int index = TransitionArray::kProtoTransitionHeaderSize + i;
      Tagged<MaybeObject> maybe_target = proto_trans->get(index);
      Tagged<HeapObject> target;
      if (maybe_target.GetHeapObjectIfWeak(&target)) {
        if (v8_flags.move_prototype_transitions_first) {
          Tagged<Map> parent =
              Cast<Map>(Cast<Map>(target)->constructor_or_back_pointer());
          if (owner.is_null()) {
            parent = Cast<Map>(target);
          } else {
            CHECK_EQ(parent, owner);
          }
        } else {
          CHECK(IsUndefined(Cast<Map>(target)->GetBackPointer()));
        }
      }
    }
  }
  // Check all entries are valid
  if (HasSideStepTransitions()) {
    Tagged<WeakFixedArray> side_trans = GetSideStepTransitions();
    for (uint32_t index = SideStepTransition::kFirstMapIdx;
         index <= SideStepTransition::kLastMapIdx; ++index) {
      Tagged<MaybeObject> maybe_target = side_trans->get(index);
      Tagged<HeapObject> target;
      if (maybe_target.GetHeapObjectIfWeak(&target)) {
        CHECK(IsMap(target));
      } else {
        CHECK(maybe_target == SideStepTransition::Unreachable ||
              maybe_target == SideStepTransition::Empty ||
              maybe_target.IsCleared());
      }
    }
    Tagged<MaybeObject> maybe_cell =
        side_trans->get(SideStepTransition::index_of(
            SideStepTransition::Kind::kObjectAssignValidityCell));
    Tagged<HeapObject> cell;
    if (maybe_cell.GetHeapObjectIfWeak(&cell)) {
      CHECK(IsCell(cell));
    } else {
      CHECK(maybe_cell == SideStepTransition::Empty || maybe_cell.IsCleared());
    }
  }
}

namespace {
void SloppyArgumentsElementsVerify(Isolate* isolate,
                                   Tagged<SloppyArgumentsElements> elements,
                                   Tagged<JSObject> holder) {
  elements->SloppyArgumentsElementsVerify(isolate);
  ElementsKind kind = holder->GetElementsKind();
  bool is_fast = kind == FAST_SLOPPY_ARGUMENTS_ELEMENTS;
  Tagged<Context> context_object = elements->context();
  Tagged<FixedArray> arg_elements = elements->arguments();
  if (arg_elements->length() == 0) {
    CHECK(arg_elements == ReadOnlyRoots(isolate).empty_fixed_array());
    return;
  }
  ElementsAccessor* accessor;
  if (is_fast) {
    accessor = ElementsAccessor::ForKind(HOLEY_ELEMENTS);
  } else {
    accessor = ElementsAccessor::ForKind(DICTIONARY_ELEMENTS);
  }
  int nofMappedParameters = 0;
  int maxMappedIndex = 0;
  for (int i = 0; i < nofMappedParameters; i++) {
    // Verify that each context-mapped argument is either the hole or a valid
    // Smi within context length range.
    Tagged<Object> mapped = elements->mapped_entries(i, kRelaxedLoad);
    if (IsTheHole(mapped, isolate)) {
      // Slow sloppy arguments can be holey.
      if (!is_fast) continue;
      // Fast sloppy arguments elements are never holey. Either the element is
      // context-mapped or present in the arguments elements.
      CHECK(accessor->HasElement(holder, i, arg_elements));
      continue;
    }
    int mappedIndex = Smi::ToInt(mapped);
    nofMappedParameters++;
    CHECK_LE(maxMappedIndex, mappedIndex);
    maxMappedIndex = mappedIndex;
    Tagged<Object> value = context_object->get(mappedIndex);
    CHECK(IsObject(value));
    // None of the context-mapped entries should exist in the arguments
    // elements.
    CHECK(!accessor->HasElement(holder, i, arg_elements));
  }
  CHECK_LE(nofMappedParameters, context_object->length());
  CHECK_LE(nofMappedParameters, arg_elements->length());
  CHECK_LE(maxMappedIndex, context_object->length());
  CHECK_LE(maxMappedIndex, arg_elements->length());
}
}  // namespace

void JSArgumentsObject::JSArgumentsObjectVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSArgumentsObjectVerify(*this, isolate);
  if (IsSloppyArgumentsElementsKind(GetElementsKind())) {
    SloppyArgumentsElementsVerify(
        isolate, Cast<SloppyArgumentsElements>(elements()), *this);
  }
  if (isolate->IsInAnyContext(map(), Context::SLOPPY_ARGUMENTS_MAP_INDEX) ||
      isolate->IsInAnyContext(map(),
                              Context::SLOW_ALIASED_ARGUMENTS_MAP_INDEX) ||
      isolate->IsInAnyContext(map(),
                              Context::FAST_ALIASED_ARGUMENTS_MAP_INDEX)) {
    VerifyObjectField(isolate, JSSloppyArgumentsObject::kLengthOffset);
    VerifyObjectField(isolate, JSSloppyArgumentsObject::kCalleeOffset);
  } else if (isolate->IsInAnyContext(map(),
                                     Context::STRICT_ARGUMENTS_MAP_INDEX)) {
    VerifyObjectField(isolate, JSStrictArgumentsObject::kLengthOffset);
  }
}

void JSAsyncFunctionObject::JSAsyncFunctionObjectVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSAsyncFunctionObjectVerify(*this, isolate);
}

void JSAsyncGeneratorObject::JSAsyncGeneratorObjectVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSAsyncGeneratorObjectVerify(*this, isolate);
}

void JSDate::JSDateVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSDateVerify(*this, isolate);

  if (IsSmi(month())) {
    int month = Smi::ToInt(this->month());
    CHECK(0 <= month && month <= 11);
  }
  if (IsSmi(day())) {
    int day = Smi::ToInt(this->day());
    CHECK(1 <= day && day <= 31);
  }
  if (IsSmi(hour())) {
    int hour = Smi::ToInt(this->hour());
    CHECK(0 <= hour && hour <= 23);
  }
  if (IsSmi(min())) {
    int min = Smi::ToInt(this->min());
    CHECK(0 <= min && min <= 59);
  }
  if (IsSmi(sec())) {
    int sec = Smi::ToInt(this->sec());
    CHECK(0 <= sec && sec <= 59);
  }
  if (IsSmi(weekday())) {
    int weekday = Smi::ToInt(this->weekday());
    CHECK(0 <= weekday && weekday <= 6);
  }
  if (IsSmi(cache_stamp())) {
    CHECK(Smi::ToInt(cache_stamp()) <=
          Smi::ToInt(isolate->date_cache()->stamp()));
  }
}

void String::StringVerify(Isolate* isolate) {
  PrimitiveHeapObjectVerify(isolate);
  CHECK(IsString(this, isolate));
  CHECK(length() >= 0 && length() <= Smi::kMaxValue);
  CHECK_IMPLIES(length() == 0, this == ReadOnlyRoots(isolate).empty_string());
  if (IsInternalizedString(this)) {
    CHECK(HasHashCode());
    CHECK(!ObjectInYoungGeneration(this));
  }
}

void ConsString::ConsStringVerify(Isolate* isolate) {
  StringVerify(isolate);
  CHECK(IsConsString(this, isolate));
  CHECK_GE(length(), ConsString::kMinLength);
  CHECK(length() == first()->length() + second()->length());
  if (IsFlat()) {
    // A flat cons can only be created by String::SlowFlatten.
    // Afterwards, the first part may be externalized or internalized.
    CHECK(IsSeqString(first()) || IsExternalString(first()) ||
          IsThinString(first()));
  }
}

void ThinString::ThinStringVerify(Isolate* isolate) {
  StringVerify(isolate);
  CHECK(IsThinString(this, isolate));
  CHECK(!HasForwardingIndex(kAcquireLoad));
  CHECK(IsInternalizedString(actual()));
  CHECK(IsSeqString(actual()) || IsExternalString(actual()));
}

void SlicedString::SlicedStringVerify(Isolate* isolate) {
  StringVerify(isolate);
  CHECK(IsSlicedString(this, isolate));
  CHECK(!IsConsString(parent()));
  CHECK(!IsSlicedString(parent()));
#ifdef DEBUG
  if (!isolate->has_turbofan_string_builders()) {
    // Turbofan's string builder optimization can introduce SlicedString that
    // are less than SlicedString::kMinLength characters. Their live range and
    // scope are pretty limitted, but they can be visible to the GC, which
    // shouldn't treat them as invalid.
    CHECK_GE(length(), SlicedString::kMinLength);
  }
#endif
}

void ExternalString::ExternalStringVerify(Isolate* isolate) {
  StringVerify(isolate);
  CHECK(IsExternalString(this, isolate));
}

void JSBoundFunction::JSBoundFunctionVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSBoundFunctionVerify(*this, isolate);
  CHECK(IsCallable(*this));
  CHECK_EQ(IsConstructor(*this), IsConstructor(bound_target_function()));
  // Ensure that the function's meta map belongs to the same native context
  // as the target function (i.e. meta maps are the same).
  CHECK_EQ(map()->map(), bound_target_function()->map()->map());
}

void JSFunction::JSFunctionVerify(Isolate* isolate) {
  // Don't call TorqueGeneratedClassVerifiers::JSFunctionVerify here because the
  // Torque class definition contains the field `prototype_or_initial_map` which
  // may not be allocated.

  // This assertion exists to encourage updating this verification function if
  // new fields are added in the Torque class layout definition.
  static_assert(JSFunction::TorqueGeneratedClass::kHeaderSize ==
                (8 + V8_ENABLE_LEAPTIERING_BOOL) * kTaggedSize);

  JSFunctionOrBoundFunctionOrWrappedFunctionVerify(isolate);
  CHECK(IsJSFunction(*this));
  Object::VerifyPointer(isolate, shared(isolate));
  CHECK(IsSharedFunctionInfo(shared(isolate)));
  Object::VerifyPointer(isolate, context(isolate, kRelaxedLoad));
  CHECK(IsContext(context(isolate, kRelaxedLoad)));
  Object::VerifyPointer(isolate, raw_feedback_cell(isolate));
  CHECK(IsFeedbackCell(raw_feedback_cell(isolate)));
  Object::VerifyPointer(isolate, code(isolate));
  CHECK(IsCode(code(isolate)));
  CHECK(map(isolate)->is_callable());
  // Ensure that the function's meta map belongs to the same native context.
  CHECK_EQ(map()->map()->native_context_or_null(), native_context());

#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchHandle handle = dispatch_handle();
  // Currently, the handle can still be the null handle as not all places where
  // a JSFunction object is created populate the entry correctly. However, in
  // the future, every JSFunction must have a valid dispatch handle, and then
  // this check should be removed.
  if (handle != kNullJSDispatchHandle) {
    uint16_t parameter_count =
        GetProcessWideJSDispatchTable()->GetParameterCount(handle);
    CHECK_EQ(parameter_count,
             shared(isolate)->internal_formal_parameter_count_with_receiver());
  }
#endif  // V8_ENABLE_LEAPTIERING

  Handle<JSFunction> function(*this, isolate);
  LookupIterator it(isolate, function, isolate->factory()->prototype_string(),
                    LookupIterator::OWN_SKIP_INTERCEPTOR);
  if (has_prototype_slot()) {
    VerifyObjectField(isolate, kPrototypeOrInitialMapOffset);
  }

  if (has_prototype_property()) {
    CHECK(it.IsFound());
    CHECK_EQ(LookupIterator::ACCESSOR, it.state());
    CHECK(IsAccessorInfo(*it.GetAccessors()));
  } else {
    CHECK(!it.IsFound() || it.state() != LookupIterator::ACCESSOR ||
          !IsAccessorInfo(*it.GetAccessors()));
  }
}

void JSWrappedFunction::JSWrappedFunctionVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSWrappedFunctionVerify(*this, isolate);
  CHECK(IsCallable(*this));
  // Ensure that the function's meta map belongs to the same native context.
  CHECK_EQ(map()->map()->native_context_or_null(), context());
}

namespace {

bool ShouldVerifySharedFunctionInfoFunctionIndex(
    Tagged<SharedFunctionInfo> sfi) {
  if (!sfi->HasBuiltinId()) return true;
  switch (sfi->builtin_id()) {
    case Builtin::kPromiseCapabilityDefaultReject:
    case Builtin::kPromiseCapabilityDefaultResolve:
      // For these we manually set custom function indices.
      return false;
    default:
      return true;
  }
  UNREACHABLE();
}

}  // namespace

void SharedFunctionInfo::SharedFunctionInfoVerify(LocalIsolate* isolate) {
  ReadOnlyRoots roots(isolate);

  Tagged<Object> value = name_or_scope_info(kAcquireLoad);
  if (IsScopeInfo(value)) {
    CHECK(!Cast<ScopeInfo>(value)->IsEmpty());
    CHECK_NE(value, roots.empty_scope_info());
  }

#if V8_ENABLE_WEBASSEMBLY
  bool is_wasm = HasWasmExportedFunctionData() || HasAsmWasmData() ||
                 HasWasmJSFunctionData() || HasWasmCapiFunctionData() ||
                 HasWasmResumeData();
#else
  bool is_wasm = false;
#endif  // V8_ENABLE_WEBASSEMBLY
  CHECK(is_wasm || IsApiFunction() || HasBytecodeArray() || HasBuiltinId() ||
        HasUncompiledDataWithPreparseData() ||
        HasUncompiledDataWithoutPreparseData());

  {
    Tagged<HeapObject> script = this->script(kAcquireLoad);
    CHECK(IsUndefined(script, roots) || IsScript(script));
  }

  if (!is_compiled()) {
    CHECK(!HasFeedbackMetadata());
    CHECK(IsScopeInfo(outer_scope_info()) ||
          IsTheHole(outer_scope_info(), roots));
  } else if (HasBytecodeArray() && HasFeedbackMetadata()) {
    CHECK(IsFeedbackMetadata(feedback_metadata()));
  }

  if (HasBytecodeArray() && !IsDontAdaptArguments()) {
    CHECK_EQ(GetBytecodeArray(isolate)->parameter_count(),
             internal_formal_parameter_count_with_receiver());
  }

  if (ShouldVerifySharedFunctionInfoFunctionIndex(*this)) {
    int expected_map_index =
        Context::FunctionMapIndex(language_mode(), kind(), HasSharedName());
    CHECK_EQ(expected_map_index, function_map_index());
  }

  Tagged<ScopeInfo> info = EarlyScopeInfo(kAcquireLoad);
  if (!info->IsEmpty()) {
    CHECK(kind() == info->function_kind());
    CHECK_EQ(internal::IsModule(kind()), info->scope_type() == MODULE_SCOPE);
  }

  if (IsApiFunction()) {
    CHECK(construct_as_builtin());
  } else if (!HasBuiltinId()) {
    CHECK(!construct_as_builtin());
  } else {
    if (builtin_id() != Builtin::kCompileLazy &&
        builtin_id() != Builtin::kEmptyFunction) {
      CHECK(construct_as_builtin());
    } else {
      CHECK(!construct_as_builtin());
    }
  }
}

void SharedFunctionInfo::SharedFunctionInfoVerify(Isolate* isolate) {
  // TODO(leszeks): Add a TorqueGeneratedClassVerifier for LocalIsolate.
  SharedFunctionInfoVerify(isolate->AsLocalIsolate());
}

void SharedFunctionInfoWrapper::SharedFunctionInfoWrapperVerify(
    Isolate* isolate) {
  Object::VerifyPointer(isolate, shared_info());
}

void JSGlobalProxy::JSGlobalProxyVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSGlobalProxyVerify(*this, isolate);
  CHECK(map()->is_access_check_needed());
  // Make sure that this object has no properties, elements.
  CHECK_EQ(0, Cast<FixedArray>(elements())->length());
}

void JSGlobalObject::JSGlobalObjectVerify(Isolate* isolate) {
  CHECK(IsJSGlobalObject(*this));
  // Do not check the dummy global object for the builtins.
  if (global_dictionary(kAcquireLoad)->NumberOfElements() == 0 &&
      elements()->length() == 0) {
    return;
  }
  JSObjectVerify(isolate);
}

void PrimitiveHeapObject::PrimitiveHeapObjectVerify(Isolate* isolate) {
  CHECK(IsPrimitiveHeapObject(this, isolate));
}

void HeapNumber::HeapNumberVerify(Isolate* isolate) {
  PrimitiveHeapObjectVerify(isolate);
  CHECK(IsHeapNumber(this, isolate));
}

void Oddball::OddballVerify(Isolate* isolate) {
  PrimitiveHeapObjectVerify(isolate);
  CHECK(IsOddball(this, isolate));

  Heap* heap = isolate->heap();
  Tagged<Object> string = to_string();
  Object::VerifyPointer(isolate, string);
  CHECK(IsString(string));
  Tagged<Object> type = type_of();
  Object::VerifyPointer(isolate, type);
  CHECK(IsString(type));
  Tagged<Object> kind_value = kind_.load();
  Object::VerifyPointer(isolate, kind_value);
  CHECK(IsSmi(kind_value));

  Tagged<Object> number = to_number();
  Object::VerifyPointer(isolate, number);
  CHECK(IsSmi(number) || IsHeapNumber(number));
  if (IsHeapObject(number)) {
    CHECK(number == ReadOnlyRoots(heap).nan_value() ||
          number == ReadOnlyRoots(heap).hole_nan_value());
  } else {
    CHECK(IsSmi(number));
    int value = Smi::ToInt(number);
    // Hidden oddballs have negative smis.
    const int kLeastHiddenOddballNumber = -7;
    CHECK_LE(value, 1);
    CHECK_GE(value, kLeastHiddenOddballNumber);
  }

  ReadOnlyRoots roots(heap);
  if (map() == roots.undefined_map()) {
    CHECK(this == roots.undefined_value());
  } else if (map() == roots.null_map()) {
    CHECK(this == roots.null_value());
  } else if (map() == roots.boolean_map()) {
    CHECK(this == roots.true_value() || this == roots.false_value());
  } else {
    UNREACHABLE();
  }
}

void Hole::HoleVerify(Isolate* isolate) {
  ReadOnlyRoots roots(isolate->heap());
  CHECK_EQ(map(), roots.hole_map());

#define COMPARE_ROOTS_VALUE(_, Value, __) \
  if (*this == roots.Value()) {           \
    return;                               \
  }
  HOLE_LIST(COMPARE_ROOTS_VALUE);
#undef COMPARE_ROOTS_VALUE

  UNREACHABLE();
}

void PropertyCell::PropertyCellVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::PropertyCellVerify(*this, isolate);
  CHECK(IsUniqueName(name()));
  CheckDataIsCompatible(property_details(), value());
}

void ConstTrackingLetCell::ConstTrackingLetCellVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::ConstTrackingLetCellVerify(*this, isolate);
}

void TrustedObject::TrustedObjectVerify(Isolate* isolate) {
#if defined(V8_ENABLE_SANDBOX)
  // All trusted objects must live in trusted space.
  // TODO(saelo): Some objects are trusted but do not yet live in trusted space.
  CHECK(IsTrustedSpaceObject(*this) || IsCode(*this));
#endif
}

void ExposedTrustedObject::ExposedTrustedObjectVerify(Isolate* isolate) {
  TrustedObjectVerify(isolate);
#if defined(V8_ENABLE_SANDBOX)
  // Check that the self indirect pointer is consistent, i.e. points back to
  // this object.
  InstanceType instance_type = map()->instance_type();
  IndirectPointerTag tag = IndirectPointerTagFromInstanceType(instance_type);
  // We can't use ReadIndirectPointerField here because the tag is not a
  // compile-time constant.
  IndirectPointerSlot slot =
      RawIndirectPointerField(kSelfIndirectPointerOffset, tag);
  Tagged<Object> self = slot.load(isolate);
  CHECK_EQ(self, *this);
  // If the object is in the read-only space, the self indirect pointer entry
  // must be in the read-only segment, and vice versa.
  if (tag == kCodeIndirectPointerTag) {
    CodePointerTable::Space* space =
        IsolateForSandbox(isolate).GetCodePointerTableSpaceFor(slot.address());
    // During snapshot creation, the code pointer space of the read-only heap is
    // not marked as an internal read-only space.
    bool is_space_read_only =
        space == isolate->read_only_heap()->code_pointer_space();
    CHECK_EQ(is_space_read_only, InReadOnlySpace(*this));
  } else {
    CHECK(!InReadOnlySpace(*this));
  }
#endif
}

void Code::CodeVerify(Isolate* isolate) {
  ExposedTrustedObjectVerify(isolate);
  CHECK(IsCode(*this));
  if (has_instruction_stream()) {
    Tagged<InstructionStream> istream = instruction_stream();
    CHECK_EQ(istream->code(kAcquireLoad), *this);
    CHECK_EQ(safepoint_table_offset(), 0);
    CHECK_LE(safepoint_table_offset(), handler_table_offset());
    CHECK_LE(handler_table_offset(), constant_pool_offset());
    CHECK_LE(constant_pool_offset(), code_comments_offset());
    CHECK_LE(code_comments_offset(), unwinding_info_offset());
    CHECK_LE(unwinding_info_offset(), metadata_size());

    // Ensure the cached code entry point corresponds to the InstructionStream
    // object associated with this Code.
#if defined(V8_COMPRESS_POINTERS) && defined(V8_SHORT_BUILTIN_CALLS)
    if (istream->instruction_start() == instruction_start()) {
      // Most common case, all good.
    } else {
      // When shared pointer compression cage is enabled and it has the
      // embedded code blob copy then the
      // InstructionStream::instruction_start() might return the address of
      // the remapped builtin regardless of whether the builtins copy existed
      // when the instruction_start value was cached in the Code (see
      // InstructionStream::OffHeapInstructionStart()).  So, do a reverse
      // Code object lookup via instruction_start value to ensure it
      // corresponds to this current Code object.
      Tagged<Code> lookup_result =
          isolate->heap()->FindCodeForInnerPointer(instruction_start());
      CHECK_EQ(lookup_result, *this);
    }
#else
    CHECK_EQ(istream->instruction_start(), instruction_start());
#endif  // V8_COMPRESS_POINTERS && V8_SHORT_BUILTIN_CALLS
  }

  // Our wrapper must point back to us.
  CHECK_EQ(wrapper()->code(isolate), *this);
}

void CodeWrapper::CodeWrapperVerify(Isolate* isolate) {
  if (!this->has_code()) return;
  auto code = this->code(isolate);
  Object::VerifyPointer(isolate, code);
  CHECK_EQ(code->wrapper(), *this);
}

void InstructionStream::InstructionStreamVerify(Isolate* isolate) {
  TrustedObjectVerify(isolate);
  Tagged<Code> code;
  if (!TryGetCode(&code, kAcquireLoad)) return;
  CHECK(
      IsAligned(code->instruction_size(),
                static_cast<unsigned>(InstructionStream::kMetadataAlignment)));
#if (!defined(_MSC_VER) || defined(__clang__)) && !defined(V8_OS_ZOS)
  // See also: PlatformEmbeddedFileWriterWin::AlignToCodeAlignment
  //      and: PlatformEmbeddedFileWriterZOS::AlignToCodeAlignment
  CHECK_IMPLIES(!ReadOnlyHeap::Contains(*this),
                IsAligned(instruction_start(), kCodeAlignment));
#endif  // (!defined(_MSC_VER) || defined(__clang__)) && !defined(V8_OS_ZOS)
  CHECK_IMPLIES(!ReadOnlyHeap::Contains(*this),
                IsAligned(instruction_start(), kCodeAlignment));
  CHECK_EQ(*this, code->instruction_stream());
  CHECK(V8_ENABLE_THIRD_PARTY_HEAP_BOOL ||
        Size() <= MemoryChunkLayout::MaxRegularCodeObjectSize() ||
        isolate->heap()->InSpace(*this, CODE_LO_SPACE));
  Address last_gc_pc = kNullAddress;

  Object::ObjectVerify(relocation_info(), isolate);

  for (RelocIterator it(code); !it.done(); it.next()) {
    it.rinfo()->Verify(isolate);
    // Ensure that GC will not iterate twice over the same pointer.
    if (RelocInfo::IsGCRelocMode(it.rinfo()->rmode())) {
      CHECK(it.rinfo()->pc() != last_gc_pc);
      last_gc_pc = it.rinfo()->pc();
    }
  }
}

void JSArray::JSArrayVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSArrayVerify(*this, isolate);
  // If a GC was caused while constructing this array, the elements
  // pointer may point to a one pointer filler map.
  if (!ElementsAreSafeToExamine(isolate)) return;
  if (IsUndefined(elements(), isolate)) return;
  CHECK(IsFixedArray(elements()) || IsFixedDoubleArray(elements()));
  if (elements()->length() == 0) {
    CHECK_EQ(elements(), ReadOnlyRoots(isolate).empty_fixed_array());
  }
  // Verify that the length and the elements backing store are in sync.
  if (IsSmi(length()) && (HasFastElements() || HasAnyNonextensibleElements())) {
    if (elements()->length() > 0) {
      CHECK_IMPLIES(HasDoubleElements(), IsFixedDoubleArray(elements()));
      CHECK_IMPLIES(HasSmiOrObjectElements() || HasAnyNonextensibleElements(),
                    IsFixedArray(elements()));
    }
    int size = Smi::ToInt(length());
    // Holey / Packed backing stores might have slack or might have not been
    // properly initialized yet.
    CHECK(size <= elements()->length() ||
          elements() == ReadOnlyRoots(isolate).empty_fixed_array());
  } else {
    CHECK(HasDictionaryElements());
    uint32_t array_length;
    CHECK(Object::ToArrayLength(length(), &array_length));
    if (array_length == 0xFFFFFFFF) {
      CHECK(Object::ToArrayLength(length(), &array_length));
    }
    if (array_length != 0) {
      Tagged<NumberDictionary> dict = Cast<NumberDictionary>(elements());
      // The dictionary can never have more elements than the array length + 1.
      // If the backing store grows the verification might be triggered with
      // the old length in place.
      uint32_t nof_elements = static_cast<uint32_t>(dict->NumberOfElements());
      if (nof_elements != 0) nof_elements--;
      CHECK_LE(nof_elements, array_length);
    }
  }
}

void JSSet::JSSetVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSSetVerify(*this, isolate);
  CHECK(IsOrderedHashSet(table()) || IsUndefined(table(), isolate));
  // TODO(arv): Verify OrderedHashTable too.
}

void JSMap::JSMapVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSMapVerify(*this, isolate);
  CHECK(IsOrderedHashMap(table()) || IsUndefined(table(), isolate));
  // TODO(arv): Verify OrderedHashTable too.
}

void JSSetIterator::JSSetIteratorVerify(Isolate* isolate) {
  CHECK(IsJSSetIterator(*this));
  JSCollectionIteratorVerify(isolate);
  CHECK(IsOrderedHashSet(table()));
  CHECK(IsSmi(index()));
}

void JSMapIterator::JSMapIteratorVerify(Isolate* isolate) {
  CHECK(IsJSMapIterator(*this));
  JSCollectionIteratorVerify(isolate);
  CHECK(IsOrderedHashMap(table()));
  CHECK(IsSmi(index()));
}

USE_TORQUE_VERIFIER(JSShadowRealm)

namespace {

void VerifyElementIsShared(Tagged<Object> element) {
  // Exception for ThinStrings:
  // When storing a ThinString in a shared object, we want to store the actual
  // string, which is shared when sharing the string table.
  // It is possible that a stored shared string migrates to a ThinString later
  // on, which is fine as the ThinString resides in shared space if the original
  // string was in shared space.
  if (IsThinString(element)) {
    CHECK(v8_flags.shared_string_table);
    CHECK(InWritableSharedSpace(Cast<ThinString>(element)));
  } else {
    CHECK(IsShared(element));
  }
}

}  // namespace

void JSSharedStruct::JSSharedStructVerify(Isolate* isolate) {
  CHECK(IsJSSharedStruct(*this));
  CHECK(InWritableSharedSpace(*this));
  JSObjectVerify(isolate);
  CHECK(HasFastProperties());
  // Shared structs can only point to primitives or other shared HeapObjects,
  // even internally.
  Tagged<Map> struct_map = map();
  CHECK(InAnySharedSpace(property_array()));
  Tagged<DescriptorArray> descriptors =
      struct_map->instance_descriptors(isolate);
  for (InternalIndex i : struct_map->IterateOwnDescriptors()) {
    PropertyDetails details = descriptors->GetDetails(i);
    CHECK_EQ(PropertyKind::kData, details.kind());

    if (JSSharedStruct::IsRegistryKeyDescriptor(isolate, struct_map, i)) {
      CHECK_EQ(PropertyLocation::kDescriptor, details.location());
      CHECK(IsInternalizedString(descriptors->GetStrongValue(i)));
    } else if (JSSharedStruct::IsElementsTemplateDescriptor(isolate, struct_map,
                                                            i)) {
      CHECK_EQ(PropertyLocation::kDescriptor, details.location());
      CHECK(IsNumberDictionary(descriptors->GetStrongValue(i)));
    } else {
      CHECK_EQ(PropertyLocation::kField, details.location());
      CHECK(details.representation().IsTagged());
      CHECK(!IsNumberDictionary(descriptors->GetStrongValue(i)));
      CHECK(!IsInternalizedString(descriptors->GetStrongValue(i)));
      FieldIndex field_index = FieldIndex::ForDetails(struct_map, details);
      VerifyElementIsShared(RawFastPropertyAt(field_index));
    }
  }
}

void JSAtomicsMutex::JSAtomicsMutexVerify(Isolate* isolate) {
  CHECK(IsJSAtomicsMutex(*this));
  CHECK(InWritableSharedSpace(*this));
  JSObjectVerify(isolate);
}

void JSAtomicsCondition::JSAtomicsConditionVerify(Isolate* isolate) {
  CHECK(IsJSAtomicsCondition(*this));
  CHECK(InAnySharedSpace(*this));
  JSObjectVerify(isolate);
}

void JSDisposableStackBase::JSDisposableStackBaseVerify(Isolate* isolate) {
  CHECK(IsJSDisposableStackBase(*this));
  JSObjectVerify(isolate);
  CHECK_EQ(length() % 3, 0);
  CHECK_GE(stack()->capacity(), length());
}

void JSSyncDisposableStack::JSSyncDisposableStackVerify(Isolate* isolate) {
  CHECK(IsJSSyncDisposableStack(*this));
  JSDisposableStackBase::JSDisposableStackBaseVerify(isolate);
}

void JSAsyncDisposableStack::JSAsyncDisposableStackVerify(Isolate* isolate) {
  CHECK(IsJSAsyncDisposableStack(*this));
  JSDisposableStackBase::JSDisposableStackBaseVerify(isolate);
}

void JSSharedArray::JSSharedArrayVerify(Isolate* isolate) {
  CHECK(IsJSSharedArray(*this));
  JSObjectVerify(isolate);
  CHECK(HasFastProperties());
  // Shared arrays can only point to primitives or other shared HeapObjects,
  // even internally.
  Tagged<FixedArray> storage = Cast<FixedArray>(elements());
  uint32_t length = storage->length();
  for (uint32_t j = 0; j < length; j++) {
    Tagged<Object> element_value = storage->get(j);
    VerifyElementIsShared(element_value);
  }
}

void JSIteratorMapHelper::JSIteratorMapHelperVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSIteratorMapHelperVerify(*this, isolate);
  CHECK(IsCallable(mapper()));
  CHECK_GE(Object::NumberValue(counter()), 0);
}

void JSIteratorFilterHelper::JSIteratorFilterHelperVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSIteratorFilterHelperVerify(*this, isolate);
  CHECK(IsCallable(predicate()));
  CHECK_GE(Object::NumberValue(counter()), 0);
}

void JSIteratorTakeHelper::JSIteratorTakeHelperVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSIteratorTakeHelperVerify(*this, isolate);
  CHECK_GE(Object::NumberValue(remaining()), 0);
}

void JSIteratorDropHelper::JSIteratorDropHelperVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSIteratorDropHelperVerify(*this, isolate);
  CHECK_GE(Object::NumberValue(remaining()), 0);
}

void JSIteratorFlatMapHelper::JSIteratorFlatMapHelperVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSIteratorFlatMapHelperVerify(*this, isolate);
  CHECK(IsCallable(mapper()));
  CHECK_GE(Object::NumberValue(counter()), 0);
}

void WeakCell::WeakCellVerify(Isolate* isolate) {
  CHECK(IsWeakCell(*this));

  CHECK(IsUndefined(target(), isolate) || Object::CanBeHeldWeakly(target()));

  CHECK(IsWeakCell(prev()) || IsUndefined(prev(), isolate));
  if (IsWeakCell(prev())) {
    CHECK_EQ(Cast<WeakCell>(prev())->next(), *this);
  }

  CHECK(IsWeakCell(next()) || IsUndefined(next(), isolate));
  if (IsWeakCell(next())) {
    CHECK_EQ(Cast<WeakCell>(next())->prev(), *this);
  }

  CHECK_IMPLIES(IsUndefined(unregister_token(), isolate),
                IsUndefined(key_list_prev(), isolate));
  CHECK_IMPLIES(IsUndefined(unregister_token(), isolate),
                IsUndefined(key_list_next(), isolate));

  CHECK(IsWeakCell(key_list_prev()) || IsUndefined(key_list_prev(), isolate));

  CHECK(IsWeakCell(key_list_next()) || IsUndefined(key_list_next(), isolate));

  CHECK(IsUndefined(finalization_registry(), isolate) ||
        IsJSFinalizationRegistry(finalization_registry()));
}

void JSWeakRef::JSWeakRefVerify(Isolate* isolate) {
  CHECK(IsJSWeakRef(*this));
  JSObjectVerify(isolate);
  CHECK(IsUndefined(target(), isolate) || Object::CanBeHeldWeakly(target()));
}

void JSFinalizationRegistry::JSFinalizationRegistryVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSFinalizationRegistryVerify(*this, isolate);
  if (IsWeakCell(active_cells())) {
    CHECK(IsUndefined(Cast<WeakCell>(active_cells())->prev(), isolate));
  }
  if (IsWeakCell(cleared_cells())) {
    CHECK(IsUndefined(Cast<WeakCell>(cleared_cells())->prev(), isolate));
  }
}

void JSWeakMap::JSWeakMapVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSWeakMapVerify(*this, isolate);
  CHECK(IsEphemeronHashTable(table()) || IsUndefined(table(), isolate));
}

void JSArrayIterator::JSArrayIteratorVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSArrayIteratorVerify(*this, isolate);

  CHECK_GE(Object::NumberValue(next_index()), 0);
  CHECK_LE(Object::NumberValue(next_index()), kMaxSafeInteger);

  if (IsJSTypedArray(iterated_object())) {
    // JSTypedArray::length is limited to Smi range.
    CHECK(IsSmi(next_index()));
    CHECK_LE(Object::NumberValue(next_index()), Smi::kMaxValue);
  } else if (IsJSArray(iterated_object())) {
    // JSArray::length is limited to Uint32 range.
    CHECK_LE(Object::NumberValue(next_index()), kMaxUInt32);
  }
}

void JSStringIterator::JSStringIteratorVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSStringIteratorVerify(*this, isolate);
  CHECK_GE(index(), 0);
  CHECK_LE(index(), String::kMaxLength);
}

void JSWeakSet::JSWeakSetVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSWeakSetVerify(*this, isolate);
  CHECK(IsEphemeronHashTable(table()) || IsUndefined(table(), isolate));
}

void CallableTask::CallableTaskVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::CallableTaskVerify(*this, isolate);
  CHECK(IsCallable(callable()));
}

void JSPromise::JSPromiseVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSPromiseVerify(*this, isolate);
  if (status() == Promise::kPending) {
    CHECK(IsSmi(reactions()) || IsPromiseReaction(reactions()));
  }
}

template <typename Derived>
void SmallOrderedHashTable<Derived>::SmallOrderedHashTableVerify(
    Isolate* isolate) {
  CHECK(IsSmallOrderedHashTable(*this));

  int capacity = Capacity();
  CHECK_GE(capacity, kMinCapacity);
  CHECK_LE(capacity, kMaxCapacity);

  for (int entry = 0; entry < NumberOfBuckets(); entry++) {
    int bucket = GetFirstEntry(entry);
    if (bucket == kNotFound) continue;
    CHECK_GE(bucket, 0);
    CHECK_LE(bucket, capacity);
  }

  for (int entry = 0; entry < NumberOfElements(); entry++) {
    int chain = GetNextEntry(entry);
    if (chain == kNotFound) continue;
    CHECK_GE(chain, 0);
    CHECK_LE(chain, capacity);
  }

  for (int entry = 0; entry < NumberOfElements(); entry++) {
    for (int offset = 0; offset < Derived::kEntrySize; offset++) {
      Tagged<Object> val = GetDataEntry(entry, offset);
      Object::VerifyPointer(isolate, val);
    }
  }

  for (int entry = NumberOfElements() + NumberOfDeletedElements();
       entry < Capacity(); entry++) {
    for (int offset = 0; offset < Derived::kEntrySize; offset++) {
      Tagged<Object> val = GetDataEntry(entry, offset);
      CHECK(IsTheHole(val, isolate));
    }
  }
}

void SmallOrderedHashMap::SmallOrderedHashMapVerify(Isolate* isolate) {
  CHECK(IsSmallOrderedHashMap(*this));
  SmallOrderedHashTable<SmallOrderedHashMap>::SmallOrderedHashTableVerify(
      isolate);
  for (int entry = NumberOfElements(); entry < NumberOfDeletedElements();
       entry++) {
    for (int offset = 0; offset < kEntrySize; offset++) {
      Tagged<Object> val = GetDataEntry(entry, offset);
      CHECK(IsTheHole(val, isolate));
    }
  }
}

void SmallOrderedHashSet::SmallOrderedHashSetVerify(Isolate* isolate) {
  CHECK(IsSmallOrderedHashSet(*this));
  SmallOrderedHashTable<SmallOrderedHashSet>::SmallOrderedHashTableVerify(
      isolate);
  for (int entry = NumberOfElements(); entry < NumberOfDeletedElements();
       entry++) {
    for (int offset = 0; offset < kEntrySize; offset++) {
      Tagged<Object> val = GetDataEntry(entry, offset);
      CHECK(IsTheHole(val, isolate));
    }
  }
}

void SmallOrderedNameDictionary::SmallOrderedNameDictionaryVerify(
    Isolate* isolate) {
  CHECK(IsSmallOrderedNameDictionary(*this));
  SmallOrderedHashTable<
      SmallOrderedNameDictionary>::SmallOrderedHashTableVerify(isolate);
  for (int entry = NumberOfElements(); entry < NumberOfDeletedElements();
       entry++) {
    for (int offset = 0; offset < kEntrySize; offset++) {
      Tagged<Object> val = GetDataEntry(entry, offset);
      CHECK(IsTheHole(val, isolate) ||
            (PropertyDetails::Empty().AsSmi() == Cast<Smi>(val)));
    }
  }
}

void SwissNameDictionary::SwissNameDictionaryVerify(Isolate* isolate) {
  this->SwissNameDictionaryVerify(isolate, false);
}

void SwissNameDictionary::SwissNameDictionaryVerify(Isolate* isolate,
                                                    bool slow_checks) {
  DisallowHeapAllocation no_gc;

  CHECK(IsValidCapacity(Capacity()));

  meta_table()->ByteArrayVerify(isolate);

  int seen_deleted = 0;
  int seen_present = 0;

  for (int i = 0; i < Capacity(); i++) {
    ctrl_t ctrl = GetCtrl(i);

    if (IsFull(ctrl) || slow_checks) {
      Tagged<Object> key = KeyAt(i);
      Tagged<Object> value = ValueAtRaw(i);

      if (IsFull(ctrl)) {
        ++seen_present;

        Tagged<Name> name = Cast<Name>(key);
        if (slow_checks) {
          CHECK_EQ(swiss_table::H2(name->hash()), ctrl);
        }

        CHECK(!IsTheHole(key));
        CHECK(!IsTheHole(value));
        name->NameVerify(isolate);
        Object::ObjectVerify(value, isolate);
      } else if (IsDeleted(ctrl)) {
        ++seen_deleted;
        CHECK(IsTheHole(key));
        CHECK(IsTheHole(value));
      } else if (IsEmpty(ctrl)) {
        CHECK(IsTheHole(key));
        CHECK(IsTheHole(value));
      } else {
        // Something unexpected. Note that we don't use kSentinel at the moment.
        UNREACHABLE();
      }
    }
  }

  CHECK_EQ(seen_present, NumberOfElements());
  if (slow_checks) {
    CHECK_EQ(seen_deleted, NumberOfDeletedElements());

    // Verify copy of first group at end (= after Capacity() slots) of control
    // table.
    for (int i = 0; i < std::min(static_cast<int>(Group::kWidth), Capacity());
         ++i) {
      CHECK_EQ(CtrlTable()[i], CtrlTable()[Capacity() + i]);
    }
    // If 2 * capacity is smaller than the capacity plus group width, the slots
    // after that must be empty.
    for (int i = 2 * Capacity(); i < Capacity() + kGroupWidth; ++i) {
      CHECK_EQ(Ctrl::kEmpty, CtrlTable()[i]);
    }

    for (int enum_index = 0; enum_index < UsedCapacity(); ++enum_index) {
      int entry = EntryForEnumerationIndex(enum_index);
      CHECK_LT(entry, Capacity());
      ctrl_t ctrl = GetCtrl(entry);

      // Enum table must not point to empty slots.
      CHECK(IsFull(ctrl) || IsDeleted(ctrl));
    }
  }
}

void JSRegExp::JSRegExpVerify(Isolate* isolate) {
  Tagged<Object> source = TaggedField<Object>::load(*this, kSourceOffset);
  Tagged<Object> flags = TaggedField<Object>::load(*this, kFlagsOffset);
  CHECK(IsString(source) || IsUndefined(source));
  CHECK(IsSmi(flags) || IsUndefined(flags));
  if (!has_data()) return;

  Tagged<RegExpData> data = this->data(isolate);
  switch (data->type_tag()) {
    case RegExpData::Type::ATOM:
      CHECK(Is<AtomRegExpData>(data));
      return;
    case RegExpData::Type::EXPERIMENTAL:
    case RegExpData::Type::IRREGEXP:
      CHECK(Is<IrRegExpData>(data));
      return;
  }
  UNREACHABLE();
}

void RegExpData::RegExpDataVerify(Isolate* isolate) {
  ExposedTrustedObjectVerify(isolate);
  CHECK(IsSmi(TaggedField<Object>::load(*this, kTypeTagOffset)));
  CHECK(IsString(source()));
  CHECK(IsSmi(TaggedField<Object>::load(*this, kFlagsOffset)));
}

void AtomRegExpData::AtomRegExpDataVerify(Isolate* isolate) {
  ExposedTrustedObjectVerify(isolate);
  RegExpDataVerify(isolate);
  CHECK(IsString(pattern()));
}

void IrRegExpData::IrRegExpDataVerify(Isolate* isolate) {
  ExposedTrustedObjectVerify(isolate);
  RegExpDataVerify(isolate);

  VerifyProtectedPointerField(isolate, kLatin1BytecodeOffset);
  VerifyProtectedPointerField(isolate, kUc16BytecodeOffset);

  CHECK_IMPLIES(!has_latin1_code(), !has_latin1_bytecode());
  CHECK_IMPLIES(!has_uc16_code(), !has_uc16_bytecode());

  CHECK_IMPLIES(has_latin1_code(), Is<Code>(latin1_code(isolate)));
  CHECK_IMPLIES(has_uc16_code(), Is<Code>(uc16_code(isolate)));
  CHECK_IMPLIES(has_latin1_bytecode(), Is<TrustedByteArray>(latin1_bytecode()));
  CHECK_IMPLIES(has_uc16_bytecode(), Is<TrustedByteArray>(uc16_bytecode()));

  CHECK_IMPLIES(
      IsSmi(capture_name_map()),
      Smi::ToInt(capture_name_map()) == JSRegExp::kUninitializedValue ||
          capture_name_map() == Smi::zero());
  CHECK_IMPLIES(!IsSmi(capture_name_map()), Is<FixedArray>(capture_name_map()));
  CHECK(IsSmi(TaggedField<Object>::load(*this, kMaxRegisterCountOffset)));
  CHECK(IsSmi(TaggedField<Object>::load(*this, kCaptureCountOffset)));
  CHECK(IsSmi(TaggedField<Object>::load(*this, kTicksUntilTierUpOffset)));
  CHECK(IsSmi(TaggedField<Object>::load(*this, kBacktrackLimitOffset)));

  switch (type_tag()) {
    case RegExpData::Type::EXPERIMENTAL: {
      if (has_latin1_code()) {
        CHECK_EQ(latin1_code(isolate)->builtin_id(),
                 Builtin::kRegExpExperimentalTrampoline);
        CHECK_EQ(latin1_code(isolate), uc16_code(isolate));
        CHECK(Is<TrustedByteArray>(latin1_bytecode()));
        CHECK_EQ(latin1_bytecode(), uc16_bytecode());
      } else {
        CHECK(!has_uc16_code());
        CHECK(!has_latin1_bytecode());
        CHECK(!has_uc16_bytecode());
      }

      CHECK_EQ(max_register_count(), JSRegExp::kUninitializedValue);
      CHECK_EQ(ticks_until_tier_up(), JSRegExp::kUninitializedValue);
      CHECK_EQ(backtrack_limit(), JSRegExp::kUninitializedValue);

      break;
    }
    case RegExpData::Type::IRREGEXP: {
      bool can_be_interpreted = RegExp::CanGenerateBytecode();
      CHECK_IMPLIES(has_latin1_bytecode(), can_be_interpreted);
      CHECK_IMPLIES(has_uc16_bytecode(), can_be_interpreted);

      static_assert(JSRegExp::kUninitializedValue == -1);
      CHECK_GE(max_register_count(), JSRegExp::kUninitializedValue);
      CHECK_GE(capture_count(), 0);
      if (v8_flags.regexp_tier_up) {
        // With tier-up enabled, ticks_until_tier_up should actually be >= 0.
        // However FlagScopes in unittests can modify the flag and verification
        // on Isolate deinitialization will fail.
        CHECK_GE(ticks_until_tier_up(), JSRegExp::kUninitializedValue);
        CHECK_LE(ticks_until_tier_up(), v8_flags.regexp_tier_up_ticks);
      } else {
        CHECK_EQ(ticks_until_tier_up(), JSRegExp::kUninitializedValue);
      }
      CHECK_GE(backtrack_limit(), 0);

      break;
    }
    default:
      UNREACHABLE();
  }
}

void RegExpDataWrapper::RegExpDataWrapperVerify(Isolate* isolate) {
  if (!this->has_data()) return;
  auto data = this->data(isolate);
  Object::VerifyPointer(isolate, data);
  CHECK_EQ(data->wrapper(), *this);
}

void JSProxy::JSProxyVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSProxyVerify(*this, isolate);
  CHECK(IsJSFunction(map()->GetConstructor()));
  if (!IsRevoked()) {
    CHECK_EQ(IsCallable(target()), map()->is_callable());
    CHECK_EQ(IsConstructor(target()), map()->is_constructor());
  }
  CHECK(IsNull(map()->prototype(), isolate));
  // There should be no properties on a Proxy.
  CHECK_EQ(0, map()->NumberOfOwnDescriptors());
}

void JSArrayBuffer::JSArrayBufferVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSArrayBufferVerify(*this, isolate);
  if (FIELD_SIZE(kOptionalPaddingOffset) != 0) {
    CHECK_EQ(4, FIELD_SIZE(kOptionalPaddingOffset));
    CHECK_EQ(0,
             *reinterpret_cast<uint32_t*>(address() + kOptionalPaddingOffset));
  }
}

void JSArrayBufferView::JSArrayBufferViewVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSArrayBufferViewVerify(*this, isolate);
  CHECK_LE(byte_length(), JSArrayBuffer::kMaxByteLength);
  CHECK_LE(byte_offset(), JSArrayBuffer::kMaxByteLength);
}

void JSTypedArray::JSTypedArrayVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSTypedArrayVerify(*this, isolate);
  CHECK_LE(GetLength(), JSTypedArray::kMaxByteLength / element_size());
}

void JSDataView::JSDataViewVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSDataViewVerify(*this, isolate);
  CHECK(!IsVariableLength());
  if (!WasDetached()) {
    CHECK_EQ(reinterpret_cast<uint8_t*>(
                 Cast<JSArrayBuffer>(buffer())->backing_store()) +
                 byte_offset(),
             data_pointer());
  }
}

void JSRabGsabDataView::JSRabGsabDataViewVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::JSRabGsabDataViewVerify(*this, isolate);
  CHECK(IsVariableLength());
  if (!WasDetached()) {
    CHECK_EQ(reinterpret_cast<uint8_t*>(
                 Cast<JSArrayBuffer>(buffer())->backing_store()) +
                 byte_offset(),
             data_pointer());
  }
}

void AsyncGeneratorRequest::AsyncGeneratorRequestVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::AsyncGeneratorRequestVerify(*this, isolate);
  CHECK_GE(resume_mode(), JSGeneratorObject::kNext);
  CHECK_LE(resume_mode(), JSGeneratorObject::kThrow);
}

void BigIntBase::BigIntBaseVerify(Isolate* isolate) {
  CHECK_GE(length(), 0);
  CHECK_IMPLIES(is_zero(), !sign());  // There is no -0n.
}

void SourceTextModuleInfoEntry::SourceTextModuleInfoEntryVerify(
    Isolate* isolate) {
  TorqueGeneratedClassVerifiers::SourceTextModuleInfoEntryVerify(*this,
                                                                 isolate);
  CHECK_IMPLIES(IsString(import_name()), module_request() >= 0);
  CHECK_IMPLIES(IsString(export_name()) && IsString(import_name()),
                IsUndefined(local_name(), isolate));
}

void Module::ModuleVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::ModuleVerify(*this, isolate);

  CHECK_EQ(status() == Module::kErrored, !IsTheHole(exception(), isolate));

  CHECK(IsUndefined(module_namespace(), isolate) ||
        IsJSModuleNamespace(module_namespace()));
  if (IsJSModuleNamespace(module_namespace())) {
    CHECK_LE(Module::kLinking, status());
    CHECK_EQ(Cast<JSModuleNamespace>(module_namespace())->module(), *this);
  }

  if (!(status() == kErrored || status() == kEvaluating ||
        status() == kEvaluatingAsync || status() == kEvaluated)) {
    CHECK(IsUndefined(top_level_capability()));
  }

  CHECK_NE(hash(), 0);
}

void ModuleRequest::ModuleRequestVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::ModuleRequestVerify(*this, isolate);
  CHECK_EQ(0,
           import_attributes()->length() % ModuleRequest::kAttributeEntrySize);

  for (int i = 0; i < import_attributes()->length();
       i += ModuleRequest::kAttributeEntrySize) {
    CHECK(IsString(import_attributes()->get(i)));      // Attribute key
    CHECK(IsString(import_attributes()->get(i + 1)));  // Attribute value
    CHECK(IsSmi(import_attributes()->get(i + 2)));     // Attribute location
  }
}

void SourceTextModule::SourceTextModuleVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::SourceTextModuleVerify(*this, isolate);

  if (status() == kErrored) {
    CHECK(IsSharedFunctionInfo(code()));
  } else if (status() == kEvaluating || status() == kEvaluatingAsync ||
             status() == kEvaluated) {
    CHECK(IsJSGeneratorObject(code()));
  } else {
    if (status() == kLinked) {
      CHECK(IsJSGeneratorObject(code()));
    } else if (status() == kLinking) {
      CHECK(IsJSFunction(code()));
    } else if (status() == kPreLinking) {
      CHECK(IsSharedFunctionInfo(code()));
    } else if (status() == kUnlinked) {
      CHECK(IsSharedFunctionInfo(code()));
    }
    CHECK(!AsyncParentModuleCount());
    CHECK(!pending_async_dependencies());
    CHECK(!HasAsyncEvaluationOrdinal());
  }

  CHECK_EQ(requested_modules()->length(), info()->module_requests()->length());
}

void SyntheticModule::SyntheticModuleVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::SyntheticModuleVerify(*this, isolate);

  for (int i = 0; i < export_names()->length(); i++) {
    CHECK(IsString(export_names()->get(i)));
  }
}

void PrototypeInfo::PrototypeInfoVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::PrototypeInfoVerify(*this, isolate);
  if (IsWeakArrayList(prototype_users())) {
    PrototypeUsers::Verify(Cast<WeakArrayList>(prototype_users()));
  } else {
    CHECK(IsSmi(prototype_users()));
  }
  Tagged<HeapObject> derived = derived_maps(isolate);
  if (!IsUndefined(derived)) {
    auto derived_list = Cast<WeakArrayList>(derived);
    CHECK_GT(derived_list->length(), 0);
    for (int i = 0; i < derived_list->length(); ++i) {
      derived_list->Get(i).IsWeakOrCleared();
    }
  }
}

void PrototypeUsers::Verify(Tagged<WeakArrayList> array) {
  if (array->length() == 0) {
    // Allow empty & uninitialized lists.
    return;
  }
  // Verify empty slot chain.
  int empty_slot = Smi::ToInt(empty_slot_index(array));
  int empty_slots_count = 0;
  while (empty_slot != kNoEmptySlotsMarker) {
    CHECK_GT(empty_slot, 0);
    CHECK_LT(empty_slot, array->length());
    empty_slot = array->Get(empty_slot).ToSmi().value();
    ++empty_slots_count;
  }

  // Verify that all elements are either weak pointers or SMIs marking empty
  // slots.
  int weak_maps_count = 0;
  for (int i = kFirstIndex; i < array->length(); ++i) {
    Tagged<HeapObject> heap_object;
    Tagged<MaybeObject> object = array->Get(i);
    if ((object.GetHeapObjectIfWeak(&heap_object) && IsMap(heap_object)) ||
        object.IsCleared()) {
      ++weak_maps_count;
    } else {
      CHECK(IsSmi(object));
    }
  }

  CHECK_EQ(weak_maps_count + empty_slots_count + 1, array->length());
}

void EnumCache::EnumCacheVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::EnumCacheVerify(*this, isolate);
  Heap* heap = isolate->heap();
  if (*this == ReadOnlyRoots(heap).empty_enum_cache()) {
    CHECK_EQ(ReadOnlyRoots(heap).empty_fixed_array(), keys());
    CHECK_EQ(ReadOnlyRoots(heap).empty_fixed_array(), indices());
  }
}

void ObjectBoilerplateDescription::ObjectBoilerplateDescriptionVerify(
    Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, Shape::kCapacityOffset)));
  CHECK(
      IsSmi(TaggedField<Object>::load(*this, Shape::kBackingStoreSizeOffset)));
  CHECK(IsSmi(TaggedField<Object>::load(*this, Shape::kFlagsOffset)));
  for (int i = 0; i < capacity(); ++i) {
    CHECK(!IsThinString(get(i), isolate));
  }
}

void ClassBoilerplate::ClassBoilerplateVerify(Isolate* isolate) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, kArgumentsCountOffset)));
  Object::VerifyPointer(isolate, static_properties_template());
  Object::VerifyPointer(isolate, static_elements_template());
  Object::VerifyPointer(isolate, static_computed_properties());
  CHECK(IsFixedArray(static_computed_properties()));
  Object::VerifyPointer(isolate, instance_properties_template());
  Object::VerifyPointer(isolate, instance_elements_template());
  Object::VerifyPointer(isolate, instance_computed_properties());
  CHECK(IsFixedArray(instance_computed_properties()));
}

void RegExpBoilerplateDescription::RegExpBoilerplateDescriptionVerify(
    Isolate* isolate) {
  {
    auto o = data(isolate);
    Object::VerifyPointer(isolate, o);
    CHECK(IsRegExpData(o));
  }
  {
    auto o = source();
    Object::VerifyPointer(isolate, o);
    CHECK(IsString(o));
  }
  CHECK(IsSmi(TaggedField<Object>::load(*this, kFlagsOffset)));
}

#if V8_ENABLE_WEBASSEMBLY

void WasmTrustedInstanceData::WasmTrustedInstanceDataVerify(Isolate* isolate) {
  // Check all tagged fields.
  for (uint16_t offset : kTaggedFieldOffsets) {
    VerifyObjectField(isolate, offset);
  }

  // Check all protected fields.
  for (uint16_t offset : kProtectedFieldOffsets) {
    VerifyProtectedPointerField(isolate, offset);
  }

  int num_dispatch_tables = dispatch_tables()->length();
  for (int i = 0; i < num_dispatch_tables; ++i) {
    Tagged<Object> table = dispatch_tables()->get(i);
    if (table == Smi::zero()) continue;
    CHECK(IsWasmDispatchTable(table));
    if (i == 0) CHECK_EQ(table, dispatch_table0());
  }
  if (num_dispatch_tables == 0) CHECK_EQ(0, dispatch_table0()->length());
}

void WasmDispatchTable::WasmDispatchTableVerify(Isolate* isolate) {
  TrustedObjectVerify(isolate);

  int len = length();
  CHECK_LE(len, capacity());
  for (int i = 0; i < len; ++i) {
    Tagged<Object> arg = implicit_arg(i);
    Object::VerifyPointer(isolate, arg);
    CHECK(IsWasmTrustedInstanceData(arg) || IsWasmImportData(arg) ||
          arg == Smi::zero());
    if (!v8_flags.wasm_jitless) {
      // call_target always null with the interpreter.
      CHECK_EQ(arg == Smi::zero(), target(i) == kNullAddress);
    }
  }
}

void WasmValueObject::WasmValueObjectVerify(Isolate* isolate) {
  JSObjectVerify(isolate);
  CHECK(IsWasmValueObject(*this));
}

void WasmExceptionPackage::WasmExceptionPackageVerify(Isolate* isolate) {
  JSObjectVerify(isolate);
  CHECK(IsWasmExceptionPackage(*this));
}

void WasmExportedFunctionData::WasmExportedFunctionDataVerify(
    Isolate* isolate) {
  TorqueGeneratedClassVerifiers::WasmExportedFunctionDataVerify(*this, isolate);
  Tagged<Code> wrapper = wrapper_code(isolate);
  CHECK(
      wrapper->kind() == CodeKind::JS_TO_WASM_FUNCTION ||
      wrapper->kind() == CodeKind::C_WASM_ENTRY ||
      (wrapper->is_builtin() &&
       (wrapper->builtin_id() == Builtin::kJSToWasmWrapper ||
#if V8_ENABLE_DRUMBRAKE
        wrapper->builtin_id() == Builtin::kGenericJSToWasmInterpreterWrapper ||
#endif  // V8_ENABLE_DRUMBRAKE
        wrapper->builtin_id() == Builtin::kWasmPromising)));
}

#endif  // V8_ENABLE_WEBASSEMBLY

void DataHandler::DataHandlerVerify(Isolate* isolate) {
  // Don't call TorqueGeneratedClassVerifiers::DataHandlerVerify because the
  // Torque definition of this class includes all of the optional fields.

  // This assertion exists to encourage updating this verification function if
  // new fields are added in the Torque class layout definition.
  static_assert(DataHandler::kHeaderSize == 6 * kTaggedSize);

  StructVerify(isolate);
  CHECK(IsDataHandler(*this));
  Object::VerifyPointer(isolate, smi_handler(isolate));
  CHECK_IMPLIES(!IsSmi(smi_handler()),
                IsStoreHandler(*this) && IsCode(smi_handler()));
  Object::VerifyPointer(isolate, validity_cell(isolate));
  CHECK(IsSmi(validity_cell()) || IsCell(validity_cell()));
  int data_count = data_field_count();
  if (data_count >= 1) {
    VerifyMaybeObjectField(isolate, kData1Offset);
  }
  if (data_count >= 2) {
    VerifyMaybeObjectField(isolate, kData2Offset);
  }
  if (data_count >= 3) {
    VerifyMaybeObjectField(isolate, kData3Offset);
  }
}

void LoadHandler::LoadHandlerVerify(Isolate* isolate) {
  DataHandler::DataHandlerVerify(isolate);
  // TODO(ishell): check handler integrity
}

void StoreHandler::StoreHandlerVerify(Isolate* isolate) {
  DataHandler::DataHandlerVerify(isolate);
  // TODO(ishell): check handler integrity
}

void AllocationSite::AllocationSiteVerify(Isolate* isolate) {
  CHECK(IsAllocationSite(*this));
  CHECK(IsDependentCode(dependent_code()));
  CHECK(IsSmi(transition_info_or_boilerplate()) ||
        IsJSObject(transition_info_or_boilerplate()));
  CHECK(IsAllocationSite(nested_site()) || nested_site() == Smi::zero());
}

void Script::ScriptVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::ScriptVerify(*this, isolate);
#if V8_ENABLE_WEBASSEMBLY
  if (type() == Script::Type::kWasm) {
    CHECK_EQ(line_ends(), ReadOnlyRoots(isolate).empty_fixed_array());
  } else {
    CHECK(CanHaveLineEnds());
  }
#else   // V8_ENABLE_WEBASSEMBLY
  CHECK(CanHaveLineEnds());
#endif  // V8_ENABLE_WEBASSEMBLY
  for (int i = 0; i < infos()->length(); ++i) {
    Tagged<MaybeObject> maybe_object = infos()->get(i);
    Tagged<HeapObject> heap_object;
    CHECK(!maybe_object.GetHeapObjectIfWeak(isolate, &heap_object) ||
          (maybe_object.GetHeapObjectIfStrong(&heap_object) &&
           IsUndefined(heap_object, isolate)) ||
          Is<SharedFunctionInfo>(heap_object) || Is<ScopeInfo>(heap_object));
  }
}

void NormalizedMapCache::NormalizedMapCacheVerify(Isolate* isolate) {
  Cast<WeakFixedArray>(*this)->WeakFixedArrayVerify(isolate);
  if (v8_flags.enable_slow_asserts) {
    for (int i = 0; i < length(); i++) {
      Tagged<MaybeObject> e = WeakFixedArray::get(i);
      Tagged<HeapObject> heap_object;
      if (e.GetHeapObjectIfWeak(&heap_object)) {
        Cast<Map>(heap_object)->DictionaryMapVerify(isolate);
      } else {
        CHECK(e.IsCleared() || (e.GetHeapObjectIfStrong(&heap_object) &&
                                IsUndefined(heap_object, isolate)));
      }
    }
  }
}

void PreparseData::PreparseDataVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::PreparseDataVerify(*this, isolate);
  CHECK_LE(0, data_length());
  CHECK_LE(0, children_length());

  for (int i = 0; i < children_length(); ++i) {
    Tagged<Object> child = get_child_raw(i);
    CHECK(IsNull(child) || IsPreparseData(child));
    Object::VerifyPointer(isolate, child);
  }
}

void CallSiteInfo::CallSiteInfoVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::CallSiteInfoVerify(*this, isolate);
#if V8_ENABLE_WEBASSEMBLY
  CHECK_IMPLIES(IsAsmJsWasm(), IsWasm());
  CHECK_IMPLIES(IsWasm(), IsWasmInstanceObject(receiver_or_instance()));
  CHECK_IMPLIES(IsWasm() || IsBuiltin(), IsSmi(function()));
  CHECK_IMPLIES(!IsWasm() && !IsBuiltin(), IsJSFunction(function()));
  CHECK_IMPLIES(IsAsync(), !IsWasm());
  CHECK_IMPLIES(IsConstructor(), !IsWasm());
#endif  // V8_ENABLE_WEBASSEMBLY
}

void FunctionTemplateRareData::FunctionTemplateRareDataVerify(
    Isolate* isolate) {
  CHECK(IsFixedArray(c_function_overloads()) ||
        IsUndefined(c_function_overloads(), isolate));
}

void StackFrameInfo::StackFrameInfoVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::StackFrameInfoVerify(*this, isolate);
}

void ErrorStackData::ErrorStackDataVerify(Isolate* isolate) {
  TorqueGeneratedClassVerifiers::ErrorStackDataVerify(*this, isolate);
  CHECK_IMPLIES(!IsFixedArray(call_site_infos_or_formatted_stack()),
                IsFixedArray(limit_or_stack_frame_infos()));
}

void SloppyArgumentsElements::SloppyArgumentsElementsVerify(Isolate* isolate) {
  FixedArrayBaseVerify(isolate);
  {
    auto o = context();
    Object::VerifyPointer(isolate, o);
    CHECK(IsContext(o));
  }
  {
    auto o = arguments();
    Object::VerifyPointer(isolate, o);
    CHECK(IsFixedArray(o));
  }
  for (int i = 0; i < length(); ++i) {
    auto o = mapped_entries(i, kRelaxedLoad);
    CHECK(IsSmi(o) || IsTheHole(o));
  }
}

// Helper class for verifying the string table.
class StringTableVerifier : public RootVisitor {
 public:
  explicit StringTableVerifier(Isolate* isolate) : isolate_(isolate) {}

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    UNREACHABLE();
  }
  void VisitRootPointers(Root root, const char* description,
                         OffHeapObjectSlot start,
                         OffHeapObjectSlot end) override {
    // Visit all HeapObject pointers in [start, end).
    for (OffHeapObjectSlot p = start; p < end; ++p) {
      Tagged<Object> o = p.load(isolate_);
      DCHECK(!HasWeakHeapObjectTag(o));
      if (IsHeapObject(o)) {
        Tagged<HeapObject> object = Cast<HeapObject>(o);
        // Check that the string is actually internalized.
        CHECK(IsInternalizedString(object));
      }
    }
  }

 private:
  Isolate* isolate_;
};

void StringTable::VerifyIfOwnedBy(Isolate* isolate) {
  DCHECK_EQ(isolate->string_table(), this);
  if (!isolate->OwnsStringTables()) return;
  StringTableVerifier verifier(isolate);
  IterateElements(&verifier);
}

#endif  // VERIFY_HEAP

#ifdef DEBUG

void JSObject::IncrementSpillStatistics(Isolate* isolate,
                                        SpillInformation* info) {
  info->number_of_objects_++;
  // Named properties
  if (HasFastProperties()) {
    info->number_of_objects_with_fast_properties_++;
    info->number_of_fast_used_fields_ += map()->NextFreePropertyIndex();
    info->number_of_fast_unused_fields_ += map()->UnusedPropertyFields();
  } else if (IsJSGlobalObject(*this)) {
    Tagged<GlobalDictionary> dict =
        Cast<JSGlobalObject>(*this)->global_dictionary(kAcquireLoad);
    info->number_of_slow_used_properties_ += dict->NumberOfElements();
    info->number_of_slow_unused_properties_ +=
        dict->Capacity() - dict->NumberOfElements();
  } else if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    Tagged<SwissNameDictionary> dict = property_dictionary_swiss();
    info->number_of_slow_used_properties_ += dict->NumberOfElements();
    info->number_of_slow_unused_properties_ +=
        dict->Capacity() - dict->NumberOfElements();
  } else {
    Tagged<NameDictionary> dict = property_dictionary();
    info->number_of_slow_used_properties_ += dict->NumberOfElements();
    info->number_of_slow_unused_properties_ +=
        dict->Capacity() - dict->NumberOfElements();
  }
  // Indexed properties
  switch (GetElementsKind()) {
    case HOLEY_SMI_ELEMENTS:
    case PACKED_SMI_ELEMENTS:
    case HOLEY_DOUBLE_ELEMENTS:
    case PACKED_DOUBLE_ELEMENTS:
    case HOLEY_ELEMENTS:
    case HOLEY_FROZEN_ELEMENTS:
    case HOLEY_SEALED_ELEMENTS:
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
    case PACKED_ELEMENTS:
    case PACKED_FROZEN_ELEMENTS:
    case PACKED_SEALED_ELEMENTS:
    case PACKED_NONEXTENSIBLE_ELEMENTS:
    case FAST_STRING_WRAPPER_ELEMENTS:
    case SHARED_ARRAY_ELEMENTS: {
      info->number_of_objects_with_fast_elements_++;
      int holes = 0;
      Tagged<FixedArray> e = Cast<FixedArray>(elements());
      int len = e->length();
      for (int i = 0; i < len; i++) {
        if (IsTheHole(e->get(i), isolate)) holes++;
      }
      info->number_of_fast_used_elements_ += len - holes;
      info->number_of_fast_unused_elements_ += holes;
      break;
    }

#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) case TYPE##_ELEMENTS:

      TYPED_ARRAYS(TYPED_ARRAY_CASE)
      RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
      {
        info->number_of_objects_with_fast_elements_++;
        Tagged<FixedArrayBase> e = Cast<FixedArrayBase>(elements());
        info->number_of_fast_used_elements_ += e->length();
        break;
      }
    case DICTIONARY_ELEMENTS:
    case SLOW_STRING_WRAPPER_ELEMENTS: {
      Tagged<NumberDictionary> dict = element_dictionary();
      info->number_of_slow_used_elements_ += dict->NumberOfElements();
      info->number_of_slow_unused_elements_ +=
          dict->Capacity() - dict->NumberOfElements();
      break;
    }
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
    case WASM_ARRAY_ELEMENTS:
    case NO_ELEMENTS:
      break;
  }
}

void JSObject::SpillInformation::Clear() {
  number_of_objects_ = 0;
  number_of_objects_with_fast_properties_ = 0;
  number_of_objects_with_fast_elements_ = 0;
  number_of_fast_used_fields_ = 0;
  number_of_fast_unused_fields_ = 0;
  number_of_slow_used_properties_ = 0;
  number_of_slow_unused_properties_ = 0;
  number_of_fast_used_elements_ = 0;
  number_of_fast_unused_elements_ = 0;
  number_of_slow_used_elements_ = 0;
  number_of_slow_unused_elements_ = 0;
}

void JSObject::SpillInformation::Print() {
  PrintF("\n  JSObject Spill Statistics (#%d):\n", number_of_objects_);

  PrintF("    - fast properties (#%d): %d (used) %d (unused)\n",
         number_of_objects_with_fast_properties_, number_of_fast_used_fields_,
         number_of_fast_unused_fields_);

  PrintF("    - slow properties (#%d): %d (used) %d (unused)\n",
         number_of_objects_ - number_of_objects_with_fast_properties_,
         number_of_slow_used_properties_, number_of_slow_unused_properties_);

  PrintF("    - fast elements (#%d): %d (used) %d (unused)\n",
         number_of_objects_with_fast_elements_, number_of_fast_used_elements_,
         number_of_fast_unused_elements_);

  PrintF("    - slow elements (#%d): %d (used) %d (unused)\n",
         number_of_objects_ - number_of_objects_with_fast_elements_,
         number_of_slow_used_elements_, number_of_slow_unused_elements_);

  PrintF("\n");
}

bool DescriptorArray::IsSortedNoDuplicates() {
  Tagged<Name> current_key;
  uint32_t current = 0;
  for (int i = 0; i < number_of_descriptors(); i++) {
    Tagged<Name> key = GetSortedKey(i);
    uint32_t hash;
    const bool has_hash = key->TryGetHash(&hash);
    CHECK(has_hash);
    if (key == current_key) {
      Print(*this);
      return false;
    }
    current_key = key;
    if (hash < current) {
      Print(*this);
      return false;
    }
    current = hash;
  }
  return true;
}

bool TransitionArray::IsSortedNoDuplicates() {
  Tagged<Name> prev_key;
  PropertyKind prev_kind = PropertyKind::kData;
  PropertyAttributes prev_attributes = NONE;
  uint32_t prev_hash = 0;

  for (int i = 0; i < number_of_transitions(); i++) {
    Tagged<Name> key = GetSortedKey(i);
    uint32_t hash;
    const bool has_hash = key->TryGetHash(&hash);
    CHECK(has_hash);
    PropertyKind kind = PropertyKind::kData;
    PropertyAttributes attributes = NONE;
    if (!TransitionsAccessor::IsSpecialTransition(key->GetReadOnlyRoots(),
                                                  key)) {
      Tagged<Map> target = GetTarget(i);
      PropertyDetails details =
          TransitionsAccessor::GetTargetDetails(key, target);
      kind = details.kind();
      attributes = details.attributes();
    } else {
      // Duplicate entries are not allowed for non-property transitions.
      DCHECK_NE(prev_key, key);
    }

    int cmp = CompareKeys(prev_key, prev_hash, prev_kind, prev_attributes, key,
                          hash, kind, attributes);
    if (cmp >= 0) {
      Print(*this);
      return false;
    }
    prev_key = key;
    prev_hash = hash;
    prev_attributes = attributes;
    prev_kind = kind;
  }
  return true;
}

bool TransitionsAccessor::IsSortedNoDuplicates() {
  // Simple and non-existent transitions are always sorted.
  if (encoding() != kFullTransitionArray) return true;
  return transitions()->IsSortedNoDuplicates();
}

static bool CheckOneBackPointer(Tagged<Map> current_map, Tagged<Map> target) {
  return target->GetBackPointer() == current_map;
}

bool TransitionsAccessor::IsConsistentWithBackPointers() {
  DisallowGarbageCollection no_gc;
  bool success = true;
  ReadOnlyRoots roots(isolate_);
  DCHECK_IMPLIES(map_->IsInobjectSlackTrackingInProgress(),
                 !HasSideStepTransitions());
  auto CheckTarget =
      [&](Tagged<Map> target) {
#ifdef DEBUG
        if (!map_->is_deprecated() && !target->is_deprecated()) {
          DCHECK_EQ(map_->IsInobjectSlackTrackingInProgress(),
                    target->IsInobjectSlackTrackingInProgress());
          // Check prototype transitions are first.
          DCHECK_IMPLIES(map_->prototype() != target->prototype(),
                         IsUndefined(map_->GetBackPointer()));
        }
#endif  // DEBUG
        if (!CheckOneBackPointer(map_, target)) {
          success = false;
        }
      };
  ForEachTransition(
      &no_gc, [&](Tagged<Map> target) { CheckTarget(target); },
      [&](Tagged<Map> proto_target) {
        if (v8_flags.move_prototype_transitions_first) {
          CheckTarget(proto_target);
        }
      },
      [&](Tagged<Object> side_step) {
        if (!side_step.IsSmi()) {
          DCHECK(!Cast<Map>(side_step)->IsInobjectSlackTrackingInProgress());
          DCHECK_EQ(
              Cast<Map>(side_step)->GetInObjectProperties() -
                  Cast<Map>(side_step)->UnusedInObjectProperties(),
              map_->GetInObjectProperties() - map_->UnusedInObjectProperties());
        }
      });
  return success;
}

#undef USE_TORQUE_VERIFIER

#endif  // DEBUG

}  // namespace internal
}  // namespace v8
