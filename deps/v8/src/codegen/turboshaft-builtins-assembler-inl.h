// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_TURBOSHAFT_BUILTINS_ASSEMBLER_INL_H_
#define V8_CODEGEN_TURBOSHAFT_BUILTINS_ASSEMBLER_INL_H_

#include <iterator>

#include "src/common/globals.h"
#include "src/compiler/turboshaft/access-builder.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/machine-lowering-reducer-inl.h"
#include "src/compiler/turboshaft/operation-matcher.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/objects/elements-kind.h"

#define DEFINE_TURBOSHAFT_ALIASES()                                            \
  template <typename T>                                                        \
  using V = compiler::turboshaft::V<T>;                                        \
  template <typename T>                                                        \
  using ConstOrV = compiler::turboshaft::ConstOrV<T>;                          \
  template <typename T>                                                        \
  using OptionalV = compiler::turboshaft::OptionalV<T>;                        \
  template <typename... Ts>                                                    \
  using Label = compiler::turboshaft::Label<Ts...>;                            \
  template <typename... Ts>                                                    \
  using LoopLabel = compiler::turboshaft::LoopLabel<Ts...>;                    \
  using Block = compiler::turboshaft::Block;                                   \
  using OpIndex = compiler::turboshaft::OpIndex;                               \
  using Word32 = compiler::turboshaft::Word32;                                 \
  using Word64 = compiler::turboshaft::Word64;                                 \
  using WordPtr = compiler::turboshaft::WordPtr;                               \
  using Float32 = compiler::turboshaft::Float32;                               \
  using Float64 = compiler::turboshaft::Float64;                               \
  using RegisterRepresentation = compiler::turboshaft::RegisterRepresentation; \
  using MemoryRepresentation = compiler::turboshaft::MemoryRepresentation;     \
  using BuiltinCallDescriptor = compiler::turboshaft::BuiltinCallDescriptor;   \
  using AccessBuilderTS = compiler::turboshaft::AccessBuilderTS;

#define BUILTIN_REDUCER(name)          \
  TURBOSHAFT_REDUCER_BOILERPLATE(name) \
  DEFINE_TURBOSHAFT_ALIASES()

namespace v8::internal {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

namespace detail {

// TODO(nicohartmann): Rename once CSA is (mostly) gone.
template <typename Assembler>
class BuiltinArgumentsTS {
 public:
  DEFINE_TURBOSHAFT_ALIASES()

  auto& Asm() const { return *assembler_; }

  // |argc| specifies the number of arguments passed to the builtin.
  template <typename T>
  BuiltinArgumentsTS(Assembler* assembler, V<T> argc,
                     OptionalV<WordPtr> fp = {})
      : assembler_(assembler) {
    if constexpr (std::is_same_v<T, WordPtr>) {
      argc_ = argc;
    } else {
      if constexpr (std::is_same_v<T, Word32>) {
        DCHECK((std::is_same_v<WordPtr, Word64>));
        argc_ = assembler_->ChangeInt32ToInt64(argc);
      } else {
        static_assert(std::is_same_v<T, Word64>);
        DCHECK((std::is_same_v<WordPtr, Word32>));
        argc_ = assembler_->TruncateWord64ToWord32(argc);
      }
    }
    if (fp.has_value()) {
      fp_ = fp.value();
    } else {
      fp_ = __ FramePointer();
    }
    const intptr_t offset =
        (StandardFrameConstants::kFixedSlotCountAboveFp + 1) *
        kSystemPointerSize;
    // base_ points to the first argument, not the receiver
    // whether present or not.
    base_ = __ WordPtrAdd(fp_, offset);
  }

  V<WordPtr> GetLengthWithReceiver() const { return argc_; }

  V<WordPtr> GetLengthWithoutReceiver() const {
    return __ WordPtrSub(argc_, kJSArgcReceiverSlots);
  }

  V<Object> AtIndex(ConstOrV<WordPtr> index) const {
    TSA_DCHECK(this, __ UintPtrLessThan(index, GetLengthWithoutReceiver()));
    return V<Object>::Cast(
        __ LoadOffHeap(AtIndexPtr(index), MemoryRepresentation::AnyTagged()));
  }

  class Iterator {
   public:
    using iterator_type = V<WordPtr>;
    using value_type = V<Object>;

    // {end} is the iterator-typical exclusive one past the last element.
    Iterator(const BuiltinArgumentsTS* args, ConstOrV<WordPtr> begin_index,
             ConstOrV<WordPtr> end_index)
        : args_(args), begin_index_(begin_index), end_index_(end_index) {}

