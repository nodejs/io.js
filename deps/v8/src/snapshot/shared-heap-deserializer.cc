// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/shared-heap-deserializer.h"

#include "src/heap/heap-inl.h"

namespace v8 {
namespace internal {

void SharedHeapDeserializer::DeserializeIntoIsolate() {
  // Don't deserialize into isolates that don't own their string table. If there
  // are client Isolates, the shared heap object cache should already be
  // populated.
  // TODO(372493838): The shared heap object cache can only contain strings.
  // Update name to reflect this.
  if (!isolate()->OwnsStringTables()) {
    DCHECK(!isolate()->shared_heap_object_cache()->empty());
    return;
  }

  DCHECK(isolate()->shared_heap_object_cache()->empty());
  HandleScope scope(isolate());

  IterateSharedHeapObjectCache(isolate(), this);
  DeserializeStringTable();
  DeserializeDeferredObjects();

  if (should_rehash()) {
    // The hash seed has already been initialized in ReadOnlyDeserializer, thus
    // there is no need to call `isolate()->heap()->InitializeHashSeed();`.
    Rehash();
  }
}

void SharedHeapDeserializer::DeserializeStringTable() {
  // See SharedHeapSerializer::SerializeStringTable.

  DCHECK(isolate()->OwnsStringTables());

  // Get the string table size.
  const int length = source()->GetUint30();

  // .. and the contents.
  DirectHandleVector<String> strings(isolate());
  strings.reserve(length);
  for (int i = 0; i < length; ++i) {
    strings.emplace_back(Cast<String>(ReadObject()));
  }

  StringTable* t = isolate()->string_table();
  DCHECK_EQ(t->NumberOfElements(), 0);
  t->InsertForIsolateDeserialization(
      isolate(), base::VectorOf(strings.data(), strings.size()));
  DCHECK_EQ(t->NumberOfElements(), length);
}

}  // namespace internal
}  // namespace v8
