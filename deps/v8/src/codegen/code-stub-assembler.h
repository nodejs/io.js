// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_CODE_STUB_ASSEMBLER_H_
#define V8_CODEGEN_CODE_STUB_ASSEMBLER_H_

#include <functional>
#include <optional>

#include "src/base/macros.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/heap-object-list.h"
#include "src/codegen/tnode.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/compiler/code-assembler.h"
#include "src/numbers/integer-literal.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/arguments.h"
#include "src/objects/bigint.h"
#include "src/objects/cell.h"
#include "src/objects/dictionary.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/foreign.h"
#include "src/objects/heap-number.h"
#include "src/objects/hole.h"
#include "src/objects/js-function.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-promise.h"
#include "src/objects/js-proxy.h"
#include "src/objects/objects.h"
#include "src/objects/oddball.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/smi.h"
#include "src/objects/string.h"
#include "src/objects/swiss-name-dictionary.h"
#include "src/objects/tagged-index.h"
#include "src/objects/tagged.h"
#include "src/objects/templates.h"
#include "src/roots/roots.h"
#include "torque-generated/exported-macros-assembler.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

class CallInterfaceDescriptor;
class CodeStubArguments;
class CodeStubAssembler;
class StatsCounter;
class StubCache;

enum class PrimitiveType { kBoolean, kNumber, kString, kSymbol };

#ifdef DEBUG
#define CSA_CHECK(csa, x) \
  (csa)->Check([&]() -> TNode<BoolT> { return x; }, #x, __FILE__, __LINE__)
#else
#define CSA_CHECK(csa, x) (csa)->FastCheck(x)
#endif

#define CSA_CHECK_WITH_ABORT(csa, x) \
  (csa)->Check([&]() -> TNode<BoolT> { return x; }, #x, __FILE__, __LINE__)

// This is a check that always calls into the runtime if it aborts.
// This also exits silently when --hole-fuzzing is enabled.
#define CSA_HOLE_SECURITY_CHECK(csa, x) CSA_CHECK_WITH_ABORT(csa, x)

#ifdef DEBUG
// CSA_DCHECK_ARGS generates an
// std::initializer_list<CodeStubAssembler::ExtraNode> from __VA_ARGS__. It
// currently supports between 0 and 2 arguments.

// clang-format off
#define CSA_DCHECK_0_ARGS(...) {}
#define CSA_DCHECK_1_ARG(a, ...) {{a, #a}}
#define CSA_DCHECK_2_ARGS(a, b, ...) {{a, #a}, {b, #b}}
// clang-format on
#define SWITCH_CSA_DCHECK_ARGS(dummy, a, b, FUNC, ...) FUNC(a, b)
#define CSA_DCHECK_ARGS(...)                                        \
  CALL(SWITCH_CSA_DCHECK_ARGS, (, ##__VA_ARGS__, CSA_DCHECK_2_ARGS, \
                                CSA_DCHECK_1_ARG, CSA_DCHECK_0_ARGS))
// Workaround for MSVC to skip comma in empty __VA_ARGS__.
#define CALL(x, y) x y

// CSA_DCHECK(csa, <condition>, <extra values to print...>)

#define CSA_DCHECK(csa, condition_node, ...)                         \
  (csa)->Dcheck(condition_node, #condition_node, __FILE__, __LINE__, \
                CSA_DCHECK_ARGS(__VA_ARGS__))

// CSA_DCHECK_BRANCH(csa, [](Label* ok, Label* not_ok) {...},
//     <extra values to print...>)

#define CSA_DCHECK_BRANCH(csa, gen, ...) \
  (csa)->Dcheck(gen, #gen, __FILE__, __LINE__, CSA_DCHECK_ARGS(__VA_ARGS__))

#define CSA_DCHECK_JS_ARGC_OP(csa, Op, op, expected)                           \
  (csa)->Dcheck(                                                               \
      [&]() -> TNode<BoolT> {                                                  \
        const TNode<Word32T> argc = (csa)->UncheckedParameter<Word32T>(        \
            Descriptor::kJSActualArgumentsCount);                              \
        return (csa)->Op(argc,                                                 \
                         (csa)->Int32Constant(i::JSParameterCount(expected))); \
      },                                                                       \
      "argc " #op " " #expected, __FILE__, __LINE__,                           \
      {{SmiFromInt32((csa)->UncheckedParameter<Int32T>(                        \
            Descriptor::kJSActualArgumentsCount)),                             \
        "argc"}})

#define CSA_DCHECK_JS_ARGC_EQ(csa, expected) \
  CSA_DCHECK_JS_ARGC_OP(csa, Word32Equal, ==, expected)

#define CSA_DEBUG_INFO(name) \
  { #name, __FILE__, __LINE__ }
#define BIND(label) Bind(label, CSA_DEBUG_INFO(label))
#define TYPED_VARIABLE_DEF(type, name, ...) \
  TVariable<type> name(CSA_DEBUG_INFO(name), __VA_ARGS__)
#define TYPED_VARIABLE_CONSTRUCTOR(name, ...) \
  name(CSA_DEBUG_INFO(name), __VA_ARGS__)
#else  // DEBUG
#define CSA_DCHECK(csa, ...) ((void)0)
#define CSA_DCHECK_BRANCH(csa, ...) ((void)0)
#define CSA_DCHECK_JS_ARGC_EQ(csa, expected) ((void)0)
#define BIND(label) Bind(label)
#define TYPED_VARIABLE_DEF(type, name, ...) TVariable<type> name(__VA_ARGS__)
#define TYPED_VARIABLE_CONSTRUCTOR(name, ...) name(__VA_ARGS__)
#endif  // DEBUG

#define TVARIABLE(...) EXPAND(TYPED_VARIABLE_DEF(__VA_ARGS__, this))
#define TVARIABLE_CONSTRUCTOR(...) \
  EXPAND(TYPED_VARIABLE_CONSTRUCTOR(__VA_ARGS__, this))

#ifdef ENABLE_SLOW_DCHECKS
#define CSA_SLOW_DCHECK(csa, ...)     \
  if (v8_flags.enable_slow_asserts) { \
    CSA_DCHECK(csa, __VA_ARGS__);     \
  }
#else
#define CSA_SLOW_DCHECK(csa, ...) ((void)0)
#endif

// Similar to SBXCHECK in C++, these become a CSA_CHECK in sandbox-enabled
// builds, otherwise a CSA_DCHECK.
#ifdef V8_ENABLE_SANDBOX
#define CSA_SBXCHECK(csa, ...) CSA_CHECK(csa, __VA_ARGS__)
#else
#define CSA_SBXCHECK(csa, ...) CSA_DCHECK(csa, __VA_ARGS__)
#endif

// Provides JavaScript-specific "macro-assembler" functionality on top of the
// CodeAssembler. By factoring the JavaScript-isms out of the CodeAssembler,
// it's possible to add JavaScript-specific useful CodeAssembler "macros"
// without modifying files in the compiler directory (and requiring a review
// from a compiler directory OWNER).
class V8_EXPORT_PRIVATE CodeStubAssembler
    : public compiler::CodeAssembler,
      public TorqueGeneratedExportedMacrosAssembler {
 public:
  using ScopedExceptionHandler = compiler::ScopedExceptionHandler;

  template <typename T>
  using LazyNode = std::function<TNode<T>()>;

  explicit CodeStubAssembler(compiler::CodeAssemblerState* state);

  enum class AllocationFlag : uint8_t {
    kNone = 0,
    kDoubleAlignment = 1,
    kPretenured = 1 << 1
  };

  enum SlackTrackingMode {
    kWithSlackTracking,
    kNoSlackTracking,
    kDontInitializeInObjectProperties,
  };

  using AllocationFlags = base::Flags<AllocationFlag>;

  TNode<IntPtrT> ParameterToIntPtr(TNode<Smi> value) { return SmiUntag(value); }
  TNode<IntPtrT> ParameterToIntPtr(TNode<IntPtrT> value) { return value; }
  TNode<IntPtrT> ParameterToIntPtr(TNode<UintPtrT> value) {
    return Signed(value);
  }
  TNode<IntPtrT> ParameterToIntPtr(TNode<TaggedIndex> value) {
    return TaggedIndexToIntPtr(value);
  }

  TNode<Smi> ParameterToTagged(TNode<Smi> value) { return value; }

  TNode<Smi> ParameterToTagged(TNode<IntPtrT> value) { return SmiTag(value); }

  template <typename TIndex>
  TNode<TIndex> TaggedToParameter(TNode<Smi> value);

  bool ToParameterConstant(TNode<Smi> node, intptr_t* out) {
    if (TryToIntPtrConstant(node, out)) {
      return true;
    }
    return false;
  }

  bool ToParameterConstant(TNode<IntPtrT> node, intptr_t* out) {
    intptr_t constant;
    if (TryToIntPtrConstant(node, &constant)) {
      *out = constant;
      return true;
    }
    return false;
  }

#if defined(BINT_IS_SMI)
  TNode<Smi> BIntToSmi(TNode<BInt> source) { return source; }
  TNode<IntPtrT> BIntToIntPtr(TNode<BInt> source) {
    return SmiToIntPtr(source);
  }
  TNode<BInt> SmiToBInt(TNode<Smi> source) { return source; }
  TNode<BInt> IntPtrToBInt(TNode<IntPtrT> source) {
    return SmiFromIntPtr(source);
  }
#elif defined(BINT_IS_INTPTR)
  TNode<Smi> BIntToSmi(TNode<BInt> source) { return SmiFromIntPtr(source); }
  TNode<IntPtrT> BIntToIntPtr(TNode<BInt> source) { return source; }
  TNode<BInt> SmiToBInt(TNode<Smi> source) { return SmiToIntPtr(source); }
  TNode<BInt> IntPtrToBInt(TNode<IntPtrT> source) { return source; }
#else
#error Unknown architecture.
#endif

  TNode<IntPtrT> TaggedIndexToIntPtr(TNode<TaggedIndex> value);
  TNode<TaggedIndex> IntPtrToTaggedIndex(TNode<IntPtrT> value);
  // TODO(v8:10047): Get rid of these convertions eventually.
  TNode<Smi> TaggedIndexToSmi(TNode<TaggedIndex> value);
  TNode<TaggedIndex> SmiToTaggedIndex(TNode<Smi> value);

  // Pointer compression specific. Ensures that the upper 32 bits of a Smi
  // contain the sign of a lower 32 bits so that the Smi can be directly used
  // as an index in element offset computation.
  TNode<Smi> NormalizeSmiIndex(TNode<Smi> smi_index);

  TNode<Smi> TaggedToSmi(TNode<Object> value, Label* fail) {
    GotoIf(TaggedIsNotSmi(value), fail);
    return UncheckedCast<Smi>(value);
  }

  TNode<Smi> TaggedToPositiveSmi(TNode<Object> value, Label* fail) {
    GotoIfNot(TaggedIsPositiveSmi(value), fail);
    return UncheckedCast<Smi>(value);
  }

  TNode<String> TaggedToDirectString(TNode<Object> value, Label* fail);

  TNode<HeapObject> TaggedToHeapObject(TNode<Object> value, Label* fail) {
    GotoIf(TaggedIsSmi(value), fail);
    return UncheckedCast<HeapObject>(value);
  }

  TNode<Uint16T> Uint16Constant(uint16_t t) {
    return UncheckedCast<Uint16T>(Int32Constant(t));
  }

  TNode<JSDataView> HeapObjectToJSDataView(TNode<HeapObject> heap_object,
                                           Label* fail) {
    GotoIfNot(IsJSDataView(heap_object), fail);
    return CAST(heap_object);
  }

  TNode<JSProxy> HeapObjectToJSProxy(TNode<HeapObject> heap_object,
                                     Label* fail) {
    GotoIfNot(IsJSProxy(heap_object), fail);
    return CAST(heap_object);
  }

  TNode<JSStringIterator> HeapObjectToJSStringIterator(
      TNode<HeapObject> heap_object, Label* fail) {
    GotoIfNot(IsJSStringIterator(heap_object), fail);
    return CAST(heap_object);
  }

  TNode<JSReceiver> HeapObjectToCallable(TNode<HeapObject> heap_object,
                                         Label* fail) {
    GotoIfNot(IsCallable(heap_object), fail);
    return CAST(heap_object);
  }

  TNode<String> HeapObjectToString(TNode<HeapObject> heap_object, Label* fail) {
    GotoIfNot(IsString(heap_object), fail);
    return CAST(heap_object);
  }

  TNode<JSReceiver> HeapObjectToConstructor(TNode<HeapObject> heap_object,
                                            Label* fail) {
    GotoIfNot(IsConstructor(heap_object), fail);
    return CAST(heap_object);
  }

  TNode<JSFunction> HeapObjectToJSFunctionWithPrototypeSlot(
      TNode<HeapObject> heap_object, Label* fail) {
    GotoIfNot(IsJSFunctionWithPrototypeSlot(heap_object), fail);
    return CAST(heap_object);
  }

  template <typename T>
  TNode<T> RunLazy(LazyNode<T> lazy) {
    return lazy();
  }

#define PARAMETER_BINOP(OpName, IntPtrOpName, SmiOpName)                    \
  TNode<Smi> OpName(TNode<Smi> a, TNode<Smi> b) { return SmiOpName(a, b); } \
  TNode<IntPtrT> OpName(TNode<IntPtrT> a, TNode<IntPtrT> b) {               \
    return IntPtrOpName(a, b);                                              \
  }                                                                         \
  TNode<UintPtrT> OpName(TNode<UintPtrT> a, TNode<UintPtrT> b) {            \
    return Unsigned(IntPtrOpName(Signed(a), Signed(b)));                    \
  }                                                                         \
  TNode<RawPtrT> OpName(TNode<RawPtrT> a, TNode<RawPtrT> b) {               \
    return ReinterpretCast<RawPtrT>(IntPtrOpName(                           \
        ReinterpretCast<IntPtrT>(a), ReinterpretCast<IntPtrT>(b)));         \
  }
  // TODO(v8:9708): Define BInt operations once all uses are ported.
  PARAMETER_BINOP(IntPtrOrSmiAdd, IntPtrAdd, SmiAdd)
  PARAMETER_BINOP(IntPtrOrSmiSub, IntPtrSub, SmiSub)
#undef PARAMETER_BINOP

#define PARAMETER_BINOP(OpName, IntPtrOpName, SmiOpName)                       \
  TNode<BoolT> OpName(TNode<Smi> a, TNode<Smi> b) { return SmiOpName(a, b); }  \
  TNode<BoolT> OpName(TNode<IntPtrT> a, TNode<IntPtrT> b) {                    \
    return IntPtrOpName(a, b);                                                 \
  }                                                                            \
  /* IntPtrXXX comparisons shouldn't be used with unsigned types, use       */ \
  /* UintPtrXXX operations explicitly instead.                              */ \
  TNode<BoolT> OpName(TNode<UintPtrT> a, TNode<UintPtrT> b) { UNREACHABLE(); } \
  TNode<BoolT> OpName(TNode<RawPtrT> a, TNode<RawPtrT> b) { UNREACHABLE(); }
  // TODO(v8:9708): Define BInt operations once all uses are ported.
  PARAMETER_BINOP(IntPtrOrSmiEqual, WordEqual, SmiEqual)
  PARAMETER_BINOP(IntPtrOrSmiNotEqual, WordNotEqual, SmiNotEqual)
  PARAMETER_BINOP(IntPtrOrSmiLessThan, IntPtrLessThan, SmiLessThan)
  PARAMETER_BINOP(IntPtrOrSmiLessThanOrEqual, IntPtrLessThanOrEqual,
                  SmiLessThanOrEqual)
  PARAMETER_BINOP(IntPtrOrSmiGreaterThan, IntPtrGreaterThan, SmiGreaterThan)
#undef PARAMETER_BINOP

#define PARAMETER_BINOP(OpName, UintPtrOpName, SmiOpName)                     \
  TNode<BoolT> OpName(TNode<Smi> a, TNode<Smi> b) { return SmiOpName(a, b); } \
  TNode<BoolT> OpName(TNode<IntPtrT> a, TNode<IntPtrT> b) {                   \
    return UintPtrOpName(Unsigned(a), Unsigned(b));                           \
  }                                                                           \
  TNode<BoolT> OpName(TNode<UintPtrT> a, TNode<UintPtrT> b) {                 \
    return UintPtrOpName(a, b);                                               \
  }                                                                           \
  TNode<BoolT> OpName(TNode<RawPtrT> a, TNode<RawPtrT> b) {                   \
    return UintPtrOpName(a, b);                                               \
  }
  // TODO(v8:9708): Define BInt operations once all uses are ported.
  PARAMETER_BINOP(UintPtrOrSmiEqual, WordEqual, SmiEqual)
  PARAMETER_BINOP(UintPtrOrSmiNotEqual, WordNotEqual, SmiNotEqual)
  PARAMETER_BINOP(UintPtrOrSmiLessThan, UintPtrLessThan, SmiBelow)
  PARAMETER_BINOP(UintPtrOrSmiLessThanOrEqual, UintPtrLessThanOrEqual,
                  SmiBelowOrEqual)
  PARAMETER_BINOP(UintPtrOrSmiGreaterThan, UintPtrGreaterThan, SmiAbove)
  PARAMETER_BINOP(UintPtrOrSmiGreaterThanOrEqual, UintPtrGreaterThanOrEqual,
                  SmiAboveOrEqual)
#undef PARAMETER_BINOP

  uintptr_t ConstexprUintPtrShl(uintptr_t a, int32_t b) { return a << b; }
  uintptr_t ConstexprUintPtrShr(uintptr_t a, int32_t b) { return a >> b; }
  intptr_t ConstexprIntPtrAdd(intptr_t a, intptr_t b) { return a + b; }
  uintptr_t ConstexprUintPtrAdd(uintptr_t a, uintptr_t b) { return a + b; }
  intptr_t ConstexprWordNot(intptr_t a) { return ~a; }
  uintptr_t ConstexprWordNot(uintptr_t a) { return ~a; }

  TNode<BoolT> TaggedEqual(TNode<AnyTaggedT> a, TNode<AnyTaggedT> b) {
    if (COMPRESS_POINTERS_BOOL) {
      return Word32Equal(ReinterpretCast<Word32T>(a),
                         ReinterpretCast<Word32T>(b));
    } else {
      return WordEqual(ReinterpretCast<WordT>(a), ReinterpretCast<WordT>(b));
    }
  }

  TNode<BoolT> TaggedNotEqual(TNode<AnyTaggedT> a, TNode<AnyTaggedT> b) {
    return Word32BinaryNot(TaggedEqual(a, b));
  }

  TNode<Smi> NoContextConstant();

  TNode<IntPtrT> StackAlignmentInBytes() {
    // This node makes the graph platform-specific. To make sure that the graph
    // structure is still platform independent, UniqueIntPtrConstants are used
    // here.
#if V8_TARGET_ARCH_ARM64
    return UniqueIntPtrConstant(16);
#else
    return UniqueIntPtrConstant(kSystemPointerSize);
#endif
  }

#define HEAP_CONSTANT_ACCESSOR(rootIndexName, rootAccessorName, name)    \
  TNode<RemoveTagged<                                                    \
      decltype(std::declval<ReadOnlyRoots>().rootAccessorName())>::type> \
      name##Constant();
  HEAP_IMMUTABLE_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_ACCESSOR)
#undef HEAP_CONSTANT_ACCESSOR

#define HEAP_CONSTANT_ACCESSOR(rootIndexName, rootAccessorName, name)          \
  TNode<RemoveTagged<decltype(std::declval<Heap>().rootAccessorName())>::type> \
      name##Constant();
  HEAP_MUTABLE_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_ACCESSOR)
#undef HEAP_CONSTANT_ACCESSOR

#define HEAP_CONSTANT_TEST(rootIndexName, rootAccessorName, name) \
  TNode<BoolT> Is##name(TNode<Object> value);                     \
  TNode<BoolT> IsNot##name(TNode<Object> value);
  HEAP_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_TEST)
#undef HEAP_CONSTANT_TEST

  TNode<BInt> BIntConstant(int value);

  template <typename TIndex>
  TNode<TIndex> IntPtrOrSmiConstant(int value);

  bool TryGetIntPtrOrSmiConstantValue(TNode<Smi> maybe_constant, int* value);
  bool TryGetIntPtrOrSmiConstantValue(TNode<IntPtrT> maybe_constant,
                                      int* value);

  TNode<IntPtrT> PopulationCountFallback(TNode<UintPtrT> value);
  TNode<Int64T> PopulationCount64(TNode<Word64T> value);
  TNode<Int32T> PopulationCount32(TNode<Word32T> value);
  TNode<Int64T> CountTrailingZeros64(TNode<Word64T> value);
  TNode<Int32T> CountTrailingZeros32(TNode<Word32T> value);
  TNode<Int64T> CountLeadingZeros64(TNode<Word64T> value);
  TNode<Int32T> CountLeadingZeros32(TNode<Word32T> value);

  // Round the 32bits payload of the provided word up to the next power of two.
  TNode<IntPtrT> IntPtrRoundUpToPowerOfTwo32(TNode<IntPtrT> value);
  // Select the maximum of the two provided IntPtr values.
  TNode<IntPtrT> IntPtrMax(TNode<IntPtrT> left, TNode<IntPtrT> right);
  // Select the minimum of the two provided IntPtr values.
  TNode<IntPtrT> IntPtrMin(TNode<IntPtrT> left, TNode<IntPtrT> right);
  TNode<UintPtrT> UintPtrMin(TNode<UintPtrT> left, TNode<UintPtrT> right);

  // Float64 operations.
  TNode<BoolT> Float64AlmostEqual(TNode<Float64T> x, TNode<Float64T> y,
                                  double max_relative_error = 0.0000001);
  TNode<Float64T> Float64Ceil(TNode<Float64T> x);
  TNode<Float64T> Float64Floor(TNode<Float64T> x);
  TNode<Float64T> Float64Round(TNode<Float64T> x);
  TNode<Float64T> Float64RoundToEven(TNode<Float64T> x);
  TNode<Float64T> Float64Trunc(TNode<Float64T> x);
  // Select the minimum of the two provided Number values.
  TNode<Number> NumberMax(TNode<Number> left, TNode<Number> right);
  // Select the minimum of the two provided Number values.
  TNode<Number> NumberMin(TNode<Number> left, TNode<Number> right);

  // Returns true iff the given value fits into smi range and is >= 0.
  TNode<BoolT> IsValidPositiveSmi(TNode<IntPtrT> value);

  // Tag an IntPtr as a Smi value.
  TNode<Smi> SmiTag(TNode<IntPtrT> value);
  // Untag a Smi value as an IntPtr.
  TNode<IntPtrT> SmiUntag(TNode<Smi> value);
  // Untag a positive Smi value as an IntPtr, it's slightly better than
  // SmiUntag() because it doesn't have to do sign extension.
  TNode<IntPtrT> PositiveSmiUntag(TNode<Smi> value);

  // Smi conversions.
  TNode<Float64T> SmiToFloat64(TNode<Smi> value);
  TNode<Smi> SmiFromIntPtr(TNode<IntPtrT> value) { return SmiTag(value); }
  TNode<Smi> SmiFromInt32(TNode<Int32T> value);
  TNode<Smi> SmiFromUint32(TNode<Uint32T> value);
  TNode<IntPtrT> SmiToIntPtr(TNode<Smi> value) { return SmiUntag(value); }
  TNode<Int32T> SmiToInt32(TNode<Smi> value);
  TNode<Uint32T> PositiveSmiToUint32(TNode<Smi> value);

  // Smi operations.
#define SMI_ARITHMETIC_BINOP(SmiOpName, IntPtrOpName, Int32OpName)          \
  TNode<Smi> SmiOpName(TNode<Smi> a, TNode<Smi> b) {                        \
    if (SmiValuesAre32Bits()) {                                             \
      return BitcastWordToTaggedSigned(                                     \
          IntPtrOpName(BitcastTaggedToWordForTagAndSmiBits(a),              \
                       BitcastTaggedToWordForTagAndSmiBits(b)));            \
    } else {                                                                \
      DCHECK(SmiValuesAre31Bits());                                         \
      return BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(Int32OpName(     \
          TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(a)),    \
          TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(b))))); \
    }                                                                       \
  }
  SMI_ARITHMETIC_BINOP(SmiAdd, IntPtrAdd, Int32Add)
  SMI_ARITHMETIC_BINOP(SmiSub, IntPtrSub, Int32Sub)
  SMI_ARITHMETIC_BINOP(SmiAnd, WordAnd, Word32And)
  SMI_ARITHMETIC_BINOP(SmiOr, WordOr, Word32Or)
  SMI_ARITHMETIC_BINOP(SmiXor, WordXor, Word32Xor)
#undef SMI_ARITHMETIC_BINOP

  TNode<IntPtrT> TryIntPtrAdd(TNode<IntPtrT> a, TNode<IntPtrT> b,
                              Label* if_overflow);
  TNode<IntPtrT> TryIntPtrSub(TNode<IntPtrT> a, TNode<IntPtrT> b,
                              Label* if_overflow);
  TNode<IntPtrT> TryIntPtrMul(TNode<IntPtrT> a, TNode<IntPtrT> b,
                              Label* if_overflow);
  TNode<IntPtrT> TryIntPtrDiv(TNode<IntPtrT> a, TNode<IntPtrT> b,
                              Label* if_div_zero);
  TNode<IntPtrT> TryIntPtrMod(TNode<IntPtrT> a, TNode<IntPtrT> b,
                              Label* if_div_zero);
  TNode<Int32T> TryInt32Mul(TNode<Int32T> a, TNode<Int32T> b,
                            Label* if_overflow);
  TNode<Smi> TrySmiAdd(TNode<Smi> a, TNode<Smi> b, Label* if_overflow);
  TNode<Smi> TrySmiSub(TNode<Smi> a, TNode<Smi> b, Label* if_overflow);
  TNode<Smi> TrySmiAbs(TNode<Smi> a, Label* if_overflow);

  TNode<Smi> UnsignedSmiShl(TNode<Smi> a, int shift) {
    if (SmiValuesAre32Bits()) {
      return BitcastWordToTaggedSigned(
          WordShl(BitcastTaggedToWordForTagAndSmiBits(a), shift));
    } else {
      DCHECK(SmiValuesAre31Bits());
      return BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(Word32Shl(
          TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(a)),
          Int32Constant(shift))));
    }
  }

  TNode<Smi> SmiShl(TNode<Smi> a, int shift) {
    TNode<Smi> result = UnsignedSmiShl(a, shift);
    // Smi shift have different result to int32 shift when the inputs are not
    // strictly limited. The CSA_DCHECK is to ensure valid inputs.
    CSA_DCHECK(
        this, TaggedEqual(result, BitwiseOp(SmiToInt32(a), Int32Constant(shift),
                                            Operation::kShiftLeft)));
    return result;
  }

  TNode<Smi> SmiShr(TNode<Smi> a, int shift) {
    TNode<Smi> result;
    if (kTaggedSize == kInt64Size) {
      result = BitcastWordToTaggedSigned(
          WordAnd(WordShr(BitcastTaggedToWordForTagAndSmiBits(a), shift),
                  BitcastTaggedToWordForTagAndSmiBits(SmiConstant(-1))));
    } else {
      // For pointer compressed Smis, we want to make sure that we truncate to
      // int32 before shifting, to avoid the values of the top 32-bits from
      // leaking into the sign bit of the smi.
      result = BitcastWordToTaggedSigned(WordAnd(
          ChangeInt32ToIntPtr(Word32Shr(
              TruncateWordToInt32(BitcastTaggedToWordForTagAndSmiBits(a)),
              shift)),
          BitcastTaggedToWordForTagAndSmiBits(SmiConstant(-1))));
    }
    // Smi shift have different result to int32 shift when the inputs are not
    // strictly limited. The CSA_DCHECK is to ensure valid inputs.
    CSA_DCHECK(
        this, TaggedEqual(result, BitwiseOp(SmiToInt32(a), Int32Constant(shift),
                                            Operation::kShiftRightLogical)));
    return result;
  }

  TNode<Smi> SmiSar(TNode<Smi> a, int shift) {
    // The number of shift bits is |shift % 64| for 64-bits value and |shift %
    // 32| for 32-bits value. The DCHECK is to ensure valid inputs.
    DCHECK_LT(shift, 32);
    if (kTaggedSize == kInt64Size) {
      return BitcastWordToTaggedSigned(
          WordAnd(WordSar(BitcastTaggedToWordForTagAndSmiBits(a), shift),
                  BitcastTaggedToWordForTagAndSmiBits(SmiConstant(-1))));
    } else {
      // For pointer compressed Smis, we want to make sure that we truncate to
      // int32 before shifting, to avoid the values of the top 32-bits from
      // changing the sign bit of the smi.
      return BitcastWordToTaggedSigned(WordAnd(
          ChangeInt32ToIntPtr(Word32Sar(
              TruncateWordToInt32(BitcastTaggedToWordForTagAndSmiBits(a)),
              shift)),
          BitcastTaggedToWordForTagAndSmiBits(SmiConstant(-1))));
    }
  }

  TNode<Smi> WordOrSmiShr(TNode<Smi> a, int shift) { return SmiShr(a, shift); }

  TNode<IntPtrT> WordOrSmiShr(TNode<IntPtrT> a, int shift) {
    return WordShr(a, shift);
  }

#define SMI_COMPARISON_OP(SmiOpName, IntPtrOpName, Int32OpName)           \
  TNode<BoolT> SmiOpName(TNode<Smi> a, TNode<Smi> b) {                    \
    if (kTaggedSize == kInt64Size) {                                      \
      return IntPtrOpName(BitcastTaggedToWordForTagAndSmiBits(a),         \
                          BitcastTaggedToWordForTagAndSmiBits(b));        \
    } else {                                                              \
      DCHECK_EQ(kTaggedSize, kInt32Size);                                 \
      DCHECK(SmiValuesAre31Bits());                                       \
      return Int32OpName(                                                 \
          TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(a)),  \
          TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(b))); \
    }                                                                     \
  }
  SMI_COMPARISON_OP(SmiEqual, WordEqual, Word32Equal)
  SMI_COMPARISON_OP(SmiNotEqual, WordNotEqual, Word32NotEqual)
  SMI_COMPARISON_OP(SmiAbove, UintPtrGreaterThan, Uint32GreaterThan)
  SMI_COMPARISON_OP(SmiAboveOrEqual, UintPtrGreaterThanOrEqual,
                    Uint32GreaterThanOrEqual)
  SMI_COMPARISON_OP(SmiBelow, UintPtrLessThan, Uint32LessThan)
  SMI_COMPARISON_OP(SmiBelowOrEqual, UintPtrLessThanOrEqual,
                    Uint32LessThanOrEqual)
  SMI_COMPARISON_OP(SmiLessThan, IntPtrLessThan, Int32LessThan)
  SMI_COMPARISON_OP(SmiLessThanOrEqual, IntPtrLessThanOrEqual,
                    Int32LessThanOrEqual)
  SMI_COMPARISON_OP(SmiGreaterThan, IntPtrGreaterThan, Int32GreaterThan)
  SMI_COMPARISON_OP(SmiGreaterThanOrEqual, IntPtrGreaterThanOrEqual,
                    Int32GreaterThanOrEqual)
