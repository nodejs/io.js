// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_LOOKUP_H_
#define V8_OBJECTS_LOOKUP_H_

#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/js-objects.h"
#include "src/objects/map.h"
#include "src/objects/objects.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/value-type.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

class PropertyKey {
 public:
  inline PropertyKey(Isolate* isolate, double index);
  // {name} might be a string representation of an element index.
  inline PropertyKey(Isolate* isolate, Handle<Name> name);
  // {valid_key} is a Name or Number.
  inline PropertyKey(Isolate* isolate, Handle<Object> valid_key);
  // {key} could be anything.
  PropertyKey(Isolate* isolate, Handle<Object> key, bool* success);

  inline bool is_element() const;
  Handle<Name> name() const { return name_; }
  size_t index() const { return index_; }
  inline Handle<Name> GetName(Isolate* isolate);

 private:
  friend LookupIterator;

  // Shortcut for constructing PropertyKey from an active LookupIterator.
  inline PropertyKey(Isolate* isolate, Handle<Name> name, size_t index);

  Handle<Name> name_;
  size_t index_;
};

class V8_EXPORT_PRIVATE LookupIterator final {
 public:
  enum Configuration {
    // Configuration bits.
    kInterceptor = 1 << 0,
    kPrototypeChain = 1 << 1,

    // Convenience combinations of bits.
    OWN_SKIP_INTERCEPTOR = 0,
    OWN = kInterceptor,
    PROTOTYPE_CHAIN_SKIP_INTERCEPTOR = kPrototypeChain,
    PROTOTYPE_CHAIN = kPrototypeChain | kInterceptor,
    DEFAULT = PROTOTYPE_CHAIN
  };

  enum State {
    // The property was not found by the iterator (this is a terminal state,
    // iteration should not continue after hitting a not-found state).
    NOT_FOUND,
    // Typed arrays have special handling for "canonical numeric index string"
    // (https://tc39.es/ecma262/#sec-canonicalnumericindexstring), where not
    // finding such an index (either because of OOB, or because it's not a valid
    // integer index) immediately returns undefined, without walking the
    // prototype (https://tc39.es/ecma262/#sec-typedarray-get).
    TYPED_ARRAY_INDEX_NOT_FOUND,
    // The next lookup requires an access check -- we can continue iteration on
    // a successful check, otherwise we should abort.
    ACCESS_CHECK,
    // Interceptors are API-level hooks for optionally handling a lookup in
    // embedder code -- if their handling returns false, then we should continue
    // the iteration, though we should be conscious that an interceptor may have
    // side effects despite returning false and might invalidate the lookup
    // iterator state.
    INTERCEPTOR,
    // Proxies are user-space hooks for handling lookups in JS code.
    // https://tc39.es/ecma262/#proxy-exotic-object
    JSPROXY,
    // Accessors are hooks for property getters/setters -- these can be
    // user-space accessors (AccessorPair), or API accessors (AccessorInfo).
    ACCESSOR,
    // Data properties are stored as data fields on an object (either properties
    // or elements).
    DATA,
    // WasmGC objects are opaque in JS, and appear to have no properties.
    WASM_OBJECT,

    // A LookupIterator in the transition state is in the middle of performing
    // a data transition (that is, as part of a data property write, updating
    // the receiver and its map to allow the write).
    //
    // This state is not expected to be observed while performing a lookup.
    TRANSITION,

    // Set state_ to BEFORE_PROPERTY to ensure that the next lookup will be a
    // PROPERTY lookup.
    BEFORE_PROPERTY = INTERCEPTOR
  };

  // {name} is guaranteed to be a property name (and not e.g. "123").
  inline LookupIterator(Isolate* isolate, Handle<Object> receiver,
                        Handle<Name> name,
                        Configuration configuration = DEFAULT);
  inline LookupIterator(Isolate* isolate, Handle<Object> receiver,
                        Handle<Name> name, Handle<Object> lookup_start_object,
                        Configuration configuration = DEFAULT);

  inline LookupIterator(Isolate* isolate, Handle<Object> receiver, size_t index,
                        Configuration configuration = DEFAULT);
  inline LookupIterator(Isolate* isolate, Handle<Object> receiver, size_t index,
                        Handle<Object> lookup_start_object,
                        Configuration configuration = DEFAULT);

