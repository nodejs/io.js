// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/ast.h"
#include "src/common/globals.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/objects/allocation-site-scopes-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/literal-objects-inl.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

namespace {

bool IsUninitializedLiteralSite(Tagged<Object> literal_site) {
  return literal_site == Smi::zero();
}

bool HasBoilerplate(Handle<Object> literal_site) {
  return !IsSmi(*literal_site);
}

void PreInitializeLiteralSite(Handle<FeedbackVector> vector,
                              FeedbackSlot slot) {
  vector->SynchronizedSet(slot, Smi::FromInt(1));
}

template <class ContextObject>
class JSObjectWalkVisitor {
 public:
  explicit JSObjectWalkVisitor(ContextObject* site_context)
      : site_context_(site_context) {}

  V8_WARN_UNUSED_RESULT MaybeHandle<JSObject> StructureWalk(
      Handle<JSObject> object);

 protected:
  V8_WARN_UNUSED_RESULT inline MaybeHandle<JSObject> VisitElementOrProperty(
      Handle<JSObject> object, Handle<JSObject> value) {
    // Dont create allocation sites for nested object literals
    if (!IsJSArray(*value)) {
      return StructureWalk(value);
    }

    Handle<AllocationSite> current_site = site_context()->EnterNewScope();
    MaybeHandle<JSObject> copy_of_value = StructureWalk(value);
    site_context()->ExitScope(current_site, value);
    return copy_of_value;
  }

  inline ContextObject* site_context() { return site_context_; }
  inline Isolate* isolate() { return site_context()->isolate(); }

 private:
  ContextObject* site_context_;
};

template <class ContextObject>
MaybeHandle<JSObject> JSObjectWalkVisitor<ContextObject>::StructureWalk(
    Handle<JSObject> object) {
  Isolate* isolate = this->isolate();
  bool copying = ContextObject::kCopying;

  {
    StackLimitCheck check(isolate);

    if (check.HasOverflowed()) {
      isolate->StackOverflow();
      return MaybeHandle<JSObject>();
    }
  }

  if (object->map(isolate)->is_deprecated()) {
    base::SharedMutexGuard<base::kExclusive> mutex_guard(
        isolate->boilerplate_migration_access());
    JSObject::MigrateInstance(isolate, object);
  }

  Handle<JSObject> copy;
  if (copying) {
    // JSFunction objects are not allowed to be in normal boilerplates at all.
    DCHECK(!IsJSFunction(*object, isolate));
    Handle<AllocationSite> site_to_pass;
    if (site_context()->ShouldCreateMemento(object)) {
      site_to_pass = site_context()->current();
    }
    copy = isolate->factory()->CopyJSObjectWithAllocationSite(object,
                                                              site_to_pass);
  } else {
    copy = object;
  }

  DCHECK(copying || copy.is_identical_to(object));

  HandleScope scope(isolate);

  // Deep copy own properties. Arrays only have 1 property "length".
  if (!IsJSArray(*copy, isolate)) {
    if (copy->HasFastProperties(isolate)) {
      Handle<DescriptorArray> descriptors(
          copy->map(isolate)->instance_descriptors(isolate), isolate);
      for (InternalIndex i : copy->map(isolate)->IterateOwnDescriptors()) {
        PropertyDetails details = descriptors->GetDetails(i);
        DCHECK_EQ(PropertyLocation::kField, details.location());
        DCHECK_EQ(PropertyKind::kData, details.kind());
        FieldIndex index = FieldIndex::ForPropertyIndex(
            copy->map(isolate), details.field_index(),
            details.representation());
        Tagged<Object> raw = copy->RawFastPropertyAt(isolate, index);
        if (IsJSObject(raw, isolate)) {
          Handle<JSObject> value(JSObject::cast(raw), isolate);
          ASSIGN_RETURN_ON_EXCEPTION(
              isolate, value, VisitElementOrProperty(copy, value), JSObject);
          if (copying) copy->FastPropertyAtPut(index, *value);
        } else if (copying && details.representation().IsDouble()) {
          uint64_t double_value = HeapNumber::cast(raw)->value_as_bits();
          auto value = isolate->factory()->NewHeapNumberFromBits(double_value);
          copy->FastPropertyAtPut(index, *value);
        }
      }
    } else {
      if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
        Handle<SwissNameDictionary> dict(
            copy->property_dictionary_swiss(isolate), isolate);
        for (InternalIndex i : dict->IterateEntries()) {
          Tagged<Object> raw = dict->ValueAt(i);
          if (!IsJSObject(raw, isolate)) continue;
          DCHECK(IsName(dict->KeyAt(i)));
          Handle<JSObject> value(JSObject::cast(raw), isolate);
          ASSIGN_RETURN_ON_EXCEPTION(
              isolate, value, VisitElementOrProperty(copy, value), JSObject);
          if (copying) dict->ValueAtPut(i, *value);
        }
      } else {
        Handle<NameDictionary> dict(copy->property_dictionary(isolate),
                                    isolate);
        for (InternalIndex i : dict->IterateEntries()) {
          Tagged<Object> raw = dict->ValueAt(isolate, i);
          if (!IsJSObject(raw, isolate)) continue;
          DCHECK(IsName(dict->KeyAt(isolate, i)));
          Handle<JSObject> value(JSObject::cast(raw), isolate);
          ASSIGN_RETURN_ON_EXCEPTION(
              isolate, value, VisitElementOrProperty(copy, value), JSObject);
          if (copying) dict->ValueAtPut(i, *value);
        }
      }
    }

    // Assume non-arrays don't end up having elements.
    if (copy->elements(isolate)->length() == 0) return copy;
  }

