// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/literal-objects.h"

#include "src/ast/ast.h"
#include "src/base/logging.h"
#include "src/builtins/accessors.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/heap/local-factory-inl.h"
#include "src/objects/dictionary.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/js-regexp.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/objects/struct-inl.h"

namespace v8 {
namespace internal {

namespace {

// The enumeration order index in the property details is unused if they are
// stored in a SwissNameDictionary or NumberDictionary (because they handle
// propery ordering differently). We then use this dummy value instead.
constexpr int kDummyEnumerationIndex = 0;

inline int EncodeComputedEntry(ClassBoilerplate::ValueKind value_kind,
                               unsigned key_index) {
  using Flags = ClassBoilerplate::ComputedEntryFlags;
  int flags = Flags::ValueKindBits::encode(value_kind) |
              Flags::KeyIndexBits::encode(key_index);
  return flags;
}

constexpr AccessorComponent ToAccessorComponent(
    ClassBoilerplate::ValueKind value_kind) {
  return value_kind == ClassBoilerplate::kGetter ? ACCESSOR_GETTER
                                                 : ACCESSOR_SETTER;
}

template <typename IsolateT>
void AddToDescriptorArrayTemplate(
    IsolateT* isolate, Handle<DescriptorArray> descriptor_array_template,
    Handle<Name> name, ClassBoilerplate::ValueKind value_kind,
    Handle<Object> value) {
  InternalIndex entry = descriptor_array_template->Search(
      *name, descriptor_array_template->number_of_descriptors());
  // TODO(ishell): deduplicate properties at AST level, this will allow us to
  // avoid creation of closures that will be overwritten anyway.
  if (entry.is_not_found()) {
    // Entry not found, add new one.
    Descriptor d;
    if (value_kind == ClassBoilerplate::kData) {
      d = Descriptor::DataConstant(name, value, DONT_ENUM);
    } else {
      DCHECK(value_kind == ClassBoilerplate::kGetter ||
             value_kind == ClassBoilerplate::kSetter);
      Handle<AccessorPair> pair = isolate->factory()->NewAccessorPair();
      pair->set(ToAccessorComponent(value_kind), *value);
      d = Descriptor::AccessorConstant(name, pair, DONT_ENUM);
    }
    descriptor_array_template->Append(&d);

  } else {
    // Entry found, update it.
    int sorted_index = descriptor_array_template->GetDetails(entry).pointer();
    if (value_kind == ClassBoilerplate::kData) {
      Descriptor d = Descriptor::DataConstant(name, value, DONT_ENUM);
      d.SetSortedKeyIndex(sorted_index);
      descriptor_array_template->Set(entry, &d);
    } else {
      DCHECK(value_kind == ClassBoilerplate::kGetter ||
             value_kind == ClassBoilerplate::kSetter);
      Tagged<Object> raw_accessor =
          descriptor_array_template->GetStrongValue(entry);
      Tagged<AccessorPair> pair;
      if (IsAccessorPair(raw_accessor)) {
        pair = AccessorPair::cast(raw_accessor);
      } else {
        Handle<AccessorPair> new_pair = isolate->factory()->NewAccessorPair();
        Descriptor d = Descriptor::AccessorConstant(name, new_pair, DONT_ENUM);
        d.SetSortedKeyIndex(sorted_index);
        descriptor_array_template->Set(entry, &d);
        pair = *new_pair;
      }
      pair->set(ToAccessorComponent(value_kind), *value, kReleaseStore);
    }
  }
}

template <typename IsolateT>
Handle<NameDictionary> DictionaryAddNoUpdateNextEnumerationIndex(
    IsolateT* isolate, Handle<NameDictionary> dictionary, Handle<Name> name,
    Handle<Object> value, PropertyDetails details,
    InternalIndex* entry_out = nullptr) {
  return NameDictionary::AddNoUpdateNextEnumerationIndex(
      isolate, dictionary, name, value, details, entry_out);
}

template <typename IsolateT>
Handle<SwissNameDictionary> DictionaryAddNoUpdateNextEnumerationIndex(
    IsolateT* isolate, Handle<SwissNameDictionary> dictionary,
    Handle<Name> name, Handle<Object> value, PropertyDetails details,
    InternalIndex* entry_out = nullptr) {
  // SwissNameDictionary does not maintain the enumeration order in property
  // details, so it's a normal Add().
  return SwissNameDictionary::Add(isolate, dictionary, name, value, details);
}

template <typename IsolateT>
Handle<NumberDictionary> DictionaryAddNoUpdateNextEnumerationIndex(
    IsolateT* isolate, Handle<NumberDictionary> dictionary, uint32_t element,
    Handle<Object> value, PropertyDetails details,
    InternalIndex* entry_out = nullptr) {
  // NumberDictionary does not maintain the enumeration order, so it's
  // a normal Add().
  return NumberDictionary::Add(isolate, dictionary, element, value, details,
                               entry_out);
}

template <typename Dictionary>
void DictionaryUpdateMaxNumberKey(Handle<Dictionary> dictionary,
                                  Handle<Name> name) {
  static_assert((std::is_same<Dictionary, SwissNameDictionary>::value ||
                 std::is_same<Dictionary, NameDictionary>::value));
  // No-op for (ordered) name dictionaries.
}

void DictionaryUpdateMaxNumberKey(Handle<NumberDictionary> dictionary,
                                  uint32_t element) {
  dictionary->UpdateMaxNumberKey(element, Handle<JSObject>());
  dictionary->set_requires_slow_elements();
}

constexpr int ComputeEnumerationIndex(int value_index) {
  // We "shift" value indices to ensure that the enumeration index for the value
  // will not overlap with minimum properties set for both class and prototype
  // objects.
  return value_index +
         std::max({ClassBoilerplate::kMinimumClassPropertiesCount,
                   ClassBoilerplate::kMinimumPrototypePropertiesCount});
}

constexpr int kAccessorNotDefined = -1;

inline int GetExistingValueIndex(Tagged<Object> value) {
  return IsSmi(value) ? Smi::ToInt(value) : kAccessorNotDefined;
}

template <typename IsolateT, typename Dictionary, typename Key>
void AddToDictionaryTemplate(IsolateT* isolate, Handle<Dictionary> dictionary,
                             Key key, int key_index,
                             ClassBoilerplate::ValueKind value_kind,
                             Tagged<Smi> value) {
  InternalIndex entry = dictionary->FindEntry(isolate, key);

  const bool is_elements_dictionary =
      std::is_same<Dictionary, NumberDictionary>::value;
  static_assert(is_elements_dictionary !=
                (std::is_same<Dictionary, NameDictionary>::value ||
                 std::is_same<Dictionary, SwissNameDictionary>::value));

  if (entry.is_not_found()) {
    // Entry not found, add new one.
    int enum_order =
        Dictionary::kIsOrderedDictionaryType || is_elements_dictionary
            ? kDummyEnumerationIndex
            : ComputeEnumerationIndex(key_index);
    Handle<Object> value_handle;
    PropertyDetails details(
        value_kind != ClassBoilerplate::kData ? PropertyKind::kAccessor
                                              : PropertyKind::kData,
        DONT_ENUM, PropertyDetails::kConstIfDictConstnessTracking, enum_order);
    if (value_kind == ClassBoilerplate::kData) {
      value_handle = handle(value, isolate);
    } else {
      Handle<AccessorPair> pair(isolate->factory()->NewAccessorPair());
      pair->set(ToAccessorComponent(value_kind), value);
      value_handle = pair;
    }

    // Add value to the dictionary without updating next enumeration index.
    Handle<Dictionary> dict = DictionaryAddNoUpdateNextEnumerationIndex(
        isolate, dictionary, key, value_handle, details, &entry);
    // It is crucial to avoid dictionary reallocations because it may remove
    // potential gaps in enumeration indices values that are necessary for
    // inserting computed properties into right places in the enumeration order.
    CHECK_EQ(*dict, *dictionary);

    DictionaryUpdateMaxNumberKey(dictionary, key);

  } else {
    // Entry found, update it.
    int enum_order_existing =
        Dictionary::kIsOrderedDictionaryType
            ? kDummyEnumerationIndex
            : dictionary->DetailsAt(entry).dictionary_index();
    int enum_order_computed =
        Dictionary::kIsOrderedDictionaryType || is_elements_dictionary
            ? kDummyEnumerationIndex
            : ComputeEnumerationIndex(key_index);

    Tagged<Object> existing_value = dictionary->ValueAt(entry);
    if (value_kind == ClassBoilerplate::kData) {
      // Computed value is a normal method.
      if (IsAccessorPair(existing_value)) {
        Tagged<AccessorPair> current_pair = AccessorPair::cast(existing_value);

        int existing_getter_index =
            GetExistingValueIndex(current_pair->getter());
        int existing_setter_index =
            GetExistingValueIndex(current_pair->setter());
        // At least one of the accessors must already be defined.
        static_assert(kAccessorNotDefined < 0);
        DCHECK(existing_getter_index >= 0 || existing_setter_index >= 0);
        if (existing_getter_index < key_index &&
            existing_setter_index < key_index) {
          // Either both getter and setter were defined before the computed
          // method or just one of them was defined before while the other one
          // was not defined yet, so overwrite property to kData.
          PropertyDetails details(
              PropertyKind::kData, DONT_ENUM,
              PropertyDetails::kConstIfDictConstnessTracking,
              enum_order_existing);
          dictionary->DetailsAtPut(entry, details);
          dictionary->ValueAtPut(entry, value);

        } else if (existing_getter_index != kAccessorNotDefined &&
                   existing_getter_index < key_index) {
          DCHECK_LT(key_index, existing_setter_index);
          // Getter was defined and it was done before the computed method
          // and then it was overwritten by the current computed method which
          // in turn was later overwritten by the setter method. So we clear
          // the getter.
          current_pair->set_getter(*isolate->factory()->null_value());

        } else if (existing_setter_index != kAccessorNotDefined &&
                   existing_setter_index < key_index) {
          DCHECK_LT(key_index, existing_getter_index);
          // Setter was defined and it was done before the computed method
          // and then it was overwritten by the current computed method which
          // in turn was later overwritten by the getter method. So we clear
          // the setter.
          current_pair->set_setter(*isolate->factory()->null_value());

        } else {
          // One of the following cases holds:
          // The computed method was defined before ...
          // 1.) the getter and setter, both of which are defined,
          // 2.) the getter, and the setter isn't defined,
          // 3.) the setter, and the getter isn't defined.
          // Therefore, the computed value is overwritten, receiving the
          // computed property's enum index.
          DCHECK(key_index < existing_getter_index ||
                 existing_getter_index == kAccessorNotDefined);
          DCHECK(key_index < existing_setter_index ||
                 existing_setter_index == kAccessorNotDefined);
          DCHECK(existing_getter_index != kAccessorNotDefined ||
                 existing_setter_index != kAccessorNotDefined);
          if (!is_elements_dictionary) {
            // The enum index is unused by elements dictionaries,
            // which is why we don't need to update the property details if
            // |is_elements_dictionary| holds.
            PropertyDetails details = dictionary->DetailsAt(entry);
            details = details.set_index(enum_order_computed);
            dictionary->DetailsAtPut(entry, details);
          }
        }
      } else {  // if (existing_value.IsAccessorPair()) ends here
        DCHECK(value_kind == ClassBoilerplate::kData);

        DCHECK_IMPLIES(!IsSmi(existing_value), IsAccessorInfo(existing_value));
        DCHECK_IMPLIES(!IsSmi(existing_value),
                       AccessorInfo::cast(existing_value)->name() ==
                               *isolate->factory()->length_string() ||
                           AccessorInfo::cast(existing_value)->name() ==
                               *isolate->factory()->name_string());
        if (!IsSmi(existing_value) || Smi::ToInt(existing_value) < key_index) {
          // Overwrite existing value because it was defined before the computed
          // one (AccessorInfo "length" and "name" properties are always defined
          // before).
          PropertyDetails details(
              PropertyKind::kData, DONT_ENUM,
              PropertyDetails::kConstIfDictConstnessTracking,
              enum_order_existing);
          dictionary->DetailsAtPut(entry, details);
          dictionary->ValueAtPut(entry, value);
        } else {
          // The computed value appears before the existing one. Set the
          // existing entry's enum index to that of the computed one.
          if (!is_elements_dictionary) {
            // The enum index is unused by elements dictionaries,
            // which is why we don't need to update the property details if
            // |is_elements_dictionary| holds.
            PropertyDetails details(
                PropertyKind::kData, DONT_ENUM,
                PropertyDetails::kConstIfDictConstnessTracking,
                enum_order_computed);

            dictionary->DetailsAtPut(entry, details);
          }
        }
      }
    } else {  // if (value_kind == ClassBoilerplate::kData) ends here
      AccessorComponent component = ToAccessorComponent(value_kind);
      if (IsAccessorPair(existing_value)) {
        // Update respective component of existing AccessorPair.
        Tagged<AccessorPair> current_pair = AccessorPair::cast(existing_value);

        int existing_component_index =
            GetExistingValueIndex(current_pair->get(component));
        if (existing_component_index < key_index) {
          current_pair->set(component, value, kReleaseStore);
        } else {
          // The existing accessor property overwrites the computed one, update
          // its enumeration order accordingly.

          if (!is_elements_dictionary) {
            // The enum index is unused by elements dictionaries,
            // which is why we don't need to update the property details if
            // |is_elements_dictionary| holds.

            PropertyDetails details(
                PropertyKind::kAccessor, DONT_ENUM,
                PropertyDetails::kConstIfDictConstnessTracking,
                enum_order_computed);
            dictionary->DetailsAtPut(entry, details);
          }
        }

      } else {
        DCHECK(!IsAccessorPair(existing_value));
        DCHECK(value_kind != ClassBoilerplate::kData);

        if (!IsSmi(existing_value) || Smi::ToInt(existing_value) < key_index) {
          // Overwrite the existing data property because it was defined before
          // the computed accessor property.
          Handle<AccessorPair> pair(isolate->factory()->NewAccessorPair());
          pair->set(component, value);
          PropertyDetails details(
              PropertyKind::kAccessor, DONT_ENUM,
              PropertyDetails::kConstIfDictConstnessTracking,
              enum_order_existing);
          dictionary->DetailsAtPut(entry, details);
          dictionary->ValueAtPut(entry, *pair);
        } else {
          // The computed accessor property appears before the existing data
          // property. Set the existing entry's enum index to that of the
          // computed one.

          if (!is_elements_dictionary) {
            // The enum index is unused by elements dictionaries,
            // which is why we don't need to update the property details if
            // |is_elements_dictionary| holds.
            PropertyDetails details(
                PropertyKind::kData, DONT_ENUM,
                PropertyDetails::kConstIfDictConstnessTracking,
                enum_order_computed);

            dictionary->DetailsAtPut(entry, details);
          }
        }
      }
    }
  }
}

}  // namespace

// Helper class that eases building of a properties, elements and computed
// properties templates.
template <typename IsolateT>
class ObjectDescriptor {
 public:
  void IncComputedCount() { ++computed_count_; }
  void IncPropertiesCount() { ++property_count_; }
  void IncElementsCount() { ++element_count_; }