  inline LookupIterator(Isolate* isolate, Handle<Object> receiver,
                        const PropertyKey& key,
                        Configuration configuration = DEFAULT);
  inline LookupIterator(Isolate* isolate, Handle<Object> receiver,
                        const PropertyKey& key,
                        Handle<Object> lookup_start_object,
                        Configuration configuration = DEFAULT);

  // Special case for lookup of the |error_stack_trace| private symbol in
  // prototype chain (usually private symbols are limited to
  // OWN_SKIP_INTERCEPTOR lookups).
  inline LookupIterator(Isolate* isolate, Configuration configuration,
                        Handle<Object> receiver, Handle<Symbol> name);

  void Restart() {
    InterceptorState state = InterceptorState::kUninitialized;
    IsElement() ? RestartInternal<true>(state) : RestartInternal<false>(state);
  }

  Isolate* isolate() const { return isolate_; }
  State state() const { return state_; }

  inline Handle<Name> name() const;
  inline Handle<Name> GetName();
  size_t index() const { return index_; }
  uint32_t array_index() const {
    DCHECK_LE(index_, JSArray::kMaxArrayIndex);
    return static_cast<uint32_t>(index_);
  }

  // Helper method for creating a copy of of the iterator.
  inline PropertyKey GetKey() const;

  // Returns true if this LookupIterator has an index in the range
  // [0, size_t::max).
  bool IsElement() const { return index_ != kInvalidIndex; }
  // Returns true if this LookupIterator has an index that counts as an
  // element for the given object (up to kMaxArrayIndex for JSArrays,
  // any integer for JSTypedArrays).
  inline bool IsElement(Tagged<JSReceiver> object) const;

  inline bool IsPrivateName() const;

  bool IsFound() const { return state_ != NOT_FOUND; }
  void Next();
  void NotFound() {
    has_property_ = false;
    state_ = NOT_FOUND;
  }

  Heap* heap() const { return isolate_->heap(); }
  Factory* factory() const { return isolate_->factory(); }
  Handle<Object> GetReceiver() const { return receiver_; }

  template <class T>
  inline Handle<T> GetStoreTarget() const;
  inline bool is_dictionary_holder() const;
  inline Handle<Map> transition_map() const;
  inline Handle<PropertyCell> transition_cell() const;
  template <class T>
  inline Handle<T> GetHolder() const;

  Handle<Object> lookup_start_object() const { return lookup_start_object_; }

  bool HolderIsReceiver() const;
  bool HolderIsReceiverOrHiddenPrototype() const;

  bool check_prototype_chain() const {
    return (configuration_ & kPrototypeChain) != 0;
  }

  /* ACCESS_CHECK */
  bool HasAccess() const;

  /* PROPERTY */
  inline bool ExtendingNonExtensible(Handle<JSReceiver> receiver);
  void PrepareForDataProperty(Handle<Object> value);
  void PrepareTransitionToDataProperty(Handle<JSReceiver> receiver,
                                       Handle<Object> value,
                                       PropertyAttributes attributes,
                                       StoreOrigin store_origin);
  inline bool IsCacheableTransition();
  void ApplyTransitionToDataProperty(Handle<JSReceiver> receiver);
  void ReconfigureDataProperty(Handle<Object> value,
                               PropertyAttributes attributes);
  void Delete();
  void TransitionToAccessorProperty(Handle<Object> getter,
                                    Handle<Object> setter,
                                    PropertyAttributes attributes);
  void TransitionToAccessorPair(Handle<Object> pair,
                                PropertyAttributes attributes);
  PropertyDetails property_details() const {
    DCHECK(has_property_);
    return property_details_;
  }
  PropertyAttributes property_attributes() const {
    return property_details().attributes();
  }
  bool IsConfigurable() const { return property_details().IsConfigurable(); }
  bool IsReadOnly() const { return property_details().IsReadOnly(); }
  bool IsEnumerable() const { return property_details().IsEnumerable(); }
  Representation representation() const {
    return property_details().representation();
  }
  PropertyLocation location() const { return property_details().location(); }
  PropertyConstness constness() const { return property_details().constness(); }
  FieldIndex GetFieldIndex() const;
  int GetFieldDescriptorIndex() const;
  int GetAccessorIndex() const;
  Handle<PropertyCell> GetPropertyCell() const;
  Handle<Object> GetAccessors() const;
  inline Handle<InterceptorInfo> GetInterceptor() const;
  Handle<InterceptorInfo> GetInterceptorForFailedAccessCheck() const;
  Handle<Object> GetDataValue(AllocationPolicy allocation_policy =
                                  AllocationPolicy::kAllocationAllowed) const;
  void WriteDataValue(Handle<Object> value, bool initializing_store);
  Handle<Object> GetDataValue(SeqCstAccessTag tag) const;
  void WriteDataValue(Handle<Object> value, SeqCstAccessTag tag);
  Handle<Object> SwapDataValue(Handle<Object> value, SeqCstAccessTag tag);
  Handle<Object> CompareAndSwapDataValue(Handle<Object> expected,
                                         Handle<Object> value,
                                         SeqCstAccessTag tag);
  inline void UpdateProtector();
  static inline void UpdateProtector(Isolate* isolate, Handle<Object> receiver,
                                     Handle<Name> name);

