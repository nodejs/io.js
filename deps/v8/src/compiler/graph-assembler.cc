// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph-assembler.h"

#include "src/base/container-utils.h"
#include "src/codegen/callable.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/tnode.h"
#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/linkage.h"
#include "src/compiler/type-cache.h"
// For TNode types.
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/objects/elements-kind.h"
#include "src/objects/heap-number.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer.h"
#include "src/objects/oddball.h"
#include "src/objects/string.h"

namespace v8 {
namespace internal {
namespace compiler {

class V8_NODISCARD GraphAssembler::BlockInlineReduction {
 public:
  explicit BlockInlineReduction(GraphAssembler* gasm) : gasm_(gasm) {
    DCHECK(!gasm_->inline_reductions_blocked_);
    gasm_->inline_reductions_blocked_ = true;
  }
  ~BlockInlineReduction() {
    DCHECK(gasm_->inline_reductions_blocked_);
    gasm_->inline_reductions_blocked_ = false;
  }

 private:
  GraphAssembler* gasm_;
};

GraphAssembler::GraphAssembler(
    MachineGraph* mcgraph, Zone* zone, BranchSemantics default_branch_semantics,
    base::Optional<NodeChangedCallback> node_changed_callback,
    bool mark_loop_exits)
    : temp_zone_(zone),
      mcgraph_(mcgraph),
      default_branch_semantics_(default_branch_semantics),
      effect_(nullptr),
      control_(nullptr),
      node_changed_callback_(node_changed_callback),
      inline_reducers_(zone),
      inline_reductions_blocked_(false),
      loop_headers_(zone),
      mark_loop_exits_(mark_loop_exits) {
  DCHECK_NE(default_branch_semantics_, BranchSemantics::kUnspecified);
}

GraphAssembler::~GraphAssembler() { DCHECK_EQ(loop_nesting_level_, 0); }

Node* GraphAssembler::IntPtrConstant(intptr_t value) {
  return AddClonedNode(mcgraph()->IntPtrConstant(value));
}

TNode<UintPtrT> GraphAssembler::UintPtrConstant(uintptr_t value) {
  return TNode<UintPtrT>::UncheckedCast(mcgraph()->UintPtrConstant(value));
}

Node* GraphAssembler::Int32Constant(int32_t value) {
  return AddClonedNode(mcgraph()->Int32Constant(value));
}

TNode<Uint32T> GraphAssembler::Uint32Constant(uint32_t value) {
  return TNode<Uint32T>::UncheckedCast(mcgraph()->Uint32Constant(value));
}

Node* GraphAssembler::Int64Constant(int64_t value) {
  return AddClonedNode(mcgraph()->Int64Constant(value));
}

Node* GraphAssembler::Uint64Constant(uint64_t value) {
  return AddClonedNode(mcgraph()->Uint64Constant(value));
}

Node* GraphAssembler::UniqueIntPtrConstant(intptr_t value) {
  return AddNode(graph()->NewNode(
      machine()->Is64()
          ? common()->Int64Constant(value)
          : common()->Int32Constant(static_cast<int32_t>(value))));
}

Node* JSGraphAssembler::SmiConstant(int32_t value) {
  return AddClonedNode(jsgraph()->SmiConstant(value));
}

Node* GraphAssembler::Float64Constant(double value) {
  return AddClonedNode(mcgraph()->Float64Constant(value));
}

TNode<HeapObject> JSGraphAssembler::HeapConstant(Handle<HeapObject> object) {
  return TNode<HeapObject>::UncheckedCast(
      AddClonedNode(jsgraph()->HeapConstantNoHole(object)));
}

TNode<Object> JSGraphAssembler::Constant(ObjectRef ref) {
  return TNode<Object>::UncheckedCast(
      AddClonedNode(jsgraph()->ConstantNoHole(ref, broker())));
}

TNode<Number> JSGraphAssembler::NumberConstant(double value) {
  return TNode<Number>::UncheckedCast(
      AddClonedNode(jsgraph()->ConstantNoHole(value)));
}

Node* GraphAssembler::ExternalConstant(ExternalReference ref) {
  return AddClonedNode(mcgraph()->ExternalConstant(ref));
}

Node* GraphAssembler::Parameter(int index) {
  return AddNode(
      graph()->NewNode(common()->Parameter(index), graph()->start()));
}

Node* JSGraphAssembler::CEntryStubConstant(int result_size) {
  return AddClonedNode(jsgraph()->CEntryStubConstant(result_size));
}

Node* GraphAssembler::LoadFramePointer() {
  return AddNode(graph()->NewNode(machine()->LoadFramePointer()));
}

#if V8_ENABLE_WEBASSEMBLY
Node* GraphAssembler::LoadStackPointer() {
  return AddNode(graph()->NewNode(machine()->LoadStackPointer(), effect()));
}

Node* GraphAssembler::SetStackPointer(Node* node,
                                      wasm::FPRelativeScope fp_scope) {
  return AddNode(
      graph()->NewNode(machine()->SetStackPointer(fp_scope), node, effect()));
}
#endif

Node* GraphAssembler::LoadHeapNumberValue(Node* heap_number) {
  return Load(MachineType::Float64(), heap_number,
              IntPtrConstant(offsetof(HeapNumber, value_) - kHeapObjectTag));
}

#define SINGLETON_CONST_DEF(Name, Type)              \
  TNode<Type> JSGraphAssembler::Name##Constant() {   \
    return TNode<Type>::UncheckedCast(               \
        AddClonedNode(jsgraph()->Name##Constant())); \
  }
JSGRAPH_SINGLETON_CONSTANT_LIST(SINGLETON_CONST_DEF)
#undef SINGLETON_CONST_DEF

#define SINGLETON_CONST_TEST_DEF(Name, ...)                        \
  TNode<Boolean> JSGraphAssembler::Is##Name(TNode<Object> value) { \
    return TNode<Boolean>::UncheckedCast(                          \
        ReferenceEqual(value, Name##Constant()));                  \
  }
JSGRAPH_SINGLETON_CONSTANT_LIST(SINGLETON_CONST_TEST_DEF)
#undef SINGLETON_CONST_TEST_DEF

#define PURE_UNOP_DEF(Name)                                     \
  Node* GraphAssembler::Name(Node* input) {                     \
    return AddNode(graph()->NewNode(machine()->Name(), input)); \
  }
PURE_ASSEMBLER_MACH_UNOP_LIST(PURE_UNOP_DEF)
#undef PURE_UNOP_DEF

#define PURE_BINOP_DEF(Name)                                          \
  Node* GraphAssembler::Name(Node* left, Node* right) {               \
    return AddNode(graph()->NewNode(machine()->Name(), left, right)); \
  }
#define PURE_BINOP_DEF_TNODE(Name, Result, Left, Right)                       \
  TNode<Result> GraphAssembler::Name(SloppyTNode<Left> left,                  \
                                     SloppyTNode<Right> right) {              \
    return AddNode<Result>(graph()->NewNode(machine()->Name(), left, right)); \
  }
PURE_ASSEMBLER_MACH_BINOP_LIST(PURE_BINOP_DEF, PURE_BINOP_DEF_TNODE)
#undef PURE_BINOP_DEF
#undef PURE_BINOP_DEF_TNODE

TNode<BoolT> GraphAssembler::UintPtrLessThan(TNode<UintPtrT> left,
                                             TNode<UintPtrT> right) {
  return kSystemPointerSize == 8
             ? Uint64LessThan(TNode<Uint64T>::UncheckedCast(left),
                              TNode<Uint64T>::UncheckedCast(right))
             : Uint32LessThan(TNode<Uint32T>::UncheckedCast(left),
                              TNode<Uint32T>::UncheckedCast(right));
}

TNode<BoolT> GraphAssembler::UintPtrLessThanOrEqual(TNode<UintPtrT> left,
                                                    TNode<UintPtrT> right) {
  return kSystemPointerSize == 8
             ? Uint64LessThanOrEqual(TNode<Uint64T>::UncheckedCast(left),
                                     TNode<Uint64T>::UncheckedCast(right))
             : Uint32LessThanOrEqual(TNode<Uint32T>::UncheckedCast(left),
                                     TNode<Uint32T>::UncheckedCast(right));
}

TNode<UintPtrT> GraphAssembler::UintPtrAdd(TNode<UintPtrT> left,
                                           TNode<UintPtrT> right) {
  return kSystemPointerSize == 8
             ? TNode<UintPtrT>::UncheckedCast(Int64Add(left, right))
             : TNode<UintPtrT>::UncheckedCast(Int32Add(left, right));
}
TNode<UintPtrT> GraphAssembler::UintPtrSub(TNode<UintPtrT> left,
                                           TNode<UintPtrT> right) {
  return kSystemPointerSize == 8
             ? TNode<UintPtrT>::UncheckedCast(Int64Sub(left, right))
             : TNode<UintPtrT>::UncheckedCast(Int32Sub(left, right));
}

TNode<UintPtrT> GraphAssembler::UintPtrDiv(TNode<UintPtrT> left,
                                           TNode<UintPtrT> right) {
  return kSystemPointerSize == 8
             ? TNode<UintPtrT>::UncheckedCast(Uint64Div(left, right))
             : TNode<UintPtrT>::UncheckedCast(Uint32Div(left, right));
}

TNode<UintPtrT> GraphAssembler::ChangeUint32ToUintPtr(
    SloppyTNode<Uint32T> value) {
  return kSystemPointerSize == 8
             ? TNode<UintPtrT>::UncheckedCast(ChangeUint32ToUint64(value))
             : TNode<UintPtrT>::UncheckedCast(value);
}

#define CHECKED_BINOP_DEF(Name)                                       \
  Node* GraphAssembler::Name(Node* left, Node* right) {               \
    return AddNode(                                                   \
        graph()->NewNode(machine()->Name(), left, right, control())); \
  }
CHECKED_ASSEMBLER_MACH_BINOP_LIST(CHECKED_BINOP_DEF)
#undef CHECKED_BINOP_DEF

Node* GraphAssembler::IntPtrEqual(Node* left, Node* right) {
  return WordEqual(left, right);
}

Node* GraphAssembler::TaggedEqual(Node* left, Node* right) {
  if (COMPRESS_POINTERS_BOOL) {
    return Word32Equal(left, right);
  } else {
    return WordEqual(left, right);
  }
}

Node* GraphAssembler::SmiSub(Node* left, Node* right) {
  if (COMPRESS_POINTERS_BOOL) {
    return BitcastWord32ToWord64(Int32Sub(left, right));
  } else {
    return IntSub(left, right);
  }
}

Node* GraphAssembler::SmiLessThan(Node* left, Node* right) {
  if (COMPRESS_POINTERS_BOOL) {
    return Int32LessThan(left, right);
  } else {
    return IntLessThan(left, right);
  }
}

Node* GraphAssembler::Float64RoundDown(Node* value) {
  CHECK(machine()->Float64RoundDown().IsSupported());
  return AddNode(graph()->NewNode(machine()->Float64RoundDown().op(), value));
}

Node* GraphAssembler::Float64RoundTruncate(Node* value) {
  CHECK(machine()->Float64RoundTruncate().IsSupported());
  return AddNode(
      graph()->NewNode(machine()->Float64RoundTruncate().op(), value));
}

Node* GraphAssembler::TruncateFloat64ToInt64(Node* value, TruncateKind kind) {
  return AddNode(
      graph()->NewNode(machine()->TruncateFloat64ToInt64(kind), value));
}

Node* GraphAssembler::Projection(int index, Node* value, Node* ctrl) {
  return AddNode(graph()->NewNode(common()->Projection(index), value,
                                  ctrl ? ctrl : control()));
}

Node* JSGraphAssembler::Allocate(AllocationType allocation, Node* size) {
  return AddNode(
      graph()->NewNode(simplified()->AllocateRaw(Type::Any(), allocation), size,
                       effect(), control()));
}

TNode<Map> JSGraphAssembler::LoadMap(TNode<HeapObject> object) {
  return TNode<Map>::UncheckedCast(LoadField(AccessBuilder::ForMap(), object));
}

Node* JSGraphAssembler::LoadField(FieldAccess const& access, Node* object) {
  Node* value = AddNode(graph()->NewNode(simplified()->LoadField(access),
                                         object, effect(), control()));
  return value;
}

TNode<Uint32T> JSGraphAssembler::LoadElementsKind(TNode<Map> map) {
  TNode<Uint8T> bit_field2 = EnterMachineGraph<Uint8T>(
      LoadField<Uint8T>(AccessBuilder::ForMapBitField2(), map),
      UseInfo::TruncatingWord32());
  return TNode<Uint32T>::UncheckedCast(
      Word32Shr(TNode<Word32T>::UncheckedCast(bit_field2),
                Uint32Constant(Map::Bits2::ElementsKindBits::kShift)));
}

Node* JSGraphAssembler::LoadElement(ElementAccess const& access, Node* object,
                                    Node* index) {
  Node* value = AddNode(graph()->NewNode(simplified()->LoadElement(access),
                                         object, index, effect(), control()));
  return value;
}

Node* JSGraphAssembler::StoreField(FieldAccess const& access, Node* object,
                                   Node* value) {
  return AddNode(graph()->NewNode(simplified()->StoreField(access), object,
                                  value, effect(), control()));
}

Node* JSGraphAssembler::ClearPendingMessage() {
  ExternalReference const ref =
      ExternalReference::address_of_pending_message(isolate());
  return AddNode(graph()->NewNode(
      simplified()->StoreMessage(), jsgraph()->ExternalConstant(ref),
      jsgraph()->TheHoleConstant(), effect(), control()));
}

#ifdef V8_MAP_PACKING
TNode<Map> GraphAssembler::UnpackMapWord(Node* map_word) {
  map_word = BitcastTaggedToWordForTagAndSmiBits(map_word);
  // TODO(wenyuzhao): Clear header metadata.
  Node* map = WordXor(map_word, IntPtrConstant(Internals::kMapWordXorMask));
  return TNode<Map>::UncheckedCast(BitcastWordToTagged(map));
}

Node* GraphAssembler::PackMapWord(TNode<Map> map) {
  Node* map_word = BitcastTaggedToWordForTagAndSmiBits(map);
  Node* packed = WordXor(map_word, IntPtrConstant(Internals::kMapWordXorMask));
  return BitcastWordToTaggedSigned(packed);
}
#endif

TNode<Map> GraphAssembler::LoadMap(Node* object) {
  Node* map_word = Load(MachineType::TaggedPointer(), object,
                        HeapObject::kMapOffset - kHeapObjectTag);
#ifdef V8_MAP_PACKING
  return UnpackMapWord(map_word);
#else
  return TNode<Map>::UncheckedCast(map_word);
#endif
}

Node* JSGraphAssembler::StoreElement(ElementAccess const& access, Node* object,
                                     Node* index, Node* value) {
  return AddNode(graph()->NewNode(simplified()->StoreElement(access), object,
                                  index, value, effect(), control()));
}

void JSGraphAssembler::TransitionAndStoreElement(MapRef double_map,
                                                 MapRef fast_map,
                                                 TNode<HeapObject> object,
                                                 TNode<Number> index,
                                                 TNode<Object> value) {
  AddNode(graph()->NewNode(
      simplified()->TransitionAndStoreElement(double_map, fast_map), object,
      index, value, effect(), control()));
}

TNode<Number> JSGraphAssembler::StringLength(TNode<String> string) {
  return AddNode<Number>(
      graph()->NewNode(simplified()->StringLength(), string));
}

TNode<Boolean> JSGraphAssembler::ReferenceEqual(TNode<Object> lhs,
                                                TNode<Object> rhs) {
  return AddNode<Boolean>(
      graph()->NewNode(simplified()->ReferenceEqual(), lhs, rhs));
}

TNode<Boolean> JSGraphAssembler::NumberEqual(TNode<Number> lhs,
                                             TNode<Number> rhs) {
  return AddNode<Boolean>(
      graph()->NewNode(simplified()->NumberEqual(), lhs, rhs));
}

TNode<Number> JSGraphAssembler::NumberMin(TNode<Number> lhs,
                                          TNode<Number> rhs) {
  return AddNode<Number>(graph()->NewNode(simplified()->NumberMin(), lhs, rhs));
}

TNode<Number> JSGraphAssembler::NumberMax(TNode<Number> lhs,
                                          TNode<Number> rhs) {
  return AddNode<Number>(graph()->NewNode(simplified()->NumberMax(), lhs, rhs));
}

TNode<Number> JSGraphAssembler::NumberAdd(TNode<Number> lhs,
                                          TNode<Number> rhs) {
  return AddNode<Number>(graph()->NewNode(simplified()->NumberAdd(), lhs, rhs));
}

TNode<Number> JSGraphAssembler::NumberSubtract(TNode<Number> lhs,
                                               TNode<Number> rhs) {
  return AddNode<Number>(
      graph()->NewNode(simplified()->NumberSubtract(), lhs, rhs));
}

TNode<Boolean> JSGraphAssembler::NumberLessThan(TNode<Number> lhs,
                                                TNode<Number> rhs) {
  return AddNode<Boolean>(
      graph()->NewNode(simplified()->NumberLessThan(), lhs, rhs));
}

TNode<Boolean> JSGraphAssembler::NumberLessThanOrEqual(TNode<Number> lhs,
                                                       TNode<Number> rhs) {
  return AddNode<Boolean>(
      graph()->NewNode(simplified()->NumberLessThanOrEqual(), lhs, rhs));
}

TNode<Number> JSGraphAssembler::NumberShiftRightLogical(TNode<Number> lhs,
                                                        TNode<Number> rhs) {
  return AddNode<Number>(
      graph()->NewNode(simplified()->NumberShiftRightLogical(), lhs, rhs));
}

TNode<Number> JSGraphAssembler::NumberBitwiseAnd(TNode<Number> lhs,
                                                 TNode<Number> rhs) {
  return AddNode<Number>(
      graph()->NewNode(simplified()->NumberBitwiseAnd(), lhs, rhs));
}

TNode<Number> JSGraphAssembler::NumberBitwiseOr(TNode<Number> lhs,
                                                TNode<Number> rhs) {
  return AddNode<Number>(
      graph()->NewNode(simplified()->NumberBitwiseOr(), lhs, rhs));
}

TNode<String> JSGraphAssembler::StringSubstring(TNode<String> string,
                                                TNode<Number> from,
                                                TNode<Number> to) {
  return AddNode<String>(graph()->NewNode(
      simplified()->StringSubstring(), string, from, to, effect(), control()));
}

TNode<Boolean> JSGraphAssembler::ObjectIsCallable(TNode<Object> value) {
  return AddNode<Boolean>(
      graph()->NewNode(simplified()->ObjectIsCallable(), value));
}

TNode<Boolean> JSGraphAssembler::ObjectIsSmi(TNode<Object> value) {
  return AddNode<Boolean>(graph()->NewNode(simplified()->ObjectIsSmi(), value));
}

TNode<Boolean> JSGraphAssembler::ObjectIsUndetectable(TNode<Object> value) {
  return AddNode<Boolean>(
      graph()->NewNode(simplified()->ObjectIsUndetectable(), value));
}

Node* JSGraphAssembler::CheckIf(Node* cond, DeoptimizeReason reason,
                                const FeedbackSource& feedback) {
  return AddNode(graph()->NewNode(simplified()->CheckIf(reason, feedback), cond,
                                  effect(), control()));
}

Node* JSGraphAssembler::Assert(Node* cond, const char* condition_string,
                               const char* file, int line) {
  return AddNode(graph()->NewNode(
      common()->Assert(BranchSemantics::kJS, condition_string, file, line),
      cond, effect(), control()));
}

TNode<Boolean> JSGraphAssembler::NumberIsFloat64Hole(TNode<Number> value) {
  return AddNode<Boolean>(
      graph()->NewNode(simplified()->NumberIsFloat64Hole(), value));
}

TNode<Boolean> JSGraphAssembler::ToBoolean(TNode<Object> value) {
  return AddNode<Boolean>(graph()->NewNode(simplified()->ToBoolean(), value));
}

TNode<Object> JSGraphAssembler::ConvertTaggedHoleToUndefined(
    TNode<Object> value) {
  return AddNode<Object>(
      graph()->NewNode(simplified()->ConvertTaggedHoleToUndefined(), value));
}

TNode<FixedArrayBase> JSGraphAssembler::MaybeGrowFastElements(
    ElementsKind kind, const FeedbackSource& feedback, TNode<JSArray> array,
    TNode<FixedArrayBase> elements, TNode<Number> index_needed,
    TNode<Number> old_length) {
  GrowFastElementsMode mode = IsDoubleElementsKind(kind)
                                  ? GrowFastElementsMode::kDoubleElements
                                  : GrowFastElementsMode::kSmiOrObjectElements;
  return AddNode<FixedArrayBase>(graph()->NewNode(
      simplified()->MaybeGrowFastElements(mode, feedback), array, elements,
      index_needed, old_length, effect(), control()));
}

TNode<Object> JSGraphAssembler::DoubleArrayMax(TNode<JSArray> array) {
  return AddNode<Object>(graph()->NewNode(simplified()->DoubleArrayMax(), array,
                                          effect(), control()));
}

TNode<Object> JSGraphAssembler::DoubleArrayMin(TNode<JSArray> array) {
  return AddNode<Object>(graph()->NewNode(simplified()->DoubleArrayMin(), array,
                                          effect(), control()));
}

Node* JSGraphAssembler::StringCharCodeAt(TNode<String> string,
                                         TNode<Number> position) {
  return AddNode(graph()->NewNode(simplified()->StringCharCodeAt(), string,
                                  position, effect(), control()));
}

TNode<String> JSGraphAssembler::StringFromSingleCharCode(TNode<Number> code) {
  return AddNode<String>(
      graph()->NewNode(simplified()->StringFromSingleCharCode(), code));
}

class ArrayBufferViewAccessBuilder {
 public:
  explicit ArrayBufferViewAccessBuilder(JSGraphAssembler* assembler,
                                        InstanceType instance_type,
                                        std::set<ElementsKind> candidates)
      : assembler_(assembler),
        instance_type_(instance_type),
        candidates_(std::move(candidates)) {
    DCHECK_NOT_NULL(assembler_);
    // TODO(v8:11111): Optimize for JS_RAB_GSAB_DATA_VIEW_TYPE too.
    DCHECK(instance_type_ == JS_DATA_VIEW_TYPE ||
           instance_type_ == JS_TYPED_ARRAY_TYPE);
  }

  bool maybe_rab_gsab() const {
    if (candidates_.empty()) return true;
    return !base::all_of(candidates_, [](auto e) {
      return !IsRabGsabTypedArrayElementsKind(e);
    });
  }

  base::Optional<int> TryComputeStaticElementShift() {
    DCHECK(instance_type_ != JS_RAB_GSAB_DATA_VIEW_TYPE);
    if (instance_type_ == JS_DATA_VIEW_TYPE) return 0;
    if (candidates_.empty()) return base::nullopt;
    int shift = ElementsKindToShiftSize(*candidates_.begin());
    if (!base::all_of(candidates_, [shift](auto e) {
          return ElementsKindToShiftSize(e) == shift;
        })) {
      return base::nullopt;
    }
    return shift;
  }

  base::Optional<int> TryComputeStaticElementSize() {
    DCHECK(instance_type_ != JS_RAB_GSAB_DATA_VIEW_TYPE);
    if (instance_type_ == JS_DATA_VIEW_TYPE) return 1;
    if (candidates_.empty()) return base::nullopt;
    int size = ElementsKindToByteSize(*candidates_.begin());
    if (!base::all_of(candidates_, [size](auto e) {
          return ElementsKindToByteSize(e) == size;
        })) {
      return base::nullopt;
    }
    return size;
  }

  TNode<UintPtrT> BuildLength(TNode<JSArrayBufferView> view,
                              TNode<Context> context) {
    auto& a = *assembler_;

    // Case 1: Normal (backed by AB/SAB) or non-length tracking backed by GSAB
    // (can't go oob once constructed)
    auto GsabFixedOrNormal = [&]() {
      return MachineLoadField<UintPtrT>(AccessBuilder::ForJSTypedArrayLength(),
                                        view, UseInfo::Word());
    };

    // If we statically know we cannot have rab/gsab backed, we can simply
    // load from the view.
    if (!maybe_rab_gsab()) {
      return GsabFixedOrNormal();
    }

    // Otherwise, we need to generate the checks for the view's bitfield.
    TNode<Word32T> bitfield = a.EnterMachineGraph<Word32T>(
        a.LoadField<Word32T>(AccessBuilder::ForJSArrayBufferViewBitField(),
                             view),
        UseInfo::TruncatingWord32());
    TNode<Word32T> length_tracking_bit = a.Word32And(
        bitfield, a.Uint32Constant(JSArrayBufferView::kIsLengthTracking));
    TNode<Word32T> backed_by_rab_bit = a.Word32And(
        bitfield, a.Uint32Constant(JSArrayBufferView::kIsBackedByRab));

    // Load the underlying buffer.
    TNode<HeapObject> buffer = a.LoadField<HeapObject>(
        AccessBuilder::ForJSArrayBufferViewBuffer(), view);

    // Compute the element size.
    TNode<Uint32T> element_size;
    if (auto size_opt = TryComputeStaticElementSize()) {
      element_size = a.Uint32Constant(*size_opt);
    } else {
      DCHECK_EQ(instance_type_, JS_TYPED_ARRAY_TYPE);
      TNode<Map> typed_array_map = a.LoadField<Map>(
          AccessBuilder::ForMap(WriteBarrierKind::kNoWriteBarrier), view);
      TNode<Uint32T> elements_kind = a.LoadElementsKind(typed_array_map);
      element_size = a.LookupByteSizeForElementsKind(elements_kind);
    }

    // 2) Fixed length backed by RAB (can go oob once constructed)
    auto RabFixed = [&]() {
      TNode<UintPtrT> unchecked_byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteLength(), view,
          UseInfo::Word());
      TNode<UintPtrT> underlying_byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferByteLength(), buffer, UseInfo::Word());
      TNode<UintPtrT> byte_offset = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteOffset(), view,
          UseInfo::Word());

      TNode<UintPtrT> byte_length =
          a
              .MachineSelectIf<UintPtrT>(a.UintPtrLessThanOrEqual(
                  a.UintPtrAdd(byte_offset, unchecked_byte_length),
                  underlying_byte_length))
              .Then([&]() { return unchecked_byte_length; })
              .Else([&]() { return a.UintPtrConstant(0); })
              .Value();
      return a.UintPtrDiv(byte_length, a.ChangeUint32ToUintPtr(element_size));
    };

    // 3) Length-tracking backed by RAB (JSArrayBuffer stores the length)
    auto RabTracking = [&]() {
      TNode<UintPtrT> byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferByteLength(), buffer, UseInfo::Word());
      TNode<UintPtrT> byte_offset = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteOffset(), view,
          UseInfo::Word());

