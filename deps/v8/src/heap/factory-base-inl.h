// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FACTORY_BASE_INL_H_
#define V8_HEAP_FACTORY_BASE_INL_H_

#include "src/heap/factory-base.h"
#include "src/heap/local-heap-inl.h"
#include "src/numbers/conversions.h"
#include "src/objects/heap-number.h"
#include "src/objects/map.h"
#include "src/objects/slots-inl.h"
#include "src/objects/smi.h"
#include "src/objects/struct-inl.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

#define RO_ROOT_ACCESSOR(Type, name, CamelName) \
  template <typename Impl>                      \
  Handle<Type> FactoryBase<Impl>::name() {      \
    return read_only_roots().name##_handle();   \
  }
READ_ONLY_ROOT_LIST(RO_ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

#define MUTABLE_ROOT_ACCESSOR(Type, name, CamelName)     \
  template <typename Impl>                               \
  Handle<Type> FactoryBase<Impl>::name() {               \
    return handle(isolate()->heap()->name(), isolate()); \
  }
MUTABLE_ROOT_LIST(MUTABLE_ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

template <typename Impl>
Handle<Boolean> FactoryBase<Impl>::ToBoolean(bool value) {
  return value ? Handle<Boolean>::cast(impl()->true_value())
               : Handle<Boolean>::cast(impl()->false_value());
}

template <typename Impl>
template <AllocationType allocation>
Handle<Object> FactoryBase<Impl>::NewNumber(double value) {
  // Materialize as a SMI if possible.
  int32_t int_value;
  if (DoubleToSmiInteger(value, &int_value)) {
    return handle(Smi::FromInt(int_value), isolate());
  }
  return NewHeapNumber<allocation>(value);
}

template <typename Impl>
template <AllocationType allocation>
Handle<Object> FactoryBase<Impl>::NewNumberFromInt(int32_t value) {
  if (Smi::IsValid(value)) return handle(Smi::FromInt(value), isolate());
  // Bypass NewNumber to avoid various redundant checks.
  return NewHeapNumber<allocation>(FastI2D(value));
}

template <typename Impl>
template <AllocationType allocation>
Handle<Object> FactoryBase<Impl>::NewNumberFromUint(uint32_t value) {
  int32_t int32v = static_cast<int32_t>(value);
  if (int32v >= 0 && Smi::IsValid(int32v)) {
    return handle(Smi::FromInt(int32v), isolate());
  }
  return NewHeapNumber<allocation>(FastUI2D(value));
}

template <typename Impl>
template <AllocationType allocation>
Handle<Object> FactoryBase<Impl>::NewNumberFromSize(size_t value) {
  // We can't use Smi::IsValid() here because that operates on a signed
  // intptr_t, and casting from size_t could create a bogus sign bit.
  if (value <= static_cast<size_t>(Smi::kMaxValue)) {
    return handle(Smi::FromIntptr(static_cast<intptr_t>(value)), isolate());
  }
  return NewHeapNumber<allocation>(static_cast<double>(value));
}

template <typename Impl>
template <AllocationType allocation>
Handle<Object> FactoryBase<Impl>::NewNumberFromInt64(int64_t value) {
  if (value <= std::numeric_limits<int32_t>::max() &&
      value >= std::numeric_limits<int32_t>::min() &&
      Smi::IsValid(static_cast<int32_t>(value))) {
    return handle(Smi::FromInt(static_cast<int32_t>(value)), isolate());
  }
  return NewHeapNumber<allocation>(static_cast<double>(value));
}

template <typename Impl>
template <AllocationType allocation>
Handle<HeapNumber> FactoryBase<Impl>::NewHeapNumber(double value) {
  Handle<HeapNumber> heap_number = NewHeapNumber<allocation>();
  heap_number->set_value(value);
  return heap_number;
}

template <typename Impl>
template <AllocationType allocation>
Handle<HeapNumber> FactoryBase<Impl>::NewHeapNumberFromBits(uint64_t bits) {
  Handle<HeapNumber> heap_number = NewHeapNumber<allocation>();
  heap_number->set_value_as_bits(bits);
  return heap_number;
}

template <typename Impl>
template <AllocationType allocation>
Handle<HeapNumber> FactoryBase<Impl>::NewHeapNumberWithHoleNaN() {
  return NewHeapNumberFromBits<allocation>(kHoleNanInt64);
}

template <typename Impl>
template <typename StructType>
Tagged<StructType> FactoryBase<Impl>::NewStructInternal(
    InstanceType type, AllocationType allocation) {
  ReadOnlyRoots roots = read_only_roots();
  Tagged<Map> map = Map::GetMapFor(roots, type);
  int size = StructType::kSize;
  return StructType::cast(NewStructInternal(roots, map, size, allocation));
}

template <typename Impl>
Tagged<Struct> FactoryBase<Impl>::NewStructInternal(ReadOnlyRoots roots,
                                                    Tagged<Map> map, int size,
                                                    AllocationType allocation) {
  DCHECK_EQ(size, map->instance_size());
  Tagged<HeapObject> result = AllocateRawWithImmortalMap(size, allocation, map);
  Tagged<Struct> str = Tagged<Struct>::cast(result);
  Tagged<Undefined> undefined = roots.undefined_value();
  int length = (size >> kTaggedSizeLog2) - 1;
  MemsetTagged(str->RawField(Struct::kHeaderSize), undefined, length);
  return str;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FACTORY_BASE_INL_H_
