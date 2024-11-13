// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-natives.h"
#include "src/api/api.h"
#include "src/builtins/accessors.h"
#include "src/codegen/compilation-cache.h"
#include "src/common/assert-scope.h"
#include "src/execution/isolate.h"
#include "src/execution/protectors.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/heap/new-spaces.h"
#include "src/ic/handler-configuration.h"
#include "src/init/heap-symbols.h"
#include "src/init/setup-isolate.h"
#include "src/interpreter/interpreter.h"
#include "src/objects/arguments.h"
#include "src/objects/call-site-info.h"
#include "src/objects/cell-inl.h"
#include "src/objects/contexts.h"
#include "src/objects/data-handler.h"
#include "src/objects/debug-objects.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/dictionary.h"
#include "src/objects/foreign.h"
#include "src/objects/heap-number.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-atomics-synchronization.h"
#include "src/objects/js-generator.h"
#include "src/objects/js-shared-array.h"
#include "src/objects/js-weak-refs.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/lookup-cache.h"
#include "src/objects/map.h"
#include "src/objects/microtask.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball-inl.h"
#include "src/objects/ordered-hash-table.h"
#include "src/objects/promise.h"
#include "src/objects/property-descriptor-object.h"
#include "src/objects/script.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/smi.h"
#include "src/objects/source-text-module.h"
#include "src/objects/string.h"
#include "src/objects/synthetic-module.h"
#include "src/objects/template-objects-inl.h"
#include "src/objects/templates.h"
#include "src/objects/torque-defined-classes-inl.h"
#include "src/objects/turbofan-types.h"
#include "src/objects/turboshaft-types.h"
#include "src/regexp/regexp.h"
#include "src/roots/roots.h"
#include "src/utils/allocation.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