  explicit ObjectDescriptor(int property_slack)
      : property_slack_(property_slack) {}

  bool HasDictionaryProperties() const {
    return computed_count_ > 0 ||
           (property_count_ + property_slack_) > kMaxNumberOfDescriptors;
  }

  Handle<Object> properties_template() const {
    return HasDictionaryProperties()
               ? properties_dictionary_template_
               : Handle<Object>::cast(descriptor_array_template_);
  }

  Handle<NumberDictionary> elements_template() const {
    return elements_dictionary_template_;
  }

  Handle<FixedArray> computed_properties() const {
    return computed_properties_;
  }

  void CreateTemplates(IsolateT* isolate) {
    auto* factory = isolate->factory();
    descriptor_array_template_ = factory->empty_descriptor_array();
    if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
      properties_dictionary_template_ =
          factory->empty_swiss_property_dictionary();
    } else {
      properties_dictionary_template_ = factory->empty_property_dictionary();
    }
    if (property_count_ || computed_count_ || property_slack_) {
      if (HasDictionaryProperties()) {
        int need_space_for =
            property_count_ + computed_count_ + property_slack_;
        if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
          properties_dictionary_template_ =
              isolate->factory()->NewSwissNameDictionary(need_space_for,
                                                         AllocationType::kOld);

        } else {
          properties_dictionary_template_ = NameDictionary::New(
              isolate, need_space_for, AllocationType::kOld);
        }
      } else {
        descriptor_array_template_ = DescriptorArray::Allocate(
            isolate, 0, property_count_ + property_slack_,
            AllocationType::kOld);
      }
    }
    elements_dictionary_template_ =
        element_count_ || computed_count_
            ? NumberDictionary::New(isolate, element_count_ + computed_count_,
                                    AllocationType::kOld)
            : factory->empty_slow_element_dictionary();

