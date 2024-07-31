// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ARGUMENTS_INL_H_
#define V8_EXECUTION_ARGUMENTS_INL_H_

#include "src/execution/arguments.h"

#include "src/handles/handles-inl.h"
#include "src/objects/objects-inl.h"  // TODO(jkummerow): Just smi-inl.h.
#include "src/objects/tagged-index.h"

namespace v8 {
namespace internal {

template <ArgumentsType T>
Arguments<T>::ChangeValueScope::ChangeValueScope(Isolate* isolate,
                                                 Arguments* args, int index,
                                                 Tagged<Object> value)
    : location_(args->address_of_arg_at(index)) {
  old_value_ = handle(Tagged<Object>(*location_), isolate);
  *location_ = value.ptr();
}

template <ArgumentsType T>
int Arguments<T>::smi_value_at(int index) const {
  Tagged<Object> obj = (*this)[index];
  int value = Smi::ToInt(obj);
  DCHECK_IMPLIES(IsTaggedIndex(obj), value == tagged_index_value_at(index));
  return value;
}

template <ArgumentsType T>
uint32_t Arguments<T>::positive_smi_value_at(int index) const {
  int value = smi_value_at(index);
  DCHECK_LE(0, value);
  return value;
}

template <ArgumentsType T>
int Arguments<T>::tagged_index_value_at(int index) const {
  return static_cast<int>(TaggedIndex::cast((*this)[index]).value());
}

template <ArgumentsType T>
double Arguments<T>::number_value_at(int index) const {
  return Object::Number((*this)[index]);
}

template <ArgumentsType T>
Handle<Object> Arguments<T>::atOrUndefined(Isolate* isolate, int index) const {
  if (index >= length_) {
    return Handle<Object>::cast(isolate->factory()->undefined_value());
  }
  return at<Object>(index);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ARGUMENTS_INL_H_
