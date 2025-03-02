// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_STRUCT_H_
#define V8_OBJECTS_JS_STRUCT_H_

#include "src/objects/js-objects.h"
#include "src/objects/smi.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-struct-tq.inc"

class AlwaysSharedSpaceJSObject
    : public TorqueGeneratedAlwaysSharedSpaceJSObject<AlwaysSharedSpaceJSObject,
                                                      JSObject> {
 public:
  // Prepare a Map to be used as the instance map for shared JS objects.
  static void PrepareMapNoEnumerableProperties(Tagged<Map> map);
  static void PrepareMapNoEnumerableProperties(
      Isolate* isolate, Tagged<Map> map, Tagged<DescriptorArray> descriptors);
  static void PrepareMapWithEnumerableProperties(
      Isolate* isolate, DirectHandle<Map> map,
      DirectHandle<DescriptorArray> descriptors, int enum_length);

  V8_WARN_UNUSED_RESULT static Maybe<bool> DefineOwnProperty(
      Isolate* isolate, DirectHandle<AlwaysSharedSpaceJSObject> shared_obj,
      DirectHandle<Object> key, PropertyDescriptor* desc,
      Maybe<ShouldThrow> should_throw);

  // This is a generic `HasInstance` that checks the constructor's initial map
  // against the object's map. It is on `AlwaysSharedSpaceJSObject` because this
  // kind of instanceof resolution resolution is used only for shared objects.
  static Maybe<bool> HasInstance(Isolate* isolate,
                                 DirectHandle<JSFunction> constructor,
                                 DirectHandle<Object> object);

  static_assert(kHeaderSize == JSObject::kHeaderSize);
  TQ_OBJECT_CONSTRUCTORS(AlwaysSharedSpaceJSObject)
};

class JSSharedStruct
    : public TorqueGeneratedJSSharedStruct<JSSharedStruct,
                                           AlwaysSharedSpaceJSObject> {
 public:
  static DirectHandle<Map> CreateInstanceMap(
      Isolate* isolate,
      const base::Vector<const DirectHandle<Name>> field_names,
      const std::set<uint32_t>& element_names,
      MaybeDirectHandle<String> maybe_registry_key);

  static MaybeHandle<String> GetRegistryKey(Isolate* isolate,
                                            Tagged<Map> instance_map);

  static bool IsRegistryKeyDescriptor(Isolate* isolate,
                                      Tagged<Map> instance_map,
                                      InternalIndex i);

  static MaybeDirectHandle<NumberDictionary> GetElementsTemplate(
      Isolate* isolate, Tagged<Map> instance_map);

  static bool IsElementsTemplateDescriptor(Isolate* isolate,
                                           Tagged<Map> instance_map,
                                           InternalIndex i);

  DECL_PRINTER(JSSharedStruct)
  EXPORT_DECL_VERIFIER(JSSharedStruct)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(JSSharedStruct)
};

class SharedStructTypeRegistry final {
 public:
  static constexpr Tagged<Smi> deleted_element() { return Smi::FromInt(1); }

  SharedStructTypeRegistry();
  ~SharedStructTypeRegistry();

  MaybeDirectHandle<Map> Register(
      Isolate* isolate, Handle<String> key,
      const base::Vector<const DirectHandle<Name>> field_names,
      const std::set<uint32_t>& element_names);

  void IterateElements(Isolate* isolate, RootVisitor* visitor);
  void NotifyElementsRemoved(int count);

 private:
  class Data;

  MaybeDirectHandle<Map> RegisterNoThrow(
      Isolate* isolate, Handle<String> key,
      const base::Vector<const DirectHandle<Name>> field_names,
      const std::set<uint32_t>& element_names);

  MaybeDirectHandle<Map> CheckIfEntryMatches(
      Isolate* isolate, InternalIndex entry, DirectHandle<String> key,
      const base::Vector<const DirectHandle<Name>> field_names,
      const std::set<uint32_t>& element_names);

  void EnsureCapacity(PtrComprCageBase cage_base, int additional_elements);

  std::unique_ptr<Data> data_;

  // Protects all access to the registry.
  base::Mutex data_mutex_;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_STRUCT_H_
