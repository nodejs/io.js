// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-struct.h"

#include "src/objects/lookup-inl.h"
#include "src/objects/map-inl.h"
#include "src/objects/off-heap-hash-table-inl.h"
#include "src/objects/property-descriptor.h"

namespace v8 {
namespace internal {

namespace {

void PrepareMapCommon(Tagged<Map> map) {
  DCHECK(IsAlwaysSharedSpaceJSObjectMap(map));
  DisallowGarbageCollection no_gc;
  // Shared objects have fixed layout ahead of time, so there's no slack.
  map->SetInObjectUnusedPropertyFields(0);
  // Shared objects are not extensible and have a null prototype.
  map->set_is_extensible(false);
  // Shared space objects are not optimizable as prototypes because it is
  // not threadsafe.
  map->set_prototype_validity_cell(Smi::FromInt(Map::kPrototypeChainValid),
                                   kRelaxedStore, SKIP_WRITE_BARRIER);
}

}  // namespace

// static
void AlwaysSharedSpaceJSObject::PrepareMapNoEnumerableProperties(
    Tagged<Map> map) {
  PrepareMapCommon(map);
  map->SetEnumLength(0);
}

// static
void AlwaysSharedSpaceJSObject::PrepareMapNoEnumerableProperties(
    Isolate* isolate, Tagged<Map> map, Tagged<DescriptorArray> descriptors) {
  PrepareMapCommon(map);
  map->InitializeDescriptors(isolate, *descriptors);
  DCHECK_EQ(0, map->NumberOfEnumerableProperties());
  map->SetEnumLength(0);
}

// static
void AlwaysSharedSpaceJSObject::PrepareMapWithEnumerableProperties(
    Isolate* isolate, Handle<Map> map, Handle<DescriptorArray> descriptors,
    int enum_length) {
  PrepareMapCommon(*map);
  // Shared objects with enumerable own properties need to pre-create the enum
  // cache, as creating it lazily is racy.
  map->InitializeDescriptors(isolate, *descriptors);
  FastKeyAccumulator::InitializeFastPropertyEnumCache(
      isolate, map, enum_length, AllocationType::kSharedOld);
  DCHECK_EQ(enum_length, map->EnumLength());
}

// static
Maybe<bool> AlwaysSharedSpaceJSObject::DefineOwnProperty(
    Isolate* isolate, Handle<AlwaysSharedSpaceJSObject> shared_obj,
    Handle<Object> key, PropertyDescriptor* desc,
    Maybe<ShouldThrow> should_throw) {
  // Shared objects are designed to have fixed layout, i.e. their maps are
  // effectively immutable. They are constructed seal, but the semantics of
  // ordinary ECMAScript objects allow writable properties to be upgraded to
  // non-writable properties. This upgrade violates the fixed layout invariant
  // and is disallowed.

  DCHECK(IsName(*key) || IsNumber(*key));  // |key| is a PropertyKey.
  PropertyKey lookup_key(isolate, key);
  LookupIterator it(isolate, shared_obj, lookup_key, LookupIterator::OWN);
  PropertyDescriptor current;
  MAYBE_RETURN(GetOwnPropertyDescriptor(&it, &current), Nothing<bool>());

  // The only redefinition allowed is to set the value if all attributes match.
  if (!it.IsFound() ||
      PropertyDescriptor::IsDataDescriptor(desc) !=
          PropertyDescriptor::IsDataDescriptor(&current) ||
      desc->ToAttributes() != current.ToAttributes()) {
    DCHECK(!shared_obj->map()->is_extensible());
    RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                   NewTypeError(MessageTemplate::kDefineDisallowedFixedLayout,
                                it.GetName()));
  }
  DCHECK(it.property_attributes() == desc->ToAttributes());
  if (desc->has_value()) {
    return Object::SetDataProperty(&it, desc->value());
  }
  return Just(true);
}

Maybe<bool> AlwaysSharedSpaceJSObject::HasInstance(
    Isolate* isolate, Handle<JSFunction> constructor, Handle<Object> object) {
  if (!constructor->has_prototype_slot() || !constructor->has_initial_map() ||
      !IsJSReceiver(*object)) {
    return Just(false);
  }
  Handle<Map> constructor_map = handle(constructor->initial_map(), isolate);
  PrototypeIterator iter(isolate, Handle<JSReceiver>::cast(object),
                         kStartAtReceiver);
  Handle<Map> current_map;
  while (true) {
    current_map = handle(PrototypeIterator::GetCurrent(iter)->map(), isolate);
    if (current_map.is_identical_to(constructor_map)) {
      return Just(true);
    }
    if (!iter.AdvanceFollowingProxies()) return Nothing<bool>();
    if (iter.IsAtEnd()) return Just(false);
  }
}

namespace {

// Currently there are 2, both optionally present:
//  - Registry key
//  - Elements template
constexpr int kSpecialSlots = 2;

InternalIndex GetSpecialSlotIndex(Tagged<Map> instance_map,
                                  Tagged<Symbol> special_slot_name) {
  DCHECK(IsJSSharedStructMap(instance_map));
  DCHECK(IsPrivateSymbol(special_slot_name));
  Tagged<DescriptorArray> descriptors = instance_map->instance_descriptors();
  // Special slots are optional and start at descriptor number 0.
  int end = std::min(static_cast<int>(descriptors->number_of_all_descriptors()),
                     kSpecialSlots);
  for (int i = 0; i < end; ++i) {
    InternalIndex idx(i);
    if (descriptors->GetKey(idx) == special_slot_name) {
      DCHECK_EQ(PropertyLocation::kDescriptor,
                descriptors->GetDetails(idx).location());
      return idx;
    }
  }
  return InternalIndex::NotFound();
}

template <typename T>
MaybeHandle<T> GetSpecialSlotValue(Isolate* isolate, Tagged<Map> instance_map,
                                   Tagged<Symbol> special_slot_name) {
  DisallowGarbageCollection no_gc;
  MaybeHandle<T> result;
  InternalIndex entry = GetSpecialSlotIndex(instance_map, special_slot_name);
  if (entry.is_found()) {
    DCHECK_IMPLIES(
        special_slot_name ==
            ReadOnlyRoots(isolate).shared_struct_map_registry_key_symbol(),
        entry.as_int() == 0);
    result =
        handle(T::cast(instance_map->instance_descriptors()->GetStrongValue(
                   isolate, entry)),
               isolate);
  }
  return result;
}

}  // namespace

// static
Handle<Map> JSSharedStruct::CreateInstanceMap(
    Isolate* isolate, const std::vector<Handle<Name>>& field_names,
    const std::set<uint32_t>& element_names,
    MaybeHandle<String> maybe_registry_key) {
  auto* factory = isolate->factory();

  int num_fields = 0;
  int num_elements = 0;

  int num_descriptors = static_cast<int>(field_names.size());
  // If there are elements, an template NumberDictionary is created and stored
  // as a data constant on a descriptor.
  if (!element_names.empty()) num_descriptors++;
  // If this is a registered map, the key is stored as a data constant on a
  // descriptor because the registry stores the maps weakly. Storing the key in
  // the map simplifies the weakness handling in the GC.
  if (!maybe_registry_key.is_null()) num_descriptors++;

  // Create the DescriptorArray if there are fields or elements.
  Handle<DescriptorArray> descriptors;
  if (num_descriptors != 0) {
    descriptors = factory->NewDescriptorArray(num_descriptors, 0,
                                              AllocationType::kSharedOld);

    int special_slots = 0;

    // Store the registry key if the map is registered. This must be the first
    // slot if present. The registry depends on this for rehashing.
    Handle<String> registry_key;
    if (maybe_registry_key.ToHandle(&registry_key)) {
      Handle<String> registry_key = maybe_registry_key.ToHandleChecked();
      Descriptor d = Descriptor::DataConstant(
          factory->shared_struct_map_registry_key_symbol(), registry_key,
          ALL_ATTRIBUTES_MASK);
      DCHECK_EQ(0, special_slots);
      descriptors->Set(InternalIndex(special_slots++), &d);
    }

    // Elements in shared structs are only supported as a dictionary. Create the
    // template NumberDictionary if needed.
    if (!element_names.empty()) {
      Handle<NumberDictionary> elements_template;
      num_elements = static_cast<int>(element_names.size());
      elements_template = NumberDictionary::New(isolate, num_elements,
                                                AllocationType::kSharedOld);
      for (uint32_t index : element_names) {
        PropertyDetails details(PropertyKind::kData, SEALED,
                                PropertyConstness::kMutable, 0);
        NumberDictionary::UncheckedAdd<Isolate, AllocationType::kSharedOld>(
            isolate, elements_template, index,
            ReadOnlyRoots(isolate).undefined_value_handle(), details);
      }
      elements_template->SetInitialNumberOfElements(num_elements);
      DCHECK(InAnySharedSpace(*elements_template));

      Descriptor d = Descriptor::DataConstant(
          factory->shared_struct_map_elements_template_symbol(),
          elements_template, ALL_ATTRIBUTES_MASK);
      descriptors->Set(InternalIndex(special_slots++), &d);
    }

    DCHECK_LE(special_slots, kSpecialSlots);

    for (const Handle<Name>& field_name : field_names) {
      // Shared structs' fields need to be aligned, so make it all tagged.
      PropertyDetails details(
          PropertyKind::kData, SEALED, PropertyLocation::kField,
          PropertyConstness::kMutable, Representation::Tagged(), num_fields);
      descriptors->Set(InternalIndex(special_slots + num_fields), *field_name,
                       FieldType::Any(), details);
      num_fields++;
    }

    descriptors->Sort();
  }

  // Calculate the size for instances and create the map.
  int instance_size;
  int in_object_properties;
  JSFunction::CalculateInstanceSizeHelper(JS_SHARED_STRUCT_TYPE, false, 0,
                                          num_fields, &instance_size,
                                          &in_object_properties);
  Handle<Map> instance_map = factory->NewContextlessMap(
      JS_SHARED_STRUCT_TYPE, instance_size, DICTIONARY_ELEMENTS,
      in_object_properties, AllocationType::kSharedMap);

  // Prepare the enum cache if necessary.
  if (num_descriptors == 0) {
    DCHECK_EQ(0, num_fields);
    // No properties at all.
    AlwaysSharedSpaceJSObject::PrepareMapNoEnumerableProperties(*instance_map);
  } else if (num_fields == 0) {
    // Have descriptors, but no enumerable fields.
    AlwaysSharedSpaceJSObject::PrepareMapNoEnumerableProperties(
        isolate, *instance_map, *descriptors);
  } else {
    // Have enumerable fields.
    AlwaysSharedSpaceJSObject::PrepareMapWithEnumerableProperties(
        isolate, instance_map, descriptors, num_fields);
  }

  // Structs have fixed layout ahead of time, so there's no slack.
  int out_of_object_properties = num_fields - in_object_properties;
  if (out_of_object_properties != 0) {
    instance_map->SetOutOfObjectUnusedPropertyFields(0);
  }

  return instance_map;
}

// static
MaybeHandle<String> JSSharedStruct::GetRegistryKey(Isolate* isolate,
                                                   Tagged<Map> instance_map) {
  return GetSpecialSlotValue<String>(
      isolate, *instance_map,
      ReadOnlyRoots(isolate).shared_struct_map_registry_key_symbol());
}

// static
bool JSSharedStruct::IsRegistryKeyDescriptor(Isolate* isolate,
                                             Tagged<Map> instance_map,
                                             InternalIndex i) {
  DCHECK(IsJSSharedStructMap(instance_map));
  return instance_map->instance_descriptors(isolate)->GetKey(i) ==
         ReadOnlyRoots(isolate).shared_struct_map_registry_key_symbol();
}

// static
MaybeHandle<NumberDictionary> JSSharedStruct::GetElementsTemplate(
    Isolate* isolate, Tagged<Map> instance_map) {
  return GetSpecialSlotValue<NumberDictionary>(
      isolate, instance_map,
      ReadOnlyRoots(isolate).shared_struct_map_elements_template_symbol());
}

// static
bool JSSharedStruct::IsElementsTemplateDescriptor(Isolate* isolate,
                                                  Tagged<Map> instance_map,
                                                  InternalIndex i) {
  DCHECK(IsJSSharedStructMap(instance_map));
  return instance_map->instance_descriptors(isolate)->GetKey(i) ==
         ReadOnlyRoots(isolate).shared_struct_map_elements_template_symbol();
}

// Hash table mapping string keys to shared struct maps.
class SharedStructTypeRegistry::Data : public OffHeapHashTableBase<Data> {
 public:
  static constexpr int kEntrySize = 1;
  static constexpr int kMaxEmptyFactor = 4;
  static constexpr int kMinCapacity = 4;