  // Lookup a 'cached' private property for an accessor.
  // If not found returns false and leaves the LookupIterator unmodified.
  bool TryLookupCachedProperty(Handle<AccessorPair> accessor);
  bool TryLookupCachedProperty();

  // Test whether the object has an internal marker property.
  static bool HasInternalMarkerProperty(Isolate* isolate,
                                        Tagged<JSReceiver> object,
                                        Handle<Symbol> marker);

 private:
  friend PropertyKey;

  static const size_t kInvalidIndex = std::numeric_limits<size_t>::max();

  bool LookupCachedProperty(Handle<AccessorPair> accessor);
  inline LookupIterator(Isolate* isolate, Handle<Object> receiver,
                        Handle<Name> name, size_t index,
                        Handle<Object> lookup_start_object,
                        Configuration configuration);

  // Lookup private symbol on the prototype chain. Currently used only for
  // error_stack_symbol.
  inline LookupIterator(Isolate* isolate, Configuration configuration,
                        Handle<Object> receiver, Handle<Symbol> name,
                        Handle<Object> lookup_start_object);

  static void InternalUpdateProtector(Isolate* isolate, Handle<Object> receiver,
                                      Handle<Name> name);

  enum class InterceptorState {
    kUninitialized,
    kSkipNonMasking,
    kProcessNonMasking
  };

  Handle<Map> GetReceiverMap() const;

  V8_WARN_UNUSED_RESULT inline Tagged<JSReceiver> NextHolder(Tagged<Map> map);

  bool is_js_array_element(bool is_element) const {
    return is_element && index_ <= JSArray::kMaxArrayIndex;
  }
  template <bool is_element>
  V8_EXPORT_PRIVATE void Start();
  template <bool is_element>
  void NextInternal(Tagged<Map> map, Tagged<JSReceiver> holder);
  template <bool is_element>
  inline State LookupInHolder(Tagged<Map> map, Tagged<JSReceiver> holder) {
    return IsSpecialReceiverMap(map)
               ? LookupInSpecialHolder<is_element>(map, holder)
               : LookupInRegularHolder<is_element>(map, holder);
  }
  template <bool is_element>
  State LookupInRegularHolder(Tagged<Map> map, Tagged<JSReceiver> holder);
  template <bool is_element>
  State LookupInSpecialHolder(Tagged<Map> map, Tagged<JSReceiver> holder);
  template <bool is_element>
  void RestartLookupForNonMaskingInterceptors() {
    RestartInternal<is_element>(InterceptorState::kProcessNonMasking);
  }
  template <bool is_element>
  void RestartInternal(InterceptorState interceptor_state);
  Handle<Object> FetchValue(AllocationPolicy allocation_policy =
                                AllocationPolicy::kAllocationAllowed) const;
  bool CanStayConst(Tagged<Object> value) const;
  bool DictCanStayConst(Tagged<Object> value) const;

  Handle<Object> CompareAndSwapInternal(Handle<Object> desired,
                                        Handle<Object> value,
                                        SeqCstAccessTag tag, bool& success);

  template <bool is_element>
  void ReloadPropertyInformation();

  template <bool is_element>
  bool SkipInterceptor(Tagged<JSObject> holder);
  template <bool is_element>
  inline Tagged<InterceptorInfo> GetInterceptor(Tagged<JSObject> holder) const;