    computed_properties_ =
        computed_count_
            ? factory->NewFixedArray(computed_count_, AllocationType::kOld)
            : factory->empty_fixed_array();

    temp_handle_ = handle(Smi::zero(), isolate);
  }

  void AddConstant(IsolateT* isolate, Handle<Name> name, Handle<Object> value,
                   PropertyAttributes attribs) {
    bool is_accessor = IsAccessorInfo(*value);
    DCHECK(!IsAccessorPair(*value));
    if (HasDictionaryProperties()) {
      PropertyKind kind =
          is_accessor ? i::PropertyKind::kAccessor : i::PropertyKind::kData;
      int enum_order = V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL
                           ? kDummyEnumerationIndex
                           : next_enumeration_index_++;
      PropertyDetails details(kind, attribs, PropertyCellType::kNoCell,
                              enum_order);
      if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
        properties_dictionary_template_ =
            DictionaryAddNoUpdateNextEnumerationIndex(
                isolate, properties_ordered_dictionary_template(), name, value,
                details);
      } else {
        properties_dictionary_template_ =
            DictionaryAddNoUpdateNextEnumerationIndex(
                isolate, properties_dictionary_template(), name, value,
                details);
      }
    } else {
      Descriptor d = is_accessor
                         ? Descriptor::AccessorConstant(name, value, attribs)
                         : Descriptor::DataConstant(name, value, attribs);
      descriptor_array_template_->Append(&d);
    }
  }

  void AddNamedProperty(IsolateT* isolate, Handle<Name> name,
                        ClassBoilerplate::ValueKind value_kind,
                        int value_index) {
    Tagged<Smi> value = Smi::FromInt(value_index);
    if (HasDictionaryProperties()) {
      UpdateNextEnumerationIndex(value_index);
      if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
        AddToDictionaryTemplate(isolate,
                                properties_ordered_dictionary_template(), name,
                                value_index, value_kind, value);
      } else {
        AddToDictionaryTemplate(isolate, properties_dictionary_template(), name,
                                value_index, value_kind, value);
      }
    } else {
      temp_handle_.PatchValue(value);
      AddToDescriptorArrayTemplate(isolate, descriptor_array_template_, name,
                                   value_kind, temp_handle_);
    }
  }

  void AddIndexedProperty(IsolateT* isolate, uint32_t element,
                          ClassBoilerplate::ValueKind value_kind,
                          int value_index) {
    Tagged<Smi> value = Smi::FromInt(value_index);
    AddToDictionaryTemplate(isolate, elements_dictionary_template_, element,
                            value_index, value_kind, value);
  }

  void AddComputed(ClassBoilerplate::ValueKind value_kind, int key_index) {
    int value_index = key_index + 1;
    UpdateNextEnumerationIndex(value_index);

    int flags = EncodeComputedEntry(value_kind, key_index);
    computed_properties_->set(current_computed_index_++, Smi::FromInt(flags));
  }

  void UpdateNextEnumerationIndex(int value_index) {
    int current_index = ComputeEnumerationIndex(value_index);
    DCHECK_LE(next_enumeration_index_, current_index);
    next_enumeration_index_ = current_index + 1;
  }

  void Finalize(IsolateT* isolate) {
    if (HasDictionaryProperties()) {
      DCHECK_EQ(current_computed_index_, computed_properties_->length());
      if (!V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
        properties_dictionary_template()->set_next_enumeration_index(
            next_enumeration_index_);
      }
    } else {
      DCHECK(descriptor_array_template_->IsSortedNoDuplicates());
    }
  }

 private:
  Handle<NameDictionary> properties_dictionary_template() const {
    return Handle<NameDictionary>::cast(properties_dictionary_template_);
  }

  Handle<SwissNameDictionary> properties_ordered_dictionary_template() const {
    return Handle<SwissNameDictionary>::cast(properties_dictionary_template_);
  }

  const int property_slack_;
  int property_count_ = 0;
  int next_enumeration_index_ = PropertyDetails::kInitialIndex;
  int element_count_ = 0;
  int computed_count_ = 0;
  int current_computed_index_ = 0;

  Handle<DescriptorArray> descriptor_array_template_;

  // Is either a NameDictionary or SwissNameDictionary.
  Handle<HeapObject> properties_dictionary_template_;

  Handle<NumberDictionary> elements_dictionary_template_;
  Handle<FixedArray> computed_properties_;
  // This temporary handle is used for storing to descriptor array.
  Handle<Object> temp_handle_;
};

