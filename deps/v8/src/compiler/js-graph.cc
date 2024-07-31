// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-graph.h"

#include "src/codegen/code-factory.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

#define GET_CACHED_FIELD(ptr, expr) (*(ptr)) ? *(ptr) : (*(ptr) = (expr))

#define DEFINE_GETTER(name, Type, expr)                                  \
  TNode<Type> JSGraph::name() {                                          \
    return TNode<Type>::UncheckedCast(GET_CACHED_FIELD(&name##_, expr)); \
  }

Node* JSGraph::CEntryStubConstant(int result_size, ArgvMode argv_mode,
                                  bool builtin_exit_frame) {
  if (argv_mode == ArgvMode::kStack) {
    DCHECK(result_size >= 1 && result_size <= 3);
    if (!builtin_exit_frame) {
      Node** ptr = nullptr;
      if (result_size == 1) {
        ptr = &CEntryStub1Constant_;
      } else if (result_size == 2) {
        ptr = &CEntryStub2Constant_;
      } else {
        DCHECK_EQ(3, result_size);
        ptr = &CEntryStub3Constant_;
      }
      return GET_CACHED_FIELD(
          ptr, HeapConstantNoHole(CodeFactory::CEntry(
                   isolate(), result_size, argv_mode, builtin_exit_frame)));
    }
    Node** ptr = builtin_exit_frame ? &CEntryStub1WithBuiltinExitFrameConstant_
                                    : &CEntryStub1Constant_;
    return GET_CACHED_FIELD(
        ptr, HeapConstantNoHole(CodeFactory::CEntry(
                 isolate(), result_size, argv_mode, builtin_exit_frame)));
  }
  return HeapConstantNoHole(CodeFactory::CEntry(isolate(), result_size,
                                                argv_mode, builtin_exit_frame));
}

Node* JSGraph::ConstantNoHole(ObjectRef ref, JSHeapBroker* broker) {
  // This CHECK is security critical, we should never observe a hole
  // here.  Please do not remove this! (crbug.com/1486789)
  CHECK(ref.IsSmi() || ref.IsHeapNumber() ||
        ref.AsHeapObject().GetHeapObjectType(broker).hole_type() ==
            HoleType::kNone);
  return Constant(ref, broker);
}

Node* JSGraph::ConstantMaybeHole(ObjectRef ref, JSHeapBroker* broker) {
  return Constant(ref, broker);
}

Node* JSGraph::Constant(ObjectRef ref, JSHeapBroker* broker) {
  if (ref.IsSmi()) return ConstantMaybeHole(ref.AsSmi());
  if (ref.IsHeapNumber()) {
    return ConstantMaybeHole(ref.AsHeapNumber().value());
  }

  switch (ref.AsHeapObject().GetHeapObjectType(broker).hole_type()) {
    case HoleType::kNone:
      break;
    case HoleType::kGeneric:
      return TheHoleConstant();
    case HoleType::kPropertyCellHole:
      return PropertyCellHoleConstant();
    case HoleType::kHashTableHole:
      return HashTableHoleConstant();
    case HoleType::kPromiseHole:
      return PromiseHoleConstant();
    case HoleType::kOptimizedOut:
      return OptimizedOutConstant();
    case HoleType::kStaleRegister:
      return StaleRegisterConstant();
    case HoleType::kUninitialized:
      return UninitializedConstant();
    case HoleType::kException:
    case HoleType::kTerminationException:
    case HoleType::kArgumentsMarker:
    case HoleType::kSelfReferenceMarker:
    case HoleType::kBasicBlockCountersMarker:
      UNREACHABLE();
  }

  OddballType oddball_type =
      ref.AsHeapObject().GetHeapObjectType(broker).oddball_type();
  ReadOnlyRoots roots(isolate());
  if (oddball_type == OddballType::kUndefined) {
    DCHECK(IsUndefined(*ref.object(), roots));
    return UndefinedConstant();
  } else if (oddball_type == OddballType::kNull) {
    DCHECK(IsNull(*ref.object(), roots));
    return NullConstant();
  } else if (oddball_type == OddballType::kBoolean) {
    if (IsTrue(*ref.object(), roots)) {
      return TrueConstant();
    } else {
      DCHECK(IsFalse(*ref.object(), roots));
      return FalseConstant();
    }
  } else {
    return HeapConstantNoHole(ref.AsHeapObject().object());
  }
}

Node* JSGraph::ConstantNoHole(double value) {
  CHECK_NE(base::bit_cast<uint64_t>(value), kHoleNanInt64);
  return ConstantMaybeHole(value);
}

Node* JSGraph::ConstantMaybeHole(double value) {
  if (base::bit_cast<int64_t>(value) == base::bit_cast<int64_t>(0.0))
    return ZeroConstant();
  if (base::bit_cast<int64_t>(value) == base::bit_cast<int64_t>(1.0))
    return OneConstant();
  return NumberConstant(value);
}

Node* JSGraph::NumberConstant(double value) {
  Node** loc = cache_.FindNumberConstant(value);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->NumberConstant(value));
  }
  return *loc;
}

Node* JSGraph::HeapConstantNoHole(Handle<HeapObject> value) {
  CHECK(!IsAnyHole(*value));
  Node** loc = cache_.FindHeapConstant(value);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->HeapConstant(value));
  }
  return *loc;
}

Node* JSGraph::HeapConstantMaybeHole(Handle<HeapObject> value) {
  Node** loc = cache_.FindHeapConstant(value);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->HeapConstant(value));
  }
  return *loc;
}

Node* JSGraph::HeapConstantHole(Handle<HeapObject> value) {
  DCHECK(IsAnyHole(*value));
  Node** loc = cache_.FindHeapConstant(value);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->HeapConstant(value));
  }
  return *loc;
}