  bool check_interceptor() const {
    return (configuration_ & kInterceptor) != 0;
  }
  inline InternalIndex descriptor_number() const;
  inline InternalIndex dictionary_entry() const;

  static inline Configuration ComputeConfiguration(Isolate* isolate,
                                                   Configuration configuration,
                                                   Handle<Name> name);

  static MaybeHandle<JSReceiver> GetRootForNonJSReceiver(
      Isolate* isolate, Handle<Object> lookup_start_object, size_t index,
      Configuration configuration);
  static inline MaybeHandle<JSReceiver> GetRoot(
      Isolate* isolate, Handle<Object> lookup_start_object, size_t index,
      Configuration configuration);

  State NotFound(Tagged<JSReceiver> const holder) const;

  // If configuration_ becomes mutable, update
  // HolderIsReceiverOrHiddenPrototype.
  const Configuration configuration_;
  State state_ = NOT_FOUND;
  bool has_property_ = false;
  InterceptorState interceptor_state_ = InterceptorState::kUninitialized;
  PropertyDetails property_details_ = PropertyDetails::Empty();
  Isolate* const isolate_;
  Handle<Name> name_;
  Handle<Object> transition_;
  const Handle<Object> receiver_;
  Handle<JSReceiver> holder_;
  const Handle<Object> lookup_start_object_;
  const size_t index_;
  InternalIndex number_ = InternalIndex::NotFound();
};

// Similar to the LookupIterator, but for concurrent accesses from a background
// thread.
//
// Note: This is a work in progress, intended to bundle code related to
// concurrent lookups here. In its current state, the class is obviously not an
// 'iterator'. Still, keeping the name for now, with the intent to clarify
// names and implementation once we've gotten some experience with more
// involved logic.
// TODO(jgruber, v8:7790): Consider using a LookupIterator-style interface.
// TODO(jgruber, v8:7790): Consider merging back into the LookupIterator once
// functionality and constraints are better known.
class ConcurrentLookupIterator final : public AllStatic {
 public:
  // Tri-state to distinguish between 'not-present' and 'who-knows' failures.
  enum Result {
    kPresent,     // The value was found.
    kNotPresent,  // No value exists.
    kGaveUp,      // The operation can't be completed.
  };

  // Implements the own data property lookup for the specialized case of
  // fixed_cow_array backing stores (these are only in use for array literal
  // boilerplates). The contract is that the elements, elements kind, and array
  // length passed to this function should all be read from the same JSArray
  // instance; but due to concurrency it's possible that they may not be
  // consistent among themselves (e.g. the elements kind may not match the
  // given elements backing store). We are thus extra-careful to handle
  // exceptional situations.
  V8_EXPORT_PRIVATE static base::Optional<Tagged<Object>> TryGetOwnCowElement(
      Isolate* isolate, Tagged<FixedArray> array_elements,
      ElementsKind elements_kind, int array_length, size_t index);

  // As above, the contract is that the elements and elements kind should be
  // read from the same holder, but this function is implemented defensively to
  // tolerate concurrency issues.
  V8_EXPORT_PRIVATE static Result TryGetOwnConstantElement(
      Tagged<Object>* result_out, Isolate* isolate, LocalIsolate* local_isolate,
      Tagged<JSObject> holder, Tagged<FixedArrayBase> elements,
      ElementsKind elements_kind, size_t index);

  // Implements the own data property lookup for the specialized case of
  // strings.
  V8_EXPORT_PRIVATE static Result TryGetOwnChar(Tagged<String>* result_out,
                                                Isolate* isolate,
                                                LocalIsolate* local_isolate,
                                                Tagged<String> string,
                                                size_t index);

  // This method reimplements the following sequence in a concurrent setting:
  //
  // LookupIterator it(holder, isolate, name, LookupIterator::OWN);
  // it.TryLookupCachedProperty();
  // if (it.state() == LookupIterator::DATA) it.GetPropertyCell();
  V8_EXPORT_PRIVATE static base::Optional<Tagged<PropertyCell>>
  TryGetPropertyCell(Isolate* isolate, LocalIsolate* local_isolate,
                     Handle<JSGlobalObject> holder, Handle<Name> name);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_LOOKUP_H_
