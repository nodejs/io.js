// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ARRAY_H_
#define V8_OBJECTS_JS_ARRAY_H_

#include "src/objects/allocation-site.h"
#include "src/objects/fixed-array.h"
#include "src/objects/js-objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-array-tq.inc"

// The JSArray describes JavaScript Arrays
//  Such an array can be in one of two modes:
//    - fast, backing storage is a FixedArray and length <= elements.length();
//       Please note: push and pop can be used to grow and shrink the array.
//    - slow, backing storage is a HashTable with numbers as keys.
class JSArray : public TorqueGeneratedJSArray<JSArray, JSObject> {
 public:
  // [length]: The length property.
  DECL_ACCESSORS(length, Tagged<Number>)
  DECL_RELAXED_GETTER(length, Tagged<Number>)

  // Acquire/release semantics on this field are explicitly forbidden to avoid
  // confusion, since the default setter uses relaxed semantics. If
  // acquire/release semantics ever become necessary, the default setter should
  // be reverted to non-atomic behavior, and setters with explicit tags
  // introduced and used when required.
  Tagged<Number> length(PtrComprCageBase cage_base,
                        AcquireLoadTag tag) const = delete;
  void set_length(Tagged<Number> value, ReleaseStoreTag tag,
                  WriteBarrierMode mode = UPDATE_WRITE_BARRIER) = delete;

  // Overload the length setter to skip write barrier when the length
  // is set to a smi. This matches the set function on FixedArray.
  inline void set_length(Tagged<Smi> length);

  static bool MayHaveReadOnlyLength(Tagged<Map> js_array_map);
  static bool HasReadOnlyLength(Handle<JSArray> array);
  static bool WouldChangeReadOnlyLength(Handle<JSArray> array, uint32_t index);

  // Initialize the array with the given capacity. The function may
  // fail due to out-of-memory situations, but only if the requested
  // capacity is non-zero.
  V8_EXPORT_PRIVATE static void Initialize(DirectHandle<JSArray> array,
                                           int capacity, int length = 0);

  // If the JSArray has fast elements, and new_length would result in
  // normalization, returns true.
  bool SetLengthWouldNormalize(uint32_t new_length);
  static inline bool SetLengthWouldNormalize(Heap* heap, uint32_t new_length);

  // Initializes the array to a certain length.
  V8_EXPORT_PRIVATE static Maybe<bool> SetLength(Handle<JSArray> array,
                                                 uint32_t length);

  // Set the content of the array to the content of storage.
  static inline void SetContent(Handle<JSArray> array,
                                Handle<FixedArrayBase> storage);

  // ES6 9.4.2.1
  V8_WARN_UNUSED_RESULT static Maybe<bool> DefineOwnProperty(
      Isolate* isolate, Handle<JSArray> o, Handle<Object> name,
      PropertyDescriptor* desc, Maybe<ShouldThrow> should_throw);

  static bool AnythingToArrayLength(Isolate* isolate,
                                    Handle<Object> length_object,
                                    uint32_t* output);
  V8_WARN_UNUSED_RESULT static Maybe<bool> ArraySetLength(
      Isolate* isolate, Handle<JSArray> a, PropertyDescriptor* desc,
      Maybe<ShouldThrow> should_throw);

  // Support for Array.prototype.join().
  // Writes a fixed array of strings and separators to a single destination
  // string. This helpers assumes the fixed array encodes separators in two
  // ways:
  //   1) Explicitly with a smi, whos value represents the number of repeated
  //      separators.
  //   2) Implicitly between two consecutive strings a single separator.
  //
  // In addition repeated strings are represented by a negative smi, indicating
  // how many times the previously written string has to be repeated.
  //
  // Here are some input/output examples given the separator string is ',':
  //
  //   [1, 'hello', 2, 'world', 1] => ',hello,,world,'
  //   ['hello', 'world']          => 'hello,world'
  //   ['hello', -2, 'world']      => 'hello,hello,hello,world'
  //
  // To avoid any allocations, this helper assumes the destination string is the
  // exact length necessary to write the strings and separators from the fixed
  // array.
  // Since this is called via ExternalReferences, it uses raw Address values:
  // - {raw_fixed_array} is a tagged FixedArray pointer.
  // - {raw_separator} and {raw_dest} are tagged String pointers.
  // - Returns a tagged String pointer.
  static Address ArrayJoinConcatToSequentialString(Isolate* isolate,
                                                   Address raw_fixed_array,
                                                   intptr_t length,
                                                   Address raw_separator,
                                                   Address raw_dest);

  // Checks whether the Array has the current realm's Array.prototype as its
  // prototype. This function is best-effort and only gives a conservative
  // approximation, erring on the side of false, in particular with respect
  // to Proxies and objects with a hidden prototype.
  inline bool HasArrayPrototype(Isolate* isolate);

  // Dispatched behavior.
  DECL_PRINTER(JSArray)
  DECL_VERIFIER(JSArray)

  // Number of element slots to pre-allocate for an empty array.
  static const int kPreallocatedArrayElements = 4;

  static const int kLengthDescriptorIndex = 0;

  // Max. number of elements being copied in Array builtins.
  static const int kMaxCopyElements = 100;

  // Valid array indices range from +0 <= i < 2^32 - 1 (kMaxUInt32).
  static constexpr uint32_t kMaxArrayLength = JSObject::kMaxElementCount;
  static constexpr uint32_t kMaxArrayIndex = JSObject::kMaxElementIndex;
  static_assert(kMaxArrayLength == kMaxUInt32);
  static_assert(kMaxArrayIndex == kMaxUInt32 - 1);

  // This constant is somewhat arbitrary. Any large enough value would work.
  static constexpr uint32_t kMaxFastArrayLength = 32 * 1024 * 1024;
  static_assert(kMaxFastArrayLength <= kMaxArrayLength);

  // Min. stack size for detecting an Array.prototype.join() call cycle.
  static const uint32_t kMinJoinStackSize = 2;

  static const int kInitialMaxFastElementArray =
      (kMaxRegularHeapObjectSize - FixedArray::kHeaderSize - kHeaderSize -
       AllocationMemento::kSize) >>
      kDoubleSizeLog2;

  TQ_OBJECT_CONSTRUCTORS(JSArray)
};

// The JSArrayIterator describes JavaScript Array Iterators Objects, as
// defined in ES section #sec-array-iterator-objects.
class JSArrayIterator
    : public TorqueGeneratedJSArrayIterator<JSArrayIterator, JSObject> {
 public:
  DECL_PRINTER(JSArrayIterator)
  DECL_VERIFIER(JSArrayIterator)

  // [kind]: the [[ArrayIterationKind]] inobject property.
  inline IterationKind kind() const;
  inline void set_kind(IterationKind kind);

 private:
  DECL_INT_ACCESSORS(raw_kind)

  TQ_OBJECT_CONSTRUCTORS(JSArrayIterator)
};

// Helper class for JSArrays that are template literal objects
class TemplateLiteralObject
    : public TorqueGeneratedTemplateLiteralObject<TemplateLiteralObject,
                                                  JSArray> {
 public:
 private:
  TQ_OBJECT_CONSTRUCTORS(TemplateLiteralObject)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ARRAY_H_
