// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ALLOCATION_BUILDER_INL_H_
#define V8_COMPILER_ALLOCATION_BUILDER_INL_H_

#include "src/compiler/access-builder.h"
#include "src/compiler/allocation-builder.h"
#include "src/heap/heap-inl.h"
#include "src/objects/arguments-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

void AllocationBuilder::Allocate(int size, AllocationType allocation,
                                 Type type) {
  CHECK_GT(size, 0);
  DCHECK_LE(size, isolate()->heap()->MaxRegularHeapObjectSize(allocation));
  effect_ = graph()->NewNode(
      common()->BeginRegion(RegionObservability::kNotObservable), effect_);
  allocation_ =
      graph()->NewNode(simplified()->Allocate(type, allocation),
                       jsgraph()->ConstantNoHole(size), effect_, control_);
  effect_ = allocation_;
}

void AllocationBuilder::AllocateContext(int variadic_part_length, MapRef map) {
  DCHECK(base::IsInRange(map.instance_type(), FIRST_CONTEXT_TYPE,
                         LAST_CONTEXT_TYPE));
  DCHECK_NE(NATIVE_CONTEXT_TYPE, map.instance_type());
  int size = Context::SizeFor(variadic_part_length);
  Allocate(size, AllocationType::kYoung, Type::OtherInternal());
  Store(AccessBuilder::ForMap(), map);
  static_assert(static_cast<int>(Context::kLengthOffset) ==
                static_cast<int>(FixedArray::kLengthOffset));
  Store(AccessBuilder::ForFixedArrayLength(),
        jsgraph()->ConstantNoHole(variadic_part_length));
}

bool AllocationBuilder::CanAllocateArray(int length, MapRef map,
                                         AllocationType allocation) {
  DCHECK(map.instance_type() == FIXED_ARRAY_TYPE ||
         map.instance_type() == FIXED_DOUBLE_ARRAY_TYPE);
  int const size = (map.instance_type() == FIXED_ARRAY_TYPE)
                       ? FixedArray::SizeFor(length)
                       : FixedDoubleArray::SizeFor(length);
  return size <= isolate()->heap()->MaxRegularHeapObjectSize(allocation);
}

// Compound allocation of a FixedArray.
void AllocationBuilder::AllocateArray(int length, MapRef map,
                                      AllocationType allocation) {
  DCHECK(CanAllocateArray(length, map, allocation));
  int size = (map.instance_type() == FIXED_ARRAY_TYPE)
                 ? FixedArray::SizeFor(length)
                 : FixedDoubleArray::SizeFor(length);
  Allocate(size, allocation, Type::OtherInternal());
  Store(AccessBuilder::ForMap(), map);
  Store(AccessBuilder::ForFixedArrayLength(),
        jsgraph()->ConstantNoHole(length));
}

bool AllocationBuilder::CanAllocateSloppyArgumentElements(
    int length, MapRef map, AllocationType allocation) {
  int const size = SloppyArgumentsElements::SizeFor(length);
  return size <= isolate()->heap()->MaxRegularHeapObjectSize(allocation);
}

void AllocationBuilder::AllocateSloppyArgumentElements(
    int length, MapRef map, AllocationType allocation) {
  DCHECK(CanAllocateSloppyArgumentElements(length, map, allocation));
  int size = SloppyArgumentsElements::SizeFor(length);
  Allocate(size, allocation, Type::OtherInternal());
  Store(AccessBuilder::ForMap(), map);
  Store(AccessBuilder::ForFixedArrayLength(),
        jsgraph()->ConstantNoHole(length));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ALLOCATION_BUILDER_INL_H_
