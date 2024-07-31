// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"

namespace v8 {
namespace internal {

// ES #sec-isfinite-number
TF_BUILTIN(GlobalIsFinite, CodeStubAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);

  Label return_true(this), return_false(this);

  // We might need to loop once for ToNumber conversion.
  TVARIABLE(Object, var_num);
  Label loop(this, &var_num);
  var_num = Parameter<Object>(Descriptor::kNumber);
  Goto(&loop);
  BIND(&loop);
  {
    TNode<Object> num = var_num.value();

    // Check if {num} is a Smi or a HeapObject.
    GotoIf(TaggedIsSmi(num), &return_true);
    TNode<HeapObject> num_heap_object = CAST(num);

    // Check if {num_heap_object} is a HeapNumber.
    Label if_numisheapnumber(this),
        if_numisnotheapnumber(this, Label::kDeferred);
    Branch(IsHeapNumber(num_heap_object), &if_numisheapnumber,
           &if_numisnotheapnumber);

    BIND(&if_numisheapnumber);
    {
      // Check if {num_heap_object} contains a finite, non-NaN value.
      TNode<Float64T> num_value = LoadHeapNumberValue(num_heap_object);
      BranchIfFloat64IsNaN(Float64Sub(num_value, num_value), &return_false,
                           &return_true);
    }

    BIND(&if_numisnotheapnumber);
    {
      // Need to convert {num_heap_object} to a Number first.
      var_num =
          CallBuiltin(Builtin::kNonNumberToNumber, context, num_heap_object);
      Goto(&loop);
    }
  }

  BIND(&return_true);
  Return(TrueConstant());

  BIND(&return_false);
  Return(FalseConstant());
}

// ES6 #sec-isnan-number
TF_BUILTIN(GlobalIsNaN, CodeStubAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);

  Label return_true(this), return_false(this);

  // We might need to loop once for ToNumber conversion.
  TVARIABLE(Object, var_num);
  Label loop(this, &var_num);
  var_num = Parameter<Object>(Descriptor::kNumber);
  Goto(&loop);
  BIND(&loop);
  {
    TNode<Object> num = var_num.value();

    // Check if {num} is a Smi or a HeapObject.
    GotoIf(TaggedIsSmi(num), &return_false);
    TNode<HeapObject> num_heap_object = CAST(num);

    // Check if {num_heap_object} is a HeapNumber.
    Label if_numisheapnumber(this),
        if_numisnotheapnumber(this, Label::kDeferred);
    Branch(IsHeapNumber(num_heap_object), &if_numisheapnumber,
           &if_numisnotheapnumber);

    BIND(&if_numisheapnumber);
    {
      // Check if {num_heap_object} contains a NaN.
      TNode<Float64T> num_value = LoadHeapNumberValue(num_heap_object);
      BranchIfFloat64IsNaN(num_value, &return_true, &return_false);
    }

    BIND(&if_numisnotheapnumber);
    {
      // Need to convert {num_heap_object} to a Number first.
      var_num =
          CallBuiltin(Builtin::kNonNumberToNumber, context, num_heap_object);
      Goto(&loop);
    }
  }

  BIND(&return_true);
  Return(TrueConstant());

  BIND(&return_false);
  Return(FalseConstant());
}

}  // namespace internal
}  // namespace v8
