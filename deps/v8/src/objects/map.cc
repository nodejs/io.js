// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/map.h"

#include <optional>

#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/execution/frames.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/handles/maybe-handles.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/init/bootstrapper.h"
#include "src/logging/log.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/arguments-inl.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/elements-kind.h"
#include "src/objects/field-type.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-objects.h"
#include "src/objects/map-updater.h"
#include "src/objects/maybe-object.h"
#include "src/objects/oddball.h"
#include "src/objects/property.h"
#include "src/objects/transitions-inl.h"
#include "src/roots/roots.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-containers.h"

namespace v8::internal {

Tagged<Map> Map::GetPrototypeChainRootMap(Isolate* isolate) const {
  DisallowGarbageCollection no_alloc;
  if (IsJSReceiverMap(*this)) {
    return *this;
  }
  int constructor_function_index = GetConstructorFunctionIndex();
  if (constructor_function_index != Map::kNoConstructorFunctionIndex) {
    Tagged<Context> native_context = isolate->context()->native_context();
    Tagged<JSFunction> constructor_function =
        Cast<JSFunction>(native_context->get(constructor_function_index));
    return constructor_function->initial_map();
  }
  return ReadOnlyRoots(isolate).null_value()->map();
}

// static
std::optional<Tagged<JSFunction>> Map::GetConstructorFunction(
    Tagged<Map> map, Tagged<Context> native_context) {
  DisallowGarbageCollection no_gc;
  if (IsPrimitiveMap(map)) {
    int const constructor_function_index = map->GetConstructorFunctionIndex();
    if (constructor_function_index != kNoConstructorFunctionIndex) {
      return Cast<JSFunction>(native_context->get(constructor_function_index));
    }
  }
  return {};
}

VisitorId Map::GetVisitorId(Tagged<Map> map) {
  static_assert(kVisitorIdCount <= 256);

  const int instance_type = map->instance_type();

  if (instance_type < FIRST_NONSTRING_TYPE) {
    switch (instance_type & kStringRepresentationMask) {
      case kSeqStringTag:
        if ((instance_type & kStringEncodingMask) == kOneByteStringTag) {
          return kVisitSeqOneByteString;
        } else {
          return kVisitSeqTwoByteString;
        }

      case kConsStringTag:
        if (IsShortcutCandidate(instance_type)) {
          return kVisitShortcutCandidate;
        } else {
          return kVisitConsString;
        }

      case kSlicedStringTag:
        return kVisitSlicedString;

      case kExternalStringTag:
        return kVisitExternalString;

      case kThinStringTag:
        return kVisitThinString;
    }
    UNREACHABLE();
  }

  if (InstanceTypeChecker::IsJSApiObject(map->instance_type())) {
    return kVisitJSApiObject;
  }

  switch (instance_type) {
    case EXTERNAL_POINTER_ARRAY_TYPE:
      return kVisitExternalPointerArray;

    case FILLER_TYPE:
      return kVisitFiller;
    case FREE_SPACE_TYPE:
      return kVisitFreeSpace;

    case EMBEDDER_DATA_ARRAY_TYPE:
      return kVisitEmbedderDataArray;

    case NAME_TO_INDEX_HASH_TABLE_TYPE:
    case REGISTERED_SYMBOL_TABLE_TYPE:
    case HASH_TABLE_TYPE:
    case ORDERED_HASH_MAP_TYPE:
    case ORDERED_HASH_SET_TYPE:
    case ORDERED_NAME_DICTIONARY_TYPE:
    case NAME_DICTIONARY_TYPE:
    case GLOBAL_DICTIONARY_TYPE:
    case NUMBER_DICTIONARY_TYPE:
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
      return kVisitFixedArray;

    case SLOPPY_ARGUMENTS_ELEMENTS_TYPE:
      return kVisitSloppyArgumentsElements;

    case AWAIT_CONTEXT_TYPE:
    case BLOCK_CONTEXT_TYPE:
    case CATCH_CONTEXT_TYPE:
    case DEBUG_EVALUATE_CONTEXT_TYPE:
    case EVAL_CONTEXT_TYPE:
    case FUNCTION_CONTEXT_TYPE:
    case MODULE_CONTEXT_TYPE:
    case SCRIPT_CONTEXT_TYPE:
    case WITH_CONTEXT_TYPE:
      return kVisitContext;

    case NATIVE_CONTEXT_TYPE:
      return kVisitNativeContext;

    case EPHEMERON_HASH_TABLE_TYPE:
      return kVisitEphemeronHashTable;

    case PROPERTY_ARRAY_TYPE:
      return kVisitPropertyArray;

    case FEEDBACK_CELL_TYPE:
      return kVisitFeedbackCell;

    case FEEDBACK_METADATA_TYPE:
      return kVisitFeedbackMetadata;

    case ODDBALL_TYPE:
      return kVisitOddball;

    case HOLE_TYPE:
      return kVisitHole;

    case MAP_TYPE:
      return kVisitMap;

    case CELL_TYPE:
      return kVisitCell;

    case PROPERTY_CELL_TYPE:
      return kVisitPropertyCell;

    case CONST_TRACKING_LET_CELL_TYPE:
      return kVisitConstTrackingLetCell;

    case TRANSITION_ARRAY_TYPE:
      return kVisitTransitionArray;

    case JS_WEAK_MAP_TYPE:
    case JS_WEAK_SET_TYPE:
      return kVisitJSWeakCollection;

    case ACCESSOR_INFO_TYPE:
      return kVisitAccessorInfo;

    case FUNCTION_TEMPLATE_INFO_TYPE:
      return kVisitFunctionTemplateInfo;

    case OBJECT_TEMPLATE_INFO_TYPE:
      return kVisitStruct;

    case JS_PROXY_TYPE:
      return kVisitStruct;

    case SYMBOL_TYPE:
      return kVisitSymbol;

    case JS_ARRAY_BUFFER_TYPE:
      return kVisitJSArrayBuffer;

    case JS_DATA_VIEW_TYPE:
    case JS_RAB_GSAB_DATA_VIEW_TYPE:
      return kVisitJSDataViewOrRabGsabDataView;

    case JS_EXTERNAL_OBJECT_TYPE:
      return kVisitJSExternalObject;

    case JS_FUNCTION_TYPE:
    case JS_CLASS_CONSTRUCTOR_TYPE:
    case JS_PROMISE_CONSTRUCTOR_TYPE:
    case JS_REG_EXP_CONSTRUCTOR_TYPE:
    case JS_ARRAY_CONSTRUCTOR_TYPE:
#define TYPED_ARRAY_CONSTRUCTORS_SWITCH(Type, type, TYPE, Ctype) \
  case TYPE##_TYPED_ARRAY_CONSTRUCTOR_TYPE:
      TYPED_ARRAYS(TYPED_ARRAY_CONSTRUCTORS_SWITCH)
#undef TYPED_ARRAY_CONSTRUCTORS_SWITCH
      return kVisitJSFunction;

    case JS_TYPED_ARRAY_TYPE:
      return kVisitJSTypedArray;

    case SMALL_ORDERED_HASH_MAP_TYPE:
      return kVisitSmallOrderedHashMap;

    case SMALL_ORDERED_HASH_SET_TYPE:
      return kVisitSmallOrderedHashSet;

    case SMALL_ORDERED_NAME_DICTIONARY_TYPE:
      return kVisitSmallOrderedNameDictionary;

    case SWISS_NAME_DICTIONARY_TYPE:
      return kVisitSwissNameDictionary;

    case SHARED_FUNCTION_INFO_TYPE:
      return kVisitSharedFunctionInfo;

    case PREPARSE_DATA_TYPE:
      return kVisitPreparseData;

    case COVERAGE_INFO_TYPE:
      return kVisitCoverageInfo;

    // Objects that may have embedder fields but otherwise are just a regular
    // JSObject.
    case JS_PROMISE_TYPE: {
      const bool has_raw_data_fields =
          COMPRESS_POINTERS_BOOL && JSObject::GetEmbedderFieldCount(map) > 0;
      return has_raw_data_fields ? kVisitJSObject : kVisitJSObjectFast;
    }

    // Objects that are guaranteed to not have any embedder fields and just
    // behave like regular JSObject.
    case JS_ARGUMENTS_OBJECT_TYPE:
    case JS_ARRAY_ITERATOR_PROTOTYPE_TYPE:
    case JS_ARRAY_ITERATOR_TYPE:
    case JS_ARRAY_TYPE:
    case JS_ASYNC_DISPOSABLE_STACK_TYPE:
    case JS_ASYNC_FROM_SYNC_ITERATOR_TYPE:
    case JS_ASYNC_FUNCTION_OBJECT_TYPE:
    case JS_ASYNC_GENERATOR_OBJECT_TYPE:
    case JS_CONTEXT_EXTENSION_OBJECT_TYPE:
    case JS_DISPOSABLE_STACK_BASE_TYPE:
    case JS_ERROR_TYPE:
    case JS_GENERATOR_OBJECT_TYPE:
    case JS_ITERATOR_FILTER_HELPER_TYPE:
    case JS_ITERATOR_MAP_HELPER_TYPE:
    case JS_ITERATOR_TAKE_HELPER_TYPE:
    case JS_ITERATOR_DROP_HELPER_TYPE:
    case JS_ITERATOR_FLAT_MAP_HELPER_TYPE:
    case JS_ITERATOR_PROTOTYPE_TYPE:
    case JS_MAP_ITERATOR_PROTOTYPE_TYPE:
    case JS_MAP_KEY_ITERATOR_TYPE:
    case JS_MAP_KEY_VALUE_ITERATOR_TYPE:
    case JS_MAP_TYPE:
    case JS_MAP_VALUE_ITERATOR_TYPE:
    case JS_MESSAGE_OBJECT_TYPE:
    case JS_MODULE_NAMESPACE_TYPE:
    case JS_OBJECT_PROTOTYPE_TYPE:
    case JS_OBJECT_TYPE:
    case JS_PRIMITIVE_WRAPPER_TYPE:
    case JS_PROMISE_PROTOTYPE_TYPE:
    case JS_REG_EXP_PROTOTYPE_TYPE:
    case JS_REG_EXP_STRING_ITERATOR_TYPE:
    case JS_SET_ITERATOR_PROTOTYPE_TYPE:
    case JS_SET_KEY_VALUE_ITERATOR_TYPE:
    case JS_SET_PROTOTYPE_TYPE:
    case JS_SET_TYPE:
    case JS_SET_VALUE_ITERATOR_TYPE:
    case JS_SYNC_DISPOSABLE_STACK_TYPE:
    case JS_SHADOW_REALM_TYPE:
    case JS_SHARED_ARRAY_TYPE:
    case JS_SHARED_STRUCT_TYPE:
    case JS_STRING_ITERATOR_PROTOTYPE_TYPE:
    case JS_STRING_ITERATOR_TYPE:
    case JS_TEMPORAL_CALENDAR_TYPE:
    case JS_TEMPORAL_DURATION_TYPE:
    case JS_TEMPORAL_INSTANT_TYPE:
    case JS_TEMPORAL_PLAIN_DATE_TYPE:
    case JS_TEMPORAL_PLAIN_DATE_TIME_TYPE:
    case JS_TEMPORAL_PLAIN_MONTH_DAY_TYPE:
    case JS_TEMPORAL_PLAIN_TIME_TYPE:
    case JS_TEMPORAL_PLAIN_YEAR_MONTH_TYPE:
    case JS_TEMPORAL_TIME_ZONE_TYPE:
    case JS_TEMPORAL_ZONED_DATE_TIME_TYPE:
    case JS_TYPED_ARRAY_PROTOTYPE_TYPE:
    case JS_VALID_ITERATOR_WRAPPER_TYPE:
    case JS_RAW_JSON_TYPE:
#ifdef V8_INTL_SUPPORT
    case JS_V8_BREAK_ITERATOR_TYPE:
    case JS_COLLATOR_TYPE:
    case JS_DATE_TIME_FORMAT_TYPE:
    case JS_DISPLAY_NAMES_TYPE:
    case JS_DURATION_FORMAT_TYPE:
    case JS_LIST_FORMAT_TYPE:
    case JS_LOCALE_TYPE:
    case JS_NUMBER_FORMAT_TYPE:
    case JS_PLURAL_RULES_TYPE:
    case JS_RELATIVE_TIME_FORMAT_TYPE:
    case JS_SEGMENT_ITERATOR_TYPE:
    case JS_SEGMENTER_TYPE:
    case JS_SEGMENTS_TYPE:
#endif  // V8_INTL_SUPPORT
#if V8_ENABLE_WEBASSEMBLY
    case WASM_EXCEPTION_PACKAGE_TYPE:
    case WASM_MEMORY_OBJECT_TYPE:
    case WASM_MODULE_OBJECT_TYPE:
    case WASM_VALUE_OBJECT_TYPE:
#endif  // V8_ENABLE_WEBASSEMBLY
    case JS_BOUND_FUNCTION_TYPE:
    case JS_WRAPPED_FUNCTION_TYPE: {
      CHECK_EQ(0, JSObject::GetEmbedderFieldCount(map));
      return kVisitJSObjectFast;
    }
    case JS_REG_EXP_TYPE:
      return kVisitJSRegExp;

    // Objects that are used as API wrapper objects and can have embedder
    // fields. Note that there's more of these kinds (e.g. JS_ARRAY_BUFFER_TYPE)
    // but they have their own visitor id for other reasons
    case JS_API_OBJECT_TYPE:
    case JS_GLOBAL_PROXY_TYPE:
    case JS_GLOBAL_OBJECT_TYPE:
    case JS_SPECIAL_API_OBJECT_TYPE:
      return kVisitJSApiObject;

    case JS_DATE_TYPE:
      return kVisitJSDate;

    case JS_WEAK_REF_TYPE:
      return kVisitJSWeakRef;

    case WEAK_CELL_TYPE:
      return kVisitWeakCell;

    case JS_FINALIZATION_REGISTRY_TYPE:
      return kVisitJSFinalizationRegistry;

    case JS_ATOMICS_MUTEX_TYPE:
    case JS_ATOMICS_CONDITION_TYPE:
      return kVisitJSSynchronizationPrimitive;

    case HEAP_NUMBER_TYPE:
      return kVisitHeapNumber;

    case FOREIGN_TYPE:
      return kVisitForeign;

    case BIGINT_TYPE:
      return kVisitBigInt;

    case ALLOCATION_SITE_TYPE:
      return kVisitAllocationSite;

#define MAKE_STRUCT_CASE(TYPE, Name, name) case TYPE:
      STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE
      if (instance_type == PROTOTYPE_INFO_TYPE) {
        return kVisitPrototypeInfo;
      }
      if (instance_type == DEBUG_INFO_TYPE) {
        return kVisitDebugInfo;
      }
      if (instance_type == CALL_SITE_INFO_TYPE) {
        return kVisitCallSiteInfo;
      }
      if (instance_type == BYTECODE_WRAPPER_TYPE) {
        return kVisitBytecodeWrapper;
      }
      if (instance_type == CODE_WRAPPER_TYPE) {
        return kVisitCodeWrapper;
      }
      if (instance_type == INTERPRETER_DATA_TYPE) {
        return kVisitInterpreterData;
      }
      if (instance_type == REG_EXP_BOILERPLATE_DESCRIPTION_TYPE) {
        return kVisitRegExpBoilerplateDescription;
      }
      if (instance_type == REG_EXP_DATA_WRAPPER_TYPE) {
        return kVisitRegExpDataWrapper;
      }
      return kVisitStruct;

    case LOAD_HANDLER_TYPE:
    case STORE_HANDLER_TYPE:
      return kVisitDataHandler;

    case SOURCE_TEXT_MODULE_TYPE:
      return kVisitSourceTextModule;
    case SYNTHETIC_MODULE_TYPE:
      return kVisitSyntheticModule;

#if V8_ENABLE_WEBASSEMBLY
    case WASM_INSTANCE_OBJECT_TYPE:
      return kVisitWasmInstanceObject;
    case WASM_ARRAY_TYPE:
      return kVisitWasmArray;
    case WASM_STRUCT_TYPE:
      return kVisitWasmStruct;
    case WASM_CONTINUATION_OBJECT_TYPE:
      return kVisitWasmContinuationObject;
    case WASM_TYPE_INFO_TYPE:
      return kVisitWasmTypeInfo;
    case WASM_RESUME_DATA_TYPE:
      return kVisitWasmResumeData;
    case WASM_FUNC_REF_TYPE:
      return kVisitWasmFuncRef;
    case WASM_TAG_OBJECT_TYPE:
      return kVisitWasmTagObject;
    case WASM_TABLE_OBJECT_TYPE:
      return kVisitWasmTableObject;
    case WASM_GLOBAL_OBJECT_TYPE:
      return kVisitWasmGlobalObject;
    case WASM_SUSPENDER_OBJECT_TYPE:
      return kVisitWasmSuspenderObject;
    case WASM_SUSPENDING_OBJECT_TYPE:
      return kVisitWasmSuspendingObject;
    case WASM_NULL_TYPE:
      return kVisitWasmNull;
#endif  // V8_ENABLE_WEBASSEMBLY

#define MAKE_TQ_CASE(TYPE, Name) \
  case TYPE:                     \
    return kVisit##Name;
      TORQUE_INSTANCE_TYPE_TO_BODY_DESCRIPTOR_LIST(MAKE_TQ_CASE)
#undef MAKE_TQ_CASE

#define CASE(TypeCamelCase, TYPE_UPPER_CASE) \
  case TYPE_UPPER_CASE##_TYPE:               \
    return kVisit##TypeCamelCase;
      SIMPLE_HEAP_OBJECT_LIST2(CASE)
      CONCRETE_TRUSTED_OBJECT_TYPE_LIST2(CASE)
#undef CASE
  }
  UNREACHABLE();
}

// static
MaybeObjectHandle Map::WrapFieldType(Handle<FieldType> type) {
  if (IsClass(*type)) {
    return MaybeObjectHandle::Weak(FieldType::AsClass(type));
  }
  return MaybeObjectHandle(type);
}

// static
Tagged<FieldType> Map::UnwrapFieldType(Tagged<MaybeObject> wrapped_type) {
  DCHECK(!wrapped_type.IsCleared());
  Tagged<HeapObject> heap_object;
  if (wrapped_type.GetHeapObjectIfWeak(&heap_object)) {
    return Cast<FieldType>(heap_object);
  }
  return Cast<FieldType>(wrapped_type);
}

MaybeHandle<Map> Map::CopyWithField(Isolate* isolate, Handle<Map> map,
                                    Handle<Name> name, Handle<FieldType> type,
                                    PropertyAttributes attributes,
                                    PropertyConstness constness,
                                    Representation representation,
                                    TransitionFlag flag) {
  DCHECK(map->instance_descriptors(isolate)
             ->Search(*name, map->NumberOfOwnDescriptors())
             .is_not_found());

  // Ensure the descriptor array does not get too big.
  if (map->NumberOfOwnDescriptors() >= kMaxNumberOfDescriptors) {
    return MaybeHandle<Map>();
  }

  // Compute the new index for new field.
  int index = map->NextFreePropertyIndex();

  if (map->instance_type() == JS_CONTEXT_EXTENSION_OBJECT_TYPE) {
    constness = PropertyConstness::kMutable;
    representation = Representation::Tagged();
    type = FieldType::Any(isolate);
  } else {
    Map::GeneralizeIfCanHaveTransitionableFastElementsKind(
        isolate, map->instance_type(), &representation, &type);
  }

  MaybeObjectHandle wrapped_type = WrapFieldType(type);

  Descriptor d = Descriptor::DataField(name, index, attributes, constness,
                                       representation, wrapped_type);
  Handle<Map> new_map = Map::CopyAddDescriptor(isolate, map, &d, flag);
  new_map->AccountAddedPropertyField();
  return new_map;
}

MaybeHandle<Map> Map::CopyWithConstant(Isolate* isolate, Handle<Map> map,
                                       Handle<Name> name,
                                       DirectHandle<Object> constant,
                                       PropertyAttributes attributes,
                                       TransitionFlag flag) {
  // Ensure the descriptor array does not get too big.
  if (map->NumberOfOwnDescriptors() >= kMaxNumberOfDescriptors) {
    return MaybeHandle<Map>();
  }

  Representation representation =
      Object::OptimalRepresentation(*constant, isolate);
  Handle<FieldType> type =
      Object::OptimalType(*constant, isolate, representation);
  return CopyWithField(isolate, map, name, type, attributes,
                       PropertyConstness::kConst, representation, flag);
}

bool Map::InstancesNeedRewriting(Tagged<Map> target,
                                 ConcurrencyMode cmode) const {
  int target_number_of_fields = target->NumberOfFields(cmode);
  int target_inobject = target->GetInObjectProperties();
  int target_unused = target->UnusedPropertyFields();
  int old_number_of_fields;

  return InstancesNeedRewriting(target, target_number_of_fields,
                                target_inobject, target_unused,
                                &old_number_of_fields, cmode);
}

bool Map::InstancesNeedRewriting(Tagged<Map> target,
                                 int target_number_of_fields,
                                 int target_inobject, int target_unused,
                                 int* old_number_of_fields,
                                 ConcurrencyMode cmode) const {
  // If fields were added (or removed), rewrite the instance.
  *old_number_of_fields = NumberOfFields(cmode);
  DCHECK(target_number_of_fields >= *old_number_of_fields);
  if (target_number_of_fields != *old_number_of_fields) return true;

  // If smi descriptors were replaced by double descriptors, rewrite.
  Tagged<DescriptorArray> old_desc = IsConcurrent(cmode)
                                         ? instance_descriptors(kAcquireLoad)
                                         : instance_descriptors();
  Tagged<DescriptorArray> new_desc =
      IsConcurrent(cmode) ? target->instance_descriptors(kAcquireLoad)
                          : target->instance_descriptors();
  for (InternalIndex i : IterateOwnDescriptors()) {
    if (new_desc->GetDetails(i).representation().IsDouble() !=
        old_desc->GetDetails(i).representation().IsDouble()) {
      return true;
    }
  }

  // If no fields were added, and no inobject properties were removed, setting
  // the map is sufficient.
  if (target_inobject == GetInObjectProperties()) return false;
  // In-object slack tracking may have reduced the object size of the new map.
  // In that case, succeed if all existing fields were inobject, and they still
  // fit within the new inobject size.
  DCHECK(target_inobject < GetInObjectProperties());
  if (target_number_of_fields <= target_inobject) {
    DCHECK(target_number_of_fields + target_unused == target_inobject);
    return false;
  }
  // Otherwise, properties will need to be moved to the backing store.
  return true;
}

int Map::NumberOfFields(ConcurrencyMode cmode) const {
  Tagged<DescriptorArray> descriptors = IsConcurrent(cmode)
                                            ? instance_descriptors(kAcquireLoad)
                                            : instance_descriptors();
  int result = 0;
  for (InternalIndex i : IterateOwnDescriptors()) {
    if (descriptors->GetDetails(i).location() == PropertyLocation::kField)
      result++;
  }
  return result;
}

Map::FieldCounts Map::GetFieldCounts() const {
  Tagged<DescriptorArray> descriptors = instance_descriptors();
  int mutable_count = 0;
  int const_count = 0;
  for (InternalIndex i : IterateOwnDescriptors()) {
    PropertyDetails details = descriptors->GetDetails(i);
    if (details.location() == PropertyLocation::kField) {
      switch (details.constness()) {
        case PropertyConstness::kMutable:
          mutable_count++;
          break;
        case PropertyConstness::kConst:
          const_count++;
          break;
      }
    }
  }
  return FieldCounts(mutable_count, const_count);
}

void Map::DeprecateTransitionTree(Isolate* isolate) {
  if (is_deprecated()) return;
  DisallowGarbageCollection no_gc;
  ReadOnlyRoots roots(isolate);
  TransitionsAccessor transitions(isolate, *this);
  transitions.ForEachTransition(
      &no_gc, [&](Tagged<Map> map) { map->DeprecateTransitionTree(isolate); },
      [&](Tagged<Map> map) {
        if (v8_flags.move_prototype_transitions_first) {
          map->DeprecateTransitionTree(isolate);
        }
      },
      nullptr);
  DCHECK(!IsFunctionTemplateInfo(constructor_or_back_pointer()));
  DCHECK(CanBeDeprecated());
  set_is_deprecated(true);
  if (v8_flags.log_maps) {
    LOG(isolate, MapEvent("Deprecate", handle(*this, isolate), Handle<Map>()));
  }
  DependentCode::DeoptimizeDependencyGroups(isolate, *this,
                                            DependentCode::kTransitionGroup);
  NotifyLeafMapLayoutChange(isolate);
}

// Installs |new_descriptors| over the current instance_descriptors to ensure
// proper sharing of descriptor arrays.
void Map::ReplaceDescriptors(Isolate* isolate,
                             Tagged<DescriptorArray> new_descriptors) {
  PtrComprCageBase cage_base(isolate);
  // Don't overwrite the empty descriptor array or initial map's descriptors.
  if (NumberOfOwnDescriptors() == 0 ||
      IsUndefined(GetBackPointer(cage_base), isolate)) {
    return;
  }

  Tagged<DescriptorArray> to_replace = instance_descriptors(cage_base);
  // Replace descriptors by new_descriptors in all maps that share it. The old
  // descriptors will not be trimmed in the mark-compactor, we need to mark
  // all its elements.
  Tagged<Map> current = *this;
#ifndef V8_DISABLE_WRITE_BARRIERS
  WriteBarrier::Marking(to_replace, to_replace->number_of_descriptors());
#endif
  while (current->instance_descriptors(cage_base) == to_replace) {
    Tagged<Map> next;
    if (!current->TryGetBackPointer(cage_base, &next)) {
      break;  // Stop overwriting at initial map.
    }
    current->SetEnumLength(kInvalidEnumCacheSentinel);
    current->UpdateDescriptors(isolate, new_descriptors,
                               current->NumberOfOwnDescriptors());
    current = next;
  }
  set_owns_descriptors(false);
}

Tagged<Map> Map::FindRootMap(PtrComprCageBase cage_base) const {
  DisallowGarbageCollection no_gc;
  Tagged<Map> result = *this;
  while (true) {
    Tagged<Map> parent;
    if (!result->TryGetBackPointer(cage_base, &parent)) {
      // Initial map must not contain descriptors in the descriptors array
      // that do not belong to the map.
      DCHECK_LE(result->NumberOfOwnDescriptors(),
                result->instance_descriptors(cage_base, kRelaxedLoad)
                    ->number_of_descriptors());
      return result;
    }
    result = parent;
  }
}

Tagged<Map> Map::FindFieldOwner(PtrComprCageBase cage_base,
                                InternalIndex descriptor) const {
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(PropertyLocation::kField,
            instance_descriptors(cage_base, kRelaxedLoad)
                ->GetDetails(descriptor)
                .location());
  Tagged<Map> result = *this;
  while (true) {
    Tagged<Map> parent;
    if (!result->TryGetBackPointer(cage_base, &parent)) break;
    if (parent->NumberOfOwnDescriptors() <= descriptor.as_int()) break;
    result = parent;
  }
  return result;
}

namespace {

Tagged<Map> SearchMigrationTarget(Isolate* isolate, Tagged<Map> old_map) {
  DisallowGarbageCollection no_gc;

  Tagged<Map> target = old_map;
  do {
    target = TransitionsAccessor(isolate, target).GetMigrationTarget();
  } while (!target.is_null() && target->is_deprecated());
  if (target.is_null()) return Map();

  SLOW_DCHECK(MapUpdater::TryUpdateNoLock(
                  isolate, old_map, ConcurrencyMode::kSynchronous) == target);
  return target;
}
}  // namespace

// static
MaybeHandle<Map> Map::TryUpdate(Isolate* isolate, Handle<Map> old_map) {
  DisallowGarbageCollection no_gc;
  DisallowDeoptimization no_deoptimization(isolate);

  if (!old_map->is_deprecated()) return old_map;

  if (v8_flags.fast_map_update) {
    Tagged<Map> target_map = SearchMigrationTarget(isolate, *old_map);
    if (!target_map.is_null()) {
      return handle(target_map, isolate);
    }
  }

  std::optional<Tagged<Map>> new_map = MapUpdater::TryUpdateNoLock(
      isolate, *old_map, ConcurrencyMode::kSynchronous);
  if (!new_map.has_value()) return MaybeHandle<Map>();
  if (v8_flags.fast_map_update) {
    TransitionsAccessor::SetMigrationTarget(isolate, old_map, new_map.value());
  }
  return handle(new_map.value(), isolate);
}

Tagged<Map> Map::TryReplayPropertyTransitions(Isolate* isolate,
                                              Tagged<Map> old_map,
                                              ConcurrencyMode cmode) {
  DisallowGarbageCollection no_gc;

  const int root_nof = NumberOfOwnDescriptors();
  const int old_nof = old_map->NumberOfOwnDescriptors();
  // TODO(jgruber,chromium:1239009): The main thread should use non-atomic
  // reads, but this currently leads to odd behavior (see the linked bug).
  // Investigate and fix this properly. Also below and in called functions.
  Tagged<DescriptorArray> old_descriptors =
      old_map->instance_descriptors(isolate, kAcquireLoad);

  Tagged<Map> new_map = *this;
  for (InternalIndex i : InternalIndex::Range(root_nof, old_nof)) {
    PropertyDetails old_details = old_descriptors->GetDetails(i);
    Tagged<Map> transition =
        TransitionsAccessor(isolate, new_map, IsConcurrent(cmode))
            .SearchTransition(old_descriptors->GetKey(i), old_details.kind(),
                              old_details.attributes());
    if (transition.is_null()) return Map();
    new_map = transition;
    Tagged<DescriptorArray> new_descriptors =
        new_map->instance_descriptors(isolate, kAcquireLoad);

    PropertyDetails new_details = new_descriptors->GetDetails(i);
    DCHECK_EQ(old_details.kind(), new_details.kind());
    DCHECK_EQ(old_details.attributes(), new_details.attributes());
    if (!IsGeneralizableTo(old_details.constness(), new_details.constness())) {
      return Map();
    }
    DCHECK(IsGeneralizableTo(old_details.location(), new_details.location()));
    if (!old_details.representation().fits_into(new_details.representation())) {
      return Map();
    }
    if (new_details.location() == PropertyLocation::kField) {
      if (new_details.kind() == PropertyKind::kData) {
        Tagged<FieldType> new_type = new_descriptors->GetFieldType(i);
        DCHECK_EQ(PropertyKind::kData, old_details.kind());
        DCHECK_EQ(PropertyLocation::kField, old_details.location());
        Tagged<FieldType> old_type = old_descriptors->GetFieldType(i);
        if (!FieldType::NowIs(old_type, new_type)) {
          return Map();
        }
      } else {
        DCHECK_EQ(PropertyKind::kAccessor, new_details.kind());
#ifdef DEBUG
        Tagged<FieldType> new_type = new_descriptors->GetFieldType(i);
        DCHECK(IsAny(new_type));
#endif
        UNREACHABLE();
      }
    } else {
      DCHECK_EQ(PropertyLocation::kDescriptor, new_details.location());
      if (old_details.location() == PropertyLocation::kField ||
          old_descriptors->GetStrongValue(i) !=
              new_descriptors->GetStrongValue(i)) {
        return Map();
      }
    }
  }
  if (new_map->NumberOfOwnDescriptors() != old_nof) return Map();
  return new_map;
}

// static
Handle<Map> Map::Update(Isolate* isolate, Handle<Map> map) {
  if (!map->is_deprecated()) return map;
  if (v8_flags.fast_map_update) {
    Tagged<Map> target_map = SearchMigrationTarget(isolate, *map);
    if (!target_map.is_null()) {
      return handle(target_map, isolate);
    }
  }
  MapUpdater mu(isolate, map);
  return mu.Update();
}

void Map::EnsureDescriptorSlack(Isolate* isolate, DirectHandle<Map> map,
                                int slack) {
  // Only supports adding slack to owned descriptors.
  CHECK(map->owns_descriptors());

  DirectHandle<DescriptorArray> descriptors(map->instance_descriptors(isolate),
                                            isolate);
  int old_size = map->NumberOfOwnDescriptors();
  if (slack <= descriptors->number_of_slack_descriptors()) return;

  DirectHandle<DescriptorArray> new_descriptors =
      DescriptorArray::CopyUpTo(isolate, descriptors, old_size, slack);

  DisallowGarbageCollection no_gc;
  if (old_size == 0) {
    map->UpdateDescriptors(isolate, *new_descriptors,
                           map->NumberOfOwnDescriptors());
    return;
  }

  // If the source descriptors had an enum cache we copy it. This ensures
  // that the maps to which we push the new descriptor array back can rely
  // on a cache always being available once it is set. If the map has more
  // enumerated descriptors than available in the original cache, the cache
  // will be lazily replaced by the extended cache when needed.
  new_descriptors->CopyEnumCacheFrom(*descriptors);

  // Replace descriptors by new_descriptors in all maps that share it. The old
  // descriptors will not be trimmed in the mark-compactor, we need to mark
  // all its elements.
#ifndef V8_DISABLE_WRITE_BARRIERS
  WriteBarrier::Marking(*descriptors, descriptors->number_of_descriptors());
#endif

  // Update the descriptors from {map} (inclusive) until the initial map
  // (exclusive). In the case that {map} is the initial map, update it.
  map->UpdateDescriptors(isolate, *new_descriptors,
                         map->NumberOfOwnDescriptors());
  Tagged<Object> next = map->GetBackPointer();
  if (IsUndefined(next, isolate)) return;

  Tagged<Map> current = Cast<Map>(next);
  while (current->instance_descriptors(isolate) == *descriptors) {
    next = current->GetBackPointer();
    if (IsUndefined(next, isolate)) break;
    current->UpdateDescriptors(isolate, *new_descriptors,
                               current->NumberOfOwnDescriptors());
    current = Cast<Map>(next);
  }
}

// static
Handle<Map> Map::GetObjectCreateMap(Isolate* isolate,
                                    Handle<HeapObject> prototype) {
  Handle<Map> map(isolate->native_context()->object_function()->initial_map(),
                  isolate);
  if (map->prototype() == *prototype) return map;
  if (IsNull(*prototype, isolate)) {
    return isolate->slow_object_with_null_prototype_map();
  }
  if (IsJSObjectThatCanBeTrackedAsPrototype(*prototype)) {
    DirectHandle<JSObject> js_prototype = Cast<JSObject>(prototype);
    if (!js_prototype->map()->is_prototype_map()) {
      JSObject::OptimizeAsPrototype(js_prototype);
    }
    DirectHandle<PrototypeInfo> info =
        Map::GetOrCreatePrototypeInfo(js_prototype, isolate);
    // TODO(verwaest): Use inobject slack tracking for this map.
    Tagged<HeapObject> map_obj;
    if (info->ObjectCreateMap().GetHeapObjectIfWeak(&map_obj)) {
      map = handle(Cast<Map>(map_obj), isolate);
    } else {
      map = Map::CopyInitialMap(isolate, map);
      Map::SetPrototype(isolate, map, prototype);
      PrototypeInfo::SetObjectCreateMap(info, map, isolate);
    }
    return map;
  }

  return Map::TransitionRootMapToPrototypeForNewObject(isolate, map, prototype);
}

// static
Handle<Map> Map::GetDerivedMap(Isolate* isolate, Handle<Map> from,
                               Handle<JSReceiver> prototype) {
  DCHECK(IsUndefined(from->GetBackPointer()));

  if (IsJSObjectThatCanBeTrackedAsPrototype(*prototype)) {
    DirectHandle<JSObject> js_prototype = Cast<JSObject>(prototype);
    if (!js_prototype->map()->is_prototype_map()) {
      JSObject::OptimizeAsPrototype(js_prototype);
    }
    DirectHandle<PrototypeInfo> info =
        Map::GetOrCreatePrototypeInfo(js_prototype, isolate);
    Tagged<HeapObject> map_obj;
    Handle<Map> map;
    if (info->GetDerivedMap(from).GetHeapObjectIfWeak(&map_obj)) {
      map = handle(Cast<Map>(map_obj), isolate);
    } else {
      map = Map::CopyInitialMap(isolate, from);
      map->set_new_target_is_base(false);
      if (map->prototype() != *prototype) {
        Map::SetPrototype(isolate, map, prototype);
      }
      PrototypeInfo::AddDerivedMap(info, map, isolate);
    }
    return map;
  }

  // The TransitionToPrototype map will not have new_target_is_base reset. But
  // we don't need it to for proxies.
  return Map::TransitionRootMapToPrototypeForNewObject(isolate, from,
                                                       prototype);
}

static bool ContainsMap(MapHandlesSpan maps, Tagged<Map> map) {
  DCHECK(!map.is_null());
  for (Handle<Map> current : maps) {
    if (!current.is_null() && *current == map) return true;
  }
  return false;
}

static bool HasElementsKind(MapHandlesSpan maps, ElementsKind elements_kind) {
  for (Handle<Map> current : maps) {
    if (!current.is_null() && current->elements_kind() == elements_kind)
      return true;
  }
  return false;
}

Tagged<Map> Map::FindElementsKindTransitionedMap(Isolate* isolate,
                                                 MapHandlesSpan candidates,
                                                 ConcurrencyMode cmode) {
  DisallowGarbageCollection no_gc;

  if (IsDetached(isolate)) return Map();

  ElementsKind kind = elements_kind();
  bool is_packed = IsFastPackedElementsKind(kind);

  Tagged<Map> transition;
  if (IsTransitionableFastElementsKind(kind)) {
    // Check the state of the root map.
    Tagged<Map> root_map = FindRootMap(isolate);
    if (!EquivalentToForElementsKindTransition(root_map, cmode)) return Map();
    root_map = root_map->LookupElementsTransitionMap(isolate, kind, cmode);
    DCHECK(!root_map.is_null());
    // Starting from the next existing elements kind transition try to
    // replay the property transitions that does not involve instance rewriting
    // (ElementsTransitionAndStoreStub does not support that).
    for (root_map = root_map->ElementsTransitionMap(isolate, cmode);
         !root_map.is_null() && root_map->has_fast_elements();
         root_map = root_map->ElementsTransitionMap(isolate, cmode)) {
      // If root_map's elements kind doesn't match any of the elements kind in
      // the candidates there is no need to do any additional work.
      if (!HasElementsKind(candidates, root_map->elements_kind())) continue;
      Tagged<Map> current =
          root_map->TryReplayPropertyTransitions(isolate, *this, cmode);
      if (current.is_null()) continue;
      if (InstancesNeedRewriting(current, cmode)) continue;

      const bool current_is_packed =
          IsFastPackedElementsKind(current->elements_kind());
      if (ContainsMap(candidates, current) &&
          (is_packed || !current_is_packed)) {
        transition = current;
        is_packed = is_packed && current_is_packed;
      }
    }
  }
  return transition;
}

static Tagged<Map> FindClosestElementsTransition(Isolate* isolate,
                                                 Tagged<Map> map,
                                                 ElementsKind to_kind,
                                                 ConcurrencyMode cmode) {
  DisallowGarbageCollection no_gc;
  // Ensure we are requested to search elements kind transition "near the root".
  DCHECK_EQ(map->FindRootMap(isolate)->NumberOfOwnDescriptors(),
            map->NumberOfOwnDescriptors());
  Tagged<Map> current_map = map;

  ElementsKind kind = map->elements_kind();
  while (kind != to_kind) {
    Tagged<Map> next_map = current_map->ElementsTransitionMap(isolate, cmode);
    if (next_map.is_null()) return current_map;
    kind = next_map->elements_kind();
    current_map = next_map;
  }

  DCHECK_EQ(to_kind, current_map->elements_kind());
  return current_map;
}

Tagged<Map> Map::LookupElementsTransitionMap(Isolate* isolate,
                                             ElementsKind to_kind,
                                             ConcurrencyMode cmode) {
  Tagged<Map> to_map =
      FindClosestElementsTransition(isolate, *this, to_kind, cmode);
  if (to_map->elements_kind() == to_kind) return to_map;
  return Map();
}

bool Map::IsMapInArrayPrototypeChain(Isolate* isolate) const {
  if (isolate->initial_array_prototype()->map() == *this) {
    return true;
  }

  if (isolate->initial_object_prototype()->map() == *this) {
    return true;
  }

  return false;
}

Handle<Map> Map::TransitionElementsTo(Isolate* isolate, Handle<Map> map,
                                      ElementsKind to_kind) {
  ElementsKind from_kind = map->elements_kind();
  if (from_kind == to_kind) return map;

  Tagged<Context> native_context = isolate->context()->native_context();
  if (from_kind == FAST_SLOPPY_ARGUMENTS_ELEMENTS) {
    if (*map == native_context->fast_aliased_arguments_map()) {
      DCHECK_EQ(SLOW_SLOPPY_ARGUMENTS_ELEMENTS, to_kind);
      return handle(native_context->slow_aliased_arguments_map(), isolate);
    }
  } else if (from_kind == SLOW_SLOPPY_ARGUMENTS_ELEMENTS) {
    if (*map == native_context->slow_aliased_arguments_map()) {
      DCHECK_EQ(FAST_SLOPPY_ARGUMENTS_ELEMENTS, to_kind);
      return handle(native_context->fast_aliased_arguments_map(), isolate);
    }
  } else if (IsFastElementsKind(from_kind) && IsFastElementsKind(to_kind)) {
    // Reuse map transitions for JSArrays.
    DisallowGarbageCollection no_gc;
    if (native_context->GetInitialJSArrayMap(from_kind) == *map) {
      Tagged<Object> maybe_transitioned_map =
          native_context->get(Context::ArrayMapIndex(to_kind));
      if (IsMap(maybe_transitioned_map)) {
        return handle(Cast<Map>(maybe_transitioned_map), isolate);
      }
    }
  }

  DCHECK(!IsUndefined(*map, isolate));
  // Check if we can go back in the elements kind transition chain.
  if (IsHoleyElementsKind(from_kind) &&
      to_kind == GetPackedElementsKind(from_kind) &&
      IsMap(map->GetBackPointer()) &&
      Cast<Map>(map->GetBackPointer())->elements_kind() == to_kind) {
    return handle(Cast<Map>(map->GetBackPointer()), isolate);
  }

  bool allow_store_transition = IsTransitionElementsKind(from_kind);
  // Only store fast element maps in ascending generality.
  if (IsFastElementsKind(to_kind)) {
    allow_store_transition =
        allow_store_transition && IsTransitionableFastElementsKind(from_kind) &&
        IsMoreGeneralElementsKindTransition(from_kind, to_kind);
  }

  if (!allow_store_transition) {
    return Map::CopyAsElementsKind(isolate, map, to_kind, OMIT_TRANSITION);
  }

  return MapUpdater{isolate, map}.ReconfigureElementsKind(to_kind);
}

static Handle<Map> AddMissingElementsTransitions(Isolate* isolate,
                                                 Handle<Map> map,
                                                 ElementsKind to_kind) {
  DCHECK(IsTransitionElementsKind(map->elements_kind()));

  Handle<Map> current_map = map;

  ElementsKind kind = map->elements_kind();
  TransitionFlag flag;
  if (map->IsDetached(isolate)) {
    flag = OMIT_TRANSITION;
  } else {
    flag = INSERT_TRANSITION;
    if (IsFastElementsKind(kind)) {
      while (kind != to_kind && !IsTerminalElementsKind(kind)) {
        kind = GetNextTransitionElementsKind(kind);
        current_map = Map::CopyAsElementsKind(isolate, current_map, kind, flag);
      }
    }
  }

  // In case we are exiting the fast elements kind system, just add the map in
  // the end.
  if (kind != to_kind) {
    current_map = Map::CopyAsElementsKind(isolate, current_map, to_kind, flag);
  }

  DCHECK(current_map->elements_kind() == to_kind);
  return current_map;
}

// static
std::optional<Tagged<Map>> Map::TryAsElementsKind(Isolate* isolate,
                                                  DirectHandle<Map> map,
                                                  ElementsKind kind,
                                                  ConcurrencyMode cmode) {
  Tagged<Map> closest_map =
      FindClosestElementsTransition(isolate, *map, kind, cmode);
  if (closest_map->elements_kind() != kind) return {};
  return closest_map;
}

// static
Handle<Map> Map::AsElementsKind(Isolate* isolate, DirectHandle<Map> map,
                                ElementsKind kind) {
  Handle<Map> closest_map(
      FindClosestElementsTransition(isolate, *map, kind,
                                    ConcurrencyMode::kSynchronous),
      isolate);

  if (closest_map->elements_kind() == kind) {
    return closest_map;
  }

  return AddMissingElementsTransitions(isolate, closest_map, kind);
}

int Map::NumberOfEnumerableProperties() const {
  int result = 0;
  Tagged<DescriptorArray> descs = instance_descriptors(kRelaxedLoad);
  for (InternalIndex i : IterateOwnDescriptors()) {
    if ((int{descs->GetDetails(i).attributes()} & ONLY_ENUMERABLE) == 0 &&
        !Object::FilterKey(descs->GetKey(i), ENUMERABLE_STRINGS)) {
      result++;
    }
  }
  return result;
}

int Map::NextFreePropertyIndex() const {
  int number_of_own_descriptors = NumberOfOwnDescriptors();
  Tagged<DescriptorArray> descs = instance_descriptors(kRelaxedLoad);
  // Search properties backwards to find the last field.
  for (int i = number_of_own_descriptors - 1; i >= 0; --i) {
    PropertyDetails details = descs->GetDetails(InternalIndex(i));
    if (details.location() == PropertyLocation::kField) {
      return details.field_index() + details.field_width_in_words();
    }
  }
  return 0;
}

bool Map::OnlyHasSimpleProperties() const {
  // Wrapped string elements aren't explicitly stored in the elements backing
  // store, but are loaded indirectly from the underlying string.
  return !IsStringWrapperElementsKind(elements_kind()) &&
         !IsSpecialReceiverMap(*this) && !is_dictionary_map();
}

bool Map::ShouldCheckForReadOnlyElementsInPrototypeChain(Isolate* isolate) {
  // If this map has TypedArray elements kind, we won't look at the prototype
  // chain, so we can return early.
  if (IsTypedArrayElementsKind(elements_kind())) return false;

  for (PrototypeIterator iter(isolate, *this); !iter.IsAtEnd();
       iter.Advance()) {
    // Be conservative, don't look into any JSReceivers that may have custom
    // elements. For example, into JSProxies, String wrappers (which have have
    // non-configurable, non-writable elements), API objects, etc.
    if (IsCustomElementsReceiverMap(iter.GetCurrent()->map())) return true;

    Tagged<JSObject> current = iter.GetCurrent<JSObject>();
    ElementsKind elements_kind = current->GetElementsKind(isolate);
    // If this prototype has TypedArray elements kind, we won't look any further
    // in the prototype chain, so we can return early.
    if (IsTypedArrayElementsKind(elements_kind)) return false;
    if (IsFrozenElementsKind(elements_kind)) return true;

    if (IsDictionaryElementsKind(elements_kind) &&
        current->element_dictionary(isolate)->requires_slow_elements()) {
      return true;
    }

    if (IsSlowArgumentsElementsKind(elements_kind)) {
      Tagged<SloppyArgumentsElements> elements =
          Cast<SloppyArgumentsElements>(current->elements(isolate));
      Tagged<Object> arguments = elements->arguments();
      if (Cast<NumberDictionary>(arguments)->requires_slow_elements()) {
        return true;
      }
    }
  }

  return false;
}

Handle<Map> Map::RawCopy(Isolate* isolate, Handle<Map> src_handle,
                         int instance_size, int inobject_properties) {
  Handle<Map> result = isolate->factory()->NewMap(
      src_handle, src_handle->instance_type(), instance_size,
      TERMINAL_FAST_ELEMENTS_KIND, inobject_properties);

  // We have to set the bitfields before any potential GCs could happen because
  // heap verification might fail otherwise.
  {
    DisallowGarbageCollection no_gc;
    Tagged<Map> src = *src_handle;
    Tagged<Map> raw = *result;
    raw->set_constructor_or_back_pointer(src->GetConstructorRaw());
    raw->set_bit_field(src->bit_field());
    raw->set_bit_field2(src->bit_field2());
    int new_bit_field3 = src->bit_field3();
    new_bit_field3 = Bits3::OwnsDescriptorsBit::update(new_bit_field3, true);
    new_bit_field3 =
        Bits3::NumberOfOwnDescriptorsBits::update(new_bit_field3, 0);
    new_bit_field3 = Bits3::EnumLengthBits::update(new_bit_field3,
                                                   kInvalidEnumCacheSentinel);
    new_bit_field3 = Bits3::IsDeprecatedBit::update(new_bit_field3, false);
    new_bit_field3 =
        Bits3::IsInRetainedMapListBit::update(new_bit_field3, false);
    if (!src->is_dictionary_map()) {
      new_bit_field3 = Bits3::IsUnstableBit::update(new_bit_field3, false);
    }
    // Same as bit_field comment above.
    raw->set_bit_field3(new_bit_field3);
    raw->clear_padding();
  }
  Handle<HeapObject> prototype(src_handle->prototype(), isolate);
  Map::SetPrototype(isolate, result, prototype);
  return result;
}

Handle<Map> Map::Normalize(Isolate* isolate, Handle<Map> fast_map,
                           ElementsKind new_elements_kind,
                           Handle<HeapObject> new_prototype,
                           PropertyNormalizationMode mode, bool use_cache,
                           const char* reason) {
  DCHECK(!fast_map->is_dictionary_map());

  Tagged<Map> meta_map = fast_map->map();
  if (fast_map->is_prototype_map()) {
    use_cache = false;
  }
  DirectHandle<NormalizedMapCache> cache;
  if (use_cache) {
    Tagged<Object> normalized_map_cache =
        meta_map->native_context()->normalized_map_cache();
    use_cache = !IsUndefined(normalized_map_cache, isolate);
    if (use_cache) {
      cache = Cast<NormalizedMapCache>(handle(normalized_map_cache, isolate));
    }
  }

  Handle<Map> new_map;
  if (use_cache && cache
                       ->Get(fast_map, new_elements_kind,
                             new_prototype.is_null() ? fast_map->prototype()
                                                     : *new_prototype,
                             mode)
                       .ToHandle(&new_map)) {
#ifdef VERIFY_HEAP
    if (v8_flags.verify_heap) new_map->DictionaryMapVerify(isolate);
#endif
#ifdef ENABLE_SLOW_DCHECKS
    if (v8_flags.enable_slow_asserts) {
      // The cached map should match newly created normalized map bit-by-bit,
      // except for the code cache, which can contain some ICs which can be
      // applied to the shared map, dependent code and weak cell cache.
      DirectHandle<Map> fresh = Map::CopyNormalized(isolate, fast_map, mode);
      fresh->set_elements_kind(new_elements_kind);
      if (!new_prototype.is_null()) {
        Map::SetPrototype(isolate, fresh, new_prototype);
      }

      static_assert(Map::kPrototypeValidityCellOffset ==
                    Map::kDependentCodeOffset + kTaggedSize);
      DCHECK_EQ(0, memcmp(reinterpret_cast<void*>(fresh->address()),
                          reinterpret_cast<void*>(new_map->address()),
                          Map::kBitField3Offset));
      // The IsInRetainedMapListBit might be different if the {new_map}
      // that we got from the {cache} was already embedded into optimized
      // code somewhere.
      // The IsMigrationTargetBit might be different if the {new_map} from
      // {cache} has already been marked as a migration target.
      constexpr int ignored_bit_field3_bits =
          Bits3::IsInRetainedMapListBit::kMask |
          Bits3::IsMigrationTargetBit::kMask;
      DCHECK_EQ(fresh->bit_field3() & ~ignored_bit_field3_bits,
                new_map->bit_field3() & ~ignored_bit_field3_bits);
      int offset = Map::kBitField3Offset + kInt32Size;
      DCHECK_EQ(0, memcmp(reinterpret_cast<void*>(fresh->address() + offset),
                          reinterpret_cast<void*>(new_map->address() + offset),
                          Map::kDependentCodeOffset - offset));
      offset = Map::kPrototypeValidityCellOffset + kTaggedSize;
      if (new_map->is_prototype_map()) {
        // For prototype maps, the PrototypeInfo is not copied.
        static_assert(Map::kTransitionsOrPrototypeInfoOffset ==
                      Map::kPrototypeValidityCellOffset + kTaggedSize);
        offset = kTransitionsOrPrototypeInfoOffset + kTaggedSize;
        DCHECK_EQ(fresh->raw_transitions(), Smi::zero());
      }
      DCHECK_EQ(0, memcmp(reinterpret_cast<void*>(fresh->address() + offset),
                          reinterpret_cast<void*>(new_map->address() + offset),
                          Map::kSize - offset));
    }
#endif
    if (v8_flags.log_maps) {
      LOG(isolate, MapEvent("NormalizeCached", fast_map, new_map, reason));
    }
  } else {
    new_map = Map::CopyNormalized(isolate, fast_map, mode);
    new_map->set_elements_kind(new_elements_kind);
    if (!new_prototype.is_null()) {
      Map::SetPrototype(isolate, new_map, new_prototype);
      DCHECK(new_map->is_dictionary_map() && !new_map->is_deprecated());
    }
    if (use_cache) {
      cache->Set(fast_map, new_map);
    }
    if (v8_flags.log_maps) {
      LOG(isolate, MapEvent("Normalize", fast_map, new_map, reason));
    }
  }
  fast_map->NotifyLeafMapLayoutChange(isolate);
  return new_map;
}

Handle<Map> Map::CopyNormalized(Isolate* isolate, Handle<Map> map,
                                PropertyNormalizationMode mode) {
  int new_instance_size = map->instance_size();
  if (mode == CLEAR_INOBJECT_PROPERTIES) {
    new_instance_size -= map->GetInObjectProperties() * kTaggedSize;
  }

  Handle<Map> result = RawCopy(
      isolate, map, new_instance_size,
      mode == CLEAR_INOBJECT_PROPERTIES ? 0 : map->GetInObjectProperties());
  {
    DisallowGarbageCollection no_gc;
    Tagged<Map> raw = *result;
    // Clear the unused_property_fields explicitly as this field should not
    // be accessed for normalized maps.
    raw->SetInObjectUnusedPropertyFields(0);
    raw->set_is_dictionary_map(true);
    raw->set_is_migration_target(false);
    raw->set_may_have_interesting_properties(true);
    raw->set_construction_counter(kNoSlackTracking);
  }

#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) result->DictionaryMapVerify(isolate);
#endif

  return result;
}

// Return an immutable prototype exotic object version of the input map.
// Never even try to cache it in the transition tree, as it is intended
// for the global object and its prototype chain, and excluding it saves
// memory on the map transition tree.

// static
Handle<Map> Map::TransitionToImmutableProto(Isolate* isolate, Handle<Map> map) {
  Handle<Map> new_map = Map::Copy(isolate, map, "ImmutablePrototype");
  new_map->set_is_immutable_proto(true);
  return new_map;
}

namespace {
void EnsureInitialMap(Isolate* isolate, DirectHandle<Map> map) {
#ifdef DEBUG
  Tagged<Object> maybe_constructor = map->GetConstructor();
  DCHECK((IsJSFunction(maybe_constructor) &&
          *map == Cast<JSFunction>(maybe_constructor)->initial_map()) ||
         // Below are the exceptions to the check above.
         // |Function|'s initial map is a |sloppy_function_map| but
         // other function map variants such as sloppy with name or readonly
         // prototype or various strict function maps variants, etc. also
         // have Function as a constructor.
         *map == *isolate->strict_function_map() ||
         *map == *isolate->strict_function_with_name_map() ||
         // Same applies to |GeneratorFunction|'s initial map and generator
         // function map variants.
         *map == *isolate->generator_function_with_name_map() ||
         // Same applies to |AsyncFunction|'s initial map and other async
         // function map variants.
         *map == *isolate->async_function_with_name_map());
#endif
  // Initial maps must not contain descriptors in the descriptors array
  // that do not belong to the map.
  DCHECK_EQ(map->NumberOfOwnDescriptors(),
            map->instance_descriptors(isolate)->number_of_descriptors());
}
}  // namespace

// static
Handle<Map> Map::CopyInitialMapNormalized(Isolate* isolate, Handle<Map> map,
                                          PropertyNormalizationMode mode) {
  EnsureInitialMap(isolate, map);
  return CopyNormalized(isolate, map, mode);
}

// static
Handle<Map> Map::CopyInitialMap(Isolate* isolate, Handle<Map> map,
                                int instance_size, int inobject_properties,
                                int unused_property_fields) {
  EnsureInitialMap(isolate, map);

  Handle<Map> result =
      RawCopy(isolate, map, instance_size, inobject_properties);

  // Please note instance_type and instance_size are set when allocated.
  result->SetInObjectUnusedPropertyFields(unused_property_fields);

  int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  if (number_of_own_descriptors > 0) {
    // The copy will use the same descriptors array without ownership.
    Tagged<DescriptorArray> descriptors = map->instance_descriptors(isolate);
    result->set_owns_descriptors(false);
    result->UpdateDescriptors(isolate, descriptors, number_of_own_descriptors);

    DCHECK_EQ(result->NumberOfFields(ConcurrencyMode::kSynchronous),
              result->GetInObjectProperties() - result->UnusedPropertyFields());
  }

  return result;
}

Handle<Map> Map::CopyDropDescriptors(Isolate* isolate, Handle<Map> map) {
  Handle<Map> result =
      RawCopy(isolate, map, map->instance_size(),
              IsJSObjectMap(*map) ? map->GetInObjectProperties() : 0);

  // Please note instance_type and instance_size are set when allocated.
  if (IsJSObjectMap(*map)) {
    result->CopyUnusedPropertyFields(*map);
  }
  map->NotifyLeafMapLayoutChange(isolate);
  return result;
}

Handle<Map> Map::ShareDescriptor(Isolate* isolate, Handle<Map> map,
                                 DirectHandle<DescriptorArray> descriptors,
                                 Descriptor* descriptor) {
  // Sanity check. This path is only to be taken if the map owns its descriptor
  // array, implying that its NumberOfOwnDescriptors equals the number of
  // descriptors in the descriptor array.
  DCHECK_EQ(map->NumberOfOwnDescriptors(),
            map->instance_descriptors(isolate)->number_of_descriptors());

  Handle<Map> result = CopyDropDescriptors(isolate, map);
  Handle<Name> name = descriptor->GetKey();

  // Properly mark the {result} if the {name} is an "interesting symbol".
  if (name->IsInteresting(isolate)) {
    result->set_may_have_interesting_properties(true);
  }

  // Ensure there's space for the new descriptor in the shared descriptor array.
  if (descriptors->number_of_slack_descriptors() == 0) {
    int old_size = descriptors->number_of_descriptors();
    if (old_size == 0) {
      descriptors = DescriptorArray::Allocate(isolate, 0, 1);
    } else {
      int slack = SlackForArraySize(old_size, kMaxNumberOfDescriptors);
      EnsureDescriptorSlack(isolate, map, slack);
      descriptors = handle(map->instance_descriptors(isolate), isolate);
    }
  }

  {
    DisallowGarbageCollection no_gc;
    descriptors->Append(descriptor);
    result->InitializeDescriptors(isolate, *descriptors);
  }

  DCHECK(result->NumberOfOwnDescriptors() == map->NumberOfOwnDescriptors() + 1);
  ConnectTransition(isolate, map, result, name, SIMPLE_PROPERTY_TRANSITION);

  return result;
}

void Map::ConnectTransition(Isolate* isolate, Handle<Map> parent,
                            Handle<Map> child, Handle<Name> name,
                            TransitionKindFlag transition_kind,
                            bool force_connect) {
  DCHECK_EQ(parent->map(), child->map());
  DCHECK_IMPLIES(name->IsInteresting(isolate),
                 child->may_have_interesting_properties());
  DCHECK_IMPLIES(parent->may_have_interesting_properties(),
                 child->may_have_interesting_properties());
  if (!IsUndefined(parent->GetBackPointer(), isolate)) {
    parent->set_owns_descriptors(false);
  } else if (!parent->IsDetached(isolate)) {
    // |parent| is initial map and it must not contain descriptors in the
    // descriptors array that do not belong to the map.
    DCHECK_EQ(parent->NumberOfOwnDescriptors(),
              parent->instance_descriptors(isolate)->number_of_descriptors());
  }
  if (parent->IsDetached(isolate) && !force_connect) {
    DCHECK(child->IsDetached(isolate));
    if (v8_flags.log_maps) {
      LOG(isolate, MapEvent("Transition", parent, child, "prototype", name));
    }
  } else {
    TransitionsAccessor::Insert(isolate, parent, name, child, transition_kind);
    if (v8_flags.log_maps) {
      LOG(isolate, MapEvent("Transition", parent, child, "", name));
    }
  }
}

Handle<Map> Map::CopyReplaceDescriptors(
    Isolate* isolate, Handle<Map> map,
    DirectHandle<DescriptorArray> descriptors, TransitionFlag flag,
    MaybeHandle<Name> maybe_name, const char* reason,
    TransitionKindFlag transition_kind) {
  DCHECK(descriptors->IsSortedNoDuplicates());

  Handle<Map> result = CopyDropDescriptors(isolate, map);
  bool is_connected = false;

  // Properly mark the {result} if the {name} is an "interesting symbol".
  Handle<Name> name;
  if (maybe_name.ToHandle(&name) && name->IsInteresting(isolate)) {
    result->set_may_have_interesting_properties(true);
  }

  if (map->is_prototype_map()) {
    result->InitializeDescriptors(isolate, *descriptors);
  } else {
    if (flag == INSERT_TRANSITION &&
        TransitionsAccessor::CanHaveMoreTransitions(isolate, map)) {
      result->InitializeDescriptors(isolate, *descriptors);

      DCHECK(!maybe_name.is_null());
      ConnectTransition(isolate, map, result, name, transition_kind);
      is_connected = true;
    } else if ((transition_kind == PROTOTYPE_TRANSITION &&
                v8_flags.move_prototype_transitions_first) ||
               isolate->bootstrapper()->IsActive()) {
      // Prototype transitions are always between root maps. UpdatePrototype
      // uses the MapUpdater and instance migration. Thus, field generalization
      // is allowed to happen lazily.
      DCHECK_IMPLIES(transition_kind == PROTOTYPE_TRANSITION,
                     IsUndefined(map->GetBackPointer()));
      result->InitializeDescriptors(isolate, *descriptors);
    } else {
      DCHECK_IMPLIES(transition_kind == PROTOTYPE_TRANSITION,
                     !v8_flags.move_prototype_transitions_first);
      descriptors->GeneralizeAllFields(transition_kind == PROTOTYPE_TRANSITION);
      result->InitializeDescriptors(isolate, *descriptors);
    }
  }
  if (v8_flags.log_maps && !is_connected) {
    LOG(isolate, MapEvent("ReplaceDescriptors", map, result, reason,
                          maybe_name.is_null() ? Handle<HeapObject>() : name));
  }
  return result;
}

// Creates transition tree starting from |split_map| and adding all descriptors
// starting from descriptor with index |split_map|.NumberOfOwnDescriptors().
// The way how it is done is tricky because of GC and special descriptors
// marking logic.
Handle<Map> Map::AddMissingTransitions(
    Isolate* isolate, Handle<Map> split_map,
    DirectHandle<DescriptorArray> descriptors) {
  DCHECK(descriptors->IsSortedNoDuplicates());
  int split_nof = split_map->NumberOfOwnDescriptors();
  int nof_descriptors = descriptors->number_of_descriptors();
  CHECK_LT(split_nof, nof_descriptors);

  // Start with creating last map which will own full descriptors array.
  // This is necessary to guarantee that GC will mark the whole descriptor
  // array if any of the allocations happening below fail.
  // Number of unused properties is temporarily incorrect and the layout
  // descriptor could unnecessarily be in slow mode but we will fix after
  // all the other intermediate maps are created.
  // Also the last map might have interesting symbols, we temporarily set
  // the flag and clear it right before the descriptors are installed. This
  // makes heap verification happy and ensures the flag ends up accurate.
  Handle<Map> last_map = CopyDropDescriptors(isolate, split_map);
  last_map->InitializeDescriptors(isolate, *descriptors);
  last_map->SetInObjectUnusedPropertyFields(0);
  last_map->set_may_have_interesting_properties(true);

  // During creation of intermediate maps we violate descriptors sharing
  // invariant since the last map is not yet connected to the transition tree
  // we create here. But it is safe because GC never trims map's descriptors
  // if there are no dead transitions from that map and this is exactly the
  // case for all the intermediate maps we create here.
  Handle<Map> map = split_map;
  for (InternalIndex i : InternalIndex::Range(split_nof, nof_descriptors - 1)) {
    Handle<Map> new_map = CopyDropDescriptors(isolate, map);
    // Force connection of these maps to prevent split_map being a root map to
    // be treated as detached.
    InstallDescriptors(isolate, map, new_map, i, descriptors,
                       /* force_connect */ true);
    DCHECK_EQ(*new_map->GetBackPointer(), *map);
    map = new_map;
  }
  map->NotifyLeafMapLayoutChange(isolate);
  last_map->set_may_have_interesting_properties(false);
  InstallDescriptors(isolate, map, last_map, InternalIndex(nof_descriptors - 1),
                     descriptors);
  return last_map;
}

// Since this method is used to rewrite an existing transition tree, it can
// always insert transitions without checking.
void Map::InstallDescriptors(Isolate* isolate, Handle<Map> parent,
                             Handle<Map> child, InternalIndex new_descriptor,
                             DirectHandle<DescriptorArray> descriptors,
                             bool force_connect) {
  DCHECK(descriptors->IsSortedNoDuplicates());

  child->SetInstanceDescriptors(isolate, *descriptors,
                                new_descriptor.as_int() + 1);
  child->CopyUnusedPropertyFields(*parent);
  PropertyDetails details = descriptors->GetDetails(new_descriptor);
  if (details.location() == PropertyLocation::kField) {
    child->AccountAddedPropertyField();
  }

  Handle<Name> name = handle(descriptors->GetKey(new_descriptor), isolate);
  if (parent->may_have_interesting_properties() ||
      name->IsInteresting(isolate)) {
    child->set_may_have_interesting_properties(true);
  }
  ConnectTransition(isolate, parent, child, name, SIMPLE_PROPERTY_TRANSITION,
                    force_connect);
}

Handle<Map> Map::CopyAsElementsKind(Isolate* isolate, Handle<Map> map,
                                    ElementsKind kind, TransitionFlag flag) {
  // Only certain objects are allowed to have non-terminal fast transitional
  // elements kinds.
  DCHECK(IsJSObjectMap(*map));
  DCHECK_IMPLIES(
      !map->CanHaveFastTransitionableElementsKind(),
      IsDictionaryElementsKind(kind) || IsTerminalElementsKind(kind));

  Tagged<Map> maybe_elements_transition_map;
  if (flag == INSERT_TRANSITION) {
    // Ensure we are requested to add elements kind transition "near the root".
    DCHECK_EQ(map->FindRootMap(isolate)->NumberOfOwnDescriptors(),
              map->NumberOfOwnDescriptors());

    maybe_elements_transition_map =
        map->ElementsTransitionMap(isolate, ConcurrencyMode::kSynchronous);
    DCHECK(maybe_elements_transition_map.is_null() ||
           (maybe_elements_transition_map->elements_kind() ==
                DICTIONARY_ELEMENTS &&
            kind == DICTIONARY_ELEMENTS));
    DCHECK(!IsFastElementsKind(kind) ||
           IsMoreGeneralElementsKindTransition(map->elements_kind(), kind));
    DCHECK(kind != map->elements_kind());
  }

  bool insert_transition =
      flag == INSERT_TRANSITION &&
      TransitionsAccessor::CanHaveMoreTransitions(isolate, map) &&
      maybe_elements_transition_map.is_null();

  if (insert_transition) {
    Handle<Map> new_map = CopyForElementsTransition(isolate, map);
    new_map->set_elements_kind(kind);

    Handle<Name> name = isolate->factory()->elements_transition_symbol();
    ConnectTransition(isolate, map, new_map, name, SPECIAL_TRANSITION);
    return new_map;
  }

  // Create a new free-floating map only if we are not allowed to store it.
  Handle<Map> new_map = Copy(isolate, map, "CopyAsElementsKind");
  new_map->set_elements_kind(kind);
  return new_map;
}

Handle<Map> Map::AsLanguageMode(Isolate* isolate, Handle<Map> initial_map,
                                DirectHandle<SharedFunctionInfo> shared_info) {
  DCHECK(InstanceTypeChecker::IsJSFunction(initial_map->instance_type()));
  // Initial map for sloppy mode function is stored in the function
  // constructor. Initial maps for strict mode are cached as special transitions
  // using |strict_function_transition_symbol| as a key.
  if (is_sloppy(shared_info->language_mode())) return initial_map;

  Handle<Map> function_map(Cast<Map>(isolate->native_context()->get(
                               shared_info->function_map_index())),
                           isolate);

  static_assert(LanguageModeSize == 2);
  DCHECK_EQ(LanguageMode::kStrict, shared_info->language_mode());
  Handle<Symbol> transition_symbol =
      isolate->factory()->strict_function_transition_symbol();
  MaybeHandle<Map> maybe_transition = TransitionsAccessor::SearchSpecial(
      isolate, initial_map, *transition_symbol);
  if (!maybe_transition.is_null()) {
    return maybe_transition.ToHandleChecked();
  }
  initial_map->NotifyLeafMapLayoutChange(isolate);

  // Create new map taking descriptors from the |function_map| and all
  // the other details from the |initial_map|.
  Handle<Map> map =
      Map::CopyInitialMap(isolate, function_map, initial_map->instance_size(),
                          initial_map->GetInObjectProperties(),
                          initial_map->UnusedPropertyFields());
  map->SetConstructor(initial_map->GetConstructor());
  map->set_prototype(initial_map->prototype());
  map->set_construction_counter(initial_map->construction_counter());

  if (TransitionsAccessor::CanHaveMoreTransitions(isolate, initial_map)) {
    Map::ConnectTransition(isolate, initial_map, map, transition_symbol,
                           SPECIAL_TRANSITION);
  }
  return map;
}

Handle<Map> Map::CopyForElementsTransition(Isolate* isolate, Handle<Map> map) {
  DCHECK(!map->IsDetached(isolate));
  DCHECK(!map->is_dictionary_map());
  Handle<Map> new_map = CopyDropDescriptors(isolate, map);

  if (map->owns_descriptors()) {
    // In case the map owned its own descriptors, share the descriptors and
    // transfer ownership to the new map.
    // The properties did not change, so reuse descriptors.
    map->set_owns_descriptors(false);
    new_map->InitializeDescriptors(isolate, map->instance_descriptors(isolate));
  } else {
    // In case the map did not own its own descriptors, a split is forced by
    // copying the map; creating a new descriptor array cell.
    DirectHandle<DescriptorArray> descriptors(
        map->instance_descriptors(isolate), isolate);
    int number_of_own_descriptors = map->NumberOfOwnDescriptors();
    DirectHandle<DescriptorArray> new_descriptors = DescriptorArray::CopyUpTo(
        isolate, descriptors, number_of_own_descriptors);
    new_map->InitializeDescriptors(isolate, *new_descriptors);
  }
  return new_map;
}

Handle<Map> Map::CopyForPrototypeTransition(Isolate* isolate, Handle<Map> map,
                                            Handle<HeapObject> prototype) {
  // For simplicity we always copy descriptors although it would be possible to
  // share them in some situations.
  Handle<Map> new_map =
      Copy(isolate, map, "TransitionToPrototype", PROTOTYPE_TRANSITION);
  Map::SetPrototype(isolate, new_map, prototype);
  return new_map;
}

Handle<Map> Map::Copy(Isolate* isolate, Handle<Map> map, const char* reason,
                      TransitionKindFlag kind) {
  DirectHandle<DescriptorArray> descriptors(map->instance_descriptors(isolate),
                                            isolate);
  int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  DirectHandle<DescriptorArray> new_descriptors = DescriptorArray::CopyUpTo(
      isolate, descriptors, number_of_own_descriptors);
  auto res =
      CopyReplaceDescriptors(isolate, map, new_descriptors, OMIT_TRANSITION,
                             MaybeHandle<Name>(), reason, kind);
  return res;
}

Handle<Map> Map::Create(Isolate* isolate, int inobject_properties) {
  Handle<Map> copy_handle =
      Copy(isolate, handle(isolate->object_function()->initial_map(), isolate),
           "MapCreate");
  DisallowGarbageCollection no_gc;
  Tagged<Map> copy = *copy_handle;

  // Check that we do not overflow the instance size when adding the extra
  // inobject properties. If the instance size overflows, we allocate as many
  // properties as we can as inobject properties.
  if (inobject_properties > JSObject::kMaxInObjectProperties) {
    inobject_properties = JSObject::kMaxInObjectProperties;
  }

  int new_instance_size =
      JSObject::kHeaderSize + kTaggedSize * inobject_properties;

  // Adjust the map with the extra inobject properties.
  copy->set_instance_size(new_instance_size);
  copy->SetInObjectPropertiesStartInWords(JSObject::kHeaderSize / kTaggedSize);
  DCHECK_EQ(copy->GetInObjectProperties(), inobject_properties);
  copy->SetInObjectUnusedPropertyFields(inobject_properties);
  copy->set_visitor_id(Map::GetVisitorId(copy));

  return copy_handle;
}

Handle<Map> Map::CopyForPreventExtensions(
    Isolate* isolate, Handle<Map> map, PropertyAttributes attrs_to_add,
    Handle<Symbol> transition_marker, const char* reason,
    bool old_map_is_dictionary_elements_kind) {
  int num_descriptors = map->NumberOfOwnDescriptors();
  DirectHandle<DescriptorArray> new_desc =
      DescriptorArray::CopyUpToAddAttributes(
          isolate, handle(map->instance_descriptors(isolate), isolate),
          num_descriptors, attrs_to_add);
  // Do not track transitions during bootstrapping.
  TransitionFlag flag =
      isolate->bootstrapper()->IsActive() ? OMIT_TRANSITION : INSERT_TRANSITION;
  Handle<Map> new_map =
      CopyReplaceDescriptors(isolate, map, new_desc, flag, transition_marker,
                             reason, SPECIAL_TRANSITION);
  new_map->set_is_extensible(false);
  if (!IsTypedArrayOrRabGsabTypedArrayElementsKind(map->elements_kind())) {
    ElementsKind new_kind = IsStringWrapperElementsKind(map->elements_kind())
                                ? SLOW_STRING_WRAPPER_ELEMENTS
                                : DICTIONARY_ELEMENTS;
    if (v8_flags.enable_sealed_frozen_elements_kind &&
        !old_map_is_dictionary_elements_kind) {
      switch (map->elements_kind()) {
        case PACKED_ELEMENTS:
          if (attrs_to_add == SEALED) {
            new_kind = PACKED_SEALED_ELEMENTS;
          } else if (attrs_to_add == FROZEN) {
            new_kind = PACKED_FROZEN_ELEMENTS;
          } else {
            new_kind = PACKED_NONEXTENSIBLE_ELEMENTS;
          }
          break;
        case PACKED_NONEXTENSIBLE_ELEMENTS:
          if (attrs_to_add == SEALED) {
            new_kind = PACKED_SEALED_ELEMENTS;
          } else if (attrs_to_add == FROZEN) {
            new_kind = PACKED_FROZEN_ELEMENTS;
          }
          break;
        case PACKED_SEALED_ELEMENTS:
          if (attrs_to_add == FROZEN) {
            new_kind = PACKED_FROZEN_ELEMENTS;
          }
          break;
        case HOLEY_ELEMENTS:
          if (attrs_to_add == SEALED) {
            new_kind = HOLEY_SEALED_ELEMENTS;
          } else if (attrs_to_add == FROZEN) {
            new_kind = HOLEY_FROZEN_ELEMENTS;
          } else {
            new_kind = HOLEY_NONEXTENSIBLE_ELEMENTS;
          }
          break;
        case HOLEY_NONEXTENSIBLE_ELEMENTS:
          if (attrs_to_add == SEALED) {
            new_kind = HOLEY_SEALED_ELEMENTS;
          } else if (attrs_to_add == FROZEN) {
            new_kind = HOLEY_FROZEN_ELEMENTS;
          }
          break;
        case HOLEY_SEALED_ELEMENTS:
          if (attrs_to_add == FROZEN) {
            new_kind = HOLEY_FROZEN_ELEMENTS;
          }
          break;
        default:
          break;
      }
    }
    new_map->set_elements_kind(new_kind);
  }
  return new_map;
}

namespace {

bool CanHoldValue(Tagged<DescriptorArray> descriptors, InternalIndex descriptor,
                  PropertyConstness constness, Tagged<Object> value) {
  PropertyDetails details = descriptors->GetDetails(descriptor);
  if (details.location() == PropertyLocation::kField) {
    if (details.kind() == PropertyKind::kData) {
      return IsGeneralizableTo(constness, details.constness()) &&
             Object::FitsRepresentation(value, details.representation()) &&
             FieldType::NowContains(descriptors->GetFieldType(descriptor),
                                    value);
    } else {
      DCHECK_EQ(PropertyKind::kAccessor, details.kind());
      return false;
    }

  } else {
    DCHECK_EQ(PropertyLocation::kDescriptor, details.location());
    DCHECK_EQ(PropertyConstness::kConst, details.constness());
    DCHECK_EQ(PropertyKind::kAccessor, details.kind());
    return false;
  }
  UNREACHABLE();
}

Handle<Map> UpdateDescriptorForValue(Isolate* isolate, Handle<Map> map,
                                     InternalIndex descriptor,
                                     PropertyConstness constness,
                                     DirectHandle<Object> value) {
  if (CanHoldValue(map->instance_descriptors(isolate), descriptor, constness,
                   *value)) {
    return map;
  }

  PropertyAttributes attributes =
      map->instance_descriptors(isolate)->GetDetails(descriptor).attributes();
  Representation representation =
      Object::OptimalRepresentation(*value, isolate);
  Handle<FieldType> type = Object::OptimalType(*value, isolate, representation);

  MapUpdater mu(isolate, map);
  return mu.ReconfigureToDataField(descriptor, attributes, constness,
                                   representation, type);
}

}  // namespace

// static
Handle<Map> Map::PrepareForDataProperty(Isolate* isolate, Handle<Map> map,
                                        InternalIndex descriptor,
                                        PropertyConstness constness,
                                        DirectHandle<Object> value) {
  // The map should already be fully updated before storing the property.
  DCHECK(!map->is_deprecated());
  // Dictionaries can store any property value.
  DCHECK(!map->is_dictionary_map());
  return UpdateDescriptorForValue(isolate, map, descriptor, constness, value);
}

Handle<Map> Map::TransitionToDataProperty(Isolate* isolate, Handle<Map> map,
                                          Handle<Name> name,
                                          DirectHandle<Object> value,
                                          PropertyAttributes attributes,
                                          PropertyConstness constness,
                                          StoreOrigin store_origin) {
  RCS_SCOPE(isolate,
            map->IsDetached(isolate)
                ? RuntimeCallCounterId::kPrototypeMap_TransitionToDataProperty
                : RuntimeCallCounterId::kMap_TransitionToDataProperty);

  DCHECK(IsUniqueName(*name));
  DCHECK(!map->is_dictionary_map());

  // Migrate to the newest map before storing the property.
  map = Update(isolate, map);

  MaybeHandle<Map> maybe_transition = TransitionsAccessor::SearchTransition(
      isolate, map, *name, PropertyKind::kData, attributes);
  Handle<Map> transition;
  if (maybe_transition.ToHandle(&transition)) {
    InternalIndex descriptor = transition->LastAdded();

    DCHECK_EQ(attributes, transition->instance_descriptors(isolate)
                              ->GetDetails(descriptor)
                              .attributes());

    return UpdateDescriptorForValue(isolate, transition, descriptor, constness,
                                    value);
  }

  // Do not track transitions during bootstrapping.
  TransitionFlag flag =
      isolate->bootstrapper()->IsActive() ? OMIT_TRANSITION : INSERT_TRANSITION;
  MaybeHandle<Map> maybe_map;
  if (!map->TooManyFastProperties(store_origin)) {
    Representation representation =
        Object::OptimalRepresentation(*value, isolate);
    Handle<FieldType> type =
        Object::OptimalType(*value, isolate, representation);
    maybe_map = Map::CopyWithField(isolate, map, name, type, attributes,
                                   constness, representation, flag);
  }

  Handle<Map> result;
  if (!maybe_map.ToHandle(&result)) {
    const char* reason = "TooManyFastProperties";
#if V8_TRACE_MAPS
    std::unique_ptr<base::ScopedVector<char>> buffer;
    if (v8_flags.log_maps) {
      base::ScopedVector<char> name_buffer(100);
      name->NameShortPrint(name_buffer);
      buffer.reset(new base::ScopedVector<char>(128));
      SNPrintF(*buffer, "TooManyFastProperties %s", name_buffer.begin());
      reason = buffer->begin();
    }
#endif
    Handle<Object> maybe_constructor(map->GetConstructor(), isolate);
    if (v8_flags.feedback_normalization && map->new_target_is_base() &&
        IsJSFunction(*maybe_constructor) &&
        !Cast<JSFunction>(*maybe_constructor)->shared()->native()) {
      auto constructor = Cast<JSFunction>(maybe_constructor);
      DCHECK_NE(*constructor, constructor->native_context()->object_function());
      Handle<Map> initial_map(constructor->initial_map(), isolate);
      result = Map::Normalize(isolate, initial_map, CLEAR_INOBJECT_PROPERTIES,
                              reason);
      initial_map->DeprecateTransitionTree(isolate);
      Handle<HeapObject> prototype(result->prototype(), isolate);
      JSFunction::SetInitialMap(isolate, constructor, result, prototype);

      // Deoptimize all code that embeds the previous initial map.
      DependentCode::DeoptimizeDependencyGroups(
          isolate, *initial_map, DependentCode::kInitialMapChangedGroup);
      if (!result->EquivalentToForNormalization(*map,
                                                CLEAR_INOBJECT_PROPERTIES)) {
        result =
            Map::Normalize(isolate, map, CLEAR_INOBJECT_PROPERTIES, reason);
      }
    } else {
      result = Map::Normalize(isolate, map, CLEAR_INOBJECT_PROPERTIES, reason);
    }
  }

  return result;
}

Handle<Map> Map::TransitionToAccessorProperty(Isolate* isolate, Handle<Map> map,
                                              Handle<Name> name,
                                              InternalIndex descriptor,
                                              DirectHandle<Object> getter,
                                              DirectHandle<Object> setter,
                                              PropertyAttributes attributes) {
  RCS_SCOPE(
      isolate,
      map->IsDetached(isolate)
          ? RuntimeCallCounterId::kPrototypeMap_TransitionToAccessorProperty
          : RuntimeCallCounterId::kMap_TransitionToAccessorProperty);

  // At least one of the accessors needs to be a new value.
  DCHECK(!IsNull(*getter, isolate) || !IsNull(*setter, isolate));
  DCHECK(IsUniqueName(*name));

  // Migrate to the newest map before transitioning to the new property.
  map = Update(isolate, map);

  // Dictionary maps can always have additional data properties.
  if (map->is_dictionary_map()) return map;

  PropertyNormalizationMode mode = map->is_prototype_map()
                                       ? KEEP_INOBJECT_PROPERTIES
                                       : CLEAR_INOBJECT_PROPERTIES;

  MaybeHandle<Map> maybe_transition = TransitionsAccessor::SearchTransition(
      isolate, map, *name, PropertyKind::kAccessor, attributes);
  Handle<Map> transition;
  if (maybe_transition.ToHandle(&transition)) {
    Tagged<DescriptorArray> descriptors =
        transition->instance_descriptors(isolate);
    InternalIndex last_descriptor = transition->LastAdded();
    DCHECK(descriptors->GetKey(last_descriptor)->Equals(*name));

    DCHECK_EQ(PropertyKind::kAccessor,
              descriptors->GetDetails(last_descriptor).kind());
    DCHECK_EQ(attributes,
              descriptors->GetDetails(last_descriptor).attributes());

    Handle<Object> maybe_pair(descriptors->GetStrongValue(last_descriptor),
                              isolate);
    if (!IsAccessorPair(*maybe_pair)) {
      return Map::Normalize(isolate, map, mode,
                            "TransitionToAccessorFromNonPair");
    }

    auto pair = Cast<AccessorPair>(maybe_pair);
    if (!pair->Equals(*getter, *setter)) {
      return Map::Normalize(isolate, map, mode,
                            "TransitionToDifferentAccessor");
    }

    return transition;
  }

  Handle<AccessorPair> pair;
  Tagged<DescriptorArray> old_descriptors = map->instance_descriptors(isolate);
  if (descriptor.is_found()) {
    if (descriptor != map->LastAdded()) {
      return Map::Normalize(isolate, map, mode, "AccessorsOverwritingNonLast");
    }
    PropertyDetails old_details = old_descriptors->GetDetails(descriptor);
    if (old_details.kind() != PropertyKind::kAccessor) {
      return Map::Normalize(isolate, map, mode,
                            "AccessorsOverwritingNonAccessors");
    }

    if (old_details.attributes() != attributes) {
      return Map::Normalize(isolate, map, mode, "AccessorsWithAttributes");
    }

    Handle<Object> maybe_pair(old_descriptors->GetStrongValue(descriptor),
                              isolate);
    if (!IsAccessorPair(*maybe_pair)) {
      return Map::Normalize(isolate, map, mode, "AccessorsOverwritingNonPair");
    }

    auto current_pair = Cast<AccessorPair>(maybe_pair);
    if (current_pair->Equals(*getter, *setter)) return map;

    bool overwriting_accessor = false;
    if (!IsNull(*getter, isolate) &&
        !IsNull(current_pair->get(ACCESSOR_GETTER), isolate) &&
        current_pair->get(ACCESSOR_GETTER) != *getter) {
      overwriting_accessor = true;
    }
    if (!IsNull(*setter, isolate) &&
        !IsNull(current_pair->get(ACCESSOR_SETTER), isolate) &&
        current_pair->get(ACCESSOR_SETTER) != *setter) {
      overwriting_accessor = true;
    }
    if (overwriting_accessor) {
      return Map::Normalize(isolate, map, mode,
                            "AccessorsOverwritingAccessors");
    }

    pair = AccessorPair::Copy(isolate, Cast<AccessorPair>(maybe_pair));
  } else if (map->NumberOfOwnDescriptors() >= kMaxNumberOfDescriptors ||
             map->TooManyFastProperties(StoreOrigin::kNamed)) {
    return Map::Normalize(isolate, map, CLEAR_INOBJECT_PROPERTIES,
                          "TooManyAccessors");
  } else {
    pair = isolate->factory()->NewAccessorPair();
  }

  pair->SetComponents(*getter, *setter);

  // Do not track transitions during bootstrapping.
  TransitionFlag flag =
      isolate->bootstrapper()->IsActive() ? OMIT_TRANSITION : INSERT_TRANSITION;
  Descriptor d = Descriptor::AccessorConstant(name, pair, attributes);
  return Map::CopyInsertDescriptor(isolate, map, &d, flag);
}

Handle<Map> Map::CopyAddDescriptor(Isolate* isolate, Handle<Map> map,
                                   Descriptor* descriptor,
                                   TransitionFlag flag) {
  DirectHandle<DescriptorArray> descriptors(map->instance_descriptors(isolate),
                                            isolate);

  // Share descriptors only if map owns descriptors and is not an initial map.
  if (flag == INSERT_TRANSITION && map->owns_descriptors() &&
      !IsUndefined(map->GetBackPointer(), isolate) &&
      TransitionsAccessor::CanHaveMoreTransitions(isolate, map)) {
    return ShareDescriptor(isolate, map, descriptors, descriptor);
  }

  int nof = map->NumberOfOwnDescriptors();
  DirectHandle<DescriptorArray> new_descriptors =
      DescriptorArray::CopyUpTo(isolate, descriptors, nof, 1);
  new_descriptors->Append(descriptor);

  return CopyReplaceDescriptors(isolate, map, new_descriptors, flag,
                                descriptor->GetKey(), "CopyAddDescriptor",
                                SIMPLE_PROPERTY_TRANSITION);
}

Handle<Map> Map::CopyInsertDescriptor(Isolate* isolate, Handle<Map> map,
                                      Descriptor* descriptor,
                                      TransitionFlag flag) {
  DirectHandle<DescriptorArray> old_descriptors(
      map->instance_descriptors(isolate), isolate);

  // We replace the key if it is already present.
  InternalIndex index =
      old_descriptors->SearchWithCache(isolate, *descriptor->GetKey(), *map);
  if (index.is_found()) {
    return CopyReplaceDescriptor(isolate, map, old_descriptors, descriptor,
                                 index, flag);
  }
  return CopyAddDescriptor(isolate, map, descriptor, flag);
}

Handle<Map> Map::CopyReplaceDescriptor(
    Isolate* isolate, Handle<Map> map,
    DirectHandle<DescriptorArray> descriptors, Descriptor* descriptor,
    InternalIndex insertion_index, TransitionFlag flag) {
  Handle<Name> key = descriptor->GetKey();
  DCHECK_EQ(*key, descriptors->GetKey(insertion_index));
  // This function does not support replacing property fields as
  // that would break property field counters.
  DCHECK_NE(PropertyLocation::kField, descriptor->GetDetails().location());
  DCHECK_NE(PropertyLocation::kField,
            descriptors->GetDetails(insertion_index).location());

  DirectHandle<DescriptorArray> new_descriptors = DescriptorArray::CopyUpTo(
      isolate, descriptors, map->NumberOfOwnDescriptors());

  new_descriptors->Replace(insertion_index, descriptor);

  TransitionKindFlag simple_flag =
      (insertion_index.as_int() == descriptors->number_of_descriptors() - 1)
          ? SIMPLE_PROPERTY_TRANSITION
          : PROPERTY_TRANSITION;
  return CopyReplaceDescriptors(isolate, map, new_descriptors, flag, key,
                                "CopyReplaceDescriptor", simple_flag);
}

int Map::Hash() { return Hash(prototype()); }

int Map::Hash(Tagged<HeapObject> prototype) {
  // For performance reasons we only hash the 2 most variable fields of a map:
  // prototype and bit_field2.

  int prototype_hash;
  if (IsNull(prototype)) {
    // No identity hash for null, so just pick a random number.
    prototype_hash = 1;
  } else {
    Tagged<JSReceiver> receiver = Cast<JSReceiver>(prototype);
    Isolate* isolate = GetIsolateFromWritableObject(receiver);
    prototype_hash = receiver->GetOrCreateIdentityHash(isolate).value();
  }

  return prototype_hash ^ bit_field2();
}

namespace {

bool CheckEquivalentModuloProto(const Tagged<Map> first,
                                const Tagged<Map> second) {
  return first->GetConstructorRaw() == second->GetConstructorRaw() &&
         first->instance_type() == second->instance_type() &&
         first->bit_field() == second->bit_field() &&
         first->is_extensible() == second->is_extensible() &&
         first->new_target_is_base() == second->new_target_is_base();
}

}  // namespace

bool Map::EquivalentToForTransition(const Tagged<Map> other,
                                    ConcurrencyMode cmode,
                                    Handle<HeapObject> new_prototype) const {
  CHECK_EQ(GetConstructor(), other->GetConstructor());
  CHECK_EQ(instance_type(), other->instance_type());

  if (bit_field() != other->bit_field()) return false;
  if (new_prototype.is_null()) {
    if (prototype() != other->prototype()) return false;
  } else {
    if (*new_prototype != other->prototype()) return false;
  }
  if (new_target_is_base() != other->new_target_is_base()) return false;
  if (InstanceTypeChecker::IsJSFunction(instance_type())) {
    // JSFunctions require more checks to ensure that sloppy function is
    // not equivalent to strict function.
    int nof =
        std::min(NumberOfOwnDescriptors(), other->NumberOfOwnDescriptors());
    Tagged<DescriptorArray> this_descriptors =
        IsConcurrent(cmode) ? instance_descriptors(kAcquireLoad)
                            : instance_descriptors();
    Tagged<DescriptorArray> that_descriptors =
        IsConcurrent(cmode) ? other->instance_descriptors(kAcquireLoad)
                            : other->instance_descriptors();
    return this_descriptors->IsEqualUpTo(that_descriptors, nof);
  }
  return true;
}

bool Map::EquivalentToForElementsKindTransition(const Tagged<Map> other,
                                                ConcurrencyMode cmode) const {
  if (!EquivalentToForTransition(other, cmode)) {
    return false;
  }
#ifdef DEBUG
  // Ensure that we don't try to generate elements kind transitions from maps
  // with fields that may be generalized in-place. This must already be handled
  // during addition of a new field.
  Tagged<DescriptorArray> descriptors = IsConcurrent(cmode)
                                            ? instance_descriptors(kAcquireLoad)
                                            : instance_descriptors();
  for (InternalIndex i : IterateOwnDescriptors()) {
    PropertyDetails details = descriptors->GetDetails(i);
    if (details.location() == PropertyLocation::kField) {
      DCHECK(IsMostGeneralFieldType(details.representation(),
                                    descriptors->GetFieldType(i)));
    }
  }
#endif
  return true;
}

bool Map::EquivalentToForNormalization(const Tagged<Map> other,
                                       ElementsKind elements_kind,
                                       Tagged<HeapObject> other_prototype,
                                       PropertyNormalizationMode mode) const {
  int properties =
      mode == CLEAR_INOBJECT_PROPERTIES ? 0 : other->GetInObjectProperties();
  // Make sure the elements_kind bits are in bit_field2.
  DCHECK_EQ(this->elements_kind(),
            Map::Bits2::ElementsKindBits::decode(bit_field2()));
  int adjusted_other_bit_field2 =
      Map::Bits2::ElementsKindBits::update(other->bit_field2(), elements_kind);
  return CheckEquivalentModuloProto(*this, other) &&
         prototype() == other_prototype &&
         bit_field2() == adjusted_other_bit_field2 &&
         GetInObjectProperties() == properties &&
         JSObject::GetEmbedderFieldCount(*this) ==
             JSObject::GetEmbedderFieldCount(other);
}

int Map::ComputeMinObjectSlack(Isolate* isolate) {
  // Has to be an initial map.
  DCHECK(IsUndefined(GetBackPointer(), isolate));

  int slack = UnusedPropertyFields();
  TransitionsAccessor transitions(isolate, *this);
  TransitionsAccessor::TraverseCallback callback = [&](Tagged<Map> map) {
    slack = std::min(slack, map->UnusedPropertyFields());
  };
  transitions.TraverseTransitionTree(callback);
  return slack;
}

void Map::SetInstanceDescriptors(Isolate* isolate,
                                 Tagged<DescriptorArray> descriptors,
                                 int number_of_own_descriptors) {
  set_instance_descriptors(descriptors, kReleaseStore);
  SetNumberOfOwnDescriptors(number_of_own_descriptors);
#ifndef V8_DISABLE_WRITE_BARRIERS
  WriteBarrier::Marking(descriptors, number_of_own_descriptors);
#endif
}

// static
Handle<PrototypeInfo> Map::GetOrCreatePrototypeInfo(
    DirectHandle<JSObject> prototype, Isolate* isolate) {
  DCHECK(IsJSObjectThatCanBeTrackedAsPrototype(*prototype));
  {
    Tagged<PrototypeInfo> prototype_info;
    if (prototype->map()->TryGetPrototypeInfo(&prototype_info)) {
      return handle(prototype_info, isolate);
    }
  }
  Handle<PrototypeInfo> proto_info = isolate->factory()->NewPrototypeInfo();
  prototype->map()->set_prototype_info(*proto_info, kReleaseStore);
  return proto_info;
}

// static
Handle<PrototypeInfo> Map::GetOrCreatePrototypeInfo(
    DirectHandle<Map> prototype_map, Isolate* isolate) {
  {
    Tagged<Object> maybe_proto_info = prototype_map->prototype_info();
    if (PrototypeInfo::IsPrototypeInfoFast(maybe_proto_info)) {
      return handle(Cast<PrototypeInfo>(maybe_proto_info), isolate);
    }
  }
  Handle<PrototypeInfo> proto_info = isolate->factory()->NewPrototypeInfo();
  prototype_map->set_prototype_info(*proto_info, kReleaseStore);
  return proto_info;
}

// static
void Map::SetShouldBeFastPrototypeMap(DirectHandle<Map> map, bool value,
                                      Isolate* isolate) {
  DCHECK(map->is_prototype_map());
  if (value == false && !map->has_prototype_info()) {
    // "False" is the implicit default value, so there's nothing to do.
    return;
  }
  GetOrCreatePrototypeInfo(map, isolate)->set_should_be_fast_map(value);
}

// static
Handle<UnionOf<Smi, Cell>> Map::GetOrCreatePrototypeChainValidityCell(
    DirectHandle<Map> map, Isolate* isolate) {
  Handle<Object> maybe_prototype;
  if (IsJSGlobalObjectMap(*map)) {
    DCHECK(map->is_prototype_map());
    // Global object is prototype of a global proxy and therefore we can
    // use its validity cell for guarding global object's prototype change.
    maybe_prototype = isolate->global_object();
  } else {
    maybe_prototype =
        handle(map->GetPrototypeChainRootMap(isolate)->prototype(), isolate);
  }
  if (!IsJSObjectThatCanBeTrackedAsPrototype(*maybe_prototype)) {
    return handle(Smi::FromInt(Map::kPrototypeChainValid), isolate);
  }
  auto prototype = Cast<JSObject>(maybe_prototype);
  // Ensure the prototype is registered with its own prototypes so its cell
  // will be invalidated when necessary.
  JSObject::LazyRegisterPrototypeUser(handle(prototype->map(), isolate),
                                      isolate);

  Tagged<Object> maybe_cell =
      prototype->map()->prototype_validity_cell(kRelaxedLoad);
  // Return existing cell if it's still valid.
  if (IsCell(maybe_cell)) {
    Tagged<Cell> cell = Cast<Cell>(maybe_cell);
    if (cell->value() == Smi::FromInt(Map::kPrototypeChainValid)) {
      return handle(cell, isolate);
    }
  }
  // Otherwise create a new cell.
  Handle<Cell> cell =
      isolate->factory()->NewCell(Smi::FromInt(Map::kPrototypeChainValid));
  prototype->map()->set_prototype_validity_cell(*cell, kRelaxedStore);
  return cell;
}

// static
bool Map::IsPrototypeChainInvalidated(Tagged<Map> map) {
  DCHECK(map->is_prototype_map());
  Tagged<Object> maybe_cell = map->prototype_validity_cell(kRelaxedLoad);
  if (IsCell(maybe_cell)) {
    Tagged<Cell> cell = Cast<Cell>(maybe_cell);
    return cell->value() != Smi::FromInt(Map::kPrototypeChainValid);
  }
  return true;
}

// static
void Map::SetPrototype(Isolate* isolate, DirectHandle<Map> map,
                       Handle<HeapObject> prototype,
                       bool enable_prototype_setup_mode) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kMap_SetPrototype);

  if (IsJSObjectThatCanBeTrackedAsPrototype(*prototype)) {
    DirectHandle<JSObject> prototype_jsobj = Cast<JSObject>(prototype);
    JSObject::OptimizeAsPrototype(prototype_jsobj, enable_prototype_setup_mode);
  } else {
    DCHECK(IsNull(*prototype, isolate) || IsJSProxy(*prototype) ||
           IsWasmObject(*prototype) || InWritableSharedSpace(*prototype));
  }

  WriteBarrierMode wb_mode =
      IsNull(*prototype, isolate) ? SKIP_WRITE_BARRIER : UPDATE_WRITE_BARRIER;
  map->set_prototype(*prototype, wb_mode);
}

