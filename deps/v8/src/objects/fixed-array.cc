// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/fixed-array.h"

#include "src/objects/map-inl.h"

namespace v8 {
namespace internal {

int FixedArrayBase::GetMaxLengthForNewSpaceAllocation(ElementsKind kind) {
  return ((kMaxRegularHeapObjectSize - FixedArrayBase::kHeaderSize) >>
          ElementsKindToShiftSize(kind));
}

bool FixedArrayBase::IsCowArray() const {
  return map() == GetReadOnlyRoots().fixed_cow_array_map();
}

Handle<FixedArray> FixedArray::SetAndGrow(Isolate* isolate,
                                          Handle<FixedArray> array, int index,
                                          Handle<Object> value) {
  int len = array->length();
  if (index >= len) {
    int new_capacity = FixedArray::NewCapacityForIndex(index, len);
    array = Handle<FixedArray>::cast(
        FixedArray::Resize(isolate, array, new_capacity));
    // TODO(jgruber): This is somewhat subtle - other FixedArray methods
    // use `undefined` as a filler. Make this more explicit.
    array->FillWithHoles(len, new_capacity);
  }

  array->set(index, *value);
  return array;
}

void FixedArray::RightTrim(Isolate* isolate, int new_capacity) {
  DCHECK_NE(map(), ReadOnlyRoots{isolate}.fixed_cow_array_map());
  Super::RightTrim(isolate, new_capacity);
}

Handle<FixedArray> FixedArray::RightTrimOrEmpty(Isolate* isolate,
                                                Handle<FixedArray> array,
                                                int new_length) {
  if (new_length == 0) {
    return ReadOnlyRoots{isolate}.empty_fixed_array_handle();
  }
  array->RightTrim(isolate, new_length);
  return array;
}

// static
Handle<ArrayList> ArrayList::Add(Isolate* isolate, Handle<ArrayList> array,
                                 Tagged<Smi> obj, AllocationType allocation) {
  int length = array->length();
  int new_length = length + 1;
  array = EnsureSpace(isolate, array, new_length, allocation);
  DCHECK_EQ(array->length(), length);

  DisallowGarbageCollection no_gc;
  array->set(length, obj, SKIP_WRITE_BARRIER);
  array->set_length(new_length);
  return array;
}

// static
Handle<ArrayList> ArrayList::Add(Isolate* isolate, Handle<ArrayList> array,
                                 Handle<Object> obj,
                                 AllocationType allocation) {
  int length = array->length();
  int new_length = length + 1;
  array = EnsureSpace(isolate, array, new_length, allocation);
  DCHECK_EQ(array->length(), length);

  DisallowGarbageCollection no_gc;
  array->set(length, *obj);
  array->set_length(new_length);
  return array;
}

// static
Handle<ArrayList> ArrayList::Add(Isolate* isolate, Handle<ArrayList> array,
                                 Handle<Object> obj0, Handle<Object> obj1,
                                 AllocationType allocation) {
  int length = array->length();
  int new_length = length + 2;
  array = EnsureSpace(isolate, array, new_length, allocation);
  DCHECK_EQ(array->length(), length);

  DisallowGarbageCollection no_gc;
  array->set(length + 0, *obj0);
  array->set(length + 1, *obj1);
  array->set_length(new_length);
  return array;
}

// static
Handle<FixedArray> ArrayList::ToFixedArray(Isolate* isolate,
                                           Handle<ArrayList> array,
                                           AllocationType allocation) {
  int length = array->length();
  if (length == 0) return isolate->factory()->empty_fixed_array();

  Handle<FixedArray> result = FixedArray::New(isolate, length, allocation);
  DisallowGarbageCollection no_gc;
  WriteBarrierMode mode = result->GetWriteBarrierMode(no_gc);
  ObjectSlot dst_slot(result->RawFieldOfElementAt(0));
  ObjectSlot src_slot(array->RawFieldOfElementAt(0));
  isolate->heap()->CopyRange(*result, dst_slot, src_slot, length, mode);
  return result;
}

void ArrayList::RightTrim(Isolate* isolate, int new_capacity) {
  Super::RightTrim(isolate, new_capacity);
  if (new_capacity < length()) set_length(new_capacity);
}

// static
Handle<ArrayList> ArrayList::EnsureSpace(Isolate* isolate,
                                         Handle<ArrayList> array, int length,
                                         AllocationType allocation) {
  DCHECK_LT(0, length);
  int old_capacity = array->capacity();
  if (old_capacity >= length) return array;

  int old_length = array->length();
  // Ensure calculation matches CodeStubAssembler::ArrayListEnsureSpace.
  int new_capacity = length + std::max(length / 2, 2);
  Handle<ArrayList> new_array =
      ArrayList::New(isolate, new_capacity, allocation);
  DisallowGarbageCollection no_gc;
  new_array->set_length(old_length);
  WriteBarrierMode mode = new_array->GetWriteBarrierMode(no_gc);
  CopyElements(isolate, *new_array, 0, *array, 0, old_length, mode);
  return new_array;
}

// static
Handle<WeakArrayList> WeakArrayList::AddToEnd(Isolate* isolate,
                                              Handle<WeakArrayList> array,
                                              MaybeObjectHandle value) {
  int length = array->length();
  array = EnsureSpace(isolate, array, length + 1);
  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw = *array;
    // Reload length; GC might have removed elements from the array.
    length = raw->length();
    raw->Set(length, *value);
    raw->set_length(length + 1);
  }
  return array;
}

Handle<WeakArrayList> WeakArrayList::AddToEnd(Isolate* isolate,
                                              Handle<WeakArrayList> array,
                                              MaybeObjectHandle value1,
                                              Tagged<Smi> value2) {
  int length = array->length();
  array = EnsureSpace(isolate, array, length + 2);
  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw = *array;
    // Reload length; GC might have removed elements from the array.
    length = array->length();
    raw->Set(length, *value1);
    raw->Set(length + 1, value2);
    raw->set_length(length + 2);
  }
  return array;
}