      return a
          .MachineSelectIf<UintPtrT>(
              a.UintPtrLessThanOrEqual(byte_offset, byte_length))
          .Then([&]() {
            // length = floor((byte_length - byte_offset) / element_size)
            return a.UintPtrDiv(a.UintPtrSub(byte_length, byte_offset),
                                a.ChangeUint32ToUintPtr(element_size));
          })
          .Else([&]() { return a.UintPtrConstant(0); })
          .ExpectTrue()
          .Value();
    };

    // 4) Length-tracking backed by GSAB (BackingStore stores the length)
    auto GsabTracking = [&]() {
      TNode<Number> temp = TNode<Number>::UncheckedCast(a.TypeGuard(
          TypeCache::Get()->kJSArrayBufferViewByteLengthType,
          a.JSCallRuntime1(Runtime::kGrowableSharedArrayBufferByteLength,
                           buffer, context, base::nullopt,
                           Operator::kNoWrite)));
      TNode<UintPtrT> byte_length =
          a.EnterMachineGraph<UintPtrT>(temp, UseInfo::Word());
      TNode<UintPtrT> byte_offset = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteOffset(), view,
          UseInfo::Word());

      return a
          .MachineSelectIf<UintPtrT>(
              a.UintPtrLessThanOrEqual(byte_offset, byte_length))
          .Then([&]() {
            // length = floor((byte_length - byte_offset) / element_size)
            return a.UintPtrDiv(a.UintPtrSub(byte_length, byte_offset),
                                a.ChangeUint32ToUintPtr(element_size));
          })
          .Else([&]() { return a.UintPtrConstant(0); })
          .ExpectTrue()
          .Value();
    };

