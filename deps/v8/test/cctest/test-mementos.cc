// Copyright 2014 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {

static void SetUpNewSpaceWithPoisonedMementoAtTop() {
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();

  // Make sure we can allocate some objects without causing a GC later.
  heap::InvokeMajorGC(heap);

  // Allocate a string, the GC may suspect a memento behind the string.
  Handle<SeqOneByteString> string =
      isolate->factory()->NewRawOneByteString(12).ToHandleChecked();
  CHECK(!(*string).is_null());

  // Create an allocation memento behind the string with a garbage allocation
  // site pointer.
  Tagged<AllocationMemento> memento = AllocationMemento::unchecked_cast(
      Tagged<Object>(heap->NewSpaceTop() + kHeapObjectTag));
  memento->set_map_after_allocation(
      ReadOnlyRoots(heap).allocation_memento_map(), SKIP_WRITE_BARRIER);

  // Using this accessor as we're writing an invalid tagged pointer.
  Tagged_t poison = kHeapObjectTag;
  memento->WriteField<Tagged_t>(AllocationMemento::kAllocationSiteOffset,
                                poison);
}


TEST(Regress340063) {
  CcTest::InitializeVM();
  if (!i::v8_flags.allocation_site_pretenuring || v8_flags.single_generation)
    return;
  v8::HandleScope scope(CcTest::isolate());

  SetUpNewSpaceWithPoisonedMementoAtTop();

  // Call GC to see if we can handle a poisonous memento right after the
  // current new space top pointer.
  i::heap::InvokeAtomicMajorGC(CcTest::heap());
}


TEST(BadMementoAfterTopForceMinorGC) {
  CcTest::InitializeVM();
  if (!i::v8_flags.allocation_site_pretenuring || v8_flags.single_generation)
    return;
  v8::HandleScope scope(CcTest::isolate());

  SetUpNewSpaceWithPoisonedMementoAtTop();

  // Force GC to test the poisoned memento handling
  i::heap::InvokeMinorGC(CcTest::heap());
}

}  // namespace internal
}  // namespace v8