  explicit Data(int capacity) : OffHeapHashTableBase<Data>(capacity) {}

  static uint32_t Hash(PtrComprCageBase cage_base, Tagged<Object> key) {
    // Registry keys, if present, store them at the first descriptor. All maps
    // in the registry have registry keys.
    return String::cast(
               Map::cast(key)->instance_descriptors(cage_base)->GetStrongValue(
                   InternalIndex(0)))
        ->hash();
  }

  template <typename IsolateT>
  static bool KeyIsMatch(IsolateT* isolate, Handle<String> key,
                         Tagged<Object> obj) {
    Handle<String> existing =
        JSSharedStruct::GetRegistryKey(isolate, Tagged<Map>::cast(obj))
            .ToHandleChecked();
    DCHECK(IsInternalizedString(*key));
    DCHECK(IsInternalizedString(*existing));
    return *key == *existing;
  }

  Tagged<Object> GetKey(PtrComprCageBase cage_base, InternalIndex index) const {
    return slot(index).load(cage_base);
  }

  void SetKey(InternalIndex index, Tagged<Object> key) {
    DCHECK(IsMap(key));
    slot(index).store(key);
  }
  void Set(InternalIndex index, Tagged<Map> map) { SetKey(index, map); }

  void CopyEntryExcludingKeyInto(PtrComprCageBase cage_base,
                                 InternalIndex from_index, Data* to,
                                 InternalIndex to_index) {
    // Do nothing, since kEntrySize is 1.
  }