    template <typename A>
    iterator_type Begin(A& assembler) {
      DCHECK(!end_offset_.valid());
      // Pre-compute the end offset.
      end_offset_ = args_->AtIndexPtr(assembler.resolve(end_index_));
      return args_->AtIndexPtr(assembler.resolve(begin_index_));
    }

    template <typename A>
    OptionalV<Word32> IsEnd(A& assembler,
                            iterator_type current_iterator) const {
      return assembler.UintPtrLessThanOrEqual(end_offset_, current_iterator);
    }

    template <typename A>
    iterator_type Advance(A& assembler, iterator_type current_iterator) const {
      return assembler.WordPtrAdd(
          current_iterator, ElementsKindToByteSize(SYSTEM_POINTER_ELEMENTS));
    }

    template <typename A>
    value_type Dereference(A& assembler, iterator_type current_iterator) const {
      return V<Object>::Cast(assembler.LoadOffHeap(
          current_iterator, MemoryRepresentation::AnyTagged()));
    }

   private:
    const BuiltinArgumentsTS* args_;
    ConstOrV<WordPtr> begin_index_;
    ConstOrV<WordPtr> end_index_;
    V<WordPtr> end_offset_;
  };

  Iterator Range(ConstOrV<WordPtr> begin, ConstOrV<WordPtr> end) const {
    return Iterator(this, begin, end);
  }

  Iterator Range(ConstOrV<WordPtr> begin) const {
    return Iterator(this, begin, GetLengthWithoutReceiver());
  }

  Iterator Range() const {
    return Iterator(this, 0, GetLengthWithoutReceiver());
  }

 private:
  V<WordPtr> AtIndexPtr(ConstOrV<WordPtr> index) const {
    V<WordPtr> offset =
        assembler_->ElementOffsetFromIndex(index, SYSTEM_POINTER_ELEMENTS, 0);
    return __ WordPtrAdd(base_, offset);
  }

  Assembler* assembler_;
  V<WordPtr> argc_;
  V<WordPtr> fp_;
  V<WordPtr> base_;
};

}  // namespace detail

class FeedbackCollector {};

template <typename Next>
class FeedbackCollectorReducer : public Next {
 public:
  BUILTIN_REDUCER(FeedbackCollector)

  void CombineFeedback(FeedbackCollector* feedback, int additional_feedback) {
    if (feedback == nullptr) return;
    UNIMPLEMENTED();
  }

  void OverwriteFeedback(FeedbackCollector* feedback, int new_feedback) {
    if (feedback == nullptr) return;
    UNIMPLEMENTED();
  }

  V<Word32> FeedbackIs(FeedbackCollector* feedback, int checked_feedback) {
    DCHECK_NOT_NULL(feedback);
    UNIMPLEMENTED();
  }
};

template <typename Next>
class NoFeedbackCollectorReducer : public Next {
 public:
  BUILTIN_REDUCER(NoFeedbackCollector)

  void CombineFeedback(FeedbackCollector* feedback, int additional_feedback) {
    DCHECK_NULL(feedback);
  }

  void OverwriteFeedback(FeedbackCollector* feedback, int new_feedback) {
    DCHECK_NULL(feedback);
  }

  V<Word32> FeedbackIs(FeedbackCollector* feedback, int checked_feedback) {
    UNREACHABLE();
  }
};

template <typename Next>
class BuiltinsReducer : public Next {
 public:
  BUILTIN_REDUCER(Builtins)

  using BuiltinArgumentsTS = detail::BuiltinArgumentsTS<assembler_t>;

  void EmitBuiltinProlog(Builtin builtin_id) {
    // Bind the entry block.
    __ Bind(__ NewBlock());
    // Eagerly emit all parameters such that they are guaranteed to be in the
    // entry block (assembler will cache them).
    const compiler::CallDescriptor* desc =
        __ data() -> builtin_call_descriptor();
    for (int i = 0; i < static_cast<int>(desc->ParameterCount()); ++i) {
      __ Parameter(i, RegisterRepresentation::FromMachineType(
                          desc->GetParameterType(i)));
    }
    // TODO(nicohartmann): CSA tracks some debug information here.
    // Emit stack check.
    if (Builtins::KindOf(builtin_id) == Builtins::TSJ) {
      __ PerformStackCheck(__ JSContextParameter());
    }
  }

