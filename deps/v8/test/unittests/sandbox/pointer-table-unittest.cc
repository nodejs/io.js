// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/handles/handles-inl.h"
#include "src/objects/js-objects.h"
#include "src/sandbox/external-pointer-table.h"
#include "test/unittests/heap/heap-utils.h"  // For ManualGCScope
#include "test/unittests/test-utils.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

using PointerTableTest = TestWithContext;

TEST_F(PointerTableTest, ExternalPointerTableCompaction) {
  // This tests ensures that pointer table compaction works as expected and
  // that --stress-compaction causes us to compact the table whenever possible.

  auto* iso = i_isolate();
  auto* heap = iso->heap();
  auto* space = heap->old_external_pointer_space();

  ManualGCScope manual_gc_scope(iso);

  v8_flags.stress_compaction = true;

  int* external_1 = new int;
  int* external_2 = new int;

  {
    v8::HandleScope scope(reinterpret_cast<v8::Isolate*>(iso));

    // Allocate one segment worth of external pointer table entries and keep the
    // host objects in a FixedArray so they and their entries are kept alive.
    uint32_t num_entries = space->freelist_length();
    DirectHandle<FixedArray> array = iso->factory()->NewFixedArray(num_entries);
    {
      v8::HandleScope inner_scope(reinterpret_cast<v8::Isolate*>(iso));
      for (uint32_t i = 0; i < num_entries; i++) {
        DirectHandle<JSObject> obj =
            iso->factory()->NewExternal(external_1, AllocationType::kOld);
        array->set(i, *obj);
      }
      CHECK_EQ(0, space->freelist_length());
      CHECK_EQ(1, space->NumSegmentsForTesting());
    }

    {
      v8::HandleScope inner_scope(reinterpret_cast<v8::Isolate*>(iso));

      // Allocate one additional external poiner table entry, which should now
      // end up on a new segment.
      CHECK_EQ(1, space->NumSegmentsForTesting());
      DirectHandle<JSExternalObject> obj = Cast<JSExternalObject>(
          iso->factory()->NewExternal(external_2, AllocationType::kOld));
      CHECK_EQ(2, space->NumSegmentsForTesting());

      // TODO(saelo): maybe it'd be nice to also automatically generate
      // accessors for the underlying table handles.
      ExternalPointerHandle original_handle =
          obj->ReadField<ExternalPointerHandle>(JSExternalObject::kValueOffset);

      // Free one entry in the array so that the table entry can be reclaimed.
      array->set(0, *iso->factory()->undefined_value());

      // There should be no free entries in the table yet, so nothing can be
      // compacted during the first GC.
      InvokeMajorGC();
      CHECK_EQ(2, space->NumSegmentsForTesting());
      ExternalPointerHandle current_handle =
          obj->ReadField<ExternalPointerHandle>(JSExternalObject::kValueOffset);
      CHECK_EQ(original_handle, current_handle);
      CHECK_EQ(obj->value(), external_2);

      // Now at least one entry in the first segment must be free, so compaction
      // should be possible. This should leave the 2nd segment empty, causing it
      // to be deallocated.
      InvokeMajorGC();
      CHECK_EQ(1, space->NumSegmentsForTesting());
      current_handle =
          obj->ReadField<ExternalPointerHandle>(JSExternalObject::kValueOffset);
      CHECK_NE(original_handle, current_handle);
      CHECK_EQ(obj->value(), external_2);
    }
  }

  delete external_1;
  delete external_2;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX
