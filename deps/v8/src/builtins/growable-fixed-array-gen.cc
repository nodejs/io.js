// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/growable-fixed-array-gen.h"

#include <optional>

#include "src/compiler/code-assembler.h"

namespace v8 {
namespace internal {

#include "src/codegen/define-code-stub-assembler-macros.inc"

void GrowableFixedArray::Reserve(TNode<IntPtrT> required_capacity) {
  Label out(this);

  GotoIf(IntPtrGreaterThanOrEqual(var_capacity_.value(), required_capacity),
         &out);

  // Gotta grow.
  TVARIABLE(IntPtrT, var_new_capacity, var_capacity_.value());
  Label loop(this, &var_new_capacity);
  Goto(&loop);

  // First find the new capacity.
  BIND(&loop);
  {
    var_new_capacity = NewCapacity(var_new_capacity.value());
    GotoIf(IntPtrLessThan(var_new_capacity.value(), required_capacity), &loop);
  }

  // Now grow.
  var_capacity_ = var_new_capacity.value();
  var_array_ = ResizeFixedArray(var_length_.value(), var_capacity_.value());
  Goto(&out);

  BIND(&out);
}

void GrowableFixedArray::Push(const TNode<Object> value) {
  const TNode<IntPtrT> length = var_length_.value();
  const TNode<IntPtrT> capacity = var_capacity_.value();

  Label grow(this), store(this);
  Branch(IntPtrEqual(capacity, length), &grow, &store);

  BIND(&grow);
  {
    var_capacity_ = NewCapacity(capacity);
    var_array_ = ResizeFixedArray(length, var_capacity_.value());

    Goto(&store);
  }

  BIND(&store);
  {
    const TNode<FixedArray> array = var_array_.value();
    UnsafeStoreFixedArrayElement(array, length, value);

    var_length_ = IntPtrAdd(length, IntPtrConstant(1));
  }
}

TNode<FixedArray> GrowableFixedArray::ToFixedArray() {
  return ResizeFixedArray(length(), length());
}

TNode<JSArray> GrowableFixedArray::ToJSArray(const TNode<Context> context) {
  const ElementsKind kind = PACKED_ELEMENTS;

  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Map> array_map = LoadJSArrayElementsMap(kind, native_context);

  // Shrink to fit if necessary.
  {
    Label next(this);

    const TNode<IntPtrT> length = var_length_.value();
    const TNode<IntPtrT> capacity = var_capacity_.value();

    GotoIf(WordEqual(length, capacity), &next);

    var_array_ = ResizeFixedArray(length, length);
    var_capacity_ = length;
    Goto(&next);

    BIND(&next);
  }

  const TNode<Smi> result_length = SmiTag(length());
  const TNode<JSArray> result =
      AllocateJSArray(array_map, var_array_.value(), result_length);
  return result;
}

TNode<IntPtrT> GrowableFixedArray::NewCapacity(
    TNode<IntPtrT> current_capacity) {
  CSA_DCHECK(this,
             IntPtrGreaterThanOrEqual(current_capacity, IntPtrConstant(0)));

  // Growth rate is analog to JSObject::NewElementsCapacity:
  // new_capacity = (current_capacity + (current_capacity >> 1)) + 16.

  const TNode<IntPtrT> new_capacity =
      IntPtrAdd(IntPtrAdd(current_capacity, WordShr(current_capacity, 1)),
                IntPtrConstant(16));

  return new_capacity;
}

TNode<FixedArray> GrowableFixedArray::ResizeFixedArray(
    const TNode<IntPtrT> element_count, const TNode<IntPtrT> new_capacity) {
  CSA_DCHECK(this, IntPtrGreaterThanOrEqual(element_count, IntPtrConstant(0)));
  CSA_DCHECK(this, IntPtrGreaterThanOrEqual(new_capacity, IntPtrConstant(0)));
  CSA_DCHECK(this, IntPtrGreaterThanOrEqual(new_capacity, element_count));

  const TNode<FixedArray> from_array = var_array_.value();

  CodeStubAssembler::ExtractFixedArrayFlags flags;
  flags |= CodeStubAssembler::ExtractFixedArrayFlag::kFixedArrays;
  TNode<FixedArray> to_array = CAST(
      ExtractFixedArray(from_array, std::optional<TNode<IntPtrT>>(std::nullopt),
                        std::optional<TNode<IntPtrT>>(element_count),
                        std::optional<TNode<IntPtrT>>(new_capacity), flags));

  return to_array;
}

#include "src/codegen/undef-code-stub-assembler-macros.inc"

}  // namespace internal
}  // namespace v8
