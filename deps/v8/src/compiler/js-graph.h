// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_GRAPH_H_
#define V8_COMPILER_JS_GRAPH_H_

#include "src/common/globals.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/machine-graph.h"
#include "src/execution/isolate.h"
#include "src/objects/oddball.h"

namespace v8 {
namespace internal {
namespace compiler {

class SimplifiedOperatorBuilder;
class Typer;

// Implements a facade on a Graph, enhancing the graph with JS-specific
// notions, including various builders for operators, canonicalized global
// constants, and various helper methods.
class V8_EXPORT_PRIVATE JSGraph : public MachineGraph {
 public:
  JSGraph(Isolate* isolate, Graph* graph, CommonOperatorBuilder* common,
          JSOperatorBuilder* javascript, SimplifiedOperatorBuilder* simplified,
          MachineOperatorBuilder* machine)
      : MachineGraph(graph, common, machine),
        isolate_(isolate),
        javascript_(javascript),
        simplified_(simplified) {}

  JSGraph(const JSGraph&) = delete;
  JSGraph& operator=(const JSGraph&) = delete;

  // CEntryStubs are cached depending on the result size and other flags.
  Node* CEntryStubConstant(int result_size,
                           ArgvMode argv_mode = ArgvMode::kStack,
                           bool builtin_exit_frame = false);

  // Used for padding frames. (alias: the hole)
  TNode<Hole> PaddingConstant() { return TheHoleConstant(); }

  // Used for stubs and runtime functions with no context. (alias: SMI zero)
  TNode<Number> NoContextConstant() { return ZeroConstant(); }

  // Creates a HeapConstant node, possibly canonicalized.
  // Checks that we don't emit hole values. Use this if possible to emit
  // JSReceiver heap constants.
  Node* HeapConstantNoHole(Handle<HeapObject> value);

  // Creates a HeapConstant node, possibly canonicalized.
  // This can be used whenever we might need to emit a hole value or a
  // JSReceiver. Use this cautiously only if you really need it.
  Node* HeapConstantMaybeHole(Handle<HeapObject> value);

  // Creates a HeapConstant node, possibly canonicalized.
  // This is only used to emit hole values. Use this if you are sure that you
  // only emit a Hole value.
  Node* HeapConstantHole(Handle<HeapObject> value);

  // Creates a Constant node of the appropriate type for
  // the given object.  Inspect the (serialized) object and determine whether
  // one of the canonicalized globals or a number constant should be returned.
  // Checks that we do not emit a Hole value, use this whenever possible.
  Node* ConstantNoHole(ObjectRef ref, JSHeapBroker* broker);
  // Creates a Constant node of the appropriate type for
  // the given object.  Inspect the (serialized) object and determine whether
  // one of the canonicalized globals or a number constant should be returned.
  // Use this if you really need to emit Hole values.
  Node* ConstantMaybeHole(ObjectRef ref, JSHeapBroker* broker);

  // Creates a NumberConstant node, usually canonicalized.
  Node* ConstantMaybeHole(double value);
  // Same, but checks that we are not emitting a kHoleNanInt64, please use
  // whenever you can.
  Node* ConstantNoHole(double value);

  // Creates a HeapConstant node for either true or false.
  TNode<Boolean> BooleanConstant(bool is_true) {
    return is_true ? TNode<Boolean>(TrueConstant())
                   : TNode<Boolean>(FalseConstant());
  }

  Node* SmiConstant(int32_t immediate) {
    DCHECK(Smi::IsValid(immediate));
    return ConstantMaybeHole(immediate);
  }

  JSOperatorBuilder* javascript() const { return javascript_; }
  SimplifiedOperatorBuilder* simplified() const { return simplified_; }
  Isolate* isolate() const { return isolate_; }
  Factory* factory() const { return isolate()->factory(); }

  // Adds all the cached nodes to the given list.
  void GetCachedNodes(NodeVector* nodes);

// Cached global nodes.
#define CACHED_GLOBAL_LIST(V)                                 \
  V(AllocateInYoungGenerationStubConstant, Code)              \
  V(AllocateInOldGenerationStubConstant, Code)                \
  IF_WASM(V, WasmAllocateInYoungGenerationStubConstant, Code) \
  IF_WASM(V, WasmAllocateInOldGenerationStubConstant, Code)   \
  V(ArrayConstructorStubConstant, Code)                       \
  V(BigIntMapConstant, Map)                                   \
  V(BooleanMapConstant, Map)                                  \
  V(ToNumberBuiltinConstant, Code)                            \
  V(PlainPrimitiveToNumberBuiltinConstant, Code)              \
  V(EmptyFixedArrayConstant, FixedArray)                      \
  V(EmptyStringConstant, String)                              \
  V(FixedArrayMapConstant, Map)                               \
  V(PropertyArrayMapConstant, Map)                            \
  V(FixedDoubleArrayMapConstant, Map)                         \
  V(WeakFixedArrayMapConstant, Map)                           \
  V(HeapNumberMapConstant, Map)                               \
  V(UndefinedConstant, Undefined)                             \
  V(TheHoleConstant, Hole)                                    \
  V(PropertyCellHoleConstant, Hole)                           \
  V(HashTableHoleConstant, Hole)                              \
  V(PromiseHoleConstant, Hole)                                \
  V(UninitializedConstant, Hole)                              \
  V(OptimizedOutConstant, Hole)                               \
  V(StaleRegisterConstant, Hole)                              \
  V(TrueConstant, True)                                       \
  V(FalseConstant, False)                                     \
  V(NullConstant, Null)                                       \
  V(ZeroConstant, Number)                                     \
  V(MinusZeroConstant, Number)                                \
  V(OneConstant, Number)                                      \
  V(MinusOneConstant, Number)                                 \
  V(NaNConstant, Number)                                      \
  V(EmptyStateValues, UntaggedT)                              \
  V(SingleDeadTypedStateValues, UntaggedT)                    \
  V(ExternalObjectMapConstant, Map)

// Cached global node accessor methods.
#define DECLARE_GETTER(name, Type) TNode<Type> name();
  CACHED_GLOBAL_LIST(DECLARE_GETTER)
#undef DECLARE_GETTER

 private:
  Isolate* isolate_;
  JSOperatorBuilder* javascript_;
  SimplifiedOperatorBuilder* simplified_;

#define CACHED_CENTRY_LIST(V) \
  V(CEntryStub1Constant)      \
  V(CEntryStub2Constant)      \
  V(CEntryStub3Constant)      \
  V(CEntryStub1WithBuiltinExitFrameConstant)

// Canonicalized global node fields.
#define DECLARE_FIELD(name, ...) Node* name##_ = nullptr;
  CACHED_GLOBAL_LIST(DECLARE_FIELD)
  CACHED_CENTRY_LIST(DECLARE_FIELD)
#undef DECLARE_FIELD

  // Internal helper to canonicalize a number constant.
  Node* NumberConstant(double value);

  // Internal helper that creates a Constant node of the appropriate type for
  // the given object.  Inspect the (serialized) object and determine whether
  // one of the canonicalized globals or a number constant should be returned.
  Node* Constant(ObjectRef value, JSHeapBroker* broker);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_GRAPH_H_