    return a.MachineSelectIf<UintPtrT>(length_tracking_bit)
        .Then([&]() {
          return a.MachineSelectIf<UintPtrT>(backed_by_rab_bit)
              .Then(RabTracking)
              .Else(GsabTracking)
              .Value();
        })
        .Else([&]() {
          return a.MachineSelectIf<UintPtrT>(backed_by_rab_bit)
              .Then(RabFixed)
              .Else(GsabFixedOrNormal)
              .Value();
        })
        .Value();
  }

  TNode<UintPtrT> BuildByteLength(TNode<JSArrayBufferView> view,
                                  TNode<Context> context) {
    auto& a = *assembler_;

    // Case 1: Normal (backed by AB/SAB) or non-length tracking backed by GSAB
    // (can't go oob once constructed)
    auto GsabFixedOrNormal = [&]() {
      return MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteLength(), view,
          UseInfo::Word());
    };

    // If we statically know we cannot have rab/gsab backed, we can simply
    // use load from the view.
    if (!maybe_rab_gsab()) {
      return GsabFixedOrNormal();
    }

    // Otherwise, we need to generate the checks for the view's bitfield.
    TNode<Word32T> bitfield = a.EnterMachineGraph<Word32T>(
        a.LoadField<Word32T>(AccessBuilder::ForJSArrayBufferViewBitField(),
                             view),
        UseInfo::TruncatingWord32());
    TNode<Word32T> length_tracking_bit = a.Word32And(
        bitfield, a.Uint32Constant(JSArrayBufferView::kIsLengthTracking));
    TNode<Word32T> backed_by_rab_bit = a.Word32And(
        bitfield, a.Uint32Constant(JSArrayBufferView::kIsBackedByRab));

    // Load the underlying buffer.
    TNode<HeapObject> buffer = a.LoadField<HeapObject>(
        AccessBuilder::ForJSArrayBufferViewBuffer(), view);

    // Case 2: Fixed length backed by RAB (can go oob once constructed)
    auto RabFixed = [&]() {
      TNode<UintPtrT> unchecked_byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteLength(), view,
          UseInfo::Word());
      TNode<UintPtrT> underlying_byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferByteLength(), buffer, UseInfo::Word());
      TNode<UintPtrT> byte_offset = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteOffset(), view,
          UseInfo::Word());

      return a
          .MachineSelectIf<UintPtrT>(a.UintPtrLessThanOrEqual(
              a.UintPtrAdd(byte_offset, unchecked_byte_length),
              underlying_byte_length))
          .Then([&]() { return unchecked_byte_length; })
          .Else([&]() { return a.UintPtrConstant(0); })
          .Value();
    };

    auto RoundDownToElementSize = [&](TNode<UintPtrT> byte_size) {
      if (auto shift_opt = TryComputeStaticElementShift()) {
        constexpr uintptr_t all_bits = static_cast<uintptr_t>(-1);
        if (*shift_opt == 0) return byte_size;
        return TNode<UintPtrT>::UncheckedCast(
            a.WordAnd(byte_size, a.UintPtrConstant(all_bits << (*shift_opt))));
      }
      DCHECK_EQ(instance_type_, JS_TYPED_ARRAY_TYPE);
      TNode<Map> typed_array_map = a.LoadField<Map>(
          AccessBuilder::ForMap(WriteBarrierKind::kNoWriteBarrier), view);
      TNode<Uint32T> elements_kind = a.LoadElementsKind(typed_array_map);
      TNode<Uint32T> element_shift =
          a.LookupByteShiftForElementsKind(elements_kind);
      return TNode<UintPtrT>::UncheckedCast(
          a.WordShl(a.WordShr(byte_size, element_shift), element_shift));
    };

    // Case 3: Length-tracking backed by RAB (JSArrayBuffer stores the length)
    auto RabTracking = [&]() {
      TNode<UintPtrT> byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferByteLength(), buffer, UseInfo::Word());
      TNode<UintPtrT> byte_offset = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteOffset(), view,
          UseInfo::Word());

      return a
          .MachineSelectIf<UintPtrT>(
              a.UintPtrLessThanOrEqual(byte_offset, byte_length))
          .Then([&]() {
            return RoundDownToElementSize(
                a.UintPtrSub(byte_length, byte_offset));
          })
          .Else([&]() { return a.UintPtrConstant(0); })
          .ExpectTrue()
          .Value();
    };

    // Case 4: Length-tracking backed by GSAB (BackingStore stores the length)
    auto GsabTracking = [&]() {
      TNode<Number> temp = TNode<Number>::UncheckedCast(a.TypeGuard(
          TypeCache::Get()->kJSArrayBufferViewByteLengthType,
          a.JSCallRuntime1(Runtime::kGrowableSharedArrayBufferByteLength,
                           buffer, context, base::nullopt,
                           Operator::kNoWrite)));
      TNode<UintPtrT> byte_length =
          a.EnterMachineGraph<UintPtrT>(temp, UseInfo::Word());
      TNode<UintPtrT> byte_offset = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteOffset(), view,
          UseInfo::Word());

      return a
          .MachineSelectIf<UintPtrT>(
              a.UintPtrLessThanOrEqual(byte_offset, byte_length))
          .Then([&]() {
            return RoundDownToElementSize(
                a.UintPtrSub(byte_length, byte_offset));
          })
          .Else([&]() { return a.UintPtrConstant(0); })
          .ExpectTrue()
          .Value();
    };

    return a.MachineSelectIf<UintPtrT>(length_tracking_bit)
        .Then([&]() {
          return a.MachineSelectIf<UintPtrT>(backed_by_rab_bit)
              .Then(RabTracking)
              .Else(GsabTracking)
              .Value();
        })
        .Else([&]() {
          return a.MachineSelectIf<UintPtrT>(backed_by_rab_bit)
              .Then(RabFixed)
              .Else(GsabFixedOrNormal)
              .Value();
        })
        .Value();
  }

  TNode<Word32T> BuildDetachedCheck(TNode<JSArrayBufferView> view) {
    auto& a = *assembler_;

    // Load the underlying buffer and its bitfield.
    TNode<HeapObject> buffer = a.LoadField<HeapObject>(
        AccessBuilder::ForJSArrayBufferViewBuffer(), view);
    TNode<Word32T> buffer_bit_field =
        MachineLoadField<Word32T>(AccessBuilder::ForJSArrayBufferBitField(),
                                  buffer, UseInfo::TruncatingWord32());
    // Mask the detached bit.
    TNode<Word32T> detached_bit =
        a.Word32And(buffer_bit_field,
                    a.Uint32Constant(JSArrayBuffer::WasDetachedBit::kMask));

    // If we statically know we cannot have rab/gsab backed, we are done here.
    if (!maybe_rab_gsab()) {
      return detached_bit;
    }

    // Otherwise, we need to generate the checks for the view's bitfield.
    TNode<Word32T> bitfield = a.EnterMachineGraph<Word32T>(
        a.LoadField<Word32T>(AccessBuilder::ForJSArrayBufferViewBitField(),
                             view),
        UseInfo::TruncatingWord32());
    TNode<Word32T> length_tracking_bit = a.Word32And(
        bitfield, a.Uint32Constant(JSArrayBufferView::kIsLengthTracking));
    TNode<Word32T> backed_by_rab_bit = a.Word32And(
        bitfield, a.Uint32Constant(JSArrayBufferView::kIsBackedByRab));

    auto RabLengthTracking = [&]() {
      TNode<UintPtrT> byte_offset = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteOffset(), view,
          UseInfo::Word());

      TNode<UintPtrT> underlying_byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferByteLength(), buffer, UseInfo::Word());

      return a.Word32Or(detached_bit,
                        a.UintPtrLessThan(underlying_byte_length, byte_offset));
    };

    auto RabFixed = [&]() {
      TNode<UintPtrT> unchecked_byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteLength(), view,
          UseInfo::Word());
      TNode<UintPtrT> byte_offset = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferViewByteOffset(), view,
          UseInfo::Word());

      TNode<UintPtrT> underlying_byte_length = MachineLoadField<UintPtrT>(
          AccessBuilder::ForJSArrayBufferByteLength(), buffer, UseInfo::Word());

      return a.Word32Or(
          detached_bit,
          a.UintPtrLessThan(underlying_byte_length,
                            a.UintPtrAdd(byte_offset, unchecked_byte_length)));
    };

    // Dispatch depending on rab/gsab and length tracking.
    return a.MachineSelectIf<Word32T>(backed_by_rab_bit)
        .Then([&]() {
          return a.MachineSelectIf<Word32T>(length_tracking_bit)
              .Then(RabLengthTracking)
              .Else(RabFixed)
              .Value();
        })
        .Else([&]() { return detached_bit; })
        .Value();
  }

 private:
  template <typename T>
  TNode<T> MachineLoadField(FieldAccess const& access, TNode<HeapObject> object,
                            const UseInfo& use_info) {
    return assembler_->EnterMachineGraph<T>(
        assembler_->LoadField<T>(access, object), use_info);
  }

  JSGraphAssembler* assembler_;
  InstanceType instance_type_;
  std::set<ElementsKind> candidates_;
};

