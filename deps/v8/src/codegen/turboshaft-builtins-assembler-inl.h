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
#include "src/compiler/turboshaft/runtime-call-descriptors.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/interpreter/bytecode-register.h"
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

enum IsKnownTaggedPointer { kNo, kYes };

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

template <typename Next>
class FeedbackCollectorReducer : public Next {
 public:
  BUILTIN_REDUCER(FeedbackCollector)

  using FeedbackVectorOrUndefined = Union<FeedbackVector, Undefined>;
  static constexpr bool HasFeedbackCollector() { return true; }

  void CombineFeedback(int additional_feedback) {
    __ CodeComment("CombineFeedback");
    feedback_ = __ SmiBitwiseOr(
        feedback_, __ SmiConstant(Smi::FromInt(additional_feedback)));
  }

  void OverwriteFeedback(int new_feedback) {
    __ CodeComment("OverwriteFeedback");
    feedback_ = __ SmiConstant(Smi::FromInt(new_feedback));
  }

  void CombineFeedbackOnException(int additional_feedback) {
    feedback_on_exception_ = __ SmiConstant(Smi::FromInt(additional_feedback));
  }

  void CombineExceptionFeedback() {
    feedback_ = __ SmiBitwiseOr(feedback_, feedback_on_exception_);
  }

  V<Word32> FeedbackIs(int checked_feedback) {
    return __ SmiEqual(feedback_,
                       __ SmiConstant(Smi::FromInt(checked_feedback)));
  }

  V<FeedbackVectorOrUndefined> LoadFeedbackVector() {
    return V<FeedbackVectorOrUndefined>::Cast(
        __ LoadRegister(interpreter::Register::feedback_vector()));
  }

  V<WordPtr> LoadFeedbackVectorLength(V<FeedbackVector> feedback_vector) {
    V<Word32> length = __ LoadField(feedback_vector,
                                    AccessBuilderTS::ForFeedbackVectorLength());
    return ChangePositiveInt32ToIntPtr(length);
  }

  V<MaybeObject> LoadFeedbackVectorSlot(V<FeedbackVector> feedback_vector,
                                        V<WordPtr> slot,
                                        int additional_offset = 0) {
    __ CodeComment("LoadFeedbackVectorSlot");
    int32_t header_size =
        FeedbackVector::kRawFeedbackSlotsOffset + additional_offset;
    V<WordPtr> offset =
        __ ElementOffsetFromIndex(slot, HOLEY_ELEMENTS, header_size);
    TSA_SLOW_DCHECK(this, IsOffsetInBounds(
                              offset, LoadFeedbackVectorLength(feedback_vector),
                              FeedbackVector::kHeaderSize));
    return V<MaybeObject>::Cast(
        __ Load(feedback_vector, offset,
                compiler::turboshaft::LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::AnyTagged()));
  }

  void StoreFeedbackVectorSlot(
      V<FeedbackVector> feedback_vector, V<WordPtr> slot, V<Object> value,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER,
      int additional_offset = 0) {
    __ CodeComment("StoreFeedbackVectorSlot");
    DCHECK(IsAligned(additional_offset, kTaggedSize));
    int header_size =
        FeedbackVector::kRawFeedbackSlotsOffset + additional_offset;
    V<WordPtr> offset =
        __ ElementOffsetFromIndex(slot, HOLEY_ELEMENTS, header_size);
    TSA_DCHECK(this, IsOffsetInBounds(offset,
                                      LoadFeedbackVectorLength(feedback_vector),
                                      FeedbackVector::kHeaderSize));
    switch (barrier_mode) {
      case SKIP_WRITE_BARRIER: {
        __ Store(feedback_vector, offset, value,
                 compiler::turboshaft::StoreOp::Kind::TaggedBase(),
                 MemoryRepresentation::AnyTagged(),
                 compiler::WriteBarrierKind::kNoWriteBarrier);
        return;
      }
      case UNSAFE_SKIP_WRITE_BARRIER:
        UNIMPLEMENTED();
      case UPDATE_WRITE_BARRIER:
        UNIMPLEMENTED();
      case UPDATE_EPHEMERON_KEY_WRITE_BARRIER:
        UNREACHABLE();
    }
  }

