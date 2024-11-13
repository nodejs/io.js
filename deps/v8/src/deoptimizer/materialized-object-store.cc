// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/materialized-object-store.h"

#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/oddball.h"

namespace v8 {
namespace internal {

Handle<FixedArray> MaterializedObjectStore::Get(Address fp) {
  int index = StackIdToIndex(fp);
  if (index == -1) {
    return Handle<FixedArray>::null();
  }
  DirectHandle<FixedArray> array = GetStackEntries();
  CHECK_GT(array->length(), index);
  return Cast<FixedArray>(Handle<Object>(array->get(index), isolate()));
}

void MaterializedObjectStore::Set(
    Address fp, DirectHandle<FixedArray> materialized_objects) {
  int index = StackIdToIndex(fp);
  if (index == -1) {
    index = static_cast<int>(frame_fps_.size());
    frame_fps_.push_back(fp);
  }

  DirectHandle<FixedArray> array = EnsureStackEntries(index + 1);
  array->set(index, *materialized_objects);
}

bool MaterializedObjectStore::Remove(Address fp) {
  auto it = std::find(frame_fps_.begin(), frame_fps_.end(), fp);
  if (it == frame_fps_.end()) return false;
  int index = static_cast<int>(std::distance(frame_fps_.begin(), it));

  frame_fps_.erase(it);
  Tagged<FixedArray> array = isolate()->heap()->materialized_objects();

  CHECK_LT(index, array->length());
  int fps_size = static_cast<int>(frame_fps_.size());
  for (int i = index; i < fps_size; i++) {
    array->set(i, array->get(i + 1));
  }
  array->set(fps_size, ReadOnlyRoots(isolate()).undefined_value());
  return true;
}

int MaterializedObjectStore::StackIdToIndex(Address fp) {
  auto it = std::find(frame_fps_.begin(), frame_fps_.end(), fp);
  return it == frame_fps_.end()
             ? -1
             : static_cast<int>(std::distance(frame_fps_.begin(), it));
}

Handle<FixedArray> MaterializedObjectStore::GetStackEntries() {
  return Handle<FixedArray>(isolate()->heap()->materialized_objects(),
                            isolate());
}

Handle<FixedArray> MaterializedObjectStore::EnsureStackEntries(int length) {
  Handle<FixedArray> array = GetStackEntries();
  if (array->length() >= length) {
    return array;
  }

  int new_length = length > 10 ? length : 10;
  if (new_length < 2 * array->length()) {
    new_length = 2 * array->length();
  }

  Handle<FixedArray> new_array =
      isolate()->factory()->NewFixedArray(new_length, AllocationType::kOld);
  for (int i = 0; i < array->length(); i++) {
    new_array->set(i, array->get(i));
  }
  Tagged<HeapObject> undefined_value =
      ReadOnlyRoots(isolate()).undefined_value();
  for (int i = array->length(); i < length; i++) {
    new_array->set(i, undefined_value);
  }
  isolate()->heap()->SetRootMaterializedObjects(*new_array);
  return new_array;
}

}  // namespace internal
}  // namespace v8