  // Deep copy own elements.
  switch (copy->GetElementsKind(isolate)) {
    case PACKED_ELEMENTS:
    case PACKED_FROZEN_ELEMENTS:
    case PACKED_SEALED_ELEMENTS:
    case PACKED_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_FROZEN_ELEMENTS:
    case HOLEY_SEALED_ELEMENTS:
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_ELEMENTS:
    case SHARED_ARRAY_ELEMENTS: {
      Handle<FixedArray> elements(FixedArray::cast(copy->elements(isolate)),
                                  isolate);
      if (elements->map(isolate) ==
          ReadOnlyRoots(isolate).fixed_cow_array_map()) {
#ifdef DEBUG
        for (int i = 0; i < elements->length(); i++) {
          DCHECK(!IsJSObject(elements->get(i)));
        }
#endif
      } else {
        for (int i = 0; i < elements->length(); i++) {
          Tagged<Object> raw = elements->get(i);
          if (!IsJSObject(raw, isolate)) continue;
          Handle<JSObject> value(JSObject::cast(raw), isolate);
          ASSIGN_RETURN_ON_EXCEPTION(
              isolate, value, VisitElementOrProperty(copy, value), JSObject);
          if (copying) elements->set(i, *value);
        }
      }
      break;
    }
    case DICTIONARY_ELEMENTS: {
      Handle<NumberDictionary> element_dictionary(
          copy->element_dictionary(isolate), isolate);
      for (InternalIndex i : element_dictionary->IterateEntries()) {
        Tagged<Object> raw = element_dictionary->ValueAt(isolate, i);
        if (!IsJSObject(raw, isolate)) continue;
        Handle<JSObject> value(JSObject::cast(raw), isolate);
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, value, VisitElementOrProperty(copy, value), JSObject);
        if (copying) element_dictionary->ValueAtPut(i, *value);
      }
      break;
    }
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
      UNIMPLEMENTED();
      break;
    case FAST_STRING_WRAPPER_ELEMENTS:
    case SLOW_STRING_WRAPPER_ELEMENTS:
    case WASM_ARRAY_ELEMENTS:
      UNREACHABLE();

#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) case TYPE##_ELEMENTS:

      TYPED_ARRAYS(TYPED_ARRAY_CASE)
      RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
      // Typed elements cannot be created using an object literal.
      UNREACHABLE();

    case PACKED_SMI_ELEMENTS:
    case HOLEY_SMI_ELEMENTS:
    case PACKED_DOUBLE_ELEMENTS:
    case HOLEY_DOUBLE_ELEMENTS:
    case NO_ELEMENTS:
      // No contained objects, nothing to do.
      break;
  }

  return copy;
}

class DeprecationUpdateContext {
 public:
  explicit DeprecationUpdateContext(Isolate* isolate) { isolate_ = isolate; }
  Isolate* isolate() { return isolate_; }
  bool ShouldCreateMemento(Handle<JSObject> object) { return false; }
  inline void ExitScope(Handle<AllocationSite> scope_site,
                        Handle<JSObject> object) {}
  Handle<AllocationSite> EnterNewScope() { return Handle<AllocationSite>(); }
  Handle<AllocationSite> current() {
    UNREACHABLE();
  }