  static std::unique_ptr<Data> New(int capacity) {
    return std::unique_ptr<Data>(new (capacity) Data(capacity));
  }

  void* operator new(size_t size, int capacity) {
    DCHECK_GE(capacity, kMinCapacity);
    DCHECK_EQ(size, sizeof(Data));
    return OffHeapHashTableBase<Data>::Allocate<Data,
                                                offsetof(Data, elements_)>(
        capacity);
  }
  void* operator new(size_t size) = delete;

  void operator delete(void* table) { OffHeapHashTableBase<Data>::Free(table); }
};

SharedStructTypeRegistry::SharedStructTypeRegistry()
    : data_(Data::New(Data::kMinCapacity)) {
  DCHECK_EQ(deleted_element(), Data::deleted_element());
}

SharedStructTypeRegistry::~SharedStructTypeRegistry() = default;

MaybeHandle<Map> SharedStructTypeRegistry::CheckIfEntryMatches(
    Isolate* isolate, InternalIndex entry, Handle<String> key,
    const std::vector<Handle<Name>>& field_names,
    const std::set<uint32_t>& element_names) {
  Tagged<Map> existing_map = Tagged<Map>::cast(data_->GetKey(isolate, entry));

  // A map is considered a match iff all of the following hold:
  // - field names are the same element-wise (in order)
  // - element indices are the same

  // Registered types always have the key as the first descriptor.
  DCHECK_EQ(
      *JSSharedStruct::GetRegistryKey(isolate, existing_map).ToHandleChecked(),
      *key);

  int num_descriptors = static_cast<int>(field_names.size()) + 1;
  if (!element_names.empty()) {
    if (JSSharedStruct::GetElementsTemplate(isolate, existing_map).is_null()) {
      return MaybeHandle<Map>();
    }
    num_descriptors++;
  }

  if (num_descriptors != existing_map->NumberOfOwnDescriptors()) {
    return MaybeHandle<Map>();
  }

  Tagged<DescriptorArray> existing_descriptors =
      existing_map->instance_descriptors(isolate);
  auto field_names_iter = field_names.begin();
  for (InternalIndex i : existing_map->IterateOwnDescriptors()) {
    if (JSSharedStruct::IsElementsTemplateDescriptor(isolate, existing_map,
                                                     i)) {
      Handle<NumberDictionary> elements_template(
          NumberDictionary::cast(
              existing_map->instance_descriptors()->GetStrongValue(isolate, i)),
          isolate);
      if (static_cast<int>(element_names.size()) !=
          elements_template->NumberOfElements()) {
        return MaybeHandle<Map>();
      }
      for (int element : element_names) {
        if (elements_template->FindEntry(isolate, element).is_not_found()) {
          return MaybeHandle<Map>();
        }
      }

      continue;
    }

    if (JSSharedStruct::IsRegistryKeyDescriptor(isolate, existing_map, i)) {
      continue;
    }

    Tagged<Name> existing_name = existing_descriptors->GetKey(i);
    DCHECK(IsUniqueName(existing_name));
    Tagged<Name> name = **field_names_iter;
    DCHECK(IsUniqueName(name));
    if (name != existing_name) return MaybeHandle<Map>();
    ++field_names_iter;
  }

  return handle(existing_map, isolate);
}

MaybeHandle<Map> SharedStructTypeRegistry::RegisterNoThrow(
    Isolate* isolate, Handle<String> key,
    const std::vector<Handle<Name>>& field_names,
    const std::set<uint32_t>& element_names) {
  key = isolate->factory()->InternalizeString(key);

  // To avoid deadlock with iteration during GC and modifying the table, no GC
  // must occur under lock.

  {
    NoGarbageCollectionMutexGuard data_guard(&data_mutex_);
    InternalIndex entry = data_->FindEntry(isolate, key, key->hash());
    if (entry.is_found()) {
      return CheckIfEntryMatches(isolate, entry, key, field_names,
                                 element_names);
    }
  }

  // We have a likely miss. Create a new instance map outside of the lock.
  Handle<Map> map = JSSharedStruct::CreateInstanceMap(isolate, field_names,
                                                      element_names, key);

  // Relookup to see if it's in fact a miss.
  NoGarbageCollectionMutexGuard data_guard(&data_mutex_);

  EnsureCapacity(isolate, 1);
  InternalIndex entry =
      data_->FindEntryOrInsertionEntry(isolate, key, key->hash());
  Tagged<Object> existing_key = data_->GetKey(isolate, entry);
  if (existing_key == Data::empty_element()) {
    data_->AddAt(isolate, entry, *map);
    return map;
  } else if (existing_key == Data::deleted_element()) {
    data_->OverwriteDeletedAt(isolate, entry, *map);
    return map;
  } else {
    // An entry with the same key was inserted between the two locks.
    return CheckIfEntryMatches(isolate, entry, key, field_names, element_names);
  }
}

MaybeHandle<Map> SharedStructTypeRegistry::Register(
    Isolate* isolate, Handle<String> key,
    const std::vector<Handle<Name>>& field_names,
    const std::set<uint32_t>& element_names) {
  MaybeHandle<Map> canonical_map =
      RegisterNoThrow(isolate, key, field_names, element_names);
  if (canonical_map.is_null()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kSharedStructTypeRegistryMismatch, key),
        Map);
  }
  return canonical_map;
}

void SharedStructTypeRegistry::IterateElements(Isolate* isolate,
                                               RootVisitor* visitor) {
  // Ideally this should only happen during a global safepoint, when all
  // workers and background threads are paused, so there would be no need to
  // take the data mutex. However, the array left trimming has a verifier
  // visitor that visits all roots (including weak ones), thus we take the
  // mutex.
  //
  // TODO(v8:12547): Figure out how to do
  // isolate->global_safepoint()->AssertActive() instead.
  base::MutexGuard data_guard(&data_mutex_);
  data_->IterateElements(Root::kSharedStructTypeRegistry, visitor);
}

void SharedStructTypeRegistry::NotifyElementsRemoved(int count) {
  data_->ElementsRemoved(count);
}

void SharedStructTypeRegistry::EnsureCapacity(PtrComprCageBase cage_base,
                                              int additional_elements) {
  data_mutex_.AssertHeld();

  int new_capacity;
  if (data_->ShouldResizeToAdd(additional_elements, &new_capacity)) {
    std::unique_ptr<Data> new_data(Data::New(new_capacity));
    data_->RehashInto(cage_base, new_data.get());
    data_ = std::move(new_data);
  }
}

}  // namespace internal
}  // namespace v8
