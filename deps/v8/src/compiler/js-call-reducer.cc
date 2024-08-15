// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-call-reducer.h"

#include <functional>

#include "src/base/container-utils.h"
#include "src/base/small-vector.h"
#include "src/builtins/builtins-promise.h"
#include "src/builtins/builtins-utils.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/tnode.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/access-info.h"
#include "src/compiler/allocation-builder-inl.h"
#include "src/compiler/allocation-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/fast-api-calls.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/map-inference.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/state-values-utils.h"
#include "src/compiler/type-cache.h"
#include "src/compiler/use-info.h"
#include "src/flags/flags.h"
#include "src/ic/call-optimization.h"
#include "src/objects/elements-kind.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-function.h"
#include "src/objects/objects-inl.h"
#include "src/objects/ordered-hash-table.h"
#include "src/utils/utils.h"

#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#endif

namespace v8 {
namespace internal {
namespace compiler {

// Shorter lambda declarations with less visual clutter.
#define _ [&]()

class JSCallReducerAssembler : public JSGraphAssembler {
  static constexpr bool kMarkLoopExits = true;

 public:
  JSCallReducerAssembler(JSCallReducer* reducer, Node* node,
                         Node* effect = nullptr, Node* control = nullptr)
      : JSGraphAssembler(
            reducer->broker(), reducer->JSGraphForGraphAssembler(),
            reducer->ZoneForGraphAssembler(), BranchSemantics::kJS,
            [reducer](Node* n) { reducer->RevisitForGraphAssembler(n); },
            kMarkLoopExits),
        dependencies_(reducer->dependencies()),
        node_(node) {
    InitializeEffectControl(
        effect ? effect : NodeProperties::GetEffectInput(node),
        control ? control : NodeProperties::GetControlInput(node));

    // Finish initializing the outermost catch scope.
    bool has_handler =
        NodeProperties::IsExceptionalCall(node, &outermost_handler_);
    outermost_catch_scope_.set_has_handler(has_handler);
  }

  TNode<Object> ReduceJSCallWithArrayLikeOrSpreadOfEmpty(
      std::unordered_set<Node*>* generated_calls_with_array_like_or_spread);
  TNode<Object> ReduceMathUnary(const Operator* op);
  TNode<Object> ReduceMathBinary(const Operator* op);
  TNode<String> ReduceStringPrototypeSubstring();
  TNode<Boolean> ReduceStringPrototypeStartsWith();
  TNode<Boolean> ReduceStringPrototypeStartsWith(
      StringRef search_element_string);
  TNode<Boolean> ReduceStringPrototypeEndsWith();
  TNode<Boolean> ReduceStringPrototypeEndsWith(StringRef search_element_string);
  TNode<String> ReduceStringPrototypeCharAt();
  TNode<String> ReduceStringPrototypeCharAt(StringRef s, uint32_t index);
  TNode<String> ReduceStringPrototypeSlice();
  TNode<Object> ReduceJSCallMathMinMaxWithArrayLike(Builtin builtin);

  TNode<Object> TargetInput() const { return JSCallNode{node_ptr()}.target(); }

  template <typename T>
  TNode<T> ReceiverInputAs() const {
    return TNode<T>::UncheckedCast(JSCallNode{node_ptr()}.receiver());
  }

  TNode<Object> ReceiverInput() const { return ReceiverInputAs<Object>(); }

  Node* node_ptr() const { return node_; }

  // Simplified operators.
  TNode<Number> SpeculativeToNumber(
      TNode<Object> value,
      NumberOperationHint hint = NumberOperationHint::kNumberOrOddball);
  TNode<Smi> CheckSmi(TNode<Object> value);
  TNode<Number> CheckNumber(TNode<Object> value);
  TNode<String> CheckString(TNode<Object> value);
  TNode<Number> CheckBounds(TNode<Number> value, TNode<Number> limit,
                            CheckBoundsFlags flags = {});

  // Common operators.
  TNode<Smi> TypeGuardUnsignedSmall(TNode<Object> value);
  TNode<Object> TypeGuardNonInternal(TNode<Object> value);
  TNode<Number> TypeGuardFixedArrayLength(TNode<Object> value);
  TNode<Object> Call4(const Callable& callable, TNode<Context> context,
                      TNode<Object> arg0, TNode<Object> arg1,
                      TNode<Object> arg2, TNode<Object> arg3);

  // Javascript operators.
  TNode<Object> JSCall3(TNode<Object> function, TNode<Object> this_arg,
                        TNode<Object> arg0, TNode<Object> arg1,
                        TNode<Object> arg2, FrameState frame_state);
  TNode<Object> JSCall4(TNode<Object> function, TNode<Object> this_arg,
                        TNode<Object> arg0, TNode<Object> arg1,
                        TNode<Object> arg2, TNode<Object> arg3,
                        FrameState frame_state);

  // Emplace a copy of the call node into the graph at current effect/control.
  TNode<Object> CopyNode();

  // Used in special cases in which we are certain CreateArray does not throw.
  TNode<JSArray> CreateArrayNoThrow(TNode<Object> ctor, TNode<Number> size,
                                    FrameState frame_state);

  TNode<JSArray> AllocateEmptyJSArray(ElementsKind kind,
                                      NativeContextRef native_context);

  TNode<Number> NumberInc(TNode<Number> value) {
    return NumberAdd(value, OneConstant());
  }

  TNode<Number> LoadMapElementsKind(TNode<Map> map);

  template <typename T, typename U>
  TNode<T> EnterMachineGraph(TNode<U> input, UseInfo use_info) {
    return AddNode<T>(
        graph()->NewNode(common()->EnterMachineGraph(use_info), input));
  }

  template <typename T, typename U>
  TNode<T> ExitMachineGraph(TNode<U> input,
                            MachineRepresentation output_representation,
                            Type output_type) {
    return AddNode<T>(graph()->NewNode(
        common()->ExitMachineGraph(output_representation, output_type), input));
  }

  void MaybeInsertMapChecks(MapInference* inference,
                            bool has_stability_dependency) {
    // TODO(jgruber): Implement MapInference::InsertMapChecks in graph
    // assembler.
    if (!has_stability_dependency) {
      Effect e = effect();
      inference->InsertMapChecks(jsgraph(), &e, Control{control()}, feedback());
      InitializeEffectControl(e, control());
    }
  }

  TNode<Object> ConvertHoleToUndefined(TNode<Object> value, ElementsKind kind) {
    DCHECK(IsHoleyElementsKind(kind));
    if (kind == HOLEY_DOUBLE_ELEMENTS) {
      return AddNode<Number>(
          graph()->NewNode(simplified()->ChangeFloat64HoleToTagged(), value));
    }
    return ConvertTaggedHoleToUndefined(value);
  }

  class TryCatchBuilder0 {
   public:
    using TryFunction = VoidGenerator0;
    using CatchFunction = std::function<void(TNode<Object>)>;

    TryCatchBuilder0(JSCallReducerAssembler* gasm, const TryFunction& try_body)
        : gasm_(gasm), try_body_(try_body) {}

    void Catch(const CatchFunction& catch_body) {
      TNode<Object> handler_exception;
      Effect handler_effect{nullptr};
      Control handler_control{nullptr};

      auto continuation = gasm_->MakeLabel();

      // Try.
      {
        CatchScope catch_scope = CatchScope::Inner(gasm_->temp_zone(), gasm_);
        try_body_();
        gasm_->Goto(&continuation);

        catch_scope.MergeExceptionalPaths(&handler_exception, &handler_effect,
                                          &handler_control);
      }

      // Catch.
      {
        gasm_->InitializeEffectControl(handler_effect, handler_control);
        catch_body(handler_exception);
        gasm_->Goto(&continuation);
      }

      gasm_->Bind(&continuation);
    }

   private:
    JSCallReducerAssembler* const gasm_;
    const VoidGenerator0 try_body_;
  };

  TryCatchBuilder0 Try(const VoidGenerator0& try_body) {
    return {this, try_body};
  }

  using ConditionFunction1 = std::function<TNode<Boolean>(TNode<Number>)>;
  using StepFunction1 = std::function<TNode<Number>(TNode<Number>)>;
  class ForBuilder0 {
    using For0BodyFunction = std::function<void(TNode<Number>)>;

   public:
    ForBuilder0(JSGraphAssembler* gasm, TNode<Number> initial_value,
                const ConditionFunction1& cond, const StepFunction1& step)
        : gasm_(gasm),
          initial_value_(initial_value),
          cond_(cond),
          step_(step) {}

    void Do(const For0BodyFunction& body) {
      auto loop_exit = gasm_->MakeLabel();

      {
        GraphAssembler::LoopScope<kPhiRepresentation> loop_scope(gasm_);

        auto loop_header = loop_scope.loop_header_label();
        auto loop_body = gasm_->MakeLabel();

        gasm_->Goto(loop_header, initial_value_);

        gasm_->Bind(loop_header);
        TNode<Number> i = loop_header->PhiAt<Number>(0);

        gasm_->BranchWithHint(cond_(i), &loop_body, &loop_exit,
                              BranchHint::kTrue);

        gasm_->Bind(&loop_body);
        body(i);
        gasm_->Goto(loop_header, step_(i));
      }

      gasm_->Bind(&loop_exit);
    }

   private:
    static constexpr MachineRepresentation kPhiRepresentation =
        MachineRepresentation::kTagged;

    JSGraphAssembler* const gasm_;
    const TNode<Number> initial_value_;
    const ConditionFunction1 cond_;
    const StepFunction1 step_;
  };

  ForBuilder0 ForZeroUntil(TNode<Number> excluded_limit) {
    TNode<Number> initial_value = ZeroConstant();
    auto cond = [=](TNode<Number> i) {
      return NumberLessThan(i, excluded_limit);
    };
    auto step = [=](TNode<Number> i) { return NumberAdd(i, OneConstant()); };
    return {this, initial_value, cond, step};
  }

  ForBuilder0 Forever(TNode<Number> initial_value, const StepFunction1& step) {
    return {this, initial_value, [=](TNode<Number>) { return TrueConstant(); },
            step};
  }

  using For1BodyFunction = std::function<void(TNode<Number>, TNode<Object>*)>;
  class ForBuilder1 {
   public:
    ForBuilder1(JSGraphAssembler* gasm, TNode<Number> initial_value,
                const ConditionFunction1& cond, const StepFunction1& step,
                TNode<Object> initial_arg0)
        : gasm_(gasm),
          initial_value_(initial_value),
          cond_(cond),
          step_(step),
          initial_arg0_(initial_arg0) {}

    V8_WARN_UNUSED_RESULT ForBuilder1& Do(const For1BodyFunction& body) {
      body_ = body;
      return *this;
    }

    V8_WARN_UNUSED_RESULT TNode<Object> Value() {
      DCHECK(body_);
      TNode<Object> arg0 = initial_arg0_;

      auto loop_exit = gasm_->MakeDeferredLabel(kPhiRepresentation);

      {
        GraphAssembler::LoopScope<kPhiRepresentation, kPhiRepresentation>
            loop_scope(gasm_);

        auto loop_header = loop_scope.loop_header_label();
        auto loop_body = gasm_->MakeDeferredLabel(kPhiRepresentation);

        gasm_->Goto(loop_header, initial_value_, initial_arg0_);

        gasm_->Bind(loop_header);
        TNode<Number> i = loop_header->PhiAt<Number>(0);
        arg0 = loop_header->PhiAt<Object>(1);

        gasm_->BranchWithHint(cond_(i), &loop_body, &loop_exit,
                              BranchHint::kTrue, arg0);

        gasm_->Bind(&loop_body);
        body_(i, &arg0);
        gasm_->Goto(loop_header, step_(i), arg0);
      }

      gasm_->Bind(&loop_exit);
      return TNode<Object>::UncheckedCast(loop_exit.PhiAt<Object>(0));
    }

    void ValueIsUnused() { USE(Value()); }

   private:
    static constexpr MachineRepresentation kPhiRepresentation =
        MachineRepresentation::kTagged;

    JSGraphAssembler* const gasm_;
    const TNode<Number> initial_value_;
    const ConditionFunction1 cond_;
    const StepFunction1 step_;
    For1BodyFunction body_;
    const TNode<Object> initial_arg0_;
  };

  ForBuilder1 For1(TNode<Number> initial_value, const ConditionFunction1& cond,
                   const StepFunction1& step, TNode<Object> initial_arg0) {
    return {this, initial_value, cond, step, initial_arg0};
  }

  ForBuilder1 For1ZeroUntil(TNode<Number> excluded_limit,
                            TNode<Object> initial_arg0) {
    TNode<Number> initial_value = ZeroConstant();
    auto cond = [=](TNode<Number> i) {
      return NumberLessThan(i, excluded_limit);
    };
    auto step = [=](TNode<Number> i) { return NumberAdd(i, OneConstant()); };
    return {this, initial_value, cond, step, initial_arg0};
  }

  void ThrowIfNotCallable(TNode<Object> maybe_callable,
                          FrameState frame_state) {
    IfNot(ObjectIsCallable(maybe_callable))
        .Then(_ {
          JSCallRuntime1(Runtime::kThrowCalledNonCallable, maybe_callable,
                         ContextInput(), frame_state);
          Unreachable();  // The runtime call throws unconditionally.
        })
        .ExpectTrue();
  }

  const FeedbackSource& feedback() const {
    CallParameters const& p = CallParametersOf(node_ptr()->op());
    return p.feedback();
  }

  int ArgumentCount() const { return JSCallNode{node_ptr()}.ArgumentCount(); }

  TNode<Object> Argument(int index) const {
    return TNode<Object>::UncheckedCast(JSCallNode{node_ptr()}.Argument(index));
  }

  template <typename T>
  TNode<T> ArgumentAs(int index) const {
    return TNode<T>::UncheckedCast(Argument(index));
  }

  TNode<Object> ArgumentOrNaN(int index) {
    return TNode<Object>::UncheckedCast(
        ArgumentCount() > index ? Argument(index) : NaNConstant());
  }

  TNode<Object> ArgumentOrUndefined(int index) {
    return TNode<Object>::UncheckedCast(
        ArgumentCount() > index ? Argument(index) : UndefinedConstant());
  }

  TNode<Number> ArgumentOrZero(int index) {
    return TNode<Number>::UncheckedCast(
        ArgumentCount() > index ? Argument(index) : ZeroConstant());
  }

  TNode<Context> ContextInput() const {
    return TNode<Context>::UncheckedCast(
        NodeProperties::GetContextInput(node_));
  }

  FrameState FrameStateInput() const {
    return FrameState(NodeProperties::GetFrameStateInput(node_));
  }

  CompilationDependencies* dependencies() const { return dependencies_; }

 private:
  CompilationDependencies* const dependencies_;
  Node* const node_;
};

enum class ArrayReduceDirection { kLeft, kRight };
enum class ArrayFindVariant { kFind, kFindIndex };
enum class ArrayEverySomeVariant { kEvery, kSome };
enum class ArrayIndexOfIncludesVariant { kIncludes, kIndexOf };

// This subclass bundles functionality specific to reducing iterating array
// builtins.
class IteratingArrayBuiltinReducerAssembler : public JSCallReducerAssembler {
 public:
  IteratingArrayBuiltinReducerAssembler(JSCallReducer* reducer, Node* node)
      : JSCallReducerAssembler(reducer, node) {
    DCHECK(v8_flags.turbo_inline_array_builtins);
  }

  TNode<Object> ReduceArrayPrototypeForEach(MapInference* inference,
                                            const bool has_stability_dependency,
                                            ElementsKind kind,
                                            SharedFunctionInfoRef shared);
  TNode<Object> ReduceArrayPrototypeReduce(MapInference* inference,
                                           const bool has_stability_dependency,
                                           ElementsKind kind,
                                           ArrayReduceDirection direction,
                                           SharedFunctionInfoRef shared);
  TNode<JSArray> ReduceArrayPrototypeMap(MapInference* inference,
                                         const bool has_stability_dependency,
                                         ElementsKind kind,
                                         SharedFunctionInfoRef shared,
                                         NativeContextRef native_context);
  TNode<JSArray> ReduceArrayPrototypeFilter(MapInference* inference,
                                            const bool has_stability_dependency,
                                            ElementsKind kind,
                                            SharedFunctionInfoRef shared,
                                            NativeContextRef native_context);
  TNode<Object> ReduceArrayPrototypeFind(MapInference* inference,
                                         const bool has_stability_dependency,
                                         ElementsKind kind,
                                         SharedFunctionInfoRef shared,
                                         NativeContextRef native_context,
                                         ArrayFindVariant variant);
  TNode<Boolean> ReduceArrayPrototypeEverySome(
      MapInference* inference, const bool has_stability_dependency,
      ElementsKind kind, SharedFunctionInfoRef shared,
      NativeContextRef native_context, ArrayEverySomeVariant variant);
  TNode<Object> ReduceArrayPrototypeAt(ZoneVector<MapRef> kinds,
                                       bool needs_fallback_builtin_call);
  TNode<Object> ReduceArrayPrototypeIndexOfIncludes(
      ElementsKind kind, ArrayIndexOfIncludesVariant variant);
  TNode<Number> ReduceArrayPrototypePush(MapInference* inference);

 private:
  // Returns {index,value}. Assumes that the map has not changed, but possibly
  // the length and backing store.
  std::pair<TNode<Number>, TNode<Object>> SafeLoadElement(ElementsKind kind,
                                                          TNode<JSArray> o,
                                                          TNode<Number> index) {
    // Make sure that the access is still in bounds, since the callback could
    // have changed the array's size.
    TNode<Number> length = LoadJSArrayLength(o, kind);
    index = CheckBounds(index, length);

    // Reload the elements pointer before calling the callback, since the
    // previous callback might have resized the array causing the elements
    // buffer to be re-allocated.
    TNode<HeapObject> elements =
        LoadField<HeapObject>(AccessBuilder::ForJSObjectElements(), o);
    TNode<Object> value = LoadElement<Object>(
        AccessBuilder::ForFixedArrayElement(kind), elements, index);
    return std::make_pair(index, value);
  }

  template <typename... Vars>
  TNode<Object> MaybeSkipHole(
      TNode<Object> o, ElementsKind kind,
      GraphAssemblerLabel<sizeof...(Vars)>* continue_label,
      TNode<Vars>... vars) {
    if (!IsHoleyElementsKind(kind)) return o;

    auto if_not_hole = MakeLabel(MachineRepresentationOf<Vars>::value...);
    BranchWithHint(HoleCheck(kind, o), continue_label, &if_not_hole,
                   BranchHint::kFalse, vars...);

    // The contract is that we don't leak "the hole" into "user JavaScript",
    // so we must rename the {element} here to explicitly exclude "the hole"
    // from the type of {element}.
    Bind(&if_not_hole);
    return TypeGuardNonInternal(o);
  }

  TNode<Smi> LoadJSArrayLength(TNode<JSArray> array, ElementsKind kind) {
    return LoadField<Smi>(AccessBuilder::ForJSArrayLength(kind), array);
  }
  void StoreJSArrayLength(TNode<JSArray> array, TNode<Number> value,
                          ElementsKind kind) {
    StoreField(AccessBuilder::ForJSArrayLength(kind), array, value);
  }
  void StoreFixedArrayBaseElement(TNode<FixedArrayBase> o, TNode<Number> index,
                                  TNode<Object> v, ElementsKind kind) {
    StoreElement(AccessBuilder::ForFixedArrayElement(kind), o, index, v);
  }

  TNode<FixedArrayBase> LoadElements(TNode<JSObject> o) {
    return LoadField<FixedArrayBase>(AccessBuilder::ForJSObjectElements(), o);
  }
  TNode<Smi> LoadFixedArrayBaseLength(TNode<FixedArrayBase> o) {
    return LoadField<Smi>(AccessBuilder::ForFixedArrayLength(), o);
  }

  TNode<Boolean> HoleCheck(ElementsKind kind, TNode<Object> v) {
    return IsDoubleElementsKind(kind)
               ? NumberIsFloat64Hole(TNode<Number>::UncheckedCast(v))
               : IsTheHole(v);
  }
};

class PromiseBuiltinReducerAssembler : public JSCallReducerAssembler {
 public:
  PromiseBuiltinReducerAssembler(JSCallReducer* reducer, Node* node)
      : JSCallReducerAssembler(reducer, node) {
    DCHECK_EQ(IrOpcode::kJSConstruct, node->opcode());
  }

  TNode<Object> ReducePromiseConstructor(NativeContextRef native_context);

  int ConstructArity() const {
    return JSConstructNode{node_ptr()}.ArgumentCount();
  }

  TNode<Object> TargetInput() const {
    return JSConstructNode{node_ptr()}.target();
  }

  TNode<Object> NewTargetInput() const {
    return JSConstructNode{node_ptr()}.new_target();
  }

 private:
  TNode<JSPromise> CreatePromise(TNode<Context> context) {
    return AddNode<JSPromise>(
        graph()->NewNode(javascript()->CreatePromise(), context, effect()));
  }

  TNode<Context> CreateFunctionContext(NativeContextRef native_context,
                                       TNode<Context> outer_context,
                                       int slot_count) {
    return AddNode<Context>(graph()->NewNode(
        javascript()->CreateFunctionContext(
            native_context.scope_info(broker()),
            slot_count - Context::MIN_CONTEXT_SLOTS, FUNCTION_SCOPE),
        outer_context, effect(), control()));
  }

  void StoreContextSlot(TNode<Context> context, size_t slot_index,
                        TNode<Object> value) {
    StoreField(AccessBuilder::ForContextSlot(slot_index), context, value);
  }

  TNode<JSFunction> CreateClosureFromBuiltinSharedFunctionInfo(
      SharedFunctionInfoRef shared, TNode<Context> context) {
    DCHECK(shared.HasBuiltinId());
    Handle<FeedbackCell> feedback_cell =
        isolate()->factory()->many_closures_cell();
    Callable const callable =
        Builtins::CallableFor(isolate(), shared.builtin_id());
    CodeRef code = MakeRef(broker(), *callable.code());
    return AddNode<JSFunction>(graph()->NewNode(
        javascript()->CreateClosure(shared, code), HeapConstant(feedback_cell),
        context, effect(), control()));
  }

  void CallPromiseExecutor(TNode<Object> executor, TNode<JSFunction> resolve,
                           TNode<JSFunction> reject, FrameState frame_state) {
    JSConstructNode n(node_ptr());
    const ConstructParameters& p = n.Parameters();
    FeedbackSource no_feedback_source{};
    Node* no_feedback = UndefinedConstant();
    MayThrow(_ {
      return AddNode<Object>(graph()->NewNode(
          javascript()->Call(JSCallNode::ArityForArgc(2), p.frequency(),
                             no_feedback_source,
                             ConvertReceiverMode::kNullOrUndefined),
          executor, UndefinedConstant(), resolve, reject, no_feedback,
          n.context(), frame_state, effect(), control()));
    });
  }

  void CallPromiseReject(TNode<JSFunction> reject, TNode<Object> exception,
                         FrameState frame_state) {
    JSConstructNode n(node_ptr());
    const ConstructParameters& p = n.Parameters();
    FeedbackSource no_feedback_source{};
    Node* no_feedback = UndefinedConstant();
    MayThrow(_ {
      return AddNode<Object>(graph()->NewNode(
          javascript()->Call(JSCallNode::ArityForArgc(1), p.frequency(),
                             no_feedback_source,
                             ConvertReceiverMode::kNullOrUndefined),
          reject, UndefinedConstant(), exception, no_feedback, n.context(),
          frame_state, effect(), control()));
    });
  }
};

class FastApiCallReducerAssembler : public JSCallReducerAssembler {
 public:
  FastApiCallReducerAssembler(
      JSCallReducer* reducer, Node* node,
      const FunctionTemplateInfoRef function_template_info,
      const FastApiCallFunctionVector& c_candidate_functions, Node* receiver,
      Node* holder, const SharedFunctionInfoRef shared, Node* target,
      const int arity, Node* effect)
      : JSCallReducerAssembler(reducer, node),
        c_candidate_functions_(c_candidate_functions),
        function_template_info_(function_template_info),
        receiver_(receiver),
        holder_(holder),
        shared_(shared),
        target_(target),
        arity_(arity) {
    DCHECK_EQ(IrOpcode::kJSCall, node->opcode());
    CHECK_GT(c_candidate_functions.size(), 0);
    InitializeEffectControl(effect, NodeProperties::GetControlInput(node));
  }

  TNode<Object> ReduceFastApiCall() {
    JSCallNode n(node_ptr());

    // C arguments include the receiver at index 0. Thus C index 1 corresponds
    // to the JS argument 0, etc.
    // All functions in c_candidate_functions_ have the same number of
    // arguments, so extract c_argument_count from the first function.
    const int c_argument_count =
        static_cast<int>(c_candidate_functions_[0].signature->ArgumentCount());
    CHECK_GE(c_argument_count, kReceiver);

    int cursor = 0;
    base::SmallVector<Node*, kInlineSize> inputs(c_argument_count + arity_ +
                                                 kExtraInputsCount);
    inputs[cursor++] = n.receiver();

    // TODO(turbofan): Consider refactoring CFunctionInfo to distinguish
    // between receiver and arguments, simplifying this (and related) spots.
    int js_args_count = c_argument_count - kReceiver;
    for (int i = 0; i < js_args_count; ++i) {
      if (i < n.ArgumentCount()) {
        inputs[cursor++] = n.Argument(i);
      } else {
        inputs[cursor++] = UndefinedConstant();
      }
    }

    // Here we add the arguments for the slow call, which will be
    // reconstructed at a later phase. Those are effectively the same
    // arguments as for the fast call, but we want to have them as
    // separate inputs, so that SimplifiedLowering can provide the best
    // possible UseInfos for each of them. The inputs to FastApiCall
    // look like:
    // [fast callee, receiver, ... C arguments,
    // call code, external constant for function, argc, call handler info data,
    // holder, receiver, ... JS arguments, context, new frame state]
    bool no_profiling =
        broker()->dependencies()->DependOnNoProfilingProtector();
    Callable call_api_callback = Builtins::CallableFor(
        isolate(), no_profiling ? Builtin::kCallApiCallbackOptimizedNoProfiling
                                : Builtin::kCallApiCallbackOptimized);
    CallInterfaceDescriptor cid = call_api_callback.descriptor();
    CallDescriptor* call_descriptor =
        Linkage::GetStubCallDescriptor(graph()->zone(), cid, arity_ + kReceiver,
                                       CallDescriptor::kNeedsFrameState);
    ApiFunction api_function(function_template_info_.callback(broker()));
    ExternalReference function_reference = ExternalReference::Create(
        isolate(), &api_function, ExternalReference::DIRECT_API_CALL,
        function_template_info_.c_functions(broker()).data(),
        function_template_info_.c_signatures(broker()).data(),
        static_cast<unsigned>(
            function_template_info_.c_functions(broker()).size()));

    Node* continuation_frame_state = CreateInlinedApiFunctionFrameState(
        jsgraph(), shared_, target_, ContextInput(), receiver_,
        FrameStateInput());

    inputs[cursor++] = HeapConstant(call_api_callback.code());
    inputs[cursor++] = ExternalConstant(function_reference);
    inputs[cursor++] = NumberConstant(arity_);
    inputs[cursor++] =
        Constant(function_template_info_.callback_data(broker()).value());
    inputs[cursor++] = holder_;
    inputs[cursor++] = receiver_;
    for (int i = 0; i < arity_; ++i) {
      inputs[cursor++] = Argument(i);
    }
    inputs[cursor++] = ContextInput();
    inputs[cursor++] = continuation_frame_state;
    inputs[cursor++] = effect();
    inputs[cursor++] = control();

    DCHECK_EQ(cursor, c_argument_count + arity_ + kExtraInputsCount);

    return FastApiCall(call_descriptor, inputs.begin(), inputs.size());
  }

 private:
  static constexpr int kSlowTarget = 1;
  static constexpr int kEffectAndControl = 2;
  static constexpr int kContextAndFrameState = 2;
  static constexpr int kCallCodeDataAndArgc = 3;
  static constexpr int kHolder = 1, kReceiver = 1;
  static constexpr int kExtraInputsCount =
      kSlowTarget + kEffectAndControl + kContextAndFrameState +
      kCallCodeDataAndArgc + kHolder + kReceiver;
  static constexpr int kInlineSize = 12;

  TNode<Object> FastApiCall(CallDescriptor* descriptor, Node** inputs,
                            size_t inputs_size) {
    return AddNode<Object>(
        graph()->NewNode(simplified()->FastApiCall(c_candidate_functions_,
                                                   feedback(), descriptor),
                         static_cast<int>(inputs_size), inputs));
  }

  const FastApiCallFunctionVector c_candidate_functions_;
  const FunctionTemplateInfoRef function_template_info_;
  Node* const receiver_;
  Node* const holder_;
  const SharedFunctionInfoRef shared_;
  Node* const target_;
  const int arity_;
};

TNode<Number> JSCallReducerAssembler::SpeculativeToNumber(
    TNode<Object> value, NumberOperationHint hint) {
  return AddNode<Number>(
      graph()->NewNode(simplified()->SpeculativeToNumber(hint, feedback()),
                       value, effect(), control()));
}

TNode<Smi> JSCallReducerAssembler::CheckSmi(TNode<Object> value) {
  return AddNode<Smi>(graph()->NewNode(simplified()->CheckSmi(feedback()),
                                       value, effect(), control()));
}

TNode<Number> JSCallReducerAssembler::CheckNumber(TNode<Object> value) {
  return AddNode<Number>(graph()->NewNode(simplified()->CheckNumber(feedback()),
                                          value, effect(), control()));
}

TNode<String> JSCallReducerAssembler::CheckString(TNode<Object> value) {
  return AddNode<String>(graph()->NewNode(simplified()->CheckString(feedback()),
                                          value, effect(), control()));
}

TNode<Number> JSCallReducerAssembler::CheckBounds(TNode<Number> value,
                                                  TNode<Number> limit,
                                                  CheckBoundsFlags flags) {
  return AddNode<Number>(
      graph()->NewNode(simplified()->CheckBounds(feedback(), flags), value,
                       limit, effect(), control()));
}

TNode<Smi> JSCallReducerAssembler::TypeGuardUnsignedSmall(TNode<Object> value) {
  return TNode<Smi>::UncheckedCast(TypeGuard(Type::UnsignedSmall(), value));
}

TNode<Object> JSCallReducerAssembler::TypeGuardNonInternal(
    TNode<Object> value) {
  return TNode<Object>::UncheckedCast(TypeGuard(Type::NonInternal(), value));
}

TNode<Number> JSCallReducerAssembler::TypeGuardFixedArrayLength(
    TNode<Object> value) {
  DCHECK(TypeCache::Get()->kFixedDoubleArrayLengthType.Is(
      TypeCache::Get()->kFixedArrayLengthType));
  return TNode<Number>::UncheckedCast(
      TypeGuard(TypeCache::Get()->kFixedArrayLengthType, value));
}

TNode<Object> JSCallReducerAssembler::Call4(
    const Callable& callable, TNode<Context> context, TNode<Object> arg0,
    TNode<Object> arg1, TNode<Object> arg2, TNode<Object> arg3) {
  // TODO(jgruber): Make this more generic. Currently it's fitted to its single
  // callsite.
  CallDescriptor* desc = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), CallDescriptor::kNoFlags,
      Operator::kEliminatable);

  return TNode<Object>::UncheckedCast(Call(desc, HeapConstant(callable.code()),
                                           arg0, arg1, arg2, arg3, context));
}

TNode<Object> JSCallReducerAssembler::JSCall3(
    TNode<Object> function, TNode<Object> this_arg, TNode<Object> arg0,
    TNode<Object> arg1, TNode<Object> arg2, FrameState frame_state) {
  JSCallNode n(node_ptr());
  CallParameters const& p = n.Parameters();
  return MayThrow(_ {
    return AddNode<Object>(graph()->NewNode(
        javascript()->Call(JSCallNode::ArityForArgc(3), p.frequency(),
                           p.feedback(), ConvertReceiverMode::kAny,
                           p.speculation_mode(),
                           CallFeedbackRelation::kUnrelated),
        function, this_arg, arg0, arg1, arg2, n.feedback_vector(),
        ContextInput(), frame_state, effect(), control()));
  });
}

TNode<Object> JSCallReducerAssembler::JSCall4(
    TNode<Object> function, TNode<Object> this_arg, TNode<Object> arg0,
    TNode<Object> arg1, TNode<Object> arg2, TNode<Object> arg3,
    FrameState frame_state) {
  JSCallNode n(node_ptr());
  CallParameters const& p = n.Parameters();
  return MayThrow(_ {
    return AddNode<Object>(graph()->NewNode(
        javascript()->Call(JSCallNode::ArityForArgc(4), p.frequency(),
                           p.feedback(), ConvertReceiverMode::kAny,
                           p.speculation_mode(),
                           CallFeedbackRelation::kUnrelated),
        function, this_arg, arg0, arg1, arg2, arg3, n.feedback_vector(),
        ContextInput(), frame_state, effect(), control()));
  });
}

TNode<Object> JSCallReducerAssembler::CopyNode() {
  return MayThrow(_ {
    Node* copy = graph()->CloneNode(node_ptr());
    NodeProperties::ReplaceEffectInput(copy, effect());
    NodeProperties::ReplaceControlInput(copy, control());
    return AddNode<Object>(copy);
  });
}

TNode<JSArray> JSCallReducerAssembler::CreateArrayNoThrow(
    TNode<Object> ctor, TNode<Number> size, FrameState frame_state) {
  return AddNode<JSArray>(
      graph()->NewNode(javascript()->CreateArray(1, base::nullopt), ctor, ctor,
                       size, ContextInput(), frame_state, effect(), control()));
}

TNode<JSArray> JSCallReducerAssembler::AllocateEmptyJSArray(
    ElementsKind kind, NativeContextRef native_context) {
  // TODO(jgruber): Port AllocationBuilder to JSGraphAssembler.
  MapRef map = native_context.GetInitialJSArrayMap(broker(), kind);

  AllocationBuilder ab(jsgraph(), broker(), effect(), control());
  ab.Allocate(map.instance_size(), AllocationType::kYoung, Type::Array());
  ab.Store(AccessBuilder::ForMap(), map);
  Node* empty_fixed_array = jsgraph()->EmptyFixedArrayConstant();
  ab.Store(AccessBuilder::ForJSObjectPropertiesOrHashKnownPointer(),
           empty_fixed_array);
  ab.Store(AccessBuilder::ForJSObjectElements(), empty_fixed_array);
  ab.Store(AccessBuilder::ForJSArrayLength(kind), jsgraph()->ZeroConstant());
  for (int i = 0; i < map.GetInObjectProperties(); ++i) {
    ab.Store(AccessBuilder::ForJSObjectInObjectProperty(map, i),
             jsgraph()->UndefinedConstant());
  }
  Node* result = ab.Finish();
  InitializeEffectControl(result, control());
  return TNode<JSArray>::UncheckedCast(result);
}

TNode<Number> JSCallReducerAssembler::LoadMapElementsKind(TNode<Map> map) {
  TNode<Number> bit_field2 =
      LoadField<Number>(AccessBuilder::ForMapBitField2(), map);
  return NumberShiftRightLogical(
      NumberBitwiseAnd(bit_field2,
                       NumberConstant(Map::Bits2::ElementsKindBits::kMask)),
      NumberConstant(Map::Bits2::ElementsKindBits::kShift));
}

TNode<Object> JSCallReducerAssembler::ReduceMathUnary(const Operator* op) {
  TNode<Object> input = Argument(0);
  TNode<Number> input_as_number = SpeculativeToNumber(input);
  return TNode<Object>::UncheckedCast(graph()->NewNode(op, input_as_number));
}

TNode<Object> JSCallReducerAssembler::ReduceMathBinary(const Operator* op) {
  TNode<Object> left = Argument(0);
  TNode<Object> right = ArgumentOrNaN(1);
  TNode<Number> left_number = SpeculativeToNumber(left);
  TNode<Number> right_number = SpeculativeToNumber(right);
  return TNode<Object>::UncheckedCast(
      graph()->NewNode(op, left_number, right_number));
}

TNode<String> JSCallReducerAssembler::ReduceStringPrototypeSubstring() {
  TNode<Object> receiver = ReceiverInput();
  TNode<Object> start = Argument(0);
  TNode<Object> end = ArgumentOrUndefined(1);

  TNode<String> receiver_string = CheckString(receiver);
  TNode<Number> start_smi = CheckSmi(start);

  TNode<Number> length = StringLength(receiver_string);

  TNode<Number> end_smi = SelectIf<Number>(IsUndefined(end))
                              .Then(_ { return length; })
                              .Else(_ { return CheckSmi(end); })
                              .ExpectFalse()
                              .Value();

  TNode<Number> zero = TNode<Number>::UncheckedCast(ZeroConstant());
  TNode<Number> finalStart = NumberMin(NumberMax(start_smi, zero), length);
  TNode<Number> finalEnd = NumberMin(NumberMax(end_smi, zero), length);
  TNode<Number> from = NumberMin(finalStart, finalEnd);
  TNode<Number> to = NumberMax(finalStart, finalEnd);

  return StringSubstring(receiver_string, from, to);
}

TNode<Boolean> JSCallReducerAssembler::ReduceStringPrototypeStartsWith(
    StringRef search_element_string) {
  DCHECK(search_element_string.IsContentAccessible());
  TNode<Object> receiver = ReceiverInput();
  TNode<Object> start = ArgumentOrZero(1);

  TNode<String> receiver_string = CheckString(receiver);
  TNode<Smi> start_smi = CheckSmi(start);
  TNode<Number> length = StringLength(receiver_string);

  TNode<Number> zero = ZeroConstant();
  TNode<Number> clamped_start = NumberMin(NumberMax(start_smi, zero), length);

  int search_string_length = search_element_string.length();
  DCHECK(search_string_length <= JSCallReducer::kMaxInlineMatchSequence);

  auto out = MakeLabel(MachineRepresentation::kTagged);

  auto search_string_too_long =
      NumberLessThan(NumberSubtract(length, clamped_start),
                     NumberConstant(search_string_length));

  GotoIf(search_string_too_long, &out, BranchHint::kFalse, FalseConstant());

  static_assert(String::kMaxLength <= kSmiMaxValue);

  for (int i = 0; i < search_string_length; i++) {
    TNode<Number> k = NumberConstant(i);
    TNode<Number> receiver_string_position = TNode<Number>::UncheckedCast(
        TypeGuard(Type::UnsignedSmall(), NumberAdd(k, clamped_start)));
    Node* receiver_string_char =
        StringCharCodeAt(receiver_string, receiver_string_position);
    Node* search_string_char = jsgraph()->ConstantNoHole(
        search_element_string.GetChar(broker(), i).value());
    auto is_equal = graph()->NewNode(simplified()->NumberEqual(),
                                     search_string_char, receiver_string_char);
    GotoIfNot(is_equal, &out, FalseConstant());
  }

  Goto(&out, TrueConstant());

  Bind(&out);
  return out.PhiAt<Boolean>(0);
}

TNode<Boolean> JSCallReducerAssembler::ReduceStringPrototypeStartsWith() {
  TNode<Object> receiver = ReceiverInput();
  TNode<Object> search_element = ArgumentOrUndefined(0);
  TNode<Object> start = ArgumentOrZero(1);

  TNode<String> receiver_string = CheckString(receiver);
  TNode<String> search_string = CheckString(search_element);
  TNode<Smi> start_smi = CheckSmi(start);
  TNode<Number> length = StringLength(receiver_string);

  TNode<Number> zero = ZeroConstant();
  TNode<Number> clamped_start = NumberMin(NumberMax(start_smi, zero), length);

  TNode<Number> search_string_length = StringLength(search_string);

  auto out = MakeLabel(MachineRepresentation::kTagged);

  auto search_string_too_long = NumberLessThan(
      NumberSubtract(length, clamped_start), search_string_length);

  GotoIf(search_string_too_long, &out, BranchHint::kFalse, FalseConstant());

  static_assert(String::kMaxLength <= kSmiMaxValue);

  ForZeroUntil(search_string_length).Do([&](TNode<Number> k) {
    TNode<Number> receiver_string_position = TNode<Number>::UncheckedCast(
        TypeGuard(Type::UnsignedSmall(), NumberAdd(k, clamped_start)));
    Node* receiver_string_char =
        StringCharCodeAt(receiver_string, receiver_string_position);
    if (!v8_flags.turbo_loop_variable) {
      // Without loop variable analysis, Turbofan's typer is unable to derive a
      // sufficiently precise type here. This is not a soundness problem, but
      // triggers graph verification errors. So we only insert the TypeGuard if
      // necessary.
      k = TypeGuard(Type::Unsigned32(), k);
    }
    Node* search_string_char = StringCharCodeAt(search_string, k);
    auto is_equal = graph()->NewNode(simplified()->NumberEqual(),
                                     receiver_string_char, search_string_char);
    GotoIfNot(is_equal, &out, FalseConstant());
  });

  Goto(&out, TrueConstant());

  Bind(&out);
  return out.PhiAt<Boolean>(0);
}

TNode<Boolean> JSCallReducerAssembler::ReduceStringPrototypeEndsWith(
    StringRef search_element_string) {
  DCHECK(search_element_string.IsContentAccessible());
  TNode<Object> receiver = ReceiverInput();
  TNode<Object> end_position = ArgumentOrUndefined(1);
  TNode<Number> zero = ZeroConstant();

  TNode<String> receiver_string = CheckString(receiver);
  TNode<Number> length = StringLength(receiver_string);
  int search_string_length = search_element_string.length();
  DCHECK_LE(search_string_length, JSCallReducer::kMaxInlineMatchSequence);

  TNode<Number> clamped_end =
      SelectIf<Number>(IsUndefined(end_position))
          .Then(_ { return length; })
          .Else(_ {
            return NumberMin(NumberMax(CheckSmi(end_position), zero), length);
          })
          .ExpectTrue()
          .Value();

  TNode<Number> start =
      NumberSubtract(clamped_end, NumberConstant(search_string_length));

  auto out = MakeLabel(MachineRepresentation::kTagged);

  TNode<Boolean> search_string_too_long = NumberLessThan(start, zero);
  GotoIf(search_string_too_long, &out, BranchHint::kFalse, FalseConstant());

  for (int i = 0; i < search_string_length; i++) {
    TNode<Number> k = NumberConstant(i);
    TNode<Number> receiver_string_position = TNode<Number>::UncheckedCast(
        TypeGuard(Type::UnsignedSmall(), NumberAdd(k, start)));
    Node* receiver_string_char =
        StringCharCodeAt(receiver_string, receiver_string_position);
    Node* search_string_char = jsgraph()->ConstantNoHole(
        search_element_string.GetChar(broker(), i).value());
    auto is_equal = graph()->NewNode(simplified()->NumberEqual(),
                                     receiver_string_char, search_string_char);
    GotoIfNot(is_equal, &out, FalseConstant());
  }

  Goto(&out, TrueConstant());

  Bind(&out);
  return out.PhiAt<Boolean>(0);
}

TNode<Boolean> JSCallReducerAssembler::ReduceStringPrototypeEndsWith() {
  TNode<Object> receiver = ReceiverInput();
  TNode<Object> search_string = ArgumentOrUndefined(0);
  TNode<Object> end_position = ArgumentOrUndefined(1);
  TNode<Number> zero = ZeroConstant();

  TNode<String> receiver_string = CheckString(receiver);
  TNode<Number> length = StringLength(receiver_string);
  TNode<String> search_element_string = CheckString(search_string);
  TNode<Number> search_string_length = StringLength(search_element_string);

  TNode<Number> clamped_end =
      SelectIf<Number>(IsUndefined(end_position))
          .Then(_ { return length; })
          .Else(_ {
            return NumberMin(NumberMax(CheckSmi(end_position), zero), length);
          })
          .ExpectTrue()
          .Value();

  TNode<Number> start = NumberSubtract(clamped_end, search_string_length);

  auto out = MakeLabel(MachineRepresentation::kTagged);

  TNode<Boolean> search_string_too_long = NumberLessThan(start, zero);
  GotoIf(search_string_too_long, &out, BranchHint::kFalse, FalseConstant());

  ForZeroUntil(search_string_length).Do([&](TNode<Number> k) {
    TNode<Number> receiver_string_position = TNode<Number>::UncheckedCast(
        TypeGuard(Type::UnsignedSmall(), NumberAdd(k, start)));
    Node* receiver_string_char =
        StringCharCodeAt(receiver_string, receiver_string_position);
    if (!v8_flags.turbo_loop_variable) {
      // Without loop variable analysis, Turbofan's typer is unable to derive a
      // sufficiently precise type here. This is not a soundness problem, but
      // triggers graph verification errors. So we only insert the TypeGuard if
      // necessary.
      k = TypeGuard(Type::Unsigned32(), k);
    }
    Node* search_string_char = StringCharCodeAt(search_element_string, k);
    auto is_equal = graph()->NewNode(simplified()->NumberEqual(),
                                     receiver_string_char, search_string_char);
    GotoIfNot(is_equal, &out, FalseConstant());
  });

  Goto(&out, TrueConstant());

  Bind(&out);
  return out.PhiAt<Boolean>(0);
}

TNode<String> JSCallReducerAssembler::ReduceStringPrototypeCharAt(
    StringRef s, uint32_t index) {
  DCHECK(s.IsContentAccessible());
  if (s.IsOneByteRepresentation()) {
    OptionalObjectRef elem = s.GetCharAsStringOrUndefined(broker(), index);
    TNode<String> elem_string =
        elem.has_value()
            ? TNode<String>::UncheckedCast(
                  jsgraph()->ConstantNoHole(elem.value(), broker()))
            : EmptyStringConstant();
    return elem_string;
  } else {
    const uint32_t length = static_cast<uint32_t>(s.length());
    if (index >= length) return EmptyStringConstant();
    Handle<SeqTwoByteString> flat = broker()->CanonicalPersistentHandle(
        broker()
            ->local_isolate_or_isolate()
            ->factory()
            ->NewRawTwoByteString(1, AllocationType::kOld)
            .ToHandleChecked());
    flat->SeqTwoByteStringSet(0, s.GetChar(broker(), index).value());
    TNode<String> two_byte_elem =
        TNode<String>::UncheckedCast(jsgraph()->HeapConstantNoHole(flat));
    return two_byte_elem;
  }
}

TNode<String> JSCallReducerAssembler::ReduceStringPrototypeCharAt() {
  TNode<Object> receiver = ReceiverInput();
  TNode<Object> index = ArgumentOrZero(0);

  TNode<String> receiver_string = CheckString(receiver);
  TNode<Number> index_smi = CheckSmi(index);
  TNode<Number> length = StringLength(receiver_string);

  TNode<Number> bounded_index = CheckBounds(index_smi, length);

  Node* result = StringCharCodeAt(receiver_string, bounded_index);
  TNode<String> result_string =
      StringFromSingleCharCode(TNode<Number>::UncheckedCast(result));
  return result_string;
}

TNode<String> JSCallReducerAssembler::ReduceStringPrototypeSlice() {
  TNode<Object> receiver = ReceiverInput();
  TNode<Object> start = Argument(0);
  TNode<Object> end = ArgumentOrUndefined(1);

  TNode<String> receiver_string = CheckString(receiver);
  TNode<Number> start_smi = CheckSmi(start);

  TNode<Number> length = StringLength(receiver_string);

  TNode<Number> end_smi = SelectIf<Number>(IsUndefined(end))
                              .Then(_ { return length; })
                              .Else(_ { return CheckSmi(end); })
                              .ExpectFalse()
                              .Value();

  TNode<Number> zero = TNode<Number>::UncheckedCast(ZeroConstant());
  TNode<Number> from_untyped =
      SelectIf<Number>(NumberLessThan(start_smi, zero))
          .Then(_ { return NumberMax(NumberAdd(length, start_smi), zero); })
          .Else(_ { return NumberMin(start_smi, length); })
          .ExpectFalse()
          .Value();
  // {from} is always in non-negative Smi range, but our typer cannot figure
  // that out yet.
  TNode<Smi> from = TypeGuardUnsignedSmall(from_untyped);

  TNode<Number> to_untyped =
      SelectIf<Number>(NumberLessThan(end_smi, zero))
          .Then(_ { return NumberMax(NumberAdd(length, end_smi), zero); })
          .Else(_ { return NumberMin(end_smi, length); })
          .ExpectFalse()
          .Value();
  // {to} is always in non-negative Smi range, but our typer cannot figure that
  // out yet.
  TNode<Smi> to = TypeGuardUnsignedSmall(to_untyped);

  return SelectIf<String>(NumberLessThan(from, to))
      .Then(_ { return StringSubstring(receiver_string, from, to); })
      .Else(_ { return EmptyStringConstant(); })
      .ExpectTrue()
      .Value();
}

TNode<Object> JSCallReducerAssembler::ReduceJSCallMathMinMaxWithArrayLike(
    Builtin builtin) {
  JSCallWithArrayLikeNode n(node_ptr());
  TNode<Object> arguments_list = n.Argument(0);

  auto call_builtin = MakeLabel();
  auto done = MakeLabel(MachineRepresentation::kTagged);

  // Check if {arguments_list} is a JSArray.
  GotoIf(ObjectIsSmi(arguments_list), &call_builtin);
  TNode<Map> arguments_list_map =
      LoadField<Map>(AccessBuilder::ForMap(),
                     TNode<HeapObject>::UncheckedCast(arguments_list));
  TNode<Number> arguments_list_instance_type = LoadField<Number>(
      AccessBuilder::ForMapInstanceType(), arguments_list_map);
  auto check_instance_type =
      NumberEqual(arguments_list_instance_type, NumberConstant(JS_ARRAY_TYPE));
  GotoIfNot(check_instance_type, &call_builtin);

  // Check if {arguments_list} has PACKED_DOUBLE_ELEMENTS.
  TNode<Number> arguments_list_elements_kind =
      LoadMapElementsKind(arguments_list_map);

  auto check_element_kind = NumberEqual(arguments_list_elements_kind,
                                        NumberConstant(PACKED_DOUBLE_ELEMENTS));
  GotoIfNot(check_element_kind, &call_builtin);

  // If {arguments_list} is a JSArray with PACKED_DOUBLE_ELEMENTS, calculate the
  // result with inlined loop.
  TNode<JSArray> array_arguments_list =
      TNode<JSArray>::UncheckedCast(arguments_list);
  Goto(&done, builtin == Builtin::kMathMax
                  ? DoubleArrayMax(array_arguments_list)
                  : DoubleArrayMin(array_arguments_list));

  // Otherwise, call BuiltinMathMin/Max as usual.
  Bind(&call_builtin);
  TNode<Object> call = CopyNode();
  CallParameters const& p = n.Parameters();

  // Set SpeculationMode to kDisallowSpeculation to avoid infinite
  // recursion.
  NodeProperties::ChangeOp(
      call, javascript()->CallWithArrayLike(
                p.frequency(), p.feedback(),
                SpeculationMode::kDisallowSpeculation, p.feedback_relation()));
  Goto(&done, call);

  Bind(&done);
  return done.PhiAt<Object>(0);
}

TNode<Object> IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypeAt(
    ZoneVector<MapRef> maps, bool needs_fallback_builtin_call) {
  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Object> index = ArgumentOrZero(0);

  TNode<Number> index_num = CheckSmi(index);
  TNode<FixedArrayBase> elements = LoadElements(receiver);

  TNode<Map> receiver_map =
      TNode<Map>::UncheckedCast(LoadField(AccessBuilder::ForMap(), receiver));

  auto out = MakeLabel(MachineRepresentation::kTagged);

  for (MapRef map : maps) {
    DCHECK(map.supports_fast_array_iteration(broker()));
    auto correct_map_label = MakeLabel(), wrong_map_label = MakeLabel();
    TNode<Boolean> is_map_equal = ReferenceEqual(receiver_map, Constant(map));
    Branch(is_map_equal, &correct_map_label, &wrong_map_label);
    Bind(&correct_map_label);

    TNode<Number> length = LoadJSArrayLength(receiver, map.elements_kind());

    // If index is less than 0, then subtract from length.
    TNode<Boolean> cond = NumberLessThan(index_num, ZeroConstant());
    TNode<Number> real_index_num =
        SelectIf<Number>(cond)
            .Then(_ { return NumberAdd(length, index_num); })
            .Else(_ { return index_num; })
            .ExpectTrue()  // Most common usage should be .at(-1)
            .Value();

    // Bound checking.
    GotoIf(NumberLessThan(real_index_num, ZeroConstant()), &out,
           UndefinedConstant());
    GotoIfNot(NumberLessThan(real_index_num, length), &out,
              UndefinedConstant());
    if (v8_flags.turbo_typer_hardening) {
      real_index_num = CheckBounds(real_index_num, length,
                                   CheckBoundsFlag::kAbortOnOutOfBounds);
    }

    // Retrieving element at index.
    TNode<Object> element = LoadElement<Object>(
        AccessBuilder::ForFixedArrayElement(map.elements_kind()), elements,
        real_index_num);
    if (IsHoleyElementsKind(map.elements_kind())) {
      // This case is needed in particular for HOLEY_DOUBLE_ELEMENTS: raw
      // doubles are stored in the FixedDoubleArray, and need to be converted to
      // HeapNumber or to Smi so that this function can return an Object. The
      // automatic converstion performed by
      // RepresentationChanger::GetTaggedRepresentationFor does not handle
      // holes, so we convert manually a potential hole here.
      element = ConvertHoleToUndefined(element, map.elements_kind());
    }
    Goto(&out, element);

    Bind(&wrong_map_label);
  }

  if (needs_fallback_builtin_call) {
    JSCallNode n(node_ptr());
    CallParameters const& p = n.Parameters();

    // We set SpeculationMode to kDisallowSpeculation to avoid infinite
    // recursion on the node we're creating (since, after all, it's calling
    // Array.Prototype.at).
    const Operator* op = javascript()->Call(
        JSCallNode::ArityForArgc(1), p.frequency(), p.feedback(),
        ConvertReceiverMode::kNotNullOrUndefined,
        SpeculationMode::kDisallowSpeculation, CallFeedbackRelation::kTarget);
    Node* fallback_builtin = node_ptr()->InputAt(0);

    TNode<Object> res = AddNode<Object>(graph()->NewNode(
        op, fallback_builtin, receiver, index, n.feedback_vector(),
        ContextInput(), n.frame_state(), effect(), control()));
    Goto(&out, res);
  } else {
    Goto(&out, UndefinedConstant());
  }

  Bind(&out);
  return out.PhiAt<Object>(0);
}

TNode<Number> IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypePush(
    MapInference* inference) {
  int const num_push_arguments = ArgumentCount();
  ZoneRefSet<Map> const& receiver_maps = inference->GetMaps();

  base::SmallVector<MachineRepresentation, 4> argument_reps;
  base::SmallVector<Node*, 4> argument_nodes;

  for (int i = 0; i < num_push_arguments; ++i) {
    argument_reps.push_back(MachineRepresentation::kTagged);
    argument_nodes.push_back(Argument(i));
  }

  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Map> receiver_map = LoadMap(receiver);

  auto double_label = MakeLabel(argument_reps);
  auto smi_label = MakeLabel(argument_reps);
  auto object_label = MakeLabel(argument_reps);

  for (size_t i = 0; i < receiver_maps.size(); i++) {
    MapRef map = receiver_maps[i];
    ElementsKind kind = map.elements_kind();

    if (i < receiver_maps.size() - 1) {
      TNode<Boolean> is_map_equal = ReferenceEqual(receiver_map, Constant(map));
      if (IsDoubleElementsKind(kind)) {
        GotoIf(is_map_equal, &double_label, argument_nodes);
      } else if (IsSmiElementsKind(kind)) {
        GotoIf(is_map_equal, &smi_label, argument_nodes);
      } else {
        GotoIf(is_map_equal, &object_label, argument_nodes);
      }
    } else {
      if (IsDoubleElementsKind(kind)) {
        Goto(&double_label, argument_nodes);
      } else if (IsSmiElementsKind(kind)) {
        Goto(&smi_label, argument_nodes);
      } else {
        Goto(&object_label, argument_nodes);
      }
    }
  }

  auto return_label = MakeLabel(MachineRepresentation::kTagged);

  auto build_array_push = [&](ElementsKind kind,
                              base::SmallVector<Node*, 1>& push_arguments) {
    // Only support PACKED_ELEMENTS and PACKED_DOUBLE_ELEMENTS, as "markers" of
    // what the elements array is (a FixedArray or FixedDoubleArray).
    DCHECK(kind == PACKED_ELEMENTS || kind == PACKED_DOUBLE_ELEMENTS);

    // Load the "length" property of the {receiver}.
    TNode<Smi> length = LoadJSArrayLength(receiver, kind);
    TNode<Number> return_value = length;

    // Check if we have any {values} to push.
    if (num_push_arguments > 0) {
      // Compute the resulting "length" of the {receiver}.
      TNode<Number> new_length = return_value =
          NumberAdd(length, NumberConstant(num_push_arguments));

      // Load the elements backing store of the {receiver}.
      TNode<FixedArrayBase> elements = LoadElements(receiver);
      TNode<Smi> elements_length = LoadFixedArrayBaseLength(elements);

      elements = MaybeGrowFastElements(
          kind, feedback(), receiver, elements,
          NumberAdd(length, NumberConstant(num_push_arguments - 1)),
          elements_length);

      // Update the JSArray::length field. Since this is observable,
      // there must be no other check after this.
      StoreJSArrayLength(receiver, new_length, kind);

      // Append the {values} to the {elements}.
      for (int i = 0; i < num_push_arguments; ++i) {
        StoreFixedArrayBaseElement(
            elements, NumberAdd(length, NumberConstant(i)),
            TNode<Object>::UncheckedCast(push_arguments[i]), kind);
      }
    }

    Goto(&return_label, return_value);
  };

  if (double_label.IsUsed()) {
    Bind(&double_label);
    base::SmallVector<Node*, 1> push_arguments(num_push_arguments);
    for (int i = 0; i < num_push_arguments; ++i) {
      Node* value =
          CheckNumber(TNode<Object>::UncheckedCast(double_label.PhiAt(i)));
      // Make sure we do not store signaling NaNs into double arrays.
      value = AddNode<Number>(
          graph()->NewNode(simplified()->NumberSilenceNaN(), value));
      push_arguments[i] = value;
    }
    build_array_push(PACKED_DOUBLE_ELEMENTS, push_arguments);
  }

  if (smi_label.IsUsed()) {
    Bind(&smi_label);
    base::SmallVector<Node*, 4> push_arguments(num_push_arguments);
    for (int i = 0; i < num_push_arguments; ++i) {
      Node* value = CheckSmi(TNode<Object>::UncheckedCast(smi_label.PhiAt(i)));
      push_arguments[i] = value;
    }
    Goto(&object_label, push_arguments);
  }

  if (object_label.IsUsed()) {
    Bind(&object_label);
    base::SmallVector<Node*, 1> push_arguments(num_push_arguments);
    for (int i = 0; i < num_push_arguments; ++i) {
      push_arguments[i] = object_label.PhiAt(i);
    }
    build_array_push(PACKED_ELEMENTS, push_arguments);
  }

  Bind(&return_label);
  return TNode<Number>::UncheckedCast(return_label.PhiAt(0));
}

namespace {

struct ForEachFrameStateParams {
  JSGraph* jsgraph;
  SharedFunctionInfoRef shared;
  TNode<Context> context;
  TNode<Object> target;
  FrameState outer_frame_state;
  TNode<Object> receiver;
  TNode<Object> callback;
  TNode<Object> this_arg;
  TNode<Object> original_length;
};

FrameState ForEachLoopLazyFrameState(const ForEachFrameStateParams& params,
                                     TNode<Object> k) {
  Builtin builtin = Builtin::kArrayForEachLoopLazyDeoptContinuation;
  Node* checkpoint_params[] = {params.receiver, params.callback,
                               params.this_arg, k, params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::LAZY);
}

FrameState ForEachLoopEagerFrameState(const ForEachFrameStateParams& params,
                                      TNode<Object> k) {
  Builtin builtin = Builtin::kArrayForEachLoopEagerDeoptContinuation;
  Node* checkpoint_params[] = {params.receiver, params.callback,
                               params.this_arg, k, params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::EAGER);
}

}  // namespace

TNode<Object>
IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypeForEach(
    MapInference* inference, const bool has_stability_dependency,
    ElementsKind kind, SharedFunctionInfoRef shared) {
  FrameState outer_frame_state = FrameStateInput();
  TNode<Context> context = ContextInput();
  TNode<Object> target = TargetInput();
  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Object> fncallback = ArgumentOrUndefined(0);
  TNode<Object> this_arg = ArgumentOrUndefined(1);

  TNode<Number> original_length = LoadJSArrayLength(receiver, kind);

  ForEachFrameStateParams frame_state_params{
      jsgraph(), shared,     context,  target,         outer_frame_state,
      receiver,  fncallback, this_arg, original_length};

  ThrowIfNotCallable(fncallback, ForEachLoopLazyFrameState(frame_state_params,
                                                           ZeroConstant()));

  ForZeroUntil(original_length).Do([&](TNode<Number> k) {
    Checkpoint(ForEachLoopEagerFrameState(frame_state_params, k));

    // Deopt if the map has changed during the iteration.
    MaybeInsertMapChecks(inference, has_stability_dependency);

    TNode<Object> element;
    std::tie(k, element) = SafeLoadElement(kind, receiver, k);

    auto continue_label = MakeLabel();
    element = MaybeSkipHole(element, kind, &continue_label);

    TNode<Number> next_k = NumberAdd(k, OneConstant());
    JSCall3(fncallback, this_arg, element, k, receiver,
            ForEachLoopLazyFrameState(frame_state_params, next_k));

    Goto(&continue_label);
    Bind(&continue_label);
  });

  return UndefinedConstant();
}

namespace {

struct ReduceFrameStateParams {
  JSGraph* jsgraph;
  SharedFunctionInfoRef shared;
  ArrayReduceDirection direction;
  TNode<Context> context;
  TNode<Object> target;
  FrameState outer_frame_state;
};

FrameState ReducePreLoopLazyFrameState(const ReduceFrameStateParams& params,
                                       TNode<Object> receiver,
                                       TNode<Object> callback, TNode<Object> k,
                                       TNode<Number> original_length) {
  Builtin builtin = (params.direction == ArrayReduceDirection::kLeft)
                        ? Builtin::kArrayReduceLoopLazyDeoptContinuation
                        : Builtin::kArrayReduceRightLoopLazyDeoptContinuation;
  Node* checkpoint_params[] = {receiver, callback, k, original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::LAZY);
}

FrameState ReducePreLoopEagerFrameState(const ReduceFrameStateParams& params,
                                        TNode<Object> receiver,
                                        TNode<Object> callback,
                                        TNode<Number> original_length) {
  Builtin builtin =
      (params.direction == ArrayReduceDirection::kLeft)
          ? Builtin::kArrayReducePreLoopEagerDeoptContinuation
          : Builtin::kArrayReduceRightPreLoopEagerDeoptContinuation;
  Node* checkpoint_params[] = {receiver, callback, original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::EAGER);
}

FrameState ReduceLoopLazyFrameState(const ReduceFrameStateParams& params,
                                    TNode<Object> receiver,
                                    TNode<Object> callback, TNode<Object> k,
                                    TNode<Number> original_length) {
  Builtin builtin = (params.direction == ArrayReduceDirection::kLeft)
                        ? Builtin::kArrayReduceLoopLazyDeoptContinuation
                        : Builtin::kArrayReduceRightLoopLazyDeoptContinuation;
  Node* checkpoint_params[] = {receiver, callback, k, original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::LAZY);
}

FrameState ReduceLoopEagerFrameState(const ReduceFrameStateParams& params,
                                     TNode<Object> receiver,
                                     TNode<Object> callback, TNode<Object> k,
                                     TNode<Number> original_length,
                                     TNode<Object> accumulator) {
  Builtin builtin = (params.direction == ArrayReduceDirection::kLeft)
                        ? Builtin::kArrayReduceLoopEagerDeoptContinuation
                        : Builtin::kArrayReduceRightLoopEagerDeoptContinuation;
  Node* checkpoint_params[] = {receiver, callback, k, original_length,
                               accumulator};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::EAGER);
}

}  // namespace

TNode<Object> IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypeReduce(
    MapInference* inference, const bool has_stability_dependency,
    ElementsKind kind, ArrayReduceDirection direction,
    SharedFunctionInfoRef shared) {
  FrameState outer_frame_state = FrameStateInput();
  TNode<Context> context = ContextInput();
  TNode<Object> target = TargetInput();
  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Object> fncallback = ArgumentOrUndefined(0);

  ReduceFrameStateParams frame_state_params{
      jsgraph(), shared, direction, context, target, outer_frame_state};

  TNode<Number> original_length = LoadJSArrayLength(receiver, kind);

  // Set up variable behavior depending on the reduction kind (left/right).
  TNode<Number> k;
  StepFunction1 step;
  ConditionFunction1 cond;
  TNode<Number> zero = ZeroConstant();
  TNode<Number> one = OneConstant();
  if (direction == ArrayReduceDirection::kLeft) {
    k = zero;
    step = [&](TNode<Number> i) { return NumberAdd(i, one); };
    cond = [&](TNode<Number> i) { return NumberLessThan(i, original_length); };
  } else {
    k = NumberSubtract(original_length, one);
    step = [&](TNode<Number> i) { return NumberSubtract(i, one); };
    cond = [&](TNode<Number> i) { return NumberLessThanOrEqual(zero, i); };
  }

  ThrowIfNotCallable(
      fncallback, ReducePreLoopLazyFrameState(frame_state_params, receiver,
                                              fncallback, k, original_length));

  // Set initial accumulator value.
  TNode<Object> accumulator;
  if (ArgumentCount() > 1) {
    accumulator = Argument(1);  // Initial value specified by the user.
  } else {
    // The initial value was not specified by the user. In this case, the first
    // (or last in the case of reduceRight) non-holey value of the array is
    // used. Loop until we find it. If not found, trigger a deopt.
    // TODO(jgruber): The deopt does not seem necessary. Instead we could simply
    // throw the TypeError here from optimized code.
    auto found_initial_element = MakeLabel(MachineRepresentation::kTagged,
                                           MachineRepresentation::kTagged);
    Forever(k, step).Do([&](TNode<Number> k) {
      Checkpoint(ReducePreLoopEagerFrameState(frame_state_params, receiver,
                                              fncallback, original_length));
      CheckIf(cond(k), DeoptimizeReason::kNoInitialElement);

      TNode<Object> element;
      std::tie(k, element) = SafeLoadElement(kind, receiver, k);

      auto continue_label = MakeLabel();
      GotoIf(HoleCheck(kind, element), &continue_label);
      Goto(&found_initial_element, k, TypeGuardNonInternal(element));

      Bind(&continue_label);
    });
    Unreachable();  // The loop is exited either by deopt or a jump to below.

    // TODO(jgruber): This manual fiddling with blocks could be avoided by
    // implementing a `break` mechanic for loop builders.
    Bind(&found_initial_element);
    k = step(found_initial_element.PhiAt<Number>(0));
    accumulator = found_initial_element.PhiAt<Object>(1);
  }

  TNode<Object> result =
      For1(k, cond, step, accumulator)
          .Do([&](TNode<Number> k, TNode<Object>* accumulator) {
            Checkpoint(ReduceLoopEagerFrameState(frame_state_params, receiver,
                                                 fncallback, k, original_length,
                                                 *accumulator));

            // Deopt if the map has changed during the iteration.
            MaybeInsertMapChecks(inference, has_stability_dependency);

            TNode<Object> element;
            std::tie(k, element) = SafeLoadElement(kind, receiver, k);

            auto continue_label = MakeLabel(MachineRepresentation::kTagged);
            element =
                MaybeSkipHole(element, kind, &continue_label, *accumulator);

            TNode<Number> next_k = step(k);
            TNode<Object> next_accumulator = JSCall4(
                fncallback, UndefinedConstant(), *accumulator, element, k,
                receiver,
                ReduceLoopLazyFrameState(frame_state_params, receiver,
                                         fncallback, next_k, original_length));
            Goto(&continue_label, next_accumulator);

            Bind(&continue_label);
            *accumulator = continue_label.PhiAt<Object>(0);
          })
          .Value();

  return result;
}

namespace {

struct MapFrameStateParams {
  JSGraph* jsgraph;
  SharedFunctionInfoRef shared;
  TNode<Context> context;
  TNode<Object> target;
  FrameState outer_frame_state;
  TNode<Object> receiver;
  TNode<Object> callback;
  TNode<Object> this_arg;
  base::Optional<TNode<JSArray>> a;
  TNode<Object> original_length;
};

FrameState MapPreLoopLazyFrameState(const MapFrameStateParams& params) {
  DCHECK(!params.a);
  Node* checkpoint_params[] = {params.receiver, params.callback,
                               params.this_arg, params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared,
      Builtin::kArrayMapPreLoopLazyDeoptContinuation, params.target,
      params.context, checkpoint_params, arraysize(checkpoint_params),
      params.outer_frame_state, ContinuationFrameStateMode::LAZY);
}

FrameState MapLoopLazyFrameState(const MapFrameStateParams& params,
                                 TNode<Number> k) {
  Node* checkpoint_params[] = {
      params.receiver,       params.callback, params.this_arg, *params.a, k,
      params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared,
      Builtin::kArrayMapLoopLazyDeoptContinuation, params.target,
      params.context, checkpoint_params, arraysize(checkpoint_params),
      params.outer_frame_state, ContinuationFrameStateMode::LAZY);
}

FrameState MapLoopEagerFrameState(const MapFrameStateParams& params,
                                  TNode<Number> k) {
  Node* checkpoint_params[] = {
      params.receiver,       params.callback, params.this_arg, *params.a, k,
      params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared,
      Builtin::kArrayMapLoopEagerDeoptContinuation, params.target,
      params.context, checkpoint_params, arraysize(checkpoint_params),
      params.outer_frame_state, ContinuationFrameStateMode::EAGER);
}

}  // namespace

TNode<JSArray> IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypeMap(
    MapInference* inference, const bool has_stability_dependency,
    ElementsKind kind, SharedFunctionInfoRef shared,
    NativeContextRef native_context) {
  FrameState outer_frame_state = FrameStateInput();
  TNode<Context> context = ContextInput();
  TNode<Object> target = TargetInput();
  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Object> fncallback = ArgumentOrUndefined(0);
  TNode<Object> this_arg = ArgumentOrUndefined(1);

  TNode<Number> original_length = LoadJSArrayLength(receiver, kind);

  // If the array length >= kMaxFastArrayLength, then CreateArray
  // will create a dictionary. We should deopt in this case, and make sure
  // not to attempt inlining again.
  original_length = CheckBounds(original_length,
                                NumberConstant(JSArray::kMaxFastArrayLength));

  // Even though {JSCreateArray} is not marked as {kNoThrow}, we can elide the
  // exceptional projections because it cannot throw with the given
  // parameters.
  TNode<Object> array_ctor =
      Constant(native_context.GetInitialJSArrayMap(broker(), kind)
                   .GetConstructor(broker()));

  MapFrameStateParams frame_state_params{
      jsgraph(), shared,     context,  target,       outer_frame_state,
      receiver,  fncallback, this_arg, {} /* TBD */, original_length};

  TNode<JSArray> a =
      CreateArrayNoThrow(array_ctor, original_length,
                         MapPreLoopLazyFrameState(frame_state_params));
  frame_state_params.a = a;

  ThrowIfNotCallable(fncallback,
                     MapLoopLazyFrameState(frame_state_params, ZeroConstant()));

  ForZeroUntil(original_length).Do([&](TNode<Number> k) {
    Checkpoint(MapLoopEagerFrameState(frame_state_params, k));
    MaybeInsertMapChecks(inference, has_stability_dependency);

    TNode<Object> element;
    std::tie(k, element) = SafeLoadElement(kind, receiver, k);

    auto continue_label = MakeLabel();
    element = MaybeSkipHole(element, kind, &continue_label);

    TNode<Object> v = JSCall3(fncallback, this_arg, element, k, receiver,
                              MapLoopLazyFrameState(frame_state_params, k));

    // The array {a} should be HOLEY_SMI_ELEMENTS because we'd only come into
    // this loop if the input array length is non-zero, and "new Array({x > 0})"
    // always produces a HOLEY array.
    MapRef holey_double_map =
        native_context.GetInitialJSArrayMap(broker(), HOLEY_DOUBLE_ELEMENTS);
    MapRef holey_map =
        native_context.GetInitialJSArrayMap(broker(), HOLEY_ELEMENTS);
    TransitionAndStoreElement(holey_double_map, holey_map, a, k, v);

    Goto(&continue_label);
    Bind(&continue_label);
  });

  return a;
}

namespace {

struct FilterFrameStateParams {
  JSGraph* jsgraph;
  SharedFunctionInfoRef shared;
  TNode<Context> context;
  TNode<Object> target;
  FrameState outer_frame_state;
  TNode<Object> receiver;
  TNode<Object> callback;
  TNode<Object> this_arg;
  TNode<JSArray> a;
  TNode<Object> original_length;
};

FrameState FilterLoopLazyFrameState(const FilterFrameStateParams& params,
                                    TNode<Number> k, TNode<Number> to,
                                    TNode<Object> element) {
  Node* checkpoint_params[] = {params.receiver,
                               params.callback,
                               params.this_arg,
                               params.a,
                               k,
                               params.original_length,
                               element,
                               to};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared,
      Builtin::kArrayFilterLoopLazyDeoptContinuation, params.target,
      params.context, checkpoint_params, arraysize(checkpoint_params),
      params.outer_frame_state, ContinuationFrameStateMode::LAZY);
}

FrameState FilterLoopEagerPostCallbackFrameState(
    const FilterFrameStateParams& params, TNode<Number> k, TNode<Number> to,
    TNode<Object> element, TNode<Object> callback_value) {
  // Note that we are intentionally reusing the
  // Builtin::kArrayFilterLoopLazyDeoptContinuation as an *eager* entry
  // point in this case. This is safe, because re-evaluating a [ToBoolean]
  // coercion is safe.
  Node* checkpoint_params[] = {params.receiver,
                               params.callback,
                               params.this_arg,
                               params.a,
                               k,
                               params.original_length,
                               element,
                               to,
                               callback_value};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared,
      Builtin::kArrayFilterLoopLazyDeoptContinuation, params.target,
      params.context, checkpoint_params, arraysize(checkpoint_params),
      params.outer_frame_state, ContinuationFrameStateMode::EAGER);
}

FrameState FilterLoopEagerFrameState(const FilterFrameStateParams& params,
                                     TNode<Number> k, TNode<Number> to) {
  Node* checkpoint_params[] = {params.receiver,
                               params.callback,
                               params.this_arg,
                               params.a,
                               k,
                               params.original_length,
                               to};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared,
      Builtin::kArrayFilterLoopEagerDeoptContinuation, params.target,
      params.context, checkpoint_params, arraysize(checkpoint_params),
      params.outer_frame_state, ContinuationFrameStateMode::EAGER);
}

}  // namespace

TNode<JSArray>
IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypeFilter(
    MapInference* inference, const bool has_stability_dependency,
    ElementsKind kind, SharedFunctionInfoRef shared,
    NativeContextRef native_context) {
  FrameState outer_frame_state = FrameStateInput();
  TNode<Context> context = ContextInput();
  TNode<Object> target = TargetInput();
  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Object> fncallback = ArgumentOrUndefined(0);
  TNode<Object> this_arg = ArgumentOrUndefined(1);

  // The output array is packed (filter doesn't visit holes).
  const ElementsKind packed_kind = GetPackedElementsKind(kind);
  TNode<JSArray> a = AllocateEmptyJSArray(packed_kind, native_context);

  TNode<Number> original_length = LoadJSArrayLength(receiver, kind);

  FilterFrameStateParams frame_state_params{
      jsgraph(), shared,     context,  target, outer_frame_state,
      receiver,  fncallback, this_arg, a,      original_length};

  // This frame state doesn't ever call the deopt continuation, it's only
  // necessary to specify a continuation in order to handle the exceptional
  // case. We don't have all the values available to completely fill out
  // the checkpoint parameters yet, but that's okay because it'll never be
  // called.
  TNode<Number> zero = ZeroConstant();
  ThrowIfNotCallable(fncallback, FilterLoopLazyFrameState(frame_state_params,
                                                          zero, zero, zero));

  TNode<Number> initial_a_length = zero;
  For1ZeroUntil(original_length, initial_a_length)
      .Do([&](TNode<Number> k, TNode<Object>* a_length_object) {
        TNode<Number> a_length = TNode<Number>::UncheckedCast(*a_length_object);
        Checkpoint(FilterLoopEagerFrameState(frame_state_params, k, a_length));
        MaybeInsertMapChecks(inference, has_stability_dependency);

        TNode<Object> element;
        std::tie(k, element) = SafeLoadElement(kind, receiver, k);

        auto continue_label = MakeLabel(MachineRepresentation::kTaggedSigned);
        element = MaybeSkipHole(element, kind, &continue_label, a_length);

        TNode<Object> v = JSCall3(
            fncallback, this_arg, element, k, receiver,
            FilterLoopLazyFrameState(frame_state_params, k, a_length, element));

        // We need an eager frame state for right after the callback function
        // returned, just in case an attempt to grow the output array fails.
        Checkpoint(FilterLoopEagerPostCallbackFrameState(frame_state_params, k,
                                                         a_length, element, v));

        GotoIfNot(ToBoolean(v), &continue_label, a_length);

        // Since the callback returned a trueish value, store the element in a.
        {
          TNode<Number> a_length1 = TypeGuardFixedArrayLength(a_length);
          TNode<FixedArrayBase> elements = LoadElements(a);
          elements = MaybeGrowFastElements(kind, FeedbackSource{}, a, elements,
                                           a_length1,
                                           LoadFixedArrayBaseLength(elements));

          TNode<Number> new_a_length = NumberInc(a_length1);
          StoreJSArrayLength(a, new_a_length, kind);
          StoreFixedArrayBaseElement(elements, a_length1, element, kind);

          Goto(&continue_label, new_a_length);
        }

        Bind(&continue_label);
        *a_length_object =
            TNode<Object>::UncheckedCast(continue_label.PhiAt(0));
      })
      .ValueIsUnused();

  return a;
}

namespace {

struct FindFrameStateParams {
  JSGraph* jsgraph;
  SharedFunctionInfoRef shared;
  TNode<Context> context;
  TNode<Object> target;
  FrameState outer_frame_state;
  TNode<Object> receiver;
  TNode<Object> callback;
  TNode<Object> this_arg;
  TNode<Object> original_length;
};

FrameState FindLoopLazyFrameState(const FindFrameStateParams& params,
                                  TNode<Number> k, ArrayFindVariant variant) {
  Builtin builtin = (variant == ArrayFindVariant::kFind)
                        ? Builtin::kArrayFindLoopLazyDeoptContinuation
                        : Builtin::kArrayFindIndexLoopLazyDeoptContinuation;
  Node* checkpoint_params[] = {params.receiver, params.callback,
                               params.this_arg, k, params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::LAZY);
}

FrameState FindLoopEagerFrameState(const FindFrameStateParams& params,
                                   TNode<Number> k, ArrayFindVariant variant) {
  Builtin builtin = (variant == ArrayFindVariant::kFind)
                        ? Builtin::kArrayFindLoopEagerDeoptContinuation
                        : Builtin::kArrayFindIndexLoopEagerDeoptContinuation;
  Node* checkpoint_params[] = {params.receiver, params.callback,
                               params.this_arg, k, params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::EAGER);
}

FrameState FindLoopAfterCallbackLazyFrameState(
    const FindFrameStateParams& params, TNode<Number> next_k,
    TNode<Object> if_found_value, ArrayFindVariant variant) {
  Builtin builtin =
      (variant == ArrayFindVariant::kFind)
          ? Builtin::kArrayFindLoopAfterCallbackLazyDeoptContinuation
          : Builtin::kArrayFindIndexLoopAfterCallbackLazyDeoptContinuation;
  Node* checkpoint_params[] = {params.receiver,        params.callback,
                               params.this_arg,        next_k,
                               params.original_length, if_found_value};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::LAZY);
}

}  // namespace

TNode<Object> IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypeFind(
    MapInference* inference, const bool has_stability_dependency,
    ElementsKind kind, SharedFunctionInfoRef shared,
    NativeContextRef native_context, ArrayFindVariant variant) {
  FrameState outer_frame_state = FrameStateInput();
  TNode<Context> context = ContextInput();
  TNode<Object> target = TargetInput();
  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Object> fncallback = ArgumentOrUndefined(0);
  TNode<Object> this_arg = ArgumentOrUndefined(1);

  TNode<Number> original_length = LoadJSArrayLength(receiver, kind);

  FindFrameStateParams frame_state_params{
      jsgraph(), shared,     context,  target,         outer_frame_state,
      receiver,  fncallback, this_arg, original_length};

  ThrowIfNotCallable(
      fncallback,
      FindLoopLazyFrameState(frame_state_params, ZeroConstant(), variant));

  const bool is_find_variant = (variant == ArrayFindVariant::kFind);
  auto out = MakeLabel(MachineRepresentation::kTagged);

  ForZeroUntil(original_length).Do([&](TNode<Number> k) {
    Checkpoint(FindLoopEagerFrameState(frame_state_params, k, variant));
    MaybeInsertMapChecks(inference, has_stability_dependency);

    TNode<Object> element;
    std::tie(k, element) = SafeLoadElement(kind, receiver, k);

    if (IsHoleyElementsKind(kind)) {
      element = ConvertHoleToUndefined(element, kind);
    }

    TNode<Object> if_found_value = is_find_variant ? element : k;
    TNode<Number> next_k = NumberInc(k);

    // The callback result states whether the desired element was found.
    TNode<Object> v =
        JSCall3(fncallback, this_arg, element, k, receiver,
                FindLoopAfterCallbackLazyFrameState(frame_state_params, next_k,
                                                    if_found_value, variant));

    GotoIf(ToBoolean(v), &out, if_found_value);
  });

  // If the loop completed, the element was not found.
  TNode<Object> if_not_found_value =
      is_find_variant ? TNode<Object>::UncheckedCast(UndefinedConstant())
                      : TNode<Object>::UncheckedCast(MinusOneConstant());
  Goto(&out, if_not_found_value);

  Bind(&out);
  return out.PhiAt<Object>(0);
}

namespace {

struct EverySomeFrameStateParams {
  JSGraph* jsgraph;
  SharedFunctionInfoRef shared;
  TNode<Context> context;
  TNode<Object> target;
  FrameState outer_frame_state;
  TNode<Object> receiver;
  TNode<Object> callback;
  TNode<Object> this_arg;
  TNode<Object> original_length;
};

FrameState EverySomeLoopLazyFrameState(const EverySomeFrameStateParams& params,
                                       TNode<Number> k,
                                       ArrayEverySomeVariant variant) {
  Builtin builtin = (variant == ArrayEverySomeVariant::kEvery)
                        ? Builtin::kArrayEveryLoopLazyDeoptContinuation
                        : Builtin::kArraySomeLoopLazyDeoptContinuation;
  Node* checkpoint_params[] = {params.receiver, params.callback,
                               params.this_arg, k, params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::LAZY);
}

FrameState EverySomeLoopEagerFrameState(const EverySomeFrameStateParams& params,
                                        TNode<Number> k,
                                        ArrayEverySomeVariant variant) {
  Builtin builtin = (variant == ArrayEverySomeVariant::kEvery)
                        ? Builtin::kArrayEveryLoopEagerDeoptContinuation
                        : Builtin::kArraySomeLoopEagerDeoptContinuation;
  Node* checkpoint_params[] = {params.receiver, params.callback,
                               params.this_arg, k, params.original_length};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared, builtin, params.target, params.context,
      checkpoint_params, arraysize(checkpoint_params), params.outer_frame_state,
      ContinuationFrameStateMode::EAGER);
}

}  // namespace

TNode<Boolean>
IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypeEverySome(
    MapInference* inference, const bool has_stability_dependency,
    ElementsKind kind, SharedFunctionInfoRef shared,
    NativeContextRef native_context, ArrayEverySomeVariant variant) {
  FrameState outer_frame_state = FrameStateInput();
  TNode<Context> context = ContextInput();
  TNode<Object> target = TargetInput();
  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Object> fncallback = ArgumentOrUndefined(0);
  TNode<Object> this_arg = ArgumentOrUndefined(1);

  TNode<Number> original_length = LoadJSArrayLength(receiver, kind);

  EverySomeFrameStateParams frame_state_params{
      jsgraph(), shared,     context,  target,         outer_frame_state,
      receiver,  fncallback, this_arg, original_length};

  ThrowIfNotCallable(
      fncallback,
      EverySomeLoopLazyFrameState(frame_state_params, ZeroConstant(), variant));

  auto out = MakeLabel(MachineRepresentation::kTagged);

  ForZeroUntil(original_length).Do([&](TNode<Number> k) {
    Checkpoint(EverySomeLoopEagerFrameState(frame_state_params, k, variant));
    MaybeInsertMapChecks(inference, has_stability_dependency);

    TNode<Object> element;
    std::tie(k, element) = SafeLoadElement(kind, receiver, k);

    auto continue_label = MakeLabel();
    element = MaybeSkipHole(element, kind, &continue_label);

    TNode<Object> v =
        JSCall3(fncallback, this_arg, element, k, receiver,
                EverySomeLoopLazyFrameState(frame_state_params, k, variant));

    if (variant == ArrayEverySomeVariant::kEvery) {
      GotoIfNot(ToBoolean(v), &out, FalseConstant());
    } else {
      DCHECK_EQ(variant, ArrayEverySomeVariant::kSome);
      GotoIf(ToBoolean(v), &out, TrueConstant());
    }
    Goto(&continue_label);
    Bind(&continue_label);
  });

  Goto(&out, (variant == ArrayEverySomeVariant::kEvery) ? TrueConstant()
                                                        : FalseConstant());

  Bind(&out);
  return out.PhiAt<Boolean>(0);
}

namespace {

Callable GetCallableForArrayIndexOfIncludes(ArrayIndexOfIncludesVariant variant,
                                            ElementsKind elements_kind,
                                            Isolate* isolate) {
  if (variant == ArrayIndexOfIncludesVariant::kIndexOf) {
    switch (elements_kind) {
      case PACKED_SMI_ELEMENTS:
      case HOLEY_SMI_ELEMENTS:
        return Builtins::CallableFor(isolate, Builtin::kArrayIndexOfSmi);
      case PACKED_ELEMENTS:
      case HOLEY_ELEMENTS:
        return Builtins::CallableFor(isolate,
                                     Builtin::kArrayIndexOfSmiOrObject);
      case PACKED_DOUBLE_ELEMENTS:
        return Builtins::CallableFor(isolate,
                                     Builtin::kArrayIndexOfPackedDoubles);
      default:
        DCHECK_EQ(HOLEY_DOUBLE_ELEMENTS, elements_kind);
        return Builtins::CallableFor(isolate,
                                     Builtin::kArrayIndexOfHoleyDoubles);
    }
  } else {
    DCHECK_EQ(variant, ArrayIndexOfIncludesVariant::kIncludes);
    switch (elements_kind) {
      case PACKED_SMI_ELEMENTS:
      case HOLEY_SMI_ELEMENTS:
        return Builtins::CallableFor(isolate, Builtin::kArrayIncludesSmi);
      case PACKED_ELEMENTS:
      case HOLEY_ELEMENTS:
        return Builtins::CallableFor(isolate,
                                     Builtin::kArrayIncludesSmiOrObject);
      case PACKED_DOUBLE_ELEMENTS:
        return Builtins::CallableFor(isolate,
                                     Builtin::kArrayIncludesPackedDoubles);
      default:
        DCHECK_EQ(HOLEY_DOUBLE_ELEMENTS, elements_kind);
        return Builtins::CallableFor(isolate,
                                     Builtin::kArrayIncludesHoleyDoubles);
    }
  }
  UNREACHABLE();
}

}  // namespace
TNode<Object>
IteratingArrayBuiltinReducerAssembler::ReduceArrayPrototypeIndexOfIncludes(
    ElementsKind kind, ArrayIndexOfIncludesVariant variant) {
  TNode<Context> context = ContextInput();
  TNode<JSArray> receiver = ReceiverInputAs<JSArray>();
  TNode<Object> search_element = ArgumentOrUndefined(0);
  TNode<Object> from_index = ArgumentOrZero(1);

  // TODO(jgruber): This currently only reduces to a stub call. Create a full
  // reduction (similar to other higher-order array builtins) instead of
  // lowering to a builtin call. E.g. Array.p.every and Array.p.some have almost
  // identical functionality.

  TNode<Number> length = LoadJSArrayLength(receiver, kind);
  TNode<FixedArrayBase> elements = LoadElements(receiver);

  const bool have_from_index = ArgumentCount() > 1;
  if (have_from_index) {
    TNode<Smi> from_index_smi = CheckSmi(from_index);

    // If the index is negative, it means the offset from the end and
    // therefore needs to be added to the length. If the result is still
    // negative, it needs to be clamped to 0.
    TNode<Boolean> cond = NumberLessThan(from_index_smi, ZeroConstant());
    from_index = SelectIf<Number>(cond)
                     .Then(_ {
                       return NumberMax(NumberAdd(length, from_index_smi),
                                        ZeroConstant());
                     })
                     .Else(_ { return from_index_smi; })
                     .ExpectFalse()
                     .Value();
  }

  return Call4(GetCallableForArrayIndexOfIncludes(variant, kind, isolate()),
               context, elements, search_element, length, from_index);
}
namespace {

struct PromiseCtorFrameStateParams {
  JSGraph* jsgraph;
  SharedFunctionInfoRef shared;
  Node* node_ptr;
  TNode<Context> context;
  TNode<Object> target;
  FrameState outer_frame_state;
};

// Remnant of old-style JSCallReducer code. Could be ported to graph assembler,
// but probably not worth the effort.
FrameState CreateConstructInvokeStubFrameState(
    Node* node, Node* outer_frame_state, SharedFunctionInfoRef shared,
    Node* context, CommonOperatorBuilder* common, Graph* graph) {
  const FrameStateFunctionInfo* state_info =
      common->CreateFrameStateFunctionInfo(FrameStateType::kConstructInvokeStub,
                                           1, 0, shared.object());

  const Operator* op = common->FrameState(
      BytecodeOffset::None(), OutputFrameStateCombine::Ignore(), state_info);
  const Operator* op0 = common->StateValues(0, SparseInputMask::Dense());
  Node* node0 = graph->NewNode(op0);

  static constexpr int kTargetInputIndex = 0;
  static constexpr int kReceiverInputIndex = 1;
  std::vector<Node*> params;
  params.push_back(node->InputAt(kReceiverInputIndex));
  const Operator* op_param = common->StateValues(
      static_cast<int>(params.size()), SparseInputMask::Dense());
  Node* params_node = graph->NewNode(op_param, static_cast<int>(params.size()),
                                     &params.front());
  DCHECK(context);
  return FrameState(graph->NewNode(op, params_node, node0, node0, context,
                                   node->InputAt(kTargetInputIndex),
                                   outer_frame_state));
}

FrameState PromiseConstructorFrameState(
    const PromiseCtorFrameStateParams& params, CommonOperatorBuilder* common,
    Graph* graph) {
  DCHECK_EQ(1,
            params.shared.internal_formal_parameter_count_without_receiver());
  return CreateConstructInvokeStubFrameState(
      params.node_ptr, params.outer_frame_state, params.shared, params.context,
      common, graph);
}

FrameState PromiseConstructorLazyFrameState(
    const PromiseCtorFrameStateParams& params,
    FrameState constructor_frame_state) {
  // The deopt continuation of this frame state is never called; the frame state
  // is only necessary to obtain the right stack trace.
  JSGraph* jsgraph = params.jsgraph;
  Node* checkpoint_params[] = {
      jsgraph->UndefinedConstant(), /* receiver */
      jsgraph->UndefinedConstant(), /* promise */
      jsgraph->UndefinedConstant(), /* reject function */
      jsgraph->TheHoleConstant()    /* exception */
  };
  return CreateJavaScriptBuiltinContinuationFrameState(
      jsgraph, params.shared, Builtin::kPromiseConstructorLazyDeoptContinuation,
      params.target, params.context, checkpoint_params,
      arraysize(checkpoint_params), constructor_frame_state,
      ContinuationFrameStateMode::LAZY);
}

FrameState PromiseConstructorLazyWithCatchFrameState(
    const PromiseCtorFrameStateParams& params,
    FrameState constructor_frame_state, TNode<JSPromise> promise,
    TNode<JSFunction> reject) {
  // This continuation just returns the created promise and takes care of
  // exceptions thrown by the executor.
  Node* checkpoint_params[] = {
      params.jsgraph->UndefinedConstant(), /* receiver */
      promise, reject};
  return CreateJavaScriptBuiltinContinuationFrameState(
      params.jsgraph, params.shared,
      Builtin::kPromiseConstructorLazyDeoptContinuation, params.target,
      params.context, checkpoint_params, arraysize(checkpoint_params),
      constructor_frame_state, ContinuationFrameStateMode::LAZY_WITH_CATCH);
}

}  // namespace

TNode<Object> PromiseBuiltinReducerAssembler::ReducePromiseConstructor(
    NativeContextRef native_context) {
  DCHECK_GE(ConstructArity(), 1);

  JSConstructNode n(node_ptr());
  FrameState outer_frame_state = FrameStateInput();
  TNode<Context> context = ContextInput();
  TNode<Object> target = TargetInput();
  TNode<Object> executor = n.Argument(0);
  DCHECK_EQ(target, NewTargetInput());

  SharedFunctionInfoRef promise_shared =
      native_context.promise_function(broker()).shared(broker());

  PromiseCtorFrameStateParams frame_state_params{jsgraph(),  promise_shared,
                                                 node_ptr(), context,
                                                 target,     outer_frame_state};

  // Insert a construct stub frame into the chain of frame states. This will
  // reconstruct the proper frame when deoptimizing within the constructor.
  // For the frame state, we only provide the executor parameter, even if more
  // arguments were passed. This is not observable from JS.
  FrameState constructor_frame_state =
      PromiseConstructorFrameState(frame_state_params, common(), graph());

  ThrowIfNotCallable(executor,
                     PromiseConstructorLazyFrameState(frame_state_params,
                                                      constructor_frame_state));

  TNode<JSPromise> promise = CreatePromise(context);

  // 8. CreatePromiseResolvingFunctions
  // Allocate a promise context for the closures below.
  TNode<Context> promise_context = CreateFunctionContext(
      native_context, context, PromiseBuiltins::kPromiseContextLength);
  StoreContextSlot(promise_context, PromiseBuiltins::kPromiseSlot, promise);
  StoreContextSlot(promise_context, PromiseBuiltins::kAlreadyResolvedSlot,
                   FalseConstant());
  StoreContextSlot(promise_context, PromiseBuiltins::kDebugEventSlot,
                   TrueConstant());

  // Allocate closures for the resolve and reject cases.
  SharedFunctionInfoRef resolve_sfi =
      MakeRef(broker(), broker()
                            ->isolate()
                            ->factory()
                            ->promise_capability_default_resolve_shared_fun());
  TNode<JSFunction> resolve =
      CreateClosureFromBuiltinSharedFunctionInfo(resolve_sfi, promise_context);

  SharedFunctionInfoRef reject_sfi =
      MakeRef(broker(), broker()
                            ->isolate()
                            ->factory()
                            ->promise_capability_default_reject_shared_fun());
  TNode<JSFunction> reject =
      CreateClosureFromBuiltinSharedFunctionInfo(reject_sfi, promise_context);

  FrameState lazy_with_catch_frame_state =
      PromiseConstructorLazyWithCatchFrameState(
          frame_state_params, constructor_frame_state, promise, reject);

  // 9. Call executor with both resolving functions.
  // 10a. Call reject if the call to executor threw.
  Try(_ {
    CallPromiseExecutor(executor, resolve, reject, lazy_with_catch_frame_state);
  }).Catch([&](TNode<Object> exception) {
    // Clear pending message since the exception is not going to be rethrown.
    ClearPendingMessage();
    CallPromiseReject(reject, exception, lazy_with_catch_frame_state);
  });

  return promise;
}

#undef _

std::pair<Node*, Node*> JSCallReducer::ReleaseEffectAndControlFromAssembler(
    JSCallReducerAssembler* gasm) {
  auto catch_scope = gasm->catch_scope();
  DCHECK(catch_scope->is_outermost());

  if (catch_scope->has_handler() &&
      catch_scope->has_exceptional_control_flow()) {
    TNode<Object> handler_exception;
    Effect handler_effect{nullptr};
    Control handler_control{nullptr};
    gasm->catch_scope()->MergeExceptionalPaths(
        &handler_exception, &handler_effect, &handler_control);

    ReplaceWithValue(gasm->outermost_handler(), handler_exception,
                     handler_effect, handler_control);
  }

  return {gasm->effect(), gasm->control()};
}

Reduction JSCallReducer::ReplaceWithSubgraph(JSCallReducerAssembler* gasm,
                                             Node* subgraph) {
  // TODO(jgruber): Consider a less fiddly way of integrating the new subgraph
  // into the outer graph. For instance, the subgraph could be created in
  // complete isolation, and then plugged into the outer graph in one go.
  // Instead of manually tracking IfException nodes, we could iterate the
  // subgraph.

  // Replace the Call node with the newly-produced subgraph.
  ReplaceWithValue(gasm->node_ptr(), subgraph, gasm->effect(), gasm->control());

  // Wire exception edges contained in the newly-produced subgraph into the
  // outer graph.
  auto catch_scope = gasm->catch_scope();
  DCHECK(catch_scope->is_outermost());

  if (catch_scope->has_handler() &&
      catch_scope->has_exceptional_control_flow()) {
    TNode<Object> handler_exception;
    Effect handler_effect{nullptr};
    Control handler_control{nullptr};
    gasm->catch_scope()->MergeExceptionalPaths(
        &handler_exception, &handler_effect, &handler_control);

    ReplaceWithValue(gasm->outermost_handler(), handler_exception,
                     handler_effect, handler_control);
  }

  return Replace(subgraph);
}

Reduction JSCallReducer::ReduceMathUnary(Node* node, const Operator* op) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->NaNConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }

  JSCallReducerAssembler a(this, node);
  Node* subgraph = a.ReduceMathUnary(op);
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceMathBinary(Node* node, const Operator* op) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->NaNConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }

  JSCallReducerAssembler a(this, node);
  Node* subgraph = a.ReduceMathBinary(op);
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES6 section 20.2.2.19 Math.imul ( x, y )
Reduction JSCallReducer::ReduceMathImul(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->ZeroConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* left = n.Argument(0);
  Node* right = n.ArgumentOr(1, jsgraph()->ZeroConstant());
  Effect effect = n.effect();
  Control control = n.control();

  left = effect =
      graph()->NewNode(simplified()->SpeculativeToNumber(
                           NumberOperationHint::kNumberOrOddball, p.feedback()),
                       left, effect, control);
  right = effect =
      graph()->NewNode(simplified()->SpeculativeToNumber(
                           NumberOperationHint::kNumberOrOddball, p.feedback()),
                       right, effect, control);
  left = graph()->NewNode(simplified()->NumberToUint32(), left);
  right = graph()->NewNode(simplified()->NumberToUint32(), right);
  Node* value = graph()->NewNode(simplified()->NumberImul(), left, right);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

// ES6 section 20.2.2.11 Math.clz32 ( x )
Reduction JSCallReducer::ReduceMathClz32(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->ConstantNoHole(32);
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* input = n.Argument(0);
  Effect effect = n.effect();
  Control control = n.control();

  input = effect =
      graph()->NewNode(simplified()->SpeculativeToNumber(
                           NumberOperationHint::kNumberOrOddball, p.feedback()),
                       input, effect, control);
  input = graph()->NewNode(simplified()->NumberToUint32(), input);
  Node* value = graph()->NewNode(simplified()->NumberClz32(), input);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

// ES6 section 20.2.2.24 Math.max ( value1, value2, ...values )
// ES6 section 20.2.2.25 Math.min ( value1, value2, ...values )
Reduction JSCallReducer::ReduceMathMinMax(Node* node, const Operator* op,
                                          Node* empty_value) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() < 1) {
    ReplaceWithValue(node, empty_value);
    return Replace(empty_value);
  }
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  Node* value = effect =
      graph()->NewNode(simplified()->SpeculativeToNumber(
                           NumberOperationHint::kNumberOrOddball, p.feedback()),
                       n.Argument(0), effect, control);
  for (int i = 1; i < n.ArgumentCount(); i++) {
    Node* input = effect = graph()->NewNode(
        simplified()->SpeculativeToNumber(NumberOperationHint::kNumberOrOddball,
                                          p.feedback()),
        n.Argument(i), effect, control);
    value = graph()->NewNode(op, value, input);
  }

  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

Reduction JSCallReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kJSConstruct:
      return ReduceJSConstruct(node);
    case IrOpcode::kJSConstructWithArrayLike:
      return ReduceJSConstructWithArrayLike(node);
    case IrOpcode::kJSConstructWithSpread:
      return ReduceJSConstructWithSpread(node);
    case IrOpcode::kJSConstructForwardAllArgs:
      return ReduceJSConstructForwardAllArgs(node);
    case IrOpcode::kJSCall:
      return ReduceJSCall(node);
    case IrOpcode::kJSCallWithArrayLike:
      return ReduceJSCallWithArrayLike(node);
    case IrOpcode::kJSCallWithSpread:
      return ReduceJSCallWithSpread(node);
    default:
      break;
  }
  return NoChange();
}

void JSCallReducer::Finalize() {
  // TODO(turbofan): This is not the best solution; ideally we would be able
  // to teach the GraphReducer about arbitrary dependencies between different
  // nodes, even if they don't show up in the use list of the other node.
  std::set<Node*> const waitlist = std::move(waitlist_);
  for (Node* node : waitlist) {
    if (!node->IsDead()) {
      // Remember the max node id before reduction.
      NodeId const max_id = static_cast<NodeId>(graph()->NodeCount() - 1);
      Reduction const reduction = Reduce(node);
      if (reduction.Changed()) {
        Node* replacement = reduction.replacement();
        if (replacement != node) {
          Replace(node, replacement, max_id);
        }
      }
    }
  }
}

// ES6 section 22.1.1 The Array Constructor
Reduction JSCallReducer::ReduceArrayConstructor(Node* node) {
  JSCallNode n(node);
  Node* target = n.target();
  CallParameters const& p = n.Parameters();

  // Turn the {node} into a {JSCreateArray} call.
  size_t const arity = p.arity_without_implicit_args();
  node->RemoveInput(n.FeedbackVectorIndex());
  NodeProperties::ReplaceValueInput(node, target, 0);
  NodeProperties::ReplaceValueInput(node, target, 1);
  NodeProperties::ChangeOp(node,
                           javascript()->CreateArray(arity, base::nullopt));
  return Changed(node);
}

// ES6 section 19.3.1.1 Boolean ( value )
Reduction JSCallReducer::ReduceBooleanConstructor(Node* node) {
  // Replace the {node} with a proper {ToBoolean} operator.
  JSCallNode n(node);
  Node* value = n.ArgumentOrUndefined(0, jsgraph());
  value = graph()->NewNode(simplified()->ToBoolean(), value);
  ReplaceWithValue(node, value);
  return Replace(value);
}

// ES section #sec-object-constructor
Reduction JSCallReducer::ReduceObjectConstructor(Node* node) {
  JSCallNode n(node);
  if (n.ArgumentCount() < 1) return NoChange();
  Node* value = n.Argument(0);
  Effect effect = n.effect();

  // We can fold away the Tagged<Object>(x) call if |x| is definitely not a
  // primitive.
  if (NodeProperties::CanBePrimitive(broker(), value, effect)) {
    if (!NodeProperties::CanBeNullOrUndefined(broker(), value, effect)) {
      // Turn the {node} into a {JSToObject} call if we know that
      // the {value} cannot be null or undefined.
      NodeProperties::ReplaceValueInputs(node, value);
      NodeProperties::ChangeOp(node, javascript()->ToObject());
      return Changed(node);
    }
  } else {
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 19.2.3.1 Function.prototype.apply ( thisArg, argArray )
Reduction JSCallReducer::ReduceFunctionPrototypeApply(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  CallFeedbackRelation new_feedback_relation =
      p.feedback_relation() == CallFeedbackRelation::kReceiver
          ? CallFeedbackRelation::kTarget
          : CallFeedbackRelation::kUnrelated;
  int arity = p.arity_without_implicit_args();

  if (arity < 2) {
    // Degenerate cases.
    ConvertReceiverMode convert_mode;
    if (arity == 0) {
      // Neither thisArg nor argArray was provided.
      convert_mode = ConvertReceiverMode::kNullOrUndefined;
      node->ReplaceInput(n.TargetIndex(), n.receiver());
      node->ReplaceInput(n.ReceiverIndex(), jsgraph()->UndefinedConstant());
    } else {
      DCHECK_EQ(arity, 1);
      // The argArray was not provided, just remove the {target}.
      convert_mode = ConvertReceiverMode::kAny;
      node->RemoveInput(n.TargetIndex());
      --arity;
    }
    // Change {node} to a {JSCall} and try to reduce further.
    NodeProperties::ChangeOp(
        node, javascript()->Call(JSCallNode::ArityForArgc(arity), p.frequency(),
                                 p.feedback(), convert_mode,
                                 p.speculation_mode(), new_feedback_relation));
    return Changed(node).FollowedBy(ReduceJSCall(node));
  }

  // Turn the JSCall into a JSCallWithArrayLike.
  // If {argArray} can be null or undefined, we have to generate branches since
  // JSCallWithArrayLike would throw for null or undefined.

  Node* target = n.receiver();
  Node* this_argument = n.Argument(0);
  Node* arguments_list = n.Argument(1);
  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Effect effect = n.effect();
  Control control = n.control();

  // If {arguments_list} cannot be null or undefined, we don't need
  // to expand this {node} to control-flow.
  if (!NodeProperties::CanBeNullOrUndefined(broker(), arguments_list, effect)) {
    // Massage the value inputs appropriately.
    node->ReplaceInput(n.TargetIndex(), target);
    node->ReplaceInput(n.ReceiverIndex(), this_argument);
    node->ReplaceInput(n.ArgumentIndex(0), arguments_list);
    while (arity-- > 1) node->RemoveInput(n.ArgumentIndex(1));

    // Morph the {node} to a {JSCallWithArrayLike}.
    NodeProperties::ChangeOp(
        node, javascript()->CallWithArrayLike(p.frequency(), p.feedback(),
                                              p.speculation_mode(),
                                              new_feedback_relation));
    return Changed(node).FollowedBy(ReduceJSCallWithArrayLike(node));
  }

  // Check whether {arguments_list} is null.
  Node* check_null =
      graph()->NewNode(simplified()->ReferenceEqual(), arguments_list,
                       jsgraph()->NullConstant());
  control = graph()->NewNode(common()->Branch(BranchHint::kFalse), check_null,
                             control);
  Node* if_null = graph()->NewNode(common()->IfTrue(), control);
  control = graph()->NewNode(common()->IfFalse(), control);

  // Check whether {arguments_list} is undefined.
  Node* check_undefined =
      graph()->NewNode(simplified()->ReferenceEqual(), arguments_list,
                       jsgraph()->UndefinedConstant());
  control = graph()->NewNode(common()->Branch(BranchHint::kFalse),
                             check_undefined, control);
  Node* if_undefined = graph()->NewNode(common()->IfTrue(), control);
  control = graph()->NewNode(common()->IfFalse(), control);

  // Lower to {JSCallWithArrayLike} if {arguments_list} is neither null
  // nor undefined.
  Node* effect0 = effect;
  Node* control0 = control;
  Node* value0 = effect0 = control0 = graph()->NewNode(
      javascript()->CallWithArrayLike(p.frequency(), p.feedback(),
                                      p.speculation_mode(),
                                      new_feedback_relation),
      target, this_argument, arguments_list, n.feedback_vector(), context,
      frame_state, effect0, control0);

  // Lower to {JSCall} if {arguments_list} is either null or undefined.
  Node* effect1 = effect;
  Node* control1 = graph()->NewNode(common()->Merge(2), if_null, if_undefined);
  Node* value1 = effect1 = control1 = graph()->NewNode(
      javascript()->Call(JSCallNode::ArityForArgc(0)), target, this_argument,
      n.feedback_vector(), context, frame_state, effect1, control1);

  // Rewire potential exception edges.
  Node* if_exception = nullptr;
  if (NodeProperties::IsExceptionalCall(node, &if_exception)) {
    // Create appropriate {IfException} and {IfSuccess} nodes.
    Node* if_exception0 =
        graph()->NewNode(common()->IfException(), control0, effect0);
    control0 = graph()->NewNode(common()->IfSuccess(), control0);
    Node* if_exception1 =
        graph()->NewNode(common()->IfException(), control1, effect1);
    control1 = graph()->NewNode(common()->IfSuccess(), control1);

    // Join the exception edges.
    Node* merge =
        graph()->NewNode(common()->Merge(2), if_exception0, if_exception1);
    Node* ephi = graph()->NewNode(common()->EffectPhi(2), if_exception0,
                                  if_exception1, merge);
    Node* phi =
        graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                         if_exception0, if_exception1, merge);
    ReplaceWithValue(if_exception, phi, ephi, merge);
  }

  // Join control paths.
  control = graph()->NewNode(common()->Merge(2), control0, control1);
  effect = graph()->NewNode(common()->EffectPhi(2), effect0, effect1, control);
  Node* value =
      graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2), value0,
                       value1, control);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES section #sec-function.prototype.bind
Reduction JSCallReducer::ReduceFunctionPrototypeBind(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  // Value inputs to the {node} are as follows:
  //
  //  - target, which is Function.prototype.bind JSFunction
  //  - receiver, which is the [[BoundTargetFunction]]
  //  - bound_this (optional), which is the [[BoundThis]]
  //  - and all the remaining value inputs are [[BoundArguments]]
  Node* receiver = n.receiver();
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();

  // Ensure that the {receiver} is known to be a JSBoundFunction or
  // a JSFunction with the same [[Prototype]], and all maps we've
  // seen for the {receiver} so far indicate that {receiver} is
  // definitely a constructor or not a constructor.
  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps()) return NoChange();
  ZoneRefSet<Map> const& receiver_maps = inference.GetMaps();

  MapRef first_receiver_map = receiver_maps[0];
  bool const is_constructor = first_receiver_map.is_constructor();

  HeapObjectRef prototype = first_receiver_map.prototype(broker());

  for (MapRef receiver_map : receiver_maps) {
    HeapObjectRef map_prototype = receiver_map.prototype(broker());

    // Check for consistency among the {receiver_maps}.
    if (!map_prototype.equals(prototype) ||
        receiver_map.is_constructor() != is_constructor ||
        !InstanceTypeChecker::IsJSFunctionOrBoundFunctionOrWrappedFunction(
            receiver_map.instance_type())) {
      return inference.NoChange();
    }

    // Disallow binding of slow-mode functions. We need to figure out
    // whether the length and name property are in the original state.
    if (receiver_map.is_dictionary_map()) return inference.NoChange();

    // Check whether the length and name properties are still present
    // as AccessorInfo objects. In that case, their values can be
    // recomputed even if the actual value of the object changes.
    // This mirrors the checks done in builtins-function-gen.cc at
    // runtime otherwise.
    int minimum_nof_descriptors =
        std::max(
            {JSFunctionOrBoundFunctionOrWrappedFunction::kLengthDescriptorIndex,
             JSFunctionOrBoundFunctionOrWrappedFunction::
                 kNameDescriptorIndex}) +
        1;
    if (receiver_map.NumberOfOwnDescriptors() < minimum_nof_descriptors) {
      return inference.NoChange();
    }
    const InternalIndex kLengthIndex(
        JSFunctionOrBoundFunctionOrWrappedFunction::kLengthDescriptorIndex);
    const InternalIndex kNameIndex(
        JSFunctionOrBoundFunctionOrWrappedFunction::kNameDescriptorIndex);
    StringRef length_string = broker()->length_string();
    StringRef name_string = broker()->name_string();

    OptionalObjectRef length_value(
        receiver_map.GetStrongValue(broker(), kLengthIndex));
    OptionalObjectRef name_value(
        receiver_map.GetStrongValue(broker(), kNameIndex));
    if (!length_value || !name_value) {
      TRACE_BROKER_MISSING(
          broker(), "name or length descriptors on map " << receiver_map);
      return inference.NoChange();
    }
    if (!receiver_map.GetPropertyKey(broker(), kLengthIndex)
             .equals(length_string) ||
        !length_value->IsAccessorInfo() ||
        !receiver_map.GetPropertyKey(broker(), kNameIndex)
             .equals(name_string) ||
        !name_value->IsAccessorInfo()) {
      return inference.NoChange();
    }
  }

  // Choose the map for the resulting JSBoundFunction (but bail out in case of a
  // custom prototype).
  MapRef map =
      is_constructor
          ? native_context().bound_function_with_constructor_map(broker())
          : native_context().bound_function_without_constructor_map(broker());
  if (!map.prototype(broker()).equals(prototype)) return inference.NoChange();

  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  // Replace the {node} with a JSCreateBoundFunction.
  static constexpr int kBoundThis = 1;
  static constexpr int kReceiverContextEffectAndControl = 4;
  int const arity = n.ArgumentCount();

  if (arity > 0) {
    MapRef fixed_array_map = broker()->fixed_array_map();
    AllocationBuilder ab(jsgraph(), broker(), effect, control);
    if (!ab.CanAllocateArray(arity, fixed_array_map)) {
      return NoChange();
    }
  }

  int const arity_with_bound_this = std::max(arity, kBoundThis);
  int const input_count =
      arity_with_bound_this + kReceiverContextEffectAndControl;
  Node** inputs = graph()->zone()->AllocateArray<Node*>(input_count);
  int cursor = 0;
  inputs[cursor++] = receiver;
  inputs[cursor++] = n.ArgumentOrUndefined(0, jsgraph());  // bound_this.
  for (int i = 1; i < arity; ++i) {
    inputs[cursor++] = n.Argument(i);
  }
  inputs[cursor++] = context;
  inputs[cursor++] = effect;
  inputs[cursor++] = control;
  DCHECK_EQ(cursor, input_count);
  Node* value = effect =
      graph()->NewNode(javascript()->CreateBoundFunction(
                           arity_with_bound_this - kBoundThis, map),
                       input_count, inputs);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES6 section 19.2.3.3 Function.prototype.call (thisArg, ...args)
Reduction JSCallReducer::ReduceFunctionPrototypeCall(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  Node* target = n.target();
  Effect effect = n.effect();
  Control control = n.control();

  // Change context of {node} to the Function.prototype.call context,
  // to ensure any exception is thrown in the correct context.
  Node* context;
  HeapObjectMatcher m(target);
  if (m.HasResolvedValue() && m.Ref(broker()).IsJSFunction()) {
    JSFunctionRef function = m.Ref(broker()).AsJSFunction();
    context = jsgraph()->ConstantNoHole(function.context(broker()), broker());
  } else {
    context = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSFunctionContext()), target,
        effect, control);
  }
  NodeProperties::ReplaceContextInput(node, context);
  NodeProperties::ReplaceEffectInput(node, effect);

  // Remove the target from {node} and use the receiver as target instead, and
  // the thisArg becomes the new target.  If thisArg was not provided, insert
  // undefined instead.
  int arity = p.arity_without_implicit_args();
  ConvertReceiverMode convert_mode;
  if (arity == 0) {
    // The thisArg was not provided, use undefined as receiver.
    convert_mode = ConvertReceiverMode::kNullOrUndefined;
    node->ReplaceInput(n.TargetIndex(), n.receiver());
    node->ReplaceInput(n.ReceiverIndex(), jsgraph()->UndefinedConstant());
  } else {
    // Just remove the target, which is the first value input.
    convert_mode = ConvertReceiverMode::kAny;
    node->RemoveInput(n.TargetIndex());
    --arity;
  }
  NodeProperties::ChangeOp(
      node, javascript()->Call(JSCallNode::ArityForArgc(arity), p.frequency(),
                               p.feedback(), convert_mode, p.speculation_mode(),
                               CallFeedbackRelation::kUnrelated));
  // Try to further reduce the JSCall {node}.
  return Changed(node).FollowedBy(ReduceJSCall(node));
}

// ES6 section 19.2.3.6 Function.prototype [ @@hasInstance ] (V)
Reduction JSCallReducer::ReduceFunctionPrototypeHasInstance(Node* node) {
  JSCallNode n(node);
  Node* receiver = n.receiver();
  Node* object = n.ArgumentOrUndefined(0, jsgraph());
  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Effect effect = n.effect();
  Control control = n.control();

  // TODO(turbofan): If JSOrdinaryToInstance raises an exception, the
  // stack trace doesn't contain the @@hasInstance call; we have the
  // corresponding bug in the baseline case. Some massaging of the frame
  // state would be necessary here.

  // Morph this {node} into a JSOrdinaryHasInstance node.
  node->ReplaceInput(0, receiver);
  node->ReplaceInput(1, object);
  node->ReplaceInput(2, context);
  node->ReplaceInput(3, frame_state);
  node->ReplaceInput(4, effect);
  node->ReplaceInput(5, control);
  node->TrimInputCount(6);
  NodeProperties::ChangeOp(node, javascript()->OrdinaryHasInstance());
  return Changed(node);
}

Reduction JSCallReducer::ReduceObjectGetPrototype(Node* node, Node* object) {
  Effect effect{NodeProperties::GetEffectInput(node)};

  // Try to determine the {object} map.
  MapInference inference(broker(), object, effect);
  if (!inference.HaveMaps()) return NoChange();
  ZoneRefSet<Map> const& object_maps = inference.GetMaps();

  MapRef candidate_map = object_maps[0];
  HeapObjectRef candidate_prototype = candidate_map.prototype(broker());

  // Check if we can constant-fold the {candidate_prototype}.
  for (size_t i = 0; i < object_maps.size(); ++i) {
    MapRef object_map = object_maps[i];
    HeapObjectRef map_prototype = object_map.prototype(broker());
    if (IsSpecialReceiverInstanceType(object_map.instance_type()) ||
        !map_prototype.equals(candidate_prototype)) {
      // We exclude special receivers, like JSProxy or API objects that
      // might require access checks here; we also don't want to deal
      // with hidden prototypes at this point.
      return inference.NoChange();
    }
    // The above check also excludes maps for primitive values, which is
    // important because we are not applying [[ToObject]] here as expected.
    DCHECK(!object_map.IsPrimitiveMap() && object_map.IsJSReceiverMap());
  }
  if (!inference.RelyOnMapsViaStability(dependencies())) {
    return inference.NoChange();
  }
  Node* value = jsgraph()->ConstantNoHole(candidate_prototype, broker());
  ReplaceWithValue(node, value);
  return Replace(value);
}

// ES6 section 19.1.2.11 Object.getPrototypeOf ( O )
Reduction JSCallReducer::ReduceObjectGetPrototypeOf(Node* node) {
  JSCallNode n(node);
  Node* object = n.ArgumentOrUndefined(0, jsgraph());
  return ReduceObjectGetPrototype(node, object);
}

// ES section #sec-object.is
Reduction JSCallReducer::ReduceObjectIs(Node* node) {
  JSCallNode n(node);
  Node* lhs = n.ArgumentOrUndefined(0, jsgraph());
  Node* rhs = n.ArgumentOrUndefined(1, jsgraph());
  Node* value = graph()->NewNode(simplified()->SameValue(), lhs, rhs);
  ReplaceWithValue(node, value);
  return Replace(value);
}

// ES6 section B.2.2.1.1 get Object.prototype.__proto__
Reduction JSCallReducer::ReduceObjectPrototypeGetProto(Node* node) {
  JSCallNode n(node);
  return ReduceObjectGetPrototype(node, n.receiver());
}

// ES #sec-object.prototype.hasownproperty
Reduction JSCallReducer::ReduceObjectPrototypeHasOwnProperty(Node* node) {
  JSCallNode call_node(node);
  Node* receiver = call_node.receiver();
  Node* name = call_node.ArgumentOrUndefined(0, jsgraph());
  Effect effect = call_node.effect();
  Control control = call_node.control();

  // We can optimize a call to Object.prototype.hasOwnProperty if it's being
  // used inside a fast-mode for..in, so for code like this:
  //
  //   for (name in receiver) {
  //     if (receiver.hasOwnProperty(name)) {
  //        ...
  //     }
  //   }
  //
  // If the for..in is in fast-mode, we know that the {receiver} has {name}
  // as own property, otherwise the enumeration wouldn't include it. The graph
  // constructed by the BytecodeGraphBuilder in this case looks like this:

  // receiver
  //  ^    ^
  //  |    |
  //  |    +-+
  //  |      |
  //  |   JSToObject
  //  |      ^
  //  |      |
  //  |   JSForInNext
  //  |      ^
  //  +----+ |
  //       | |
  //  JSCall[hasOwnProperty]

  // We can constant-fold the {node} to True in this case, and insert
  // a (potentially redundant) map check to guard the fact that the
  // {receiver} map didn't change since the dominating JSForInNext. This
  // map check is only necessary when TurboFan cannot prove that there
  // is no observable side effect between the {JSForInNext} and the
  // {JSCall} to Object.prototype.hasOwnProperty.
  //
  // Also note that it's safe to look through the {JSToObject}, since the
  // Object.prototype.hasOwnProperty does an implicit ToObject anyway, and
  // these operations are not observable.
  if (name->opcode() == IrOpcode::kJSForInNext) {
    JSForInNextNode n(name);
    if (n.Parameters().mode() != ForInMode::kGeneric) {
      Node* object = n.receiver();
      Node* cache_type = n.cache_type();
      if (object->opcode() == IrOpcode::kJSToObject) {
        object = NodeProperties::GetValueInput(object, 0);
      }
      if (object == receiver) {
        // No need to repeat the map check if we can prove that there's no
        // observable side effect between {effect} and {name].
        if (!NodeProperties::NoObservableSideEffectBetween(effect, name)) {
          Node* receiver_map = effect =
              graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                               receiver, effect, control);
          Node* check = graph()->NewNode(simplified()->ReferenceEqual(),
                                         receiver_map, cache_type);
          effect = graph()->NewNode(
              simplified()->CheckIf(DeoptimizeReason::kWrongMap), check, effect,
              control);
        }
        Node* value = jsgraph()->TrueConstant();
        ReplaceWithValue(node, value, effect, control);
        return Replace(value);
      }
    }
  }

  return NoChange();
}

// ES #sec-object.prototype.isprototypeof
Reduction JSCallReducer::ReduceObjectPrototypeIsPrototypeOf(Node* node) {
  JSCallNode n(node);
  Node* receiver = n.receiver();
  Node* value = n.ArgumentOrUndefined(0, jsgraph());
  Effect effect = n.effect();

  // Ensure that the {receiver} is known to be a JSReceiver (so that
  // the ToObject step of Object.prototype.isPrototypeOf is a no-op).
  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() || !inference.AllOfInstanceTypesAreJSReceiver()) {
    return NoChange();
  }

  // We don't check whether {value} is a proper JSReceiver here explicitly,
  // and don't explicitly rule out Primitive {value}s, since all of them
  // have null as their prototype, so the prototype chain walk inside the
  // JSHasInPrototypeChain operator immediately aborts and yields false.
  NodeProperties::ReplaceValueInput(node, value, n.TargetIndex());
  for (int i = node->op()->ValueInputCount(); i > 2; i--) {
    node->RemoveInput(2);
  }
  NodeProperties::ChangeOp(node, javascript()->HasInPrototypeChain());
  return Changed(node);
}

// ES6 section 26.1.1 Reflect.apply ( target, thisArgument, argumentsList )
Reduction JSCallReducer::ReduceReflectApply(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  int arity = p.arity_without_implicit_args();
  // Massage value inputs appropriately.
  static_assert(n.ReceiverIndex() > n.TargetIndex());
  node->RemoveInput(n.ReceiverIndex());
  node->RemoveInput(n.TargetIndex());
  while (arity < 3) {
    node->InsertInput(graph()->zone(), arity++, jsgraph()->UndefinedConstant());
  }
  while (arity-- > 3) {
    node->RemoveInput(arity);
  }
  NodeProperties::ChangeOp(
      node, javascript()->CallWithArrayLike(p.frequency(), p.feedback(),
                                            p.speculation_mode(),
                                            CallFeedbackRelation::kUnrelated));
  return Changed(node).FollowedBy(ReduceJSCallWithArrayLike(node));
}

// ES6 section 26.1.2 Reflect.construct ( target, argumentsList [, newTarget] )
Reduction JSCallReducer::ReduceReflectConstruct(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  int arity = p.arity_without_implicit_args();
  // Massage value inputs appropriately.
  TNode<Object> arg_target = n.ArgumentOrUndefined(0, jsgraph());
  TNode<Object> arg_argument_list = n.ArgumentOrUndefined(1, jsgraph());
  TNode<Object> arg_new_target = n.ArgumentOr(2, arg_target);

  static_assert(n.ReceiverIndex() > n.TargetIndex());
  node->RemoveInput(n.ReceiverIndex());
  node->RemoveInput(n.TargetIndex());

  // TODO(jgruber): This pattern essentially ensures that we have the correct
  // number of inputs for a given argument count. Wrap it in a helper function.
  static_assert(JSConstructNode::FirstArgumentIndex() == 2);
  while (arity < 3) {
    node->InsertInput(graph()->zone(), arity++, jsgraph()->UndefinedConstant());
  }
  while (arity-- > 3) {
    node->RemoveInput(arity);
  }

  static_assert(JSConstructNode::TargetIndex() == 0);
  static_assert(JSConstructNode::NewTargetIndex() == 1);
  static_assert(JSConstructNode::kFeedbackVectorIsLastInput);
  node->ReplaceInput(JSConstructNode::TargetIndex(), arg_target);
  node->ReplaceInput(JSConstructNode::NewTargetIndex(), arg_new_target);
  node->ReplaceInput(JSConstructNode::ArgumentIndex(0), arg_argument_list);

  NodeProperties::ChangeOp(
      node, javascript()->ConstructWithArrayLike(p.frequency(), p.feedback()));
  return Changed(node).FollowedBy(ReduceJSConstructWithArrayLike(node));
}

// ES6 section 26.1.7 Reflect.getPrototypeOf ( target )
Reduction JSCallReducer::ReduceReflectGetPrototypeOf(Node* node) {
  JSCallNode n(node);
  Node* target = n.ArgumentOrUndefined(0, jsgraph());
  return ReduceObjectGetPrototype(node, target);
}

// ES6 section #sec-object.create Object.create(proto, properties)
Reduction JSCallReducer::ReduceObjectCreate(Node* node) {
  JSCallNode n(node);
  Node* properties = n.ArgumentOrUndefined(1, jsgraph());
  if (properties != jsgraph()->UndefinedConstant()) return NoChange();

  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Effect effect = n.effect();
  Control control = n.control();
  Node* prototype = n.ArgumentOrUndefined(0, jsgraph());
  node->ReplaceInput(0, prototype);
  node->ReplaceInput(1, context);
  node->ReplaceInput(2, frame_state);
  node->ReplaceInput(3, effect);
  node->ReplaceInput(4, control);
  node->TrimInputCount(5);
  NodeProperties::ChangeOp(node, javascript()->CreateObject());
  return Changed(node);
}

// ES section #sec-reflect.get
Reduction JSCallReducer::ReduceReflectGet(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  int arity = p.arity_without_implicit_args();
  if (arity != 2) return NoChange();
  Node* target = n.Argument(0);
  Node* key = n.Argument(1);
  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Effect effect = n.effect();
  Control control = n.control();

  // Check whether {target} is a JSReceiver.
  Node* check = graph()->NewNode(simplified()->ObjectIsReceiver(), target);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

  // Throw an appropriate TypeError if the {target} is not a JSReceiver.
  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = effect;
  {
    if_false = efalse = graph()->NewNode(
        javascript()->CallRuntime(Runtime::kThrowTypeError, 2),
        jsgraph()->ConstantNoHole(
            static_cast<int>(MessageTemplate::kCalledOnNonObject)),
        jsgraph()->HeapConstantNoHole(factory()->ReflectGet_string()), context,
        frame_state, efalse, if_false);
  }

  // Otherwise just use the existing GetPropertyStub.
  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = effect;
  Node* vtrue;
  {
    Callable callable = Builtins::CallableFor(isolate(), Builtin::kGetProperty);
    auto call_descriptor = Linkage::GetStubCallDescriptor(
        graph()->zone(), callable.descriptor(),
        callable.descriptor().GetStackParameterCount(),
        CallDescriptor::kNeedsFrameState, Operator::kNoProperties);
    Node* stub_code = jsgraph()->HeapConstantNoHole(callable.code());
    vtrue = etrue = if_true =
        graph()->NewNode(common()->Call(call_descriptor), stub_code, target,
                         key, context, frame_state, etrue, if_true);
  }

  // Rewire potential exception edges.
  Node* on_exception = nullptr;
  if (NodeProperties::IsExceptionalCall(node, &on_exception)) {
    // Create appropriate {IfException} and {IfSuccess} nodes.
    Node* extrue = graph()->NewNode(common()->IfException(), etrue, if_true);
    if_true = graph()->NewNode(common()->IfSuccess(), if_true);
    Node* exfalse = graph()->NewNode(common()->IfException(), efalse, if_false);
    if_false = graph()->NewNode(common()->IfSuccess(), if_false);

    // Join the exception edges.
    Node* merge = graph()->NewNode(common()->Merge(2), extrue, exfalse);
    Node* ephi =
        graph()->NewNode(common()->EffectPhi(2), extrue, exfalse, merge);
    Node* phi =
        graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                         extrue, exfalse, merge);
    ReplaceWithValue(on_exception, phi, ephi, merge);
  }

  // Connect the throwing path to end.
  if_false = graph()->NewNode(common()->Throw(), efalse, if_false);
  MergeControlToEnd(graph(), common(), if_false);

  // Continue on the regular path.
  ReplaceWithValue(node, vtrue, etrue, if_true);
  return Changed(vtrue);
}

// ES section #sec-reflect.has
Reduction JSCallReducer::ReduceReflectHas(Node* node) {
  JSCallNode n(node);
  Node* target = n.ArgumentOrUndefined(0, jsgraph());
  Node* key = n.ArgumentOrUndefined(1, jsgraph());
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();
  FrameState frame_state = n.frame_state();

  // Check whether {target} is a JSReceiver.
  Node* check = graph()->NewNode(simplified()->ObjectIsReceiver(), target);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

  // Throw an appropriate TypeError if the {target} is not a JSReceiver.
  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = effect;
  {
    if_false = efalse = graph()->NewNode(
        javascript()->CallRuntime(Runtime::kThrowTypeError, 2),
        jsgraph()->ConstantNoHole(
            static_cast<int>(MessageTemplate::kCalledOnNonObject)),
        jsgraph()->HeapConstantNoHole(factory()->ReflectHas_string()), context,
        frame_state, efalse, if_false);
  }

  // Otherwise just use the existing {JSHasProperty} logic.
  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = effect;
  Node* vtrue;
  {
    // TODO(magardn): collect feedback so this can be optimized
    vtrue = etrue = if_true = graph()->NewNode(
        javascript()->HasProperty(FeedbackSource()), target, key,
        jsgraph()->UndefinedConstant(), context, frame_state, etrue, if_true);
  }

  // Rewire potential exception edges.
  Node* on_exception = nullptr;
  if (NodeProperties::IsExceptionalCall(node, &on_exception)) {
    // Create appropriate {IfException} and {IfSuccess} nodes.
    Node* extrue = graph()->NewNode(common()->IfException(), etrue, if_true);
    if_true = graph()->NewNode(common()->IfSuccess(), if_true);
    Node* exfalse = graph()->NewNode(common()->IfException(), efalse, if_false);
    if_false = graph()->NewNode(common()->IfSuccess(), if_false);

    // Join the exception edges.
    Node* merge = graph()->NewNode(common()->Merge(2), extrue, exfalse);
    Node* ephi =
        graph()->NewNode(common()->EffectPhi(2), extrue, exfalse, merge);
    Node* phi =
        graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                         extrue, exfalse, merge);
    ReplaceWithValue(on_exception, phi, ephi, merge);
  }

  // Connect the throwing path to end.
  if_false = graph()->NewNode(common()->Throw(), efalse, if_false);
  MergeControlToEnd(graph(), common(), if_false);

  // Continue on the regular path.
  ReplaceWithValue(node, vtrue, etrue, if_true);
  return Changed(vtrue);
}

namespace {

bool CanInlineArrayIteratingBuiltin(JSHeapBroker* broker,
                                    ZoneRefSet<Map> const& receiver_maps,
                                    ElementsKind* kind_return) {
  DCHECK_NE(0, receiver_maps.size());
  *kind_return = receiver_maps[0].elements_kind();
  for (MapRef map : receiver_maps) {
    if (!map.supports_fast_array_iteration(broker) ||
        !UnionElementsKindUptoSize(kind_return, map.elements_kind())) {
      return false;
    }
  }
  return true;
}

bool CanInlineArrayResizingBuiltin(JSHeapBroker* broker,
                                   ZoneRefSet<Map> const& receiver_maps,
                                   std::vector<ElementsKind>* kinds,
                                   bool builtin_is_push = false) {
  DCHECK_NE(0, receiver_maps.size());
  for (MapRef map : receiver_maps) {
    if (!map.supports_fast_array_resize(broker)) return false;
    // TODO(turbofan): We should also handle fast holey double elements once
    // we got the hole NaN mess sorted out in TurboFan/V8.
    if (map.elements_kind() == HOLEY_DOUBLE_ELEMENTS && !builtin_is_push) {
      return false;
    }
    ElementsKind current_kind = map.elements_kind();
    auto kind_ptr = kinds->data();
    size_t i;
    for (i = 0; i < kinds->size(); i++, kind_ptr++) {
      if (UnionElementsKindUptoPackedness(kind_ptr, current_kind)) {
        break;
      }
    }
    if (i == kinds->size()) kinds->push_back(current_kind);
  }
  return true;
}

// Wraps common setup code for iterating array builtins.
class IteratingArrayBuiltinHelper {
 public:
  IteratingArrayBuiltinHelper(Node* node, JSHeapBroker* broker,
                              JSGraph* jsgraph,
                              CompilationDependencies* dependencies)
      : receiver_(NodeProperties::GetValueInput(node, 1)),
        effect_(NodeProperties::GetEffectInput(node)),
        control_(NodeProperties::GetControlInput(node)),
        inference_(broker, receiver_, effect_) {
    if (!v8_flags.turbo_inline_array_builtins) return;

    DCHECK_EQ(IrOpcode::kJSCall, node->opcode());
    const CallParameters& p = CallParametersOf(node->op());
    if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
      return;
    }

    // Try to determine the {receiver} map.
    if (!inference_.HaveMaps()) return;
    ZoneRefSet<Map> const& receiver_maps = inference_.GetMaps();

    if (!CanInlineArrayIteratingBuiltin(broker, receiver_maps,
                                        &elements_kind_)) {
      return;
    }

    // TODO(jgruber): May only be needed for holey elements kinds.
    if (!dependencies->DependOnNoElementsProtector()) return;

    has_stability_dependency_ = inference_.RelyOnMapsPreferStability(
        dependencies, jsgraph, &effect_, control_, p.feedback());

    can_reduce_ = true;
  }

  bool can_reduce() const { return can_reduce_; }
  bool has_stability_dependency() const { return has_stability_dependency_; }
  Effect effect() const { return effect_; }
  Control control() const { return control_; }
  MapInference* inference() { return &inference_; }
  ElementsKind elements_kind() const { return elements_kind_; }

 private:
  bool can_reduce_ = false;
  bool has_stability_dependency_ = false;
  Node* receiver_;
  Effect effect_;
  Control control_;
  MapInference inference_;
  ElementsKind elements_kind_;
};

}  // namespace

Reduction JSCallReducer::ReduceArrayForEach(Node* node,
                                            SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());
  TNode<Object> subgraph = a.ReduceArrayPrototypeForEach(
      h.inference(), h.has_stability_dependency(), h.elements_kind(), shared);
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceArrayReduce(Node* node,
                                           SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());
  TNode<Object> subgraph = a.ReduceArrayPrototypeReduce(
      h.inference(), h.has_stability_dependency(), h.elements_kind(),
      ArrayReduceDirection::kLeft, shared);
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceArrayReduceRight(Node* node,
                                                SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());
  TNode<Object> subgraph = a.ReduceArrayPrototypeReduce(
      h.inference(), h.has_stability_dependency(), h.elements_kind(),
      ArrayReduceDirection::kRight, shared);
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceArrayMap(Node* node,
                                        SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  // Calls CreateArray and thus requires this additional protector dependency.
  if (!dependencies()->DependOnArraySpeciesProtector()) {
    return h.inference()->NoChange();
  }

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());

  TNode<Object> subgraph =
      a.ReduceArrayPrototypeMap(h.inference(), h.has_stability_dependency(),
                                h.elements_kind(), shared, native_context());
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceArrayFilter(Node* node,
                                           SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  // Calls CreateArray and thus requires this additional protector dependency.
  if (!dependencies()->DependOnArraySpeciesProtector()) {
    return h.inference()->NoChange();
  }

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());

  TNode<Object> subgraph =
      a.ReduceArrayPrototypeFilter(h.inference(), h.has_stability_dependency(),
                                   h.elements_kind(), shared, native_context());
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceArrayFind(Node* node,
                                         SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());

  TNode<Object> subgraph = a.ReduceArrayPrototypeFind(
      h.inference(), h.has_stability_dependency(), h.elements_kind(), shared,
      native_context(), ArrayFindVariant::kFind);
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceArrayFindIndex(Node* node,
                                              SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());

  TNode<Object> subgraph = a.ReduceArrayPrototypeFind(
      h.inference(), h.has_stability_dependency(), h.elements_kind(), shared,
      native_context(), ArrayFindVariant::kFindIndex);
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceArrayEvery(Node* node,
                                          SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());

  TNode<Object> subgraph = a.ReduceArrayPrototypeEverySome(
      h.inference(), h.has_stability_dependency(), h.elements_kind(), shared,
      native_context(), ArrayEverySomeVariant::kEvery);
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES7 Array.prototype.inludes(searchElement[, fromIndex])
// #sec-array.prototype.includes
Reduction JSCallReducer::ReduceArrayIncludes(Node* node) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());

  TNode<Object> subgraph = a.ReduceArrayPrototypeIndexOfIncludes(
      h.elements_kind(), ArrayIndexOfIncludesVariant::kIncludes);
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES6 Array.prototype.indexOf(searchElement[, fromIndex])
// #sec-array.prototype.indexof
Reduction JSCallReducer::ReduceArrayIndexOf(Node* node) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());

  TNode<Object> subgraph = a.ReduceArrayPrototypeIndexOfIncludes(
      h.elements_kind(), ArrayIndexOfIncludesVariant::kIndexOf);
  return ReplaceWithSubgraph(&a, subgraph);
}

Reduction JSCallReducer::ReduceArraySome(Node* node,
                                         SharedFunctionInfoRef shared) {
  IteratingArrayBuiltinHelper h(node, broker(), jsgraph(), dependencies());
  if (!h.can_reduce()) return h.inference()->NoChange();

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(h.effect(), h.control());

  TNode<Object> subgraph = a.ReduceArrayPrototypeEverySome(
      h.inference(), h.has_stability_dependency(), h.elements_kind(), shared,
      native_context(), ArrayEverySomeVariant::kSome);
  return ReplaceWithSubgraph(&a, subgraph);
}

#if V8_ENABLE_WEBASSEMBLY

namespace {

bool CanInlineJSToWasmCall(const wasm::FunctionSig* wasm_signature) {
  if (wasm_signature->return_count() > 1) {
    return false;
  }

  wasm::ValueType externRefNonNull = wasm::kWasmExternRef.AsNonNull();
  for (auto type : wasm_signature->all()) {
#if defined(V8_TARGET_ARCH_32_BIT)
    if (type == wasm::kWasmI64) return false;
#endif
    if (type != wasm::kWasmI32 && type != wasm::kWasmI64 &&
        type != wasm::kWasmF32 && type != wasm::kWasmF64 &&
        type != wasm::kWasmExternRef && type != externRefNonNull) {
      return false;
    }
  }

  return true;
}

}  // namespace

Reduction JSCallReducer::ReduceCallWasmFunction(Node* node,
                                                SharedFunctionInfoRef shared) {
  DCHECK(flags() & kInlineJSToWasmCalls);

  JSCallNode n(node);
  const CallParameters& p = n.Parameters();

  // Avoid deoptimization loops
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  const wasm::FunctionSig* wasm_signature = shared.wasm_function_signature();
  if (!CanInlineJSToWasmCall(wasm_signature)) {
    return NoChange();
  }

  // Signal TurboFan that it should run the 'wasm-inlining' phase.
  has_wasm_calls_ = true;

  const wasm::WasmModule* wasm_module = shared.wasm_module();
  if (wasm_module_for_inlining_ == nullptr) {
    wasm_module_for_inlining_ = wasm_module;
  }

  wasm::NativeModule* native_module = nullptr;
  if (shared.object()->HasWasmExportedFunctionData()) {
    native_module = shared.object()
                        ->wasm_exported_function_data()
                        ->instance()
                        ->module_object()
                        ->native_module();
  }
  // TODO(mliedtke): We should be able to remove module, signature, native
  // module and function index from the SharedFunctionInfoRef. However, for some
  // reason I may dereference the SharedFunctionInfoRef here but not in
  // JSInliningHeuristic later on.
  const Operator* op = javascript()->CallWasm(
      wasm_module, wasm_signature, shared.wasm_function_index(), shared,
      native_module, p.feedback());

  // Remove additional inputs
  size_t actual_arity = n.ArgumentCount();
  DCHECK(JSCallNode::kFeedbackVectorIsLastInput);
  DCHECK_EQ(actual_arity + JSWasmCallNode::kExtraInputCount - 1,
            n.FeedbackVectorIndex());
  size_t expected_arity = wasm_signature->parameter_count();

  while (actual_arity > expected_arity) {
    int removal_index =
        static_cast<int>(n.FirstArgumentIndex() + expected_arity);
    DCHECK_LT(removal_index, static_cast<int>(node->InputCount()));
    node->RemoveInput(removal_index);
    actual_arity--;
  }

  // Add missing inputs
  while (actual_arity < expected_arity) {
    int insertion_index = n.ArgumentIndex(n.ArgumentCount());
    node->InsertInput(graph()->zone(), insertion_index,
                      jsgraph()->UndefinedConstant());
    actual_arity++;
  }

  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Given a FunctionTemplateInfo, checks whether the fast API call can be
// optimized, applying the initial step of the overload resolution algorithm:
// Given an overload set function_template_info.c_signatures, and a list of
// arguments of size argc:
// 1. Let max_arg be the length of the longest type list of the entries in
//    function_template_info.c_signatures.
// 2. Let argc be the size of the arguments list.
// 3. Initialize arg_count = min(max_arg, argc).
// 4. Remove from the set all entries whose type list is not of length
//    arg_count.
// Returns an array with the indexes of the remaining entries in S, which
// represents the set of "optimizable" function overloads.

FastApiCallFunctionVector CanOptimizeFastCall(
    JSHeapBroker* broker, Zone* zone,
    FunctionTemplateInfoRef function_template_info, size_t argc) {
  FastApiCallFunctionVector result(zone);
  if (!v8_flags.turbo_fast_api_calls) return result;

  static constexpr int kReceiver = 1;

  ZoneVector<Address> functions = function_template_info.c_functions(broker);
  ZoneVector<const CFunctionInfo*> signatures =
      function_template_info.c_signatures(broker);
  const size_t overloads_count = signatures.size();

  // Calculates the length of the longest type list of the entries in
  // function_template_info.
  size_t max_arg = 0;
  for (size_t i = 0; i < overloads_count; i++) {
    const CFunctionInfo* c_signature = signatures[i];
    // C arguments should include the receiver at index 0.
    DCHECK_GE(c_signature->ArgumentCount(), kReceiver);
    const size_t len = c_signature->ArgumentCount() - kReceiver;
    if (len > max_arg) max_arg = len;
  }
  const size_t arg_count = std::min(max_arg, argc);

  // Only considers entries whose type list length matches arg_count.
  for (size_t i = 0; i < overloads_count; i++) {
    const CFunctionInfo* c_signature = signatures[i];
    const size_t len = c_signature->ArgumentCount() - kReceiver;
    bool optimize_to_fast_call = (len == arg_count);

    optimize_to_fast_call =
        optimize_to_fast_call &&
        fast_api_call::CanOptimizeFastSignature(c_signature);

    if (optimize_to_fast_call) {
      // TODO(nicohartmann@): {Flags::kEnforceRangeBit} is currently only
      // supported on 64 bit architectures. We should support this on 32 bit
      // architectures.
#if defined(V8_TARGET_ARCH_32_BIT)
      for (unsigned int i = 0; i < c_signature->ArgumentCount(); ++i) {
        const uint8_t flags =
            static_cast<uint8_t>(c_signature->ArgumentInfo(i).GetFlags());
        if (flags & static_cast<uint8_t>(CTypeInfo::Flags::kEnforceRangeBit)) {
          // Bailout
          return FastApiCallFunctionVector(zone);
        }
      }
#endif
      result.push_back({functions[i], c_signature});
    }
  }

  return result;
}

Reduction JSCallReducer::ReduceCallApiFunction(Node* node,
                                               SharedFunctionInfoRef shared) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  int const argc = p.arity_without_implicit_args();
  Node* target = n.target();
  Node* global_proxy = jsgraph()->ConstantNoHole(
      native_context().global_proxy_object(broker()), broker());
  Node* receiver = (p.convert_mode() == ConvertReceiverMode::kNullOrUndefined)
                       ? global_proxy
                       : n.receiver();
  Node* holder;
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();
  FrameState frame_state = n.frame_state();

  if (!shared.function_template_info(broker()).has_value()) {
    TRACE_BROKER_MISSING(
        broker(), "FunctionTemplateInfo for function with SFI " << shared);
    return NoChange();
  }

  // See if we can optimize this API call to {shared}.
  FunctionTemplateInfoRef function_template_info(
      shared.function_template_info(broker()).value());

  if (function_template_info.accept_any_receiver() &&
      function_template_info.is_signature_undefined(broker())) {
    // We might be able to
    // optimize the API call depending on the {function_template_info}.
    // If the API function accepts any kind of {receiver}, we only need to
    // ensure that the {receiver} is actually a JSReceiver at this point,
    // and also pass that as the {holder}. There are two independent bits
    // here:
    //
    //  a. When the "accept any receiver" bit is set, it means we don't
    //     need to perform access checks, even if the {receiver}'s map
    //     has the "needs access check" bit set.
    //  b. When the {function_template_info} has no signature, we don't
    //     need to do the compatible receiver check, since all receivers
    //     are considered compatible at that point, and the {receiver}
    //     will be pass as the {holder}.
    //
    receiver = holder = effect = graph()->NewNode(
        simplified()->ConvertReceiver(p.convert_mode()), receiver,
        jsgraph()->ConstantNoHole(native_context(), broker()), global_proxy,
        effect, control);
  } else {
    // Try to infer the {receiver} maps from the graph.
    MapInference inference(broker(), receiver, effect);
    if (inference.HaveMaps()) {
      ZoneRefSet<Map> const& receiver_maps = inference.GetMaps();
      MapRef first_receiver_map = receiver_maps[0];

      // See if we can constant-fold the compatible receiver checks.
      HolderLookupResult api_holder =
          function_template_info.LookupHolderOfExpectedType(broker(),
                                                            first_receiver_map);
      if (api_holder.lookup == CallOptimization::kHolderNotFound) {
        return inference.NoChange();
      }

      // Check that all {receiver_maps} are actually JSReceiver maps and
      // that the {function_template_info} accepts them without access
      // checks (even if "access check needed" is set for {receiver}).
      //
      // Note that we don't need to know the concrete {receiver} maps here,
      // meaning it's fine if the {receiver_maps} are unreliable, and we also
      // don't need to install any stability dependencies, since the only
      // relevant information regarding the {receiver} is the Map::constructor
      // field on the root map (which is different from the JavaScript exposed
      // "constructor" property) and that field cannot change.
      //
      // So if we know that {receiver} had a certain constructor at some point
      // in the past (i.e. it had a certain map), then this constructor is going
      // to be the same later, since this information cannot change with map
      // transitions.
      //
      // The same is true for the instance type, e.g. we still know that the
      // instance type is JSObject even if that information is unreliable, and
      // the "access check needed" bit, which also cannot change later.
      CHECK(first_receiver_map.IsJSReceiverMap());
      CHECK(!first_receiver_map.is_access_check_needed() ||
            function_template_info.accept_any_receiver());

      for (size_t i = 1; i < receiver_maps.size(); ++i) {
        MapRef receiver_map = receiver_maps[i];
        HolderLookupResult holder_i =
            function_template_info.LookupHolderOfExpectedType(broker(),
                                                              receiver_map);

        if (api_holder.lookup != holder_i.lookup) return inference.NoChange();
        DCHECK(holder_i.lookup == CallOptimization::kHolderFound ||
               holder_i.lookup == CallOptimization::kHolderIsReceiver);
        if (holder_i.lookup == CallOptimization::kHolderFound) {
          DCHECK(api_holder.holder.has_value() && holder_i.holder.has_value());
          if (!api_holder.holder->equals(*holder_i.holder)) {
            return inference.NoChange();
          }
        }

        CHECK(receiver_map.IsJSReceiverMap());
        CHECK(!receiver_map.is_access_check_needed() ||
              function_template_info.accept_any_receiver());
      }

      if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation &&
          !inference.RelyOnMapsViaStability(dependencies())) {
        // We were not able to make the receiver maps reliable without map
        // checks but doing map checks would lead to deopt loops, so give up.
        return inference.NoChange();
      }

      // TODO(neis): The maps were used in a way that does not actually require
      // map checks or stability dependencies.
      inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                          control, p.feedback());

      // Determine the appropriate holder for the {lookup}.
      holder = api_holder.lookup == CallOptimization::kHolderFound
                   ? jsgraph()->ConstantNoHole(*api_holder.holder, broker())
                   : receiver;
    } else {
      // We don't have enough information to eliminate the access check
      // and/or the compatible receiver check, so use the generic builtin
      // that does those checks dynamically. This is still significantly
      // faster than the generic call sequence.
      Builtin builtin_name;
      if (function_template_info.accept_any_receiver()) {
        DCHECK(!function_template_info.is_signature_undefined(broker()));
        builtin_name = Builtin::kCallFunctionTemplate_CheckCompatibleReceiver;
      } else if (function_template_info.is_signature_undefined(broker())) {
        builtin_name = Builtin::kCallFunctionTemplate_CheckAccess;
      } else {
        builtin_name =
            Builtin::kCallFunctionTemplate_CheckAccessAndCompatibleReceiver;
      }

      // The CallFunctionTemplate builtin requires the {receiver} to be
      // an actual JSReceiver, so make sure we do the proper conversion
      // first if necessary.
      receiver = holder = effect = graph()->NewNode(
          simplified()->ConvertReceiver(p.convert_mode()), receiver,
          jsgraph()->ConstantNoHole(native_context(), broker()), global_proxy,
          effect, control);

      Callable callable = Builtins::CallableFor(isolate(), builtin_name);
      auto call_descriptor = Linkage::GetStubCallDescriptor(
          graph()->zone(), callable.descriptor(),
          argc + 1 /* implicit receiver */, CallDescriptor::kNeedsFrameState);
      node->RemoveInput(n.FeedbackVectorIndex());
      node->InsertInput(graph()->zone(), 0,
                        jsgraph()->HeapConstantNoHole(callable.code()));
      node->ReplaceInput(
          1, jsgraph()->ConstantNoHole(function_template_info, broker()));
      node->InsertInput(graph()->zone(), 2,
                        jsgraph()->Int32Constant(JSParameterCount(argc)));
      node->ReplaceInput(3, receiver);       // Update receiver input.
      node->ReplaceInput(6 + argc, effect);  // Update effect input.
      NodeProperties::ChangeOp(node, common()->Call(call_descriptor));
      return Changed(node);
    }
  }

  // TODO(turbofan): Consider introducing a JSCallApiCallback operator for
  // this and lower it during JSGenericLowering, and unify this with the
  // JSNativeContextSpecialization::InlineApiCall method a bit.
  compiler::OptionalObjectRef maybe_callback_data =
      function_template_info.callback_data(broker());
  if (!maybe_callback_data.has_value()) {
    TRACE_BROKER_MISSING(broker(), "call code for function template info "
                                       << function_template_info);
    return NoChange();
  }

  // Handles overloaded functions.

  FastApiCallFunctionVector c_candidate_functions = CanOptimizeFastCall(
      broker(), graph()->zone(), function_template_info, argc);
  DCHECK_LE(c_candidate_functions.size(), 2);

  // TODO(v8:13600): Support exception handling for FastApiCall nodes.
  if (!c_candidate_functions.empty() &&
      !NodeProperties::IsExceptionalCall(node)) {
    FastApiCallReducerAssembler a(this, node, function_template_info,
                                  c_candidate_functions, receiver, holder,
                                  shared, target, argc, effect);
    Node* fast_call_subgraph = a.ReduceFastApiCall();
    ReplaceWithSubgraph(&a, fast_call_subgraph);

    return Replace(fast_call_subgraph);
  }

  // Slow call

  bool no_profiling = broker()->dependencies()->DependOnNoProfilingProtector();
  Callable call_api_callback = Builtins::CallableFor(
      isolate(), no_profiling ? Builtin::kCallApiCallbackOptimizedNoProfiling
                              : Builtin::kCallApiCallbackOptimized);
  CallInterfaceDescriptor cid = call_api_callback.descriptor();
  auto call_descriptor =
      Linkage::GetStubCallDescriptor(graph()->zone(), cid, argc + 1 /*
     implicit receiver */, CallDescriptor::kNeedsFrameState);
  ApiFunction api_function(function_template_info.callback(broker()));
  ExternalReference function_reference = ExternalReference::Create(
      &api_function, ExternalReference::DIRECT_API_CALL);

  Node* continuation_frame_state = CreateInlinedApiFunctionFrameState(
      jsgraph(), shared, target, context, receiver, frame_state);

  node->RemoveInput(n.FeedbackVectorIndex());
  node->InsertInput(graph()->zone(), 0,
                    jsgraph()->HeapConstantNoHole(call_api_callback.code()));
  node->ReplaceInput(1, jsgraph()->ExternalConstant(function_reference));
  node->InsertInput(graph()->zone(), 2, jsgraph()->ConstantNoHole(argc));
  node->InsertInput(
      graph()->zone(), 3,
      jsgraph()->ConstantNoHole(maybe_callback_data.value(), broker()));
  node->InsertInput(graph()->zone(), 4, holder);
  node->ReplaceInput(5, receiver);  // Update receiver input.
  // 6 + argc is context input.
  node->ReplaceInput(6 + argc + 1, continuation_frame_state);
  node->ReplaceInput(6 + argc + 2, effect);
  NodeProperties::ChangeOp(node, common()->Call(call_descriptor));
  return Changed(node);
}

namespace {

// Check whether elements aren't mutated; we play it extremely safe here by
// explicitly checking that {node} is only used by {LoadField} or
// {LoadElement}.
bool IsSafeArgumentsElements(Node* node) {
  for (Edge const edge : node->use_edges()) {
    if (!NodeProperties::IsValueEdge(edge)) continue;
    if (edge.from()->opcode() != IrOpcode::kLoadField &&
        edge.from()->opcode() != IrOpcode::kLoadElement) {
      return false;
    }
  }
  return true;
}

#ifdef DEBUG
bool IsCallOrConstructWithArrayLike(Node* node) {
  return node->opcode() == IrOpcode::kJSCallWithArrayLike ||
         node->opcode() == IrOpcode::kJSConstructWithArrayLike;
}
#endif

bool IsCallOrConstructWithSpread(Node* node) {
  return node->opcode() == IrOpcode::kJSCallWithSpread ||
         node->opcode() == IrOpcode::kJSConstructWithSpread;
}

bool IsCallWithArrayLikeOrSpread(Node* node) {
  return node->opcode() == IrOpcode::kJSCallWithArrayLike ||
         node->opcode() == IrOpcode::kJSCallWithSpread;
}

}  // namespace

Node* JSCallReducer::ConvertHoleToUndefined(Node* value, ElementsKind kind) {
  DCHECK(IsHoleyElementsKind(kind));
  if (kind == HOLEY_DOUBLE_ELEMENTS) {
    return graph()->NewNode(simplified()->ChangeFloat64HoleToTagged(), value);
  }
  return graph()->NewNode(simplified()->ConvertTaggedHoleToUndefined(), value);
}

void JSCallReducer::CheckIfConstructor(Node* construct) {
  JSConstructNode n(construct);
  Node* new_target = n.new_target();
  Control control = n.control();

  Node* check =
      graph()->NewNode(simplified()->ObjectIsConstructor(), new_target);
  Node* check_branch =
      graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);
  Node* check_fail = graph()->NewNode(common()->IfFalse(), check_branch);
  Node* check_throw = check_fail = graph()->NewNode(
      javascript()->CallRuntime(Runtime::kThrowTypeError, 2),
      jsgraph()->ConstantNoHole(
          static_cast<int>(MessageTemplate::kNotConstructor)),
      new_target, n.context(), n.frame_state(), n.effect(), check_fail);
  control = graph()->NewNode(common()->IfTrue(), check_branch);
  NodeProperties::ReplaceControlInput(construct, control);

  // Rewire potential exception edges.
  Node* on_exception = nullptr;
  if (NodeProperties::IsExceptionalCall(construct, &on_exception)) {
    // Create appropriate {IfException}  and {IfSuccess} nodes.
    Node* if_exception =
        graph()->NewNode(common()->IfException(), check_throw, check_fail);
    check_fail = graph()->NewNode(common()->IfSuccess(), check_fail);

    // Join the exception edges.
    Node* merge =
        graph()->NewNode(common()->Merge(2), if_exception, on_exception);
    Node* ephi = graph()->NewNode(common()->EffectPhi(2), if_exception,
                                  on_exception, merge);
    Node* phi =
        graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                         if_exception, on_exception, merge);
    ReplaceWithValue(on_exception, phi, ephi, merge);
    merge->ReplaceInput(1, on_exception);
    ephi->ReplaceInput(1, on_exception);
    phi->ReplaceInput(1, on_exception);
  }

  // The above %ThrowTypeError runtime call is an unconditional throw,
  // making it impossible to return a successful completion in this case. We
  // simply connect the successful completion to the graph end.
  Node* throw_node =
      graph()->NewNode(common()->Throw(), check_throw, check_fail);
  MergeControlToEnd(graph(), common(), throw_node);
}

namespace {

bool ShouldUseCallICFeedback(Node* node) {
  HeapObjectMatcher m(node);
  if (m.HasResolvedValue() || m.IsCheckClosure() || m.IsJSCreateClosure()) {
    // Don't use CallIC feedback when we know the function
    // being called, i.e. either know the closure itself or
    // at least the SharedFunctionInfo.
    return false;
  } else if (m.IsPhi()) {
    // Protect against endless loops here.
    Node* control = NodeProperties::GetControlInput(node);
    if (control->opcode() == IrOpcode::kLoop ||
        control->opcode() == IrOpcode::kDead)
      return false;
    // Check if {node} is a Phi of nodes which shouldn't
    // use CallIC feedback (not looking through loops).
    int const value_input_count = m.node()->op()->ValueInputCount();
    for (int n = 0; n < value_input_count; ++n) {
      if (ShouldUseCallICFeedback(node->InputAt(n))) return true;
    }
    return false;
  }
  return true;
}

}  // namespace

Node* JSCallReducer::CheckArrayLength(Node* array, ElementsKind elements_kind,
                                      uint32_t array_length,
                                      const FeedbackSource& feedback_source,
                                      Effect effect, Control control) {
  Node* length = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSArrayLength(elements_kind)),
      array, effect, control);
  Node* check = graph()->NewNode(simplified()->NumberEqual(), length,
                                 jsgraph()->ConstantNoHole(array_length));
  return graph()->NewNode(
      simplified()->CheckIf(DeoptimizeReason::kArrayLengthChanged,
                            feedback_source),
      check, effect, control);
}

Reduction
JSCallReducer::ReduceCallOrConstructWithArrayLikeOrSpreadOfCreateArguments(
    Node* node, Node* arguments_list, int arraylike_or_spread_index,
    CallFrequency const& frequency, FeedbackSource const& feedback,
    SpeculationMode speculation_mode, CallFeedbackRelation feedback_relation) {
  DCHECK_EQ(arguments_list->opcode(), IrOpcode::kJSCreateArguments);

  // Check if {node} is the only value user of {arguments_list} (except for
  // value uses in frame states). If not, we give up for now.
  for (Edge edge : arguments_list->use_edges()) {
    if (!NodeProperties::IsValueEdge(edge)) continue;
    Node* const user = edge.from();
    switch (user->opcode()) {
      case IrOpcode::kCheckMaps:
      case IrOpcode::kFrameState:
      case IrOpcode::kStateValues:
      case IrOpcode::kReferenceEqual:
      case IrOpcode::kReturn:
        // Ignore safe uses that definitely don't mess with the arguments.
        continue;
      case IrOpcode::kLoadField: {
        DCHECK_EQ(arguments_list, user->InputAt(0));
        FieldAccess const& access = FieldAccessOf(user->op());
        if (access.offset == JSArray::kLengthOffset) {
          // Ignore uses for arguments#length.
          static_assert(
              static_cast<int>(JSArray::kLengthOffset) ==
              static_cast<int>(JSStrictArgumentsObject::kLengthOffset));
          static_assert(
              static_cast<int>(JSArray::kLengthOffset) ==
              static_cast<int>(JSSloppyArgumentsObject::kLengthOffset));
          continue;
        } else if (access.offset == JSObject::kElementsOffset) {
          // Ignore safe uses for arguments#elements.
          if (IsSafeArgumentsElements(user)) continue;
        }
        break;
      }
      case IrOpcode::kJSCallWithArrayLike: {
        // Ignore uses as argumentsList input to calls with array like.
        JSCallWithArrayLikeNode n(user);
        if (edge.index() == n.ArgumentIndex(0)) continue;
        break;
      }
      case IrOpcode::kJSConstructWithArrayLike: {
        // Ignore uses as argumentsList input to calls with array like.
        JSConstructWithArrayLikeNode n(user);
        if (edge.index() == n.ArgumentIndex(0)) continue;
        break;
      }
      case IrOpcode::kJSCallWithSpread: {
        // Ignore uses as spread input to calls with spread.
        JSCallWithSpreadNode n(user);
        if (edge.index() == n.LastArgumentIndex()) continue;
        break;
      }
      case IrOpcode::kJSConstructWithSpread: {
        // Ignore uses as spread input to construct with spread.
        JSConstructWithSpreadNode n(user);
        if (edge.index() == n.LastArgumentIndex()) continue;
        break;
      }
      default:
        break;
    }
    // We cannot currently reduce the {node} to something better than what
    // it already is, but we might be able to do something about the {node}
    // later, so put it on the waitlist and try again during finalization.
    waitlist_.insert(node);
    return NoChange();
  }

  // Get to the actual frame state from which to extract the arguments;
  // we can only optimize this in case the {node} was already inlined into
  // some other function (and same for the {arguments_list}).
  CreateArgumentsType const type = CreateArgumentsTypeOf(arguments_list->op());
  FrameState frame_state =
      FrameState{NodeProperties::GetFrameStateInput(arguments_list)};

  int formal_parameter_count;
  {
    Handle<SharedFunctionInfo> shared;
    if (!frame_state.frame_state_info().shared_info().ToHandle(&shared)) {
      return NoChange();
    }
    formal_parameter_count =
        MakeRef(broker(), shared)
            .internal_formal_parameter_count_without_receiver();
  }

  if (type == CreateArgumentsType::kMappedArguments) {
    // Mapped arguments (sloppy mode) that are aliased can only be handled
    // here if there's no side-effect between the {node} and the {arg_array}.
    // TODO(turbofan): Further relax this constraint.
    if (formal_parameter_count != 0) {
      Node* effect = NodeProperties::GetEffectInput(node);
      if (!NodeProperties::NoObservableSideEffectBetween(effect,
                                                         arguments_list)) {
        return NoChange();
      }
    }
  }

  // For call/construct with spread, we need to also install a code
  // dependency on the array iterator lookup protector cell to ensure
  // that no one messed with the %ArrayIteratorPrototype%.next method.
  if (IsCallOrConstructWithSpread(node)) {
    if (!dependencies()->DependOnArrayIteratorProtector()) return NoChange();
  }

  // Remove the {arguments_list} input from the {node}.
  node->RemoveInput(arraylike_or_spread_index);

  // The index of the first relevant parameter. Only non-zero when looking at
  // rest parameters, in which case it is set to the index of the first rest
  // parameter.
  const int start_index = (type == CreateArgumentsType::kRestParameter)
                              ? formal_parameter_count
                              : 0;

  // After removing the arraylike or spread object, the argument count is:
  int argc =
      arraylike_or_spread_index - JSCallOrConstructNode::FirstArgumentIndex();
  // Check if are spreading to inlined arguments or to the arguments of
  // the outermost function.
  if (frame_state.outer_frame_state()->opcode() != IrOpcode::kFrameState) {
    Operator const* op;
    if (IsCallWithArrayLikeOrSpread(node)) {
      static constexpr int kTargetAndReceiver = 2;
      op = javascript()->CallForwardVarargs(argc + kTargetAndReceiver,
                                            start_index);
    } else {
      static constexpr int kTargetAndNewTarget = 2;
      op = javascript()->ConstructForwardVarargs(argc + kTargetAndNewTarget,
                                                 start_index);
    }
    node->RemoveInput(JSCallOrConstructNode::FeedbackVectorIndexForArgc(argc));
    NodeProperties::ChangeOp(node, op);
    return Changed(node);
  }
  // Get to the actual frame state from which to extract the arguments;
  // we can only optimize this in case the {node} was already inlined into
  // some other function (and same for the {arg_array}).
  FrameState outer_state{frame_state.outer_frame_state()};
  FrameStateInfo outer_info = outer_state.frame_state_info();
  if (outer_info.type() == FrameStateType::kInlinedExtraArguments) {
    // Need to take the parameters from the inlined extra arguments frame state.
    frame_state = outer_state;
  }
  // Add the actual parameters to the {node}, skipping the receiver.
  StateValuesAccess parameters_access(frame_state.parameters());
  for (auto it = parameters_access.begin_without_receiver_and_skip(start_index);
       !it.done(); ++it) {
    DCHECK_NOT_NULL(it.node());
    node->InsertInput(graph()->zone(),
                      JSCallOrConstructNode::ArgumentIndex(argc++), it.node());
  }

  if (IsCallWithArrayLikeOrSpread(node)) {
    NodeProperties::ChangeOp(
        node, javascript()->Call(JSCallNode::ArityForArgc(argc), frequency,
                                 feedback, ConvertReceiverMode::kAny,
                                 speculation_mode, feedback_relation));
    return Changed(node).FollowedBy(ReduceJSCall(node));
  } else {
    NodeProperties::ChangeOp(
        node, javascript()->Construct(JSConstructNode::ArityForArgc(argc),
                                      frequency, feedback));

    // Check whether the given new target value is a constructor function. The
    // replacement {JSConstruct} operator only checks the passed target value
    // but relies on the new target value to be implicitly valid.
    CheckIfConstructor(node);
    return Changed(node).FollowedBy(ReduceJSConstruct(node));
  }
}

Reduction JSCallReducer::ReduceCallOrConstructWithArrayLikeOrSpread(
    Node* node, int argument_count, int arraylike_or_spread_index,
    CallFrequency const& frequency, FeedbackSource const& feedback_source,
    SpeculationMode speculation_mode, CallFeedbackRelation feedback_relation,
    Node* target, Effect effect, Control control) {
  DCHECK(IsCallOrConstructWithArrayLike(node) ||
         IsCallOrConstructWithSpread(node));
  DCHECK_IMPLIES(speculation_mode == SpeculationMode::kAllowSpeculation,
                 feedback_source.IsValid());

  Node* arguments_list =
      NodeProperties::GetValueInput(node, arraylike_or_spread_index);

  if (arguments_list->opcode() == IrOpcode::kJSCreateArguments) {
    return ReduceCallOrConstructWithArrayLikeOrSpreadOfCreateArguments(
        node, arguments_list, arraylike_or_spread_index, frequency,
        feedback_source, speculation_mode, feedback_relation);
  }

  if (!v8_flags.turbo_optimize_apply) return NoChange();

  // Optimization of construct nodes not supported yet.
  if (!IsCallWithArrayLikeOrSpread(node)) return NoChange();

  // Avoid deoptimization loops.
  if (speculation_mode != SpeculationMode::kAllowSpeculation) return NoChange();

  // Only optimize with array literals.
  if (arguments_list->opcode() != IrOpcode::kJSCreateLiteralArray &&
      arguments_list->opcode() != IrOpcode::kJSCreateEmptyLiteralArray) {
    return NoChange();
  }

  // For call/construct with spread, we need to also install a code
  // dependency on the array iterator lookup protector cell to ensure
  // that no one messed with the %ArrayIteratorPrototype%.next method.
  if (IsCallOrConstructWithSpread(node)) {
    if (!dependencies()->DependOnArrayIteratorProtector()) return NoChange();
  }

  if (arguments_list->opcode() == IrOpcode::kJSCreateEmptyLiteralArray) {
    if (generated_calls_with_array_like_or_spread_.count(node)) {
      return NoChange();  // Avoid infinite recursion.
    }
    JSCallReducerAssembler a(this, node);
    Node* subgraph = a.ReduceJSCallWithArrayLikeOrSpreadOfEmpty(
        &generated_calls_with_array_like_or_spread_);
    return ReplaceWithSubgraph(&a, subgraph);
  }

  DCHECK_EQ(arguments_list->opcode(), IrOpcode::kJSCreateLiteralArray);
  int new_argument_count;

  // Find array length and elements' kind from the feedback's allocation
  // site's boilerplate JSArray.
  JSCreateLiteralOpNode args_node(arguments_list);
  CreateLiteralParameters const& args_params = args_node.Parameters();
  const FeedbackSource& array_feedback = args_params.feedback();
  const ProcessedFeedback& feedback =
      broker()->GetFeedbackForArrayOrObjectLiteral(array_feedback);
  if (feedback.IsInsufficient()) return NoChange();

  AllocationSiteRef site = feedback.AsLiteral().value();
  if (!site.boilerplate(broker()).has_value()) return NoChange();

  JSArrayRef boilerplate_array = site.boilerplate(broker())->AsJSArray();
  int const array_length =
      boilerplate_array.GetBoilerplateLength(broker()).AsSmi();

  // We'll replace the arguments_list input with {array_length} element loads.
  new_argument_count = argument_count - 1 + array_length;

  // Do not optimize calls with a large number of arguments.
  // Arbitrarily sets the limit to 32 arguments.
  const int kMaxArityForOptimizedFunctionApply = 32;
  if (new_argument_count > kMaxArityForOptimizedFunctionApply) {
    return NoChange();
  }

  // Determine the array's map.
  MapRef array_map = boilerplate_array.map(broker());
  if (!array_map.supports_fast_array_iteration(broker())) {
    return NoChange();
  }

  // Check and depend on NoElementsProtector.
  if (!dependencies()->DependOnNoElementsProtector()) {
    return NoChange();
  }

  // Remove the {arguments_list} node which will be replaced by a sequence of
  // LoadElement nodes.
  node->RemoveInput(arraylike_or_spread_index);

  // Speculate on that array's map is still equal to the dynamic map of
  // arguments_list; generate a map check.
  effect = graph()->NewNode(
      simplified()->CheckMaps(CheckMapsFlag::kNone, ZoneRefSet<Map>(array_map),
                              feedback_source),
      arguments_list, effect, control);

  // Speculate on that array's length being equal to the dynamic length of
  // arguments_list; generate a deopt check.
  ElementsKind elements_kind = array_map.elements_kind();
  effect = CheckArrayLength(arguments_list, elements_kind, array_length,
                            feedback_source, effect, control);

  // Generate N element loads to replace the {arguments_list} node with a set
  // of arguments loaded from it.
  Node* elements = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSObjectElements()),
      arguments_list, effect, control);
  for (int i = 0; i < array_length; i++) {
    // Load the i-th element from the array.
    Node* index = jsgraph()->ConstantNoHole(i);
    Node* load = effect = graph()->NewNode(
        simplified()->LoadElement(
            AccessBuilder::ForFixedArrayElement(elements_kind)),
        elements, index, effect, control);

    // In "holey" arrays some arguments might be missing and we pass
    // 'undefined' instead.
    if (IsHoleyElementsKind(elements_kind)) {
      load = ConvertHoleToUndefined(load, elements_kind);
    }

    node->InsertInput(graph()->zone(), arraylike_or_spread_index + i, load);
  }

  NodeProperties::ChangeOp(
      node,
      javascript()->Call(JSCallNode::ArityForArgc(new_argument_count),
                         frequency, feedback_source, ConvertReceiverMode::kAny,
                         speculation_mode, CallFeedbackRelation::kUnrelated));
  NodeProperties::ReplaceEffectInput(node, effect);
  return Changed(node).FollowedBy(ReduceJSCall(node));
}

bool JSCallReducer::IsBuiltinOrApiFunction(JSFunctionRef function) const {
  // TODO(neis): Add a way to check if function template info isn't serialized
  // and add a warning in such cases. Currently we can't tell if function
  // template info doesn't exist or wasn't serialized.
  return function.shared(broker()).HasBuiltinId() ||
         function.shared(broker()).function_template_info(broker()).has_value();
}

Reduction JSCallReducer::ReduceJSCall(Node* node) {
  if (broker()->StackHasOverflowed()) return NoChange();

  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  Node* target = n.target();
  Effect effect = n.effect();
  Control control = n.control();
  int arity = p.arity_without_implicit_args();

  // Try to specialize JSCall {node}s with constant {target}s.
  HeapObjectMatcher m(target);
  if (m.HasResolvedValue()) {
    ObjectRef target_ref = m.Ref(broker());
    if (target_ref.IsJSFunction()) {
      JSFunctionRef function = target_ref.AsJSFunction();

      // Don't inline cross native context.
      if (!function.native_context(broker()).equals(native_context())) {
        return NoChange();
      }

      return ReduceJSCall(node, function.shared(broker()));
    } else if (target_ref.IsJSBoundFunction()) {
      JSBoundFunctionRef function = target_ref.AsJSBoundFunction();
      ObjectRef bound_this = function.bound_this(broker());
      ConvertReceiverMode const convert_mode =
          bound_this.IsNullOrUndefined()
              ? ConvertReceiverMode::kNullOrUndefined
              : ConvertReceiverMode::kNotNullOrUndefined;

      // TODO(jgruber): Inline this block below once TryGet is guaranteed to
      // succeed.
      FixedArrayRef bound_arguments = function.bound_arguments(broker());
      const int bound_arguments_length = bound_arguments.length();
      static constexpr int kInlineSize = 16;  // Arbitrary.
      base::SmallVector<Node*, kInlineSize> args;
      for (int i = 0; i < bound_arguments_length; ++i) {
        OptionalObjectRef maybe_arg = bound_arguments.TryGet(broker(), i);
        if (!maybe_arg.has_value()) {
          TRACE_BROKER_MISSING(broker(), "bound argument");
          return NoChange();
        }
        args.emplace_back(
            jsgraph()->ConstantNoHole(maybe_arg.value(), broker()));
      }

      // Patch {node} to use [[BoundTargetFunction]] and [[BoundThis]].
      NodeProperties::ReplaceValueInput(
          node,
          jsgraph()->ConstantNoHole(function.bound_target_function(broker()),
                                    broker()),
          JSCallNode::TargetIndex());
      NodeProperties::ReplaceValueInput(
          node, jsgraph()->ConstantNoHole(bound_this, broker()),
          JSCallNode::ReceiverIndex());

      // Insert the [[BoundArguments]] for {node}.
      for (int i = 0; i < bound_arguments_length; ++i) {
        node->InsertInput(graph()->zone(), i + 2, args[i]);
        arity++;
      }

      NodeProperties::ChangeOp(
          node,
          javascript()->Call(JSCallNode::ArityForArgc(arity), p.frequency(),
                             p.feedback(), convert_mode, p.speculation_mode(),
                             CallFeedbackRelation::kUnrelated));

      // Try to further reduce the JSCall {node}.
      return Changed(node).FollowedBy(ReduceJSCall(node));
    }

    // Don't mess with other {node}s that have a constant {target}.
    // TODO(bmeurer): Also support proxies here.
    return NoChange();
  }

  // If {target} is the result of a JSCreateClosure operation, we can
  // just immediately try to inline based on the SharedFunctionInfo,
  // since TurboFan generally doesn't inline cross-context, and hence
  // the {target} must have the same native context as the call site.
  // Same if the {target} is the result of a CheckClosure operation.
  if (target->opcode() == IrOpcode::kJSCreateClosure) {
    CreateClosureParameters const& params =
        JSCreateClosureNode{target}.Parameters();
    return ReduceJSCall(node, params.shared_info());
  } else if (target->opcode() == IrOpcode::kCheckClosure) {
    FeedbackCellRef cell = MakeRef(broker(), FeedbackCellOf(target->op()));
    OptionalSharedFunctionInfoRef shared = cell.shared_function_info(broker());
    if (!shared.has_value()) {
      TRACE_BROKER_MISSING(broker(), "Unable to reduce JSCall. FeedbackCell "
                                         << cell << " has no FeedbackVector");
      return NoChange();
    }
    return ReduceJSCall(node, *shared);
  }

  // If {target} is the result of a JSCreateBoundFunction operation,
  // we can just fold the construction and call the bound target
  // function directly instead.
  if (target->opcode() == IrOpcode::kJSCreateBoundFunction) {
    Node* bound_target_function = NodeProperties::GetValueInput(target, 0);
    Node* bound_this = NodeProperties::GetValueInput(target, 1);
    int const bound_arguments_length =
        static_cast<int>(CreateBoundFunctionParametersOf(target->op()).arity());

    // Patch the {node} to use [[BoundTargetFunction]] and [[BoundThis]].
    NodeProperties::ReplaceValueInput(node, bound_target_function,
                                      n.TargetIndex());
    NodeProperties::ReplaceValueInput(node, bound_this, n.ReceiverIndex());

    // Insert the [[BoundArguments]] for {node}.
    for (int i = 0; i < bound_arguments_length; ++i) {
      Node* value = NodeProperties::GetValueInput(target, 2 + i);
      node->InsertInput(graph()->zone(), n.ArgumentIndex(i), value);
      arity++;
    }

    // Update the JSCall operator on {node}.
    ConvertReceiverMode const convert_mode =
        NodeProperties::CanBeNullOrUndefined(broker(), bound_this, effect)
            ? ConvertReceiverMode::kAny
            : ConvertReceiverMode::kNotNullOrUndefined;
    NodeProperties::ChangeOp(
        node,
        javascript()->Call(JSCallNode::ArityForArgc(arity), p.frequency(),
                           p.feedback(), convert_mode, p.speculation_mode(),
                           CallFeedbackRelation::kUnrelated));

    // Try to further reduce the JSCall {node}.
    return Changed(node).FollowedBy(ReduceJSCall(node));
  }

  if (!ShouldUseCallICFeedback(target) ||
      p.feedback_relation() == CallFeedbackRelation::kUnrelated ||
      !p.feedback().IsValid()) {
    return NoChange();
  }

  ProcessedFeedback const& feedback =
      broker()->GetFeedbackForCall(p.feedback());
  if (feedback.IsInsufficient()) {
    return ReduceForInsufficientFeedback(
        node, DeoptimizeReason::kInsufficientTypeFeedbackForCall);
  }

  OptionalHeapObjectRef feedback_target;
  if (p.feedback_relation() == CallFeedbackRelation::kTarget) {
    feedback_target = feedback.AsCall().target();
  } else {
    DCHECK_EQ(p.feedback_relation(), CallFeedbackRelation::kReceiver);
    feedback_target = native_context().function_prototype_apply(broker());
  }

  if (feedback_target.has_value() &&
      feedback_target->map(broker()).is_callable()) {
    Node* target_function =
        jsgraph()->ConstantNoHole(*feedback_target, broker());

    // Check that the {target} is still the {target_function}.
    Node* check = graph()->NewNode(simplified()->ReferenceEqual(), target,
                                   target_function);
    effect = graph()->NewNode(
        simplified()->CheckIf(DeoptimizeReason::kWrongCallTarget), check,
        effect, control);

    // Specialize the JSCall node to the {target_function}.
    NodeProperties::ReplaceValueInput(node, target_function, n.TargetIndex());
    NodeProperties::ReplaceEffectInput(node, effect);

    // Try to further reduce the JSCall {node}.
    return Changed(node).FollowedBy(ReduceJSCall(node));
  } else if (feedback_target.has_value() && feedback_target->IsFeedbackCell()) {
    FeedbackCellRef feedback_cell = feedback_target.value().AsFeedbackCell();
    // TODO(neis): This check seems unnecessary.
    if (feedback_cell.feedback_vector(broker()).has_value()) {
      // Check that {target} is a closure with given {feedback_cell},
      // which uniquely identifies a given function inside a native context.
      Node* target_closure = effect =
          graph()->NewNode(simplified()->CheckClosure(feedback_cell.object()),
                           target, effect, control);

      // Specialize the JSCall node to the {target_closure}.
      NodeProperties::ReplaceValueInput(node, target_closure, n.TargetIndex());
      NodeProperties::ReplaceEffectInput(node, effect);

      // Try to further reduce the JSCall {node}.
      return Changed(node).FollowedBy(ReduceJSCall(node));
    }
  }
  return NoChange();
}

Reduction JSCallReducer::ReduceJSCall(Node* node,
                                      SharedFunctionInfoRef shared) {
  JSCallNode n(node);
  Node* target = n.target();

  // Do not reduce calls to functions with break points.
  // If this state changes during background compilation, the compilation
  // job will be aborted from the main thread (see
  // Debug::PrepareFunctionForDebugExecution()).
  if (shared.HasBreakInfo(broker())) return NoChange();

  // Class constructors are callable, but [[Call]] will raise an exception.
  // See ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList ).
  if (IsClassConstructor(shared.kind())) {
    NodeProperties::ReplaceValueInputs(node, target);
    NodeProperties::ChangeOp(
        node, javascript()->CallRuntime(
                  Runtime::kThrowConstructorNonCallableError, 1));
    return Changed(node);
  }

  // Check for known builtin functions.

  Builtin builtin =
      shared.HasBuiltinId() ? shared.builtin_id() : Builtin::kNoBuiltinId;
  switch (builtin) {
    case Builtin::kArrayConstructor:
      return ReduceArrayConstructor(node);
    case Builtin::kBooleanConstructor:
      return ReduceBooleanConstructor(node);
    case Builtin::kFunctionPrototypeApply:
      return ReduceFunctionPrototypeApply(node);
    case Builtin::kFastFunctionPrototypeBind:
      return ReduceFunctionPrototypeBind(node);
    case Builtin::kFunctionPrototypeCall:
      return ReduceFunctionPrototypeCall(node);
    case Builtin::kFunctionPrototypeHasInstance:
      return ReduceFunctionPrototypeHasInstance(node);
    case Builtin::kObjectConstructor:
      return ReduceObjectConstructor(node);
    case Builtin::kObjectCreate:
      return ReduceObjectCreate(node);
    case Builtin::kObjectGetPrototypeOf:
      return ReduceObjectGetPrototypeOf(node);
    case Builtin::kObjectIs:
      return ReduceObjectIs(node);
    case Builtin::kObjectPrototypeGetProto:
      return ReduceObjectPrototypeGetProto(node);
    case Builtin::kObjectPrototypeHasOwnProperty:
      return ReduceObjectPrototypeHasOwnProperty(node);
    case Builtin::kObjectPrototypeIsPrototypeOf:
      return ReduceObjectPrototypeIsPrototypeOf(node);
    case Builtin::kReflectApply:
      return ReduceReflectApply(node);
    case Builtin::kReflectConstruct:
      return ReduceReflectConstruct(node);
    case Builtin::kReflectGet:
      return ReduceReflectGet(node);
    case Builtin::kReflectGetPrototypeOf:
      return ReduceReflectGetPrototypeOf(node);
    case Builtin::kReflectHas:
      return ReduceReflectHas(node);
    case Builtin::kArrayForEach:
      return ReduceArrayForEach(node, shared);
    case Builtin::kArrayMap:
      return ReduceArrayMap(node, shared);
    case Builtin::kArrayFilter:
      return ReduceArrayFilter(node, shared);
    case Builtin::kArrayReduce:
      return ReduceArrayReduce(node, shared);
    case Builtin::kArrayReduceRight:
      return ReduceArrayReduceRight(node, shared);
    case Builtin::kArrayPrototypeFind:
      return ReduceArrayFind(node, shared);
    case Builtin::kArrayPrototypeFindIndex:
      return ReduceArrayFindIndex(node, shared);
    case Builtin::kArrayEvery:
      return ReduceArrayEvery(node, shared);
    case Builtin::kArrayIndexOf:
      return ReduceArrayIndexOf(node);
    case Builtin::kArrayIncludes:
      return ReduceArrayIncludes(node);
    case Builtin::kArraySome:
      return ReduceArraySome(node, shared);
    case Builtin::kArrayPrototypeAt:
      return ReduceArrayPrototypeAt(node);
    case Builtin::kArrayPrototypePush:
      return ReduceArrayPrototypePush(node);
    case Builtin::kArrayPrototypePop:
      return ReduceArrayPrototypePop(node);
    // TODO(v8:14409): The current implementation of the inlined
    // ArrayPrototypeShift version doesn't seem to be beneficial and even
    // counter-productive at least for Object ElementsKinds. Disable it until
    // improvements/better heuristics have been implemented.
    // case Builtin::kArrayPrototypeShift:
    //   return ReduceArrayPrototypeShift(node);
    case Builtin::kArrayPrototypeSlice:
      return ReduceArrayPrototypeSlice(node);
    case Builtin::kArrayPrototypeEntries:
      return ReduceArrayIterator(node, ArrayIteratorKind::kArrayLike,
                                 IterationKind::kEntries);
    case Builtin::kArrayPrototypeKeys:
      return ReduceArrayIterator(node, ArrayIteratorKind::kArrayLike,
                                 IterationKind::kKeys);
    case Builtin::kArrayPrototypeValues:
      return ReduceArrayIterator(node, ArrayIteratorKind::kArrayLike,
                                 IterationKind::kValues);
    case Builtin::kArrayIteratorPrototypeNext:
      return ReduceArrayIteratorPrototypeNext(node);
    case Builtin::kArrayIsArray:
      return ReduceArrayIsArray(node);
    case Builtin::kArrayBufferIsView:
      return ReduceArrayBufferIsView(node);
    case Builtin::kDataViewPrototypeGetByteLength:
      // TODO(v8:11111): Optimize for JS_RAB_GSAB_DATA_VIEW_TYPE too.
      return ReduceArrayBufferViewByteLengthAccessor(node, JS_DATA_VIEW_TYPE);
    case Builtin::kDataViewPrototypeGetByteOffset:
      // TODO(v8:11111): Optimize for JS_RAB_GSAB_DATA_VIEW_TYPE too.
      return ReduceArrayBufferViewAccessor(
          node, JS_DATA_VIEW_TYPE,
          AccessBuilder::ForJSArrayBufferViewByteOffset(), builtin);
    case Builtin::kDataViewPrototypeGetUint8:
      return ReduceDataViewAccess(node, DataViewAccess::kGet,
                                  ExternalArrayType::kExternalUint8Array);
    case Builtin::kDataViewPrototypeGetInt8:
      return ReduceDataViewAccess(node, DataViewAccess::kGet,
                                  ExternalArrayType::kExternalInt8Array);
    case Builtin::kDataViewPrototypeGetUint16:
      return ReduceDataViewAccess(node, DataViewAccess::kGet,
                                  ExternalArrayType::kExternalUint16Array);
    case Builtin::kDataViewPrototypeGetInt16:
      return ReduceDataViewAccess(node, DataViewAccess::kGet,
                                  ExternalArrayType::kExternalInt16Array);
    case Builtin::kDataViewPrototypeGetUint32:
      return ReduceDataViewAccess(node, DataViewAccess::kGet,
                                  ExternalArrayType::kExternalUint32Array);
    case Builtin::kDataViewPrototypeGetInt32:
      return ReduceDataViewAccess(node, DataViewAccess::kGet,
                                  ExternalArrayType::kExternalInt32Array);
    case Builtin::kDataViewPrototypeGetFloat32:
      return ReduceDataViewAccess(node, DataViewAccess::kGet,
                                  ExternalArrayType::kExternalFloat32Array);
    case Builtin::kDataViewPrototypeGetFloat64:
      return ReduceDataViewAccess(node, DataViewAccess::kGet,
                                  ExternalArrayType::kExternalFloat64Array);
    case Builtin::kDataViewPrototypeSetUint8:
      return ReduceDataViewAccess(node, DataViewAccess::kSet,
                                  ExternalArrayType::kExternalUint8Array);
    case Builtin::kDataViewPrototypeSetInt8:
      return ReduceDataViewAccess(node, DataViewAccess::kSet,
                                  ExternalArrayType::kExternalInt8Array);
    case Builtin::kDataViewPrototypeSetUint16:
      return ReduceDataViewAccess(node, DataViewAccess::kSet,
                                  ExternalArrayType::kExternalUint16Array);
    case Builtin::kDataViewPrototypeSetInt16:
      return ReduceDataViewAccess(node, DataViewAccess::kSet,
                                  ExternalArrayType::kExternalInt16Array);
    case Builtin::kDataViewPrototypeSetUint32:
      return ReduceDataViewAccess(node, DataViewAccess::kSet,
                                  ExternalArrayType::kExternalUint32Array);
    case Builtin::kDataViewPrototypeSetInt32:
      return ReduceDataViewAccess(node, DataViewAccess::kSet,
                                  ExternalArrayType::kExternalInt32Array);
    case Builtin::kDataViewPrototypeSetFloat32:
      return ReduceDataViewAccess(node, DataViewAccess::kSet,
                                  ExternalArrayType::kExternalFloat32Array);
    case Builtin::kDataViewPrototypeSetFloat64:
      return ReduceDataViewAccess(node, DataViewAccess::kSet,
                                  ExternalArrayType::kExternalFloat64Array);
    case Builtin::kTypedArrayPrototypeByteLength:
      return ReduceArrayBufferViewByteLengthAccessor(node, JS_TYPED_ARRAY_TYPE);
    case Builtin::kTypedArrayPrototypeByteOffset:
      return ReduceArrayBufferViewAccessor(
          node, JS_TYPED_ARRAY_TYPE,
          AccessBuilder::ForJSArrayBufferViewByteOffset(), builtin);
    case Builtin::kTypedArrayPrototypeLength:
      return ReduceTypedArrayPrototypeLength(node);
    case Builtin::kTypedArrayPrototypeToStringTag:
      return ReduceTypedArrayPrototypeToStringTag(node);
    case Builtin::kMathAbs:
      return ReduceMathUnary(node, simplified()->NumberAbs());
    case Builtin::kMathAcos:
      return ReduceMathUnary(node, simplified()->NumberAcos());
    case Builtin::kMathAcosh:
      return ReduceMathUnary(node, simplified()->NumberAcosh());
    case Builtin::kMathAsin:
      return ReduceMathUnary(node, simplified()->NumberAsin());
    case Builtin::kMathAsinh:
      return ReduceMathUnary(node, simplified()->NumberAsinh());
    case Builtin::kMathAtan:
      return ReduceMathUnary(node, simplified()->NumberAtan());
    case Builtin::kMathAtanh:
      return ReduceMathUnary(node, simplified()->NumberAtanh());
    case Builtin::kMathCbrt:
      return ReduceMathUnary(node, simplified()->NumberCbrt());
    case Builtin::kMathCeil:
      return ReduceMathUnary(node, simplified()->NumberCeil());
    case Builtin::kMathCos:
      return ReduceMathUnary(node, simplified()->NumberCos());
    case Builtin::kMathCosh:
      return ReduceMathUnary(node, simplified()->NumberCosh());
    case Builtin::kMathExp:
      return ReduceMathUnary(node, simplified()->NumberExp());
    case Builtin::kMathExpm1:
      return ReduceMathUnary(node, simplified()->NumberExpm1());
    case Builtin::kMathFloor:
      return ReduceMathUnary(node, simplified()->NumberFloor());
    case Builtin::kMathFround:
      return ReduceMathUnary(node, simplified()->NumberFround());
    case Builtin::kMathLog:
      return ReduceMathUnary(node, simplified()->NumberLog());
    case Builtin::kMathLog1p:
      return ReduceMathUnary(node, simplified()->NumberLog1p());
    case Builtin::kMathLog10:
      return ReduceMathUnary(node, simplified()->NumberLog10());
    case Builtin::kMathLog2:
      return ReduceMathUnary(node, simplified()->NumberLog2());
    case Builtin::kMathRound:
      return ReduceMathUnary(node, simplified()->NumberRound());
    case Builtin::kMathSign:
      return ReduceMathUnary(node, simplified()->NumberSign());
    case Builtin::kMathSin:
      return ReduceMathUnary(node, simplified()->NumberSin());
    case Builtin::kMathSinh:
      return ReduceMathUnary(node, simplified()->NumberSinh());
    case Builtin::kMathSqrt:
      return ReduceMathUnary(node, simplified()->NumberSqrt());
    case Builtin::kMathTan:
      return ReduceMathUnary(node, simplified()->NumberTan());
    case Builtin::kMathTanh:
      return ReduceMathUnary(node, simplified()->NumberTanh());
    case Builtin::kMathTrunc:
      return ReduceMathUnary(node, simplified()->NumberTrunc());
    case Builtin::kMathAtan2:
      return ReduceMathBinary(node, simplified()->NumberAtan2());
    case Builtin::kMathPow:
      return ReduceMathBinary(node, simplified()->NumberPow());
    case Builtin::kMathClz32:
      return ReduceMathClz32(node);
    case Builtin::kMathImul:
      return ReduceMathImul(node);
    case Builtin::kMathMax:
      return ReduceMathMinMax(node, simplified()->NumberMax(),
                              jsgraph()->ConstantNoHole(-V8_INFINITY));
    case Builtin::kMathMin:
      return ReduceMathMinMax(node, simplified()->NumberMin(),
                              jsgraph()->ConstantNoHole(V8_INFINITY));
    case Builtin::kNumberIsFinite:
      return ReduceNumberIsFinite(node);
    case Builtin::kNumberIsInteger:
      return ReduceNumberIsInteger(node);
    case Builtin::kNumberIsSafeInteger:
      return ReduceNumberIsSafeInteger(node);
    case Builtin::kNumberIsNaN:
      return ReduceNumberIsNaN(node);
    case Builtin::kNumberParseInt:
      return ReduceNumberParseInt(node);
    case Builtin::kGlobalIsFinite:
      return ReduceGlobalIsFinite(node);
    case Builtin::kGlobalIsNaN:
      return ReduceGlobalIsNaN(node);
    case Builtin::kMapPrototypeGet:
      return ReduceMapPrototypeGet(node);
    case Builtin::kMapPrototypeHas:
      return ReduceMapPrototypeHas(node);
    case Builtin::kSetPrototypeHas:
      return ReduceSetPrototypeHas(node);
    case Builtin::kRegExpPrototypeTest:
      return ReduceRegExpPrototypeTest(node);
    case Builtin::kReturnReceiver:
      return ReduceReturnReceiver(node);
    case Builtin::kStringPrototypeIndexOf:
      return ReduceStringPrototypeIndexOfIncludes(
          node, StringIndexOfIncludesVariant::kIndexOf);
    case Builtin::kStringPrototypeIncludes:
      return ReduceStringPrototypeIndexOfIncludes(
          node, StringIndexOfIncludesVariant::kIncludes);
    case Builtin::kStringPrototypeCharAt:
      return ReduceStringPrototypeCharAt(node);
    case Builtin::kStringPrototypeCharCodeAt:
      return ReduceStringPrototypeStringAt(simplified()->StringCharCodeAt(),
                                           node);
    case Builtin::kStringPrototypeCodePointAt:
      return ReduceStringPrototypeStringAt(simplified()->StringCodePointAt(),
                                           node);
    case Builtin::kStringPrototypeSubstring:
      return ReduceStringPrototypeSubstring(node);
    case Builtin::kStringPrototypeSlice:
      return ReduceStringPrototypeSlice(node);
    case Builtin::kStringPrototypeSubstr:
      return ReduceStringPrototypeSubstr(node);
    case Builtin::kStringPrototypeStartsWith:
      return ReduceStringPrototypeStartsWith(node);
    case Builtin::kStringPrototypeEndsWith:
      return ReduceStringPrototypeEndsWith(node);
#ifdef V8_INTL_SUPPORT
    case Builtin::kStringPrototypeToLowerCaseIntl:
      return ReduceStringPrototypeToLowerCaseIntl(node);
    case Builtin::kStringPrototypeToUpperCaseIntl:
      return ReduceStringPrototypeToUpperCaseIntl(node);
#endif  // V8_INTL_SUPPORT
    case Builtin::kStringFromCharCode:
      return ReduceStringFromCharCode(node);
    case Builtin::kStringFromCodePoint:
      return ReduceStringFromCodePoint(node);
    case Builtin::kStringPrototypeIterator:
      return ReduceStringPrototypeIterator(node);
    case Builtin::kStringPrototypeLocaleCompare:
      return ReduceStringPrototypeLocaleCompare(node);
    case Builtin::kStringIteratorPrototypeNext:
      return ReduceStringIteratorPrototypeNext(node);
    case Builtin::kStringPrototypeConcat:
      return ReduceStringPrototypeConcat(node);
    case Builtin::kTypedArrayPrototypeEntries:
      return ReduceArrayIterator(node, ArrayIteratorKind::kTypedArray,
                                 IterationKind::kEntries);
    case Builtin::kTypedArrayPrototypeKeys:
      return ReduceArrayIterator(node, ArrayIteratorKind::kTypedArray,
                                 IterationKind::kKeys);
    case Builtin::kTypedArrayPrototypeValues:
      return ReduceArrayIterator(node, ArrayIteratorKind::kTypedArray,
                                 IterationKind::kValues);
    case Builtin::kPromisePrototypeCatch:
      return ReducePromisePrototypeCatch(node);
    case Builtin::kPromisePrototypeFinally:
      return ReducePromisePrototypeFinally(node);
    case Builtin::kPromisePrototypeThen:
      return ReducePromisePrototypeThen(node);
    case Builtin::kPromiseResolveTrampoline:
      return ReducePromiseResolveTrampoline(node);
    case Builtin::kMapPrototypeEntries:
      return ReduceCollectionIteration(node, CollectionKind::kMap,
                                       IterationKind::kEntries);
    case Builtin::kMapPrototypeKeys:
      return ReduceCollectionIteration(node, CollectionKind::kMap,
                                       IterationKind::kKeys);
    case Builtin::kMapPrototypeGetSize:
      return ReduceCollectionPrototypeSize(node, CollectionKind::kMap);
    case Builtin::kMapPrototypeValues:
      return ReduceCollectionIteration(node, CollectionKind::kMap,
                                       IterationKind::kValues);
    case Builtin::kMapIteratorPrototypeNext:
      return ReduceCollectionIteratorPrototypeNext(
          node, OrderedHashMap::kEntrySize, factory()->empty_ordered_hash_map(),
          FIRST_JS_MAP_ITERATOR_TYPE, LAST_JS_MAP_ITERATOR_TYPE);
    case Builtin::kSetPrototypeEntries:
      return ReduceCollectionIteration(node, CollectionKind::kSet,
                                       IterationKind::kEntries);
    case Builtin::kSetPrototypeGetSize:
      return ReduceCollectionPrototypeSize(node, CollectionKind::kSet);
    case Builtin::kSetPrototypeValues:
      return ReduceCollectionIteration(node, CollectionKind::kSet,
                                       IterationKind::kValues);
    case Builtin::kSetIteratorPrototypeNext:
      return ReduceCollectionIteratorPrototypeNext(
          node, OrderedHashSet::kEntrySize, factory()->empty_ordered_hash_set(),
          FIRST_JS_SET_ITERATOR_TYPE, LAST_JS_SET_ITERATOR_TYPE);
    case Builtin::kDatePrototypeGetTime:
      return ReduceDatePrototypeGetTime(node);
    case Builtin::kDateNow:
      return ReduceDateNow(node);
    case Builtin::kNumberConstructor:
      return ReduceNumberConstructor(node);
    case Builtin::kBigIntConstructor:
      return ReduceBigIntConstructor(node);
    case Builtin::kBigIntAsIntN:
    case Builtin::kBigIntAsUintN:
      return ReduceBigIntAsN(node, builtin);
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
    case Builtin::kGetContinuationPreservedEmbedderData:
      return ReduceGetContinuationPreservedEmbedderData(node);
    case Builtin::kSetContinuationPreservedEmbedderData:
      return ReduceSetContinuationPreservedEmbedderData(node);
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
    default:
      break;
  }

  if (shared.function_template_info(broker()).has_value()) {
    return ReduceCallApiFunction(node, shared);
  }

#if V8_ENABLE_WEBASSEMBLY
  if ((flags() & kInlineJSToWasmCalls) && shared.wasm_function_signature()) {
    return ReduceCallWasmFunction(node, shared);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  return NoChange();
}

TNode<Object> JSCallReducerAssembler::ReduceJSCallWithArrayLikeOrSpreadOfEmpty(
    std::unordered_set<Node*>* generated_calls_with_array_like_or_spread) {
  DCHECK_EQ(generated_calls_with_array_like_or_spread->count(node_ptr()), 0);
  JSCallWithArrayLikeOrSpreadNode n(node_ptr());
  CallParameters const& p = n.Parameters();
  TNode<Object> arguments_list = n.LastArgument();
  DCHECK_EQ(static_cast<Node*>(arguments_list)->opcode(),
            IrOpcode::kJSCreateEmptyLiteralArray);

  // Turn the JSCallWithArrayLike or JSCallWithSpread roughly into:
  //
  //      "arguments_list array is still empty?"
  //               |
  //               |
  //            Branch
  //           /      \
  //          /        \
  //      IfTrue      IfFalse
  //         |          |
  //         |          |
  //      JSCall    JSCallWithArrayLike/JSCallWithSpread
  //          \        /
  //           \      /
  //            Merge

  TNode<Number> length = TNode<Number>::UncheckedCast(
      LoadField(AccessBuilder::ForJSArrayLength(NO_ELEMENTS), arguments_list));
  return SelectIf<Object>(NumberEqual(length, ZeroConstant()))
      .Then([&]() {
        TNode<Object> call = CopyNode();
        static_cast<Node*>(call)->RemoveInput(n.LastArgumentIndex());
        NodeProperties::ChangeOp(
            call, javascript()->Call(p.arity() - 1, p.frequency(), p.feedback(),
                                     p.convert_mode(), p.speculation_mode(),
                                     p.feedback_relation()));
        return call;
      })
      .Else([&]() {
        TNode<Object> call = CopyNode();
        generated_calls_with_array_like_or_spread->insert(call);
        return call;
      })
      .ExpectFalse()
      .Value();
}

namespace {

// Check if the target is a class constructor.
// We need to check all cases where the target will be typed as Function
// to prevent later optimizations from using the CallFunction trampoline,
// skipping the instance type check.
bool TargetIsClassConstructor(Node* node, JSHeapBroker* broker) {
  Node* target = NodeProperties::GetValueInput(node, 0);
  OptionalSharedFunctionInfoRef shared;
  HeapObjectMatcher m(target);
  if (m.HasResolvedValue()) {
    ObjectRef target_ref = m.Ref(broker);
    if (target_ref.IsJSFunction()) {
      JSFunctionRef function = target_ref.AsJSFunction();
      shared = function.shared(broker);
    }
  } else if (target->opcode() == IrOpcode::kJSCreateClosure) {
    CreateClosureParameters const& ccp =
        JSCreateClosureNode{target}.Parameters();
    shared = ccp.shared_info();
  } else if (target->opcode() == IrOpcode::kCheckClosure) {
    FeedbackCellRef cell = MakeRef(broker, FeedbackCellOf(target->op()));
    shared = cell.shared_function_info(broker);
  }

  if (shared.has_value() && IsClassConstructor(shared->kind())) return true;

  return false;
}

}  // namespace

Reduction JSCallReducer::ReduceJSCallWithArrayLike(Node* node) {
  JSCallWithArrayLikeNode n(node);
  CallParameters const& p = n.Parameters();
  DCHECK_EQ(p.arity_without_implicit_args(), 1);  // The arraylike object.
  // Class constructors are callable, but [[Call]] will raise an exception.
  // See ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList ).
  if (TargetIsClassConstructor(node, broker())) {
    return NoChange();
  }

  base::Optional<Reduction> maybe_result =
      TryReduceJSCallMathMinMaxWithArrayLike(node);
  if (maybe_result.has_value()) {
    return maybe_result.value();
  }

  return ReduceCallOrConstructWithArrayLikeOrSpread(
      node, n.ArgumentCount(), n.LastArgumentIndex(), p.frequency(),
      p.feedback(), p.speculation_mode(), p.feedback_relation(), n.target(),
      n.effect(), n.control());
}

Reduction JSCallReducer::ReduceJSCallWithSpread(Node* node) {
  JSCallWithSpreadNode n(node);
  CallParameters const& p = n.Parameters();
  DCHECK_GE(p.arity_without_implicit_args(), 1);  // At least the spread.
  // Class constructors are callable, but [[Call]] will raise an exception.
  // See ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList ).
  if (TargetIsClassConstructor(node, broker())) {
    return NoChange();
  }
  return ReduceCallOrConstructWithArrayLikeOrSpread(
      node, n.ArgumentCount(), n.LastArgumentIndex(), p.frequency(),
      p.feedback(), p.speculation_mode(), p.feedback_relation(), n.target(),
      n.effect(), n.control());
}

Reduction JSCallReducer::ReduceJSConstruct(Node* node) {
  if (broker()->StackHasOverflowed()) return NoChange();

  JSConstructNode n(node);
  ConstructParameters const& p = n.Parameters();
  int arity = p.arity_without_implicit_args();
  Node* target = n.target();
  Node* new_target = n.new_target();
  Effect effect = n.effect();
  Control control = n.control();

  if (p.feedback().IsValid()) {
    ProcessedFeedback const& feedback =
        broker()->GetFeedbackForCall(p.feedback());
    if (feedback.IsInsufficient()) {
      return ReduceForInsufficientFeedback(
          node, DeoptimizeReason::kInsufficientTypeFeedbackForConstruct);
    }

    OptionalHeapObjectRef feedback_target = feedback.AsCall().target();
    if (feedback_target.has_value() && feedback_target->IsAllocationSite()) {
      // The feedback is an AllocationSite, which means we have called the
      // Array function and collected transition (and pretenuring) feedback
      // for the resulting arrays.  This has to be kept in sync with the
      // implementation in Ignition.

      Node* array_function = jsgraph()->ConstantNoHole(
          native_context().array_function(broker()), broker());

      // Check that the {target} is still the {array_function}.
      Node* check = graph()->NewNode(simplified()->ReferenceEqual(), target,
                                     array_function);
      effect = graph()->NewNode(
          simplified()->CheckIf(DeoptimizeReason::kWrongCallTarget), check,
          effect, control);

      // Turn the {node} into a {JSCreateArray} call.
      NodeProperties::ReplaceEffectInput(node, effect);
      static_assert(JSConstructNode::NewTargetIndex() == 1);
      node->ReplaceInput(n.NewTargetIndex(), array_function);
      node->RemoveInput(n.FeedbackVectorIndex());
      NodeProperties::ChangeOp(
          node, javascript()->CreateArray(arity,
                                          feedback_target->AsAllocationSite()));
      return Changed(node);
    } else if (feedback_target.has_value() &&
               !HeapObjectMatcher(new_target).HasResolvedValue() &&
               feedback_target->map(broker()).is_constructor()) {
      Node* new_target_feedback =
          jsgraph()->ConstantNoHole(*feedback_target, broker());

      // Check that the {new_target} is still the {new_target_feedback}.
      Node* check = graph()->NewNode(simplified()->ReferenceEqual(), new_target,
                                     new_target_feedback);
      effect = graph()->NewNode(
          simplified()->CheckIf(DeoptimizeReason::kWrongCallTarget), check,
          effect, control);

      // Specialize the JSConstruct node to the {new_target_feedback}.
      node->ReplaceInput(n.NewTargetIndex(), new_target_feedback);
      NodeProperties::ReplaceEffectInput(node, effect);
      if (target == new_target) {
        node->ReplaceInput(n.TargetIndex(), new_target_feedback);
      }

      // Try to further reduce the JSConstruct {node}.
      return Changed(node).FollowedBy(ReduceJSConstruct(node));
    }
  }

  // Try to specialize JSConstruct {node}s with constant {target}s.
  HeapObjectMatcher m(target);
  if (m.HasResolvedValue()) {
    HeapObjectRef target_ref = m.Ref(broker());

    // Raise a TypeError if the {target} is not a constructor.
    if (!target_ref.map(broker()).is_constructor()) {
      NodeProperties::ReplaceValueInputs(node, target);
      NodeProperties::ChangeOp(node,
                               javascript()->CallRuntime(
                                   Runtime::kThrowConstructedNonConstructable));
      return Changed(node);
    }

    if (target_ref.IsJSFunction()) {
      JSFunctionRef function = target_ref.AsJSFunction();

      // Do not reduce constructors with break points.
      // If this state changes during background compilation, the compilation
      // job will be aborted from the main thread (see
      // Debug::PrepareFunctionForDebugExecution()).
      SharedFunctionInfoRef sfi = function.shared(broker());
      if (sfi.HasBreakInfo(broker())) return NoChange();

      // Don't inline cross native context.
      if (!function.native_context(broker()).equals(native_context())) {
        return NoChange();
      }

      // Check for known builtin functions.
      Builtin builtin =
          sfi.HasBuiltinId() ? sfi.builtin_id() : Builtin::kNoBuiltinId;
      switch (builtin) {
        case Builtin::kArrayConstructor: {
          // TODO(bmeurer): Deal with Array subclasses here.
          // Turn the {node} into a {JSCreateArray} call.
          static_assert(JSConstructNode::NewTargetIndex() == 1);
          node->ReplaceInput(n.NewTargetIndex(), new_target);
          node->RemoveInput(n.FeedbackVectorIndex());
          NodeProperties::ChangeOp(
              node, javascript()->CreateArray(arity, base::nullopt));
          return Changed(node);
        }
        case Builtin::kObjectConstructor: {
          // If no value is passed, we can immediately lower to a simple
          // JSCreate and don't need to do any massaging of the {node}.
          if (arity == 0) {
            node->RemoveInput(n.FeedbackVectorIndex());
            NodeProperties::ChangeOp(node, javascript()->Create());
            return Changed(node);
          }

          // If {target} is not the same as {new_target} (i.e. the Object
          // constructor), {value} will be ignored and therefore we can lower
          // to {JSCreate}. See https://tc39.es/ecma262/#sec-object-value.
          HeapObjectMatcher mnew_target(new_target);
          if (mnew_target.HasResolvedValue() &&
              !mnew_target.Ref(broker()).equals(function)) {
            // Drop the value inputs.
            node->RemoveInput(n.FeedbackVectorIndex());
            for (int i = n.ArgumentCount() - 1; i >= 0; i--) {
              node->RemoveInput(n.ArgumentIndex(i));
            }
            NodeProperties::ChangeOp(node, javascript()->Create());
            return Changed(node);
          }
          break;
        }
        case Builtin::kPromiseConstructor:
          return ReducePromiseConstructor(node);
        case Builtin::kTypedArrayConstructor:
          return ReduceTypedArrayConstructor(node, function.shared(broker()));
        default:
          break;
      }
    } else if (target_ref.IsJSBoundFunction()) {
      JSBoundFunctionRef function = target_ref.AsJSBoundFunction();
      JSReceiverRef bound_target_function =
          function.bound_target_function(broker());
      FixedArrayRef bound_arguments = function.bound_arguments(broker());
      const int bound_arguments_length = bound_arguments.length();

      // TODO(jgruber): Inline this block below once TryGet is guaranteed to
      // succeed.
      static constexpr int kInlineSize = 16;  // Arbitrary.
      base::SmallVector<Node*, kInlineSize> args;
      for (int i = 0; i < bound_arguments_length; ++i) {
        OptionalObjectRef maybe_arg = bound_arguments.TryGet(broker(), i);
        if (!maybe_arg.has_value()) {
          TRACE_BROKER_MISSING(broker(), "bound argument");
          return NoChange();
        }
        args.emplace_back(
            jsgraph()->ConstantNoHole(maybe_arg.value(), broker()));
      }

      // Patch {node} to use [[BoundTargetFunction]].
      node->ReplaceInput(n.TargetIndex(), jsgraph()->ConstantNoHole(
                                              bound_target_function, broker()));

      // Patch {node} to use [[BoundTargetFunction]]
      // as new.target if {new_target} equals {target}.
      if (target == new_target) {
        node->ReplaceInput(
            n.NewTargetIndex(),
            jsgraph()->ConstantNoHole(bound_target_function, broker()));
      } else {
        node->ReplaceInput(
            n.NewTargetIndex(),
            graph()->NewNode(
                common()->Select(MachineRepresentation::kTagged),
                graph()->NewNode(simplified()->ReferenceEqual(), target,
                                 new_target),
                jsgraph()->ConstantNoHole(bound_target_function, broker()),
                new_target));
      }

      // Insert the [[BoundArguments]] for {node}.
      for (int i = 0; i < bound_arguments_length; ++i) {
        node->InsertInput(graph()->zone(), n.ArgumentIndex(i), args[i]);
        arity++;
      }

      // Update the JSConstruct operator on {node}.
      NodeProperties::ChangeOp(
          node, javascript()->Construct(JSConstructNode::ArityForArgc(arity),
                                        p.frequency(), FeedbackSource()));

      // Try to further reduce the JSConstruct {node}.
      return Changed(node).FollowedBy(ReduceJSConstruct(node));
    }

    // TODO(bmeurer): Also support optimizing proxies here.
  }

  // If {target} is the result of a JSCreateBoundFunction operation,
  // we can just fold the construction and construct the bound target
  // function directly instead.
  if (target->opcode() == IrOpcode::kJSCreateBoundFunction) {
    Node* bound_target_function = NodeProperties::GetValueInput(target, 0);
    int const bound_arguments_length =
        static_cast<int>(CreateBoundFunctionParametersOf(target->op()).arity());

    // Patch the {node} to use [[BoundTargetFunction]].
    node->ReplaceInput(n.TargetIndex(), bound_target_function);

    // Patch {node} to use [[BoundTargetFunction]]
    // as new.target if {new_target} equals {target}.
    if (target == new_target) {
      node->ReplaceInput(n.NewTargetIndex(), bound_target_function);
    } else {
      node->ReplaceInput(
          n.NewTargetIndex(),
          graph()->NewNode(common()->Select(MachineRepresentation::kTagged),
                           graph()->NewNode(simplified()->ReferenceEqual(),
                                            target, new_target),
                           bound_target_function, new_target));
    }

    // Insert the [[BoundArguments]] for {node}.
    for (int i = 0; i < bound_arguments_length; ++i) {
      Node* value = NodeProperties::GetValueInput(target, 2 + i);
      node->InsertInput(graph()->zone(), n.ArgumentIndex(i), value);
      arity++;
    }

    // Update the JSConstruct operator on {node}.
    NodeProperties::ChangeOp(
        node, javascript()->Construct(JSConstructNode::ArityForArgc(arity),
                                      p.frequency(), FeedbackSource()));

    // Try to further reduce the JSConstruct {node}.
    return Changed(node).FollowedBy(ReduceJSConstruct(node));
  }

  return NoChange();
}

// ES #sec-string.prototype.indexof
// ES #sec-string.prototype.includes
Reduction JSCallReducer::ReduceStringPrototypeIndexOfIncludes(
    Node* node, StringIndexOfIncludesVariant variant) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Effect effect = n.effect();
  Control control = n.control();
  if (n.ArgumentCount() > 0) {
    Node* receiver = n.receiver();
    Node* new_receiver = effect = graph()->NewNode(
        simplified()->CheckString(p.feedback()), receiver, effect, control);

    Node* search_string = n.Argument(0);
    Node* new_search_string = effect =
        graph()->NewNode(simplified()->CheckString(p.feedback()), search_string,
                         effect, control);

    Node* new_position = jsgraph()->ZeroConstant();
    if (n.ArgumentCount() > 1) {
      Node* position = n.Argument(1);
      new_position = effect = graph()->NewNode(
          simplified()->CheckSmi(p.feedback()), position, effect, control);

      Node* receiver_length =
          graph()->NewNode(simplified()->StringLength(), new_receiver);
      new_position = graph()->NewNode(
          simplified()->NumberMin(),
          graph()->NewNode(simplified()->NumberMax(), new_position,
                           jsgraph()->ZeroConstant()),
          receiver_length);
    }

    NodeProperties::ReplaceEffectInput(node, effect);
    RelaxEffectsAndControls(node);
    node->ReplaceInput(0, new_receiver);
    node->ReplaceInput(1, new_search_string);
    node->ReplaceInput(2, new_position);
    node->TrimInputCount(3);
    NodeProperties::ChangeOp(node, simplified()->StringIndexOf());

    if (variant == StringIndexOfIncludesVariant::kIndexOf) {
      return Changed(node);
    } else {
      DCHECK(variant == StringIndexOfIncludesVariant::kIncludes);
      Node* result =
          graph()->NewNode(simplified()->BooleanNot(),
                           graph()->NewNode(simplified()->NumberEqual(), node,
                                            jsgraph()->SmiConstant(-1)));
      return Replace(result);
    }
  }
  return NoChange();
}

// ES #sec-string.prototype.substring
Reduction JSCallReducer::ReduceStringPrototypeSubstring(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (n.ArgumentCount() < 1) return NoChange();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  JSCallReducerAssembler a(this, node);
  Node* subgraph = a.ReduceStringPrototypeSubstring();
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES #sec-string.prototype.slice
Reduction JSCallReducer::ReduceStringPrototypeSlice(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (n.ArgumentCount() < 1) return NoChange();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  JSCallReducerAssembler a(this, node);
  Node* subgraph = a.ReduceStringPrototypeSlice();
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES #sec-string.prototype.substr
Reduction JSCallReducer::ReduceStringPrototypeSubstr(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (n.ArgumentCount() < 1) return NoChange();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* receiver = n.receiver();
  Node* start = n.Argument(0);
  Node* end = n.ArgumentOrUndefined(1, jsgraph());

  receiver = effect = graph()->NewNode(simplified()->CheckString(p.feedback()),
                                       receiver, effect, control);

  start = effect = graph()->NewNode(simplified()->CheckSmi(p.feedback()), start,
                                    effect, control);

  Node* length = graph()->NewNode(simplified()->StringLength(), receiver);

  // Replace {end} argument with {length} if it is undefined.
  {
    Node* check = graph()->NewNode(simplified()->ReferenceEqual(), end,
                                   jsgraph()->UndefinedConstant());
    Node* branch =
        graph()->NewNode(common()->Branch(BranchHint::kFalse), check, control);

    Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
    Node* etrue = effect;
    Node* vtrue = length;

    Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
    Node* efalse = effect;
    Node* vfalse = efalse = graph()->NewNode(
        simplified()->CheckSmi(p.feedback()), end, efalse, if_false);

    control = graph()->NewNode(common()->Merge(2), if_true, if_false);
    effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
    end = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                           vtrue, vfalse, control);
  }

  Node* initStart = graph()->NewNode(
      common()->Select(MachineRepresentation::kTagged, BranchHint::kFalse),
      graph()->NewNode(simplified()->NumberLessThan(), start,
                       jsgraph()->ZeroConstant()),
      graph()->NewNode(
          simplified()->NumberMax(),
          graph()->NewNode(simplified()->NumberAdd(), length, start),
          jsgraph()->ZeroConstant()),
      start);
  // The select above guarantees that initStart is non-negative, but
  // our typer can't figure that out yet.
  initStart = effect = graph()->NewNode(
      common()->TypeGuard(Type::UnsignedSmall()), initStart, effect, control);

  Node* resultLength = graph()->NewNode(
      simplified()->NumberMin(),
      graph()->NewNode(simplified()->NumberMax(), end,
                       jsgraph()->ZeroConstant()),
      graph()->NewNode(simplified()->NumberSubtract(), length, initStart));

  // The the select below uses {resultLength} only if {resultLength > 0},
  // but our typer can't figure that out yet.
  Node* to = effect = graph()->NewNode(
      common()->TypeGuard(Type::UnsignedSmall()),
      graph()->NewNode(simplified()->NumberAdd(), initStart, resultLength),
      effect, control);

  Node* result_string = nullptr;
  // Return empty string if {from} is smaller than {to}.
  {
    Node* check = graph()->NewNode(simplified()->NumberLessThan(),
                                   jsgraph()->ZeroConstant(), resultLength);

    Node* branch =
        graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

    Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
    Node* etrue = effect;
    Node* vtrue = etrue =
        graph()->NewNode(simplified()->StringSubstring(), receiver, initStart,
                         to, etrue, if_true);

    Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
    Node* efalse = effect;
    Node* vfalse = jsgraph()->EmptyStringConstant();

    control = graph()->NewNode(common()->Merge(2), if_true, if_false);
    effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
    result_string =
        graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                         vtrue, vfalse, control);
  }

  ReplaceWithValue(node, result_string, effect, control);
  return Replace(result_string);
}

Reduction JSCallReducer::ReduceJSConstructWithArrayLike(Node* node) {
  JSConstructWithArrayLikeNode n(node);
  ConstructParameters const& p = n.Parameters();
  const int arraylike_index = n.LastArgumentIndex();
  DCHECK_EQ(n.ArgumentCount(), 1);  // The arraylike object.
  return ReduceCallOrConstructWithArrayLikeOrSpread(
      node, n.ArgumentCount(), arraylike_index, p.frequency(), p.feedback(),
      SpeculationMode::kDisallowSpeculation, CallFeedbackRelation::kTarget,
      n.target(), n.effect(), n.control());
}

Reduction JSCallReducer::ReduceJSConstructWithSpread(Node* node) {
  JSConstructWithSpreadNode n(node);
  ConstructParameters const& p = n.Parameters();
  const int spread_index = n.LastArgumentIndex();
  DCHECK_GE(n.ArgumentCount(), 1);  // At least the spread.
  return ReduceCallOrConstructWithArrayLikeOrSpread(
      node, n.ArgumentCount(), spread_index, p.frequency(), p.feedback(),
      SpeculationMode::kDisallowSpeculation, CallFeedbackRelation::kTarget,
      n.target(), n.effect(), n.control());
}

Reduction JSCallReducer::ReduceJSConstructForwardAllArgs(Node* node) {
  JSConstructForwardAllArgsNode n(node);
  DCHECK_EQ(n.ArgumentCount(), 0);

  // If this frame is not being inlined, JSConstructForwardAllArgs will be
  // lowered later in JSGenericLowering to a builtin call.
  FrameState frame_state = n.frame_state();
  if (frame_state.outer_frame_state()->opcode() != IrOpcode::kFrameState) {
    return NoChange();
  }

  // Hook up the arguments directly when forwarding arguments of inlined frames.
  FrameState outer_state{frame_state.outer_frame_state()};
  FrameStateInfo outer_info = outer_state.frame_state_info();
  if (outer_info.type() == FrameStateType::kInlinedExtraArguments) {
    frame_state = outer_state;
  }

  int argc = 0;
  StateValuesAccess parameters_access(frame_state.parameters());
  for (auto it = parameters_access.begin_without_receiver(); !it.done(); ++it) {
    DCHECK_NOT_NULL(it.node());
    node->InsertInput(graph()->zone(),
                      JSCallOrConstructNode::ArgumentIndex(argc++), it.node());
  }

  ConstructParameters const& p = n.Parameters();
  NodeProperties::ChangeOp(
      node, javascript()->Construct(JSConstructNode::ArityForArgc(argc),
                                    p.frequency(), p.feedback()));
  CheckIfConstructor(node);
  return Changed(node).FollowedBy(ReduceJSConstruct(node));
}

Reduction JSCallReducer::ReduceReturnReceiver(Node* node) {
  JSCallNode n(node);
  Node* receiver = n.receiver();
  ReplaceWithValue(node, receiver);
  return Replace(receiver);
}

Reduction JSCallReducer::ReduceForInsufficientFeedback(
    Node* node, DeoptimizeReason reason) {
  DCHECK(node->opcode() == IrOpcode::kJSCall ||
         node->opcode() == IrOpcode::kJSConstruct);
  if (!(flags() & kBailoutOnUninitialized)) return NoChange();

  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* frame_state =
      NodeProperties::FindFrameStateBefore(node, jsgraph()->Dead());
  Node* deoptimize =
      graph()->NewNode(common()->Deoptimize(reason, FeedbackSource()),
                       frame_state, effect, control);
  MergeControlToEnd(graph(), common(), deoptimize);
  node->TrimInputCount(0);
  NodeProperties::ChangeOp(node, common()->Dead());
  return Changed(node);
}

Node* JSCallReducer::LoadReceiverElementsKind(Node* receiver, Effect* effect,
                                              Control control) {
  Node* effect_node = *effect;
  Node* receiver_map = effect_node =
      graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                       receiver, effect_node, control);
  Node* receiver_bit_field2 = effect_node = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForMapBitField2()), receiver_map,
      effect_node, control);
  Node* receiver_elements_kind = graph()->NewNode(
      simplified()->NumberShiftRightLogical(),
      graph()->NewNode(
          simplified()->NumberBitwiseAnd(), receiver_bit_field2,
          jsgraph()->ConstantNoHole(Map::Bits2::ElementsKindBits::kMask)),
      jsgraph()->ConstantNoHole(Map::Bits2::ElementsKindBits::kShift));
  *effect = effect_node;
  return receiver_elements_kind;
}

void JSCallReducer::CheckIfElementsKind(Node* receiver_elements_kind,
                                        ElementsKind kind, Node* control,
                                        Node** if_true, Node** if_false) {
  Node* is_packed_kind =
      graph()->NewNode(simplified()->NumberEqual(), receiver_elements_kind,
                       jsgraph()->ConstantNoHole(GetPackedElementsKind(kind)));
  Node* packed_branch =
      graph()->NewNode(common()->Branch(), is_packed_kind, control);
  Node* if_packed = graph()->NewNode(common()->IfTrue(), packed_branch);

  if (IsHoleyElementsKind(kind)) {
    Node* if_not_packed = graph()->NewNode(common()->IfFalse(), packed_branch);
    Node* is_holey_kind =
        graph()->NewNode(simplified()->NumberEqual(), receiver_elements_kind,
                         jsgraph()->ConstantNoHole(GetHoleyElementsKind(kind)));
    Node* holey_branch =
        graph()->NewNode(common()->Branch(), is_holey_kind, if_not_packed);
    Node* if_holey = graph()->NewNode(common()->IfTrue(), holey_branch);

    Node* if_not_packed_not_holey =
        graph()->NewNode(common()->IfFalse(), holey_branch);

    *if_true = graph()->NewNode(common()->Merge(2), if_packed, if_holey);
    *if_false = if_not_packed_not_holey;
  } else {
    *if_true = if_packed;
    *if_false = graph()->NewNode(common()->IfFalse(), packed_branch);
  }
}

// ES6 section 23.1.3.1 Array.prototype.at ( )
Reduction JSCallReducer::ReduceArrayPrototypeAt(Node* node) {
  if (!v8_flags.turbo_inline_array_builtins) return NoChange();

  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Node* receiver = n.receiver();
  Effect effect = n.effect();
  Control control = n.control();

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps()) return NoChange();

  // Collecting maps, and checking if a fallback builtin call will be required
  // (it is required if at least one map doesn't support fast array iteration).
  ZoneVector<MapRef> maps(broker()->zone());
  bool needs_fallback_builtin_call = false;
  for (MapRef map : inference.GetMaps()) {
    if (map.supports_fast_array_iteration(broker())) {
      maps.push_back(map);
    } else {
      needs_fallback_builtin_call = true;
    }
  }

  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  if (maps.empty()) {
    // No map in the feedback supports fast iteration. Keeping the builtin call.
    return NoChange();
  }

  if (!dependencies()->DependOnNoElementsProtector()) {
    return NoChange();
  }

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(effect, control);

  TNode<Object> subgraph =
      a.ReduceArrayPrototypeAt(maps, needs_fallback_builtin_call);
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES6 section 22.1.3.18 Array.prototype.push ( )
Reduction JSCallReducer::ReduceArrayPrototypePush(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Node* receiver = n.receiver();
  Effect effect = n.effect();
  Control control = n.control();

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps()) return NoChange();
  ZoneRefSet<Map> const& receiver_maps = inference.GetMaps();

  std::vector<ElementsKind> kinds;
  if (!CanInlineArrayResizingBuiltin(broker(), receiver_maps, &kinds, true)) {
    return inference.NoChange();
  }
  if (!dependencies()->DependOnNoElementsProtector()) {
    return inference.NoChange();
  }

  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  IteratingArrayBuiltinReducerAssembler a(this, node);
  a.InitializeEffectControl(effect, control);

  TNode<Object> subgraph = a.ReduceArrayPrototypePush(&inference);
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES6 section 22.1.3.17 Array.prototype.pop ( )
Reduction JSCallReducer::ReduceArrayPrototypePop(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* receiver = n.receiver();

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps()) return NoChange();
  ZoneRefSet<Map> const& receiver_maps = inference.GetMaps();

  std::vector<ElementsKind> kinds;
  if (!CanInlineArrayResizingBuiltin(broker(), receiver_maps, &kinds)) {
    return inference.NoChange();
  }
  if (!dependencies()->DependOnNoElementsProtector()) {
    return inference.NoChange();
  }
  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  std::vector<Node*> controls_to_merge;
  std::vector<Node*> effects_to_merge;
  std::vector<Node*> values_to_merge;
  Node* value = jsgraph()->UndefinedConstant();

  Node* receiver_elements_kind =
      LoadReceiverElementsKind(receiver, &effect, control);
  Node* next_control = control;
  Node* next_effect = effect;
  for (size_t i = 0; i < kinds.size(); i++) {
    ElementsKind kind = kinds[i];
    control = next_control;
    effect = next_effect;
    // We do not need branch for the last elements kind.
    if (i != kinds.size() - 1) {
      Node* control_node = control;
      CheckIfElementsKind(receiver_elements_kind, kind, control_node,
                          &control_node, &next_control);
      control = control_node;
    }

    // Load the "length" property of the {receiver}.
    Node* length = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSArrayLength(kind)),
        receiver, effect, control);

    // Check if the {receiver} has any elements.
    Node* check = graph()->NewNode(simplified()->NumberEqual(), length,
                                   jsgraph()->ZeroConstant());
    Node* branch =
        graph()->NewNode(common()->Branch(BranchHint::kFalse), check, control);

    Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
    Node* etrue = effect;
    Node* vtrue = jsgraph()->UndefinedConstant();

    Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
    Node* efalse = effect;
    Node* vfalse;
    {
      // TODO(turbofan): We should trim the backing store if the capacity is too
      // big, as implemented in elements.cc:ElementsAccessorBase::SetLengthImpl.

      // Load the elements backing store from the {receiver}.
      Node* elements = efalse = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForJSObjectElements()),
          receiver, efalse, if_false);

      // Ensure that we aren't popping from a copy-on-write backing store.
      if (IsSmiOrObjectElementsKind(kind)) {
        elements = efalse =
            graph()->NewNode(simplified()->EnsureWritableFastElements(),
                             receiver, elements, efalse, if_false);
      }

      // Compute the new {length}.
      Node* new_length = graph()->NewNode(simplified()->NumberSubtract(),
                                          length, jsgraph()->OneConstant());

      if (v8_flags.turbo_typer_hardening) {
        new_length = efalse = graph()->NewNode(
            simplified()->CheckBounds(p.feedback(),
                                      CheckBoundsFlag::kAbortOnOutOfBounds),
            new_length, length, efalse, if_false);
      }

      // Store the new {length} to the {receiver}.
      efalse = graph()->NewNode(
          simplified()->StoreField(AccessBuilder::ForJSArrayLength(kind)),
          receiver, new_length, efalse, if_false);

      // Load the last entry from the {elements}.
      vfalse = efalse = graph()->NewNode(
          simplified()->LoadElement(AccessBuilder::ForFixedArrayElement(kind)),
          elements, new_length, efalse, if_false);

      // Store a hole to the element we just removed from the {receiver}.
      efalse = graph()->NewNode(
          simplified()->StoreElement(
              AccessBuilder::ForFixedArrayElement(GetHoleyElementsKind(kind))),
          elements, new_length, jsgraph()->TheHoleConstant(), efalse, if_false);
    }

    control = graph()->NewNode(common()->Merge(2), if_true, if_false);
    effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
    value = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                             vtrue, vfalse, control);

    // Convert the hole to undefined. Do this last, so that we can optimize
    // conversion operator via some smart strength reduction in many cases.
    if (IsHoleyElementsKind(kind)) {
      value =
          graph()->NewNode(simplified()->ConvertTaggedHoleToUndefined(), value);
    }

    controls_to_merge.push_back(control);
    effects_to_merge.push_back(effect);
    values_to_merge.push_back(value);
  }

  if (controls_to_merge.size() > 1) {
    int const count = static_cast<int>(controls_to_merge.size());

    control = graph()->NewNode(common()->Merge(count), count,
                               &controls_to_merge.front());
    effects_to_merge.push_back(control);
    effect = graph()->NewNode(common()->EffectPhi(count), count + 1,
                              &effects_to_merge.front());
    values_to_merge.push_back(control);
    value =
        graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, count),
                         count + 1, &values_to_merge.front());
  }

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES6 section 22.1.3.22 Array.prototype.shift ( )
// Currently disabled. See https://crbug.com/v8/14409
Reduction JSCallReducer::ReduceArrayPrototypeShift(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Node* target = n.target();
  Node* receiver = n.receiver();
  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Effect effect = n.effect();
  Control control = n.control();

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps()) return NoChange();
  ZoneRefSet<Map> const& receiver_maps = inference.GetMaps();

  std::vector<ElementsKind> kinds;
  if (!CanInlineArrayResizingBuiltin(broker(), receiver_maps, &kinds)) {
    return inference.NoChange();
  }
  if (!dependencies()->DependOnNoElementsProtector()) {
    return inference.NoChange();
  }
  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  std::vector<Node*> controls_to_merge;
  std::vector<Node*> effects_to_merge;
  std::vector<Node*> values_to_merge;
  Node* value = jsgraph()->UndefinedConstant();

  Node* receiver_elements_kind =
      LoadReceiverElementsKind(receiver, &effect, control);
  Node* next_control = control;
  Node* next_effect = effect;
  for (size_t i = 0; i < kinds.size(); i++) {
    ElementsKind kind = kinds[i];
    control = next_control;
    effect = next_effect;
    // We do not need branch for the last elements kind.
    if (i != kinds.size() - 1) {
      Node* control_node = control;
      CheckIfElementsKind(receiver_elements_kind, kind, control_node,
                          &control_node, &next_control);
      control = control_node;
    }

    // Load length of the {receiver}.
    Node* length = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSArrayLength(kind)),
        receiver, effect, control);

    // Return undefined if {receiver} has no elements.
    Node* check0 = graph()->NewNode(simplified()->NumberEqual(), length,
                                    jsgraph()->ZeroConstant());
    Node* branch0 =
        graph()->NewNode(common()->Branch(BranchHint::kFalse), check0, control);

    Node* if_true0 = graph()->NewNode(common()->IfTrue(), branch0);
    Node* etrue0 = effect;
    Node* vtrue0 = jsgraph()->UndefinedConstant();

    Node* if_false0 = graph()->NewNode(common()->IfFalse(), branch0);
    Node* efalse0 = effect;
    Node* vfalse0;
    {
      // Check if we should take the fast-path.
      Node* check1 = graph()->NewNode(
          simplified()->NumberLessThanOrEqual(), length,
          jsgraph()->ConstantNoHole(JSArray::kMaxCopyElements));
      Node* branch1 = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                       check1, if_false0);

      Node* if_true1 = graph()->NewNode(common()->IfTrue(), branch1);
      Node* etrue1 = efalse0;
      Node* vtrue1;
      {
        Node* elements = etrue1 = graph()->NewNode(
            simplified()->LoadField(AccessBuilder::ForJSObjectElements()),
            receiver, etrue1, if_true1);

        // Load the first element here, which we return below.
        vtrue1 = etrue1 = graph()->NewNode(
            simplified()->LoadElement(
                AccessBuilder::ForFixedArrayElement(kind)),
            elements, jsgraph()->ZeroConstant(), etrue1, if_true1);

        // Ensure that we aren't shifting a copy-on-write backing store.
        if (IsSmiOrObjectElementsKind(kind)) {
          elements = etrue1 =
              graph()->NewNode(simplified()->EnsureWritableFastElements(),
                               receiver, elements, etrue1, if_true1);
        }

        // Shift the remaining {elements} by one towards the start.
        Node* loop = graph()->NewNode(common()->Loop(2), if_true1, if_true1);
        Node* eloop =
            graph()->NewNode(common()->EffectPhi(2), etrue1, etrue1, loop);
        Node* terminate = graph()->NewNode(common()->Terminate(), eloop, loop);
        MergeControlToEnd(graph(), common(), terminate);

        Node* index = graph()->NewNode(
            common()->Phi(MachineRepresentation::kTagged, 2),
            jsgraph()->OneConstant(),
            jsgraph()->ConstantNoHole(JSArray::kMaxCopyElements - 1), loop);

        {
          Node* check2 =
              graph()->NewNode(simplified()->NumberLessThan(), index, length);
          Node* branch2 = graph()->NewNode(common()->Branch(), check2, loop);

          if_true1 = graph()->NewNode(common()->IfFalse(), branch2);
          etrue1 = eloop;

          Node* control2 = graph()->NewNode(common()->IfTrue(), branch2);
          Node* effect2 = etrue1;

          ElementAccess const access =
              AccessBuilder::ForFixedArrayElement(kind);

          // When disable v8_flags.turbo_loop_variable, typer cannot infer index
          // is in [1, kMaxCopyElements-1], and will break in representing
          // kRepFloat64 (Range(1, inf)) to kRepWord64 when converting
          // input for kLoadElement. So we need to add type guard here.
          // And we need to use index when using NumberLessThan to check
          // terminate and updating index, otherwise which will break inducing
          // variables in LoopVariableOptimizer.
          static_assert(JSArray::kMaxCopyElements < kSmiMaxValue);
          Node* index_retyped = effect2 =
              graph()->NewNode(common()->TypeGuard(Type::UnsignedSmall()),
                               index, effect2, control2);

          Node* value2 = effect2 =
              graph()->NewNode(simplified()->LoadElement(access), elements,
                               index_retyped, effect2, control2);
          effect2 = graph()->NewNode(
              simplified()->StoreElement(access), elements,
              graph()->NewNode(simplified()->NumberSubtract(), index_retyped,
                               jsgraph()->OneConstant()),
              value2, effect2, control2);

          loop->ReplaceInput(1, control2);
          eloop->ReplaceInput(1, effect2);
          index->ReplaceInput(1,
                              graph()->NewNode(simplified()->NumberAdd(), index,
                                               jsgraph()->OneConstant()));
        }

        // Compute the new {length}.
        Node* new_length = graph()->NewNode(simplified()->NumberSubtract(),
                                            length, jsgraph()->OneConstant());

        if (v8_flags.turbo_typer_hardening) {
          new_length = etrue1 = graph()->NewNode(
              simplified()->CheckBounds(p.feedback(),
                                        CheckBoundsFlag::kAbortOnOutOfBounds),
              new_length, length, etrue1, if_true1);
        }

        // Store the new {length} to the {receiver}.
        etrue1 = graph()->NewNode(
            simplified()->StoreField(AccessBuilder::ForJSArrayLength(kind)),
            receiver, new_length, etrue1, if_true1);

        // Store a hole to the element we just removed from the {receiver}.
        etrue1 = graph()->NewNode(
            simplified()->StoreElement(AccessBuilder::ForFixedArrayElement(
                GetHoleyElementsKind(kind))),
            elements, new_length, jsgraph()->TheHoleConstant(), etrue1,
            if_true1);
      }

      Node* if_false1 = graph()->NewNode(common()->IfFalse(), branch1);
      Node* efalse1 = efalse0;
      Node* vfalse1;
      {
        // Call the generic C++ implementation.
        const Builtin builtin = Builtin::kArrayShift;
        auto call_descriptor = Linkage::GetCEntryStubCallDescriptor(
            graph()->zone(), 1, BuiltinArguments::kNumExtraArgsWithReceiver,
            Builtins::name(builtin), node->op()->properties(),
            CallDescriptor::kNeedsFrameState);
        Node* stub_code =
            jsgraph()->CEntryStubConstant(1, ArgvMode::kStack, true);
        Address builtin_entry = Builtins::CppEntryOf(builtin);
        Node* entry = jsgraph()->ExternalConstant(
            ExternalReference::Create(builtin_entry));
        Node* argc = jsgraph()->ConstantNoHole(
            BuiltinArguments::kNumExtraArgsWithReceiver);
        if_false1 = efalse1 = vfalse1 =
            graph()->NewNode(common()->Call(call_descriptor), stub_code,
                             receiver, jsgraph()->PaddingConstant(), argc,
                             target, jsgraph()->UndefinedConstant(), entry,
                             argc, context, frame_state, efalse1, if_false1);
      }

      if_false0 = graph()->NewNode(common()->Merge(2), if_true1, if_false1);
      efalse0 =
          graph()->NewNode(common()->EffectPhi(2), etrue1, efalse1, if_false0);
      vfalse0 =
          graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                           vtrue1, vfalse1, if_false0);
    }

    control = graph()->NewNode(common()->Merge(2), if_true0, if_false0);
    effect = graph()->NewNode(common()->EffectPhi(2), etrue0, efalse0, control);
    value = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                             vtrue0, vfalse0, control);

    // Convert the hole to undefined. Do this last, so that we can optimize
    // conversion operator via some smart strength reduction in many cases.
    if (IsHoleyElementsKind(kind)) {
      value =
          graph()->NewNode(simplified()->ConvertTaggedHoleToUndefined(), value);
    }

    controls_to_merge.push_back(control);
    effects_to_merge.push_back(effect);
    values_to_merge.push_back(value);
  }

  if (controls_to_merge.size() > 1) {
    int const count = static_cast<int>(controls_to_merge.size());

    control = graph()->NewNode(common()->Merge(count), count,
                               &controls_to_merge.front());
    effects_to_merge.push_back(control);
    effect = graph()->NewNode(common()->EffectPhi(count), count + 1,
                              &effects_to_merge.front());
    values_to_merge.push_back(control);
    value =
        graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, count),
                         count + 1, &values_to_merge.front());
  }

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES6 section 22.1.3.23 Array.prototype.slice ( )
Reduction JSCallReducer::ReduceArrayPrototypeSlice(Node* node) {
  if (!v8_flags.turbo_inline_array_builtins) return NoChange();
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Node* receiver = n.receiver();
  Node* start = n.ArgumentOr(0, jsgraph()->ZeroConstant());
  Node* end = n.ArgumentOrUndefined(1, jsgraph());
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();

  // Optimize for the case where we simply clone the {receiver}, i.e. when the
  // {start} is zero and the {end} is undefined (meaning it will be set to
  // {receiver}s "length" property). This logic should be in sync with
  // ReduceArrayPrototypeSlice (to a reasonable degree). This is because
  // CloneFastJSArray produces arrays which are potentially COW. If there's a
  // discrepancy, TF generates code which produces a COW array and then expects
  // it to be non-COW (or the other way around) -> immediate deopt.
  if (!NumberMatcher(start).Is(0) ||
      !HeapObjectMatcher(end).Is(factory()->undefined_value())) {
    return NoChange();
  }

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps()) return NoChange();
  ZoneRefSet<Map> const& receiver_maps = inference.GetMaps();

  // Check that the maps are of JSArray (and more).
  // TODO(turbofan): Consider adding special case for the common pattern
  // `slice.call(arguments)`, for example jQuery makes heavy use of that.
  bool can_be_holey = false;
  for (MapRef receiver_map : receiver_maps) {
    if (!receiver_map.supports_fast_array_iteration(broker())) {
      return inference.NoChange();
    }
    if (IsHoleyElementsKind(receiver_map.elements_kind())) {
      can_be_holey = true;
    }
  }

  if (!dependencies()->DependOnArraySpeciesProtector()) {
    return inference.NoChange();
  }
  if (can_be_holey && !dependencies()->DependOnNoElementsProtector()) {
    return inference.NoChange();
  }
  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  // TODO(turbofan): We can do even better here, either adding a CloneArray
  // simplified operator, whose output type indicates that it's an Array,
  // saving subsequent checks, or yet better, by introducing new operators
  // CopySmiOrObjectElements / CopyDoubleElements and inlining the JSArray
  // allocation in here. That way we'd even get escape analysis and scalar
  // replacement to help in some cases.
  Callable callable =
      Builtins::CallableFor(isolate(), Builtin::kCloneFastJSArray);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), CallDescriptor::kNoFlags,
      Operator::kNoThrow | Operator::kNoDeopt);

  // Calls to Builtin::kCloneFastJSArray produce COW arrays
  // if the original array is COW
  Node* clone = effect =
      graph()->NewNode(common()->Call(call_descriptor),
                       jsgraph()->HeapConstantNoHole(callable.code()), receiver,
                       context, effect, control);

  ReplaceWithValue(node, clone, effect, control);
  return Replace(clone);
}

// ES6 section 22.1.2.2 Array.isArray ( arg )
Reduction JSCallReducer::ReduceArrayIsArray(Node* node) {
  // We certainly know that undefined is not an array.
  JSCallNode n(node);
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->FalseConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Node* object = n.Argument(0);
  node->ReplaceInput(0, object);
  node->ReplaceInput(1, context);
  node->ReplaceInput(2, frame_state);
  node->ReplaceInput(3, effect);
  node->ReplaceInput(4, control);
  node->TrimInputCount(5);
  NodeProperties::ChangeOp(node, javascript()->ObjectIsArray());
  return Changed(node);
}

Reduction JSCallReducer::ReduceArrayIterator(Node* node,
                                             ArrayIteratorKind array_kind,
                                             IterationKind iteration_kind) {
  JSCallNode n(node);
  Node* receiver = n.receiver();
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();

  // Check if we know that {receiver} is a valid JSReceiver.
  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() || !inference.AllOfInstanceTypesAreJSReceiver()) {
    return NoChange();
  }

  // TypedArray iteration is stricter: it throws if the receiver is not a typed
  // array. So don't bother optimizing in that case.
  if (array_kind == ArrayIteratorKind::kTypedArray &&
      !inference.AllOfInstanceTypesAre(InstanceType::JS_TYPED_ARRAY_TYPE)) {
    return NoChange();
  }

  if (array_kind == ArrayIteratorKind::kTypedArray) {
    // Make sure we deopt when the JSArrayBuffer is detached.
    if (!dependencies()->DependOnArrayBufferDetachingProtector()) {
      CallParameters const& p = CallParametersOf(node->op());
      if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
        return NoChange();
      }

      std::set<ElementsKind> elements_kinds;
      for (MapRef map : inference.GetMaps()) {
        elements_kinds.insert(map.elements_kind());
      }

      // The following detach check is built with the given {elements_kinds} in
      // mind, so we have to install dependency on those.
      inference.RelyOnMapsPreferStability(
          dependencies(), jsgraph(), &effect, control,
          CallParametersOf(node->op()).feedback());

      JSCallReducerAssembler a(this, node);
      a.CheckIfTypedArrayWasDetached(
          TNode<JSTypedArray>::UncheckedCast(receiver),
          std::move(elements_kinds), p.feedback());
      std::tie(effect, control) = ReleaseEffectAndControlFromAssembler(&a);
    }
  }

  // JSCreateArrayIterator doesn't have control output, so we bypass the old
  // JSCall node on the control chain.
  ReplaceWithValue(node, node, node, control);

  // Morph the {node} into a JSCreateArrayIterator with the given {kind}.
  node->ReplaceInput(0, receiver);
  node->ReplaceInput(1, context);
  node->ReplaceInput(2, effect);
  node->ReplaceInput(3, control);
  node->TrimInputCount(4);
  NodeProperties::ChangeOp(node,
                           javascript()->CreateArrayIterator(iteration_kind));
  return Changed(node);
}

// ES #sec-%arrayiteratorprototype%.next
Reduction JSCallReducer::ReduceArrayIteratorPrototypeNext(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  Node* iterator = n.receiver();
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();

  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  if (iterator->opcode() != IrOpcode::kJSCreateArrayIterator) return NoChange();

  IterationKind const iteration_kind =
      CreateArrayIteratorParametersOf(iterator->op()).kind();
  Node* iterated_object = NodeProperties::GetValueInput(iterator, 0);
  Effect iterator_effect{NodeProperties::GetEffectInput(iterator)};

  MapInference inference(broker(), iterated_object, iterator_effect);
  if (!inference.HaveMaps()) return NoChange();
  ZoneRefSet<Map> const& iterated_object_maps = inference.GetMaps();

  // Check that various {iterated_object_maps} have compatible elements kinds.
  ElementsKind elements_kind = iterated_object_maps[0].elements_kind();
  if (IsTypedArrayElementsKind(elements_kind)) {
    // TurboFan doesn't support loading from BigInt typed arrays yet.
    if (elements_kind == BIGUINT64_ELEMENTS ||
        elements_kind == BIGINT64_ELEMENTS) {
      return inference.NoChange();
    }
    for (MapRef iterated_object_map : iterated_object_maps) {
      if (iterated_object_map.elements_kind() != elements_kind) {
        return inference.NoChange();
      }
    }
  } else {
    if (!CanInlineArrayIteratingBuiltin(broker(), iterated_object_maps,
                                        &elements_kind)) {
      return inference.NoChange();
    }
  }

  if (IsHoleyElementsKind(elements_kind) &&
      !dependencies()->DependOnNoElementsProtector()) {
    return inference.NoChange();
  }

  // Since the map inference was done relative to {iterator_effect} rather than
  // {effect}, we need to guard the use of the map(s) even when the inference
  // was reliable.
  inference.InsertMapChecks(jsgraph(), &effect, control, p.feedback());

  if (IsTypedArrayElementsKind(elements_kind)) {
    // See if we can skip the detaching check.
    if (!dependencies()->DependOnArrayBufferDetachingProtector()) {
      // Bail out if the {iterated_object}s JSArrayBuffer was detached.
      Node* buffer = effect = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForJSArrayBufferViewBuffer()),
          iterated_object, effect, control);
      Node* buffer_bit_field = effect = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForJSArrayBufferBitField()),
          buffer, effect, control);
      Node* check = graph()->NewNode(
          simplified()->NumberEqual(),
          graph()->NewNode(
              simplified()->NumberBitwiseAnd(), buffer_bit_field,
              jsgraph()->ConstantNoHole(JSArrayBuffer::WasDetachedBit::kMask)),
          jsgraph()->ZeroConstant());
      effect = graph()->NewNode(
          simplified()->CheckIf(DeoptimizeReason::kArrayBufferWasDetached,
                                p.feedback()),
          check, effect, control);
    }
  }

  // Load the [[NextIndex]] from the {iterator} and leverage the fact
  // that we definitely know that it's in Unsigned32 range since the
  // {iterated_object} is either a JSArray or a JSTypedArray. For the
  // latter case we even know that it's a Smi in UnsignedSmall range.
  FieldAccess index_access = AccessBuilder::ForJSArrayIteratorNextIndex();
  if (IsTypedArrayElementsKind(elements_kind)) {
    index_access.type = TypeCache::Get()->kJSTypedArrayLengthType;
  } else {
    index_access.type = TypeCache::Get()->kJSArrayLengthType;
  }
  Node* index = effect = graph()->NewNode(simplified()->LoadField(index_access),
                                          iterator, effect, control);

  // Load the elements of the {iterated_object}. While it feels
  // counter-intuitive to place the elements pointer load before
  // the condition below, as it might not be needed (if the {index}
  // is out of bounds for the {iterated_object}), it's better this
  // way as it allows the LoadElimination to eliminate redundant
  // reloads of the elements pointer.
  Node* elements = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSObjectElements()),
      iterated_object, effect, control);

  // Load the length of the {iterated_object}. Due to the map checks we
  // already know something about the length here, which we can leverage
  // to generate Word32 operations below without additional checking.
  FieldAccess length_access =
      IsTypedArrayElementsKind(elements_kind)
          ? AccessBuilder::ForJSTypedArrayLength()
          : AccessBuilder::ForJSArrayLength(elements_kind);
  Node* length = effect = graph()->NewNode(
      simplified()->LoadField(length_access), iterated_object, effect, control);

  // Check whether {index} is within the valid range for the {iterated_object}.
  Node* check = graph()->NewNode(simplified()->NumberLessThan(), index, length);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kNone), check, control);

  Node* done_true;
  Node* value_true;
  Node* etrue = effect;
  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  {
    // This extra check exists to refine the type of {index} but also to break
    // an exploitation technique that abuses typer mismatches.
    if (v8_flags.turbo_typer_hardening) {
      index = etrue = graph()->NewNode(
          simplified()->CheckBounds(p.feedback(),
                                    CheckBoundsFlag::kAbortOnOutOfBounds),
          index, length, etrue, if_true);
    }

    done_true = jsgraph()->FalseConstant();
    if (iteration_kind == IterationKind::kKeys) {
      // Just return the {index}.
      value_true = index;
    } else {
      DCHECK(iteration_kind == IterationKind::kEntries ||
             iteration_kind == IterationKind::kValues);

      if (IsTypedArrayElementsKind(elements_kind)) {
        Node* base_ptr = etrue =
            graph()->NewNode(simplified()->LoadField(
                                 AccessBuilder::ForJSTypedArrayBasePointer()),
                             iterated_object, etrue, if_true);
        Node* external_ptr = etrue = graph()->NewNode(
            simplified()->LoadField(
                AccessBuilder::ForJSTypedArrayExternalPointer()),
            iterated_object, etrue, if_true);

        ExternalArrayType array_type = kExternalInt8Array;
        switch (elements_kind) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) \
  case TYPE##_ELEMENTS:                           \
    array_type = kExternal##Type##Array;          \
    break;
          TYPED_ARRAYS(TYPED_ARRAY_CASE)
          default:
            UNREACHABLE();
#undef TYPED_ARRAY_CASE
        }

        Node* buffer = etrue =
            graph()->NewNode(simplified()->LoadField(
                                 AccessBuilder::ForJSArrayBufferViewBuffer()),
                             iterated_object, etrue, if_true);

        value_true = etrue =
            graph()->NewNode(simplified()->LoadTypedElement(array_type), buffer,
                             base_ptr, external_ptr, index, etrue, if_true);
      } else {
        value_true = etrue = graph()->NewNode(
            simplified()->LoadElement(
                AccessBuilder::ForFixedArrayElement(elements_kind)),
            elements, index, etrue, if_true);

        // Convert hole to undefined if needed.
        if (IsHoleyElementsKind(elements_kind)) {
          value_true = ConvertHoleToUndefined(value_true, elements_kind);
        }
      }

      if (iteration_kind == IterationKind::kEntries) {
        // Allocate elements for key/value pair
        value_true = etrue =
            graph()->NewNode(javascript()->CreateKeyValueArray(), index,
                             value_true, context, etrue);
      } else {
        DCHECK_EQ(IterationKind::kValues, iteration_kind);
      }
    }

    // Increment the [[NextIndex]] field in the {iterator}. The TypeGuards
    // above guarantee that the {next_index} is in the UnsignedSmall range.
    Node* next_index = graph()->NewNode(simplified()->NumberAdd(), index,
                                        jsgraph()->OneConstant());
    etrue = graph()->NewNode(simplified()->StoreField(index_access), iterator,
                             next_index, etrue, if_true);
  }

  Node* done_false;
  Node* value_false;
  Node* efalse = effect;
  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  {
    // iterator.[[NextIndex]] >= array.length, stop iterating.
    done_false = jsgraph()->TrueConstant();
    value_false = jsgraph()->UndefinedConstant();

    if (!IsTypedArrayElementsKind(elements_kind)) {
      // Mark the {iterator} as exhausted by setting the [[NextIndex]] to a
      // value that will never pass the length check again (aka the maximum
      // value possible for the specific iterated object). Note that this is
      // different from what the specification says, which is changing the
      // [[IteratedObject]] field to undefined, but that makes it difficult
      // to eliminate the map checks and "length" accesses in for..of loops.
      //
      // This is not necessary for JSTypedArray's, since the length of those
      // cannot change later and so if we were ever out of bounds for them
      // we will stay out-of-bounds forever.
      Node* end_index = jsgraph()->ConstantNoHole(index_access.type.Max());
      efalse = graph()->NewNode(simplified()->StoreField(index_access),
                                iterator, end_index, efalse, if_false);
    }
  }

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
  Node* value =
      graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                       value_true, value_false, control);
  Node* done =
      graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                       done_true, done_false, control);

  // Create IteratorResult object.
  value = effect = graph()->NewNode(javascript()->CreateIterResultObject(),
                                    value, done, context, effect);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES6 section 21.1.3.2 String.prototype.charCodeAt ( pos )
// ES6 section 21.1.3.3 String.prototype.codePointAt ( pos )
Reduction JSCallReducer::ReduceStringPrototypeStringAt(
    const Operator* string_access_operator, Node* node) {
  DCHECK(string_access_operator->opcode() == IrOpcode::kStringCharCodeAt ||
         string_access_operator->opcode() == IrOpcode::kStringCodePointAt);
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Node* receiver = n.receiver();
  Node* index = n.ArgumentOr(0, jsgraph()->ZeroConstant());
  Effect effect = n.effect();
  Control control = n.control();

  // Ensure that the {receiver} is actually a String.
  receiver = effect = graph()->NewNode(simplified()->CheckString(p.feedback()),
                                       receiver, effect, control);

  // Determine the {receiver} length.
  Node* receiver_length =
      graph()->NewNode(simplified()->StringLength(), receiver);

  // Check that the {index} is within range.
  index = effect = graph()->NewNode(simplified()->CheckBounds(p.feedback()),
                                    index, receiver_length, effect, control);

  // Return the character from the {receiver} as single character string.
  Node* value = effect = graph()->NewNode(string_access_operator, receiver,
                                          index, effect, control);

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES section 21.1.3.20
// String.prototype.startsWith ( searchString [ , position ] )
Reduction JSCallReducer::ReduceStringPrototypeStartsWith(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  TNode<Object> search_element = n.ArgumentOrUndefined(0, jsgraph());

  // Here are three conditions:
  // First, If search_element is definitely not a string, we make no change.
  // Second, If search_element is definitely a string and its length is less
  // or equal than max inline matching sequence threshold, we could inline
  // the entire matching sequence.
  // Third, we try to inline, and have a runtime deopt if search_element is
  // not a string.
  HeapObjectMatcher search_element_matcher(search_element);
  if (search_element_matcher.HasResolvedValue()) {
    ObjectRef target_ref = search_element_matcher.Ref(broker());
    if (!target_ref.IsString()) return NoChange();
    StringRef search_element_string = target_ref.AsString();
    if (!search_element_string.IsContentAccessible()) return NoChange();
    int length = search_element_string.length();
    // If search_element's length is less or equal than
    // kMaxInlineMatchSequence, we inline the entire
    // matching sequence.
    if (length <= kMaxInlineMatchSequence) {
      JSCallReducerAssembler a(this, node);
      Node* subgraph = a.ReduceStringPrototypeStartsWith(search_element_string);
      return ReplaceWithSubgraph(&a, subgraph);
    }
  }

  JSCallReducerAssembler a(this, node);
  Node* subgraph = a.ReduceStringPrototypeStartsWith();
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES section 21.1.3.7
// String.prototype.endsWith(searchString [, endPosition])
Reduction JSCallReducer::ReduceStringPrototypeEndsWith(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  TNode<Object> search_element = n.ArgumentOrUndefined(0, jsgraph());

  // Here are three conditions:
  // First, If search_element is definitely not a string, we make no change.
  // Second, If search_element is definitely a string and its length is less
  // or equal than max inline matching sequence threshold, we could inline
  // the entire matching sequence.
  // Third, we try to inline, and have a runtime deopt if search_element is
  // not a string.
  HeapObjectMatcher search_element_matcher(search_element);
  if (search_element_matcher.HasResolvedValue()) {
    ObjectRef target_ref = search_element_matcher.Ref(broker());
    if (!target_ref.IsString()) return NoChange();
    StringRef search_element_string = target_ref.AsString();
    if (!search_element_string.IsContentAccessible()) return NoChange();
    int length = search_element_string.length();
    // If search_element's length is less or equal than
    // kMaxInlineMatchSequence, we inline the entire
    // matching sequence.
    if (length <= kMaxInlineMatchSequence) {
      JSCallReducerAssembler a(this, node);
      Node* subgraph = a.ReduceStringPrototypeEndsWith(search_element_string);
      return ReplaceWithSubgraph(&a, subgraph);
    }
  }

  JSCallReducerAssembler a(this, node);
  Node* subgraph = a.ReduceStringPrototypeEndsWith();
  return ReplaceWithSubgraph(&a, subgraph);
}

// ES section 21.1.3.1 String.prototype.charAt ( pos )
Reduction JSCallReducer::ReduceStringPrototypeCharAt(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Node* receiver = n.receiver();
  Node* index = n.ArgumentOr(0, jsgraph()->ZeroConstant());

  // Attempt to constant fold the operation when we know
  // that receiver is a string and index is a number.
  HeapObjectMatcher receiver_matcher(receiver);
  NumberMatcher index_matcher(index);
  if (receiver_matcher.HasResolvedValue()) {
    HeapObjectRef receiver_ref = receiver_matcher.Ref(broker());
    if (!receiver_ref.IsString()) return NoChange();
    StringRef receiver_string = receiver_ref.AsString();
    bool is_content_accessible = receiver_string.IsContentAccessible();
    bool is_integer_in_max_range =
        index_matcher.IsInteger() &&
        index_matcher.IsInRange(
            0.0, static_cast<double>(JSObject::kMaxElementIndex));
    if (is_content_accessible && is_integer_in_max_range) {
      const uint32_t index =
          static_cast<uint32_t>(index_matcher.ResolvedValue());
      JSCallReducerAssembler a(this, node);
      Node* subgraph = a.ReduceStringPrototypeCharAt(receiver_string, index);
      return ReplaceWithSubgraph(&a, subgraph);
    }
  }

  JSCallReducerAssembler a(this, node);
  Node* subgraph = a.ReduceStringPrototypeCharAt();
  return ReplaceWithSubgraph(&a, subgraph);
}

#ifdef V8_INTL_SUPPORT

Reduction JSCallReducer::ReduceStringPrototypeToLowerCaseIntl(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  Effect effect = n.effect();
  Control control = n.control();

  Node* receiver = effect = graph()->NewNode(
      simplified()->CheckString(p.feedback()), n.receiver(), effect, control);

  NodeProperties::ReplaceEffectInput(node, effect);
  RelaxEffectsAndControls(node);
  node->ReplaceInput(0, receiver);
  node->TrimInputCount(1);
  NodeProperties::ChangeOp(node, simplified()->StringToLowerCaseIntl());
  NodeProperties::SetType(node, Type::String());
  return Changed(node);
}

Reduction JSCallReducer::ReduceStringPrototypeToUpperCaseIntl(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  Effect effect = n.effect();
  Control control = n.control();

  Node* receiver = effect = graph()->NewNode(
      simplified()->CheckString(p.feedback()), n.receiver(), effect, control);

  NodeProperties::ReplaceEffectInput(node, effect);
  RelaxEffectsAndControls(node);
  node->ReplaceInput(0, receiver);
  node->TrimInputCount(1);
  NodeProperties::ChangeOp(node, simplified()->StringToUpperCaseIntl());
  NodeProperties::SetType(node, Type::String());
  return Changed(node);
}

#endif  // V8_INTL_SUPPORT

// ES #sec-string.fromcharcode
Reduction JSCallReducer::ReduceStringFromCharCode(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() == 1) {
    Effect effect = n.effect();
    Control control = n.control();
    Node* input = n.Argument(0);

    input = effect = graph()->NewNode(
        simplified()->SpeculativeToNumber(NumberOperationHint::kNumberOrOddball,
                                          p.feedback()),
        input, effect, control);

    Node* value =
        graph()->NewNode(simplified()->StringFromSingleCharCode(), input);
    ReplaceWithValue(node, value, effect);
    return Replace(value);
  }
  return NoChange();
}

// ES #sec-string.fromcodepoint
Reduction JSCallReducer::ReduceStringFromCodePoint(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() != 1) return NoChange();

  Effect effect = n.effect();
  Control control = n.control();
  Node* input = n.Argument(0);

  input = effect = graph()->NewNode(
      simplified()->CheckBounds(p.feedback(),
                                CheckBoundsFlag::kConvertStringAndMinusZero),
      input, jsgraph()->ConstantNoHole(0x10FFFF + 1), effect, control);

  Node* value =
      graph()->NewNode(simplified()->StringFromSingleCodePoint(), input);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

Reduction JSCallReducer::ReduceStringPrototypeIterator(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* receiver = effect = graph()->NewNode(
      simplified()->CheckString(p.feedback()), n.receiver(), effect, control);
  Node* iterator = effect =
      graph()->NewNode(javascript()->CreateStringIterator(), receiver,
                       jsgraph()->NoContextConstant(), effect);
  ReplaceWithValue(node, iterator, effect, control);
  return Replace(iterator);
}

Reduction JSCallReducer::ReduceStringPrototypeLocaleCompare(Node* node) {
#ifdef V8_INTL_SUPPORT
  JSCallNode n(node);
  // Signature: receiver.localeCompare(compareString, locales, options)
  if (n.ArgumentCount() < 1 || n.ArgumentCount() > 3) {
    return NoChange();
  }

  {
    Handle<Object> locales;
    {
      HeapObjectMatcher m(n.ArgumentOrUndefined(1, jsgraph()));
      if (!m.HasResolvedValue()) return NoChange();
      if (m.Is(factory()->undefined_value())) {
        locales = factory()->undefined_value();
      } else {
        ObjectRef ref = m.Ref(broker());
        if (!ref.IsString()) return NoChange();
        StringRef sref = ref.AsString();
        if (base::Optional<Handle<String>> maybe_locales =
                sref.ObjectIfContentAccessible(broker())) {
          locales = *maybe_locales;
        } else {
          return NoChange();
        }
      }
    }

    TNode<Object> options = n.ArgumentOrUndefined(2, jsgraph());
    {
      HeapObjectMatcher m(options);
      if (!m.Is(factory()->undefined_value())) {
        return NoChange();
      }
    }

    if (Intl::CompareStringsOptionsFor(broker()->local_isolate_or_isolate(),
                                       locales, factory()->undefined_value()) !=
        Intl::CompareStringsOptions::kTryFastPath) {
      return NoChange();
    }
  }

  Callable callable =
      Builtins::CallableFor(isolate(), Builtin::kStringFastLocaleCompare);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(),
      CallDescriptor::kNeedsFrameState);
  node->RemoveInput(n.FeedbackVectorIndex());
  if (n.ArgumentCount() == 3) {
    node->RemoveInput(n.ArgumentIndex(2));
  } else if (n.ArgumentCount() == 1) {
    node->InsertInput(graph()->zone(), n.LastArgumentIndex() + 1,
                      jsgraph()->UndefinedConstant());
  } else {
    DCHECK_EQ(2, n.ArgumentCount());
  }
  node->InsertInput(graph()->zone(), 0,
                    jsgraph()->HeapConstantNoHole(callable.code()));
  NodeProperties::ChangeOp(node, common()->Call(call_descriptor));
  return Changed(node);
#else
  return NoChange();
#endif
}

Reduction JSCallReducer::ReduceStringIteratorPrototypeNext(Node* node) {
  JSCallNode n(node);
  Node* receiver = n.receiver();
  Effect effect = n.effect();
  Control control = n.control();
  Node* context = n.context();

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() ||
      !inference.AllOfInstanceTypesAre(JS_STRING_ITERATOR_TYPE)) {
    return NoChange();
  }

  Node* string = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSStringIteratorString()),
      receiver, effect, control);
  Node* index = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSStringIteratorIndex()),
      receiver, effect, control);
  Node* length = graph()->NewNode(simplified()->StringLength(), string);

  // branch0: if (index < length)
  Node* check0 =
      graph()->NewNode(simplified()->NumberLessThan(), index, length);
  Node* branch0 =
      graph()->NewNode(common()->Branch(BranchHint::kNone), check0, control);

  Node* etrue0 = effect;
  Node* if_true0 = graph()->NewNode(common()->IfTrue(), branch0);
  Node* done_true;
  Node* vtrue0;
  {
    done_true = jsgraph()->FalseConstant();
    vtrue0 = etrue0 = graph()->NewNode(simplified()->StringFromCodePointAt(),
                                       string, index, etrue0, if_true0);

    // Update iterator.[[NextIndex]]
    Node* char_length = graph()->NewNode(simplified()->StringLength(), vtrue0);
    index = graph()->NewNode(simplified()->NumberAdd(), index, char_length);
    etrue0 = graph()->NewNode(
        simplified()->StoreField(AccessBuilder::ForJSStringIteratorIndex()),
        receiver, index, etrue0, if_true0);
  }

  Node* if_false0 = graph()->NewNode(common()->IfFalse(), branch0);
  Node* done_false;
  Node* vfalse0;
  {
    vfalse0 = jsgraph()->UndefinedConstant();
    done_false = jsgraph()->TrueConstant();
  }

  control = graph()->NewNode(common()->Merge(2), if_true0, if_false0);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue0, effect, control);
  Node* value =
      graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2), vtrue0,
                       vfalse0, control);
  Node* done =
      graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                       done_true, done_false, control);

  value = effect = graph()->NewNode(javascript()->CreateIterResultObject(),
                                    value, done, context, effect);

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES #sec-string.prototype.concat
Reduction JSCallReducer::ReduceStringPrototypeConcat(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  const int parameter_count = n.ArgumentCount();
  if (parameter_count > 1) return NoChange();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* receiver = effect = graph()->NewNode(
      simplified()->CheckString(p.feedback()), n.receiver(), effect, control);

  if (parameter_count == 0) {
    ReplaceWithValue(node, receiver, effect, control);
    return Replace(receiver);
  }

  Node* argument = effect = graph()->NewNode(
      simplified()->CheckString(p.feedback()), n.Argument(0), effect, control);
  Node* receiver_length =
      graph()->NewNode(simplified()->StringLength(), receiver);
  Node* argument_length =
      graph()->NewNode(simplified()->StringLength(), argument);
  Node* length = graph()->NewNode(simplified()->NumberAdd(), receiver_length,
                                  argument_length);
  length = effect = graph()->NewNode(
      simplified()->CheckBounds(p.feedback()), length,
      jsgraph()->ConstantNoHole(String::kMaxLength + 1), effect, control);

  Node* value = graph()->NewNode(simplified()->StringConcat(), length, receiver,
                                 argument);

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSCallReducer::ReducePromiseConstructor(Node* node) {
  PromiseBuiltinReducerAssembler a(this, node);

  // We only inline when we have the executor.
  if (a.ConstructArity() < 1) return NoChange();
  // Only handle builtins Promises, not subclasses.
  if (a.TargetInput() != a.NewTargetInput()) return NoChange();
  if (!dependencies()->DependOnPromiseHookProtector()) return NoChange();

  TNode<Object> subgraph = a.ReducePromiseConstructor(native_context());
  return ReplaceWithSubgraph(&a, subgraph);
}

bool JSCallReducer::DoPromiseChecks(MapInference* inference) {
  if (!inference->HaveMaps()) return false;
  ZoneRefSet<Map> const& receiver_maps = inference->GetMaps();

  // Check whether all {receiver_maps} are JSPromise maps and
  // have the initial Promise.prototype as their [[Prototype]].
  for (MapRef receiver_map : receiver_maps) {
    if (!receiver_map.IsJSPromiseMap()) return false;
    HeapObjectRef prototype = receiver_map.prototype(broker());
    if (!prototype.equals(native_context().promise_prototype(broker()))) {
      return false;
    }
  }

  return true;
}

// ES section #sec-promise.prototype.catch
Reduction JSCallReducer::ReducePromisePrototypeCatch(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  int arity = p.arity_without_implicit_args();
  Node* receiver = n.receiver();
  Effect effect = n.effect();
  Control control = n.control();

  MapInference inference(broker(), receiver, effect);
  if (!DoPromiseChecks(&inference)) return inference.NoChange();

  if (!dependencies()->DependOnPromiseThenProtector()) {
    return inference.NoChange();
  }
  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  // Massage the {node} to call "then" instead by first removing all inputs
  // following the onRejected parameter, and then filling up the parameters
  // to two inputs from the left with undefined.
  Node* target = jsgraph()->ConstantNoHole(
      native_context().promise_then(broker()), broker());
  NodeProperties::ReplaceValueInput(node, target, 0);
  NodeProperties::ReplaceEffectInput(node, effect);
  for (; arity > 1; --arity) node->RemoveInput(3);
  for (; arity < 2; ++arity) {
    node->InsertInput(graph()->zone(), 2, jsgraph()->UndefinedConstant());
  }
  NodeProperties::ChangeOp(
      node, javascript()->Call(
                JSCallNode::ArityForArgc(arity), p.frequency(), p.feedback(),
                ConvertReceiverMode::kNotNullOrUndefined, p.speculation_mode(),
                CallFeedbackRelation::kUnrelated));
  return Changed(node).FollowedBy(ReducePromisePrototypeThen(node));
}

Node* JSCallReducer::CreateClosureFromBuiltinSharedFunctionInfo(
    SharedFunctionInfoRef shared, Node* context, Node* effect, Node* control) {
  DCHECK(shared.HasBuiltinId());
  Handle<FeedbackCell> feedback_cell =
      isolate()->factory()->many_closures_cell();
  Callable const callable =
      Builtins::CallableFor(isolate(), shared.builtin_id());
  CodeRef code = MakeRef(broker(), *callable.code());
  return graph()->NewNode(javascript()->CreateClosure(shared, code),
                          jsgraph()->HeapConstantNoHole(feedback_cell), context,
                          effect, control);
}

// ES section #sec-promise.prototype.finally
Reduction JSCallReducer::ReducePromisePrototypeFinally(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  int arity = p.arity_without_implicit_args();
  Node* receiver = n.receiver();
  Node* on_finally = n.ArgumentOrUndefined(0, jsgraph());
  Effect effect = n.effect();
  Control control = n.control();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  MapInference inference(broker(), receiver, effect);
  if (!DoPromiseChecks(&inference)) return inference.NoChange();
  ZoneRefSet<Map> const& receiver_maps = inference.GetMaps();

  if (!dependencies()->DependOnPromiseHookProtector()) {
    return inference.NoChange();
  }
  if (!dependencies()->DependOnPromiseThenProtector()) {
    return inference.NoChange();
  }
  if (!dependencies()->DependOnPromiseSpeciesProtector()) {
    return inference.NoChange();
  }
  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  // Check if {on_finally} is callable, and if so wrap it into appropriate
  // closures that perform the finalization.
  Node* check = graph()->NewNode(simplified()->ObjectIsCallable(), on_finally);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = effect;
  Node* catch_true;
  Node* then_true;
  {
    Node* context = jsgraph()->ConstantNoHole(native_context(), broker());
    Node* constructor = jsgraph()->ConstantNoHole(
        native_context().promise_function(broker()), broker());

    // Allocate shared context for the closures below.
    context = etrue = graph()->NewNode(
        javascript()->CreateFunctionContext(
            native_context().scope_info(broker()),
            int{PromiseBuiltins::kPromiseFinallyContextLength} -
                Context::MIN_CONTEXT_SLOTS,
            FUNCTION_SCOPE),
        context, etrue, if_true);
    etrue = graph()->NewNode(
        simplified()->StoreField(
            AccessBuilder::ForContextSlot(PromiseBuiltins::kOnFinallySlot)),
        context, on_finally, etrue, if_true);
    etrue = graph()->NewNode(
        simplified()->StoreField(
            AccessBuilder::ForContextSlot(PromiseBuiltins::kConstructorSlot)),
        context, constructor, etrue, if_true);

    // Allocate the closure for the reject case.
    SharedFunctionInfoRef promise_catch_finally =
        MakeRef(broker(), factory()->promise_catch_finally_shared_fun());
    catch_true = etrue = CreateClosureFromBuiltinSharedFunctionInfo(
        promise_catch_finally, context, etrue, if_true);

    // Allocate the closure for the fulfill case.
    SharedFunctionInfoRef promise_then_finally =
        MakeRef(broker(), factory()->promise_then_finally_shared_fun());
    then_true = etrue = CreateClosureFromBuiltinSharedFunctionInfo(
        promise_then_finally, context, etrue, if_true);
  }

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = effect;
  Node* catch_false = on_finally;
  Node* then_false = on_finally;

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
  Node* catch_finally =
      graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                       catch_true, catch_false, control);
  Node* then_finally =
      graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                       then_true, then_false, control);

  // At this point we definitely know that {receiver} has one of the
  // {receiver_maps}, so insert a MapGuard as a hint for the lowering
  // of the call to "then" below.
  {
    effect = graph()->NewNode(simplified()->MapGuard(receiver_maps), receiver,
                              effect, control);
  }

  // Massage the {node} to call "then" instead by first removing all inputs
  // following the onFinally parameter, and then replacing the only parameter
  // input with the {on_finally} value.
  Node* target = jsgraph()->ConstantNoHole(
      native_context().promise_then(broker()), broker());
  NodeProperties::ReplaceValueInput(node, target, n.TargetIndex());
  NodeProperties::ReplaceEffectInput(node, effect);
  NodeProperties::ReplaceControlInput(node, control);
  for (; arity > 2; --arity) node->RemoveInput(2);
  for (; arity < 2; ++arity) {
    node->InsertInput(graph()->zone(), 2, then_finally);
  }
  node->ReplaceInput(2, then_finally);
  node->ReplaceInput(3, catch_finally);
  NodeProperties::ChangeOp(
      node, javascript()->Call(
                JSCallNode::ArityForArgc(arity), p.frequency(), p.feedback(),
                ConvertReceiverMode::kNotNullOrUndefined, p.speculation_mode(),
                CallFeedbackRelation::kUnrelated));
  return Changed(node).FollowedBy(ReducePromisePrototypeThen(node));
}

Reduction JSCallReducer::ReducePromisePrototypeThen(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Node* receiver = n.receiver();
  Node* on_fulfilled = n.ArgumentOrUndefined(0, jsgraph());
  Node* on_rejected = n.ArgumentOrUndefined(1, jsgraph());
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();
  FrameState frame_state = n.frame_state();

  MapInference inference(broker(), receiver, effect);
  if (!DoPromiseChecks(&inference)) return inference.NoChange();

  if (!dependencies()->DependOnPromiseHookProtector()) {
    return inference.NoChange();
  }
  if (!dependencies()->DependOnPromiseSpeciesProtector()) {
    return inference.NoChange();
  }
  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  // Check that {on_fulfilled} is callable.
  on_fulfilled = graph()->NewNode(
      common()->Select(MachineRepresentation::kTagged, BranchHint::kTrue),
      graph()->NewNode(simplified()->ObjectIsCallable(), on_fulfilled),
      on_fulfilled, jsgraph()->UndefinedConstant());

  // Check that {on_rejected} is callable.
  on_rejected = graph()->NewNode(
      common()->Select(MachineRepresentation::kTagged, BranchHint::kTrue),
      graph()->NewNode(simplified()->ObjectIsCallable(), on_rejected),
      on_rejected, jsgraph()->UndefinedConstant());

  // Create the resulting JSPromise.
  Node* promise = effect =
      graph()->NewNode(javascript()->CreatePromise(), context, effect);

  // Chain {result} onto {receiver}.
  promise = effect = graph()->NewNode(
      javascript()->PerformPromiseThen(), receiver, on_fulfilled, on_rejected,
      promise, context, frame_state, effect, control);

  // At this point we know that {promise} is going to have the
  // initial Promise map, since even if {PerformPromiseThen}
  // above called into the host rejection tracker, the {promise}
  // doesn't escape to user JavaScript. So bake this information
  // into the graph such that subsequent passes can use the
  // information for further optimizations.
  MapRef promise_map =
      native_context().promise_function(broker()).initial_map(broker());
  effect =
      graph()->NewNode(simplified()->MapGuard(ZoneRefSet<Map>(promise_map)),
                       promise, effect, control);

  ReplaceWithValue(node, promise, effect, control);
  return Replace(promise);
}

// ES section #sec-promise.resolve
Reduction JSCallReducer::ReducePromiseResolveTrampoline(Node* node) {
  JSCallNode n(node);
  Node* receiver = n.receiver();
  Node* value = n.ArgumentOrUndefined(0, jsgraph());
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();
  FrameState frame_state = n.frame_state();

  // Only reduce when the receiver is guaranteed to be a JSReceiver.
  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() || !inference.AllOfInstanceTypesAreJSReceiver()) {
    return NoChange();
  }

  // Morph the {node} into a JSPromiseResolve operation.
  node->ReplaceInput(0, receiver);
  node->ReplaceInput(1, value);
  node->ReplaceInput(2, context);
  node->ReplaceInput(3, frame_state);
  node->ReplaceInput(4, effect);
  node->ReplaceInput(5, control);
  node->TrimInputCount(6);
  NodeProperties::ChangeOp(node, javascript()->PromiseResolve());
  return Changed(node);
}

// ES #sec-typedarray-constructors
Reduction JSCallReducer::ReduceTypedArrayConstructor(
    Node* node, SharedFunctionInfoRef shared) {
  JSConstructNode n(node);
  Node* target = n.target();
  Node* arg0 = n.ArgumentOrUndefined(0, jsgraph());
  Node* arg1 = n.ArgumentOrUndefined(1, jsgraph());
  Node* arg2 = n.ArgumentOrUndefined(2, jsgraph());
  Node* new_target = n.new_target();
  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Effect effect = n.effect();
  Control control = n.control();

  // Insert a construct stub frame into the chain of frame states. This will
  // reconstruct the proper frame when deoptimizing within the constructor.
  frame_state = CreateConstructInvokeStubFrameState(node, frame_state, shared,
                                                    context, common(), graph());

  // This continuation just returns the newly created JSTypedArray. We
  // pass the_hole as the receiver, just like the builtin construct stub
  // does in this case.
  Node* const receiver = jsgraph()->TheHoleConstant();
  Node* continuation_frame_state = CreateGenericLazyDeoptContinuationFrameState(
      jsgraph(), shared, target, context, receiver, frame_state);

  Node* result = graph()->NewNode(javascript()->CreateTypedArray(), target,
                                  new_target, arg0, arg1, arg2, context,
                                  continuation_frame_state, effect, control);
  return Replace(result);
}

// ES #sec-get-%typedarray%.prototype-@@tostringtag
Reduction JSCallReducer::ReduceTypedArrayPrototypeToStringTag(Node* node) {
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  NodeVector values(graph()->zone());
  NodeVector effects(graph()->zone());
  NodeVector controls(graph()->zone());

  Node* smi_check = graph()->NewNode(simplified()->ObjectIsSmi(), receiver);
  control = graph()->NewNode(common()->Branch(BranchHint::kFalse), smi_check,
                             control);

  values.push_back(jsgraph()->UndefinedConstant());
  effects.push_back(effect);
  controls.push_back(graph()->NewNode(common()->IfTrue(), control));

  control = graph()->NewNode(common()->IfFalse(), control);
  Node* receiver_map = effect =
      graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                       receiver, effect, control);
  Node* receiver_bit_field2 = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForMapBitField2()), receiver_map,
      effect, control);
  Node* receiver_elements_kind = graph()->NewNode(
      simplified()->NumberShiftRightLogical(),
      graph()->NewNode(
          simplified()->NumberBitwiseAnd(), receiver_bit_field2,
          jsgraph()->ConstantNoHole(Map::Bits2::ElementsKindBits::kMask)),
      jsgraph()->ConstantNoHole(Map::Bits2::ElementsKindBits::kShift));

  // Offset the elements kind by FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND,
  // so that the branch cascade below is turned into a simple table
  // switch by the ControlFlowOptimizer later.
  receiver_elements_kind = graph()->NewNode(
      simplified()->NumberSubtract(), receiver_elements_kind,
      jsgraph()->ConstantNoHole(FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND));

  // To be converted into a switch by the ControlFlowOptimizer, the below
  // code requires that TYPED_ARRAYS and RAB_GSAB_TYPED_ARRAYS are consecutive.
  static_assert(LAST_FIXED_TYPED_ARRAY_ELEMENTS_KIND + 1 ==
                FIRST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND);
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype)                          \
  do {                                                                     \
    Node* check = graph()->NewNode(                                        \
        simplified()->NumberEqual(), receiver_elements_kind,               \
        jsgraph()->ConstantNoHole(TYPE##_ELEMENTS -                        \
                                  FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND)); \
    control = graph()->NewNode(common()->Branch(), check, control);        \
    values.push_back(jsgraph()->ConstantNoHole(                            \
        broker()->GetTypedArrayStringTag(TYPE##_ELEMENTS), broker()));     \
    effects.push_back(effect);                                             \
    controls.push_back(graph()->NewNode(common()->IfTrue(), control));     \
    control = graph()->NewNode(common()->IfFalse(), control);              \
  } while (false);
  TYPED_ARRAYS(TYPED_ARRAY_CASE)
  RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

  values.push_back(jsgraph()->UndefinedConstant());
  effects.push_back(effect);
  controls.push_back(control);

  int const count = static_cast<int>(controls.size());
  control = graph()->NewNode(common()->Merge(count), count, &controls.front());
  effects.push_back(control);
  effect =
      graph()->NewNode(common()->EffectPhi(count), count + 1, &effects.front());
  values.push_back(control);
  Node* value =
      graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, count),
                       count + 1, &values.front());
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSCallReducer::ReduceArrayBufferViewByteLengthAccessor(
    Node* node, InstanceType instance_type) {
  // TODO(v8:11111): Optimize for JS_RAB_GSAB_DATA_VIEW_TYPE too.
  DCHECK(instance_type == JS_TYPED_ARRAY_TYPE ||
         instance_type == JS_DATA_VIEW_TYPE);
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() ||
      !inference.AllOfInstanceTypesAre(instance_type)) {
    return inference.NoChange();
  }

  std::set<ElementsKind> elements_kinds;
  bool maybe_rab_gsab = false;
  if (instance_type == JS_TYPED_ARRAY_TYPE) {
    for (MapRef map : inference.GetMaps()) {
      ElementsKind kind = map.elements_kind();
      elements_kinds.insert(kind);
      if (IsRabGsabTypedArrayElementsKind(kind)) maybe_rab_gsab = true;
    }
  }

  if (!maybe_rab_gsab) {
    // We do not perform any change depending on this inference.
    Reduction unused_reduction = inference.NoChange();
    USE(unused_reduction);
    // Call default implementation for non-rab/gsab TAs.
    return ReduceArrayBufferViewAccessor(
        node, JS_TYPED_ARRAY_TYPE,
        AccessBuilder::ForJSArrayBufferViewByteLength(),
        Builtin::kTypedArrayPrototypeByteLength);
  }

  const CallParameters& p = CallParametersOf(node->op());
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return inference.NoChange();
  }
  DCHECK(p.feedback().IsValid());

  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  const bool depended_on_detaching_protector =
      dependencies()->DependOnArrayBufferDetachingProtector();
  if (!depended_on_detaching_protector && instance_type == JS_DATA_VIEW_TYPE) {
    // DataView prototype accessors throw on detached ArrayBuffers instead of
    // return 0, so skip the optimization.
    //
    // TODO(turbofan): Ideally we would bail out if the buffer is actually
    // detached.
    return inference.NoChange();
  }

  JSCallReducerAssembler a(this, node);
  TNode<JSTypedArray> typed_array =
      TNode<JSTypedArray>::UncheckedCast(receiver);
  TNode<Number> length = a.ArrayBufferViewByteLength(
      typed_array, instance_type, std::move(elements_kinds), a.ContextInput());

  return ReplaceWithSubgraph(&a, length);
}

Reduction JSCallReducer::ReduceTypedArrayPrototypeLength(Node* node) {
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() ||
      !inference.AllOfInstanceTypesAre(JS_TYPED_ARRAY_TYPE)) {
    return inference.NoChange();
  }

  std::set<ElementsKind> elements_kinds;
  bool maybe_rab_gsab = false;
  for (MapRef map : inference.GetMaps()) {
    ElementsKind kind = map.elements_kind();
    elements_kinds.insert(kind);
    if (IsRabGsabTypedArrayElementsKind(kind)) maybe_rab_gsab = true;
  }

  if (!maybe_rab_gsab) {
    // We do not perform any change depending on this inference.
    Reduction unused_reduction = inference.NoChange();
    USE(unused_reduction);
    // Call default implementation for non-rab/gsab TAs.
    return ReduceArrayBufferViewAccessor(node, JS_TYPED_ARRAY_TYPE,
                                         AccessBuilder::ForJSTypedArrayLength(),
                                         Builtin::kTypedArrayPrototypeLength);
  }

  if (!inference.RelyOnMapsViaStability(dependencies())) {
    return inference.NoChange();
  }

  JSCallReducerAssembler a(this, node);
  TNode<JSTypedArray> typed_array =
      TNode<JSTypedArray>::UncheckedCast(receiver);
  TNode<Number> length = a.TypedArrayLength(
      typed_array, std::move(elements_kinds), a.ContextInput());

  return ReplaceWithSubgraph(&a, length);
}

// ES #sec-number.isfinite
Reduction JSCallReducer::ReduceNumberIsFinite(Node* node) {
  JSCallNode n(node);
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->FalseConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* input = n.Argument(0);
  Node* value = graph()->NewNode(simplified()->ObjectIsFiniteNumber(), input);
  ReplaceWithValue(node, value);
  return Replace(value);
}

// ES #sec-number.isfinite
Reduction JSCallReducer::ReduceNumberIsInteger(Node* node) {
  JSCallNode n(node);
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->FalseConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* input = n.Argument(0);
  Node* value = graph()->NewNode(simplified()->ObjectIsInteger(), input);
  ReplaceWithValue(node, value);
  return Replace(value);
}

// ES #sec-number.issafeinteger
Reduction JSCallReducer::ReduceNumberIsSafeInteger(Node* node) {
  JSCallNode n(node);
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->FalseConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* input = n.Argument(0);
  Node* value = graph()->NewNode(simplified()->ObjectIsSafeInteger(), input);
  ReplaceWithValue(node, value);
  return Replace(value);
}

// ES #sec-number.isnan
Reduction JSCallReducer::ReduceNumberIsNaN(Node* node) {
  JSCallNode n(node);
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->FalseConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* input = n.Argument(0);
  Node* value = graph()->NewNode(simplified()->ObjectIsNaN(), input);
  ReplaceWithValue(node, value);
  return Replace(value);
}

Reduction JSCallReducer::ReduceMapPrototypeGet(Node* node) {
  // We only optimize if we have target, receiver and key parameters.
  JSCallNode n(node);
  if (n.ArgumentCount() != 1) return NoChange();
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};
  Node* key = NodeProperties::GetValueInput(node, 2);

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() || !inference.AllOfInstanceTypesAre(JS_MAP_TYPE)) {
    return NoChange();
  }

  Node* table = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSCollectionTable()), receiver,
      effect, control);

  Node* entry = effect = graph()->NewNode(
      simplified()->FindOrderedCollectionEntry(CollectionKind::kMap), table,
      key, effect, control);

  Node* check = graph()->NewNode(simplified()->NumberEqual(), entry,
                                 jsgraph()->MinusOneConstant());

  Node* branch = graph()->NewNode(common()->Branch(), check, control);

  // Key not found.
  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = effect;
  Node* vtrue = jsgraph()->UndefinedConstant();

  // Key found.
  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = effect;
  Node* vfalse = efalse = graph()->NewNode(
      simplified()->LoadElement(AccessBuilder::ForOrderedHashMapEntryValue()),
      table, entry, efalse, if_false);

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* value = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), vtrue, vfalse, control);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

namespace {

InstanceType InstanceTypeForCollectionKind(CollectionKind kind) {
  switch (kind) {
    case CollectionKind::kMap:
      return JS_MAP_TYPE;
    case CollectionKind::kSet:
      return JS_SET_TYPE;
  }
  UNREACHABLE();
}

}  // namespace

Reduction JSCallReducer::ReduceCollectionPrototypeHas(
    Node* node, CollectionKind collection_kind) {
  // We only optimize if we have target, receiver and key parameters.
  JSCallNode n(node);
  if (n.ArgumentCount() != 1) return NoChange();
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};
  Node* key = NodeProperties::GetValueInput(node, 2);
  InstanceType instance_type = InstanceTypeForCollectionKind(collection_kind);

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() ||
      !inference.AllOfInstanceTypesAre(instance_type)) {
    return NoChange();
  }

  Node* table = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSCollectionTable()), receiver,
      effect, control);

  Node* index = effect = graph()->NewNode(
      simplified()->FindOrderedCollectionEntry(collection_kind), table, key,
      effect, control);

  Node* value = graph()->NewNode(simplified()->NumberEqual(), index,
                                 jsgraph()->MinusOneConstant());
  value = graph()->NewNode(simplified()->BooleanNot(), value);

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSCallReducer::ReduceMapPrototypeHas(Node* node) {
  return ReduceCollectionPrototypeHas(node, CollectionKind::kMap);
}

Reduction JSCallReducer::ReduceSetPrototypeHas(Node* node) {
  return ReduceCollectionPrototypeHas(node, CollectionKind::kSet);
}

Reduction JSCallReducer::ReduceCollectionIteration(
    Node* node, CollectionKind collection_kind, IterationKind iteration_kind) {
  DCHECK_EQ(IrOpcode::kJSCall, node->opcode());
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Node* context = NodeProperties::GetContextInput(node);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  InstanceType type = InstanceTypeForCollectionKind(collection_kind);
  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() || !inference.AllOfInstanceTypesAre(type)) {
    return NoChange();
  }

  Node* js_create_iterator = effect = graph()->NewNode(
      javascript()->CreateCollectionIterator(collection_kind, iteration_kind),
      receiver, context, effect, control);
  ReplaceWithValue(node, js_create_iterator, effect);
  return Replace(js_create_iterator);
}

Reduction JSCallReducer::ReduceCollectionPrototypeSize(
    Node* node, CollectionKind collection_kind) {
  DCHECK_EQ(IrOpcode::kJSCall, node->opcode());
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  InstanceType type = InstanceTypeForCollectionKind(collection_kind);
  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() || !inference.AllOfInstanceTypesAre(type)) {
    return NoChange();
  }

  Node* table = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSCollectionTable()), receiver,
      effect, control);
  Node* value = effect = graph()->NewNode(
      simplified()->LoadField(
          AccessBuilder::ForOrderedHashMapOrSetNumberOfElements()),
      table, effect, control);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSCallReducer::ReduceCollectionIteratorPrototypeNext(
    Node* node, int entry_size, Handle<HeapObject> empty_collection,
    InstanceType collection_iterator_instance_type_first,
    InstanceType collection_iterator_instance_type_last) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Node* receiver = n.receiver();
  Node* context = n.context();
  Effect effect = n.effect();
  Control control = n.control();

  // A word of warning to begin with: This whole method might look a bit
  // strange at times, but that's mostly because it was carefully handcrafted
  // to allow for full escape analysis and scalar replacement of both the
  // collection iterator object and the iterator results, including the
  // key-value arrays in case of Set/Map entry iteration.
  //
  // TODO(turbofan): Currently the escape analysis (and the store-load
  // forwarding) is unable to eliminate the allocations for the key-value
  // arrays in case of Set/Map entry iteration, and we should investigate
  // how to update the escape analysis / arrange the graph in a way that
  // this becomes possible.

  InstanceType receiver_instance_type;
  {
    MapInference inference(broker(), receiver, effect);
    if (!inference.HaveMaps()) return NoChange();
    ZoneRefSet<Map> const& receiver_maps = inference.GetMaps();
    receiver_instance_type = receiver_maps[0].instance_type();
    for (size_t i = 1; i < receiver_maps.size(); ++i) {
      if (receiver_maps[i].instance_type() != receiver_instance_type) {
        return inference.NoChange();
      }
    }
    if (receiver_instance_type < collection_iterator_instance_type_first ||
        receiver_instance_type > collection_iterator_instance_type_last) {
      return inference.NoChange();
    }
    inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                        control, p.feedback());
  }

  // Transition the JSCollectionIterator {receiver} if necessary
  // (i.e. there were certain mutations while we're iterating).
  {
    Node* done_loop;
    Node* done_eloop;
    Node* loop = control =
        graph()->NewNode(common()->Loop(2), control, control);
    Node* eloop = effect =
        graph()->NewNode(common()->EffectPhi(2), effect, effect, loop);
    Node* terminate = graph()->NewNode(common()->Terminate(), eloop, loop);
    MergeControlToEnd(graph(), common(), terminate);

    // Check if reached the final table of the {receiver}.
    Node* table = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSCollectionIteratorTable()),
        receiver, effect, control);
    Node* next_table = effect =
        graph()->NewNode(simplified()->LoadField(
                             AccessBuilder::ForOrderedHashMapOrSetNextTable()),
                         table, effect, control);
    Node* check = graph()->NewNode(simplified()->ObjectIsSmi(), next_table);
    control =
        graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

    // Abort the {loop} when we reach the final table.
    done_loop = graph()->NewNode(common()->IfTrue(), control);
    done_eloop = effect;

    // Migrate to the {next_table} otherwise.
    control = graph()->NewNode(common()->IfFalse(), control);

    // Self-heal the {receiver}s index.
    Node* index = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSCollectionIteratorIndex()),
        receiver, effect, control);
    Callable const callable =
        Builtins::CallableFor(isolate(), Builtin::kOrderedHashTableHealIndex);
    auto call_descriptor = Linkage::GetStubCallDescriptor(
        graph()->zone(), callable.descriptor(),
        callable.descriptor().GetStackParameterCount(),
        CallDescriptor::kNoFlags, Operator::kEliminatable);
    index = effect =
        graph()->NewNode(common()->Call(call_descriptor),
                         jsgraph()->HeapConstantNoHole(callable.code()), table,
                         index, jsgraph()->NoContextConstant(), effect);

    index = effect = graph()->NewNode(
        common()->TypeGuard(TypeCache::Get()->kFixedArrayLengthType), index,
        effect, control);

    // Update the {index} and {table} on the {receiver}.
    effect = graph()->NewNode(
        simplified()->StoreField(AccessBuilder::ForJSCollectionIteratorIndex()),
        receiver, index, effect, control);
    effect = graph()->NewNode(
        simplified()->StoreField(AccessBuilder::ForJSCollectionIteratorTable()),
        receiver, next_table, effect, control);

    // Tie the knot.
    loop->ReplaceInput(1, control);
    eloop->ReplaceInput(1, effect);

    control = done_loop;
    effect = done_eloop;
  }

  // Get current index and table from the JSCollectionIterator {receiver}.
  Node* index = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSCollectionIteratorIndex()),
      receiver, effect, control);
  Node* table = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSCollectionIteratorTable()),
      receiver, effect, control);

  // Create the {JSIteratorResult} first to ensure that we always have
  // a dominating Allocate node for the allocation folding phase.
  Node* iterator_result = effect = graph()->NewNode(
      javascript()->CreateIterResultObject(), jsgraph()->UndefinedConstant(),
      jsgraph()->TrueConstant(), context, effect);

  // Look for the next non-holey key, starting from {index} in the {table}.
  Node* controls[2];
  Node* effects[3];
  {
    // Compute the currently used capacity.
    Node* number_of_buckets = effect = graph()->NewNode(
        simplified()->LoadField(
            AccessBuilder::ForOrderedHashMapOrSetNumberOfBuckets()),
        table, effect, control);
    Node* number_of_elements = effect = graph()->NewNode(
        simplified()->LoadField(
            AccessBuilder::ForOrderedHashMapOrSetNumberOfElements()),
        table, effect, control);
    Node* number_of_deleted_elements = effect = graph()->NewNode(
        simplified()->LoadField(
            AccessBuilder::ForOrderedHashMapOrSetNumberOfDeletedElements()),
        table, effect, control);
    Node* used_capacity =
        graph()->NewNode(simplified()->NumberAdd(), number_of_elements,
                         number_of_deleted_elements);

    // Skip holes and update the {index}.
    Node* loop = graph()->NewNode(common()->Loop(2), control, control);
    Node* eloop =
        graph()->NewNode(common()->EffectPhi(2), effect, effect, loop);
    Node* terminate = graph()->NewNode(common()->Terminate(), eloop, loop);
    MergeControlToEnd(graph(), common(), terminate);
    Node* iloop = graph()->NewNode(
        common()->Phi(MachineRepresentation::kTagged, 2), index, index, loop);

    index = effect = graph()->NewNode(
        common()->TypeGuard(TypeCache::Get()->kFixedArrayLengthType), iloop,
        eloop, control);
    {
      Node* check0 = graph()->NewNode(simplified()->NumberLessThan(), index,
                                      used_capacity);
      Node* branch0 =
          graph()->NewNode(common()->Branch(BranchHint::kTrue), check0, loop);

      Node* if_false0 = graph()->NewNode(common()->IfFalse(), branch0);
      Node* efalse0 = effect;
      {
        // Mark the {receiver} as exhausted.
        efalse0 = graph()->NewNode(
            simplified()->StoreField(
                AccessBuilder::ForJSCollectionIteratorTable()),
            receiver, jsgraph()->HeapConstantNoHole(empty_collection), efalse0,
            if_false0);

        controls[0] = if_false0;
        effects[0] = efalse0;
      }

      Node* if_true0 = graph()->NewNode(common()->IfTrue(), branch0);
      Node* etrue0 = effect;
      {
        // Load the key of the entry.
        static_assert(OrderedHashMap::HashTableStartIndex() ==
                      OrderedHashSet::HashTableStartIndex());
        Node* entry_start_position = graph()->NewNode(
            simplified()->NumberAdd(),
            graph()->NewNode(
                simplified()->NumberAdd(),
                graph()->NewNode(simplified()->NumberMultiply(), index,
                                 jsgraph()->ConstantNoHole(entry_size)),
                number_of_buckets),
            jsgraph()->ConstantNoHole(OrderedHashMap::HashTableStartIndex()));
        Node* entry_key = etrue0 = graph()->NewNode(
            simplified()->LoadElement(AccessBuilder::ForFixedArrayElement()),
            table, entry_start_position, etrue0, if_true0);

        // Advance the index.
        index = graph()->NewNode(simplified()->NumberAdd(), index,
                                 jsgraph()->OneConstant());

        Node* check1 =
            graph()->NewNode(simplified()->ReferenceEqual(), entry_key,
                             jsgraph()->HashTableHoleConstant());
        Node* branch1 = graph()->NewNode(common()->Branch(BranchHint::kFalse),
                                         check1, if_true0);

        {
          // Abort loop with resulting value.
          control = graph()->NewNode(common()->IfFalse(), branch1);
          effect = etrue0;
          Node* value = effect =
              graph()->NewNode(common()->TypeGuard(Type::NonInternal()),
                               entry_key, effect, control);
          Node* done = jsgraph()->FalseConstant();

          // Advance the index on the {receiver}.
          effect = graph()->NewNode(
              simplified()->StoreField(
                  AccessBuilder::ForJSCollectionIteratorIndex()),
              receiver, index, effect, control);

          // The actual {value} depends on the {receiver} iteration type.
          switch (receiver_instance_type) {
            case JS_MAP_KEY_ITERATOR_TYPE:
            case JS_SET_VALUE_ITERATOR_TYPE:
              break;

            case JS_SET_KEY_VALUE_ITERATOR_TYPE:
              value = effect =
                  graph()->NewNode(javascript()->CreateKeyValueArray(), value,
                                   value, context, effect);
              break;

            case JS_MAP_VALUE_ITERATOR_TYPE:
              value = effect = graph()->NewNode(
                  simplified()->LoadElement(
                      AccessBuilder::ForFixedArrayElement()),
                  table,
                  graph()->NewNode(
                      simplified()->NumberAdd(), entry_start_position,
                      jsgraph()->ConstantNoHole(OrderedHashMap::kValueOffset)),
                  effect, control);
              break;

            case JS_MAP_KEY_VALUE_ITERATOR_TYPE:
              value = effect = graph()->NewNode(
                  simplified()->LoadElement(
                      AccessBuilder::ForFixedArrayElement()),
                  table,
                  graph()->NewNode(
                      simplified()->NumberAdd(), entry_start_position,
                      jsgraph()->ConstantNoHole(OrderedHashMap::kValueOffset)),
                  effect, control);
              value = effect =
                  graph()->NewNode(javascript()->CreateKeyValueArray(),
                                   entry_key, value, context, effect);
              break;

            default:
              UNREACHABLE();
          }

          // Store final {value} and {done} into the {iterator_result}.
          effect =
              graph()->NewNode(simplified()->StoreField(
                                   AccessBuilder::ForJSIteratorResultValue()),
                               iterator_result, value, effect, control);
          effect =
              graph()->NewNode(simplified()->StoreField(
                                   AccessBuilder::ForJSIteratorResultDone()),
                               iterator_result, done, effect, control);

          controls[1] = control;
          effects[1] = effect;
        }

        // Continue with next loop index.
        loop->ReplaceInput(1, graph()->NewNode(common()->IfTrue(), branch1));
        eloop->ReplaceInput(1, etrue0);
        iloop->ReplaceInput(1, index);
      }
    }

    control = effects[2] = graph()->NewNode(common()->Merge(2), 2, controls);
    effect = graph()->NewNode(common()->EffectPhi(2), 3, effects);
  }

  // Yield the final {iterator_result}.
  ReplaceWithValue(node, iterator_result, effect, control);
  return Replace(iterator_result);
}

Reduction JSCallReducer::ReduceArrayBufferIsView(Node* node) {
  JSCallNode n(node);
  Node* value = n.ArgumentOrUndefined(0, jsgraph());
  RelaxEffectsAndControls(node);
  node->ReplaceInput(0, value);
  node->TrimInputCount(1);
  NodeProperties::ChangeOp(node, simplified()->ObjectIsArrayBufferView());
  return Changed(node);
}

Reduction JSCallReducer::ReduceArrayBufferViewAccessor(
    Node* node, InstanceType instance_type, FieldAccess const& access,
    Builtin builtin) {
  // TODO(v8:11111): Optimize for JS_RAB_GSAB_DATA_VIEW_TYPE too.
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() ||
      !inference.AllOfInstanceTypesAre(instance_type)) {
    return inference.NoChange();
  }

  DCHECK_IMPLIES((builtin == Builtin::kTypedArrayPrototypeLength ||
                  builtin == Builtin::kTypedArrayPrototypeByteLength),
                 base::none_of(inference.GetMaps(), [](MapRef map) {
                   return IsRabGsabTypedArrayElementsKind(map.elements_kind());
                 }));

  if (!inference.RelyOnMapsViaStability(dependencies())) {
    return inference.NoChange();
  }

  const bool depended_on_detaching_protector =
      dependencies()->DependOnArrayBufferDetachingProtector();
  if (!depended_on_detaching_protector && instance_type == JS_DATA_VIEW_TYPE) {
    // DataView prototype accessors throw on detached ArrayBuffers instead of
    // return 0, so skip the optimization.
    //
    // TODO(turbofan): Ideally we would bail out if the buffer is actually
    // detached.
    return inference.NoChange();
  }

  // Load the {receiver}s field.
  Node* value = effect = graph()->NewNode(simplified()->LoadField(access),
                                          receiver, effect, control);

  // See if we can skip the detaching check.
  if (!depended_on_detaching_protector) {
    // Check whether {receiver}s JSArrayBuffer was detached.
    Node* buffer = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSArrayBufferViewBuffer()),
        receiver, effect, control);
    Node* buffer_bit_field = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSArrayBufferBitField()),
        buffer, effect, control);
    Node* check = graph()->NewNode(
        simplified()->NumberEqual(),
        graph()->NewNode(
            simplified()->NumberBitwiseAnd(), buffer_bit_field,
            jsgraph()->ConstantNoHole(JSArrayBuffer::WasDetachedBit::kMask)),
        jsgraph()->ZeroConstant());

    // TODO(turbofan): Ideally we would bail out here if the {receiver}s
    // JSArrayBuffer was detached, but there's no way to guard against
    // deoptimization loops right now, since the JSCall {node} is usually
    // created from a LOAD_IC inlining, and so there's no CALL_IC slot
    // from which we could use the speculation bit.
    value = graph()->NewNode(
        common()->Select(MachineRepresentation::kTagged, BranchHint::kTrue),
        check, value, jsgraph()->ZeroConstant());
  }

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

namespace {
uint32_t ExternalArrayElementSize(const ExternalArrayType element_type) {
  switch (element_type) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) \
  case kExternal##Type##Array:                    \
    DCHECK_LE(sizeof(ctype), 8);                  \
    return sizeof(ctype);
    TYPED_ARRAYS(TYPED_ARRAY_CASE)
    default:
      UNREACHABLE();
#undef TYPED_ARRAY_CASE
  }
}
}  // namespace

Reduction JSCallReducer::ReduceDataViewAccess(Node* node, DataViewAccess access,
                                              ExternalArrayType element_type) {
  // TODO(v8:11111): Optimize for JS_RAB_GSAB_DATA_VIEW_TYPE too.
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  size_t const element_size = ExternalArrayElementSize(element_type);
  Effect effect = n.effect();
  Control control = n.control();
  Node* receiver = n.receiver();
  Node* offset = n.ArgumentOr(0, jsgraph()->ZeroConstant());
  Node* value = nullptr;
  if (access == DataViewAccess::kSet) {
    value = n.ArgumentOrUndefined(1, jsgraph());
  }
  const int endian_index = (access == DataViewAccess::kGet ? 1 : 2);
  Node* is_little_endian =
      n.ArgumentOr(endian_index, jsgraph()->FalseConstant());

  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  // Only do stuff if the {receiver} is really a DataView.
  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() ||
      !inference.AllOfInstanceTypesAre(JS_DATA_VIEW_TYPE)) {
    return NoChange();
  }

  // Check that the {offset} is within range for the {receiver}.
  HeapObjectMatcher m(receiver);
  if (m.HasResolvedValue() && m.Ref(broker()).IsJSDataView()) {
    // We only deal with DataViews here whose [[ByteLength]] is at least
    // {element_size}, as for all other DataViews it'll be out-of-bounds.
    JSDataViewRef dataview = m.Ref(broker()).AsJSDataView();

    size_t length = dataview.byte_length();
    if (length < element_size) return NoChange();

    // Check that the {offset} is within range of the {length}.
    Node* byte_length = jsgraph()->ConstantNoHole(length - (element_size - 1));
    offset = effect = graph()->NewNode(simplified()->CheckBounds(p.feedback()),
                                       offset, byte_length, effect, control);
  } else {
    // We only deal with DataViews here that have Smi [[ByteLength]]s.
    Node* byte_length = effect =
        graph()->NewNode(simplified()->LoadField(
                             AccessBuilder::ForJSArrayBufferViewByteLength()),
                         receiver, effect, control);
    if (element_size > 1) {
      // For non-byte accesses we also need to check that the {offset}
      // plus the {element_size}-1 fits within the given {byte_length}.
      // So to keep this as a single check on the {offset}, we subtract
      // the {element_size}-1 from the {byte_length} here (clamped to
      // positive safe integer range), and perform a check against that
      // with the {offset} below.
      byte_length = graph()->NewNode(
          simplified()->NumberMax(), jsgraph()->ZeroConstant(),
          graph()->NewNode(simplified()->NumberSubtract(), byte_length,
                           jsgraph()->ConstantNoHole(element_size - 1)));
    }

    // Check that the {offset} is within range of the {byte_length}.
    offset = effect = graph()->NewNode(simplified()->CheckBounds(p.feedback()),
                                       offset, byte_length, effect, control);
  }

  // Coerce {is_little_endian} to boolean.
  is_little_endian =
      graph()->NewNode(simplified()->ToBoolean(), is_little_endian);

  // Coerce {value} to Number.
  if (access == DataViewAccess::kSet) {
    value = effect = graph()->NewNode(
        simplified()->SpeculativeToNumber(NumberOperationHint::kNumberOrOddball,
                                          p.feedback()),
        value, effect, control);
  }

  // We need to retain either the {receiver} itself or it's backing
  // JSArrayBuffer to make sure that the GC doesn't collect the raw
  // memory. We default to {receiver} here, and only use the buffer
  // if we anyways have to load it (to reduce register pressure).
  Node* buffer_or_receiver = receiver;

  if (!dependencies()->DependOnArrayBufferDetachingProtector()) {
    // Get the underlying buffer and check that it has not been detached.
    Node* buffer = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSArrayBufferViewBuffer()),
        receiver, effect, control);

    // Bail out if the {buffer} was detached.
    Node* buffer_bit_field = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSArrayBufferBitField()),
        buffer, effect, control);
    Node* check = graph()->NewNode(
        simplified()->NumberEqual(),
        graph()->NewNode(
            simplified()->NumberBitwiseAnd(), buffer_bit_field,
            jsgraph()->ConstantNoHole(JSArrayBuffer::WasDetachedBit::kMask)),
        jsgraph()->ZeroConstant());
    effect = graph()->NewNode(
        simplified()->CheckIf(DeoptimizeReason::kArrayBufferWasDetached,
                              p.feedback()),
        check, effect, control);

    // We can reduce register pressure by holding on to the {buffer}
    // now to retain the backing store memory.
    buffer_or_receiver = buffer;
  }

  // Load the {receiver}s data pointer.
  Node* data_pointer = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSDataViewDataPointer()),
      receiver, effect, control);

  switch (access) {
    case DataViewAccess::kGet:
      // Perform the load.
      value = effect = graph()->NewNode(
          simplified()->LoadDataViewElement(element_type), buffer_or_receiver,
          data_pointer, offset, is_little_endian, effect, control);
      break;
    case DataViewAccess::kSet:
      // Perform the store.
      effect = graph()->NewNode(
          simplified()->StoreDataViewElement(element_type), buffer_or_receiver,
          data_pointer, offset, value, is_little_endian, effect, control);
      value = jsgraph()->UndefinedConstant();
      break;
  }

  ReplaceWithValue(node, value, effect, control);
  return Changed(value);
}

// ES6 section 18.2.2 isFinite ( number )
Reduction JSCallReducer::ReduceGlobalIsFinite(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->FalseConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* input = n.Argument(0);

  input = effect =
      graph()->NewNode(simplified()->SpeculativeToNumber(
                           NumberOperationHint::kNumberOrOddball, p.feedback()),
                       input, effect, control);
  Node* value = graph()->NewNode(simplified()->NumberIsFinite(), input);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

// ES6 section 18.2.3 isNaN ( number )
Reduction JSCallReducer::ReduceGlobalIsNaN(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->TrueConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* input = n.Argument(0);

  input = effect =
      graph()->NewNode(simplified()->SpeculativeToNumber(
                           NumberOperationHint::kNumberOrOddball, p.feedback()),
                       input, effect, control);
  Node* value = graph()->NewNode(simplified()->NumberIsNaN(), input);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

// ES6 section 20.3.4.10 Date.prototype.getTime ( )
Reduction JSCallReducer::ReduceDatePrototypeGetTime(Node* node) {
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() || !inference.AllOfInstanceTypesAre(JS_DATE_TYPE)) {
    return NoChange();
  }

  Node* value = effect =
      graph()->NewNode(simplified()->LoadField(AccessBuilder::ForJSDateValue()),
                       receiver, effect, control);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES6 section 20.3.3.1 Date.now ( )
Reduction JSCallReducer::ReduceDateNow(Node* node) {
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* value = effect =
      graph()->NewNode(simplified()->DateNow(), effect, control);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

// ES6 section 20.1.2.13 Number.parseInt ( string, radix )
Reduction JSCallReducer::ReduceNumberParseInt(Node* node) {
  JSCallNode n(node);
  if (n.ArgumentCount() < 1) {
    Node* value = jsgraph()->NaNConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Node* object = n.Argument(0);
  Node* radix = n.ArgumentOrUndefined(1, jsgraph());

  // Try constant-folding when input is a string constant.
  HeapObjectMatcher object_matcher(object);
  HeapObjectMatcher radix_object_matcher(radix);
  NumberMatcher radix_number_matcher(radix);
  if (object_matcher.HasResolvedValue() &&
      object_matcher.Ref(broker()).IsString() &&
      (radix_object_matcher.Is(factory()->undefined_value()) ||
       radix_number_matcher.HasResolvedValue())) {
    StringRef input_value = object_matcher.Ref(broker()).AsString();
    // {undefined} is treated same as 0.
    int radix_value = radix_object_matcher.Is(factory()->undefined_value())
                          ? 0
                          : DoubleToInt32(radix_number_matcher.ResolvedValue());
    if (radix_value != 0 && (radix_value < 2 || radix_value > 36)) {
      Node* value = jsgraph()->NaNConstant();
      ReplaceWithValue(node, value);
      return Replace(value);
    }

    base::Optional<double> number = input_value.ToInt(broker(), radix_value);
    if (number.has_value()) {
      Node* result = graph()->NewNode(common()->NumberConstant(number.value()));
      ReplaceWithValue(node, result);
      return Replace(result);
    }
  }

  node->ReplaceInput(0, object);
  node->ReplaceInput(1, radix);
  node->ReplaceInput(2, context);
  node->ReplaceInput(3, frame_state);
  node->ReplaceInput(4, effect);
  node->ReplaceInput(5, control);
  node->TrimInputCount(6);
  NodeProperties::ChangeOp(node, javascript()->ParseInt());
  return Changed(node);
}

Reduction JSCallReducer::ReduceRegExpPrototypeTest(Node* node) {
  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (v8_flags.force_slow_path) return NoChange();
  if (n.ArgumentCount() < 1) return NoChange();

  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* regexp = n.receiver();

  // Only the initial JSRegExp map is valid here, since the following lastIndex
  // check as well as the lowered builtin call rely on a known location of the
  // lastIndex field.
  MapRef regexp_initial_map =
      native_context().regexp_function(broker()).initial_map(broker());

  MapInference inference(broker(), regexp, effect);
  if (!inference.Is(regexp_initial_map)) return inference.NoChange();
  ZoneRefSet<Map> const& regexp_maps = inference.GetMaps();

  ZoneVector<PropertyAccessInfo> access_infos(graph()->zone());
  AccessInfoFactory access_info_factory(broker(), graph()->zone());

  for (MapRef map : regexp_maps) {
    access_infos.push_back(broker()->GetPropertyAccessInfo(
        map, broker()->exec_string(), AccessMode::kLoad));
  }

  PropertyAccessInfo ai_exec =
      access_info_factory.FinalizePropertyAccessInfosAsOne(access_infos,
                                                           AccessMode::kLoad);
  if (ai_exec.IsInvalid()) return inference.NoChange();
  if (!ai_exec.IsFastDataConstant()) return inference.NoChange();

  // Do not reduce if the exec method is not on the prototype chain.
  OptionalJSObjectRef holder = ai_exec.holder();
  if (!holder.has_value()) return inference.NoChange();

  // Bail out if the exec method is not the original one.
  if (ai_exec.field_representation().IsDouble()) return inference.NoChange();
  OptionalObjectRef constant = holder->GetOwnFastConstantDataProperty(
      broker(), ai_exec.field_representation(), ai_exec.field_index(),
      dependencies());
  if (!constant.has_value() ||
      !constant->equals(native_context().regexp_exec_function(broker()))) {
    return inference.NoChange();
  }

  // Add proper dependencies on the {regexp}s [[Prototype]]s.
  dependencies()->DependOnStablePrototypeChains(
      ai_exec.lookup_start_object_maps(), kStartAtPrototype, holder.value());

  inference.RelyOnMapsPreferStability(dependencies(), jsgraph(), &effect,
                                      control, p.feedback());

  Node* context = n.context();
  FrameState frame_state = n.frame_state();
  Node* search = n.Argument(0);
  Node* search_string = effect = graph()->NewNode(
      simplified()->CheckString(p.feedback()), search, effect, control);

  Node* lastIndex = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSRegExpLastIndex()), regexp,
      effect, control);

  Node* lastIndexSmi = effect = graph()->NewNode(
      simplified()->CheckSmi(p.feedback()), lastIndex, effect, control);

  Node* is_positive = graph()->NewNode(simplified()->NumberLessThanOrEqual(),
                                       jsgraph()->ZeroConstant(), lastIndexSmi);

  effect = graph()->NewNode(
      simplified()->CheckIf(DeoptimizeReason::kNotASmi, p.feedback()),
      is_positive, effect, control);

  node->ReplaceInput(0, regexp);
  node->ReplaceInput(1, search_string);
  node->ReplaceInput(2, context);
  node->ReplaceInput(3, frame_state);
  node->ReplaceInput(4, effect);
  node->ReplaceInput(5, control);
  node->TrimInputCount(6);
  NodeProperties::ChangeOp(node, javascript()->RegExpTest());
  return Changed(node);
}

// ES section #sec-number-constructor
Reduction JSCallReducer::ReduceNumberConstructor(Node* node) {
  JSCallNode n(node);
  Node* target = n.target();
  Node* receiver = n.receiver();
  Node* value = n.ArgumentOr(0, jsgraph()->ZeroConstant());
  Node* context = n.context();
  FrameState frame_state = n.frame_state();

  // Create the artificial frame state in the middle of the Number constructor.
  SharedFunctionInfoRef shared_info =
      native_context().number_function(broker()).shared(broker());
  Node* continuation_frame_state = CreateGenericLazyDeoptContinuationFrameState(
      jsgraph(), shared_info, target, context, receiver, frame_state);

  // Convert the {value} to a Number.
  NodeProperties::ReplaceValueInputs(node, value);
  NodeProperties::ChangeOp(node, javascript()->ToNumberConvertBigInt());
  NodeProperties::ReplaceFrameStateInput(node, continuation_frame_state);
  return Changed(node);
}

// ES section #sec-bigint-constructor
Reduction JSCallReducer::ReduceBigIntConstructor(Node* node) {
  if (!jsgraph()->machine()->Is64()) return NoChange();

  JSCallNode n(node);
  if (n.ArgumentCount() < 1) {
    return NoChange();
  }

  Node* target = n.target();
  Node* receiver = n.receiver();
  Node* value = n.Argument(0);
  Node* context = n.context();
  FrameState frame_state = n.frame_state();

  // Create the artificial frame state in the middle of the BigInt constructor.
  SharedFunctionInfoRef shared_info =
      native_context().bigint_function(broker()).shared(broker());
  Node* continuation_frame_state = CreateGenericLazyDeoptContinuationFrameState(
      jsgraph(), shared_info, target, context, receiver, frame_state);

  // Convert the {value} to a BigInt.
  NodeProperties::ReplaceValueInputs(node, value);
  NodeProperties::ChangeOp(node, javascript()->ToBigIntConvertNumber());
  NodeProperties::ReplaceFrameStateInput(node, continuation_frame_state);
  return Changed(node);
}

Reduction JSCallReducer::ReduceBigIntAsN(Node* node, Builtin builtin) {
  DCHECK(builtin == Builtin::kBigIntAsIntN ||
         builtin == Builtin::kBigIntAsUintN);

  if (!jsgraph()->machine()->Is64()) return NoChange();

  JSCallNode n(node);
  CallParameters const& p = n.Parameters();
  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return NoChange();
  }
  if (n.ArgumentCount() < 2) {
    return NoChange();
  }

  Effect effect = n.effect();
  Control control = n.control();
  Node* bits = n.Argument(0);
  Node* value = n.Argument(1);

  NumberMatcher matcher(bits);
  if (matcher.IsInteger() && matcher.IsInRange(0, 64)) {
    const int bits_value = static_cast<int>(matcher.ResolvedValue());
    value = effect = graph()->NewNode(
        (builtin == Builtin::kBigIntAsIntN
             ? simplified()->SpeculativeBigIntAsIntN(bits_value, p.feedback())
             : simplified()->SpeculativeBigIntAsUintN(bits_value,
                                                      p.feedback())),
        value, effect, control);
    ReplaceWithValue(node, value, effect);
    return Replace(value);
  }

  return NoChange();
}

base::Optional<Reduction> JSCallReducer::TryReduceJSCallMathMinMaxWithArrayLike(
    Node* node) {
  if (!v8_flags.turbo_optimize_math_minmax) return base::nullopt;

  JSCallWithArrayLikeNode n(node);
  CallParameters const& p = n.Parameters();
  Node* target = n.target();
  Effect effect = n.effect();
  Control control = n.control();

  if (p.speculation_mode() == SpeculationMode::kDisallowSpeculation) {
    return base::nullopt;
  }

  if (n.ArgumentCount() != 1) {
    return base::nullopt;
  }

  if (!dependencies()->DependOnNoElementsProtector()) {
    return base::nullopt;
  }

  // These ops are handled by ReduceCallOrConstructWithArrayLikeOrSpread.
  // IrOpcode::kJSCreateEmptyLiteralArray is not included, since arguments_list
  // for Math.min/min is not likely to keep empty.
  Node* arguments_list = n.Argument(0);
  if (arguments_list->opcode() == IrOpcode::kJSCreateLiteralArray ||
      arguments_list->opcode() == IrOpcode::kJSCreateArguments) {
    return base::nullopt;
  }

  HeapObjectMatcher m(target);
  if (m.HasResolvedValue()) {
    ObjectRef target_ref = m.Ref(broker());
    if (target_ref.IsJSFunction()) {
      JSFunctionRef function = target_ref.AsJSFunction();

      // Don't inline cross native context.
      if (!function.native_context(broker()).equals(native_context())) {
        return base::nullopt;
      }

      SharedFunctionInfoRef shared = function.shared(broker());
      Builtin builtin =
          shared.HasBuiltinId() ? shared.builtin_id() : Builtin::kNoBuiltinId;
      if (builtin == Builtin::kMathMax || builtin == Builtin::kMathMin) {
        return ReduceJSCallMathMinMaxWithArrayLike(node, builtin);
      } else {
        return base::nullopt;
      }
    }
  }

  // Try specialize the JSCallWithArrayLike node with feedback target.
  if (ShouldUseCallICFeedback(target) &&
      p.feedback_relation() == CallFeedbackRelation::kTarget &&
      p.feedback().IsValid()) {
    ProcessedFeedback const& feedback =
        broker()->GetFeedbackForCall(p.feedback());
    if (feedback.IsInsufficient()) {
      return base::nullopt;
    }
    OptionalHeapObjectRef feedback_target = feedback.AsCall().target();
    if (feedback_target.has_value() &&
        feedback_target->map(broker()).is_callable()) {
      Node* target_function =
          jsgraph()->ConstantNoHole(*feedback_target, broker());
      ObjectRef target_ref = feedback_target.value();
      if (!target_ref.IsJSFunction()) {
        return base::nullopt;
      }
      JSFunctionRef function = target_ref.AsJSFunction();
      SharedFunctionInfoRef shared = function.shared(broker());
      Builtin builtin =
          shared.HasBuiltinId() ? shared.builtin_id() : Builtin::kNoBuiltinId;
      if (builtin == Builtin::kMathMax || builtin == Builtin::kMathMin) {
        // Check that the {target} is still the {target_function}.
        Node* check = graph()->NewNode(simplified()->ReferenceEqual(), target,
                                       target_function);
        effect = graph()->NewNode(
            simplified()->CheckIf(DeoptimizeReason::kWrongCallTarget), check,
            effect, control);

        // Specialize the JSCallWithArrayLike node to the {target_function}.
        NodeProperties::ReplaceValueInput(node, target_function,
                                          n.TargetIndex());
        NodeProperties::ReplaceEffectInput(node, effect);
        // Try to further reduce the Call MathMin/Max with double array.
        return Changed(node).FollowedBy(
            ReduceJSCallMathMinMaxWithArrayLike(node, builtin));
      }
    }
  }

  return base::nullopt;
}

Reduction JSCallReducer::ReduceJSCallMathMinMaxWithArrayLike(Node* node,
                                                             Builtin builtin) {
  JSCallWithArrayLikeNode n(node);
  DCHECK_NE(n.Parameters().speculation_mode(),
            SpeculationMode::kDisallowSpeculation);
  DCHECK_EQ(n.ArgumentCount(), 1);

  JSCallReducerAssembler a(this, node);
  Node* subgraph = a.ReduceJSCallMathMinMaxWithArrayLike(builtin);
  return ReplaceWithSubgraph(&a, subgraph);
}

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
Reduction JSCallReducer::ReduceGetContinuationPreservedEmbedderData(
    Node* node) {
  JSCallNode n(node);
  Effect effect = n.effect();
  Control control = n.control();

  Node* value = effect = graph()->NewNode(
      simplified()->GetContinuationPreservedEmbedderData(), effect);

  ReplaceWithValue(node, value, effect, control);
  return Replace(node);
}

Reduction JSCallReducer::ReduceSetContinuationPreservedEmbedderData(
    Node* node) {
  JSCallNode n(node);
  Effect effect = n.effect();
  Control control = n.control();

  if (n.ArgumentCount() == 0) return NoChange();

  effect =
      graph()->NewNode(simplified()->SetContinuationPreservedEmbedderData(),
                       n.Argument(0), effect);

  Node* value = jsgraph()->UndefinedConstant();

  ReplaceWithValue(node, value, effect, control);
  return Replace(node);
}
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

CompilationDependencies* JSCallReducer::dependencies() const {
  return broker()->dependencies();
}

Graph* JSCallReducer::graph() const { return jsgraph()->graph(); }

Isolate* JSCallReducer::isolate() const { return jsgraph()->isolate(); }

Factory* JSCallReducer::factory() const { return isolate()->factory(); }

NativeContextRef JSCallReducer::native_context() const {
  return broker()->target_native_context();
}

CommonOperatorBuilder* JSCallReducer::common() const {
  return jsgraph()->common();
}

JSOperatorBuilder* JSCallReducer::javascript() const {
  return jsgraph()->javascript();
}

SimplifiedOperatorBuilder* JSCallReducer::simplified() const {
  return jsgraph()->simplified();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