  static const bool kCopying = false;

 private:
  Isolate* isolate_;
};

// AllocationSiteCreationContext aids in the creation of AllocationSites to
// accompany object literals.
class AllocationSiteCreationContext : public AllocationSiteContext {
 public:
  explicit AllocationSiteCreationContext(Isolate* isolate)
      : AllocationSiteContext(isolate) {}

  Handle<AllocationSite> EnterNewScope() {
    Handle<AllocationSite> scope_site;
    if (top().is_null()) {
      // We are creating the top level AllocationSite as opposed to a nested
      // AllocationSite.
      InitializeTraversal(isolate()->factory()->NewAllocationSite(true));
      scope_site = Handle<AllocationSite>(*top(), isolate());
      if (v8_flags.trace_creation_allocation_sites) {
        PrintF("*** Creating top level %s AllocationSite %p\n", "Fat",
               reinterpret_cast<void*>(scope_site->ptr()));
      }
    } else {
      DCHECK(!current().is_null());
      scope_site = isolate()->factory()->NewAllocationSite(false);
      if (v8_flags.trace_creation_allocation_sites) {
        PrintF(
            "*** Creating nested %s AllocationSite (top, current, new) (%p, "
            "%p, "
            "%p)\n",
            "Slim", reinterpret_cast<void*>(top()->ptr()),
            reinterpret_cast<void*>(current()->ptr()),
            reinterpret_cast<void*>(scope_site->ptr()));
      }
      current()->set_nested_site(*scope_site);
      update_current_site(*scope_site);
    }
    DCHECK(!scope_site.is_null());
    return scope_site;
  }
  void ExitScope(Handle<AllocationSite> scope_site, Handle<JSObject> object) {
    if (object.is_null()) return;
    scope_site->set_boilerplate(*object, kReleaseStore);
    if (v8_flags.trace_creation_allocation_sites) {
      bool top_level =
          !scope_site.is_null() && top().is_identical_to(scope_site);
      if (top_level) {
        PrintF("*** Setting AllocationSite %p transition_info %p\n",
               reinterpret_cast<void*>(scope_site->ptr()),
               reinterpret_cast<void*>(object->ptr()));
      } else {
        PrintF("*** Setting AllocationSite (%p, %p) transition_info %p\n",
               reinterpret_cast<void*>(top()->ptr()),
               reinterpret_cast<void*>(scope_site->ptr()),
               reinterpret_cast<void*>(object->ptr()));
      }
    }
  }
  static const bool kCopying = false;
};

MaybeHandle<JSObject> DeepWalk(Handle<JSObject> object,
                               DeprecationUpdateContext* site_context) {
  JSObjectWalkVisitor<DeprecationUpdateContext> v(site_context);
  MaybeHandle<JSObject> result = v.StructureWalk(object);
  Handle<JSObject> for_assert;
  DCHECK(!result.ToHandle(&for_assert) || for_assert.is_identical_to(object));
  return result;
}

MaybeHandle<JSObject> DeepWalk(Handle<JSObject> object,
                               AllocationSiteCreationContext* site_context) {
  JSObjectWalkVisitor<AllocationSiteCreationContext> v(site_context);
  MaybeHandle<JSObject> result = v.StructureWalk(object);
  Handle<JSObject> for_assert;
  DCHECK(!result.ToHandle(&for_assert) || for_assert.is_identical_to(object));
  return result;
}

MaybeHandle<JSObject> DeepCopy(Handle<JSObject> object,
                               AllocationSiteUsageContext* site_context) {
  JSObjectWalkVisitor<AllocationSiteUsageContext> v(site_context);
  MaybeHandle<JSObject> copy = v.StructureWalk(object);
  Handle<JSObject> for_assert;
  DCHECK(!copy.ToHandle(&for_assert) || !for_assert.is_identical_to(object));
  return copy;
}

Handle<JSObject> CreateObjectLiteral(
    Isolate* isolate,
    Handle<ObjectBoilerplateDescription> object_boilerplate_description,
    int flags, AllocationType allocation);

Handle<JSObject> CreateArrayLiteral(
    Isolate* isolate,
    Handle<ArrayBoilerplateDescription> array_boilerplate_description,
    AllocationType allocation);

struct ObjectLiteralHelper {
  static inline Handle<JSObject> Create(Isolate* isolate,
                                        Handle<HeapObject> description,
                                        int flags, AllocationType allocation) {
    Handle<ObjectBoilerplateDescription> object_boilerplate_description =
        Handle<ObjectBoilerplateDescription>::cast(description);
    return CreateObjectLiteral(isolate, object_boilerplate_description, flags,
                               allocation);
  }
};