namespace {

Handle<SharedFunctionInfo> CreateSharedFunctionInfo(
    Isolate* isolate, Builtin builtin, int len,
    FunctionKind kind = FunctionKind::kNormalFunction) {
  Handle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfoForBuiltin(
          isolate->factory()->empty_string(), builtin, kind);
  shared->set_internal_formal_parameter_count(JSParameterCount(len));
  shared->set_length(len);
  return shared;
}

#ifdef DEBUG
bool IsMutableMap(InstanceType instance_type, ElementsKind elements_kind) {
  bool is_js_object = InstanceTypeChecker::IsJSObject(instance_type);
  bool is_always_shared_space_js_object =
      InstanceTypeChecker::IsAlwaysSharedSpaceJSObject(instance_type);
  bool is_wasm_object = false;
#if V8_ENABLE_WEBASSEMBLY
  is_wasm_object =
      instance_type == WASM_STRUCT_TYPE || instance_type == WASM_ARRAY_TYPE;
#endif  // V8_ENABLE_WEBASSEMBLY
  DCHECK_IMPLIES(is_js_object &&
                     !Map::CanHaveFastTransitionableElementsKind(instance_type),
                 IsDictionaryElementsKind(elements_kind) ||
                     IsTerminalElementsKind(elements_kind) ||
                     (is_always_shared_space_js_object &&
                      elements_kind == SHARED_ARRAY_ELEMENTS));
  // JSObjects have maps with a mutable prototype_validity_cell, so they cannot
  // go in RO_SPACE. Maps for managed Wasm objects have mutable subtype lists.
  return (is_js_object && !is_always_shared_space_js_object) || is_wasm_object;
}
#endif

struct ConstantStringInit {
  const char* contents;
  RootIndex index;
};

constexpr std::initializer_list<ConstantStringInit>
#define CONSTANT_STRING_ELEMENT(_, name, contents) \
  {contents, RootIndex::k##name},
    kImportantConstantStringTable{
        EXTRA_IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(
            CONSTANT_STRING_ELEMENT, /* not used */)
            IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(
                CONSTANT_STRING_ELEMENT, /* not used */)
#undef CONSTANT_STRING_ELEMENT
    };

constexpr std::initializer_list<ConstantStringInit>
#define CONSTANT_STRING_ELEMENT(_, name, contents) \
  {contents, RootIndex::k##name},
    kNotImportantConstantStringTable{
        NOT_IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(
            CONSTANT_STRING_ELEMENT, /* not used */)
#undef CONSTANT_STRING_ELEMENT
    };

struct StringTypeInit {
  InstanceType type;
  int size;
  RootIndex index;
};

constexpr std::initializer_list<StringTypeInit> kStringTypeTable{
#define STRING_TYPE_ELEMENT(type, size, name, CamelName) \
  {type, size, RootIndex::k##CamelName##Map},
    STRING_TYPE_LIST(STRING_TYPE_ELEMENT)
#undef STRING_TYPE_ELEMENT
};

struct StructInit {
  InstanceType type;
  int size;
  RootIndex index;
};

constexpr bool is_important_struct(InstanceType type) {
  return type == ENUM_CACHE_TYPE || type == CALL_SITE_INFO_TYPE;
}

constexpr std::initializer_list<StructInit> kStructTable{
#define STRUCT_TABLE_ELEMENT(TYPE, Name, name) \
  {TYPE, Name::kSize, RootIndex::k##Name##Map},
    STRUCT_LIST(STRUCT_TABLE_ELEMENT)
#undef STRUCT_TABLE_ELEMENT
#define ALLOCATION_SITE_ELEMENT(_, TYPE, Name, Size, name) \
  {TYPE, Name::kSize##Size, RootIndex::k##Name##Size##Map},
        ALLOCATION_SITE_LIST(ALLOCATION_SITE_ELEMENT, /* not used */)
#undef ALLOCATION_SITE_ELEMENT
#define DATA_HANDLER_ELEMENT(_, TYPE, Name, Size, name) \
  {TYPE, Name::kSizeWithData##Size, RootIndex::k##Name##Size##Map},
            DATA_HANDLER_LIST(DATA_HANDLER_ELEMENT, /* not used */)
#undef DATA_HANDLER_ELEMENT
};

}  // namespace

bool SetupIsolateDelegate::SetupHeapInternal(Isolate* isolate) {
  auto heap = isolate->heap();
  if (!isolate->read_only_heap()->roots_init_complete()) {
    if (!heap->CreateReadOnlyHeapObjects()) return false;
    isolate->VerifyStaticRoots();
    isolate->read_only_heap()->OnCreateRootsComplete(isolate);
  }
  // We prefer to fit all of read-only space in one page.
  CHECK_EQ(heap->read_only_space()->pages().size(), 1);
  auto ro_size = heap->read_only_space()->Size();
  DCHECK_EQ(heap->old_space()->Size(), 0);
  DCHECK_IMPLIES(heap->new_space(), heap->new_space()->Size() == 0);
  auto res = heap->CreateMutableHeapObjects();
  DCHECK_EQ(heap->read_only_space()->Size(), ro_size);
  USE(ro_size);
  return res;
}

bool Heap::CreateReadOnlyHeapObjects() {
  // Create initial maps and important objects.
  if (!CreateEarlyReadOnlyMapsAndObjects()) return false;
  if (!CreateImportantReadOnlyObjects()) return false;

#if V8_STATIC_ROOTS_BOOL
  // The read only heap is sorted such that often used objects are allocated
  // early for their compressed address to fit into 12bit arm immediates.
  ReadOnlySpace* ro_space = isolate()->heap()->read_only_space();
  DCHECK_LT(V8HeapCompressionScheme::CompressAny(ro_space->top()), 0xfff);
  USE(ro_space);
#endif

  if (!CreateLateReadOnlyNonJSReceiverMaps()) return false;
  CreateReadOnlyApiObjects();
  if (!CreateReadOnlyObjects()) return false;

  // Order is important. JSReceiver maps must come after all non-JSReceiver maps
  // in RO space with a sufficiently large gap in address. Currently there are
  // no JSReceiver instances in RO space.
  //
  // See InstanceTypeChecker::kNonJsReceiverMapLimit.
  if (!CreateLateReadOnlyJSReceiverMaps()) return false;

#ifdef DEBUG
  ReadOnlyRoots roots(isolate());
  for (auto pos = RootIndex::kFirstReadOnlyRoot;
       pos <= RootIndex::kLastReadOnlyRoot; ++pos) {
    DCHECK(roots.is_initialized(pos));
  }
#endif
  return true;
}

bool Heap::CreateMutableHeapObjects() {
  ReadOnlyRoots roots(this);

#define ALLOCATE_MAP(instance_type, size, field_name)                       \
  {                                                                         \
    Tagged<Map> map;                                                        \
    if (!AllocateMap(AllocationType::kMap, (instance_type), size).To(&map)) \
      return false;                                                         \
    set_##field_name##_map(map);                                            \
  }

  {  // Map allocation
    ALLOCATE_MAP(JS_MESSAGE_OBJECT_TYPE, JSMessageObject::kHeaderSize,
                 message_object)
    ALLOCATE_MAP(JS_EXTERNAL_OBJECT_TYPE, JSExternalObject::kHeaderSize,
                 external)
    external_map()->set_is_extensible(false);
  }
#undef ALLOCATE_MAP

  // Ensure that all young generation pages are iterable. It must be after heap
  // setup, so that the maps have been created.
  if (new_space()) new_space()->MakeIterable();

  CreateMutableApiObjects();

  // Create initial objects
  CreateInitialMutableObjects();
  CreateInternalAccessorInfoObjects();
  CHECK_EQ(0u, gc_count_);

  set_native_contexts_list(roots.undefined_value());
  set_allocation_sites_list(roots.undefined_value());
  set_dirty_js_finalization_registries_list(roots.undefined_value());
  set_dirty_js_finalization_registries_list_tail(roots.undefined_value());

  return true;
}

// Allocates contextless map in read-only or map (old) space.
AllocationResult Heap::AllocateMap(AllocationType allocation_type,
                                   InstanceType instance_type,
                                   int instance_size,
                                   ElementsKind elements_kind,
                                   int inobject_properties) {
  static_assert(LAST_JS_OBJECT_TYPE == LAST_TYPE);
  Tagged<HeapObject> result;
  DCHECK_EQ(allocation_type, IsMutableMap(instance_type, elements_kind)
                                 ? AllocationType::kMap
                                 : AllocationType::kReadOnly);
  AllocationResult allocation = AllocateRaw(Map::kSize, allocation_type);
  if (!allocation.To(&result)) return allocation;

  ReadOnlyRoots roots(this);
  result->set_map_after_allocation(roots.meta_map(), SKIP_WRITE_BARRIER);
  Tagged<Map> map = isolate()->factory()->InitializeMap(
      Cast<Map>(result), instance_type, instance_size, elements_kind,
      inobject_properties, roots);

  return AllocationResult::FromObject(map);
}

namespace {
void InitializePartialMap(Tagged<Map> map, Tagged<Map> meta_map,
                          InstanceType instance_type, int instance_size) {
  map->set_map_after_allocation(meta_map, SKIP_WRITE_BARRIER);
  map->set_instance_type(instance_type);
  map->set_instance_size(instance_size);
  map->set_visitor_id(Map::GetVisitorId(map));
  map->set_inobject_properties_start_or_constructor_function_index(0);
  DCHECK(!IsJSObjectMap(map));
  map->set_prototype_validity_cell(Smi::FromInt(Map::kPrototypeChainValid),
                                   kRelaxedStore);
  map->SetInObjectUnusedPropertyFields(0);
  map->set_bit_field(0);
  map->set_bit_field2(0);
  int bit_field3 =
      Map::Bits3::EnumLengthBits::encode(kInvalidEnumCacheSentinel) |
      Map::Bits3::OwnsDescriptorsBit::encode(true) |
      Map::Bits3::ConstructionCounterBits::encode(Map::kNoSlackTracking);
  map->set_bit_field3(bit_field3);
  DCHECK(!map->is_in_retained_map_list());
  map->clear_padding();
  map->set_elements_kind(TERMINAL_FAST_ELEMENTS_KIND);
}
}  // namespace

AllocationResult Heap::AllocatePartialMap(InstanceType instance_type,
                                          int instance_size) {
  Tagged<Object> result;
  AllocationResult allocation =
      AllocateRaw(Map::kSize, AllocationType::kReadOnly);
  if (!allocation.To(&result)) return allocation;
  // Cast<Map> cannot be used due to uninitialized map field.
  Tagged<Map> map = UncheckedCast<Map>(result);
  InitializePartialMap(map,
                       UncheckedCast<Map>(isolate()->root(RootIndex::kMetaMap)),
                       instance_type, instance_size);
  return AllocationResult::FromObject(map);
}

void Heap::FinalizePartialMap(Tagged<Map> map) {
  ReadOnlyRoots roots(this);
  map->set_dependent_code(DependentCode::empty_dependent_code(roots));
  map->set_raw_transitions(Smi::zero());
  map->SetInstanceDescriptors(isolate(), roots.empty_descriptor_array(), 0);
  map->init_prototype_and_constructor_or_back_pointer(roots);
}

AllocationResult Heap::Allocate(DirectHandle<Map> map,
                                AllocationType allocation_type) {
  DCHECK(map->instance_type() != MAP_TYPE);
  int size = map->instance_size();
  Tagged<HeapObject> result;
  AllocationResult allocation = AllocateRaw(size, allocation_type);
  if (!allocation.To(&result)) return allocation;
  // New space objects are allocated white.
  WriteBarrierMode write_barrier_mode =
      allocation_type == AllocationType::kYoung ? SKIP_WRITE_BARRIER
                                                : UPDATE_WRITE_BARRIER;
  result->set_map_after_allocation(*map, write_barrier_mode);
  return AllocationResult::FromObject(result);
}

bool Heap::CreateEarlyReadOnlyMapsAndObjects() {
  // Setup maps and objects which are used often, or used in
  // CreateImportantReadOnlyObjects.
  ReadOnlyRoots roots(this);

  // First create the following, in the following order:
  //   - Undefined value
  //   - Null value
  //   - Empty string
  //   - False value
  //   - True value
  //   - /String maps
  //     \...
  //   - Symbol map
  //   - Meta-map
  //   - Undefined map
  //   - Null map
  //   - Boolean map
  //
  // This is so that:
  //   1. The falsy values are the first in the space, allowing ToBoolean false
  //      checks to be a single less-than.
  //   2. The true value is immediately after the falsy values, so that we can
  //      use a single compare's condition flags to check both falsy and true.
  //   3. The string maps are all together, and are the first maps, allowing
  //      them to be checked with a single less-than if we know we have a map.
  //   4. The symbol map is with the string maps, for similarly fast Name
  //      checks.

  Tagged<HeapObject> obj;
  {
    // We're a bit loose with raw pointers here for readability -- this is all
    // guaranteed to be safe anyway since the allocations can't cause a GC, so
    // disable gcmole in this range.
    DisableGCMole no_gc_mole;

    // First, set up the roots to all point to the right offset in the
    // allocation folded allocation.
#define ALLOCATE_AND_SET_ROOT(Type, name, Size)                            \
  {                                                                        \
    AllocationResult alloc = AllocateRaw(Size, AllocationType::kReadOnly); \
    if (!alloc.To(&obj)) return false;                                     \
  }                                                                        \
  Tagged<Type> name = UncheckedCast<Type>(obj);                            \
  set_##name(name)

    ALLOCATE_AND_SET_ROOT(Undefined, undefined_value, sizeof(Undefined));
    ALLOCATE_AND_SET_ROOT(Null, null_value, sizeof(Null));
    ALLOCATE_AND_SET_ROOT(SeqOneByteString, empty_string,
                          SeqOneByteString::SizeFor(0));
    ALLOCATE_AND_SET_ROOT(False, false_value, sizeof(False));
    ALLOCATE_AND_SET_ROOT(True, true_value, sizeof(True));

    for (const StringTypeInit& entry : kStringTypeTable) {
      {
        AllocationResult alloc =
            AllocateRaw(Map::kSize, AllocationType::kReadOnly);
        if (!alloc.To(&obj)) return false;
      }
      Tagged<Map> map = UncheckedCast<Map>(obj);
      roots_table()[entry.index] = map.ptr();
    }
    ALLOCATE_AND_SET_ROOT(Map, symbol_map, Map::kSize);

    ALLOCATE_AND_SET_ROOT(Map, meta_map, Map::kSize);
    // Keep HeapNumber and Oddball maps together for cheap NumberOrOddball
    // checks.
    ALLOCATE_AND_SET_ROOT(Map, undefined_map, Map::kSize);
    ALLOCATE_AND_SET_ROOT(Map, null_map, Map::kSize);
    // Keep HeapNumber and Boolean maps together for cheap NumberOrBoolean
    // checks.
    ALLOCATE_AND_SET_ROOT(Map, boolean_map, Map::kSize);
    // Keep HeapNumber and BigInt maps together for cheaper numerics checks.
    ALLOCATE_AND_SET_ROOT(Map, heap_number_map, Map::kSize);
    ALLOCATE_AND_SET_ROOT(Map, bigint_map, Map::kSize);

#undef ALLOCATE_AND_SET_ROOT

    // Then, initialise the initial maps.
    InitializePartialMap(meta_map, meta_map, MAP_TYPE, Map::kSize);
    InitializePartialMap(undefined_map, meta_map, ODDBALL_TYPE,
                         sizeof(Undefined));
    InitializePartialMap(null_map, meta_map, ODDBALL_TYPE, sizeof(Null));
    InitializePartialMap(boolean_map, meta_map, ODDBALL_TYPE, sizeof(Boolean));
    boolean_map->SetConstructorFunctionIndex(Context::BOOLEAN_FUNCTION_INDEX);
    InitializePartialMap(heap_number_map, meta_map, HEAP_NUMBER_TYPE,
                         sizeof(HeapNumber));
    heap_number_map->SetConstructorFunctionIndex(
        Context::NUMBER_FUNCTION_INDEX);
    InitializePartialMap(bigint_map, meta_map, BIGINT_TYPE,
                         kVariableSizeSentinel);

    for (const StringTypeInit& entry : kStringTypeTable) {
      Tagged<Map> map = UncheckedCast<Map>(roots.object_at(entry.index));
      InitializePartialMap(map, meta_map, entry.type, entry.size);
      map->SetConstructorFunctionIndex(Context::STRING_FUNCTION_INDEX);
      if (StringShape(entry.type).IsCons()) map->mark_unstable();
    }
    InitializePartialMap(symbol_map, meta_map, SYMBOL_TYPE, sizeof(Symbol));
    symbol_map->SetConstructorFunctionIndex(Context::SYMBOL_FUNCTION_INDEX);

    // Finally, initialise the non-map objects using those maps.
    undefined_value->set_map_after_allocation(undefined_map,
                                              SKIP_WRITE_BARRIER);
    undefined_value->set_kind(Oddball::kUndefined);

    null_value->set_map_after_allocation(null_map, SKIP_WRITE_BARRIER);
    null_value->set_kind(Oddball::kNull);

    true_value->set_map_after_allocation(boolean_map, SKIP_WRITE_BARRIER);
    true_value->set_kind(Oddball::kTrue);

    false_value->set_map_after_allocation(boolean_map, SKIP_WRITE_BARRIER);
    false_value->set_kind(Oddball::kFalse);

    // The empty string is initialised with an empty hash despite being
    // internalized -- this will be calculated once the hashseed is available.
    // TODO(leszeks): Unify this initialisation with normal string
    // initialisation.
    empty_string->set_map_after_allocation(
        roots.unchecked_internalized_one_byte_string_map(), SKIP_WRITE_BARRIER);
    empty_string->clear_padding_destructively(0);
    empty_string->set_length(0);
    empty_string->set_raw_hash_field(String::kEmptyHashField);
  }

  // Now that the initial objects are allocated, we can start allocating other
  // objects where the order matters less.

#define ALLOCATE_PARTIAL_MAP(instance_type, size, field_name)                \
  {                                                                          \
    Tagged<Map> map;                                                         \
    if (!AllocatePartialMap((instance_type), (size)).To(&map)) return false; \
    set_##field_name##_map(map);                                             \
  }

  {  // Partial map allocation
    ALLOCATE_PARTIAL_MAP(FIXED_ARRAY_TYPE, kVariableSizeSentinel, fixed_array);
    ALLOCATE_PARTIAL_MAP(TRUSTED_FIXED_ARRAY_TYPE, kVariableSizeSentinel,
                         trusted_fixed_array);
    ALLOCATE_PARTIAL_MAP(PROTECTED_FIXED_ARRAY_TYPE, kVariableSizeSentinel,
                         protected_fixed_array);
    ALLOCATE_PARTIAL_MAP(WEAK_FIXED_ARRAY_TYPE, kVariableSizeSentinel,
                         weak_fixed_array);
    ALLOCATE_PARTIAL_MAP(TRUSTED_WEAK_FIXED_ARRAY_TYPE, kVariableSizeSentinel,
                         trusted_weak_fixed_array);
    ALLOCATE_PARTIAL_MAP(WEAK_ARRAY_LIST_TYPE, kVariableSizeSentinel,
                         weak_array_list);
    ALLOCATE_PARTIAL_MAP(FIXED_ARRAY_TYPE, kVariableSizeSentinel,
                         fixed_cow_array)
    DCHECK_NE(roots.fixed_array_map(), roots.fixed_cow_array_map());

    ALLOCATE_PARTIAL_MAP(DESCRIPTOR_ARRAY_TYPE, kVariableSizeSentinel,
                         descriptor_array)

    ALLOCATE_PARTIAL_MAP(HOLE_TYPE, Hole::kSize, hole);

    // Some struct maps which we need for later dependencies
    for (const StructInit& entry : kStructTable) {
      if (!is_important_struct(entry.type)) continue;
      Tagged<Map> map;
      if (!AllocatePartialMap(entry.type, entry.size).To(&map)) return false;
      roots_table()[entry.index] = map.ptr();
    }
  }
#undef ALLOCATE_PARTIAL_MAP

  {
    AllocationResult alloc =
        AllocateRaw(FixedArray::SizeFor(0), AllocationType::kReadOnly);
    if (!alloc.To(&obj)) return false;
    obj->set_map_after_allocation(roots.fixed_array_map(), SKIP_WRITE_BARRIER);
    Cast<FixedArray>(obj)->set_length(0);
  }
  set_empty_fixed_array(Cast<FixedArray>(obj));

  {
    AllocationResult alloc =
        AllocateRaw(WeakFixedArray::SizeFor(0), AllocationType::kReadOnly);
    if (!alloc.To(&obj)) return false;
    obj->set_map_after_allocation(roots.weak_fixed_array_map(),
                                  SKIP_WRITE_BARRIER);
    Cast<WeakFixedArray>(obj)->set_length(0);
  }
  set_empty_weak_fixed_array(Cast<WeakFixedArray>(obj));

  {
    AllocationResult allocation = AllocateRaw(WeakArrayList::SizeForCapacity(0),
                                              AllocationType::kReadOnly);
    if (!allocation.To(&obj)) return false;
    obj->set_map_after_allocation(roots.weak_array_list_map(),
                                  SKIP_WRITE_BARRIER);
    Cast<WeakArrayList>(obj)->set_capacity(0);
    Cast<WeakArrayList>(obj)->set_length(0);
  }
  set_empty_weak_array_list(Cast<WeakArrayList>(obj));

  DCHECK(!InYoungGeneration(roots.undefined_value()));
  {
    AllocationResult allocation =
        Allocate(roots.hole_map_handle(), AllocationType::kReadOnly);
    if (!allocation.To(&obj)) return false;
  }
  set_the_hole_value(Cast<Hole>(obj));

  // Set preliminary exception sentinel value before actually initializing it.
  set_exception(Cast<Hole>(obj));

  // Allocate the empty enum cache.
  {
    AllocationResult allocation =
        Allocate(roots.enum_cache_map_handle(), AllocationType::kReadOnly);
    if (!allocation.To(&obj)) return false;
  }
  set_empty_enum_cache(Cast<EnumCache>(obj));
  Cast<EnumCache>(obj)->set_keys(roots.empty_fixed_array());
  Cast<EnumCache>(obj)->set_indices(roots.empty_fixed_array());

  // Allocate the empty descriptor array.
  {
    int size = DescriptorArray::SizeFor(0);
    if (!AllocateRaw(size, AllocationType::kReadOnly).To(&obj)) return false;
    obj->set_map_after_allocation(roots.descriptor_array_map(),
                                  SKIP_WRITE_BARRIER);
    Tagged<DescriptorArray> array = Cast<DescriptorArray>(obj);
    array->Initialize(roots.empty_enum_cache(), roots.undefined_value(), 0, 0,
                      DescriptorArrayMarkingState::kInitialGCState);
  }
  set_empty_descriptor_array(Cast<DescriptorArray>(obj));

  // Fix the instance_descriptors for the existing maps.
  FinalizePartialMap(roots.meta_map());
  FinalizePartialMap(roots.fixed_array_map());
  FinalizePartialMap(roots.trusted_fixed_array_map());
  FinalizePartialMap(roots.protected_fixed_array_map());
  FinalizePartialMap(roots.weak_fixed_array_map());
  FinalizePartialMap(roots.weak_array_list_map());
  FinalizePartialMap(roots.trusted_weak_fixed_array_map());
  FinalizePartialMap(roots.fixed_cow_array_map());
  FinalizePartialMap(roots.descriptor_array_map());
  FinalizePartialMap(roots.undefined_map());
  roots.undefined_map()->set_is_undetectable(true);
  FinalizePartialMap(roots.null_map());
  roots.null_map()->set_is_undetectable(true);
  FinalizePartialMap(roots.boolean_map());
  FinalizePartialMap(roots.heap_number_map());
  FinalizePartialMap(roots.bigint_map());
  FinalizePartialMap(roots.hole_map());
  FinalizePartialMap(roots.symbol_map());
  for (const StructInit& entry : kStructTable) {
    if (!is_important_struct(entry.type)) continue;
    FinalizePartialMap(Cast<Map>(roots.object_at(entry.index)));
  }
  for (const StringTypeInit& entry : kStringTypeTable) {
    FinalizePartialMap(Cast<Map>(roots.object_at(entry.index)));
  }

#define ALLOCATE_MAP(instance_type, size, field_name)                  \
  {                                                                    \
    Tagged<Map> map;                                                   \
    if (!AllocateMap(AllocationType::kReadOnly, (instance_type), size) \
             .To(&map)) {                                              \
      return false;                                                    \
    }                                                                  \
    set_##field_name##_map(map);                                       \
  }

#define ALLOCATE_VARSIZE_MAP(instance_type, field_name) \
  ALLOCATE_MAP(instance_type, kVariableSizeSentinel, field_name)

#define ALLOCATE_PRIMITIVE_MAP(instance_type, size, field_name, \
                               constructor_function_index)      \
  {                                                             \
    ALLOCATE_MAP((instance_type), (size), field_name);          \
    roots.field_name##_map()->SetConstructorFunctionIndex(      \
        (constructor_function_index));                          \
  }

  {  // Map allocation
    ALLOCATE_VARSIZE_MAP(SCOPE_INFO_TYPE, scope_info)
    ALLOCATE_VARSIZE_MAP(FIXED_ARRAY_TYPE, module_info)
    ALLOCATE_VARSIZE_MAP(CLOSURE_FEEDBACK_CELL_ARRAY_TYPE,
                         closure_feedback_cell_array)
    ALLOCATE_VARSIZE_MAP(FEEDBACK_VECTOR_TYPE, feedback_vector)

    ALLOCATE_MAP(FOREIGN_TYPE, Foreign::kSize, foreign)
    ALLOCATE_MAP(TRUSTED_FOREIGN_TYPE, TrustedForeign::kSize, trusted_foreign)
    ALLOCATE_MAP(MEGA_DOM_HANDLER_TYPE, MegaDomHandler::kSize, mega_dom_handler)

    ALLOCATE_VARSIZE_MAP(FIXED_DOUBLE_ARRAY_TYPE, fixed_double_array)
    roots.fixed_double_array_map()->set_elements_kind(HOLEY_DOUBLE_ELEMENTS);
    ALLOCATE_VARSIZE_MAP(FEEDBACK_METADATA_TYPE, feedback_metadata)
    ALLOCATE_VARSIZE_MAP(BYTE_ARRAY_TYPE, byte_array)
    ALLOCATE_VARSIZE_MAP(TRUSTED_BYTE_ARRAY_TYPE, trusted_byte_array)
    ALLOCATE_VARSIZE_MAP(BYTECODE_ARRAY_TYPE, bytecode_array)
    ALLOCATE_VARSIZE_MAP(FREE_SPACE_TYPE, free_space)
    ALLOCATE_VARSIZE_MAP(PROPERTY_ARRAY_TYPE, property_array)
    ALLOCATE_VARSIZE_MAP(SMALL_ORDERED_HASH_MAP_TYPE, small_ordered_hash_map)
    ALLOCATE_VARSIZE_MAP(SMALL_ORDERED_HASH_SET_TYPE, small_ordered_hash_set)
    ALLOCATE_VARSIZE_MAP(SMALL_ORDERED_NAME_DICTIONARY_TYPE,
                         small_ordered_name_dictionary)

    ALLOCATE_VARSIZE_MAP(INSTRUCTION_STREAM_TYPE, instruction_stream)

    ALLOCATE_MAP(CELL_TYPE, Cell::kSize, cell);
    {
      // The invalid_prototype_validity_cell is needed for JSObject maps.
      Tagged<Smi> value = Smi::FromInt(Map::kPrototypeChainInvalid);
      AllocationResult alloc =
          AllocateRaw(Cell::kSize, AllocationType::kReadOnly);
      if (!alloc.To(&obj)) return false;
      obj->set_map_after_allocation(roots.cell_map(), SKIP_WRITE_BARRIER);
      Cast<Cell>(obj)->set_value(value);
      set_invalid_prototype_validity_cell(Cast<Cell>(obj));
    }

    ALLOCATE_MAP(PROPERTY_CELL_TYPE, PropertyCell::kSize, global_property_cell)
    ALLOCATE_MAP(FILLER_TYPE, kTaggedSize, one_pointer_filler)
    ALLOCATE_MAP(FILLER_TYPE, 2 * kTaggedSize, two_pointer_filler)

    // The "no closures" and "one closure" FeedbackCell maps need
    // to be marked unstable because their objects can change maps.
    ALLOCATE_MAP(FEEDBACK_CELL_TYPE, FeedbackCell::kAlignedSize,
                 no_closures_cell)
    roots.no_closures_cell_map()->mark_unstable();
    ALLOCATE_MAP(FEEDBACK_CELL_TYPE, FeedbackCell::kAlignedSize,
                 one_closure_cell)
    roots.one_closure_cell_map()->mark_unstable();
    ALLOCATE_MAP(FEEDBACK_CELL_TYPE, FeedbackCell::kAlignedSize,
                 many_closures_cell)

    ALLOCATE_VARSIZE_MAP(TRANSITION_ARRAY_TYPE, transition_array)

    ALLOCATE_VARSIZE_MAP(HASH_TABLE_TYPE, hash_table)
    ALLOCATE_VARSIZE_MAP(ORDERED_NAME_DICTIONARY_TYPE, ordered_name_dictionary)
    ALLOCATE_VARSIZE_MAP(NAME_DICTIONARY_TYPE, name_dictionary)
    ALLOCATE_VARSIZE_MAP(SWISS_NAME_DICTIONARY_TYPE, swiss_name_dictionary)
    ALLOCATE_VARSIZE_MAP(GLOBAL_DICTIONARY_TYPE, global_dictionary)
    ALLOCATE_VARSIZE_MAP(NUMBER_DICTIONARY_TYPE, number_dictionary)

    ALLOCATE_VARSIZE_MAP(REGISTERED_SYMBOL_TABLE_TYPE, registered_symbol_table)

    ALLOCATE_VARSIZE_MAP(ARRAY_LIST_TYPE, array_list)

    ALLOCATE_MAP(ACCESSOR_INFO_TYPE, AccessorInfo::kSize, accessor_info)

    ALLOCATE_VARSIZE_MAP(PREPARSE_DATA_TYPE, preparse_data)
    ALLOCATE_MAP(SHARED_FUNCTION_INFO_TYPE, SharedFunctionInfo::kSize,
                 shared_function_info)
    ALLOCATE_MAP(CODE_TYPE, Code::kSize, code)

    return true;
  }
}

bool Heap::CreateLateReadOnlyNonJSReceiverMaps() {
  ReadOnlyRoots roots(this);
  {
    // Setup the struct maps.
    for (const StructInit& entry : kStructTable) {
      if (is_important_struct(entry.type)) continue;
      Tagged<Map> map;
      if (!AllocateMap(AllocationType::kReadOnly, entry.type, entry.size)
               .To(&map))
        return false;
      roots_table()[entry.index] = map.ptr();
    }

#define TORQUE_ALLOCATE_MAP(NAME, Name, name) \
  ALLOCATE_MAP(NAME, Name::SizeFor(), name)
    TORQUE_DEFINED_FIXED_INSTANCE_TYPE_LIST(TORQUE_ALLOCATE_MAP);
#undef TORQUE_ALLOCATE_MAP

#define TORQUE_ALLOCATE_VARSIZE_MAP(NAME, Name, name)                   \
  /* The DescriptorArray map is pre-allocated and initialized above. */ \
  if (NAME != DESCRIPTOR_ARRAY_TYPE) {                                  \
    ALLOCATE_VARSIZE_MAP(NAME, name)                                    \
  }
    TORQUE_DEFINED_VARSIZE_INSTANCE_TYPE_LIST(TORQUE_ALLOCATE_VARSIZE_MAP);
#undef TORQUE_ALLOCATE_VARSIZE_MAP

    ALLOCATE_VARSIZE_MAP(ORDERED_HASH_MAP_TYPE, ordered_hash_map)
    ALLOCATE_VARSIZE_MAP(ORDERED_HASH_SET_TYPE, ordered_hash_set)

    ALLOCATE_VARSIZE_MAP(SIMPLE_NUMBER_DICTIONARY_TYPE,
                         simple_number_dictionary)
    ALLOCATE_VARSIZE_MAP(NAME_TO_INDEX_HASH_TABLE_TYPE,
                         name_to_index_hash_table)

    ALLOCATE_VARSIZE_MAP(EMBEDDER_DATA_ARRAY_TYPE, embedder_data_array)
    ALLOCATE_VARSIZE_MAP(EPHEMERON_HASH_TABLE_TYPE, ephemeron_hash_table)

    ALLOCATE_VARSIZE_MAP(SCRIPT_CONTEXT_TABLE_TYPE, script_context_table)

    ALLOCATE_VARSIZE_MAP(OBJECT_BOILERPLATE_DESCRIPTION_TYPE,
                         object_boilerplate_description)

    ALLOCATE_VARSIZE_MAP(COVERAGE_INFO_TYPE, coverage_info);
    ALLOCATE_VARSIZE_MAP(REG_EXP_MATCH_INFO_TYPE, regexp_match_info);

    ALLOCATE_MAP(REG_EXP_DATA_TYPE, RegExpData::kSize, regexp_data);
    ALLOCATE_MAP(ATOM_REG_EXP_DATA_TYPE, AtomRegExpData::kSize,
                 atom_regexp_data);
    ALLOCATE_MAP(IR_REG_EXP_DATA_TYPE, IrRegExpData::kSize, ir_regexp_data);

    ALLOCATE_MAP(SOURCE_TEXT_MODULE_TYPE, SourceTextModule::kSize,
                 source_text_module)
    ALLOCATE_MAP(SYNTHETIC_MODULE_TYPE, SyntheticModule::kSize,
                 synthetic_module)

    ALLOCATE_MAP(CONST_TRACKING_LET_CELL_TYPE, ConstTrackingLetCell::kSize,
                 global_const_tracking_let_cell)

    IF_WASM(ALLOCATE_MAP, WASM_IMPORT_DATA_TYPE, WasmImportData::kSize,
            wasm_import_data)
    IF_WASM(ALLOCATE_MAP, WASM_CAPI_FUNCTION_DATA_TYPE,
            WasmCapiFunctionData::kSize, wasm_capi_function_data)
    IF_WASM(ALLOCATE_MAP, WASM_EXPORTED_FUNCTION_DATA_TYPE,
            WasmExportedFunctionData::kSize, wasm_exported_function_data)
    IF_WASM(ALLOCATE_MAP, WASM_INTERNAL_FUNCTION_TYPE,
            WasmInternalFunction::kSize, wasm_internal_function)
    IF_WASM(ALLOCATE_MAP, WASM_FUNC_REF_TYPE, WasmFuncRef::kSize, wasm_func_ref)
    IF_WASM(ALLOCATE_MAP, WASM_JS_FUNCTION_DATA_TYPE, WasmJSFunctionData::kSize,
            wasm_js_function_data)
    IF_WASM(ALLOCATE_MAP, WASM_RESUME_DATA_TYPE, WasmResumeData::kSize,
            wasm_resume_data)
    IF_WASM(ALLOCATE_MAP, WASM_SUSPENDER_OBJECT_TYPE,
            WasmSuspenderObject::kSize, wasm_suspender_object)
    IF_WASM(ALLOCATE_MAP, WASM_TYPE_INFO_TYPE, kVariableSizeSentinel,
            wasm_type_info)
    IF_WASM(ALLOCATE_MAP, WASM_CONTINUATION_OBJECT_TYPE,
            WasmContinuationObject::kSize, wasm_continuation_object)
    IF_WASM(ALLOCATE_MAP, WASM_NULL_TYPE, kVariableSizeSentinel, wasm_null);
    IF_WASM(ALLOCATE_MAP, WASM_TRUSTED_INSTANCE_DATA_TYPE,
            WasmTrustedInstanceData::kSize, wasm_trusted_instance_data);
    IF_WASM(ALLOCATE_VARSIZE_MAP, WASM_DISPATCH_TABLE_TYPE,
            wasm_dispatch_table);

    ALLOCATE_MAP(WEAK_CELL_TYPE, WeakCell::kSize, weak_cell)
    ALLOCATE_VARSIZE_MAP(EXTERNAL_POINTER_ARRAY_TYPE, external_pointer_array)
    ALLOCATE_MAP(INTERPRETER_DATA_TYPE, InterpreterData::kSize,
                 interpreter_data)
    ALLOCATE_MAP(SHARED_FUNCTION_INFO_WRAPPER_TYPE,
                 SharedFunctionInfoWrapper::kSize, shared_function_info_wrapper)

    ALLOCATE_MAP(DICTIONARY_TEMPLATE_INFO_TYPE, DictionaryTemplateInfo::kSize,
                 dictionary_template_info)
  }

  return true;
}

bool Heap::CreateLateReadOnlyJSReceiverMaps() {
#define ALLOCATE_ALWAYS_SHARED_SPACE_JSOBJECT_MAP(instance_type, size, \
                                                  field_name)          \
  {                                                                    \
    Tagged<Map> map;                                                   \
    if (!AllocateMap(AllocationType::kReadOnly, (instance_type), size, \
                     DICTIONARY_ELEMENTS)                              \
             .To(&map)) {                                              \
      return false;                                                    \
    }                                                                  \
    AlwaysSharedSpaceJSObject::PrepareMapNoEnumerableProperties(map);  \
    set_##field_name##_map(map);                                       \
  }

  HandleScope late_jsreceiver_maps_handle_scope(isolate());
  Factory* factory = isolate()->factory();
  ReadOnlyRoots roots(this);

  // Shared space object maps are immutable and can be in RO space.
  {
    Tagged<Map> shared_array_map;
    if (!AllocateMap(AllocationType::kReadOnly, JS_SHARED_ARRAY_TYPE,
                     JSSharedArray::kSize, SHARED_ARRAY_ELEMENTS,
                     JSSharedArray::kInObjectFieldCount)
             .To(&shared_array_map)) {
      return false;
    }
    AlwaysSharedSpaceJSObject::PrepareMapNoEnumerableProperties(
        shared_array_map);
    DirectHandle<DescriptorArray> descriptors =
        factory->NewDescriptorArray(1, 0, AllocationType::kReadOnly);
    Descriptor length_descriptor = Descriptor::DataField(
        factory->length_string(), JSSharedArray::kLengthFieldIndex,
        ALL_ATTRIBUTES_MASK, PropertyConstness::kConst, Representation::Smi(),
        MaybeObjectHandle(FieldType::Any(isolate())));
    descriptors->Set(InternalIndex(0), &length_descriptor);
    shared_array_map->InitializeDescriptors(isolate(), *descriptors);
    set_js_shared_array_map(shared_array_map);
  }

  ALLOCATE_ALWAYS_SHARED_SPACE_JSOBJECT_MAP(
      JS_ATOMICS_MUTEX_TYPE, JSAtomicsMutex::kHeaderSize, js_atomics_mutex)
  ALLOCATE_ALWAYS_SHARED_SPACE_JSOBJECT_MAP(JS_ATOMICS_CONDITION_TYPE,
                                            JSAtomicsCondition::kHeaderSize,
                                            js_atomics_condition)

#undef ALLOCATE_ALWAYS_SHARED_SPACE_JSOBJECT_MAP
#undef ALLOCATE_PRIMITIVE_MAP
#undef ALLOCATE_VARSIZE_MAP
#undef ALLOCATE_MAP

  return true;
}

// For static roots we need the r/o space to have identical layout on all
// compile targets. Varying objects are padded to their biggest size.
void Heap::StaticRootsEnsureAllocatedSize(DirectHandle<HeapObject> obj,
                                          int required) {
  if (V8_STATIC_ROOTS_BOOL || V8_STATIC_ROOTS_GENERATION_BOOL) {
    int obj_size = obj->Size();
    if (required == obj_size) return;
    CHECK_LT(obj_size, required);
    int filler_size = required - obj_size;

    Tagged<HeapObject> filler =
        allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(
            filler_size, AllocationType::kReadOnly, AllocationOrigin::kRuntime,
            AllocationAlignment::kTaggedAligned);
    CreateFillerObjectAt(filler.address(), filler_size,
                         ClearFreedMemoryMode::kClearFreedMemory);

    CHECK_EQ(filler.address(), obj->address() + obj_size);
    CHECK_EQ(filler.address() + filler->Size(), obj->address() + required);
  }
}

bool Heap::CreateImportantReadOnlyObjects() {
  // Allocate some objects early to get addresses to fit as arm64 immediates.
  Tagged<HeapObject> obj;
  ReadOnlyRoots roots(isolate());
  HandleScope initial_objects_handle_scope(isolate());

  // Hash seed for strings

  Factory* factory = isolate()->factory();
  set_hash_seed(*factory->NewByteArray(kInt64Size, AllocationType::kReadOnly));
  InitializeHashSeed();

  // Important strings and symbols
  for (const ConstantStringInit& entry : kImportantConstantStringTable) {
    if (entry.index == RootIndex::kempty_string) {
      // Special case the empty string, since it's allocated and initialised in
      // the initial section.
      isolate()->string_table()->InsertEmptyStringForBootstrapping(isolate());
    } else {
      DirectHandle<String> str = factory->InternalizeUtf8String(entry.contents);
      roots_table()[entry.index] = str->ptr();
    }
  }

  {
#define SYMBOL_INIT(_, name)                                                \
  {                                                                         \
    DirectHandle<Symbol> symbol(                                            \
        isolate()->factory()->NewPrivateSymbol(AllocationType::kReadOnly)); \
    roots_table()[RootIndex::k##name] = symbol->ptr();                      \
  }
      IMPORTANT_PRIVATE_SYMBOL_LIST_GENERATOR(SYMBOL_INIT, /* not used */)}
  // SYMBOL_INIT used again later.

  // Empty elements
  DirectHandle<NameDictionary>
      empty_property_dictionary = NameDictionary::New(
          isolate(), 1, AllocationType::kReadOnly, USE_CUSTOM_MINIMUM_CAPACITY);
  DCHECK(!empty_property_dictionary->HasSufficientCapacityToAdd(1));

  set_empty_property_dictionary(*empty_property_dictionary);

  // Allocate the empty OrderedNameDictionary
  DirectHandle<OrderedNameDictionary> empty_ordered_property_dictionary =
      OrderedNameDictionary::AllocateEmpty(isolate(), AllocationType::kReadOnly)
          .ToHandleChecked();
  set_empty_ordered_property_dictionary(*empty_ordered_property_dictionary);

  {
    if (!AllocateRaw(ByteArray::SizeFor(0), AllocationType::kReadOnly)
             .To(&obj)) {
      return false;
    }
    obj->set_map_after_allocation(roots.byte_array_map(), SKIP_WRITE_BARRIER);
    Cast<ByteArray>(obj)->set_length(0);
    set_empty_byte_array(Cast<ByteArray>(obj));
  }

  {
    AllocationResult alloc =
        AllocateRaw(ScopeInfo::SizeFor(ScopeInfo::kVariablePartIndex),
                    AllocationType::kReadOnly);
    if (!alloc.To(&obj)) return false;
    obj->set_map_after_allocation(roots.scope_info_map(), SKIP_WRITE_BARRIER);
    int flags = ScopeInfo::IsEmptyBit::encode(true);
    DCHECK_EQ(ScopeInfo::LanguageModeBit::decode(flags), LanguageMode::kSloppy);
    DCHECK_EQ(ScopeInfo::ReceiverVariableBits::decode(flags),
              VariableAllocationInfo::NONE);
    DCHECK_EQ(ScopeInfo::FunctionVariableBits::decode(flags),
              VariableAllocationInfo::NONE);
    Cast<ScopeInfo>(obj)->set_flags(flags);
    Cast<ScopeInfo>(obj)->set_context_local_count(0);
    Cast<ScopeInfo>(obj)->set_parameter_count(0);
    Cast<ScopeInfo>(obj)->set_position_info_start(0);
    Cast<ScopeInfo>(obj)->set_position_info_end(0);
  }
  set_empty_scope_info(Cast<ScopeInfo>(obj));

  {
    if (!AllocateRaw(FixedArray::SizeFor(0), AllocationType::kReadOnly)
             .To(&obj)) {
      return false;
    }
    obj->set_map_after_allocation(roots.property_array_map(),
                                  SKIP_WRITE_BARRIER);
    Cast<PropertyArray>(obj)->initialize_length(0);
    set_empty_property_array(Cast<PropertyArray>(obj));
  }

  // Heap Numbers
  // The -0 value must be set before NewNumber works.
  set_minus_zero_value(
      *factory->NewHeapNumber<AllocationType::kReadOnly>(-0.0));
  DCHECK(std::signbit(Object::NumberValue(roots.minus_zero_value())));

  set_nan_value(*factory->NewHeapNumber<AllocationType::kReadOnly>(
      std::numeric_limits<double>::quiet_NaN()));
  set_hole_nan_value(*factory->NewHeapNumberFromBits<AllocationType::kReadOnly>(
      kHoleNanInt64));
  set_infinity_value(
      *factory->NewHeapNumber<AllocationType::kReadOnly>(V8_INFINITY));
  set_minus_infinity_value(
      *factory->NewHeapNumber<AllocationType::kReadOnly>(-V8_INFINITY));
  set_max_safe_integer(
      *factory->NewHeapNumber<AllocationType::kReadOnly>(kMaxSafeInteger));
  set_max_uint_32(
      *factory->NewHeapNumber<AllocationType::kReadOnly>(kMaxUInt32));
  set_smi_min_value(
      *factory->NewHeapNumber<AllocationType::kReadOnly>(kSmiMinValue));
  set_smi_max_value_plus_one(
      *factory->NewHeapNumber<AllocationType::kReadOnly>(0.0 - kSmiMinValue));

  return true;
}

bool Heap::CreateReadOnlyObjects() {
  HandleScope initial_objects_handle_scope(isolate());
  Factory* factory = isolate()->factory();
  ReadOnlyRoots roots(this);
  Tagged<HeapObject> obj;

  {
    AllocationResult alloc =
        AllocateRaw(ArrayList::SizeFor(0), AllocationType::kReadOnly);
    if (!alloc.To(&obj)) return false;
    obj->set_map_after_allocation(roots.array_list_map(), SKIP_WRITE_BARRIER);
    // Unchecked to skip failing checks since required roots are uninitialized.
    UncheckedCast<ArrayList>(obj)->set_capacity(0);
    UncheckedCast<ArrayList>(obj)->set_length(0);
  }
  set_empty_array_list(UncheckedCast<ArrayList>(obj));

  {
    AllocationResult alloc = AllocateRaw(
        ObjectBoilerplateDescription::SizeFor(0), AllocationType::kReadOnly);
    if (!alloc.To(&obj)) return false;
    obj->set_map_after_allocation(roots.object_boilerplate_description_map(),
                                  SKIP_WRITE_BARRIER);

    Cast<ObjectBoilerplateDescription>(obj)->set_capacity(0);
    Cast<ObjectBoilerplateDescription>(obj)->set_backing_store_size(0);
    Cast<ObjectBoilerplateDescription>(obj)->set_flags(0);
  }
  set_empty_object_boilerplate_description(
      Cast<ObjectBoilerplateDescription>(obj));

  {
    // Empty array boilerplate description
    AllocationResult alloc =
        Allocate(roots.array_boilerplate_description_map_handle(),
                 AllocationType::kReadOnly);
    if (!alloc.To(&obj)) return false;

    Cast<ArrayBoilerplateDescription>(obj)->set_constant_elements(
        roots.empty_fixed_array());
    Cast<ArrayBoilerplateDescription>(obj)->set_elements_kind(
        ElementsKind::PACKED_SMI_ELEMENTS);
  }
  set_empty_array_boilerplate_description(
      Cast<ArrayBoilerplateDescription>(obj));

  // Empty arrays.
  {
    if (!AllocateRaw(ClosureFeedbackCellArray::SizeFor(0),
                     AllocationType::kReadOnly)
             .To(&obj)) {
      return false;
    }
    obj->set_map_after_allocation(roots.closure_feedback_cell_array_map(),
                                  SKIP_WRITE_BARRIER);
    Cast<ClosureFeedbackCellArray>(obj)->set_length(0);
    set_empty_closure_feedback_cell_array(Cast<ClosureFeedbackCellArray>(obj));
  }

  DCHECK(!InYoungGeneration(roots.empty_fixed_array()));

  // Allocate the empty SwissNameDictionary
  DirectHandle<SwissNameDictionary> empty_swiss_property_dictionary =
      factory->CreateCanonicalEmptySwissNameDictionary();
  set_empty_swiss_property_dictionary(*empty_swiss_property_dictionary);
  StaticRootsEnsureAllocatedSize(empty_swiss_property_dictionary,
                                 8 * kTaggedSize);

  roots.bigint_map()->SetConstructorFunctionIndex(
      Context::BIGINT_FUNCTION_INDEX);

  // Allocate and initialize table for single character one byte strings.
  int table_size = String::kMaxOneByteCharCode + 1;
  set_single_character_string_table(
      *factory->NewFixedArray(table_size, AllocationType::kReadOnly));
  for (int i = 0; i < table_size; ++i) {
    uint8_t code = static_cast<uint8_t>(i);
    DirectHandle<String> str =
        factory->InternalizeString(base::Vector<const uint8_t>(&code, 1));
    DCHECK(ReadOnlyHeap::Contains(*str));
    single_character_string_table()->set(i, *str);
  }

  for (const ConstantStringInit& entry : kNotImportantConstantStringTable) {
    DirectHandle<String> str = factory->InternalizeUtf8String(entry.contents);
    roots_table()[entry.index] = str->ptr();
  }

  // Finish initializing oddballs after creating the string table.
  Oddball::Initialize(isolate(), factory->undefined_value(), "undefined",
                      factory->nan_value(), "undefined", Oddball::kUndefined);

  // Initialize the null_value.
  Oddball::Initialize(isolate(), factory->null_value(), "null",
                      handle(Smi::zero(), isolate()), "object", Oddball::kNull);

  // Initialize the true_value.
  Oddball::Initialize(isolate(), factory->true_value(), "true",
                      handle(Smi::FromInt(1), isolate()), "boolean",
                      Oddball::kTrue);

  // Initialize the false_value.
  Oddball::Initialize(isolate(), factory->false_value(), "false",
                      handle(Smi::zero(), isolate()), "boolean",
                      Oddball::kFalse);

  // Initialize the_hole_value.
  Hole::Initialize(isolate(), factory->the_hole_value(),
                   factory->hole_nan_value());

  set_property_cell_hole_value(*factory->NewHole());
  set_hash_table_hole_value(*factory->NewHole());
  set_promise_hole_value(*factory->NewHole());
  set_uninitialized_value(*factory->NewHole());
  set_arguments_marker(*factory->NewHole());
  set_termination_exception(*factory->NewHole());
  set_exception(*factory->NewHole());
  set_optimized_out(*factory->NewHole());
  set_stale_register(*factory->NewHole());

  // Initialize marker objects used during compilation.
  set_self_reference_marker(*factory->NewHole());
  set_basic_block_counters_marker(*factory->NewHole());

  {
    HandleScope handle_scope(isolate());
    NOT_IMPORTANT_PRIVATE_SYMBOL_LIST_GENERATOR(SYMBOL_INIT, /* not used */)
#undef SYMBOL_INIT
  }

  {
    HandleScope handle_scope(isolate());
#define PUBLIC_SYMBOL_INIT(_, name, description)                               \
  DirectHandle<Symbol> name = factory->NewSymbol(AllocationType::kReadOnly);   \
  DirectHandle<String> name##d = factory->InternalizeUtf8String(#description); \
  name->set_description(*name##d);                                             \
  roots_table()[RootIndex::k##name] = name->ptr();

    PUBLIC_SYMBOL_LIST_GENERATOR(PUBLIC_SYMBOL_INIT, /* not used */)

#define WELL_KNOWN_SYMBOL_INIT(_, name, description)                           \
  DirectHandle<Symbol> name = factory->NewSymbol(AllocationType::kReadOnly);   \
  DirectHandle<String> name##d = factory->InternalizeUtf8String(#description); \
  name->set_is_well_known_symbol(true);                                        \
  name->set_description(*name##d);                                             \
  roots_table()[RootIndex::k##name] = name->ptr();

    WELL_KNOWN_SYMBOL_LIST_GENERATOR(WELL_KNOWN_SYMBOL_INIT, /* not used */)

    // Mark "Interesting Symbols" appropriately.
    to_string_tag_symbol->set_is_interesting_symbol(true);
  }

  {
    // All Names that can cause protector invalidation have to be allocated
    // consecutively to allow for fast checks

    // Allocate the symbols's internal strings first, so we don't get
    // interleaved string allocations for the symbols later.
#define ALLOCATE_SYMBOL_STRING(_, name, description) \
  Handle<String> name##symbol_string =               \
      factory->InternalizeUtf8String(#description);  \
  USE(name##symbol_string);

    SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(ALLOCATE_SYMBOL_STRING,
                                        /* not used */)
    PUBLIC_SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(ALLOCATE_SYMBOL_STRING,
                                               /* not used */)
    WELL_KNOWN_SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(ALLOCATE_SYMBOL_STRING,
                                                   /* not used */)
#undef ALLOCATE_SYMBOL_STRING

#define INTERNALIZED_STRING_INIT(_, name, description)                     \
  DirectHandle<String> name = factory->InternalizeUtf8String(description); \
  roots_table()[RootIndex::k##name] = name->ptr();

    INTERNALIZED_STRING_FOR_PROTECTOR_LIST_GENERATOR(INTERNALIZED_STRING_INIT,
                                                     /* not used */)
    SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(PUBLIC_SYMBOL_INIT,
                                        /* not used */)
    PUBLIC_SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(PUBLIC_SYMBOL_INIT,
                                               /* not used */)
    WELL_KNOWN_SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(WELL_KNOWN_SYMBOL_INIT,
                                                   /* not used */)

    // Mark "Interesting Symbols" appropriately.
    to_primitive_symbol->set_is_interesting_symbol(true);

#ifdef DEBUG
    roots.VerifyNameForProtectors();
#endif
    roots.VerifyNameForProtectorsPages();

#undef INTERNALIZED_STRING_INIT
#undef PUBLIC_SYMBOL_INIT
#undef WELL_KNOWN_SYMBOL_INIT
  }

  DirectHandle<NumberDictionary> slow_element_dictionary =
      NumberDictionary::New(isolate(), 1, AllocationType::kReadOnly,
                            USE_CUSTOM_MINIMUM_CAPACITY);
  DCHECK(!slow_element_dictionary->HasSufficientCapacityToAdd(1));
  set_empty_slow_element_dictionary(*slow_element_dictionary);

  DirectHandle<RegisteredSymbolTable> empty_symbol_table =
      RegisteredSymbolTable::New(isolate(), 1, AllocationType::kReadOnly,
                                 USE_CUSTOM_MINIMUM_CAPACITY);
  DCHECK(!empty_symbol_table->HasSufficientCapacityToAdd(1));
  set_empty_symbol_table(*empty_symbol_table);

  // Allocate the empty OrderedHashMap.
  DirectHandle<OrderedHashMap> empty_ordered_hash_map =
      OrderedHashMap::AllocateEmpty(isolate(), AllocationType::kReadOnly)
          .ToHandleChecked();
  set_empty_ordered_hash_map(*empty_ordered_hash_map);

  // Allocate the empty OrderedHashSet.
  DirectHandle<OrderedHashSet> empty_ordered_hash_set =
      OrderedHashSet::AllocateEmpty(isolate(), AllocationType::kReadOnly)
          .ToHandleChecked();
  set_empty_ordered_hash_set(*empty_ordered_hash_set);

  // Allocate the empty FeedbackMetadata.
  DirectHandle<FeedbackMetadata> empty_feedback_metadata =
      factory->NewFeedbackMetadata(0, 0, AllocationType::kReadOnly);
  set_empty_feedback_metadata(*empty_feedback_metadata);

  // Canonical scope arrays.
  DirectHandle<ScopeInfo> global_this_binding =
      ScopeInfo::CreateGlobalThisBinding(isolate());
  set_global_this_binding_scope_info(*global_this_binding);

  DirectHandle<ScopeInfo> empty_function =
      ScopeInfo::CreateForEmptyFunction(isolate());
  set_empty_function_scope_info(*empty_function);

  DirectHandle<ScopeInfo> native_scope_info =
      ScopeInfo::CreateForNativeContext(isolate());
  set_native_scope_info(*native_scope_info);

  DirectHandle<ScopeInfo> shadow_realm_scope_info =
      ScopeInfo::CreateForShadowRealmNativeContext(isolate());
  set_shadow_realm_scope_info(*shadow_realm_scope_info);

  // EmptyExternalPointerArray:
  {
    if (!AllocateRaw(ExternalPointerArray::SizeFor(0),
                     AllocationType::kReadOnly)
             .To(&obj))
      return false;
    obj->set_map_after_allocation(roots.external_pointer_array_map(),
                                  SKIP_WRITE_BARRIER);
    Cast<ExternalPointerArray>(obj)->set_length(0);
    set_empty_external_pointer_array(Cast<ExternalPointerArray>(obj));
  }

  // Initialize the wasm null_value.

#ifdef V8_ENABLE_WEBASSEMBLY
  // Allocate the wasm-null object. It is a regular V8 heap object contained in
  // a V8 page.
  // In static-roots builds, it is large enough so that its payload (other than
  // its map word) can be mprotected on OS page granularity. We adjust the
  // layout such that we have a filler object in the current OS page, and the
  // wasm-null map word at the end of the current OS page. The payload then is
  // contained on a separate OS page which can be protected.
  // In non-static-roots builds, it is a regular object of size {kTaggedSize}
  // and does not need padding.

  constexpr size_t kLargestPossibleOSPageSize = 64 * KB;
  static_assert(kLargestPossibleOSPageSize >= kMinimumOSPageSize);

  if (V8_STATIC_ROOTS_BOOL || V8_STATIC_ROOTS_GENERATION_BOOL) {
    // Ensure all of the following lands on the same V8 page.
    constexpr int kOffsetAfterMapWord = HeapObject::kMapOffset + kTaggedSize;
    static_assert(kOffsetAfterMapWord % kObjectAlignment == 0);
    read_only_space_->EnsureSpaceForAllocation(
        kLargestPossibleOSPageSize + WasmNull::kSize - kOffsetAfterMapWord);
    Address next_page = RoundUp(read_only_space_->top() + kOffsetAfterMapWord,
                                kLargestPossibleOSPageSize);

    // Add some filler to end up right before an OS page boundary.
    int filler_size = static_cast<int>(next_page - read_only_space_->top() -
                                       kOffsetAfterMapWord);
    // TODO(v8:7748) Depending on where we end up this might actually not hold,
    // in which case we would need to use a one or two-word filler.
    CHECK(filler_size > 2 * kTaggedSize);
    Tagged<HeapObject> filler =
        allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(
            filler_size, AllocationType::kReadOnly, AllocationOrigin::kRuntime,
            AllocationAlignment::kTaggedAligned);
    CreateFillerObjectAt(filler.address(), filler_size,
                         ClearFreedMemoryMode::kClearFreedMemory);
    set_wasm_null_padding(filler);
    CHECK_EQ(read_only_space_->top() + kOffsetAfterMapWord, next_page);
  } else {
    set_wasm_null_padding(roots.undefined_value());
  }

  // Finally, allocate the wasm-null object.
  {
    Tagged<HeapObject> obj;
    CHECK(AllocateRaw(WasmNull::kSize, AllocationType::kReadOnly).To(&obj));
    // No need to initialize the payload since it's either empty or unmapped.
    CHECK_IMPLIES(!(V8_STATIC_ROOTS_BOOL || V8_STATIC_ROOTS_GENERATION_BOOL),
                  WasmNull::kSize == sizeof(Tagged_t));
    obj->set_map_after_allocation(roots.wasm_null_map(), SKIP_WRITE_BARRIER);
    set_wasm_null(Cast<WasmNull>(obj));
    if (V8_STATIC_ROOTS_BOOL || V8_STATIC_ROOTS_GENERATION_BOOL) {
      CHECK_EQ(read_only_space_->top() % kLargestPossibleOSPageSize, 0);
    }
  }
#endif

  return true;
}

void Heap::CreateMutableApiObjects() {
  HandleScope scope(isolate());
  set_message_listeners(*ArrayList::New(isolate(), 2, AllocationType::kOld));
}

void Heap::CreateReadOnlyApiObjects() {
  HandleScope scope(isolate());
  auto info = Cast<InterceptorInfo>(isolate()->factory()->NewStruct(
      INTERCEPTOR_INFO_TYPE, AllocationType::kReadOnly));
  info->set_flags(0);
  set_noop_interceptor_info(*info);
}

void Heap::CreateInitialMutableObjects() {
  HandleScope initial_objects_handle_scope(isolate());
  Factory* factory = isolate()->factory();
  ReadOnlyRoots roots(this);

  // There's no "current microtask" in the beginning.
  set_current_microtask(roots.undefined_value());

  set_weak_refs_keep_during_job(roots.undefined_value());

  set_public_symbol_table(roots.empty_symbol_table());
  set_api_symbol_table(roots.empty_symbol_table());
  set_api_private_symbol_table(roots.empty_symbol_table());

  set_number_string_cache(*factory->NewFixedArray(
      kInitialNumberStringCacheSize * 2, AllocationType::kOld));

  // Unchecked to skip failing checks since required roots are uninitialized.
  set_basic_block_profiling_data(roots.unchecked_empty_array_list());

  // Allocate cache for string split and regexp-multiple.
  set_string_split_cache(*factory->NewFixedArray(
      RegExpResultsCache::kRegExpResultsCacheSize, AllocationType::kOld));
  set_regexp_multiple_cache(*factory->NewFixedArray(
      RegExpResultsCache::kRegExpResultsCacheSize, AllocationType::kOld));

  // Allocate FeedbackCell for builtins.
  DirectHandle<FeedbackCell> many_closures_cell =
      factory->NewManyClosuresCell();
  set_many_closures_cell(*many_closures_cell);

  set_detached_contexts(roots.empty_weak_array_list());

  set_feedback_vectors_for_profiling_tools(roots.undefined_value());
  set_functions_marked_for_manual_optimization(roots.undefined_value());
  set_shared_wasm_memories(roots.empty_weak_array_list());
  set_locals_block_list_cache(roots.undefined_value());
#ifdef V8_ENABLE_WEBASSEMBLY
  set_active_continuation(roots.undefined_value());
  set_active_suspender(roots.undefined_value());
  set_js_to_wasm_wrappers(roots.empty_weak_array_list());
  set_wasm_canonical_rtts(roots.empty_weak_array_list());
#endif  // V8_ENABLE_WEBASSEMBLY

  set_script_list(roots.empty_weak_array_list());

  set_materialized_objects(*factory->NewFixedArray(0, AllocationType::kOld));

  // Handling of script id generation is in Heap::NextScriptId().
  set_last_script_id(Smi::FromInt(v8::UnboundScript::kNoScriptId));
  set_last_debugging_id(Smi::FromInt(DebugInfo::kNoDebuggingId));
  set_next_template_serial_number(Smi::zero());

  // Allocate the empty script.
  DirectHandle<Script> script = factory->NewScript(factory->empty_string());
  script->set_type(Script::Type::kNative);
  // This is used for exceptions thrown with no stack frames. Such exceptions
  // can be shared everywhere.
  script->set_origin_options(ScriptOriginOptions(true, false));
  set_empty_script(*script);

  // Protectors
  set_array_buffer_detaching_protector(*factory->NewProtector());
  set_array_constructor_protector(*factory->NewProtector());
  set_array_iterator_protector(*factory->NewProtector());
  set_array_species_protector(*factory->NewProtector());
  set_is_concat_spreadable_protector(*factory->NewProtector());
  set_map_iterator_protector(*factory->NewProtector());
  set_no_elements_protector(*factory->NewProtector());
  set_mega_dom_protector(*factory->NewProtector());
  set_no_profiling_protector(*factory->NewProtector());
  set_no_undetectable_objects_protector(*factory->NewProtector());
  set_promise_hook_protector(*factory->NewProtector());
  set_promise_resolve_protector(*factory->NewProtector());
  set_promise_species_protector(*factory->NewProtector());
  set_promise_then_protector(*factory->NewProtector());
  set_regexp_species_protector(*factory->NewProtector());
  set_set_iterator_protector(*factory->NewProtector());
  set_string_iterator_protector(*factory->NewProtector());
  set_string_length_protector(*factory->NewProtector());
  set_string_wrapper_to_primitive_protector(*factory->NewProtector());
  set_number_string_not_regexp_like_protector(*factory->NewProtector());
  set_typed_array_species_protector(*factory->NewProtector());

  set_serialized_objects(roots.empty_fixed_array());
  set_serialized_global_proxy_sizes(roots.empty_fixed_array());

  // Evaluate the hash values which will then be cached in the strings.
  isolate()->factory()->zero_string()->EnsureHash();
  isolate()->factory()->one_string()->EnsureHash();

  // Initialize builtins constants table.
  set_builtins_constants_table(roots.empty_fixed_array());

  // Initialize descriptor cache.
  isolate_->descriptor_lookup_cache()->Clear();

  // Initialize compilation cache.
  isolate_->compilation_cache()->Clear();

  // Error.stack accessor callbacks:
  {
    DirectHandle<FunctionTemplateInfo> function_template;
    function_template = ApiNatives::CreateAccessorFunctionTemplateInfo(
        isolate_, Accessors::ErrorStackGetter, 0,
        SideEffectType::kHasSideEffect);
    set_error_stack_getter_fun_template(*function_template);

    function_template = ApiNatives::CreateAccessorFunctionTemplateInfo(
        isolate_, Accessors::ErrorStackSetter, 1,
        SideEffectType::kHasSideEffectToReceiver);
    set_error_stack_setter_fun_template(*function_template);
  }

  // Create internal SharedFunctionInfos.
  // Async functions:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate(), Builtin::kAsyncFunctionAwaitRejectClosure, 1);
    set_async_function_await_reject_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate(), Builtin::kAsyncFunctionAwaitResolveClosure, 1);
    set_async_function_await_resolve_shared_fun(*info);
  }

  // Async generators:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate(), Builtin::kAsyncGeneratorAwaitResolveClosure, 1);
    set_async_generator_await_resolve_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate(), Builtin::kAsyncGeneratorAwaitRejectClosure, 1);
    set_async_generator_await_reject_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate(), Builtin::kAsyncGeneratorYieldWithAwaitResolveClosure, 1);
    set_async_generator_yield_with_await_resolve_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate(), Builtin::kAsyncGeneratorReturnResolveClosure, 1);
    set_async_generator_return_resolve_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate(), Builtin::kAsyncGeneratorReturnClosedResolveClosure, 1);
    set_async_generator_return_closed_resolve_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate(), Builtin::kAsyncGeneratorReturnClosedRejectClosure, 1);
    set_async_generator_return_closed_reject_shared_fun(*info);
  }

  // AsyncIterator:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kAsyncIteratorValueUnwrap, 1);
    set_async_iterator_value_unwrap_shared_fun(*info);
  }

  // AsyncFromSyncIterator:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kAsyncFromSyncIteratorCloseSyncAndRethrow, 1);
    set_async_from_sync_iterator_close_sync_and_rethrow_shared_fun(*info);
  }

  // Promises:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kPromiseCapabilityDefaultResolve, 1,
        FunctionKind::kConciseMethod);
    info->set_native(true);
    info->set_function_map_index(
        Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
    set_promise_capability_default_resolve_shared_fun(*info);

    info = CreateSharedFunctionInfo(isolate_,
                                    Builtin::kPromiseCapabilityDefaultReject, 1,
                                    FunctionKind::kConciseMethod);
    info->set_native(true);
    info->set_function_map_index(
        Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
    set_promise_capability_default_reject_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate_, Builtin::kPromiseGetCapabilitiesExecutor, 2);
    set_promise_get_capabilities_executor_shared_fun(*info);
  }