#undef SMI_COMPARISON_OP
  TNode<Smi> SmiMax(TNode<Smi> a, TNode<Smi> b);
  TNode<Smi> SmiMin(TNode<Smi> a, TNode<Smi> b);
  // Computes a % b for Smi inputs a and b; result is not necessarily a Smi.
  TNode<Number> SmiMod(TNode<Smi> a, TNode<Smi> b);
  // Computes a * b for Smi inputs a and b; result is not necessarily a Smi.
  TNode<Number> SmiMul(TNode<Smi> a, TNode<Smi> b);
  // Tries to compute dividend / divisor for Smi inputs; branching to bailout
  // if the division needs to be performed as a floating point operation.
  TNode<Smi> TrySmiDiv(TNode<Smi> dividend, TNode<Smi> divisor, Label* bailout);

  // Compares two Smis a and b as if they were converted to strings and then
  // compared lexicographically. Returns:
  // -1 iff x < y.
  //  0 iff x == y.
  //  1 iff x > y.
  TNode<Smi> SmiLexicographicCompare(TNode<Smi> x, TNode<Smi> y);

  // Returns Smi::zero() if no CoverageInfo exists.
  TNode<Object> GetCoverageInfo(TNode<SharedFunctionInfo> sfi);

#ifdef BINT_IS_SMI
#define BINT_COMPARISON_OP(BIntOpName, SmiOpName, IntPtrOpName) \
  TNode<BoolT> BIntOpName(TNode<BInt> a, TNode<BInt> b) {       \
    return SmiOpName(a, b);                                     \
  }
#else
#define BINT_COMPARISON_OP(BIntOpName, SmiOpName, IntPtrOpName) \
  TNode<BoolT> BIntOpName(TNode<BInt> a, TNode<BInt> b) {       \
    return IntPtrOpName(a, b);                                  \
  }
#endif
  BINT_COMPARISON_OP(BIntEqual, SmiEqual, WordEqual)
  BINT_COMPARISON_OP(BIntNotEqual, SmiNotEqual, WordNotEqual)
  BINT_COMPARISON_OP(BIntAbove, SmiAbove, UintPtrGreaterThan)
  BINT_COMPARISON_OP(BIntAboveOrEqual, SmiAboveOrEqual,
                     UintPtrGreaterThanOrEqual)
  BINT_COMPARISON_OP(BIntBelow, SmiBelow, UintPtrLessThan)
  BINT_COMPARISON_OP(BIntLessThan, SmiLessThan, IntPtrLessThan)
  BINT_COMPARISON_OP(BIntLessThanOrEqual, SmiLessThanOrEqual,
                     IntPtrLessThanOrEqual)
  BINT_COMPARISON_OP(BIntGreaterThan, SmiGreaterThan, IntPtrGreaterThan)
  BINT_COMPARISON_OP(BIntGreaterThanOrEqual, SmiGreaterThanOrEqual,
                     IntPtrGreaterThanOrEqual)
#undef BINT_COMPARISON_OP

  // Smi | HeapNumber operations.
  TNode<Number> NumberInc(TNode<Number> value);
  TNode<Number> NumberDec(TNode<Number> value);
  TNode<Number> NumberAdd(TNode<Number> a, TNode<Number> b);
  TNode<Number> NumberSub(TNode<Number> a, TNode<Number> b);
  void GotoIfNotNumber(TNode<Object> value, Label* is_not_number);
  void GotoIfNumber(TNode<Object> value, Label* is_number);
  TNode<Number> SmiToNumber(TNode<Smi> v) { return v; }

  // BigInt operations.
  void GotoIfLargeBigInt(TNode<BigInt> bigint, Label* true_label);

  TNode<Word32T> NormalizeShift32OperandIfNecessary(TNode<Word32T> right32);
  TNode<Number> BitwiseOp(TNode<Word32T> left32, TNode<Word32T> right32,
                          Operation bitwise_op);
  TNode<Number> BitwiseSmiOp(TNode<Smi> left32, TNode<Smi> right32,
                             Operation bitwise_op);

  // Align the value to kObjectAlignment8GbHeap if V8_COMPRESS_POINTERS_8GB is
  // defined.
  TNode<IntPtrT> AlignToAllocationAlignment(TNode<IntPtrT> value);

  // Allocate an object of the given size.
  TNode<HeapObject> AllocateInNewSpace(
      TNode<IntPtrT> size, AllocationFlags flags = AllocationFlag::kNone);
  TNode<HeapObject> AllocateInNewSpace(
      int size, AllocationFlags flags = AllocationFlag::kNone);
  TNode<HeapObject> Allocate(TNode<IntPtrT> size,
                             AllocationFlags flags = AllocationFlag::kNone);

  TNode<HeapObject> Allocate(int size,
                             AllocationFlags flags = AllocationFlag::kNone);

  TNode<BoolT> IsRegularHeapObjectSize(TNode<IntPtrT> size);

  using BranchGenerator = std::function<void(Label*, Label*)>;
  template <typename T>
  using NodeGenerator = std::function<TNode<T>()>;
  using ExtraNode = std::pair<TNode<Object>, const char*>;

  void Dcheck(const BranchGenerator& branch, const char* message,
              const char* file, int line,
              std::initializer_list<ExtraNode> extra_nodes = {},
              const SourceLocation& loc = SourceLocation::Current());
  void Dcheck(const NodeGenerator<BoolT>& condition_body, const char* message,
              const char* file, int line,
              std::initializer_list<ExtraNode> extra_nodes = {},
              const SourceLocation& loc = SourceLocation::Current());
  void Dcheck(TNode<Word32T> condition_node, const char* message,
              const char* file, int line,
              std::initializer_list<ExtraNode> extra_nodes = {},
              const SourceLocation& loc = SourceLocation::Current());
  void Check(const BranchGenerator& branch, const char* message,
             const char* file, int line,
             std::initializer_list<ExtraNode> extra_nodes = {},
             const SourceLocation& loc = SourceLocation::Current());
  void Check(const NodeGenerator<BoolT>& condition_body, const char* message,
             const char* file, int line,
             std::initializer_list<ExtraNode> extra_nodes = {},
             const SourceLocation& loc = SourceLocation::Current());
  void Check(TNode<Word32T> condition_node, const char* message,
             const char* file, int line,
             std::initializer_list<ExtraNode> extra_nodes = {},
             const SourceLocation& loc = SourceLocation::Current());
  void FailAssert(const char* message,
                  const std::vector<FileAndLine>& files_and_lines,
                  std::initializer_list<ExtraNode> extra_nodes = {},
                  const SourceLocation& loc = SourceLocation::Current());

  void FastCheck(TNode<BoolT> condition);

  TNode<RawPtrT> LoadCodeInstructionStart(TNode<Code> code,
                                          CodeEntrypointTag tag);
  TNode<BoolT> IsMarkedForDeoptimization(TNode<Code> code);

  void DCheckReceiver(ConvertReceiverMode mode, TNode<Object> receiver);

  // The following Call wrappers call an object according to the semantics that
  // one finds in the EcmaScript spec, operating on an Callable (e.g. a
  // JSFunction or proxy) rather than a InstructionStream object.
  template <typename TCallable, class... TArgs>
  inline TNode<Object> Call(TNode<Context> context, TNode<TCallable> callable,
                            ConvertReceiverMode mode, TNode<Object> receiver,
                            TArgs... args);
  template <typename TCallable, class... TArgs>
  inline TNode<Object> Call(TNode<Context> context, TNode<TCallable> callable,
                            TNode<JSReceiver> receiver, TArgs... args);
  template <typename TCallable, class... TArgs>
  inline TNode<Object> Call(TNode<Context> context, TNode<TCallable> callable,
                            TNode<Object> receiver, TArgs... args);
  template <class... TArgs>
  inline TNode<Object> CallFunction(TNode<Context> context,
                                    TNode<JSFunction> callable,
                                    ConvertReceiverMode mode,
                                    TNode<Object> receiver, TArgs... args);
  template <class... TArgs>
  inline TNode<Object> CallFunction(TNode<Context> context,
                                    TNode<JSFunction> callable,
                                    TNode<JSReceiver> receiver, TArgs... args);
  template <class... TArgs>
  inline TNode<Object> CallFunction(TNode<Context> context,
                                    TNode<JSFunction> callable,
                                    TNode<Object> receiver, TArgs... args);

  TNode<Object> CallApiCallback(TNode<Object> context, TNode<RawPtrT> callback,
                                TNode<Int32T> argc, TNode<Object> data,
                                TNode<Object> holder, TNode<Object> receiver);

  TNode<Object> CallApiCallback(TNode<Object> context, TNode<RawPtrT> callback,
                                TNode<Int32T> argc, TNode<Object> data,
                                TNode<Object> holder, TNode<Object> receiver,
                                TNode<Object> value);

  TNode<Object> CallRuntimeNewArray(TNode<Context> context,
                                    TNode<Object> receiver,
                                    TNode<Object> length,
                                    TNode<Object> new_target,
                                    TNode<Object> allocation_site);

  void TailCallRuntimeNewArray(TNode<Context> context, TNode<Object> receiver,
                               TNode<Object> length, TNode<Object> new_target,
                               TNode<Object> allocation_site);

  template <class... TArgs>
  TNode<JSReceiver> ConstructWithTarget(TNode<Context> context,
                                        TNode<JSReceiver> target,
                                        TNode<JSReceiver> new_target,
                                        TArgs... args) {
    return CAST(ConstructJSWithTarget(Builtin::kConstruct, context, target,
                                      new_target,
                                      implicit_cast<TNode<Object>>(args)...));
  }
  template <class... TArgs>
  TNode<JSReceiver> Construct(TNode<Context> context,
                              TNode<JSReceiver> new_target, TArgs... args) {
    return ConstructWithTarget(context, new_target, new_target, args...);
  }

  template <typename T>
  TNode<T> Select(TNode<BoolT> condition, const NodeGenerator<T>& true_body,
                  const NodeGenerator<T>& false_body) {
    TVARIABLE(T, value);
    Label vtrue(this), vfalse(this), end(this);
    Branch(condition, &vtrue, &vfalse);

    BIND(&vtrue);
    {
      value = true_body();
      Goto(&end);
    }
    BIND(&vfalse);
    {
      value = false_body();
      Goto(&end);
    }

    BIND(&end);
    return value.value();
  }

  template <class A>
  TNode<A> SelectConstant(TNode<BoolT> condition, TNode<A> true_value,
                          TNode<A> false_value) {
    return Select<A>(
        condition, [=] { return true_value; }, [=] { return false_value; });
  }

  TNode<Int32T> SelectInt32Constant(TNode<BoolT> condition, int true_value,
                                    int false_value);
  TNode<IntPtrT> SelectIntPtrConstant(TNode<BoolT> condition, int true_value,
                                      int false_value);
  TNode<Boolean> SelectBooleanConstant(TNode<BoolT> condition);
  TNode<Smi> SelectSmiConstant(TNode<BoolT> condition, Tagged<Smi> true_value,
                               Tagged<Smi> false_value);
  TNode<Smi> SelectSmiConstant(TNode<BoolT> condition, int true_value,
                               Tagged<Smi> false_value) {
    return SelectSmiConstant(condition, Smi::FromInt(true_value), false_value);
  }
  TNode<Smi> SelectSmiConstant(TNode<BoolT> condition, Tagged<Smi> true_value,
                               int false_value) {
    return SelectSmiConstant(condition, true_value, Smi::FromInt(false_value));
  }
  TNode<Smi> SelectSmiConstant(TNode<BoolT> condition, int true_value,
                               int false_value) {
    return SelectSmiConstant(condition, Smi::FromInt(true_value),
                             Smi::FromInt(false_value));
  }

  TNode<String> SingleCharacterStringConstant(char const* single_char) {
    DCHECK_EQ(strlen(single_char), 1);
    return HeapConstantNoHole(
        isolate()->factory()->LookupSingleCharacterStringFromCode(
            single_char[0]));
  }

  TNode<Float16T> TruncateFloat32ToFloat16(TNode<Float32T> value);
  TNode<Float16T> TruncateFloat64ToFloat16(TNode<Float64T> value);

  TNode<Int32T> TruncateWordToInt32(TNode<WordT> value);
  TNode<Int32T> TruncateIntPtrToInt32(TNode<IntPtrT> value);
  TNode<Word32T> TruncateWord64ToWord32(TNode<Word64T> value);

  // Check a value for smi-ness
  TNode<BoolT> TaggedIsSmi(TNode<MaybeObject> a);
  TNode<BoolT> TaggedIsNotSmi(TNode<MaybeObject> a);

  // Check that the value is a non-negative smi.
  TNode<BoolT> TaggedIsPositiveSmi(TNode<Object> a);
  // Check that a word has a word-aligned address.
  TNode<BoolT> WordIsAligned(TNode<WordT> word, size_t alignment);
  TNode<BoolT> WordIsPowerOfTwo(TNode<IntPtrT> value);

  // Check if lower_limit <= value <= higher_limit.
  template <typename U>
  TNode<BoolT> IsInRange(TNode<Word32T> value, U lower_limit, U higher_limit) {
    DCHECK_LE(lower_limit, higher_limit);
    static_assert(sizeof(U) <= kInt32Size);
    if (lower_limit == 0) {
      return Uint32LessThanOrEqual(value, Int32Constant(higher_limit));
    }
    return Uint32LessThanOrEqual(Int32Sub(value, Int32Constant(lower_limit)),
                                 Int32Constant(higher_limit - lower_limit));
  }

  TNode<BoolT> IsInRange(TNode<UintPtrT> value, TNode<UintPtrT> lower_limit,
                         TNode<UintPtrT> higher_limit) {
    CSA_DCHECK(this, UintPtrLessThanOrEqual(lower_limit, higher_limit));
    return UintPtrLessThanOrEqual(UintPtrSub(value, lower_limit),
                                  UintPtrSub(higher_limit, lower_limit));
  }

  TNode<BoolT> IsInRange(TNode<WordT> value, intptr_t lower_limit,
                         intptr_t higher_limit) {
    DCHECK_LE(lower_limit, higher_limit);
    if (lower_limit == 0) {
      return UintPtrLessThanOrEqual(value, IntPtrConstant(higher_limit));
    }
    return UintPtrLessThanOrEqual(IntPtrSub(value, IntPtrConstant(lower_limit)),
                                  IntPtrConstant(higher_limit - lower_limit));
  }

#if DEBUG
  void Bind(Label* label, AssemblerDebugInfo debug_info);
#endif  // DEBUG
  void Bind(Label* label);

  template <class... T>
  void Bind(compiler::CodeAssemblerParameterizedLabel<T...>* label,
            TNode<T>*... phis) {
    CodeAssembler::Bind(label, phis...);
  }

  void BranchIfSmiEqual(TNode<Smi> a, TNode<Smi> b, Label* if_true,
                        Label* if_false) {
    Branch(SmiEqual(a, b), if_true, if_false);
  }

  void BranchIfSmiLessThan(TNode<Smi> a, TNode<Smi> b, Label* if_true,
                           Label* if_false) {
    Branch(SmiLessThan(a, b), if_true, if_false);
  }

  void BranchIfSmiLessThanOrEqual(TNode<Smi> a, TNode<Smi> b, Label* if_true,
                                  Label* if_false) {
    Branch(SmiLessThanOrEqual(a, b), if_true, if_false);
  }

  void BranchIfFloat64IsNaN(TNode<Float64T> value, Label* if_true,
                            Label* if_false) {
    Branch(Float64Equal(value, value), if_false, if_true);
  }

  // Branches to {if_true} if ToBoolean applied to {value} yields true,
  // otherwise goes to {if_false}.
  void BranchIfToBooleanIsTrue(TNode<Object> value, Label* if_true,
                               Label* if_false);

  // Branches to {if_false} if ToBoolean applied to {value} yields false,
  // otherwise goes to {if_true}.
  void BranchIfToBooleanIsFalse(TNode<Object> value, Label* if_false,
                                Label* if_true) {
    BranchIfToBooleanIsTrue(value, if_true, if_false);
  }

  void BranchIfJSReceiver(TNode<Object> object, Label* if_true,
                          Label* if_false);

  // Branches to {if_true} when --force-slow-path flag has been passed.
  // It's used for testing to ensure that slow path implementation behave
  // equivalent to corresponding fast paths (where applicable).
  //
  // Works only with V8_ENABLE_FORCE_SLOW_PATH compile time flag. Nop otherwise.
  void GotoIfForceSlowPath(Label* if_true);

  //
  // Sandboxed pointer related functionality.
  //

  // Load a sandboxed pointer value from an object.
  TNode<RawPtrT> LoadSandboxedPointerFromObject(TNode<HeapObject> object,
                                                int offset) {
    return LoadSandboxedPointerFromObject(object, IntPtrConstant(offset));
  }

  TNode<RawPtrT> LoadSandboxedPointerFromObject(TNode<HeapObject> object,
                                                TNode<IntPtrT> offset);

  // Stored a sandboxed pointer value to an object.
  void StoreSandboxedPointerToObject(TNode<HeapObject> object, int offset,
                                     TNode<RawPtrT> pointer) {
    StoreSandboxedPointerToObject(object, IntPtrConstant(offset), pointer);
  }

  void StoreSandboxedPointerToObject(TNode<HeapObject> object,
                                     TNode<IntPtrT> offset,
                                     TNode<RawPtrT> pointer);

  TNode<RawPtrT> EmptyBackingStoreBufferConstant();

  //
  // Bounded size related functionality.
  //

  // Load a bounded size value from an object.
  TNode<UintPtrT> LoadBoundedSizeFromObject(TNode<HeapObject> object,
                                            int offset) {
    return LoadBoundedSizeFromObject(object, IntPtrConstant(offset));
  }

  TNode<UintPtrT> LoadBoundedSizeFromObject(TNode<HeapObject> object,
                                            TNode<IntPtrT> offset);

  // Stored a bounded size value to an object.
  void StoreBoundedSizeToObject(TNode<HeapObject> object, int offset,
                                TNode<UintPtrT> value) {
    StoreBoundedSizeToObject(object, IntPtrConstant(offset), value);
  }

  void StoreBoundedSizeToObject(TNode<HeapObject> object, TNode<IntPtrT> offset,
                                TNode<UintPtrT> value);
  //
  // ExternalPointerT-related functionality.
  //

  TNode<RawPtrT> ExternalPointerTableAddress(ExternalPointerTag tag);

  // Load an external pointer value from an object.
  TNode<RawPtrT> LoadExternalPointerFromObject(TNode<HeapObject> object,
                                               int offset,
                                               ExternalPointerTag tag) {
    return LoadExternalPointerFromObject(object, IntPtrConstant(offset), tag);
  }

  TNode<RawPtrT> LoadExternalPointerFromObject(TNode<HeapObject> object,
                                               TNode<IntPtrT> offset,
                                               ExternalPointerTag tag);

  // Store external object pointer to object.
  void StoreExternalPointerToObject(TNode<HeapObject> object, int offset,
                                    TNode<RawPtrT> pointer,
                                    ExternalPointerTag tag) {
    StoreExternalPointerToObject(object, IntPtrConstant(offset), pointer, tag);
  }

  void StoreExternalPointerToObject(TNode<HeapObject> object,
                                    TNode<IntPtrT> offset,
                                    TNode<RawPtrT> pointer,
                                    ExternalPointerTag tag);

  // Load a trusted pointer field.
  // When the sandbox is enabled, these are indirect pointers using the trusted
  // pointer table. Otherwise they are regular tagged fields.
  TNode<TrustedObject> LoadTrustedPointerFromObject(TNode<HeapObject> object,
                                                    int offset,
                                                    IndirectPointerTag tag);

  // Load a code pointer field.
  // These are special versions of trusted pointers that, when the sandbox is
  // enabled, reference code objects through the code pointer table.
  TNode<Code> LoadCodePointerFromObject(TNode<HeapObject> object, int offset);

#ifdef V8_ENABLE_SANDBOX
  // Load an indirect pointer field.
  TNode<TrustedObject> LoadIndirectPointerFromObject(TNode<HeapObject> object,
                                                     int offset,
                                                     IndirectPointerTag tag);

  // Determines whether the given indirect pointer handle is a trusted pointer
  // handle or a code pointer handle.
  TNode<BoolT> IsTrustedPointerHandle(TNode<IndirectPointerHandleT> handle);

  // Retrieve the heap object referenced by the given indirect pointer handle,
  // which can either be a trusted pointer handle or a code pointer handle.
  TNode<TrustedObject> ResolveIndirectPointerHandle(
      TNode<IndirectPointerHandleT> handle, IndirectPointerTag tag);

  // Retrieve the Code object referenced by the given trusted pointer handle.
  TNode<Code> ResolveCodePointerHandle(TNode<IndirectPointerHandleT> handle);

  // Retrieve the heap object referenced by the given trusted pointer handle.
  TNode<TrustedObject> ResolveTrustedPointerHandle(
      TNode<IndirectPointerHandleT> handle, IndirectPointerTag tag);

  // Helper function to compute the offset into the code pointer table from a
  // code pointer handle.
  TNode<UintPtrT> ComputeCodePointerTableEntryOffset(
      TNode<IndirectPointerHandleT> handle);

  // Load the pointer to a Code's entrypoint via code pointer.
  // Only available when the sandbox is enabled as it requires the code pointer
  // table.
  TNode<RawPtrT> LoadCodeEntrypointViaCodePointerField(TNode<HeapObject> object,
                                                       int offset,
                                                       CodeEntrypointTag tag) {
    return LoadCodeEntrypointViaCodePointerField(object, IntPtrConstant(offset),
                                                 tag);
  }
  TNode<RawPtrT> LoadCodeEntrypointViaCodePointerField(TNode<HeapObject> object,
                                                       TNode<IntPtrT> offset,
                                                       CodeEntrypointTag tag);
  TNode<RawPtrT> LoadCodeEntryFromIndirectPointerHandle(
      TNode<IndirectPointerHandleT> handle, CodeEntrypointTag tag);

  TNode<UintPtrT> ComputeJSDispatchTableEntryOffset(
      TNode<JSDispatchHandleT> handle);
#endif

  TNode<Object> LoadProtectedPointerField(TNode<TrustedObject> object,
                                          TNode<IntPtrT> offset) {
    return CAST(LoadProtectedPointerFromObject(
        object, IntPtrSub(offset, IntPtrConstant(kHeapObjectTag))));
  }
  TNode<Object> LoadProtectedPointerField(TNode<TrustedObject> object,
                                          int offset) {
    return CAST(LoadProtectedPointerFromObject(
        object, IntPtrConstant(offset - kHeapObjectTag)));
  }

  TNode<RawPtrT> LoadForeignForeignAddressPtr(TNode<Foreign> object,
                                              ExternalPointerTag tag) {
    return LoadExternalPointerFromObject(object, Foreign::kForeignAddressOffset,
                                         tag);
  }

  TNode<RawPtrT> LoadFunctionTemplateInfoJsCallbackPtr(
      TNode<FunctionTemplateInfo> object) {
    return LoadExternalPointerFromObject(
        object, FunctionTemplateInfo::kMaybeRedirectedCallbackOffset,
        kFunctionTemplateInfoCallbackTag);
  }

  TNode<RawPtrT> LoadExternalStringResourcePtr(TNode<ExternalString> object) {
    return LoadExternalPointerFromObject(object,
                                         offsetof(ExternalString, resource_),
                                         kExternalStringResourceTag);
  }

  TNode<RawPtrT> LoadExternalStringResourceDataPtr(
      TNode<ExternalString> object) {
    // This is only valid for ExternalStrings where the resource data
    // pointer is cached (i.e. no uncached external strings).
    CSA_DCHECK(this, Word32NotEqual(
                         Word32And(LoadInstanceType(object),
                                   Int32Constant(kUncachedExternalStringMask)),
                         Int32Constant(kUncachedExternalStringTag)));
    return LoadExternalPointerFromObject(
        object, offsetof(ExternalString, resource_data_),
        kExternalStringResourceDataTag);
  }

  TNode<RawPtr<Uint64T>> Log10OffsetTable() {
    return ReinterpretCast<RawPtr<Uint64T>>(
        ExternalConstant(ExternalReference::address_of_log10_offset_table()));
  }