struct ArrayLiteralHelper {
  static inline Handle<JSObject> Create(Isolate* isolate,
                                        Handle<HeapObject> description,
                                        int flags_not_used,
                                        AllocationType allocation) {
    Handle<ArrayBoilerplateDescription> array_boilerplate_description =
        Handle<ArrayBoilerplateDescription>::cast(description);
    return CreateArrayLiteral(isolate, array_boilerplate_description,
                              allocation);
  }
};

Handle<JSObject> CreateObjectLiteral(
    Isolate* isolate,
    Handle<ObjectBoilerplateDescription> object_boilerplate_description,
    int flags, AllocationType allocation) {
  Handle<NativeContext> native_context = isolate->native_context();
  bool use_fast_elements = (flags & ObjectLiteral::kFastElements) != 0;
  bool has_null_prototype = (flags & ObjectLiteral::kHasNullPrototype) != 0;

  // In case we have function literals, we want the object to be in
  // slow properties mode for now. We don't go in the map cache because
  // maps with constant functions can't be shared if the functions are
  // not the same (which is the common case).
  int number_of_properties =
      object_boilerplate_description->backing_store_size();

  // Ignoring number_of_properties for force dictionary map with
  // __proto__:null.
  Handle<Map> map =
      has_null_prototype
          ? handle(native_context->slow_object_with_null_prototype_map(),
                   isolate)
          : isolate->factory()->ObjectLiteralMapFromCache(native_context,
                                                          number_of_properties);

  Handle<JSObject> boilerplate =
      isolate->factory()->NewFastOrSlowJSObjectFromMap(
          map, number_of_properties, allocation);

  // Normalize the elements of the boilerplate to save space if needed.
  if (!use_fast_elements) JSObject::NormalizeElements(boilerplate);

  // Add the constant properties to the boilerplate.
  int length = object_boilerplate_description->boilerplate_properties_count();
  // TODO(verwaest): Support tracking representations in the boilerplate.
  for (int index = 0; index < length; index++) {
    Handle<Object> key(object_boilerplate_description->name(index), isolate);
    Handle<Object> value(object_boilerplate_description->value(index), isolate);

    if (IsHeapObject(*value)) {
      if (IsArrayBoilerplateDescription(HeapObject::cast(*value), isolate)) {
        Handle<ArrayBoilerplateDescription> array_boilerplate =
            Handle<ArrayBoilerplateDescription>::cast(value);
        value = CreateArrayLiteral(isolate, array_boilerplate, allocation);

      } else if (IsObjectBoilerplateDescription(HeapObject::cast(*value),
                                                isolate)) {
        Handle<ObjectBoilerplateDescription> object_boilerplate =
            Handle<ObjectBoilerplateDescription>::cast(value);
        value = CreateObjectLiteral(isolate, object_boilerplate,
                                    object_boilerplate->flags(), allocation);
      }
    }

    uint32_t element_index = 0;
    if (Object::ToArrayIndex(*key, &element_index)) {
      // Array index (uint32).
      if (IsUninitialized(*value, isolate)) {
        value = handle(Smi::zero(), isolate);
      }
      JSObject::SetOwnElementIgnoreAttributes(boilerplate, element_index, value,
                                              NONE)
          .Check();
    } else {
      Handle<String> name = Handle<String>::cast(key);
      DCHECK(!name->AsArrayIndex(&element_index));
      JSObject::SetOwnPropertyIgnoreAttributes(boilerplate, name, value, NONE)
          .Check();
    }
  }

  if (map->is_dictionary_map() && !has_null_prototype) {
    // TODO(cbruni): avoid making the boilerplate fast again, the clone stub
    // supports dict-mode objects directly.
    JSObject::MigrateSlowToFast(
        boilerplate, boilerplate->map()->UnusedPropertyFields(), "FastLiteral");
  }
  return boilerplate;
}