TNode<Number> JSGraphAssembler::ArrayBufferViewByteLength(
    TNode<JSArrayBufferView> array_buffer_view, InstanceType instance_type,
    std::set<ElementsKind> elements_kinds_candidates, TNode<Context> context) {
  ArrayBufferViewAccessBuilder builder(this, instance_type,
                                       std::move(elements_kinds_candidates));
  return ExitMachineGraph<Number>(
      builder.BuildByteLength(array_buffer_view, context),
      MachineType::PointerRepresentation(),
      TypeCache::Get()->kJSArrayBufferByteLengthType);
}

TNode<Number> JSGraphAssembler::TypedArrayLength(
    TNode<JSTypedArray> typed_array,
    std::set<ElementsKind> elements_kinds_candidates, TNode<Context> context) {
  ArrayBufferViewAccessBuilder builder(this, JS_TYPED_ARRAY_TYPE,
                                       std::move(elements_kinds_candidates));
  return ExitMachineGraph<Number>(builder.BuildLength(typed_array, context),
                                  MachineType::PointerRepresentation(),
                                  TypeCache::Get()->kJSTypedArrayLengthType);
}

void JSGraphAssembler::CheckIfTypedArrayWasDetached(
    TNode<JSTypedArray> typed_array,
    std::set<ElementsKind> elements_kinds_candidates,
    const FeedbackSource& feedback) {
  ArrayBufferViewAccessBuilder builder(this, JS_TYPED_ARRAY_TYPE,
                                       std::move(elements_kinds_candidates));

  TNode<Word32T> detached_check = builder.BuildDetachedCheck(typed_array);
  TNode<Boolean> is_not_detached =
      ExitMachineGraph<Boolean>(Word32Equal(detached_check, Uint32Constant(0)),
                                MachineRepresentation::kBit, Type::Boolean());
  CheckIf(is_not_detached, DeoptimizeReason::kArrayBufferWasDetached, feedback);
}

