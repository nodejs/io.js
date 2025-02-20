// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_KEYS_H_
#define V8_OBJECTS_KEYS_H_

#include "include/v8-object.h"
#include "src/objects/hash-table.h"
#include "src/objects/js-objects.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class AccessCheckInfo;
class FastKeyAccumulator;
class JSProxy;

enum AddKeyConversion { DO_NOT_CONVERT, CONVERT_TO_ARRAY_INDEX };

enum class GetKeysConversion {
  kKeepNumbers = static_cast<int>(v8::KeyConversionMode::kKeepNumbers),
  kConvertToString = static_cast<int>(v8::KeyConversionMode::kConvertToString),
  kNoNumbers = static_cast<int>(v8::KeyConversionMode::kNoNumbers)
};

enum class KeyCollectionMode {
  kOwnOnly = static_cast<int>(v8::KeyCollectionMode::kOwnOnly),
  kIncludePrototypes =
      static_cast<int>(v8::KeyCollectionMode::kIncludePrototypes)
};

// This is a helper class for JSReceiver::GetKeys which collects and sorts keys.
// GetKeys needs to sort keys per prototype level, first showing the integer
// indices from elements then the strings from the properties. However, this
// does not apply to proxies which are in full control of how the keys are
// sorted.
//
// For performance reasons the KeyAccumulator internally separates integer keys
// in |elements_| into sorted lists per prototype level. String keys are
// collected in |string_properties_|, a single OrderedHashSet (similar for
// Symbols in |symbol_properties_|. To separate the keys per level later when
// assembling the final list, |levelLengths_| keeps track of the number of
// String and Symbol keys per level.
//
// Only unique keys are kept by the KeyAccumulator, strings are stored in a
// HashSet for inexpensive lookups. Integer keys are kept in sorted lists which
// are more compact and allow for reasonably fast includes check.
class KeyAccumulator final {
 public:
  KeyAccumulator(Isolate* isolate, KeyCollectionMode mode,
                 PropertyFilter filter)
      : isolate_(isolate), mode_(mode), filter_(filter) {}
  ~KeyAccumulator() = default;
  KeyAccumulator(const KeyAccumulator&) = delete;
  KeyAccumulator& operator=(const KeyAccumulator&) = delete;

  static MaybeHandle<FixedArray> GetKeys(
      Isolate* isolate, DirectHandle<JSReceiver> object, KeyCollectionMode mode,
      PropertyFilter filter,
      GetKeysConversion keys_conversion = GetKeysConversion::kKeepNumbers,
      bool is_for_in = false, bool skip_indices = false);

  Handle<FixedArray> GetKeys(
      GetKeysConversion convert = GetKeysConversion::kKeepNumbers);
  Maybe<bool> CollectKeys(DirectHandle<JSReceiver> receiver,
                          DirectHandle<JSReceiver> object);

  // Might return directly the object's enum_cache, copy the result before using
  // as an elements backing store for a JSObject.
  // Does not throw for uninitialized exports in module namespace objects, so
  // this has to be checked separately.
  static Handle<FixedArray> GetOwnEnumPropertyKeys(
      Isolate* isolate, DirectHandle<JSObject> object);

  V8_WARN_UNUSED_RESULT ExceptionStatus
  AddKey(Tagged<Object> key, AddKeyConversion convert = DO_NOT_CONVERT);
  V8_WARN_UNUSED_RESULT ExceptionStatus
  AddKey(DirectHandle<Object> key, AddKeyConversion convert = DO_NOT_CONVERT);

  // Jump to the next level, pushing the current |levelLength_| to
  // |levelLengths_| and adding a new list to |elements_|.
  Isolate* isolate() { return isolate_; }
  // Filter keys based on their property descriptors.
  PropertyFilter filter() { return filter_; }
  // The collection mode defines whether we collect the keys from the prototype
  // chain or only look at the receiver.
  KeyCollectionMode mode() { return mode_; }
  void set_skip_indices(bool value) { skip_indices_ = value; }
  // Shadowing keys are used to filter keys. This happens when non-enumerable
  // keys appear again on the prototype chain.
  void AddShadowingKey(Tagged<Object> key, AllowGarbageCollection* allow_gc);
  void AddShadowingKey(DirectHandle<Object> key);

 private:
  enum IndexedOrNamed { kIndexed, kNamed };

  V8_WARN_UNUSED_RESULT ExceptionStatus CollectPrivateNames(
      DirectHandle<JSReceiver> receiver, DirectHandle<JSObject> object);
  Maybe<bool> CollectAccessCheckInterceptorKeys(
      DirectHandle<AccessCheckInfo> access_check_info,
      DirectHandle<JSReceiver> receiver, DirectHandle<JSObject> object);

  Maybe<bool> CollectInterceptorKeysInternal(
      DirectHandle<JSReceiver> receiver, DirectHandle<JSObject> object,
      DirectHandle<InterceptorInfo> interceptor, IndexedOrNamed type);
  Maybe<bool> CollectInterceptorKeys(DirectHandle<JSReceiver> receiver,
                                     DirectHandle<JSObject> object,
                                     IndexedOrNamed type);

  Maybe<bool> CollectOwnElementIndices(DirectHandle<JSReceiver> receiver,
                                       DirectHandle<JSObject> object);
  Maybe<bool> CollectOwnPropertyNames(DirectHandle<JSReceiver> receiver,
                                      DirectHandle<JSObject> object);
  Maybe<bool> CollectOwnKeys(DirectHandle<JSReceiver> receiver,
                             DirectHandle<JSObject> object);
  Maybe<bool> CollectOwnJSProxyKeys(DirectHandle<JSReceiver> receiver,
                                    DirectHandle<JSProxy> proxy);
  Maybe<bool> CollectOwnJSProxyTargetKeys(DirectHandle<JSProxy> proxy,
                                          DirectHandle<JSReceiver> target);