template <typename IsolateT, typename PropertyDict>
void ClassBoilerplate::AddToPropertiesTemplate(
    IsolateT* isolate, Handle<PropertyDict> dictionary, Handle<Name> name,
    int key_index, ClassBoilerplate::ValueKind value_kind, Tagged<Smi> value) {
  AddToDictionaryTemplate(isolate, dictionary, name, key_index, value_kind,
                          value);
}
template void ClassBoilerplate::AddToPropertiesTemplate(
    Isolate* isolate, Handle<NameDictionary> dictionary, Handle<Name> name,
    int key_index, ClassBoilerplate::ValueKind value_kind, Tagged<Smi> value);
template void ClassBoilerplate::AddToPropertiesTemplate(
    LocalIsolate* isolate, Handle<NameDictionary> dictionary, Handle<Name> name,
    int key_index, ClassBoilerplate::ValueKind value_kind, Tagged<Smi> value);
template void ClassBoilerplate::AddToPropertiesTemplate(
    Isolate* isolate, Handle<SwissNameDictionary> dictionary, Handle<Name> name,
    int key_index, ClassBoilerplate::ValueKind value_kind, Tagged<Smi> value);

template <typename IsolateT>
void ClassBoilerplate::AddToElementsTemplate(
    IsolateT* isolate, Handle<NumberDictionary> dictionary, uint32_t key,
    int key_index, ClassBoilerplate::ValueKind value_kind, Tagged<Smi> value) {
  AddToDictionaryTemplate(isolate, dictionary, key, key_index, value_kind,
                          value);
}
template void ClassBoilerplate::AddToElementsTemplate(
    Isolate* isolate, Handle<NumberDictionary> dictionary, uint32_t key,
    int key_index, ClassBoilerplate::ValueKind value_kind, Tagged<Smi> value);