  // Promises / finally:
  {
    DirectHandle<SharedFunctionInfo> info =
        CreateSharedFunctionInfo(isolate(), Builtin::kPromiseThenFinally, 1);
    info->set_native(true);
    set_promise_then_finally_shared_fun(*info);

    info =
        CreateSharedFunctionInfo(isolate(), Builtin::kPromiseCatchFinally, 1);
    info->set_native(true);
    set_promise_catch_finally_shared_fun(*info);

    info = CreateSharedFunctionInfo(isolate(),
                                    Builtin::kPromiseValueThunkFinally, 0);
    set_promise_value_thunk_finally_shared_fun(*info);

    info =
        CreateSharedFunctionInfo(isolate(), Builtin::kPromiseThrowerFinally, 0);
    set_promise_thrower_finally_shared_fun(*info);
  }

  // Promise combinators:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kPromiseAllResolveElementClosure, 1);
    set_promise_all_resolve_element_closure_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate_, Builtin::kPromiseAllSettledResolveElementClosure, 1);
    set_promise_all_settled_resolve_element_closure_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate_, Builtin::kPromiseAllSettledRejectElementClosure, 1);
    set_promise_all_settled_reject_element_closure_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate_, Builtin::kPromiseAnyRejectElementClosure, 1);
    set_promise_any_reject_element_closure_shared_fun(*info);
  }

  // ProxyRevoke:
  {
    DirectHandle<SharedFunctionInfo> info =
        CreateSharedFunctionInfo(isolate_, Builtin::kProxyRevoke, 0);
    set_proxy_revoke_shared_fun(*info);
  }

  // ShadowRealm:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kShadowRealmImportValueFulfilled, 1);
    set_shadow_realm_import_value_fulfilled_shared_fun(*info);
  }

  // SourceTextModule:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kCallAsyncModuleFulfilled, 0);
    set_source_text_module_execute_async_module_fulfilled_sfi(*info);

    info = CreateSharedFunctionInfo(isolate_, Builtin::kCallAsyncModuleRejected,
                                    0);
    set_source_text_module_execute_async_module_rejected_sfi(*info);
  }

  // Array.fromAsync:
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kArrayFromAsyncIterableOnFulfilled, 1);
    set_array_from_async_iterable_on_fulfilled_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate_, Builtin::kArrayFromAsyncIterableOnRejected, 1);
    set_array_from_async_iterable_on_rejected_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate_, Builtin::kArrayFromAsyncArrayLikeOnFulfilled, 1);
    set_array_from_async_array_like_on_fulfilled_shared_fun(*info);

    info = CreateSharedFunctionInfo(
        isolate_, Builtin::kArrayFromAsyncArrayLikeOnRejected, 1);
    set_array_from_async_array_like_on_rejected_shared_fun(*info);
  }

  // Atomics.Mutex
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kAtomicsMutexAsyncUnlockResolveHandler, 1);
    set_atomics_mutex_async_unlock_resolve_handler_sfi(*info);
    info = CreateSharedFunctionInfo(
        isolate_, Builtin::kAtomicsMutexAsyncUnlockRejectHandler, 1);
    set_atomics_mutex_async_unlock_reject_handler_sfi(*info);
  }

  // Atomics.Condition
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kAtomicsConditionAcquireLock, 0);
    set_atomics_condition_acquire_lock_sfi(*info);
  }

  // Async Disposable Stack
  {
    DirectHandle<SharedFunctionInfo> info = CreateSharedFunctionInfo(
        isolate_, Builtin::kAsyncDisposeFromSyncDispose, 0);
    set_async_dispose_from_sync_dispose_shared_fun(*info);
  }

  // Trusted roots:
  // TODO(saelo): these would ideally be read-only and shared, but we currently
  // don't have a trusted RO space.
  {
    set_empty_trusted_byte_array(*TrustedByteArray::New(isolate_, 0));
    set_empty_trusted_fixed_array(*TrustedFixedArray::New(isolate_, 0));
    set_empty_trusted_weak_fixed_array(
        *TrustedWeakFixedArray::New(isolate_, 0));
    set_empty_protected_fixed_array(*ProtectedFixedArray::New(isolate_, 0));
  }
}

