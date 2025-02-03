// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TEMPLATES_H_
#define V8_OBJECTS_TEMPLATES_H_

#include <optional>
#include <string_view>

#include "include/v8-exception.h"
#include "include/v8-memory-span.h"
#include "src/handles/handles.h"
#include "src/objects/contexts.h"
#include "src/objects/struct.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {

class CFunctionInfo;
class StructBodyDescriptor;

namespace internal {

#include "torque-generated/src/objects/templates-tq.inc"

class TemplateInfo
    : public TorqueGeneratedTemplateInfo<TemplateInfo, HeapObject> {
 public:
  static const int kFastTemplateInstantiationsCacheSize = 1 * KB;

  // While we could grow the slow cache until we run out of memory, we put
  // a limit on it anyway to not crash for embedders that re-create templates
  // instead of caching them.
  static const int kSlowTemplateInstantiationsCacheSize = 1 * MB;

  // If the serial number is set to kDoNotCache, then we should never cache this
  // TemplateInfo.
  static const int kDoNotCache = -1;
  // If the serial number is set to kUncached, it means that this TemplateInfo
  // has not been cached yet but it can be.
  static const int kUncached = -2;

  inline bool should_cache() const;
  inline bool is_cached() const;

  inline bool TryGetIsolate(Isolate** isolate) const;
  inline Isolate* GetIsolateChecked() const;

  using BodyDescriptor = StructBodyDescriptor;

  // Whether or not to cache every instance: when we materialize a getter or
  // setter from an lazy AccessorPair, we rely on this cache to be able to
  // always return the same getter or setter. However, objects will be cloned
  // anyways, so it's not observable if we didn't cache an instance.
  // Furthermore, a badly behaved embedder might create an unlimited number of
  // objects, so we limit the cache for those cases.
  enum class CachingMode { kLimited, kUnlimited };

  template <typename ReturnType>
  static MaybeHandle<ReturnType> ProbeInstantiationsCache(
      Isolate* isolate, DirectHandle<NativeContext> native_context,
      int serial_number, CachingMode caching_mode);

  template <typename InstantiationType, typename TemplateInfoType>
  static void CacheTemplateInstantiation(
      Isolate* isolate, DirectHandle<NativeContext> native_context,
      DirectHandle<TemplateInfoType> data, CachingMode caching_mode,
      Handle<InstantiationType> object);

  template <typename TemplateInfoType>
  static void UncacheTemplateInstantiation(
      Isolate* isolate, DirectHandle<NativeContext> native_context,
      DirectHandle<TemplateInfoType> data, CachingMode caching_mode);

  TQ_OBJECT_CONSTRUCTORS(TemplateInfo)
};

// Contains data members that are rarely set on a FunctionTemplateInfo.
class FunctionTemplateRareData
    : public TorqueGeneratedFunctionTemplateRareData<FunctionTemplateRareData,
                                                     Struct> {
 public:
  DECL_VERIFIER(FunctionTemplateRareData)

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(FunctionTemplateRareData)
};

// See the api-exposed FunctionTemplate for more information.
class FunctionTemplateInfo
    : public TorqueGeneratedFunctionTemplateInfo<FunctionTemplateInfo,
                                                 TemplateInfo> {
 public:
#define DECL_RARE_ACCESSORS(Name, CamelName, ...)                \
  DECL_GETTER(Get##CamelName, Tagged<__VA_ARGS__>)               \
  static inline void Set##CamelName(                             \
      Isolate* isolate,                                          \
      DirectHandle<FunctionTemplateInfo> function_template_info, \
      DirectHandle<__VA_ARGS__> Name);

  // ObjectTemplateInfo or Undefined, used for the prototype property of the
  // resulting JSFunction instance of this FunctionTemplate.
  DECL_RARE_ACCESSORS(prototype_template, PrototypeTemplate,
                      UnionOf<Undefined, ObjectTemplateInfo>)

  // In the case the prototype_template is Undefined we use the
  // prototype_provider_template to retrieve the instance prototype. Either
  // contains an FunctionTemplateInfo or Undefined.
  DECL_RARE_ACCESSORS(prototype_provider_template, PrototypeProviderTemplate,
                      UnionOf<Undefined, FunctionTemplateInfo>)

  // Used to create prototype chains. The parent_template's prototype is set as
  // __proto__ of this FunctionTemplate's instance prototype. Is either a
  // FunctionTemplateInfo or Undefined.
  DECL_RARE_ACCESSORS(parent_template, ParentTemplate,
                      UnionOf<Undefined, FunctionTemplateInfo>)

  // Returns an InterceptorInfo or Undefined for named properties.
  DECL_RARE_ACCESSORS(named_property_handler, NamedPropertyHandler,
                      UnionOf<Undefined, InterceptorInfo>)
  // Returns an InterceptorInfo or Undefined for indexed properties/elements.
  DECL_RARE_ACCESSORS(indexed_property_handler, IndexedPropertyHandler,
                      UnionOf<Undefined, InterceptorInfo>)

  // An ObjectTemplateInfo that is used when instantiating the JSFunction
  // associated with this FunctionTemplateInfo. Contains either an
  // ObjectTemplateInfo or Undefined. A default instance_template is assigned
  // upon first instantiation if it's Undefined.
  DECL_RARE_ACCESSORS(instance_template, InstanceTemplate,
                      UnionOf<Undefined, ObjectTemplateInfo>)

  // Either a FunctionTemplateInfo or Undefined. If an instance_call_handler is
  // provided the instances created from the associated JSFunction are marked as
  // callable.
  DECL_RARE_ACCESSORS(instance_call_handler, InstanceCallHandler,
                      UnionOf<Undefined, FunctionTemplateInfo>)

  DECL_RARE_ACCESSORS(access_check_info, AccessCheckInfo,
                      UnionOf<Undefined, AccessCheckInfo>)

  DECL_RARE_ACCESSORS(c_function_overloads, CFunctionOverloads, FixedArray)
#undef DECL_RARE_ACCESSORS

  DECL_RELAXED_UINT32_ACCESSORS(flag)

  // Begin flag bits ---------------------

  // This FunctionTemplateInfo is just a storage for callback function and
  // callback data for a callable ObjectTemplate object.
  DECL_BOOLEAN_ACCESSORS(is_object_template_call_handler)

  DECL_BOOLEAN_ACCESSORS(has_side_effects)

  DECL_BOOLEAN_ACCESSORS(undetectable)

  // If set, object instances created by this function requires access check.
  DECL_BOOLEAN_ACCESSORS(needs_access_check)

  DECL_BOOLEAN_ACCESSORS(read_only_prototype)

  // If set, do not create a prototype property for the associated
  // JSFunction. This bit implies that neither the prototype_template nor the
  // prototype_provoider_template are instantiated.
  DECL_BOOLEAN_ACCESSORS(remove_prototype)

  // If not set an access may be performed on calling the associated JSFunction.
  DECL_BOOLEAN_ACCESSORS(accept_any_receiver)

  // This flag is used to check that the FunctionTemplateInfo instance is not
  // changed after it became visible to TurboFan (either set in a
  // SharedFunctionInfo or an accessor), because TF relies on immutability to
  // safely read concurrently.
  DECL_BOOLEAN_ACCESSORS(published)

  // This specifies the permissable range of instance type of objects that can
  // be allowed to be used as receivers with the given template.
  DECL_PRIMITIVE_GETTER(allowed_receiver_instance_type_range_start,
                        InstanceType)
  DECL_PRIMITIVE_GETTER(allowed_receiver_instance_type_range_end, InstanceType)

  // End flag bits ---------------------

  inline InstanceType GetInstanceType() const;
  inline void SetInstanceType(int api_instance_type);

  inline void SetAllowedReceiverInstanceTypeRange(int api_instance_type_start,
                                                  int api_instance_type_end);

  static Handle<SharedFunctionInfo> GetOrCreateSharedFunctionInfo(
      Isolate* isolate, DirectHandle<FunctionTemplateInfo> info,
      MaybeDirectHandle<Name> maybe_name);

  static Handle<SharedFunctionInfo> GetOrCreateSharedFunctionInfo(
      LocalIsolate* isolate, DirectHandle<FunctionTemplateInfo> info,
      DirectHandle<Name> maybe_name) {
    // We don't support streaming compilation of scripts with natives, so we
    // don't need an off-thread implementation of this.
    UNREACHABLE();
  }

  // Returns parent function template or a null FunctionTemplateInfo.
  inline Tagged<FunctionTemplateInfo> GetParent(Isolate* isolate);
  // Returns true if |object| is an instance of this function template.
  inline bool IsTemplateFor(Tagged<JSObject> object) const;
  bool IsTemplateFor(Tagged<Map> map) const;
  // Returns true if |object| is an API object and is constructed by this
  // particular function template (skips walking up the chain of inheriting
  // functions that is done by IsTemplateFor).
  bool IsLeafTemplateForApiObject(Tagged<Object> object) const;
  inline bool instantiated();

  bool BreakAtEntry(Isolate* isolate);
  bool HasInstanceType();

  // Helper function for cached accessors.
  static std::optional<Tagged<Name>> TryGetCachedPropertyName(
      Isolate* isolate, Tagged<Object> getter);
  // Fast API overloads.
  int GetCFunctionsCount() const;
  Address GetCFunction(Isolate* isolate, int index) const;
  const CFunctionInfo* GetCSignature(Isolate* isolate, int index) const;

  // CFunction data for a set of overloads is stored into a FixedArray, as
  // [address_0, signature_0, ... address_n-1, signature_n-1].
  static const int kFunctionOverloadEntrySize = 2;

  // Bit position in the flag, from least significant bit position.
  DEFINE_TORQUE_GENERATED_FUNCTION_TEMPLATE_INFO_FLAGS()

  // This is a wrapper around |maybe_redirected_callback| accessor which
  // returns/accepts C function and converts the value from and to redirected
  // pointer.
  DECL_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST(callback, Address)
  inline void init_callback_redirection(i::IsolateForSandbox isolate);
  inline void remove_callback_redirection(i::IsolateForSandbox isolate);

  template <class IsolateT>
  inline bool has_callback(IsolateT* isolate) const;

  DECL_PRINTER(FunctionTemplateInfo)

  class BodyDescriptor;

 private:
  // When simulator is enabled the field stores the "redirected" address of the
  // C function (the one that's callabled from simulated compiled code), in
  // this case the original address of the C function has to be taken from the
  // redirection.
  // For native builds the field contains the address of the C function.
  // This field is initialized implicitly via respective |callback|-related
  // methods.
  DECL_EXTERNAL_POINTER_ACCESSORS_MAYBE_READ_ONLY_HOST(
      maybe_redirected_callback, Address)

  // For ease of use of the BITFIELD macro.
  inline int32_t relaxed_flag() const;
  inline void set_relaxed_flag(int32_t flags);

  // Enforce using SetInstanceType() and SetAllowedReceiverInstanceTypeRange()
  // instead of raw accessors.
  using TorqueGeneratedFunctionTemplateInfo<FunctionTemplateInfo,
                                            TemplateInfo>::set_instance_type;
  DECL_PRIMITIVE_SETTER(allowed_receiver_instance_type_range_start,
                        InstanceType)
  DECL_PRIMITIVE_SETTER(allowed_receiver_instance_type_range_end, InstanceType)

  static constexpr int kNoJSApiObjectType = 0;
  static inline Tagged<FunctionTemplateRareData> EnsureFunctionTemplateRareData(
      Isolate* isolate,
      DirectHandle<FunctionTemplateInfo> function_template_info);

  static Tagged<FunctionTemplateRareData> AllocateFunctionTemplateRareData(
      Isolate* isolate,
      DirectHandle<FunctionTemplateInfo> function_template_info);

  TQ_OBJECT_CONSTRUCTORS(FunctionTemplateInfo)
};

class ObjectTemplateInfo
    : public TorqueGeneratedObjectTemplateInfo<ObjectTemplateInfo,
                                               TemplateInfo> {
 public:
  NEVER_READ_ONLY_SPACE

  DECL_INT_ACCESSORS(embedder_field_count)
  DECL_BOOLEAN_ACCESSORS(immutable_proto)
  DECL_BOOLEAN_ACCESSORS(code_like)

  // Starting from given object template's constructor walk up the inheritance
  // chain till a function template that has an instance template is found.
  inline Tagged<ObjectTemplateInfo> GetParent(Isolate* isolate);

  using BodyDescriptor = StructBodyDescriptor;

 private:
  DEFINE_TORQUE_GENERATED_OBJECT_TEMPLATE_INFO_FLAGS()

  TQ_OBJECT_CONSTRUCTORS(ObjectTemplateInfo)
};

class DictionaryTemplateInfo
    : public TorqueGeneratedDictionaryTemplateInfo<DictionaryTemplateInfo,
                                                   HeapObject> {
 public:
  class BodyDescriptor;

  static Handle<DictionaryTemplateInfo> Create(
      Isolate* isolate, const v8::MemorySpan<const std::string_view>& names);

  static Handle<JSObject> NewInstance(
      DirectHandle<NativeContext> context,
      DirectHandle<DictionaryTemplateInfo> self,
      const MemorySpan<MaybeLocal<Value>>& property_values);

  NEVER_READ_ONLY_SPACE

  TQ_OBJECT_CONSTRUCTORS(DictionaryTemplateInfo)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TEMPLATES_H_