#if V8_ENABLE_WEBASSEMBLY
  // Returns WasmTrustedInstanceData|Smi.
  TNode<Object> LoadInstanceDataFromWasmImportData(TNode<WasmImportData> ref) {
    return LoadProtectedPointerField(
        ref, WasmImportData::kProtectedInstanceDataOffset);
  }

  // Returns WasmImportData or WasmTrustedInstanceData.
  TNode<TrustedObject> LoadImplicitArgFromWasmInternalFunction(
      TNode<WasmInternalFunction> object) {
    TNode<Object> obj = LoadProtectedPointerField(
        object, WasmInternalFunction::kProtectedImplicitArgOffset);
    CSA_DCHECK(this, TaggedIsNotSmi(obj));
    TNode<HeapObject> implicit_arg = CAST(obj);
    CSA_DCHECK(
        this,
        Word32Or(HasInstanceType(implicit_arg, WASM_TRUSTED_INSTANCE_DATA_TYPE),
                 HasInstanceType(implicit_arg, WASM_IMPORT_DATA_TYPE)));
    return CAST(implicit_arg);
  }

  TNode<RawPtrT> LoadWasmTypeInfoNativeTypePtr(TNode<WasmTypeInfo> object) {
    return LoadExternalPointerFromObject(
        object, WasmTypeInfo::kNativeTypeOffset, kWasmTypeInfoNativeTypeTag);
  }

  TNode<WasmInternalFunction> LoadWasmInternalFunctionFromFuncRef(
      TNode<WasmFuncRef> func_ref) {
    return CAST(LoadTrustedPointerFromObject(
        func_ref, WasmFuncRef::kTrustedInternalOffset,
        kWasmInternalFunctionIndirectPointerTag));
  }

  TNode<WasmInternalFunction> LoadWasmInternalFunctionFromFunctionData(
      TNode<WasmFunctionData> data) {
    return CAST(LoadProtectedPointerField(
        data, WasmFunctionData::kProtectedInternalOffset));
  }

  TNode<WasmTrustedInstanceData>
  LoadWasmTrustedInstanceDataFromWasmExportedFunctionData(
      TNode<WasmExportedFunctionData> data) {
    return CAST(LoadProtectedPointerField(
        data, WasmExportedFunctionData::kProtectedInstanceDataOffset));
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  TNode<RawPtrT> LoadJSTypedArrayExternalPointerPtr(
      TNode<JSTypedArray> holder) {
    return LoadSandboxedPointerFromObject(holder,
                                          JSTypedArray::kExternalPointerOffset);
  }

  void StoreJSTypedArrayExternalPointerPtr(TNode<JSTypedArray> holder,
                                           TNode<RawPtrT> value) {
    StoreSandboxedPointerToObject(holder, JSTypedArray::kExternalPointerOffset,
                                  value);
  }

  void InitializeJSAPIObjectWithEmbedderSlotsCppHeapWrapperPtr(
      TNode<JSAPIObjectWithEmbedderSlots> holder) {
    auto zero_constant =
#ifdef V8_COMPRESS_POINTERS
        Int32Constant(0);
#else   // !V8_COMPRESS_POINTERS
        IntPtrConstant(0);
#endif  // !V8_COMPRESS_POINTERS
    StoreObjectFieldNoWriteBarrier(
        holder, JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset,
        zero_constant);
  }

  // Load value from current parent frame by given offset in bytes.
  TNode<Object> LoadFromParentFrame(int offset);

  // Load an object pointer from a buffer that isn't in the heap.
  TNode<Object> LoadBufferObject(TNode<RawPtrT> buffer, int offset) {
    return LoadFullTagged(buffer, IntPtrConstant(offset));
  }
  template <typename T>
  TNode<T> LoadBufferData(TNode<RawPtrT> buffer, int offset) {
    return UncheckedCast<T>(
        Load(MachineTypeOf<T>::value, buffer, IntPtrConstant(offset)));
  }
  TNode<RawPtrT> LoadBufferPointer(TNode<RawPtrT> buffer, int offset) {
    return LoadBufferData<RawPtrT>(buffer, offset);
  }
  TNode<Smi> LoadBufferSmi(TNode<RawPtrT> buffer, int offset) {
    return CAST(LoadBufferObject(buffer, offset));
  }
  TNode<IntPtrT> LoadBufferIntptr(TNode<RawPtrT> buffer, int offset) {
    return LoadBufferData<IntPtrT>(buffer, offset);
  }
  TNode<Uint8T> LoadUint8Ptr(TNode<RawPtrT> ptr, TNode<IntPtrT> offset);
  TNode<Uint64T> LoadUint64Ptr(TNode<RawPtrT> ptr, TNode<IntPtrT> index);

  // Load a field from an object on the heap.
  template <typename T>
  TNode<T> LoadObjectField(TNode<HeapObject> object, int offset) {
    MachineType machine_type = MachineTypeOf<T>::value;
    TNode<IntPtrT> raw_offset = IntPtrConstant(offset - kHeapObjectTag);
    if constexpr (is_subtype_v<T, UntaggedT>) {
      // Load an untagged field.
      return UncheckedCast<T>(LoadFromObject(machine_type, object, raw_offset));
    } else {
      static_assert(is_subtype_v<T, Object>);
      // Load a tagged field.
      if constexpr (is_subtype_v<T, Map>) {
        // If this is potentially loading a map, we need to check the offset.
        if (offset == HeapObject::kMapOffset) {
          machine_type = MachineType::MapInHeader();
        }
      }
      return CAST(LoadFromObject(machine_type, object, raw_offset));
    }
  }
  TNode<Object> LoadObjectField(TNode<HeapObject> object, int offset) {
    return UncheckedCast<Object>(
        LoadFromObject(MachineType::AnyTagged(), object,
                       IntPtrConstant(offset - kHeapObjectTag)));
  }
  TNode<Object> LoadObjectField(TNode<HeapObject> object,
                                TNode<IntPtrT> offset) {
    return UncheckedCast<Object>(
        LoadFromObject(MachineType::AnyTagged(), object,
                       IntPtrSub(offset, IntPtrConstant(kHeapObjectTag))));
  }
  template <class T, typename std::enable_if<
                         std::is_convertible<TNode<T>, TNode<UntaggedT>>::value,
                         int>::type = 0>
  TNode<T> LoadObjectField(TNode<HeapObject> object, TNode<IntPtrT> offset) {
    return UncheckedCast<T>(
        LoadFromObject(MachineTypeOf<T>::value, object,
                       IntPtrSub(offset, IntPtrConstant(kHeapObjectTag))));
  }
  // Load a positive SMI field and untag it.
  TNode<IntPtrT> LoadAndUntagPositiveSmiObjectField(TNode<HeapObject> object,
                                                    int offset);
  // Load a SMI field, untag it, and convert to Word32.
  TNode<Int32T> LoadAndUntagToWord32ObjectField(TNode<HeapObject> object,
                                                int offset);

  TNode<MaybeObject> LoadMaybeWeakObjectField(TNode<HeapObject> object,
                                              int offset) {
    return UncheckedCast<MaybeObject>(LoadObjectField(object, offset));
  }

  TNode<Object> LoadConstructorOrBackPointer(TNode<Map> map) {
    return LoadObjectField(map,
                           Map::kConstructorOrBackPointerOrNativeContextOffset);
  }

  TNode<Simd128T> LoadSimd128(TNode<IntPtrT> ptr) {
    return Load<Simd128T>(ptr);
  }

  // Reference is the CSA-equivalent of a Torque reference value, representing
  // an inner pointer into a HeapObject.
  //
  // The object can be a HeapObject or an all-zero bitpattern. The latter is
  // used for off-heap data, in which case the offset holds the actual address
  // and the data must be untagged (i.e. accessed via the Load-/StoreReference
  // overloads for TNode<UntaggedT>-convertible types below).
  //
  // TODO(gsps): Remove in favor of flattened {Load,Store}Reference interface.
  struct Reference {
    TNode<Object> object;
    TNode<IntPtrT> offset;

    std::tuple<TNode<Object>, TNode<IntPtrT>> Flatten() const {
      return std::make_tuple(object, offset);
    }
  };

  template <class T, typename std::enable_if<
                         std::is_convertible<TNode<T>, TNode<Object>>::value,
                         int>::type = 0>
  TNode<T> LoadReference(Reference reference) {
    if (IsMapOffsetConstant(reference.offset)) {
      TNode<Map> map = LoadMap(CAST(reference.object));
      DCHECK((std::is_base_of<T, Map>::value));
      return ReinterpretCast<T>(map);
    }

    TNode<IntPtrT> offset =
        IntPtrSub(reference.offset, IntPtrConstant(kHeapObjectTag));
    CSA_DCHECK(this, TaggedIsNotSmi(reference.object));
    return CAST(
        LoadFromObject(MachineTypeOf<T>::value, reference.object, offset));
  }
  template <class T,
            typename std::enable_if<
                std::is_convertible<TNode<T>, TNode<UntaggedT>>::value ||
                    std::is_same<T, MaybeObject>::value,
                int>::type = 0>
  TNode<T> LoadReference(Reference reference) {
    DCHECK(!IsMapOffsetConstant(reference.offset));
    TNode<IntPtrT> offset =
        IntPtrSub(reference.offset, IntPtrConstant(kHeapObjectTag));
    return UncheckedCast<T>(
        LoadFromObject(MachineTypeOf<T>::value, reference.object, offset));
  }
  template <class T, typename std::enable_if<
                         std::is_convertible<TNode<T>, TNode<Object>>::value ||
                             std::is_same<T, MaybeObject>::value,
                         int>::type = 0>
  void StoreReference(Reference reference, TNode<T> value) {
    if (IsMapOffsetConstant(reference.offset)) {
      DCHECK((std::is_base_of<T, Map>::value));
      return StoreMap(CAST(reference.object), ReinterpretCast<Map>(value));
    }
    MachineRepresentation rep = MachineRepresentationOf<T>::value;
    StoreToObjectWriteBarrier write_barrier = StoreToObjectWriteBarrier::kFull;
    if (std::is_same<T, Smi>::value) {
      write_barrier = StoreToObjectWriteBarrier::kNone;
    } else if (std::is_same<T, Map>::value) {
      write_barrier = StoreToObjectWriteBarrier::kMap;
    }
    TNode<IntPtrT> offset =
        IntPtrSub(reference.offset, IntPtrConstant(kHeapObjectTag));
    CSA_DCHECK(this, TaggedIsNotSmi(reference.object));
    StoreToObject(rep, reference.object, offset, value, write_barrier);
  }
  template <class T, typename std::enable_if<
                         std::is_convertible<TNode<T>, TNode<UntaggedT>>::value,
                         int>::type = 0>
  void StoreReference(Reference reference, TNode<T> value) {
    DCHECK(!IsMapOffsetConstant(reference.offset));
    TNode<IntPtrT> offset =
        IntPtrSub(reference.offset, IntPtrConstant(kHeapObjectTag));
    StoreToObject(MachineRepresentationOf<T>::value, reference.object, offset,
                  value, StoreToObjectWriteBarrier::kNone);
  }

  TNode<RawPtrT> GCUnsafeReferenceToRawPtr(TNode<Object> object,
                                           TNode<IntPtrT> offset) {
    return ReinterpretCast<RawPtrT>(
        IntPtrAdd(BitcastTaggedToWord(object),
                  IntPtrSub(offset, IntPtrConstant(kHeapObjectTag))));
  }

  // Load the floating point value of a HeapNumber.
  TNode<Float64T> LoadHeapNumberValue(TNode<HeapObject> object);
  // Load the Map of an HeapObject.
  TNode<Map> LoadMap(TNode<HeapObject> object);
  // Load the instance type of an HeapObject.
  TNode<Uint16T> LoadInstanceType(TNode<HeapObject> object);
  // Compare the instance the type of the object against the provided one.
  TNode<BoolT> HasInstanceType(TNode<HeapObject> object, InstanceType type);
  TNode<BoolT> DoesntHaveInstanceType(TNode<HeapObject> object,
                                      InstanceType type);
  TNode<BoolT> TaggedDoesntHaveInstanceType(TNode<HeapObject> any_tagged,
                                            InstanceType type);

  TNode<Word32T> IsStringWrapperElementsKind(TNode<Map> map);
  void GotoIfMapHasSlowProperties(TNode<Map> map, Label* if_slow);

  // Load the properties backing store of a JSReceiver.
  TNode<HeapObject> LoadSlowProperties(TNode<JSReceiver> object);
  TNode<HeapObject> LoadFastProperties(TNode<JSReceiver> object,
                                       bool skip_empty_check = false);
  // Load the elements backing store of a JSObject.
  TNode<FixedArrayBase> LoadElements(TNode<JSObject> object) {
    return LoadJSObjectElements(object);
  }
  // Load the length of a JSArray instance.
  TNode<Object> LoadJSArgumentsObjectLength(TNode<Context> context,
                                            TNode<JSArgumentsObject> array);
  // Load the length of a fast JSArray instance. Returns a positive Smi.
  TNode<Smi> LoadFastJSArrayLength(TNode<JSArray> array);
  // Load the length of a fixed array base instance.
  TNode<Smi> LoadFixedArrayBaseLength(TNode<FixedArrayBase> array);
  template <typename Array>
  TNode<Smi> LoadArrayCapacity(TNode<Array> array) {
    return LoadObjectField<Smi>(array, Array::Shape::kCapacityOffset);
  }
  // Load the length of a fixed array base instance.
  TNode<IntPtrT> LoadAndUntagFixedArrayBaseLength(TNode<FixedArrayBase> array);
  TNode<Uint32T> LoadAndUntagFixedArrayBaseLengthAsUint32(
      TNode<FixedArrayBase> array);
  // Load the length of a WeakFixedArray.
  TNode<Smi> LoadWeakFixedArrayLength(TNode<WeakFixedArray> array);
  TNode<IntPtrT> LoadAndUntagWeakFixedArrayLength(TNode<WeakFixedArray> array);
  TNode<Uint32T> LoadAndUntagWeakFixedArrayLengthAsUint32(
      TNode<WeakFixedArray> array);
  // Load the length of a BytecodeArray.
  TNode<Uint32T> LoadAndUntagBytecodeArrayLength(TNode<BytecodeArray> array);
  // Load the number of descriptors in DescriptorArray.
  TNode<Int32T> LoadNumberOfDescriptors(TNode<DescriptorArray> array);
  // Load the number of own descriptors of a map.
  TNode<Int32T> LoadNumberOfOwnDescriptors(TNode<Map> map);
  // Load the bit field of a Map.
  TNode<Int32T> LoadMapBitField(TNode<Map> map);
  // Load bit field 2 of a map.
  TNode<Int32T> LoadMapBitField2(TNode<Map> map);
  // Load bit field 3 of a map.
  TNode<Uint32T> LoadMapBitField3(TNode<Map> map);
  // Load the instance type of a map.
  TNode<Uint16T> LoadMapInstanceType(TNode<Map> map);
  // Load the ElementsKind of a map.
  TNode<Int32T> LoadMapElementsKind(TNode<Map> map);
  TNode<Int32T> LoadElementsKind(TNode<HeapObject> object);
  // Load the instance descriptors of a map.
  TNode<DescriptorArray> LoadMapDescriptors(TNode<Map> map);
  // Load the prototype of a map.
  TNode<HeapObject> LoadMapPrototype(TNode<Map> map);
  // Load the instance size of a Map.
  TNode<IntPtrT> LoadMapInstanceSizeInWords(TNode<Map> map);
  // Load the inobject properties start of a Map (valid only for JSObjects).
  TNode<IntPtrT> LoadMapInobjectPropertiesStartInWords(TNode<Map> map);
  // Load the constructor function index of a Map (only for primitive maps).
  TNode<IntPtrT> LoadMapConstructorFunctionIndex(TNode<Map> map);
  // Load the constructor of a Map (equivalent to Map::GetConstructor()).
  TNode<Object> LoadMapConstructor(TNode<Map> map);
  // Load the EnumLength of a Map.
  TNode<Uint32T> LoadMapEnumLength(TNode<Map> map);
  // Load the back-pointer of a Map.
  TNode<Object> LoadMapBackPointer(TNode<Map> map);
  // Compute the used instance size in words of a map.
  TNode<IntPtrT> MapUsedInstanceSizeInWords(TNode<Map> map);
  // Compute the number of used inobject properties on a map.
  TNode<IntPtrT> MapUsedInObjectProperties(TNode<Map> map);
  // Checks that |map| has only simple properties, returns bitfield3.
  TNode<Uint32T> EnsureOnlyHasSimpleProperties(TNode<Map> map,
                                               TNode<Int32T> instance_type,
                                               Label* bailout);
  // Load the identity hash of a JSRececiver.
  TNode<Uint32T> LoadJSReceiverIdentityHash(TNode<JSReceiver> receiver,
                                            Label* if_no_hash = nullptr);

  // This is only used on a newly allocated PropertyArray which
  // doesn't have an existing hash.
  void InitializePropertyArrayLength(TNode<PropertyArray> property_array,
                                     TNode<IntPtrT> length);

  // Check if the map is set for slow properties.
  TNode<BoolT> IsDictionaryMap(TNode<Map> map);

  // Load the Name::hash() value of a name as an uint32 value.
  // If {if_hash_not_computed} label is specified then it also checks if
  // hash is actually computed.
  TNode<Uint32T> LoadNameHash(TNode<Name> name,
                              Label* if_hash_not_computed = nullptr);
  TNode<Uint32T> LoadNameHashAssumeComputed(TNode<Name> name);

  // Load the Name::RawHash() value of a name as an uint32 value. Follows
  // through the forwarding table.
  TNode<Uint32T> LoadNameRawHash(TNode<Name> name);

  // Load length field of a String object as Smi value.
  TNode<Smi> LoadStringLengthAsSmi(TNode<String> string);
  // Load length field of a String object as intptr_t value.
  TNode<IntPtrT> LoadStringLengthAsWord(TNode<String> string);
  // Load length field of a String object as uint32_t value.
  TNode<Uint32T> LoadStringLengthAsWord32(TNode<String> string);
  // Load value field of a JSPrimitiveWrapper object.
  TNode<Object> LoadJSPrimitiveWrapperValue(TNode<JSPrimitiveWrapper> object);

  // Figures out whether the value of maybe_object is:
  // - a SMI (jump to "if_smi", "extracted" will be the SMI value)
  // - a cleared weak reference (jump to "if_cleared", "extracted" will be
  // untouched)
  // - a weak reference (jump to "if_weak", "extracted" will be the object
  // pointed to)
  // - a strong reference (jump to "if_strong", "extracted" will be the object
  // pointed to)
  void DispatchMaybeObject(TNode<MaybeObject> maybe_object, Label* if_smi,
                           Label* if_cleared, Label* if_weak, Label* if_strong,
                           TVariable<Object>* extracted);
  // See Tagged<MaybeObject> for semantics of these functions.
  TNode<BoolT> IsStrong(TNode<MaybeObject> value);
  TNode<BoolT> IsStrong(TNode<HeapObjectReference> value);
  TNode<HeapObject> GetHeapObjectIfStrong(TNode<MaybeObject> value,
                                          Label* if_not_strong);
  TNode<HeapObject> GetHeapObjectIfStrong(TNode<HeapObjectReference> value,
                                          Label* if_not_strong);

  TNode<BoolT> IsWeakOrCleared(TNode<MaybeObject> value);
  TNode<BoolT> IsWeakOrCleared(TNode<HeapObjectReference> value);
  TNode<BoolT> IsCleared(TNode<MaybeObject> value);
  TNode<BoolT> IsNotCleared(TNode<MaybeObject> value) {
    return Word32BinaryNot(IsCleared(value));
  }

  // Removes the weak bit + asserts it was set.
  TNode<HeapObject> GetHeapObjectAssumeWeak(TNode<MaybeObject> value);

  TNode<HeapObject> GetHeapObjectAssumeWeak(TNode<MaybeObject> value,
                                            Label* if_cleared);

  // Checks if |maybe_object| is a weak reference to given |heap_object|.
  // Works for both any tagged |maybe_object| values.
  TNode<BoolT> IsWeakReferenceTo(TNode<MaybeObject> maybe_object,
                                 TNode<HeapObject> heap_object);
  // Returns true if the |object| is a HeapObject and |maybe_object| is a weak
  // reference to |object|.
  // The |maybe_object| must not be a Smi.
  TNode<BoolT> IsWeakReferenceToObject(TNode<MaybeObject> maybe_object,
                                       TNode<Object> object);

  TNode<HeapObjectReference> MakeWeak(TNode<HeapObject> value);
  TNode<MaybeObject> ClearedValue();

  void FixedArrayBoundsCheck(TNode<FixedArrayBase> array, TNode<Smi> index,
                             int additional_offset);
  void FixedArrayBoundsCheck(TNode<FixedArray> array, TNode<Smi> index,
                             int additional_offset) {
    FixedArrayBoundsCheck(UncheckedCast<FixedArrayBase>(array), index,
                          additional_offset);
  }

  void FixedArrayBoundsCheck(TNode<FixedArrayBase> array, TNode<IntPtrT> index,
                             int additional_offset);
  void FixedArrayBoundsCheck(TNode<FixedArray> array, TNode<IntPtrT> index,
                             int additional_offset) {
    FixedArrayBoundsCheck(UncheckedCast<FixedArrayBase>(array), index,
                          additional_offset);
  }

  void FixedArrayBoundsCheck(TNode<FixedArrayBase> array, TNode<UintPtrT> index,
                             int additional_offset) {
    FixedArrayBoundsCheck(array, Signed(index), additional_offset);
  }

  void FixedArrayBoundsCheck(TNode<FixedArrayBase> array,
                             TNode<TaggedIndex> index, int additional_offset);
  void FixedArrayBoundsCheck(TNode<FixedArray> array, TNode<TaggedIndex> index,
                             int additional_offset) {
    FixedArrayBoundsCheck(UncheckedCast<FixedArrayBase>(array), index,
                          additional_offset);
  }

  // Array is any array-like type that has a fixed header followed by
  // tagged elements.
  template <typename Array>
  TNode<IntPtrT> LoadArrayLength(TNode<Array> array);

  // Array is any array-like type that has a fixed header followed by
  // tagged elements.
  template <typename Array, typename TIndex, typename TValue = MaybeObject>
  TNode<TValue> LoadArrayElement(TNode<Array> array, int array_header_size,
                                 TNode<TIndex> index,
                                 int additional_offset = 0);
  template <typename Array, typename TIndex>
  TNode<typename Array::Shape::ElementT> LoadArrayElement(
      TNode<Array> array, TNode<TIndex> index, int additional_offset = 0) {
    return LoadArrayElement<Array, TIndex, typename Array::Shape::ElementT>(
        array, Array::Shape::kHeaderSize, index, additional_offset);
  }

  template <typename TIndex>
  TNode<Object> LoadFixedArrayElement(
      TNode<FixedArray> object, TNode<TIndex> index, int additional_offset = 0,
      CheckBounds check_bounds = CheckBounds::kAlways);

  // This doesn't emit a bounds-check. As part of the security-performance
  // tradeoff, only use it if it is performance critical.
  TNode<Object> UnsafeLoadFixedArrayElement(TNode<FixedArray> object,
                                            TNode<IntPtrT> index,
                                            int additional_offset = 0) {
    return LoadFixedArrayElement(object, index, additional_offset,
                                 CheckBounds::kDebugOnly);
  }

  TNode<Object> LoadFixedArrayElement(TNode<FixedArray> object, int index,
                                      int additional_offset = 0) {
    return LoadFixedArrayElement(object, IntPtrConstant(index),
                                 additional_offset);
  }
  // This doesn't emit a bounds-check. As part of the security-performance
  // tradeoff, only use it if it is performance critical.
  TNode<Object> UnsafeLoadFixedArrayElement(TNode<FixedArray> object, int index,
                                            int additional_offset = 0) {
    return LoadFixedArrayElement(object, IntPtrConstant(index),
                                 additional_offset, CheckBounds::kDebugOnly);
  }

  TNode<Object> LoadPropertyArrayElement(TNode<PropertyArray> object,
                                         TNode<IntPtrT> index);
  TNode<IntPtrT> LoadPropertyArrayLength(TNode<PropertyArray> object);

  // Load an element from an array and untag it and return it as Word32.
  // Array is any array-like type that has a fixed header followed by
  // tagged elements.
  template <typename Array>
  TNode<Int32T> LoadAndUntagToWord32ArrayElement(TNode<Array> array,
                                                 int array_header_size,
                                                 TNode<IntPtrT> index,
                                                 int additional_offset = 0);

  // Load an array element from a FixedArray, untag it and return it as Word32.
  TNode<Int32T> LoadAndUntagToWord32FixedArrayElement(
      TNode<FixedArray> object, TNode<IntPtrT> index,
      int additional_offset = 0);

  // Load an array element from a WeakFixedArray.
  TNode<MaybeObject> LoadWeakFixedArrayElement(TNode<WeakFixedArray> object,
                                               TNode<IntPtrT> index,
                                               int additional_offset = 0);

  // Load an array element from a FixedDoubleArray.
  TNode<Float64T> LoadFixedDoubleArrayElement(
      TNode<FixedDoubleArray> object, TNode<IntPtrT> index,
      Label* if_hole = nullptr,
      MachineType machine_type = MachineType::Float64());

  // Load an array element from a FixedArray, FixedDoubleArray or a
  // NumberDictionary (depending on the |elements_kind|) and return
  // it as a tagged value. Assumes that the |index| passed a length
  // check before. Bails out to |if_accessor| if the element that
  // was found is an accessor, or to |if_hole| if the element at
  // the given |index| is not found in |elements|.
  TNode<Object> LoadFixedArrayBaseElementAsTagged(
      TNode<FixedArrayBase> elements, TNode<IntPtrT> index,
      TNode<Int32T> elements_kind, Label* if_accessor, Label* if_hole);

  // Load a feedback slot from a FeedbackVector.
  template <typename TIndex>
  TNode<MaybeObject> LoadFeedbackVectorSlot(
      TNode<FeedbackVector> feedback_vector, TNode<TIndex> slot,
      int additional_offset = 0);

  TNode<IntPtrT> LoadFeedbackVectorLength(TNode<FeedbackVector>);
  TNode<Float64T> LoadDoubleWithHoleCheck(TNode<FixedDoubleArray> array,
                                          TNode<IntPtrT> index,
                                          Label* if_hole = nullptr);

  TNode<BoolT> IsDoubleHole(TNode<Object> base, TNode<IntPtrT> offset);
  // Load Float64 value by |base| + |offset| address. If the value is a double
  // hole then jump to |if_hole|. If |machine_type| is None then only the hole
  // check is generated.
  TNode<Float64T> LoadDoubleWithHoleCheck(
      TNode<Object> base, TNode<IntPtrT> offset, Label* if_hole,
      MachineType machine_type = MachineType::Float64());
  TNode<Numeric> LoadFixedTypedArrayElementAsTagged(TNode<RawPtrT> data_pointer,
                                                    TNode<UintPtrT> index,
                                                    ElementsKind elements_kind);
  TNode<Numeric> LoadFixedTypedArrayElementAsTagged(
      TNode<RawPtrT> data_pointer, TNode<UintPtrT> index,
      TNode<Int32T> elements_kind);
  // Parts of the above, factored out for readability:
  TNode<BigInt> LoadFixedBigInt64ArrayElementAsTagged(
      TNode<RawPtrT> data_pointer, TNode<IntPtrT> offset);
  TNode<BigInt> LoadFixedBigUint64ArrayElementAsTagged(
      TNode<RawPtrT> data_pointer, TNode<IntPtrT> offset);
  // 64-bit platforms only:
  TNode<BigInt> BigIntFromInt64(TNode<IntPtrT> value);
  TNode<BigInt> BigIntFromUint64(TNode<UintPtrT> value);
  // 32-bit platforms only:
  TNode<BigInt> BigIntFromInt32Pair(TNode<IntPtrT> low, TNode<IntPtrT> high);
  TNode<BigInt> BigIntFromUint32Pair(TNode<UintPtrT> low, TNode<UintPtrT> high);

  // ScopeInfo:
  TNode<ScopeInfo> LoadScopeInfo(TNode<Context> context);
  TNode<BoolT> LoadScopeInfoHasExtensionField(TNode<ScopeInfo> scope_info);
  TNode<BoolT> LoadScopeInfoClassScopeHasPrivateBrand(
      TNode<ScopeInfo> scope_info);

  // Context manipulation:
  void StoreContextElementNoWriteBarrier(TNode<Context> context, int slot_index,
                                         TNode<Object> value);
  TNode<NativeContext> LoadNativeContext(TNode<Context> context);
  // Calling this is only valid if there's a module context in the chain.
  TNode<Context> LoadModuleContext(TNode<Context> context);

  TNode<Object> GetImportMetaObject(TNode<Context> context);

  void GotoIfContextElementEqual(TNode<Object> value,
                                 TNode<NativeContext> native_context,
                                 int slot_index, Label* if_equal) {
    GotoIf(TaggedEqual(value, LoadContextElement(native_context, slot_index)),
           if_equal);
  }

  // Loads the initial map of the the Object constructor.
  TNode<Map> LoadObjectFunctionInitialMap(TNode<NativeContext> native_context);
  TNode<Map> LoadSlowObjectWithNullPrototypeMap(
      TNode<NativeContext> native_context);
  TNode<Map> LoadCachedMap(TNode<NativeContext> native_context,
                           TNode<IntPtrT> number_of_properties, Label* runtime);

  TNode<Map> LoadJSArrayElementsMap(ElementsKind kind,
                                    TNode<NativeContext> native_context);
  TNode<Map> LoadJSArrayElementsMap(TNode<Int32T> kind,
                                    TNode<NativeContext> native_context);

  TNode<BoolT> IsJSFunctionWithPrototypeSlot(TNode<HeapObject> object);
  TNode<Uint32T> LoadFunctionKind(TNode<JSFunction> function);
  TNode<BoolT> IsGeneratorFunction(TNode<JSFunction> function);
  void BranchIfHasPrototypeProperty(TNode<JSFunction> function,
                                    TNode<Int32T> function_map_bit_field,
                                    Label* if_true, Label* if_false);
  void GotoIfPrototypeRequiresRuntimeLookup(TNode<JSFunction> function,
                                            TNode<Map> map, Label* runtime);
  // Load the "prototype" property of a JSFunction.
  TNode<HeapObject> LoadJSFunctionPrototype(TNode<JSFunction> function,
                                            Label* if_bailout);

  // Load the "code" property of a JSFunction.
  TNode<Code> LoadJSFunctionCode(TNode<JSFunction> function);

  TNode<Object> LoadSharedFunctionInfoTrustedData(
      TNode<SharedFunctionInfo> sfi);
  TNode<Object> LoadSharedFunctionInfoUntrustedData(
      TNode<SharedFunctionInfo> sfi);

  TNode<BoolT> SharedFunctionInfoHasBaselineCode(TNode<SharedFunctionInfo> sfi);

  TNode<Smi> LoadSharedFunctionInfoBuiltinId(TNode<SharedFunctionInfo> sfi);

  TNode<BytecodeArray> LoadSharedFunctionInfoBytecodeArray(
      TNode<SharedFunctionInfo> sfi);

#ifdef V8_ENABLE_WEBASSEMBLY
  TNode<WasmFunctionData> LoadSharedFunctionInfoWasmFunctionData(
      TNode<SharedFunctionInfo> sfi);
  TNode<WasmExportedFunctionData>
  LoadSharedFunctionInfoWasmExportedFunctionData(TNode<SharedFunctionInfo> sfi);
  TNode<WasmJSFunctionData> LoadSharedFunctionInfoWasmJSFunctionData(
      TNode<SharedFunctionInfo> sfi);
#endif  // V8_ENABLE_WEBASSEMBLY

  TNode<Int32T> LoadBytecodeArrayParameterCount(
      TNode<BytecodeArray> bytecode_array);
  TNode<Int32T> LoadBytecodeArrayParameterCountWithoutReceiver(
      TNode<BytecodeArray> bytecode_array);

  void StoreObjectByteNoWriteBarrier(TNode<HeapObject> object, int offset,
                                     TNode<Word32T> value);

  // Store the floating point value of a HeapNumber.
  void StoreHeapNumberValue(TNode<HeapNumber> object, TNode<Float64T> value);

  // Store a field to an object on the heap.
  void StoreObjectField(TNode<HeapObject> object, int offset, TNode<Smi> value);
  void StoreObjectField(TNode<HeapObject> object, TNode<IntPtrT> offset,
                        TNode<Smi> value);
  void StoreObjectField(TNode<HeapObject> object, int offset,
                        TNode<Object> value);
  void StoreObjectField(TNode<HeapObject> object, TNode<IntPtrT> offset,
                        TNode<Object> value);

  // Store to an indirect pointer field. This involves loading the index for
  // the pointer table entry owned by the pointed-to object (which points back
  // to it) and storing that into the specified field.
  // Stores that may require a write barrier also need to know the indirect
  // pointer tag for the field. Otherwise, it is not needed
  void StoreIndirectPointerField(TNode<HeapObject> object, int offset,
                                 IndirectPointerTag tag,
                                 TNode<ExposedTrustedObject> value);
  void StoreIndirectPointerFieldNoWriteBarrier(
      TNode<HeapObject> object, int offset, IndirectPointerTag tag,
      TNode<ExposedTrustedObject> value);

  // Store a trusted pointer field.
  // When the sandbox is enabled, these are indirect pointers using the trusted
  // pointer table. Otherwise they are regular tagged fields.
  void StoreTrustedPointerField(TNode<HeapObject> object, int offset,
                                IndirectPointerTag tag,
                                TNode<ExposedTrustedObject> value);
  void StoreTrustedPointerFieldNoWriteBarrier(
      TNode<HeapObject> object, int offset, IndirectPointerTag tag,
      TNode<ExposedTrustedObject> value);

  void ClearTrustedPointerField(TNode<HeapObject> object, int offset);

  // Store a code pointer field.
  // These are special versions of trusted pointers that, when the sandbox is
  // enabled, reference code objects through the code pointer table.
  void StoreCodePointerField(TNode<HeapObject> object, int offset,
                             TNode<Code> value) {
    StoreTrustedPointerField(object, offset, kCodeIndirectPointerTag, value);
  }
  void StoreCodePointerFieldNoWriteBarrier(TNode<HeapObject> object, int offset,
                                           TNode<Code> value) {
    StoreTrustedPointerFieldNoWriteBarrier(object, offset,
                                           kCodeIndirectPointerTag, value);
  }

  template <class T>
  void StoreObjectFieldNoWriteBarrier(TNode<HeapObject> object,
                                      TNode<IntPtrT> offset, TNode<T> value) {
    int const_offset;
    if (TryToInt32Constant(offset, &const_offset)) {
      return StoreObjectFieldNoWriteBarrier<T>(object, const_offset, value);
    }
    StoreNoWriteBarrier(MachineRepresentationOf<T>::value, object,
                        IntPtrSub(offset, IntPtrConstant(kHeapObjectTag)),
                        value);
  }
  template <class T>
  void StoreObjectFieldNoWriteBarrier(TNode<HeapObject> object, int offset,
                                      TNode<T> value) {
    if (CanBeTaggedPointer(MachineRepresentationOf<T>::value)) {
      OptimizedStoreFieldAssertNoWriteBarrier(MachineRepresentationOf<T>::value,
                                              object, offset, value);
    } else {
      OptimizedStoreFieldUnsafeNoWriteBarrier(MachineRepresentationOf<T>::value,
                                              object, offset, value);
    }
  }

  void UnsafeStoreObjectFieldNoWriteBarrier(TNode<HeapObject> object,
                                            int offset, TNode<Object> value);

  // Store the Map of an HeapObject.
  void StoreMap(TNode<HeapObject> object, TNode<Map> map);
  void StoreMapNoWriteBarrier(TNode<HeapObject> object,
                              RootIndex map_root_index);
  void StoreMapNoWriteBarrier(TNode<HeapObject> object, TNode<Map> map);
  void StoreObjectFieldRoot(TNode<HeapObject> object, int offset,
                            RootIndex root);

  // Store an array element to a FixedArray.
  void StoreFixedArrayElement(
      TNode<FixedArray> object, int index, TNode<Object> value,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER,
      CheckBounds check_bounds = CheckBounds::kAlways) {
    return StoreFixedArrayElement(object, IntPtrConstant(index), value,
                                  barrier_mode, 0, check_bounds);
  }

  void StoreFixedArrayElement(TNode<FixedArray> object, int index,
                              TNode<Smi> value,
                              CheckBounds check_bounds = CheckBounds::kAlways) {
    return StoreFixedArrayElement(object, IntPtrConstant(index),
                                  TNode<Object>{value},
                                  UNSAFE_SKIP_WRITE_BARRIER, 0, check_bounds);
  }

  template <typename TIndex>
  void StoreFixedArrayElement(
      TNode<FixedArray> array, TNode<TIndex> index, TNode<Object> value,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER,
      int additional_offset = 0,
      CheckBounds check_bounds = CheckBounds::kAlways) {
    // TODO(v8:9708): Do we want to keep both IntPtrT and UintPtrT variants?
    static_assert(std::is_same<TIndex, Smi>::value ||
                      std::is_same<TIndex, UintPtrT>::value ||
                      std::is_same<TIndex, IntPtrT>::value,
                  "Only Smi, UintPtrT or IntPtrT index is allowed");
    if (NeedsBoundsCheck(check_bounds)) {
      FixedArrayBoundsCheck(array, index, additional_offset);
    }
    StoreFixedArrayOrPropertyArrayElement(array, index, value, barrier_mode,
                                          additional_offset);
  }

  template <typename TIndex>
  void StoreFixedArrayElement(TNode<FixedArray> array, TNode<TIndex> index,
                              TNode<Smi> value, int additional_offset = 0) {
    static_assert(std::is_same<TIndex, Smi>::value ||
                      std::is_same<TIndex, IntPtrT>::value,
                  "Only Smi or IntPtrT indeces is allowed");
    StoreFixedArrayElement(array, index, TNode<Object>{value},
                           UNSAFE_SKIP_WRITE_BARRIER, additional_offset);
  }

  // These don't emit a bounds-check. As part of the security-performance
  // tradeoff, only use it if it is performance critical.
  void UnsafeStoreFixedArrayElement(
      TNode<FixedArray> object, int index, TNode<Object> value,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER) {
    return StoreFixedArrayElement(object, IntPtrConstant(index), value,
                                  barrier_mode, 0, CheckBounds::kDebugOnly);
  }
  template <typename Array>
  void UnsafeStoreArrayElement(
      TNode<Array> object, int index,
      TNode<typename Array::Shape::ElementT> value,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER) {
    DCHECK(barrier_mode == SKIP_WRITE_BARRIER ||
           barrier_mode == UNSAFE_SKIP_WRITE_BARRIER ||
           barrier_mode == UPDATE_WRITE_BARRIER);
    // TODO(jgruber): This is just a barebones implementation taken from
    // StoreFixedArrayOrPropertyArrayElement. We can make it more robust and
    // generic if needed.
    int offset = Array::OffsetOfElementAt(index);
    if (barrier_mode == UNSAFE_SKIP_WRITE_BARRIER) {
      UnsafeStoreObjectFieldNoWriteBarrier(object, offset, value);
    } else if (barrier_mode == SKIP_WRITE_BARRIER) {
      StoreObjectFieldNoWriteBarrier(object, offset, value);
    } else if (barrier_mode == UPDATE_WRITE_BARRIER) {
      StoreObjectField(object, offset, value);
    } else {
      UNREACHABLE();
    }
  }
  template <typename Array>
  void UnsafeStoreArrayElement(
      TNode<Array> object, TNode<Smi> index,
      TNode<typename Array::Shape::ElementT> value,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER) {
    DCHECK(barrier_mode == SKIP_WRITE_BARRIER ||
           barrier_mode == UPDATE_WRITE_BARRIER);
    // TODO(jgruber): This is just a barebones implementation taken from
    // StoreFixedArrayOrPropertyArrayElement. We can make it more robust and
    // generic if needed.
    TNode<IntPtrT> offset = ElementOffsetFromIndex(index, PACKED_ELEMENTS,
                                                   Array::Shape::kHeaderSize);
    if (barrier_mode == SKIP_WRITE_BARRIER) {
      StoreObjectFieldNoWriteBarrier(object, offset, value);
    } else if (barrier_mode == UPDATE_WRITE_BARRIER) {
      StoreObjectField(object, offset, value);
    } else {
      UNREACHABLE();
    }
  }

  void UnsafeStoreFixedArrayElement(TNode<FixedArray> object, int index,
                                    TNode<Smi> value) {
    return StoreFixedArrayElement(object, IntPtrConstant(index), value,
                                  UNSAFE_SKIP_WRITE_BARRIER, 0,
                                  CheckBounds::kDebugOnly);
  }

  void UnsafeStoreFixedArrayElement(
      TNode<FixedArray> array, TNode<IntPtrT> index, TNode<Object> value,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER,
      int additional_offset = 0) {
    return StoreFixedArrayElement(array, index, value, barrier_mode,
                                  additional_offset, CheckBounds::kDebugOnly);
  }

  void UnsafeStoreFixedArrayElement(TNode<FixedArray> array,
                                    TNode<IntPtrT> index, TNode<Smi> value,
                                    int additional_offset) {
    return StoreFixedArrayElement(array, index, value,
                                  UNSAFE_SKIP_WRITE_BARRIER, additional_offset,
                                  CheckBounds::kDebugOnly);
  }

  void StorePropertyArrayElement(TNode<PropertyArray> array,
                                 TNode<IntPtrT> index, TNode<Object> value) {
    StoreFixedArrayOrPropertyArrayElement(array, index, value,
                                          UPDATE_WRITE_BARRIER);
  }

  template <typename TIndex>
  void StoreFixedDoubleArrayElement(
      TNode<FixedDoubleArray> object, TNode<TIndex> index,
      TNode<Float64T> value, CheckBounds check_bounds = CheckBounds::kAlways);

  void StoreDoubleHole(TNode<HeapObject> object, TNode<IntPtrT> offset);
  void StoreFixedDoubleArrayHole(TNode<FixedDoubleArray> array,
                                 TNode<IntPtrT> index);
  void StoreFeedbackVectorSlot(
      TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot,
      TNode<AnyTaggedT> value,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER,
      int additional_offset = 0);

  void StoreSharedObjectField(TNode<HeapObject> object, TNode<IntPtrT> offset,
                              TNode<Object> value);

  void StoreJSSharedStructPropertyArrayElement(TNode<PropertyArray> array,
                                               TNode<IntPtrT> index,
                                               TNode<Object> value) {
    StoreFixedArrayOrPropertyArrayElement(array, index, value);
  }

  // EnsureArrayPushable verifies that receiver with this map is:
  //   1. Is not a prototype.
  //   2. Is not a dictionary.
  //   3. Has a writeable length property.
  // It returns ElementsKind as a node for further division into cases.
  TNode<Int32T> EnsureArrayPushable(TNode<Context> context, TNode<Map> map,
                                    Label* bailout);

  void TryStoreArrayElement(ElementsKind kind, Label* bailout,
                            TNode<FixedArrayBase> elements, TNode<BInt> index,
                            TNode<Object> value);
  // Consumes args into the array, and returns tagged new length.
  TNode<Smi> BuildAppendJSArray(ElementsKind kind, TNode<JSArray> array,
                                CodeStubArguments* args,
                                TVariable<IntPtrT>* arg_index, Label* bailout);
  // Pushes value onto the end of array.
  void BuildAppendJSArray(ElementsKind kind, TNode<JSArray> array,
                          TNode<Object> value, Label* bailout);

  void StoreFieldsNoWriteBarrier(TNode<IntPtrT> start_address,
                                 TNode<IntPtrT> end_address,
                                 TNode<Object> value);

  // Marks the FixedArray copy-on-write without moving it.
  void MakeFixedArrayCOW(TNode<FixedArray> array);

  TNode<Cell> AllocateCellWithValue(
      TNode<Object> value, WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  TNode<Cell> AllocateSmiCell(int value = 0) {
    return AllocateCellWithValue(SmiConstant(value), SKIP_WRITE_BARRIER);
  }

  TNode<Object> LoadCellValue(TNode<Cell> cell);

  void StoreCellValue(TNode<Cell> cell, TNode<Object> value,
                      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Allocate a HeapNumber without initializing its value.
  TNode<HeapNumber> AllocateHeapNumber();
  // Allocate a HeapNumber with a specific value.
  TNode<HeapNumber> AllocateHeapNumberWithValue(TNode<Float64T> value);
  TNode<HeapNumber> AllocateHeapNumberWithValue(double value) {
    return AllocateHeapNumberWithValue(Float64Constant(value));
  }

  // Allocate a BigInt with {length} digits. Sets the sign bit to {false}.
  // Does not initialize the digits.
  TNode<BigInt> AllocateBigInt(TNode<IntPtrT> length);
  // Like above, but allowing custom bitfield initialization.
  TNode<BigInt> AllocateRawBigInt(TNode<IntPtrT> length);
  void StoreBigIntBitfield(TNode<BigInt> bigint, TNode<Word32T> bitfield);
  void StoreBigIntDigit(TNode<BigInt> bigint, intptr_t digit_index,
                        TNode<UintPtrT> digit);
  void StoreBigIntDigit(TNode<BigInt> bigint, TNode<IntPtrT> digit_index,
                        TNode<UintPtrT> digit);

  TNode<Word32T> LoadBigIntBitfield(TNode<BigInt> bigint);
  TNode<UintPtrT> LoadBigIntDigit(TNode<BigInt> bigint, intptr_t digit_index);
  TNode<UintPtrT> LoadBigIntDigit(TNode<BigInt> bigint,
                                  TNode<IntPtrT> digit_index);

  // Allocate a ByteArray with the given non-zero length.
  TNode<ByteArray> AllocateNonEmptyByteArray(
      TNode<UintPtrT> length, AllocationFlags flags = AllocationFlag::kNone);

  // Allocate a ByteArray with the given length.
  TNode<ByteArray> AllocateByteArray(
      TNode<UintPtrT> length, AllocationFlags flags = AllocationFlag::kNone);

  // Allocate a SeqOneByteString with the given length.
  TNode<String> AllocateSeqOneByteString(
      uint32_t length, AllocationFlags flags = AllocationFlag::kNone);
  using TorqueGeneratedExportedMacrosAssembler::AllocateSeqOneByteString;

  // Allocate a SeqTwoByteString with the given length.
  TNode<String> AllocateSeqTwoByteString(
      uint32_t length, AllocationFlags flags = AllocationFlag::kNone);
  using TorqueGeneratedExportedMacrosAssembler::AllocateSeqTwoByteString;

  // Allocate a SlicedOneByteString with the given length, parent and offset.
  // |length| and |offset| are expected to be tagged.

  TNode<String> AllocateSlicedOneByteString(TNode<Uint32T> length,
                                            TNode<String> parent,
                                            TNode<Smi> offset);
  // Allocate a SlicedTwoByteString with the given length, parent and offset.
  // |length| and |offset| are expected to be tagged.
  TNode<String> AllocateSlicedTwoByteString(TNode<Uint32T> length,
                                            TNode<String> parent,
                                            TNode<Smi> offset);

  TNode<NameDictionary> AllocateNameDictionary(int at_least_space_for);
  TNode<NameDictionary> AllocateNameDictionary(
      TNode<IntPtrT> at_least_space_for,
      AllocationFlags = AllocationFlag::kNone);
  TNode<NameDictionary> AllocateNameDictionaryWithCapacity(
      TNode<IntPtrT> capacity, AllocationFlags = AllocationFlag::kNone);

  TNode<PropertyDictionary> AllocatePropertyDictionary(int at_least_space_for);
  TNode<PropertyDictionary> AllocatePropertyDictionary(
      TNode<IntPtrT> at_least_space_for,
      AllocationFlags = AllocationFlag::kNone);
  TNode<PropertyDictionary> AllocatePropertyDictionaryWithCapacity(
      TNode<IntPtrT> capacity, AllocationFlags = AllocationFlag::kNone);

  TNode<NameDictionary> CopyNameDictionary(TNode<NameDictionary> dictionary,
                                           Label* large_object_fallback);

  TNode<OrderedHashSet> AllocateOrderedHashSet();
  TNode<OrderedHashSet> AllocateOrderedHashSet(TNode<IntPtrT> capacity);

  TNode<OrderedHashMap> AllocateOrderedHashMap();

  // Allocates an OrderedNameDictionary of the given capacity. This guarantees
  // that |capacity| entries can be added without reallocating.
  TNode<OrderedNameDictionary> AllocateOrderedNameDictionary(
      TNode<IntPtrT> capacity);
  TNode<OrderedNameDictionary> AllocateOrderedNameDictionary(int capacity);

  TNode<JSObject> AllocateJSObjectFromMap(
      TNode<Map> map,
      std::optional<TNode<HeapObject>> properties = std::nullopt,
      std::optional<TNode<FixedArray>> elements = std::nullopt,
      AllocationFlags flags = AllocationFlag::kNone,
      SlackTrackingMode slack_tracking_mode = kNoSlackTracking);

  void InitializeJSObjectFromMap(
      TNode<HeapObject> object, TNode<Map> map, TNode<IntPtrT> instance_size,
      std::optional<TNode<HeapObject>> properties = std::nullopt,
      std::optional<TNode<FixedArray>> elements = std::nullopt,
      SlackTrackingMode slack_tracking_mode = kNoSlackTracking);

  void InitializeJSObjectBodyWithSlackTracking(TNode<HeapObject> object,
                                               TNode<Map> map,
                                               TNode<IntPtrT> instance_size);
  void InitializeJSObjectBodyNoSlackTracking(
      TNode<HeapObject> object, TNode<Map> map, TNode<IntPtrT> instance_size,
      int start_offset = JSObject::kHeaderSize);

  TNode<BoolT> IsValidFastJSArrayCapacity(TNode<IntPtrT> capacity);

  //
  // Allocate and return a JSArray with initialized header fields and its
  // uninitialized elements.
  std::pair<TNode<JSArray>, TNode<FixedArrayBase>>
  AllocateUninitializedJSArrayWithElements(
      ElementsKind kind, TNode<Map> array_map, TNode<Smi> length,
      std::optional<TNode<AllocationSite>> allocation_site,
      TNode<IntPtrT> capacity,
      AllocationFlags allocation_flags = AllocationFlag::kNone,
      int array_header_size = JSArray::kHeaderSize);

  // Allocate a JSArray and fill elements with the hole.
  TNode<JSArray> AllocateJSArray(
      ElementsKind kind, TNode<Map> array_map, TNode<IntPtrT> capacity,
      TNode<Smi> length, std::optional<TNode<AllocationSite>> allocation_site,
      AllocationFlags allocation_flags = AllocationFlag::kNone);
  TNode<JSArray> AllocateJSArray(
      ElementsKind kind, TNode<Map> array_map, TNode<Smi> capacity,
      TNode<Smi> length, std::optional<TNode<AllocationSite>> allocation_site,
      AllocationFlags allocation_flags = AllocationFlag::kNone) {
    return AllocateJSArray(kind, array_map, PositiveSmiUntag(capacity), length,
                           allocation_site, allocation_flags);
  }
  TNode<JSArray> AllocateJSArray(
      ElementsKind kind, TNode<Map> array_map, TNode<Smi> capacity,
      TNode<Smi> length,
      AllocationFlags allocation_flags = AllocationFlag::kNone) {
    return AllocateJSArray(kind, array_map, PositiveSmiUntag(capacity), length,
                           std::nullopt, allocation_flags);
  }
  TNode<JSArray> AllocateJSArray(
      ElementsKind kind, TNode<Map> array_map, TNode<IntPtrT> capacity,
      TNode<Smi> length,
      AllocationFlags allocation_flags = AllocationFlag::kNone) {
    return AllocateJSArray(kind, array_map, capacity, length, std::nullopt,
                           allocation_flags);
  }

  // Allocate a JSArray and initialize the header fields.
  TNode<JSArray> AllocateJSArray(
      TNode<Map> array_map, TNode<FixedArrayBase> elements, TNode<Smi> length,
      std::optional<TNode<AllocationSite>> allocation_site = std::nullopt,
      int array_header_size = JSArray::kHeaderSize);

  enum class HoleConversionMode { kDontConvert, kConvertToUndefined };
  // Clone a fast JSArray |array| into a new fast JSArray.
  // |convert_holes| tells the function to convert holes into undefined or not.
  // If |convert_holes| is set to kConvertToUndefined, but the function did not
  // find any hole in |array|, the resulting array will have the same elements
  // kind as |array|. If the function did find a hole, it will convert holes in
  // |array| to undefined in the resulting array, who will now have
  // PACKED_ELEMENTS kind.
  // If |convert_holes| is set kDontConvert, holes are also copied to the
  // resulting array, who will have the same elements kind as |array|. The
  // function generates significantly less code in this case.
  TNode<JSArray> CloneFastJSArray(
      TNode<Context> context, TNode<JSArray> array,
      std::optional<TNode<AllocationSite>> allocation_site = std::nullopt,
      HoleConversionMode convert_holes = HoleConversionMode::kDontConvert);

  TNode<JSArray> ExtractFastJSArray(TNode<Context> context,
                                    TNode<JSArray> array, TNode<BInt> begin,
                                    TNode<BInt> count);

  template <typename TIndex>
  TNode<FixedArrayBase> AllocateFixedArray(
      ElementsKind kind, TNode<TIndex> capacity,
      AllocationFlags flags = AllocationFlag::kNone,
      std::optional<TNode<Map>> fixed_array_map = std::nullopt);

  template <typename Function>
  TNode<Object> FastCloneJSObject(TNode<HeapObject> source,
                                  TNode<Map> source_map, TNode<Map> target_map,
                                  const Function& materialize_target,
                                  bool target_is_new);

  TNode<NativeContext> GetCreationContextFromMap(TNode<Map> map,
                                                 Label* if_bailout);
  TNode<NativeContext> GetCreationContext(TNode<JSReceiver> receiver,
                                          Label* if_bailout);
  TNode<NativeContext> GetFunctionRealm(TNode<Context> context,
                                        TNode<JSReceiver> receiver,
                                        Label* if_bailout);
  TNode<Object> GetConstructor(TNode<Map> map);

  void FindNonDefaultConstructor(TNode<JSFunction> this_function,
                                 TVariable<Object>& constructor,
                                 Label* found_default_base_ctor,
                                 Label* found_something_else);

  TNode<Map> GetInstanceTypeMap(InstanceType instance_type);

  TNode<FixedArray> AllocateUninitializedFixedArray(intptr_t capacity) {
    return UncheckedCast<FixedArray>(AllocateFixedArray(
        PACKED_ELEMENTS, IntPtrConstant(capacity), AllocationFlag::kNone));
  }

  TNode<FixedArray> AllocateZeroedFixedArray(TNode<IntPtrT> capacity) {
    TNode<FixedArray> result = UncheckedCast<FixedArray>(
        AllocateFixedArray(PACKED_ELEMENTS, capacity));
    FillEntireFixedArrayWithSmiZero(PACKED_ELEMENTS, result, capacity);
    return result;
  }

  TNode<FixedDoubleArray> AllocateZeroedFixedDoubleArray(
      TNode<IntPtrT> capacity) {
    TNode<FixedDoubleArray> result = UncheckedCast<FixedDoubleArray>(
        AllocateFixedArray(PACKED_DOUBLE_ELEMENTS, capacity));
    FillEntireFixedDoubleArrayWithZero(result, capacity);
    return result;
  }

  TNode<FixedArray> AllocateFixedArrayWithHoles(
      TNode<IntPtrT> capacity, AllocationFlags flags = AllocationFlag::kNone) {
    TNode<FixedArray> result = UncheckedCast<FixedArray>(
        AllocateFixedArray(PACKED_ELEMENTS, capacity, flags));
    FillFixedArrayWithValue(PACKED_ELEMENTS, result, IntPtrConstant(0),
                            capacity, RootIndex::kTheHoleValue);
    return result;
  }

  TNode<FixedDoubleArray> AllocateFixedDoubleArrayWithHoles(
      TNode<IntPtrT> capacity, AllocationFlags flags = AllocationFlag::kNone) {
    TNode<FixedDoubleArray> result = UncheckedCast<FixedDoubleArray>(
        AllocateFixedArray(PACKED_DOUBLE_ELEMENTS, capacity, flags));
    FillFixedArrayWithValue(PACKED_DOUBLE_ELEMENTS, result, IntPtrConstant(0),
                            capacity, RootIndex::kTheHoleValue);
    return result;
  }

  TNode<PropertyArray> AllocatePropertyArray(TNode<IntPtrT> capacity);

  // TODO(v8:9722): Return type should be JSIteratorResult
  TNode<JSObject> AllocateJSIteratorResult(TNode<Context> context,
                                           TNode<Object> value,
                                           TNode<Boolean> done);

  // TODO(v8:9722): Return type should be JSIteratorResult
  TNode<JSObject> AllocateJSIteratorResultForEntry(TNode<Context> context,
                                                   TNode<Object> key,
                                                   TNode<Object> value);

  TNode<JSObject> AllocatePromiseWithResolversResult(TNode<Context> context,
                                                     TNode<Object> promise,
                                                     TNode<Object> resolve,
                                                     TNode<Object> reject);

  TNode<JSReceiver> ArraySpeciesCreate(TNode<Context> context,
                                       TNode<Object> originalArray,
                                       TNode<Number> len);

  template <typename TIndex>
  void FillFixedArrayWithValue(ElementsKind kind, TNode<FixedArrayBase> array,
                               TNode<TIndex> from_index, TNode<TIndex> to_index,
                               RootIndex value_root_index);
  template <typename TIndex>
  void FillFixedArrayWithValue(ElementsKind kind, TNode<FixedArray> array,
                               TNode<TIndex> from_index, TNode<TIndex> to_index,
                               RootIndex value_root_index) {
    FillFixedArrayWithValue(kind, UncheckedCast<FixedArrayBase>(array),
                            from_index, to_index, value_root_index);
  }

  // Uses memset to effectively initialize the given FixedArray with zeroes.
  void FillFixedArrayWithSmiZero(ElementsKind kind, TNode<FixedArray> array,
                                 TNode<IntPtrT> start, TNode<IntPtrT> length);
  void FillEntireFixedArrayWithSmiZero(ElementsKind kind,
                                       TNode<FixedArray> array,
                                       TNode<IntPtrT> length) {
    CSA_DCHECK(this,
               WordEqual(length, LoadAndUntagFixedArrayBaseLength(array)));
    FillFixedArrayWithSmiZero(kind, array, IntPtrConstant(0), length);
  }

  void FillFixedDoubleArrayWithZero(TNode<FixedDoubleArray> array,
                                    TNode<IntPtrT> start,
                                    TNode<IntPtrT> length);
  void FillEntireFixedDoubleArrayWithZero(TNode<FixedDoubleArray> array,
                                          TNode<IntPtrT> length) {
    CSA_DCHECK(this,
               WordEqual(length, LoadAndUntagFixedArrayBaseLength(array)));
    FillFixedDoubleArrayWithZero(array, IntPtrConstant(0), length);
  }

  void FillPropertyArrayWithUndefined(TNode<PropertyArray> array,
                                      TNode<IntPtrT> from_index,
                                      TNode<IntPtrT> to_index);

  enum class DestroySource { kNo, kYes };

  // Increment the call count for a CALL_IC or construct call.
  // The call count is located at feedback_vector[slot_id + 1].
  void IncrementCallCount(TNode<FeedbackVector> feedback_vector,
                          TNode<UintPtrT> slot_id);

  // Specify DestroySource::kYes if {from_array} is being supplanted by
  // {to_array}. This offers a slight performance benefit by simply copying the
  // array word by word. The source may be destroyed at the end of this macro.
  //
  // Otherwise, specify DestroySource::kNo for operations where an Object is
  // being cloned, to ensure that mutable HeapNumbers are unique between the
  // source and cloned object.
  void CopyPropertyArrayValues(TNode<HeapObject> from_array,
                               TNode<PropertyArray> to_array,
                               TNode<IntPtrT> length,
                               WriteBarrierMode barrier_mode,
                               DestroySource destroy_source);

  // Copies all elements from |from_array| of |length| size to
  // |to_array| of the same size respecting the elements kind.
  template <typename TIndex>
  void CopyFixedArrayElements(
      ElementsKind kind, TNode<FixedArrayBase> from_array,
      TNode<FixedArrayBase> to_array, TNode<TIndex> length,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER) {
    CopyFixedArrayElements(kind, from_array, kind, to_array,
                           IntPtrOrSmiConstant<TIndex>(0), length, length,
                           barrier_mode);
  }

  // Copies |element_count| elements from |from_array| starting from element
  // zero to |to_array| of |capacity| size respecting both array's elements
  // kinds.
  template <typename TIndex>
  void CopyFixedArrayElements(
      ElementsKind from_kind, TNode<FixedArrayBase> from_array,
      ElementsKind to_kind, TNode<FixedArrayBase> to_array,
      TNode<TIndex> element_count, TNode<TIndex> capacity,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER) {
    CopyFixedArrayElements(from_kind, from_array, to_kind, to_array,
                           IntPtrOrSmiConstant<TIndex>(0), element_count,
                           capacity, barrier_mode);
  }

  // Copies |element_count| elements from |from_array| starting from element
  // |first_element| to |to_array| of |capacity| size respecting both array's
  // elements kinds.
  // |convert_holes| tells the function whether to convert holes to undefined.
  // |var_holes_converted| can be used to signify that the conversion happened
  // (i.e. that there were holes). If |convert_holes_to_undefined| is
  // HoleConversionMode::kConvertToUndefined, then it must not be the case that
  // IsDoubleElementsKind(to_kind).
  template <typename TIndex>
  void CopyFixedArrayElements(
      ElementsKind from_kind, TNode<FixedArrayBase> from_array,
      ElementsKind to_kind, TNode<FixedArrayBase> to_array,
      TNode<TIndex> first_element, TNode<TIndex> element_count,
      TNode<TIndex> capacity,
      WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER,
      HoleConversionMode convert_holes = HoleConversionMode::kDontConvert,
      TVariable<BoolT>* var_holes_converted = nullptr);

  void JumpIfPointersFromHereAreInteresting(TNode<Object> object,
                                            Label* interesting);

  // Efficiently copy elements within a single array. The regions
  // [src_index, src_index + length) and [dst_index, dst_index + length)
  // can be overlapping.
  void MoveElements(ElementsKind kind, TNode<FixedArrayBase> elements,
                    TNode<IntPtrT> dst_index, TNode<IntPtrT> src_index,
                    TNode<IntPtrT> length);

  // Efficiently copy elements from one array to another. The ElementsKind
  // needs to be the same. Copy from src_elements at
  // [src_index, src_index + length) to dst_elements at
  // [dst_index, dst_index + length).
  // The function decides whether it can use memcpy. In case it cannot,
  // |write_barrier| can help it to skip write barrier. SKIP_WRITE_BARRIER is
  // only safe when copying to new space, or when copying to old space and the
  // array does not contain object pointers.
  void CopyElements(ElementsKind kind, TNode<FixedArrayBase> dst_elements,
                    TNode<IntPtrT> dst_index,
                    TNode<FixedArrayBase> src_elements,
                    TNode<IntPtrT> src_index, TNode<IntPtrT> length,
                    WriteBarrierMode write_barrier = UPDATE_WRITE_BARRIER);

  void CopyRange(TNode<HeapObject> dst_object, int dst_offset,
                 TNode<HeapObject> src_object, int src_offset,
                 TNode<IntPtrT> length_in_tagged,
                 WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  TNode<FixedArray> HeapObjectToFixedArray(TNode<HeapObject> base,
                                           Label* cast_fail);

  TNode<FixedDoubleArray> HeapObjectToFixedDoubleArray(TNode<HeapObject> base,
                                                       Label* cast_fail) {
    GotoIf(TaggedNotEqual(LoadMap(base), FixedDoubleArrayMapConstant()),
           cast_fail);
    return UncheckedCast<FixedDoubleArray>(base);
  }

  TNode<ArrayList> AllocateArrayList(TNode<Smi> size);
  TNode<ArrayList> ArrayListEnsureSpace(TNode<ArrayList> array,
                                        TNode<Smi> length);
  TNode<ArrayList> ArrayListAdd(TNode<ArrayList> array, TNode<Object> object);
  void ArrayListSet(TNode<ArrayList> array, TNode<Smi> index,
                    TNode<Object> object);
  TNode<Smi> ArrayListGetLength(TNode<ArrayList> array);
  void ArrayListSetLength(TNode<ArrayList> array, TNode<Smi> length);
  // TODO(jgruber): Rename to ArrayListToFixedArray.
  TNode<FixedArray> ArrayListElements(TNode<ArrayList> array);

  template <typename T>
  bool ClassHasMapConstant() {
    return false;
  }

  template <typename T>
  TNode<Map> GetClassMapConstant() {
    UNREACHABLE();
    return TNode<Map>();
  }

  enum class ExtractFixedArrayFlag {
    kFixedArrays = 1,
    kFixedDoubleArrays = 2,
    kDontCopyCOW = 4,
    kAllFixedArrays = kFixedArrays | kFixedDoubleArrays,
    kAllFixedArraysDontCopyCOW = kAllFixedArrays | kDontCopyCOW
  };

  using ExtractFixedArrayFlags = base::Flags<ExtractFixedArrayFlag>;

  // Copy a portion of an existing FixedArray or FixedDoubleArray into a new
  // array, including special appropriate handling for empty arrays and COW
  // arrays. The result array will be of the same type as the original array.
  //
  // * |source| is either a FixedArray or FixedDoubleArray from which to copy
  // elements.
  // * |first| is the starting element index to copy from, if nullptr is passed
  // then index zero is used by default.
  // * |count| is the number of elements to copy out of the source array
  // starting from and including the element indexed by |start|. If |count| is
  // nullptr, then all of the elements from |start| to the end of |source| are
  // copied.
  // * |capacity| determines the size of the allocated result array, with
  // |capacity| >= |count|. If |capacity| is nullptr, then |count| is used as
  // the destination array's capacity.
  // * |extract_flags| determines whether FixedArrays, FixedDoubleArrays or both
  // are detected and copied. Although it's always correct to pass
  // kAllFixedArrays, the generated code is more compact and efficient if the
  // caller can specify whether only FixedArrays or FixedDoubleArrays will be
  // passed as the |source| parameter.
  // * |parameter_mode| determines the parameter mode of |first|, |count| and
  // |capacity|.
  // * If |var_holes_converted| is given, any holes will be converted to
  // undefined and the variable will be set according to whether or not there
  // were any hole.
  // * If |source_elements_kind| is given, the function will try to use the
  // runtime elements kind of source to make copy faster. More specifically, it
  // can skip write barriers.
  template <typename TIndex>
  TNode<FixedArrayBase> ExtractFixedArray(
      TNode<FixedArrayBase> source, std::optional<TNode<TIndex>> first,
      std::optional<TNode<TIndex>> count = std::nullopt,
      std::optional<TNode<TIndex>> capacity = std::nullopt,
      ExtractFixedArrayFlags extract_flags =
          ExtractFixedArrayFlag::kAllFixedArrays,
      TVariable<BoolT>* var_holes_converted = nullptr,
      std::optional<TNode<Int32T>> source_elements_kind = std::nullopt);

  // Copy a portion of an existing FixedArray or FixedDoubleArray into a new
  // FixedArray, including special appropriate handling for COW arrays.
  // * |source| is either a FixedArray or FixedDoubleArray from which to copy
  // elements. |source| is assumed to be non-empty.
  // * |first| is the starting element index to copy from.
  // * |count| is the number of elements to copy out of the source array
  // starting from and including the element indexed by |start|.
  // * |capacity| determines the size of the allocated result array, with
  // |capacity| >= |count|.
  // * |source_map| is the map of the |source|.
  // * |from_kind| is the elements kind that is consistent with |source| being
  // a FixedArray or FixedDoubleArray. This function only cares about double vs.
  // non-double, so as to distinguish FixedDoubleArray vs. FixedArray. It does
  // not care about holeyness. For example, when |source| is a FixedArray,
  // PACKED/HOLEY_ELEMENTS can be used, but not PACKED_DOUBLE_ELEMENTS.
  // * |allocation_flags| and |extract_flags| influence how the target
  // FixedArray is allocated.
  // * |convert_holes| is used to signify that the target array should use
  // undefined in places of holes.
  // * If |convert_holes| is true and |var_holes_converted| not nullptr, then
  // |var_holes_converted| is used to signal whether any holes were found and
  // converted. The caller should use this information to decide which map is
  // compatible with the result array. For example, if the input was of
  // HOLEY_SMI_ELEMENTS kind, and a conversion took place, the result will be
  // compatible only with HOLEY_ELEMENTS and PACKED_ELEMENTS.
  template <typename TIndex>
  TNode<FixedArray> ExtractToFixedArray(
      TNode<FixedArrayBase> source, TNode<TIndex> first, TNode<TIndex> count,
      TNode<TIndex> capacity, TNode<Map> source_map, ElementsKind from_kind,
      AllocationFlags allocation_flags, ExtractFixedArrayFlags extract_flags,
      HoleConversionMode convert_holes,
      TVariable<BoolT>* var_holes_converted = nullptr,
      std::optional<TNode<Int32T>> source_runtime_kind = std::nullopt);

  // Attempt to copy a FixedDoubleArray to another FixedDoubleArray. In the case
  // where the source array has a hole, produce a FixedArray instead where holes
  // are replaced with undefined.
  // * |source| is a FixedDoubleArray from which to copy elements.
  // * |first| is the starting element index to copy from.
  // * |count| is the number of elements to copy out of the source array
  // starting from and including the element indexed by |start|.
  // * |capacity| determines the size of the allocated result array, with
  // |capacity| >= |count|.
  // * |source_map| is the map of |source|. It will be used as the map of the
  // target array if the target can stay a FixedDoubleArray. Otherwise if the
  // target array needs to be a FixedArray, the FixedArrayMap will be used.
  // * |var_holes_converted| is used to signal whether a FixedAray
  // is produced or not.
  // * |allocation_flags| and |extract_flags| influence how the target array is
  // allocated.
  template <typename TIndex>
  TNode<FixedArrayBase> ExtractFixedDoubleArrayFillingHoles(
      TNode<FixedArrayBase> source, TNode<TIndex> first, TNode<TIndex> count,
      TNode<TIndex> capacity, TNode<Map> source_map,
      TVariable<BoolT>* var_holes_converted, AllocationFlags allocation_flags,
      ExtractFixedArrayFlags extract_flags);

  // Copy the entire contents of a FixedArray or FixedDoubleArray to a new
  // array, including special appropriate handling for empty arrays and COW
  // arrays.
  //
  // * |source| is either a FixedArray or FixedDoubleArray from which to copy
  // elements.
  // * |extract_flags| determines whether FixedArrays, FixedDoubleArrays or both
  // are detected and copied. Although it's always correct to pass
  // kAllFixedArrays, the generated code is more compact and efficient if the
  // caller can specify whether only FixedArrays or FixedDoubleArrays will be
  // passed as the |source| parameter.
  TNode<FixedArrayBase> CloneFixedArray(
      TNode<FixedArrayBase> source,
      ExtractFixedArrayFlags flags =
          ExtractFixedArrayFlag::kAllFixedArraysDontCopyCOW);

  // Loads an element from |array| of |from_kind| elements by given |offset|
  // (NOTE: not index!), does a hole check if |if_hole| is provided and
  // converts the value so that it becomes ready for storing to array of
  // |to_kind| elements.
  template <typename TResult>
  TNode<TResult> LoadElementAndPrepareForStore(TNode<FixedArrayBase> array,
                                               TNode<IntPtrT> offset,
                                               ElementsKind from_kind,
                                               ElementsKind to_kind,
                                               Label* if_hole);

  template <typename TIndex>
  TNode<TIndex> CalculateNewElementsCapacity(TNode<TIndex> old_capacity);

  // Tries to grow the |elements| array of given |object| to store the |key|
  // or bails out if the growing gap is too big. Returns new elements.
  TNode<FixedArrayBase> TryGrowElementsCapacity(TNode<HeapObject> object,
                                                TNode<FixedArrayBase> elements,
                                                ElementsKind kind,
                                                TNode<Smi> key, Label* bailout);

  // Tries to grow the |capacity|-length |elements| array of given |object|
  // to store the |key| or bails out if the growing gap is too big. Returns
  // new elements.
  template <typename TIndex>
  TNode<FixedArrayBase> TryGrowElementsCapacity(TNode<HeapObject> object,
                                                TNode<FixedArrayBase> elements,
                                                ElementsKind kind,
                                                TNode<TIndex> key,
                                                TNode<TIndex> capacity,
                                                Label* bailout);

  // Grows elements capacity of given object. Returns new elements.
  template <typename TIndex>
  TNode<FixedArrayBase> GrowElementsCapacity(
      TNode<HeapObject> object, TNode<FixedArrayBase> elements,
      ElementsKind from_kind, ElementsKind to_kind, TNode<TIndex> capacity,
      TNode<TIndex> new_capacity, Label* bailout);

  // Given a need to grow by |growth|, allocate an appropriate new capacity
  // if necessary, and return a new elements FixedArray object. Label |bailout|
  // is followed for allocation failure.
  void PossiblyGrowElementsCapacity(ElementsKind kind, TNode<HeapObject> array,
                                    TNode<BInt> length,
                                    TVariable<FixedArrayBase>* var_elements,
                                    TNode<BInt> growth, Label* bailout);

  // Allocation site manipulation
  void InitializeAllocationMemento(TNode<HeapObject> base,
                                   TNode<IntPtrT> base_allocation_size,
                                   TNode<AllocationSite> allocation_site);

  TNode<IntPtrT> TryTaggedToInt32AsIntPtr(TNode<Object> value,
                                          Label* if_not_possible);
  TNode<Float64T> TryTaggedToFloat64(TNode<Object> value,
                                     Label* if_valueisnotnumber);
  TNode<Float64T> TruncateTaggedToFloat64(TNode<Context> context,
                                          TNode<Object> value);
  TNode<Word32T> TruncateTaggedToWord32(TNode<Context> context,
                                        TNode<Object> value);
  void TaggedToWord32OrBigInt(TNode<Context> context, TNode<Object> value,
                              Label* if_number, TVariable<Word32T>* var_word32,
                              Label* if_bigint, Label* if_bigint64,
                              TVariable<BigInt>* var_maybe_bigint);
  struct FeedbackValues {
    TVariable<Smi>* var_feedback = nullptr;
    const LazyNode<HeapObject>* maybe_feedback_vector = nullptr;
    TNode<UintPtrT>* slot = nullptr;
    UpdateFeedbackMode update_mode = UpdateFeedbackMode::kNoFeedback;
  };
  void TaggedToWord32OrBigIntWithFeedback(TNode<Context> context,
                                          TNode<Object> value, Label* if_number,
                                          TVariable<Word32T>* var_word32,
                                          Label* if_bigint, Label* if_bigint64,
                                          TVariable<BigInt>* var_maybe_bigint,
                                          const FeedbackValues& feedback);
  void TaggedPointerToWord32OrBigIntWithFeedback(
      TNode<Context> context, TNode<HeapObject> pointer, Label* if_number,
      TVariable<Word32T>* var_word32, Label* if_bigint, Label* if_bigint64,
      TVariable<BigInt>* var_maybe_bigint, const FeedbackValues& feedback);

  TNode<Int32T> TruncateNumberToWord32(TNode<Number> value);
  // Truncate the floating point value of a HeapNumber to an Int32.
  TNode<Int32T> TruncateHeapNumberValueToWord32(TNode<HeapNumber> object);

  // Conversions.
  TNode<Smi> TryHeapNumberToSmi(TNode<HeapNumber> number, Label* not_smi);
  TNode<Smi> TryFloat32ToSmi(TNode<Float32T> number, Label* not_smi);
  TNode<Smi> TryFloat64ToSmi(TNode<Float64T> number, Label* not_smi);

  TNode<Uint32T> BitcastFloat16ToUint32(TNode<Float16T> value);
  TNode<Float16T> BitcastUint32ToFloat16(TNode<Uint32T> value);
  TNode<Float16T> RoundInt32ToFloat16(TNode<Int32T> value);

  TNode<Float64T> ChangeFloat16ToFloat64(TNode<Float16T> value);
  TNode<Float32T> ChangeFloat16ToFloat32(TNode<Float16T> value);
  TNode<Number> ChangeFloat32ToTagged(TNode<Float32T> value);
  TNode<Number> ChangeFloat64ToTagged(TNode<Float64T> value);
  TNode<Number> ChangeInt32ToTagged(TNode<Int32T> value);
  TNode<Number> ChangeInt32ToTaggedNoOverflow(TNode<Int32T> value);
  TNode<Number> ChangeUint32ToTagged(TNode<Uint32T> value);
  TNode<Number> ChangeUintPtrToTagged(TNode<UintPtrT> value);
  TNode<Uint32T> ChangeNonNegativeNumberToUint32(TNode<Number> value);
  TNode<Float64T> ChangeNumberToFloat64(TNode<Number> value);

  TNode<Int32T> ChangeTaggedNonSmiToInt32(TNode<Context> context,
                                          TNode<HeapObject> input);
  TNode<Float64T> ChangeTaggedToFloat64(TNode<Context> context,
                                        TNode<Object> input);

  TNode<Int32T> ChangeBoolToInt32(TNode<BoolT> b);

  void TaggedToBigInt(TNode<Context> context, TNode<Object> value,
                      Label* if_not_bigint, Label* if_bigint,
                      Label* if_bigint64, TVariable<BigInt>* var_bigint,
                      TVariable<Smi>* var_feedback);

  // Ensures that {var_shared_value} is shareable across Isolates, and throws if
  // not.
  void SharedValueBarrier(TNode<Context> context,
                          TVariable<Object>* var_shared_value);

  TNode<WordT> TimesSystemPointerSize(TNode<WordT> value);
  TNode<IntPtrT> TimesSystemPointerSize(TNode<IntPtrT> value) {
    return Signed(TimesSystemPointerSize(implicit_cast<TNode<WordT>>(value)));
  }
  TNode<UintPtrT> TimesSystemPointerSize(TNode<UintPtrT> value) {
    return Unsigned(TimesSystemPointerSize(implicit_cast<TNode<WordT>>(value)));
  }

  TNode<WordT> TimesTaggedSize(TNode<WordT> value);
  TNode<IntPtrT> TimesTaggedSize(TNode<IntPtrT> value) {
    return Signed(TimesTaggedSize(implicit_cast<TNode<WordT>>(value)));
  }
  TNode<UintPtrT> TimesTaggedSize(TNode<UintPtrT> value) {
    return Unsigned(TimesTaggedSize(implicit_cast<TNode<WordT>>(value)));
  }

  TNode<WordT> TimesDoubleSize(TNode<WordT> value);
  TNode<UintPtrT> TimesDoubleSize(TNode<UintPtrT> value) {
    return Unsigned(TimesDoubleSize(implicit_cast<TNode<WordT>>(value)));
  }
  TNode<IntPtrT> TimesDoubleSize(TNode<IntPtrT> value) {
    return Signed(TimesDoubleSize(implicit_cast<TNode<WordT>>(value)));
  }

  // Type conversions.
  // Throws a TypeError for {method_name} if {value} is not coercible to Object,
  // or returns the {value} converted to a String otherwise.
  TNode<String> ToThisString(TNode<Context> context, TNode<Object> value,
                             TNode<String> method_name);
  TNode<String> ToThisString(TNode<Context> context, TNode<Object> value,
                             char const* method_name) {
    return ToThisString(context, value, StringConstant(method_name));
  }

  // Throws a TypeError for {method_name} if {value} is neither of the given
  // {primitive_type} nor a JSPrimitiveWrapper wrapping a value of
  // {primitive_type}, or returns the {value} (or wrapped value) otherwise.
  TNode<Object> ToThisValue(TNode<Context> context, TNode<Object> value,
                            PrimitiveType primitive_type,
                            char const* method_name);

  // Throws a TypeError for {method_name} if {value} is not of the given
  // instance type.
  void ThrowIfNotInstanceType(TNode<Context> context, TNode<Object> value,
                              InstanceType instance_type,
                              char const* method_name);
  // Throws a TypeError for {method_name} if {value} is not a JSReceiver.
  void ThrowIfNotJSReceiver(TNode<Context> context, TNode<Object> value,
                            MessageTemplate msg_template,
                            const char* method_name);
  void ThrowIfNotCallable(TNode<Context> context, TNode<Object> value,
                          const char* method_name);

  void ThrowRangeError(TNode<Context> context, MessageTemplate message,
                       std::optional<TNode<Object>> arg0 = std::nullopt,
                       std::optional<TNode<Object>> arg1 = std::nullopt,
                       std::optional<TNode<Object>> arg2 = std::nullopt);
  void ThrowTypeError(TNode<Context> context, MessageTemplate message,
                      char const* arg0 = nullptr, char const* arg1 = nullptr);
  void ThrowTypeError(TNode<Context> context, MessageTemplate message,
                      std::optional<TNode<Object>> arg0,
                      std::optional<TNode<Object>> arg1 = std::nullopt,
                      std::optional<TNode<Object>> arg2 = std::nullopt);

  void TerminateExecution(TNode<Context> context);

  TNode<HeapObject> GetPendingMessage();
  void SetPendingMessage(TNode<HeapObject> message);
  TNode<BoolT> IsExecutionTerminating();

  TNode<Object> GetContinuationPreservedEmbedderData();
  void SetContinuationPreservedEmbedderData(TNode<Object> value);

  // Type checks.
  // Check whether the map is for an object with special properties, such as a
  // JSProxy or an object with interceptors.
  TNode<BoolT> InstanceTypeEqual(TNode<Int32T> instance_type, int type);
  TNode<BoolT> IsNoElementsProtectorCellInvalid();
  TNode<BoolT> IsMegaDOMProtectorCellInvalid();
  TNode<BoolT> IsAlwaysSharedSpaceJSObjectInstanceType(
      TNode<Int32T> instance_type);
  TNode<BoolT> IsArrayIteratorProtectorCellInvalid();
  TNode<BoolT> IsBigIntInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsBigInt(TNode<HeapObject> object);
  TNode<BoolT> IsBoolean(TNode<HeapObject> object);
  TNode<BoolT> IsCallableMap(TNode<Map> map);
  TNode<BoolT> IsCallable(TNode<HeapObject> object);
  TNode<BoolT> TaggedIsCallable(TNode<Object> object);
  TNode<BoolT> IsCode(TNode<HeapObject> object);
  TNode<BoolT> TaggedIsCode(TNode<Object> object);
  TNode<BoolT> IsConsStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsConstructorMap(TNode<Map> map);
  TNode<BoolT> IsConstructor(TNode<HeapObject> object);
  TNode<BoolT> IsDeprecatedMap(TNode<Map> map);
  TNode<BoolT> IsPropertyDictionary(TNode<HeapObject> object);
  TNode<BoolT> IsOrderedNameDictionary(TNode<HeapObject> object);
  TNode<BoolT> IsGlobalDictionary(TNode<HeapObject> object);
  TNode<BoolT> IsExtensibleMap(TNode<Map> map);
  TNode<BoolT> IsExtensibleNonPrototypeMap(TNode<Map> map);
  TNode<BoolT> IsExternalStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsFixedArray(TNode<HeapObject> object);
  TNode<BoolT> IsFixedArraySubclass(TNode<HeapObject> object);
  TNode<BoolT> IsFixedArrayWithKind(TNode<HeapObject> object,
                                    ElementsKind kind);
  TNode<BoolT> IsFixedArrayWithKindOrEmpty(TNode<FixedArrayBase> object,
                                           ElementsKind kind);
  TNode<BoolT> IsFunctionWithPrototypeSlotMap(TNode<Map> map);
  TNode<BoolT> IsHashTable(TNode<HeapObject> object);
  TNode<BoolT> IsEphemeronHashTable(TNode<HeapObject> object);
  TNode<BoolT> IsHeapNumberInstanceType(TNode<Int32T> instance_type);
  // We only want to check for any hole in a negated way. For regular hole
  // checks, we should check for a specific hole kind instead.
  TNode<BoolT> IsNotAnyHole(TNode<Object> object);
  TNode<BoolT> IsHoleInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsOddball(TNode<HeapObject> object);
  TNode<BoolT> IsOddballInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsIndirectStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSArrayBuffer(TNode<HeapObject> object);
  TNode<BoolT> IsJSDataView(TNode<HeapObject> object);
  TNode<BoolT> IsJSRabGsabDataView(TNode<HeapObject> object);
  TNode<BoolT> IsJSArrayInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSArrayMap(TNode<Map> map);
  TNode<BoolT> IsJSArray(TNode<HeapObject> object);
  TNode<BoolT> IsJSArrayIterator(TNode<HeapObject> object);
  TNode<BoolT> IsJSAsyncGeneratorObject(TNode<HeapObject> object);
  TNode<BoolT> IsFunctionInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSFunctionInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSFunctionMap(TNode<Map> map);
  TNode<BoolT> IsJSFunction(TNode<HeapObject> object);
  TNode<BoolT> IsJSBoundFunction(TNode<HeapObject> object);
  TNode<BoolT> IsJSGeneratorObject(TNode<HeapObject> object);
  TNode<BoolT> IsJSGlobalProxyInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSGlobalProxyMap(TNode<Map> map);
  TNode<BoolT> IsJSGlobalProxy(TNode<HeapObject> object);
  TNode<BoolT> IsJSObjectInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSObjectMap(TNode<Map> map);
  TNode<BoolT> IsJSObject(TNode<HeapObject> object);
  TNode<BoolT> IsJSApiObjectInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSApiObjectMap(TNode<Map> map);
  TNode<BoolT> IsJSApiObject(TNode<HeapObject> object);
  TNode<BoolT> IsJSFinalizationRegistryMap(TNode<Map> map);
  TNode<BoolT> IsJSFinalizationRegistry(TNode<HeapObject> object);
  TNode<BoolT> IsJSPromiseMap(TNode<Map> map);
  TNode<BoolT> IsJSPromise(TNode<HeapObject> object);
  TNode<BoolT> IsJSProxy(TNode<HeapObject> object);
  TNode<BoolT> IsJSStringIterator(TNode<HeapObject> object);
  TNode<BoolT> IsJSShadowRealm(TNode<HeapObject> object);
  TNode<BoolT> IsJSRegExpStringIterator(TNode<HeapObject> object);
  TNode<BoolT> IsJSReceiverInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSReceiverMap(TNode<Map> map);
  TNode<BoolT> IsJSReceiver(TNode<HeapObject> object);
  // The following two methods assume that we deal either with a primitive
  // object or a JS receiver.
  TNode<BoolT> JSAnyIsNotPrimitiveMap(TNode<Map> map);
  TNode<BoolT> JSAnyIsNotPrimitive(TNode<HeapObject> object);
  TNode<BoolT> IsJSRegExp(TNode<HeapObject> object);
  TNode<BoolT> IsJSTypedArrayInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSTypedArrayMap(TNode<Map> map);
  TNode<BoolT> IsJSTypedArray(TNode<HeapObject> object);
  TNode<BoolT> IsJSGeneratorMap(TNode<Map> map);
  TNode<BoolT> IsJSPrimitiveWrapperInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSPrimitiveWrapperMap(TNode<Map> map);
  TNode<BoolT> IsJSPrimitiveWrapper(TNode<HeapObject> object);
  TNode<BoolT> IsJSSharedArrayInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSSharedArrayMap(TNode<Map> map);
  TNode<BoolT> IsJSSharedArray(TNode<HeapObject> object);
  TNode<BoolT> IsJSSharedArray(TNode<Object> object);
  TNode<BoolT> IsJSSharedStructInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsJSSharedStructMap(TNode<Map> map);
  TNode<BoolT> IsJSSharedStruct(TNode<HeapObject> object);
  TNode<BoolT> IsJSSharedStruct(TNode<Object> object);
  TNode<BoolT> IsJSWrappedFunction(TNode<HeapObject> object);
  TNode<BoolT> IsMap(TNode<HeapObject> object);
  TNode<BoolT> IsName(TNode<HeapObject> object);
  TNode<BoolT> IsNameInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsNullOrJSReceiver(TNode<HeapObject> object);
  TNode<BoolT> IsNullOrUndefined(TNode<Object> object);
  TNode<BoolT> IsNumberDictionary(TNode<HeapObject> object);
  TNode<BoolT> IsOneByteStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsSeqOneByteStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsPrimitiveInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsPrivateName(TNode<Symbol> symbol);
  TNode<BoolT> IsPropertyArray(TNode<HeapObject> object);
  TNode<BoolT> IsPropertyCell(TNode<HeapObject> object);
  TNode<BoolT> IsPromiseReactionJobTask(TNode<HeapObject> object);
  TNode<BoolT> IsPrototypeInitialArrayPrototype(TNode<Context> context,
                                                TNode<Map> map);
  TNode<BoolT> IsPrototypeTypedArrayPrototype(TNode<Context> context,
                                              TNode<Map> map);

  TNode<BoolT> IsFastAliasedArgumentsMap(TNode<Context> context,
                                         TNode<Map> map);
  TNode<BoolT> IsSlowAliasedArgumentsMap(TNode<Context> context,
                                         TNode<Map> map);
  TNode<BoolT> IsSloppyArgumentsMap(TNode<Context> context, TNode<Map> map);
  TNode<BoolT> IsStrictArgumentsMap(TNode<Context> context, TNode<Map> map);

  TNode<BoolT> IsSequentialStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsUncachedExternalStringInstanceType(
      TNode<Int32T> instance_type);
  TNode<BoolT> IsSpecialReceiverInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsCustomElementsReceiverInstanceType(
      TNode<Int32T> instance_type);
  TNode<BoolT> IsSpecialReceiverMap(TNode<Map> map);
  TNode<BoolT> IsStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsString(TNode<HeapObject> object);
  TNode<Word32T> IsStringWrapper(TNode<HeapObject> object);
  TNode<BoolT> IsSeqOneByteString(TNode<HeapObject> object);

  TNode<BoolT> IsSequentialStringMap(TNode<Map> map);
  TNode<BoolT> IsExternalStringMap(TNode<Map> map);
  TNode<BoolT> IsUncachedExternalStringMap(TNode<Map> map);
  TNode<BoolT> IsOneByteStringMap(TNode<Map> map);

  TNode<BoolT> IsSymbolInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsInternalizedStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsSharedStringInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsTemporalInstantInstanceType(TNode<Int32T> instance_type);
  TNode<BoolT> IsUniqueName(TNode<HeapObject> object);
  TNode<BoolT> IsUniqueNameNoIndex(TNode<HeapObject> object);
  TNode<BoolT> IsUniqueNameNoCachedIndex(TNode<HeapObject> object);
  TNode<BoolT> IsUndetectableMap(TNode<Map> map);
  TNode<BoolT> IsNotWeakFixedArraySubclass(TNode<HeapObject> object);
  TNode<BoolT> IsZeroOrContext(TNode<Object> object);

  TNode<BoolT> IsPromiseResolveProtectorCellInvalid();
  TNode<BoolT> IsPromiseThenProtectorCellInvalid();
  TNode<BoolT> IsArraySpeciesProtectorCellInvalid();
  TNode<BoolT> IsIsConcatSpreadableProtectorCellInvalid();
  TNode<BoolT> IsTypedArraySpeciesProtectorCellInvalid();
  TNode<BoolT> IsRegExpSpeciesProtectorCellInvalid();
  TNode<BoolT> IsPromiseSpeciesProtectorCellInvalid();
  TNode<BoolT> IsNumberStringNotRegexpLikeProtectorCellInvalid();
  TNode<BoolT> IsSetIteratorProtectorCellInvalid();
  TNode<BoolT> IsMapIteratorProtectorCellInvalid();
  void InvalidateStringWrapperToPrimitiveProtector();

  TNode<IntPtrT> LoadMemoryChunkFlags(TNode<HeapObject> object);

  TNode<BoolT> LoadRuntimeFlag(ExternalReference address_of_flag) {
    TNode<Word32T> flag_value = UncheckedCast<Word32T>(
        Load(MachineType::Uint8(), ExternalConstant(address_of_flag)));
    return Word32NotEqual(Word32And(flag_value, Int32Constant(0xFF)),
                          Int32Constant(0));
  }

  TNode<BoolT> IsMockArrayBufferAllocatorFlag() {
    return LoadRuntimeFlag(
        ExternalReference::address_of_mock_arraybuffer_allocator_flag());
  }

  TNode<BoolT> HasBuiltinSubclassingFlag() {
    return LoadRuntimeFlag(
        ExternalReference::address_of_builtin_subclassing_flag());
  }

  TNode<BoolT> HasSharedStringTableFlag() {
    return LoadRuntimeFlag(
        ExternalReference::address_of_shared_string_table_flag());
  }

  // True iff |object| is a Smi or a HeapNumber or a BigInt.
  TNode<BoolT> IsNumeric(TNode<Object> object);

  // True iff |number| is either a Smi, or a HeapNumber whose value is not
  // within Smi range.
  TNode<BoolT> IsNumberNormalized(TNode<Number> number);
  TNode<BoolT> IsNumberPositive(TNode<Number> number);
  TNode<BoolT> IsHeapNumberPositive(TNode<HeapNumber> number);

  // True iff {number} is non-negative and less or equal than 2**53-1.
  TNode<BoolT> IsNumberNonNegativeSafeInteger(TNode<Number> number);

  // True iff {number} represents an integer value.
  TNode<BoolT> IsInteger(TNode<Object> number);
  TNode<BoolT> IsInteger(TNode<HeapNumber> number);

  // True iff abs({number}) <= 2**53 -1
  TNode<BoolT> IsSafeInteger(TNode<Object> number);
  TNode<BoolT> IsSafeInteger(TNode<HeapNumber> number);

  // True iff {number} represents a valid uint32t value.
  TNode<BoolT> IsHeapNumberUint32(TNode<HeapNumber> number);

  // True iff {number} is a positive number and a valid array index in the range
  // [0, 2^32-1).
  TNode<BoolT> IsNumberArrayIndex(TNode<Number> number);

  template <typename TIndex>
  TNode<BoolT> FixedArraySizeDoesntFitInNewSpace(TNode<TIndex> element_count,
                                                 int base_size);

  // ElementsKind helpers:
  TNode<BoolT> ElementsKindEqual(TNode<Int32T> a, TNode<Int32T> b) {
    return Word32Equal(a, b);
  }
  bool ElementsKindEqual(ElementsKind a, ElementsKind b) { return a == b; }
  TNode<BoolT> IsFastElementsKind(TNode<Int32T> elements_kind);
  bool IsFastElementsKind(ElementsKind kind) {
    return v8::internal::IsFastElementsKind(kind);
  }
  TNode<BoolT> IsFastPackedElementsKind(TNode<Int32T> elements_kind);
  bool IsFastPackedElementsKind(ElementsKind kind) {
    return v8::internal::IsFastPackedElementsKind(kind);
  }
  TNode<BoolT> IsFastOrNonExtensibleOrSealedElementsKind(
      TNode<Int32T> elements_kind);

  TNode<BoolT> IsDictionaryElementsKind(TNode<Int32T> elements_kind) {
    return ElementsKindEqual(elements_kind, Int32Constant(DICTIONARY_ELEMENTS));
  }
  TNode<BoolT> IsDoubleElementsKind(TNode<Int32T> elements_kind);
  bool IsDoubleElementsKind(ElementsKind kind) {
    return v8::internal::IsDoubleElementsKind(kind);
  }
  TNode<BoolT> IsFastSmiOrTaggedElementsKind(TNode<Int32T> elements_kind);
  TNode<BoolT> IsFastSmiElementsKind(TNode<Int32T> elements_kind);
  TNode<BoolT> IsHoleyFastElementsKind(TNode<Int32T> elements_kind);
  TNode<BoolT> IsHoleyFastElementsKindForRead(TNode<Int32T> elements_kind);
  TNode<BoolT> IsElementsKindGreaterThan(TNode<Int32T> target_kind,
                                         ElementsKind reference_kind);
  TNode<BoolT> IsElementsKindGreaterThanOrEqual(TNode<Int32T> target_kind,
                                                ElementsKind reference_kind);
  TNode<BoolT> IsElementsKindLessThanOrEqual(TNode<Int32T> target_kind,
                                             ElementsKind reference_kind);
  // Check if lower_reference_kind <= target_kind <= higher_reference_kind.
  TNode<BoolT> IsElementsKindInRange(TNode<Int32T> target_kind,
                                     ElementsKind lower_reference_kind,
                                     ElementsKind higher_reference_kind) {
    return IsInRange(target_kind, lower_reference_kind, higher_reference_kind);
  }
  TNode<Int32T> GetNonRabGsabElementsKind(TNode<Int32T> elements_kind);

  // String helpers.
  // Load a character from a String (might flatten a ConsString).
  TNode<Uint16T> StringCharCodeAt(TNode<String> string, TNode<UintPtrT> index);
  // Return the single character string with only {code}.
  TNode<String> StringFromSingleCharCode(TNode<Int32T> code);

  // Type conversion helpers.
  enum class BigIntHandling { kConvertToNumber, kThrow };
  // Convert a String to a Number.
  TNode<Number> StringToNumber(TNode<String> input);
  // Convert a Number to a String.
  TNode<String> NumberToString(TNode<Number> input);
  TNode<String> NumberToString(TNode<Number> input, Label* bailout);

  // Convert a Non-Number object to a Number.
  TNode<Number> NonNumberToNumber(
      TNode<Context> context, TNode<HeapObject> input,
      BigIntHandling bigint_handling = BigIntHandling::kThrow);
  // Convert a Non-Number object to a Numeric.
  TNode<Numeric> NonNumberToNumeric(TNode<Context> context,
                                    TNode<HeapObject> input);
  // Convert any object to a Number.
  // Conforms to ES#sec-tonumber if {bigint_handling} == kThrow.
  // With {bigint_handling} == kConvertToNumber, matches behavior of
  // tc39.github.io/proposal-bigint/#sec-number-constructor-number-value.
  TNode<Number> ToNumber(
      TNode<Context> context, TNode<Object> input,
      BigIntHandling bigint_handling = BigIntHandling::kThrow);
  TNode<Number> ToNumber_Inline(TNode<Context> context, TNode<Object> input);
  TNode<Numeric> ToNumberOrNumeric(
      LazyNode<Context> context, TNode<Object> input,
      TVariable<Smi>* var_type_feedback, Object::Conversion mode,
      BigIntHandling bigint_handling = BigIntHandling::kThrow);
  // Convert any plain primitive to a Number. No need to handle BigInts since
  // they are not plain primitives.
  TNode<Number> PlainPrimitiveToNumber(TNode<Object> input);

  // Try to convert an object to a BigInt. Throws on failure (e.g. for Numbers).
  // https://tc39.github.io/proposal-bigint/#sec-to-bigint
  TNode<BigInt> ToBigInt(TNode<Context> context, TNode<Object> input);
  // Try to convert any object to a BigInt, including Numbers.
  TNode<BigInt> ToBigIntConvertNumber(TNode<Context> context,
                                      TNode<Object> input);

  // Converts |input| to one of 2^32 integer values in the range 0 through
  // 2^32-1, inclusive.
  // ES#sec-touint32
  TNode<Number> ToUint32(TNode<Context> context, TNode<Object> input);

  // No-op on 32-bit, otherwise zero extend.
  TNode<IntPtrT> ChangePositiveInt32ToIntPtr(TNode<Int32T> input) {
    CSA_DCHECK(this, Int32GreaterThanOrEqual(input, Int32Constant(0)));
    return Signed(ChangeUint32ToWord(input));
  }

  // Convert any object to a String.
  TNode<String> ToString_Inline(TNode<Context> context, TNode<Object> input);

  TNode<JSReceiver> ToObject(TNode<Context> context, TNode<Object> input);

  // Same as ToObject but avoids the Builtin call if |input| is already a
  // JSReceiver.
  TNode<JSReceiver> ToObject_Inline(TNode<Context> context,
                                    TNode<Object> input);

  // ES6 7.1.15 ToLength, but with inlined fast path.
  TNode<Number> ToLength_Inline(TNode<Context> context, TNode<Object> input);

  TNode<Object> OrdinaryToPrimitive(TNode<Context> context, TNode<Object> input,
                                    OrdinaryToPrimitiveHint hint);

  // Returns a node that contains a decoded (unsigned!) value of a bit
  // field |BitField| in |word32|. Returns result as an uint32 node.
  template <typename BitField>
  TNode<Uint32T> DecodeWord32(TNode<Word32T> word32) {
    return DecodeWord32(word32, BitField::kShift, BitField::kMask);
  }

  // Returns a node that contains a decoded (unsigned!) value of a bit
  // field |BitField| in |word|. Returns result as a word-size node.
  template <typename BitField>
  TNode<UintPtrT> DecodeWord(TNode<WordT> word) {
    return DecodeWord(word, BitField::kShift, BitField::kMask);
  }

  // Returns a node that contains a decoded (unsigned!) value of a bit
  // field |BitField| in |word32|. Returns result as a word-size node.
  template <typename BitField>
  TNode<UintPtrT> DecodeWordFromWord32(TNode<Word32T> word32) {
    return DecodeWord<BitField>(ChangeUint32ToWord(word32));
  }

  // Returns a node that contains a decoded (unsigned!) value of a bit
  // field |BitField| in |word|. Returns result as an uint32 node.
  template <typename BitField>
  TNode<Uint32T> DecodeWord32FromWord(TNode<WordT> word) {
    return UncheckedCast<Uint32T>(
        TruncateIntPtrToInt32(Signed(DecodeWord<BitField>(word))));
  }

  // Decodes an unsigned (!) value from |word32| to an uint32 node.
  TNode<Uint32T> DecodeWord32(TNode<Word32T> word32, uint32_t shift,
                              uint32_t mask);

  // Decodes an unsigned (!) value from |word| to a word-size node.
  TNode<UintPtrT> DecodeWord(TNode<WordT> word, uint32_t shift, uintptr_t mask);

  // Returns a node that contains the updated values of a |BitField|.
  template <typename BitField>
  TNode<Word32T> UpdateWord32(TNode<Word32T> word, TNode<Uint32T> value,
                              bool starts_as_zero = false) {
    return UpdateWord32(word, value, BitField::kShift, BitField::kMask,
                        starts_as_zero);
  }

  // Returns a node that contains the updated values of a |BitField|.
  template <typename BitField>
  TNode<WordT> UpdateWord(TNode<WordT> word, TNode<UintPtrT> value,
                          bool starts_as_zero = false) {
    return UpdateWord(word, value, BitField::kShift, BitField::kMask,
                      starts_as_zero);
  }

  // Returns a node that contains the updated values of a |BitField|.
  template <typename BitField>
  TNode<Word32T> UpdateWordInWord32(TNode<Word32T> word, TNode<UintPtrT> value,
                                    bool starts_as_zero = false) {
    return UncheckedCast<Uint32T>(
        TruncateIntPtrToInt32(Signed(UpdateWord<BitField>(
            ChangeUint32ToWord(word), value, starts_as_zero))));
  }

  // Returns a node that contains the updated values of a |BitField|.
  template <typename BitField>
  TNode<WordT> UpdateWord32InWord(TNode<WordT> word, TNode<Uint32T> value,
                                  bool starts_as_zero = false) {
    return UpdateWord<BitField>(word, ChangeUint32ToWord(value),
                                starts_as_zero);
  }

  // Returns a node that contains the updated {value} inside {word} starting
  // at {shift} and fitting in {mask}.
  TNode<Word32T> UpdateWord32(TNode<Word32T> word, TNode<Uint32T> value,
                              uint32_t shift, uint32_t mask,
                              bool starts_as_zero = false);

  // Returns a node that contains the updated {value} inside {word} starting
  // at {shift} and fitting in {mask}.
  TNode<WordT> UpdateWord(TNode<WordT> word, TNode<UintPtrT> value,
                          uint32_t shift, uintptr_t mask,
                          bool starts_as_zero = false);

  // Returns true if any of the |T|'s bits in given |word32| are set.
  template <typename T>
  TNode<BoolT> IsSetWord32(TNode<Word32T> word32) {
    return IsSetWord32(word32, T::kMask);
  }

  // Returns true if none of the |T|'s bits in given |word32| are set.
  template <typename T>
  TNode<BoolT> IsNotSetWord32(TNode<Word32T> word32) {
    return IsNotSetWord32(word32, T::kMask);
  }

  // Returns true if any of the mask's bits in given |word32| are set.
  TNode<BoolT> IsSetWord32(TNode<Word32T> word32, uint32_t mask) {
    return Word32NotEqual(Word32And(word32, Int32Constant(mask)),
                          Int32Constant(0));
  }

  // Returns true if none of the mask's bits in given |word32| are set.
  TNode<BoolT> IsNotSetWord32(TNode<Word32T> word32, uint32_t mask) {
    return Word32Equal(Word32And(word32, Int32Constant(mask)),
                       Int32Constant(0));
  }

  // Returns true if all of the mask's bits in a given |word32| are set.
  TNode<BoolT> IsAllSetWord32(TNode<Word32T> word32, uint32_t mask) {
    TNode<Int32T> const_mask = Int32Constant(mask);
    return Word32Equal(Word32And(word32, const_mask), const_mask);
  }

  // Returns true if the bit field |BitField| in |word32| is equal to a given
  // constant |value|. Avoids a shift compared to using DecodeWord32.
  template <typename BitField>
  TNode<BoolT> IsEqualInWord32(TNode<Word32T> word32,
                               typename BitField::FieldType value) {
    TNode<Word32T> masked_word32 =
        Word32And(word32, Int32Constant(BitField::kMask));
    return Word32Equal(masked_word32, Int32Constant(BitField::encode(value)));
  }

  // Checks if two values of non-overlapping bitfields are both set.
  template <typename BitField1, typename BitField2>
  TNode<BoolT> IsBothEqualInWord32(TNode<Word32T> word32,
                                   typename BitField1::FieldType value1,
                                   typename BitField2::FieldType value2) {
    static_assert((BitField1::kMask & BitField2::kMask) == 0);
    TNode<Word32T> combined_masked_word32 =
        Word32And(word32, Int32Constant(BitField1::kMask | BitField2::kMask));
    TNode<Int32T> combined_value =
        Int32Constant(BitField1::encode(value1) | BitField2::encode(value2));
    return Word32Equal(combined_masked_word32, combined_value);
  }

  // Returns true if the bit field |BitField| in |word32| is not equal to a
  // given constant |value|. Avoids a shift compared to using DecodeWord32.
  template <typename BitField>
  TNode<BoolT> IsNotEqualInWord32(TNode<Word32T> word32,
                                  typename BitField::FieldType value) {
    return Word32BinaryNot(IsEqualInWord32<BitField>(word32, value));
  }

  // Returns true if any of the |T|'s bits in given |word| are set.
  template <typename T>
  TNode<BoolT> IsSetWord(TNode<WordT> word) {
    return IsSetWord(word, T::kMask);
  }

  // Returns true if any of the mask's bits in given |word| are set.
  TNode<BoolT> IsSetWord(TNode<WordT> word, uint32_t mask) {
    return WordNotEqual(WordAnd(word, IntPtrConstant(mask)), IntPtrConstant(0));
  }

  // Returns true if any of the mask's bit are set in the given Smi.
  // Smi-encoding of the mask is performed implicitly!
  TNode<BoolT> IsSetSmi(TNode<Smi> smi, int untagged_mask) {
    intptr_t mask_word = base::bit_cast<intptr_t>(Smi::FromInt(untagged_mask));
    return WordNotEqual(WordAnd(BitcastTaggedToWordForTagAndSmiBits(smi),
                                IntPtrConstant(mask_word)),
                        IntPtrConstant(0));
  }

  // Returns true if all of the |T|'s bits in given |word32| are clear.
  template <typename T>
  TNode<BoolT> IsClearWord32(TNode<Word32T> word32) {
    return IsClearWord32(word32, T::kMask);
  }

  // Returns true if all of the mask's bits in given |word32| are clear.
  TNode<BoolT> IsClearWord32(TNode<Word32T> word32, uint32_t mask) {
    return Word32Equal(Word32And(word32, Int32Constant(mask)),
                       Int32Constant(0));
  }

  // Returns true if all of the |T|'s bits in given |word| are clear.
  template <typename T>
  TNode<BoolT> IsClearWord(TNode<WordT> word) {
    return IsClearWord(word, T::kMask);
  }

  // Returns true if all of the mask's bits in given |word| are clear.
  TNode<BoolT> IsClearWord(TNode<WordT> word, uint32_t mask) {
    return IntPtrEqual(WordAnd(word, IntPtrConstant(mask)), IntPtrConstant(0));
  }

  void SetCounter(StatsCounter* counter, int value);
  void IncrementCounter(StatsCounter* counter, int delta);
  void DecrementCounter(StatsCounter* counter, int delta);

  template <typename TIndex>
  void Increment(TVariable<TIndex>* variable, int value = 1);

  template <typename TIndex>
  void Decrement(TVariable<TIndex>* variable, int value = 1) {
    Increment(variable, -value);
  }

  // Generates "if (false) goto label" code. Useful for marking a label as
  // "live" to avoid assertion failures during graph building. In the resulting
  // code this check will be eliminated.
  void Use(Label* label);

  // Various building blocks for stubs doing property lookups.

  // |if_notinternalized| is optional; |if_bailout| will be used by default.
  // Note: If |key| does not yet have a hash, |if_notinternalized| will be taken
  // even if |key| is an array index. |if_keyisunique| will never
  // be taken for array indices.
  void TryToName(TNode<Object> key, Label* if_keyisindex,
                 TVariable<IntPtrT>* var_index, Label* if_keyisunique,
                 TVariable<Name>* var_unique, Label* if_bailout,
                 Label* if_notinternalized = nullptr);

  // Call non-allocating runtime String::WriteToFlat using fast C-calls.
  void StringWriteToFlatOneByte(TNode<String> source, TNode<RawPtrT> sink,
                                TNode<Int32T> start, TNode<Int32T> length);
  void StringWriteToFlatTwoByte(TNode<String> source, TNode<RawPtrT> sink,
                                TNode<Int32T> start, TNode<Int32T> length);

  // Calls External{One,Two}ByteString::GetChars with a fast C-call.
  TNode<RawPtr<Uint8T>> ExternalOneByteStringGetChars(
      TNode<ExternalOneByteString> string);
  TNode<RawPtr<Uint16T>> ExternalTwoByteStringGetChars(
      TNode<ExternalTwoByteString> string);

  TNode<RawPtr<Uint8T>> IntlAsciiCollationWeightsL1();
  TNode<RawPtr<Uint8T>> IntlAsciiCollationWeightsL3();

  // Performs a hash computation and string table lookup for the given string,
  // and jumps to:
  // - |if_index| if the string is an array index like "123"; |var_index|
  //              will contain the intptr representation of that index.
  // - |if_internalized| if the string exists in the string table; the
  //                     internalized version will be in |var_internalized|.
  // - |if_not_internalized| if the string is not in the string table (but
  //                         does not add it).
  // - |if_bailout| for unsupported cases (e.g. uncachable array index).
  void TryInternalizeString(TNode<String> string, Label* if_index,
                            TVariable<IntPtrT>* var_index,
                            Label* if_internalized,
                            TVariable<Name>* var_internalized,
                            Label* if_not_internalized, Label* if_bailout);

  // Calculates array index for given dictionary entry and entry field.
  // See Dictionary::EntryToIndex().
  template <typename Dictionary>
  TNode<IntPtrT> EntryToIndex(TNode<IntPtrT> entry, int field_index);
  template <typename Dictionary>
  TNode<IntPtrT> EntryToIndex(TNode<IntPtrT> entry) {
    return EntryToIndex<Dictionary>(entry, Dictionary::kEntryKeyIndex);
  }

  // Loads the details for the entry with the given key_index.
  // Returns an untagged int32.
  template <class ContainerType>
  TNode<Uint32T> LoadDetailsByKeyIndex(TNode<ContainerType> container,
                                       TNode<IntPtrT> key_index);

  // Loads the value for the entry with the given key_index.
  // Returns a tagged value.
  template <class ContainerType>
  TNode<Object> LoadValueByKeyIndex(TNode<ContainerType> container,
                                    TNode<IntPtrT> key_index);

  // Stores the details for the entry with the given key_index.
  // |details| must be a Smi.
  template <class ContainerType>
  void StoreDetailsByKeyIndex(TNode<ContainerType> container,
                              TNode<IntPtrT> key_index, TNode<Smi> details);

  // Stores the value for the entry with the given key_index.
  template <class ContainerType>
  void StoreValueByKeyIndex(
      TNode<ContainerType> container, TNode<IntPtrT> key_index,
      TNode<Object> value,
      WriteBarrierMode write_barrier = UPDATE_WRITE_BARRIER);

  // Calculate a valid size for the a hash table.
  TNode<IntPtrT> HashTableComputeCapacity(TNode<IntPtrT> at_least_space_for);

  TNode<IntPtrT> NameToIndexHashTableLookup(TNode<NameToIndexHashTable> table,
                                            TNode<Name> name, Label* not_found);

  template <class Dictionary>
  TNode<Smi> GetNumberOfElements(TNode<Dictionary> dictionary);

  TNode<Smi> GetNumberDictionaryNumberOfElements(
      TNode<NumberDictionary> dictionary) {
    return GetNumberOfElements<NumberDictionary>(dictionary);
  }

  template <class Dictionary>
  void SetNumberOfElements(TNode<Dictionary> dictionary,
                           TNode<Smi> num_elements_smi) {
    // Not supposed to be used for SwissNameDictionary.
    static_assert(!(std::is_same<Dictionary, SwissNameDictionary>::value));

    StoreFixedArrayElement(dictionary, Dictionary::kNumberOfElementsIndex,
                           num_elements_smi, SKIP_WRITE_BARRIER);
  }

  template <class Dictionary>
  TNode<Smi> GetNumberOfDeletedElements(TNode<Dictionary> dictionary) {
    // Not supposed to be used for SwissNameDictionary.
    static_assert(!(std::is_same<Dictionary, SwissNameDictionary>::value));

    return CAST(LoadFixedArrayElement(
        dictionary, Dictionary::kNumberOfDeletedElementsIndex));
  }

  template <class Dictionary>
  void SetNumberOfDeletedElements(TNode<Dictionary> dictionary,
                                  TNode<Smi> num_deleted_smi) {
    // Not supposed to be used for SwissNameDictionary.
    static_assert(!(std::is_same<Dictionary, SwissNameDictionary>::value));

    StoreFixedArrayElement(dictionary,
                           Dictionary::kNumberOfDeletedElementsIndex,
                           num_deleted_smi, SKIP_WRITE_BARRIER);
  }

  template <class Dictionary>
  TNode<Smi> GetCapacity(TNode<Dictionary> dictionary) {
    // Not supposed to be used for SwissNameDictionary.
    static_assert(!(std::is_same<Dictionary, SwissNameDictionary>::value));

    return CAST(
        UnsafeLoadFixedArrayElement(dictionary, Dictionary::kCapacityIndex));
  }

  template <class Dictionary>
  TNode<Smi> GetNextEnumerationIndex(TNode<Dictionary> dictionary) {
    return CAST(LoadFixedArrayElement(dictionary,
                                      Dictionary::kNextEnumerationIndexIndex));
  }

  template <class Dictionary>
  void SetNextEnumerationIndex(TNode<Dictionary> dictionary,
                               TNode<Smi> next_enum_index_smi) {
    StoreFixedArrayElement(dictionary, Dictionary::kNextEnumerationIndexIndex,
                           next_enum_index_smi, SKIP_WRITE_BARRIER);
  }

  template <class Dictionary>
  TNode<Smi> GetNameDictionaryFlags(TNode<Dictionary> dictionary);
  template <class Dictionary>
  void SetNameDictionaryFlags(TNode<Dictionary>, TNode<Smi> flags);

  enum LookupMode {
    kFindExisting,
    kFindInsertionIndex,
    kFindExistingOrInsertionIndex
  };

  template <typename Dictionary>
  TNode<HeapObject> LoadName(TNode<HeapObject> key);

  // Looks up an entry in a NameDictionaryBase successor.
  // If the entry is found control goes to {if_found} and {var_name_index}
  // contains an index of the key field of the entry found.
  // If the key is not found control goes to {if_not_found}. If mode is
  // {kFindExisting}, {var_name_index} might contain garbage, otherwise
  // {var_name_index} contains the index of the key field to insert the given
  // name at.
  template <typename Dictionary>
  void NameDictionaryLookup(TNode<Dictionary> dictionary,
                            TNode<Name> unique_name, Label* if_found,
                            TVariable<IntPtrT>* var_name_index,
                            Label* if_not_found,
                            LookupMode mode = kFindExisting);
  // Slow lookup for unique_names with forwarding index.
  // Both resolving the actual hash and the lookup are handled via runtime.
  template <typename Dictionary>
  void NameDictionaryLookupWithForwardIndex(TNode<Dictionary> dictionary,
                                            TNode<Name> unique_name,
                                            Label* if_found,
                                            TVariable<IntPtrT>* var_name_index,
                                            Label* if_not_found,
                                            LookupMode mode = kFindExisting);

  TNode<Word32T> ComputeSeededHash(TNode<IntPtrT> key);

  // Looks up an entry in a NameDictionaryBase successor. If the entry is found
  // control goes to {if_found} and {var_name_index} contains an index of the
  // key field of the entry found. If the key is not found control goes to
  // {if_not_found}.
  void NumberDictionaryLookup(TNode<NumberDictionary> dictionary,
                              TNode<IntPtrT> intptr_index, Label* if_found,
                              TVariable<IntPtrT>* var_entry,
                              Label* if_not_found);

  TNode<Object> BasicLoadNumberDictionaryElement(
      TNode<NumberDictionary> dictionary, TNode<IntPtrT> intptr_index,
      Label* not_data, Label* if_hole);

  template <class Dictionary>
  void FindInsertionEntry(TNode<Dictionary> dictionary, TNode<Name> key,
                          TVariable<IntPtrT>* var_key_index);

  template <class Dictionary>
  void InsertEntry(TNode<Dictionary> dictionary, TNode<Name> key,
                   TNode<Object> value, TNode<IntPtrT> index,
                   TNode<Smi> enum_index);

  template <class Dictionary>
  void AddToDictionary(
      TNode<Dictionary> dictionary, TNode<Name> key, TNode<Object> value,
      Label* bailout,
      std::optional<TNode<IntPtrT>> insertion_index = std::nullopt);

  // Tries to check if {object} has own {unique_name} property.
  void TryHasOwnProperty(TNode<HeapObject> object, TNode<Map> map,
                         TNode<Int32T> instance_type, TNode<Name> unique_name,
                         Label* if_found, Label* if_not_found,
                         Label* if_bailout);

  // Operating mode for TryGetOwnProperty and CallGetterIfAccessor
  enum GetOwnPropertyMode {
    // kCallJSGetterDontUseCachedName is used when we want to get the result of
    // the getter call, and don't use cached_name_property when the getter is
    // the function template and it has cached_property_name, which would just
    // bailout for the IC system to create a named property handler
    kCallJSGetterDontUseCachedName,
    // kCallJSGetterUseCachedName is used when we want to get the result of
    // the getter call, and use cached_name_property when the getter is
    // the function template and it has cached_property_name, which would call
    // GetProperty rather than bailout for Generic/NoFeedback load
    kCallJSGetterUseCachedName,
    // kReturnAccessorPair is used when we're only getting the property
    // descriptor
    kReturnAccessorPair
  };
  // Receiver handling mode for TryGetOwnProperty and CallGetterIfAccessor.
  enum ExpectedReceiverMode {
    // The receiver is guaranteed to be JSReceiver, no conversion is necessary
    // in case a function callback template has to be called.
    kExpectingJSReceiver,
    // The receiver can be anything, it has to be converted to JSReceiver
    // in case a function callback template has to be called.
    kExpectingAnyReceiver,
  };
  // Tries to get {object}'s own {unique_name} property value. If the property
  // is an accessor then it also calls a getter. If the property is a double
  // field it re-wraps value in an immutable heap number. {unique_name} must be
  // a unique name (Symbol or InternalizedString) that is not an array index.
  void TryGetOwnProperty(
      TNode<Context> context, TNode<Object> receiver, TNode<JSReceiver> object,
      TNode<Map> map, TNode<Int32T> instance_type, TNode<Name> unique_name,
      Label* if_found_value, TVariable<Object>* var_value, Label* if_not_found,
      Label* if_bailout,
      ExpectedReceiverMode expected_receiver_mode = kExpectingAnyReceiver);
  void TryGetOwnProperty(
      TNode<Context> context, TNode<Object> receiver, TNode<JSReceiver> object,
      TNode<Map> map, TNode<Int32T> instance_type, TNode<Name> unique_name,
      Label* if_found_value, TVariable<Object>* var_value,
      TVariable<Uint32T>* var_details, TVariable<Object>* var_raw_value,
      Label* if_not_found, Label* if_bailout, GetOwnPropertyMode mode,
      ExpectedReceiverMode expected_receiver_mode = kExpectingAnyReceiver);

  TNode<PropertyDescriptorObject> AllocatePropertyDescriptorObject(
      TNode<Context> context);
  void InitializePropertyDescriptorObject(
      TNode<PropertyDescriptorObject> descriptor, TNode<Object> value,
      TNode<Uint32T> details, Label* if_bailout);

  TNode<Object> GetProperty(TNode<Context> context, TNode<Object> receiver,
                            Handle<Name> name) {
    return GetProperty(context, receiver, HeapConstantNoHole(name));
  }

  TNode<Object> GetProperty(TNode<Context> context, TNode<Object> receiver,
                            TNode<Object> name) {
    return CallBuiltin(Builtin::kGetProperty, context, receiver, name);
  }

  TNode<BoolT> IsInterestingProperty(TNode<Name> name);
  TNode<Object> GetInterestingProperty(TNode<Context> context,
                                       TNode<JSReceiver> receiver,
                                       TNode<Name> name, Label* if_not_found);
  TNode<Object> GetInterestingProperty(TNode<Context> context,
                                       TNode<Object> receiver,
                                       TVariable<HeapObject>* var_holder,
                                       TVariable<Map>* var_holder_map,
                                       TNode<Name> name, Label* if_not_found);

  TNode<Object> SetPropertyStrict(TNode<Context> context,
                                  TNode<Object> receiver, TNode<Object> key,
                                  TNode<Object> value) {
    return CallBuiltin(Builtin::kSetProperty, context, receiver, key, value);
  }

  TNode<Object> CreateDataProperty(TNode<Context> context,
                                   TNode<JSObject> receiver, TNode<Object> key,
                                   TNode<Object> value) {
    return CallBuiltin(Builtin::kCreateDataProperty, context, receiver, key,
                       value);
  }

  TNode<Object> GetMethod(TNode<Context> context, TNode<Object> object,
                          Handle<Name> name, Label* if_null_or_undefined);

  TNode<Object> GetIteratorMethod(TNode<Context> context,
                                  TNode<HeapObject> heap_obj,
                                  Label* if_iteratorundefined);

  TNode<Object> CreateAsyncFromSyncIterator(TNode<Context> context,
                                            TNode<Object> sync_iterator);
  TNode<JSObject> CreateAsyncFromSyncIterator(TNode<Context> context,
                                              TNode<JSReceiver> sync_iterator,
                                              TNode<Object> next);

  void LoadPropertyFromFastObject(TNode<HeapObject> object, TNode<Map> map,
                                  TNode<DescriptorArray> descriptors,
                                  TNode<IntPtrT> name_index,
                                  TVariable<Uint32T>* var_details,
                                  TVariable<Object>* var_value);

  void LoadPropertyFromFastObject(TNode<HeapObject> object, TNode<Map> map,
                                  TNode<DescriptorArray> descriptors,
                                  TNode<IntPtrT> name_index, TNode<Uint32T>,
                                  TVariable<Object>* var_value);

  template <typename Dictionary>
  void LoadPropertyFromDictionary(TNode<Dictionary> dictionary,
                                  TNode<IntPtrT> name_index,
                                  TVariable<Uint32T>* var_details,
                                  TVariable<Object>* var_value);
  void LoadPropertyFromGlobalDictionary(TNode<GlobalDictionary> dictionary,
                                        TNode<IntPtrT> name_index,
                                        TVariable<Uint32T>* var_details,
                                        TVariable<Object>* var_value,
                                        Label* if_deleted);

  // Generic property lookup generator. If the {object} is fast and
  // {unique_name} property is found then the control goes to {if_found_fast}
  // label and {var_meta_storage} and {var_name_index} will contain
  // DescriptorArray and an index of the descriptor's name respectively.
  // If the {object} is slow or global then the control goes to {if_found_dict}
  // or {if_found_global} and the {var_meta_storage} and {var_name_index} will
  // contain a dictionary and an index of the key field of the found entry.
  // If property is not found or given lookup is not supported then
  // the control goes to {if_not_found} or {if_bailout} respectively.
  //
  // Note: this code does not check if the global dictionary points to deleted
  // entry! This has to be done by the caller.
  void TryLookupProperty(TNode<HeapObject> object, TNode<Map> map,
                         TNode<Int32T> instance_type, TNode<Name> unique_name,
                         Label* if_found_fast, Label* if_found_dict,
                         Label* if_found_global,
                         TVariable<HeapObject>* var_meta_storage,
                         TVariable<IntPtrT>* var_name_index,
                         Label* if_not_found, Label* if_bailout);

  // This is a building block for TryLookupProperty() above. Supports only
  // non-special fast and dictionary objects.
  // TODO(v8:11167, v8:11177) |bailout| only needed for SetDataProperties
  // workaround.
  void TryLookupPropertyInSimpleObject(TNode<JSObject> object, TNode<Map> map,
                                       TNode<Name> unique_name,
                                       Label* if_found_fast,
                                       Label* if_found_dict,
                                       TVariable<HeapObject>* var_meta_storage,
                                       TVariable<IntPtrT>* var_name_index,
                                       Label* if_not_found, Label* bailout);

  // This method jumps to if_found if the element is known to exist. To
  // if_absent if it's known to not exist. To if_not_found if the prototype
  // chain needs to be checked. And if_bailout if the lookup is unsupported.
  void TryLookupElement(TNode<HeapObject> object, TNode<Map> map,
                        TNode<Int32T> instance_type,
                        TNode<IntPtrT> intptr_index, Label* if_found,
                        Label* if_absent, Label* if_not_found,
                        Label* if_bailout);

  // For integer indexed exotic cases, check if the given string cannot be a
  // special index. If we are not sure that the given string is not a special
  // index with a simple check, return False. Note that "False" return value
  // does not mean that the name_string is a special index in the current
  // implementation.
  void BranchIfMaybeSpecialIndex(TNode<String> name_string,
                                 Label* if_maybe_special_index,
                                 Label* if_not_special_index);

  // This is a type of a lookup property in holder generator function. The {key}
  // is guaranteed to be an unique name.
  using LookupPropertyInHolder = std::function<void(
      TNode<HeapObject> receiver, TNode<HeapObject> holder, TNode<Map> map,
      TNode<Int32T> instance_type, TNode<Name> key, Label* next_holder,
      Label* if_bailout)>;

  // This is a type of a lookup element in holder generator function. The {key}
  // is an Int32 index.
  using LookupElementInHolder = std::function<void(
      TNode<HeapObject> receiver, TNode<HeapObject> holder, TNode<Map> map,
      TNode<Int32T> instance_type, TNode<IntPtrT> key, Label* next_holder,
      Label* if_bailout)>;

  // Generic property prototype chain lookup generator.
  // For properties it generates lookup using given {lookup_property_in_holder}
  // and for elements it uses {lookup_element_in_holder}.
  // Upon reaching the end of prototype chain the control goes to {if_end}.
  // If it can't handle the case {receiver}/{key} case then the control goes
  // to {if_bailout}.
  // If {if_proxy} is nullptr, proxies go to if_bailout.
  void TryPrototypeChainLookup(
      TNode<Object> receiver, TNode<Object> object, TNode<Object> key,
      const LookupPropertyInHolder& lookup_property_in_holder,
      const LookupElementInHolder& lookup_element_in_holder, Label* if_end,
      Label* if_bailout, Label* if_proxy, bool handle_private_names = false);

  // Instanceof helpers.
  // Returns true if {object} has {prototype} somewhere in it's prototype
  // chain, otherwise false is returned. Might cause arbitrary side effects
  // due to [[GetPrototypeOf]] invocations.
  TNode<Boolean> HasInPrototypeChain(TNode<Context> context,
                                     TNode<HeapObject> object,
                                     TNode<Object> prototype);
  // ES6 section 7.3.19 OrdinaryHasInstance (C, O)
  TNode<Boolean> OrdinaryHasInstance(TNode<Context> context,
                                     TNode<Object> callable,
                                     TNode<Object> object);

  // Load type feedback vector from the stub caller's frame.
  TNode<FeedbackVector> LoadFeedbackVectorForStub();
  TNode<FeedbackVector> LoadFeedbackVectorFromBaseline();
  TNode<Context> LoadContextFromBaseline();
  // Load type feedback vector from the stub caller's frame, skipping an
  // intermediate trampoline frame.
  TNode<FeedbackVector> LoadFeedbackVectorForStubWithTrampoline();

  // Load the value from closure's feedback cell.
  TNode<HeapObject> LoadFeedbackCellValue(TNode<JSFunction> closure);

  // Load the object from feedback vector cell for the given closure.
  // The returned object could be undefined if the closure does not have
  // a feedback vector associated with it.
  TNode<HeapObject> LoadFeedbackVector(TNode<JSFunction> closure);
  TNode<FeedbackVector> LoadFeedbackVector(TNode<JSFunction> closure,
                                           Label* if_no_feedback_vector);

  // Load the ClosureFeedbackCellArray that contains the feedback cells
  // used when creating closures from this function. This array could be
  // directly hanging off the FeedbackCell when there is no feedback vector
  // or available from the feedback vector's header.
  TNode<ClosureFeedbackCellArray> LoadClosureFeedbackArray(
      TNode<JSFunction> closure);

  // Update the type feedback vector.
  bool UpdateFeedbackModeEqual(UpdateFeedbackMode a, UpdateFeedbackMode b) {
    return a == b;
  }
  void UpdateFeedback(TNode<Smi> feedback,
                      TNode<HeapObject> maybe_feedback_vector,
                      TNode<UintPtrT> slot_id, UpdateFeedbackMode mode);
  void UpdateFeedback(TNode<Smi> feedback,
                      TNode<FeedbackVector> feedback_vector,
                      TNode<UintPtrT> slot_id);
  void MaybeUpdateFeedback(TNode<Smi> feedback,
                           TNode<HeapObject> maybe_feedback_vector,
                           TNode<UintPtrT> slot_id);

  // Report that there was a feedback update, performing any tasks that should
  // be done after a feedback update.
  void ReportFeedbackUpdate(TNode<FeedbackVector> feedback_vector,
                            TNode<UintPtrT> slot_id, const char* reason);

  // Combine the new feedback with the existing_feedback. Do nothing if
  // existing_feedback is nullptr.
  void CombineFeedback(TVariable<Smi>* existing_feedback, int feedback);
  void CombineFeedback(TVariable<Smi>* existing_feedback, TNode<Smi> feedback);

  // Overwrite the existing feedback with new_feedback. Do nothing if
  // existing_feedback is nullptr.
  void OverwriteFeedback(TVariable<Smi>* existing_feedback, int new_feedback);

  // Check if a property name might require protector invalidation when it is
  // used for a property store or deletion.
  void CheckForAssociatedProtector(TNode<Name> name, Label* if_protector);

  TNode<Map> LoadReceiverMap(TNode<Object> receiver);

  // Loads script context from the script context table.
  TNode<Context> LoadScriptContext(TNode<Context> context,
                                   TNode<IntPtrT> context_index);

  TNode<Uint8T> Int32ToUint8Clamped(TNode<Int32T> int32_value);
  TNode<Uint8T> Float64ToUint8Clamped(TNode<Float64T> float64_value);

  template <typename T>
  TNode<T> PrepareValueForWriteToTypedArray(TNode<Object> input,
                                            ElementsKind elements_kind,
                                            TNode<Context> context);

  // Store value to an elements array with given elements kind.
  // TODO(turbofan): For BIGINT64_ELEMENTS and BIGUINT64_ELEMENTS
  // we pass {value} as BigInt object instead of int64_t. We should
  // teach TurboFan to handle int64_t on 32-bit platforms eventually.
  template <typename TIndex, typename TValue>
  void StoreElement(TNode<RawPtrT> elements, ElementsKind kind,
                    TNode<TIndex> index, TNode<TValue> value);

  // Implements the BigInt part of
  // https://tc39.github.io/proposal-bigint/#sec-numbertorawbytes,
  // including truncation to 64 bits (i.e. modulo 2^64).
  // {var_high} is only used on 32-bit platforms.
  void BigIntToRawBytes(TNode<BigInt> bigint, TVariable<UintPtrT>* var_low,
                        TVariable<UintPtrT>* var_high);

#if V8_ENABLE_WEBASSEMBLY
  TorqueStructInt64AsInt32Pair BigIntToRawBytes(TNode<BigInt> value);
#endif  // V8_ENABLE_WEBASSEMBLY

  void EmitElementStore(TNode<JSObject> object, TNode<Object> key,
                        TNode<Object> value, ElementsKind elements_kind,
                        KeyedAccessStoreMode store_mode, Label* bailout,
                        TNode<Context> context,
                        TVariable<Object>* maybe_converted_value = nullptr);

  TNode<FixedArrayBase> CheckForCapacityGrow(
      TNode<JSObject> object, TNode<FixedArrayBase> elements, ElementsKind kind,
      TNode<UintPtrT> length, TNode<IntPtrT> key, Label* bailout);

  TNode<FixedArrayBase> CopyElementsOnWrite(TNode<HeapObject> object,
                                            TNode<FixedArrayBase> elements,
                                            ElementsKind kind,
                                            TNode<IntPtrT> length,
                                            Label* bailout);

  void TransitionElementsKind(TNode<JSObject> object, TNode<Map> map,
                              ElementsKind from_kind, ElementsKind to_kind,
                              Label* bailout);

  void TrapAllocationMemento(TNode<JSObject> object, Label* memento_found);

  // Helpers to look up Page metadata for a given address.
  // Equivalent to MemoryChunk::FromAddress().
  TNode<IntPtrT> MemoryChunkFromAddress(TNode<IntPtrT> address);
  // Equivalent to MemoryChunk::MutablePageMetadata().
  TNode<IntPtrT> PageMetadataFromMemoryChunk(TNode<IntPtrT> address);
  // Equivalent to MemoryChunkMetadata::FromAddress().
  TNode<IntPtrT> PageMetadataFromAddress(TNode<IntPtrT> address);

  // Store a weak in-place reference into the FeedbackVector.
  TNode<MaybeObject> StoreWeakReferenceInFeedbackVector(
      TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot,
      TNode<HeapObject> value, int additional_offset = 0);

  // Create a new AllocationSite and install it into a feedback vector.
  TNode<AllocationSite> CreateAllocationSiteInFeedbackVector(
      TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot);

  TNode<BoolT> HasBoilerplate(TNode<Object> maybe_literal_site);
  TNode<Smi> LoadTransitionInfo(TNode<AllocationSite> allocation_site);
  TNode<JSObject> LoadBoilerplate(TNode<AllocationSite> allocation_site);
  TNode<Int32T> LoadElementsKind(TNode<AllocationSite> allocation_site);
  TNode<Object> LoadNestedAllocationSite(TNode<AllocationSite> allocation_site);

  enum class IndexAdvanceMode { kPre, kPost };
  enum class LoopUnrollingMode { kNo, kYes };

  template <typename TIndex>
  using FastLoopBody = std::function<void(TNode<TIndex> index)>;

  template <typename TIndex>
  void BuildFastLoop(const VariableList& vars, TVariable<TIndex>& var_index,
                     TNode<TIndex> start_index, TNode<TIndex> end_index,
                     const FastLoopBody<TIndex>& body, int increment,
                     LoopUnrollingMode unrolling_mode,
                     IndexAdvanceMode advance_mode = IndexAdvanceMode::kPre);

  template <typename TIndex>
  void BuildFastLoop(TVariable<TIndex>& var_index, TNode<TIndex> start_index,
                     TNode<TIndex> end_index, const FastLoopBody<TIndex>& body,
                     int increment, LoopUnrollingMode unrolling_mode,
                     IndexAdvanceMode advance_mode = IndexAdvanceMode::kPre) {
    BuildFastLoop(VariableList(0, zone()), var_index, start_index, end_index,
                  body, increment, unrolling_mode, advance_mode);
  }

  template <typename TIndex>
  void BuildFastLoop(const VariableList& vars, TNode<TIndex> start_index,
                     TNode<TIndex> end_index, const FastLoopBody<TIndex>& body,
                     int increment, LoopUnrollingMode unrolling_mode,
                     IndexAdvanceMode advance_mode) {
    TVARIABLE(TIndex, var_index);
    BuildFastLoop(vars, var_index, start_index, end_index, body, increment,
                  unrolling_mode, advance_mode);
  }

  template <typename TIndex>
  void BuildFastLoop(TNode<TIndex> start_index, TNode<TIndex> end_index,
                     const FastLoopBody<TIndex>& body, int increment,
                     LoopUnrollingMode unrolling_mode,
                     IndexAdvanceMode advance_mode = IndexAdvanceMode::kPre) {
    BuildFastLoop(VariableList(0, zone()), start_index, end_index, body,
                  increment, unrolling_mode, advance_mode);
  }

  enum class ForEachDirection { kForward, kReverse };

  using FastArrayForEachBody =
      std::function<void(TNode<HeapObject> array, TNode<IntPtrT> offset)>;

  template <typename TIndex>
  void BuildFastArrayForEach(
      TNode<UnionOf<FixedArray, PropertyArray, HeapObject>> array,
      ElementsKind kind, TNode<TIndex> first_element_inclusive,
      TNode<TIndex> last_element_exclusive, const FastArrayForEachBody& body,
      LoopUnrollingMode loop_unrolling_mode,
      ForEachDirection direction = ForEachDirection::kReverse);

  template <typename TIndex>
  TNode<IntPtrT> GetArrayAllocationSize(TNode<TIndex> element_count,
                                        ElementsKind kind, int header_size) {
    return ElementOffsetFromIndex(element_count, kind, header_size);
  }

  template <typename TIndex>
  TNode<IntPtrT> GetFixedArrayAllocationSize(TNode<TIndex> element_count,
                                             ElementsKind kind) {
    return GetArrayAllocationSize(element_count, kind, FixedArray::kHeaderSize);
  }

  TNode<IntPtrT> GetPropertyArrayAllocationSize(TNode<IntPtrT> element_count) {
    return GetArrayAllocationSize(element_count, PACKED_ELEMENTS,
                                  PropertyArray::kHeaderSize);
  }

  template <typename TIndex>
  void GotoIfFixedArraySizeDoesntFitInNewSpace(TNode<TIndex> element_count,
                                               Label* doesnt_fit,
                                               int base_size);

  void InitializeFieldsWithRoot(TNode<HeapObject> object,
                                TNode<IntPtrT> start_offset,
                                TNode<IntPtrT> end_offset, RootIndex root);

  // Goto the given |target| if the context chain starting at |context| has any
  // extensions up to the given |depth|. Returns the Context with the
  // extensions if there was one, otherwise returns the Context at the given
  // |depth|.
  TNode<Context> GotoIfHasContextExtensionUpToDepth(TNode<Context> context,
                                                    TNode<Uint32T> depth,
                                                    Label* target);

  TNode<Boolean> RelationalComparison(
      Operation op, TNode<Object> left, TNode<Object> right,
      TNode<Context> context, TVariable<Smi>* var_type_feedback = nullptr) {
    return RelationalComparison(
        op, left, right, [=]() { return context; }, var_type_feedback);
  }

  TNode<Boolean> RelationalComparison(
      Operation op, TNode<Object> left, TNode<Object> right,
      const LazyNode<Context>& context,
      TVariable<Smi>* var_type_feedback = nullptr);

  void BranchIfNumberRelationalComparison(Operation op, TNode<Number> left,
                                          TNode<Number> right, Label* if_true,
                                          Label* if_false);

  void BranchIfNumberEqual(TNode<Number> left, TNode<Number> right,
                           Label* if_true, Label* if_false) {
    BranchIfNumberRelationalComparison(Operation::kEqual, left, right, if_true,
                                       if_false);
  }

  void BranchIfNumberNotEqual(TNode<Number> left, TNode<Number> right,
                              Label* if_true, Label* if_false) {
    BranchIfNumberEqual(left, right, if_false, if_true);
  }

  void BranchIfNumberLessThan(TNode<Number> left, TNode<Number> right,
                              Label* if_true, Label* if_false) {
    BranchIfNumberRelationalComparison(Operation::kLessThan, left, right,
                                       if_true, if_false);
  }

  void BranchIfNumberLessThanOrEqual(TNode<Number> left, TNode<Number> right,
                                     Label* if_true, Label* if_false) {
    BranchIfNumberRelationalComparison(Operation::kLessThanOrEqual, left, right,
                                       if_true, if_false);
  }

  void BranchIfNumberGreaterThan(TNode<Number> left, TNode<Number> right,
                                 Label* if_true, Label* if_false) {
    BranchIfNumberRelationalComparison(Operation::kGreaterThan, left, right,
                                       if_true, if_false);
  }

  void BranchIfNumberGreaterThanOrEqual(TNode<Number> left, TNode<Number> right,
                                        Label* if_true, Label* if_false) {
    BranchIfNumberRelationalComparison(Operation::kGreaterThanOrEqual, left,
                                       right, if_true, if_false);
  }

  void BranchIfAccessorPair(TNode<Object> value, Label* if_accessor_pair,
                            Label* if_not_accessor_pair) {
    GotoIf(TaggedIsSmi(value), if_not_accessor_pair);
    Branch(IsAccessorPair(CAST(value)), if_accessor_pair, if_not_accessor_pair);
  }

  void GotoIfNumberGreaterThanOrEqual(TNode<Number> left, TNode<Number> right,
                                      Label* if_false);

  TNode<Boolean> Equal(TNode<Object> lhs, TNode<Object> rhs,
                       TNode<Context> context,
                       TVariable<Smi>* var_type_feedback = nullptr) {
    return Equal(
        lhs, rhs, [=]() { return context; }, var_type_feedback);
  }
  TNode<Boolean> Equal(TNode<Object> lhs, TNode<Object> rhs,
                       const LazyNode<Context>& context,
                       TVariable<Smi>* var_type_feedback = nullptr);

  TNode<Boolean> StrictEqual(TNode<Object> lhs, TNode<Object> rhs,
                             TVariable<Smi>* var_type_feedback = nullptr);

  void GotoIfStringEqual(TNode<String> lhs, TNode<IntPtrT> lhs_length,
                         TNode<String> rhs, Label* if_true) {
    Label if_false(this);
    // Callers must handle the case where {lhs} and {rhs} refer to the same
    // String object.
    CSA_DCHECK(this, TaggedNotEqual(lhs, rhs));
    TNode<IntPtrT> rhs_length = LoadStringLengthAsWord(rhs);
    BranchIfStringEqual(lhs, lhs_length, rhs, rhs_length, if_true, &if_false,
                        nullptr);

    BIND(&if_false);
  }

  void BranchIfStringEqual(TNode<String> lhs, TNode<String> rhs, Label* if_true,
                           Label* if_false,
                           TVariable<Boolean>* result = nullptr) {
    return BranchIfStringEqual(lhs, LoadStringLengthAsWord(lhs), rhs,
                               LoadStringLengthAsWord(rhs), if_true, if_false,
                               result);
  }

  void BranchIfStringEqual(TNode<String> lhs, TNode<IntPtrT> lhs_length,
                           TNode<String> rhs, TNode<IntPtrT> rhs_length,
                           Label* if_true, Label* if_false,
                           TVariable<Boolean>* result = nullptr);

  // ECMA#sec-samevalue
  // Similar to StrictEqual except that NaNs are treated as equal and minus zero
  // differs from positive zero.
  enum class SameValueMode { kNumbersOnly, kFull };
  void BranchIfSameValue(TNode<Object> lhs, TNode<Object> rhs, Label* if_true,
                         Label* if_false,
                         SameValueMode mode = SameValueMode::kFull);
  // A part of BranchIfSameValue() that handles two double values.
  // Treats NaN == NaN and +0 != -0.
  void BranchIfSameNumberValue(TNode<Float64T> lhs_value,
                               TNode<Float64T> rhs_value, Label* if_true,
                               Label* if_false);

  enum HasPropertyLookupMode { kHasProperty, kForInHasProperty };

  TNode<Boolean> HasProperty(TNode<Context> context, TNode<Object> object,
                             TNode<Object> key, HasPropertyLookupMode mode);

  // Due to naming conflict with the builtin function namespace.
  TNode<Boolean> HasProperty_Inline(TNode<Context> context,
                                    TNode<JSReceiver> object,
                                    TNode<Object> key) {
    return HasProperty(context, object, key,
                       HasPropertyLookupMode::kHasProperty);
  }

  void ForInPrepare(TNode<HeapObject> enumerator, TNode<UintPtrT> slot,
                    TNode<HeapObject> maybe_feedback_vector,
                    TNode<FixedArray>* cache_array_out,
                    TNode<Smi>* cache_length_out,
                    UpdateFeedbackMode update_feedback_mode);

  TNode<String> Typeof(
      TNode<Object> value, std::optional<TNode<UintPtrT>> slot_id = {},
      std::optional<TNode<HeapObject>> maybe_feedback_vector = {});

  TNode<HeapObject> GetSuperConstructor(TNode<JSFunction> active_function);

  TNode<JSReceiver> SpeciesConstructor(TNode<Context> context,
                                       TNode<Object> object,
                                       TNode<JSReceiver> default_constructor);

  TNode<Boolean> InstanceOf(TNode<Object> object, TNode<Object> callable,
                            TNode<Context> context);

  // Debug helpers
  TNode<BoolT> IsDebugActive();

  // JSArrayBuffer helpers
  TNode<UintPtrT> LoadJSArrayBufferByteLength(
      TNode<JSArrayBuffer> array_buffer);
  TNode<UintPtrT> LoadJSArrayBufferMaxByteLength(
      TNode<JSArrayBuffer> array_buffer);
  TNode<RawPtrT> LoadJSArrayBufferBackingStorePtr(
      TNode<JSArrayBuffer> array_buffer);
  void ThrowIfArrayBufferIsDetached(TNode<Context> context,
                                    TNode<JSArrayBuffer> array_buffer,
                                    const char* method_name);

  // JSArrayBufferView helpers
  TNode<JSArrayBuffer> LoadJSArrayBufferViewBuffer(
      TNode<JSArrayBufferView> array_buffer_view);
  TNode<UintPtrT> LoadJSArrayBufferViewByteLength(
      TNode<JSArrayBufferView> array_buffer_view);
  void StoreJSArrayBufferViewByteLength(
      TNode<JSArrayBufferView> array_buffer_view, TNode<UintPtrT> value);
  TNode<UintPtrT> LoadJSArrayBufferViewByteOffset(
      TNode<JSArrayBufferView> array_buffer_view);
  void StoreJSArrayBufferViewByteOffset(
      TNode<JSArrayBufferView> array_buffer_view, TNode<UintPtrT> value);
  void ThrowIfArrayBufferViewBufferIsDetached(
      TNode<Context> context, TNode<JSArrayBufferView> array_buffer_view,
      const char* method_name);

  // JSTypedArray helpers
  TNode<UintPtrT> LoadJSTypedArrayLength(TNode<JSTypedArray> typed_array);
  void StoreJSTypedArrayLength(TNode<JSTypedArray> typed_array,
                               TNode<UintPtrT> value);
  TNode<UintPtrT> LoadJSTypedArrayLengthAndCheckDetached(
      TNode<JSTypedArray> typed_array, Label* detached);
  // Helper for length tracking JSTypedArrays and JSTypedArrays backed by
  // ResizableArrayBuffer.
  TNode<UintPtrT> LoadVariableLengthJSTypedArrayLength(
      TNode<JSTypedArray> array, TNode<JSArrayBuffer> buffer,
      Label* detached_or_out_of_bounds);
  // Helper for length tracking JSTypedArrays and JSTypedArrays backed by
  // ResizableArrayBuffer.
  TNode<UintPtrT> LoadVariableLengthJSTypedArrayByteLength(
      TNode<Context> context, TNode<JSTypedArray> array,
      TNode<JSArrayBuffer> buffer);
  TNode<UintPtrT> LoadVariableLengthJSArrayBufferViewByteLength(
      TNode<JSArrayBufferView> array, TNode<JSArrayBuffer> buffer,
      Label* detached_or_out_of_bounds);

  void IsJSArrayBufferViewDetachedOrOutOfBounds(
      TNode<JSArrayBufferView> array_buffer_view, Label* detached_or_oob,
      Label* not_detached_nor_oob);

  TNode<BoolT> IsJSArrayBufferViewDetachedOrOutOfBoundsBoolean(
      TNode<JSArrayBufferView> array_buffer_view);

  void CheckJSTypedArrayIndex(TNode<JSTypedArray> typed_array,
                              TNode<UintPtrT> index,
                              Label* detached_or_out_of_bounds);

  TNode<IntPtrT> RabGsabElementsKindToElementByteSize(
      TNode<Int32T> elementsKind);
  TNode<RawPtrT> LoadJSTypedArrayDataPtr(TNode<JSTypedArray> typed_array);
  TNode<JSArrayBuffer> GetTypedArrayBuffer(TNode<Context> context,
                                           TNode<JSTypedArray> array);

  template <typename TIndex>
  TNode<IntPtrT> ElementOffsetFromIndex(TNode<TIndex> index, ElementsKind kind,
                                        int base_size = 0);
  template <typename Array, typename TIndex>
  TNode<IntPtrT> OffsetOfElementAt(TNode<TIndex> index) {
    static_assert(Array::Shape::kElementSize == kTaggedSize);
    return ElementOffsetFromIndex(index, PACKED_ELEMENTS,
                                  Array::kHeaderSize - kHeapObjectTag);
  }

  // Check that a field offset is within the bounds of the an object.
  TNode<BoolT> IsOffsetInBounds(TNode<IntPtrT> offset, TNode<IntPtrT> length,
                                int header_size,
                                ElementsKind kind = HOLEY_ELEMENTS);

  // Load a builtin's code from the builtin array in the isolate.
  TNode<Code> LoadBuiltin(TNode<Smi> builtin_id);

#ifdef V8_ENABLE_LEAPTIERING
  // Load a builtin's handle into the JSDispatchTable.
  TNode<JSDispatchHandleT> LoadBuiltinDispatchHandle(Builtin builtin);
  TNode<JSDispatchHandleT> LoadBuiltinDispatchHandle(RootIndex idx);
  TNode<JSDispatchHandleT> LoadBuiltinDispatchHandle(TNode<Smi> builtin_id);

  // Load a Code object from the JSDispatchTable.
  TNode<Code> ResolveJSDispatchHandle(TNode<JSDispatchHandleT> dispatch_handle);
#endif

  // Figure out the SFI's code object using its data field.
  // If |data_type_out| is provided, the instance type of the function data will
  // be stored in it. In case the code object is a builtin (data is a Smi),
  // data_type_out will be set to 0.
  // If |if_compile_lazy| is provided then the execution will go to the given
  // label in case of an CompileLazy code object.
  TNode<Code> GetSharedFunctionInfoCode(
      TNode<SharedFunctionInfo> shared_info,
      TVariable<Uint16T>* data_type_out = nullptr,
      Label* if_compile_lazy = nullptr);

  TNode<JSFunction> AllocateFunctionWithContext(
      TNode<SharedFunctionInfo> shared_info,
#ifdef V8_ENABLE_LEAPTIERING
      TNode<JSDispatchHandleT> dispatch_handle,
#endif
      TNode<Context> context);
  TNode<JSFunction> AllocateRootFunctionWithContext(RootIndex function,
                                                    TNode<Context> context) {
    return AllocateFunctionWithContext(
        UncheckedCast<SharedFunctionInfo>(LoadRoot(function)),
#ifdef V8_ENABLE_LEAPTIERING
        LoadBuiltinDispatchHandle(function),
#endif
        context);
  }
  // Used from Torque because Torque
  TNode<JSFunction> AllocateRootFunctionWithContext(intptr_t function,
                                                    TNode<Context> context) {
    return AllocateRootFunctionWithContext(static_cast<RootIndex>(function),
                                           context);
  }

  // Promise helpers
  TNode<Uint32T> PromiseHookFlags();
  TNode<BoolT> HasAsyncEventDelegate();
#ifdef V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
  TNode<BoolT> IsContextPromiseHookEnabled(TNode<Uint32T> flags);
#endif
  TNode<BoolT> IsIsolatePromiseHookEnabled(TNode<Uint32T> flags);
  TNode<BoolT> IsAnyPromiseHookEnabled(TNode<Uint32T> flags);
  TNode<BoolT> IsAnyPromiseHookEnabled() {
    return IsAnyPromiseHookEnabled(PromiseHookFlags());
  }
  TNode<BoolT> IsIsolatePromiseHookEnabledOrHasAsyncEventDelegate(
      TNode<Uint32T> flags);
  TNode<BoolT> IsIsolatePromiseHookEnabledOrHasAsyncEventDelegate() {
    return IsIsolatePromiseHookEnabledOrHasAsyncEventDelegate(
        PromiseHookFlags());
  }
  TNode<BoolT>
  IsIsolatePromiseHookEnabledOrDebugIsActiveOrHasAsyncEventDelegate(
      TNode<Uint32T> flags);
  TNode<BoolT>
  IsIsolatePromiseHookEnabledOrDebugIsActiveOrHasAsyncEventDelegate() {
    return IsIsolatePromiseHookEnabledOrDebugIsActiveOrHasAsyncEventDelegate(
        PromiseHookFlags());
  }

  TNode<BoolT> NeedsAnyPromiseHooks(TNode<Uint32T> flags);
  TNode<BoolT> NeedsAnyPromiseHooks() {
    return NeedsAnyPromiseHooks(PromiseHookFlags());
  }

  // for..in helpers
  void CheckPrototypeEnumCache(TNode<JSReceiver> receiver,
                               TNode<Map> receiver_map, Label* if_fast,
                               Label* if_slow);
  TNode<Map> CheckEnumCache(TNode<JSReceiver> receiver, Label* if_empty,
                            Label* if_runtime);

  TNode<Object> GetArgumentValue(TorqueStructArguments args,
                                 TNode<IntPtrT> index);

  void SetArgumentValue(TorqueStructArguments args, TNode<IntPtrT> index,
                        TNode<Object> value);

  enum class FrameArgumentsArgcType {
    kCountIncludesReceiver,
    kCountExcludesReceiver
  };

  TorqueStructArguments GetFrameArguments(
      TNode<RawPtrT> frame, TNode<IntPtrT> argc,
      FrameArgumentsArgcType argc_type =
          FrameArgumentsArgcType::kCountExcludesReceiver);

  inline TNode<Int32T> JSParameterCount(int argc_without_receiver) {
    return Int32Constant(argc_without_receiver + kJSArgcReceiverSlots);
  }
  inline TNode<Word32T> JSParameterCount(TNode<Word32T> argc_without_receiver) {
    return Int32Add(argc_without_receiver, Int32Constant(kJSArgcReceiverSlots));
  }

  // Support for printf-style debugging
  void Print(const char* s);
  void Print(const char* prefix, TNode<MaybeObject> tagged_value);
  void Print(TNode<MaybeObject> tagged_value) {
    return Print(nullptr, tagged_value);
  }
  void Print(const char* prefix, TNode<UintPtrT> value);
  void Print(const char* prefix, TNode<Float64T> value);
  void PrintErr(const char* s);
  void PrintErr(const char* prefix, TNode<MaybeObject> tagged_value);
  void PrintErr(TNode<MaybeObject> tagged_value) {
    return PrintErr(nullptr, tagged_value);
  }
  void PrintToStream(const char* s, int stream);
  void PrintToStream(const char* prefix, TNode<MaybeObject> tagged_value,
                     int stream);
  void PrintToStream(const char* prefix, TNode<UintPtrT> value, int stream);
  void PrintToStream(const char* prefix, TNode<Float64T> value, int stream);

  template <class... TArgs>
  TNode<HeapObject> MakeTypeError(MessageTemplate message,
                                  TNode<Context> context, TArgs... args) {
    static_assert(sizeof...(TArgs) <= 3);
    return CAST(CallRuntime(Runtime::kNewTypeError, context,
                            SmiConstant(message), args...));
  }

  void Abort(AbortReason reason) {
    CallRuntime(Runtime::kAbort, NoContextConstant(), SmiConstant(reason));
    Unreachable();
  }

  bool ConstexprBoolNot(bool value) { return !value; }
  int31_t ConstexprIntegerLiteralToInt31(const IntegerLiteral& i) {
    return int31_t(i.To<int32_t>());
  }
  int32_t ConstexprIntegerLiteralToInt32(const IntegerLiteral& i) {
    return i.To<int32_t>();
  }
  uint32_t ConstexprIntegerLiteralToUint32(const IntegerLiteral& i) {
    return i.To<uint32_t>();
  }
  int8_t ConstexprIntegerLiteralToInt8(const IntegerLiteral& i) {
    return i.To<int8_t>();
  }
  uint8_t ConstexprIntegerLiteralToUint8(const IntegerLiteral& i) {
    return i.To<uint8_t>();
  }
  int64_t ConstexprIntegerLiteralToInt64(const IntegerLiteral& i) {
    return i.To<int64_t>();
  }
  uint64_t ConstexprIntegerLiteralToUint64(const IntegerLiteral& i) {
    return i.To<uint64_t>();
  }
  intptr_t ConstexprIntegerLiteralToIntptr(const IntegerLiteral& i) {
    return i.To<intptr_t>();
  }
  uintptr_t ConstexprIntegerLiteralToUintptr(const IntegerLiteral& i) {
    return i.To<uintptr_t>();
  }
  double ConstexprIntegerLiteralToFloat64(const IntegerLiteral& i) {
    int64_t i_value = i.To<int64_t>();
    double d_value = static_cast<double>(i_value);
    CHECK_EQ(i_value, static_cast<int64_t>(d_value));
    return d_value;
  }
  bool ConstexprIntegerLiteralEqual(IntegerLiteral lhs, IntegerLiteral rhs) {
    return lhs == rhs;
  }
  IntegerLiteral ConstexprIntegerLiteralAdd(const IntegerLiteral& lhs,
                                            const IntegerLiteral& rhs);
  IntegerLiteral ConstexprIntegerLiteralLeftShift(const IntegerLiteral& lhs,
                                                  const IntegerLiteral& rhs);
  IntegerLiteral ConstexprIntegerLiteralBitwiseOr(const IntegerLiteral& lhs,
                                                  const IntegerLiteral& rhs);

  bool ConstexprInt31Equal(int31_t a, int31_t b) { return a == b; }
  bool ConstexprInt31NotEqual(int31_t a, int31_t b) { return a != b; }
  bool ConstexprInt31GreaterThanEqual(int31_t a, int31_t b) { return a >= b; }
  bool ConstexprUint32Equal(uint32_t a, uint32_t b) { return a == b; }
  bool ConstexprUint32NotEqual(uint32_t a, uint32_t b) { return a != b; }
  bool ConstexprInt32Equal(int32_t a, int32_t b) { return a == b; }
  bool ConstexprInt32NotEqual(int32_t a, int32_t b) { return a != b; }
  bool ConstexprInt32GreaterThanEqual(int32_t a, int32_t b) { return a >= b; }
  uint32_t ConstexprUint32Add(uint32_t a, uint32_t b) { return a + b; }
  int32_t ConstexprUint32Sub(uint32_t a, uint32_t b) { return a - b; }
  int32_t ConstexprInt32Sub(int32_t a, int32_t b) { return a - b; }
  int32_t ConstexprInt32Add(int32_t a, int32_t b) { return a + b; }
  int31_t ConstexprInt31Add(int31_t a, int31_t b) {
    int32_t val;
    CHECK(!base::bits::SignedAddOverflow32(a, b, &val));
    return val;
  }
  int31_t ConstexprInt31Mul(int31_t a, int31_t b) {
    int32_t val;
    CHECK(!base::bits::SignedMulOverflow32(a, b, &val));
    return val;
  }

  int32_t ConstexprWord32Or(int32_t a, int32_t b) { return a | b; }
  uint32_t ConstexprWord32Shl(uint32_t a, int32_t b) { return a << b; }

  bool ConstexprUintPtrLessThan(uintptr_t a, uintptr_t b) { return a < b; }

  // CSA does not support 64-bit types on 32-bit platforms so as a workaround
  // the kMaxSafeIntegerUint64 is defined as uintptr and allowed to be used only
  // inside if constexpr (Is64()) i.e. on 64-bit architectures.
  static uintptr_t MaxSafeIntegerUintPtr() {
#if defined(V8_HOST_ARCH_64_BIT)
    // This ifdef is required to avoid build issues on 32-bit MSVC which
    // complains about static_cast<uintptr_t>(kMaxSafeIntegerUint64).
    return kMaxSafeIntegerUint64;
#else
    UNREACHABLE();
#endif
  }

  void PerformStackCheck(TNode<Context> context);

  void SetPropertyLength(TNode<Context> context, TNode<Object> array,
                         TNode<Number> length);

  // Implements DescriptorArray::Search().
  void DescriptorLookup(TNode<Name> unique_name,
                        TNode<DescriptorArray> descriptors,
                        TNode<Uint32T> bitfield3, Label* if_found,
                        TVariable<IntPtrT>* var_name_index,
                        Label* if_not_found);

  // Implements TransitionArray::SearchName() - searches for first transition
  // entry with given name (note that there could be multiple entries with
  // the same name).
  void TransitionLookup(TNode<Name> unique_name,
                        TNode<TransitionArray> transitions, Label* if_found,
                        TVariable<IntPtrT>* var_name_index,
                        Label* if_not_found);

  // Implements generic search procedure like i::Search<Array>().
  template <typename Array>
  void Lookup(TNode<Name> unique_name, TNode<Array> array,
              TNode<Uint32T> number_of_valid_entries, Label* if_found,
              TVariable<IntPtrT>* var_name_index, Label* if_not_found);

  // Implements generic linear search procedure like i::LinearSearch<Array>().
  template <typename Array>
  void LookupLinear(TNode<Name> unique_name, TNode<Array> array,
                    TNode<Uint32T> number_of_valid_entries, Label* if_found,
                    TVariable<IntPtrT>* var_name_index, Label* if_not_found);

  // Implements generic binary search procedure like i::BinarySearch<Array>().
  template <typename Array>
  void LookupBinary(TNode<Name> unique_name, TNode<Array> array,
                    TNode<Uint32T> number_of_valid_entries, Label* if_found,
                    TVariable<IntPtrT>* var_name_index, Label* if_not_found);

  // Converts [Descriptor/Transition]Array entry number to a fixed array index.
  template <typename Array>
  TNode<IntPtrT> EntryIndexToIndex(TNode<Uint32T> entry_index);

  // Implements [Descriptor/Transition]Array::ToKeyIndex.
  template <typename Array>
  TNode<IntPtrT> ToKeyIndex(TNode<Uint32T> entry_index);

  // Implements [Descriptor/Transition]Array::GetKey.
  template <typename Array>
  TNode<Name> GetKey(TNode<Array> array, TNode<Uint32T> entry_index);

  // Implements DescriptorArray::GetDetails.
  TNode<Uint32T> DescriptorArrayGetDetails(TNode<DescriptorArray> descriptors,
                                           TNode<Uint32T> descriptor_number);

  using ForEachDescriptorBodyFunction =
      std::function<void(TNode<IntPtrT> descriptor_key_index)>;

  // Descriptor array accessors based on key_index, which is equal to
  // DescriptorArray::ToKeyIndex(descriptor).
  TNode<Name> LoadKeyByKeyIndex(TNode<DescriptorArray> container,
                                TNode<IntPtrT> key_index);
  TNode<Uint32T> LoadDetailsByKeyIndex(TNode<DescriptorArray> container,
                                       TNode<IntPtrT> key_index);
  TNode<Object> LoadValueByKeyIndex(TNode<DescriptorArray> container,
                                    TNode<IntPtrT> key_index);
  TNode<MaybeObject> LoadFieldTypeByKeyIndex(TNode<DescriptorArray> container,
                                             TNode<IntPtrT> key_index);

  TNode<IntPtrT> DescriptorEntryToIndex(TNode<IntPtrT> descriptor);

  // Descriptor array accessors based on descriptor.
  TNode<Name> LoadKeyByDescriptorEntry(TNode<DescriptorArray> descriptors,
                                       TNode<IntPtrT> descriptor);
  TNode<Name> LoadKeyByDescriptorEntry(TNode<DescriptorArray> descriptors,
                                       int descriptor);
  TNode<Uint32T> LoadDetailsByDescriptorEntry(
      TNode<DescriptorArray> descriptors, TNode<IntPtrT> descriptor);
  TNode<Uint32T> LoadDetailsByDescriptorEntry(
      TNode<DescriptorArray> descriptors, int descriptor);
  TNode<Object> LoadValueByDescriptorEntry(TNode<DescriptorArray> descriptors,
                                           TNode<IntPtrT> descriptor);
  TNode<Object> LoadValueByDescriptorEntry(TNode<DescriptorArray> descriptors,
                                           int descriptor);
  TNode<MaybeObject> LoadFieldTypeByDescriptorEntry(
      TNode<DescriptorArray> descriptors, TNode<IntPtrT> descriptor);

  using ForEachKeyValueFunction =
      std::function<void(TNode<Name> key, LazyNode<Object> value)>;

  // For each JSObject property (in DescriptorArray order), check if the key is
  // enumerable, and if so, load the value from the receiver and evaluate the
  // closure. The value is provided as a LazyNode, which lazily evaluates
  // accessors if present.
  void ForEachEnumerableOwnProperty(TNode<Context> context, TNode<Map> map,
                                    TNode<JSObject> object,
                                    PropertiesEnumerationMode mode,
                                    const ForEachKeyValueFunction& body,
                                    Label* bailout);

  TNode<Object> CallGetterIfAccessor(
      TNode<Object> value, TNode<HeapObject> holder, TNode<Uint32T> details,
      TNode<Context> context, TNode<Object> receiver, TNode<Object> name,
      Label* if_bailout,
      GetOwnPropertyMode mode = kCallJSGetterDontUseCachedName,
      ExpectedReceiverMode expected_receiver_mode = kExpectingJSReceiver);

  TNode<IntPtrT> TryToIntptr(TNode<Object> key, Label* if_not_intptr,
                             TVariable<Int32T>* var_instance_type = nullptr);

  TNode<JSArray> ArrayCreate(TNode<Context> context, TNode<Number> length);

  // Allocate a clone of a mutable primitive, if {object} is a mutable
  // HeapNumber.
  TNode<Object> CloneIfMutablePrimitive(TNode<Object> object);

  TNode<Smi> RefillMathRandom(TNode<NativeContext> native_context);

  void RemoveFinalizationRegistryCellFromUnregisterTokenMap(
      TNode<JSFinalizationRegistry> finalization_registry,
      TNode<WeakCell> weak_cell);

  TNode<IntPtrT> FeedbackIteratorEntrySize() {
    return IntPtrConstant(FeedbackIterator::kEntrySize);
  }

  TNode<IntPtrT> FeedbackIteratorHandlerOffset() {
    return IntPtrConstant(FeedbackIterator::kHandlerOffset);
  }

  TNode<SwissNameDictionary> AllocateSwissNameDictionary(
      TNode<IntPtrT> at_least_space_for);
  TNode<SwissNameDictionary> AllocateSwissNameDictionary(
      int at_least_space_for);

  TNode<SwissNameDictionary> AllocateSwissNameDictionaryWithCapacity(
      TNode<IntPtrT> capacity);

  // MT stands for "minus tag".
  TNode<IntPtrT> SwissNameDictionaryOffsetIntoDataTableMT(
      TNode<SwissNameDictionary> dict, TNode<IntPtrT> index, int field_index);

  // MT stands for "minus tag".
  TNode<IntPtrT> SwissNameDictionaryOffsetIntoPropertyDetailsTableMT(
      TNode<SwissNameDictionary> dict, TNode<IntPtrT> capacity,
      TNode<IntPtrT> index);

  TNode<IntPtrT> LoadSwissNameDictionaryNumberOfElements(
      TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity);

  TNode<IntPtrT> LoadSwissNameDictionaryNumberOfDeletedElements(
      TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity);

  // Specialized operation to be used when adding entries:
  // If used capacity (= number of present + deleted elements) is less than
  // |max_usable|, increment the number of present entries and return the used
  // capacity value (prior to the incrementation). Otherwise, goto |bailout|.
  TNode<Uint32T> SwissNameDictionaryIncreaseElementCountOrBailout(
      TNode<ByteArray> meta_table, TNode<IntPtrT> capacity,
      TNode<Uint32T> max_usable_capacity, Label* bailout);

  // Specialized operation to be used when deleting entries: Decreases the
  // number of present entries and increases the number of deleted ones. Returns
  // new (= decremented) number of present entries.
  TNode<Uint32T> SwissNameDictionaryUpdateCountsForDeletion(
      TNode<ByteArray> meta_table, TNode<IntPtrT> capacity);

  void StoreSwissNameDictionaryCapacity(TNode<SwissNameDictionary> table,
                                        TNode<Int32T> capacity);

  void StoreSwissNameDictionaryEnumToEntryMapping(
      TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity,
      TNode<IntPtrT> enum_index, TNode<Int32T> entry);

  TNode<Name> LoadSwissNameDictionaryKey(TNode<SwissNameDictionary> dict,
                                         TNode<IntPtrT> entry);

  void StoreSwissNameDictionaryKeyAndValue(TNode<SwissNameDictionary> dict,
                                           TNode<IntPtrT> entry,
                                           TNode<Object> key,
                                           TNode<Object> value);

  // Equivalent to SwissNameDictionary::SetCtrl, therefore preserves the copy of
  // the first group at the end of the control table.
  void SwissNameDictionarySetCtrl(TNode<SwissNameDictionary> table,
                                  TNode<IntPtrT> capacity, TNode<IntPtrT> entry,
                                  TNode<Uint8T> ctrl);

  TNode<Uint64T> LoadSwissNameDictionaryCtrlTableGroup(TNode<IntPtrT> address);

  TNode<Uint8T> LoadSwissNameDictionaryPropertyDetails(
      TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity,
      TNode<IntPtrT> entry);

  void StoreSwissNameDictionaryPropertyDetails(TNode<SwissNameDictionary> table,
                                               TNode<IntPtrT> capacity,
                                               TNode<IntPtrT> entry,
                                               TNode<Uint8T> details);

  TNode<SwissNameDictionary> CopySwissNameDictionary(
      TNode<SwissNameDictionary> original);

  void SwissNameDictionaryFindEntry(TNode<SwissNameDictionary> table,
                                    TNode<Name> key, Label* found,
                                    TVariable<IntPtrT>* var_found_entry,
                                    Label* not_found);

  void SwissNameDictionaryAdd(TNode<SwissNameDictionary> table, TNode<Name> key,
                              TNode<Object> value,
                              TNode<Uint8T> property_details,
                              Label* needs_resize);

  // If the current code is running on a secondary stack, move the stack pointer
  // to the central stack (but not the frame pointer) and adjust the stack
  // limit. Returns the old stack pointer, or nullptr if no switch was
  // performed.
  TNode<RawPtrT> SwitchToTheCentralStackIfNeeded();
  // Switch the SP back to the secondary stack after switching to the central
  // stack.
  void SwitchFromTheCentralStack(TNode<RawPtrT> old_sp);

  TNode<BoolT> IsMarked(TNode<Object> object);

  void GetMarkBit(TNode<IntPtrT> object, TNode<IntPtrT>* cell,
                  TNode<IntPtrT>* mask);

  TNode<BoolT> IsPageFlagSet(TNode<IntPtrT> object, int mask) {
    TNode<IntPtrT> header = MemoryChunkFromAddress(object);
    TNode<IntPtrT> flags = UncheckedCast<IntPtrT>(
        Load(MachineType::Pointer(), header,
             IntPtrConstant(MemoryChunkLayout::kFlagsOffset)));
    return WordNotEqual(WordAnd(flags, IntPtrConstant(mask)),
                        IntPtrConstant(0));
  }

  TNode<BoolT> IsPageFlagReset(TNode<IntPtrT> object, int mask) {
    TNode<IntPtrT> header = MemoryChunkFromAddress(object);
    TNode<IntPtrT> flags = UncheckedCast<IntPtrT>(
        Load(MachineType::Pointer(), header,
             IntPtrConstant(MemoryChunkLayout::kFlagsOffset)));
    return WordEqual(WordAnd(flags, IntPtrConstant(mask)), IntPtrConstant(0));
  }

 private:
  friend class CodeStubArguments;

  void BigInt64Comparison(Operation op, TNode<Object>& left,
                          TNode<Object>& right, Label* return_true,
                          Label* return_false);

  void HandleBreakOnNode();

  TNode<HeapObject> AllocateRawDoubleAligned(TNode<IntPtrT> size_in_bytes,
                                             AllocationFlags flags,
                                             TNode<RawPtrT> top_address,
                                             TNode<RawPtrT> limit_address);
  TNode<HeapObject> AllocateRawUnaligned(TNode<IntPtrT> size_in_bytes,
                                         AllocationFlags flags,
                                         TNode<RawPtrT> top_address,
                                         TNode<RawPtrT> limit_address);
  TNode<HeapObject> AllocateRaw(TNode<IntPtrT> size_in_bytes,
                                AllocationFlags flags,
                                TNode<RawPtrT> top_address,
                                TNode<RawPtrT> limit_address);

  // Allocate and return a JSArray of given total size in bytes with header
  // fields initialized.
  TNode<JSArray> AllocateUninitializedJSArray(
      TNode<Map> array_map, TNode<Smi> length,
      std::optional<TNode<AllocationSite>> allocation_site,
      TNode<IntPtrT> size_in_bytes);

  // Increases the provided capacity to the next valid value, if necessary.
  template <typename CollectionType>
  TNode<CollectionType> AllocateOrderedHashTable(TNode<IntPtrT> capacity);

  // Uses the provided capacity (which must be valid) in verbatim.
  template <typename CollectionType>
  TNode<CollectionType> AllocateOrderedHashTableWithCapacity(
      TNode<IntPtrT> capacity);

  TNode<IntPtrT> SmiShiftBitsConstant() {
    return IntPtrConstant(kSmiShiftSize + kSmiTagSize);
  }
  TNode<Int32T> SmiShiftBitsConstant32() {
    return Int32Constant(kSmiShiftSize + kSmiTagSize);
  }

  TNode<String> AllocateSlicedString(RootIndex map_root_index,
                                     TNode<Uint32T> length,
                                     TNode<String> parent, TNode<Smi> offset);

  // Implements [Descriptor/Transition]Array::number_of_entries.
  template <typename Array>
  TNode<Uint32T> NumberOfEntries(TNode<Array> array);

  template <typename Array>
  constexpr int MaxNumberOfEntries();

  // Implements [Descriptor/Transition]Array::GetSortedKeyIndex.
  template <typename Array>
  TNode<Uint32T> GetSortedKeyIndex(TNode<Array> descriptors,
                                   TNode<Uint32T> entry_index);

  TNode<Smi> CollectFeedbackForString(TNode<Int32T> instance_type);
  void GenerateEqual_Same(TNode<Object> value, Label* if_equal,
                          Label* if_notequal,
                          TVariable<Smi>* var_type_feedback = nullptr);

  static const int kElementLoopUnrollThreshold = 8;

  // {convert_bigint} is only meaningful when {mode} == kToNumber.
  TNode<Numeric> NonNumberToNumberOrNumeric(
      TNode<Context> context, TNode<HeapObject> input, Object::Conversion mode,
      BigIntHandling bigint_handling = BigIntHandling::kThrow);

  enum IsKnownTaggedPointer { kNo, kYes };
  template <Object::Conversion conversion>
  void TaggedToWord32OrBigIntImpl(
      TNode<Context> context, TNode<Object> value, Label* if_number,
      TVariable<Word32T>* var_word32,
      IsKnownTaggedPointer is_known_tagged_pointer,
      const FeedbackValues& feedback, Label* if_bigint = nullptr,
      Label* if_bigint64 = nullptr,
      TVariable<BigInt>* var_maybe_bigint = nullptr);

  // Low-level accessors for Descriptor arrays.
  template <typename T>
  TNode<T> LoadDescriptorArrayElement(TNode<DescriptorArray> object,
                                      TNode<IntPtrT> index,
                                      int additional_offset);

  // Hide LoadRoot for subclasses of CodeStubAssembler. If you get an error
  // complaining about this method, don't make it public, add your root to
  // HEAP_(IM)MUTABLE_IMMOVABLE_OBJECT_LIST instead. If you *really* need
  // LoadRoot, use CodeAssembler::LoadRoot.
  TNode<Object> LoadRoot(RootIndex root_index) {
    return CodeAssembler::LoadRoot(root_index);
  }

  TNode<AnyTaggedT> LoadRootMapWord(RootIndex root_index) {
    return CodeAssembler::LoadRootMapWord(root_index);
  }

  template <typename TIndex>
  void StoreFixedArrayOrPropertyArrayElement(
      TNode<UnionOf<FixedArray, PropertyArray>> array, TNode<TIndex> index,
      TNode<Object> value, WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER,
      int additional_offset = 0);

  template <typename TIndex>
  void StoreElementTypedArrayBigInt(TNode<RawPtrT> elements, ElementsKind kind,
                                    TNode<TIndex> index, TNode<BigInt> value);

  template <typename TIndex>
  void StoreElementTypedArrayWord32(TNode<RawPtrT> elements, ElementsKind kind,
                                    TNode<TIndex> index, TNode<Word32T> value);

  // Store value to an elements array with given elements kind.
  // TODO(turbofan): For BIGINT64_ELEMENTS and BIGUINT64_ELEMENTS
  // we pass {value} as BigInt object instead of int64_t. We should
  // teach TurboFan to handle int64_t on 32-bit platforms eventually.
  // TODO(solanes): This method can go away and simplify into only one version
  // of StoreElement once we have "if constexpr" available to use.
  template <typename TArray, typename TIndex, typename TValue>
  void StoreElementTypedArray(TNode<TArray> elements, ElementsKind kind,
                              TNode<TIndex> index, TNode<TValue> value);

  template <typename TIndex>
  void StoreElement(TNode<FixedArrayBase> elements, ElementsKind kind,
                    TNode<TIndex> index, TNode<Object> value);

  template <typename TIndex>
  void StoreElement(TNode<FixedArrayBase> elements, ElementsKind kind,
                    TNode<TIndex> index, TNode<Float64T> value);

  // Converts {input} to a number if {input} is a plain primitve (i.e. String or
  // Oddball) and stores the result in {var_result}. Otherwise, it bails out to
  // {if_bailout}.
  void TryPlainPrimitiveNonNumberToNumber(TNode<HeapObject> input,
                                          TVariable<Number>* var_result,
                                          Label* if_bailout);

  void DcheckHasValidMap(TNode<HeapObject> object);

  template <typename TValue>
  void EmitElementStoreTypedArray(TNode<JSTypedArray> typed_array,
                                  TNode<IntPtrT> key, TNode<Object> value,
                                  ElementsKind elements_kind,
                                  KeyedAccessStoreMode store_mode,
                                  Label* bailout, TNode<Context> context,
                                  TVariable<Object>* maybe_converted_value);

  template <typename TValue>
  void EmitElementStoreTypedArrayUpdateValue(
      TNode<Object> value, ElementsKind elements_kind,
      TNode<TValue> converted_value, TVariable<Object>* maybe_converted_value);

  TNode<RawPtrT> SwitchToTheCentralStack();
};

class V8_EXPORT_PRIVATE CodeStubArguments {
 public:
  // |argc| specifies the number of arguments passed to the builtin.
  CodeStubArguments(CodeStubAssembler* assembler, TNode<IntPtrT> argc)
      : CodeStubArguments(assembler, argc, TNode<RawPtrT>()) {}
  CodeStubArguments(CodeStubAssembler* assembler, TNode<Int32T> argc)
      : CodeStubArguments(assembler, assembler->ChangeInt32ToIntPtr(argc)) {}
  CodeStubArguments(CodeStubAssembler* assembler, TNode<IntPtrT> argc,
                    TNode<RawPtrT> fp);

  // Used by Torque to construct arguments based on a Torque-defined
  // struct of values.
  CodeStubArguments(CodeStubAssembler* assembler,
                    TorqueStructArguments torque_arguments)
      : assembler_(assembler),
        argc_(torque_arguments.actual_count),
        base_(torque_arguments.base),
        fp_(torque_arguments.frame) {}

  TNode<Object> GetReceiver() const;
  // Replaces receiver argument on the expression stack. Should be used only
  // for manipulating arguments in trampoline builtins before tail calling
  // further with passing all the JS arguments as is.
  void SetReceiver(TNode<Object> object) const;

  // Computes address of the index'th argument.
  TNode<RawPtrT> AtIndexPtr(TNode<IntPtrT> index) const;

  // |index| is zero-based and does not include the receiver
  TNode<Object> AtIndex(TNode<IntPtrT> index) const;
  TNode<Object> AtIndex(int index) const;

  // Return the number of arguments (excluding the receiver).
  TNode<IntPtrT> GetLengthWithoutReceiver() const;
  // Return the number of arguments (including the receiver).
  TNode<IntPtrT> GetLengthWithReceiver() const;

  TorqueStructArguments GetTorqueArguments() const {
    return TorqueStructArguments{fp_, base_, GetLengthWithoutReceiver(), argc_};
  }

  TNode<Object> GetOptionalArgumentValue(TNode<IntPtrT> index,
                                         TNode<Object> default_value);
  TNode<Object> GetOptionalArgumentValue(TNode<IntPtrT> index) {
    return GetOptionalArgumentValue(index, assembler_->UndefinedConstant());
  }
  TNode<Object> GetOptionalArgumentValue(int index) {
    return GetOptionalArgumentValue(assembler_->IntPtrConstant(index));
  }

  void SetArgumentValue(TNode<IntPtrT> index, TNode<Object> value);

  // Iteration doesn't include the receiver. |first| and |last| are zero-based.
  using ForEachBodyFunction = std::function<void(TNode<Object> arg)>;
  void ForEach(const ForEachBodyFunction& body, TNode<IntPtrT> first = {},
               TNode<IntPtrT> last = {}) const {
    CodeStubAssembler::VariableList list(0, assembler_->zone());
    ForEach(list, body, first, last);
  }
  void ForEach(const CodeStubAssembler::VariableList& vars,
               const ForEachBodyFunction& body, TNode<IntPtrT> first = {},
               TNode<IntPtrT> last = {}) const;

  void PopAndReturn(TNode<Object> value);

 private:
  CodeStubAssembler* assembler_;
  TNode<IntPtrT> argc_;
  TNode<RawPtrT> base_;
  TNode<RawPtrT> fp_;
};

class ToDirectStringAssembler : public CodeStubAssembler {
 private:
  enum StringPointerKind { PTR_TO_DATA, PTR_TO_STRING };

 public:
  enum Flag {
    kDontUnpackSlicedStrings = 1 << 0,
  };
  using Flags = base::Flags<Flag>;

  ToDirectStringAssembler(compiler::CodeAssemblerState* state,
                          TNode<String> string, Flags flags = Flags());

  // Converts flat cons, thin, and sliced strings and returns the direct
  // string. The result can be either a sequential or external string.
  // Jumps to if_bailout if the string if the string is indirect and cannot
  // be unpacked.
  TNode<String> TryToDirect(Label* if_bailout);

  // Returns a pointer to the beginning of the string data.
  // Jumps to if_bailout if the external string cannot be unpacked.
  TNode<RawPtrT> PointerToData(Label* if_bailout) {
    return TryToSequential(PTR_TO_DATA, if_bailout);
  }

  // Returns a pointer that, offset-wise, looks like a String.
  // Jumps to if_bailout if the external string cannot be unpacked.
  TNode<RawPtrT> PointerToString(Label* if_bailout) {
    return TryToSequential(PTR_TO_STRING, if_bailout);
  }

  TNode<BoolT> IsOneByte();

  TNode<String> string() { return var_string_.value(); }
  TNode<IntPtrT> offset() { return var_offset_.value(); }
  TNode<Word32T> is_external() { return var_is_external_.value(); }

 private:
  TNode<RawPtrT> TryToSequential(StringPointerKind ptr_kind, Label* if_bailout);

  TVariable<String> var_string_;
#if V8_STATIC_ROOTS_BOOL
  TVariable<Map> var_map_;
#else
  TVariable<Int32T> var_instance_type_;
#endif
  // TODO(v8:9880): Use UintPtrT here.
  TVariable<IntPtrT> var_offset_;
  TVariable<Word32T> var_is_external_;

  const Flags flags_;
};

// Performs checks on a given prototype (e.g. map identity, property
// verification), intended for use in fast path checks.
class PrototypeCheckAssembler : public CodeStubAssembler {
 public:
  enum Flag {
    kCheckPrototypePropertyConstness = 1 << 0,
    kCheckPrototypePropertyIdentity = 1 << 1,
    kCheckFull =
        kCheckPrototypePropertyConstness | kCheckPrototypePropertyIdentity,
  };
  using Flags = base::Flags<Flag>;

  // A tuple describing a relevant property. It contains the descriptor index of
  // the property (within the descriptor array), the property's expected name
  // (stored as a root), and the property's expected value (stored on the native
  // context).
  struct DescriptorIndexNameValue {
    int descriptor_index;
    RootIndex name_root_index;
    int expected_value_context_index;
  };

  PrototypeCheckAssembler(compiler::CodeAssemblerState* state, Flags flags,
                          TNode<NativeContext> native_context,
                          TNode<Map> initial_prototype_map,
                          base::Vector<DescriptorIndexNameValue> properties);

  void CheckAndBranch(TNode<HeapObject> prototype, Label* if_unmodified,
                      Label* if_modified);

 private:
  const Flags flags_;
  const TNode<NativeContext> native_context_;
  const TNode<Map> initial_prototype_map_;
  const base::Vector<DescriptorIndexNameValue> properties_;
};

DEFINE_OPERATORS_FOR_FLAGS(CodeStubAssembler::AllocationFlags)

#define CLASS_MAP_CONSTANT_ADAPTER(V, rootIndexName, rootAccessorName,     \
                                   class_name)                             \
  template <>                                                              \
  inline bool CodeStubAssembler::ClassHasMapConstant<class_name>() {       \
    return true;                                                           \
  }                                                                        \
  template <>                                                              \
  inline TNode<Map> CodeStubAssembler::GetClassMapConstant<class_name>() { \
    return class_name##MapConstant();                                      \
  }

UNIQUE_INSTANCE_TYPE_MAP_LIST_GENERATOR(CLASS_MAP_CONSTANT_ADAPTER, _)

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_CODE_STUB_ASSEMBLER_H_
