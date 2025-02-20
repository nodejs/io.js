// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_GROWABLE_FIXED_ARRAY_GEN_H_
#define V8_BUILTINS_GROWABLE_FIXED_ARRAY_GEN_H_

#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {


// Utility class implementing a growable fixed array through CSA.
class GrowableFixedArray : public CodeStubAssembler {
 public:
  explicit GrowableFixedArray(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state),
        var_array_(this),
        var_length_(this),
        var_capacity_(this) {
    var_array_ = EmptyFixedArrayConstant();
    var_capacity_ = IntPtrConstant(0);
    var_length_ = IntPtrConstant(0);
  }

  TNode<IntPtrT> length() const { return var_length_.value(); }

  TVariable<FixedArray>* var_array() { return &var_array_; }
  TVariable<IntPtrT>* var_length() { return &var_length_; }
  TVariable<IntPtrT>* var_capacity() { return &var_capacity_; }

  void Reserve(TNode<IntPtrT> required_capacity);

  void Push(const TNode<Object> value);

  TNode<FixedArray> ToFixedArray();
  TNode<JSArray> ToJSArray(const TNode<Context> context);

 private:
  TNode<IntPtrT> NewCapacity(TNode<IntPtrT> current_capacity);

  // Creates a new array with {new_capacity} and copies the first
  // {element_count} elements from the current array.
  TNode<FixedArray> ResizeFixedArray(const TNode<IntPtrT> element_count,
                                     const TNode<IntPtrT> new_capacity);

 private:
  TVariable<FixedArray> var_array_;
  TVariable<IntPtrT> var_length_;
  TVariable<IntPtrT> var_capacity_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_GROWABLE_FIXED_ARRAY_GEN_H_
