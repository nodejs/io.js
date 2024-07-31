// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TYPED_OPTIMIZATION_H_
#define V8_COMPILER_TYPED_OPTIMIZATION_H_

#include "src/base/compiler-specific.h"
#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Factory;
class Isolate;

namespace compiler {

// Forward declarations.
class CompilationDependencies;
class JSGraph;
class SimplifiedOperatorBuilder;
class TypeCache;

class V8_EXPORT_PRIVATE TypedOptimization final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  TypedOptimization(Editor* editor, CompilationDependencies* dependencies,
                    JSGraph* jsgraph, JSHeapBroker* broker);
  ~TypedOptimization() override;
  TypedOptimization(const TypedOptimization&) = delete;
  TypedOptimization& operator=(const TypedOptimization&) = delete;

  const char* reducer_name() const override { return "TypedOptimization"; }

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceConvertReceiver(Node* node);
  Reduction ReduceMaybeGrowFastElements(Node* node);
  Reduction ReduceCheckBounds(Node* node);
  Reduction ReduceCheckHeapObject(Node* node);
  Reduction ReduceCheckMaps(Node* node);
  Reduction ReduceCheckNumber(Node* node);
  Reduction ReduceCheckString(Node* node);
  Reduction ReduceCheckStringOrStringWrapper(Node* node);
  Reduction ReduceCheckEqualsInternalizedString(Node* node);
  Reduction ReduceCheckEqualsSymbol(Node* node);
  Reduction ReduceLoadField(Node* node);
  Reduction ReduceNumberFloor(Node* node);
  Reduction ReduceNumberRoundop(Node* node);
  Reduction ReduceNumberSilenceNaN(Node* node);
  Reduction ReduceNumberToUint8Clamped(Node* node);
  Reduction ReducePhi(Node* node);
  Reduction ReduceReferenceEqual(Node* node);
  Reduction ReduceStringComparison(Node* node);
  Reduction ReduceStringLength(Node* node);
  Reduction ReduceSameValue(Node* node);
  Reduction ReduceSelect(Node* node);
  Reduction ReduceSpeculativeToNumber(Node* node);
  Reduction ReduceCheckNotTaggedHole(Node* node);
  Reduction ReduceTypeOf(Node* node);
  Reduction ReduceToBoolean(Node* node);
  Reduction ReduceSpeculativeNumberAdd(Node* node);
  Reduction ReduceSpeculativeNumberMultiply(Node* node);
  Reduction ReduceSpeculativeNumberPow(Node* node);
  Reduction ReduceSpeculativeNumberBinop(Node* node);
  Reduction ReduceSpeculativeNumberComparison(Node* node);

  Reduction TryReduceStringComparisonOfStringFromSingleCharCode(
      Node* comparison, Node* from_char_code, Type constant_type,
      bool inverted);
  Reduction TryReduceStringComparisonOfStringFromSingleCharCodeToConstant(
      Node* comparison, StringRef string, bool inverted);
  const Operator* NumberComparisonFor(const Operator* op);

  Node* ConvertPlainPrimitiveToNumber(Node* node);
  Reduction ReduceJSToNumberInput(Node* input);

  SimplifiedOperatorBuilder* simplified() const;
  Factory* factory() const;
  Graph* graph() const;

  CompilationDependencies* dependencies() const { return dependencies_; }
  JSGraph* jsgraph() const { return jsgraph_; }
  JSHeapBroker* broker() const { return broker_; }

  CompilationDependencies* const dependencies_;
  JSGraph* const jsgraph_;
  JSHeapBroker* broker_;
  Type const true_type_;
  Type const false_type_;
  TypeCache const* type_cache_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TYPED_OPTIMIZATION_H_
