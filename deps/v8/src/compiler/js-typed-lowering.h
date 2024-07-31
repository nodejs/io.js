// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_TYPED_LOWERING_H_
#define V8_COMPILER_JS_TYPED_LOWERING_H_

#include "src/base/compiler-specific.h"
#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Factory;

namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class CompilationDependencies;
class JSGraph;
class JSOperatorBuilder;
class SimplifiedOperatorBuilder;
class TypeCache;

enum Signedness { kSigned, kUnsigned };

// Lowers JS-level operators to simplified operators based on types.
class V8_EXPORT_PRIVATE JSTypedLowering final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  JSTypedLowering(Editor* editor, JSGraph* jsgraph, JSHeapBroker* broker,
                  Zone* zone);
  ~JSTypedLowering() final = default;

  const char* reducer_name() const override { return "JSTypedLowering"; }

  Reduction Reduce(Node* node) final;

 private:
  friend class JSBinopReduction;

  Reduction ReduceJSAdd(Node* node);
  Reduction ReduceJSBitwiseNot(Node* node);
  Reduction ReduceJSDecrement(Node* node);
  Reduction ReduceJSIncrement(Node* node);
  Reduction ReduceJSNegate(Node* node);
  Reduction ReduceJSComparison(Node* node);
  Reduction ReduceJSLoadNamed(Node* node);
  Reduction ReduceJSHasInPrototypeChain(Node* node);
  Reduction ReduceJSOrdinaryHasInstance(Node* node);
  Reduction ReduceJSHasContextExtension(Node* node);
  Reduction ReduceJSLoadContext(Node* node);
  Reduction ReduceJSStoreContext(Node* node);
  Reduction ReduceJSLoadModule(Node* node);
  Reduction ReduceJSStoreModule(Node* node);
  Reduction ReduceJSEqual(Node* node);
  Reduction ReduceJSStrictEqual(Node* node);
  Reduction ReduceJSToLength(Node* node);
  Reduction ReduceJSToName(Node* node);
  Reduction ReduceJSToNumberInput(Node* input);
  Reduction ReduceJSToNumber(Node* node);
  Reduction ReduceJSToBigInt(Node* node);
  Reduction ReduceJSToBigIntConvertNumber(Node* node);
  Reduction ReduceJSToNumeric(Node* node);
  Reduction ReduceJSToStringInput(Node* input);
  Reduction ReduceJSToString(Node* node);
  Reduction ReduceJSToObject(Node* node);
  Reduction ReduceJSConstructForwardVarargs(Node* node);
  Reduction ReduceJSConstruct(Node* node);
  Reduction ReduceJSCallForwardVarargs(Node* node);
  Reduction ReduceJSCall(Node* node);
  Reduction ReduceJSForInNext(Node* node);
  Reduction ReduceJSForInPrepare(Node* node);
  Reduction ReduceJSLoadMessage(Node* node);
  Reduction ReduceJSStoreMessage(Node* node);
  Reduction ReduceJSGeneratorStore(Node* node);
  Reduction ReduceJSGeneratorRestoreContinuation(Node* node);
  Reduction ReduceJSGeneratorRestoreContext(Node* node);
  Reduction ReduceJSGeneratorRestoreRegister(Node* node);
  Reduction ReduceJSGeneratorRestoreInputOrDebugPos(Node* node);
  Reduction ReduceNumberBinop(Node* node);
  Reduction ReduceInt32Binop(Node* node);
  Reduction ReduceUI32Shift(Node* node, Signedness signedness);
  Reduction ReduceObjectIsArray(Node* node);
  Reduction ReduceJSParseInt(Node* node);
  Reduction ReduceJSResolvePromise(Node* node);

  // Helper for ReduceJSLoadModule and ReduceJSStoreModule.
  Node* BuildGetModuleCell(Node* node);

  // Helpers for ReduceJSAdd.
  Reduction GenerateStringAddition(Node* node, Node* left, Node* right,
                                   Node* context, Node* frame_state,
                                   Node** effect, Node** control,
                                   bool should_create_cons_string);
  Node* UnwrapStringWrapper(Node* string_or_wrapper, Node** effect,
                            Node** control);

  Factory* factory() const;
  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  JSHeapBroker* broker() const { return broker_; }
  CompilationDependencies* dependencies() const;
  Isolate* isolate() const;
  JSOperatorBuilder* javascript() const;
  CommonOperatorBuilder* common() const;
  SimplifiedOperatorBuilder* simplified() const;

  JSGraph* jsgraph_;
  JSHeapBroker* broker_;
  Type empty_string_type_;
  Type pointer_comparable_type_;
  TypeCache const* type_cache_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_TYPED_LOWERING_H_
