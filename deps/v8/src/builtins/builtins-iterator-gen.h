// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_ITERATOR_GEN_H_
#define V8_BUILTINS_BUILTINS_ITERATOR_GEN_H_

#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {

class GrowableFixedArray;

class IteratorBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit IteratorBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  using IteratorRecord = TorqueStructIteratorRecord;

  // Returns object[Symbol.iterator].
  TNode<Object> GetIteratorMethod(TNode<Context> context, TNode<Object>);

  // https://tc39.github.io/ecma262/#sec-getiterator --- never used for
  // @@asyncIterator.
  IteratorRecord GetIterator(TNode<Context> context, TNode<Object> object);
  IteratorRecord GetIterator(TNode<Context> context, TNode<Object> object,
                             TNode<Object> method);

  // https://tc39.github.io/ecma262/#sec-iteratorstep
  // If the iterator is done, goto {if_done}, otherwise returns an iterator
  // result.
  // `fast_iterator_result_map` refers to the map for the JSIteratorResult
  // object, loaded from the native context.
  TNode<JSReceiver> IteratorStep(
      TNode<Context> context, const IteratorRecord& iterator, Label* if_done,
      base::Optional<TNode<Map>> fast_iterator_result_map = base::nullopt);
  TNode<JSReceiver> IteratorStep(
      TNode<Context> context, const IteratorRecord& iterator,
      base::Optional<TNode<Map>> fast_iterator_result_map, Label* if_done) {
    return IteratorStep(context, iterator, if_done, fast_iterator_result_map);
  }

  // https://tc39.es/ecma262/#sec-iteratorcomplete
  void IteratorComplete(
      TNode<Context> context, const TNode<HeapObject> iterator, Label* if_done,
      base::Optional<TNode<Map>> fast_iterator_result_map = base::nullopt);
  void IteratorComplete(TNode<Context> context,
                        const TNode<HeapObject> iterator,
                        base::Optional<TNode<Map>> fast_iterator_result_map,
                        Label* if_done) {
    return IteratorComplete(context, iterator, if_done,
                            fast_iterator_result_map);
  }

  // https://tc39.github.io/ecma262/#sec-iteratorvalue
  // Return the `value` field from an iterator.
  // `fast_iterator_result_map` refers to the map for the JSIteratorResult
  // object, loaded from the native context.
  TNode<Object> IteratorValue(
      TNode<Context> context, TNode<JSReceiver> result,
      base::Optional<TNode<Map>> fast_iterator_result_map = base::nullopt);

  void Iterate(TNode<Context> context, TNode<Object> iterable,
               std::function<void(TNode<Object>)> func,
               std::initializer_list<compiler::CodeAssemblerVariable*>
                   merged_variables = {});
  void Iterate(TNode<Context> context, TNode<Object> iterable,
               TNode<Object> iterable_fn,
               std::function<void(TNode<Object>)> func,
               std::initializer_list<compiler::CodeAssemblerVariable*>
                   merged_variables = {});

  // #sec-iterabletolist
  // Build a JSArray by iterating over {iterable} using {iterator_fn},
  // following the ECMAscript operation with the same name.
  TNode<JSArray> IterableToList(TNode<Context> context, TNode<Object> iterable,
                                TNode<Object> iterator_fn);

  TNode<FixedArray> IterableToFixedArray(TNode<Context> context,
                                         TNode<Object> iterable,
                                         TNode<Object> iterator_fn);

  void FillFixedArrayFromIterable(TNode<Context> context,
                                  TNode<Object> iterable,
                                  TNode<Object> iterator_fn,
                                  GrowableFixedArray* values);

  // Currently at https://tc39.github.io/proposal-intl-list-format/
  // #sec-createstringlistfromiterable
  TNode<FixedArray> StringListFromIterable(TNode<Context> context,
                                           TNode<Object> iterable);

  void FastIterableToList(TNode<Context> context, TNode<Object> iterable,
                          TVariable<JSArray>* var_result, Label* slow);
  TNode<JSArray> FastIterableToList(TNode<Context> context,
                                    TNode<Object> iterable, Label* slow);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_ITERATOR_GEN_H_