TNode<Uint32T> JSGraphAssembler::LookupByteShiftForElementsKind(
    TNode<Uint32T> elements_kind) {
  TNode<UintPtrT> index = ChangeUint32ToUintPtr(Int32Sub(
      elements_kind, Uint32Constant(FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND)));
  TNode<RawPtrT> shift_table = TNode<RawPtrT>::UncheckedCast(ExternalConstant(
      ExternalReference::
          typed_array_and_rab_gsab_typed_array_elements_kind_shifts()));
  return TNode<Uint8T>::UncheckedCast(
      Load(MachineType::Uint8(), shift_table, index));
}

TNode<Uint32T> JSGraphAssembler::LookupByteSizeForElementsKind(
    TNode<Uint32T> elements_kind) {
  TNode<UintPtrT> index = ChangeUint32ToUintPtr(Int32Sub(
      elements_kind, Uint32Constant(FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND)));
  TNode<RawPtrT> size_table = TNode<RawPtrT>::UncheckedCast(ExternalConstant(
      ExternalReference::
          typed_array_and_rab_gsab_typed_array_elements_kind_sizes()));
  return TNode<Uint8T>::UncheckedCast(
      Load(MachineType::Uint8(), size_table, index));
}

TNode<Object> JSGraphAssembler::JSCallRuntime1(
    Runtime::FunctionId function_id, TNode<Object> arg0, TNode<Context> context,
    base::Optional<FrameState> frame_state, Operator::Properties properties) {
  return MayThrow([&]() {
    if (frame_state.has_value()) {
      return AddNode<Object>(graph()->NewNode(
          javascript()->CallRuntime(function_id, 1, properties), arg0, context,
          static_cast<Node*>(*frame_state), effect(), control()));
    } else {
      return AddNode<Object>(graph()->NewNode(
          javascript()->CallRuntime(function_id, 1, properties), arg0, context,
          effect(), control()));
    }
  });
}

