// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_CALL_REDUCER_H_
#define V8_COMPILER_JS_CALL_REDUCER_H_

#include "src/base/flags.h"
#include "src/compiler/globals.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/node-properties.h"
#include "src/deoptimizer/deoptimize-reason.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Factory;
class JSGlobalProxy;

namespace compiler {

// Forward declarations.
class CallFrequency;
class CommonOperatorBuilder;
class CompilationDependencies;
struct FeedbackSource;
struct FieldAccess;
class JSCallReducerAssembler;
class JSGraph;
class JSHeapBroker;
class JSOperatorBuilder;
class MapInference;
class NodeProperties;
class SimplifiedOperatorBuilder;

// Performs strength reduction on {JSConstruct} and {JSCall} nodes,
// which might allow inlining or other optimizations to be performed afterwards.
class V8_EXPORT_PRIVATE JSCallReducer final : public AdvancedReducer {
 public:
  // Flags that control the mode of operation.
  enum Flag {
    kNoFlags = 0u,
    kBailoutOnUninitialized = 1u << 0,
    kInlineJSToWasmCalls = 1u << 1,
  };
  using Flags = base::Flags<Flag>;

  JSCallReducer(Editor* editor, JSGraph* jsgraph, JSHeapBroker* broker,
                Zone* temp_zone, Flags flags)
      : AdvancedReducer(editor),
        jsgraph_(jsgraph),
        broker_(broker),
        temp_zone_(temp_zone),
        flags_(flags) {}

  // Max string length for inlining entire match sequence for
  // String.prototype.startsWith in JSCallReducer.
  static constexpr int kMaxInlineMatchSequence = 3;

  const char* reducer_name() const override { return "JSCallReducer"; }

  Reduction Reduce(Node* node) final;

  // Processes the waitlist gathered while the reducer was running,
  // and does a final attempt to reduce the nodes in the waitlist.
  void Finalize() final;

  // JSCallReducer outsources much work to a graph assembler.
  void RevisitForGraphAssembler(Node* node) { Revisit(node); }
  Zone* ZoneForGraphAssembler() const { return temp_zone(); }
  JSGraph* JSGraphForGraphAssembler() const { return jsgraph(); }

  bool has_wasm_calls() const { return has_wasm_calls_; }
  const wasm::WasmModule* wasm_module_for_inlining() const {
    return wasm_module_for_inlining_;
  }

  CompilationDependencies* dependencies() const;
  JSHeapBroker* broker() const { return broker_; }

