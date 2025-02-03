// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/extensions/statistics-extension.h"

#include "include/v8-template.h"
#include "src/common/assert-scope.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"  // crbug.com/v8/8499
#include "src/logging/counters.h"
#include "src/objects/tagged.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

const char* const StatisticsExtension::kSource =
    "native function getV8Statistics();";


v8::Local<v8::FunctionTemplate> StatisticsExtension::GetNativeFunctionTemplate(
    v8::Isolate* isolate, v8::Local<v8::String> str) {
  DCHECK_EQ(strcmp(*v8::String::Utf8Value(isolate, str), "getV8Statistics"), 0);
  return v8::FunctionTemplate::New(isolate, StatisticsExtension::GetCounters);
}


static void AddCounter(v8::Isolate* isolate,
                       v8::Local<v8::Object> object,
                       StatsCounter* counter,
                       const char* name) {
  if (counter->Enabled()) {
    object
        ->Set(isolate->GetCurrentContext(),
              v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
              v8::Number::New(isolate, *counter->GetInternalPointer()))
        .FromJust();
  }
}

static void AddNumber(v8::Isolate* isolate, v8::Local<v8::Object> object,
                      double value, const char* name) {
  object
      ->Set(isolate->GetCurrentContext(),
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            v8::Number::New(isolate, value))
      .FromJust();
}


static void AddNumber64(v8::Isolate* isolate,
                        v8::Local<v8::Object> object,
                        int64_t value,
                        const char* name) {
  object
      ->Set(isolate->GetCurrentContext(),
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            v8::Number::New(isolate, static_cast<double>(value)))
      .FromJust();
}

void StatisticsExtension::GetCounters(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  Isolate* isolate = reinterpret_cast<Isolate*>(info.GetIsolate());
  Heap* heap = isolate->heap();

  if (info.Length() > 0) {  // GC if first argument evaluates to true.
    if (info[0]->IsBoolean() && info[0]->BooleanValue(info.GetIsolate())) {
      heap->CollectAllGarbage(GCFlag::kNoFlags,
                              GarbageCollectionReason::kCountersExtension);
    }
  }

  Counters* counters = isolate->counters();
  v8::Local<v8::Object> result = v8::Object::New(info.GetIsolate());

  heap->FreeMainThreadLinearAllocationAreas();

  struct StatisticsCounter {
    v8::internal::StatsCounter* counter;
    const char* name;
  };
  // clang-format off
  const StatisticsCounter counter_list[] = {
#define ADD_COUNTER(name, caption) {counters->name(), #name},
      STATS_COUNTER_LIST(ADD_COUNTER)
      STATS_COUNTER_NATIVE_CODE_LIST(ADD_COUNTER)
#undef ADD_COUNTER
  };  // End counter_list array.
  // clang-format on

  for (size_t i = 0; i < arraysize(counter_list); i++) {
    AddCounter(info.GetIsolate(), result, counter_list[i].counter,
               counter_list[i].name);
  }

  struct StatisticNumber {
    size_t number;
    const char* name;
  };

  size_t new_space_size = 0;
  size_t new_space_available = 0;
  size_t new_space_committed_memory = 0;

  if (heap->new_space()) {
    new_space_size = heap->new_space()->Size();
    new_space_available = heap->new_space()->Available();
    new_space_committed_memory = heap->new_space()->CommittedMemory();
  }

  const StatisticNumber numbers[] = {
      {heap->memory_allocator()->Size(), "total_committed_bytes"},
      {new_space_size, "new_space_live_bytes"},
      {new_space_available, "new_space_available_bytes"},
      {new_space_committed_memory, "new_space_commited_bytes"},
      {heap->old_space()->Size(), "old_space_live_bytes"},
      {heap->old_space()->Available(), "old_space_available_bytes"},
      {heap->old_space()->CommittedMemory(), "old_space_commited_bytes"},
      {heap->code_space()->Size(), "code_space_live_bytes"},
      {heap->code_space()->Available(), "code_space_available_bytes"},
      {heap->code_space()->CommittedMemory(), "code_space_commited_bytes"},
      {heap->lo_space()->Size(), "lo_space_live_bytes"},
      {heap->lo_space()->Available(), "lo_space_available_bytes"},
      {heap->lo_space()->CommittedMemory(), "lo_space_commited_bytes"},
      {heap->code_lo_space()->Size(), "code_lo_space_live_bytes"},
      {heap->code_lo_space()->Available(), "code_lo_space_available_bytes"},
      {heap->code_lo_space()->CommittedMemory(),
       "code_lo_space_commited_bytes"},
      {heap->trusted_space()->Size(), "trusted_space_live_bytes"},
      {heap->trusted_space()->Available(), "trusted_space_available_bytes"},
      {heap->trusted_space()->CommittedMemory(),
       "trusted_space_commited_bytes"},
      {heap->trusted_lo_space()->Size(), "trusted_lo_space_live_bytes"},
      {heap->trusted_lo_space()->Available(),
       "trusted_lo_space_available_bytes"},
      {heap->trusted_lo_space()->CommittedMemory(),
       "trusted_lo_space_commited_bytes"},
  };

  for (size_t i = 0; i < arraysize(numbers); i++) {
    AddNumber(info.GetIsolate(), result, numbers[i].number, numbers[i].name);
  }

  AddNumber64(info.GetIsolate(), result, heap->external_memory(),
              "amount_of_external_allocated_memory");

  int reloc_info_total = 0;
  int source_position_table_total = 0;
  {
    HeapObjectIterator iterator(
        reinterpret_cast<Isolate*>(info.GetIsolate())->heap());
    DCHECK(!AllowGarbageCollection::IsAllowed());
    for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
         obj = iterator.Next()) {
      Tagged<Object> maybe_source_positions;
      if (IsCode(obj)) {
        Tagged<Code> code = Cast<Code>(obj);
        reloc_info_total += code->relocation_size();
        if (!code->has_source_position_table()) continue;
        maybe_source_positions = code->source_position_table();
      } else if (IsBytecodeArray(obj)) {
        maybe_source_positions =
            Cast<BytecodeArray>(obj)->raw_source_position_table(kAcquireLoad);
      } else {
        continue;
      }
      if (!IsTrustedByteArray(maybe_source_positions)) continue;
      Tagged<TrustedByteArray> source_positions =
          Cast<TrustedByteArray>(maybe_source_positions);
      if (source_positions->length() == 0) continue;
      source_position_table_total += source_positions->AllocatedSize();
    }
  }

  AddNumber(info.GetIsolate(), result, reloc_info_total,
            "reloc_info_total_size");
  AddNumber(info.GetIsolate(), result, source_position_table_total,
            "source_position_table_total_size");
  info.GetReturnValue().Set(result);
}

}  // namespace internal
}  // namespace v8