  V8_WARN_UNUSED_RESULT ExceptionStatus FilterForEnumerableProperties(
      DirectHandle<JSReceiver> receiver, DirectHandle<JSObject> object,
      DirectHandle<InterceptorInfo> interceptor, DirectHandle<JSObject> result,
      IndexedOrNamed type);

  Maybe<bool> AddKeysFromJSProxy(DirectHandle<JSProxy> proxy,
                                 DirectHandle<FixedArray> keys);
  V8_WARN_UNUSED_RESULT ExceptionStatus AddKeys(DirectHandle<FixedArray> array,
                                                AddKeyConversion convert);
  V8_WARN_UNUSED_RESULT ExceptionStatus
  AddKeys(DirectHandle<JSObject> array_like, AddKeyConversion convert);

  bool IsShadowed(DirectHandle<Object> key);
  bool HasShadowingKeys();
  Handle<OrderedHashSet> keys();

  // In case of for-in loops we have to treat JSProxy keys differently and
  // deduplicate them. Additionally we convert JSProxy keys back to array
  // indices.
  void set_is_for_in(bool value) { is_for_in_ = value; }
  void set_first_prototype_map(DirectHandle<Map> value) {
    first_prototype_map_ = value;
  }
  void set_try_prototype_info_cache(bool value) {
    try_prototype_info_cache_ = value;
  }
  void set_receiver(DirectHandle<JSReceiver> object) { receiver_ = object; }
  // The last_non_empty_prototype is used to limit the prototypes for which
  // we have to keep track of non-enumerable keys that can shadow keys
  // repeated on the prototype chain.
  void set_last_non_empty_prototype(DirectHandle<JSReceiver> object) {
    last_non_empty_prototype_ = object;
  }
  void set_may_have_elements(bool value) { may_have_elements_ = value; }

  Isolate* isolate_;
  Handle<OrderedHashSet> keys_;
  DirectHandle<Map> first_prototype_map_;
  DirectHandle<JSReceiver> receiver_;
  DirectHandle<JSReceiver> last_non_empty_prototype_;
  Handle<ObjectHashSet> shadowing_keys_;
  KeyCollectionMode mode_;
  PropertyFilter filter_;
  bool is_for_in_ = false;
  bool skip_indices_ = false;
  // For all the keys on the first receiver adding a shadowing key we can skip
  // the shadow check.
  bool skip_shadow_check_ = true;
  bool may_have_elements_ = true;
  bool try_prototype_info_cache_ = false;

  friend FastKeyAccumulator;
};

// The FastKeyAccumulator handles the cases where there are no elements on the
// prototype chain and forwards the complex/slow cases to the normal
// KeyAccumulator. This significantly speeds up the cases where the OWN_ONLY
// case where we do not have to walk the prototype chain.
class FastKeyAccumulator {
 public:
  FastKeyAccumulator(Isolate* isolate, DirectHandle<JSReceiver> receiver,
                     KeyCollectionMode mode, PropertyFilter filter,
                     bool is_for_in = false, bool skip_indices = false)
      : isolate_(isolate),
        receiver_(receiver),
        mode_(mode),
        filter_(filter),
        is_for_in_(is_for_in),
        skip_indices_(skip_indices) {
    Prepare();
  }
  FastKeyAccumulator(const FastKeyAccumulator&) = delete;
  FastKeyAccumulator& operator=(const FastKeyAccumulator&) = delete;

  bool is_receiver_simple_enum() { return is_receiver_simple_enum_; }
  bool has_empty_prototype() { return has_empty_prototype_; }
  bool may_have_elements() { return may_have_elements_; }

  MaybeHandle<FixedArray> GetKeys(
      GetKeysConversion convert = GetKeysConversion::kKeepNumbers);

  // Initialize the the enum cache for a map with all of the following:
  //   - uninitialized enum length
  //   - fast properties (i.e. !is_dictionary_map())
  //   - has >0 enumerable own properties
  //
  // The number of enumerable properties is passed in as an optimization, for
  // when the caller has already computed it.
  //
  // Returns the keys.
  static Handle<FixedArray> InitializeFastPropertyEnumCache(
      Isolate* isolate, DirectHandle<Map> map, int enum_length,
      AllocationType allocation = AllocationType::kOld);

 private:
  void Prepare();
  MaybeHandle<FixedArray> GetKeysFast(GetKeysConversion convert);
  MaybeHandle<FixedArray> GetKeysSlow(GetKeysConversion convert);
  MaybeHandle<FixedArray> GetKeysWithPrototypeInfoCache(
      GetKeysConversion convert);

  MaybeHandle<FixedArray> GetOwnKeysWithUninitializedEnumLength();

  bool MayHaveElements(Tagged<JSReceiver> receiver);
  bool TryPrototypeInfoCache(DirectHandle<JSReceiver> receiver);

  Isolate* isolate_;
  DirectHandle<JSReceiver> receiver_;
  DirectHandle<Map> first_prototype_map_;
  DirectHandle<JSReceiver> first_prototype_;
  DirectHandle<JSReceiver> last_non_empty_prototype_;
  KeyCollectionMode mode_;
  PropertyFilter filter_;
  bool is_for_in_ = false;
  bool skip_indices_ = false;
  bool is_receiver_simple_enum_ = false;
  bool has_empty_prototype_ = false;
  bool may_have_elements_ = true;
  bool has_prototype_info_cache_ = false;
  bool try_prototype_info_cache_ = false;
  bool only_own_has_simple_elements_ = false;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_KEYS_H_