  void SetFeedbackSlot(V<WordPtr> slot_id) { slot_id_ = slot_id; }
  void SetFeedbackVector(V<FeedbackVector> feedback_vector) {
    TSA_DCHECK(this, IsFeedbackVector(feedback_vector));
    maybe_feedback_vector_ = feedback_vector;
    feedback_ = __ SmiConstant(Smi::FromInt(0));
    feedback_on_exception_ = feedback_.Get();
  }

  void LoadFeedbackVectorOrUndefinedIfJitless() {
#ifndef V8_JITLESS
    maybe_feedback_vector_ = LoadFeedbackVector();
#else
    maybe_feedback_vector_ = __ UndefinedConstant();
#endif
    feedback_ = __ SmiConstant(Smi::FromInt(0));
    feedback_on_exception_ = feedback_.Get();
  }

  static constexpr UpdateFeedbackMode DefaultUpdateFeedbackMode() {
#ifndef V8_JITLESS
    return UpdateFeedbackMode::kOptionalFeedback;
#else
    return UpdateFeedbackMode::kNoFeedback;
#endif  // !V8_JITLESS
  }

  void UpdateFeedback() {
    __ CodeComment("UpdateFeedback");
    if (mode_ == UpdateFeedbackMode::kNoFeedback) {
#ifdef V8_JITLESS
      TSA_DCHECK(this, __ IsUndefined(maybe_feedback_vector_));
      return;
#else
      UNREACHABLE();
#endif  // !V8_JITLESS
    }

    Label<> done(this);
    if (mode_ == UpdateFeedbackMode::kOptionalFeedback) {
      GOTO_IF(__ IsUndefined(maybe_feedback_vector_), done);
    } else {
      DCHECK_EQ(mode_, UpdateFeedbackMode::kGuaranteedFeedback);
    }

    V<FeedbackVector> feedback_vector =
        V<FeedbackVector>::Cast(maybe_feedback_vector_);

    V<MaybeObject> feedback_element =
        LoadFeedbackVectorSlot(feedback_vector, slot_id_);
    V<Smi> previous_feedback = V<Smi>::Cast(feedback_element);
    V<Smi> combined_feedback = __ SmiBitwiseOr(previous_feedback, feedback_);
    IF_NOT (__ SmiEqual(previous_feedback, combined_feedback)) {
      StoreFeedbackVectorSlot(feedback_vector, slot_id_, combined_feedback,
                              SKIP_WRITE_BARRIER);
      // TODO(nicohartmann):
      // ReportFeedbackUpdate(maybe_feedback_vector_, slot_id_,
      // "UpdateFeedback");
    }
    GOTO(done);

    BIND(done);
  }

  V<Smi> SmiBitwiseOr(V<Smi> a, V<Smi> b) {
    return __ BitcastWord32ToSmi(
        __ Word32BitwiseOr(__ BitcastSmiToWord32(a), __ BitcastSmiToWord32(b)));
  }

  V<Word32> SmiEqual(V<Smi> a, V<Smi> b) {
    return __ Word32Equal(__ BitcastSmiToWord32(a), __ BitcastSmiToWord32(b));
  }

  V<WordPtr> ChangePositiveInt32ToIntPtr(V<Word32> input) {
    TSA_DCHECK(this, __ Int32LessThanOrEqual(0, input));
    return __ ChangeUint32ToUintPtr(input);
  }

  V<Word32> IsFeedbackVector(V<HeapObject> heap_object) {
    V<Map> map = __ LoadMapField(heap_object);
    return __ IsFeedbackVectorMap(map);
  }

  V<Word32> IsOffsetInBounds(V<WordPtr> offset, V<WordPtr> length,
                             int header_size,
                             ElementsKind kind = HOLEY_ELEMENTS) {
    // Make sure we point to the last field.
    int element_size = 1 << ElementsKindToShiftSize(kind);
    int correction = header_size - element_size;
    V<WordPtr> last_offset =
        __ ElementOffsetFromIndex(length, kind, correction);
    return __ IntPtrLessThanOrEqual(offset, last_offset);
  }

 private:
  V<FeedbackVectorOrUndefined> maybe_feedback_vector_;
  V<WordPtr> slot_id_;
  compiler::turboshaft::Var<Smi, assembler_t> feedback_{this};
  compiler::turboshaft::Var<Smi, assembler_t> feedback_on_exception_{this};
  UpdateFeedbackMode mode_ = DefaultUpdateFeedbackMode();
};