  V<Context> JSContextParameter() {
    return __ template Parameter<Context>(
        compiler::Linkage::GetJSCallContextParamIndex(static_cast<int>(
            __ data()->builtin_call_descriptor()->JSParameterCount())));
  }

  void PerformStackCheck(V<Context> context) {
    __ JSStackCheck(context,
                    OptionalV<compiler::turboshaft::FrameState>::Nullopt(),
                    compiler::turboshaft::JSStackCheckOp::Kind::kBuiltinEntry);
  }

  void PopAndReturn(BuiltinArgumentsTS& arguments,
                    compiler::turboshaft::V<Object> return_value) {
    // PopAndReturn is supposed to be using ONLY in CSA/Torque builtins for
    // dropping ALL JS arguments that are currently located on the stack.
    // The check below ensures that there are no directly accessible stack
    // parameters from current builtin, which implies that the builtin with
    // JS calling convention (TFJ) was created with kDontAdaptArgumentsSentinel.
    // This simplifies semantics of this instruction because in case of presence
    // of directly accessible stack parameters it's impossible to distinguish
    // the following cases:
    // 1) stack parameter is included in JS arguments (and therefore it will be
    //    dropped as a part of 'pop' number of arguments),
    // 2) stack parameter is NOT included in JS arguments (and therefore it
    // should
    //    be dropped in ADDITION to the 'pop' number of arguments).
    // Additionally, in order to simplify assembly code, PopAndReturn is also
    // not allowed in builtins with stub linkage and parameters on stack.
    CHECK_EQ(__ data()->builtin_call_descriptor()->ParameterSlotCount(), 0);
    V<WordPtr> pop_count = arguments.GetLengthWithReceiver();
    std::initializer_list<const OpIndex> temp{return_value};
    __ Return(__ TruncateWordPtrToWord32(pop_count), base::VectorOf(temp));
  }

  V<Word32> TruncateTaggedToWord32(V<Context> context, V<Object> value) {
    Label<Word32> is_number(this);
    TaggedToWord32OrBigIntImpl<Object::Conversion::kToNumber>(
        context, value, IsKnownTaggedPointer::kNo, is_number);

    BIND(is_number, number);
    return number;
  }

  enum IsKnownTaggedPointer { kNo, kYes };
  class FeedbackValues {};

  V<Word32> IsBigIntInstanceType(V<Word32> instance_type) { UNIMPLEMENTED(); }
  V<Word32> IsSmallBigInt(V<BigInt> value) { UNIMPLEMENTED(); }
  V<Word32> InstanceTypeEqual(ConstOrV<Word32> instance_type,
                              ConstOrV<Word32> other_instance_type) {
    return __ Word32Equal(instance_type, other_instance_type);
  }

  V<WordPtr> AlignTagged(V<WordPtr> size) {
    return __ WordPtrBitwiseAnd(__ WordPtrAdd(size, kObjectAlignmentMask),
                                ~kObjectAlignmentMask);
  }

  V<WordPtr> ElementOffsetFromIndex(ConstOrV<WordPtr> index, ElementsKind kind,
                                    intptr_t base_size) {
    const int element_size_shift = ElementsKindToShiftSize(kind);
    if (std::optional<intptr_t> constant_index = TryToIntPtrConstant(index)) {
      return __ WordPtrConstant(base_size +
                                (1 << element_size_shift) * (*constant_index));
    }
    if (element_size_shift == 0) {
      return __ WordPtrAdd(base_size, index);
    } else {
      DCHECK_LT(0, element_size_shift);
      return __ WordPtrAdd(base_size,
                           __ WordPtrShiftLeft(index, element_size_shift));
    }
  }

  std::optional<intptr_t> TryToIntPtrConstant(ConstOrV<WordPtr> index) {
    if (index.is_constant()) return index.constant_value();
    intptr_t value;
    if (matcher_.MatchIntegralWordPtrConstant(index.value(), &value)) {
      return value;
    }
    return std::nullopt;
  }