TNode<Object> JSGraphAssembler::JSCallRuntime2(Runtime::FunctionId function_id,
                                               TNode<Object> arg0,
                                               TNode<Object> arg1,
                                               TNode<Context> context,
                                               FrameState frame_state) {
  return MayThrow([&]() {
    return AddNode<Object>(
        graph()->NewNode(javascript()->CallRuntime(function_id, 2), arg0, arg1,
                         context, frame_state, effect(), control()));
  });
}

Node* JSGraphAssembler::Chained(const Operator* op, Node* input) {
  DCHECK_EQ(op->ValueInputCount(), 1);
  return AddNode(
      graph()->NewNode(common()->Chained(op), input, effect(), control()));
}

Node* GraphAssembler::TypeGuard(Type type, Node* value) {
  return AddNode(
      graph()->NewNode(common()->TypeGuard(type), value, effect(), control()));
}

Node* GraphAssembler::Checkpoint(FrameState frame_state) {
  return AddNode(graph()->NewNode(common()->Checkpoint(), frame_state, effect(),
                                  control()));
}

Node* GraphAssembler::DebugBreak() {
  return AddNode(
      graph()->NewNode(machine()->DebugBreak(), effect(), control()));
}

Node* GraphAssembler::Unreachable() {
  Node* result = UnreachableWithoutConnectToEnd();
  ConnectUnreachableToEnd();
  InitializeEffectControl(nullptr, nullptr);
  return result;
}

