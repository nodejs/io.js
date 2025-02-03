// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FACTORY_INL_H_
#define V8_HEAP_FACTORY_INL_H_

#include "src/common/globals.h"
#include "src/heap/factory.h"

// Clients of this interface shouldn't depend on lots of heap internals.
// Do not include anything from src/heap here!
// TODO(all): Remove the heap-inl.h include below.
#include "src/execution/isolate-inl.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory-base-inl.h"
#include "src/heap/heap-inl.h"  // For MaxNumberToStringCacheSize.
#include "src/objects/feedback-cell.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/string-inl.h"
#include "src/objects/string-table-inl.h"
#include "src/strings/string-hasher.h"

namespace v8 {
namespace internal {

#define ROOT_ACCESSOR(Type, name, CamelName)                                 \
  Handle<Type> Factory::name() {                                             \
    return Handle<Type>(&isolate()->roots_table()[RootIndex::k##CamelName]); \
  }
MUTABLE_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

template <typename T, typename>
Handle<String> Factory::InternalizeString(Handle<T> string) {
  // T should be a subtype of String, which is enforced by the second template
  // argument.
  if (IsInternalizedString(*string)) return string;
  return indirect_handle(
      isolate()->string_table()->LookupString(isolate(), string), isolate());
}

template <typename T, typename>
Handle<Name> Factory::InternalizeName(Handle<T> name) {
  // T should be a subtype of Name, which is enforced by the second template
  // argument.
  if (IsUniqueName(*name)) return name;
  return indirect_handle(
      isolate()->string_table()->LookupString(isolate(), Cast<String>(name)),
      isolate());
}

#ifdef V8_ENABLE_DIRECT_HANDLE
template <typename T, typename>
DirectHandle<String> Factory::InternalizeString(DirectHandle<T> string) {
  // T should be a subtype of String, which is enforced by the second template
  // argument.
  if (IsInternalizedString(*string)) return string;
  return isolate()->string_table()->LookupString(isolate(), string);
}

template <typename T, typename>
DirectHandle<Name> Factory::InternalizeName(DirectHandle<T> name) {
  // T should be a subtype of Name, which is enforced by the second template
  // argument.
  if (IsUniqueName(*name)) return name;
  return isolate()->string_table()->LookupString(isolate(), Cast<String>(name));
}
#endif

template <size_t N>
Handle<String> Factory::NewStringFromStaticChars(const char (&str)[N],
                                                 AllocationType allocation) {
  DCHECK_EQ(N, strlen(str) + 1);
  return NewStringFromOneByte(base::StaticOneByteVector(str), allocation)
      .ToHandleChecked();
}

Handle<String> Factory::NewStringFromAsciiChecked(const char* str,
                                                  AllocationType allocation) {
  return NewStringFromOneByte(base::OneByteVector(str), allocation)
      .ToHandleChecked();
}

Handle<String> Factory::NewSubString(Handle<String> str, int begin, int end) {
  if (begin == 0 && end == str->length()) return str;
  return NewProperSubString(str, begin, end);
}

Handle<JSArray> Factory::NewJSArrayWithElements(
    DirectHandle<FixedArrayBase> elements, ElementsKind elements_kind,
    AllocationType allocation) {
  return NewJSArrayWithElements(elements, elements_kind, elements->length(),
                                allocation);
}

Handle<JSObject> Factory::NewFastOrSlowJSObjectFromMap(
    DirectHandle<Map> map, int number_of_slow_properties,
    AllocationType allocation, DirectHandle<AllocationSite> allocation_site,
    NewJSObjectType new_js_object_type) {
  auto js_object =
      map->is_dictionary_map()
          ? NewSlowJSObjectFromMap(map, number_of_slow_properties, allocation,
                                   allocation_site, new_js_object_type)
          : NewJSObjectFromMap(map, allocation, allocation_site,
                               new_js_object_type);
  return js_object;
}

Handle<JSObject> Factory::NewFastOrSlowJSObjectFromMap(DirectHandle<Map> map) {
  return NewFastOrSlowJSObjectFromMap(map,
                                      PropertyDictionary::kInitialCapacity);
}

template <ExternalPointerTag tag>
Handle<Foreign> Factory::NewForeign(Address addr,
                                    AllocationType allocation_type) {
  // Statically ensure that it is safe to allocate foreigns in paged spaces.
  static_assert(Foreign::kSize <= kMaxRegularHeapObjectSize);
  Tagged<Map> map = *foreign_map();
  Tagged<Foreign> foreign = Cast<Foreign>(
      AllocateRawWithImmortalMap(map->instance_size(), allocation_type, map));
  DisallowGarbageCollection no_gc;
  foreign->init_foreign_address<tag>(isolate(), addr);
  return handle(foreign, isolate());
}

Handle<Object> Factory::NewURIError() {
  return NewError(isolate()->uri_error_function(),
                  MessageTemplate::kURIMalformed);
}

ReadOnlyRoots Factory::read_only_roots() const {
  return ReadOnlyRoots(isolate());
}

HeapAllocator* Factory::allocator() const {
  return isolate()->heap()->allocator();
}

Factory::CodeBuilder& Factory::CodeBuilder::set_empty_source_position_table() {
  return set_source_position_table(
      isolate_->factory()->empty_trusted_byte_array());
}

Factory::CodeBuilder& Factory::CodeBuilder::set_interpreter_data(
    Handle<TrustedObject> interpreter_data) {
  // This DCHECK requires this function to be in -inl.h.
  DCHECK(IsInterpreterData(*interpreter_data) ||
         IsBytecodeArray(*interpreter_data));
  interpreter_data_ = interpreter_data;
  return *this;
}

void Factory::NumberToStringCacheSet(DirectHandle<Object> number, int hash,
                                     DirectHandle<String> js_string) {
  if (!IsUndefined(number_string_cache()->get(hash * 2), isolate()) &&
      !v8_flags.optimize_for_size) {
    int full_size = isolate()->heap()->MaxNumberToStringCacheSize();
    if (number_string_cache()->length() != full_size) {
      DirectHandle<FixedArray> new_cache =
          NewFixedArray(full_size, AllocationType::kOld);
      isolate()->heap()->set_number_string_cache(*new_cache);
      return;
    }
  }
  DisallowGarbageCollection no_gc;
  Tagged<FixedArray> cache = *number_string_cache();
  cache->set(hash * 2, *number);
  cache->set(hash * 2 + 1, *js_string);
}

Handle<Object> Factory::NumberToStringCacheGet(Tagged<Object> number,
                                               int hash) {
  DisallowGarbageCollection no_gc;
  Tagged<FixedArray> cache = *number_string_cache();
  Tagged<Object> key = cache->get(hash * 2);
  if (key == number ||
      (IsHeapNumber(key) && IsHeapNumber(number) &&
       Cast<HeapNumber>(key)->value() == Cast<HeapNumber>(number)->value())) {
    return Handle<String>(Cast<String>(cache->get(hash * 2 + 1)), isolate());
  }
  return undefined_value();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FACTORY_INL_H_