Handle<JSObject> CreateArrayLiteral(
    Isolate* isolate,
    Handle<ArrayBoilerplateDescription> array_boilerplate_description,
    AllocationType allocation) {
  ElementsKind constant_elements_kind =
      array_boilerplate_description->elements_kind();

  Handle<FixedArrayBase> constant_elements_values(
      array_boilerplate_description->constant_elements(isolate), isolate);

  // Create the JSArray.
  Handle<FixedArrayBase> copied_elements_values;
  if (IsDoubleElementsKind(constant_elements_kind)) {
    copied_elements_values = isolate->factory()->CopyFixedDoubleArray(
        Handle<FixedDoubleArray>::cast(constant_elements_values));
  } else {
    DCHECK(IsSmiOrObjectElementsKind(constant_elements_kind));
    const bool is_cow = (constant_elements_values->map(isolate) ==
                         ReadOnlyRoots(isolate).fixed_cow_array_map());
    if (is_cow) {
      copied_elements_values = constant_elements_values;
      if (DEBUG_BOOL) {
        Handle<FixedArray> fixed_array_values =
            Handle<FixedArray>::cast(copied_elements_values);
        for (int i = 0; i < fixed_array_values->length(); i++) {
          DCHECK(!IsFixedArray(fixed_array_values->get(i)));
        }
      }
    } else {
      Handle<FixedArray> fixed_array_values =
          Handle<FixedArray>::cast(constant_elements_values);
      Handle<FixedArray> fixed_array_values_copy =
          isolate->factory()->CopyFixedArray(fixed_array_values);
      copied_elements_values = fixed_array_values_copy;
      for (int i = 0; i < fixed_array_values->length(); i++) {
        Tagged<Object> value = fixed_array_values_copy->get(i);
        Tagged<HeapObject> value_heap_object;
        if (value.GetHeapObject(isolate, &value_heap_object)) {
          if (IsArrayBoilerplateDescription(value_heap_object, isolate)) {
            HandleScope sub_scope(isolate);
            Handle<ArrayBoilerplateDescription> boilerplate(
                ArrayBoilerplateDescription::cast(value_heap_object), isolate);
            Handle<JSObject> result =
                CreateArrayLiteral(isolate, boilerplate, allocation);
            fixed_array_values_copy->set(i, *result);

          } else if (IsObjectBoilerplateDescription(value_heap_object,
                                                    isolate)) {
            HandleScope sub_scope(isolate);
            Handle<ObjectBoilerplateDescription> boilerplate(
                ObjectBoilerplateDescription::cast(value_heap_object), isolate);
            Handle<JSObject> result = CreateObjectLiteral(
                isolate, boilerplate, boilerplate->flags(), allocation);
            fixed_array_values_copy->set(i, *result);
          }
        }
      }
    }
  }
  return isolate->factory()->NewJSArrayWithElements(
      copied_elements_values, constant_elements_kind,
      copied_elements_values->length(), allocation);
}

template <typename LiteralHelper>
MaybeHandle<JSObject> CreateLiteralWithoutAllocationSite(
    Isolate* isolate, Handle<HeapObject> description, int flags) {
  Handle<JSObject> literal = LiteralHelper::Create(isolate, description, flags,
                                                   AllocationType::kYoung);
  DeprecationUpdateContext update_context(isolate);
  RETURN_ON_EXCEPTION(isolate, DeepWalk(literal, &update_context), JSObject);
  return literal;
}

template <typename LiteralHelper>
MaybeHandle<JSObject> CreateLiteral(Isolate* isolate,
                                    Handle<HeapObject> maybe_vector,
                                    int literals_index,
                                    Handle<HeapObject> description, int flags) {
  if (!IsFeedbackVector(*maybe_vector)) {
    DCHECK(IsUndefined(*maybe_vector));
    return CreateLiteralWithoutAllocationSite<LiteralHelper>(
        isolate, description, flags);
  }
  Handle<FeedbackVector> vector = Handle<FeedbackVector>::cast(maybe_vector);
  FeedbackSlot literals_slot(FeedbackVector::ToSlot(literals_index));
  CHECK(literals_slot.ToInt() < vector->length());
  Handle<Object> literal_site(Tagged<Object>::cast(vector->Get(literals_slot)),
                              isolate);
  Handle<AllocationSite> site;
  Handle<JSObject> boilerplate;

  if (HasBoilerplate(literal_site)) {
    site = Handle<AllocationSite>::cast(literal_site);
    boilerplate = Handle<JSObject>(site->boilerplate(), isolate);
  } else {
    // Eagerly create AllocationSites for literals that contain an Array.
    bool needs_initial_allocation_site =
        (flags & AggregateLiteral::kNeedsInitialAllocationSite) != 0;
    if (!needs_initial_allocation_site &&
        IsUninitializedLiteralSite(*literal_site)) {
      PreInitializeLiteralSite(vector, literals_slot);
      return CreateLiteralWithoutAllocationSite<LiteralHelper>(
          isolate, description, flags);
    } else {
      boilerplate = LiteralHelper::Create(isolate, description, flags,
                                          AllocationType::kOld);
    }
    // Install AllocationSite objects.
    AllocationSiteCreationContext creation_context(isolate);
    site = creation_context.EnterNewScope();
    RETURN_ON_EXCEPTION(isolate, DeepWalk(boilerplate, &creation_context),
                        JSObject);
    creation_context.ExitScope(site, boilerplate);

    vector->SynchronizedSet(literals_slot, *site);
  }

  static_assert(static_cast<int>(ObjectLiteral::kDisableMementos) ==
                static_cast<int>(ArrayLiteral::kDisableMementos));
  bool enable_mementos = (flags & ObjectLiteral::kDisableMementos) == 0;

  // Copy the existing boilerplate.
  AllocationSiteUsageContext usage_context(isolate, site, enable_mementos);
  usage_context.EnterNewScope();
  MaybeHandle<JSObject> copy = DeepCopy(boilerplate, &usage_context);
  usage_context.ExitScope(site, boilerplate);
  return copy;
}

}  // namespace