void Heap::CreateInternalAccessorInfoObjects() {
  Isolate* isolate = this->isolate();
  HandleScope scope(isolate);
  DirectHandle<AccessorInfo> accessor_info;

#define INIT_ACCESSOR_INFO(_, accessor_name, AccessorName, ...) \
  accessor_info = Accessors::Make##AccessorName##Info(isolate); \
  roots_table()[RootIndex::k##AccessorName##Accessor] = accessor_info->ptr();
  ACCESSOR_INFO_LIST_GENERATOR(INIT_ACCESSOR_INFO, /* not used */)
#undef INIT_ACCESSOR_INFO

#define INIT_SIDE_EFFECT_FLAG(_, accessor_name, AccessorName, GetterType,  \
                              SetterType)                                  \
  Cast<AccessorInfo>(                                                      \
      Tagged<Object>(roots_table()[RootIndex::k##AccessorName##Accessor])) \
      ->set_getter_side_effect_type(SideEffectType::GetterType);           \
  Cast<AccessorInfo>(                                                      \
      Tagged<Object>(roots_table()[RootIndex::k##AccessorName##Accessor])) \
      ->set_setter_side_effect_type(SideEffectType::SetterType);
  ACCESSOR_INFO_LIST_GENERATOR(INIT_SIDE_EFFECT_FLAG, /* not used */)
#undef INIT_SIDE_EFFECT_FLAG
}

}  // namespace internal
}  // namespace v8