Node* GraphAssembler::UnreachableWithoutConnectToEnd() {
  return AddNode(
      graph()->NewNode(common()->Unreachable(), effect(), control()));
}

TNode<RawPtrT> GraphAssembler::StackSlot(int size, int alignment,
                                         bool is_tagged) {
  return AddNode<RawPtrT>(
      graph()->NewNode(machine()->StackSlot(size, alignment, is_tagged)));
}

Node* GraphAssembler::AdaptLocalArgument(Node* argument) {
#ifdef V8_ENABLE_DIRECT_LOCAL
  // With direct locals, the argument can be passed directly.
  return BitcastTaggedToWord(argument);
#else
  // With indirect locals, the argument has to be stored on the stack and the
  // slot address is passed.
  Node* stack_slot = StackSlot(sizeof(uintptr_t), alignof(uintptr_t), true);
  Store(StoreRepresentation(MachineType::PointerRepresentation(),
                            kNoWriteBarrier),
        stack_slot, 0, BitcastTaggedToWord(argument));
  return stack_slot;
#endif
}

Node* GraphAssembler::Store(StoreRepresentation rep, Node* object, Node* offset,
                            Node* value) {
  return AddNode(graph()->NewNode(machine()->Store(rep), object, offset, value,
                                  effect(), control()));
}

Node* GraphAssembler::Store(StoreRepresentation rep, Node* object, int offset,
                            Node* value) {
  return Store(rep, object, IntPtrConstant(offset), value);
}

Node* GraphAssembler::Load(MachineType type, Node* object, Node* offset) {
  return AddNode(graph()->NewNode(machine()->Load(type), object, offset,
                                  effect(), control()));
}

Node* GraphAssembler::Load(MachineType type, Node* object, int offset) {
  return Load(type, object, IntPtrConstant(offset));
}

Node* GraphAssembler::StoreUnaligned(MachineRepresentation rep, Node* object,
                                     Node* offset, Node* value) {
  Operator const* const op =
      (rep == MachineRepresentation::kWord8 ||
       machine()->UnalignedStoreSupported(rep))
          ? machine()->Store(StoreRepresentation(rep, kNoWriteBarrier))
          : machine()->UnalignedStore(rep);
  return AddNode(
      graph()->NewNode(op, object, offset, value, effect(), control()));
}

Node* GraphAssembler::LoadUnaligned(MachineType type, Node* object,
                                    Node* offset) {
  Operator const* const op =
      (type.representation() == MachineRepresentation::kWord8 ||
       machine()->UnalignedLoadSupported(type.representation()))
          ? machine()->Load(type)
          : machine()->UnalignedLoad(type);
  return AddNode(graph()->NewNode(op, object, offset, effect(), control()));
}

Node* GraphAssembler::ProtectedStore(MachineRepresentation rep, Node* object,
                                     Node* offset, Node* value) {
  return AddNode(graph()->NewNode(machine()->ProtectedStore(rep), object,
                                  offset, value, effect(), control()));
}

Node* GraphAssembler::ProtectedLoad(MachineType type, Node* object,
                                    Node* offset) {
  return AddNode(graph()->NewNode(machine()->ProtectedLoad(type), object,
                                  offset, effect(), control()));
}

Node* GraphAssembler::LoadTrapOnNull(MachineType type, Node* object,
                                     Node* offset) {
  return AddNode(graph()->NewNode(machine()->LoadTrapOnNull(type), object,
                                  offset, effect(), control()));
}

Node* GraphAssembler::StoreTrapOnNull(StoreRepresentation rep, Node* object,
                                      Node* offset, Node* value) {
  return AddNode(graph()->NewNode(machine()->StoreTrapOnNull(rep), object,
                                  offset, value, effect(), control()));
}