  template <Object::Conversion Conversion>
  void TaggedToWord32OrBigIntImpl(V<Context> context, V<Object> value,
                                  IsKnownTaggedPointer is_known_tagged_pointer,
                                  Label<Word32>& if_number,
                                  Label<BigInt>* if_bigint = nullptr,
                                  Label<BigInt>* if_bigint64 = nullptr,
                                  FeedbackCollector* feedback = nullptr) {
    DCHECK_EQ(Conversion == Object::Conversion::kToNumeric,
              if_bigint != nullptr);

    if (is_known_tagged_pointer == IsKnownTaggedPointer::kNo) {
      IF (__ IsSmi(value)) {
        __ CombineFeedback(feedback, BinaryOperationFeedback::kSignedSmall);
        GOTO(if_number, __ UntagSmi(V<Smi>::Cast(value)));
      }
    }

    ScopedVar<HeapObject> value_heap_object(this, V<HeapObject>::Cast(value));
    WHILE(1) {
      V<Map> map = __ LoadMapField(value_heap_object);

      IF (__ IsHeapNumberMap(map)) {
        __ CombineFeedback(feedback, BinaryOperationFeedback::kNumber);
        V<Float64> value_float64 =
            __ LoadHeapNumberValue(V<HeapNumber>::Cast(value_heap_object));
        GOTO(if_number, __ JSTruncateFloat64ToWord32(value_float64));
      }

      V<Word32> instance_type = __ LoadInstanceTypeField(map);
      if (Conversion == Object::Conversion::kToNumeric) {
        IF (IsBigIntInstanceType(instance_type)) {
          V<BigInt> value_bigint = V<BigInt>::Cast(value_heap_object);
          if (Is64() && if_bigint64) {
            IF (IsSmallBigInt(value_bigint)) {
              __ CombineFeedback(feedback, BinaryOperationFeedback::kBigInt64);
              GOTO(*if_bigint64, value_bigint);
            }
          }
          __ CombineFeedback(feedback, BinaryOperationFeedback::kBigInt);
          GOTO(*if_bigint, value_bigint);
        }
      }

      // Not HeapNumber (or BigInt if conversion == kToNumeric).
      if (feedback != nullptr) {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a Numeric, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        TSA_DCHECK(this,
                   __ FeedbackIs(feedback, BinaryOperationFeedback::kNone));
      }
      IF (InstanceTypeEqual(instance_type, ODDBALL_TYPE)) {
        __ OverwriteFeedback(feedback,
                             BinaryOperationFeedback::kNumberOrOddball);
        V<Float64> oddball_value =
            __ LoadField(V<Oddball>::Cast(value_heap_object),
                         AccessBuilderTS::ForHeapNumberOrOddballOrHoleValue());
        GOTO(if_number, __ JSTruncateFloat64ToWord32(oddball_value));
      }

      // Not an oddball either -> convert.
      V<Object> converted_value;
      // TODO(nicohartmann): We have to make sure that we store the feedback if
      // any of those calls throws an exception.
      if constexpr (Conversion == Object::Conversion::kToNumeric) {
        converted_value =
            __ template CallBuiltin<BuiltinCallDescriptor::NonNumberToNumeric>(
                isolate(), context,
                {V<JSAnyNotNumber>::Cast(value_heap_object)});
      } else {
        converted_value =
            __ template CallBuiltin<BuiltinCallDescriptor::NonNumberToNumber>(
                isolate(), context,
                {V<JSAnyNotNumber>::Cast(value_heap_object)});
      }
      __ OverwriteFeedback(feedback, BinaryOperationFeedback::kAny);

      GOTO_IF(__ IsSmi(converted_value), if_number,
              __ UntagSmi(V<Smi>::Cast(converted_value)));
      value_heap_object = V<HeapObject>::Cast(converted_value);
    }

    __ Unreachable();
  }

 private:
  compiler::turboshaft::OperationMatcher matcher_{__ data()->graph()};
  Isolate* isolate() { return __ data() -> isolate(); }
};

template <template <typename> typename Reducer>
class TurboshaftBuiltinsAssembler
    : public compiler::turboshaft::TSAssembler<
          Reducer, BuiltinsReducer, NoFeedbackCollectorReducer,
          compiler::turboshaft::MachineLoweringReducer,
          compiler::turboshaft::VariableReducer> {
 public:
  using Base = compiler::turboshaft::TSAssembler<
      Reducer, BuiltinsReducer, NoFeedbackCollectorReducer,
      compiler::turboshaft::MachineLoweringReducer,
      compiler::turboshaft::VariableReducer>;
  TurboshaftBuiltinsAssembler(compiler::turboshaft::PipelineData* data,
                              compiler::turboshaft::Graph& graph,
                              Zone* phase_zone)
      : Base(data, graph, graph, phase_zone) {}

  using Base::Asm;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal

#endif  // V8_CODEGEN_TURBOSHAFT_BUILTINS_ASSEMBLER_INL_H_