 private:
  Reduction ReduceBooleanConstructor(Node* node);
  Reduction ReduceCallApiFunction(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceCallWasmFunction(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceFunctionPrototypeApply(Node* node);
  Reduction ReduceFunctionPrototypeBind(Node* node);
  Reduction ReduceFunctionPrototypeCall(Node* node);
  Reduction ReduceFunctionPrototypeHasInstance(Node* node);
  Reduction ReduceObjectConstructor(Node* node);
  Reduction ReduceObjectGetPrototype(Node* node, Node* object);
  Reduction ReduceObjectGetPrototypeOf(Node* node);
  Reduction ReduceObjectIs(Node* node);
  Reduction ReduceObjectPrototypeGetProto(Node* node);
  Reduction ReduceObjectPrototypeHasOwnProperty(Node* node);
  Reduction ReduceObjectPrototypeIsPrototypeOf(Node* node);
  Reduction ReduceObjectCreate(Node* node);
  Reduction ReduceReflectApply(Node* node);
  Reduction ReduceReflectConstruct(Node* node);
  Reduction ReduceReflectGet(Node* node);
  Reduction ReduceReflectGetPrototypeOf(Node* node);
  Reduction ReduceReflectHas(Node* node);

  Reduction ReduceArrayConstructor(Node* node);
  Reduction ReduceArrayEvery(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceArrayFilter(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceArrayFindIndex(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceArrayFind(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceArrayForEach(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceArrayIncludes(Node* node);
  Reduction ReduceArrayIndexOf(Node* node);
  Reduction ReduceArrayIsArray(Node* node);
  Reduction ReduceArrayMap(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceArrayPrototypeAt(Node* node);
  Reduction ReduceArrayPrototypePop(Node* node);
  Reduction ReduceArrayPrototypePush(Node* node);
  Reduction ReduceArrayPrototypeShift(Node* node);
  Reduction ReduceArrayPrototypeSlice(Node* node);
  Reduction ReduceArrayReduce(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceArrayReduceRight(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceArraySome(Node* node, SharedFunctionInfoRef shared);

  enum class ArrayIteratorKind { kArrayLike, kTypedArray };
  Reduction ReduceArrayIterator(Node* node, ArrayIteratorKind array_kind,
                                IterationKind iteration_kind);
  Reduction ReduceArrayIteratorPrototypeNext(Node* node);
  Reduction ReduceFastArrayIteratorNext(InstanceType type, Node* node,
                                        IterationKind kind);

  Reduction ReduceCallOrConstructWithArrayLikeOrSpreadOfCreateArguments(
      Node* node, Node* arguments_list, int arraylike_or_spread_index,
      CallFrequency const& frequency, FeedbackSource const& feedback,
      SpeculationMode speculation_mode, CallFeedbackRelation feedback_relation);
  Reduction ReduceCallOrConstructWithArrayLikeOrSpread(
      Node* node, int argument_count, int arraylike_or_spread_index,
      CallFrequency const& frequency, FeedbackSource const& feedback_source,
      SpeculationMode speculation_mode, CallFeedbackRelation feedback_relation,
      Node* target, Effect effect, Control control);
  Reduction ReduceJSConstruct(Node* node);
  Reduction ReduceJSConstructWithArrayLike(Node* node);
  Reduction ReduceJSConstructWithSpread(Node* node);
  Reduction ReduceJSConstructForwardAllArgs(Node* node);
  Reduction ReduceJSCall(Node* node);
  Reduction ReduceJSCall(Node* node, SharedFunctionInfoRef shared);
  Reduction ReduceJSCallWithArrayLike(Node* node);
  Reduction ReduceJSCallWithSpread(Node* node);
  Reduction ReduceRegExpPrototypeTest(Node* node);
  Reduction ReduceReturnReceiver(Node* node);

  enum class StringIndexOfIncludesVariant { kIncludes, kIndexOf };
  Reduction ReduceStringPrototypeIndexOfIncludes(
      Node* node, StringIndexOfIncludesVariant variant);
  Reduction ReduceStringPrototypeSubstring(Node* node);
  Reduction ReduceStringPrototypeSlice(Node* node);
  Reduction ReduceStringPrototypeSubstr(Node* node);
  Reduction ReduceStringPrototypeStringAt(
      const Operator* string_access_operator, Node* node);
  Reduction ReduceStringPrototypeCharAt(Node* node);
  Reduction ReduceStringPrototypeStartsWith(Node* node);
  Reduction ReduceStringPrototypeEndsWith(Node* node);

#ifdef V8_INTL_SUPPORT
  Reduction ReduceStringPrototypeToLowerCaseIntl(Node* node);
  Reduction ReduceStringPrototypeToUpperCaseIntl(Node* node);
#endif  // V8_INTL_SUPPORT

  Reduction ReduceStringFromCharCode(Node* node);
  Reduction ReduceStringFromCodePoint(Node* node);
  Reduction ReduceStringPrototypeIterator(Node* node);
  Reduction ReduceStringPrototypeLocaleCompare(Node* node);
  Reduction ReduceStringIteratorPrototypeNext(Node* node);
  Reduction ReduceStringPrototypeConcat(Node* node);

  Reduction ReducePromiseConstructor(Node* node);
  Reduction ReducePromiseInternalConstructor(Node* node);
  Reduction ReducePromiseInternalReject(Node* node);
  Reduction ReducePromiseInternalResolve(Node* node);
  Reduction ReducePromisePrototypeCatch(Node* node);
  Reduction ReducePromisePrototypeFinally(Node* node);
  Reduction ReducePromisePrototypeThen(Node* node);
  Reduction ReducePromiseResolveTrampoline(Node* node);

  Reduction ReduceTypedArrayConstructor(Node* node,
                                        SharedFunctionInfoRef shared);
  Reduction ReduceTypedArrayPrototypeToStringTag(Node* node);
  Reduction ReduceArrayBufferViewByteLengthAccessor(Node* node,
                                                    InstanceType instance_type);
  Reduction ReduceTypedArrayPrototypeLength(Node* node);

  Reduction ReduceForInsufficientFeedback(Node* node, DeoptimizeReason reason);

  Reduction ReduceMathUnary(Node* node, const Operator* op);
  Reduction ReduceMathBinary(Node* node, const Operator* op);
  Reduction ReduceMathImul(Node* node);
  Reduction ReduceMathClz32(Node* node);
  Reduction ReduceMathMinMax(Node* node, const Operator* op, Node* empty_value);

  Reduction ReduceNumberIsFinite(Node* node);
  Reduction ReduceNumberIsInteger(Node* node);
  Reduction ReduceNumberIsSafeInteger(Node* node);
  Reduction ReduceNumberIsNaN(Node* node);

  Reduction ReduceGlobalIsFinite(Node* node);
  Reduction ReduceGlobalIsNaN(Node* node);

  Reduction ReduceMapPrototypeHas(Node* node);
  Reduction ReduceMapPrototypeGet(Node* node);
  Reduction ReduceSetPrototypeHas(Node* node);
  Reduction ReduceCollectionPrototypeHas(Node* node,
                                         CollectionKind collection_kind);
  Reduction ReduceCollectionIteration(Node* node,
                                      CollectionKind collection_kind,
                                      IterationKind iteration_kind);
  Reduction ReduceCollectionPrototypeSize(Node* node,
                                          CollectionKind collection_kind);
  Reduction ReduceCollectionIteratorPrototypeNext(
      Node* node, int entry_size, Handle<HeapObject> empty_collection,
      InstanceType collection_iterator_instance_type_first,
      InstanceType collection_iterator_instance_type_last);

  Reduction ReduceArrayBufferIsView(Node* node);
  Reduction ReduceArrayBufferViewAccessor(Node* node,
                                          InstanceType instance_type,
                                          FieldAccess const& access,
                                          Builtin builtin);

  enum class DataViewAccess { kGet, kSet };
  Reduction ReduceDataViewAccess(Node* node, DataViewAccess access,
                                 ExternalArrayType element_type);

  Reduction ReduceDatePrototypeGetTime(Node* node);
  Reduction ReduceDateNow(Node* node);
  Reduction ReduceNumberParseInt(Node* node);

  Reduction ReduceNumberConstructor(Node* node);
  Reduction ReduceBigIntConstructor(Node* node);
  Reduction ReduceBigIntAsN(Node* node, Builtin builtin);

  base::Optional<Reduction> TryReduceJSCallMathMinMaxWithArrayLike(Node* node);
  Reduction ReduceJSCallMathMinMaxWithArrayLike(Node* node, Builtin builtin);

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  Reduction ReduceGetContinuationPreservedEmbedderData(Node* node);
  Reduction ReduceSetContinuationPreservedEmbedderData(Node* node);
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

  // The pendant to ReplaceWithValue when using GraphAssembler-based reductions.
  Reduction ReplaceWithSubgraph(JSCallReducerAssembler* gasm, Node* subgraph);
  std::pair<Node*, Node*> ReleaseEffectAndControlFromAssembler(
      JSCallReducerAssembler* gasm);

  // Helper to verify promise receiver maps are as expected.
  // On bailout from a reduction, be sure to return inference.NoChange().
  bool DoPromiseChecks(MapInference* inference);

  Node* CreateClosureFromBuiltinSharedFunctionInfo(SharedFunctionInfoRef shared,
                                                   Node* context, Node* effect,
                                                   Node* control);

  void CheckIfElementsKind(Node* receiver_elements_kind, ElementsKind kind,
                           Node* control, Node** if_true, Node** if_false);
  Node* LoadReceiverElementsKind(Node* receiver, Effect* effect,
                                 Control control);

  bool IsBuiltinOrApiFunction(JSFunctionRef target_ref) const;

  // Check whether an array has the expected length. Returns the new effect.
  Node* CheckArrayLength(Node* array, ElementsKind elements_kind,
                         uint32_t array_length,
                         const FeedbackSource& feedback_source, Effect effect,
                         Control control);

  // Check whether the given new target value is a constructor function.
  void CheckIfConstructor(Node* call);

  Node* ConvertHoleToUndefined(Node* value, ElementsKind elements_kind);

  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Zone* temp_zone() const { return temp_zone_; }
  Isolate* isolate() const;
  Factory* factory() const;
  NativeContextRef native_context() const;
  CommonOperatorBuilder* common() const;
  JSOperatorBuilder* javascript() const;
  SimplifiedOperatorBuilder* simplified() const;
  Flags flags() const { return flags_; }

  JSGraph* const jsgraph_;
  JSHeapBroker* const broker_;
  Zone* const temp_zone_;
  Flags const flags_;
  std::set<Node*> waitlist_;

  // For preventing infinite recursion via ReduceJSCallWithArrayLikeOrSpread.
  std::unordered_set<Node*> generated_calls_with_array_like_or_spread_;

  bool has_wasm_calls_ = false;
  const wasm::WasmModule* wasm_module_for_inlining_ = nullptr;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_CALL_REDUCER_H_
