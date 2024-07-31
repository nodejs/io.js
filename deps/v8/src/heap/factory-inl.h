// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FACTORY_INL_H_
#define V8_HEAP_FACTORY_INL_H_

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

Handle<String> Factory::InternalizeString(Handle<String> string) {
  if (IsInternalizedString(*string)) return string;
  return isolate()->string_table()->LookupString(isolate(), string);
}

Handle<Name> Factory::InternalizeName(Handle<Name> name) {
  if (IsUniqueName(*name)) return name;
  return isolate()->string_table()->LookupString(isolate(),
                                                 Handle<String>::cast(name));
}

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
    AllocationType allocation, DirectHandle<AllocationSite> allocation_site) {
  return map->is_dictionary_map()
             ? NewSlowJSObjectFromMap(map, number_of_slow_properties,
                                      allocation, allocation_site)
             : NewJSObjectFromMap(map, allocation, allocation_site);
}

Handle<JSObject> Factory::NewFastOrSlowJSObjectFromMap(DirectHandle<Map> map) {
  return NewFastOrSlowJSObjectFromMap(map,
                                      PropertyDictionary::kInitialCapacity);
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
      Handle<FixedArray> new_cache =
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
  if (key == number || (IsHeapNumber(key) && IsHeapNumber(number) &&
                        Object::Number(key) == Object::Number(number))) {
    return Handle<String>(String::cast(cache->get(hash * 2 + 1)), isolate());
  }
  return undefined_value();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FACTORY_INL_H_