RUNTIME_FUNCTION(Runtime_CreateObjectLiteral) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(0);
  int literals_index = args.tagged_index_value_at(1);
  Handle<ObjectBoilerplateDescription> description =
      args.at<ObjectBoilerplateDescription>(2);
  int flags = args.smi_value_at(3);
  RETURN_RESULT_OR_FAILURE(
      isolate, CreateLiteral<ObjectLiteralHelper>(
                   isolate, maybe_vector, literals_index, description, flags));
}

RUNTIME_FUNCTION(Runtime_CreateArrayLiteral) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(0);
  int literals_index = args.tagged_index_value_at(1);
  Handle<ArrayBoilerplateDescription> elements =
      args.at<ArrayBoilerplateDescription>(2);
  int flags = args.smi_value_at(3);
  RETURN_RESULT_OR_FAILURE(
      isolate, CreateLiteral<ArrayLiteralHelper>(
                   isolate, maybe_vector, literals_index, elements, flags));
}

RUNTIME_FUNCTION(Runtime_CreateRegExpLiteral) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Handle<HeapObject> maybe_vector = args.at<HeapObject>(0);
  int index = args.tagged_index_value_at(1);
  Handle<String> pattern = args.at<String>(2);
  int flags = args.smi_value_at(3);

  if (IsUndefined(*maybe_vector)) {
    // We don't have a vector; don't create a boilerplate, simply construct a
    // plain JSRegExp instance and return it.
    RETURN_RESULT_OR_FAILURE(
        isolate, JSRegExp::New(isolate, pattern, JSRegExp::Flags(flags)));
  }

  Handle<FeedbackVector> vector = Handle<FeedbackVector>::cast(maybe_vector);
  FeedbackSlot literal_slot(FeedbackVector::ToSlot(index));
  Handle<Object> literal_site(Tagged<Object>::cast(vector->Get(literal_slot)),
                              isolate);

  // This function must not be called when a boilerplate already exists (if it
  // exists, callers should instead copy the boilerplate into a new JSRegExp
  // instance).
  CHECK(!HasBoilerplate(literal_site));

  Handle<JSRegExp> regexp_instance;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, regexp_instance,
      JSRegExp::New(isolate, pattern, JSRegExp::Flags(flags)));

  // JSRegExp literal sites are initialized in a two-step process:
  // Uninitialized-Preinitialized, and Preinitialized-Initialized.
  if (IsUninitializedLiteralSite(*literal_site)) {
    PreInitializeLiteralSite(vector, literal_slot);
    return *regexp_instance;
  }

  Handle<FixedArray> data(FixedArray::cast(regexp_instance->data()), isolate);
  Handle<String> source(String::cast(regexp_instance->source()), isolate);
  Handle<RegExpBoilerplateDescription> boilerplate =
      isolate->factory()->NewRegExpBoilerplateDescription(
          data, source,
          Smi::FromInt(static_cast<int>(regexp_instance->flags())));

  vector->SynchronizedSet(literal_slot, *boilerplate);
  DCHECK(HasBoilerplate(
      handle(Tagged<Object>::cast(vector->Get(literal_slot)), isolate)));

  return *regexp_instance;
}

}  // namespace internal
}  // namespace v8