Node* GraphAssembler::Retain(Node* buffer) {
  return AddNode(graph()->NewNode(common()->Retain(), buffer, effect()));
}

Node* GraphAssembler::IntPtrAdd(Node* a, Node* b) {
  return AddNode(graph()->NewNode(
      machine()->Is64() ? machine()->Int64Add() : machine()->Int32Add(), a, b));
}

Node* GraphAssembler::IntPtrSub(Node* a, Node* b) {
  return AddNode(graph()->NewNode(
      machine()->Is64() ? machine()->Int64Sub() : machine()->Int32Sub(), a, b));
}

TNode<Number> JSGraphAssembler::PlainPrimitiveToNumber(TNode<Object> value) {
  return AddNode<Number>(graph()->NewNode(
      PlainPrimitiveToNumberOperator(), PlainPrimitiveToNumberBuiltinConstant(),
      value, effect()));
}

Node* GraphAssembler::BitcastWordToTaggedSigned(Node* value) {
  return AddNode(
      graph()->NewNode(machine()->BitcastWordToTaggedSigned(), value));
}

Node* GraphAssembler::BitcastWordToTagged(Node* value) {
  return AddNode(graph()->NewNode(machine()->BitcastWordToTagged(), value,
                                  effect(), control()));
}

Node* GraphAssembler::BitcastTaggedToWord(Node* value) {
  return AddNode(graph()->NewNode(machine()->BitcastTaggedToWord(), value,
                                  effect(), control()));
}

Node* GraphAssembler::BitcastTaggedToWordForTagAndSmiBits(Node* value) {
  return AddNode(graph()->NewNode(
      machine()->BitcastTaggedToWordForTagAndSmiBits(), value));
}

Node* GraphAssembler::BitcastMaybeObjectToWord(Node* value) {
  return AddNode(graph()->NewNode(machine()->BitcastMaybeObjectToWord(), value,
                                  effect(), control()));
}

Node* GraphAssembler::DeoptimizeIf(DeoptimizeReason reason,
                                   FeedbackSource const& feedback,
                                   Node* condition, Node* frame_state) {
  return AddNode(graph()->NewNode(common()->DeoptimizeIf(reason, feedback),
                                  condition, frame_state, effect(), control()));
}

Node* GraphAssembler::DeoptimizeIfNot(DeoptimizeReason reason,
                                      FeedbackSource const& feedback,
                                      Node* condition, Node* frame_state) {
  return AddNode(graph()->NewNode(common()->DeoptimizeUnless(reason, feedback),
                                  condition, frame_state, effect(), control()));
}

TNode<Object> GraphAssembler::Call(const CallDescriptor* call_descriptor,
                                   int inputs_size, Node** inputs) {
  return Call(common()->Call(call_descriptor), inputs_size, inputs);
}

TNode<Object> GraphAssembler::Call(const Operator* op, int inputs_size,
                                   Node** inputs) {
  DCHECK_EQ(IrOpcode::kCall, op->opcode());
  return AddNode<Object>(graph()->NewNode(op, inputs_size, inputs));
}

void GraphAssembler::TailCall(const CallDescriptor* call_descriptor,
                              int inputs_size, Node** inputs) {
#ifdef DEBUG
  static constexpr int kTargetEffectControl = 3;
  DCHECK_EQ(inputs_size,
            call_descriptor->ParameterCount() + kTargetEffectControl);
#endif  // DEBUG

  Node* node = AddNode(graph()->NewNode(common()->TailCall(call_descriptor),
                                        inputs_size, inputs));

  // Unlike ConnectUnreachableToEnd, the TailCall node terminates a block; to
  // keep it live, it *must* be connected to End (also in Turboprop schedules).
  NodeProperties::MergeControlToEnd(graph(), common(), node);

  // Setting effect, control to nullptr effectively terminates the current block
  // by disallowing the addition of new nodes until a new label has been bound.
  InitializeEffectControl(nullptr, nullptr);
}

void GraphAssembler::BranchWithCriticalSafetyCheck(
    Node* condition, GraphAssemblerLabel<0u>* if_true,
    GraphAssemblerLabel<0u>* if_false) {
  BranchHint hint = BranchHint::kNone;
  if (if_true->IsDeferred() != if_false->IsDeferred()) {
    hint = if_false->IsDeferred() ? BranchHint::kTrue : BranchHint::kFalse;
  }

  BranchImpl(default_branch_semantics_, condition, if_true, if_false, hint);
}

void GraphAssembler::ConnectUnreachableToEnd() {
  DCHECK_EQ(effect()->opcode(), IrOpcode::kUnreachable);
  Node* throw_node = graph()->NewNode(common()->Throw(), effect(), control());
  NodeProperties::MergeControlToEnd(graph(), common(), throw_node);
  if (node_changed_callback_.has_value()) {
    (*node_changed_callback_)(graph()->end());
  }
  effect_ = control_ = mcgraph()->Dead();
}

Node* GraphAssembler::AddClonedNode(Node* node) {
  DCHECK(node->op()->HasProperty(Operator::kPure));
  UpdateEffectControlWith(node);
  return node;
}

Node* GraphAssembler::AddNode(Node* node) {
  if (!inline_reducers_.empty() && !inline_reductions_blocked_) {
    // Reducers may add new nodes to the graph using this graph assembler,
    // however they should never introduce nodes that need further reduction,
    // so block reduction
    BlockInlineReduction scope(this);
    Reduction reduction;
    for (auto reducer : inline_reducers_) {
      reduction = reducer->Reduce(node, nullptr);
      if (reduction.Changed()) break;
    }
    if (reduction.Changed()) {
      Node* replacement = reduction.replacement();
      if (replacement != node) {
        // Replace all uses of node and kill the node to make sure we don't
        // leave dangling dead uses.
        NodeProperties::ReplaceUses(node, replacement, effect(), control());
        node->Kill();
        return replacement;
      }
    }
  }

  if (node->opcode() == IrOpcode::kTerminate) {
    return node;
  }

  UpdateEffectControlWith(node);
  return node;
}

void GraphAssembler::Reset() {
  effect_ = nullptr;
  control_ = nullptr;
}

void GraphAssembler::InitializeEffectControl(Node* effect, Node* control) {
  effect_ = effect;
  control_ = control;
}

Operator const* JSGraphAssembler::PlainPrimitiveToNumberOperator() {
  if (!to_number_operator_.is_set()) {
    Callable callable =
        Builtins::CallableFor(isolate(), Builtin::kPlainPrimitiveToNumber);
    CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
    auto call_descriptor = Linkage::GetStubCallDescriptor(
        graph()->zone(), callable.descriptor(),
        callable.descriptor().GetStackParameterCount(), flags,
        Operator::kEliminatable);
    to_number_operator_.set(common()->Call(call_descriptor));
  }
  return to_number_operator_.get();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