template void ClassBoilerplate::AddToElementsTemplate(
    LocalIsolate* isolate, Handle<NumberDictionary> dictionary, uint32_t key,
    int key_index, ClassBoilerplate::ValueKind value_kind, Tagged<Smi> value);

// static
template <typename IsolateT>
Handle<ClassBoilerplate> ClassBoilerplate::New(IsolateT* isolate,
                                               ClassLiteral* expr,
                                               AllocationType allocation) {
  // Create a non-caching handle scope to ensure that the temporary handle used
  // by ObjectDescriptor for passing Smis around does not corrupt handle cache
  // in CanonicalHandleScope.
  typename IsolateT::HandleScopeType scope(isolate);
  auto* factory = isolate->factory();
  ObjectDescriptor<IsolateT> static_desc(kMinimumClassPropertiesCount);
  ObjectDescriptor<IsolateT> instance_desc(kMinimumPrototypePropertiesCount);

  for (int i = 0; i < expr->public_members()->length(); i++) {
    ClassLiteral::Property* property = expr->public_members()->at(i);
    ObjectDescriptor<IsolateT>& desc =
        property->is_static() ? static_desc : instance_desc;
    if (property->is_computed_name()) {
      if (property->kind() != ClassLiteral::Property::FIELD) {
        desc.IncComputedCount();
      }
    } else {
      if (property->key()->AsLiteral()->IsPropertyName()) {
        desc.IncPropertiesCount();
      } else {
        desc.IncElementsCount();
      }
    }
  }

  //
  // Initialize class object template.
  //
  static_desc.CreateTemplates(isolate);
  static_assert(JSFunction::kLengthDescriptorIndex == 0);
  {
    // Add length_accessor.
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);
    static_desc.AddConstant(isolate, factory->length_string(),
                            factory->function_length_accessor(), attribs);
  }
  {
    // Add name_accessor.
    // All classes, even anonymous ones, have a name accessor.
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);
    static_desc.AddConstant(isolate, factory->name_string(),
                            factory->function_name_accessor(), attribs);
  }
  {
    // Add prototype_accessor.
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
    static_desc.AddConstant(isolate, factory->prototype_string(),
                            factory->function_prototype_accessor(), attribs);
  }
  {
    Handle<ClassPositions> class_positions = factory->NewClassPositions(
        expr->start_position(), expr->end_position());
    static_desc.AddConstant(isolate, factory->class_positions_symbol(),
                            class_positions, DONT_ENUM);
  }

  //
  // Initialize prototype object template.
  //
  instance_desc.CreateTemplates(isolate);
  {
    Handle<Object> value(
        Smi::FromInt(ClassBoilerplate::kConstructorArgumentIndex), isolate);
    instance_desc.AddConstant(isolate, factory->constructor_string(), value,
                              DONT_ENUM);
  }

  //
  // Fill in class boilerplate.
  //
  int dynamic_argument_index = ClassBoilerplate::kFirstDynamicArgumentIndex;

  for (int i = 0; i < expr->public_members()->length(); i++) {
    ClassLiteral::Property* property = expr->public_members()->at(i);
    ClassBoilerplate::ValueKind value_kind;
    switch (property->kind()) {
      case ClassLiteral::Property::METHOD:
        value_kind = ClassBoilerplate::kData;
        break;
      case ClassLiteral::Property::GETTER:
        value_kind = ClassBoilerplate::kGetter;
        break;
      case ClassLiteral::Property::SETTER:
        value_kind = ClassBoilerplate::kSetter;
        break;
      case ClassLiteral::Property::FIELD:
        DCHECK_IMPLIES(property->is_computed_name(), !property->is_private());
        if (property->is_computed_name()) {
          ++dynamic_argument_index;
        }
        continue;
    }

    ObjectDescriptor<IsolateT>& desc =
        property->is_static() ? static_desc : instance_desc;
    if (property->is_computed_name()) {
      int computed_name_index = dynamic_argument_index;
      dynamic_argument_index += 2;  // Computed name and value indices.
      desc.AddComputed(value_kind, computed_name_index);
      continue;
    }
    int value_index = dynamic_argument_index++;

    Literal* key_literal = property->key()->AsLiteral();
    uint32_t index;
    if (key_literal->AsArrayIndex(&index)) {
      desc.AddIndexedProperty(isolate, index, value_kind, value_index);

    } else {
      Handle<String> name = key_literal->AsRawPropertyName()->string();
      DCHECK(IsInternalizedString(*name));
      desc.AddNamedProperty(isolate, name, value_kind, value_index);
    }
  }

  static_desc.Finalize(isolate);
  instance_desc.Finalize(isolate);

  auto result = Handle<ClassBoilerplate>::cast(
      factory->NewStruct(CLASS_BOILERPLATE_TYPE, allocation));

  result->set_arguments_count(dynamic_argument_index);

  result->set_static_properties_template(*static_desc.properties_template());
  result->set_static_elements_template(*static_desc.elements_template());
  result->set_static_computed_properties(*static_desc.computed_properties());

  result->set_instance_properties_template(
      *instance_desc.properties_template());
  result->set_instance_elements_template(*instance_desc.elements_template());
  result->set_instance_computed_properties(
      *instance_desc.computed_properties());

  return scope.CloseAndEscape(result);
}

template Handle<ClassBoilerplate> ClassBoilerplate::New(
    Isolate* isolate, ClassLiteral* expr, AllocationType allocation);
template Handle<ClassBoilerplate> ClassBoilerplate::New(
    LocalIsolate* isolate, ClassLiteral* expr, AllocationType allocation);

void ArrayBoilerplateDescription::BriefPrintDetails(std::ostream& os) {
  os << " " << ElementsKindToString(elements_kind()) << ", "
     << Brief(constant_elements());
}

void RegExpBoilerplateDescription::BriefPrintDetails(std::ostream& os) {
  // Note: keep boilerplate layout synced with JSRegExp layout.
  static_assert(JSRegExp::kDataOffset == JSObject::kHeaderSize);
  static_assert(JSRegExp::kSourceOffset == JSRegExp::kDataOffset + kTaggedSize);
  static_assert(JSRegExp::kFlagsOffset ==
                JSRegExp::kSourceOffset + kTaggedSize);
  static_assert(JSRegExp::kHeaderSize == JSRegExp::kFlagsOffset + kTaggedSize);
  os << " " << Brief(data()) << ", " << Brief(source()) << ", " << flags();
}

}  // namespace internal
}  // namespace v8