template <typename Next>
class NoFeedbackCollectorReducer : public Next {
 public:
  BUILTIN_REDUCER(NoFeedbackCollector)

  static constexpr bool HasFeedbackCollector() { return false; }

  void CombineFeedback(int additional_feedback) {}

  void OverwriteFeedback(int new_feedback) {}

  V<Word32> FeedbackIs(int checked_feedback) { UNREACHABLE(); }

  void UpdateFeedback() {}
  void CombineExceptionFeedback() {}
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

  void EmitEpilog(Block* catch_block) {
    DCHECK_EQ(__ HasFeedbackCollector(), catch_block != nullptr);
    if (catch_block) {
      // If the handler can potentially throw, we catch the exception here and
      // update the feedback vector before we rethrow the exception.
      if (__ Bind(catch_block)) {
        V<Object> exception = __ CatchBlockBegin();
        __ CombineExceptionFeedback();
        __ UpdateFeedback();
        __ template CallRuntime<
            compiler::turboshaft::RuntimeCallDescriptor::ReThrow>(
            __ data()->isolate(), __ NoContextConstant(), {exception});
        __ Unreachable();
      }
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

  V<Word32> IsBigIntInstanceType(ConstOrV<Word32> instance_type) {
    return InstanceTypeEqual(instance_type, BIGINT_TYPE);
  }
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
                                  Label<BigInt>* if_bigint64 = nullptr) {
    DCHECK_EQ(Conversion == Object::Conversion::kToNumeric,
              if_bigint != nullptr);

    if (is_known_tagged_pointer == IsKnownTaggedPointer::kNo) {
      IF (__ IsSmi(value)) {
        __ CombineFeedback(BinaryOperationFeedback::kSignedSmall);
        GOTO(if_number, __ UntagSmi(V<Smi>::Cast(value)));
      }
    }

    ScopedVar<HeapObject> value_heap_object(this, V<HeapObject>::Cast(value));
    WHILE(1) {
      V<Map> map = __ LoadMapField(value_heap_object);

      IF (__ IsHeapNumberMap(map)) {
        __ CombineFeedback(BinaryOperationFeedback::kNumber);
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
              __ CombineFeedback(BinaryOperationFeedback::kBigInt64);
              GOTO(*if_bigint64, value_bigint);
            }
          }
          __ CombineFeedback(BinaryOperationFeedback::kBigInt);
          GOTO(*if_bigint, value_bigint);
        }
      }

      // Not HeapNumber (or BigInt if conversion == kToNumeric).
      if (__ HasFeedbackCollector()) {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a Numeric, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        TSA_DCHECK(this, __ FeedbackIs(BinaryOperationFeedback::kNone));
      }
      IF (InstanceTypeEqual(instance_type, ODDBALL_TYPE)) {
        __ OverwriteFeedback(BinaryOperationFeedback::kNumberOrOddball);
        V<Float64> oddball_value =
            __ LoadField(V<Oddball>::Cast(value_heap_object),
                         AccessBuilderTS::ForHeapNumberOrOddballOrHoleValue());
        GOTO(if_number, __ JSTruncateFloat64ToWord32(oddball_value));
      }

      // Not an oddball either -> convert.
      V<Object> converted_value;
      // TODO(nicohartmann): We have to make sure that we store the feedback if
      // any of those calls throws an exception.
      __ OverwriteFeedback(BinaryOperationFeedback::kAny);

      using Builtin =
          std::conditional_t<Conversion == Object::Conversion::kToNumeric,
                             BuiltinCallDescriptor::NonNumberToNumeric,
                             BuiltinCallDescriptor::NonNumberToNumber>;
      converted_value = __ template CallBuiltin<Builtin>(
          isolate(), context, {V<JSAnyNotNumber>::Cast(value_heap_object)});

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

template <template <typename> typename Reducer,
          template <typename> typename FeedbackReducer>
class TurboshaftBuiltinsAssembler
    : public compiler::turboshaft::TSAssembler<
          Reducer, BuiltinsReducer, FeedbackReducer,
          compiler::turboshaft::MachineLoweringReducer,
          compiler::turboshaft::VariableReducer> {
 public:
  using Base = compiler::turboshaft::TSAssembler<
      Reducer, BuiltinsReducer, FeedbackReducer,
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