void Map::StartInobjectSlackTracking() {
  DCHECK(!this->IsInobjectSlackTrackingInProgress());
  if (UnusedPropertyFields() == 0) return;
  set_construction_counter(Map::kSlackTrackingCounterStart);
}

Handle<Map> Map::TransitionRootMapToPrototypeForNewObject(
    Isolate* isolate, Handle<Map> map, Handle<HeapObject> prototype) {
  DCHECK(IsUndefined(map->GetBackPointer()));
  Handle<Map> new_map = TransitionToUpdatePrototype(isolate, map, prototype);
  if (new_map->GetBackPointer() != *map &&
      map->IsInobjectSlackTrackingInProgress()) {
    // Advance the construction count on the base map to keep it in sync with
    // the transitioned map.
    map->InobjectSlackTrackingStep(isolate);
  }
  return new_map;
}

Handle<Map> Map::TransitionToUpdatePrototype(Isolate* isolate, Handle<Map> map,
                                             Handle<HeapObject> prototype) {
  Handle<Map> new_map;
  DCHECK_IMPLIES(v8_flags.move_prototype_transitions_first,
                 IsUndefined(map->GetBackPointer()));
  if (auto maybe_map = TransitionsAccessor::GetPrototypeTransition(
          isolate, *map, *prototype)) {
    new_map = handle(*maybe_map, isolate);
  } else {
    new_map = CopyForPrototypeTransition(isolate, map, prototype);
    if (!map->IsDetached(isolate)) {
      TransitionsAccessor::PutPrototypeTransition(isolate, map, prototype,
                                                  new_map);
    }
  }
  DCHECK_IMPLIES(map->IsInobjectSlackTrackingInProgress(),
                 new_map->IsInobjectSlackTrackingInProgress());
  CHECK_IMPLIES(map->IsInobjectSlackTrackingInProgress(),
                map->construction_counter() <= new_map->construction_counter());
  return new_map;
}