void JSGraph::GetCachedNodes(NodeVector* nodes) {
  cache_.GetCachedNodes(nodes);
#define DO_CACHED_FIELD(name, ...) \
  if (name##_) nodes->push_back(name##_);

  CACHED_GLOBAL_LIST(DO_CACHED_FIELD)
  CACHED_CENTRY_LIST(DO_CACHED_FIELD)
#undef DO_CACHED_FIELD
}

DEFINE_GETTER(AllocateInYoungGenerationStubConstant, Code,
              HeapConstantNoHole(BUILTIN_CODE(isolate(),
                                              AllocateInYoungGeneration)))

DEFINE_GETTER(AllocateInOldGenerationStubConstant, Code,
              HeapConstantNoHole(BUILTIN_CODE(isolate(),
                                              AllocateInOldGeneration)))

#if V8_ENABLE_WEBASSEMBLY
DEFINE_GETTER(WasmAllocateInYoungGenerationStubConstant, Code,
              HeapConstantNoHole(BUILTIN_CODE(isolate(),
                                              WasmAllocateInYoungGeneration)))

DEFINE_GETTER(WasmAllocateInOldGenerationStubConstant, Code,
              HeapConstantNoHole(BUILTIN_CODE(isolate(),
                                              WasmAllocateInOldGeneration)))
#endif

DEFINE_GETTER(ArrayConstructorStubConstant, Code,
              HeapConstantNoHole(BUILTIN_CODE(isolate(), ArrayConstructorImpl)))

DEFINE_GETTER(BigIntMapConstant, Map,
              HeapConstantNoHole(factory()->bigint_map()))

DEFINE_GETTER(BooleanMapConstant, Map,
              HeapConstantNoHole(factory()->boolean_map()))

DEFINE_GETTER(ToNumberBuiltinConstant, Code,
              HeapConstantNoHole(BUILTIN_CODE(isolate(), ToNumber)))

DEFINE_GETTER(PlainPrimitiveToNumberBuiltinConstant, Code,
              HeapConstantNoHole(BUILTIN_CODE(isolate(),
                                              PlainPrimitiveToNumber)))

DEFINE_GETTER(EmptyFixedArrayConstant, FixedArray,
              HeapConstantNoHole(factory()->empty_fixed_array()))

DEFINE_GETTER(EmptyStringConstant, String,
              HeapConstantNoHole(factory()->empty_string()))

DEFINE_GETTER(FixedArrayMapConstant, Map,
              HeapConstantNoHole(factory()->fixed_array_map()))

DEFINE_GETTER(PropertyArrayMapConstant, Map,
              HeapConstantNoHole(factory()->property_array_map()))

DEFINE_GETTER(FixedDoubleArrayMapConstant, Map,
              HeapConstantNoHole(factory()->fixed_double_array_map()))

DEFINE_GETTER(WeakFixedArrayMapConstant, Map,
              HeapConstantNoHole(factory()->weak_fixed_array_map()))

DEFINE_GETTER(HeapNumberMapConstant, Map,
              HeapConstantNoHole(factory()->heap_number_map()))

DEFINE_GETTER(UndefinedConstant, Undefined,
              HeapConstantNoHole(factory()->undefined_value()))

DEFINE_GETTER(TheHoleConstant, Hole,
              HeapConstantHole(factory()->the_hole_value()))

DEFINE_GETTER(PropertyCellHoleConstant, Hole,
              HeapConstantHole(factory()->property_cell_hole_value()))

DEFINE_GETTER(HashTableHoleConstant, Hole,
              HeapConstantHole(factory()->hash_table_hole_value()))

DEFINE_GETTER(PromiseHoleConstant, Hole,
              HeapConstantHole(factory()->promise_hole_value()))

DEFINE_GETTER(UninitializedConstant, Hole,
              HeapConstantHole(factory()->uninitialized_value()))

DEFINE_GETTER(OptimizedOutConstant, Hole,
              HeapConstantHole(factory()->optimized_out()))

DEFINE_GETTER(StaleRegisterConstant, Hole,
              HeapConstantHole(factory()->stale_register()))

DEFINE_GETTER(TrueConstant, True, HeapConstantNoHole(factory()->true_value()))

DEFINE_GETTER(FalseConstant, False,
              HeapConstantNoHole(factory()->false_value()))

DEFINE_GETTER(NullConstant, Null, HeapConstantNoHole(factory()->null_value()))

DEFINE_GETTER(ZeroConstant, Number, NumberConstant(0.0))

DEFINE_GETTER(MinusZeroConstant, Number, NumberConstant(-0.0))

DEFINE_GETTER(OneConstant, Number, NumberConstant(1.0))

DEFINE_GETTER(MinusOneConstant, Number, NumberConstant(-1.0))

DEFINE_GETTER(NaNConstant, Number,
              NumberConstant(std::numeric_limits<double>::quiet_NaN()))

DEFINE_GETTER(EmptyStateValues, UntaggedT,
              graph()->NewNode(common()->StateValues(0,
                                                     SparseInputMask::Dense())))

DEFINE_GETTER(
    SingleDeadTypedStateValues, UntaggedT,
    graph()->NewNode(common()->TypedStateValues(
        graph()->zone()->New<ZoneVector<MachineType>>(0, graph()->zone()),
        SparseInputMask(SparseInputMask::kEndMarker << 1))))

DEFINE_GETTER(ExternalObjectMapConstant, Map,
              HeapConstantNoHole(factory()->external_map()))

#undef DEFINE_GETTER
#undef GET_CACHED_FIELD

}  // namespace compiler
}  // namespace internal
}  // namespace v8