// static
Handle<WeakArrayList> WeakArrayList::Append(Isolate* isolate,
                                            Handle<WeakArrayList> array,
                                            MaybeObjectHandle value,
                                            AllocationType allocation) {
  int length = 0;
  int new_length = 0;
  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw = *array;
    length = raw->length();

    if (length < raw->capacity()) {
      raw->Set(length, *value);
      raw->set_length(length + 1);
      return array;
    }

    // Not enough space in the array left, either grow, shrink or
    // compact the array.
    new_length = raw->CountLiveElements() + 1;
  }

  bool shrink = new_length < length / 4;
  bool grow = 3 * (length / 4) < new_length;

  if (shrink || grow) {
    // Grow or shrink array and compact out-of-place.
    int new_capacity = CapacityForLength(new_length);
    array = isolate->factory()->CompactWeakArrayList(array, new_capacity,
                                                     allocation);

  } else {
    // Perform compaction in the current array.
    array->Compact(isolate);
  }

  // Now append value to the array, there should always be enough space now.
  DCHECK_LT(array->length(), array->capacity());

  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw = *array;
    // Reload length, allocation might have killed some weak refs.
    int index = raw->length();
    raw->Set(index, *value);
    raw->set_length(index + 1);
  }
  return array;
}

void WeakArrayList::Compact(Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  int length = this->length();
  int new_length = 0;

  for (int i = 0; i < length; i++) {
    Tagged<MaybeObject> value = Get(isolate, i);

    if (!value.IsCleared()) {
      if (new_length != i) {
        Set(new_length, value);
      }
      ++new_length;
    }
  }

  set_length(new_length);
}

bool WeakArrayList::IsFull() const { return length() == capacity(); }

// static
Handle<WeakArrayList> WeakArrayList::EnsureSpace(Isolate* isolate,
                                                 Handle<WeakArrayList> array,
                                                 int length,
                                                 AllocationType allocation) {
  int capacity = array->capacity();
  if (capacity < length) {
    int grow_by = CapacityForLength(length) - capacity;
    array = isolate->factory()->CopyWeakArrayListAndGrow(array, grow_by,
                                                         allocation);
  }
  return array;
}

int WeakArrayList::CountLiveWeakReferences() const {
  int live_weak_references = 0;
  for (int i = 0; i < length(); i++) {
    if (Get(i).IsWeak()) {
      ++live_weak_references;
    }
  }
  return live_weak_references;
}

int WeakArrayList::CountLiveElements() const {
  int non_cleared_objects = 0;
  for (int i = 0; i < length(); i++) {
    if (!Get(i).IsCleared()) {
      ++non_cleared_objects;
    }
  }
  return non_cleared_objects;
}

bool WeakArrayList::RemoveOne(MaybeObjectHandle value) {
  int last_index = length() - 1;
  // Optimize for the most recently added element to be removed again.
  for (int i = last_index; i >= 0; --i) {
    if (Get(i) != *value) continue;
    // Move the last element into this slot (or no-op, if this is the last
    // slot).
    Set(i, Get(last_index));
    Set(last_index, ClearedValue(GetIsolate()));
    set_length(last_index);
    return true;
  }
  return false;
}

bool WeakArrayList::Contains(Tagged<MaybeObject> value) {
  for (int i = 0; i < length(); ++i) {
    if (Get(i) == value) return true;
  }
  return false;
}

}  // namespace internal
}  // namespace v8