Handle<NormalizedMapCache> NormalizedMapCache::New(Isolate* isolate) {
  Handle<WeakFixedArray> array(
      isolate->factory()->NewWeakFixedArray(kEntries, AllocationType::kOld));
  return Cast<NormalizedMapCache>(array);
}

MaybeHandle<Map> NormalizedMapCache::Get(DirectHandle<Map> fast_map,
                                         ElementsKind elements_kind,
                                         Tagged<HeapObject> prototype,
                                         PropertyNormalizationMode mode) {
  DisallowGarbageCollection no_gc;
  Tagged<MaybeObject> value =
      WeakFixedArray::get(GetIndex(*fast_map, *prototype));
  Tagged<HeapObject> heap_object;
  if (!value.GetHeapObjectIfWeak(&heap_object)) {
    return MaybeHandle<Map>();
  }

  Tagged<Map> normalized_map = Cast<Map>(heap_object);
  CHECK(normalized_map->is_dictionary_map());
  if (!normalized_map->EquivalentToForNormalization(*fast_map, elements_kind,
                                                    prototype, mode)) {
    return MaybeHandle<Map>();
  }
  return handle(normalized_map, GetIsolate());
}

void NormalizedMapCache::Set(DirectHandle<Map> fast_map,
                             DirectHandle<Map> normalized_map) {
  DisallowGarbageCollection no_gc;
  DCHECK(normalized_map->is_dictionary_map());
  WeakFixedArray::set(GetIndex(*fast_map, normalized_map->prototype()),
                      MakeWeak(*normalized_map));
}

}  // namespace v8::internal
