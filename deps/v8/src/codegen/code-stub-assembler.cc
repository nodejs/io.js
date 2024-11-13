// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/code-stub-assembler.h"

#include <stdio.h>

#include <functional>
#include <optional>

#include "include/v8-internal.h"
#include "src/base/macros.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/tnode.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames-inl.h"
#include "src/execution/frames.h"
#include "src/execution/protectors.h"
#include "src/heap/heap-inl.h"  // For MutablePageMetadata. TODO(jkummerow): Drop.
#include "src/heap/mutable-page-metadata.h"
#include "src/logging/counters.h"
#include "src/numbers/integer-literal-inl.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/cell.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/function-kind.h"
#include "src/objects/heap-number.h"
#include "src/objects/instance-type-checker.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-generator.h"
#include "src/objects/oddball.h"
#include "src/objects/ordered-hash-table-inl.h"
#include "src/objects/property-cell.h"
#include "src/objects/property-descriptor-object.h"
#include "src/objects/tagged-field.h"
#include "src/roots/roots.h"
#include "third_party/v8/codegen/fp16-inl.h"

namespace v8 {
namespace internal {

namespace {

Builtin BigIntComparisonBuiltinOf(Operation const& op) {
  switch (op) {
    case Operation::kLessThan:
      return Builtin::kBigIntLessThan;
    case Operation::kGreaterThan:
      return Builtin::kBigIntGreaterThan;
    case Operation::kLessThanOrEqual:
      return Builtin::kBigIntLessThanOrEqual;
    case Operation::kGreaterThanOrEqual:
      return Builtin::kBigIntGreaterThanOrEqual;
    default:
      UNREACHABLE();
  }
}

}  // namespace

CodeStubAssembler::CodeStubAssembler(compiler::CodeAssemblerState* state)
    : compiler::CodeAssembler(state),
      TorqueGeneratedExportedMacrosAssembler(state) {
  if (v8_flags.csa_trap_on_node != nullptr) {
    HandleBreakOnNode();
  }
}

void CodeStubAssembler::HandleBreakOnNode() {
  // v8_flags.csa_trap_on_node should be in a form "STUB,NODE" where STUB is a
  // string specifying the name of a stub and NODE is number specifying node id.
  const char* name = state()->name();
  size_t name_length = strlen(name);
  if (strncmp(v8_flags.csa_trap_on_node, name, name_length) != 0) {
    // Different name.
    return;
  }
  size_t option_length = strlen(v8_flags.csa_trap_on_node);
  if (option_length < name_length + 2 ||
      v8_flags.csa_trap_on_node[name_length] != ',') {
    // Option is too short.
    return;
  }
  const char* start = &v8_flags.csa_trap_on_node[name_length + 1];
  char* end;
  int node_id = static_cast<int>(strtol(start, &end, 10));
  if (start == end) {
    // Bad node id.
    return;
  }
  BreakOnNode(node_id);
}

void CodeStubAssembler::Dcheck(const BranchGenerator& branch,
                               const char* message, const char* file, int line,
                               std::initializer_list<ExtraNode> extra_nodes,
                               const SourceLocation& loc) {
#if defined(DEBUG)
  if (v8_flags.debug_code) {
    Check(branch, message, file, line, extra_nodes, loc);
  }
#endif
}

void CodeStubAssembler::Dcheck(const NodeGenerator<BoolT>& condition_body,
                               const char* message, const char* file, int line,
                               std::initializer_list<ExtraNode> extra_nodes,
                               const SourceLocation& loc) {
#if defined(DEBUG)
  if (v8_flags.debug_code) {
    Check(condition_body, message, file, line, extra_nodes, loc);
  }
#endif
}

void CodeStubAssembler::Dcheck(TNode<Word32T> condition_node,
                               const char* message, const char* file, int line,
                               std::initializer_list<ExtraNode> extra_nodes,
                               const SourceLocation& loc) {
#if defined(DEBUG)
  if (v8_flags.debug_code) {
    Check(condition_node, message, file, line, extra_nodes, loc);
  }
#endif
}

void CodeStubAssembler::Check(const BranchGenerator& branch,
                              const char* message, const char* file, int line,
                              std::initializer_list<ExtraNode> extra_nodes,
                              const SourceLocation& loc) {
  Label ok(this);
  Label not_ok(this, Label::kDeferred);
  if (message != nullptr) {
    Comment({"[ Assert: ", loc}, message);
  } else {
    Comment({"[ Assert: ", loc});
  }
  branch(&ok, &not_ok);

  BIND(&not_ok);
  std::vector<FileAndLine> file_and_line;
  if (file != nullptr) {
    file_and_line.push_back({file, line});
  }
  FailAssert(message, file_and_line, extra_nodes, loc);

  BIND(&ok);
  Comment({"] Assert", SourceLocation()});
}

void CodeStubAssembler::Check(const NodeGenerator<BoolT>& condition_body,
                              const char* message, const char* file, int line,
                              std::initializer_list<ExtraNode> extra_nodes,
                              const SourceLocation& loc) {
  BranchGenerator branch = [=, this](Label* ok, Label* not_ok) {
    TNode<BoolT> condition = condition_body();
    Branch(condition, ok, not_ok);
  };

  Check(branch, message, file, line, extra_nodes, loc);
}

void CodeStubAssembler::Check(TNode<Word32T> condition_node,
                              const char* message, const char* file, int line,
                              std::initializer_list<ExtraNode> extra_nodes,
                              const SourceLocation& loc) {
  BranchGenerator branch = [=, this](Label* ok, Label* not_ok) {
    Branch(condition_node, ok, not_ok);
  };

  Check(branch, message, file, line, extra_nodes, loc);
}

void CodeStubAssembler::IncrementCallCount(
    TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot_id) {
  Comment("increment call count");
  TNode<Smi> call_count =
      CAST(LoadFeedbackVectorSlot(feedback_vector, slot_id, kTaggedSize));
  // The lowest {FeedbackNexus::CallCountField::kShift} bits of the call
  // count are used as flags. To increment the call count by 1 we hence
  // have to increment by 1 << {FeedbackNexus::CallCountField::kShift}.
  TNode<Smi> new_count = SmiAdd(
      call_count, SmiConstant(1 << FeedbackNexus::CallCountField::kShift));
  // Count is Smi, so we don't need a write barrier.
  StoreFeedbackVectorSlot(feedback_vector, slot_id, new_count,
                          SKIP_WRITE_BARRIER, kTaggedSize);
}

void CodeStubAssembler::FastCheck(TNode<BoolT> condition) {
  Label ok(this), not_ok(this, Label::kDeferred);
  Branch(condition, &ok, &not_ok);
  BIND(&not_ok);
  Unreachable();
  BIND(&ok);
}

void CodeStubAssembler::FailAssert(
    const char* message, const std::vector<FileAndLine>& files_and_lines,
    std::initializer_list<ExtraNode> extra_nodes, const SourceLocation& loc) {
  DCHECK_NOT_NULL(message);
  base::EmbeddedVector<char, 1024> chars;
  std::stringstream stream;
  for (auto it = files_and_lines.rbegin(); it != files_and_lines.rend(); ++it) {
    if (it->first != nullptr) {
      stream << " [" << it->first << ":" << it->second << "]";
#ifndef DEBUG
      // To limit the size of these strings in release builds, we include only
      // the innermost macro's file name and line number.
      break;
#endif
    }
  }
  std::string files_and_lines_text = stream.str();
  if (!files_and_lines_text.empty()) {
    SNPrintF(chars, "%s%s", message, files_and_lines_text.c_str());
    message = chars.begin();
  }
  TNode<String> message_node = StringConstant(message);

#ifdef DEBUG
  // Only print the extra nodes in debug builds.
  for (auto& node : extra_nodes) {
    CallRuntime(Runtime::kPrintWithNameForAssert, SmiConstant(0),
                StringConstant(node.second), node.first);
  }
#endif

  AbortCSADcheck(message_node);
  Unreachable();
}

TNode<Int32T> CodeStubAssembler::SelectInt32Constant(TNode<BoolT> condition,
                                                     int true_value,
                                                     int false_value) {
  return SelectConstant<Int32T>(condition, Int32Constant(true_value),
                                Int32Constant(false_value));
}

TNode<IntPtrT> CodeStubAssembler::SelectIntPtrConstant(TNode<BoolT> condition,
                                                       int true_value,
                                                       int false_value) {
  return SelectConstant<IntPtrT>(condition, IntPtrConstant(true_value),
                                 IntPtrConstant(false_value));
}

TNode<Boolean> CodeStubAssembler::SelectBooleanConstant(
    TNode<BoolT> condition) {
  return SelectConstant<Boolean>(condition, TrueConstant(), FalseConstant());
}

TNode<Smi> CodeStubAssembler::SelectSmiConstant(TNode<BoolT> condition,
                                                Tagged<Smi> true_value,
                                                Tagged<Smi> false_value) {
  return SelectConstant<Smi>(condition, SmiConstant(true_value),
                             SmiConstant(false_value));
}

TNode<Smi> CodeStubAssembler::NoContextConstant() {
  return SmiConstant(Context::kNoContext);
}

#define HEAP_CONSTANT_ACCESSOR(rootIndexName, rootAccessorName, name)          \
  TNode<RemoveTagged<decltype(std::declval<Heap>().rootAccessorName())>::type> \
      CodeStubAssembler::name##Constant() {                                    \
    return UncheckedCast<RemoveTagged<                                         \
        decltype(std::declval<Heap>().rootAccessorName())>::type>(             \
        LoadRoot(RootIndex::k##rootIndexName));                                \
  }
HEAP_MUTABLE_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_ACCESSOR)
#undef HEAP_CONSTANT_ACCESSOR

#define HEAP_CONSTANT_ACCESSOR(rootIndexName, rootAccessorName, name)       \
  TNode<RemoveTagged<                                                       \
      decltype(std::declval<ReadOnlyRoots>().rootAccessorName())>::type>    \
      CodeStubAssembler::name##Constant() {                                 \
    return UncheckedCast<RemoveTagged<                                      \
        decltype(std::declval<ReadOnlyRoots>().rootAccessorName())>::type>( \
        LoadRoot(RootIndex::k##rootIndexName));                             \
  }
HEAP_IMMUTABLE_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_ACCESSOR)
#undef HEAP_CONSTANT_ACCESSOR

#define HEAP_CONSTANT_TEST(rootIndexName, rootAccessorName, name)    \
  TNode<BoolT> CodeStubAssembler::Is##name(TNode<Object> value) {    \
    return TaggedEqual(value, name##Constant());                     \
  }                                                                  \
  TNode<BoolT> CodeStubAssembler::IsNot##name(TNode<Object> value) { \
    return TaggedNotEqual(value, name##Constant());                  \
  }
HEAP_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_TEST)
#undef HEAP_CONSTANT_TEST

TNode<BInt> CodeStubAssembler::BIntConstant(int value) {
#if defined(BINT_IS_SMI)
  return SmiConstant(value);
#elif defined(BINT_IS_INTPTR)
  return IntPtrConstant(value);
#else
#error Unknown architecture.
#endif
}

template <>
TNode<Smi> CodeStubAssembler::IntPtrOrSmiConstant<Smi>(int value) {
  return SmiConstant(value);
}

template <>
TNode<IntPtrT> CodeStubAssembler::IntPtrOrSmiConstant<IntPtrT>(int value) {
  return IntPtrConstant(value);
}

template <>
TNode<UintPtrT> CodeStubAssembler::IntPtrOrSmiConstant<UintPtrT>(int value) {
  return Unsigned(IntPtrConstant(value));
}

template <>
TNode<RawPtrT> CodeStubAssembler::IntPtrOrSmiConstant<RawPtrT>(int value) {
  return ReinterpretCast<RawPtrT>(IntPtrConstant(value));
}

bool CodeStubAssembler::TryGetIntPtrOrSmiConstantValue(
    TNode<Smi> maybe_constant, int* value) {
  Tagged<Smi> smi_constant;
  if (TryToSmiConstant(maybe_constant, &smi_constant)) {
    *value = Smi::ToInt(smi_constant);
    return true;
  }
  return false;
}

bool CodeStubAssembler::TryGetIntPtrOrSmiConstantValue(
    TNode<IntPtrT> maybe_constant, int* value) {
  int32_t int32_constant;
  if (TryToInt32Constant(maybe_constant, &int32_constant)) {
    *value = int32_constant;
    return true;
  }
  return false;
}

TNode<IntPtrT> CodeStubAssembler::IntPtrRoundUpToPowerOfTwo32(
    TNode<IntPtrT> value) {
  Comment("IntPtrRoundUpToPowerOfTwo32");
  CSA_DCHECK(this, UintPtrLessThanOrEqual(value, IntPtrConstant(0x80000000u)));
  value = Signed(IntPtrSub(value, IntPtrConstant(1)));
  for (int i = 1; i <= 16; i *= 2) {
    value = Signed(WordOr(value, WordShr(value, IntPtrConstant(i))));
  }
  return Signed(IntPtrAdd(value, IntPtrConstant(1)));
}

TNode<BoolT> CodeStubAssembler::WordIsPowerOfTwo(TNode<IntPtrT> value) {
  intptr_t constant;
  if (TryToIntPtrConstant(value, &constant)) {
    return BoolConstant(base::bits::IsPowerOfTwo(constant));
  }
  // value && !(value & (value - 1))
  return IntPtrEqual(Select<IntPtrT>(
                         IntPtrEqual(value, IntPtrConstant(0)),
                         [=, this] { return IntPtrConstant(1); },
                         [=, this] {
                           return WordAnd(value,
                                          IntPtrSub(value, IntPtrConstant(1)));
                         }),
                     IntPtrConstant(0));
}

TNode<BoolT> CodeStubAssembler::Float64AlmostEqual(TNode<Float64T> x,
                                                   TNode<Float64T> y,
                                                   double max_relative_error) {
  TVARIABLE(BoolT, result, BoolConstant(true));
  Label done(this);

  GotoIf(Float64Equal(x, y), &done);
  GotoIf(Float64LessThan(Float64Div(Float64Abs(Float64Sub(x, y)),
                                    Float64Max(Float64Abs(x), Float64Abs(y))),
                         Float64Constant(max_relative_error)),
         &done);

  result = BoolConstant(false);
  Goto(&done);

  BIND(&done);
  return result.value();
}

TNode<Float64T> CodeStubAssembler::Float64Round(TNode<Float64T> x) {
  TNode<Float64T> one = Float64Constant(1.0);
  TNode<Float64T> one_half = Float64Constant(0.5);

  Label return_x(this);

  // Round up {x} towards Infinity.
  TVARIABLE(Float64T, var_x, Float64Ceil(x));

  GotoIf(Float64LessThanOrEqual(Float64Sub(var_x.value(), one_half), x),
         &return_x);
  var_x = Float64Sub(var_x.value(), one);
  Goto(&return_x);

  BIND(&return_x);
  return var_x.value();
}

TNode<Float64T> CodeStubAssembler::Float64Ceil(TNode<Float64T> x) {
  TVARIABLE(Float64T, var_x, x);
  Label round_op_supported(this), round_op_fallback(this), return_x(this);
  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  Branch(UniqueInt32Constant(IsFloat64RoundUpSupported()), &round_op_supported,
         &round_op_fallback);

  BIND(&round_op_supported);
  {
    // This optional operation is used behind a static check and we rely
    // on the dead code elimination to remove this unused unsupported
    // instruction. We generate builtins this way in order to ensure that
    // builtins PGO profiles are interchangeable between architectures.
    var_x = Float64RoundUp(x);
    Goto(&return_x);
  }

  BIND(&round_op_fallback);
  {
    TNode<Float64T> one = Float64Constant(1.0);
    TNode<Float64T> zero = Float64Constant(0.0);
    TNode<Float64T> two_52 = Float64Constant(4503599627370496.0E0);
    TNode<Float64T> minus_two_52 = Float64Constant(-4503599627370496.0E0);

    Label return_minus_x(this);

    // Check if {x} is greater than zero.
    Label if_xgreaterthanzero(this), if_xnotgreaterthanzero(this);
    Branch(Float64GreaterThan(x, zero), &if_xgreaterthanzero,
           &if_xnotgreaterthanzero);

    BIND(&if_xgreaterthanzero);
    {
      // Just return {x} unless it's in the range ]0,2^52[.
      GotoIf(Float64GreaterThanOrEqual(x, two_52), &return_x);

      // Round positive {x} towards Infinity.
      var_x = Float64Sub(Float64Add(two_52, x), two_52);
      GotoIfNot(Float64LessThan(var_x.value(), x), &return_x);
      var_x = Float64Add(var_x.value(), one);
      Goto(&return_x);
    }

    BIND(&if_xnotgreaterthanzero);
    {
      // Just return {x} unless it's in the range ]-2^52,0[
      GotoIf(Float64LessThanOrEqual(x, minus_two_52), &return_x);
      GotoIfNot(Float64LessThan(x, zero), &return_x);

      // Round negated {x} towards Infinity and return the result negated.
      TNode<Float64T> minus_x = Float64Neg(x);
      var_x = Float64Sub(Float64Add(two_52, minus_x), two_52);
      GotoIfNot(Float64GreaterThan(var_x.value(), minus_x), &return_minus_x);
      var_x = Float64Sub(var_x.value(), one);
      Goto(&return_minus_x);
    }

    BIND(&return_minus_x);
    var_x = Float64Neg(var_x.value());
    Goto(&return_x);
  }
  BIND(&return_x);
  return var_x.value();
}

TNode<Float64T> CodeStubAssembler::Float64Floor(TNode<Float64T> x) {
  TVARIABLE(Float64T, var_x, x);
  Label round_op_supported(this), round_op_fallback(this), return_x(this);
  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  Branch(UniqueInt32Constant(IsFloat64RoundDownSupported()),
         &round_op_supported, &round_op_fallback);

  BIND(&round_op_supported);
  {
    // This optional operation is used behind a static check and we rely
    // on the dead code elimination to remove this unused unsupported
    // instruction. We generate builtins this way in order to ensure that
    // builtins PGO profiles are interchangeable between architectures.
    var_x = Float64RoundDown(x);
    Goto(&return_x);
  }

  BIND(&round_op_fallback);
  {
    TNode<Float64T> one = Float64Constant(1.0);
    TNode<Float64T> zero = Float64Constant(0.0);
    TNode<Float64T> two_52 = Float64Constant(4503599627370496.0E0);
    TNode<Float64T> minus_two_52 = Float64Constant(-4503599627370496.0E0);

    Label return_minus_x(this);

    // Check if {x} is greater than zero.
    Label if_xgreaterthanzero(this), if_xnotgreaterthanzero(this);
    Branch(Float64GreaterThan(x, zero), &if_xgreaterthanzero,
           &if_xnotgreaterthanzero);

    BIND(&if_xgreaterthanzero);
    {
      // Just return {x} unless it's in the range ]0,2^52[.
      GotoIf(Float64GreaterThanOrEqual(x, two_52), &return_x);

      // Round positive {x} towards -Infinity.
      var_x = Float64Sub(Float64Add(two_52, x), two_52);
      GotoIfNot(Float64GreaterThan(var_x.value(), x), &return_x);
      var_x = Float64Sub(var_x.value(), one);
      Goto(&return_x);
    }

    BIND(&if_xnotgreaterthanzero);
    {
      // Just return {x} unless it's in the range ]-2^52,0[
      GotoIf(Float64LessThanOrEqual(x, minus_two_52), &return_x);
      GotoIfNot(Float64LessThan(x, zero), &return_x);

      // Round negated {x} towards -Infinity and return the result negated.
      TNode<Float64T> minus_x = Float64Neg(x);
      var_x = Float64Sub(Float64Add(two_52, minus_x), two_52);
      GotoIfNot(Float64LessThan(var_x.value(), minus_x), &return_minus_x);
      var_x = Float64Add(var_x.value(), one);
      Goto(&return_minus_x);
    }

    BIND(&return_minus_x);
    var_x = Float64Neg(var_x.value());
    Goto(&return_x);
  }
  BIND(&return_x);
  return var_x.value();
}

TNode<Float64T> CodeStubAssembler::Float64RoundToEven(TNode<Float64T> x) {
  TVARIABLE(Float64T, var_result);
  Label round_op_supported(this), round_op_fallback(this), done(this);
  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  Branch(UniqueInt32Constant(IsFloat64RoundTiesEvenSupported()),
         &round_op_supported, &round_op_fallback);

  BIND(&round_op_supported);
  {
    // This optional operation is used behind a static check and we rely
    // on the dead code elimination to remove this unused unsupported
    // instruction. We generate builtins this way in order to ensure that
    // builtins PGO profiles are interchangeable between architectures.
    var_result = Float64RoundTiesEven(x);
    Goto(&done);
  }

  BIND(&round_op_fallback);
  {
    // See ES#sec-touint8clamp for details.
    TNode<Float64T> f = Float64Floor(x);
    TNode<Float64T> f_and_half = Float64Add(f, Float64Constant(0.5));

    Label return_f(this), return_f_plus_one(this);

    GotoIf(Float64LessThan(f_and_half, x), &return_f_plus_one);
    GotoIf(Float64LessThan(x, f_and_half), &return_f);
    {
      TNode<Float64T> f_mod_2 = Float64Mod(f, Float64Constant(2.0));
      Branch(Float64Equal(f_mod_2, Float64Constant(0.0)), &return_f,
             &return_f_plus_one);
    }

    BIND(&return_f);
    var_result = f;
    Goto(&done);

    BIND(&return_f_plus_one);
    var_result = Float64Add(f, Float64Constant(1.0));
    Goto(&done);
  }
  BIND(&done);
  return var_result.value();
}

TNode<Float64T> CodeStubAssembler::Float64Trunc(TNode<Float64T> x) {
  TVARIABLE(Float64T, var_x, x);
  Label trunc_op_supported(this), trunc_op_fallback(this), return_x(this);
  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  Branch(UniqueInt32Constant(IsFloat64RoundTruncateSupported()),
         &trunc_op_supported, &trunc_op_fallback);

  BIND(&trunc_op_supported);
  {
    // This optional operation is used behind a static check and we rely
    // on the dead code elimination to remove this unused unsupported
    // instruction. We generate builtins this way in order to ensure that
    // builtins PGO profiles are interchangeable between architectures.
    var_x = Float64RoundTruncate(x);
    Goto(&return_x);
  }

  BIND(&trunc_op_fallback);
  {
    TNode<Float64T> one = Float64Constant(1.0);
    TNode<Float64T> zero = Float64Constant(0.0);
    TNode<Float64T> two_52 = Float64Constant(4503599627370496.0E0);
    TNode<Float64T> minus_two_52 = Float64Constant(-4503599627370496.0E0);

    Label return_minus_x(this);

    // Check if {x} is greater than 0.
    Label if_xgreaterthanzero(this), if_xnotgreaterthanzero(this);
    Branch(Float64GreaterThan(x, zero), &if_xgreaterthanzero,
           &if_xnotgreaterthanzero);

    BIND(&if_xgreaterthanzero);
    {
      Label round_op_supported(this), round_op_fallback(this);
      Branch(UniqueInt32Constant(IsFloat64RoundDownSupported()),
             &round_op_supported, &round_op_fallback);
      BIND(&round_op_supported);
      {
        // This optional operation is used behind a static check and we rely
        // on the dead code elimination to remove this unused unsupported
        // instruction. We generate builtins this way in order to ensure that
        // builtins PGO profiles are interchangeable between architectures.
        var_x = Float64RoundDown(x);
        Goto(&return_x);
      }
      BIND(&round_op_fallback);
      {
        // Just return {x} unless it's in the range ]0,2^52[.
        GotoIf(Float64GreaterThanOrEqual(x, two_52), &return_x);

        // Round positive {x} towards -Infinity.
        var_x = Float64Sub(Float64Add(two_52, x), two_52);
        GotoIfNot(Float64GreaterThan(var_x.value(), x), &return_x);
        var_x = Float64Sub(var_x.value(), one);
        Goto(&return_x);
      }
    }

    BIND(&if_xnotgreaterthanzero);
    {
      Label round_op_supported(this), round_op_fallback(this);
      Branch(UniqueInt32Constant(IsFloat64RoundUpSupported()),
             &round_op_supported, &round_op_fallback);
      BIND(&round_op_supported);
      {
        // This optional operation is used behind a static check and we rely
        // on the dead code elimination to remove this unused unsupported
        // instruction. We generate builtins this way in order to ensure that
        // builtins PGO profiles are interchangeable between architectures.
        var_x = Float64RoundUp(x);
        Goto(&return_x);
      }
      BIND(&round_op_fallback);
      {
        // Just return {x} unless its in the range ]-2^52,0[.
        GotoIf(Float64LessThanOrEqual(x, minus_two_52), &return_x);
        GotoIfNot(Float64LessThan(x, zero), &return_x);

        // Round negated {x} towards -Infinity and return result negated.
        TNode<Float64T> minus_x = Float64Neg(x);
        var_x = Float64Sub(Float64Add(two_52, minus_x), two_52);
        GotoIfNot(Float64GreaterThan(var_x.value(), minus_x), &return_minus_x);
        var_x = Float64Sub(var_x.value(), one);
        Goto(&return_minus_x);
      }
    }

    BIND(&return_minus_x);
    var_x = Float64Neg(var_x.value());
    Goto(&return_x);
  }
  BIND(&return_x);
  return var_x.value();
}

TNode<IntPtrT> CodeStubAssembler::PopulationCountFallback(
    TNode<UintPtrT> value) {
  // Taken from slow path of base::bits::CountPopulation, the comments here show
  // C++ code and comments from there for reference.
  // Fall back to divide-and-conquer popcount (see "Hacker's Delight" by Henry
  // S. Warren,  Jr.), chapter 5-1.
  constexpr uintptr_t mask[] = {static_cast<uintptr_t>(0x5555555555555555),
                                static_cast<uintptr_t>(0x3333333333333333),
                                static_cast<uintptr_t>(0x0f0f0f0f0f0f0f0f)};

  // TNode<UintPtrT> value = Unsigned(value_word);
  TNode<UintPtrT> lhs, rhs;

  // Start with 64 buckets of 1 bits, holding values from [0,1].
  // {value = ((value >> 1) & mask[0]) + (value & mask[0])}
  lhs = WordAnd(WordShr(value, UintPtrConstant(1)), UintPtrConstant(mask[0]));
  rhs = WordAnd(value, UintPtrConstant(mask[0]));
  value = UintPtrAdd(lhs, rhs);

  // Having 32 buckets of 2 bits, holding values from [0,2] now.
  // {value = ((value >> 2) & mask[1]) + (value & mask[1])}
  lhs = WordAnd(WordShr(value, UintPtrConstant(2)), UintPtrConstant(mask[1]));
  rhs = WordAnd(value, UintPtrConstant(mask[1]));
  value = UintPtrAdd(lhs, rhs);

  // Having 16 buckets of 4 bits, holding values from [0,4] now.
  // {value = ((value >> 4) & mask[2]) + (value & mask[2])}
  lhs = WordAnd(WordShr(value, UintPtrConstant(4)), UintPtrConstant(mask[2]));
  rhs = WordAnd(value, UintPtrConstant(mask[2]));
  value = UintPtrAdd(lhs, rhs);

  // Having 8 buckets of 8 bits, holding values from [0,8] now.
  // From this point on, the buckets are bigger than the number of bits
  // required to hold the values, and the buckets are bigger the maximum
  // result, so there's no need to mask value anymore, since there's no
  // more risk of overflow between buckets.
  // {value = (value >> 8) + value}
  lhs = WordShr(value, UintPtrConstant(8));
  value = UintPtrAdd(lhs, value);

  // Having 4 buckets of 16 bits, holding values from [0,16] now.
  // {value = (value >> 16) + value}
  lhs = WordShr(value, UintPtrConstant(16));
  value = UintPtrAdd(lhs, value);

  if (Is64()) {
    // Having 2 buckets of 32 bits, holding values from [0,32] now.
    // {value = (value >> 32) + value}
    lhs = WordShr(value, UintPtrConstant(32));
    value = UintPtrAdd(lhs, value);
  }

  // Having 1 buckets of sizeof(intptr_t) bits, holding values from [0,64] now.
  // {return static_cast<unsigned>(value & 0xff)}
  return Signed(WordAnd(value, UintPtrConstant(0xff)));
}

TNode<Int64T> CodeStubAssembler::PopulationCount64(TNode<Word64T> value) {
  if (IsWord64PopcntSupported()) {
    return Word64Popcnt(value);
  }

  if (Is32()) {
    // Unsupported.
    UNREACHABLE();
  }

  return ReinterpretCast<Int64T>(
      PopulationCountFallback(ReinterpretCast<UintPtrT>(value)));
}

TNode<Int32T> CodeStubAssembler::PopulationCount32(TNode<Word32T> value) {
  if (IsWord32PopcntSupported()) {
    return Word32Popcnt(value);
  }

  if (Is32()) {
    TNode<IntPtrT> res =
        PopulationCountFallback(ReinterpretCast<UintPtrT>(value));
    return ReinterpretCast<Int32T>(res);
  } else {
    TNode<IntPtrT> res = PopulationCountFallback(
        ReinterpretCast<UintPtrT>(ChangeUint32ToUint64(value)));
    return TruncateInt64ToInt32(ReinterpretCast<Int64T>(res));
  }
}

TNode<Int64T> CodeStubAssembler::CountTrailingZeros64(TNode<Word64T> value) {
  if (IsWord64CtzSupported()) {
    return Word64Ctz(value);
  }

  if (Is32()) {
    // Unsupported.
    UNREACHABLE();
  }

  // Same fallback as in base::bits::CountTrailingZeros.
  // Fall back to popcount (see "Hacker's Delight" by Henry S. Warren, Jr.),
  // chapter 5-4. On x64, since is faster than counting in a loop and faster
  // than doing binary search.
  TNode<Word64T> lhs = Word64Not(value);
  TNode<Word64T> rhs = Uint64Sub(Unsigned(value), Uint64Constant(1));
  return PopulationCount64(Word64And(lhs, rhs));
}

TNode<Int32T> CodeStubAssembler::CountTrailingZeros32(TNode<Word32T> value) {
  if (IsWord32CtzSupported()) {
    return Word32Ctz(value);
  }

  if (Is32()) {
    // Same fallback as in Word64CountTrailingZeros.
    TNode<Word32T> lhs = Word32BitwiseNot(value);
    TNode<Word32T> rhs = Int32Sub(Signed(value), Int32Constant(1));
    return PopulationCount32(Word32And(lhs, rhs));
  } else {
    TNode<Int64T> res64 = CountTrailingZeros64(ChangeUint32ToUint64(value));
    return TruncateInt64ToInt32(Signed(res64));
  }
}

TNode<Int64T> CodeStubAssembler::CountLeadingZeros64(TNode<Word64T> value) {
  return Word64Clz(value);
}

TNode<Int32T> CodeStubAssembler::CountLeadingZeros32(TNode<Word32T> value) {
  return Word32Clz(value);
}

template <>
TNode<Smi> CodeStubAssembler::TaggedToParameter(TNode<Smi> value) {
  return value;
}

template <>
TNode<IntPtrT> CodeStubAssembler::TaggedToParameter(TNode<Smi> value) {
  return SmiUntag(value);
}

TNode<IntPtrT> CodeStubAssembler::TaggedIndexToIntPtr(
    TNode<TaggedIndex> value) {
  return Signed(WordSarShiftOutZeros(BitcastTaggedToWordForTagAndSmiBits(value),
                                     IntPtrConstant(kSmiTagSize)));
}

TNode<TaggedIndex> CodeStubAssembler::IntPtrToTaggedIndex(
    TNode<IntPtrT> value) {
  return ReinterpretCast<TaggedIndex>(
      BitcastWordToTaggedSigned(WordShl(value, IntPtrConstant(kSmiTagSize))));
}

TNode<Smi> CodeStubAssembler::TaggedIndexToSmi(TNode<TaggedIndex> value) {
  if (SmiValuesAre32Bits()) {
    DCHECK_EQ(kSmiShiftSize, 31);
    return BitcastWordToTaggedSigned(
        WordShl(BitcastTaggedToWordForTagAndSmiBits(value),
                IntPtrConstant(kSmiShiftSize)));
  }
  DCHECK(SmiValuesAre31Bits());
  DCHECK_EQ(kSmiShiftSize, 0);
  return ReinterpretCast<Smi>(value);
}

TNode<TaggedIndex> CodeStubAssembler::SmiToTaggedIndex(TNode<Smi> value) {
  if (kSystemPointerSize == kInt32Size) {
    return ReinterpretCast<TaggedIndex>(value);
  }
  if (SmiValuesAre32Bits()) {
    DCHECK_EQ(kSmiShiftSize, 31);
    return ReinterpretCast<TaggedIndex>(BitcastWordToTaggedSigned(
        WordSar(BitcastTaggedToWordForTagAndSmiBits(value),
                IntPtrConstant(kSmiShiftSize))));
  }
  DCHECK(SmiValuesAre31Bits());
  DCHECK_EQ(kSmiShiftSize, 0);
  // Just sign-extend the lower 32 bits.
  TNode<Int32T> raw =
      TruncateWordToInt32(BitcastTaggedToWordForTagAndSmiBits(value));
  return ReinterpretCast<TaggedIndex>(
      BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(raw)));
}

TNode<Smi> CodeStubAssembler::NormalizeSmiIndex(TNode<Smi> smi_index) {
  if (COMPRESS_POINTERS_BOOL) {
    TNode<Int32T> raw =
        TruncateWordToInt32(BitcastTaggedToWordForTagAndSmiBits(smi_index));
    smi_index = BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(raw));
  }
  return smi_index;
}

TNode<Smi> CodeStubAssembler::SmiFromInt32(TNode<Int32T> value) {
  if (COMPRESS_POINTERS_BOOL) {
    static_assert(!COMPRESS_POINTERS_BOOL || (kSmiShiftSize + kSmiTagSize == 1),
                  "Use shifting instead of add");
    return BitcastWordToTaggedSigned(
        ChangeUint32ToWord(Int32Add(value, value)));
  }
  return SmiTag(ChangeInt32ToIntPtr(value));
}

TNode<Smi> CodeStubAssembler::SmiFromUint32(TNode<Uint32T> value) {
  CSA_DCHECK(this, IntPtrLessThan(ChangeUint32ToWord(value),
                                  IntPtrConstant(Smi::kMaxValue)));
  return SmiFromInt32(Signed(value));
}

TNode<BoolT> CodeStubAssembler::IsValidPositiveSmi(TNode<IntPtrT> value) {
  intptr_t constant_value;
  if (TryToIntPtrConstant(value, &constant_value)) {
    return (static_cast<uintptr_t>(constant_value) <=
            static_cast<uintptr_t>(Smi::kMaxValue))
               ? Int32TrueConstant()
               : Int32FalseConstant();
  }

  return UintPtrLessThanOrEqual(value, IntPtrConstant(Smi::kMaxValue));
}

TNode<Smi> CodeStubAssembler::SmiTag(TNode<IntPtrT> value) {
  int32_t constant_value;
  if (TryToInt32Constant(value, &constant_value) &&
      Smi::IsValid(constant_value)) {
    return SmiConstant(constant_value);
  }
  if (COMPRESS_POINTERS_BOOL) {
    return SmiFromInt32(TruncateIntPtrToInt32(value));
  }
  TNode<Smi> smi =
      BitcastWordToTaggedSigned(WordShl(value, SmiShiftBitsConstant()));
  return smi;
}

TNode<IntPtrT> CodeStubAssembler::SmiUntag(TNode<Smi> value) {
  intptr_t constant_value;
  if (TryToIntPtrConstant(value, &constant_value)) {
    return IntPtrConstant(constant_value >> (kSmiShiftSize + kSmiTagSize));
  }
  TNode<IntPtrT> raw_bits = BitcastTaggedToWordForTagAndSmiBits(value);
  if (COMPRESS_POINTERS_BOOL) {
    return ChangeInt32ToIntPtr(Word32SarShiftOutZeros(
        TruncateIntPtrToInt32(raw_bits), SmiShiftBitsConstant32()));
  }
  return Signed(WordSarShiftOutZeros(raw_bits, SmiShiftBitsConstant()));
}

TNode<Int32T> CodeStubAssembler::SmiToInt32(TNode<Smi> value) {
  if (COMPRESS_POINTERS_BOOL) {
    return Signed(Word32SarShiftOutZeros(
        TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(value)),
        SmiShiftBitsConstant32()));
  }
  TNode<IntPtrT> result = SmiUntag(value);
  return TruncateIntPtrToInt32(result);
}

TNode<Uint32T> CodeStubAssembler::PositiveSmiToUint32(TNode<Smi> value) {
  DCHECK(SmiGreaterThanOrEqual(value, SmiConstant(0)));
  return Unsigned(SmiToInt32(value));
}

TNode<IntPtrT> CodeStubAssembler::PositiveSmiUntag(TNode<Smi> value) {
  return ChangePositiveInt32ToIntPtr(SmiToInt32(value));
}

TNode<Float64T> CodeStubAssembler::SmiToFloat64(TNode<Smi> value) {
  return ChangeInt32ToFloat64(SmiToInt32(value));
}

TNode<Smi> CodeStubAssembler::SmiMax(TNode<Smi> a, TNode<Smi> b) {
  return SelectConstant<Smi>(SmiLessThan(a, b), b, a);
}

TNode<Smi> CodeStubAssembler::SmiMin(TNode<Smi> a, TNode<Smi> b) {
  return SelectConstant<Smi>(SmiLessThan(a, b), a, b);
}

TNode<IntPtrT> CodeStubAssembler::TryIntPtrAdd(TNode<IntPtrT> a,
                                               TNode<IntPtrT> b,
                                               Label* if_overflow) {
  TNode<PairT<IntPtrT, BoolT>> pair = IntPtrAddWithOverflow(a, b);
  TNode<BoolT> overflow = Projection<1>(pair);
  GotoIf(overflow, if_overflow);
  return Projection<0>(pair);
}

TNode<IntPtrT> CodeStubAssembler::TryIntPtrSub(TNode<IntPtrT> a,
                                               TNode<IntPtrT> b,
                                               Label* if_overflow) {
  TNode<PairT<IntPtrT, BoolT>> pair = IntPtrSubWithOverflow(a, b);
  TNode<BoolT> overflow = Projection<1>(pair);
  GotoIf(overflow, if_overflow);
  return Projection<0>(pair);
}

TNode<IntPtrT> CodeStubAssembler::TryIntPtrMul(TNode<IntPtrT> a,
                                               TNode<IntPtrT> b,
                                               Label* if_overflow) {
  TNode<PairT<IntPtrT, BoolT>> pair = IntPtrMulWithOverflow(a, b);
  TNode<BoolT> overflow = Projection<1>(pair);
  GotoIf(overflow, if_overflow);
  return Projection<0>(pair);
}

TNode<IntPtrT> CodeStubAssembler::TryIntPtrDiv(TNode<IntPtrT> a,
                                               TNode<IntPtrT> b,
                                               Label* if_div_zero) {
  GotoIf(IntPtrEqual(b, IntPtrConstant(0)), if_div_zero);
  return IntPtrDiv(a, b);
}

TNode<IntPtrT> CodeStubAssembler::TryIntPtrMod(TNode<IntPtrT> a,
                                               TNode<IntPtrT> b,
                                               Label* if_div_zero) {
  GotoIf(IntPtrEqual(b, IntPtrConstant(0)), if_div_zero);
  return IntPtrMod(a, b);
}

TNode<Int32T> CodeStubAssembler::TryInt32Mul(TNode<Int32T> a, TNode<Int32T> b,
                                             Label* if_overflow) {
  TNode<PairT<Int32T, BoolT>> pair = Int32MulWithOverflow(a, b);
  TNode<BoolT> overflow = Projection<1>(pair);
  GotoIf(overflow, if_overflow);
  return Projection<0>(pair);
}

TNode<Smi> CodeStubAssembler::TrySmiAdd(TNode<Smi> lhs, TNode<Smi> rhs,
                                        Label* if_overflow) {
  if (SmiValuesAre32Bits()) {
    return BitcastWordToTaggedSigned(
        TryIntPtrAdd(BitcastTaggedToWordForTagAndSmiBits(lhs),
                     BitcastTaggedToWordForTagAndSmiBits(rhs), if_overflow));
  } else {
    DCHECK(SmiValuesAre31Bits());
    TNode<PairT<Int32T, BoolT>> pair = Int32AddWithOverflow(
        TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(lhs)),
        TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(rhs)));
    TNode<BoolT> overflow = Projection<1>(pair);
    GotoIf(overflow, if_overflow);
    TNode<Int32T> result = Projection<0>(pair);
    return BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(result));
  }
}

TNode<Smi> CodeStubAssembler::TrySmiSub(TNode<Smi> lhs, TNode<Smi> rhs,
                                        Label* if_overflow) {
  if (SmiValuesAre32Bits()) {
    TNode<PairT<IntPtrT, BoolT>> pair =
        IntPtrSubWithOverflow(BitcastTaggedToWordForTagAndSmiBits(lhs),
                              BitcastTaggedToWordForTagAndSmiBits(rhs));
    TNode<BoolT> overflow = Projection<1>(pair);
    GotoIf(overflow, if_overflow);
    TNode<IntPtrT> result = Projection<0>(pair);
    return BitcastWordToTaggedSigned(result);
  } else {
    DCHECK(SmiValuesAre31Bits());
    TNode<PairT<Int32T, BoolT>> pair = Int32SubWithOverflow(
        TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(lhs)),
        TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(rhs)));
    TNode<BoolT> overflow = Projection<1>(pair);
    GotoIf(overflow, if_overflow);
    TNode<Int32T> result = Projection<0>(pair);
    return BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(result));
  }
}

TNode<Smi> CodeStubAssembler::TrySmiAbs(TNode<Smi> a, Label* if_overflow) {
  if (SmiValuesAre32Bits()) {
    TNode<PairT<IntPtrT, BoolT>> pair =
        IntPtrAbsWithOverflow(BitcastTaggedToWordForTagAndSmiBits(a));
    TNode<BoolT> overflow = Projection<1>(pair);
    GotoIf(overflow, if_overflow);
    TNode<IntPtrT> result = Projection<0>(pair);
    return BitcastWordToTaggedSigned(result);
  } else {
    CHECK(SmiValuesAre31Bits());
    CHECK(IsInt32AbsWithOverflowSupported());
    TNode<PairT<Int32T, BoolT>> pair = Int32AbsWithOverflow(
        TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(a)));
    TNode<BoolT> overflow = Projection<1>(pair);
    GotoIf(overflow, if_overflow);
    TNode<Int32T> result = Projection<0>(pair);
    return BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(result));
  }
}

TNode<Number> CodeStubAssembler::NumberMax(TNode<Number> a, TNode<Number> b) {
  // TODO(danno): This could be optimized by specifically handling smi cases.
  TVARIABLE(Number, result);
  Label done(this), greater_than_equal_a(this), greater_than_equal_b(this);
  GotoIfNumberGreaterThanOrEqual(a, b, &greater_than_equal_a);
  GotoIfNumberGreaterThanOrEqual(b, a, &greater_than_equal_b);
  result = NanConstant();
  Goto(&done);
  BIND(&greater_than_equal_a);
  result = a;
  Goto(&done);
  BIND(&greater_than_equal_b);
  result = b;
  Goto(&done);
  BIND(&done);
  return result.value();
}

TNode<Number> CodeStubAssembler::NumberMin(TNode<Number> a, TNode<Number> b) {
  // TODO(danno): This could be optimized by specifically handling smi cases.
  TVARIABLE(Number, result);
  Label done(this), greater_than_equal_a(this), greater_than_equal_b(this);
  GotoIfNumberGreaterThanOrEqual(a, b, &greater_than_equal_a);
  GotoIfNumberGreaterThanOrEqual(b, a, &greater_than_equal_b);
  result = NanConstant();
  Goto(&done);
  BIND(&greater_than_equal_a);
  result = b;
  Goto(&done);
  BIND(&greater_than_equal_b);
  result = a;
  Goto(&done);
  BIND(&done);
  return result.value();
}

TNode<Number> CodeStubAssembler::SmiMod(TNode<Smi> a, TNode<Smi> b) {
  TVARIABLE(Number, var_result);
  Label return_result(this, &var_result),
      return_minuszero(this, Label::kDeferred),
      return_nan(this, Label::kDeferred);

  // Untag {a} and {b}.
  TNode<Int32T> int_a = SmiToInt32(a);
  TNode<Int32T> int_b = SmiToInt32(b);

  // Return NaN if {b} is zero.
  GotoIf(Word32Equal(int_b, Int32Constant(0)), &return_nan);

  // Check if {a} is non-negative.
  Label if_aisnotnegative(this), if_aisnegative(this, Label::kDeferred);
  Branch(Int32LessThanOrEqual(Int32Constant(0), int_a), &if_aisnotnegative,
         &if_aisnegative);

  BIND(&if_aisnotnegative);
  {
    // Fast case, don't need to check any other edge cases.
    TNode<Int32T> r = Int32Mod(int_a, int_b);
    var_result = SmiFromInt32(r);
    Goto(&return_result);
  }

  BIND(&if_aisnegative);
  {
    if (SmiValuesAre32Bits()) {
      // Check if {a} is kMinInt and {b} is -1 (only relevant if the
      // kMinInt is actually representable as a Smi).
      Label join(this);
      GotoIfNot(Word32Equal(int_a, Int32Constant(kMinInt)), &join);
      GotoIf(Word32Equal(int_b, Int32Constant(-1)), &return_minuszero);
      Goto(&join);
      BIND(&join);
    }

    // Perform the integer modulus operation.
    TNode<Int32T> r = Int32Mod(int_a, int_b);

    // Check if {r} is zero, and if so return -0, because we have to
    // take the sign of the left hand side {a}, which is negative.
    GotoIf(Word32Equal(r, Int32Constant(0)), &return_minuszero);

    // The remainder {r} can be outside the valid Smi range on 32bit
    // architectures, so we cannot just say SmiFromInt32(r) here.
    var_result = ChangeInt32ToTagged(r);
    Goto(&return_result);
  }

  BIND(&return_minuszero);
  var_result = MinusZeroConstant();
  Goto(&return_result);

  BIND(&return_nan);
  var_result = NanConstant();
  Goto(&return_result);

  BIND(&return_result);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::SmiMul(TNode<Smi> a, TNode<Smi> b) {
  TVARIABLE(Number, var_result);
  TVARIABLE(Float64T, var_lhs_float64);
  TVARIABLE(Float64T, var_rhs_float64);
  Label return_result(this, &var_result);

  // Both {a} and {b} are Smis. Convert them to integers and multiply.
  TNode<Int32T> lhs32 = SmiToInt32(a);
  TNode<Int32T> rhs32 = SmiToInt32(b);
  auto pair = Int32MulWithOverflow(lhs32, rhs32);

  TNode<BoolT> overflow = Projection<1>(pair);

  // Check if the multiplication overflowed.
  Label if_overflow(this, Label::kDeferred), if_notoverflow(this);
  Branch(overflow, &if_overflow, &if_notoverflow);
  BIND(&if_notoverflow);
  {
    // If the answer is zero, we may need to return -0.0, depending on the
    // input.
    Label answer_zero(this), answer_not_zero(this);
    TNode<Int32T> answer = Projection<0>(pair);
    TNode<Int32T> zero = Int32Constant(0);
    Branch(Word32Equal(answer, zero), &answer_zero, &answer_not_zero);
    BIND(&answer_not_zero);
    {
      var_result = ChangeInt32ToTagged(answer);
      Goto(&return_result);
    }
    BIND(&answer_zero);
    {
      TNode<Int32T> or_result = Word32Or(lhs32, rhs32);
      Label if_should_be_negative_zero(this), if_should_be_zero(this);
      Branch(Int32LessThan(or_result, zero), &if_should_be_negative_zero,
             &if_should_be_zero);
      BIND(&if_should_be_negative_zero);
      {
        var_result = MinusZeroConstant();
        Goto(&return_result);
      }
      BIND(&if_should_be_zero);
      {
        var_result = SmiConstant(0);
        Goto(&return_result);
      }
    }
  }
  BIND(&if_overflow);
  {
    var_lhs_float64 = SmiToFloat64(a);
    var_rhs_float64 = SmiToFloat64(b);
    TNode<Float64T> value =
        Float64Mul(var_lhs_float64.value(), var_rhs_float64.value());
    var_result = AllocateHeapNumberWithValue(value);
    Goto(&return_result);
  }

  BIND(&return_result);
  return var_result.value();
}

TNode<Smi> CodeStubAssembler::TrySmiDiv(TNode<Smi> dividend, TNode<Smi> divisor,
                                        Label* bailout) {
  // Both {a} and {b} are Smis. Bailout to floating point division if {divisor}
  // is zero.
  GotoIf(TaggedEqual(divisor, SmiConstant(0)), bailout);

  // Do floating point division if {dividend} is zero and {divisor} is
  // negative.
  Label dividend_is_zero(this), dividend_is_not_zero(this);
  Branch(TaggedEqual(dividend, SmiConstant(0)), &dividend_is_zero,
         &dividend_is_not_zero);

  BIND(&dividend_is_zero);
  {
    GotoIf(SmiLessThan(divisor, SmiConstant(0)), bailout);
    Goto(&dividend_is_not_zero);
  }
  BIND(&dividend_is_not_zero);

  TNode<Int32T> untagged_divisor = SmiToInt32(divisor);
  TNode<Int32T> untagged_dividend = SmiToInt32(dividend);

  // Do floating point division if {dividend} is kMinInt (or kMinInt - 1
  // if the Smi size is 31) and {divisor} is -1.
  Label divisor_is_minus_one(this), divisor_is_not_minus_one(this);
  Branch(Word32Equal(untagged_divisor, Int32Constant(-1)),
         &divisor_is_minus_one, &divisor_is_not_minus_one);

  BIND(&divisor_is_minus_one);
  {
    GotoIf(Word32Equal(
               untagged_dividend,
               Int32Constant(kSmiValueSize == 32 ? kMinInt : (kMinInt >> 1))),
           bailout);
    Goto(&divisor_is_not_minus_one);
  }
  BIND(&divisor_is_not_minus_one);

  TNode<Int32T> untagged_result = Int32Div(untagged_dividend, untagged_divisor);
  TNode<Int32T> truncated = Int32Mul(untagged_result, untagged_divisor);

  // Do floating point division if the remainder is not 0.
  GotoIf(Word32NotEqual(untagged_dividend, truncated), bailout);

  return SmiFromInt32(untagged_result);
}

TNode<Smi> CodeStubAssembler::SmiLexicographicCompare(TNode<Smi> x,
                                                      TNode<Smi> y) {
  TNode<ExternalReference> smi_lexicographic_compare =
      ExternalConstant(ExternalReference::smi_lexicographic_compare_function());
  TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());
  return CAST(CallCFunction(smi_lexicographic_compare, MachineType::AnyTagged(),
                            std::make_pair(MachineType::Pointer(), isolate_ptr),
                            std::make_pair(MachineType::AnyTagged(), x),
                            std::make_pair(MachineType::AnyTagged(), y)));
}

TNode<Object> CodeStubAssembler::GetCoverageInfo(
    TNode<SharedFunctionInfo> sfi) {
  TNode<ExternalReference> f =
      ExternalConstant(ExternalReference::debug_get_coverage_info_function());
  TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());
  return CAST(CallCFunction(f, MachineType::AnyTagged(),
                            std::make_pair(MachineType::Pointer(), isolate_ptr),
                            std::make_pair(MachineType::TaggedPointer(), sfi)));
}

TNode<Int32T> CodeStubAssembler::TruncateWordToInt32(TNode<WordT> value) {
  if (Is64()) {
    return TruncateInt64ToInt32(ReinterpretCast<Int64T>(value));
  }
  return ReinterpretCast<Int32T>(value);
}

TNode<Int32T> CodeStubAssembler::TruncateIntPtrToInt32(TNode<IntPtrT> value) {
  if (Is64()) {
    return TruncateInt64ToInt32(ReinterpretCast<Int64T>(value));
  }
  return ReinterpretCast<Int32T>(value);
}

TNode<Word32T> CodeStubAssembler::TruncateWord64ToWord32(TNode<Word64T> value) {
  return TruncateInt64ToInt32(ReinterpretCast<Int64T>(value));
}

TNode<BoolT> CodeStubAssembler::TaggedIsSmi(TNode<MaybeObject> a) {
  static_assert(kSmiTagMask < kMaxUInt32);
  return Word32Equal(
      Word32And(TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(a)),
                Int32Constant(kSmiTagMask)),
      Int32Constant(0));
}

TNode<BoolT> CodeStubAssembler::TaggedIsNotSmi(TNode<MaybeObject> a) {
  return Word32BinaryNot(TaggedIsSmi(a));
}

TNode<BoolT> CodeStubAssembler::TaggedIsPositiveSmi(TNode<Object> a) {
#if defined(V8_HOST_ARCH_32_BIT) || defined(V8_31BIT_SMIS_ON_64BIT_ARCH)
  return Word32Equal(
      Word32And(
          TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(a)),
          Uint32Constant(static_cast<uint32_t>(kSmiTagMask | kSmiSignMask))),
      Int32Constant(0));
#else
  return WordEqual(WordAnd(BitcastTaggedToWordForTagAndSmiBits(a),
                           IntPtrConstant(kSmiTagMask | kSmiSignMask)),
                   IntPtrConstant(0));
#endif
}

TNode<BoolT> CodeStubAssembler::WordIsAligned(TNode<WordT> word,
                                              size_t alignment) {
  DCHECK(base::bits::IsPowerOfTwo(alignment));
  DCHECK_LE(alignment, kMaxUInt32);
  return Word32Equal(
      Int32Constant(0),
      Word32And(TruncateWordToInt32(word),
                Uint32Constant(static_cast<uint32_t>(alignment) - 1)));
}

#if DEBUG
void CodeStubAssembler::Bind(Label* label, AssemblerDebugInfo debug_info) {
  CodeAssembler::Bind(label, debug_info);
}
#endif  // DEBUG

void CodeStubAssembler::Bind(Label* label) { CodeAssembler::Bind(label); }

TNode<Float64T> CodeStubAssembler::LoadDoubleWithHoleCheck(
    TNode<FixedDoubleArray> array, TNode<IntPtrT> index, Label* if_hole) {
  return LoadFixedDoubleArrayElement(array, index, if_hole);
}

void CodeStubAssembler::BranchIfJSReceiver(TNode<Object> object, Label* if_true,
                                           Label* if_false) {
  GotoIf(TaggedIsSmi(object), if_false);
  static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
  Branch(IsJSReceiver(CAST(object)), if_true, if_false);
}

void CodeStubAssembler::GotoIfForceSlowPath(Label* if_true) {
#ifdef V8_ENABLE_FORCE_SLOW_PATH
  bool enable_force_slow_path = true;
#else
  bool enable_force_slow_path = false;
#endif

  Label done(this);
  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  GotoIf(UniqueInt32Constant(!enable_force_slow_path), &done);
  {
    // This optional block is used behind a static check and we rely
    // on the dead code elimination to remove it. We generate builtins this
    // way in order to ensure that builtins PGO profiles are agnostic to
    // V8_ENABLE_FORCE_SLOW_PATH value.
    const TNode<ExternalReference> force_slow_path_addr =
        ExternalConstant(ExternalReference::force_slow_path(isolate()));
    const TNode<Uint8T> force_slow = Load<Uint8T>(force_slow_path_addr);
    Branch(force_slow, if_true, &done);
  }
  BIND(&done);
}

TNode<HeapObject> CodeStubAssembler::AllocateRaw(TNode<IntPtrT> size_in_bytes,
                                                 AllocationFlags flags,
                                                 TNode<RawPtrT> top_address,
                                                 TNode<RawPtrT> limit_address) {
  Label if_out_of_memory(this, Label::kDeferred);

  // TODO(jgruber,jkummerow): Extract the slow paths (= probably everything
  // but bump pointer allocation) into a builtin to save code space. The
  // size_in_bytes check may be moved there as well since a non-smi
  // size_in_bytes probably doesn't fit into the bump pointer region
  // (double-check that).

  intptr_t size_in_bytes_constant;
  bool size_in_bytes_is_constant = false;
  if (TryToIntPtrConstant(size_in_bytes, &size_in_bytes_constant)) {
    size_in_bytes_is_constant = true;
    CHECK(Internals::IsValidSmi(size_in_bytes_constant));
    CHECK_GT(size_in_bytes_constant, 0);
  } else {
    GotoIfNot(IsValidPositiveSmi(size_in_bytes), &if_out_of_memory);
  }

  TNode<RawPtrT> top = Load<RawPtrT>(top_address);
  TNode<RawPtrT> limit = Load<RawPtrT>(limit_address);

  // If there's not enough space, call the runtime.
  TVARIABLE(Object, result);
  Label runtime_call(this, Label::kDeferred), no_runtime_call(this), out(this);

  bool needs_double_alignment = flags & AllocationFlag::kDoubleAlignment;

  Label next(this);
  GotoIf(IsRegularHeapObjectSize(size_in_bytes), &next);

  TNode<Smi> runtime_flags = SmiConstant(
      Smi::FromInt(AllocateDoubleAlignFlag::encode(needs_double_alignment)));
  result = CallRuntime(Runtime::kAllocateInYoungGeneration, NoContextConstant(),
                       SmiTag(size_in_bytes), runtime_flags);
  Goto(&out);

  BIND(&next);

  TVARIABLE(IntPtrT, adjusted_size, size_in_bytes);

  if (needs_double_alignment) {
    Label next(this);
    GotoIfNot(WordAnd(top, IntPtrConstant(kDoubleAlignmentMask)), &next);

    adjusted_size = IntPtrAdd(size_in_bytes, IntPtrConstant(4));
    Goto(&next);

    BIND(&next);
  }

  adjusted_size = AlignToAllocationAlignment(adjusted_size.value());
  TNode<IntPtrT> new_top =
      IntPtrAdd(UncheckedCast<IntPtrT>(top), adjusted_size.value());

  Branch(UintPtrGreaterThanOrEqual(new_top, limit), &runtime_call,
         &no_runtime_call);

  BIND(&runtime_call);
  {
    TNode<Smi> runtime_flags = SmiConstant(
        Smi::FromInt(AllocateDoubleAlignFlag::encode(needs_double_alignment)));
    if (flags & AllocationFlag::kPretenured) {
      result =
          CallRuntime(Runtime::kAllocateInOldGeneration, NoContextConstant(),
                      SmiTag(size_in_bytes), runtime_flags);
    } else {
      result =
          CallRuntime(Runtime::kAllocateInYoungGeneration, NoContextConstant(),
                      SmiTag(size_in_bytes), runtime_flags);
    }
    Goto(&out);
  }

  // When there is enough space, return `top' and bump it up.
  BIND(&no_runtime_call);
  {
    StoreNoWriteBarrier(MachineType::PointerRepresentation(), top_address,
                        new_top);

    TVARIABLE(IntPtrT, address, UncheckedCast<IntPtrT>(top));

    if (needs_double_alignment) {
      Label next(this);
      GotoIf(IntPtrEqual(adjusted_size.value(), size_in_bytes), &next);

      // Store a filler and increase the address by 4.
      StoreNoWriteBarrier(MachineRepresentation::kTagged, top,
                          OnePointerFillerMapConstant());
      address = IntPtrAdd(UncheckedCast<IntPtrT>(top), IntPtrConstant(4));
      Goto(&next);

      BIND(&next);
    }

    result = BitcastWordToTagged(
        IntPtrAdd(address.value(), IntPtrConstant(kHeapObjectTag)));
    Goto(&out);
  }

  if (!size_in_bytes_is_constant) {
    BIND(&if_out_of_memory);
    CallRuntime(Runtime::kFatalProcessOutOfMemoryInAllocateRaw,
                NoContextConstant());
    Unreachable();
  }

  BIND(&out);
  if (v8_flags.sticky_mark_bits && (flags & AllocationFlag::kPretenured)) {
    CSA_DCHECK(this, IsMarked(result.value()));
  }
  return UncheckedCast<HeapObject>(result.value());
}

TNode<HeapObject> CodeStubAssembler::AllocateRawUnaligned(
    TNode<IntPtrT> size_in_bytes, AllocationFlags flags,
    TNode<RawPtrT> top_address, TNode<RawPtrT> limit_address) {
  DCHECK_EQ(flags & AllocationFlag::kDoubleAlignment, 0);
  return AllocateRaw(size_in_bytes, flags, top_address, limit_address);
}

TNode<HeapObject> CodeStubAssembler::AllocateRawDoubleAligned(
    TNode<IntPtrT> size_in_bytes, AllocationFlags flags,
    TNode<RawPtrT> top_address, TNode<RawPtrT> limit_address) {
#if defined(V8_HOST_ARCH_32_BIT)
  return AllocateRaw(size_in_bytes, flags | AllocationFlag::kDoubleAlignment,
                     top_address, limit_address);
#elif defined(V8_HOST_ARCH_64_BIT)
#ifdef V8_COMPRESS_POINTERS
// TODO(ishell, v8:8875): Consider using aligned allocations once the
// allocation alignment inconsistency is fixed. For now we keep using
// unaligned access since both x64 and arm64 architectures (where pointer
// compression is supported) allow unaligned access to doubles and full words.
#endif  // V8_COMPRESS_POINTERS
  // Allocation on 64 bit machine is naturally double aligned
  return AllocateRaw(size_in_bytes, flags & ~AllocationFlag::kDoubleAlignment,
                     top_address, limit_address);
#else
#error Architecture not supported
#endif
}

TNode<HeapObject> CodeStubAssembler::AllocateInNewSpace(
    TNode<IntPtrT> size_in_bytes, AllocationFlags flags) {
  DCHECK(flags == AllocationFlag::kNone ||
         flags == AllocationFlag::kDoubleAlignment);
  CSA_DCHECK(this, IsRegularHeapObjectSize(size_in_bytes));
  return Allocate(size_in_bytes, flags);
}

TNode<HeapObject> CodeStubAssembler::Allocate(TNode<IntPtrT> size_in_bytes,
                                              AllocationFlags flags) {
  Comment("Allocate");
  if (v8_flags.single_generation) flags |= AllocationFlag::kPretenured;
  bool const new_space = !(flags & AllocationFlag::kPretenured);
  if (!(flags & AllocationFlag::kDoubleAlignment)) {
    TNode<HeapObject> heap_object =
        OptimizedAllocate(size_in_bytes, new_space ? AllocationType::kYoung
                                                   : AllocationType::kOld);
    if (v8_flags.sticky_mark_bits && !new_space) {
      CSA_DCHECK(this, IsMarked(heap_object));
    }
    return heap_object;
  }
  TNode<ExternalReference> top_address = ExternalConstant(
      new_space
          ? ExternalReference::new_space_allocation_top_address(isolate())
          : ExternalReference::old_space_allocation_top_address(isolate()));

#ifdef DEBUG
  // New space is optional and if disabled both top and limit return
  // kNullAddress.
  if (ExternalReference::new_space_allocation_top_address(isolate())
          .address() != kNullAddress) {
    Address raw_top_address =
        ExternalReference::new_space_allocation_top_address(isolate())
            .address();
    Address raw_limit_address =
        ExternalReference::new_space_allocation_limit_address(isolate())
            .address();

    CHECK_EQ(kSystemPointerSize, raw_limit_address - raw_top_address);
  }

  DCHECK_EQ(kSystemPointerSize,
            ExternalReference::old_space_allocation_limit_address(isolate())
                    .address() -
                ExternalReference::old_space_allocation_top_address(isolate())
                    .address());
#endif

  TNode<IntPtrT> limit_address =
      IntPtrAdd(ReinterpretCast<IntPtrT>(top_address),
                IntPtrConstant(kSystemPointerSize));

  if (flags & AllocationFlag::kDoubleAlignment) {
    return AllocateRawDoubleAligned(size_in_bytes, flags,
                                    ReinterpretCast<RawPtrT>(top_address),
                                    ReinterpretCast<RawPtrT>(limit_address));
  } else {
    return AllocateRawUnaligned(size_in_bytes, flags,
                                ReinterpretCast<RawPtrT>(top_address),
                                ReinterpretCast<RawPtrT>(limit_address));
  }
}

TNode<HeapObject> CodeStubAssembler::AllocateInNewSpace(int size_in_bytes,
                                                        AllocationFlags flags) {
  CHECK(flags == AllocationFlag::kNone ||
        flags == AllocationFlag::kDoubleAlignment);
  DCHECK_LE(size_in_bytes, kMaxRegularHeapObjectSize);
  return CodeStubAssembler::Allocate(IntPtrConstant(size_in_bytes), flags);
}

TNode<HeapObject> CodeStubAssembler::Allocate(int size_in_bytes,
                                              AllocationFlags flags) {
  return CodeStubAssembler::Allocate(IntPtrConstant(size_in_bytes), flags);
}

TNode<BoolT> CodeStubAssembler::IsRegularHeapObjectSize(TNode<IntPtrT> size) {
  return UintPtrLessThanOrEqual(size,
                                IntPtrConstant(kMaxRegularHeapObjectSize));
}

void CodeStubAssembler::BranchIfToBooleanIsTrue(TNode<Object> value,
                                                Label* if_true,
                                                Label* if_false) {
  Label if_smi(this, Label::kDeferred), if_heapnumber(this, Label::kDeferred),
      if_bigint(this, Label::kDeferred);

  // Check if {value} is a Smi.
  GotoIf(TaggedIsSmi(value), &if_smi);

  TNode<HeapObject> value_heapobject = CAST(value);

#if V8_STATIC_ROOTS_BOOL
  // Check if {object} is a falsey root or the true value.
  // Undefined is the first root, so it's the smallest possible pointer
  // value, which means we don't have to subtract it for the range check.
  ReadOnlyRoots roots(isolate());
  static_assert(StaticReadOnlyRoot::kFirstAllocatedRoot ==
                StaticReadOnlyRoot::kUndefinedValue);
  static_assert(StaticReadOnlyRoot::kUndefinedValue + sizeof(Undefined) ==
                StaticReadOnlyRoot::kNullValue);
  static_assert(StaticReadOnlyRoot::kNullValue + sizeof(Null) ==
                StaticReadOnlyRoot::kempty_string);
  static_assert(StaticReadOnlyRoot::kempty_string +
                    SeqOneByteString::SizeFor(0) ==
                StaticReadOnlyRoot::kFalseValue);
  static_assert(StaticReadOnlyRoot::kFalseValue + sizeof(False) ==
                StaticReadOnlyRoot::kTrueValue);
  TNode<Word32T> object_as_word32 =
      TruncateIntPtrToInt32(BitcastTaggedToWord(value_heapobject));
  TNode<Word32T> true_as_word32 = Int32Constant(StaticReadOnlyRoot::kTrueValue);
  GotoIf(Uint32LessThan(object_as_word32, true_as_word32), if_false);
  GotoIf(Word32Equal(object_as_word32, true_as_word32), if_true);
#else
  // Rule out false {value}.
  GotoIf(TaggedEqual(value, FalseConstant()), if_false);

  // Fast path on true {value}.
  GotoIf(TaggedEqual(value, TrueConstant()), if_true);

  // Check if {value} is the empty string.
  GotoIf(IsEmptyString(value_heapobject), if_false);
#endif

  // The {value} is a HeapObject, load its map.
  TNode<Map> value_map = LoadMap(value_heapobject);

  // Only null, undefined and document.all have the undetectable bit set,
  // so we can return false immediately when that bit is set. With static roots
  // we've already checked for null and undefined, but we need to check the
  // undetectable bit for document.all anyway on the common path and it doesn't
  // help to check the undetectable object protector in builtins since we can't
  // deopt.
  GotoIf(IsUndetectableMap(value_map), if_false);

  // We still need to handle numbers specially, but all other {value}s
  // that make it here yield true.
  GotoIf(IsHeapNumberMap(value_map), &if_heapnumber);
  Branch(IsBigInt(value_heapobject), &if_bigint, if_true);

  BIND(&if_smi);
  {
    // Check if the Smi {value} is a zero.
    Branch(TaggedEqual(value, SmiConstant(0)), if_false, if_true);
  }

  BIND(&if_heapnumber);
  {
    // Load the floating point value of {value}.
    TNode<Float64T> value_value = LoadObjectField<Float64T>(
        value_heapobject, offsetof(HeapNumber, value_));

    // Check if the floating point {value} is neither 0.0, -0.0 nor NaN.
    Branch(Float64LessThan(Float64Constant(0.0), Float64Abs(value_value)),
           if_true, if_false);
  }

  BIND(&if_bigint);
  {
    TNode<BigInt> bigint = CAST(value);
    TNode<Word32T> bitfield = LoadBigIntBitfield(bigint);
    TNode<Uint32T> length = DecodeWord32<BigIntBase::LengthBits>(bitfield);
    Branch(Word32Equal(length, Int32Constant(0)), if_false, if_true);
  }
}

TNode<RawPtrT> CodeStubAssembler::LoadSandboxedPointerFromObject(
    TNode<HeapObject> object, TNode<IntPtrT> field_offset) {
#ifdef V8_ENABLE_SANDBOX
  return ReinterpretCast<RawPtrT>(
      LoadObjectField<SandboxedPtrT>(object, field_offset));
#else
  return LoadObjectField<RawPtrT>(object, field_offset);
#endif  // V8_ENABLE_SANDBOX
}

void CodeStubAssembler::StoreSandboxedPointerToObject(TNode<HeapObject> object,
                                                      TNode<IntPtrT> offset,
                                                      TNode<RawPtrT> pointer) {
#ifdef V8_ENABLE_SANDBOX
  TNode<SandboxedPtrT> sbx_ptr = ReinterpretCast<SandboxedPtrT>(pointer);

  // Ensure pointer points into the sandbox.
  TNode<ExternalReference> sandbox_base_address =
      ExternalConstant(ExternalReference::sandbox_base_address());
  TNode<ExternalReference> sandbox_end_address =
      ExternalConstant(ExternalReference::sandbox_end_address());
  TNode<UintPtrT> sandbox_base = Load<UintPtrT>(sandbox_base_address);
  TNode<UintPtrT> sandbox_end = Load<UintPtrT>(sandbox_end_address);
  CSA_CHECK(this, UintPtrGreaterThanOrEqual(sbx_ptr, sandbox_base));
  CSA_CHECK(this, UintPtrLessThan(sbx_ptr, sandbox_end));

  StoreObjectFieldNoWriteBarrier<SandboxedPtrT>(object, offset, sbx_ptr);
#else
  StoreObjectFieldNoWriteBarrier<RawPtrT>(object, offset, pointer);
#endif  // V8_ENABLE_SANDBOX
}

TNode<RawPtrT> CodeStubAssembler::EmptyBackingStoreBufferConstant() {
#ifdef V8_ENABLE_SANDBOX
  // TODO(chromium:1218005) consider creating a LoadSandboxedPointerConstant()
  // if more of these constants are required later on.
  TNode<ExternalReference> empty_backing_store_buffer =
      ExternalConstant(ExternalReference::empty_backing_store_buffer());
  return Load<RawPtrT>(empty_backing_store_buffer);
#else
  return ReinterpretCast<RawPtrT>(IntPtrConstant(0));
#endif  // V8_ENABLE_SANDBOX
}

TNode<UintPtrT> CodeStubAssembler::LoadBoundedSizeFromObject(
    TNode<HeapObject> object, TNode<IntPtrT> field_offset) {
#ifdef V8_ENABLE_SANDBOX
  TNode<Uint64T> raw_value = LoadObjectField<Uint64T>(object, field_offset);
  TNode<Uint64T> shift_amount = Uint64Constant(kBoundedSizeShift);
  TNode<Uint64T> decoded_value = Word64Shr(raw_value, shift_amount);
  return ReinterpretCast<UintPtrT>(decoded_value);
#else
  return LoadObjectField<UintPtrT>(object, field_offset);
#endif  // V8_ENABLE_SANDBOX
}

void CodeStubAssembler::StoreBoundedSizeToObject(TNode<HeapObject> object,
                                                 TNode<IntPtrT> offset,
                                                 TNode<UintPtrT> value) {
#ifdef V8_ENABLE_SANDBOX
  CSA_DCHECK(this, UintPtrLessThanOrEqual(
                       value, IntPtrConstant(kMaxSafeBufferSizeForSandbox)));
  TNode<Uint64T> raw_value = ReinterpretCast<Uint64T>(value);
  TNode<Uint64T> shift_amount = Uint64Constant(kBoundedSizeShift);
  TNode<Uint64T> encoded_value = Word64Shl(raw_value, shift_amount);
  StoreObjectFieldNoWriteBarrier<Uint64T>(object, offset, encoded_value);
#else
  StoreObjectFieldNoWriteBarrier<UintPtrT>(object, offset, value);
#endif  // V8_ENABLE_SANDBOX
}

#ifdef V8_ENABLE_SANDBOX
TNode<RawPtrT> CodeStubAssembler::ExternalPointerTableAddress(
    ExternalPointerTag tag) {
  if (IsSharedExternalPointerType(tag)) {
    TNode<ExternalReference> table_address_address = ExternalConstant(
        ExternalReference::shared_external_pointer_table_address_address(
            isolate()));
    return UncheckedCast<RawPtrT>(
        Load(MachineType::Pointer(), table_address_address));
  }
  return ExternalConstant(
      ExternalReference::external_pointer_table_address(isolate()));
}
#endif  // V8_ENABLE_SANDBOX

TNode<RawPtrT> CodeStubAssembler::LoadExternalPointerFromObject(
    TNode<HeapObject> object, TNode<IntPtrT> offset, ExternalPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
  DCHECK_NE(tag, kExternalPointerNullTag);
  TNode<RawPtrT> external_pointer_table_address =
      ExternalPointerTableAddress(tag);
  TNode<RawPtrT> table = UncheckedCast<RawPtrT>(
      Load(MachineType::Pointer(), external_pointer_table_address,
           UintPtrConstant(Internals::kExternalPointerTableBasePointerOffset)));

  TNode<ExternalPointerHandleT> handle =
      LoadObjectField<ExternalPointerHandleT>(object, offset);

  // Use UniqueUint32Constant instead of Uint32Constant here in order to ensure
  // that the graph structure does not depend on the configuration-specific
  // constant value (Uint32Constant uses cached nodes).
  TNode<Uint32T> index =
      Word32Shr(handle, UniqueUint32Constant(kExternalPointerIndexShift));
  TNode<IntPtrT> table_offset = ElementOffsetFromIndex(
      ChangeUint32ToWord(index), SYSTEM_POINTER_ELEMENTS, 0);

  TNode<UintPtrT> entry = Load<UintPtrT>(table, table_offset);
  entry = UncheckedCast<UintPtrT>(WordAnd(entry, UintPtrConstant(~tag)));
  return UncheckedCast<RawPtrT>(UncheckedCast<WordT>(entry));
#else
  return LoadObjectField<RawPtrT>(object, offset);
#endif  // V8_ENABLE_SANDBOX
}

void CodeStubAssembler::StoreExternalPointerToObject(TNode<HeapObject> object,
                                                     TNode<IntPtrT> offset,
                                                     TNode<RawPtrT> pointer,
                                                     ExternalPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
  DCHECK_NE(tag, kExternalPointerNullTag);
  TNode<RawPtrT> external_pointer_table_address =
      ExternalPointerTableAddress(tag);
  TNode<RawPtrT> table = UncheckedCast<RawPtrT>(
      Load(MachineType::Pointer(), external_pointer_table_address,
           UintPtrConstant(Internals::kExternalPointerTableBasePointerOffset)));
  TNode<ExternalPointerHandleT> handle =
      LoadObjectField<ExternalPointerHandleT>(object, offset);

  // Use UniqueUint32Constant instead of Uint32Constant here in order to ensure
  // that the graph structure does not depend on the configuration-specific
  // constant value (Uint32Constant uses cached nodes).
  TNode<Uint32T> index =
      Word32Shr(handle, UniqueUint32Constant(kExternalPointerIndexShift));
  TNode<IntPtrT> table_offset = ElementOffsetFromIndex(
      ChangeUint32ToWord(index), SYSTEM_POINTER_ELEMENTS, 0);

  TNode<UintPtrT> value = UncheckedCast<UintPtrT>(pointer);
  value = UncheckedCast<UintPtrT>(WordOr(pointer, UintPtrConstant(tag)));
  StoreNoWriteBarrier(MachineType::PointerRepresentation(), table, table_offset,
                      value);
#else
  StoreObjectFieldNoWriteBarrier<RawPtrT>(object, offset, pointer);
#endif  // V8_ENABLE_SANDBOX
}

TNode<TrustedObject> CodeStubAssembler::LoadTrustedPointerFromObject(
    TNode<HeapObject> object, int field_offset, IndirectPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
  return LoadIndirectPointerFromObject(object, field_offset, tag);
#else
  return LoadObjectField<TrustedObject>(object, field_offset);
#endif  // V8_ENABLE_SANDBOX
}

TNode<Code> CodeStubAssembler::LoadCodePointerFromObject(
    TNode<HeapObject> object, int field_offset) {
  return UncheckedCast<Code>(LoadTrustedPointerFromObject(
      object, field_offset, kCodeIndirectPointerTag));
}

#ifdef V8_ENABLE_SANDBOX
TNode<TrustedObject> CodeStubAssembler::LoadIndirectPointerFromObject(
    TNode<HeapObject> object, int field_offset, IndirectPointerTag tag) {
  TNode<IndirectPointerHandleT> handle =
      LoadObjectField<IndirectPointerHandleT>(object, field_offset);
  return ResolveIndirectPointerHandle(handle, tag);
}

TNode<BoolT> CodeStubAssembler::IsTrustedPointerHandle(
    TNode<IndirectPointerHandleT> handle) {
  return Word32Equal(Word32And(handle, Int32Constant(kCodePointerHandleMarker)),
                     Int32Constant(0));
}

TNode<TrustedObject> CodeStubAssembler::ResolveIndirectPointerHandle(
    TNode<IndirectPointerHandleT> handle, IndirectPointerTag tag) {
  // The tag implies which pointer table to use.
  if (tag == kUnknownIndirectPointerTag) {
    // In this case we have to rely on the handle marking to determine which
    // pointer table to use.
    return Select<TrustedObject>(
        IsTrustedPointerHandle(handle),
        [=, this] { return ResolveTrustedPointerHandle(handle, tag); },
        [=, this] { return ResolveCodePointerHandle(handle); });
  } else if (tag == kCodeIndirectPointerTag) {
    return ResolveCodePointerHandle(handle);
  } else {
    return ResolveTrustedPointerHandle(handle, tag);
  }
}

#ifdef V8_ENABLE_LEAPTIERING
TNode<Code> CodeStubAssembler::ResolveJSDispatchHandle(
    TNode<JSDispatchHandleT> handle) {
  TNode<RawPtrT> table =
      ExternalConstant(ExternalReference::js_dispatch_table_address());
  TNode<UintPtrT> offset = ComputeJSDispatchTableEntryOffset(handle);
  offset = UintPtrAdd(offset,
                      UintPtrConstant(JSDispatchTable::kEntryCodeObjectOffset));
  TNode<UintPtrT> value = Load<UintPtrT>(table, offset);
  // The LSB is used as marking bit by the js dispatch table, so here we have
  // to set it using a bitwise OR as it may or may not be set.
  value = UncheckedCast<UintPtrT>(WordOr(
      WordShr(value, UintPtrConstant(JSDispatchEntry::kObjectPointerShift)),
      UintPtrConstant(kHeapObjectTag)));
  return CAST(BitcastWordToTagged(value));
}
#endif  // V8_ENABLE_LEAPTIERING

TNode<Code> CodeStubAssembler::ResolveCodePointerHandle(
    TNode<IndirectPointerHandleT> handle) {
  TNode<RawPtrT> table =
      ExternalConstant(ExternalReference::code_pointer_table_address());
  TNode<UintPtrT> offset = ComputeCodePointerTableEntryOffset(handle);
  offset = UintPtrAdd(offset,
                      UintPtrConstant(kCodePointerTableEntryCodeObjectOffset));
  TNode<UintPtrT> value = Load<UintPtrT>(table, offset);
  // The LSB is used as marking bit by the code pointer table, so here we have
  // to set it using a bitwise OR as it may or may not be set.
  value =
      UncheckedCast<UintPtrT>(WordOr(value, UintPtrConstant(kHeapObjectTag)));
  return CAST(BitcastWordToTagged(value));
}

TNode<TrustedObject> CodeStubAssembler::ResolveTrustedPointerHandle(
    TNode<IndirectPointerHandleT> handle, IndirectPointerTag tag) {
  TNode<RawPtrT> table = ExternalConstant(
      ExternalReference::trusted_pointer_table_base_address(isolate()));
  TNode<Uint32T> index =
      Word32Shr(handle, Uint32Constant(kTrustedPointerHandleShift));
  // We're using a 32-bit shift here to reduce code size, but for that we need
  // to be sure that the offset will always fit into a 32-bit integer.
  static_assert(kTrustedPointerTableReservationSize <= 4ULL * GB);
  TNode<UintPtrT> offset = ChangeUint32ToWord(
      Word32Shl(index, Uint32Constant(kTrustedPointerTableEntrySizeLog2)));
  TNode<UintPtrT> value = Load<UintPtrT>(table, offset);
  // Untag the pointer and remove the marking bit in one operation.
  value = UncheckedCast<UintPtrT>(
      WordAnd(value, UintPtrConstant(~(tag | kTrustedPointerTableMarkBit))));
  return CAST(BitcastWordToTagged(value));
}

TNode<UintPtrT> CodeStubAssembler::ComputeJSDispatchTableEntryOffset(
    TNode<JSDispatchHandleT> handle) {
  TNode<Uint32T> index =
      Word32Shr(handle, Uint32Constant(kJSDispatchHandleShift));
  // We're using a 32-bit shift here to reduce code size, but for that we need
  // to be sure that the offset will always fit into a 32-bit integer.
  static_assert(kJSDispatchTableReservationSize <= 4ULL * GB);
  TNode<UintPtrT> offset = ChangeUint32ToWord(
      Word32Shl(index, Uint32Constant(kJSDispatchTableEntrySizeLog2)));
  return offset;
}

TNode<UintPtrT> CodeStubAssembler::ComputeCodePointerTableEntryOffset(
    TNode<IndirectPointerHandleT> handle) {
  TNode<Uint32T> index =
      Word32Shr(handle, Uint32Constant(kCodePointerHandleShift));
  // We're using a 32-bit shift here to reduce code size, but for that we need
  // to be sure that the offset will always fit into a 32-bit integer.
  static_assert(kCodePointerTableReservationSize <= 4ULL * GB);
  TNode<UintPtrT> offset = ChangeUint32ToWord(
      Word32Shl(index, Uint32Constant(kCodePointerTableEntrySizeLog2)));
  return offset;
}

TNode<RawPtrT> CodeStubAssembler::LoadCodeEntrypointViaCodePointerField(
    TNode<HeapObject> object, TNode<IntPtrT> field_offset,
    CodeEntrypointTag tag) {
  TNode<IndirectPointerHandleT> handle =
      LoadObjectField<IndirectPointerHandleT>(object, field_offset);
  return LoadCodeEntryFromIndirectPointerHandle(handle, tag);
}

TNode<RawPtrT> CodeStubAssembler::LoadCodeEntryFromIndirectPointerHandle(
    TNode<IndirectPointerHandleT> handle, CodeEntrypointTag tag) {
  TNode<RawPtrT> table =
      ExternalConstant(ExternalReference::code_pointer_table_address());
  TNode<UintPtrT> offset = ComputeCodePointerTableEntryOffset(handle);
  TNode<UintPtrT> entry = Load<UintPtrT>(table, offset);
  if (tag != 0) {
    entry = UncheckedCast<UintPtrT>(WordXor(entry, UintPtrConstant(tag)));
  }
  return UncheckedCast<RawPtrT>(UncheckedCast<WordT>(entry));
}

#endif  // V8_ENABLE_SANDBOX

TNode<Object> CodeStubAssembler::LoadFromParentFrame(int offset) {
  TNode<RawPtrT> frame_pointer = LoadParentFramePointer();
  return LoadFullTagged(frame_pointer, IntPtrConstant(offset));
}

TNode<Uint8T> CodeStubAssembler::LoadUint8Ptr(TNode<RawPtrT> ptr,
                                              TNode<IntPtrT> offset) {
  return Load<Uint8T>(IntPtrAdd(ReinterpretCast<IntPtrT>(ptr), offset));
}

TNode<Uint64T> CodeStubAssembler::LoadUint64Ptr(TNode<RawPtrT> ptr,
                                                TNode<IntPtrT> index) {
  return Load<Uint64T>(
      IntPtrAdd(ReinterpretCast<IntPtrT>(ptr),
                IntPtrMul(index, IntPtrConstant(sizeof(uint64_t)))));
}

TNode<IntPtrT> CodeStubAssembler::LoadAndUntagPositiveSmiObjectField(
    TNode<HeapObject> object, int offset) {
  TNode<Int32T> value = LoadAndUntagToWord32ObjectField(object, offset);
  CSA_DCHECK(this, Int32GreaterThanOrEqual(value, Int32Constant(0)));
  return Signed(ChangeUint32ToWord(value));
}

TNode<Int32T> CodeStubAssembler::LoadAndUntagToWord32ObjectField(
    TNode<HeapObject> object, int offset) {
  // Please use LoadMap(object) instead.
  DCHECK_NE(offset, HeapObject::kMapOffset);
  if (SmiValuesAre32Bits()) {
#if V8_TARGET_LITTLE_ENDIAN
    offset += 4;
#endif
    return LoadObjectField<Int32T>(object, offset);
  } else {
    return SmiToInt32(LoadObjectField<Smi>(object, offset));
  }
}

TNode<Float64T> CodeStubAssembler::LoadHeapNumberValue(
    TNode<HeapObject> object) {
  CSA_DCHECK(this, Word32Or(IsHeapNumber(object), IsTheHole(object)));
  static_assert(offsetof(HeapNumber, value_) == Hole::kRawNumericValueOffset);
  return LoadObjectField<Float64T>(object, offsetof(HeapNumber, value_));
}

TNode<Map> CodeStubAssembler::GetInstanceTypeMap(InstanceType instance_type) {
  RootIndex map_idx = Map::TryGetMapRootIdxFor(instance_type).value();
  return HeapConstantNoHole(
      i::Cast<Map>(ReadOnlyRoots(isolate()).handle_at(map_idx)));
}

TNode<Map> CodeStubAssembler::LoadMap(TNode<HeapObject> object) {
  TNode<Map> map = LoadObjectField<Map>(object, HeapObject::kMapOffset);
#ifdef V8_MAP_PACKING
  // Check the loaded map is unpacked. i.e. the lowest two bits != 0b10
  CSA_DCHECK(this,
             WordNotEqual(WordAnd(BitcastTaggedToWord(map),
                                  IntPtrConstant(Internals::kMapWordXorMask)),
                          IntPtrConstant(Internals::kMapWordSignature)));
#endif
  return map;
}

TNode<Uint16T> CodeStubAssembler::LoadInstanceType(TNode<HeapObject> object) {
  return LoadMapInstanceType(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::HasInstanceType(TNode<HeapObject> object,
                                                InstanceType instance_type) {
  if (V8_STATIC_ROOTS_BOOL) {
    if (std::optional<RootIndex> expected_map =
            InstanceTypeChecker::UniqueMapOfInstanceType(instance_type)) {
      TNode<Map> map = LoadMap(object);
      return TaggedEqual(map, LoadRoot(*expected_map));
    }
  }
  return InstanceTypeEqual(LoadInstanceType(object), instance_type);
}

TNode<BoolT> CodeStubAssembler::DoesntHaveInstanceType(
    TNode<HeapObject> object, InstanceType instance_type) {
  if (V8_STATIC_ROOTS_BOOL) {
    if (std::optional<RootIndex> expected_map =
            InstanceTypeChecker::UniqueMapOfInstanceType(instance_type)) {
      TNode<Map> map = LoadMap(object);
      return TaggedNotEqual(map, LoadRoot(*expected_map));
    }
  }
  return Word32NotEqual(LoadInstanceType(object), Int32Constant(instance_type));
}

TNode<BoolT> CodeStubAssembler::TaggedDoesntHaveInstanceType(
    TNode<HeapObject> any_tagged, InstanceType type) {
  /* return Phi <TaggedIsSmi(val), DoesntHaveInstanceType(val, type)> */
  TNode<BoolT> tagged_is_smi = TaggedIsSmi(any_tagged);
  return Select<BoolT>(
      tagged_is_smi, [=]() { return tagged_is_smi; },
      [=, this]() { return DoesntHaveInstanceType(any_tagged, type); });
}

TNode<BoolT> CodeStubAssembler::IsSpecialReceiverMap(TNode<Map> map) {
  TNode<BoolT> is_special =
      IsSpecialReceiverInstanceType(LoadMapInstanceType(map));
  uint32_t mask = Map::Bits1::HasNamedInterceptorBit::kMask |
                  Map::Bits1::IsAccessCheckNeededBit::kMask;
  USE(mask);
  // Interceptors or access checks imply special receiver.
  CSA_DCHECK(this,
             SelectConstant<BoolT>(IsSetWord32(LoadMapBitField(map), mask),
                                   is_special, Int32TrueConstant()));
  return is_special;
}

TNode<Word32T> CodeStubAssembler::IsStringWrapperElementsKind(TNode<Map> map) {
  TNode<Int32T> kind = LoadMapElementsKind(map);
  return Word32Or(
      Word32Equal(kind, Int32Constant(FAST_STRING_WRAPPER_ELEMENTS)),
      Word32Equal(kind, Int32Constant(SLOW_STRING_WRAPPER_ELEMENTS)));
}

void CodeStubAssembler::GotoIfMapHasSlowProperties(TNode<Map> map,
                                                   Label* if_slow) {
  GotoIf(IsStringWrapperElementsKind(map), if_slow);
  GotoIf(IsSpecialReceiverMap(map), if_slow);
  GotoIf(IsDictionaryMap(map), if_slow);
}

TNode<HeapObject> CodeStubAssembler::LoadFastProperties(
    TNode<JSReceiver> object, bool skip_empty_check) {
  CSA_SLOW_DCHECK(this, Word32BinaryNot(IsDictionaryMap(LoadMap(object))));
  TNode<Object> properties = LoadJSReceiverPropertiesOrHash(object);
  if (skip_empty_check) {
    return CAST(properties);
  } else {
    // TODO(ishell): use empty_property_array instead of empty_fixed_array here.
    return Select<HeapObject>(
        TaggedIsSmi(properties),
        [=, this] { return EmptyFixedArrayConstant(); },
        [=, this] { return CAST(properties); });
  }
}

TNode<HeapObject> CodeStubAssembler::LoadSlowProperties(
    TNode<JSReceiver> object) {
  CSA_SLOW_DCHECK(this, IsDictionaryMap(LoadMap(object)));
  TNode<Object> properties = LoadJSReceiverPropertiesOrHash(object);
  NodeGenerator<HeapObject> make_empty = [=, this]() -> TNode<HeapObject> {
    if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
      return EmptySwissPropertyDictionaryConstant();
    } else {
      return EmptyPropertyDictionaryConstant();
    }
  };
  NodeGenerator<HeapObject> cast_properties = [=, this] {
    TNode<HeapObject> dict = CAST(properties);
    CSA_DCHECK(this,
               Word32Or(IsPropertyDictionary(dict), IsGlobalDictionary(dict)));
    return dict;
  };
  return Select<HeapObject>(TaggedIsSmi(properties), make_empty,
                            cast_properties);
}

TNode<Object> CodeStubAssembler::LoadJSArgumentsObjectLength(
    TNode<Context> context, TNode<JSArgumentsObject> array) {
  CSA_DCHECK(this, IsJSArgumentsObjectWithLength(context, array));
  constexpr int offset = JSStrictArgumentsObject::kLengthOffset;
  static_assert(offset == JSSloppyArgumentsObject::kLengthOffset);
  return LoadObjectField(array, offset);
}

TNode<Smi> CodeStubAssembler::LoadFastJSArrayLength(TNode<JSArray> array) {
  TNode<Number> length = LoadJSArrayLength(array);
  CSA_DCHECK(this, Word32Or(IsFastElementsKind(LoadElementsKind(array)),
                            IsElementsKindInRange(
                                LoadElementsKind(array),
                                FIRST_ANY_NONEXTENSIBLE_ELEMENTS_KIND,
                                LAST_ANY_NONEXTENSIBLE_ELEMENTS_KIND)));
  // JSArray length is always a positive Smi for fast arrays.
  CSA_SLOW_DCHECK(this, TaggedIsPositiveSmi(length));
  return CAST(length);
}

TNode<Smi> CodeStubAssembler::LoadFixedArrayBaseLength(
    TNode<FixedArrayBase> array) {
  CSA_SLOW_DCHECK(this, IsNotWeakFixedArraySubclass(array));
  return LoadObjectField<Smi>(array, FixedArrayBase::kLengthOffset);
}

TNode<IntPtrT> CodeStubAssembler::LoadAndUntagFixedArrayBaseLength(
    TNode<FixedArrayBase> array) {
  return LoadAndUntagPositiveSmiObjectField(array,
                                            FixedArrayBase::kLengthOffset);
}

TNode<Uint32T> CodeStubAssembler::LoadAndUntagFixedArrayBaseLengthAsUint32(
    TNode<FixedArrayBase> array) {
  TNode<Int32T> value =
      LoadAndUntagToWord32ObjectField(array, FixedArrayBase::kLengthOffset);
  CSA_DCHECK(this, Int32GreaterThanOrEqual(value, Int32Constant(0)));
  return Unsigned(value);
}

TNode<IntPtrT> CodeStubAssembler::LoadFeedbackVectorLength(
    TNode<FeedbackVector> vector) {
  TNode<Int32T> length =
      LoadObjectField<Int32T>(vector, FeedbackVector::kLengthOffset);
  return ChangePositiveInt32ToIntPtr(length);
}

TNode<Smi> CodeStubAssembler::LoadWeakFixedArrayLength(
    TNode<WeakFixedArray> array) {
  return LoadObjectField<Smi>(array, WeakFixedArray::kLengthOffset);
}

TNode<IntPtrT> CodeStubAssembler::LoadAndUntagWeakFixedArrayLength(
    TNode<WeakFixedArray> array) {
  return LoadAndUntagPositiveSmiObjectField(array,
                                            WeakFixedArray::kLengthOffset);
}

TNode<Uint32T> CodeStubAssembler::LoadAndUntagWeakFixedArrayLengthAsUint32(
    TNode<WeakFixedArray> array) {
  TNode<Int32T> length =
      LoadAndUntagToWord32ObjectField(array, WeakFixedArray::kLengthOffset);
  CSA_DCHECK(this, Int32GreaterThanOrEqual(length, Int32Constant(0)));
  return Unsigned(length);
}

TNode<Uint32T> CodeStubAssembler::LoadAndUntagBytecodeArrayLength(
    TNode<BytecodeArray> array) {
  TNode<Int32T> value =
      LoadAndUntagToWord32ObjectField(array, BytecodeArray::kLengthOffset);
  CSA_DCHECK(this, Int32GreaterThanOrEqual(value, Int32Constant(0)));
  return Unsigned(value);
}

TNode<Int32T> CodeStubAssembler::LoadNumberOfDescriptors(
    TNode<DescriptorArray> array) {
  return UncheckedCast<Int32T>(LoadObjectField<Int16T>(
      array, DescriptorArray::kNumberOfDescriptorsOffset));
}

TNode<Int32T> CodeStubAssembler::LoadNumberOfOwnDescriptors(TNode<Map> map) {
  TNode<Uint32T> bit_field3 = LoadMapBitField3(map);
  return UncheckedCast<Int32T>(
      DecodeWord32<Map::Bits3::NumberOfOwnDescriptorsBits>(bit_field3));
}

TNode<Int32T> CodeStubAssembler::LoadMapBitField(TNode<Map> map) {
  return UncheckedCast<Int32T>(
      LoadObjectField<Uint8T>(map, Map::kBitFieldOffset));
}

TNode<Int32T> CodeStubAssembler::LoadMapBitField2(TNode<Map> map) {
  return UncheckedCast<Int32T>(
      LoadObjectField<Uint8T>(map, Map::kBitField2Offset));
}

TNode<Uint32T> CodeStubAssembler::LoadMapBitField3(TNode<Map> map) {
  return LoadObjectField<Uint32T>(map, Map::kBitField3Offset);
}

TNode<Uint16T> CodeStubAssembler::LoadMapInstanceType(TNode<Map> map) {
  return LoadObjectField<Uint16T>(map, Map::kInstanceTypeOffset);
}

TNode<Int32T> CodeStubAssembler::LoadMapElementsKind(TNode<Map> map) {
  TNode<Int32T> bit_field2 = LoadMapBitField2(map);
  return Signed(DecodeWord32<Map::Bits2::ElementsKindBits>(bit_field2));
}

TNode<Int32T> CodeStubAssembler::LoadElementsKind(TNode<HeapObject> object) {
  return LoadMapElementsKind(LoadMap(object));
}

TNode<DescriptorArray> CodeStubAssembler::LoadMapDescriptors(TNode<Map> map) {
  return LoadObjectField<DescriptorArray>(map, Map::kInstanceDescriptorsOffset);
}

TNode<HeapObject> CodeStubAssembler::LoadMapPrototype(TNode<Map> map) {
  return LoadObjectField<HeapObject>(map, Map::kPrototypeOffset);
}

TNode<IntPtrT> CodeStubAssembler::LoadMapInstanceSizeInWords(TNode<Map> map) {
  return ChangeInt32ToIntPtr(
      LoadObjectField<Uint8T>(map, Map::kInstanceSizeInWordsOffset));
}

TNode<IntPtrT> CodeStubAssembler::LoadMapInobjectPropertiesStartInWords(
    TNode<Map> map) {
  // See Map::GetInObjectPropertiesStartInWords() for details.
  CSA_DCHECK(this, IsJSObjectMap(map));
  return ChangeInt32ToIntPtr(LoadObjectField<Uint8T>(
      map, Map::kInobjectPropertiesStartOrConstructorFunctionIndexOffset));
}

TNode<IntPtrT> CodeStubAssembler::MapUsedInstanceSizeInWords(TNode<Map> map) {
  TNode<IntPtrT> used_or_unused =
      ChangeInt32ToIntPtr(LoadMapUsedOrUnusedInstanceSizeInWords(map));

  return Select<IntPtrT>(
      UintPtrGreaterThanOrEqual(used_or_unused,
                                IntPtrConstant(JSObject::kFieldsAdded)),
      [=] { return used_or_unused; },
      [=, this] { return LoadMapInstanceSizeInWords(map); });
}

TNode<IntPtrT> CodeStubAssembler::MapUsedInObjectProperties(TNode<Map> map) {
  return IntPtrSub(MapUsedInstanceSizeInWords(map),
                   LoadMapInobjectPropertiesStartInWords(map));
}

TNode<IntPtrT> CodeStubAssembler::LoadMapConstructorFunctionIndex(
    TNode<Map> map) {
  // See Map::GetConstructorFunctionIndex() for details.
  CSA_DCHECK(this, IsPrimitiveInstanceType(LoadMapInstanceType(map)));
  return ChangeInt32ToIntPtr(LoadObjectField<Uint8T>(
      map, Map::kInobjectPropertiesStartOrConstructorFunctionIndexOffset));
}

TNode<Object> CodeStubAssembler::LoadMapConstructor(TNode<Map> map) {
  TVARIABLE(Object, result,
            LoadObjectField(
                map, Map::kConstructorOrBackPointerOrNativeContextOffset));

  Label done(this), loop(this, &result);
  Goto(&loop);
  BIND(&loop);
  {
    GotoIf(TaggedIsSmi(result.value()), &done);
    TNode<BoolT> is_map_type =
        InstanceTypeEqual(LoadInstanceType(CAST(result.value())), MAP_TYPE);
    GotoIfNot(is_map_type, &done);
    result =
        LoadObjectField(CAST(result.value()),
                        Map::kConstructorOrBackPointerOrNativeContextOffset);
    Goto(&loop);
  }
  BIND(&done);
  return result.value();
}

TNode<Uint32T> CodeStubAssembler::LoadMapEnumLength(TNode<Map> map) {
  TNode<Uint32T> bit_field3 = LoadMapBitField3(map);
  return DecodeWord32<Map::Bits3::EnumLengthBits>(bit_field3);
}

TNode<Object> CodeStubAssembler::LoadMapBackPointer(TNode<Map> map) {
  TNode<HeapObject> object = CAST(LoadObjectField(
      map, Map::kConstructorOrBackPointerOrNativeContextOffset));
  return Select<Object>(
      IsMap(object), [=] { return object; },
      [=, this] { return UndefinedConstant(); });
}

TNode<Uint32T> CodeStubAssembler::EnsureOnlyHasSimpleProperties(
    TNode<Map> map, TNode<Int32T> instance_type, Label* bailout) {
  // This check can have false positives, since it applies to any
  // JSPrimitiveWrapper type.
  GotoIf(IsCustomElementsReceiverInstanceType(instance_type), bailout);

  TNode<Uint32T> bit_field3 = LoadMapBitField3(map);
  GotoIf(IsSetWord32(bit_field3, Map::Bits3::IsDictionaryMapBit::kMask),
         bailout);

  return bit_field3;
}

TNode<Uint32T> CodeStubAssembler::LoadJSReceiverIdentityHash(
    TNode<JSReceiver> receiver, Label* if_no_hash) {
  TVARIABLE(Uint32T, var_hash);
  Label done(this), if_smi(this), if_property_array(this),
      if_swiss_property_dictionary(this), if_property_dictionary(this),
      if_fixed_array(this);

  TNode<Object> properties_or_hash =
      LoadObjectField(receiver, JSReceiver::kPropertiesOrHashOffset);
  GotoIf(TaggedIsSmi(properties_or_hash), &if_smi);

  TNode<HeapObject> properties = CAST(properties_or_hash);
  TNode<Uint16T> properties_instance_type = LoadInstanceType(properties);

  GotoIf(InstanceTypeEqual(properties_instance_type, PROPERTY_ARRAY_TYPE),
         &if_property_array);
  if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    GotoIf(
        InstanceTypeEqual(properties_instance_type, SWISS_NAME_DICTIONARY_TYPE),
        &if_swiss_property_dictionary);
  }
  Branch(InstanceTypeEqual(properties_instance_type, NAME_DICTIONARY_TYPE),
         &if_property_dictionary, &if_fixed_array);

  BIND(&if_fixed_array);
  {
    var_hash = Uint32Constant(PropertyArray::kNoHashSentinel);
    Goto(&done);
  }

  BIND(&if_smi);
  {
    var_hash = PositiveSmiToUint32(CAST(properties_or_hash));
    Goto(&done);
  }

  BIND(&if_property_array);
  {
    TNode<Int32T> length_and_hash = LoadAndUntagToWord32ObjectField(
        properties, PropertyArray::kLengthAndHashOffset);
    var_hash = DecodeWord32<PropertyArray::HashField>(length_and_hash);
    Goto(&done);
  }
  if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    BIND(&if_swiss_property_dictionary);
    {
      var_hash = LoadSwissNameDictionaryHash(CAST(properties));
      CSA_DCHECK(this, Uint32LessThanOrEqual(var_hash.value(),
                                             Uint32Constant(Smi::kMaxValue)));
      Goto(&done);
    }
  }

  BIND(&if_property_dictionary);
  {
    var_hash = PositiveSmiToUint32(CAST(LoadFixedArrayElement(
        CAST(properties), NameDictionary::kObjectHashIndex)));
    Goto(&done);
  }

  BIND(&done);
  if (if_no_hash != nullptr) {
    GotoIf(Word32Equal(var_hash.value(),
                       Uint32Constant(PropertyArray::kNoHashSentinel)),
           if_no_hash);
  }
  return var_hash.value();
}

TNode<Uint32T> CodeStubAssembler::LoadNameHashAssumeComputed(TNode<Name> name) {
  TNode<Uint32T> hash_field = LoadNameRawHash(name);
  CSA_DCHECK(this, IsClearWord32(hash_field, Name::kHashNotComputedMask));
  return DecodeWord32<Name::HashBits>(hash_field);
}

TNode<Uint32T> CodeStubAssembler::LoadNameHash(TNode<Name> name,
                                               Label* if_hash_not_computed) {
  TNode<Uint32T> raw_hash_field = LoadNameRawHashField(name);
  if (if_hash_not_computed != nullptr) {
    GotoIf(IsSetWord32(raw_hash_field, Name::kHashNotComputedMask),
           if_hash_not_computed);
  }
  return DecodeWord32<Name::HashBits>(raw_hash_field);
}

TNode<Uint32T> CodeStubAssembler::LoadNameRawHash(TNode<Name> name) {
  TVARIABLE(Uint32T, var_raw_hash);

  Label if_forwarding_index(this, Label::kDeferred), done(this);

  TNode<Uint32T> raw_hash_field = LoadNameRawHashField(name);
  GotoIf(IsSetWord32(raw_hash_field, Name::kHashNotComputedMask),
         &if_forwarding_index);

  var_raw_hash = raw_hash_field;
  Goto(&done);

  BIND(&if_forwarding_index);
  {
    CSA_DCHECK(this,
               IsEqualInWord32<Name::HashFieldTypeBits>(
                   raw_hash_field, Name::HashFieldType::kForwardingIndex));
    TNode<ExternalReference> function =
        ExternalConstant(ExternalReference::raw_hash_from_forward_table());
    const TNode<ExternalReference> isolate_ptr =
        ExternalConstant(ExternalReference::isolate_address());
    TNode<Uint32T> result = UncheckedCast<Uint32T>(CallCFunction(
        function, MachineType::Uint32(),
        std::make_pair(MachineType::Pointer(), isolate_ptr),
        std::make_pair(
            MachineType::Int32(),
            DecodeWord32<Name::ForwardingIndexValueBits>(raw_hash_field))));

    var_raw_hash = result;
    Goto(&done);
  }

  BIND(&done);
  return var_raw_hash.value();
}

TNode<Smi> CodeStubAssembler::LoadStringLengthAsSmi(TNode<String> string) {
  return SmiFromIntPtr(LoadStringLengthAsWord(string));
}

TNode<IntPtrT> CodeStubAssembler::LoadStringLengthAsWord(TNode<String> string) {
  return Signed(ChangeUint32ToWord(LoadStringLengthAsWord32(string)));
}

TNode<Uint32T> CodeStubAssembler::LoadStringLengthAsWord32(
    TNode<String> string) {
  return LoadObjectField<Uint32T>(string, offsetof(String, length_));
}

TNode<Object> CodeStubAssembler::LoadJSPrimitiveWrapperValue(
    TNode<JSPrimitiveWrapper> object) {
  return LoadObjectField(object, JSPrimitiveWrapper::kValueOffset);
}

void CodeStubAssembler::DispatchMaybeObject(TNode<MaybeObject> maybe_object,
                                            Label* if_smi, Label* if_cleared,
                                            Label* if_weak, Label* if_strong,
                                            TVariable<Object>* extracted) {
  Label inner_if_smi(this), inner_if_strong(this);

  GotoIf(TaggedIsSmi(maybe_object), &inner_if_smi);

  GotoIf(IsCleared(maybe_object), if_cleared);

  TNode<HeapObjectReference> object_ref = CAST(maybe_object);

  GotoIf(IsStrong(object_ref), &inner_if_strong);

  *extracted = GetHeapObjectAssumeWeak(maybe_object);
  Goto(if_weak);

  BIND(&inner_if_smi);
  *extracted = CAST(maybe_object);
  Goto(if_smi);

  BIND(&inner_if_strong);
  *extracted = CAST(maybe_object);
  Goto(if_strong);
}

void CodeStubAssembler::DcheckHasValidMap(TNode<HeapObject> object) {
#ifdef V8_MAP_PACKING
  // Test if the map is an unpacked and valid map
  CSA_DCHECK(this, IsMap(LoadMap(object)));
#endif
}

TNode<BoolT> CodeStubAssembler::IsStrong(TNode<MaybeObject> value) {
  return Word32Equal(Word32And(TruncateIntPtrToInt32(
                                   BitcastTaggedToWordForTagAndSmiBits(value)),
                               Int32Constant(kHeapObjectTagMask)),
                     Int32Constant(kHeapObjectTag));
}

TNode<BoolT> CodeStubAssembler::IsStrong(TNode<HeapObjectReference> value) {
  return IsNotSetWord32(
      TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(value)),
      kHeapObjectReferenceTagMask);
}

TNode<HeapObject> CodeStubAssembler::GetHeapObjectIfStrong(
    TNode<MaybeObject> value, Label* if_not_strong) {
  GotoIfNot(IsStrong(value), if_not_strong);
  return CAST(value);
}

TNode<HeapObject> CodeStubAssembler::GetHeapObjectIfStrong(
    TNode<HeapObjectReference> value, Label* if_not_strong) {
  GotoIfNot(IsStrong(value), if_not_strong);
  return ReinterpretCast<HeapObject>(value);
}

TNode<BoolT> CodeStubAssembler::IsWeakOrCleared(TNode<MaybeObject> value) {
  return Word32Equal(Word32And(TruncateIntPtrToInt32(
                                   BitcastTaggedToWordForTagAndSmiBits(value)),
                               Int32Constant(kHeapObjectTagMask)),
                     Int32Constant(kWeakHeapObjectTag));
}

TNode<BoolT> CodeStubAssembler::IsWeakOrCleared(
    TNode<HeapObjectReference> value) {
  return IsSetWord32(
      TruncateIntPtrToInt32(BitcastTaggedToWordForTagAndSmiBits(value)),
      kHeapObjectReferenceTagMask);
}

TNode<BoolT> CodeStubAssembler::IsCleared(TNode<MaybeObject> value) {
  return Word32Equal(TruncateIntPtrToInt32(BitcastMaybeObjectToWord(value)),
                     Int32Constant(kClearedWeakHeapObjectLower32));
}

TNode<HeapObject> CodeStubAssembler::GetHeapObjectAssumeWeak(
    TNode<MaybeObject> value) {
  CSA_DCHECK(this, IsWeakOrCleared(value));
  CSA_DCHECK(this, IsNotCleared(value));
  return UncheckedCast<HeapObject>(BitcastWordToTagged(WordAnd(
      BitcastMaybeObjectToWord(value), IntPtrConstant(~kWeakHeapObjectMask))));
}

TNode<HeapObject> CodeStubAssembler::GetHeapObjectAssumeWeak(
    TNode<MaybeObject> value, Label* if_cleared) {
  GotoIf(IsCleared(value), if_cleared);
  return GetHeapObjectAssumeWeak(value);
}

// This version generates
//   (maybe_object & ~mask) == value
// It works for non-Smi |maybe_object| and for both Smi and HeapObject values
// but requires a big constant for ~mask.
TNode<BoolT> CodeStubAssembler::IsWeakReferenceToObject(
    TNode<MaybeObject> maybe_object, TNode<Object> value) {
  CSA_DCHECK(this, TaggedIsNotSmi(maybe_object));
  if (COMPRESS_POINTERS_BOOL) {
    return Word32Equal(
        Word32And(TruncateWordToInt32(BitcastMaybeObjectToWord(maybe_object)),
                  Uint32Constant(~static_cast<uint32_t>(kWeakHeapObjectMask))),
        TruncateWordToInt32(BitcastTaggedToWord(value)));
  } else {
    return WordEqual(WordAnd(BitcastMaybeObjectToWord(maybe_object),
                             IntPtrConstant(~kWeakHeapObjectMask)),
                     BitcastTaggedToWord(value));
  }
}

// This version generates
//   maybe_object == (heap_object | mask)
// It works for any |maybe_object| values and generates a better code because it
// uses a small constant for mask.
TNode<BoolT> CodeStubAssembler::IsWeakReferenceTo(
    TNode<MaybeObject> maybe_object, TNode<HeapObject> heap_object) {
  if (COMPRESS_POINTERS_BOOL) {
    return Word32Equal(
        TruncateWordToInt32(BitcastMaybeObjectToWord(maybe_object)),
        Word32Or(TruncateWordToInt32(BitcastTaggedToWord(heap_object)),
                 Int32Constant(kWeakHeapObjectMask)));
  } else {
    return WordEqual(BitcastMaybeObjectToWord(maybe_object),
                     WordOr(BitcastTaggedToWord(heap_object),
                            IntPtrConstant(kWeakHeapObjectMask)));
  }
}

TNode<HeapObjectReference> CodeStubAssembler::MakeWeak(
    TNode<HeapObject> value) {
  return ReinterpretCast<HeapObjectReference>(BitcastWordToTagged(
      WordOr(BitcastTaggedToWord(value), IntPtrConstant(kWeakHeapObjectTag))));
}

TNode<MaybeObject> CodeStubAssembler::ClearedValue() {
  return ReinterpretCast<MaybeObject>(
      BitcastWordToTagged(IntPtrConstant(kClearedWeakHeapObjectLower32)));
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(TNode<FixedArray> array) {
  return LoadAndUntagFixedArrayBaseLength(array);
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(
    TNode<ClosureFeedbackCellArray> array) {
  return SmiUntag(LoadArrayCapacity(array));
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(
    TNode<ScriptContextTable> array) {
  return SmiUntag(LoadArrayCapacity(array));
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(
    TNode<RegExpMatchInfo> array) {
  return SmiUntag(LoadArrayCapacity(array));
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(TNode<WeakFixedArray> array) {
  return LoadAndUntagWeakFixedArrayLength(array);
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(TNode<PropertyArray> array) {
  return LoadPropertyArrayLength(array);
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(
    TNode<DescriptorArray> array) {
  return IntPtrMul(ChangeInt32ToIntPtr(LoadNumberOfDescriptors(array)),
                   IntPtrConstant(DescriptorArray::kEntrySize));
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(
    TNode<TransitionArray> array) {
  return LoadAndUntagWeakFixedArrayLength(array);
}

template <>
TNode<IntPtrT> CodeStubAssembler::LoadArrayLength(
    TNode<TrustedFixedArray> array) {
  return SmiUntag(LoadArrayCapacity(array));
}

template <typename Array, typename TIndex, typename TValue>
TNode<TValue> CodeStubAssembler::LoadArrayElement(TNode<Array> array,
                                                  int array_header_size,
                                                  TNode<TIndex> index_node,
                                                  int additional_offset) {
  // TODO(v8:9708): Do we want to keep both IntPtrT and UintPtrT variants?
  static_assert(
      std::is_same<TIndex, Smi>::value ||
          std::is_same<TIndex, UintPtrT>::value ||
          std::is_same<TIndex, IntPtrT>::value ||
          std::is_same<TIndex, TaggedIndex>::value,
      "Only Smi, UintPtrT, IntPtrT or TaggedIndex indices are allowed");
  CSA_DCHECK(this, IntPtrGreaterThanOrEqual(ParameterToIntPtr(index_node),
                                            IntPtrConstant(0)));
  DCHECK(IsAligned(additional_offset, kTaggedSize));
  int32_t header_size = array_header_size + additional_offset - kHeapObjectTag;
  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(index_node, HOLEY_ELEMENTS, header_size);
  CSA_DCHECK(this, IsOffsetInBounds(offset, LoadArrayLength(array),
                                    array_header_size));
  constexpr MachineType machine_type = MachineTypeOf<TValue>::value;
  return UncheckedCast<TValue>(LoadFromObject(machine_type, array, offset));
}

template V8_EXPORT_PRIVATE TNode<MaybeObject>
CodeStubAssembler::LoadArrayElement<TransitionArray, IntPtrT>(
    TNode<TransitionArray>, int, TNode<IntPtrT>, int);
template V8_EXPORT_PRIVATE TNode<FeedbackCell>
CodeStubAssembler::LoadArrayElement<ClosureFeedbackCellArray, UintPtrT>(
    TNode<ClosureFeedbackCellArray>, int, TNode<UintPtrT>, int);
template V8_EXPORT_PRIVATE TNode<Smi> CodeStubAssembler::LoadArrayElement<
    RegExpMatchInfo, IntPtrT>(TNode<RegExpMatchInfo>, int, TNode<IntPtrT>, int);
template V8_EXPORT_PRIVATE TNode<Context>
CodeStubAssembler::LoadArrayElement<ScriptContextTable, IntPtrT>(
    TNode<ScriptContextTable>, int, TNode<IntPtrT>, int);
template V8_EXPORT_PRIVATE TNode<MaybeObject>
CodeStubAssembler::LoadArrayElement<TrustedFixedArray, IntPtrT>(
    TNode<TrustedFixedArray>, int, TNode<IntPtrT>, int);

template <typename TIndex>
TNode<Object> CodeStubAssembler::LoadFixedArrayElement(
    TNode<FixedArray> object, TNode<TIndex> index, int additional_offset,
    CheckBounds check_bounds) {
  // TODO(v8:9708): Do we want to keep both IntPtrT and UintPtrT variants?
  static_assert(
      std::is_same<TIndex, Smi>::value ||
          std::is_same<TIndex, UintPtrT>::value ||
          std::is_same<TIndex, IntPtrT>::value ||
          std::is_same<TIndex, TaggedIndex>::value,
      "Only Smi, UintPtrT, IntPtrT or TaggedIndex indexes are allowed");
  CSA_DCHECK(this, IsFixedArraySubclass(object));
  CSA_DCHECK(this, IsNotWeakFixedArraySubclass(object));

  if (NeedsBoundsCheck(check_bounds)) {
    FixedArrayBoundsCheck(object, index, additional_offset);
  }
  TNode<MaybeObject> element = LoadArrayElement(object, FixedArray::kHeaderSize,
                                                index, additional_offset);
  return CAST(element);
}

template V8_EXPORT_PRIVATE TNode<Object>
CodeStubAssembler::LoadFixedArrayElement<Smi>(TNode<FixedArray>, TNode<Smi>,
                                              int, CheckBounds);
template V8_EXPORT_PRIVATE TNode<Object>
CodeStubAssembler::LoadFixedArrayElement<TaggedIndex>(TNode<FixedArray>,
                                                      TNode<TaggedIndex>, int,
                                                      CheckBounds);
template V8_EXPORT_PRIVATE TNode<Object>
CodeStubAssembler::LoadFixedArrayElement<UintPtrT>(TNode<FixedArray>,
                                                   TNode<UintPtrT>, int,
                                                   CheckBounds);
template V8_EXPORT_PRIVATE TNode<Object>
CodeStubAssembler::LoadFixedArrayElement<IntPtrT>(TNode<FixedArray>,
                                                  TNode<IntPtrT>, int,
                                                  CheckBounds);

void CodeStubAssembler::FixedArrayBoundsCheck(TNode<FixedArrayBase> array,
                                              TNode<Smi> index,
                                              int additional_offset) {
  if (!v8_flags.fixed_array_bounds_checks) return;
  DCHECK(IsAligned(additional_offset, kTaggedSize));
  TNode<Smi> effective_index;
  Tagged<Smi> constant_index;
  bool index_is_constant = TryToSmiConstant(index, &constant_index);
  if (index_is_constant) {
    effective_index = SmiConstant(Smi::ToInt(constant_index) +
                                  additional_offset / kTaggedSize);
  } else {
    effective_index =
        SmiAdd(index, SmiConstant(additional_offset / kTaggedSize));
  }
  CSA_CHECK(this, SmiBelow(effective_index, LoadFixedArrayBaseLength(array)));
}

void CodeStubAssembler::FixedArrayBoundsCheck(TNode<FixedArrayBase> array,
                                              TNode<IntPtrT> index,
                                              int additional_offset) {
  if (!v8_flags.fixed_array_bounds_checks) return;
  DCHECK(IsAligned(additional_offset, kTaggedSize));
  // IntPtrAdd does constant-folding automatically.
  TNode<IntPtrT> effective_index =
      IntPtrAdd(index, IntPtrConstant(additional_offset / kTaggedSize));
  CSA_CHECK(this, UintPtrLessThan(effective_index,
                                  LoadAndUntagFixedArrayBaseLength(array)));
}

TNode<Object> CodeStubAssembler::LoadPropertyArrayElement(
    TNode<PropertyArray> object, TNode<IntPtrT> index) {
  int additional_offset = 0;
  return CAST(LoadArrayElement(object, PropertyArray::kHeaderSize, index,
                               additional_offset));
}

void CodeStubAssembler::FixedArrayBoundsCheck(TNode<FixedArrayBase> array,
                                              TNode<TaggedIndex> index,
                                              int additional_offset) {
  if (!v8_flags.fixed_array_bounds_checks) return;
  DCHECK(IsAligned(additional_offset, kTaggedSize));
  // IntPtrAdd does constant-folding automatically.
  TNode<IntPtrT> effective_index =
      IntPtrAdd(TaggedIndexToIntPtr(index),
                IntPtrConstant(additional_offset / kTaggedSize));
  CSA_CHECK(this, UintPtrLessThan(effective_index,
                                  LoadAndUntagFixedArrayBaseLength(array)));
}

TNode<IntPtrT> CodeStubAssembler::LoadPropertyArrayLength(
    TNode<PropertyArray> object) {
  TNode<Int32T> value = LoadAndUntagToWord32ObjectField(
      object, PropertyArray::kLengthAndHashOffset);
  return Signed(
      ChangeUint32ToWord(DecodeWord32<PropertyArray::LengthField>(value)));
}

TNode<RawPtrT> CodeStubAssembler::LoadJSTypedArrayDataPtr(
    TNode<JSTypedArray> typed_array) {
  // Data pointer = external_pointer + static_cast<Tagged_t>(base_pointer).
  TNode<RawPtrT> external_pointer =
      LoadJSTypedArrayExternalPointerPtr(typed_array);

  TNode<IntPtrT> base_pointer;
  if (COMPRESS_POINTERS_BOOL) {
    TNode<Int32T> compressed_base =
        LoadObjectField<Int32T>(typed_array, JSTypedArray::kBasePointerOffset);
    // Zero-extend TaggedT to WordT according to current compression scheme
    // so that the addition with |external_pointer| (which already contains
    // compensated offset value) below will decompress the tagged value.
    // See JSTypedArray::ExternalPointerCompensationForOnHeapArray() for
    // details.
    base_pointer = Signed(ChangeUint32ToWord(compressed_base));
  } else {
    base_pointer =
        LoadObjectField<IntPtrT>(typed_array, JSTypedArray::kBasePointerOffset);
  }
  return RawPtrAdd(external_pointer, base_pointer);
}

TNode<BigInt> CodeStubAssembler::LoadFixedBigInt64ArrayElementAsTagged(
    TNode<RawPtrT> data_pointer, TNode<IntPtrT> offset) {
  if (Is64()) {
    TNode<IntPtrT> value = Load<IntPtrT>(data_pointer, offset);
    return BigIntFromInt64(value);
  } else {
    DCHECK(!Is64());
#if defined(V8_TARGET_BIG_ENDIAN)
    TNode<IntPtrT> high = Load<IntPtrT>(data_pointer, offset);
    TNode<IntPtrT> low = Load<IntPtrT>(
        data_pointer, IntPtrAdd(offset, IntPtrConstant(kSystemPointerSize)));
#else
    TNode<IntPtrT> low = Load<IntPtrT>(data_pointer, offset);
    TNode<IntPtrT> high = Load<IntPtrT>(
        data_pointer, IntPtrAdd(offset, IntPtrConstant(kSystemPointerSize)));
#endif
    return BigIntFromInt32Pair(low, high);
  }
}

TNode<BigInt> CodeStubAssembler::BigIntFromInt32Pair(TNode<IntPtrT> low,
                                                     TNode<IntPtrT> high) {
  DCHECK(!Is64());
  TVARIABLE(BigInt, var_result);
  TVARIABLE(Word32T, var_sign, Int32Constant(BigInt::SignBits::encode(false)));
  TVARIABLE(IntPtrT, var_high, high);
  TVARIABLE(IntPtrT, var_low, low);
  Label high_zero(this), negative(this), allocate_one_digit(this),
      allocate_two_digits(this), if_zero(this), done(this);

  GotoIf(IntPtrEqual(var_high.value(), IntPtrConstant(0)), &high_zero);
  Branch(IntPtrLessThan(var_high.value(), IntPtrConstant(0)), &negative,
         &allocate_two_digits);

  BIND(&high_zero);
  Branch(IntPtrEqual(var_low.value(), IntPtrConstant(0)), &if_zero,
         &allocate_one_digit);

  BIND(&negative);
  {
    var_sign = Int32Constant(BigInt::SignBits::encode(true));
    // We must negate the value by computing "0 - (high|low)", performing
    // both parts of the subtraction separately and manually taking care
    // of the carry bit (which is 1 iff low != 0).
    var_high = IntPtrSub(IntPtrConstant(0), var_high.value());
    Label carry(this), no_carry(this);
    Branch(IntPtrEqual(var_low.value(), IntPtrConstant(0)), &no_carry, &carry);
    BIND(&carry);
    var_high = IntPtrSub(var_high.value(), IntPtrConstant(1));
    Goto(&no_carry);
    BIND(&no_carry);
    var_low = IntPtrSub(IntPtrConstant(0), var_low.value());
    // var_high was non-zero going into this block, but subtracting the
    // carry bit from it could bring us back onto the "one digit" path.
    Branch(IntPtrEqual(var_high.value(), IntPtrConstant(0)),
           &allocate_one_digit, &allocate_two_digits);
  }

  BIND(&allocate_one_digit);
  {
    var_result = AllocateRawBigInt(IntPtrConstant(1));
    StoreBigIntBitfield(var_result.value(),
                        Word32Or(var_sign.value(),
                                 Int32Constant(BigInt::LengthBits::encode(1))));
    StoreBigIntDigit(var_result.value(), 0, Unsigned(var_low.value()));
    Goto(&done);
  }

  BIND(&allocate_two_digits);
  {
    var_result = AllocateRawBigInt(IntPtrConstant(2));
    StoreBigIntBitfield(var_result.value(),
                        Word32Or(var_sign.value(),
                                 Int32Constant(BigInt::LengthBits::encode(2))));
    StoreBigIntDigit(var_result.value(), 0, Unsigned(var_low.value()));
    StoreBigIntDigit(var_result.value(), 1, Unsigned(var_high.value()));
    Goto(&done);
  }

  BIND(&if_zero);
  var_result = AllocateBigInt(IntPtrConstant(0));
  Goto(&done);

  BIND(&done);
  return var_result.value();
}

TNode<BigInt> CodeStubAssembler::BigIntFromInt64(TNode<IntPtrT> value) {
  DCHECK(Is64());
  TVARIABLE(BigInt, var_result);
  Label done(this), if_positive(this), if_negative(this), if_zero(this);
  GotoIf(IntPtrEqual(value, IntPtrConstant(0)), &if_zero);
  var_result = AllocateRawBigInt(IntPtrConstant(1));
  Branch(IntPtrGreaterThan(value, IntPtrConstant(0)), &if_positive,
         &if_negative);

  BIND(&if_positive);
  {
    StoreBigIntBitfield(var_result.value(),
                        Int32Constant(BigInt::SignBits::encode(false) |
                                      BigInt::LengthBits::encode(1)));
    StoreBigIntDigit(var_result.value(), 0, Unsigned(value));
    Goto(&done);
  }

  BIND(&if_negative);
  {
    StoreBigIntBitfield(var_result.value(),
                        Int32Constant(BigInt::SignBits::encode(true) |
                                      BigInt::LengthBits::encode(1)));
    StoreBigIntDigit(var_result.value(), 0,
                     Unsigned(IntPtrSub(IntPtrConstant(0), value)));
    Goto(&done);
  }

  BIND(&if_zero);
  {
    var_result = AllocateBigInt(IntPtrConstant(0));
    Goto(&done);
  }

  BIND(&done);
  return var_result.value();
}

TNode<BigInt> CodeStubAssembler::LoadFixedBigUint64ArrayElementAsTagged(
    TNode<RawPtrT> data_pointer, TNode<IntPtrT> offset) {
  Label if_zero(this), done(this);
  if (Is64()) {
    TNode<UintPtrT> value = Load<UintPtrT>(data_pointer, offset);
    return BigIntFromUint64(value);
  } else {
    DCHECK(!Is64());
#if defined(V8_TARGET_BIG_ENDIAN)
    TNode<UintPtrT> high = Load<UintPtrT>(data_pointer, offset);
    TNode<UintPtrT> low = Load<UintPtrT>(
        data_pointer, IntPtrAdd(offset, IntPtrConstant(kSystemPointerSize)));
#else
    TNode<UintPtrT> low = Load<UintPtrT>(data_pointer, offset);
    TNode<UintPtrT> high = Load<UintPtrT>(
        data_pointer, IntPtrAdd(offset, IntPtrConstant(kSystemPointerSize)));
#endif
    return BigIntFromUint32Pair(low, high);
  }
}

TNode<BigInt> CodeStubAssembler::BigIntFromUint32Pair(TNode<UintPtrT> low,
                                                      TNode<UintPtrT> high) {
  DCHECK(!Is64());
  TVARIABLE(BigInt, var_result);
  Label high_zero(this), if_zero(this), done(this);

  GotoIf(IntPtrEqual(high, IntPtrConstant(0)), &high_zero);
  var_result = AllocateBigInt(IntPtrConstant(2));
  StoreBigIntDigit(var_result.value(), 0, low);
  StoreBigIntDigit(var_result.value(), 1, high);
  Goto(&done);

  BIND(&high_zero);
  GotoIf(IntPtrEqual(low, IntPtrConstant(0)), &if_zero);
  var_result = AllocateBigInt(IntPtrConstant(1));
  StoreBigIntDigit(var_result.value(), 0, low);
  Goto(&done);

  BIND(&if_zero);
  var_result = AllocateBigInt(IntPtrConstant(0));
  Goto(&done);

  BIND(&done);
  return var_result.value();
}

TNode<BigInt> CodeStubAssembler::BigIntFromUint64(TNode<UintPtrT> value) {
  DCHECK(Is64());
  TVARIABLE(BigInt, var_result);
  Label done(this), if_zero(this);
  GotoIf(IntPtrEqual(value, IntPtrConstant(0)), &if_zero);
  var_result = AllocateBigInt(IntPtrConstant(1));
  StoreBigIntDigit(var_result.value(), 0, value);
  Goto(&done);

  BIND(&if_zero);
  var_result = AllocateBigInt(IntPtrConstant(0));
  Goto(&done);
  BIND(&done);
  return var_result.value();
}

TNode<Numeric> CodeStubAssembler::LoadFixedTypedArrayElementAsTagged(
    TNode<RawPtrT> data_pointer, TNode<UintPtrT> index,
    ElementsKind elements_kind) {
  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(Signed(index), elements_kind, 0);
  switch (elements_kind) {
    case UINT8_ELEMENTS: /* fall through */
    case UINT8_CLAMPED_ELEMENTS:
      return SmiFromInt32(Load<Uint8T>(data_pointer, offset));
    case INT8_ELEMENTS:
      return SmiFromInt32(Load<Int8T>(data_pointer, offset));
    case UINT16_ELEMENTS:
      return SmiFromInt32(Load<Uint16T>(data_pointer, offset));
    case INT16_ELEMENTS:
      return SmiFromInt32(Load<Int16T>(data_pointer, offset));
    case UINT32_ELEMENTS:
      return ChangeUint32ToTagged(Load<Uint32T>(data_pointer, offset));
    case INT32_ELEMENTS:
      return ChangeInt32ToTagged(Load<Int32T>(data_pointer, offset));
    case FLOAT16_ELEMENTS:
      return AllocateHeapNumberWithValue(
          ChangeFloat16ToFloat64(Load<Float16T>(data_pointer, offset)));
    case FLOAT32_ELEMENTS:
      return AllocateHeapNumberWithValue(
          ChangeFloat32ToFloat64(Load<Float32T>(data_pointer, offset)));
    case FLOAT64_ELEMENTS:
      return AllocateHeapNumberWithValue(Load<Float64T>(data_pointer, offset));
    case BIGINT64_ELEMENTS:
      return LoadFixedBigInt64ArrayElementAsTagged(data_pointer, offset);
    case BIGUINT64_ELEMENTS:
      return LoadFixedBigUint64ArrayElementAsTagged(data_pointer, offset);
    default:
      UNREACHABLE();
  }
}

TNode<Numeric> CodeStubAssembler::LoadFixedTypedArrayElementAsTagged(
    TNode<RawPtrT> data_pointer, TNode<UintPtrT> index,
    TNode<Int32T> elements_kind) {
  TVARIABLE(Numeric, var_result);
  Label done(this), if_unknown_type(this, Label::kDeferred);
  int32_t elements_kinds[] = {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) TYPE##_ELEMENTS,
      TYPED_ARRAYS(TYPED_ARRAY_CASE) RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
  };

#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) Label if_##type##array(this);
  TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

  Label* elements_kind_labels[] = {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) &if_##type##array,
      TYPED_ARRAYS(TYPED_ARRAY_CASE)
      // The same labels again for RAB / GSAB. We dispatch RAB / GSAB elements
      // kinds to the corresponding non-RAB / GSAB elements kinds.
      TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
  };
  static_assert(arraysize(elements_kinds) == arraysize(elements_kind_labels));

  Switch(elements_kind, &if_unknown_type, elements_kinds, elements_kind_labels,
         arraysize(elements_kinds));

  BIND(&if_unknown_type);
  Unreachable();

#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype)                        \
  BIND(&if_##type##array);                                               \
  {                                                                      \
    var_result = LoadFixedTypedArrayElementAsTagged(data_pointer, index, \
                                                    TYPE##_ELEMENTS);    \
    Goto(&done);                                                         \
  }
  TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

  BIND(&done);
  return var_result.value();
}

template <typename TIndex>
TNode<MaybeObject> CodeStubAssembler::LoadFeedbackVectorSlot(
    TNode<FeedbackVector> feedback_vector, TNode<TIndex> slot,
    int additional_offset) {
  int32_t header_size = FeedbackVector::kRawFeedbackSlotsOffset +
                        additional_offset - kHeapObjectTag;
  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(slot, HOLEY_ELEMENTS, header_size);
  CSA_SLOW_DCHECK(
      this, IsOffsetInBounds(offset, LoadFeedbackVectorLength(feedback_vector),
                             FeedbackVector::kHeaderSize));
  return Load<MaybeObject>(feedback_vector, offset);
}

template TNode<MaybeObject> CodeStubAssembler::LoadFeedbackVectorSlot(
    TNode<FeedbackVector> feedback_vector, TNode<TaggedIndex> slot,
    int additional_offset);
template TNode<MaybeObject> CodeStubAssembler::LoadFeedbackVectorSlot(
    TNode<FeedbackVector> feedback_vector, TNode<IntPtrT> slot,
    int additional_offset);
template TNode<MaybeObject> CodeStubAssembler::LoadFeedbackVectorSlot(
    TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot,
    int additional_offset);

template <typename Array>
TNode<Int32T> CodeStubAssembler::LoadAndUntagToWord32ArrayElement(
    TNode<Array> object, int array_header_size, TNode<IntPtrT> index,
    int additional_offset) {
  DCHECK(IsAligned(additional_offset, kTaggedSize));
  int endian_correction = 0;
#if V8_TARGET_LITTLE_ENDIAN
  if (SmiValuesAre32Bits()) endian_correction = 4;
#endif
  int32_t header_size = array_header_size + additional_offset - kHeapObjectTag +
                        endian_correction;
  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(index, HOLEY_ELEMENTS, header_size);
  CSA_DCHECK(this, IsOffsetInBounds(offset, LoadArrayLength(object),
                                    array_header_size + endian_correction));
  if (SmiValuesAre32Bits()) {
    return Load<Int32T>(object, offset);
  } else {
    return SmiToInt32(Load<Smi>(object, offset));
  }
}

TNode<Int32T> CodeStubAssembler::LoadAndUntagToWord32FixedArrayElement(
    TNode<FixedArray> object, TNode<IntPtrT> index, int additional_offset) {
  CSA_SLOW_DCHECK(this, IsFixedArraySubclass(object));
  return LoadAndUntagToWord32ArrayElement(object, FixedArray::kHeaderSize,
                                          index, additional_offset);
}

TNode<MaybeObject> CodeStubAssembler::LoadWeakFixedArrayElement(
    TNode<WeakFixedArray> object, TNode<IntPtrT> index, int additional_offset) {
  return LoadArrayElement(object, WeakFixedArray::kHeaderSize, index,
                          additional_offset);
}

TNode<Float64T> CodeStubAssembler::LoadFixedDoubleArrayElement(
    TNode<FixedDoubleArray> object, TNode<IntPtrT> index, Label* if_hole,
    MachineType machine_type) {
  int32_t header_size = FixedDoubleArray::kHeaderSize - kHeapObjectTag;
  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(index, HOLEY_DOUBLE_ELEMENTS, header_size);
  CSA_DCHECK(this, IsOffsetInBounds(
                       offset, LoadAndUntagFixedArrayBaseLength(object),
                       FixedDoubleArray::kHeaderSize, HOLEY_DOUBLE_ELEMENTS));
  return LoadDoubleWithHoleCheck(object, offset, if_hole, machine_type);
}

TNode<Object> CodeStubAssembler::LoadFixedArrayBaseElementAsTagged(
    TNode<FixedArrayBase> elements, TNode<IntPtrT> index,
    TNode<Int32T> elements_kind, Label* if_accessor, Label* if_hole) {
  TVARIABLE(Object, var_result);
  Label done(this), if_packed(this), if_holey(this), if_packed_double(this),
      if_holey_double(this), if_dictionary(this, Label::kDeferred);

  int32_t kinds[] = {
      // Handled by if_packed.
      PACKED_SMI_ELEMENTS, PACKED_ELEMENTS, PACKED_NONEXTENSIBLE_ELEMENTS,
      PACKED_SEALED_ELEMENTS, PACKED_FROZEN_ELEMENTS,
      // Handled by if_holey.
      HOLEY_SMI_ELEMENTS, HOLEY_ELEMENTS, HOLEY_NONEXTENSIBLE_ELEMENTS,
      HOLEY_SEALED_ELEMENTS, HOLEY_FROZEN_ELEMENTS,
      // Handled by if_packed_double.
      PACKED_DOUBLE_ELEMENTS,
      // Handled by if_holey_double.
      HOLEY_DOUBLE_ELEMENTS};
  Label* labels[] = {// PACKED_{SMI,}_ELEMENTS
                     &if_packed, &if_packed, &if_packed, &if_packed, &if_packed,
                     // HOLEY_{SMI,}_ELEMENTS
                     &if_holey, &if_holey, &if_holey, &if_holey, &if_holey,
                     // PACKED_DOUBLE_ELEMENTS
                     &if_packed_double,
                     // HOLEY_DOUBLE_ELEMENTS
                     &if_holey_double};
  Switch(elements_kind, &if_dictionary, kinds, labels, arraysize(kinds));

  BIND(&if_packed);
  {
    var_result = LoadFixedArrayElement(CAST(elements), index, 0);
    Goto(&done);
  }

  BIND(&if_holey);
  {
    var_result = LoadFixedArrayElement(CAST(elements), index);
    Branch(TaggedEqual(var_result.value(), TheHoleConstant()), if_hole, &done);
  }

  BIND(&if_packed_double);
  {
    var_result = AllocateHeapNumberWithValue(
        LoadFixedDoubleArrayElement(CAST(elements), index));
    Goto(&done);
  }

  BIND(&if_holey_double);
  {
    var_result = AllocateHeapNumberWithValue(
        LoadFixedDoubleArrayElement(CAST(elements), index, if_hole));
    Goto(&done);
  }

  BIND(&if_dictionary);
  {
    CSA_DCHECK(this, IsDictionaryElementsKind(elements_kind));
    var_result = BasicLoadNumberDictionaryElement(CAST(elements), index,
                                                  if_accessor, if_hole);
    Goto(&done);
  }

  BIND(&done);
  return var_result.value();
}

TNode<BoolT> CodeStubAssembler::IsDoubleHole(TNode<Object> base,
                                             TNode<IntPtrT> offset) {
  // TODO(ishell): Compare only the upper part for the hole once the
  // compiler is able to fold addition of already complex |offset| with
  // |kIeeeDoubleExponentWordOffset| into one addressing mode.
  if (Is64()) {
    TNode<Uint64T> element = Load<Uint64T>(base, offset);
    return Word64Equal(element, Int64Constant(kHoleNanInt64));
  } else {
    TNode<Uint32T> element_upper = Load<Uint32T>(
        base, IntPtrAdd(offset, IntPtrConstant(kIeeeDoubleExponentWordOffset)));
    return Word32Equal(element_upper, Int32Constant(kHoleNanUpper32));
  }
}

TNode<Float64T> CodeStubAssembler::LoadDoubleWithHoleCheck(
    TNode<Object> base, TNode<IntPtrT> offset, Label* if_hole,
    MachineType machine_type) {
  if (if_hole) {
    GotoIf(IsDoubleHole(base, offset), if_hole);
  }
  if (machine_type.IsNone()) {
    // This means the actual value is not needed.
    return TNode<Float64T>();
  }
  return UncheckedCast<Float64T>(Load(machine_type, base, offset));
}

TNode<ScopeInfo> CodeStubAssembler::LoadScopeInfo(TNode<Context> context) {
  return CAST(LoadContextElement(context, Context::SCOPE_INFO_INDEX));
}

TNode<BoolT> CodeStubAssembler::LoadScopeInfoHasExtensionField(
    TNode<ScopeInfo> scope_info) {
  TNode<Uint32T> value =
      LoadObjectField<Uint32T>(scope_info, ScopeInfo::kFlagsOffset);
  return IsSetWord32<ScopeInfo::HasContextExtensionSlotBit>(value);
}

TNode<BoolT> CodeStubAssembler::LoadScopeInfoClassScopeHasPrivateBrand(
    TNode<ScopeInfo> scope_info) {
  TNode<Uint32T> value =
      LoadObjectField<Uint32T>(scope_info, ScopeInfo::kFlagsOffset);
  return IsSetWord32<ScopeInfo::ClassScopeHasPrivateBrandBit>(value);
}

void CodeStubAssembler::StoreContextElementNoWriteBarrier(
    TNode<Context> context, int slot_index, TNode<Object> value) {
  int offset = Context::SlotOffset(slot_index);
  StoreNoWriteBarrier(MachineRepresentation::kTagged, context,
                      IntPtrConstant(offset), value);
}

TNode<NativeContext> CodeStubAssembler::LoadNativeContext(
    TNode<Context> context) {
  TNode<Map> map = LoadMap(context);
  return CAST(LoadObjectField(
      map, Map::kConstructorOrBackPointerOrNativeContextOffset));
}

TNode<Context> CodeStubAssembler::LoadModuleContext(TNode<Context> context) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> module_map = CAST(
      LoadContextElement(native_context, Context::MODULE_CONTEXT_MAP_INDEX));
  TVariable<Object> cur_context(context, this);

  Label context_found(this);

  Label context_search(this, &cur_context);

  // Loop until cur_context->map() is module_map.
  Goto(&context_search);
  BIND(&context_search);
  {
    CSA_DCHECK(this, Word32BinaryNot(
                         TaggedEqual(cur_context.value(), native_context)));
    GotoIf(TaggedEqual(LoadMap(CAST(cur_context.value())), module_map),
           &context_found);

    cur_context =
        LoadContextElement(CAST(cur_context.value()), Context::PREVIOUS_INDEX);
    Goto(&context_search);
  }

  BIND(&context_found);
  return UncheckedCast<Context>(cur_context.value());
}

TNode<Object> CodeStubAssembler::GetImportMetaObject(TNode<Context> context) {
  const TNode<Context> module_context = LoadModuleContext(context);
  const TNode<HeapObject> module =
      CAST(LoadContextElement(module_context, Context::EXTENSION_INDEX));
  const TNode<Object> import_meta =
      LoadObjectField(module, SourceTextModule::kImportMetaOffset);

  TVARIABLE(Object, return_value, import_meta);

  Label end(this);
  GotoIfNot(IsTheHole(import_meta), &end);

  return_value = CallRuntime(Runtime::kGetImportMetaObject, context);
  Goto(&end);

  BIND(&end);
  return return_value.value();
}

TNode<Map> CodeStubAssembler::LoadObjectFunctionInitialMap(
    TNode<NativeContext> native_context) {
  TNode<JSFunction> object_function =
      CAST(LoadContextElement(native_context, Context::OBJECT_FUNCTION_INDEX));
  return CAST(LoadJSFunctionPrototypeOrInitialMap(object_function));
}

TNode<Map> CodeStubAssembler::LoadCachedMap(TNode<NativeContext> native_context,
                                            TNode<IntPtrT> number_of_properties,
                                            Label* runtime) {
  CSA_DCHECK(this, UintPtrLessThan(number_of_properties,
                                   IntPtrConstant(JSObject::kMapCacheSize)));
  TNode<WeakFixedArray> cache =
      CAST(LoadContextElement(native_context, Context::MAP_CACHE_INDEX));
  TNode<MaybeObject> value =
      LoadWeakFixedArrayElement(cache, number_of_properties, 0);
  TNode<Map> result = CAST(GetHeapObjectAssumeWeak(value, runtime));
  return result;
}

TNode<Map> CodeStubAssembler::LoadSlowObjectWithNullPrototypeMap(
    TNode<NativeContext> native_context) {
  TNode<Map> map = CAST(LoadContextElement(
      native_context, Context::SLOW_OBJECT_WITH_NULL_PROTOTYPE_MAP));
  return map;
}

TNode<Map> CodeStubAssembler::LoadJSArrayElementsMap(
    TNode<Int32T> kind, TNode<NativeContext> native_context) {
  CSA_DCHECK(this, IsFastElementsKind(kind));
  TNode<IntPtrT> offset =
      IntPtrAdd(IntPtrConstant(Context::FIRST_JS_ARRAY_MAP_SLOT),
                ChangeInt32ToIntPtr(kind));
  return UncheckedCast<Map>(LoadContextElement(native_context, offset));
}

TNode<Map> CodeStubAssembler::LoadJSArrayElementsMap(
    ElementsKind kind, TNode<NativeContext> native_context) {
  return UncheckedCast<Map>(
      LoadContextElement(native_context, Context::ArrayMapIndex(kind)));
}

TNode<Uint32T> CodeStubAssembler::LoadFunctionKind(TNode<JSFunction> function) {
  const TNode<SharedFunctionInfo> shared_function_info =
      LoadObjectField<SharedFunctionInfo>(
          function, JSFunction::kSharedFunctionInfoOffset);

  const TNode<Uint32T> function_kind =
      DecodeWord32<SharedFunctionInfo::FunctionKindBits>(
          LoadObjectField<Uint32T>(shared_function_info,
                                   SharedFunctionInfo::kFlagsOffset));
  return function_kind;
}

TNode<BoolT> CodeStubAssembler::IsGeneratorFunction(
    TNode<JSFunction> function) {
  const TNode<Uint32T> function_kind = LoadFunctionKind(function);

  // See IsGeneratorFunction(FunctionKind kind).
  return IsInRange(
      function_kind,
      static_cast<uint32_t>(FunctionKind::kAsyncConciseGeneratorMethod),
      static_cast<uint32_t>(FunctionKind::kConciseGeneratorMethod));
}

TNode<BoolT> CodeStubAssembler::IsJSFunctionWithPrototypeSlot(
    TNode<HeapObject> object) {
  // Only JSFunction maps may have HasPrototypeSlotBit set.
  return IsSetWord32<Map::Bits1::HasPrototypeSlotBit>(
      LoadMapBitField(LoadMap(object)));
}

void CodeStubAssembler::BranchIfHasPrototypeProperty(
    TNode<JSFunction> function, TNode<Int32T> function_map_bit_field,
    Label* if_true, Label* if_false) {
  // (has_prototype_slot() && IsConstructor()) ||
  // IsGeneratorFunction(shared()->kind())
  uint32_t mask = Map::Bits1::HasPrototypeSlotBit::kMask |
                  Map::Bits1::IsConstructorBit::kMask;

  GotoIf(IsAllSetWord32(function_map_bit_field, mask), if_true);
  Branch(IsGeneratorFunction(function), if_true, if_false);
}

void CodeStubAssembler::GotoIfPrototypeRequiresRuntimeLookup(
    TNode<JSFunction> function, TNode<Map> map, Label* runtime) {
  // !has_prototype_property() || has_non_instance_prototype()
  TNode<Int32T> map_bit_field = LoadMapBitField(map);
  Label next_check(this);
  BranchIfHasPrototypeProperty(function, map_bit_field, &next_check, runtime);
  BIND(&next_check);
  GotoIf(IsSetWord32<Map::Bits1::HasNonInstancePrototypeBit>(map_bit_field),
         runtime);
}

TNode<HeapObject> CodeStubAssembler::LoadJSFunctionPrototype(
    TNode<JSFunction> function, Label* if_bailout) {
  CSA_DCHECK(this, IsFunctionWithPrototypeSlotMap(LoadMap(function)));
  CSA_DCHECK(this, IsClearWord32<Map::Bits1::HasNonInstancePrototypeBit>(
                       LoadMapBitField(LoadMap(function))));
  TNode<HeapObject> proto_or_map = LoadObjectField<HeapObject>(
      function, JSFunction::kPrototypeOrInitialMapOffset);
  GotoIf(IsTheHole(proto_or_map), if_bailout);

  TVARIABLE(HeapObject, var_result, proto_or_map);
  Label done(this, &var_result);
  GotoIfNot(IsMap(proto_or_map), &done);

  var_result = LoadMapPrototype(CAST(proto_or_map));
  Goto(&done);

  BIND(&done);
  return var_result.value();
}

TNode<Code> CodeStubAssembler::LoadJSFunctionCode(TNode<JSFunction> function) {
  return LoadCodePointerFromObject(function, JSFunction::kCodeOffset);
}

TNode<Object> CodeStubAssembler::LoadSharedFunctionInfoTrustedData(
    TNode<SharedFunctionInfo> sfi) {
#ifdef V8_ENABLE_SANDBOX
  TNode<IndirectPointerHandleT> trusted_data_handle =
      LoadObjectField<IndirectPointerHandleT>(
          sfi, SharedFunctionInfo::kTrustedFunctionDataOffset);

  return Select<Object>(
      Word32Equal(trusted_data_handle,
                  Int32Constant(kNullIndirectPointerHandle)),
      [=, this] { return SmiConstant(0); },
      [=, this] {
        return ResolveIndirectPointerHandle(trusted_data_handle,
                                            kUnknownIndirectPointerTag);
      });
#else
  return LoadObjectField<Object>(
      sfi, SharedFunctionInfo::kTrustedFunctionDataOffset);
#endif
}

TNode<Object> CodeStubAssembler::LoadSharedFunctionInfoUntrustedData(
    TNode<SharedFunctionInfo> sfi) {
  return LoadObjectField<Object>(
      sfi, SharedFunctionInfo::kUntrustedFunctionDataOffset);
}

TNode<BoolT> CodeStubAssembler::SharedFunctionInfoHasBaselineCode(
    TNode<SharedFunctionInfo> sfi) {
  TNode<Object> data = LoadSharedFunctionInfoTrustedData(sfi);
  return TaggedIsCode(data);
}

TNode<Smi> CodeStubAssembler::LoadSharedFunctionInfoBuiltinId(
    TNode<SharedFunctionInfo> sfi) {
  return LoadObjectField<Smi>(sfi,
                              SharedFunctionInfo::kUntrustedFunctionDataOffset);
}

TNode<BytecodeArray> CodeStubAssembler::LoadSharedFunctionInfoBytecodeArray(
    TNode<SharedFunctionInfo> sfi) {
  TNode<HeapObject> function_data = LoadTrustedPointerFromObject(
      sfi, SharedFunctionInfo::kTrustedFunctionDataOffset,
      kUnknownIndirectPointerTag);

  TVARIABLE(HeapObject, var_result, function_data);

  Label check_for_interpreter_data(this, &var_result);
  Label done(this, &var_result);

  GotoIfNot(HasInstanceType(var_result.value(), CODE_TYPE),
            &check_for_interpreter_data);
  {
    TNode<Code> code = CAST(var_result.value());
#ifdef DEBUG
    TNode<Int32T> code_flags =
        LoadObjectField<Int32T>(code, Code::kFlagsOffset);
    CSA_DCHECK(
        this, Word32Equal(DecodeWord32<Code::KindField>(code_flags),
                          Int32Constant(static_cast<int>(CodeKind::BASELINE))));
#endif  // DEBUG
    TNode<HeapObject> baseline_data = CAST(LoadProtectedPointerField(
        code, Code::kDeoptimizationDataOrInterpreterDataOffset));
    var_result = baseline_data;
  }
  Goto(&check_for_interpreter_data);

  BIND(&check_for_interpreter_data);

  GotoIfNot(HasInstanceType(var_result.value(), INTERPRETER_DATA_TYPE), &done);
  TNode<BytecodeArray> bytecode_array = CAST(LoadProtectedPointerField(
      CAST(var_result.value()), InterpreterData::kBytecodeArrayOffset));
  var_result = bytecode_array;
  Goto(&done);

  BIND(&done);
  // We need an explicit check here since we use the
  // kUnknownIndirectPointerTag above and so don't have any type guarantees.
  CSA_SBXCHECK(this, HasInstanceType(var_result.value(), BYTECODE_ARRAY_TYPE));
  return CAST(var_result.value());
}

#ifdef V8_ENABLE_WEBASSEMBLY
TNode<WasmFunctionData>
CodeStubAssembler::LoadSharedFunctionInfoWasmFunctionData(
    TNode<SharedFunctionInfo> sfi) {
  return CAST(LoadTrustedPointerFromObject(
      sfi, SharedFunctionInfo::kTrustedFunctionDataOffset,
      kWasmFunctionDataIndirectPointerTag));
}

TNode<WasmExportedFunctionData>
CodeStubAssembler::LoadSharedFunctionInfoWasmExportedFunctionData(
    TNode<SharedFunctionInfo> sfi) {
  TNode<WasmFunctionData> function_data =
      LoadSharedFunctionInfoWasmFunctionData(sfi);
  // TODO(saelo): it would be nice if we could use LoadTrustedPointerFromObject
  // with a kWasmExportedFunctionDataIndirectPointerTag to avoid the SBXCHECK,
  // but for that our tagging scheme first needs to support type hierarchies.
  CSA_SBXCHECK(
      this, HasInstanceType(function_data, WASM_EXPORTED_FUNCTION_DATA_TYPE));
  return CAST(function_data);
}

TNode<WasmJSFunctionData>
CodeStubAssembler::LoadSharedFunctionInfoWasmJSFunctionData(
    TNode<SharedFunctionInfo> sfi) {
  TNode<WasmFunctionData> function_data =
      LoadSharedFunctionInfoWasmFunctionData(sfi);
  // TODO(saelo): it would be nice if we could use LoadTrustedPointerFromObject
  // with a kWasmJSFunctionDataIndirectPointerTag to avoid the SBXCHECK, but
  // for that our tagging scheme first needs to support type hierarchies.
  CSA_SBXCHECK(this,
               HasInstanceType(function_data, WASM_JS_FUNCTION_DATA_TYPE));
  return CAST(function_data);
}
#endif  // V8_ENABLE_WEBASSEMBLY

TNode<Int32T> CodeStubAssembler::LoadBytecodeArrayParameterCount(
    TNode<BytecodeArray> bytecode_array) {
  return LoadObjectField<Uint16T>(bytecode_array,
                                  BytecodeArray::kParameterSizeOffset);
}

TNode<Int32T> CodeStubAssembler::LoadBytecodeArrayParameterCountWithoutReceiver(
    TNode<BytecodeArray> bytecode_array) {
  return Int32Sub(LoadBytecodeArrayParameterCount(bytecode_array),
                  Int32Constant(kJSArgcReceiverSlots));
}

void CodeStubAssembler::StoreObjectByteNoWriteBarrier(TNode<HeapObject> object,
                                                      int offset,
                                                      TNode<Word32T> value) {
  StoreNoWriteBarrier(MachineRepresentation::kWord8, object,
                      IntPtrConstant(offset - kHeapObjectTag), value);
}

void CodeStubAssembler::StoreHeapNumberValue(TNode<HeapNumber> object,
                                             TNode<Float64T> value) {
  StoreObjectFieldNoWriteBarrier(object, offsetof(HeapNumber, value_), value);
}

void CodeStubAssembler::StoreObjectField(TNode<HeapObject> object, int offset,
                                         TNode<Smi> value) {
  StoreObjectFieldNoWriteBarrier(object, offset, value);
}

void CodeStubAssembler::StoreObjectField(TNode<HeapObject> object,
                                         TNode<IntPtrT> offset,
                                         TNode<Smi> value) {
  StoreObjectFieldNoWriteBarrier(object, offset, value);
}

void CodeStubAssembler::StoreObjectField(TNode<HeapObject> object, int offset,
                                         TNode<Object> value) {
  DCHECK_NE(HeapObject::kMapOffset, offset);  // Use StoreMap instead.
  OptimizedStoreField(MachineRepresentation::kTagged,
                      UncheckedCast<HeapObject>(object), offset, value);
}

void CodeStubAssembler::StoreObjectField(TNode<HeapObject> object,
                                         TNode<IntPtrT> offset,
                                         TNode<Object> value) {
  int const_offset;
  if (TryToInt32Constant(offset, &const_offset)) {
    StoreObjectField(object, const_offset, value);
  } else {
    Store(object, IntPtrSub(offset, IntPtrConstant(kHeapObjectTag)), value);
  }
}

void CodeStubAssembler::StoreIndirectPointerField(
    TNode<HeapObject> object, int offset, IndirectPointerTag tag,
    TNode<ExposedTrustedObject> value) {
  DCHECK(V8_ENABLE_SANDBOX_BOOL);
  OptimizedStoreIndirectPointerField(object, offset, tag, value);
}

void CodeStubAssembler::StoreIndirectPointerFieldNoWriteBarrier(
    TNode<HeapObject> object, int offset, IndirectPointerTag tag,
    TNode<ExposedTrustedObject> value) {
  DCHECK(V8_ENABLE_SANDBOX_BOOL);
  OptimizedStoreIndirectPointerFieldNoWriteBarrier(object, offset, tag, value);
}

void CodeStubAssembler::StoreTrustedPointerField(
    TNode<HeapObject> object, int offset, IndirectPointerTag tag,
    TNode<ExposedTrustedObject> value) {
#ifdef V8_ENABLE_SANDBOX
  StoreIndirectPointerField(object, offset, tag, value);
#else
  StoreObjectField(object, offset, value);
#endif  // V8_ENABLE_SANDBOX
}

void CodeStubAssembler::StoreTrustedPointerFieldNoWriteBarrier(
    TNode<HeapObject> object, int offset, IndirectPointerTag tag,
    TNode<ExposedTrustedObject> value) {
#ifdef V8_ENABLE_SANDBOX
  StoreIndirectPointerFieldNoWriteBarrier(object, offset, tag, value);
#else
  StoreObjectFieldNoWriteBarrier(object, offset, value);
#endif  // V8_ENABLE_SANDBOX
}

void CodeStubAssembler::ClearTrustedPointerField(TNode<HeapObject> object,
                                                 int offset) {
#ifdef V8_ENABLE_SANDBOX
  StoreObjectFieldNoWriteBarrier(object, offset,
                                 Uint32Constant(kNullTrustedPointerHandle));
#else
  StoreObjectFieldNoWriteBarrier(object, offset, SmiConstant(0));
#endif
}

void CodeStubAssembler::UnsafeStoreObjectFieldNoWriteBarrier(
    TNode<HeapObject> object, int offset, TNode<Object> value) {
  DCHECK_NE(HeapObject::kMapOffset, offset);  // Use StoreMap instead.
  OptimizedStoreFieldUnsafeNoWriteBarrier(MachineRepresentation::kTagged,
                                          object, offset, value);
}

void CodeStubAssembler::StoreSharedObjectField(TNode<HeapObject> object,
                                               TNode<IntPtrT> offset,
                                               TNode<Object> value) {
  CSA_DCHECK(this,
             WordNotEqual(
                 WordAnd(LoadMemoryChunkFlags(object),
                         IntPtrConstant(MemoryChunk::IN_WRITABLE_SHARED_SPACE)),
                 IntPtrConstant(0)));
  int const_offset;
  if (TryToInt32Constant(offset, &const_offset)) {
    StoreObjectField(object, const_offset, value);
  } else {
    Store(object, IntPtrSub(offset, IntPtrConstant(kHeapObjectTag)), value);
  }
}

void CodeStubAssembler::StoreMap(TNode<HeapObject> object, TNode<Map> map) {
  OptimizedStoreMap(object, map);
  DcheckHasValidMap(object);
}

void CodeStubAssembler::StoreMapNoWriteBarrier(TNode<HeapObject> object,
                                               RootIndex map_root_index) {
  StoreMapNoWriteBarrier(object, CAST(LoadRoot(map_root_index)));
}

void CodeStubAssembler::StoreMapNoWriteBarrier(TNode<HeapObject> object,
                                               TNode<Map> map) {
  OptimizedStoreMap(object, map);
  DcheckHasValidMap(object);
}

void CodeStubAssembler::StoreObjectFieldRoot(TNode<HeapObject> object,
                                             int offset, RootIndex root_index) {
  TNode<Object> root = LoadRoot(root_index);
  if (offset == HeapObject::kMapOffset) {
    StoreMap(object, CAST(root));
  } else if (RootsTable::IsImmortalImmovable(root_index)) {
    StoreObjectFieldNoWriteBarrier(object, offset, root);
  } else {
    StoreObjectField(object, offset, root);
  }
}

template <typename TIndex>
void CodeStubAssembler::StoreFixedArrayOrPropertyArrayElement(
    TNode<UnionOf<FixedArray, PropertyArray>> object, TNode<TIndex> index_node,
    TNode<Object> value, WriteBarrierMode barrier_mode, int additional_offset) {
  // TODO(v8:9708): Do we want to keep both IntPtrT and UintPtrT variants?
  static_assert(std::is_same<TIndex, Smi>::value ||
                    std::is_same<TIndex, UintPtrT>::value ||
                    std::is_same<TIndex, IntPtrT>::value,
                "Only Smi, UintPtrT or IntPtrT index is allowed");
  DCHECK(barrier_mode == SKIP_WRITE_BARRIER ||
         barrier_mode == UNSAFE_SKIP_WRITE_BARRIER ||
         barrier_mode == UPDATE_WRITE_BARRIER ||
         barrier_mode == UPDATE_EPHEMERON_KEY_WRITE_BARRIER);
  DCHECK(IsAligned(additional_offset, kTaggedSize));
  static_assert(static_cast<int>(FixedArray::kHeaderSize) ==
                static_cast<int>(PropertyArray::kHeaderSize));
  int header_size =
      FixedArray::kHeaderSize + additional_offset - kHeapObjectTag;
  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(index_node, HOLEY_ELEMENTS, header_size);
  static_assert(static_cast<int>(FixedArrayBase::kLengthOffset) ==
                static_cast<int>(WeakFixedArray::kLengthOffset));
  static_assert(static_cast<int>(FixedArrayBase::kLengthOffset) ==
                static_cast<int>(PropertyArray::kLengthAndHashOffset));
  // Check that index_node + additional_offset <= object.length.
  // TODO(cbruni): Use proper LoadXXLength helpers
  CSA_DCHECK(
      this,
      IsOffsetInBounds(
          offset,
          Select<IntPtrT>(
              IsPropertyArray(object),
              [=, this] {
                TNode<Int32T> length_and_hash = LoadAndUntagToWord32ObjectField(
                    object, PropertyArray::kLengthAndHashOffset);
                return Signed(ChangeUint32ToWord(
                    DecodeWord32<PropertyArray::LengthField>(length_and_hash)));
              },
              [=, this] {
                return LoadAndUntagPositiveSmiObjectField(
                    object, FixedArrayBase::kLengthOffset);
              }),
          FixedArray::kHeaderSize));
  if (barrier_mode == SKIP_WRITE_BARRIER) {
    StoreNoWriteBarrier(MachineRepresentation::kTagged, object, offset, value);
  } else if (barrier_mode == UNSAFE_SKIP_WRITE_BARRIER) {
    UnsafeStoreNoWriteBarrier(MachineRepresentation::kTagged, object, offset,
                              value);
  } else if (barrier_mode == UPDATE_EPHEMERON_KEY_WRITE_BARRIER) {
    StoreEphemeronKey(object, offset, value);
  } else {
    Store(object, offset, value);
  }
}

template V8_EXPORT_PRIVATE void
CodeStubAssembler::StoreFixedArrayOrPropertyArrayElement<Smi>(
    TNode<UnionOf<FixedArray, PropertyArray>>, TNode<Smi>, TNode<Object>,
    WriteBarrierMode, int);

template V8_EXPORT_PRIVATE void
CodeStubAssembler::StoreFixedArrayOrPropertyArrayElement<IntPtrT>(
    TNode<UnionOf<FixedArray, PropertyArray>>, TNode<IntPtrT>, TNode<Object>,
    WriteBarrierMode, int);

template V8_EXPORT_PRIVATE void
CodeStubAssembler::StoreFixedArrayOrPropertyArrayElement<UintPtrT>(
    TNode<UnionOf<FixedArray, PropertyArray>>, TNode<UintPtrT>, TNode<Object>,
    WriteBarrierMode, int);

template <typename TIndex>
void CodeStubAssembler::StoreFixedDoubleArrayElement(
    TNode<FixedDoubleArray> object, TNode<TIndex> index, TNode<Float64T> value,
    CheckBounds check_bounds) {
  // TODO(v8:9708): Do we want to keep both IntPtrT and UintPtrT variants?
  static_assert(std::is_same<TIndex, Smi>::value ||
                    std::is_same<TIndex, UintPtrT>::value ||
                    std::is_same<TIndex, IntPtrT>::value,
                "Only Smi, UintPtrT or IntPtrT index is allowed");
  if (NeedsBoundsCheck(check_bounds)) {
    FixedArrayBoundsCheck(object, index, 0);
  }
  TNode<IntPtrT> offset = ElementOffsetFromIndex(
      index, PACKED_DOUBLE_ELEMENTS, FixedArray::kHeaderSize - kHeapObjectTag);
  MachineRepresentation rep = MachineRepresentation::kFloat64;
  // Make sure we do not store signalling NaNs into double arrays.
  TNode<Float64T> value_silenced = Float64SilenceNaN(value);
  StoreNoWriteBarrier(rep, object, offset, value_silenced);
}

// Export the Smi version which is used outside of code-stub-assembler.
template V8_EXPORT_PRIVATE void CodeStubAssembler::StoreFixedDoubleArrayElement<
    Smi>(TNode<FixedDoubleArray>, TNode<Smi>, TNode<Float64T>, CheckBounds);

void CodeStubAssembler::StoreFeedbackVectorSlot(
    TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot,
    TNode<AnyTaggedT> value, WriteBarrierMode barrier_mode,
    int additional_offset) {
  DCHECK(IsAligned(additional_offset, kTaggedSize));
  DCHECK(barrier_mode == SKIP_WRITE_BARRIER ||
         barrier_mode == UNSAFE_SKIP_WRITE_BARRIER ||
         barrier_mode == UPDATE_WRITE_BARRIER);
  int header_size = FeedbackVector::kRawFeedbackSlotsOffset +
                    additional_offset - kHeapObjectTag;
  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(Signed(slot), HOLEY_ELEMENTS, header_size);
  // Check that slot <= feedback_vector.length.
  CSA_DCHECK(this,
             IsOffsetInBounds(offset, LoadFeedbackVectorLength(feedback_vector),
                              FeedbackVector::kHeaderSize),
             SmiFromIntPtr(offset), feedback_vector);
  if (barrier_mode == SKIP_WRITE_BARRIER) {
    StoreNoWriteBarrier(MachineRepresentation::kTagged, feedback_vector, offset,
                        value);
  } else if (barrier_mode == UNSAFE_SKIP_WRITE_BARRIER) {
    UnsafeStoreNoWriteBarrier(MachineRepresentation::kTagged, feedback_vector,
                              offset, value);
  } else {
    Store(feedback_vector, offset, value);
  }
}

TNode<Int32T> CodeStubAssembler::EnsureArrayPushable(TNode<Context> context,
                                                     TNode<Map> map,
                                                     Label* bailout) {
  // Disallow pushing onto prototypes. It might be the JSArray prototype.
  // Disallow pushing onto non-extensible objects.
  Comment("Disallow pushing onto prototypes");
  GotoIfNot(IsExtensibleNonPrototypeMap(map), bailout);

  EnsureArrayLengthWritable(context, map, bailout);

  TNode<Uint32T> kind =
      DecodeWord32<Map::Bits2::ElementsKindBits>(LoadMapBitField2(map));
  return Signed(kind);
}

void CodeStubAssembler::PossiblyGrowElementsCapacity(
    ElementsKind kind, TNode<HeapObject> array, TNode<BInt> length,
    TVariable<FixedArrayBase>* var_elements, TNode<BInt> growth,
    Label* bailout) {
  Label fits(this, var_elements);
  TNode<BInt> capacity =
      TaggedToParameter<BInt>(LoadFixedArrayBaseLength(var_elements->value()));

  TNode<BInt> new_length = IntPtrOrSmiAdd(growth, length);
  GotoIfNot(IntPtrOrSmiGreaterThan(new_length, capacity), &fits);
  TNode<BInt> new_capacity = CalculateNewElementsCapacity(new_length);
  *var_elements = GrowElementsCapacity(array, var_elements->value(), kind, kind,
                                       capacity, new_capacity, bailout);
  Goto(&fits);
  BIND(&fits);
}

TNode<Smi> CodeStubAssembler::BuildAppendJSArray(ElementsKind kind,
                                                 TNode<JSArray> array,
                                                 CodeStubArguments* args,
                                                 TVariable<IntPtrT>* arg_index,
                                                 Label* bailout) {
  Comment("BuildAppendJSArray: ", ElementsKindToString(kind));
  Label pre_bailout(this);
  Label success(this);
  TVARIABLE(Smi, var_tagged_length, LoadFastJSArrayLength(array));
  TVARIABLE(BInt, var_length, SmiToBInt(var_tagged_length.value()));
  TVARIABLE(FixedArrayBase, var_elements, LoadElements(array));

  // Trivial case: no values are being appended.
  // We have this special case here so that callers of this function can assume
  // that there is at least one argument if this function bails out. This may
  // otherwise not be the case if, due to another bug or in-sandbox memory
  // corruption, the JSArray's length is larger than that of its backing
  // FixedArray. In that case, PossiblyGrowElementsCapacity can fail even if no
  // element are to be appended.
  GotoIf(IntPtrEqual(args->GetLengthWithoutReceiver(), IntPtrConstant(0)),
         &success);

  // Resize the capacity of the fixed array if it doesn't fit.
  TNode<IntPtrT> first = arg_index->value();
  TNode<BInt> growth =
      IntPtrToBInt(IntPtrSub(args->GetLengthWithoutReceiver(), first));
  PossiblyGrowElementsCapacity(kind, array, var_length.value(), &var_elements,
                               growth, &pre_bailout);

  // Push each argument onto the end of the array now that there is enough
  // capacity.
  CodeStubAssembler::VariableList push_vars({&var_length}, zone());
  TNode<FixedArrayBase> elements = var_elements.value();
  args->ForEach(
      push_vars,
      [&](TNode<Object> arg) {
        TryStoreArrayElement(kind, &pre_bailout, elements, var_length.value(),
                             arg);
        Increment(&var_length);
      },
      first);
  {
    TNode<Smi> length = BIntToSmi(var_length.value());
    var_tagged_length = length;
    StoreObjectFieldNoWriteBarrier(array, JSArray::kLengthOffset, length);
    Goto(&success);
  }

  BIND(&pre_bailout);
  {
    TNode<Smi> length = ParameterToTagged(var_length.value());
    var_tagged_length = length;
    TNode<Smi> diff = SmiSub(length, LoadFastJSArrayLength(array));
    StoreObjectFieldNoWriteBarrier(array, JSArray::kLengthOffset, length);
    *arg_index = IntPtrAdd(arg_index->value(), SmiUntag(diff));
    Goto(bailout);
  }

  BIND(&success);
  return var_tagged_length.value();
}

void CodeStubAssembler::TryStoreArrayElement(ElementsKind kind, Label* bailout,
                                             TNode<FixedArrayBase> elements,
                                             TNode<BInt> index,
                                             TNode<Object> value) {
  if (IsSmiElementsKind(kind)) {
    GotoIf(TaggedIsNotSmi(value), bailout);
  } else if (IsDoubleElementsKind(kind)) {
    GotoIfNotNumber(value, bailout);
  }

  if (IsDoubleElementsKind(kind)) {
    StoreElement(elements, kind, index, ChangeNumberToFloat64(CAST(value)));
  } else {
    StoreElement(elements, kind, index, value);
  }
}

void CodeStubAssembler::BuildAppendJSArray(ElementsKind kind,
                                           TNode<JSArray> array,
                                           TNode<Object> value,
                                           Label* bailout) {
  Comment("BuildAppendJSArray: ", ElementsKindToString(kind));
  TVARIABLE(BInt, var_length, SmiToBInt(LoadFastJSArrayLength(array)));
  TVARIABLE(FixedArrayBase, var_elements, LoadElements(array));

  // Resize the capacity of the fixed array if it doesn't fit.
  TNode<BInt> growth = IntPtrOrSmiConstant<BInt>(1);
  PossiblyGrowElementsCapacity(kind, array, var_length.value(), &var_elements,
                               growth, bailout);

  // Push each argument onto the end of the array now that there is enough
  // capacity.
  TryStoreArrayElement(kind, bailout, var_elements.value(), var_length.value(),
                       value);
  Increment(&var_length);

  TNode<Smi> length = BIntToSmi(var_length.value());
  StoreObjectFieldNoWriteBarrier(array, JSArray::kLengthOffset, length);
}

TNode<Cell> CodeStubAssembler::AllocateCellWithValue(TNode<Object> value,
                                                     WriteBarrierMode mode) {
  TNode<HeapObject> result = Allocate(Cell::kSize, AllocationFlag::kNone);
  StoreMapNoWriteBarrier(result, RootIndex::kCellMap);
  TNode<Cell> cell = CAST(result);
  StoreCellValue(cell, value, mode);
  return cell;
}

TNode<Object> CodeStubAssembler::LoadCellValue(TNode<Cell> cell) {
  return LoadObjectField(cell, Cell::kValueOffset);
}

void CodeStubAssembler::StoreCellValue(TNode<Cell> cell, TNode<Object> value,
                                       WriteBarrierMode mode) {
  DCHECK(mode == SKIP_WRITE_BARRIER || mode == UPDATE_WRITE_BARRIER);

  if (mode == UPDATE_WRITE_BARRIER) {
    StoreObjectField(cell, Cell::kValueOffset, value);
  } else {
    StoreObjectFieldNoWriteBarrier(cell, Cell::kValueOffset, value);
  }
}

TNode<HeapNumber> CodeStubAssembler::AllocateHeapNumber() {
  TNode<HeapObject> result =
      Allocate(sizeof(HeapNumber), AllocationFlag::kNone);
  RootIndex heap_map_index = RootIndex::kHeapNumberMap;
  StoreMapNoWriteBarrier(result, heap_map_index);
  return UncheckedCast<HeapNumber>(result);
}

TNode<HeapNumber> CodeStubAssembler::AllocateHeapNumberWithValue(
    TNode<Float64T> value) {
  TNode<HeapNumber> result = AllocateHeapNumber();
  StoreHeapNumberValue(result, value);
  return result;
}

TNode<Object> CodeStubAssembler::CloneIfMutablePrimitive(TNode<Object> object) {
  TVARIABLE(Object, result, object);
  Label done(this);

  GotoIf(TaggedIsSmi(object), &done);
  // TODO(leszeks): Read the field descriptor to decide if this heap number is
  // mutable or not.
  GotoIfNot(IsHeapNumber(UncheckedCast<HeapObject>(object)), &done);
  {
    // Mutable heap number found --- allocate a clone.
    TNode<Float64T> value =
        LoadHeapNumberValue(UncheckedCast<HeapNumber>(object));
    result = AllocateHeapNumberWithValue(value);
    Goto(&done);
  }

  BIND(&done);
  return result.value();
}

TNode<BigInt> CodeStubAssembler::AllocateBigInt(TNode<IntPtrT> length) {
  TNode<BigInt> result = AllocateRawBigInt(length);
  StoreBigIntBitfield(result,
                      Word32Shl(TruncateIntPtrToInt32(length),
                                Int32Constant(BigInt::LengthBits::kShift)));
  return result;
}

TNode<BigInt> CodeStubAssembler::AllocateRawBigInt(TNode<IntPtrT> length) {
  TNode<IntPtrT> size =
      IntPtrAdd(IntPtrConstant(sizeof(BigInt)),
                Signed(WordShl(length, kSystemPointerSizeLog2)));
  TNode<HeapObject> raw_result = Allocate(size);
  StoreMapNoWriteBarrier(raw_result, RootIndex::kBigIntMap);
#ifdef BIGINT_NEEDS_PADDING
  static_assert(arraysize(BigInt::padding_) == sizeof(int32_t));
  StoreObjectFieldNoWriteBarrier(raw_result, offsetof(BigInt, padding_),
                                 Int32Constant(0));
#endif
  return UncheckedCast<BigInt>(raw_result);
}

void CodeStubAssembler::StoreBigIntBitfield(TNode<BigInt> bigint,
                                            TNode<Word32T> bitfield) {
  StoreObjectFieldNoWriteBarrier(bigint, offsetof(BigInt, bitfield_), bitfield);
}

void CodeStubAssembler::StoreBigIntDigit(TNode<BigInt> bigint,
                                         intptr_t digit_index,
                                         TNode<UintPtrT> digit) {
  CHECK_LE(0, digit_index);
  CHECK_LT(digit_index, BigInt::kMaxLength);
  StoreObjectFieldNoWriteBarrier(
      bigint,
      OFFSET_OF_DATA_START(BigInt) +
          static_cast<int>(digit_index) * kSystemPointerSize,
      digit);
}

void CodeStubAssembler::StoreBigIntDigit(TNode<BigInt> bigint,
                                         TNode<IntPtrT> digit_index,
                                         TNode<UintPtrT> digit) {
  TNode<IntPtrT> offset =
      IntPtrAdd(IntPtrConstant(OFFSET_OF_DATA_START(BigInt)),
                IntPtrMul(digit_index, IntPtrConstant(kSystemPointerSize)));
  StoreObjectFieldNoWriteBarrier(bigint, offset, digit);
}

TNode<Word32T> CodeStubAssembler::LoadBigIntBitfield(TNode<BigInt> bigint) {
  return UncheckedCast<Word32T>(
      LoadObjectField<Uint32T>(bigint, offsetof(BigInt, bitfield_)));
}

TNode<UintPtrT> CodeStubAssembler::LoadBigIntDigit(TNode<BigInt> bigint,
                                                   intptr_t digit_index) {
  CHECK_LE(0, digit_index);
  CHECK_LT(digit_index, BigInt::kMaxLength);
  return LoadObjectField<UintPtrT>(
      bigint, OFFSET_OF_DATA_START(BigInt) +
                  static_cast<int>(digit_index) * kSystemPointerSize);
}

TNode<UintPtrT> CodeStubAssembler::LoadBigIntDigit(TNode<BigInt> bigint,
                                                   TNode<IntPtrT> digit_index) {
  TNode<IntPtrT> offset =
      IntPtrAdd(IntPtrConstant(OFFSET_OF_DATA_START(BigInt)),
                IntPtrMul(digit_index, IntPtrConstant(kSystemPointerSize)));
  return LoadObjectField<UintPtrT>(bigint, offset);
}

TNode<ByteArray> CodeStubAssembler::AllocateNonEmptyByteArray(
    TNode<UintPtrT> length, AllocationFlags flags) {
  CSA_DCHECK(this, WordNotEqual(length, IntPtrConstant(0)));

  Comment("AllocateNonEmptyByteArray");
  TVARIABLE(Object, var_result);

  TNode<IntPtrT> raw_size =
      GetArrayAllocationSize(Signed(length), UINT8_ELEMENTS,
                             ByteArray::kHeaderSize + kObjectAlignmentMask);
  TNode<IntPtrT> size =
      WordAnd(raw_size, IntPtrConstant(~kObjectAlignmentMask));

  TNode<HeapObject> result = Allocate(size, flags);

  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kByteArrayMap));
  StoreMapNoWriteBarrier(result, RootIndex::kByteArrayMap);
  StoreObjectFieldNoWriteBarrier(result, ByteArray::kLengthOffset,
                                 SmiTag(Signed(length)));

  return CAST(result);
}

TNode<ByteArray> CodeStubAssembler::AllocateByteArray(TNode<UintPtrT> length,
                                                      AllocationFlags flags) {
  // TODO(ishell): unify with AllocateNonEmptyByteArray().

  Comment("AllocateByteArray");
  TVARIABLE(Object, var_result);

  // Compute the ByteArray size and check if it fits into new space.
  Label if_lengthiszero(this), if_sizeissmall(this),
      if_notsizeissmall(this, Label::kDeferred), if_join(this);
  GotoIf(WordEqual(length, UintPtrConstant(0)), &if_lengthiszero);

  TNode<IntPtrT> raw_size =
      GetArrayAllocationSize(Signed(length), UINT8_ELEMENTS,
                             ByteArray::kHeaderSize + kObjectAlignmentMask);
  TNode<IntPtrT> size =
      WordAnd(raw_size, IntPtrConstant(~kObjectAlignmentMask));
  Branch(IntPtrLessThanOrEqual(size, IntPtrConstant(kMaxRegularHeapObjectSize)),
         &if_sizeissmall, &if_notsizeissmall);

  BIND(&if_sizeissmall);
  {
    // Just allocate the ByteArray in new space.
    TNode<HeapObject> result =
        AllocateInNewSpace(UncheckedCast<IntPtrT>(size), flags);
    DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kByteArrayMap));
    StoreMapNoWriteBarrier(result, RootIndex::kByteArrayMap);
    StoreObjectFieldNoWriteBarrier(result, ByteArray::kLengthOffset,
                                   SmiTag(Signed(length)));
    var_result = result;
    Goto(&if_join);
  }

  BIND(&if_notsizeissmall);
  {
    // We might need to allocate in large object space, go to the runtime.
    TNode<Object> result =
        CallRuntime(Runtime::kAllocateByteArray, NoContextConstant(),
                    ChangeUintPtrToTagged(length));
    var_result = result;
    Goto(&if_join);
  }

  BIND(&if_lengthiszero);
  {
    var_result = EmptyByteArrayConstant();
    Goto(&if_join);
  }

  BIND(&if_join);
  return CAST(var_result.value());
}

TNode<String> CodeStubAssembler::AllocateSeqOneByteString(
    uint32_t length, AllocationFlags flags) {
  Comment("AllocateSeqOneByteString");
  if (length == 0) {
    return EmptyStringConstant();
  }
  TNode<HeapObject> result = Allocate(SeqOneByteString::SizeFor(length), flags);
  StoreNoWriteBarrier(MachineRepresentation::kTaggedSigned, result,
                      IntPtrConstant(SeqOneByteString::SizeFor(length) -
                                     kObjectAlignment - kHeapObjectTag),
                      SmiConstant(0));
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kSeqOneByteStringMap));
  StoreMapNoWriteBarrier(result, RootIndex::kSeqOneByteStringMap);
  StoreObjectFieldNoWriteBarrier(result, offsetof(SeqOneByteString, length_),
                                 Uint32Constant(length));
  StoreObjectFieldNoWriteBarrier(result,
                                 offsetof(SeqOneByteString, raw_hash_field_),
                                 Int32Constant(String::kEmptyHashField));
  return CAST(result);
}

TNode<BoolT> CodeStubAssembler::IsZeroOrContext(TNode<Object> object) {
  return Select<BoolT>(
      TaggedEqual(object, SmiConstant(0)),
      [=, this] { return Int32TrueConstant(); },
      [=, this] { return IsContext(CAST(object)); });
}

TNode<String> CodeStubAssembler::AllocateSeqTwoByteString(
    uint32_t length, AllocationFlags flags) {
  Comment("AllocateSeqTwoByteString");
  if (length == 0) {
    return EmptyStringConstant();
  }
  TNode<HeapObject> result = Allocate(SeqTwoByteString::SizeFor(length), flags);
  StoreNoWriteBarrier(MachineRepresentation::kTaggedSigned, result,
                      IntPtrConstant(SeqTwoByteString::SizeFor(length) -
                                     kObjectAlignment - kHeapObjectTag),
                      SmiConstant(0));
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kSeqTwoByteStringMap));
  StoreMapNoWriteBarrier(result, RootIndex::kSeqTwoByteStringMap);
  StoreObjectFieldNoWriteBarrier(result, offsetof(SeqTwoByteString, length_),
                                 Uint32Constant(length));
  StoreObjectFieldNoWriteBarrier(result,
                                 offsetof(SeqTwoByteString, raw_hash_field_),
                                 Int32Constant(String::kEmptyHashField));
  return CAST(result);
}

TNode<String> CodeStubAssembler::AllocateSlicedString(RootIndex map_root_index,
                                                      TNode<Uint32T> length,
                                                      TNode<String> parent,
                                                      TNode<Smi> offset) {
  DCHECK(map_root_index == RootIndex::kSlicedOneByteStringMap ||
         map_root_index == RootIndex::kSlicedTwoByteStringMap);
  TNode<HeapObject> result = Allocate(sizeof(SlicedString));
  DCHECK(RootsTable::IsImmortalImmovable(map_root_index));
  StoreMapNoWriteBarrier(result, map_root_index);
  StoreObjectFieldNoWriteBarrier(result,
                                 offsetof(SlicedString, raw_hash_field_),
                                 Int32Constant(String::kEmptyHashField));
  StoreObjectFieldNoWriteBarrier(result, offsetof(SlicedString, length_),
                                 length);
  StoreObjectFieldNoWriteBarrier(result, offsetof(SlicedString, parent_),
                                 parent);
  StoreObjectFieldNoWriteBarrier(result, offsetof(SlicedString, offset_),
                                 offset);
  return CAST(result);
}

TNode<String> CodeStubAssembler::AllocateSlicedOneByteString(
    TNode<Uint32T> length, TNode<String> parent, TNode<Smi> offset) {
  return AllocateSlicedString(RootIndex::kSlicedOneByteStringMap, length,
                              parent, offset);
}

TNode<String> CodeStubAssembler::AllocateSlicedTwoByteString(
    TNode<Uint32T> length, TNode<String> parent, TNode<Smi> offset) {
  return AllocateSlicedString(RootIndex::kSlicedTwoByteStringMap, length,
                              parent, offset);
}

TNode<NameDictionary> CodeStubAssembler::AllocateNameDictionary(
    int at_least_space_for) {
  return AllocateNameDictionary(IntPtrConstant(at_least_space_for));
}

TNode<NameDictionary> CodeStubAssembler::AllocateNameDictionary(
    TNode<IntPtrT> at_least_space_for, AllocationFlags flags) {
  CSA_DCHECK(this, UintPtrLessThanOrEqual(
                       at_least_space_for,
                       IntPtrConstant(NameDictionary::kMaxCapacity)));
  TNode<IntPtrT> capacity = HashTableComputeCapacity(at_least_space_for);
  return AllocateNameDictionaryWithCapacity(capacity, flags);
}

TNode<NameDictionary> CodeStubAssembler::AllocateNameDictionaryWithCapacity(
    TNode<IntPtrT> capacity, AllocationFlags flags) {
  CSA_DCHECK(this, WordIsPowerOfTwo(capacity));
  CSA_DCHECK(this, IntPtrGreaterThan(capacity, IntPtrConstant(0)));
  TNode<IntPtrT> length = EntryToIndex<NameDictionary>(capacity);
  TNode<IntPtrT> store_size = IntPtrAdd(
      TimesTaggedSize(length), IntPtrConstant(NameDictionary::kHeaderSize));

  TNode<NameDictionary> result =
      UncheckedCast<NameDictionary>(Allocate(store_size, flags));

  // Initialize FixedArray fields.
  {
    DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kNameDictionaryMap));
    StoreMapNoWriteBarrier(result, RootIndex::kNameDictionaryMap);
    StoreObjectFieldNoWriteBarrier(result, FixedArray::kLengthOffset,
                                   SmiFromIntPtr(length));
  }

  // Initialized HashTable fields.
  {
    TNode<Smi> zero = SmiConstant(0);
    StoreFixedArrayElement(result, NameDictionary::kNumberOfElementsIndex, zero,
                           SKIP_WRITE_BARRIER);
    StoreFixedArrayElement(result,
                           NameDictionary::kNumberOfDeletedElementsIndex, zero,
                           SKIP_WRITE_BARRIER);
    StoreFixedArrayElement(result, NameDictionary::kCapacityIndex,
                           SmiTag(capacity), SKIP_WRITE_BARRIER);
    // Initialize Dictionary fields.
    StoreFixedArrayElement(result, NameDictionary::kNextEnumerationIndexIndex,
                           SmiConstant(PropertyDetails::kInitialIndex),
                           SKIP_WRITE_BARRIER);
    StoreFixedArrayElement(result, NameDictionary::kObjectHashIndex,
                           SmiConstant(PropertyArray::kNoHashSentinel),
                           SKIP_WRITE_BARRIER);
    StoreFixedArrayElement(result, NameDictionary::kFlagsIndex,
                           SmiConstant(NameDictionary::kFlagsDefault),
                           SKIP_WRITE_BARRIER);
  }

  // Initialize NameDictionary elements.
  {
    TNode<IntPtrT> result_word = BitcastTaggedToWord(result);
    TNode<IntPtrT> start_address = IntPtrAdd(
        result_word, IntPtrConstant(NameDictionary::OffsetOfElementAt(
                                        NameDictionary::kElementsStartIndex) -
                                    kHeapObjectTag));
    TNode<IntPtrT> end_address = IntPtrAdd(
        result_word, IntPtrSub(store_size, IntPtrConstant(kHeapObjectTag)));

    TNode<Undefined> filler = UndefinedConstant();
    DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kUndefinedValue));

    StoreFieldsNoWriteBarrier(start_address, end_address, filler);
  }

  return result;
}

TNode<PropertyDictionary> CodeStubAssembler::AllocatePropertyDictionary(
    int at_least_space_for) {
  TNode<HeapObject> dict;
  if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    dict = AllocateSwissNameDictionary(at_least_space_for);
  } else {
    dict = AllocateNameDictionary(at_least_space_for);
  }
  return TNode<PropertyDictionary>::UncheckedCast(dict);
}

TNode<PropertyDictionary> CodeStubAssembler::AllocatePropertyDictionary(
    TNode<IntPtrT> at_least_space_for, AllocationFlags flags) {
  TNode<HeapObject> dict;
  if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    dict = AllocateSwissNameDictionary(at_least_space_for);
  } else {
    dict = AllocateNameDictionary(at_least_space_for, flags);
  }
  return TNode<PropertyDictionary>::UncheckedCast(dict);
}

TNode<PropertyDictionary>
CodeStubAssembler::AllocatePropertyDictionaryWithCapacity(
    TNode<IntPtrT> capacity, AllocationFlags flags) {
  TNode<HeapObject> dict;
  if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    dict = AllocateSwissNameDictionaryWithCapacity(capacity);
  } else {
    dict = AllocateNameDictionaryWithCapacity(capacity, flags);
  }
  return TNode<PropertyDictionary>::UncheckedCast(dict);
}

TNode<NameDictionary> CodeStubAssembler::CopyNameDictionary(
    TNode<NameDictionary> dictionary, Label* large_object_fallback) {
  Comment("Copy boilerplate property dict");
  TNode<IntPtrT> capacity =
      PositiveSmiUntag(GetCapacity<NameDictionary>(dictionary));
  CSA_DCHECK(this, IntPtrGreaterThanOrEqual(capacity, IntPtrConstant(0)));
  GotoIf(UintPtrGreaterThan(
             capacity, IntPtrConstant(NameDictionary::kMaxRegularCapacity)),
         large_object_fallback);
  TNode<NameDictionary> properties =
      AllocateNameDictionaryWithCapacity(capacity);
  TNode<IntPtrT> length = LoadAndUntagFixedArrayBaseLength(dictionary);
  CopyFixedArrayElements(PACKED_ELEMENTS, dictionary, properties, length,
                         SKIP_WRITE_BARRIER);
  return properties;
}

template <typename CollectionType>
TNode<CollectionType> CodeStubAssembler::AllocateOrderedHashTable(
    TNode<IntPtrT> capacity) {
  capacity = IntPtrRoundUpToPowerOfTwo32(capacity);
  capacity =
      IntPtrMax(capacity, IntPtrConstant(CollectionType::kInitialCapacity));
  return AllocateOrderedHashTableWithCapacity<CollectionType>(capacity);
}

template <typename CollectionType>
TNode<CollectionType> CodeStubAssembler::AllocateOrderedHashTableWithCapacity(
    TNode<IntPtrT> capacity) {
  CSA_DCHECK(this, WordIsPowerOfTwo(capacity));
  CSA_DCHECK(this,
             IntPtrGreaterThanOrEqual(
                 capacity, IntPtrConstant(CollectionType::kInitialCapacity)));
  CSA_DCHECK(this,
             IntPtrLessThanOrEqual(
                 capacity, IntPtrConstant(CollectionType::MaxCapacity())));

  static_assert(CollectionType::kLoadFactor == 2);
  TNode<IntPtrT> bucket_count = Signed(WordShr(capacity, IntPtrConstant(1)));
  TNode<IntPtrT> data_table_length =
      IntPtrMul(capacity, IntPtrConstant(CollectionType::kEntrySize));

  TNode<IntPtrT> data_table_start_index = IntPtrAdd(
      IntPtrConstant(CollectionType::HashTableStartIndex()), bucket_count);
  TNode<IntPtrT> fixed_array_length =
      IntPtrAdd(data_table_start_index, data_table_length);

  // Allocate the table and add the proper map.
  const ElementsKind elements_kind = HOLEY_ELEMENTS;
  TNode<Map> fixed_array_map =
      HeapConstantNoHole(CollectionType::GetMap(ReadOnlyRoots(isolate())));
  TNode<CollectionType> table =
      CAST(AllocateFixedArray(elements_kind, fixed_array_length,
                              AllocationFlag::kNone, fixed_array_map));

  Comment("Initialize the OrderedHashTable fields.");
  const WriteBarrierMode barrier_mode = SKIP_WRITE_BARRIER;
  UnsafeStoreFixedArrayElement(table, CollectionType::NumberOfElementsIndex(),
                               SmiConstant(0), barrier_mode);
  UnsafeStoreFixedArrayElement(table,
                               CollectionType::NumberOfDeletedElementsIndex(),
                               SmiConstant(0), barrier_mode);
  UnsafeStoreFixedArrayElement(table, CollectionType::NumberOfBucketsIndex(),
                               SmiFromIntPtr(bucket_count), barrier_mode);

  TNode<IntPtrT> object_address = BitcastTaggedToWord(table);

  static_assert(CollectionType::HashTableStartIndex() ==
                CollectionType::NumberOfBucketsIndex() + 1);

  TNode<Smi> not_found_sentinel = SmiConstant(CollectionType::kNotFound);

  intptr_t const_capacity;
  if (TryToIntPtrConstant(capacity, &const_capacity) &&
      const_capacity == CollectionType::kInitialCapacity) {
    int const_bucket_count =
        static_cast<int>(const_capacity / CollectionType::kLoadFactor);
    int const_data_table_length =
        static_cast<int>(const_capacity * CollectionType::kEntrySize);
    int const_data_table_start_index = static_cast<int>(
        CollectionType::HashTableStartIndex() + const_bucket_count);

    Comment("Fill the buckets with kNotFound (constant capacity).");
    for (int i = 0; i < const_bucket_count; i++) {
      UnsafeStoreFixedArrayElement(table,
                                   CollectionType::HashTableStartIndex() + i,
                                   not_found_sentinel, barrier_mode);
    }

    Comment("Fill the data table with undefined (constant capacity).");
    for (int i = 0; i < const_data_table_length; i++) {
      UnsafeStoreFixedArrayElement(table, const_data_table_start_index + i,
                                   UndefinedConstant(), barrier_mode);
    }
  } else {
    Comment("Fill the buckets with kNotFound.");
    TNode<IntPtrT> buckets_start_address =
        IntPtrAdd(object_address,
                  IntPtrConstant(FixedArray::OffsetOfElementAt(
                                     CollectionType::HashTableStartIndex()) -
                                 kHeapObjectTag));
    TNode<IntPtrT> buckets_end_address =
        IntPtrAdd(buckets_start_address, TimesTaggedSize(bucket_count));

    StoreFieldsNoWriteBarrier(buckets_start_address, buckets_end_address,
                              not_found_sentinel);

    Comment("Fill the data table with undefined.");
    TNode<IntPtrT> data_start_address = buckets_end_address;
    TNode<IntPtrT> data_end_address = IntPtrAdd(
        object_address,
        IntPtrAdd(IntPtrConstant(FixedArray::kHeaderSize - kHeapObjectTag),
                  TimesTaggedSize(fixed_array_length)));

    StoreFieldsNoWriteBarrier(data_start_address, data_end_address,
                              UndefinedConstant());

#ifdef DEBUG
    TNode<IntPtrT> ptr_diff =
        IntPtrSub(data_end_address, buckets_start_address);
    TNode<IntPtrT> array_length = LoadAndUntagFixedArrayBaseLength(table);
    TNode<IntPtrT> array_data_fields = IntPtrSub(
        array_length, IntPtrConstant(CollectionType::HashTableStartIndex()));
    TNode<IntPtrT> expected_end =
        IntPtrAdd(data_start_address,
                  TimesTaggedSize(IntPtrMul(
                      capacity, IntPtrConstant(CollectionType::kEntrySize))));

    CSA_DCHECK(this, IntPtrEqual(ptr_diff, TimesTaggedSize(array_data_fields)));
    CSA_DCHECK(this, IntPtrEqual(expected_end, data_end_address));
#endif
  }

  return table;
}

TNode<OrderedNameDictionary> CodeStubAssembler::AllocateOrderedNameDictionary(
    TNode<IntPtrT> capacity) {
  TNode<OrderedNameDictionary> table =
      AllocateOrderedHashTable<OrderedNameDictionary>(capacity);
  StoreFixedArrayElement(table, OrderedNameDictionary::PrefixIndex(),
                         SmiConstant(PropertyArray::kNoHashSentinel),
                         SKIP_WRITE_BARRIER);
  return table;
}

TNode<OrderedNameDictionary> CodeStubAssembler::AllocateOrderedNameDictionary(
    int capacity) {
  return AllocateOrderedNameDictionary(IntPtrConstant(capacity));
}

TNode<OrderedHashSet> CodeStubAssembler::AllocateOrderedHashSet() {
  return AllocateOrderedHashTableWithCapacity<OrderedHashSet>(
      IntPtrConstant(OrderedHashSet::kInitialCapacity));
}

TNode<OrderedHashSet> CodeStubAssembler::AllocateOrderedHashSet(
    TNode<IntPtrT> capacity) {
  return AllocateOrderedHashTableWithCapacity<OrderedHashSet>(capacity);
}

TNode<OrderedHashMap> CodeStubAssembler::AllocateOrderedHashMap() {
  return AllocateOrderedHashTableWithCapacity<OrderedHashMap>(
      IntPtrConstant(OrderedHashMap::kInitialCapacity));
}

TNode<JSObject> CodeStubAssembler::AllocateJSObjectFromMap(
    TNode<Map> map, std::optional<TNode<HeapObject>> properties,
    std::optional<TNode<FixedArray>> elements, AllocationFlags flags,
    SlackTrackingMode slack_tracking_mode) {
  CSA_DCHECK(this, Word32BinaryNot(IsJSFunctionMap(map)));
  CSA_DCHECK(this, Word32BinaryNot(InstanceTypeEqual(LoadMapInstanceType(map),
                                                     JS_GLOBAL_OBJECT_TYPE)));
  TNode<IntPtrT> instance_size =
      TimesTaggedSize(LoadMapInstanceSizeInWords(map));
  TNode<HeapObject> object = AllocateInNewSpace(instance_size, flags);
  StoreMapNoWriteBarrier(object, map);
  InitializeJSObjectFromMap(object, map, instance_size, properties, elements,
                            slack_tracking_mode);
  return CAST(object);
}

void CodeStubAssembler::InitializeJSObjectFromMap(
    TNode<HeapObject> object, TNode<Map> map, TNode<IntPtrT> instance_size,
    std::optional<TNode<HeapObject>> properties,
    std::optional<TNode<FixedArray>> elements,
    SlackTrackingMode slack_tracking_mode) {
  // This helper assumes that the object is in new-space, as guarded by the
  // check in AllocatedJSObjectFromMap.
  if (!properties) {
    CSA_DCHECK(this, Word32BinaryNot(IsDictionaryMap((map))));
    StoreObjectFieldRoot(object, JSObject::kPropertiesOrHashOffset,
                         RootIndex::kEmptyFixedArray);
  } else {
    CSA_DCHECK(this, Word32Or(Word32Or(IsPropertyArray(*properties),
                                       IsPropertyDictionary(*properties)),
                              IsEmptyFixedArray(*properties)));
    StoreObjectFieldNoWriteBarrier(object, JSObject::kPropertiesOrHashOffset,
                                   *properties);
  }
  if (!elements) {
    StoreObjectFieldRoot(object, JSObject::kElementsOffset,
                         RootIndex::kEmptyFixedArray);
  } else {
    StoreObjectFieldNoWriteBarrier(object, JSObject::kElementsOffset,
                                   *elements);
  }
  switch (slack_tracking_mode) {
    case SlackTrackingMode::kDontInitializeInObjectProperties:
      return;
    case kNoSlackTracking:
      return InitializeJSObjectBodyNoSlackTracking(object, map, instance_size);
    case kWithSlackTracking:
      return InitializeJSObjectBodyWithSlackTracking(object, map,
                                                     instance_size);
  }
}

void CodeStubAssembler::InitializeJSObjectBodyNoSlackTracking(
    TNode<HeapObject> object, TNode<Map> map, TNode<IntPtrT> instance_size,
    int start_offset) {
  static_assert(Map::kNoSlackTracking == 0);
  CSA_DCHECK(this, IsClearWord32<Map::Bits3::ConstructionCounterBits>(
                       LoadMapBitField3(map)));
  InitializeFieldsWithRoot(object, IntPtrConstant(start_offset), instance_size,
                           RootIndex::kUndefinedValue);
}

void CodeStubAssembler::InitializeJSObjectBodyWithSlackTracking(
    TNode<HeapObject> object, TNode<Map> map, TNode<IntPtrT> instance_size) {
  Comment("InitializeJSObjectBodyNoSlackTracking");

  // Perform in-object slack tracking if requested.
  int start_offset = JSObject::kHeaderSize;
  TNode<Uint32T> bit_field3 = LoadMapBitField3(map);
  Label end(this), slack_tracking(this), complete(this, Label::kDeferred);
  static_assert(Map::kNoSlackTracking == 0);
  GotoIf(IsSetWord32<Map::Bits3::ConstructionCounterBits>(bit_field3),
         &slack_tracking);
  Comment("No slack tracking");
  InitializeJSObjectBodyNoSlackTracking(object, map, instance_size);
  Goto(&end);

  BIND(&slack_tracking);
  {
    Comment("Decrease construction counter");
    // Slack tracking is only done on initial maps.
    CSA_DCHECK(this, IsUndefined(LoadMapBackPointer(map)));
    static_assert(Map::Bits3::ConstructionCounterBits::kLastUsedBit == 31);
    TNode<Word32T> new_bit_field3 = Int32Sub(
        bit_field3,
        Int32Constant(1 << Map::Bits3::ConstructionCounterBits::kShift));

    // The object still has in-object slack therefore the |unsed_or_unused|
    // field contain the "used" value.
    TNode<IntPtrT> used_size =
        Signed(TimesTaggedSize(ChangeUint32ToWord(LoadObjectField<Uint8T>(
            map, Map::kUsedOrUnusedInstanceSizeInWordsOffset))));

    Comment("Initialize filler fields");
    InitializeFieldsWithRoot(object, used_size, instance_size,
                             RootIndex::kOnePointerFillerMap);

    Comment("Initialize undefined fields");
    InitializeFieldsWithRoot(object, IntPtrConstant(start_offset), used_size,
                             RootIndex::kUndefinedValue);

    static_assert(Map::kNoSlackTracking == 0);
    GotoIf(IsClearWord32<Map::Bits3::ConstructionCounterBits>(new_bit_field3),
           &complete);

    // Setting ConstructionCounterBits to 0 requires taking the
    // map_updater_access mutex, which we can't do from CSA, so we only manually
    // update ConstructionCounterBits when its result is non-zero; otherwise we
    // let the runtime do it (with the GotoIf right above this comment).
    StoreObjectFieldNoWriteBarrier(map, Map::kBitField3Offset, new_bit_field3);
    static_assert(Map::kSlackTrackingCounterEnd == 1);

    Goto(&end);
  }

  // Finalize the instance size.
  BIND(&complete);
  {
    // ComplextInobjectSlackTracking doesn't allocate and thus doesn't need a
    // context.
    CallRuntime(Runtime::kCompleteInobjectSlackTrackingForMap,
                NoContextConstant(), map);
    Goto(&end);
  }

  BIND(&end);
}

void CodeStubAssembler::StoreFieldsNoWriteBarrier(TNode<IntPtrT> start_address,
                                                  TNode<IntPtrT> end_address,
                                                  TNode<Object> value) {
  Comment("StoreFieldsNoWriteBarrier");
  CSA_DCHECK(this, WordIsAligned(start_address, kTaggedSize));
  CSA_DCHECK(this, WordIsAligned(end_address, kTaggedSize));
  BuildFastLoop<IntPtrT>(
      start_address, end_address,
      [=, this](TNode<IntPtrT> current) {
        UnsafeStoreNoWriteBarrier(MachineRepresentation::kTagged, current,
                                  value);
      },
      kTaggedSize, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
}

void CodeStubAssembler::MakeFixedArrayCOW(TNode<FixedArray> array) {
  CSA_DCHECK(this, IsFixedArrayMap(LoadMap(array)));
  Label done(this);
  // The empty fixed array is not modifiable anyway. And we shouldn't change its
  // Map.
  GotoIf(TaggedEqual(array, EmptyFixedArrayConstant()), &done);
  StoreMap(array, FixedCOWArrayMapConstant());
  Goto(&done);
  BIND(&done);
}

TNode<BoolT> CodeStubAssembler::IsValidFastJSArrayCapacity(
    TNode<IntPtrT> capacity) {
  return UintPtrLessThanOrEqual(capacity,
                                UintPtrConstant(JSArray::kMaxFastArrayLength));
}

TNode<JSArray> CodeStubAssembler::AllocateJSArray(
    TNode<Map> array_map, TNode<FixedArrayBase> elements, TNode<Smi> length,
    std::optional<TNode<AllocationSite>> allocation_site,
    int array_header_size) {
  Comment("begin allocation of JSArray passing in elements");
  CSA_SLOW_DCHECK(this, TaggedIsPositiveSmi(length));

  int base_size = array_header_size;
  if (allocation_site) {
    DCHECK(V8_ALLOCATION_SITE_TRACKING_BOOL);
    base_size += ALIGN_TO_ALLOCATION_ALIGNMENT(AllocationMemento::kSize);
  }

  TNode<IntPtrT> size = IntPtrConstant(base_size);
  TNode<JSArray> result =
      AllocateUninitializedJSArray(array_map, length, allocation_site, size);
  StoreObjectFieldNoWriteBarrier(result, JSArray::kElementsOffset, elements);
  return result;
}

namespace {

// To prevent GC between the array and elements allocation, the elements
// object allocation is folded together with the js-array allocation.
TNode<FixedArrayBase> InnerAllocateElements(CodeStubAssembler* csa,
                                            TNode<JSArray> js_array,
                                            int offset) {
  return csa->UncheckedCast<FixedArrayBase>(
      csa->BitcastWordToTagged(csa->IntPtrAdd(
          csa->BitcastTaggedToWord(js_array), csa->IntPtrConstant(offset))));
}

}  // namespace

TNode<IntPtrT> CodeStubAssembler::AlignToAllocationAlignment(
    TNode<IntPtrT> value) {
  if (!V8_COMPRESS_POINTERS_8GB_BOOL) return value;

  Label not_aligned(this), is_aligned(this);
  TVARIABLE(IntPtrT, result, value);

  Branch(WordIsAligned(value, kObjectAlignment8GbHeap), &is_aligned,
         &not_aligned);

  BIND(&not_aligned);
  {
    if (kObjectAlignment8GbHeap == 2 * kTaggedSize) {
      result = IntPtrAdd(value, IntPtrConstant(kTaggedSize));
    } else {
      result =
          WordAnd(IntPtrAdd(value, IntPtrConstant(kObjectAlignment8GbHeapMask)),
                  IntPtrConstant(~kObjectAlignment8GbHeapMask));
    }
    Goto(&is_aligned);
  }

  BIND(&is_aligned);
  return result.value();
}

std::pair<TNode<JSArray>, TNode<FixedArrayBase>>
CodeStubAssembler::AllocateUninitializedJSArrayWithElements(
    ElementsKind kind, TNode<Map> array_map, TNode<Smi> length,
    std::optional<TNode<AllocationSite>> allocation_site,
    TNode<IntPtrT> capacity, AllocationFlags allocation_flags,
    int array_header_size) {
  Comment("begin allocation of JSArray with elements");
  CSA_SLOW_DCHECK(this, TaggedIsPositiveSmi(length));

  TVARIABLE(JSArray, array);
  TVARIABLE(FixedArrayBase, elements);

  Label out(this), empty(this), nonempty(this);

  int capacity_int;
  if (TryToInt32Constant(capacity, &capacity_int)) {
    if (capacity_int == 0) {
      TNode<FixedArray> empty_array = EmptyFixedArrayConstant();
      array = AllocateJSArray(array_map, empty_array, length, allocation_site,
                              array_header_size);
      return {array.value(), empty_array};
    } else {
      Goto(&nonempty);
    }
  } else {
    Branch(WordEqual(capacity, IntPtrConstant(0)), &empty, &nonempty);

    BIND(&empty);
    {
      TNode<FixedArray> empty_array = EmptyFixedArrayConstant();
      array = AllocateJSArray(array_map, empty_array, length, allocation_site,
                              array_header_size);
      elements = empty_array;
      Goto(&out);
    }
  }

  BIND(&nonempty);
  {
    int base_size = ALIGN_TO_ALLOCATION_ALIGNMENT(array_header_size);
    if (allocation_site) {
      DCHECK(V8_ALLOCATION_SITE_TRACKING_BOOL);
      base_size += ALIGN_TO_ALLOCATION_ALIGNMENT(AllocationMemento::kSize);
    }

    const int elements_offset = base_size;

    // Compute space for elements
    base_size += FixedArray::kHeaderSize;
    TNode<IntPtrT> size = AlignToAllocationAlignment(
        ElementOffsetFromIndex(capacity, kind, base_size));

    // For very large arrays in which the requested allocation exceeds the
    // maximal size of a regular heap object, we cannot use the allocation
    // folding trick. Instead, we first allocate the elements in large object
    // space, and then allocate the JSArray (and possibly the allocation
    // memento) in new space.
    Label next(this);
    GotoIf(IsRegularHeapObjectSize(size), &next);

    CSA_CHECK(this, IsValidFastJSArrayCapacity(capacity));

    // Allocate and initialize the elements first. Full initialization is
    // needed because the upcoming JSArray allocation could trigger GC.
    elements = AllocateFixedArray(kind, capacity, allocation_flags);

    if (IsDoubleElementsKind(kind)) {
      FillEntireFixedDoubleArrayWithZero(CAST(elements.value()), capacity);
    } else {
      FillEntireFixedArrayWithSmiZero(kind, CAST(elements.value()), capacity);
    }

    // The JSArray and possibly allocation memento next. Note that
    // allocation_flags are *not* passed on here and the resulting JSArray
    // will always be in new space.
    array = AllocateJSArray(array_map, elements.value(), length,
                            allocation_site, array_header_size);

    Goto(&out);

    BIND(&next);

    // Fold all objects into a single new space allocation.
    array =
        AllocateUninitializedJSArray(array_map, length, allocation_site, size);
    elements = InnerAllocateElements(this, array.value(), elements_offset);

    StoreObjectFieldNoWriteBarrier(array.value(), JSObject::kElementsOffset,
                                   elements.value());

    // Setup elements object.
    static_assert(FixedArrayBase::kHeaderSize == 2 * kTaggedSize);
    RootIndex elements_map_index = IsDoubleElementsKind(kind)
                                       ? RootIndex::kFixedDoubleArrayMap
                                       : RootIndex::kFixedArrayMap;
    DCHECK(RootsTable::IsImmortalImmovable(elements_map_index));
    StoreMapNoWriteBarrier(elements.value(), elements_map_index);

    CSA_DCHECK(this, WordNotEqual(capacity, IntPtrConstant(0)));
    TNode<Smi> capacity_smi = SmiTag(capacity);
    StoreObjectFieldNoWriteBarrier(elements.value(), FixedArray::kLengthOffset,
                                   capacity_smi);
    Goto(&out);
  }

  BIND(&out);
  return {array.value(), elements.value()};
}

TNode<JSArray> CodeStubAssembler::AllocateUninitializedJSArray(
    TNode<Map> array_map, TNode<Smi> length,
    std::optional<TNode<AllocationSite>> allocation_site,
    TNode<IntPtrT> size_in_bytes) {
  CSA_SLOW_DCHECK(this, TaggedIsPositiveSmi(length));

  // Allocate space for the JSArray and the elements FixedArray in one go.
  TNode<HeapObject> array = AllocateInNewSpace(size_in_bytes);

  StoreMapNoWriteBarrier(array, array_map);
  StoreObjectFieldNoWriteBarrier(array, JSArray::kLengthOffset, length);
  StoreObjectFieldRoot(array, JSArray::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);

  if (allocation_site) {
    DCHECK(V8_ALLOCATION_SITE_TRACKING_BOOL);
    InitializeAllocationMemento(
        array,
        IntPtrConstant(ALIGN_TO_ALLOCATION_ALIGNMENT(JSArray::kHeaderSize)),
        *allocation_site);
  }

  return CAST(array);
}

TNode<JSArray> CodeStubAssembler::AllocateJSArray(
    ElementsKind kind, TNode<Map> array_map, TNode<IntPtrT> capacity,
    TNode<Smi> length, std::optional<TNode<AllocationSite>> allocation_site,
    AllocationFlags allocation_flags) {
  CSA_SLOW_DCHECK(this, TaggedIsPositiveSmi(length));

  TNode<JSArray> array;
  TNode<FixedArrayBase> elements;

  std::tie(array, elements) = AllocateUninitializedJSArrayWithElements(
      kind, array_map, length, allocation_site, capacity, allocation_flags);

  Label out(this), nonempty(this);

  Branch(WordEqual(capacity, IntPtrConstant(0)), &out, &nonempty);

  BIND(&nonempty);
  {
    FillFixedArrayWithValue(kind, elements, IntPtrConstant(0), capacity,
                            RootIndex::kTheHoleValue);
    Goto(&out);
  }

  BIND(&out);
  return array;
}

TNode<JSArray> CodeStubAssembler::ExtractFastJSArray(TNode<Context> context,
                                                     TNode<JSArray> array,
                                                     TNode<BInt> begin,
                                                     TNode<BInt> count) {
  TNode<Map> original_array_map = LoadMap(array);
  TNode<Int32T> elements_kind = LoadMapElementsKind(original_array_map);

  // Use the canonical map for the Array's ElementsKind
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> array_map = LoadJSArrayElementsMap(elements_kind, native_context);

  TNode<FixedArrayBase> new_elements = ExtractFixedArray(
      LoadElements(array), std::optional<TNode<BInt>>(begin),
      std::optional<TNode<BInt>>(count),
      std::optional<TNode<BInt>>(std::nullopt),
      ExtractFixedArrayFlag::kAllFixedArrays, nullptr, elements_kind);

  TNode<JSArray> result = AllocateJSArray(
      array_map, new_elements, ParameterToTagged(count), std::nullopt);
  return result;
}

TNode<JSArray> CodeStubAssembler::CloneFastJSArray(
    TNode<Context> context, TNode<JSArray> array,
    std::optional<TNode<AllocationSite>> allocation_site,
    HoleConversionMode convert_holes) {
  // TODO(dhai): we should be able to assert IsFastJSArray(array) here, but this
  // function is also used to copy boilerplates even when the no-elements
  // protector is invalid. This function should be renamed to reflect its uses.

  TNode<Number> length = LoadJSArrayLength(array);
  TNode<FixedArrayBase> new_elements;
  TVARIABLE(FixedArrayBase, var_new_elements);
  TVARIABLE(Int32T, var_elements_kind, LoadMapElementsKind(LoadMap(array)));

  Label allocate_jsarray(this), holey_extract(this),
      allocate_jsarray_main(this);

  bool need_conversion =
      convert_holes == HoleConversionMode::kConvertToUndefined;
  if (need_conversion) {
    // We need to take care of holes, if the array is of holey elements kind.
    GotoIf(IsHoleyFastElementsKindForRead(var_elements_kind.value()),
           &holey_extract);
  }

  // Simple extraction that preserves holes.
  new_elements = ExtractFixedArray(
      LoadElements(array),
      std::optional<TNode<BInt>>(IntPtrOrSmiConstant<BInt>(0)),
      std::optional<TNode<BInt>>(TaggedToParameter<BInt>(CAST(length))),
      std::optional<TNode<BInt>>(std::nullopt),
      ExtractFixedArrayFlag::kAllFixedArraysDontCopyCOW, nullptr,
      var_elements_kind.value());
  var_new_elements = new_elements;
  Goto(&allocate_jsarray);

  if (need_conversion) {
    BIND(&holey_extract);
    // Convert holes to undefined.
    TVARIABLE(BoolT, var_holes_converted, Int32FalseConstant());
    // Copy |array|'s elements store. The copy will be compatible with the
    // original elements kind unless there are holes in the source. Any holes
    // get converted to undefined, hence in that case the copy is compatible
    // only with PACKED_ELEMENTS and HOLEY_ELEMENTS, and we will choose
    // PACKED_ELEMENTS. Also, if we want to replace holes, we must not use
    // ExtractFixedArrayFlag::kDontCopyCOW.
    new_elements = ExtractFixedArray(
        LoadElements(array),
        std::optional<TNode<BInt>>(IntPtrOrSmiConstant<BInt>(0)),
        std::optional<TNode<BInt>>(TaggedToParameter<BInt>(CAST(length))),
        std::optional<TNode<BInt>>(std::nullopt),
        ExtractFixedArrayFlag::kAllFixedArrays, &var_holes_converted);
    var_new_elements = new_elements;
    // If the array type didn't change, use the original elements kind.
    GotoIfNot(var_holes_converted.value(), &allocate_jsarray);
    // Otherwise use PACKED_ELEMENTS for the target's elements kind.
    var_elements_kind = Int32Constant(PACKED_ELEMENTS);
    Goto(&allocate_jsarray);
  }

  BIND(&allocate_jsarray);

  // Handle any nonextensible elements kinds
  CSA_DCHECK(this, IsElementsKindLessThanOrEqual(
                       var_elements_kind.value(),
                       LAST_ANY_NONEXTENSIBLE_ELEMENTS_KIND));
  GotoIf(IsElementsKindLessThanOrEqual(var_elements_kind.value(),
                                       LAST_FAST_ELEMENTS_KIND),
         &allocate_jsarray_main);
  var_elements_kind = Int32Constant(PACKED_ELEMENTS);
  Goto(&allocate_jsarray_main);

  BIND(&allocate_jsarray_main);
  // Use the cannonical map for the chosen elements kind.
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> array_map =
      LoadJSArrayElementsMap(var_elements_kind.value(), native_context);

  TNode<JSArray> result = AllocateJSArray(array_map, var_new_elements.value(),
                                          CAST(length), allocation_site);
  return result;
}

template <typename TIndex>
TNode<FixedArrayBase> CodeStubAssembler::AllocateFixedArray(
    ElementsKind kind, TNode<TIndex> capacity, AllocationFlags flags,
    std::optional<TNode<Map>> fixed_array_map) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT capacity is allowed");
  Comment("AllocateFixedArray");
  CSA_DCHECK(this,
             IntPtrOrSmiGreaterThan(capacity, IntPtrOrSmiConstant<TIndex>(0)));

  const intptr_t kMaxLength = IsDoubleElementsKind(kind)
                                  ? FixedDoubleArray::kMaxLength
                                  : FixedArray::kMaxLength;
  intptr_t capacity_constant;
  if (ToParameterConstant(capacity, &capacity_constant)) {
    CHECK_LE(capacity_constant, kMaxLength);
  } else {
    Label if_out_of_memory(this, Label::kDeferred), next(this);
    Branch(IntPtrOrSmiGreaterThan(capacity, IntPtrOrSmiConstant<TIndex>(
                                                static_cast<int>(kMaxLength))),
           &if_out_of_memory, &next);

    BIND(&if_out_of_memory);
    CallRuntime(Runtime::kFatalProcessOutOfMemoryInvalidArrayLength,
                NoContextConstant());
    Unreachable();

    BIND(&next);
  }

  TNode<IntPtrT> total_size = GetFixedArrayAllocationSize(capacity, kind);

  if (IsDoubleElementsKind(kind)) flags |= AllocationFlag::kDoubleAlignment;
  TNode<HeapObject> array = Allocate(total_size, flags);
  if (fixed_array_map) {
    // Conservatively only skip the write barrier if there are no allocation
    // flags, this ensures that the object hasn't ended up in LOS. Note that the
    // fixed array map is currently always immortal and technically wouldn't
    // need the write barrier even in LOS, but it's better to not take chances
    // in case this invariant changes later, since it's difficult to enforce
    // locally here.
    if (flags == AllocationFlag::kNone) {
      StoreMapNoWriteBarrier(array, *fixed_array_map);
    } else {
      StoreMap(array, *fixed_array_map);
    }
  } else {
    RootIndex map_index = IsDoubleElementsKind(kind)
                              ? RootIndex::kFixedDoubleArrayMap
                              : RootIndex::kFixedArrayMap;
    DCHECK(RootsTable::IsImmortalImmovable(map_index));
    StoreMapNoWriteBarrier(array, map_index);
  }
  StoreObjectFieldNoWriteBarrier(array, FixedArrayBase::kLengthOffset,
                                 ParameterToTagged(capacity));
  return UncheckedCast<FixedArrayBase>(array);
}

// There is no need to export the Smi version since it is only used inside
// code-stub-assembler.
template V8_EXPORT_PRIVATE TNode<FixedArrayBase>
    CodeStubAssembler::AllocateFixedArray<IntPtrT>(ElementsKind, TNode<IntPtrT>,
                                                   AllocationFlags,
                                                   std::optional<TNode<Map>>);

template <typename TIndex>
TNode<FixedArray> CodeStubAssembler::ExtractToFixedArray(
    TNode<FixedArrayBase> source, TNode<TIndex> first, TNode<TIndex> count,
    TNode<TIndex> capacity, TNode<Map> source_map, ElementsKind from_kind,
    AllocationFlags allocation_flags, ExtractFixedArrayFlags extract_flags,
    HoleConversionMode convert_holes, TVariable<BoolT>* var_holes_converted,
    std::optional<TNode<Int32T>> source_elements_kind) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT first, count, and capacity are allowed");

  DCHECK(extract_flags & ExtractFixedArrayFlag::kFixedArrays);
  CSA_DCHECK(this,
             IntPtrOrSmiNotEqual(IntPtrOrSmiConstant<TIndex>(0), capacity));
  CSA_DCHECK(this, TaggedEqual(source_map, LoadMap(source)));

  TVARIABLE(FixedArrayBase, var_result);
  TVARIABLE(Map, var_target_map, source_map);

  Label done(this, {&var_result}), is_cow(this),
      new_space_handler(this, {&var_target_map});

  // If source_map is either FixedDoubleArrayMap, or FixedCOWArrayMap but
  // we can't just use COW, use FixedArrayMap as the target map. Otherwise, use
  // source_map as the target map.
  if (IsDoubleElementsKind(from_kind)) {
    CSA_DCHECK(this, IsFixedDoubleArrayMap(source_map));
    var_target_map = FixedArrayMapConstant();
    Goto(&new_space_handler);
  } else {
    CSA_DCHECK(this, Word32BinaryNot(IsFixedDoubleArrayMap(source_map)));
    Branch(TaggedEqual(var_target_map.value(), FixedCOWArrayMapConstant()),
           &is_cow, &new_space_handler);

    BIND(&is_cow);
    {
      // |source| is a COW array, so we don't actually need to allocate a new
      // array unless:
      // 1) |extract_flags| forces us to, or
      // 2) we're asked to extract only part of the |source| (|first| != 0).
      if (extract_flags & ExtractFixedArrayFlag::kDontCopyCOW) {
        Branch(IntPtrOrSmiNotEqual(IntPtrOrSmiConstant<TIndex>(0), first),
               &new_space_handler, [&] {
                 var_result = source;
                 Goto(&done);
               });
      } else {
        var_target_map = FixedArrayMapConstant();
        Goto(&new_space_handler);
      }
    }
  }

  BIND(&new_space_handler);
  {
    Comment("Copy FixedArray in young generation");
    // We use PACKED_ELEMENTS to tell AllocateFixedArray and
    // CopyFixedArrayElements that we want a FixedArray.
    const ElementsKind to_kind = PACKED_ELEMENTS;
    TNode<FixedArrayBase> to_elements = AllocateFixedArray(
        to_kind, capacity, allocation_flags, var_target_map.value());
    var_result = to_elements;

#if !defined(V8_ENABLE_SINGLE_GENERATION) && !V8_ENABLE_STICKY_MARK_BITS_BOOL
#ifdef DEBUG
    TNode<IntPtrT> object_word = BitcastTaggedToWord(to_elements);
    TNode<IntPtrT> object_page_header = MemoryChunkFromAddress(object_word);
    TNode<IntPtrT> page_flags = Load<IntPtrT>(
        object_page_header, IntPtrConstant(MemoryChunkLayout::kFlagsOffset));
    CSA_DCHECK(
        this,
        WordNotEqual(
            WordAnd(page_flags,
                    IntPtrConstant(MemoryChunk::kIsInYoungGenerationMask)),
            IntPtrConstant(0)));
#endif
#endif

    if (convert_holes == HoleConversionMode::kDontConvert &&
        !IsDoubleElementsKind(from_kind)) {
      // We can use CopyElements (memcpy) because we don't need to replace or
      // convert any values. Since {to_elements} is in new-space, CopyElements
      // will efficiently use memcpy.
      FillFixedArrayWithValue(to_kind, to_elements, count, capacity,
                              RootIndex::kTheHoleValue);
      CopyElements(to_kind, to_elements, IntPtrConstant(0), source,
                   ParameterToIntPtr(first), ParameterToIntPtr(count),
                   SKIP_WRITE_BARRIER);
    } else {
      CopyFixedArrayElements(from_kind, source, to_kind, to_elements, first,
                             count, capacity, SKIP_WRITE_BARRIER, convert_holes,
                             var_holes_converted);
    }
    Goto(&done);
  }

  BIND(&done);
  return UncheckedCast<FixedArray>(var_result.value());
}

template <typename TIndex>
TNode<FixedArrayBase> CodeStubAssembler::ExtractFixedDoubleArrayFillingHoles(
    TNode<FixedArrayBase> from_array, TNode<TIndex> first, TNode<TIndex> count,
    TNode<TIndex> capacity, TNode<Map> fixed_array_map,
    TVariable<BoolT>* var_holes_converted, AllocationFlags allocation_flags,
    ExtractFixedArrayFlags extract_flags) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT first, count, and capacity are allowed");

  DCHECK_NE(var_holes_converted, nullptr);
  CSA_DCHECK(this, IsFixedDoubleArrayMap(fixed_array_map));

  TVARIABLE(FixedArrayBase, var_result);
  const ElementsKind kind = PACKED_DOUBLE_ELEMENTS;
  TNode<FixedArrayBase> to_elements =
      AllocateFixedArray(kind, capacity, allocation_flags, fixed_array_map);
  var_result = to_elements;
  // We first try to copy the FixedDoubleArray to a new FixedDoubleArray.
  // |var_holes_converted| is set to False preliminarily.
  *var_holes_converted = Int32FalseConstant();

  // The construction of the loop and the offsets for double elements is
  // extracted from CopyFixedArrayElements.
  CSA_SLOW_DCHECK(this, IsFixedArrayWithKindOrEmpty(from_array, kind));
  static_assert(FixedArray::kHeaderSize == FixedDoubleArray::kHeaderSize);

  Comment("[ ExtractFixedDoubleArrayFillingHoles");

  // This copy can trigger GC, so we pre-initialize the array with holes.
  FillFixedArrayWithValue(kind, to_elements, IntPtrOrSmiConstant<TIndex>(0),
                          capacity, RootIndex::kTheHoleValue);

  const int first_element_offset = FixedArray::kHeaderSize - kHeapObjectTag;
  TNode<IntPtrT> first_from_element_offset =
      ElementOffsetFromIndex(first, kind, 0);
  TNode<IntPtrT> limit_offset = IntPtrAdd(first_from_element_offset,
                                          IntPtrConstant(first_element_offset));
  TVARIABLE(IntPtrT, var_from_offset,
            ElementOffsetFromIndex(IntPtrOrSmiAdd(first, count), kind,
                                   first_element_offset));

  Label decrement(this, {&var_from_offset}), done(this);
  TNode<IntPtrT> to_array_adjusted =
      IntPtrSub(BitcastTaggedToWord(to_elements), first_from_element_offset);

  Branch(WordEqual(var_from_offset.value(), limit_offset), &done, &decrement);

  BIND(&decrement);
  {
    TNode<IntPtrT> from_offset =
        IntPtrSub(var_from_offset.value(), IntPtrConstant(kDoubleSize));
    var_from_offset = from_offset;

    TNode<IntPtrT> to_offset = from_offset;

    Label if_hole(this);

    TNode<Float64T> value = LoadDoubleWithHoleCheck(
        from_array, var_from_offset.value(), &if_hole, MachineType::Float64());

    StoreNoWriteBarrier(MachineRepresentation::kFloat64, to_array_adjusted,
                        to_offset, value);

    TNode<BoolT> compare = WordNotEqual(from_offset, limit_offset);
    Branch(compare, &decrement, &done);

    BIND(&if_hole);
    // We are unlucky: there are holes! We need to restart the copy, this time
    // we will copy the FixedDoubleArray to a new FixedArray with undefined
    // replacing holes. We signal this to the caller through
    // |var_holes_converted|.
    *var_holes_converted = Int32TrueConstant();
    to_elements =
        ExtractToFixedArray(from_array, first, count, capacity, fixed_array_map,
                            kind, allocation_flags, extract_flags,
                            HoleConversionMode::kConvertToUndefined);
    var_result = to_elements;
    Goto(&done);
  }

  BIND(&done);
  Comment("] ExtractFixedDoubleArrayFillingHoles");
  return var_result.value();
}

template <typename TIndex>
TNode<FixedArrayBase> CodeStubAssembler::ExtractFixedArray(
    TNode<FixedArrayBase> source, std::optional<TNode<TIndex>> first,
    std::optional<TNode<TIndex>> count, std::optional<TNode<TIndex>> capacity,
    ExtractFixedArrayFlags extract_flags, TVariable<BoolT>* var_holes_converted,
    std::optional<TNode<Int32T>> source_elements_kind) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT first, count, and capacity are allowed");
  DCHECK(extract_flags & ExtractFixedArrayFlag::kFixedArrays ||
         extract_flags & ExtractFixedArrayFlag::kFixedDoubleArrays);
  // If we want to replace holes, ExtractFixedArrayFlag::kDontCopyCOW should
  // not be used, because that disables the iteration which detects holes.
  DCHECK_IMPLIES(var_holes_converted != nullptr,
                 !(extract_flags & ExtractFixedArrayFlag::kDontCopyCOW));
  HoleConversionMode convert_holes =
      var_holes_converted != nullptr ? HoleConversionMode::kConvertToUndefined
                                     : HoleConversionMode::kDontConvert;
  TVARIABLE(FixedArrayBase, var_result);
  auto allocation_flags = AllocationFlag::kNone;
  if (!first) {
    first = IntPtrOrSmiConstant<TIndex>(0);
  }
  if (!count) {
    count = IntPtrOrSmiSub(
        TaggedToParameter<TIndex>(LoadFixedArrayBaseLength(source)), *first);

    CSA_DCHECK(this, IntPtrOrSmiLessThanOrEqual(IntPtrOrSmiConstant<TIndex>(0),
                                                *count));
  }
  if (!capacity) {
    capacity = *count;
  } else {
    CSA_DCHECK(this, Word32BinaryNot(IntPtrOrSmiGreaterThan(
                         IntPtrOrSmiAdd(*first, *count), *capacity)));
  }

  Label if_fixed_double_array(this), empty(this), done(this, &var_result);
  TNode<Map> source_map = LoadMap(source);
  GotoIf(IntPtrOrSmiEqual(IntPtrOrSmiConstant<TIndex>(0), *capacity), &empty);

  if (extract_flags & ExtractFixedArrayFlag::kFixedDoubleArrays) {
    if (extract_flags & ExtractFixedArrayFlag::kFixedArrays) {
      GotoIf(IsFixedDoubleArrayMap(source_map), &if_fixed_double_array);
    } else {
      CSA_DCHECK(this, IsFixedDoubleArrayMap(source_map));
    }
  }

  if (extract_flags & ExtractFixedArrayFlag::kFixedArrays) {
    // Here we can only get |source| as FixedArray, never FixedDoubleArray.
    // PACKED_ELEMENTS is used to signify that the source is a FixedArray.
    TNode<FixedArray> to_elements = ExtractToFixedArray(
        source, *first, *count, *capacity, source_map, PACKED_ELEMENTS,
        allocation_flags, extract_flags, convert_holes, var_holes_converted,
        source_elements_kind);
    var_result = to_elements;
    Goto(&done);
  }

  if (extract_flags & ExtractFixedArrayFlag::kFixedDoubleArrays) {
    BIND(&if_fixed_double_array);
    Comment("Copy FixedDoubleArray");

    if (convert_holes == HoleConversionMode::kConvertToUndefined) {
      TNode<FixedArrayBase> to_elements = ExtractFixedDoubleArrayFillingHoles(
          source, *first, *count, *capacity, source_map, var_holes_converted,
          allocation_flags, extract_flags);
      var_result = to_elements;
    } else {
      // We use PACKED_DOUBLE_ELEMENTS to signify that both the source and
      // the target are FixedDoubleArray. That it is PACKED or HOLEY does not
      // matter.
      ElementsKind kind = PACKED_DOUBLE_ELEMENTS;
      TNode<FixedArrayBase> to_elements =
          AllocateFixedArray(kind, *capacity, allocation_flags, source_map);
      FillFixedArrayWithValue(kind, to_elements, *count, *capacity,
                              RootIndex::kTheHoleValue);
      CopyElements(kind, to_elements, IntPtrConstant(0), source,
                   ParameterToIntPtr(*first), ParameterToIntPtr(*count));
      var_result = to_elements;
    }

    Goto(&done);
  }

  BIND(&empty);
  {
    Comment("Copy empty array");

    var_result = EmptyFixedArrayConstant();
    Goto(&done);
  }

  BIND(&done);
  return var_result.value();
}

template V8_EXPORT_PRIVATE TNode<FixedArrayBase>
CodeStubAssembler::ExtractFixedArray<Smi>(
    TNode<FixedArrayBase>, std::optional<TNode<Smi>>, std::optional<TNode<Smi>>,
    std::optional<TNode<Smi>>, ExtractFixedArrayFlags, TVariable<BoolT>*,
    std::optional<TNode<Int32T>>);

template V8_EXPORT_PRIVATE TNode<FixedArrayBase>
CodeStubAssembler::ExtractFixedArray<IntPtrT>(
    TNode<FixedArrayBase>, std::optional<TNode<IntPtrT>>,
    std::optional<TNode<IntPtrT>>, std::optional<TNode<IntPtrT>>,
    ExtractFixedArrayFlags, TVariable<BoolT>*, std::optional<TNode<Int32T>>);

void CodeStubAssembler::InitializePropertyArrayLength(
    TNode<PropertyArray> property_array, TNode<IntPtrT> length) {
  CSA_DCHECK(this, IntPtrGreaterThan(length, IntPtrConstant(0)));
  CSA_DCHECK(this,
             IntPtrLessThanOrEqual(
                 length, IntPtrConstant(PropertyArray::LengthField::kMax)));
  StoreObjectFieldNoWriteBarrier(
      property_array, PropertyArray::kLengthAndHashOffset, SmiTag(length));
}

TNode<PropertyArray> CodeStubAssembler::AllocatePropertyArray(
    TNode<IntPtrT> capacity) {
  CSA_DCHECK(this, IntPtrGreaterThan(capacity, IntPtrConstant(0)));
  TNode<IntPtrT> total_size = GetPropertyArrayAllocationSize(capacity);

  TNode<HeapObject> array = Allocate(total_size, AllocationFlag::kNone);
  RootIndex map_index = RootIndex::kPropertyArrayMap;
  DCHECK(RootsTable::IsImmortalImmovable(map_index));
  StoreMapNoWriteBarrier(array, map_index);
  TNode<PropertyArray> property_array = CAST(array);
  InitializePropertyArrayLength(property_array, capacity);
  return property_array;
}

void CodeStubAssembler::FillPropertyArrayWithUndefined(
    TNode<PropertyArray> array, TNode<IntPtrT> from_index,
    TNode<IntPtrT> to_index) {
  ElementsKind kind = PACKED_ELEMENTS;
  TNode<Undefined> value = UndefinedConstant();
  BuildFastArrayForEach(
      array, kind, from_index, to_index,
      [this, value](TNode<HeapObject> array, TNode<IntPtrT> offset) {
        StoreNoWriteBarrier(MachineRepresentation::kTagged, array, offset,
                            value);
      },
      LoopUnrollingMode::kYes);
}

template <typename TIndex>
void CodeStubAssembler::FillFixedArrayWithValue(ElementsKind kind,
                                                TNode<FixedArrayBase> array,
                                                TNode<TIndex> from_index,
                                                TNode<TIndex> to_index,
                                                RootIndex value_root_index) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT from and to are allowed");
  CSA_SLOW_DCHECK(this, IsFixedArrayWithKind(array, kind));
  DCHECK(value_root_index == RootIndex::kTheHoleValue ||
         value_root_index == RootIndex::kUndefinedValue);

  // Determine the value to initialize the {array} based
  // on the {value_root_index} and the elements {kind}.
  TNode<Object> value = LoadRoot(value_root_index);
  TNode<Float64T> float_value;
  if (IsDoubleElementsKind(kind)) {
    float_value = LoadHeapNumberValue(CAST(value));
  }

  BuildFastArrayForEach(
      array, kind, from_index, to_index,
      [this, value, float_value, kind](TNode<HeapObject> array,
                                       TNode<IntPtrT> offset) {
        if (IsDoubleElementsKind(kind)) {
          StoreNoWriteBarrier(MachineRepresentation::kFloat64, array, offset,
                              float_value);
        } else {
          StoreNoWriteBarrier(MachineRepresentation::kTagged, array, offset,
                              value);
        }
      },
      LoopUnrollingMode::kYes);
}

template V8_EXPORT_PRIVATE void
    CodeStubAssembler::FillFixedArrayWithValue<IntPtrT>(ElementsKind,
                                                        TNode<FixedArrayBase>,
                                                        TNode<IntPtrT>,
                                                        TNode<IntPtrT>,
                                                        RootIndex);
template V8_EXPORT_PRIVATE void CodeStubAssembler::FillFixedArrayWithValue<Smi>(
    ElementsKind, TNode<FixedArrayBase>, TNode<Smi>, TNode<Smi>, RootIndex);

void CodeStubAssembler::StoreDoubleHole(TNode<HeapObject> object,
                                        TNode<IntPtrT> offset) {
  TNode<UintPtrT> double_hole =
      Is64() ? ReinterpretCast<UintPtrT>(Int64Constant(kHoleNanInt64))
             : ReinterpretCast<UintPtrT>(Int32Constant(kHoleNanLower32));
  // TODO(danno): When we have a Float32/Float64 wrapper class that
  // preserves double bits during manipulation, remove this code/change
  // this to an indexed Float64 store.
  if (Is64()) {
    StoreNoWriteBarrier(MachineRepresentation::kWord64, object, offset,
                        double_hole);
  } else {
    StoreNoWriteBarrier(MachineRepresentation::kWord32, object, offset,
                        double_hole);
    StoreNoWriteBarrier(MachineRepresentation::kWord32, object,
                        IntPtrAdd(offset, IntPtrConstant(kInt32Size)),
                        double_hole);
  }
}

void CodeStubAssembler::StoreFixedDoubleArrayHole(TNode<FixedDoubleArray> array,
                                                  TNode<IntPtrT> index) {
  TNode<IntPtrT> offset = ElementOffsetFromIndex(
      index, PACKED_DOUBLE_ELEMENTS, FixedArray::kHeaderSize - kHeapObjectTag);
  CSA_DCHECK(this, IsOffsetInBounds(
                       offset, LoadAndUntagFixedArrayBaseLength(array),
                       FixedDoubleArray::kHeaderSize, PACKED_DOUBLE_ELEMENTS));
  StoreDoubleHole(array, offset);
}

void CodeStubAssembler::FillFixedArrayWithSmiZero(ElementsKind kind,
                                                  TNode<FixedArray> array,
                                                  TNode<IntPtrT> start,
                                                  TNode<IntPtrT> length) {
  DCHECK(IsSmiOrObjectElementsKind(kind));
  CSA_DCHECK(this,
             IntPtrLessThanOrEqual(IntPtrAdd(start, length),
                                   LoadAndUntagFixedArrayBaseLength(array)));

  TNode<IntPtrT> byte_length = TimesTaggedSize(length);
  CSA_DCHECK(this, UintPtrLessThan(length, byte_length));

  static const int32_t fa_base_data_offset =
      FixedArray::kHeaderSize - kHeapObjectTag;
  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(start, kind, fa_base_data_offset);
  TNode<IntPtrT> backing_store = IntPtrAdd(BitcastTaggedToWord(array), offset);

  // Call out to memset to perform initialization.
  TNode<ExternalReference> memset =
      ExternalConstant(ExternalReference::libc_memset_function());
  static_assert(kSizetSize == kIntptrSize);
  CallCFunction(memset, MachineType::Pointer(),
                std::make_pair(MachineType::Pointer(), backing_store),
                std::make_pair(MachineType::IntPtr(), IntPtrConstant(0)),
                std::make_pair(MachineType::UintPtr(), byte_length));
}

void CodeStubAssembler::FillFixedDoubleArrayWithZero(
    TNode<FixedDoubleArray> array, TNode<IntPtrT> start,
    TNode<IntPtrT> length) {
  CSA_DCHECK(this,
             IntPtrLessThanOrEqual(IntPtrAdd(start, length),
                                   LoadAndUntagFixedArrayBaseLength(array)));

  TNode<IntPtrT> byte_length = TimesDoubleSize(length);
  CSA_DCHECK(this, UintPtrLessThan(length, byte_length));

  static const int32_t fa_base_data_offset =
      FixedDoubleArray::kHeaderSize - kHeapObjectTag;
  TNode<IntPtrT> offset = ElementOffsetFromIndex(start, PACKED_DOUBLE_ELEMENTS,
                                                 fa_base_data_offset);
  TNode<IntPtrT> backing_store = IntPtrAdd(BitcastTaggedToWord(array), offset);

  // Call out to memset to perform initialization.
  TNode<ExternalReference> memset =
      ExternalConstant(ExternalReference::libc_memset_function());
  static_assert(kSizetSize == kIntptrSize);
  CallCFunction(memset, MachineType::Pointer(),
                std::make_pair(MachineType::Pointer(), backing_store),
                std::make_pair(MachineType::IntPtr(), IntPtrConstant(0)),
                std::make_pair(MachineType::UintPtr(), byte_length));
}

void CodeStubAssembler::JumpIfPointersFromHereAreInteresting(
    TNode<Object> object, Label* interesting) {
  Label finished(this);
  TNode<IntPtrT> object_word = BitcastTaggedToWord(object);
  TNode<IntPtrT> object_page_header = MemoryChunkFromAddress(object_word);
  TNode<IntPtrT> page_flags = UncheckedCast<IntPtrT>(
      Load(MachineType::IntPtr(), object_page_header,
           IntPtrConstant(MemoryChunkLayout::kFlagsOffset)));
  Branch(
      WordEqual(WordAnd(page_flags,
                        IntPtrConstant(
                            MemoryChunk::kPointersFromHereAreInterestingMask)),
                IntPtrConstant(0)),
      &finished, interesting);
  BIND(&finished);
}

void CodeStubAssembler::MoveElements(ElementsKind kind,
                                     TNode<FixedArrayBase> elements,
                                     TNode<IntPtrT> dst_index,
                                     TNode<IntPtrT> src_index,
                                     TNode<IntPtrT> length) {
  Label finished(this);
  Label needs_barrier(this);
#ifdef V8_DISABLE_WRITE_BARRIERS
  const bool needs_barrier_check = false;
#else
  const bool needs_barrier_check = !IsDoubleElementsKind(kind);
#endif  // V8_DISABLE_WRITE_BARRIERS

  DCHECK(IsFastElementsKind(kind));
  CSA_DCHECK(this, IsFixedArrayWithKind(elements, kind));
  CSA_DCHECK(this,
             IntPtrLessThanOrEqual(IntPtrAdd(dst_index, length),
                                   LoadAndUntagFixedArrayBaseLength(elements)));
  CSA_DCHECK(this,
             IntPtrLessThanOrEqual(IntPtrAdd(src_index, length),
                                   LoadAndUntagFixedArrayBaseLength(elements)));

  // The write barrier can be ignored if {dst_elements} is in new space, or if
  // the elements pointer is FixedDoubleArray.
  if (needs_barrier_check) {
    JumpIfPointersFromHereAreInteresting(elements, &needs_barrier);
  }

  const TNode<IntPtrT> source_byte_length =
      IntPtrMul(length, IntPtrConstant(ElementsKindToByteSize(kind)));
  static const int32_t fa_base_data_offset =
      FixedArrayBase::kHeaderSize - kHeapObjectTag;
  TNode<IntPtrT> elements_intptr = BitcastTaggedToWord(elements);
  TNode<IntPtrT> target_data_ptr =
      IntPtrAdd(elements_intptr,
                ElementOffsetFromIndex(dst_index, kind, fa_base_data_offset));
  TNode<IntPtrT> source_data_ptr =
      IntPtrAdd(elements_intptr,
                ElementOffsetFromIndex(src_index, kind, fa_base_data_offset));
  TNode<ExternalReference> memmove =
      ExternalConstant(ExternalReference::libc_memmove_function());
  CallCFunction(memmove, MachineType::Pointer(),
                std::make_pair(MachineType::Pointer(), target_data_ptr),
                std::make_pair(MachineType::Pointer(), source_data_ptr),
                std::make_pair(MachineType::UintPtr(), source_byte_length));

  if (needs_barrier_check) {
    Goto(&finished);

    BIND(&needs_barrier);
    {
      const TNode<IntPtrT> begin = src_index;
      const TNode<IntPtrT> end = IntPtrAdd(begin, length);

      // If dst_index is less than src_index, then walk forward.
      const TNode<IntPtrT> delta =
          IntPtrMul(IntPtrSub(dst_index, begin),
                    IntPtrConstant(ElementsKindToByteSize(kind)));
      auto loop_body = [&](TNode<HeapObject> array, TNode<IntPtrT> offset) {
        const TNode<AnyTaggedT> element = Load<AnyTaggedT>(array, offset);
        const TNode<WordT> delta_offset = IntPtrAdd(offset, delta);
        Store(array, delta_offset, element);
      };

      Label iterate_forward(this);
      Label iterate_backward(this);
      Branch(IntPtrLessThan(delta, IntPtrConstant(0)), &iterate_forward,
             &iterate_backward);
      BIND(&iterate_forward);
      {
        // Make a loop for the stores.
        BuildFastArrayForEach(elements, kind, begin, end, loop_body,
                              LoopUnrollingMode::kYes,
                              ForEachDirection::kForward);
        Goto(&finished);
      }

      BIND(&iterate_backward);
      {
        BuildFastArrayForEach(elements, kind, begin, end, loop_body,
                              LoopUnrollingMode::kYes,
                              ForEachDirection::kReverse);
        Goto(&finished);
      }
    }
    BIND(&finished);
  }
}

void CodeStubAssembler::CopyElements(ElementsKind kind,
                                     TNode<FixedArrayBase> dst_elements,
                                     TNode<IntPtrT> dst_index,
                                     TNode<FixedArrayBase> src_elements,
                                     TNode<IntPtrT> src_index,
                                     TNode<IntPtrT> length,
                                     WriteBarrierMode write_barrier) {
  Label finished(this);
  Label needs_barrier(this);
#ifdef V8_DISABLE_WRITE_BARRIERS
  const bool needs_barrier_check = false;
#else
  const bool needs_barrier_check = !IsDoubleElementsKind(kind);
#endif  // V8_DISABLE_WRITE_BARRIERS

  DCHECK(IsFastElementsKind(kind));
  CSA_DCHECK(this, IsFixedArrayWithKind(dst_elements, kind));
  CSA_DCHECK(this, IsFixedArrayWithKind(src_elements, kind));
  CSA_DCHECK(this, IntPtrLessThanOrEqual(
                       IntPtrAdd(dst_index, length),
                       LoadAndUntagFixedArrayBaseLength(dst_elements)));
  CSA_DCHECK(this, IntPtrLessThanOrEqual(
                       IntPtrAdd(src_index, length),
                       LoadAndUntagFixedArrayBaseLength(src_elements)));
  CSA_DCHECK(this, Word32Or(TaggedNotEqual(dst_elements, src_elements),
                            IntPtrEqual(length, IntPtrConstant(0))));

  // The write barrier can be ignored if {dst_elements} is in new space, or if
  // the elements pointer is FixedDoubleArray.
  if (needs_barrier_check) {
    JumpIfPointersFromHereAreInteresting(dst_elements, &needs_barrier);
  }

  TNode<IntPtrT> source_byte_length =
      IntPtrMul(length, IntPtrConstant(ElementsKindToByteSize(kind)));
  static const int32_t fa_base_data_offset =
      FixedArrayBase::kHeaderSize - kHeapObjectTag;
  TNode<IntPtrT> src_offset_start =
      ElementOffsetFromIndex(src_index, kind, fa_base_data_offset);
  TNode<IntPtrT> dst_offset_start =
      ElementOffsetFromIndex(dst_index, kind, fa_base_data_offset);
  TNode<IntPtrT> src_elements_intptr = BitcastTaggedToWord(src_elements);
  TNode<IntPtrT> source_data_ptr =
      IntPtrAdd(src_elements_intptr, src_offset_start);
  TNode<IntPtrT> dst_elements_intptr = BitcastTaggedToWord(dst_elements);
  TNode<IntPtrT> dst_data_ptr =
      IntPtrAdd(dst_elements_intptr, dst_offset_start);
  TNode<ExternalReference> memcpy =
      ExternalConstant(ExternalReference::libc_memcpy_function());
  CallCFunction(memcpy, MachineType::Pointer(),
                std::make_pair(MachineType::Pointer(), dst_data_ptr),
                std::make_pair(MachineType::Pointer(), source_data_ptr),
                std::make_pair(MachineType::UintPtr(), source_byte_length));

  if (needs_barrier_check) {
    Goto(&finished);

    BIND(&needs_barrier);
    {
      const TNode<IntPtrT> begin = src_index;
      const TNode<IntPtrT> end = IntPtrAdd(begin, length);
      const TNode<IntPtrT> delta =
          IntPtrMul(IntPtrSub(dst_index, src_index),
                    IntPtrConstant(ElementsKindToByteSize(kind)));
      BuildFastArrayForEach(
          src_elements, kind, begin, end,
          [&](TNode<HeapObject> array, TNode<IntPtrT> offset) {
            const TNode<AnyTaggedT> element = Load<AnyTaggedT>(array, offset);
            const TNode<WordT> delta_offset = IntPtrAdd(offset, delta);
            if (write_barrier == SKIP_WRITE_BARRIER) {
              StoreNoWriteBarrier(MachineRepresentation::kTagged, dst_elements,
                                  delta_offset, element);
            } else {
              Store(dst_elements, delta_offset, element);
            }
          },
          LoopUnrollingMode::kYes, ForEachDirection::kForward);
      Goto(&finished);
    }
    BIND(&finished);
  }
}

void CodeStubAssembler::CopyRange(TNode<HeapObject> dst_object, int dst_offset,
                                  TNode<HeapObject> src_object, int src_offset,
                                  TNode<IntPtrT> length_in_tagged,
                                  WriteBarrierMode mode) {
  // TODO(jgruber): This could be a lot more involved (e.g. better code when
  // write barriers can be skipped). Extend as needed.
  BuildFastLoop<IntPtrT>(
      IntPtrConstant(0), length_in_tagged,
      [=, this](TNode<IntPtrT> index) {
        TNode<IntPtrT> current_src_offset =
            IntPtrAdd(TimesTaggedSize(index), IntPtrConstant(src_offset));
        TNode<Object> value = LoadObjectField(src_object, current_src_offset);
        TNode<IntPtrT> current_dst_offset =
            IntPtrAdd(TimesTaggedSize(index), IntPtrConstant(dst_offset));
        if (mode == SKIP_WRITE_BARRIER) {
          StoreObjectFieldNoWriteBarrier(dst_object, current_dst_offset, value);
        } else {
          StoreObjectField(dst_object, current_dst_offset, value);
        }
      },
      1, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
}

template <typename TIndex>
void CodeStubAssembler::CopyFixedArrayElements(
    ElementsKind from_kind, TNode<FixedArrayBase> from_array,
    ElementsKind to_kind, TNode<FixedArrayBase> to_array,
    TNode<TIndex> first_element, TNode<TIndex> element_count,
    TNode<TIndex> capacity, WriteBarrierMode barrier_mode,
    HoleConversionMode convert_holes, TVariable<BoolT>* var_holes_converted) {
  DCHECK_IMPLIES(var_holes_converted != nullptr,
                 convert_holes == HoleConversionMode::kConvertToUndefined);
  CSA_SLOW_DCHECK(this, IsFixedArrayWithKindOrEmpty(from_array, from_kind));
  CSA_SLOW_DCHECK(this, IsFixedArrayWithKindOrEmpty(to_array, to_kind));
  static_assert(FixedArray::kHeaderSize == FixedDoubleArray::kHeaderSize);
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT indices are allowed");

  const int first_element_offset = FixedArray::kHeaderSize - kHeapObjectTag;
  Comment("[ CopyFixedArrayElements");

  // Typed array elements are not supported.
  DCHECK(!IsTypedArrayElementsKind(from_kind));
  DCHECK(!IsTypedArrayElementsKind(to_kind));

  Label done(this);
  bool from_double_elements = IsDoubleElementsKind(from_kind);
  bool to_double_elements = IsDoubleElementsKind(to_kind);
  bool doubles_to_objects_conversion =
      IsDoubleElementsKind(from_kind) && IsObjectElementsKind(to_kind);
  bool needs_write_barrier =
      doubles_to_objects_conversion ||
      (barrier_mode == UPDATE_WRITE_BARRIER && IsObjectElementsKind(to_kind));
  bool element_offset_matches =
      !needs_write_barrier &&
      (kTaggedSize == kDoubleSize ||
       IsDoubleElementsKind(from_kind) == IsDoubleElementsKind(to_kind));
  TNode<UintPtrT> double_hole =
      Is64() ? ReinterpretCast<UintPtrT>(Int64Constant(kHoleNanInt64))
             : ReinterpretCast<UintPtrT>(Int32Constant(kHoleNanLower32));

  // If copying might trigger a GC, we pre-initialize the FixedArray such that
  // it's always in a consistent state.
  if (convert_holes == HoleConversionMode::kConvertToUndefined) {
    DCHECK(IsObjectElementsKind(to_kind));
    // Use undefined for the part that we copy and holes for the rest.
    // Later if we run into a hole in the source we can just skip the writing
    // to the target and are still guaranteed that we get an undefined.
    FillFixedArrayWithValue(to_kind, to_array, IntPtrOrSmiConstant<TIndex>(0),
                            element_count, RootIndex::kUndefinedValue);
    FillFixedArrayWithValue(to_kind, to_array, element_count, capacity,
                            RootIndex::kTheHoleValue);
  } else if (doubles_to_objects_conversion) {
    // Pre-initialized the target with holes so later if we run into a hole in
    // the source we can just skip the writing to the target.
    FillFixedArrayWithValue(to_kind, to_array, IntPtrOrSmiConstant<TIndex>(0),
                            capacity, RootIndex::kTheHoleValue);
  } else if (element_count != capacity) {
    FillFixedArrayWithValue(to_kind, to_array, element_count, capacity,
                            RootIndex::kTheHoleValue);
  }

  TNode<IntPtrT> first_from_element_offset =
      ElementOffsetFromIndex(first_element, from_kind, 0);
  TNode<IntPtrT> limit_offset = Signed(IntPtrAdd(
      first_from_element_offset, IntPtrConstant(first_element_offset)));
  TVARIABLE(IntPtrT, var_from_offset,
            ElementOffsetFromIndex(IntPtrOrSmiAdd(first_element, element_count),
                                   from_kind, first_element_offset));
  // This second variable is used only when the element sizes of source and
  // destination arrays do not match.
  TVARIABLE(IntPtrT, var_to_offset);
  if (element_offset_matches) {
    var_to_offset = var_from_offset.value();
  } else {
    var_to_offset =
        ElementOffsetFromIndex(element_count, to_kind, first_element_offset);
  }

  VariableList vars({&var_from_offset, &var_to_offset}, zone());
  if (var_holes_converted != nullptr) vars.push_back(var_holes_converted);
  Label decrement(this, vars);

  TNode<IntPtrT> to_array_adjusted =
      element_offset_matches
          ? IntPtrSub(BitcastTaggedToWord(to_array), first_from_element_offset)
          : ReinterpretCast<IntPtrT>(to_array);

  Branch(WordEqual(var_from_offset.value(), limit_offset), &done, &decrement);

  BIND(&decrement);
  {
    TNode<IntPtrT> from_offset = Signed(IntPtrSub(
        var_from_offset.value(),
        IntPtrConstant(from_double_elements ? kDoubleSize : kTaggedSize)));
    var_from_offset = from_offset;

    TNode<IntPtrT> to_offset;
    if (element_offset_matches) {
      to_offset = from_offset;
    } else {
      to_offset = IntPtrSub(
          var_to_offset.value(),
          IntPtrConstant(to_double_elements ? kDoubleSize : kTaggedSize));
      var_to_offset = to_offset;
    }

    Label next_iter(this), store_double_hole(this), signal_hole(this);
    Label* if_hole;
    if (convert_holes == HoleConversionMode::kConvertToUndefined) {
      // The target elements array is already preinitialized with undefined
      // so we only need to signal that a hole was found and continue the loop.
      if_hole = &signal_hole;
    } else if (doubles_to_objects_conversion) {
      // The target elements array is already preinitialized with holes, so we
      // can just proceed with the next iteration.
      if_hole = &next_iter;
    } else if (IsDoubleElementsKind(to_kind)) {
      if_hole = &store_double_hole;
    } else {
      // In all the other cases don't check for holes and copy the data as is.
      if_hole = nullptr;
    }

    if (to_double_elements) {
      DCHECK(!needs_write_barrier);
      TNode<Float64T> value = LoadElementAndPrepareForStore<Float64T>(
          from_array, var_from_offset.value(), from_kind, to_kind, if_hole);
      StoreNoWriteBarrier(MachineRepresentation::kFloat64, to_array_adjusted,
                          to_offset, value);
    } else {
      TNode<Object> value = LoadElementAndPrepareForStore<Object>(
          from_array, var_from_offset.value(), from_kind, to_kind, if_hole);
      if (needs_write_barrier) {
        CHECK_EQ(to_array, to_array_adjusted);
        Store(to_array_adjusted, to_offset, value);
      } else {
        UnsafeStoreNoWriteBarrier(MachineRepresentation::kTagged,
                                  to_array_adjusted, to_offset, value);
      }
    }

    Goto(&next_iter);

    if (if_hole == &store_double_hole) {
      BIND(&store_double_hole);
      // Don't use doubles to store the hole double, since manipulating the
      // signaling NaN used for the hole in C++, e.g. with base::bit_cast,
      // will change its value on ia32 (the x87 stack is used to return values
      // and stores to the stack silently clear the signalling bit).
      //
      // TODO(danno): When we have a Float32/Float64 wrapper class that
      // preserves double bits during manipulation, remove this code/change
      // this to an indexed Float64 store.
      if (Is64()) {
        StoreNoWriteBarrier(MachineRepresentation::kWord64, to_array_adjusted,
                            to_offset, double_hole);
      } else {
        StoreNoWriteBarrier(MachineRepresentation::kWord32, to_array_adjusted,
                            to_offset, double_hole);
        StoreNoWriteBarrier(MachineRepresentation::kWord32, to_array_adjusted,
                            IntPtrAdd(to_offset, IntPtrConstant(kInt32Size)),
                            double_hole);
      }
      Goto(&next_iter);
    } else if (if_hole == &signal_hole) {
      // This case happens only when IsObjectElementsKind(to_kind).
      BIND(&signal_hole);
      if (var_holes_converted != nullptr) {
        *var_holes_converted = Int32TrueConstant();
      }
      Goto(&next_iter);
    }

    BIND(&next_iter);
    TNode<BoolT> compare = WordNotEqual(from_offset, limit_offset);
    Branch(compare, &decrement, &done);
  }

  BIND(&done);
  Comment("] CopyFixedArrayElements");
}

TNode<FixedArray> CodeStubAssembler::HeapObjectToFixedArray(
    TNode<HeapObject> base, Label* cast_fail) {
  Label fixed_array(this);
  TNode<Map> map = LoadMap(base);
  GotoIf(TaggedEqual(map, FixedArrayMapConstant()), &fixed_array);
  GotoIf(TaggedNotEqual(map, FixedCOWArrayMapConstant()), cast_fail);
  Goto(&fixed_array);
  BIND(&fixed_array);
  return UncheckedCast<FixedArray>(base);
}

void CodeStubAssembler::CopyPropertyArrayValues(TNode<HeapObject> from_array,
                                                TNode<PropertyArray> to_array,
                                                TNode<IntPtrT> property_count,
                                                WriteBarrierMode barrier_mode,
                                                DestroySource destroy_source) {
  CSA_SLOW_DCHECK(this, Word32Or(IsPropertyArray(from_array),
                                 IsEmptyFixedArray(from_array)));
  Comment("[ CopyPropertyArrayValues");

  bool needs_write_barrier = barrier_mode == UPDATE_WRITE_BARRIER;

  if (destroy_source == DestroySource::kNo) {
    // PropertyArray may contain mutable HeapNumbers, which will be cloned on
    // the heap, requiring a write barrier.
    needs_write_barrier = true;
  }

  TNode<IntPtrT> start = IntPtrConstant(0);
  ElementsKind kind = PACKED_ELEMENTS;
  BuildFastArrayForEach(
      from_array, kind, start, property_count,
      [this, to_array, needs_write_barrier, destroy_source](
          TNode<HeapObject> array, TNode<IntPtrT> offset) {
        TNode<AnyTaggedT> value = Load<AnyTaggedT>(array, offset);

        if (destroy_source == DestroySource::kNo) {
          value = CloneIfMutablePrimitive(CAST(value));
        }

        if (needs_write_barrier) {
          Store(to_array, offset, value);
        } else {
          StoreNoWriteBarrier(MachineRepresentation::kTagged, to_array, offset,
                              value);
        }
      },
      LoopUnrollingMode::kYes);

#ifdef DEBUG
  // Zap {from_array} if the copying above has made it invalid.
  if (destroy_source == DestroySource::kYes) {
    Label did_zap(this);
    GotoIf(IsEmptyFixedArray(from_array), &did_zap);
    FillPropertyArrayWithUndefined(CAST(from_array), start, property_count);

    Goto(&did_zap);
    BIND(&did_zap);
  }
#endif
  Comment("] CopyPropertyArrayValues");
}

TNode<FixedArrayBase> CodeStubAssembler::CloneFixedArray(
    TNode<FixedArrayBase> source, ExtractFixedArrayFlags flags) {
  return ExtractFixedArray(
      source, std::optional<TNode<BInt>>(IntPtrOrSmiConstant<BInt>(0)),
      std::optional<TNode<BInt>>(std::nullopt),
      std::optional<TNode<BInt>>(std::nullopt), flags);
}

template <>
TNode<Object> CodeStubAssembler::LoadElementAndPrepareForStore(
    TNode<FixedArrayBase> array, TNode<IntPtrT> offset, ElementsKind from_kind,
    ElementsKind to_kind, Label* if_hole) {
  CSA_DCHECK(this, IsFixedArrayWithKind(array, from_kind));
  DCHECK(!IsDoubleElementsKind(to_kind));
  if (IsDoubleElementsKind(from_kind)) {
    TNode<Float64T> value =
        LoadDoubleWithHoleCheck(array, offset, if_hole, MachineType::Float64());
    return AllocateHeapNumberWithValue(value);
  } else {
    TNode<Object> value = Load<Object>(array, offset);
    if (if_hole) {
      GotoIf(TaggedEqual(value, TheHoleConstant()), if_hole);
    }
    return value;
  }
}

template <>
TNode<Float64T> CodeStubAssembler::LoadElementAndPrepareForStore(
    TNode<FixedArrayBase> array, TNode<IntPtrT> offset, ElementsKind from_kind,
    ElementsKind to_kind, Label* if_hole) {
  CSA_DCHECK(this, IsFixedArrayWithKind(array, from_kind));
  DCHECK(IsDoubleElementsKind(to_kind));
  if (IsDoubleElementsKind(from_kind)) {
    return LoadDoubleWithHoleCheck(array, offset, if_hole,
                                   MachineType::Float64());
  } else {
    TNode<Object> value = Load<Object>(array, offset);
    if (if_hole) {
      GotoIf(TaggedEqual(value, TheHoleConstant()), if_hole);
    }
    if (IsSmiElementsKind(from_kind)) {
      return SmiToFloat64(CAST(value));
    }
    return LoadHeapNumberValue(CAST(value));
  }
}

template <typename TIndex>
TNode<TIndex> CodeStubAssembler::CalculateNewElementsCapacity(
    TNode<TIndex> old_capacity) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT old_capacity is allowed");
  Comment("TryGrowElementsCapacity");
  TNode<TIndex> half_old_capacity = WordOrSmiShr(old_capacity, 1);
  TNode<TIndex> new_capacity = IntPtrOrSmiAdd(half_old_capacity, old_capacity);
  TNode<TIndex> padding =
      IntPtrOrSmiConstant<TIndex>(JSObject::kMinAddedElementsCapacity);
  return IntPtrOrSmiAdd(new_capacity, padding);
}

template V8_EXPORT_PRIVATE TNode<IntPtrT>
    CodeStubAssembler::CalculateNewElementsCapacity<IntPtrT>(TNode<IntPtrT>);
template V8_EXPORT_PRIVATE TNode<Smi>
    CodeStubAssembler::CalculateNewElementsCapacity<Smi>(TNode<Smi>);

TNode<FixedArrayBase> CodeStubAssembler::TryGrowElementsCapacity(
    TNode<HeapObject> object, TNode<FixedArrayBase> elements, ElementsKind kind,
    TNode<Smi> key, Label* bailout) {
  CSA_SLOW_DCHECK(this, IsFixedArrayWithKindOrEmpty(elements, kind));
  TNode<Smi> capacity = LoadFixedArrayBaseLength(elements);

  return TryGrowElementsCapacity(object, elements, kind,
                                 TaggedToParameter<BInt>(key),
                                 TaggedToParameter<BInt>(capacity), bailout);
}

template <typename TIndex>
TNode<FixedArrayBase> CodeStubAssembler::TryGrowElementsCapacity(
    TNode<HeapObject> object, TNode<FixedArrayBase> elements, ElementsKind kind,
    TNode<TIndex> key, TNode<TIndex> capacity, Label* bailout) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT key and capacity nodes are allowed");
  Comment("TryGrowElementsCapacity");
  CSA_SLOW_DCHECK(this, IsFixedArrayWithKindOrEmpty(elements, kind));

  // If the gap growth is too big, fall back to the runtime.
  TNode<TIndex> max_gap = IntPtrOrSmiConstant<TIndex>(JSObject::kMaxGap);
  TNode<TIndex> max_capacity = IntPtrOrSmiAdd(capacity, max_gap);
  GotoIf(UintPtrOrSmiGreaterThanOrEqual(key, max_capacity), bailout);

  // Calculate the capacity of the new backing store.
  TNode<TIndex> new_capacity = CalculateNewElementsCapacity(
      IntPtrOrSmiAdd(key, IntPtrOrSmiConstant<TIndex>(1)));

  return GrowElementsCapacity(object, elements, kind, kind, capacity,
                              new_capacity, bailout);
}

template <typename TIndex>
TNode<FixedArrayBase> CodeStubAssembler::GrowElementsCapacity(
    TNode<HeapObject> object, TNode<FixedArrayBase> elements,
    ElementsKind from_kind, ElementsKind to_kind, TNode<TIndex> capacity,
    TNode<TIndex> new_capacity, Label* bailout) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT capacities are allowed");
  Comment("[ GrowElementsCapacity");
  CSA_SLOW_DCHECK(this, IsFixedArrayWithKindOrEmpty(elements, from_kind));

  // If size of the allocation for the new capacity doesn't fit in a page
  // that we can bump-pointer allocate from, fall back to the runtime.
  int max_size = FixedArrayBase::GetMaxLengthForNewSpaceAllocation(to_kind);
  GotoIf(UintPtrOrSmiGreaterThanOrEqual(new_capacity,
                                        IntPtrOrSmiConstant<TIndex>(max_size)),
         bailout);

  // Allocate the new backing store.
  TNode<FixedArrayBase> new_elements =
      AllocateFixedArray(to_kind, new_capacity);

  // Copy the elements from the old elements store to the new.
  // The size-check above guarantees that the |new_elements| is allocated
  // in new space so we can skip the write barrier.
  CopyFixedArrayElements(from_kind, elements, to_kind, new_elements, capacity,
                         new_capacity, SKIP_WRITE_BARRIER);

  StoreObjectField(object, JSObject::kElementsOffset, new_elements);
  Comment("] GrowElementsCapacity");
  return new_elements;
}

template TNode<FixedArrayBase> CodeStubAssembler::GrowElementsCapacity<IntPtrT>(
    TNode<HeapObject>, TNode<FixedArrayBase>, ElementsKind, ElementsKind,
    TNode<IntPtrT>, TNode<IntPtrT>, compiler::CodeAssemblerLabel*);

namespace {

// Helper function for folded memento allocation.
// Memento objects are designed to be put right after the objects they are
// tracking on. So memento allocations have to be folded together with previous
// object allocations.
TNode<HeapObject> InnerAllocateMemento(CodeStubAssembler* csa,
                                       TNode<HeapObject> previous,
                                       TNode<IntPtrT> offset) {
  return csa->UncheckedCast<HeapObject>(csa->BitcastWordToTagged(
      csa->IntPtrAdd(csa->BitcastTaggedToWord(previous), offset)));
}

}  // namespace

void CodeStubAssembler::InitializeAllocationMemento(
    TNode<HeapObject> base, TNode<IntPtrT> base_allocation_size,
    TNode<AllocationSite> allocation_site) {
  DCHECK(V8_ALLOCATION_SITE_TRACKING_BOOL);
  Comment("[Initialize AllocationMemento");
  TNode<HeapObject> memento =
      InnerAllocateMemento(this, base, base_allocation_size);
  StoreMapNoWriteBarrier(memento, RootIndex::kAllocationMementoMap);
  StoreObjectFieldNoWriteBarrier(
      memento, AllocationMemento::kAllocationSiteOffset, allocation_site);
  if (v8_flags.allocation_site_pretenuring) {
    TNode<Int32T> count = LoadObjectField<Int32T>(
        allocation_site, AllocationSite::kPretenureCreateCountOffset);

    TNode<Int32T> incremented_count = Int32Add(count, Int32Constant(1));
    StoreObjectFieldNoWriteBarrier(allocation_site,
                                   AllocationSite::kPretenureCreateCountOffset,
                                   incremented_count);
  }
  Comment("]");
}

TNode<IntPtrT> CodeStubAssembler::TryTaggedToInt32AsIntPtr(
    TNode<Object> acc, Label* if_not_possible) {
  TVARIABLE(IntPtrT, acc_intptr);
  Label is_not_smi(this), have_int32(this);

  GotoIfNot(TaggedIsSmi(acc), &is_not_smi);
  acc_intptr = SmiUntag(CAST(acc));
  Goto(&have_int32);

  BIND(&is_not_smi);
  GotoIfNot(IsHeapNumber(CAST(acc)), if_not_possible);
  TNode<Float64T> value = LoadHeapNumberValue(CAST(acc));
  TNode<Int32T> value32 = RoundFloat64ToInt32(value);
  TNode<Float64T> value64 = ChangeInt32ToFloat64(value32);
  GotoIfNot(Float64Equal(value, value64), if_not_possible);
  acc_intptr = ChangeInt32ToIntPtr(value32);
  Goto(&have_int32);

  BIND(&have_int32);
  return acc_intptr.value();
}

TNode<Float64T> CodeStubAssembler::TryTaggedToFloat64(
    TNode<Object> value, Label* if_valueisnotnumber) {
  return Select<Float64T>(
      TaggedIsSmi(value), [&]() { return SmiToFloat64(CAST(value)); },
      [&]() {
        GotoIfNot(IsHeapNumber(CAST(value)), if_valueisnotnumber);
        return LoadHeapNumberValue(CAST(value));
      });
}

TNode<Float64T> CodeStubAssembler::TruncateTaggedToFloat64(
    TNode<Context> context, TNode<Object> value) {
  // We might need to loop once due to ToNumber conversion.
  TVARIABLE(Object, var_value, value);
  TVARIABLE(Float64T, var_result);
  Label loop(this, &var_value), done_loop(this, &var_result);
  Goto(&loop);
  BIND(&loop);
  {
    Label if_valueisnotnumber(this, Label::kDeferred);

    // Load the current {value}.
    value = var_value.value();

    // Convert {value} to Float64 if it is a number and convert it to a number
    // otherwise.
    var_result = TryTaggedToFloat64(value, &if_valueisnotnumber);
    Goto(&done_loop);

    BIND(&if_valueisnotnumber);
    {
      // Convert the {value} to a Number first.
      var_value = CallBuiltin(Builtin::kNonNumberToNumber, context, value);
      Goto(&loop);
    }
  }
  BIND(&done_loop);
  return var_result.value();
}

TNode<Word32T> CodeStubAssembler::TruncateTaggedToWord32(TNode<Context> context,
                                                         TNode<Object> value) {
  TVARIABLE(Word32T, var_result);
  Label done(this);
  TaggedToWord32OrBigIntImpl<Object::Conversion::kToNumber>(
      context, value, &done, &var_result, IsKnownTaggedPointer::kNo, {});
  BIND(&done);
  return var_result.value();
}

// Truncate {value} to word32 and jump to {if_number} if it is a Number,
// or find that it is a BigInt and jump to {if_bigint}.
void CodeStubAssembler::TaggedToWord32OrBigInt(
    TNode<Context> context, TNode<Object> value, Label* if_number,
    TVariable<Word32T>* var_word32, Label* if_bigint, Label* if_bigint64,
    TVariable<BigInt>* var_maybe_bigint) {
  TaggedToWord32OrBigIntImpl<Object::Conversion::kToNumeric>(
      context, value, if_number, var_word32, IsKnownTaggedPointer::kNo, {},
      if_bigint, if_bigint64, var_maybe_bigint);
}

// Truncate {value} to word32 and jump to {if_number} if it is a Number,
// or find that it is a BigInt and jump to {if_bigint}. In either case,
// store the type feedback in {var_feedback}.
void CodeStubAssembler::TaggedToWord32OrBigIntWithFeedback(
    TNode<Context> context, TNode<Object> value, Label* if_number,
    TVariable<Word32T>* var_word32, Label* if_bigint, Label* if_bigint64,
    TVariable<BigInt>* var_maybe_bigint, const FeedbackValues& feedback) {
  TaggedToWord32OrBigIntImpl<Object::Conversion::kToNumeric>(
      context, value, if_number, var_word32, IsKnownTaggedPointer::kNo,
      feedback, if_bigint, if_bigint64, var_maybe_bigint);
}

// Truncate {pointer} to word32 and jump to {if_number} if it is a Number,
// or find that it is a BigInt and jump to {if_bigint}. In either case,
// store the type feedback in {var_feedback}.
void CodeStubAssembler::TaggedPointerToWord32OrBigIntWithFeedback(
    TNode<Context> context, TNode<HeapObject> pointer, Label* if_number,
    TVariable<Word32T>* var_word32, Label* if_bigint, Label* if_bigint64,
    TVariable<BigInt>* var_maybe_bigint, const FeedbackValues& feedback) {
  TaggedToWord32OrBigIntImpl<Object::Conversion::kToNumeric>(
      context, pointer, if_number, var_word32, IsKnownTaggedPointer::kYes,
      feedback, if_bigint, if_bigint64, var_maybe_bigint);
}

template <Object::Conversion conversion>
void CodeStubAssembler::TaggedToWord32OrBigIntImpl(
    TNode<Context> context, TNode<Object> value, Label* if_number,
    TVariable<Word32T>* var_word32,
    IsKnownTaggedPointer is_known_tagged_pointer,
    const FeedbackValues& feedback, Label* if_bigint, Label* if_bigint64,
    TVariable<BigInt>* var_maybe_bigint) {
  // We might need to loop after conversion.
  TVARIABLE(Object, var_value, value);
  TVARIABLE(Object, var_exception);
  OverwriteFeedback(feedback.var_feedback, BinaryOperationFeedback::kNone);
  VariableList loop_vars({&var_value}, zone());
  if (feedback.var_feedback != nullptr) {
    loop_vars.push_back(feedback.var_feedback);
  }
  Label loop(this, loop_vars);
  Label if_exception(this, Label::kDeferred);
  if (is_known_tagged_pointer == IsKnownTaggedPointer::kNo) {
    GotoIf(TaggedIsNotSmi(value), &loop);

    // {value} is a Smi.
    *var_word32 = SmiToInt32(CAST(value));
    CombineFeedback(feedback.var_feedback,
                    BinaryOperationFeedback::kSignedSmall);
    Goto(if_number);
  } else {
    Goto(&loop);
  }
  BIND(&loop);
  {
    value = var_value.value();
    Label not_smi(this), is_heap_number(this), is_oddball(this),
        maybe_bigint64(this), is_bigint(this), check_if_smi(this);

    TNode<HeapObject> value_heap_object = CAST(value);
    TNode<Map> map = LoadMap(value_heap_object);
    GotoIf(IsHeapNumberMap(map), &is_heap_number);
    TNode<Uint16T> instance_type = LoadMapInstanceType(map);
    if (conversion == Object::Conversion::kToNumeric) {
      if (Is64() && if_bigint64) {
        GotoIf(IsBigIntInstanceType(instance_type), &maybe_bigint64);
      } else {
        GotoIf(IsBigIntInstanceType(instance_type), &is_bigint);
      }
    }

    // Not HeapNumber (or BigInt if conversion == kToNumeric).
    {
      if (feedback.var_feedback != nullptr) {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a Numeric, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        CSA_DCHECK(this, SmiEqual(feedback.var_feedback->value(),
                                  SmiConstant(BinaryOperationFeedback::kNone)));
      }
      GotoIf(InstanceTypeEqual(instance_type, ODDBALL_TYPE), &is_oddball);
      // Not an oddball either -> convert.
      auto builtin = conversion == Object::Conversion::kToNumeric
                         ? Builtin::kNonNumberToNumeric
                         : Builtin::kNonNumberToNumber;
      if (feedback.var_feedback != nullptr) {
        ScopedExceptionHandler handler(this, &if_exception, &var_exception);
        var_value = CallBuiltin(builtin, context, value);
      } else {
        var_value = CallBuiltin(builtin, context, value);
      }
      OverwriteFeedback(feedback.var_feedback, BinaryOperationFeedback::kAny);
      Goto(&check_if_smi);

      if (feedback.var_feedback != nullptr) {
        BIND(&if_exception);
        DCHECK(feedback.slot != nullptr);
        DCHECK(feedback.maybe_feedback_vector != nullptr);
        UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                       (*feedback.maybe_feedback_vector)(), *feedback.slot,
                       feedback.update_mode);
        CallRuntime(Runtime::kReThrow, context, var_exception.value());
        Unreachable();
      }

      BIND(&is_oddball);
      var_value =
          LoadObjectField(value_heap_object, offsetof(Oddball, to_number_));
      OverwriteFeedback(feedback.var_feedback,
                        BinaryOperationFeedback::kNumberOrOddball);
      Goto(&check_if_smi);
    }

    BIND(&is_heap_number);
    *var_word32 = TruncateHeapNumberValueToWord32(CAST(value));
    CombineFeedback(feedback.var_feedback, BinaryOperationFeedback::kNumber);
    Goto(if_number);

    if (conversion == Object::Conversion::kToNumeric) {
      if (Is64() && if_bigint64) {
        BIND(&maybe_bigint64);
        GotoIfLargeBigInt(CAST(value), &is_bigint);
        if (var_maybe_bigint) {
          *var_maybe_bigint = CAST(value);
        }
        CombineFeedback(feedback.var_feedback,
                        BinaryOperationFeedback::kBigInt64);
        Goto(if_bigint64);
      }

      BIND(&is_bigint);
      if (var_maybe_bigint) {
        *var_maybe_bigint = CAST(value);
      }
      CombineFeedback(feedback.var_feedback, BinaryOperationFeedback::kBigInt);
      Goto(if_bigint);
    }

    BIND(&check_if_smi);
    value = var_value.value();
    GotoIf(TaggedIsNotSmi(value), &loop);

    // {value} is a Smi.
    *var_word32 = SmiToInt32(CAST(value));
    CombineFeedback(feedback.var_feedback,
                    BinaryOperationFeedback::kSignedSmall);
    Goto(if_number);
  }
}

TNode<Int32T> CodeStubAssembler::TruncateNumberToWord32(TNode<Number> number) {
  TVARIABLE(Int32T, var_result);
  Label done(this), if_heapnumber(this);
  GotoIfNot(TaggedIsSmi(number), &if_heapnumber);
  var_result = SmiToInt32(CAST(number));
  Goto(&done);

  BIND(&if_heapnumber);
  TNode<Float64T> value = LoadHeapNumberValue(CAST(number));
  var_result = Signed(TruncateFloat64ToWord32(value));
  Goto(&done);

  BIND(&done);
  return var_result.value();
}

TNode<Int32T> CodeStubAssembler::TruncateHeapNumberValueToWord32(
    TNode<HeapNumber> object) {
  TNode<Float64T> value = LoadHeapNumberValue(object);
  return Signed(TruncateFloat64ToWord32(value));
}

TNode<Smi> CodeStubAssembler::TryHeapNumberToSmi(TNode<HeapNumber> number,
                                                 Label* not_smi) {
  TNode<Float64T> value = LoadHeapNumberValue(number);
  return TryFloat64ToSmi(value, not_smi);
}

TNode<Smi> CodeStubAssembler::TryFloat32ToSmi(TNode<Float32T> value,
                                              Label* not_smi) {
  TNode<Int32T> ivalue = TruncateFloat32ToInt32(value);
  TNode<Float32T> fvalue = RoundInt32ToFloat32(ivalue);

  Label if_int32(this);

  GotoIfNot(Float32Equal(value, fvalue), not_smi);
  GotoIfNot(Word32Equal(ivalue, Int32Constant(0)), &if_int32);
  // if (value == -0.0)
  Branch(Int32LessThan(UncheckedCast<Int32T>(BitcastFloat32ToInt32(value)),
                       Int32Constant(0)),
         not_smi, &if_int32);

  BIND(&if_int32);
  if (SmiValuesAre32Bits()) {
    return SmiTag(ChangeInt32ToIntPtr(ivalue));
  } else {
    DCHECK(SmiValuesAre31Bits());
    TNode<PairT<Int32T, BoolT>> pair = Int32AddWithOverflow(ivalue, ivalue);
    TNode<BoolT> overflow = Projection<1>(pair);
    GotoIf(overflow, not_smi);
    return BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(Projection<0>(pair)));
  }
}

TNode<Smi> CodeStubAssembler::TryFloat64ToSmi(TNode<Float64T> value,
                                              Label* not_smi) {
  TNode<Int32T> value32 = RoundFloat64ToInt32(value);
  TNode<Float64T> value64 = ChangeInt32ToFloat64(value32);

  Label if_int32(this);
  GotoIfNot(Float64Equal(value, value64), not_smi);
  GotoIfNot(Word32Equal(value32, Int32Constant(0)), &if_int32);
  Branch(Int32LessThan(UncheckedCast<Int32T>(Float64ExtractHighWord32(value)),
                       Int32Constant(0)),
         not_smi, &if_int32);

  TVARIABLE(Number, var_result);
  BIND(&if_int32);
  if (SmiValuesAre32Bits()) {
    return SmiTag(ChangeInt32ToIntPtr(value32));
  } else {
    DCHECK(SmiValuesAre31Bits());
    TNode<PairT<Int32T, BoolT>> pair = Int32AddWithOverflow(value32, value32);
    TNode<BoolT> overflow = Projection<1>(pair);
    GotoIf(overflow, not_smi);
    return BitcastWordToTaggedSigned(ChangeInt32ToIntPtr(Projection<0>(pair)));
  }
}

TNode<Float16T> CodeStubAssembler::TruncateFloat64ToFloat16(
    TNode<Float64T> value) {
  // This is a verbatim CSA implementation of DoubleToFloat16.
  //
  // The 64-bit and 32-bit paths are implemented separately, but the algorithm
  // is the same in both cases. The 32-bit version requires manual pairwise
  // operations.

  if (Is64()) {
    TVARIABLE(Uint16T, out);
    TNode<Int64T> signed_in = BitcastFloat64ToInt64(value);

    // Take the absolute value of the input.
    TNode<Word64T> sign = Word64And(signed_in, Uint64Constant(kFP64SignMask));
    TNode<Word64T> in = Word64Xor(signed_in, sign);

    Label if_infinity_or_nan(this), if_finite(this), done(this);
    Branch(Uint64GreaterThanOrEqual(in,
                                    Uint64Constant(kFP16InfinityAndNaNInfimum)),
           &if_infinity_or_nan, &if_finite);

    BIND(&if_infinity_or_nan);
    {
      // Result is infinity or NaN.
      out = Select<Uint16T>(
          Uint64GreaterThan(in, Uint64Constant(kFP64Infinity)),
          [=, this] { return Uint16Constant(kFP16qNaN); },       // NaN->qNaN
          [=, this] { return Uint16Constant(kFP16Infinity); });  // Inf->Inf
      Goto(&done);
    }

    BIND(&if_finite);
    {
      // Result is a (de)normalized number or zero.

      Label if_denormal(this), not_denormal(this);
      Branch(Uint64LessThan(in, Uint64Constant(kFP16DenormalThreshold)),
             &if_denormal, &not_denormal);

      BIND(&if_denormal);
      {
        // Result is a denormal or zero. Use the magic value and FP addition to
        // align 10 mantissa bits at the bottom of the float. Depends on FP
        // addition being round-to-nearest-even.
        TNode<Float64T> temp = Float64Add(
            BitcastInt64ToFloat64(ReinterpretCast<Int64T>(in)),
            Float64Constant(base::bit_cast<double>(kFP64To16DenormalMagic)));
        out = ReinterpretCast<Uint16T>(TruncateWord64ToWord32(
            Uint64Sub(ReinterpretCast<Uint64T>(BitcastFloat64ToInt64(temp)),
                      Uint64Constant(kFP64To16DenormalMagic))));
        Goto(&done);
      }

      BIND(&not_denormal);
      {
        // Result is not a denormal.

        // Remember if the result mantissa will be odd before rounding.
        TNode<Uint64T> mant_odd = ReinterpretCast<Uint64T>(Word64And(
            Word64Shr(in, Int64Constant(kFP64MantissaBits - kFP16MantissaBits)),
            Uint64Constant(1)));

        // Update the exponent and round to nearest even.
        //
        // Rounding to nearest even is handled in two parts. First, adding
        // kFP64To16RebiasExponentAndRound has the effect of rebiasing the
        // exponent and that if any of the lower 41 bits of the mantissa are
        // set, the 11th mantissa bit from the front becomes set. Second, adding
        // mant_odd ensures ties are rounded to even.
        TNode<Uint64T> temp1 =
            Uint64Add(ReinterpretCast<Uint64T>(in),
                      Uint64Constant(kFP64To16RebiasExponentAndRound));
        TNode<Uint64T> temp2 = Uint64Add(temp1, mant_odd);

        out = ReinterpretCast<Uint16T>(TruncateWord64ToWord32(Word64Shr(
            temp2, Int64Constant(kFP64MantissaBits - kFP16MantissaBits))));

        Goto(&done);
      }
    }

    BIND(&done);
    return ReinterpretCast<Float16T>(
        Word32Or(TruncateWord64ToWord32(Word64Shr(sign, Int64Constant(48))),
                 out.value()));
  } else {
    TVARIABLE(Uint16T, out);
    TNode<Word32T> signed_in_hi_word = Float64ExtractHighWord32(value);
    TNode<Word32T> in_lo_word = Float64ExtractLowWord32(value);

    // Take the absolute value of the input.
    TNode<Word32T> sign = Word32And(
        signed_in_hi_word, Uint64HighWordConstantNoLowWord(kFP64SignMask));
    TNode<Word32T> in_hi_word = Word32Xor(signed_in_hi_word, sign);

    Label if_infinity_or_nan(this), if_finite(this), done(this);
    Branch(Uint32GreaterThanOrEqual(
               in_hi_word,
               Uint64HighWordConstantNoLowWord(kFP16InfinityAndNaNInfimum)),
           &if_infinity_or_nan, &if_finite);

    BIND(&if_infinity_or_nan);
    {
      // Result is infinity or NaN.
      out = Select<Uint16T>(
          Uint32GreaterThan(in_hi_word,
                            Uint64HighWordConstantNoLowWord(kFP64Infinity)),
          [=, this] { return Uint16Constant(kFP16qNaN); },       // NaN->qNaN
          [=, this] { return Uint16Constant(kFP16Infinity); });  // Inf->Inf
      Goto(&done);
    }

    BIND(&if_finite);
    {
      // Result is a (de)normalized number or zero.

      Label if_denormal(this), not_denormal(this);
      Branch(Uint32LessThan(in_hi_word, Uint64HighWordConstantNoLowWord(
                                            kFP16DenormalThreshold)),
             &if_denormal, &not_denormal);

      BIND(&if_denormal);
      {
        // Result is a denormal or zero. Use the magic value and FP addition to
        // align 10 mantissa bits at the bottom of the float. Depends on FP
        // addition being round-to-nearest-even.
        TNode<Float64T> double_in = Float64InsertHighWord32(
            Float64InsertLowWord32(Float64Constant(0), in_lo_word), in_hi_word);
        TNode<Float64T> temp = Float64Add(
            double_in,
            Float64Constant(base::bit_cast<double>(kFP64To16DenormalMagic)));
        out = ReinterpretCast<Uint16T>(Projection<0>(Int32PairSub(
            Float64ExtractLowWord32(temp), Float64ExtractHighWord32(temp),
            Uint64LowWordConstant(kFP64To16DenormalMagic),
            Uint64HighWordConstant(kFP64To16DenormalMagic))));

        Goto(&done);
      }

      BIND(&not_denormal);
      {
        // Result is not a denormal.

        // Remember if the result mantissa will be odd before rounding.
        TNode<Uint32T> mant_odd = ReinterpretCast<Uint32T>(Word32And(
            Word32Shr(in_hi_word, Int32Constant(kFP64MantissaBits -
                                                kFP16MantissaBits - 32)),
            Uint32Constant(1)));

        // Update the exponent and round to nearest even.
        //
        // Rounding to nearest even is handled in two parts. First, adding
        // kFP64To16RebiasExponentAndRound has the effect of rebiasing the
        // exponent and that if any of the lower 41 bits of the mantissa are
        // set, the 11th mantissa bit from the front becomes set. Second, adding
        // mant_odd ensures ties are rounded to even.
        TNode<PairT<Word32T, Word32T>> temp1 = Int32PairAdd(
            in_lo_word, in_hi_word,
            Uint64LowWordConstant(kFP64To16RebiasExponentAndRound),
            Uint64HighWordConstant(kFP64To16RebiasExponentAndRound));
        TNode<PairT<Word32T, Word32T>> temp2 =
            Int32PairAdd(Projection<0>(temp1), Projection<1>(temp1), mant_odd,
                         Int32Constant(0));

        out = ReinterpretCast<Uint16T>((Word32Shr(
            Projection<1>(temp2),
            Int32Constant(kFP64MantissaBits - kFP16MantissaBits - 32))));

        Goto(&done);
      }
    }

    BIND(&done);
    return ReinterpretCast<Float16T>(
        Word32Or(Word32Shr(sign, Int32Constant(16)), out.value()));
  }
}

TNode<Uint32T> CodeStubAssembler::BitcastFloat16ToUint32(
    TNode<Float16T> value) {
  return ReinterpretCast<Uint32T>(value);
}

TNode<Float16T> CodeStubAssembler::BitcastUint32ToFloat16(
    TNode<Uint32T> value) {
  return ReinterpretCast<Float16T>(value);
}

TNode<Float16T> CodeStubAssembler::RoundInt32ToFloat16(TNode<Int32T> value) {
  return TruncateFloat32ToFloat16(RoundInt32ToFloat32(value));
}

TNode<Float64T> CodeStubAssembler::ChangeFloat16ToFloat64(
    TNode<Float16T> value) {
  return ChangeFloat32ToFloat64(ChangeFloat16ToFloat32(value));
}

TNode<Number> CodeStubAssembler::ChangeFloat32ToTagged(TNode<Float32T> value) {
  Label not_smi(this), done(this);
  TVARIABLE(Number, var_result);
  var_result = TryFloat32ToSmi(value, &not_smi);
  Goto(&done);

  BIND(&not_smi);
  {
    var_result = AllocateHeapNumberWithValue(ChangeFloat32ToFloat64(value));
    Goto(&done);
  }

  BIND(&done);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::ChangeFloat64ToTagged(TNode<Float64T> value) {
  Label not_smi(this), done(this);
  TVARIABLE(Number, var_result);
  var_result = TryFloat64ToSmi(value, &not_smi);
  Goto(&done);

  BIND(&not_smi);
  {
    var_result = AllocateHeapNumberWithValue(value);
    Goto(&done);
  }
  BIND(&done);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::ChangeInt32ToTagged(TNode<Int32T> value) {
  if (SmiValuesAre32Bits()) {
    return SmiTag(ChangeInt32ToIntPtr(value));
  }
  DCHECK(SmiValuesAre31Bits());
  TVARIABLE(Number, var_result);
  TNode<PairT<Int32T, BoolT>> pair = Int32AddWithOverflow(value, value);
  TNode<BoolT> overflow = Projection<1>(pair);
  Label if_overflow(this, Label::kDeferred), if_notoverflow(this),
      if_join(this);
  Branch(overflow, &if_overflow, &if_notoverflow);
  BIND(&if_overflow);
  {
    TNode<Float64T> value64 = ChangeInt32ToFloat64(value);
    TNode<HeapNumber> result = AllocateHeapNumberWithValue(value64);
    var_result = result;
    Goto(&if_join);
  }
  BIND(&if_notoverflow);
  {
    TNode<IntPtrT> almost_tagged_value =
        ChangeInt32ToIntPtr(Projection<0>(pair));
    TNode<Smi> result = BitcastWordToTaggedSigned(almost_tagged_value);
    var_result = result;
    Goto(&if_join);
  }
  BIND(&if_join);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::ChangeInt32ToTaggedNoOverflow(
    TNode<Int32T> value) {
  if (SmiValuesAre32Bits()) {
    return SmiTag(ChangeInt32ToIntPtr(value));
  }
  DCHECK(SmiValuesAre31Bits());
  TNode<Int32T> result_int32 = Int32Add(value, value);
  TNode<IntPtrT> almost_tagged_value = ChangeInt32ToIntPtr(result_int32);
  TNode<Smi> result = BitcastWordToTaggedSigned(almost_tagged_value);
  return result;
}

TNode<Number> CodeStubAssembler::ChangeUint32ToTagged(TNode<Uint32T> value) {
  Label if_overflow(this, Label::kDeferred), if_not_overflow(this),
      if_join(this);
  TVARIABLE(Number, var_result);
  // If {value} > 2^31 - 1, we need to store it in a HeapNumber.
  Branch(Uint32LessThan(Uint32Constant(Smi::kMaxValue), value), &if_overflow,
         &if_not_overflow);

  BIND(&if_not_overflow);
  {
    // The {value} is definitely in valid Smi range.
    var_result = SmiTag(Signed(ChangeUint32ToWord(value)));
  }
  Goto(&if_join);

  BIND(&if_overflow);
  {
    TNode<Float64T> float64_value = ChangeUint32ToFloat64(value);
    var_result = AllocateHeapNumberWithValue(float64_value);
  }
  Goto(&if_join);

  BIND(&if_join);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::ChangeUintPtrToTagged(TNode<UintPtrT> value) {
  Label if_overflow(this, Label::kDeferred), if_not_overflow(this),
      if_join(this);
  TVARIABLE(Number, var_result);
  // If {value} > 2^31 - 1, we need to store it in a HeapNumber.
  Branch(UintPtrLessThan(UintPtrConstant(Smi::kMaxValue), value), &if_overflow,
         &if_not_overflow);

  BIND(&if_not_overflow);
  {
    // The {value} is definitely in valid Smi range.
    var_result = SmiTag(Signed(value));
  }
  Goto(&if_join);

  BIND(&if_overflow);
  {
    TNode<Float64T> float64_value = ChangeUintPtrToFloat64(value);
    var_result = AllocateHeapNumberWithValue(float64_value);
  }
  Goto(&if_join);

  BIND(&if_join);
  return var_result.value();
}

TNode<Int32T> CodeStubAssembler::ChangeBoolToInt32(TNode<BoolT> b) {
  return UncheckedCast<Int32T>(b);
}

TNode<String> CodeStubAssembler::ToThisString(TNode<Context> context,
                                              TNode<Object> value,
                                              TNode<String> method_name) {
  TVARIABLE(Object, var_value, value);

  // Check if the {value} is a Smi or a HeapObject.
  Label if_valueissmi(this, Label::kDeferred), if_valueisnotsmi(this),
      if_valueisstring(this);
  Branch(TaggedIsSmi(value), &if_valueissmi, &if_valueisnotsmi);
  BIND(&if_valueisnotsmi);
  {
    // Load the instance type of the {value}.
    TNode<Uint16T> value_instance_type = LoadInstanceType(CAST(value));

    // Check if the {value} is already String.
    Label if_valueisnotstring(this, Label::kDeferred);
    Branch(IsStringInstanceType(value_instance_type), &if_valueisstring,
           &if_valueisnotstring);
    BIND(&if_valueisnotstring);
    {
      // Check if the {value} is null.
      Label if_valueisnullorundefined(this, Label::kDeferred);
      GotoIf(IsNullOrUndefined(value), &if_valueisnullorundefined);
      // Convert the {value} to a String.
      var_value = CallBuiltin(Builtin::kToString, context, value);
      Goto(&if_valueisstring);

      BIND(&if_valueisnullorundefined);
      {
        // The {value} is either null or undefined.
        ThrowTypeError(context, MessageTemplate::kCalledOnNullOrUndefined,
                       method_name);
      }
    }
  }
  BIND(&if_valueissmi);
  {
    // The {value} is a Smi, convert it to a String.
    var_value = CallBuiltin(Builtin::kNumberToString, context, value);
    Goto(&if_valueisstring);
  }
  BIND(&if_valueisstring);
  return CAST(var_value.value());
}

// This has platform-specific and ill-defined behavior for negative inputs.
TNode<Uint32T> CodeStubAssembler::ChangeNonNegativeNumberToUint32(
    TNode<Number> value) {
  TVARIABLE(Uint32T, var_result);
  Label if_smi(this), if_heapnumber(this, Label::kDeferred), done(this);
  Branch(TaggedIsSmi(value), &if_smi, &if_heapnumber);
  BIND(&if_smi);
  {
    var_result = Unsigned(SmiToInt32(CAST(value)));
    Goto(&done);
  }
  BIND(&if_heapnumber);
  {
    var_result = ChangeFloat64ToUint32(LoadHeapNumberValue(CAST(value)));
    Goto(&done);
  }
  BIND(&done);
  return var_result.value();
}

TNode<Float64T> CodeStubAssembler::ChangeNumberToFloat64(TNode<Number> value) {
  TVARIABLE(Float64T, result);
  Label smi(this);
  Label done(this, &result);
  GotoIf(TaggedIsSmi(value), &smi);
  result = LoadHeapNumberValue(CAST(value));
  Goto(&done);

  BIND(&smi);
  {
    result = SmiToFloat64(CAST(value));
    Goto(&done);
  }

  BIND(&done);
  return result.value();
}

TNode<Int32T> CodeStubAssembler::ChangeTaggedNonSmiToInt32(
    TNode<Context> context, TNode<HeapObject> input) {
  return Select<Int32T>(
      IsHeapNumber(input),
      [=, this] {
        return Signed(TruncateFloat64ToWord32(LoadHeapNumberValue(input)));
      },
      [=, this] {
        return TruncateNumberToWord32(
            CAST(CallBuiltin(Builtin::kNonNumberToNumber, context, input)));
      });
}

TNode<Float64T> CodeStubAssembler::ChangeTaggedToFloat64(TNode<Context> context,
                                                         TNode<Object> input) {
  TVARIABLE(Float64T, var_result);
  Label end(this), not_smi(this);

  GotoIfNot(TaggedIsSmi(input), &not_smi);
  var_result = SmiToFloat64(CAST(input));
  Goto(&end);

  BIND(&not_smi);
  var_result = Select<Float64T>(
      IsHeapNumber(CAST(input)),
      [=, this] { return LoadHeapNumberValue(CAST(input)); },
      [=, this] {
        return ChangeNumberToFloat64(
            CAST(CallBuiltin(Builtin::kNonNumberToNumber, context, input)));
      });
  Goto(&end);

  BIND(&end);
  return var_result.value();
}

TNode<WordT> CodeStubAssembler::TimesSystemPointerSize(TNode<WordT> value) {
  return WordShl(value, kSystemPointerSizeLog2);
}

TNode<WordT> CodeStubAssembler::TimesTaggedSize(TNode<WordT> value) {
  return WordShl(value, kTaggedSizeLog2);
}

TNode<WordT> CodeStubAssembler::TimesDoubleSize(TNode<WordT> value) {
  return WordShl(value, kDoubleSizeLog2);
}

TNode<Object> CodeStubAssembler::ToThisValue(TNode<Context> context,
                                             TNode<Object> input_value,
                                             PrimitiveType primitive_type,
                                             char const* method_name) {
  // We might need to loop once due to JSPrimitiveWrapper unboxing.
  TVARIABLE(Object, var_value, input_value);
  Label loop(this, &var_value), done_loop(this),
      done_throw(this, Label::kDeferred);
  Goto(&loop);
  BIND(&loop);
  {
    // Check if the {value} is a Smi or a HeapObject.
    GotoIf(
        TaggedIsSmi(var_value.value()),
        (primitive_type == PrimitiveType::kNumber) ? &done_loop : &done_throw);

    TNode<HeapObject> value = CAST(var_value.value());

    // Load the map of the {value}.
    TNode<Map> value_map = LoadMap(value);

    // Load the instance type of the {value}.
    TNode<Uint16T> value_instance_type = LoadMapInstanceType(value_map);

    // Check if {value} is a JSPrimitiveWrapper.
    Label if_valueiswrapper(this, Label::kDeferred), if_valueisnotwrapper(this);
    Branch(InstanceTypeEqual(value_instance_type, JS_PRIMITIVE_WRAPPER_TYPE),
           &if_valueiswrapper, &if_valueisnotwrapper);

    BIND(&if_valueiswrapper);
    {
      // Load the actual value from the {value}.
      var_value = LoadObjectField(value, JSPrimitiveWrapper::kValueOffset);
      Goto(&loop);
    }

    BIND(&if_valueisnotwrapper);
    {
      switch (primitive_type) {
        case PrimitiveType::kBoolean:
          GotoIf(TaggedEqual(value_map, BooleanMapConstant()), &done_loop);
          break;
        case PrimitiveType::kNumber:
          GotoIf(TaggedEqual(value_map, HeapNumberMapConstant()), &done_loop);
          break;
        case PrimitiveType::kString:
          GotoIf(IsStringInstanceType(value_instance_type), &done_loop);
          break;
        case PrimitiveType::kSymbol:
          GotoIf(TaggedEqual(value_map, SymbolMapConstant()), &done_loop);
          break;
      }
      Goto(&done_throw);
    }
  }

  BIND(&done_throw);
  {
    const char* primitive_name = nullptr;
    switch (primitive_type) {
      case PrimitiveType::kBoolean:
        primitive_name = "Boolean";
        break;
      case PrimitiveType::kNumber:
        primitive_name = "Number";
        break;
      case PrimitiveType::kString:
        primitive_name = "String";
        break;
      case PrimitiveType::kSymbol:
        primitive_name = "Symbol";
        break;
    }
    CHECK_NOT_NULL(primitive_name);

    // The {value} is not a compatible receiver for this method.
    ThrowTypeError(context, MessageTemplate::kNotGeneric, method_name,
                   primitive_name);
  }

  BIND(&done_loop);
  return var_value.value();
}

void CodeStubAssembler::ThrowIfNotInstanceType(TNode<Context> context,
                                               TNode<Object> value,
                                               InstanceType instance_type,
                                               char const* method_name) {
  Label out(this), throw_exception(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(value), &throw_exception);

  // Load the instance type of the {value}.
  TNode<Map> map = LoadMap(CAST(value));
  const TNode<Uint16T> value_instance_type = LoadMapInstanceType(map);

  Branch(Word32Equal(value_instance_type, Int32Constant(instance_type)), &out,
         &throw_exception);

  // The {value} is not a compatible receiver for this method.
  BIND(&throw_exception);
  ThrowTypeError(context, MessageTemplate::kIncompatibleMethodReceiver,
                 StringConstant(method_name), value);

  BIND(&out);
}

void CodeStubAssembler::ThrowIfNotJSReceiver(TNode<Context> context,
                                             TNode<Object> value,
                                             MessageTemplate msg_template,
                                             const char* method_name) {
  Label done(this), throw_exception(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(value), &throw_exception);

  Branch(JSAnyIsNotPrimitive(CAST(value)), &done, &throw_exception);

  // The {value} is not a compatible receiver for this method.
  BIND(&throw_exception);
  ThrowTypeError(context, msg_template, StringConstant(method_name), value);

  BIND(&done);
}

void CodeStubAssembler::ThrowIfNotCallable(TNode<Context> context,
                                           TNode<Object> value,
                                           const char* method_name) {
  Label out(this), throw_exception(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(value), &throw_exception);
  Branch(IsCallable(CAST(value)), &out, &throw_exception);

  // The {value} is not a compatible receiver for this method.
  BIND(&throw_exception);
  ThrowTypeError(context, MessageTemplate::kCalledNonCallable, method_name);

  BIND(&out);
}

void CodeStubAssembler::ThrowRangeError(TNode<Context> context,
                                        MessageTemplate message,
                                        std::optional<TNode<Object>> arg0,
                                        std::optional<TNode<Object>> arg1,
                                        std::optional<TNode<Object>> arg2) {
  TNode<Smi> template_index = SmiConstant(static_cast<int>(message));
  if (!arg0) {
    CallRuntime(Runtime::kThrowRangeError, context, template_index);
  } else if (!arg1) {
    CallRuntime(Runtime::kThrowRangeError, context, template_index, *arg0);
  } else if (!arg2) {
    CallRuntime(Runtime::kThrowRangeError, context, template_index, *arg0,
                *arg1);
  } else {
    CallRuntime(Runtime::kThrowRangeError, context, template_index, *arg0,
                *arg1, *arg2);
  }
  Unreachable();
}

void CodeStubAssembler::ThrowTypeError(TNode<Context> context,
                                       MessageTemplate message,
                                       char const* arg0, char const* arg1) {
  std::optional<TNode<Object>> arg0_node;
  if (arg0) arg0_node = StringConstant(arg0);
  std::optional<TNode<Object>> arg1_node;
  if (arg1) arg1_node = StringConstant(arg1);
  ThrowTypeError(context, message, arg0_node, arg1_node);
}

void CodeStubAssembler::ThrowTypeError(TNode<Context> context,
                                       MessageTemplate message,
                                       std::optional<TNode<Object>> arg0,
                                       std::optional<TNode<Object>> arg1,
                                       std::optional<TNode<Object>> arg2) {
  TNode<Smi> template_index = SmiConstant(static_cast<int>(message));
  if (!arg0) {
    CallRuntime(Runtime::kThrowTypeError, context, template_index);
  } else if (!arg1) {
    CallRuntime(Runtime::kThrowTypeError, context, template_index, *arg0);
  } else if (!arg2) {
    CallRuntime(Runtime::kThrowTypeError, context, template_index, *arg0,
                *arg1);
  } else {
    CallRuntime(Runtime::kThrowTypeError, context, template_index, *arg0, *arg1,
                *arg2);
  }
  Unreachable();
}

void CodeStubAssembler::TerminateExecution(TNode<Context> context) {
  CallRuntime(Runtime::kTerminateExecution, context);
  Unreachable();
}

TNode<HeapObject> CodeStubAssembler::GetPendingMessage() {
  TNode<ExternalReference> pending_message = ExternalConstant(
      ExternalReference::address_of_pending_message(isolate()));
  return UncheckedCast<HeapObject>(LoadFullTagged(pending_message));
}
void CodeStubAssembler::SetPendingMessage(TNode<HeapObject> message) {
  CSA_DCHECK(this, Word32Or(IsTheHole(message),
                            InstanceTypeEqual(LoadInstanceType(message),
                                              JS_MESSAGE_OBJECT_TYPE)));
  TNode<ExternalReference> pending_message = ExternalConstant(
      ExternalReference::address_of_pending_message(isolate()));
  StoreFullTaggedNoWriteBarrier(pending_message, message);
}

TNode<BoolT> CodeStubAssembler::IsExecutionTerminating() {
  TNode<HeapObject> pending_message = GetPendingMessage();
  return TaggedEqual(pending_message,
                     LoadRoot(RootIndex::kTerminationException));
}

TNode<Object> CodeStubAssembler::GetContinuationPreservedEmbedderData() {
  TNode<ExternalReference> continuation_data =
      IsolateField(IsolateFieldId::kContinuationPreservedEmbedderData);
  return LoadFullTagged(continuation_data);
}

void CodeStubAssembler::SetContinuationPreservedEmbedderData(
    TNode<Object> value) {
  TNode<ExternalReference> continuation_data =
      IsolateField(IsolateFieldId::kContinuationPreservedEmbedderData);
  StoreFullTaggedNoWriteBarrier(continuation_data, value);
}

TNode<BoolT> CodeStubAssembler::InstanceTypeEqual(TNode<Int32T> instance_type,
                                                  int type) {
  return Word32Equal(instance_type, Int32Constant(type));
}

TNode<BoolT> CodeStubAssembler::IsDictionaryMap(TNode<Map> map) {
  return IsSetWord32<Map::Bits3::IsDictionaryMapBit>(LoadMapBitField3(map));
}

TNode<BoolT> CodeStubAssembler::IsExtensibleMap(TNode<Map> map) {
  return IsSetWord32<Map::Bits3::IsExtensibleBit>(LoadMapBitField3(map));
}

TNode<BoolT> CodeStubAssembler::IsExtensibleNonPrototypeMap(TNode<Map> map) {
  int kMask =
      Map::Bits3::IsExtensibleBit::kMask | Map::Bits3::IsPrototypeMapBit::kMask;
  int kExpected = Map::Bits3::IsExtensibleBit::kMask;
  return Word32Equal(Word32And(LoadMapBitField3(map), Int32Constant(kMask)),
                     Int32Constant(kExpected));
}

TNode<BoolT> CodeStubAssembler::IsCallableMap(TNode<Map> map) {
  return IsSetWord32<Map::Bits1::IsCallableBit>(LoadMapBitField(map));
}

TNode<BoolT> CodeStubAssembler::IsDeprecatedMap(TNode<Map> map) {
  return IsSetWord32<Map::Bits3::IsDeprecatedBit>(LoadMapBitField3(map));
}

TNode<BoolT> CodeStubAssembler::IsUndetectableMap(TNode<Map> map) {
  return IsSetWord32<Map::Bits1::IsUndetectableBit>(LoadMapBitField(map));
}

TNode<BoolT> CodeStubAssembler::IsNoElementsProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = NoElementsProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsMegaDOMProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = MegaDOMProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsArrayIteratorProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = ArrayIteratorProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsPromiseResolveProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = PromiseResolveProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsPromiseThenProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = PromiseThenProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsArraySpeciesProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = ArraySpeciesProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsIsConcatSpreadableProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = IsConcatSpreadableProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsTypedArraySpeciesProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = TypedArraySpeciesProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsRegExpSpeciesProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = RegExpSpeciesProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsPromiseSpeciesProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = PromiseSpeciesProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT>
CodeStubAssembler::IsNumberStringNotRegexpLikeProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = NumberStringNotRegexpLikeProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsSetIteratorProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = SetIteratorProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsMapIteratorProtectorCellInvalid() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = MapIteratorProtectorConstant();
  TNode<Object> cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
  return TaggedEqual(cell_value, invalid);
}

TNode<BoolT> CodeStubAssembler::IsPrototypeInitialArrayPrototype(
    TNode<Context> context, TNode<Map> map) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Object> initial_array_prototype = LoadContextElement(
      native_context, Context::INITIAL_ARRAY_PROTOTYPE_INDEX);
  TNode<HeapObject> proto = LoadMapPrototype(map);
  return TaggedEqual(proto, initial_array_prototype);
}

TNode<BoolT> CodeStubAssembler::IsPrototypeTypedArrayPrototype(
    TNode<Context> context, TNode<Map> map) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Object> typed_array_prototype =
      LoadContextElement(native_context, Context::TYPED_ARRAY_PROTOTYPE_INDEX);
  TNode<HeapObject> proto = LoadMapPrototype(map);
  TNode<HeapObject> proto_of_proto = Select<HeapObject>(
      IsJSObject(proto), [=, this] { return LoadMapPrototype(LoadMap(proto)); },
      [=, this] { return NullConstant(); });
  return TaggedEqual(proto_of_proto, typed_array_prototype);
}

void CodeStubAssembler::InvalidateStringWrapperToPrimitiveProtector() {
  TNode<Smi> invalid = SmiConstant(Protectors::kProtectorInvalid);
  TNode<PropertyCell> cell = StringWrapperToPrimitiveProtectorConstant();
  StoreObjectField(cell, PropertyCell::kValueOffset, invalid);
}

TNode<BoolT> CodeStubAssembler::IsFastAliasedArgumentsMap(
    TNode<Context> context, TNode<Map> map) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Object> arguments_map = LoadContextElement(
      native_context, Context::FAST_ALIASED_ARGUMENTS_MAP_INDEX);
  return TaggedEqual(arguments_map, map);
}

TNode<BoolT> CodeStubAssembler::IsSlowAliasedArgumentsMap(
    TNode<Context> context, TNode<Map> map) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Object> arguments_map = LoadContextElement(
      native_context, Context::SLOW_ALIASED_ARGUMENTS_MAP_INDEX);
  return TaggedEqual(arguments_map, map);
}

TNode<BoolT> CodeStubAssembler::IsSloppyArgumentsMap(TNode<Context> context,
                                                     TNode<Map> map) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Object> arguments_map =
      LoadContextElement(native_context, Context::SLOPPY_ARGUMENTS_MAP_INDEX);
  return TaggedEqual(arguments_map, map);
}

TNode<BoolT> CodeStubAssembler::IsStrictArgumentsMap(TNode<Context> context,
                                                     TNode<Map> map) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Object> arguments_map =
      LoadContextElement(native_context, Context::STRICT_ARGUMENTS_MAP_INDEX);
  return TaggedEqual(arguments_map, map);
}

TNode<BoolT> CodeStubAssembler::TaggedIsCallable(TNode<Object> object) {
  return Select<BoolT>(
      TaggedIsSmi(object), [=, this] { return Int32FalseConstant(); },
      [=, this] {
        return IsCallableMap(LoadMap(UncheckedCast<HeapObject>(object)));
      });
}

TNode<BoolT> CodeStubAssembler::IsCallable(TNode<HeapObject> object) {
  return IsCallableMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::TaggedIsCode(TNode<Object> object) {
  return Select<BoolT>(
      TaggedIsSmi(object), [=, this] { return Int32FalseConstant(); },
      [=, this] { return IsCode(UncheckedCast<HeapObject>(object)); });
}

TNode<BoolT> CodeStubAssembler::IsCode(TNode<HeapObject> object) {
  return HasInstanceType(object, CODE_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsConstructorMap(TNode<Map> map) {
  return IsSetWord32<Map::Bits1::IsConstructorBit>(LoadMapBitField(map));
}

TNode<BoolT> CodeStubAssembler::IsConstructor(TNode<HeapObject> object) {
  return IsConstructorMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsFunctionWithPrototypeSlotMap(TNode<Map> map) {
  return IsSetWord32<Map::Bits1::HasPrototypeSlotBit>(LoadMapBitField(map));
}

TNode<BoolT> CodeStubAssembler::IsSpecialReceiverInstanceType(
    TNode<Int32T> instance_type) {
  static_assert(JS_GLOBAL_OBJECT_TYPE <= LAST_SPECIAL_RECEIVER_TYPE);
  return Int32LessThanOrEqual(instance_type,
                              Int32Constant(LAST_SPECIAL_RECEIVER_TYPE));
}

TNode<BoolT> CodeStubAssembler::IsCustomElementsReceiverInstanceType(
    TNode<Int32T> instance_type) {
  return Int32LessThanOrEqual(instance_type,
                              Int32Constant(LAST_CUSTOM_ELEMENTS_RECEIVER));
}

TNode<BoolT> CodeStubAssembler::IsStringInstanceType(
    TNode<Int32T> instance_type) {
  static_assert(INTERNALIZED_TWO_BYTE_STRING_TYPE == FIRST_TYPE);
  return Int32LessThan(instance_type, Int32Constant(FIRST_NONSTRING_TYPE));
}

TNode<BoolT> CodeStubAssembler::IsTemporalInstantInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, JS_TEMPORAL_INSTANT_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsOneByteStringInstanceType(
    TNode<Int32T> instance_type) {
  CSA_DCHECK(this, IsStringInstanceType(instance_type));
  return Word32Equal(
      Word32And(instance_type, Int32Constant(kStringEncodingMask)),
      Int32Constant(kOneByteStringTag));
}

TNode<BoolT> CodeStubAssembler::IsSequentialStringInstanceType(
    TNode<Int32T> instance_type) {
  CSA_DCHECK(this, IsStringInstanceType(instance_type));
  return Word32Equal(
      Word32And(instance_type, Int32Constant(kStringRepresentationMask)),
      Int32Constant(kSeqStringTag));
}

TNode<BoolT> CodeStubAssembler::IsSeqOneByteStringInstanceType(
    TNode<Int32T> instance_type) {
  CSA_DCHECK(this, IsStringInstanceType(instance_type));
  return Word32Equal(
      Word32And(instance_type,
                Int32Constant(kStringRepresentationAndEncodingMask)),
      Int32Constant(kSeqOneByteStringTag));
}

TNode<BoolT> CodeStubAssembler::IsConsStringInstanceType(
    TNode<Int32T> instance_type) {
  CSA_DCHECK(this, IsStringInstanceType(instance_type));
  return Word32Equal(
      Word32And(instance_type, Int32Constant(kStringRepresentationMask)),
      Int32Constant(kConsStringTag));
}

TNode<BoolT> CodeStubAssembler::IsIndirectStringInstanceType(
    TNode<Int32T> instance_type) {
  CSA_DCHECK(this, IsStringInstanceType(instance_type));
  static_assert(kIsIndirectStringMask == 0x1);
  static_assert(kIsIndirectStringTag == 0x1);
  return UncheckedCast<BoolT>(
      Word32And(instance_type, Int32Constant(kIsIndirectStringMask)));
}

TNode<BoolT> CodeStubAssembler::IsExternalStringInstanceType(
    TNode<Int32T> instance_type) {
  CSA_DCHECK(this, IsStringInstanceType(instance_type));
  return Word32Equal(
      Word32And(instance_type, Int32Constant(kStringRepresentationMask)),
      Int32Constant(kExternalStringTag));
}

TNode<BoolT> CodeStubAssembler::IsUncachedExternalStringInstanceType(
    TNode<Int32T> instance_type) {
  CSA_DCHECK(this, IsStringInstanceType(instance_type));
  static_assert(kUncachedExternalStringTag != 0);
  return IsSetWord32(instance_type, kUncachedExternalStringMask);
}

TNode<BoolT> CodeStubAssembler::IsJSReceiverInstanceType(
    TNode<Int32T> instance_type) {
  static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
  return Int32GreaterThanOrEqual(instance_type,
                                 Int32Constant(FIRST_JS_RECEIVER_TYPE));
}

TNode<BoolT> CodeStubAssembler::IsSequentialStringMap(TNode<Map> map) {
#if V8_STATIC_ROOTS_BOOL
  // Both sequential string maps are allocated at the start of the read only
  // heap, so we can use a single comparison to check for them.
  static_assert(
      InstanceTypeChecker::kUniqueMapRangeOfStringType::kSeqString.first == 0);
  return IsInRange(
      TruncateIntPtrToInt32(BitcastTaggedToWord(map)),
      InstanceTypeChecker::kUniqueMapRangeOfStringType::kSeqString.first,
      InstanceTypeChecker::kUniqueMapRangeOfStringType::kSeqString.second);
#else
  return IsSequentialStringInstanceType(LoadMapInstanceType(map));
#endif
}

TNode<BoolT> CodeStubAssembler::IsExternalStringMap(TNode<Map> map) {
#if V8_STATIC_ROOTS_BOOL
  return IsInRange(
      TruncateIntPtrToInt32(BitcastTaggedToWord(map)),
      InstanceTypeChecker::kUniqueMapRangeOfStringType::kExternalString.first,
      InstanceTypeChecker::kUniqueMapRangeOfStringType::kExternalString.second);
#else
  return IsExternalStringInstanceType(LoadMapInstanceType(map));
#endif
}

TNode<BoolT> CodeStubAssembler::IsUncachedExternalStringMap(TNode<Map> map) {
#if V8_STATIC_ROOTS_BOOL
  return IsInRange(
      TruncateIntPtrToInt32(BitcastTaggedToWord(map)),
      InstanceTypeChecker::kUniqueMapRangeOfStringType::kUncachedExternalString
          .first,
      InstanceTypeChecker::kUniqueMapRangeOfStringType::kUncachedExternalString
          .second);
#else
  return IsUncachedExternalStringInstanceType(LoadMapInstanceType(map));
#endif
}

TNode<BoolT> CodeStubAssembler::IsOneByteStringMap(TNode<Map> map) {
#if V8_STATIC_ROOTS_BOOL
  CSA_DCHECK(this, IsStringInstanceType(LoadMapInstanceType(map)));

  // These static asserts make sure that the following bit magic on the map word
  // is safe. See the definition of kStringMapEncodingMask for an explanation.
#define VALIDATE_STRING_MAP_ENCODING_BIT(instance_type, size, name, Name) \
  static_assert(                                                          \
      ((instance_type & kStringEncodingMask) == kOneByteStringTag) ==     \
      ((StaticReadOnlyRoot::k##Name##Map &                                \
        InstanceTypeChecker::kStringMapEncodingMask) ==                   \
       InstanceTypeChecker::kOneByteStringMapBit));                       \
  static_assert(                                                          \
      ((instance_type & kStringEncodingMask) == kTwoByteStringTag) ==     \
      ((StaticReadOnlyRoot::k##Name##Map &                                \
        InstanceTypeChecker::kStringMapEncodingMask) ==                   \
       InstanceTypeChecker::kTwoByteStringMapBit));
  STRING_TYPE_LIST(VALIDATE_STRING_MAP_ENCODING_BIT)
#undef VALIDATE_STRING_TYPE_RANGES

  return Word32Equal(
      Word32And(TruncateIntPtrToInt32(BitcastTaggedToWord(map)),
                Int32Constant(InstanceTypeChecker::kStringMapEncodingMask)),
      Int32Constant(InstanceTypeChecker::kOneByteStringMapBit));
#else
  return IsOneByteStringInstanceType(LoadMapInstanceType(map));
#endif
}

TNode<BoolT> CodeStubAssembler::IsJSReceiverMap(TNode<Map> map) {
  return IsJSReceiverInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::JSAnyIsNotPrimitiveMap(TNode<Map> map) {
#if V8_STATIC_ROOTS_BOOL
  // Assuming this is only called with primitive objects or js receivers.
  CSA_DCHECK(this, Word32Or(IsPrimitiveInstanceType(LoadMapInstanceType(map)),
                            IsJSReceiverMap(map)));
  // All primitive object's maps are allocated at the start of the read only
  // heap. Thus JS_RECEIVER's must have maps with larger (compressed) addresses.
  return Uint32GreaterThanOrEqual(
      TruncateIntPtrToInt32(BitcastTaggedToWord(map)),
      Int32Constant(InstanceTypeChecker::kNonJsReceiverMapLimit));
#else
  return IsJSReceiverMap(map);
#endif
}

TNode<BoolT> CodeStubAssembler::IsJSReceiver(TNode<HeapObject> object) {
  return IsJSReceiverMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::JSAnyIsNotPrimitive(TNode<HeapObject> object) {
#if V8_STATIC_ROOTS_BOOL
  return JSAnyIsNotPrimitiveMap(LoadMap(object));
#else
  return IsJSReceiver(object);
#endif
}

TNode<BoolT> CodeStubAssembler::IsNullOrJSReceiver(TNode<HeapObject> object) {
  return UncheckedCast<BoolT>(Word32Or(IsJSReceiver(object), IsNull(object)));
}

TNode<BoolT> CodeStubAssembler::IsNullOrUndefined(TNode<Object> value) {
  // TODO(ishell): consider using Select<BoolT>() here.
  return UncheckedCast<BoolT>(Word32Or(IsUndefined(value), IsNull(value)));
}

TNode<BoolT> CodeStubAssembler::IsJSGlobalProxyInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, JS_GLOBAL_PROXY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSGlobalProxyMap(TNode<Map> map) {
  return IsJSGlobalProxyInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSGlobalProxy(TNode<HeapObject> object) {
  return IsJSGlobalProxyMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSGeneratorMap(TNode<Map> map) {
  return InstanceTypeEqual(LoadMapInstanceType(map), JS_GENERATOR_OBJECT_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSObjectInstanceType(
    TNode<Int32T> instance_type) {
  static_assert(LAST_JS_OBJECT_TYPE == LAST_TYPE);
  return Int32GreaterThanOrEqual(instance_type,
                                 Int32Constant(FIRST_JS_OBJECT_TYPE));
}

TNode<BoolT> CodeStubAssembler::IsJSApiObjectInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, JS_API_OBJECT_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSObjectMap(TNode<Map> map) {
  return IsJSObjectInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSApiObjectMap(TNode<Map> map) {
  return IsJSApiObjectInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSObject(TNode<HeapObject> object) {
  return IsJSObjectMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSApiObject(TNode<HeapObject> object) {
  return IsJSApiObjectMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSFinalizationRegistryMap(TNode<Map> map) {
  return InstanceTypeEqual(LoadMapInstanceType(map),
                           JS_FINALIZATION_REGISTRY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSFinalizationRegistry(
    TNode<HeapObject> object) {
  return IsJSFinalizationRegistryMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSPromiseMap(TNode<Map> map) {
  return InstanceTypeEqual(LoadMapInstanceType(map), JS_PROMISE_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSPromise(TNode<HeapObject> object) {
  return IsJSPromiseMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSProxy(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_PROXY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSStringIterator(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_STRING_ITERATOR_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSShadowRealm(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_SHADOW_REALM_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSRegExpStringIterator(
    TNode<HeapObject> object) {
  return HasInstanceType(object, JS_REG_EXP_STRING_ITERATOR_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsMap(TNode<HeapObject> object) {
  return HasInstanceType(object, MAP_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSPrimitiveWrapperInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, JS_PRIMITIVE_WRAPPER_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSPrimitiveWrapper(TNode<HeapObject> object) {
  return IsJSPrimitiveWrapperMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSPrimitiveWrapperMap(TNode<Map> map) {
  return IsJSPrimitiveWrapperInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSWrappedFunction(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_WRAPPED_FUNCTION_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSArrayInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, JS_ARRAY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSArray(TNode<HeapObject> object) {
  return IsJSArrayMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSArrayMap(TNode<Map> map) {
  return IsJSArrayInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSArrayIterator(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_ARRAY_ITERATOR_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsAlwaysSharedSpaceJSObjectInstanceType(
    TNode<Int32T> instance_type) {
  return IsInRange(instance_type, FIRST_ALWAYS_SHARED_SPACE_JS_OBJECT_TYPE,
                   LAST_ALWAYS_SHARED_SPACE_JS_OBJECT_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSSharedArrayInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, JS_SHARED_ARRAY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSSharedArrayMap(TNode<Map> map) {
  return IsJSSharedArrayInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSSharedArray(TNode<HeapObject> object) {
  return IsJSSharedArrayMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSSharedArray(TNode<Object> object) {
  return Select<BoolT>(
      TaggedIsSmi(object), [=, this] { return Int32FalseConstant(); },
      [=, this] {
        TNode<HeapObject> heap_object = CAST(object);
        return IsJSSharedArray(heap_object);
      });
}

TNode<BoolT> CodeStubAssembler::IsJSSharedStructInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, JS_SHARED_STRUCT_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSSharedStructMap(TNode<Map> map) {
  return IsJSSharedStructInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSSharedStruct(TNode<HeapObject> object) {
  return IsJSSharedStructMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSSharedStruct(TNode<Object> object) {
  return Select<BoolT>(
      TaggedIsSmi(object), [=, this] { return Int32FalseConstant(); },
      [=, this] {
        TNode<HeapObject> heap_object = CAST(object);
        return IsJSSharedStruct(heap_object);
      });
}

TNode<BoolT> CodeStubAssembler::IsJSAsyncGeneratorObject(
    TNode<HeapObject> object) {
  return HasInstanceType(object, JS_ASYNC_GENERATOR_OBJECT_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsFixedArray(TNode<HeapObject> object) {
  return HasInstanceType(object, FIXED_ARRAY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsFixedArraySubclass(TNode<HeapObject> object) {
  TNode<Uint16T> instance_type = LoadInstanceType(object);
  return UncheckedCast<BoolT>(
      Word32And(Int32GreaterThanOrEqual(instance_type,
                                        Int32Constant(FIRST_FIXED_ARRAY_TYPE)),
                Int32LessThanOrEqual(instance_type,
                                     Int32Constant(LAST_FIXED_ARRAY_TYPE))));
}

TNode<BoolT> CodeStubAssembler::IsNotWeakFixedArraySubclass(
    TNode<HeapObject> object) {
  TNode<Uint16T> instance_type = LoadInstanceType(object);
  return UncheckedCast<BoolT>(Word32Or(
      Int32LessThan(instance_type, Int32Constant(FIRST_WEAK_FIXED_ARRAY_TYPE)),
      Int32GreaterThan(instance_type,
                       Int32Constant(LAST_WEAK_FIXED_ARRAY_TYPE))));
}

TNode<BoolT> CodeStubAssembler::IsPropertyArray(TNode<HeapObject> object) {
  return HasInstanceType(object, PROPERTY_ARRAY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsPromiseReactionJobTask(
    TNode<HeapObject> object) {
  TNode<Uint16T> instance_type = LoadInstanceType(object);
  return IsInRange(instance_type, FIRST_PROMISE_REACTION_JOB_TASK_TYPE,
                   LAST_PROMISE_REACTION_JOB_TASK_TYPE);
}

// This complicated check is due to elements oddities. If a smi array is empty
// after Array.p.shift, it is replaced by the empty array constant. If it is
// later filled with a double element, we try to grow it but pass in a double
// elements kind. Usually this would cause a size mismatch (since the source
// fixed array has HOLEY_ELEMENTS and destination has
// HOLEY_DOUBLE_ELEMENTS), but we don't have to worry about it when the
// source array is empty.
// TODO(jgruber): It might we worth creating an empty_double_array constant to
// simplify this case.
TNode<BoolT> CodeStubAssembler::IsFixedArrayWithKindOrEmpty(
    TNode<FixedArrayBase> object, ElementsKind kind) {
  Label out(this);
  TVARIABLE(BoolT, var_result, Int32TrueConstant());

  GotoIf(IsFixedArrayWithKind(object, kind), &out);

  const TNode<Smi> length = LoadFixedArrayBaseLength(object);
  GotoIf(SmiEqual(length, SmiConstant(0)), &out);

  var_result = Int32FalseConstant();
  Goto(&out);

  BIND(&out);
  return var_result.value();
}

TNode<BoolT> CodeStubAssembler::IsFixedArrayWithKind(TNode<HeapObject> object,
                                                     ElementsKind kind) {
  if (IsDoubleElementsKind(kind)) {
    return IsFixedDoubleArray(object);
  } else {
    DCHECK(IsSmiOrObjectElementsKind(kind) || IsSealedElementsKind(kind) ||
           IsNonextensibleElementsKind(kind));
    return IsFixedArraySubclass(object);
  }
}

TNode<BoolT> CodeStubAssembler::IsBoolean(TNode<HeapObject> object) {
  return IsBooleanMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsPropertyCell(TNode<HeapObject> object) {
  return IsPropertyCellMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsHeapNumberInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, HEAP_NUMBER_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsNotAnyHole(TNode<Object> object) {
  return Select<BoolT>(
      TaggedIsSmi(object), [=, this] { return Int32TrueConstant(); },
      [=, this] {
        return Word32BinaryNot(IsHoleInstanceType(
            LoadInstanceType(UncheckedCast<HeapObject>(object))));
      });
}

TNode<BoolT> CodeStubAssembler::IsHoleInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, HOLE_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsOddball(TNode<HeapObject> object) {
  return IsOddballInstanceType(LoadInstanceType(object));
}

TNode<BoolT> CodeStubAssembler::IsOddballInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, ODDBALL_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsName(TNode<HeapObject> object) {
#if V8_STATIC_ROOTS_BOOL
  TNode<Map> map = LoadMap(object);
  TNode<Word32T> map_as_word32 = ReinterpretCast<Word32T>(map);
  static_assert(InstanceTypeChecker::kStringMapUpperBound + Map::kSize ==
                StaticReadOnlyRoot::kSymbolMap);
  return Uint32LessThanOrEqual(map_as_word32,
                               Int32Constant(StaticReadOnlyRoot::kSymbolMap));
#else
  return IsNameInstanceType(LoadInstanceType(object));
#endif
}

TNode<BoolT> CodeStubAssembler::IsNameInstanceType(
    TNode<Int32T> instance_type) {
  return Int32LessThanOrEqual(instance_type, Int32Constant(LAST_NAME_TYPE));
}

TNode<BoolT> CodeStubAssembler::IsString(TNode<HeapObject> object) {
#if V8_STATIC_ROOTS_BOOL
  TNode<Map> map = LoadMap(object);
  TNode<Word32T> map_as_word32 =
      TruncateIntPtrToInt32(BitcastTaggedToWord(map));
  return Uint32LessThanOrEqual(
      map_as_word32, Int32Constant(InstanceTypeChecker::kStringMapUpperBound));
#else
  return IsStringInstanceType(LoadInstanceType(object));
#endif
}

TNode<Word32T> CodeStubAssembler::IsStringWrapper(TNode<HeapObject> object) {
  return IsStringWrapperElementsKind(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsSeqOneByteString(TNode<HeapObject> object) {
  return IsSeqOneByteStringInstanceType(LoadInstanceType(object));
}

TNode<BoolT> CodeStubAssembler::IsSymbolInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, SYMBOL_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsInternalizedStringInstanceType(
    TNode<Int32T> instance_type) {
  static_assert(kNotInternalizedTag != 0);
  return Word32Equal(
      Word32And(instance_type,
                Int32Constant(kIsNotStringMask | kIsNotInternalizedMask)),
      Int32Constant(kStringTag | kInternalizedTag));
}

TNode<BoolT> CodeStubAssembler::IsSharedStringInstanceType(
    TNode<Int32T> instance_type) {
  TNode<BoolT> is_shared = Word32Equal(
      Word32And(instance_type,
                Int32Constant(kIsNotStringMask | kSharedStringMask)),
      Int32Constant(kStringTag | kSharedStringTag));
  // TODO(v8:12007): Internalized strings do not have kSharedStringTag until
  // the shared string table ships.
  return Word32Or(is_shared,
                  Word32And(HasSharedStringTableFlag(),
                            IsInternalizedStringInstanceType(instance_type)));
}

TNode<BoolT> CodeStubAssembler::IsUniqueName(TNode<HeapObject> object) {
  TNode<Uint16T> instance_type = LoadInstanceType(object);
  return Select<BoolT>(
      IsInternalizedStringInstanceType(instance_type),
      [=, this] { return Int32TrueConstant(); },
      [=, this] { return IsSymbolInstanceType(instance_type); });
}

// Semantics: guaranteed not to be an integer index (i.e. contains non-digit
// characters, or is outside MAX_SAFE_INTEGER/size_t range). Note that for
// non-TypedArray receivers, there are additional strings that must be treated
// as named property keys, namely the range [0xFFFFFFFF, MAX_SAFE_INTEGER].
// The hash could be a forwarding index to an integer index.
// For now we conservatively assume that all forwarded hashes could be integer
// indices, allowing false negatives.
// TODO(pthier): We could use 1 bit of the forward index to indicate whether the
// forwarded hash contains an integer index, if this is turns out to be a
// performance issue, at the cost of slowing down creating the forwarded string.
TNode<BoolT> CodeStubAssembler::IsUniqueNameNoIndex(TNode<HeapObject> object) {
  TNode<Uint16T> instance_type = LoadInstanceType(object);
  return Select<BoolT>(
      IsInternalizedStringInstanceType(instance_type),
      [=, this] {
        return IsSetWord32(LoadNameRawHashField(CAST(object)),
                           Name::kDoesNotContainIntegerOrForwardingIndexMask);
      },
      [=, this] { return IsSymbolInstanceType(instance_type); });
}

// Semantics: {object} is a Symbol, or a String that doesn't have a cached
// index. This returns {true} for strings containing representations of
// integers in the range above 9999999 (per kMaxCachedArrayIndexLength)
// and below MAX_SAFE_INTEGER. For CSA_DCHECKs ensuring correct usage, this is
// better than no checking; and we don't have a good/fast way to accurately
// check such strings for being within "array index" (uint32_t) range.
TNode<BoolT> CodeStubAssembler::IsUniqueNameNoCachedIndex(
    TNode<HeapObject> object) {
  TNode<Uint16T> instance_type = LoadInstanceType(object);
  return Select<BoolT>(
      IsInternalizedStringInstanceType(instance_type),
      [=, this] {
        return IsSetWord32(LoadNameRawHash(CAST(object)),
                           Name::kDoesNotContainCachedArrayIndexMask);
      },
      [=, this] { return IsSymbolInstanceType(instance_type); });
}

TNode<BoolT> CodeStubAssembler::IsBigIntInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, BIGINT_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsBigInt(TNode<HeapObject> object) {
  return IsBigIntInstanceType(LoadInstanceType(object));
}

void CodeStubAssembler::GotoIfLargeBigInt(TNode<BigInt> bigint,
                                          Label* true_label) {
  // Small BigInts are BigInts in the range [-2^63 + 1, 2^63 - 1] so that they
  // can fit in 64-bit registers. Excluding -2^63 from the range makes the check
  // simpler and faster. The other BigInts are seen as "large".
  // TODO(panq): We might need to reevaluate of the range of small BigInts.
  DCHECK(Is64());
  Label false_label(this);
  TNode<Uint32T> length =
      DecodeWord32<BigIntBase::LengthBits>(LoadBigIntBitfield(bigint));
  GotoIf(Word32Equal(length, Uint32Constant(0)), &false_label);
  GotoIfNot(Word32Equal(length, Uint32Constant(1)), true_label);
  Branch(WordEqual(UintPtrConstant(0),
                   WordAnd(LoadBigIntDigit(bigint, 0),
                           UintPtrConstant(static_cast<uintptr_t>(
                               1ULL << (sizeof(uintptr_t) * 8 - 1))))),
         &false_label, true_label);
  Bind(&false_label);
}

TNode<BoolT> CodeStubAssembler::IsPrimitiveInstanceType(
    TNode<Int32T> instance_type) {
  return Int32LessThanOrEqual(instance_type,
                              Int32Constant(LAST_PRIMITIVE_HEAP_OBJECT_TYPE));
}

TNode<BoolT> CodeStubAssembler::IsPrivateName(TNode<Symbol> symbol) {
  TNode<Uint32T> flags =
      LoadObjectField<Uint32T>(symbol, offsetof(Symbol, flags_));
  return IsSetWord32<Symbol::IsPrivateNameBit>(flags);
}

TNode<BoolT> CodeStubAssembler::IsHashTable(TNode<HeapObject> object) {
  TNode<Uint16T> instance_type = LoadInstanceType(object);
  return UncheckedCast<BoolT>(
      Word32And(Int32GreaterThanOrEqual(instance_type,
                                        Int32Constant(FIRST_HASH_TABLE_TYPE)),
                Int32LessThanOrEqual(instance_type,
                                     Int32Constant(LAST_HASH_TABLE_TYPE))));
}

TNode<BoolT> CodeStubAssembler::IsEphemeronHashTable(TNode<HeapObject> object) {
  return HasInstanceType(object, EPHEMERON_HASH_TABLE_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsPropertyDictionary(TNode<HeapObject> object) {
  return HasInstanceType(object, PROPERTY_DICTIONARY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsOrderedNameDictionary(
    TNode<HeapObject> object) {
  return HasInstanceType(object, ORDERED_NAME_DICTIONARY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsGlobalDictionary(TNode<HeapObject> object) {
  return HasInstanceType(object, GLOBAL_DICTIONARY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsNumberDictionary(TNode<HeapObject> object) {
  return HasInstanceType(object, NUMBER_DICTIONARY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSGeneratorObject(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_GENERATOR_OBJECT_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsFunctionInstanceType(
    TNode<Int32T> instance_type) {
  return IsInRange(instance_type,
                   FIRST_JS_FUNCTION_OR_BOUND_FUNCTION_OR_WRAPPED_FUNCTION_TYPE,
                   LAST_JS_FUNCTION_OR_BOUND_FUNCTION_OR_WRAPPED_FUNCTION_TYPE);
}
TNode<BoolT> CodeStubAssembler::IsJSFunctionInstanceType(
    TNode<Int32T> instance_type) {
  return IsInRange(instance_type, FIRST_JS_FUNCTION_TYPE,
                   LAST_JS_FUNCTION_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSFunction(TNode<HeapObject> object) {
  return IsJSFunctionMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSBoundFunction(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_BOUND_FUNCTION_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSFunctionMap(TNode<Map> map) {
  return IsJSFunctionInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSTypedArrayInstanceType(
    TNode<Int32T> instance_type) {
  return InstanceTypeEqual(instance_type, JS_TYPED_ARRAY_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSTypedArrayMap(TNode<Map> map) {
  return IsJSTypedArrayInstanceType(LoadMapInstanceType(map));
}

TNode<BoolT> CodeStubAssembler::IsJSTypedArray(TNode<HeapObject> object) {
  return IsJSTypedArrayMap(LoadMap(object));
}

TNode<BoolT> CodeStubAssembler::IsJSArrayBuffer(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_ARRAY_BUFFER_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSDataView(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_DATA_VIEW_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSRabGsabDataView(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_RAB_GSAB_DATA_VIEW_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsJSRegExp(TNode<HeapObject> object) {
  return HasInstanceType(object, JS_REG_EXP_TYPE);
}

TNode<BoolT> CodeStubAssembler::IsNumeric(TNode<Object> object) {
  return Select<BoolT>(
      TaggedIsSmi(object), [=, this] { return Int32TrueConstant(); },
      [=, this] {
        return UncheckedCast<BoolT>(
            Word32Or(IsHeapNumber(CAST(object)), IsBigInt(CAST(object))));
      });
}

TNode<BoolT> CodeStubAssembler::IsNumberNormalized(TNode<Number> number) {
  TVARIABLE(BoolT, var_result, Int32TrueConstant());
  Label out(this);

  GotoIf(TaggedIsSmi(number), &out);

  TNode<Float64T> value = LoadHeapNumberValue(CAST(number));
  TNode<Float64T> smi_min =
      Float64Constant(static_cast<double>(Smi::kMinValue));
  TNode<Float64T> smi_max =
      Float64Constant(static_cast<double>(Smi::kMaxValue));

  GotoIf(Float64LessThan(value, smi_min), &out);
  GotoIf(Float64GreaterThan(value, smi_max), &out);
  GotoIfNot(Float64Equal(value, value), &out);  // NaN.

  var_result = Int32FalseConstant();
  Goto(&out);

  BIND(&out);
  return var_result.value();
}

TNode<BoolT> CodeStubAssembler::IsNumberPositive(TNode<Number> number) {
  return Select<BoolT>(
      TaggedIsSmi(number), [=, this] { return TaggedIsPositiveSmi(number); },
      [=, this] { return IsHeapNumberPositive(CAST(number)); });
}

// TODO(cbruni): Use TNode<HeapNumber> instead of custom name.
TNode<BoolT> CodeStubAssembler::IsHeapNumberPositive(TNode<HeapNumber> number) {
  TNode<Float64T> value = LoadHeapNumberValue(number);
  TNode<Float64T> float_zero = Float64Constant(0.);
  return Float64GreaterThanOrEqual(value, float_zero);
}

TNode<BoolT> CodeStubAssembler::IsNumberNonNegativeSafeInteger(
    TNode<Number> number) {
  return Select<BoolT>(
      // TODO(cbruni): Introduce TaggedIsNonNegateSmi to avoid confusion.
      TaggedIsSmi(number), [=, this] { return TaggedIsPositiveSmi(number); },
      [=, this] {
        TNode<HeapNumber> heap_number = CAST(number);
        return Select<BoolT>(
            IsInteger(heap_number),
            [=, this] { return IsHeapNumberPositive(heap_number); },
            [=, this] { return Int32FalseConstant(); });
      });
}

TNode<BoolT> CodeStubAssembler::IsSafeInteger(TNode<Object> number) {
  return Select<BoolT>(
      TaggedIsSmi(number), [=, this] { return Int32TrueConstant(); },
      [=, this] {
        return Select<BoolT>(
            IsHeapNumber(CAST(number)),
            [=, this] {
              return IsSafeInteger(UncheckedCast<HeapNumber>(number));
            },
            [=, this] { return Int32FalseConstant(); });
      });
}

TNode<BoolT> CodeStubAssembler::IsSafeInteger(TNode<HeapNumber> number) {
  // Load the actual value of {number}.
  TNode<Float64T> number_value = LoadHeapNumberValue(number);
  // Truncate the value of {number} to an integer (or an infinity).
  TNode<Float64T> integer = Float64Trunc(number_value);

  return Select<BoolT>(
      // Check if {number}s value matches the integer (ruling out the
      // infinities).
      Float64Equal(Float64Sub(number_value, integer), Float64Constant(0.0)),
      [=, this] {
        // Check if the {integer} value is in safe integer range.
        return Float64LessThanOrEqual(Float64Abs(integer),
                                      Float64Constant(kMaxSafeInteger));
      },
      [=, this] { return Int32FalseConstant(); });
}

TNode<BoolT> CodeStubAssembler::IsInteger(TNode<Object> number) {
  return Select<BoolT>(
      TaggedIsSmi(number), [=, this] { return Int32TrueConstant(); },
      [=, this] {
        return Select<BoolT>(
            IsHeapNumber(CAST(number)),
            [=, this] { return IsInteger(UncheckedCast<HeapNumber>(number)); },
            [=, this] { return Int32FalseConstant(); });
      });
}

TNode<BoolT> CodeStubAssembler::IsInteger(TNode<HeapNumber> number) {
  TNode<Float64T> number_value = LoadHeapNumberValue(number);
  // Truncate the value of {number} to an integer (or an infinity).
  TNode<Float64T> integer = Float64Trunc(number_value);
  // Check if {number}s value matches the integer (ruling out the infinities).
  return Float64Equal(Float64Sub(number_value, integer), Float64Constant(0.0));
}

TNode<BoolT> CodeStubAssembler::IsHeapNumberUint32(TNode<HeapNumber> number) {
  // Check that the HeapNumber is a valid uint32
  return Select<BoolT>(
      IsHeapNumberPositive(number),
      [=, this] {
        TNode<Float64T> value = LoadHeapNumberValue(number);
        TNode<Uint32T> int_value = TruncateFloat64ToWord32(value);
        return Float64Equal(value, ChangeUint32ToFloat64(int_value));
      },
      [=, this] { return Int32FalseConstant(); });
}

TNode<BoolT> CodeStubAssembler::IsNumberArrayIndex(TNode<Number> number) {
  return Select<BoolT>(
      TaggedIsSmi(number), [=, this] { return TaggedIsPositiveSmi(number); },
      [=, this] { return IsHeapNumberUint32(CAST(number)); });
}

TNode<IntPtrT> CodeStubAssembler::LoadMemoryChunkFlags(
    TNode<HeapObject> object) {
  TNode<IntPtrT> object_word = BitcastTaggedToWord(object);
  TNode<IntPtrT> page_header = MemoryChunkFromAddress(object_word);
  return UncheckedCast<IntPtrT>(
      Load(MachineType::Pointer(), page_header,
           IntPtrConstant(MemoryChunkLayout::kFlagsOffset)));
}

template <typename TIndex>
TNode<BoolT> CodeStubAssembler::FixedArraySizeDoesntFitInNewSpace(
    TNode<TIndex> element_count, int base_size) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT element_count is allowed");
  int max_newspace_elements =
      (kMaxRegularHeapObjectSize - base_size) / kTaggedSize;
  return IntPtrOrSmiGreaterThan(
      element_count, IntPtrOrSmiConstant<TIndex>(max_newspace_elements));
}

TNode<Uint16T> CodeStubAssembler::StringCharCodeAt(TNode<String> string,
                                                   TNode<UintPtrT> index) {
  CSA_DCHECK(this, UintPtrLessThan(index, LoadStringLengthAsWord(string)));

  TVARIABLE(Uint16T, var_result);

  Label return_result(this), if_runtime(this, Label::kDeferred),
      if_stringistwobyte(this), if_stringisonebyte(this);

  ToDirectStringAssembler to_direct(state(), string);
  to_direct.TryToDirect(&if_runtime);
  const TNode<UintPtrT> offset =
      UintPtrAdd(index, Unsigned(to_direct.offset()));
  const TNode<BoolT> is_one_byte = to_direct.IsOneByte();
  const TNode<RawPtrT> string_data = to_direct.PointerToData(&if_runtime);

  // Check if the {string} is a TwoByteSeqString or a OneByteSeqString.
  Branch(is_one_byte, &if_stringisonebyte, &if_stringistwobyte);

  BIND(&if_stringisonebyte);
  {
    var_result = Load<Uint8T>(string_data, offset);
    Goto(&return_result);
  }

  BIND(&if_stringistwobyte);
  {
    var_result = Load<Uint16T>(string_data, WordShl(offset, IntPtrConstant(1)));
    Goto(&return_result);
  }

  BIND(&if_runtime);
  {
    TNode<Object> result =
        CallRuntime(Runtime::kStringCharCodeAt, NoContextConstant(), string,
                    ChangeUintPtrToTagged(index));
    var_result = UncheckedCast<Uint16T>(SmiToInt32(CAST(result)));
    Goto(&return_result);
  }

  BIND(&return_result);
  return var_result.value();
}

TNode<String> CodeStubAssembler::StringFromSingleCharCode(TNode<Int32T> code) {
  TVARIABLE(String, var_result);

  // Check if the {code} is a one-byte char code.
  Label if_codeisonebyte(this), if_codeistwobyte(this, Label::kDeferred),
      if_done(this);
  Branch(Int32LessThanOrEqual(code, Int32Constant(String::kMaxOneByteCharCode)),
         &if_codeisonebyte, &if_codeistwobyte);
  BIND(&if_codeisonebyte);
  {
    // Load the isolate wide single character string cache.
    TNode<FixedArray> cache = SingleCharacterStringTableConstant();
    TNode<IntPtrT> code_index = Signed(ChangeUint32ToWord(code));

    TNode<Object> entry = UnsafeLoadFixedArrayElement(cache, code_index);
    CSA_DCHECK(this, Word32BinaryNot(IsUndefined(entry)));

    // Return the entry from the {cache}.
    var_result = CAST(entry);
    Goto(&if_done);
  }

  BIND(&if_codeistwobyte);
  {
    // Allocate a new SeqTwoByteString for {code}.
    TNode<String> result = AllocateSeqTwoByteString(1);
    StoreNoWriteBarrier(
        MachineRepresentation::kWord16, result,
        IntPtrConstant(OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag),
        code);
    var_result = result;
    Goto(&if_done);
  }

  BIND(&if_done);
  return var_result.value();
}

ToDirectStringAssembler::ToDirectStringAssembler(
    compiler::CodeAssemblerState* state, TNode<String> string, Flags flags)
    : CodeStubAssembler(state),
      var_string_(string, this),
#if V8_STATIC_ROOTS_BOOL
      var_map_(LoadMap(string), this),
#else
      var_instance_type_(LoadInstanceType(string), this),
#endif
      var_offset_(IntPtrConstant(0), this),
      var_is_external_(Int32Constant(0), this),
      flags_(flags) {
}

TNode<String> ToDirectStringAssembler::TryToDirect(Label* if_bailout) {
  Label dispatch(this, {&var_string_, &var_offset_,
#if V8_STATIC_ROOTS_BOOL
                        &var_map_
#else
                        &var_instance_type_
#endif
                       });
  Label if_iscons(this);
  Label if_isexternal(this);
  Label if_issliced(this);
  Label if_isthin(this);
  Label out(this);

#if V8_STATIC_ROOTS_BOOL
  // The seq string check is in the dispatch.
  Goto(&dispatch);
#else
  Branch(IsSequentialStringInstanceType(var_instance_type_.value()), &out,
         &dispatch);
#endif

  // Dispatch based on string representation.
  BIND(&dispatch);
  {
#if V8_STATIC_ROOTS_BOOL
    TNode<Int32T> map_bits =
        TruncateIntPtrToInt32(BitcastTaggedToWord(var_map_.value()));

    using StringTypeRange = InstanceTypeChecker::kUniqueMapRangeOfStringType;
    // Check the string map ranges in dense increasing order, to avoid needing
    // to subtract away the lower bound. Do these couple of range checks instead
    // of a switch, since we can make them all single dense compares.
    static_assert(StringTypeRange::kSeqString.first == 0);
    GotoIf(Uint32LessThanOrEqual(
               map_bits, Int32Constant(StringTypeRange::kSeqString.second)),
           &out);

    static_assert(StringTypeRange::kSeqString.second + Map::kSize ==
                  StringTypeRange::kExternalString.first);
    GotoIf(
        Uint32LessThanOrEqual(
            map_bits, Int32Constant(StringTypeRange::kExternalString.second)),
        &if_isexternal);

    static_assert(StringTypeRange::kExternalString.second + Map::kSize ==
                  StringTypeRange::kConsString.first);
    GotoIf(Uint32LessThanOrEqual(
               map_bits, Int32Constant(StringTypeRange::kConsString.second)),
           &if_iscons);

    static_assert(StringTypeRange::kConsString.second + Map::kSize ==
                  StringTypeRange::kSlicedString.first);
    GotoIf(Uint32LessThanOrEqual(
               map_bits, Int32Constant(StringTypeRange::kSlicedString.second)),
           &if_issliced);

    static_assert(StringTypeRange::kSlicedString.second + Map::kSize ==
                  StringTypeRange::kThinString.first);
    // No need to check for thin strings, they're the last string map.
    static_assert(StringTypeRange::kThinString.second ==
                  InstanceTypeChecker::kStringMapUpperBound);
    Goto(&if_isthin);
#else
    int32_t values[] = {
        kSeqStringTag,    kConsStringTag, kExternalStringTag,
        kSlicedStringTag, kThinStringTag,
    };
    Label* labels[] = {
        &out, &if_iscons, &if_isexternal, &if_issliced, &if_isthin,
    };
    static_assert(arraysize(values) == arraysize(labels));

    const TNode<Int32T> representation = Word32And(
        var_instance_type_.value(), Int32Constant(kStringRepresentationMask));
    Switch(representation, if_bailout, values, labels, arraysize(values));
#endif
  }

  // Cons string.  Check whether it is flat, then fetch first part.
  // Flat cons strings have an empty second part.
  BIND(&if_iscons);
  {
    const TNode<String> string = var_string_.value();
    GotoIfNot(IsEmptyString(LoadObjectField<String>(
                  string, offsetof(ConsString, second_))),
              if_bailout);

    const TNode<String> lhs =
        LoadObjectField<String>(string, offsetof(ConsString, first_));
    var_string_ = lhs;
#if V8_STATIC_ROOTS_BOOL
    var_map_ = LoadMap(lhs);
#else
    var_instance_type_ = LoadInstanceType(lhs);
#endif

    Goto(&dispatch);
  }

  // Sliced string. Fetch parent and correct start index by offset.
  BIND(&if_issliced);
  {
    if (!v8_flags.string_slices || (flags_ & kDontUnpackSlicedStrings)) {
      Goto(if_bailout);
    } else {
      const TNode<String> string = var_string_.value();
      const TNode<IntPtrT> sliced_offset = LoadAndUntagPositiveSmiObjectField(
          string, offsetof(SlicedString, offset_));
      var_offset_ = IntPtrAdd(var_offset_.value(), sliced_offset);

      const TNode<String> parent =
          LoadObjectField<String>(string, offsetof(SlicedString, parent_));
      var_string_ = parent;
#if V8_STATIC_ROOTS_BOOL
      var_map_ = LoadMap(parent);
#else
      var_instance_type_ = LoadInstanceType(parent);
#endif

      Goto(&dispatch);
    }
  }

  // Thin string. Fetch the actual string.
  BIND(&if_isthin);
  {
    const TNode<String> string = var_string_.value();
    const TNode<String> actual_string =
        LoadObjectField<String>(string, offsetof(ThinString, actual_));

    var_string_ = actual_string;
#if V8_STATIC_ROOTS_BOOL
    var_map_ = LoadMap(actual_string);
#else
    var_instance_type_ = LoadInstanceType(actual_string);
#endif

    Goto(&dispatch);
  }

  // External string.
  BIND(&if_isexternal);
  var_is_external_ = Int32Constant(1);
  Goto(&out);

  BIND(&out);
  return var_string_.value();
}

TNode<BoolT> ToDirectStringAssembler::IsOneByte() {
#if V8_STATIC_ROOTS_BOOL
  return IsOneByteStringMap(var_map_.value());
#else
  return IsOneByteStringInstanceType(var_instance_type_.value());
#endif
}

TNode<RawPtrT> ToDirectStringAssembler::TryToSequential(
    StringPointerKind ptr_kind, Label* if_bailout) {
  CHECK(ptr_kind == PTR_TO_DATA || ptr_kind == PTR_TO_STRING);

  TVARIABLE(RawPtrT, var_result);
  Label out(this), if_issequential(this), if_isexternal(this, Label::kDeferred);
  Branch(is_external(), &if_isexternal, &if_issequential);

  BIND(&if_issequential);
  {
    static_assert(OFFSET_OF_DATA_START(SeqOneByteString) ==
                  OFFSET_OF_DATA_START(SeqTwoByteString));
    TNode<RawPtrT> result =
        ReinterpretCast<RawPtrT>(BitcastTaggedToWord(var_string_.value()));
    if (ptr_kind == PTR_TO_DATA) {
      result = RawPtrAdd(result,
                         IntPtrConstant(OFFSET_OF_DATA_START(SeqOneByteString) -
                                        kHeapObjectTag));
    }
    var_result = result;
    Goto(&out);
  }

  BIND(&if_isexternal);
  {
#if V8_STATIC_ROOTS_BOOL
    GotoIf(IsUncachedExternalStringMap(var_map_.value()), if_bailout);
#else
    GotoIf(IsUncachedExternalStringInstanceType(var_instance_type_.value()),
           if_bailout);
#endif

    TNode<String> string = var_string_.value();
    TNode<RawPtrT> result = LoadExternalStringResourceDataPtr(CAST(string));
    if (ptr_kind == PTR_TO_STRING) {
      result = RawPtrSub(result,
                         IntPtrConstant(OFFSET_OF_DATA_START(SeqOneByteString) -
                                        kHeapObjectTag));
    }
    var_result = result;
    Goto(&out);
  }

  BIND(&out);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::StringToNumber(TNode<String> input) {
  Label runtime(this, Label::kDeferred);
  Label end(this);

  TVARIABLE(Number, var_result);

  // Check if string has a cached array index.
  TNode<Uint32T> raw_hash_field = LoadNameRawHashField(input);
  GotoIf(IsSetWord32(raw_hash_field, Name::kDoesNotContainCachedArrayIndexMask),
         &runtime);

  var_result = SmiTag(Signed(
      DecodeWordFromWord32<String::ArrayIndexValueBits>(raw_hash_field)));
  Goto(&end);

  BIND(&runtime);
  {
    var_result =
        CAST(CallRuntime(Runtime::kStringToNumber, NoContextConstant(), input));
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

TNode<String> CodeStubAssembler::NumberToString(TNode<Number> input,
                                                Label* bailout) {
  TVARIABLE(String, result);
  TVARIABLE(Smi, smi_input);
  Label if_smi(this), not_smi(this), if_heap_number(this), done(this, &result);

  // Load the number string cache.
  TNode<FixedArray> number_string_cache = NumberStringCacheConstant();

  // Make the hash mask from the length of the number string cache. It
  // contains two elements (number and string) for each cache entry.
  TNode<Uint32T> number_string_cache_length =
      LoadAndUntagFixedArrayBaseLengthAsUint32(number_string_cache);
  TNode<Int32T> one = Int32Constant(1);
  TNode<Word32T> mask =
      Int32Sub(Word32Shr(number_string_cache_length, one), one);

  GotoIfNot(TaggedIsSmi(input), &if_heap_number);
  smi_input = CAST(input);
  Goto(&if_smi);

  BIND(&if_heap_number);
  TNode<HeapNumber> heap_number_input = CAST(input);
  {
    Comment("NumberToString - HeapNumber");
    // Try normalizing the HeapNumber.
    smi_input = TryHeapNumberToSmi(heap_number_input, &not_smi);
    Goto(&if_smi);
  }
  BIND(&if_smi);
  {
    Comment("NumberToString - Smi");
    // Load the smi key, make sure it matches the smi we're looking for.
    TNode<Word32T> hash = Word32And(SmiToInt32(smi_input.value()), mask);
    TNode<IntPtrT> entry_index =
        Signed(ChangeUint32ToWord(Int32Add(hash, hash)));
    TNode<Object> smi_key =
        UnsafeLoadFixedArrayElement(number_string_cache, entry_index);
    Label if_smi_cache_missed(this);
    GotoIf(TaggedNotEqual(smi_key, smi_input.value()), &if_smi_cache_missed);

    // Smi match, return value from cache entry.
    result = CAST(UnsafeLoadFixedArrayElement(number_string_cache, entry_index,
                                              kTaggedSize));
    Goto(&done);

    BIND(&if_smi_cache_missed);
    {
      Label store_to_cache(this);

      // Bailout when the cache is not full-size.
      const int kFullCacheSize =
          isolate()->heap()->MaxNumberToStringCacheSize();
      Branch(Uint32LessThan(number_string_cache_length,
                            Uint32Constant(kFullCacheSize)),
             bailout, &store_to_cache);

      BIND(&store_to_cache);
      {
        // Generate string and update string hash field.
        result = IntToDecimalString(SmiToInt32(smi_input.value()));

        // Store string into cache.
        StoreFixedArrayElement(number_string_cache, entry_index,
                               smi_input.value());
        StoreFixedArrayElement(number_string_cache,
                               IntPtrAdd(entry_index, IntPtrConstant(1)),
                               result.value());
        Goto(&done);
      }
    }
  }

  BIND(&not_smi);
  {
    // Make a hash from the two 32-bit values of the double.
    TNode<Int32T> low = LoadObjectField<Int32T>(heap_number_input,
                                                offsetof(HeapNumber, value_));
    TNode<Int32T> high = LoadObjectField<Int32T>(
        heap_number_input, offsetof(HeapNumber, value_) + kIntSize);
    TNode<Word32T> hash = Word32And(Word32Xor(low, high), mask);
    TNode<IntPtrT> entry_index =
        Signed(ChangeUint32ToWord(Int32Add(hash, hash)));

    // Cache entry's key must be a heap number
    TNode<Object> number_key =
        UnsafeLoadFixedArrayElement(number_string_cache, entry_index);
    GotoIf(TaggedIsSmi(number_key), bailout);
    TNode<HeapObject> number_key_heap_object = CAST(number_key);
    GotoIfNot(IsHeapNumber(number_key_heap_object), bailout);

    // Cache entry's key must match the heap number value we're looking for.
    TNode<Int32T> low_compare = LoadObjectField<Int32T>(
        number_key_heap_object, offsetof(HeapNumber, value_));
    TNode<Int32T> high_compare = LoadObjectField<Int32T>(
        number_key_heap_object, offsetof(HeapNumber, value_) + kIntSize);
    GotoIfNot(Word32Equal(low, low_compare), bailout);
    GotoIfNot(Word32Equal(high, high_compare), bailout);

    // Heap number match, return value from cache entry.
    result = CAST(UnsafeLoadFixedArrayElement(number_string_cache, entry_index,
                                              kTaggedSize));
    Goto(&done);
  }
  BIND(&done);
  return result.value();
}

TNode<String> CodeStubAssembler::NumberToString(TNode<Number> input) {
  TVARIABLE(String, result);
  Label runtime(this, Label::kDeferred), done(this, &result);

  GotoIfForceSlowPath(&runtime);

  result = NumberToString(input, &runtime);
  Goto(&done);

  BIND(&runtime);
  {
    // No cache entry, go to the runtime.
    result = CAST(
        CallRuntime(Runtime::kNumberToStringSlow, NoContextConstant(), input));
    Goto(&done);
  }
  BIND(&done);
  return result.value();
}

TNode<Numeric> CodeStubAssembler::NonNumberToNumberOrNumeric(
    TNode<Context> context, TNode<HeapObject> input, Object::Conversion mode,
    BigIntHandling bigint_handling) {
  CSA_DCHECK(this, Word32BinaryNot(IsHeapNumber(input)));

  TVARIABLE(HeapObject, var_input, input);
  TVARIABLE(Numeric, var_result);
  TVARIABLE(Uint16T, instance_type, LoadInstanceType(var_input.value()));
  Label end(this), if_inputisreceiver(this, Label::kDeferred),
      if_inputisnotreceiver(this);

  // We need to handle JSReceiver first since we might need to do two
  // conversions due to ToPritmive.
  Branch(IsJSReceiverInstanceType(instance_type.value()), &if_inputisreceiver,
         &if_inputisnotreceiver);

  BIND(&if_inputisreceiver);
  {
    // The {var_input.value()} is a JSReceiver, we need to convert it to a
    // Primitive first using the ToPrimitive type conversion, preferably
    // yielding a Number.
    Builtin builtin =
        Builtins::NonPrimitiveToPrimitive(ToPrimitiveHint::kNumber);
    TNode<Object> result = CallBuiltin(builtin, context, var_input.value());

    // Check if the {result} is already a Number/Numeric.
    Label if_done(this), if_notdone(this);
    Branch(mode == Object::Conversion::kToNumber ? IsNumber(result)
                                                 : IsNumeric(result),
           &if_done, &if_notdone);

    BIND(&if_done);
    {
      // The ToPrimitive conversion already gave us a Number/Numeric, so
      // we're done.
      var_result = CAST(result);
      Goto(&end);
    }

    BIND(&if_notdone);
    {
      // We now have a Primitive {result}, but it's not yet a
      // Number/Numeric.
      var_input = CAST(result);
      // We have a new input. Redo the check and reload instance_type.
      CSA_DCHECK(this, Word32BinaryNot(IsHeapNumber(var_input.value())));
      instance_type = LoadInstanceType(var_input.value());
      Goto(&if_inputisnotreceiver);
    }
  }

  BIND(&if_inputisnotreceiver);
  {
    Label not_plain_primitive(this), if_inputisbigint(this),
        if_inputisother(this, Label::kDeferred);

    // String and Oddball cases.
    TVARIABLE(Number, var_result_number);
    TryPlainPrimitiveNonNumberToNumber(var_input.value(), &var_result_number,
                                       &not_plain_primitive);
    var_result = var_result_number.value();
    Goto(&end);

    BIND(&not_plain_primitive);
    {
      Branch(IsBigIntInstanceType(instance_type.value()), &if_inputisbigint,
             &if_inputisother);

      BIND(&if_inputisbigint);
      {
        if (mode == Object::Conversion::kToNumeric) {
          var_result = CAST(var_input.value());
          Goto(&end);
        } else {
          DCHECK_EQ(mode, Object::Conversion::kToNumber);
          if (bigint_handling == BigIntHandling::kThrow) {
            Goto(&if_inputisother);
          } else {
            DCHECK_EQ(bigint_handling, BigIntHandling::kConvertToNumber);
            var_result = CAST(CallRuntime(Runtime::kBigIntToNumber, context,
                                          var_input.value()));
            Goto(&end);
          }
        }
      }

      BIND(&if_inputisother);
      {
        // The {var_input.value()} is something else (e.g. Symbol), let the
        // runtime figure out the correct exception. Note: We cannot tail call
        // to the runtime here, as js-to-wasm trampolines also use this code
        // currently, and they declare all outgoing parameters as untagged,
        // while we would push a tagged object here.
        auto function_id = mode == Object::Conversion::kToNumber
                               ? Runtime::kToNumber
                               : Runtime::kToNumeric;
        var_result = CAST(CallRuntime(function_id, context, var_input.value()));
        Goto(&end);
      }
    }
  }

  BIND(&end);
  if (mode == Object::Conversion::kToNumber) {
    CSA_DCHECK(this, IsNumber(var_result.value()));
  }
  return var_result.value();
}

TNode<Number> CodeStubAssembler::NonNumberToNumber(
    TNode<Context> context, TNode<HeapObject> input,
    BigIntHandling bigint_handling) {
  return CAST(NonNumberToNumberOrNumeric(
      context, input, Object::Conversion::kToNumber, bigint_handling));
}

void CodeStubAssembler::TryPlainPrimitiveNonNumberToNumber(
    TNode<HeapObject> input, TVariable<Number>* var_result, Label* if_bailout) {
  CSA_DCHECK(this, Word32BinaryNot(IsHeapNumber(input)));
  Label done(this);

  // Dispatch on the {input} instance type.
  TNode<Uint16T> input_instance_type = LoadInstanceType(input);
  Label if_inputisstring(this);
  GotoIf(IsStringInstanceType(input_instance_type), &if_inputisstring);
  GotoIfNot(InstanceTypeEqual(input_instance_type, ODDBALL_TYPE), if_bailout);

  // The {input} is an Oddball, we just need to load the Number value of it.
  *var_result = LoadObjectField<Number>(input, offsetof(Oddball, to_number_));
  Goto(&done);

  BIND(&if_inputisstring);
  {
    // The {input} is a String, use the fast stub to convert it to a Number.
    *var_result = StringToNumber(CAST(input));
    Goto(&done);
  }

  BIND(&done);
}

TNode<Numeric> CodeStubAssembler::NonNumberToNumeric(TNode<Context> context,
                                                     TNode<HeapObject> input) {
  return NonNumberToNumberOrNumeric(context, input,
                                    Object::Conversion::kToNumeric);
}

TNode<Number> CodeStubAssembler::ToNumber(TNode<Context> context,
                                          TNode<Object> input,
                                          BigIntHandling bigint_handling) {
  return CAST(ToNumberOrNumeric([context] { return context; }, input, nullptr,
                                Object::Conversion::kToNumber,
                                bigint_handling));
}

TNode<Number> CodeStubAssembler::ToNumber_Inline(TNode<Context> context,
                                                 TNode<Object> input) {
  TVARIABLE(Number, var_result);
  Label end(this), not_smi(this, Label::kDeferred);

  GotoIfNot(TaggedIsSmi(input), &not_smi);
  var_result = CAST(input);
  Goto(&end);

  BIND(&not_smi);
  {
    var_result = Select<Number>(
        IsHeapNumber(CAST(input)), [=, this] { return CAST(input); },
        [=, this] {
          return CAST(CallBuiltin(Builtin::kNonNumberToNumber, context, input));
        });
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

TNode<Numeric> CodeStubAssembler::ToNumberOrNumeric(
    LazyNode<Context> context, TNode<Object> input,
    TVariable<Smi>* var_type_feedback, Object::Conversion mode,
    BigIntHandling bigint_handling) {
  TVARIABLE(Numeric, var_result);
  Label end(this);

  Label not_smi(this, Label::kDeferred);
  GotoIfNot(TaggedIsSmi(input), &not_smi);
  TNode<Smi> input_smi = CAST(input);
  var_result = input_smi;
  if (var_type_feedback) {
    *var_type_feedback = SmiConstant(BinaryOperationFeedback::kSignedSmall);
  }
  Goto(&end);

  BIND(&not_smi);
  {
    Label not_heap_number(this, Label::kDeferred);
    TNode<HeapObject> input_ho = CAST(input);
    GotoIfNot(IsHeapNumber(input_ho), &not_heap_number);

    TNode<HeapNumber> input_hn = CAST(input_ho);
    var_result = input_hn;
    if (var_type_feedback) {
      *var_type_feedback = SmiConstant(BinaryOperationFeedback::kNumber);
    }
    Goto(&end);

    BIND(&not_heap_number);
    {
      if (mode == Object::Conversion::kToNumeric) {
        // Special case for collecting BigInt feedback.
        Label not_bigint(this);
        GotoIfNot(IsBigInt(input_ho), &not_bigint);
        {
          var_result = CAST(input_ho);
          *var_type_feedback = SmiConstant(BinaryOperationFeedback::kBigInt);
          Goto(&end);
        }
        BIND(&not_bigint);
      }
      var_result = NonNumberToNumberOrNumeric(context(), input_ho, mode,
                                              bigint_handling);
      if (var_type_feedback) {
        *var_type_feedback = SmiConstant(BinaryOperationFeedback::kAny);
      }
      Goto(&end);
    }
  }

  BIND(&end);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::PlainPrimitiveToNumber(TNode<Object> input) {
  TVARIABLE(Number, var_result);
  Label end(this), fallback(this);

  Label not_smi(this, Label::kDeferred);
  GotoIfNot(TaggedIsSmi(input), &not_smi);
  TNode<Smi> input_smi = CAST(input);
  var_result = input_smi;
  Goto(&end);

  BIND(&not_smi);
  {
    Label not_heap_number(this, Label::kDeferred);
    TNode<HeapObject> input_ho = CAST(input);
    GotoIfNot(IsHeapNumber(input_ho), &not_heap_number);

    TNode<HeapNumber> input_hn = CAST(input_ho);
    var_result = input_hn;
    Goto(&end);

    BIND(&not_heap_number);
    {
      TryPlainPrimitiveNonNumberToNumber(input_ho, &var_result, &fallback);
      Goto(&end);
      BIND(&fallback);
      Unreachable();
    }
  }

  BIND(&end);
  return var_result.value();
}

TNode<BigInt> CodeStubAssembler::ToBigInt(TNode<Context> context,
                                          TNode<Object> input) {
  TVARIABLE(BigInt, var_result);
  Label if_bigint(this), done(this), if_throw(this);

  GotoIf(TaggedIsSmi(input), &if_throw);
  GotoIf(IsBigInt(CAST(input)), &if_bigint);
  var_result = CAST(CallRuntime(Runtime::kToBigInt, context, input));
  Goto(&done);

  BIND(&if_bigint);
  var_result = CAST(input);
  Goto(&done);

  BIND(&if_throw);
  ThrowTypeError(context, MessageTemplate::kBigIntFromObject, input);

  BIND(&done);
  return var_result.value();
}

TNode<BigInt> CodeStubAssembler::ToBigIntConvertNumber(TNode<Context> context,
                                                       TNode<Object> input) {
  TVARIABLE(BigInt, var_result);
  Label if_bigint(this), if_not_bigint(this), done(this);

  GotoIf(TaggedIsSmi(input), &if_not_bigint);
  GotoIf(IsBigInt(CAST(input)), &if_bigint);
  Goto(&if_not_bigint);

  BIND(&if_bigint);
  var_result = CAST(input);
  Goto(&done);

  BIND(&if_not_bigint);
  var_result =
      CAST(CallRuntime(Runtime::kToBigIntConvertNumber, context, input));
  Goto(&done);

  BIND(&done);
  return var_result.value();
}

void CodeStubAssembler::TaggedToBigInt(TNode<Context> context,
                                       TNode<Object> value,
                                       Label* if_not_bigint, Label* if_bigint,
                                       Label* if_bigint64,
                                       TVariable<BigInt>* var_bigint,
                                       TVariable<Smi>* var_feedback) {
  Label done(this), is_smi(this), is_heapnumber(this), maybe_bigint64(this),
      is_bigint(this), is_oddball(this);
  GotoIf(TaggedIsSmi(value), &is_smi);
  TNode<HeapObject> heap_object_value = CAST(value);
  TNode<Map> map = LoadMap(heap_object_value);
  GotoIf(IsHeapNumberMap(map), &is_heapnumber);
  TNode<Uint16T> instance_type = LoadMapInstanceType(map);
  if (Is64() && if_bigint64) {
    GotoIf(IsBigIntInstanceType(instance_type), &maybe_bigint64);
  } else {
    GotoIf(IsBigIntInstanceType(instance_type), &is_bigint);
  }

  // {heap_object_value} is not a Numeric yet.
  GotoIf(Word32Equal(instance_type, Int32Constant(ODDBALL_TYPE)), &is_oddball);
  TNode<Numeric> numeric_value = CAST(
      CallBuiltin(Builtin::kNonNumberToNumeric, context, heap_object_value));
  OverwriteFeedback(var_feedback, BinaryOperationFeedback::kAny);
  GotoIf(TaggedIsSmi(numeric_value), if_not_bigint);
  GotoIfNot(IsBigInt(CAST(numeric_value)), if_not_bigint);
  *var_bigint = CAST(numeric_value);
  Goto(if_bigint);

  BIND(&is_smi);
  OverwriteFeedback(var_feedback, BinaryOperationFeedback::kSignedSmall);
  Goto(if_not_bigint);

  BIND(&is_heapnumber);
  OverwriteFeedback(var_feedback, BinaryOperationFeedback::kNumber);
  Goto(if_not_bigint);

  if (Is64() && if_bigint64) {
    BIND(&maybe_bigint64);
    GotoIfLargeBigInt(CAST(value), &is_bigint);
    *var_bigint = CAST(value);
    OverwriteFeedback(var_feedback, BinaryOperationFeedback::kBigInt64);
    Goto(if_bigint64);
  }

  BIND(&is_bigint);
  *var_bigint = CAST(value);
  OverwriteFeedback(var_feedback, BinaryOperationFeedback::kBigInt);
  Goto(if_bigint);

  BIND(&is_oddball);
  OverwriteFeedback(var_feedback, BinaryOperationFeedback::kNumberOrOddball);
  Goto(if_not_bigint);
}

// ES#sec-touint32
TNode<Number> CodeStubAssembler::ToUint32(TNode<Context> context,
                                          TNode<Object> input) {
  const TNode<Float64T> float_zero = Float64Constant(0.0);
  const TNode<Float64T> float_two_32 =
      Float64Constant(static_cast<double>(1ULL << 32));

  Label out(this);

  TVARIABLE(Object, var_result, input);

  // Early exit for positive smis.
  {
    // TODO(jgruber): This branch and the recheck below can be removed once we
    // have a ToNumber with multiple exits.
    Label next(this, Label::kDeferred);
    Branch(TaggedIsPositiveSmi(input), &out, &next);
    BIND(&next);
  }

  const TNode<Number> number = ToNumber(context, input);
  var_result = number;

  // Perhaps we have a positive smi now.
  {
    Label next(this, Label::kDeferred);
    Branch(TaggedIsPositiveSmi(number), &out, &next);
    BIND(&next);
  }

  Label if_isnegativesmi(this), if_isheapnumber(this);
  Branch(TaggedIsSmi(number), &if_isnegativesmi, &if_isheapnumber);

  BIND(&if_isnegativesmi);
  {
    const TNode<Int32T> uint32_value = SmiToInt32(CAST(number));
    TNode<Float64T> float64_value = ChangeUint32ToFloat64(uint32_value);
    var_result = AllocateHeapNumberWithValue(float64_value);
    Goto(&out);
  }

  BIND(&if_isheapnumber);
  {
    Label return_zero(this);
    const TNode<Float64T> value = LoadHeapNumberValue(CAST(number));

    {
      // +-0.
      Label next(this);
      Branch(Float64Equal(value, float_zero), &return_zero, &next);
      BIND(&next);
    }

    {
      // NaN.
      Label next(this);
      Branch(Float64Equal(value, value), &next, &return_zero);
      BIND(&next);
    }

    {
      // +Infinity.
      Label next(this);
      const TNode<Float64T> positive_infinity =
          Float64Constant(std::numeric_limits<double>::infinity());
      Branch(Float64Equal(value, positive_infinity), &return_zero, &next);
      BIND(&next);
    }

    {
      // -Infinity.
      Label next(this);
      const TNode<Float64T> negative_infinity =
          Float64Constant(-1.0 * std::numeric_limits<double>::infinity());
      Branch(Float64Equal(value, negative_infinity), &return_zero, &next);
      BIND(&next);
    }

    // * Let int be the mathematical value that is the same sign as number and
    //   whose magnitude is floor(abs(number)).
    // * Let int32bit be int modulo 2^32.
    // * Return int32bit.
    {
      TNode<Float64T> x = Float64Trunc(value);
      x = Float64Mod(x, float_two_32);
      x = Float64Add(x, float_two_32);
      x = Float64Mod(x, float_two_32);

      const TNode<Number> result = ChangeFloat64ToTagged(x);
      var_result = result;
      Goto(&out);
    }

    BIND(&return_zero);
    {
      var_result = SmiConstant(0);
      Goto(&out);
    }
  }

  BIND(&out);
  return CAST(var_result.value());
}

TNode<String> CodeStubAssembler::ToString_Inline(TNode<Context> context,
                                                 TNode<Object> input) {
  TVARIABLE(Object, var_result, input);
  Label stub_call(this, Label::kDeferred), out(this);

  GotoIf(TaggedIsSmi(input), &stub_call);
  Branch(IsString(CAST(input)), &out, &stub_call);

  BIND(&stub_call);
  var_result = CallBuiltin(Builtin::kToString, context, input);
  Goto(&out);

  BIND(&out);
  return CAST(var_result.value());
}

TNode<JSReceiver> CodeStubAssembler::ToObject(TNode<Context> context,
                                              TNode<Object> input) {
  return CAST(CallBuiltin(Builtin::kToObject, context, input));
}

TNode<JSReceiver> CodeStubAssembler::ToObject_Inline(TNode<Context> context,
                                                     TNode<Object> input) {
  TVARIABLE(JSReceiver, result);
  Label if_isreceiver(this), if_isnotreceiver(this, Label::kDeferred);
  Label done(this);

  BranchIfJSReceiver(input, &if_isreceiver, &if_isnotreceiver);

  BIND(&if_isreceiver);
  {
    result = CAST(input);
    Goto(&done);
  }

  BIND(&if_isnotreceiver);
  {
    result = ToObject(context, input);
    Goto(&done);
  }

  BIND(&done);
  return result.value();
}

TNode<Number> CodeStubAssembler::ToLength_Inline(TNode<Context> context,
                                                 TNode<Object> input) {
  TNode<Smi> smi_zero = SmiConstant(0);
  return Select<Number>(
      TaggedIsSmi(input), [=, this] { return SmiMax(CAST(input), smi_zero); },
      [=, this] {
        return CAST(CallBuiltin(Builtin::kToLength, context, input));
      });
}

TNode<Object> CodeStubAssembler::OrdinaryToPrimitive(
    TNode<Context> context, TNode<Object> input, OrdinaryToPrimitiveHint hint) {
  return CallBuiltin(Builtins::OrdinaryToPrimitive(hint), context, input);
}

TNode<Uint32T> CodeStubAssembler::DecodeWord32(TNode<Word32T> word32,
                                               uint32_t shift, uint32_t mask) {
  DCHECK_EQ((mask >> shift) << shift, mask);
  if ((std::numeric_limits<uint32_t>::max() >> shift) ==
      ((std::numeric_limits<uint32_t>::max() & mask) >> shift)) {
    return Unsigned(Word32Shr(word32, static_cast<int>(shift)));
  } else {
    return Unsigned(Word32And(Word32Shr(word32, static_cast<int>(shift)),
                              Int32Constant(mask >> shift)));
  }
}

TNode<UintPtrT> CodeStubAssembler::DecodeWord(TNode<WordT> word, uint32_t shift,
                                              uintptr_t mask) {
  DCHECK_EQ((mask >> shift) << shift, mask);
  if ((std::numeric_limits<uintptr_t>::max() >> shift) ==
      ((std::numeric_limits<uintptr_t>::max() & mask) >> shift)) {
    return Unsigned(WordShr(word, static_cast<int>(shift)));
  } else {
    return Unsigned(WordAnd(WordShr(word, static_cast<int>(shift)),
                            IntPtrConstant(mask >> shift)));
  }
}

TNode<Word32T> CodeStubAssembler::UpdateWord32(TNode<Word32T> word,
                                               TNode<Uint32T> value,
                                               uint32_t shift, uint32_t mask,
                                               bool starts_as_zero) {
  DCHECK_EQ((mask >> shift) << shift, mask);
  // Ensure the {value} fits fully in the mask.
  CSA_DCHECK(this, Uint32LessThanOrEqual(value, Uint32Constant(mask >> shift)));
  TNode<Word32T> encoded_value = Word32Shl(value, Int32Constant(shift));
  TNode<Word32T> masked_word;
  if (starts_as_zero) {
    CSA_DCHECK(this, Word32Equal(Word32And(word, Int32Constant(~mask)), word));
    masked_word = word;
  } else {
    masked_word = Word32And(word, Int32Constant(~mask));
  }
  return Word32Or(masked_word, encoded_value);
}

TNode<WordT> CodeStubAssembler::UpdateWord(TNode<WordT> word,
                                           TNode<UintPtrT> value,
                                           uint32_t shift, uintptr_t mask,
                                           bool starts_as_zero) {
  DCHECK_EQ((mask >> shift) << shift, mask);
  // Ensure the {value} fits fully in the mask.
  CSA_DCHECK(this,
             UintPtrLessThanOrEqual(value, UintPtrConstant(mask >> shift)));
  TNode<WordT> encoded_value = WordShl(value, static_cast<int>(shift));
  TNode<WordT> masked_word;
  if (starts_as_zero) {
    CSA_DCHECK(this, WordEqual(WordAnd(word, UintPtrConstant(~mask)), word));
    masked_word = word;
  } else {
    masked_word = WordAnd(word, UintPtrConstant(~mask));
  }
  return WordOr(masked_word, encoded_value);
}

void CodeStubAssembler::SetCounter(StatsCounter* counter, int value) {
  if (v8_flags.native_code_counters && counter->Enabled()) {
    TNode<ExternalReference> counter_address =
        ExternalConstant(ExternalReference::Create(counter));
    StoreNoWriteBarrier(MachineRepresentation::kWord32, counter_address,
                        Int32Constant(value));
  }
}

void CodeStubAssembler::IncrementCounter(StatsCounter* counter, int delta) {
  DCHECK_GT(delta, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    TNode<ExternalReference> counter_address =
        ExternalConstant(ExternalReference::Create(counter));
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t dummy_stats_counter_
    // field.
    TNode<Int32T> value = Load<Int32T>(counter_address);
    value = Int32Add(value, Int32Constant(delta));
    StoreNoWriteBarrier(MachineRepresentation::kWord32, counter_address, value);
  }
}

void CodeStubAssembler::DecrementCounter(StatsCounter* counter, int delta) {
  DCHECK_GT(delta, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    TNode<ExternalReference> counter_address =
        ExternalConstant(ExternalReference::Create(counter));
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t dummy_stats_counter_
    // field.
    TNode<Int32T> value = Load<Int32T>(counter_address);
    value = Int32Sub(value, Int32Constant(delta));
    StoreNoWriteBarrier(MachineRepresentation::kWord32, counter_address, value);
  }
}

template <typename TIndex>
void CodeStubAssembler::Increment(TVariable<TIndex>* variable, int value) {
  *variable =
      IntPtrOrSmiAdd(variable->value(), IntPtrOrSmiConstant<TIndex>(value));
}

// Instantiate Increment for Smi and IntPtrT.
// TODO(v8:9708): Consider renaming to [Smi|IntPtrT|RawPtrT]Increment.
template void CodeStubAssembler::Increment<Smi>(TVariable<Smi>* variable,
                                                int value);
template void CodeStubAssembler::Increment<IntPtrT>(
    TVariable<IntPtrT>* variable, int value);
template void CodeStubAssembler::Increment<RawPtrT>(
    TVariable<RawPtrT>* variable, int value);

void CodeStubAssembler::Use(Label* label) {
  GotoIf(Word32Equal(Int32Constant(0), Int32Constant(1)), label);
}

void CodeStubAssembler::TryToName(TNode<Object> key, Label* if_keyisindex,
                                  TVariable<IntPtrT>* var_index,
                                  Label* if_keyisunique,
                                  TVariable<Name>* var_unique,
                                  Label* if_bailout,
                                  Label* if_notinternalized) {
  Comment("TryToName");

  TVARIABLE(Int32T, var_instance_type);
  Label if_keyisnotindex(this);
  *var_index = TryToIntptr(key, &if_keyisnotindex, &var_instance_type);
  Goto(if_keyisindex);

  BIND(&if_keyisnotindex);
  {
    Label if_symbol(this), if_string(this),
        if_keyisother(this, Label::kDeferred);

    // Symbols are unique.
    GotoIf(IsSymbolInstanceType(var_instance_type.value()), &if_symbol);

    // Miss if |key| is not a String.
    static_assert(FIRST_NAME_TYPE == FIRST_TYPE);
    Branch(IsStringInstanceType(var_instance_type.value()), &if_string,
           &if_keyisother);

    // Symbols are unique.
    BIND(&if_symbol);
    {
      *var_unique = CAST(key);
      Goto(if_keyisunique);
    }

    BIND(&if_string);
    {
      TVARIABLE(Uint32T, var_raw_hash);
      Label check_string_hash(this, {&var_raw_hash});

      // TODO(v8:12007): LoadNameRawHashField() should be an acquire load.
      var_raw_hash = LoadNameRawHashField(CAST(key));
      Goto(&check_string_hash);
      BIND(&check_string_hash);
      {
        Label if_thinstring(this), if_has_cached_index(this),
            if_forwarding_index(this, Label::kDeferred);

        TNode<Uint32T> raw_hash_field = var_raw_hash.value();
        GotoIf(IsClearWord32(raw_hash_field,
                             Name::kDoesNotContainCachedArrayIndexMask),
               &if_has_cached_index);
        // No cached array index. If the string knows that it contains an index,
        // then it must be an uncacheable index. Handle this case in the
        // runtime.
        GotoIf(IsEqualInWord32<Name::HashFieldTypeBits>(
                   raw_hash_field, Name::HashFieldType::kIntegerIndex),
               if_bailout);

        static_assert(base::bits::CountPopulation(kThinStringTagBit) == 1);
        GotoIf(IsSetWord32(var_instance_type.value(), kThinStringTagBit),
               &if_thinstring);

        // Check if the hash field encodes a forwarding index.
        GotoIf(IsEqualInWord32<Name::HashFieldTypeBits>(
                   raw_hash_field, Name::HashFieldType::kForwardingIndex),
               &if_forwarding_index);

        // Finally, check if |key| is internalized.
        static_assert(kNotInternalizedTag != 0);
        GotoIf(IsSetWord32(var_instance_type.value(), kIsNotInternalizedMask),
               if_notinternalized != nullptr ? if_notinternalized : if_bailout);

        *var_unique = CAST(key);
        Goto(if_keyisunique);

        BIND(&if_thinstring);
        {
          *var_unique =
              LoadObjectField<String>(CAST(key), offsetof(ThinString, actual_));
          Goto(if_keyisunique);
        }

        BIND(&if_forwarding_index);
        {
          Label if_external(this), if_internalized(this);
          Branch(IsEqualInWord32<Name::IsExternalForwardingIndexBit>(
                     raw_hash_field, true),
                 &if_external, &if_internalized);
          BIND(&if_external);
          {
            // We know nothing about external forwarding indices, so load the
            // forwarded hash and check all possiblities again.
            TNode<ExternalReference> function = ExternalConstant(
                ExternalReference::raw_hash_from_forward_table());
            const TNode<ExternalReference> isolate_ptr =
                ExternalConstant(ExternalReference::isolate_address());
            TNode<Uint32T> result = UncheckedCast<Uint32T>(CallCFunction(
                function, MachineType::Uint32(),
                std::make_pair(MachineType::Pointer(), isolate_ptr),
                std::make_pair(MachineType::Int32(),
                               DecodeWord32<Name::ForwardingIndexValueBits>(
                                   raw_hash_field))));

            var_raw_hash = result;
            Goto(&check_string_hash);
          }

          BIND(&if_internalized);
          {
            // Integer indices are not overwritten with internalized forwarding
            // indices, so we are guaranteed forwarding to a unique name.
            CSA_DCHECK(this,
                       IsEqualInWord32<Name::IsExternalForwardingIndexBit>(
                           raw_hash_field, false));
            TNode<ExternalReference> function = ExternalConstant(
                ExternalReference::string_from_forward_table());
            const TNode<ExternalReference> isolate_ptr =
                ExternalConstant(ExternalReference::isolate_address());
            TNode<Object> result = CAST(CallCFunction(
                function, MachineType::AnyTagged(),
                std::make_pair(MachineType::Pointer(), isolate_ptr),
                std::make_pair(MachineType::Int32(),
                               DecodeWord32<Name::ForwardingIndexValueBits>(
                                   raw_hash_field))));

            *var_unique = CAST(result);
            Goto(if_keyisunique);
          }
        }

        BIND(&if_has_cached_index);
        {
          TNode<IntPtrT> index =
              Signed(DecodeWordFromWord32<String::ArrayIndexValueBits>(
                  raw_hash_field));
          CSA_DCHECK(this, IntPtrLessThan(index, IntPtrConstant(INT_MAX)));
          *var_index = index;
          Goto(if_keyisindex);
        }
      }
    }

    BIND(&if_keyisother);
    {
      GotoIfNot(InstanceTypeEqual(var_instance_type.value(), ODDBALL_TYPE),
                if_bailout);
      *var_unique =
          LoadObjectField<String>(CAST(key), offsetof(Oddball, to_string_));
      Goto(if_keyisunique);
    }
  }
}

void CodeStubAssembler::StringWriteToFlatOneByte(TNode<String> source,
                                                 TNode<RawPtrT> sink,
                                                 TNode<Int32T> start,
                                                 TNode<Int32T> length) {
  TNode<ExternalReference> function =
      ExternalConstant(ExternalReference::string_write_to_flat_one_byte());
  CallCFunction(function, std::nullopt,
                std::make_pair(MachineType::AnyTagged(), source),
                std::make_pair(MachineType::Pointer(), sink),
                std::make_pair(MachineType::Int32(), start),
                std::make_pair(MachineType::Int32(), length));
}

void CodeStubAssembler::StringWriteToFlatTwoByte(TNode<String> source,
                                                 TNode<RawPtrT> sink,
                                                 TNode<Int32T> start,
                                                 TNode<Int32T> length) {
  TNode<ExternalReference> function =
      ExternalConstant(ExternalReference::string_write_to_flat_two_byte());
  CallCFunction(function, std::nullopt,
                std::make_pair(MachineType::AnyTagged(), source),
                std::make_pair(MachineType::Pointer(), sink),
                std::make_pair(MachineType::Int32(), start),
                std::make_pair(MachineType::Int32(), length));
}

TNode<RawPtr<Uint8T>> CodeStubAssembler::ExternalOneByteStringGetChars(
    TNode<ExternalOneByteString> string) {
  TNode<ExternalReference> function =
      ExternalConstant(ExternalReference::external_one_byte_string_get_chars());
  return UncheckedCast<RawPtr<Uint8T>>(
      CallCFunction(function, MachineType::Pointer(),
                    std::make_pair(MachineType::AnyTagged(), string)));
}

TNode<RawPtr<Uint16T>> CodeStubAssembler::ExternalTwoByteStringGetChars(
    TNode<ExternalTwoByteString> string) {
  TNode<ExternalReference> function =
      ExternalConstant(ExternalReference::external_two_byte_string_get_chars());
  return UncheckedCast<RawPtr<Uint16T>>(
      CallCFunction(function, MachineType::Pointer(),
                    std::make_pair(MachineType::AnyTagged(), string)));
}

TNode<RawPtr<Uint8T>> CodeStubAssembler::IntlAsciiCollationWeightsL1() {
#ifdef V8_INTL_SUPPORT
  TNode<RawPtrT> ptr =
      ExternalConstant(ExternalReference::intl_ascii_collation_weights_l1());
  return ReinterpretCast<RawPtr<Uint8T>>(ptr);
#else
  UNREACHABLE();
#endif
}
TNode<RawPtr<Uint8T>> CodeStubAssembler::IntlAsciiCollationWeightsL3() {
#ifdef V8_INTL_SUPPORT
  TNode<RawPtrT> ptr =
      ExternalConstant(ExternalReference::intl_ascii_collation_weights_l3());
  return ReinterpretCast<RawPtr<Uint8T>>(ptr);
#else
  UNREACHABLE();
#endif
}

void CodeStubAssembler::TryInternalizeString(
    TNode<String> string, Label* if_index, TVariable<IntPtrT>* var_index,
    Label* if_internalized, TVariable<Name>* var_internalized,
    Label* if_not_internalized, Label* if_bailout) {
  TNode<ExternalReference> function = ExternalConstant(
      ExternalReference::try_string_to_index_or_lookup_existing());
  const TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());
  TNode<Object> result =
      CAST(CallCFunction(function, MachineType::AnyTagged(),
                         std::make_pair(MachineType::Pointer(), isolate_ptr),
                         std::make_pair(MachineType::AnyTagged(), string)));
  Label internalized(this);
  GotoIf(TaggedIsNotSmi(result), &internalized);
  TNode<IntPtrT> word_result = SmiUntag(CAST(result));
  GotoIf(IntPtrEqual(word_result, IntPtrConstant(ResultSentinel::kNotFound)),
         if_not_internalized);
  GotoIf(IntPtrEqual(word_result, IntPtrConstant(ResultSentinel::kUnsupported)),
         if_bailout);
  *var_index = word_result;
  Goto(if_index);

  BIND(&internalized);
  *var_internalized = CAST(result);
  Goto(if_internalized);
}

template <typename Dictionary>
TNode<IntPtrT> CodeStubAssembler::EntryToIndex(TNode<IntPtrT> entry,
                                               int field_index) {
  TNode<IntPtrT> entry_index =
      IntPtrMul(entry, IntPtrConstant(Dictionary::kEntrySize));
  return IntPtrAdd(entry_index, IntPtrConstant(Dictionary::kElementsStartIndex +
                                               field_index));
}

template <typename T>
TNode<T> CodeStubAssembler::LoadDescriptorArrayElement(
    TNode<DescriptorArray> object, TNode<IntPtrT> index,
    int additional_offset) {
  return LoadArrayElement<DescriptorArray, IntPtrT, T>(
      object, DescriptorArray::kHeaderSize, index, additional_offset);
}

TNode<Name> CodeStubAssembler::LoadKeyByKeyIndex(
    TNode<DescriptorArray> container, TNode<IntPtrT> key_index) {
  return CAST(LoadDescriptorArrayElement<HeapObject>(container, key_index, 0));
}

TNode<Uint32T> CodeStubAssembler::LoadDetailsByKeyIndex(
    TNode<DescriptorArray> container, TNode<IntPtrT> key_index) {
  const int kKeyToDetailsOffset =
      DescriptorArray::kEntryDetailsOffset - DescriptorArray::kEntryKeyOffset;
  return Unsigned(LoadAndUntagToWord32ArrayElement(
      container, DescriptorArray::kHeaderSize, key_index, kKeyToDetailsOffset));
}

TNode<Object> CodeStubAssembler::LoadValueByKeyIndex(
    TNode<DescriptorArray> container, TNode<IntPtrT> key_index) {
  const int kKeyToValueOffset =
      DescriptorArray::kEntryValueOffset - DescriptorArray::kEntryKeyOffset;
  return LoadDescriptorArrayElement<Object>(container, key_index,
                                            kKeyToValueOffset);
}

TNode<MaybeObject> CodeStubAssembler::LoadFieldTypeByKeyIndex(
    TNode<DescriptorArray> container, TNode<IntPtrT> key_index) {
  const int kKeyToValueOffset =
      DescriptorArray::kEntryValueOffset - DescriptorArray::kEntryKeyOffset;
  return LoadDescriptorArrayElement<MaybeObject>(container, key_index,
                                                 kKeyToValueOffset);
}

TNode<IntPtrT> CodeStubAssembler::DescriptorEntryToIndex(
    TNode<IntPtrT> descriptor_entry) {
  return IntPtrMul(descriptor_entry,
                   IntPtrConstant(DescriptorArray::kEntrySize));
}

TNode<Name> CodeStubAssembler::LoadKeyByDescriptorEntry(
    TNode<DescriptorArray> container, TNode<IntPtrT> descriptor_entry) {
  return CAST(LoadDescriptorArrayElement<HeapObject>(
      container, DescriptorEntryToIndex(descriptor_entry),
      DescriptorArray::ToKeyIndex(0) * kTaggedSize));
}

TNode<Name> CodeStubAssembler::LoadKeyByDescriptorEntry(
    TNode<DescriptorArray> container, int descriptor_entry) {
  return CAST(LoadDescriptorArrayElement<HeapObject>(
      container, IntPtrConstant(0),
      DescriptorArray::ToKeyIndex(descriptor_entry) * kTaggedSize));
}

TNode<Uint32T> CodeStubAssembler::LoadDetailsByDescriptorEntry(
    TNode<DescriptorArray> container, TNode<IntPtrT> descriptor_entry) {
  return Unsigned(LoadAndUntagToWord32ArrayElement(
      container, DescriptorArray::kHeaderSize,
      DescriptorEntryToIndex(descriptor_entry),
      DescriptorArray::ToDetailsIndex(0) * kTaggedSize));
}

TNode<Uint32T> CodeStubAssembler::LoadDetailsByDescriptorEntry(
    TNode<DescriptorArray> container, int descriptor_entry) {
  return Unsigned(LoadAndUntagToWord32ArrayElement(
      container, DescriptorArray::kHeaderSize, IntPtrConstant(0),
      DescriptorArray::ToDetailsIndex(descriptor_entry) * kTaggedSize));
}

TNode<Object> CodeStubAssembler::LoadValueByDescriptorEntry(
    TNode<DescriptorArray> container, TNode<IntPtrT> descriptor_entry) {
  return LoadDescriptorArrayElement<Object>(
      container, DescriptorEntryToIndex(descriptor_entry),
      DescriptorArray::ToValueIndex(0) * kTaggedSize);
}

TNode<Object> CodeStubAssembler::LoadValueByDescriptorEntry(
    TNode<DescriptorArray> container, int descriptor_entry) {
  return LoadDescriptorArrayElement<Object>(
      container, IntPtrConstant(0),
      DescriptorArray::ToValueIndex(descriptor_entry) * kTaggedSize);
}

TNode<MaybeObject> CodeStubAssembler::LoadFieldTypeByDescriptorEntry(
    TNode<DescriptorArray> container, TNode<IntPtrT> descriptor_entry) {
  return LoadDescriptorArrayElement<MaybeObject>(
      container, DescriptorEntryToIndex(descriptor_entry),
      DescriptorArray::ToValueIndex(0) * kTaggedSize);
}

// Loads the value for the entry with the given key_index.
// Returns a tagged value.
template <class ContainerType>
TNode<Object> CodeStubAssembler::LoadValueByKeyIndex(
    TNode<ContainerType> container, TNode<IntPtrT> key_index) {
  static_assert(!std::is_same<ContainerType, DescriptorArray>::value,
                "Use the non-templatized version for DescriptorArray");
  const int kKeyToValueOffset =
      (ContainerType::kEntryValueIndex - ContainerType::kEntryKeyIndex) *
      kTaggedSize;
  return LoadFixedArrayElement(container, key_index, kKeyToValueOffset);
}

template <>
V8_EXPORT_PRIVATE TNode<Object> CodeStubAssembler::LoadValueByKeyIndex(
    TNode<SwissNameDictionary> container, TNode<IntPtrT> key_index) {
  TNode<IntPtrT> offset_minus_tag = SwissNameDictionaryOffsetIntoDataTableMT(
      container, key_index, SwissNameDictionary::kDataTableValueEntryIndex);

  return Load<Object>(container, offset_minus_tag);
}

template <class ContainerType>
TNode<Uint32T> CodeStubAssembler::LoadDetailsByKeyIndex(
    TNode<ContainerType> container, TNode<IntPtrT> key_index) {
  static_assert(!std::is_same<ContainerType, DescriptorArray>::value,
                "Use the non-templatized version for DescriptorArray");
  const int kKeyToDetailsOffset =
      (ContainerType::kEntryDetailsIndex - ContainerType::kEntryKeyIndex) *
      kTaggedSize;
  return Unsigned(LoadAndUntagToWord32FixedArrayElement(container, key_index,
                                                        kKeyToDetailsOffset));
}

template <>
V8_EXPORT_PRIVATE TNode<Uint32T> CodeStubAssembler::LoadDetailsByKeyIndex(
    TNode<SwissNameDictionary> container, TNode<IntPtrT> key_index) {
  TNode<IntPtrT> capacity =
      ChangeInt32ToIntPtr(LoadSwissNameDictionaryCapacity(container));
  return LoadSwissNameDictionaryPropertyDetails(container, capacity, key_index);
}

// Stores the details for the entry with the given key_index.
// |details| must be a Smi.
template <class ContainerType>
void CodeStubAssembler::StoreDetailsByKeyIndex(TNode<ContainerType> container,
                                               TNode<IntPtrT> key_index,
                                               TNode<Smi> details) {
  const int kKeyToDetailsOffset =
      (ContainerType::kEntryDetailsIndex - ContainerType::kEntryKeyIndex) *
      kTaggedSize;
  StoreFixedArrayElement(container, key_index, details, kKeyToDetailsOffset);
}

template <>
V8_EXPORT_PRIVATE void CodeStubAssembler::StoreDetailsByKeyIndex(
    TNode<SwissNameDictionary> container, TNode<IntPtrT> key_index,
    TNode<Smi> details) {
  TNode<IntPtrT> capacity =
      ChangeInt32ToIntPtr(LoadSwissNameDictionaryCapacity(container));
  TNode<Uint8T> details_byte = UncheckedCast<Uint8T>(SmiToInt32(details));
  StoreSwissNameDictionaryPropertyDetails(container, capacity, key_index,
                                          details_byte);
}

// Stores the value for the entry with the given key_index.
template <class ContainerType>
void CodeStubAssembler::StoreValueByKeyIndex(TNode<ContainerType> container,
                                             TNode<IntPtrT> key_index,
                                             TNode<Object> value,
                                             WriteBarrierMode write_barrier) {
  const int kKeyToValueOffset =
      (ContainerType::kEntryValueIndex - ContainerType::kEntryKeyIndex) *
      kTaggedSize;
  StoreFixedArrayElement(container, key_index, value, write_barrier,
                         kKeyToValueOffset);
}

template <>
V8_EXPORT_PRIVATE void CodeStubAssembler::StoreValueByKeyIndex(
    TNode<SwissNameDictionary> container, TNode<IntPtrT> key_index,
    TNode<Object> value, WriteBarrierMode write_barrier) {
  TNode<IntPtrT> offset_minus_tag = SwissNameDictionaryOffsetIntoDataTableMT(
      container, key_index, SwissNameDictionary::kDataTableValueEntryIndex);

  StoreToObjectWriteBarrier mode;
  switch (write_barrier) {
    case UNSAFE_SKIP_WRITE_BARRIER:
    case SKIP_WRITE_BARRIER:
      mode = StoreToObjectWriteBarrier::kNone;
      break;
    case UPDATE_WRITE_BARRIER:
      mode = StoreToObjectWriteBarrier::kFull;
      break;
    default:
      // We shouldn't see anything else.
      UNREACHABLE();
  }
  StoreToObject(MachineRepresentation::kTagged, container, offset_minus_tag,
                value, mode);
}

template V8_EXPORT_PRIVATE TNode<IntPtrT>
CodeStubAssembler::EntryToIndex<NameDictionary>(TNode<IntPtrT>, int);
template V8_EXPORT_PRIVATE TNode<IntPtrT>
CodeStubAssembler::EntryToIndex<GlobalDictionary>(TNode<IntPtrT>, int);
template V8_EXPORT_PRIVATE TNode<IntPtrT>
CodeStubAssembler::EntryToIndex<NumberDictionary>(TNode<IntPtrT>, int);

template TNode<Object> CodeStubAssembler::LoadValueByKeyIndex(
    TNode<NameDictionary> container, TNode<IntPtrT> key_index);
template TNode<Object> CodeStubAssembler::LoadValueByKeyIndex(
    TNode<GlobalDictionary> container, TNode<IntPtrT> key_index);
template TNode<Uint32T> CodeStubAssembler::LoadDetailsByKeyIndex(
    TNode<NameDictionary> container, TNode<IntPtrT> key_index);
template void CodeStubAssembler::StoreDetailsByKeyIndex(
    TNode<NameDictionary> container, TNode<IntPtrT> key_index,
    TNode<Smi> details);
template void CodeStubAssembler::StoreValueByKeyIndex(
    TNode<NameDictionary> container, TNode<IntPtrT> key_index,
    TNode<Object> value, WriteBarrierMode write_barrier);

// This must be kept in sync with HashTableBase::ComputeCapacity().
TNode<IntPtrT> CodeStubAssembler::HashTableComputeCapacity(
    TNode<IntPtrT> at_least_space_for) {
  TNode<IntPtrT> capacity = IntPtrRoundUpToPowerOfTwo32(
      IntPtrAdd(at_least_space_for, WordShr(at_least_space_for, 1)));
  return IntPtrMax(capacity, IntPtrConstant(HashTableBase::kMinCapacity));
}

TNode<IntPtrT> CodeStubAssembler::IntPtrMax(TNode<IntPtrT> left,
                                            TNode<IntPtrT> right) {
  intptr_t left_constant;
  intptr_t right_constant;
  if (TryToIntPtrConstant(left, &left_constant) &&
      TryToIntPtrConstant(right, &right_constant)) {
    return IntPtrConstant(std::max(left_constant, right_constant));
  }
  return SelectConstant<IntPtrT>(IntPtrGreaterThanOrEqual(left, right), left,
                                 right);
}

TNode<IntPtrT> CodeStubAssembler::IntPtrMin(TNode<IntPtrT> left,
                                            TNode<IntPtrT> right) {
  intptr_t left_constant;
  intptr_t right_constant;
  if (TryToIntPtrConstant(left, &left_constant) &&
      TryToIntPtrConstant(right, &right_constant)) {
    return IntPtrConstant(std::min(left_constant, right_constant));
  }
  return SelectConstant<IntPtrT>(IntPtrLessThanOrEqual(left, right), left,
                                 right);
}

TNode<UintPtrT> CodeStubAssembler::UintPtrMin(TNode<UintPtrT> left,
                                              TNode<UintPtrT> right) {
  intptr_t left_constant;
  intptr_t right_constant;
  if (TryToIntPtrConstant(left, &left_constant) &&
      TryToIntPtrConstant(right, &right_constant)) {
    return UintPtrConstant(std::min(static_cast<uintptr_t>(left_constant),
                                    static_cast<uintptr_t>(right_constant)));
  }
  return SelectConstant<UintPtrT>(UintPtrLessThanOrEqual(left, right), left,
                                  right);
}

template <>
TNode<HeapObject> CodeStubAssembler::LoadName<NameDictionary>(
    TNode<HeapObject> key) {
  CSA_DCHECK(this, Word32Or(IsTheHole(key), IsName(key)));
  return key;
}

template <>
TNode<HeapObject> CodeStubAssembler::LoadName<GlobalDictionary>(
    TNode<HeapObject> key) {
  TNode<PropertyCell> property_cell = CAST(key);
  return CAST(LoadObjectField(property_cell, PropertyCell::kNameOffset));
}

template <>
TNode<HeapObject> CodeStubAssembler::LoadName<NameToIndexHashTable>(
    TNode<HeapObject> key) {
  CSA_DCHECK(this, IsName(key));
  return key;
}

// The implementation should be in sync with NameToIndexHashTable::Lookup.
TNode<IntPtrT> CodeStubAssembler::NameToIndexHashTableLookup(
    TNode<NameToIndexHashTable> table, TNode<Name> name, Label* not_found) {
  TVARIABLE(IntPtrT, var_entry);
  Label index_found(this, {&var_entry});
  NameDictionaryLookup<NameToIndexHashTable>(table, name, &index_found,
                                             &var_entry, not_found,
                                             LookupMode::kFindExisting);
  BIND(&index_found);
  TNode<Smi> value =
      CAST(LoadValueByKeyIndex<NameToIndexHashTable>(table, var_entry.value()));
  return SmiToIntPtr(value);
}

template <typename Dictionary>
void CodeStubAssembler::NameDictionaryLookup(
    TNode<Dictionary> dictionary, TNode<Name> unique_name, Label* if_found,
    TVariable<IntPtrT>* var_name_index, Label* if_not_found, LookupMode mode) {
  static_assert(std::is_same<Dictionary, NameDictionary>::value ||
                    std::is_same<Dictionary, GlobalDictionary>::value ||
                    std::is_same<Dictionary, NameToIndexHashTable>::value,
                "Unexpected NameDictionary");
  DCHECK_IMPLIES(var_name_index != nullptr,
                 MachineType::PointerRepresentation() == var_name_index->rep());
  DCHECK_IMPLIES(mode == kFindInsertionIndex, if_found == nullptr);
  Comment("NameDictionaryLookup");
  CSA_DCHECK(this, IsUniqueName(unique_name));

  Label if_not_computed(this, Label::kDeferred);

  TNode<IntPtrT> capacity =
      PositiveSmiUntag(GetCapacity<Dictionary>(dictionary));
  TNode<IntPtrT> mask = IntPtrSub(capacity, IntPtrConstant(1));
  TNode<UintPtrT> hash =
      ChangeUint32ToWord(LoadNameHash(unique_name, &if_not_computed));

  // See Dictionary::FirstProbe().
  TNode<IntPtrT> count = IntPtrConstant(0);
  TNode<IntPtrT> initial_entry = Signed(WordAnd(hash, mask));
  TNode<Undefined> undefined = UndefinedConstant();

  // Appease the variable merging algorithm for "Goto(&loop)" below.
  if (var_name_index) *var_name_index = IntPtrConstant(0);

  TVARIABLE(IntPtrT, var_count, count);
  TVARIABLE(IntPtrT, var_entry, initial_entry);
  VariableList loop_vars({&var_count, &var_entry}, zone());
  if (var_name_index) loop_vars.push_back(var_name_index);
  Label loop(this, loop_vars);
  Goto(&loop);
  BIND(&loop);
  {
    Label next_probe(this);
    TNode<IntPtrT> entry = var_entry.value();

    TNode<IntPtrT> index = EntryToIndex<Dictionary>(entry);
    if (var_name_index) *var_name_index = index;

    TNode<HeapObject> current =
        CAST(UnsafeLoadFixedArrayElement(dictionary, index));
    GotoIf(TaggedEqual(current, undefined), if_not_found);
    switch (mode) {
      case kFindInsertionIndex:
        GotoIf(TaggedEqual(current, TheHoleConstant()), if_not_found);
        break;
      case kFindExisting:
      case kFindExistingOrInsertionIndex:
        if (Dictionary::TodoShape::kMatchNeedsHoleCheck) {
          GotoIf(TaggedEqual(current, TheHoleConstant()), &next_probe);
        }
        current = LoadName<Dictionary>(current);
        GotoIf(TaggedEqual(current, unique_name), if_found);
        break;
    }
    Goto(&next_probe);

    BIND(&next_probe);
    // See Dictionary::NextProbe().
    Increment(&var_count);
    entry = Signed(WordAnd(IntPtrAdd(entry, var_count.value()), mask));

    var_entry = entry;
    Goto(&loop);
  }

  BIND(&if_not_computed);
  {
    // Strings will only have the forwarding index with experimental shared
    // memory features turned on. To minimize affecting the fast path, the
    // forwarding index branch defers both fetching the actual hash value and
    // the dictionary lookup to the runtime.
    NameDictionaryLookupWithForwardIndex(dictionary, unique_name, if_found,
                                         var_name_index, if_not_found, mode);
  }
}

// Instantiate template methods to workaround GCC compilation issue.
template V8_EXPORT_PRIVATE void
CodeStubAssembler::NameDictionaryLookup<NameDictionary>(TNode<NameDictionary>,
                                                        TNode<Name>, Label*,
                                                        TVariable<IntPtrT>*,
                                                        Label*, LookupMode);
template V8_EXPORT_PRIVATE void CodeStubAssembler::NameDictionaryLookup<
    GlobalDictionary>(TNode<GlobalDictionary>, TNode<Name>, Label*,
                      TVariable<IntPtrT>*, Label*, LookupMode);

template <typename Dictionary>
void CodeStubAssembler::NameDictionaryLookupWithForwardIndex(
    TNode<Dictionary> dictionary, TNode<Name> unique_name, Label* if_found,
    TVariable<IntPtrT>* var_name_index, Label* if_not_found, LookupMode mode) {
  using ER = ExternalReference;  // To avoid super long lines below.
  ER func_ref;
  if constexpr (std::is_same<Dictionary, NameDictionary>::value) {
    func_ref = mode == kFindInsertionIndex
                   ? ER::name_dictionary_find_insertion_entry_forwarded_string()
                   : ER::name_dictionary_lookup_forwarded_string();
  } else if constexpr (std::is_same<Dictionary, GlobalDictionary>::value) {
    func_ref =
        mode == kFindInsertionIndex
            ? ER::global_dictionary_find_insertion_entry_forwarded_string()
            : ER::global_dictionary_lookup_forwarded_string();
  } else {
    auto ref0 =
        ER::name_to_index_hashtable_find_insertion_entry_forwarded_string();
    auto ref1 = ER::name_to_index_hashtable_lookup_forwarded_string();
    func_ref = mode == kFindInsertionIndex ? ref0 : ref1;
  }
  const TNode<ER> function = ExternalConstant(func_ref);
  const TNode<ER> isolate_ptr = ExternalConstant(ER::isolate_address());
  TNode<IntPtrT> entry = UncheckedCast<IntPtrT>(
      CallCFunction(function, MachineType::IntPtr(),
                    std::make_pair(MachineType::Pointer(), isolate_ptr),
                    std::make_pair(MachineType::TaggedPointer(), dictionary),
                    std::make_pair(MachineType::TaggedPointer(), unique_name)));

  if (var_name_index) *var_name_index = EntryToIndex<Dictionary>(entry);
  switch (mode) {
    case kFindInsertionIndex:
      CSA_DCHECK(
          this,
          WordNotEqual(entry,
                       IntPtrConstant(InternalIndex::NotFound().raw_value())));
      Goto(if_not_found);
      break;
    case kFindExisting:
      GotoIf(IntPtrEqual(entry,
                         IntPtrConstant(InternalIndex::NotFound().raw_value())),
             if_not_found);
      Goto(if_found);
      break;
    case kFindExistingOrInsertionIndex:
      GotoIfNot(IntPtrEqual(entry, IntPtrConstant(
                                       InternalIndex::NotFound().raw_value())),
                if_found);
      NameDictionaryLookupWithForwardIndex(dictionary, unique_name, if_found,
                                           var_name_index, if_not_found,
                                           kFindInsertionIndex);
      break;
  }
}

TNode<Word32T> CodeStubAssembler::ComputeSeededHash(TNode<IntPtrT> key) {
  const TNode<ExternalReference> function_addr =
      ExternalConstant(ExternalReference::compute_integer_hash());
  const TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());

  MachineType type_ptr = MachineType::Pointer();
  MachineType type_uint32 = MachineType::Uint32();
  MachineType type_int32 = MachineType::Int32();

  return UncheckedCast<Word32T>(CallCFunction(
      function_addr, type_uint32, std::make_pair(type_ptr, isolate_ptr),
      std::make_pair(type_int32, TruncateIntPtrToInt32(key))));
}

template <>
void CodeStubAssembler::NameDictionaryLookup(
    TNode<SwissNameDictionary> dictionary, TNode<Name> unique_name,
    Label* if_found, TVariable<IntPtrT>* var_name_index, Label* if_not_found,
    LookupMode mode) {
  // TODO(pthier): Support mode kFindExistingOrInsertionIndex for
  // SwissNameDictionary.
  SwissNameDictionaryFindEntry(dictionary, unique_name, if_found,
                               var_name_index, if_not_found);
}

void CodeStubAssembler::NumberDictionaryLookup(
    TNode<NumberDictionary> dictionary, TNode<IntPtrT> intptr_index,
    Label* if_found, TVariable<IntPtrT>* var_entry, Label* if_not_found) {
  CSA_DCHECK(this, IsNumberDictionary(dictionary));
  DCHECK_EQ(MachineType::PointerRepresentation(), var_entry->rep());
  Comment("NumberDictionaryLookup");

  TNode<IntPtrT> capacity =
      PositiveSmiUntag(GetCapacity<NumberDictionary>(dictionary));
  TNode<IntPtrT> mask = IntPtrSub(capacity, IntPtrConstant(1));

  TNode<UintPtrT> hash = ChangeUint32ToWord(ComputeSeededHash(intptr_index));
  TNode<Float64T> key_as_float64 = RoundIntPtrToFloat64(intptr_index);

  // See Dictionary::FirstProbe().
  TNode<IntPtrT> count = IntPtrConstant(0);
  TNode<IntPtrT> initial_entry = Signed(WordAnd(hash, mask));

  TNode<Undefined> undefined = UndefinedConstant();
  TNode<Hole> the_hole = TheHoleConstant();

  TVARIABLE(IntPtrT, var_count, count);
  Label loop(this, {&var_count, var_entry});
  *var_entry = initial_entry;
  Goto(&loop);
  BIND(&loop);
  {
    TNode<IntPtrT> entry = var_entry->value();

    TNode<IntPtrT> index = EntryToIndex<NumberDictionary>(entry);
    TNode<Object> current = UnsafeLoadFixedArrayElement(dictionary, index);
    GotoIf(TaggedEqual(current, undefined), if_not_found);
    Label next_probe(this);
    {
      Label if_currentissmi(this), if_currentisnotsmi(this);
      Branch(TaggedIsSmi(current), &if_currentissmi, &if_currentisnotsmi);
      BIND(&if_currentissmi);
      {
        TNode<IntPtrT> current_value = SmiUntag(CAST(current));
        Branch(WordEqual(current_value, intptr_index), if_found, &next_probe);
      }
      BIND(&if_currentisnotsmi);
      {
        GotoIf(TaggedEqual(current, the_hole), &next_probe);
        // Current must be the Number.
        TNode<Float64T> current_value = LoadHeapNumberValue(CAST(current));
        Branch(Float64Equal(current_value, key_as_float64), if_found,
               &next_probe);
      }
    }

    BIND(&next_probe);
    // See Dictionary::NextProbe().
    Increment(&var_count);
    entry = Signed(WordAnd(IntPtrAdd(entry, var_count.value()), mask));

    *var_entry = entry;
    Goto(&loop);
  }
}

TNode<Object> CodeStubAssembler::BasicLoadNumberDictionaryElement(
    TNode<NumberDictionary> dictionary, TNode<IntPtrT> intptr_index,
    Label* not_data, Label* if_hole) {
  TVARIABLE(IntPtrT, var_entry);
  Label if_found(this);
  NumberDictionaryLookup(dictionary, intptr_index, &if_found, &var_entry,
                         if_hole);
  BIND(&if_found);

  // Check that the value is a data property.
  TNode<IntPtrT> index = EntryToIndex<NumberDictionary>(var_entry.value());
  TNode<Uint32T> details = LoadDetailsByKeyIndex(dictionary, index);
  TNode<Uint32T> kind = DecodeWord32<PropertyDetails::KindField>(details);
  // TODO(jkummerow): Support accessors without missing?
  GotoIfNot(
      Word32Equal(kind, Int32Constant(static_cast<int>(PropertyKind::kData))),
      not_data);
  // Finally, load the value.
  return LoadValueByKeyIndex(dictionary, index);
}

template <class Dictionary>
void CodeStubAssembler::FindInsertionEntry(TNode<Dictionary> dictionary,
                                           TNode<Name> key,
                                           TVariable<IntPtrT>* var_key_index) {
  UNREACHABLE();
}

template <>
void CodeStubAssembler::FindInsertionEntry<NameDictionary>(
    TNode<NameDictionary> dictionary, TNode<Name> key,
    TVariable<IntPtrT>* var_key_index) {
  Label done(this);
  NameDictionaryLookup<NameDictionary>(dictionary, key, nullptr, var_key_index,
                                       &done, kFindInsertionIndex);
  BIND(&done);
}

template <class Dictionary>
void CodeStubAssembler::InsertEntry(TNode<Dictionary> dictionary,
                                    TNode<Name> key, TNode<Object> value,
                                    TNode<IntPtrT> index,
                                    TNode<Smi> enum_index) {
  UNREACHABLE();  // Use specializations instead.
}

template <>
void CodeStubAssembler::InsertEntry<NameDictionary>(
    TNode<NameDictionary> dictionary, TNode<Name> name, TNode<Object> value,
    TNode<IntPtrT> index, TNode<Smi> enum_index) {
  // This should only be used for adding, not updating existing mappings.
  CSA_DCHECK(this,
             Word32Or(TaggedEqual(LoadFixedArrayElement(dictionary, index),
                                  UndefinedConstant()),
                      TaggedEqual(LoadFixedArrayElement(dictionary, index),
                                  TheHoleConstant())));

  // Store name and value.
  StoreFixedArrayElement(dictionary, index, name);
  StoreValueByKeyIndex<NameDictionary>(dictionary, index, value);

  // Prepare details of the new property.
  PropertyDetails d(PropertyKind::kData, NONE,
                    PropertyDetails::kConstIfDictConstnessTracking);

  // We ignore overflow of |enum_index| here and accept potentially
  // broken enumeration order (https://crbug.com/41432983).
  enum_index = UnsignedSmiShl(enum_index,
                              PropertyDetails::DictionaryStorageField::kShift);
  // We OR over the actual index below, so we expect the initial value to be 0.
  DCHECK_EQ(0, d.dictionary_index());
  TVARIABLE(Smi, var_details, SmiOr(SmiConstant(d.AsSmi()), enum_index));

  // Private names must be marked non-enumerable.
  Label not_private(this, &var_details);
  GotoIfNot(IsPrivateSymbol(name), &not_private);
  TNode<Smi> dont_enum = UnsignedSmiShl(
      SmiConstant(DONT_ENUM), PropertyDetails::AttributesField::kShift);
  var_details = SmiOr(var_details.value(), dont_enum);
  Goto(&not_private);
  BIND(&not_private);

  // Finally, store the details.
  StoreDetailsByKeyIndex<NameDictionary>(dictionary, index,
                                         var_details.value());
}

template <>
void CodeStubAssembler::InsertEntry<GlobalDictionary>(
    TNode<GlobalDictionary> dictionary, TNode<Name> key, TNode<Object> value,
    TNode<IntPtrT> index, TNode<Smi> enum_index) {
  UNIMPLEMENTED();
}

template <class Dictionary>
void CodeStubAssembler::AddToDictionary(
    TNode<Dictionary> dictionary, TNode<Name> key, TNode<Object> value,
    Label* bailout, std::optional<TNode<IntPtrT>> insertion_index) {
  CSA_DCHECK(this, Word32BinaryNot(IsEmptyPropertyDictionary(dictionary)));
  TNode<Smi> capacity = GetCapacity<Dictionary>(dictionary);
  TNode<Smi> nof = GetNumberOfElements<Dictionary>(dictionary);
  TNode<Smi> new_nof = SmiAdd(nof, SmiConstant(1));
  // Require 33% to still be free after adding additional_elements.
  // Computing "x + (x >> 1)" on a Smi x does not return a valid Smi!
  // But that's OK here because it's only used for a comparison.
  TNode<Smi> required_capacity_pseudo_smi = SmiAdd(new_nof, SmiShr(new_nof, 1));
  GotoIf(SmiBelow(capacity, required_capacity_pseudo_smi), bailout);
  // Require rehashing if more than 50% of free elements are deleted elements.
  TNode<Smi> deleted = GetNumberOfDeletedElements<Dictionary>(dictionary);
  CSA_DCHECK(this, SmiAbove(capacity, new_nof));
  TNode<Smi> half_of_free_elements = SmiShr(SmiSub(capacity, new_nof), 1);
  GotoIf(SmiAbove(deleted, half_of_free_elements), bailout);

  TNode<Smi> enum_index = GetNextEnumerationIndex<Dictionary>(dictionary);
  TNode<Smi> new_enum_index = SmiAdd(enum_index, SmiConstant(1));
  TNode<Smi> max_enum_index =
      SmiConstant(PropertyDetails::DictionaryStorageField::kMax);
  GotoIf(SmiAbove(new_enum_index, max_enum_index), bailout);

  // No more bailouts after this point.
  // Operations from here on can have side effects.

  SetNextEnumerationIndex<Dictionary>(dictionary, new_enum_index);
  SetNumberOfElements<Dictionary>(dictionary, new_nof);

  if (insertion_index.has_value()) {
    InsertEntry<Dictionary>(dictionary, key, value, *insertion_index,
                            enum_index);
  } else {
    TVARIABLE(IntPtrT, var_key_index);
    FindInsertionEntry<Dictionary>(dictionary, key, &var_key_index);
    InsertEntry<Dictionary>(dictionary, key, value, var_key_index.value(),
                            enum_index);
  }
}

template <>
void CodeStubAssembler::AddToDictionary(
    TNode<SwissNameDictionary> dictionary, TNode<Name> key, TNode<Object> value,
    Label* bailout, std::optional<TNode<IntPtrT>> insertion_index) {
  PropertyDetails d(PropertyKind::kData, NONE,
                    PropertyDetails::kConstIfDictConstnessTracking);

  PropertyDetails d_dont_enum(PropertyKind::kData, DONT_ENUM,
                              PropertyDetails::kConstIfDictConstnessTracking);
  TNode<Uint8T> details_byte_enum =
      UncheckedCast<Uint8T>(Uint32Constant(d.ToByte()));
  TNode<Uint8T> details_byte_dont_enum =
      UncheckedCast<Uint8T>(Uint32Constant(d_dont_enum.ToByte()));

  Label not_private(this);
  TVARIABLE(Uint8T, var_details, details_byte_enum);

  GotoIfNot(IsPrivateSymbol(key), &not_private);
  var_details = details_byte_dont_enum;
  Goto(&not_private);

  BIND(&not_private);
  // TODO(pthier): Use insertion_index if it was provided.
  SwissNameDictionaryAdd(dictionary, key, value, var_details.value(), bailout);
}

template void CodeStubAssembler::AddToDictionary<NameDictionary>(
    TNode<NameDictionary>, TNode<Name>, TNode<Object>, Label*,
    std::optional<TNode<IntPtrT>>);

template <class Dictionary>
TNode<Smi> CodeStubAssembler::GetNumberOfElements(
    TNode<Dictionary> dictionary) {
  return CAST(
      LoadFixedArrayElement(dictionary, Dictionary::kNumberOfElementsIndex));
}

template <>
TNode<Smi> CodeStubAssembler::GetNumberOfElements(
    TNode<SwissNameDictionary> dictionary) {
  TNode<IntPtrT> capacity =
      ChangeInt32ToIntPtr(LoadSwissNameDictionaryCapacity(dictionary));
  return SmiFromIntPtr(
      LoadSwissNameDictionaryNumberOfElements(dictionary, capacity));
}

template TNode<Smi> CodeStubAssembler::GetNumberOfElements(
    TNode<NameDictionary> dictionary);
template TNode<Smi> CodeStubAssembler::GetNumberOfElements(
    TNode<NumberDictionary> dictionary);
template TNode<Smi> CodeStubAssembler::GetNumberOfElements(
    TNode<GlobalDictionary> dictionary);

template <>
TNode<Smi> CodeStubAssembler::GetNameDictionaryFlags(
    TNode<NameDictionary> dictionary) {
  return CAST(LoadFixedArrayElement(dictionary, NameDictionary::kFlagsIndex));
}

template <>
void CodeStubAssembler::SetNameDictionaryFlags(TNode<NameDictionary> dictionary,
                                               TNode<Smi> flags) {
  StoreFixedArrayElement(dictionary, NameDictionary::kFlagsIndex, flags,
                         SKIP_WRITE_BARRIER);
}

template <>
TNode<Smi> CodeStubAssembler::GetNameDictionaryFlags(
    TNode<SwissNameDictionary> dictionary) {
  // TODO(pthier): Add flags to swiss dictionaries.
  Unreachable();
  return SmiConstant(0);
}

template <>
void CodeStubAssembler::SetNameDictionaryFlags(
    TNode<SwissNameDictionary> dictionary, TNode<Smi> flags) {
  // TODO(pthier): Add flags to swiss dictionaries.
  Unreachable();
}

template <typename Array>
void CodeStubAssembler::LookupLinear(TNode<Name> unique_name,
                                     TNode<Array> array,
                                     TNode<Uint32T> number_of_valid_entries,
                                     Label* if_found,
                                     TVariable<IntPtrT>* var_name_index,
                                     Label* if_not_found) {
  static_assert(std::is_base_of<FixedArray, Array>::value ||
                    std::is_base_of<WeakFixedArray, Array>::value ||
                    std::is_base_of<DescriptorArray, Array>::value,
                "T must be a descendant of FixedArray or a WeakFixedArray");
  Comment("LookupLinear");
  CSA_DCHECK(this, IsUniqueName(unique_name));
  TNode<IntPtrT> first_inclusive = IntPtrConstant(Array::ToKeyIndex(0));
  TNode<IntPtrT> factor = IntPtrConstant(Array::kEntrySize);
  TNode<IntPtrT> last_exclusive = IntPtrAdd(
      first_inclusive,
      IntPtrMul(ChangeInt32ToIntPtr(number_of_valid_entries), factor));

  BuildFastLoop<IntPtrT>(
      last_exclusive, first_inclusive,
      [=, this](TNode<IntPtrT> name_index) {
        TNode<MaybeObject> element =
            LoadArrayElement(array, Array::kHeaderSize, name_index);
        TNode<Name> candidate_name = CAST(element);
        *var_name_index = name_index;
        GotoIf(TaggedEqual(candidate_name, unique_name), if_found);
      },
      -Array::kEntrySize, LoopUnrollingMode::kYes, IndexAdvanceMode::kPre);
  Goto(if_not_found);
}

template <>
constexpr int CodeStubAssembler::MaxNumberOfEntries<TransitionArray>() {
  return TransitionsAccessor::kMaxNumberOfTransitions;
}

template <>
constexpr int CodeStubAssembler::MaxNumberOfEntries<DescriptorArray>() {
  return kMaxNumberOfDescriptors;
}

template <>
TNode<Uint32T> CodeStubAssembler::NumberOfEntries<DescriptorArray>(
    TNode<DescriptorArray> descriptors) {
  return Unsigned(LoadNumberOfDescriptors(descriptors));
}

template <>
TNode<Uint32T> CodeStubAssembler::NumberOfEntries<TransitionArray>(
    TNode<TransitionArray> transitions) {
  TNode<Uint32T> length = LoadAndUntagWeakFixedArrayLengthAsUint32(transitions);
  return Select<Uint32T>(
      Uint32LessThan(length, Uint32Constant(TransitionArray::kFirstIndex)),
      [=, this] { return Unsigned(Int32Constant(0)); },
      [=, this] {
        return Unsigned(LoadAndUntagToWord32ArrayElement(
            transitions, WeakFixedArray::kHeaderSize,
            IntPtrConstant(TransitionArray::kTransitionLengthIndex)));
      });
}

template <typename Array>
TNode<IntPtrT> CodeStubAssembler::EntryIndexToIndex(
    TNode<Uint32T> entry_index) {
  TNode<Int32T> entry_size = Int32Constant(Array::kEntrySize);
  TNode<Word32T> index = Int32Mul(entry_index, entry_size);
  return ChangeInt32ToIntPtr(index);
}

template <typename Array>
TNode<IntPtrT> CodeStubAssembler::ToKeyIndex(TNode<Uint32T> entry_index) {
  return IntPtrAdd(IntPtrConstant(Array::ToKeyIndex(0)),
                   EntryIndexToIndex<Array>(entry_index));
}

template TNode<IntPtrT> CodeStubAssembler::ToKeyIndex<DescriptorArray>(
    TNode<Uint32T>);
template TNode<IntPtrT> CodeStubAssembler::ToKeyIndex<TransitionArray>(
    TNode<Uint32T>);

template <>
TNode<Uint32T> CodeStubAssembler::GetSortedKeyIndex<DescriptorArray>(
    TNode<DescriptorArray> descriptors, TNode<Uint32T> descriptor_number) {
  TNode<Uint32T> details =
      DescriptorArrayGetDetails(descriptors, descriptor_number);
  return DecodeWord32<PropertyDetails::DescriptorPointer>(details);
}

template <>
TNode<Uint32T> CodeStubAssembler::GetSortedKeyIndex<TransitionArray>(
    TNode<TransitionArray> transitions, TNode<Uint32T> transition_number) {
  return transition_number;
}

template <typename Array>
TNode<Name> CodeStubAssembler::GetKey(TNode<Array> array,
                                      TNode<Uint32T> entry_index) {
  static_assert(std::is_base_of<TransitionArray, Array>::value ||
                    std::is_base_of<DescriptorArray, Array>::value,
                "T must be a descendant of DescriptorArray or TransitionArray");
  const int key_offset = Array::ToKeyIndex(0) * kTaggedSize;
  TNode<MaybeObject> element =
      LoadArrayElement(array, Array::kHeaderSize,
                       EntryIndexToIndex<Array>(entry_index), key_offset);
  return CAST(element);
}

template TNode<Name> CodeStubAssembler::GetKey<DescriptorArray>(
    TNode<DescriptorArray>, TNode<Uint32T>);
template TNode<Name> CodeStubAssembler::GetKey<TransitionArray>(
    TNode<TransitionArray>, TNode<Uint32T>);

TNode<Uint32T> CodeStubAssembler::DescriptorArrayGetDetails(
    TNode<DescriptorArray> descriptors, TNode<Uint32T> descriptor_number) {
  const int details_offset = DescriptorArray::ToDetailsIndex(0) * kTaggedSize;
  return Unsigned(LoadAndUntagToWord32ArrayElement(
      descriptors, DescriptorArray::kHeaderSize,
      EntryIndexToIndex<DescriptorArray>(descriptor_number), details_offset));
}

template <typename Array>
void CodeStubAssembler::LookupBinary(TNode<Name> unique_name,
                                     TNode<Array> array,
                                     TNode<Uint32T> number_of_valid_entries,
                                     Label* if_found,
                                     TVariable<IntPtrT>* var_name_index,
                                     Label* if_not_found) {
  Comment("LookupBinary");
  TVARIABLE(Uint32T, var_low, Unsigned(Int32Constant(0)));
  TNode<Uint32T> limit =
      Unsigned(Int32Sub(NumberOfEntries<Array>(array), Int32Constant(1)));
  TVARIABLE(Uint32T, var_high, limit);
  TNode<Uint32T> hash = LoadNameHashAssumeComputed(unique_name);
  CSA_DCHECK(this, Word32NotEqual(hash, Int32Constant(0)));

  // Assume non-empty array.
  CSA_DCHECK(this, Uint32LessThanOrEqual(var_low.value(), var_high.value()));

  int max_entries = MaxNumberOfEntries<Array>();

  auto calculate_mid = [&](TNode<Uint32T> low, TNode<Uint32T> high) {
    if (max_entries < kMaxInt31) {
      // mid = (low + high) / 2.
      return Unsigned(Word32Shr(Int32Add(low, high), 1));
    } else {
      // mid = low + (high - low) / 2.
      return Unsigned(Int32Add(low, Word32Shr(Int32Sub(high, low), 1)));
    }
  };

  Label binary_loop(this, {&var_high, &var_low});
  Goto(&binary_loop);
  BIND(&binary_loop);
  {
    TNode<Uint32T> mid = calculate_mid(var_low.value(), var_high.value());
    // mid_name = array->GetSortedKey(mid).
    TNode<Uint32T> sorted_key_index = GetSortedKeyIndex<Array>(array, mid);
    TNode<Name> mid_name = GetKey<Array>(array, sorted_key_index);

    TNode<Uint32T> mid_hash = LoadNameHashAssumeComputed(mid_name);

    Label mid_greater(this), mid_less(this), merge(this);
    Branch(Uint32GreaterThanOrEqual(mid_hash, hash), &mid_greater, &mid_less);
    BIND(&mid_greater);
    {
      var_high = mid;
      Goto(&merge);
    }
    BIND(&mid_less);
    {
      var_low = Unsigned(Int32Add(mid, Int32Constant(1)));
      Goto(&merge);
    }
    BIND(&merge);
    GotoIf(Word32NotEqual(var_low.value(), var_high.value()), &binary_loop);
  }

  Label scan_loop(this, &var_low);
  Goto(&scan_loop);
  BIND(&scan_loop);
  {
    GotoIf(Int32GreaterThan(var_low.value(), limit), if_not_found);

    TNode<Uint32T> sort_index =
        GetSortedKeyIndex<Array>(array, var_low.value());
    TNode<Name> current_name = GetKey<Array>(array, sort_index);
    TNode<Uint32T> current_hash = LoadNameHashAssumeComputed(current_name);
    GotoIf(Word32NotEqual(current_hash, hash), if_not_found);
    Label next(this);
    GotoIf(TaggedNotEqual(current_name, unique_name), &next);
    GotoIf(Uint32GreaterThanOrEqual(sort_index, number_of_valid_entries),
           if_not_found);
    *var_name_index = ToKeyIndex<Array>(sort_index);
    Goto(if_found);

    BIND(&next);
    var_low = Unsigned(Int32Add(var_low.value(), Int32Constant(1)));
    Goto(&scan_loop);
  }
}

void CodeStubAssembler::ForEachEnumerableOwnProperty(
    TNode<Context> context, TNode<Map> map, TNode<JSObject> object,
    PropertiesEnumerationMode mode, const ForEachKeyValueFunction& body,
    Label* bailout) {
  TNode<Uint16T> type = LoadMapInstanceType(map);
  TNode<Uint32T> bit_field3 = EnsureOnlyHasSimpleProperties(map, type, bailout);

  TVARIABLE(DescriptorArray, var_descriptors, LoadMapDescriptors(map));
  TNode<Uint32T> nof_descriptors =
      DecodeWord32<Map::Bits3::NumberOfOwnDescriptorsBits>(bit_field3);

  TVARIABLE(BoolT, var_stable, Int32TrueConstant());

  TVARIABLE(BoolT, var_has_symbol, Int32FalseConstant());
  // false - iterate only string properties, true - iterate only symbol
  // properties
  TVARIABLE(BoolT, var_is_symbol_processing_loop, Int32FalseConstant());
  TVARIABLE(IntPtrT, var_start_key_index,
            ToKeyIndex<DescriptorArray>(Unsigned(Int32Constant(0))));
  // Note: var_end_key_index is exclusive for the loop
  TVARIABLE(IntPtrT, var_end_key_index,
            ToKeyIndex<DescriptorArray>(nof_descriptors));
  VariableList list({&var_descriptors, &var_stable, &var_has_symbol,
                     &var_is_symbol_processing_loop, &var_start_key_index,
                     &var_end_key_index},
                    zone());
  Label descriptor_array_loop(this, list);

  Goto(&descriptor_array_loop);
  BIND(&descriptor_array_loop);

  BuildFastLoop<IntPtrT>(
      list, var_start_key_index.value(), var_end_key_index.value(),
      [&](TNode<IntPtrT> descriptor_key_index) {
        TNode<Name> next_key =
            LoadKeyByKeyIndex(var_descriptors.value(), descriptor_key_index);

        TVARIABLE(Object, var_value_or_accessor, SmiConstant(0));
        Label next_iteration(this);

        if (mode == kEnumerationOrder) {
          // |next_key| is either a string or a symbol
          // Skip strings or symbols depending on
          // |var_is_symbol_processing_loop|.
          Label if_string(this), if_symbol(this), if_name_ok(this);
          Branch(IsSymbol(next_key), &if_symbol, &if_string);
          BIND(&if_symbol);
          {
            // Process symbol property when |var_is_symbol_processing_loop| is
            // true.
            GotoIf(var_is_symbol_processing_loop.value(), &if_name_ok);
            // First iteration need to calculate smaller range for processing
            // symbols
            Label if_first_symbol(this);
            // var_end_key_index is still inclusive at this point.
            var_end_key_index = descriptor_key_index;
            Branch(var_has_symbol.value(), &next_iteration, &if_first_symbol);
            BIND(&if_first_symbol);
            {
              var_start_key_index = descriptor_key_index;
              var_has_symbol = Int32TrueConstant();
              Goto(&next_iteration);
            }
          }
          BIND(&if_string);
          {
            CSA_DCHECK(this, IsString(next_key));
            // Process string property when |var_is_symbol_processing_loop| is
            // false.
            Branch(var_is_symbol_processing_loop.value(), &next_iteration,
                   &if_name_ok);
          }
          BIND(&if_name_ok);
        }
        {
          TVARIABLE(Map, var_map);
          TVARIABLE(HeapObject, var_meta_storage);
          TVARIABLE(IntPtrT, var_entry);
          TVARIABLE(Uint32T, var_details);
          Label if_found(this);

          Label if_found_fast(this), if_found_dict(this);

          Label if_stable(this), if_not_stable(this);
          Branch(var_stable.value(), &if_stable, &if_not_stable);
          BIND(&if_stable);
          {
            // Directly decode from the descriptor array if |object| did not
            // change shape.
            var_map = map;
            var_meta_storage = var_descriptors.value();
            var_entry = Signed(descriptor_key_index);
            Goto(&if_found_fast);
          }
          BIND(&if_not_stable);
          {
            // If the map did change, do a slower lookup. We are still
            // guaranteed that the object has a simple shape, and that the key
            // is a name.
            var_map = LoadMap(object);
            TryLookupPropertyInSimpleObject(object, var_map.value(), next_key,
                                            &if_found_fast, &if_found_dict,
                                            &var_meta_storage, &var_entry,
                                            &next_iteration, bailout);
          }

          BIND(&if_found_fast);
          {
            TNode<DescriptorArray> descriptors = CAST(var_meta_storage.value());
            TNode<IntPtrT> name_index = var_entry.value();

            // Skip non-enumerable properties.
            var_details = LoadDetailsByKeyIndex(descriptors, name_index);
            GotoIf(IsSetWord32(var_details.value(),
                               PropertyDetails::kAttributesDontEnumMask),
                   &next_iteration);

            LoadPropertyFromFastObject(object, var_map.value(), descriptors,
                                       name_index, var_details.value(),
                                       &var_value_or_accessor);
            Goto(&if_found);
          }
          BIND(&if_found_dict);
          {
            TNode<PropertyDictionary> dictionary =
                CAST(var_meta_storage.value());
            TNode<IntPtrT> entry = var_entry.value();

            TNode<Uint32T> details = LoadDetailsByKeyIndex(dictionary, entry);
            // Skip non-enumerable properties.
            GotoIf(
                IsSetWord32(details, PropertyDetails::kAttributesDontEnumMask),
                &next_iteration);

            var_details = details;
            var_value_or_accessor =
                LoadValueByKeyIndex<PropertyDictionary>(dictionary, entry);
            Goto(&if_found);
          }

          // Here we have details and value which could be an accessor.
          BIND(&if_found);
          {
            TNode<Object> value_or_accessor = var_value_or_accessor.value();
            body(next_key, [&]() {
              TVARIABLE(Object, var_value);
              Label value_ready(this), slow_load(this, Label::kDeferred);

              var_value = CallGetterIfAccessor(
                  value_or_accessor, object, var_details.value(), context,
                  object, next_key, &slow_load, kCallJSGetterUseCachedName);
              Goto(&value_ready);

              BIND(&slow_load);
              var_value =
                  CallRuntime(Runtime::kGetProperty, context, object, next_key);
              Goto(&value_ready);

              BIND(&value_ready);
              return var_value.value();
            });

            // Check if |object| is still stable, i.e. the descriptors in the
            // preloaded |descriptors| are still the same modulo in-place
            // representation changes.
            GotoIfNot(var_stable.value(), &next_iteration);
            var_stable = TaggedEqual(LoadMap(object), map);
            // Reload the descriptors just in case the actual array changed, and
            // any of the field representations changed in-place.
            var_descriptors = LoadMapDescriptors(map);

            Goto(&next_iteration);
          }
        }
        BIND(&next_iteration);
      },
      DescriptorArray::kEntrySize, LoopUnrollingMode::kNo,
      IndexAdvanceMode::kPost);

  if (mode == kEnumerationOrder) {
    Label done(this);
    GotoIf(var_is_symbol_processing_loop.value(), &done);
    GotoIfNot(var_has_symbol.value(), &done);
    // All string properties are processed, now process symbol properties.
    var_is_symbol_processing_loop = Int32TrueConstant();
    // Add DescriptorArray::kEntrySize to make the var_end_key_index exclusive
    // as BuildFastLoop() expects.
    Increment(&var_end_key_index, DescriptorArray::kEntrySize);
    Goto(&descriptor_array_loop);

    BIND(&done);
  }
}

TNode<Object> CodeStubAssembler::GetConstructor(TNode<Map> map) {
  TVARIABLE(HeapObject, var_maybe_constructor);
  var_maybe_constructor = map;
  Label loop(this, &var_maybe_constructor), done(this);
  GotoIfNot(IsMap(var_maybe_constructor.value()), &done);
  Goto(&loop);

  BIND(&loop);
  {
    var_maybe_constructor = CAST(
        LoadObjectField(var_maybe_constructor.value(),
                        Map::kConstructorOrBackPointerOrNativeContextOffset));
    GotoIf(IsMap(var_maybe_constructor.value()), &loop);
    Goto(&done);
  }

  BIND(&done);
  return var_maybe_constructor.value();
}

TNode<NativeContext> CodeStubAssembler::GetCreationContextFromMap(
    TNode<Map> map, Label* if_bailout) {
  TNode<Map> meta_map = LoadMap(map);
  TNode<Object> maybe_context =
      LoadMapConstructorOrBackPointerOrNativeContext(meta_map);
  GotoIf(IsNull(maybe_context), if_bailout);
  return CAST(maybe_context);
}

TNode<NativeContext> CodeStubAssembler::GetCreationContext(
    TNode<JSReceiver> receiver, Label* if_bailout) {
  return GetCreationContextFromMap(LoadMap(receiver), if_bailout);
}

TNode<NativeContext> CodeStubAssembler::GetFunctionRealm(
    TNode<Context> context, TNode<JSReceiver> receiver, Label* if_bailout) {
  TVARIABLE(JSReceiver, current);
  TVARIABLE(Map, current_map);
  Label loop(this, VariableList({&current}, zone())), if_proxy(this),
      if_simple_case(this), if_bound_function(this), if_wrapped_function(this),
      proxy_revoked(this, Label::kDeferred);
  CSA_DCHECK(this, IsCallable(receiver));
  current = receiver;
  Goto(&loop);

  BIND(&loop);
  {
    current_map = LoadMap(current.value());
    TNode<Int32T> instance_type = LoadMapInstanceType(current_map.value());
    GotoIf(IsJSFunctionInstanceType(instance_type), &if_simple_case);
    GotoIf(InstanceTypeEqual(instance_type, JS_PROXY_TYPE), &if_proxy);
    GotoIf(InstanceTypeEqual(instance_type, JS_BOUND_FUNCTION_TYPE),
           &if_bound_function);
    GotoIf(InstanceTypeEqual(instance_type, JS_WRAPPED_FUNCTION_TYPE),
           &if_wrapped_function);
    Goto(&if_simple_case);
  }

  BIND(&if_proxy);
  {
    TNode<JSProxy> proxy = CAST(current.value());
    TNode<HeapObject> handler =
        CAST(LoadObjectField(proxy, JSProxy::kHandlerOffset));
    // Proxy is revoked.
    GotoIfNot(IsJSReceiver(handler), &proxy_revoked);
    TNode<JSReceiver> target =
        CAST(LoadObjectField(proxy, JSProxy::kTargetOffset));
    current = target;
    Goto(&loop);
  }

  BIND(&proxy_revoked);
  { ThrowTypeError(context, MessageTemplate::kProxyRevoked, "apply"); }

  BIND(&if_bound_function);
  {
    TNode<JSBoundFunction> bound_function = CAST(current.value());
    TNode<JSReceiver> target = CAST(LoadObjectField(
        bound_function, JSBoundFunction::kBoundTargetFunctionOffset));
    current = target;
    Goto(&loop);
  }

  BIND(&if_wrapped_function);
  {
    TNode<JSWrappedFunction> wrapped_function = CAST(current.value());
    TNode<JSReceiver> target = CAST(LoadObjectField(
        wrapped_function, JSWrappedFunction::kWrappedTargetFunctionOffset));
    current = target;
    Goto(&loop);
  }

  BIND(&if_simple_case);
  {
    // Load native context from the meta map.
    return GetCreationContextFromMap(current_map.value(), if_bailout);
  }
}

void CodeStubAssembler::DescriptorLookup(TNode<Name> unique_name,
                                         TNode<DescriptorArray> descriptors,
                                         TNode<Uint32T> bitfield3,
                                         Label* if_found,
                                         TVariable<IntPtrT>* var_name_index,
                                         Label* if_not_found) {
  Comment("DescriptorArrayLookup");
  TNode<Uint32T> nof =
      DecodeWord32<Map::Bits3::NumberOfOwnDescriptorsBits>(bitfield3);
  Lookup<DescriptorArray>(unique_name, descriptors, nof, if_found,
                          var_name_index, if_not_found);
}

void CodeStubAssembler::TransitionLookup(TNode<Name> unique_name,
                                         TNode<TransitionArray> transitions,
                                         Label* if_found,
                                         TVariable<IntPtrT>* var_name_index,
                                         Label* if_not_found) {
  Comment("TransitionArrayLookup");
  TNode<Uint32T> number_of_valid_transitions =
      NumberOfEntries<TransitionArray>(transitions);
  Lookup<TransitionArray>(unique_name, transitions, number_of_valid_transitions,
                          if_found, var_name_index, if_not_found);
}

template <typename Array>
void CodeStubAssembler::Lookup(TNode<Name> unique_name, TNode<Array> array,
                               TNode<Uint32T> number_of_valid_entries,
                               Label* if_found,
                               TVariable<IntPtrT>* var_name_index,
                               Label* if_not_found) {
  Comment("ArrayLookup");
  if (!number_of_valid_entries) {
    number_of_valid_entries = NumberOfEntries(array);
  }
  GotoIf(Word32Equal(number_of_valid_entries, Int32Constant(0)), if_not_found);
  Label linear_search(this), binary_search(this);
  const int kMaxElementsForLinearSearch = 32;
  Branch(Uint32LessThanOrEqual(number_of_valid_entries,
                               Int32Constant(kMaxElementsForLinearSearch)),
         &linear_search, &binary_search);
  BIND(&linear_search);
  {
    LookupLinear<Array>(unique_name, array, number_of_valid_entries, if_found,
                        var_name_index, if_not_found);
  }
  BIND(&binary_search);
  {
    LookupBinary<Array>(unique_name, array, number_of_valid_entries, if_found,
                        var_name_index, if_not_found);
  }
}

void CodeStubAssembler::TryLookupPropertyInSimpleObject(
    TNode<JSObject> object, TNode<Map> map, TNode<Name> unique_name,
    Label* if_found_fast, Label* if_found_dict,
    TVariable<HeapObject>* var_meta_storage, TVariable<IntPtrT>* var_name_index,
    Label* if_not_found, Label* bailout) {
  CSA_DCHECK(this, IsSimpleObjectMap(map));
  CSA_DCHECK(this, IsUniqueNameNoCachedIndex(unique_name));

  TNode<Uint32T> bit_field3 = LoadMapBitField3(map);
  Label if_isfastmap(this), if_isslowmap(this);
  Branch(IsSetWord32<Map::Bits3::IsDictionaryMapBit>(bit_field3), &if_isslowmap,
         &if_isfastmap);
  BIND(&if_isfastmap);
  {
    TNode<DescriptorArray> descriptors = LoadMapDescriptors(map);
    *var_meta_storage = descriptors;

    DescriptorLookup(unique_name, descriptors, bit_field3, if_found_fast,
                     var_name_index, if_not_found);
  }
  BIND(&if_isslowmap);
  {
    TNode<PropertyDictionary> dictionary = CAST(LoadSlowProperties(object));
    *var_meta_storage = dictionary;

    NameDictionaryLookup<PropertyDictionary>(
        dictionary, unique_name, if_found_dict, var_name_index, if_not_found);
  }
}

void CodeStubAssembler::TryLookupProperty(
    TNode<HeapObject> object, TNode<Map> map, TNode<Int32T> instance_type,
    TNode<Name> unique_name, Label* if_found_fast, Label* if_found_dict,
    Label* if_found_global, TVariable<HeapObject>* var_meta_storage,
    TVariable<IntPtrT>* var_name_index, Label* if_not_found,
    Label* if_bailout) {
  Label if_objectisspecial(this);
  GotoIf(IsSpecialReceiverInstanceType(instance_type), &if_objectisspecial);

  TryLookupPropertyInSimpleObject(CAST(object), map, unique_name, if_found_fast,
                                  if_found_dict, var_meta_storage,
                                  var_name_index, if_not_found, if_bailout);

  BIND(&if_objectisspecial);
  {
    // Handle global object here and bailout for other special objects.
    GotoIfNot(InstanceTypeEqual(instance_type, JS_GLOBAL_OBJECT_TYPE),
              if_bailout);

    // Handle interceptors and access checks in runtime.
    TNode<Int32T> bit_field = LoadMapBitField(map);
    int mask = Map::Bits1::HasNamedInterceptorBit::kMask |
               Map::Bits1::IsAccessCheckNeededBit::kMask;
    GotoIf(IsSetWord32(bit_field, mask), if_bailout);

    TNode<GlobalDictionary> dictionary = CAST(LoadSlowProperties(CAST(object)));
    *var_meta_storage = dictionary;

    NameDictionaryLookup<GlobalDictionary>(
        dictionary, unique_name, if_found_global, var_name_index, if_not_found);
  }
}

void CodeStubAssembler::TryHasOwnProperty(TNode<HeapObject> object,
                                          TNode<Map> map,
                                          TNode<Int32T> instance_type,
                                          TNode<Name> unique_name,
                                          Label* if_found, Label* if_not_found,
                                          Label* if_bailout) {
  Comment("TryHasOwnProperty");
  CSA_DCHECK(this, IsUniqueNameNoCachedIndex(unique_name));
  TVARIABLE(HeapObject, var_meta_storage);
  TVARIABLE(IntPtrT, var_name_index);

  Label if_found_global(this);
  TryLookupProperty(object, map, instance_type, unique_name, if_found, if_found,
                    &if_found_global, &var_meta_storage, &var_name_index,
                    if_not_found, if_bailout);

  BIND(&if_found_global);
  {
    TVARIABLE(Object, var_value);
    TVARIABLE(Uint32T, var_details);
    // Check if the property cell is not deleted.
    LoadPropertyFromGlobalDictionary(CAST(var_meta_storage.value()),
                                     var_name_index.value(), &var_details,
                                     &var_value, if_not_found);
    Goto(if_found);
  }
}

TNode<Object> CodeStubAssembler::GetMethod(TNode<Context> context,
                                           TNode<Object> object,
                                           Handle<Name> name,
                                           Label* if_null_or_undefined) {
  TNode<Object> method = GetProperty(context, object, name);

  GotoIf(IsUndefined(method), if_null_or_undefined);
  GotoIf(IsNull(method), if_null_or_undefined);

  return method;
}

TNode<Object> CodeStubAssembler::GetIteratorMethod(
    TNode<Context> context, TNode<HeapObject> heap_obj,
    Label* if_iteratorundefined) {
  return GetMethod(context, heap_obj, isolate()->factory()->iterator_symbol(),
                   if_iteratorundefined);
}

TNode<Object> CodeStubAssembler::CreateAsyncFromSyncIterator(
    TNode<Context> context, TNode<Object> sync_iterator) {
  Label not_receiver(this, Label::kDeferred);
  Label done(this);
  TVARIABLE(Object, return_value);

  GotoIf(TaggedIsSmi(sync_iterator), &not_receiver);
  GotoIfNot(IsJSReceiver(CAST(sync_iterator)), &not_receiver);

  const TNode<Object> next =
      GetProperty(context, sync_iterator, factory()->next_string());
  return_value =
      CreateAsyncFromSyncIterator(context, CAST(sync_iterator), next);
  Goto(&done);

  BIND(&not_receiver);
  {
    return_value = CallRuntime(Runtime::kThrowSymbolIteratorInvalid, context);

    // Unreachable due to the Throw in runtime call.
    Goto(&done);
  }

  BIND(&done);
  return return_value.value();
}

TNode<JSObject> CodeStubAssembler::CreateAsyncFromSyncIterator(
    TNode<Context> context, TNode<JSReceiver> sync_iterator,
    TNode<Object> next) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Map> map = CAST(LoadContextElement(
      native_context, Context::ASYNC_FROM_SYNC_ITERATOR_MAP_INDEX));
  const TNode<JSObject> iterator = AllocateJSObjectFromMap(map);

  StoreObjectFieldNoWriteBarrier(
      iterator, JSAsyncFromSyncIterator::kSyncIteratorOffset, sync_iterator);
  StoreObjectFieldNoWriteBarrier(iterator, JSAsyncFromSyncIterator::kNextOffset,
                                 next);
  return iterator;
}

void CodeStubAssembler::LoadPropertyFromFastObject(
    TNode<HeapObject> object, TNode<Map> map,
    TNode<DescriptorArray> descriptors, TNode<IntPtrT> name_index,
    TVariable<Uint32T>* var_details, TVariable<Object>* var_value) {
  TNode<Uint32T> details = LoadDetailsByKeyIndex(descriptors, name_index);
  *var_details = details;

  LoadPropertyFromFastObject(object, map, descriptors, name_index, details,
                             var_value);
}

void CodeStubAssembler::LoadPropertyFromFastObject(
    TNode<HeapObject> object, TNode<Map> map,
    TNode<DescriptorArray> descriptors, TNode<IntPtrT> name_index,
    TNode<Uint32T> details, TVariable<Object>* var_value) {
  Comment("[ LoadPropertyFromFastObject");

  TNode<Uint32T> location =
      DecodeWord32<PropertyDetails::LocationField>(details);

  Label if_in_field(this), if_in_descriptor(this), done(this);
  Branch(Word32Equal(location, Int32Constant(static_cast<int32_t>(
                                   PropertyLocation::kField))),
         &if_in_field, &if_in_descriptor);
  BIND(&if_in_field);
  {
    TNode<IntPtrT> field_index =
        Signed(DecodeWordFromWord32<PropertyDetails::FieldIndexField>(details));
    TNode<Uint32T> representation =
        DecodeWord32<PropertyDetails::RepresentationField>(details);

    // TODO(ishell): support WasmValues.
    CSA_DCHECK(this, Word32NotEqual(representation,
                                    Int32Constant(Representation::kWasmValue)));
    field_index =
        IntPtrAdd(field_index, LoadMapInobjectPropertiesStartInWords(map));
    TNode<IntPtrT> instance_size_in_words = LoadMapInstanceSizeInWords(map);

    Label if_inobject(this), if_backing_store(this);
    TVARIABLE(Float64T, var_double_value);
    Label rebox_double(this, &var_double_value);
    Branch(UintPtrLessThan(field_index, instance_size_in_words), &if_inobject,
           &if_backing_store);
    BIND(&if_inobject);
    {
      Comment("if_inobject");
      TNode<IntPtrT> field_offset = TimesTaggedSize(field_index);

      Label if_double(this), if_tagged(this);
      Branch(Word32NotEqual(representation,
                            Int32Constant(Representation::kDouble)),
             &if_tagged, &if_double);
      BIND(&if_tagged);
      {
        *var_value = LoadObjectField(object, field_offset);
        Goto(&done);
      }
      BIND(&if_double);
      {
        TNode<HeapNumber> heap_number =
            CAST(LoadObjectField(object, field_offset));
        var_double_value = LoadHeapNumberValue(heap_number);
        Goto(&rebox_double);
      }
    }
    BIND(&if_backing_store);
    {
      Comment("if_backing_store");
      TNode<HeapObject> properties = LoadFastProperties(CAST(object), true);
      field_index = Signed(IntPtrSub(field_index, instance_size_in_words));
      TNode<Object> value =
          LoadPropertyArrayElement(CAST(properties), field_index);

      Label if_double(this), if_tagged(this);
      Branch(Word32NotEqual(representation,
                            Int32Constant(Representation::kDouble)),
             &if_tagged, &if_double);
      BIND(&if_tagged);
      {
        *var_value = value;
        Goto(&done);
      }
      BIND(&if_double);
      {
        var_double_value = LoadHeapNumberValue(CAST(value));
        Goto(&rebox_double);
      }
    }
    BIND(&rebox_double);
    {
      Comment("rebox_double");
      TNode<HeapNumber> heap_number =
          AllocateHeapNumberWithValue(var_double_value.value());
      *var_value = heap_number;
      Goto(&done);
    }
  }
  BIND(&if_in_descriptor);
  {
    *var_value = LoadValueByKeyIndex(descriptors, name_index);
    Goto(&done);
  }
  BIND(&done);

  Comment("] LoadPropertyFromFastObject");
}

template <typename Dictionary>
void CodeStubAssembler::LoadPropertyFromDictionary(
    TNode<Dictionary> dictionary, TNode<IntPtrT> name_index,
    TVariable<Uint32T>* var_details, TVariable<Object>* var_value) {
  Comment("LoadPropertyFromNameDictionary");
  *var_details = LoadDetailsByKeyIndex(dictionary, name_index);
  *var_value = LoadValueByKeyIndex(dictionary, name_index);

  Comment("] LoadPropertyFromNameDictionary");
}

void CodeStubAssembler::LoadPropertyFromGlobalDictionary(
    TNode<GlobalDictionary> dictionary, TNode<IntPtrT> name_index,
    TVariable<Uint32T>* var_details, TVariable<Object>* var_value,
    Label* if_deleted) {
  Comment("[ LoadPropertyFromGlobalDictionary");
  TNode<PropertyCell> property_cell =
      CAST(LoadFixedArrayElement(dictionary, name_index));

  TNode<Object> value =
      LoadObjectField(property_cell, PropertyCell::kValueOffset);
  GotoIf(TaggedEqual(value, PropertyCellHoleConstant()), if_deleted);

  *var_value = value;

  TNode<Uint32T> details = Unsigned(LoadAndUntagToWord32ObjectField(
      property_cell, PropertyCell::kPropertyDetailsRawOffset));
  *var_details = details;

  Comment("] LoadPropertyFromGlobalDictionary");
}

template void CodeStubAssembler::LoadPropertyFromDictionary(
    TNode<NameDictionary> dictionary, TNode<IntPtrT> name_index,
    TVariable<Uint32T>* var_details, TVariable<Object>* var_value);

template void CodeStubAssembler::LoadPropertyFromDictionary(
    TNode<SwissNameDictionary> dictionary, TNode<IntPtrT> name_index,
    TVariable<Uint32T>* var_details, TVariable<Object>* var_value);

// |value| is the property backing store's contents, which is either a value or
// an accessor pair, as specified by |details|. |holder| is a JSObject or a
// PropertyCell (TODO: use Union). Returns either the original value, or the
// result of the getter call.
TNode<Object> CodeStubAssembler::CallGetterIfAccessor(
    TNode<Object> value, TNode<HeapObject> holder, TNode<Uint32T> details,
    TNode<Context> context, TNode<Object> receiver, TNode<Object> name,
    Label* if_bailout, GetOwnPropertyMode mode,
    ExpectedReceiverMode expected_receiver_mode) {
  TVARIABLE(Object, var_value, value);
  Label done(this), if_accessor_info(this, Label::kDeferred);

  TNode<Uint32T> kind = DecodeWord32<PropertyDetails::KindField>(details);
  GotoIf(
      Word32Equal(kind, Int32Constant(static_cast<int>(PropertyKind::kData))),
      &done);

  // Accessor case.
  GotoIfNot(IsAccessorPair(CAST(value)), &if_accessor_info);

  // AccessorPair case.
  {
    if (mode == kCallJSGetterUseCachedName ||
        mode == kCallJSGetterDontUseCachedName) {
      Label if_callable(this), if_function_template_info(this);
      TNode<AccessorPair> accessor_pair = CAST(value);
      TNode<HeapObject> getter =
          CAST(LoadObjectField(accessor_pair, AccessorPair::kGetterOffset));
      TNode<Map> getter_map = LoadMap(getter);

      GotoIf(IsCallableMap(getter_map), &if_callable);
      GotoIf(IsFunctionTemplateInfoMap(getter_map), &if_function_template_info);

      // Return undefined if the {getter} is not callable.
      var_value = UndefinedConstant();
      Goto(&done);

      BIND(&if_callable);
      {
        // Call the accessor. No need to check side-effect mode here, since it
        // will be checked later in DebugOnFunctionCall.
        // It's too early to convert receiver to JSReceiver at this point
        // (the Call builtin will do the conversion), so we ignore the
        // |expected_receiver_mode| here.
        var_value = Call(context, getter, receiver);
        Goto(&done);
      }

      BIND(&if_function_template_info);
      {
        Label use_cached_property(this);
        TNode<HeapObject> cached_property_name = LoadObjectField<HeapObject>(
            getter, FunctionTemplateInfo::kCachedPropertyNameOffset);

        Label* has_cached_property = mode == kCallJSGetterUseCachedName
                                         ? &use_cached_property
                                         : if_bailout;
        GotoIfNot(IsTheHole(cached_property_name), has_cached_property);

        TNode<JSReceiver> js_receiver;
        switch (expected_receiver_mode) {
          case kExpectingJSReceiver:
            js_receiver = CAST(receiver);
            break;
          case kExpectingAnyReceiver:
            // TODO(ishell): in case the function template info has a signature
            // and receiver is not a JSReceiver the signature check in
            // CallFunctionTemplate builtin will fail anyway, so we can short
            // cut it here and throw kIllegalInvocation immediately.
            js_receiver = ToObject_Inline(context, receiver);
            break;
        }
        TNode<NativeContext> creation_context =
            GetCreationContext(CAST(holder), if_bailout);
        TNode<Context> caller_context = context;
        var_value = CallBuiltin(
            Builtin::kCallFunctionTemplate_Generic, creation_context, getter,
            Int32Constant(i::JSParameterCount(0)), caller_context, js_receiver);
        Goto(&done);

        if (mode == kCallJSGetterUseCachedName) {
          Bind(&use_cached_property);

          var_value = GetProperty(context, holder, cached_property_name);

          Goto(&done);
        }
      }
    } else {
      DCHECK_EQ(mode, kReturnAccessorPair);
      Goto(&done);
    }
  }

  // AccessorInfo case.
  BIND(&if_accessor_info);
  {
    TNode<AccessorInfo> accessor_info = CAST(value);
    Label if_array(this), if_function(this), if_wrapper(this);

    // Dispatch based on {holder} instance type.
    TNode<Map> holder_map = LoadMap(holder);
    TNode<Uint16T> holder_instance_type = LoadMapInstanceType(holder_map);
    GotoIf(IsJSArrayInstanceType(holder_instance_type), &if_array);
    GotoIf(IsJSFunctionInstanceType(holder_instance_type), &if_function);
    Branch(IsJSPrimitiveWrapperInstanceType(holder_instance_type), &if_wrapper,
           if_bailout);

    // JSArray AccessorInfo case.
    BIND(&if_array);
    {
      // We only deal with the "length" accessor on JSArray.
      GotoIfNot(IsLengthString(
                    LoadObjectField(accessor_info, AccessorInfo::kNameOffset)),
                if_bailout);
      TNode<JSArray> array = CAST(holder);
      var_value = LoadJSArrayLength(array);
      Goto(&done);
    }

    // JSFunction AccessorInfo case.
    BIND(&if_function);
    {
      // We only deal with the "prototype" accessor on JSFunction here.
      GotoIfNot(IsPrototypeString(
                    LoadObjectField(accessor_info, AccessorInfo::kNameOffset)),
                if_bailout);

      TNode<JSFunction> function = CAST(holder);
      GotoIfPrototypeRequiresRuntimeLookup(function, holder_map, if_bailout);
      var_value = LoadJSFunctionPrototype(function, if_bailout);
      Goto(&done);
    }

    // JSPrimitiveWrapper AccessorInfo case.
    BIND(&if_wrapper);
    {
      // We only deal with the "length" accessor on JSPrimitiveWrapper string
      // wrappers.
      GotoIfNot(IsLengthString(
                    LoadObjectField(accessor_info, AccessorInfo::kNameOffset)),
                if_bailout);
      TNode<Object> holder_value = LoadJSPrimitiveWrapperValue(CAST(holder));
      GotoIfNot(TaggedIsNotSmi(holder_value), if_bailout);
      GotoIfNot(IsString(CAST(holder_value)), if_bailout);
      var_value = LoadStringLengthAsSmi(CAST(holder_value));
      Goto(&done);
    }
  }

  BIND(&done);
  return var_value.value();
}

void CodeStubAssembler::TryGetOwnProperty(
    TNode<Context> context, TNode<Object> receiver, TNode<JSReceiver> object,
    TNode<Map> map, TNode<Int32T> instance_type, TNode<Name> unique_name,
    Label* if_found_value, TVariable<Object>* var_value, Label* if_not_found,
    Label* if_bailout, ExpectedReceiverMode expected_receiver_mode) {
  TryGetOwnProperty(context, receiver, object, map, instance_type, unique_name,
                    if_found_value, var_value, nullptr, nullptr, if_not_found,
                    if_bailout,
                    receiver == object ? kCallJSGetterUseCachedName
                                       : kCallJSGetterDontUseCachedName,
                    expected_receiver_mode);
}

void CodeStubAssembler::TryGetOwnProperty(
    TNode<Context> context, TNode<Object> receiver, TNode<JSReceiver> object,
    TNode<Map> map, TNode<Int32T> instance_type, TNode<Name> unique_name,
    Label* if_found_value, TVariable<Object>* var_value,
    TVariable<Uint32T>* var_details, TVariable<Object>* var_raw_value,
    Label* if_not_found, Label* if_bailout, GetOwnPropertyMode mode,
    ExpectedReceiverMode expected_receiver_mode) {
  DCHECK_EQ(MachineRepresentation::kTagged, var_value->rep());
  Comment("TryGetOwnProperty");
  if (receiver == object) {
    // If |receiver| is exactly the same Node as the |object| which is
    // guaranteed to be JSReceiver override the |expected_receiver_mode|.
    expected_receiver_mode = kExpectingJSReceiver;
  }
  CSA_DCHECK(this, IsUniqueNameNoCachedIndex(unique_name));
  TVARIABLE(HeapObject, var_meta_storage);
  TVARIABLE(IntPtrT, var_entry);

  Label if_found_fast(this), if_found_dict(this), if_found_global(this);

  TVARIABLE(Uint32T, local_var_details);
  if (!var_details) {
    var_details = &local_var_details;
  }
  Label if_found(this);

  TryLookupProperty(object, map, instance_type, unique_name, &if_found_fast,
                    &if_found_dict, &if_found_global, &var_meta_storage,
                    &var_entry, if_not_found, if_bailout);
  BIND(&if_found_fast);
  {
    TNode<DescriptorArray> descriptors = CAST(var_meta_storage.value());
    TNode<IntPtrT> name_index = var_entry.value();

    LoadPropertyFromFastObject(object, map, descriptors, name_index,
                               var_details, var_value);
    Goto(&if_found);
  }
  BIND(&if_found_dict);
  {
    TNode<PropertyDictionary> dictionary = CAST(var_meta_storage.value());
    TNode<IntPtrT> entry = var_entry.value();
    LoadPropertyFromDictionary(dictionary, entry, var_details, var_value);

    Goto(&if_found);
  }
  BIND(&if_found_global);
  {
    TNode<GlobalDictionary> dictionary = CAST(var_meta_storage.value());
    TNode<IntPtrT> entry = var_entry.value();

    LoadPropertyFromGlobalDictionary(dictionary, entry, var_details, var_value,
                                     if_not_found);
    Goto(&if_found);
  }
  // Here we have details and value which could be an accessor.
  BIND(&if_found);
  {
    // TODO(ishell): Execute C++ accessor in case of accessor info
    if (var_raw_value) {
      *var_raw_value = *var_value;
    }
    TNode<Object> value = CallGetterIfAccessor(
        var_value->value(), object, var_details->value(), context, receiver,
        unique_name, if_bailout, mode, expected_receiver_mode);
    *var_value = value;
    Goto(if_found_value);
  }
}

void CodeStubAssembler::InitializePropertyDescriptorObject(
    TNode<PropertyDescriptorObject> descriptor, TNode<Object> value,
    TNode<Uint32T> details, Label* if_bailout) {
  Label if_data_property(this), if_accessor_property(this),
      test_configurable(this), test_property_type(this), done(this);
  TVARIABLE(Smi, flags,
            SmiConstant(PropertyDescriptorObject::HasEnumerableBit::kMask |
                        PropertyDescriptorObject::HasConfigurableBit::kMask));

  {  // test enumerable
    TNode<Uint32T> dont_enum =
        Uint32Constant(DONT_ENUM << PropertyDetails::AttributesField::kShift);
    GotoIf(Word32And(details, dont_enum), &test_configurable);
    flags =
        SmiOr(flags.value(),
              SmiConstant(PropertyDescriptorObject::IsEnumerableBit::kMask));
    Goto(&test_configurable);
  }

  BIND(&test_configurable);
  {
    TNode<Uint32T> dont_delete =
        Uint32Constant(DONT_DELETE << PropertyDetails::AttributesField::kShift);
    GotoIf(Word32And(details, dont_delete), &test_property_type);
    flags =
        SmiOr(flags.value(),
              SmiConstant(PropertyDescriptorObject::IsConfigurableBit::kMask));
    Goto(&test_property_type);
  }

  BIND(&test_property_type);
  BranchIfAccessorPair(value, &if_accessor_property, &if_data_property);

  BIND(&if_accessor_property);
  {
    Label done_get(this), store_fields(this);
    TNode<AccessorPair> accessor_pair = CAST(value);

    auto BailoutIfTemplateInfo = [this, &if_bailout](TNode<HeapObject> value) {
      TVARIABLE(HeapObject, result);

      Label bind_undefined(this), return_result(this);
      GotoIf(IsNull(value), &bind_undefined);
      result = value;
      TNode<Map> map = LoadMap(value);
      // TODO(ishell): probe template instantiations cache.
      GotoIf(IsFunctionTemplateInfoMap(map), if_bailout);
      Goto(&return_result);

      BIND(&bind_undefined);
      result = UndefinedConstant();
      Goto(&return_result);

      BIND(&return_result);
      return result.value();
    };

    TNode<HeapObject> getter =
        LoadObjectField<HeapObject>(accessor_pair, AccessorPair::kGetterOffset);
    TNode<HeapObject> setter =
        LoadObjectField<HeapObject>(accessor_pair, AccessorPair::kSetterOffset);
    getter = BailoutIfTemplateInfo(getter);
    setter = BailoutIfTemplateInfo(setter);

    Label bind_undefined(this, Label::kDeferred), return_result(this);
    flags = SmiOr(flags.value(),
                  SmiConstant(PropertyDescriptorObject::HasGetBit::kMask |
                              PropertyDescriptorObject::HasSetBit::kMask));
    StoreObjectField(descriptor, PropertyDescriptorObject::kFlagsOffset,
                     flags.value());
    StoreObjectField(descriptor, PropertyDescriptorObject::kValueOffset,
                     NullConstant());
    StoreObjectField(descriptor, PropertyDescriptorObject::kGetOffset,
                     BailoutIfTemplateInfo(getter));
    StoreObjectField(descriptor, PropertyDescriptorObject::kSetOffset,
                     BailoutIfTemplateInfo(setter));
    Goto(&done);
  }

  BIND(&if_data_property);
  {
    Label store_fields(this);
    flags = SmiOr(flags.value(),
                  SmiConstant(PropertyDescriptorObject::HasValueBit::kMask |
                              PropertyDescriptorObject::HasWritableBit::kMask));
    TNode<Uint32T> read_only =
        Uint32Constant(READ_ONLY << PropertyDetails::AttributesField::kShift);
    GotoIf(Word32And(details, read_only), &store_fields);
    flags = SmiOr(flags.value(),
                  SmiConstant(PropertyDescriptorObject::IsWritableBit::kMask));
    Goto(&store_fields);

    BIND(&store_fields);
    StoreObjectField(descriptor, PropertyDescriptorObject::kFlagsOffset,
                     flags.value());
    StoreObjectField(descriptor, PropertyDescriptorObject::kValueOffset, value);
    StoreObjectField(descriptor, PropertyDescriptorObject::kGetOffset,
                     NullConstant());
    StoreObjectField(descriptor, PropertyDescriptorObject::kSetOffset,
                     NullConstant());
    Goto(&done);
  }

  BIND(&done);
}

TNode<PropertyDescriptorObject>
CodeStubAssembler::AllocatePropertyDescriptorObject(TNode<Context> context) {
  TNode<HeapObject> result = Allocate(PropertyDescriptorObject::kSize);
  TNode<Map> map = GetInstanceTypeMap(PROPERTY_DESCRIPTOR_OBJECT_TYPE);
  StoreMapNoWriteBarrier(result, map);
  TNode<Smi> zero = SmiConstant(0);
  StoreObjectFieldNoWriteBarrier(result, PropertyDescriptorObject::kFlagsOffset,
                                 zero);
  TNode<Hole> the_hole = TheHoleConstant();
  StoreObjectFieldNoWriteBarrier(result, PropertyDescriptorObject::kValueOffset,
                                 the_hole);
  StoreObjectFieldNoWriteBarrier(result, PropertyDescriptorObject::kGetOffset,
                                 the_hole);
  StoreObjectFieldNoWriteBarrier(result, PropertyDescriptorObject::kSetOffset,
                                 the_hole);
  return CAST(result);
}

TNode<BoolT> CodeStubAssembler::IsInterestingProperty(TNode<Name> name) {
  TVARIABLE(BoolT, var_result);
  Label return_false(this), return_true(this), return_generic(this);
  // TODO(ishell): consider using ReadOnlyRoots::IsNameForProtector() trick for
  // these strings and interesting symbols.
  GotoIf(IsToJSONString(name), &return_true);
  GotoIf(IsGetString(name), &return_true);
  GotoIfNot(InstanceTypeEqual(LoadMapInstanceType(LoadMap(name)), SYMBOL_TYPE),
            &return_false);
  Branch(IsSetWord32<Symbol::IsInterestingSymbolBit>(
             LoadObjectField<Uint32T>(name, offsetof(Symbol, flags_))),
         &return_true, &return_false);

  BIND(&return_false);
  var_result = BoolConstant(false);
  Goto(&return_generic);

  BIND(&return_true);
  var_result = BoolConstant(true);
  Goto(&return_generic);

  BIND(&return_generic);
  return var_result.value();
}

TNode<Object> CodeStubAssembler::GetInterestingProperty(
    TNode<Context> context, TNode<JSReceiver> receiver, TNode<Name> name,
    Label* if_not_found) {
  TVARIABLE(HeapObject, var_holder, receiver);
  TVARIABLE(Map, var_holder_map, LoadMap(receiver));

  return GetInterestingProperty(context, receiver, &var_holder, &var_holder_map,
                                name, if_not_found);
}

TNode<Object> CodeStubAssembler::GetInterestingProperty(
    TNode<Context> context, TNode<Object> receiver,
    TVariable<HeapObject>* var_holder, TVariable<Map>* var_holder_map,
    TNode<Name> name, Label* if_not_found) {
  CSA_DCHECK(this, IsInterestingProperty(name));
  // The lookup starts at the var_holder and var_holder_map must contain
  // var_holder's map.
  CSA_DCHECK(this, TaggedEqual(LoadMap((*var_holder).value()),
                               (*var_holder_map).value()));
  TVARIABLE(Object, var_result, UndefinedConstant());

  // Check if all relevant maps (including the prototype maps) don't
  // have any interesting properties (i.e. that none of them have the
  // @@toStringTag or @@toPrimitive property).
  Label loop(this, {var_holder, var_holder_map}),
      lookup(this, Label::kDeferred);
  Goto(&loop);
  BIND(&loop);
  {
    Label interesting_properties(this);
    TNode<HeapObject> holder = (*var_holder).value();
    TNode<Map> holder_map = (*var_holder_map).value();
    GotoIf(IsNull(holder), if_not_found);
    TNode<Uint32T> holder_bit_field3 = LoadMapBitField3(holder_map);
    GotoIf(IsSetWord32<Map::Bits3::MayHaveInterestingPropertiesBit>(
               holder_bit_field3),
           &interesting_properties);
    *var_holder = LoadMapPrototype(holder_map);
    *var_holder_map = LoadMap((*var_holder).value());
    Goto(&loop);
    BIND(&interesting_properties);
    {
      // Check flags for dictionary objects.
      GotoIf(IsClearWord32<Map::Bits3::IsDictionaryMapBit>(holder_bit_field3),
             &lookup);
      // JSProxy has dictionary properties but has to be handled in runtime.
      GotoIf(InstanceTypeEqual(LoadMapInstanceType(holder_map), JS_PROXY_TYPE),
             &lookup);
      TNode<Object> properties =
          LoadObjectField(holder, JSObject::kPropertiesOrHashOffset);
      CSA_DCHECK(this, TaggedIsNotSmi(properties));
      CSA_DCHECK(this, IsPropertyDictionary(CAST(properties)));
      // TODO(pthier): Support swiss dictionaries.
      if constexpr (!V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
        TNode<Smi> flags =
            GetNameDictionaryFlags<NameDictionary>(CAST(properties));
        GotoIf(IsSetSmi(flags,
                        NameDictionary::MayHaveInterestingPropertiesBit::kMask),
               &lookup);
        *var_holder = LoadMapPrototype(holder_map);
        *var_holder_map = LoadMap((*var_holder).value());
      }
      Goto(&loop);
    }
  }

  BIND(&lookup);
  return CallBuiltin(Builtin::kGetPropertyWithReceiver, context,
                     (*var_holder).value(), name, receiver,
                     SmiConstant(OnNonExistent::kReturnUndefined));
}

void CodeStubAssembler::TryLookupElement(
    TNode<HeapObject> object, TNode<Map> map, TNode<Int32T> instance_type,
    TNode<IntPtrT> intptr_index, Label* if_found, Label* if_absent,
    Label* if_not_found, Label* if_bailout) {
  // Handle special objects in runtime.
  GotoIf(IsSpecialReceiverInstanceType(instance_type), if_bailout);

  TNode<Int32T> elements_kind = LoadMapElementsKind(map);

  // TODO(verwaest): Support other elements kinds as well.
  Label if_isobjectorsmi(this), if_isdouble(this), if_isdictionary(this),
      if_isfaststringwrapper(this), if_isslowstringwrapper(this), if_oob(this),
      if_typedarray(this), if_rab_gsab_typedarray(this);
  // clang-format off
  int32_t values[] = {
      // Handled by {if_isobjectorsmi}.
      PACKED_SMI_ELEMENTS, HOLEY_SMI_ELEMENTS, PACKED_ELEMENTS, HOLEY_ELEMENTS,
      PACKED_NONEXTENSIBLE_ELEMENTS, PACKED_SEALED_ELEMENTS,
      HOLEY_NONEXTENSIBLE_ELEMENTS, HOLEY_SEALED_ELEMENTS,
      PACKED_FROZEN_ELEMENTS, HOLEY_FROZEN_ELEMENTS,
      // Handled by {if_isdouble}.
      PACKED_DOUBLE_ELEMENTS, HOLEY_DOUBLE_ELEMENTS,
      // Handled by {if_isdictionary}.
      DICTIONARY_ELEMENTS,
      // Handled by {if_isfaststringwrapper}.
      FAST_STRING_WRAPPER_ELEMENTS,
      // Handled by {if_isslowstringwrapper}.
      SLOW_STRING_WRAPPER_ELEMENTS,
      // Handled by {if_not_found}.
      NO_ELEMENTS,
      // Handled by {if_typed_array}.
      UINT8_ELEMENTS,
      INT8_ELEMENTS,
      UINT16_ELEMENTS,
      INT16_ELEMENTS,
      UINT32_ELEMENTS,
      INT32_ELEMENTS,
      FLOAT32_ELEMENTS,
      FLOAT64_ELEMENTS,
      UINT8_CLAMPED_ELEMENTS,
      BIGUINT64_ELEMENTS,
      BIGINT64_ELEMENTS,
      RAB_GSAB_UINT8_ELEMENTS,
      RAB_GSAB_INT8_ELEMENTS,
      RAB_GSAB_UINT16_ELEMENTS,
      RAB_GSAB_INT16_ELEMENTS,
      RAB_GSAB_UINT32_ELEMENTS,
      RAB_GSAB_INT32_ELEMENTS,
      RAB_GSAB_FLOAT32_ELEMENTS,
      RAB_GSAB_FLOAT64_ELEMENTS,
      RAB_GSAB_UINT8_CLAMPED_ELEMENTS,
      RAB_GSAB_BIGUINT64_ELEMENTS,
      RAB_GSAB_BIGINT64_ELEMENTS,
  };
  Label* labels[] = {
      &if_isobjectorsmi, &if_isobjectorsmi, &if_isobjectorsmi,
      &if_isobjectorsmi, &if_isobjectorsmi, &if_isobjectorsmi,
      &if_isobjectorsmi, &if_isobjectorsmi, &if_isobjectorsmi,
      &if_isobjectorsmi,
      &if_isdouble, &if_isdouble,
      &if_isdictionary,
      &if_isfaststringwrapper,
      &if_isslowstringwrapper,
      if_not_found,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
      &if_rab_gsab_typedarray,
  };
  // clang-format on
  static_assert(arraysize(values) == arraysize(labels));
  Switch(elements_kind, if_bailout, values, labels, arraysize(values));

  BIND(&if_isobjectorsmi);
  {
    TNode<FixedArray> elements = CAST(LoadElements(CAST(object)));
    TNode<IntPtrT> length = LoadAndUntagFixedArrayBaseLength(elements);

    GotoIfNot(UintPtrLessThan(intptr_index, length), &if_oob);

    TNode<Object> element = UnsafeLoadFixedArrayElement(elements, intptr_index);
    TNode<Hole> the_hole = TheHoleConstant();
    Branch(TaggedEqual(element, the_hole), if_not_found, if_found);
  }
  BIND(&if_isdouble);
  {
    TNode<FixedArrayBase> elements = LoadElements(CAST(object));
    TNode<IntPtrT> length = LoadAndUntagFixedArrayBaseLength(elements);

    GotoIfNot(UintPtrLessThan(intptr_index, length), &if_oob);

    // Check if the element is a double hole, but don't load it.
    LoadFixedDoubleArrayElement(CAST(elements), intptr_index, if_not_found,
                                MachineType::None());
    Goto(if_found);
  }
  BIND(&if_isdictionary);
  {
    // Negative and too-large keys must be converted to property names.
    if (Is64()) {
      GotoIf(UintPtrLessThan(IntPtrConstant(JSObject::kMaxElementIndex),
                             intptr_index),
             if_bailout);
    } else {
      GotoIf(IntPtrLessThan(intptr_index, IntPtrConstant(0)), if_bailout);
    }

    TVARIABLE(IntPtrT, var_entry);
    TNode<NumberDictionary> elements = CAST(LoadElements(CAST(object)));
    NumberDictionaryLookup(elements, intptr_index, if_found, &var_entry,
                           if_not_found);
  }
  BIND(&if_isfaststringwrapper);
  {
    TNode<String> string = CAST(LoadJSPrimitiveWrapperValue(CAST(object)));
    TNode<IntPtrT> length = LoadStringLengthAsWord(string);
    GotoIf(UintPtrLessThan(intptr_index, length), if_found);
    Goto(&if_isobjectorsmi);
  }
  BIND(&if_isslowstringwrapper);
  {
    TNode<String> string = CAST(LoadJSPrimitiveWrapperValue(CAST(object)));
    TNode<IntPtrT> length = LoadStringLengthAsWord(string);
    GotoIf(UintPtrLessThan(intptr_index, length), if_found);
    Goto(&if_isdictionary);
  }
  BIND(&if_typedarray);
  {
    TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(CAST(object));
    GotoIf(IsDetachedBuffer(buffer), if_absent);

    TNode<UintPtrT> length = LoadJSTypedArrayLength(CAST(object));
    Branch(UintPtrLessThan(intptr_index, length), if_found, if_absent);
  }
  BIND(&if_rab_gsab_typedarray);
  {
    TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(CAST(object));
    TNode<UintPtrT> length =
        LoadVariableLengthJSTypedArrayLength(CAST(object), buffer, if_absent);
    Branch(UintPtrLessThan(intptr_index, length), if_found, if_absent);
  }
  BIND(&if_oob);
  {
    // Positive OOB indices mean "not found", negative indices and indices
    // out of array index range must be converted to property names.
    if (Is64()) {
      GotoIf(UintPtrLessThan(IntPtrConstant(JSObject::kMaxElementIndex),
                             intptr_index),
             if_bailout);
    } else {
      GotoIf(IntPtrLessThan(intptr_index, IntPtrConstant(0)), if_bailout);
    }
    Goto(if_not_found);
  }
}

void CodeStubAssembler::BranchIfMaybeSpecialIndex(TNode<String> name_string,
                                                  Label* if_maybe_special_index,
                                                  Label* if_not_special_index) {
  // TODO(cwhan.tunz): Implement fast cases more.

  // If a name is empty or too long, it's not a special index
  // Max length of canonical double: -X.XXXXXXXXXXXXXXXXX-eXXX
  const int kBufferSize = 24;
  TNode<Smi> string_length = LoadStringLengthAsSmi(name_string);
  GotoIf(SmiEqual(string_length, SmiConstant(0)), if_not_special_index);
  GotoIf(SmiGreaterThan(string_length, SmiConstant(kBufferSize)),
         if_not_special_index);

  // If the first character of name is not a digit or '-', or we can't match it
  // to Infinity or NaN, then this is not a special index.
  TNode<Int32T> first_char = StringCharCodeAt(name_string, UintPtrConstant(0));
  // If the name starts with '-', it can be a negative index.
  GotoIf(Word32Equal(first_char, Int32Constant('-')), if_maybe_special_index);
  // If the name starts with 'I', it can be "Infinity".
  GotoIf(Word32Equal(first_char, Int32Constant('I')), if_maybe_special_index);
  // If the name starts with 'N', it can be "NaN".
  GotoIf(Word32Equal(first_char, Int32Constant('N')), if_maybe_special_index);
  // Finally, if the first character is not a digit either, then we are sure
  // that the name is not a special index.
  GotoIf(Uint32LessThan(first_char, Int32Constant('0')), if_not_special_index);
  GotoIf(Uint32LessThan(Int32Constant('9'), first_char), if_not_special_index);
  Goto(if_maybe_special_index);
}

void CodeStubAssembler::TryPrototypeChainLookup(
    TNode<Object> receiver, TNode<Object> object_arg, TNode<Object> key,
    const LookupPropertyInHolder& lookup_property_in_holder,
    const LookupElementInHolder& lookup_element_in_holder, Label* if_end,
    Label* if_bailout, Label* if_proxy, bool handle_private_names) {
  // Ensure receiver is JSReceiver, otherwise bailout.
  GotoIf(TaggedIsSmi(receiver), if_bailout);
  TNode<HeapObject> object = CAST(object_arg);

  TNode<Map> map = LoadMap(object);
  TNode<Uint16T> instance_type = LoadMapInstanceType(map);
  {
    Label if_objectisreceiver(this);
    Branch(IsJSReceiverInstanceType(instance_type), &if_objectisreceiver,
           if_bailout);
    BIND(&if_objectisreceiver);

    GotoIf(InstanceTypeEqual(instance_type, JS_PROXY_TYPE), if_proxy);
  }

  TVARIABLE(IntPtrT, var_index);
  TVARIABLE(Name, var_unique);

  Label if_keyisindex(this), if_iskeyunique(this);
  TryToName(key, &if_keyisindex, &var_index, &if_iskeyunique, &var_unique,
            if_bailout);

  BIND(&if_iskeyunique);
  {
    TVARIABLE(HeapObject, var_holder, object);
    TVARIABLE(Map, var_holder_map, map);
    TVARIABLE(Int32T, var_holder_instance_type, instance_type);

    Label loop(this, {&var_holder, &var_holder_map, &var_holder_instance_type});
    Goto(&loop);
    BIND(&loop);
    {
      TNode<Map> holder_map = var_holder_map.value();
      TNode<Int32T> holder_instance_type = var_holder_instance_type.value();

      Label next_proto(this), check_integer_indexed_exotic(this);
      lookup_property_in_holder(CAST(receiver), var_holder.value(), holder_map,
                                holder_instance_type, var_unique.value(),
                                &check_integer_indexed_exotic, if_bailout);

      BIND(&check_integer_indexed_exotic);
      {
        // Bailout if it can be an integer indexed exotic case.
        GotoIfNot(InstanceTypeEqual(holder_instance_type, JS_TYPED_ARRAY_TYPE),
                  &next_proto);
        GotoIfNot(IsString(var_unique.value()), &next_proto);
        BranchIfMaybeSpecialIndex(CAST(var_unique.value()), if_bailout,
                                  &next_proto);
      }

      BIND(&next_proto);

      if (handle_private_names) {
        // Private name lookup doesn't walk the prototype chain.
        GotoIf(IsPrivateSymbol(CAST(key)), if_end);
      }

      TNode<HeapObject> proto = LoadMapPrototype(holder_map);

      GotoIf(IsNull(proto), if_end);

      TNode<Map> proto_map = LoadMap(proto);
      TNode<Uint16T> proto_instance_type = LoadMapInstanceType(proto_map);

      var_holder = proto;
      var_holder_map = proto_map;
      var_holder_instance_type = proto_instance_type;
      Goto(&loop);
    }
  }
  BIND(&if_keyisindex);
  {
    TVARIABLE(HeapObject, var_holder, object);
    TVARIABLE(Map, var_holder_map, map);
    TVARIABLE(Int32T, var_holder_instance_type, instance_type);

    Label loop(this, {&var_holder, &var_holder_map, &var_holder_instance_type});
    Goto(&loop);
    BIND(&loop);
    {
      Label next_proto(this);
      lookup_element_in_holder(CAST(receiver), var_holder.value(),
                               var_holder_map.value(),
                               var_holder_instance_type.value(),
                               var_index.value(), &next_proto, if_bailout);
      BIND(&next_proto);

      TNode<HeapObject> proto = LoadMapPrototype(var_holder_map.value());

      GotoIf(IsNull(proto), if_end);

      TNode<Map> proto_map = LoadMap(proto);
      TNode<Uint16T> proto_instance_type = LoadMapInstanceType(proto_map);

      var_holder = proto;
      var_holder_map = proto_map;
      var_holder_instance_type = proto_instance_type;
      Goto(&loop);
    }
  }
}

TNode<Boolean> CodeStubAssembler::HasInPrototypeChain(TNode<Context> context,
                                                      TNode<HeapObject> object,
                                                      TNode<Object> prototype) {
  TVARIABLE(Boolean, var_result);
  Label return_false(this), return_true(this),
      return_runtime(this, Label::kDeferred), return_result(this);

  // Loop through the prototype chain looking for the {prototype}.
  TVARIABLE(Map, var_object_map, LoadMap(object));
  Label loop(this, &var_object_map);
  Goto(&loop);
  BIND(&loop);
  {
    // Check if we can determine the prototype directly from the {object_map}.
    Label if_objectisdirect(this), if_objectisspecial(this, Label::kDeferred);
    TNode<Map> object_map = var_object_map.value();
    TNode<Uint16T> object_instance_type = LoadMapInstanceType(object_map);
    Branch(IsSpecialReceiverInstanceType(object_instance_type),
           &if_objectisspecial, &if_objectisdirect);
    BIND(&if_objectisspecial);
    {
      // The {object_map} is a special receiver map or a primitive map, check
      // if we need to use the if_objectisspecial path in the runtime.
      GotoIf(InstanceTypeEqual(object_instance_type, JS_PROXY_TYPE),
             &return_runtime);
      TNode<Int32T> object_bitfield = LoadMapBitField(object_map);
      int mask = Map::Bits1::HasNamedInterceptorBit::kMask |
                 Map::Bits1::IsAccessCheckNeededBit::kMask;
      Branch(IsSetWord32(object_bitfield, mask), &return_runtime,
             &if_objectisdirect);
    }
    BIND(&if_objectisdirect);

    // Check the current {object} prototype.
    TNode<HeapObject> object_prototype = LoadMapPrototype(object_map);
    GotoIf(IsNull(object_prototype), &return_false);
    GotoIf(TaggedEqual(object_prototype, prototype), &return_true);

    // Continue with the prototype.
    CSA_DCHECK(this, TaggedIsNotSmi(object_prototype));
    var_object_map = LoadMap(object_prototype);
    Goto(&loop);
  }

  BIND(&return_true);
  var_result = TrueConstant();
  Goto(&return_result);

  BIND(&return_false);
  var_result = FalseConstant();
  Goto(&return_result);

  BIND(&return_runtime);
  {
    // Fallback to the runtime implementation.
    var_result = CAST(
        CallRuntime(Runtime::kHasInPrototypeChain, context, object, prototype));
  }
  Goto(&return_result);

  BIND(&return_result);
  return var_result.value();
}

TNode<Boolean> CodeStubAssembler::OrdinaryHasInstance(
    TNode<Context> context, TNode<Object> callable_maybe_smi,
    TNode<Object> object_maybe_smi) {
  TVARIABLE(Boolean, var_result);
  Label return_runtime(this, Label::kDeferred), return_result(this);

  GotoIfForceSlowPath(&return_runtime);

  // Goto runtime if {object} is a Smi.
  GotoIf(TaggedIsSmi(object_maybe_smi), &return_runtime);

  // Goto runtime if {callable} is a Smi.
  GotoIf(TaggedIsSmi(callable_maybe_smi), &return_runtime);

  {
    // Load map of {callable}.
    TNode<HeapObject> object = CAST(object_maybe_smi);
    TNode<HeapObject> callable = CAST(callable_maybe_smi);
    TNode<Map> callable_map = LoadMap(callable);

    // Goto runtime if {callable} is not a JSFunction.
    TNode<Uint16T> callable_instance_type = LoadMapInstanceType(callable_map);
    GotoIfNot(IsJSFunctionInstanceType(callable_instance_type),
              &return_runtime);

    GotoIfPrototypeRequiresRuntimeLookup(CAST(callable), callable_map,
                                         &return_runtime);

    // Get the "prototype" (or initial map) of the {callable}.
    TNode<HeapObject> callable_prototype = LoadObjectField<HeapObject>(
        callable, JSFunction::kPrototypeOrInitialMapOffset);
    {
      Label no_initial_map(this), walk_prototype_chain(this);
      TVARIABLE(HeapObject, var_callable_prototype, callable_prototype);

      // Resolve the "prototype" if the {callable} has an initial map.
      GotoIfNot(IsMap(callable_prototype), &no_initial_map);
      var_callable_prototype = LoadObjectField<HeapObject>(
          callable_prototype, Map::kPrototypeOffset);
      Goto(&walk_prototype_chain);

      BIND(&no_initial_map);
      // {callable_prototype} is the hole if the "prototype" property hasn't
      // been requested so far.
      Branch(TaggedEqual(callable_prototype, TheHoleConstant()),
             &return_runtime, &walk_prototype_chain);

      BIND(&walk_prototype_chain);
      callable_prototype = var_callable_prototype.value();
    }

    // Loop through the prototype chain looking for the {callable} prototype.
    var_result = HasInPrototypeChain(context, object, callable_prototype);
    Goto(&return_result);
  }

  BIND(&return_runtime);
  {
    // Fallback to the runtime implementation.
    var_result = CAST(CallRuntime(Runtime::kOrdinaryHasInstance, context,
                                  callable_maybe_smi, object_maybe_smi));
  }
  Goto(&return_result);

  BIND(&return_result);
  return var_result.value();
}

template <typename TIndex>
TNode<IntPtrT> CodeStubAssembler::ElementOffsetFromIndex(
    TNode<TIndex> index_node, ElementsKind kind, int base_size) {
  // TODO(v8:9708): Remove IntPtrT variant in favor of UintPtrT.
  static_assert(std::is_same<TIndex, Smi>::value ||
                    std::is_same<TIndex, TaggedIndex>::value ||
                    std::is_same<TIndex, IntPtrT>::value ||
                    std::is_same<TIndex, UintPtrT>::value,
                "Only Smi, UintPtrT or IntPtrT index nodes are allowed");
  int element_size_shift = ElementsKindToShiftSize(kind);
  int element_size = 1 << element_size_shift;
  intptr_t index = 0;
  TNode<IntPtrT> intptr_index_node;
  bool constant_index = false;
  if (std::is_same<TIndex, Smi>::value) {
    TNode<Smi> smi_index_node = ReinterpretCast<Smi>(index_node);
    int const kSmiShiftBits = kSmiShiftSize + kSmiTagSize;
    element_size_shift -= kSmiShiftBits;
    Tagged<Smi> smi_index;
    constant_index = TryToSmiConstant(smi_index_node, &smi_index);
    if (constant_index) {
      index = smi_index.value();
    } else {
      if (COMPRESS_POINTERS_BOOL) {
        smi_index_node = NormalizeSmiIndex(smi_index_node);
      }
    }
    intptr_index_node = BitcastTaggedToWordForTagAndSmiBits(smi_index_node);
  } else if (std::is_same<TIndex, TaggedIndex>::value) {
    TNode<TaggedIndex> tagged_index_node =
        ReinterpretCast<TaggedIndex>(index_node);
    element_size_shift -= kSmiTagSize;
    intptr_index_node = BitcastTaggedToWordForTagAndSmiBits(tagged_index_node);
    constant_index = TryToIntPtrConstant(intptr_index_node, &index);
  } else {
    intptr_index_node = ReinterpretCast<IntPtrT>(index_node);
    constant_index = TryToIntPtrConstant(intptr_index_node, &index);
  }
  if (constant_index) {
    return IntPtrConstant(base_size + element_size * index);
  }

  TNode<IntPtrT> shifted_index =
      (element_size_shift == 0)
          ? intptr_index_node
          : ((element_size_shift > 0)
                 ? WordShl(intptr_index_node,
                           IntPtrConstant(element_size_shift))
                 : WordSar(intptr_index_node,
                           IntPtrConstant(-element_size_shift)));
  return IntPtrAdd(IntPtrConstant(base_size), Signed(shifted_index));
}

// Instantiate ElementOffsetFromIndex for Smi and IntPtrT.
template V8_EXPORT_PRIVATE TNode<IntPtrT>
CodeStubAssembler::ElementOffsetFromIndex<Smi>(TNode<Smi> index_node,
                                               ElementsKind kind,
                                               int base_size);
template V8_EXPORT_PRIVATE TNode<IntPtrT>
CodeStubAssembler::ElementOffsetFromIndex<TaggedIndex>(
    TNode<TaggedIndex> index_node, ElementsKind kind, int base_size);
template V8_EXPORT_PRIVATE TNode<IntPtrT>
CodeStubAssembler::ElementOffsetFromIndex<IntPtrT>(TNode<IntPtrT> index_node,
                                                   ElementsKind kind,
                                                   int base_size);

TNode<BoolT> CodeStubAssembler::IsOffsetInBounds(TNode<IntPtrT> offset,
                                                 TNode<IntPtrT> length,
                                                 int header_size,
                                                 ElementsKind kind) {
  // Make sure we point to the last field.
  int element_size = 1 << ElementsKindToShiftSize(kind);
  int correction = header_size - kHeapObjectTag - element_size;
  TNode<IntPtrT> last_offset = ElementOffsetFromIndex(length, kind, correction);
  return IntPtrLessThanOrEqual(offset, last_offset);
}

TNode<HeapObject> CodeStubAssembler::LoadFeedbackCellValue(
    TNode<JSFunction> closure) {
  TNode<FeedbackCell> feedback_cell =
      LoadObjectField<FeedbackCell>(closure, JSFunction::kFeedbackCellOffset);
  return LoadObjectField<HeapObject>(feedback_cell, FeedbackCell::kValueOffset);
}

TNode<HeapObject> CodeStubAssembler::LoadFeedbackVector(
    TNode<JSFunction> closure) {
  TVARIABLE(HeapObject, maybe_vector);
  Label if_no_feedback_vector(this), out(this);

  maybe_vector = LoadFeedbackVector(closure, &if_no_feedback_vector);
  Goto(&out);

  BIND(&if_no_feedback_vector);
  // If the closure doesn't have a feedback vector allocated yet, return
  // undefined. The FeedbackCell can contain Undefined / FixedArray (for lazy
  // allocations) / FeedbackVector.
  maybe_vector = UndefinedConstant();
  Goto(&out);

  BIND(&out);
  return maybe_vector.value();
}

TNode<FeedbackVector> CodeStubAssembler::LoadFeedbackVector(
    TNode<JSFunction> closure, Label* if_no_feedback_vector) {
  TNode<HeapObject> maybe_vector = LoadFeedbackCellValue(closure);
  GotoIfNot(IsFeedbackVector(maybe_vector), if_no_feedback_vector);
  return CAST(maybe_vector);
}

TNode<ClosureFeedbackCellArray> CodeStubAssembler::LoadClosureFeedbackArray(
    TNode<JSFunction> closure) {
  TVARIABLE(HeapObject, feedback_cell_array, LoadFeedbackCellValue(closure));
  Label end(this);

  // When feedback vectors are not yet allocated feedback cell contains a
  // an array of feedback cells used by create closures.
  GotoIf(HasInstanceType(feedback_cell_array.value(),
                         CLOSURE_FEEDBACK_CELL_ARRAY_TYPE),
         &end);

  // Load FeedbackCellArray from feedback vector.
  TNode<FeedbackVector> vector = CAST(feedback_cell_array.value());
  feedback_cell_array = CAST(
      LoadObjectField(vector, FeedbackVector::kClosureFeedbackCellArrayOffset));
  Goto(&end);

  BIND(&end);
  return CAST(feedback_cell_array.value());
}

TNode<FeedbackVector> CodeStubAssembler::LoadFeedbackVectorForStub() {
  TNode<JSFunction> function =
      CAST(LoadFromParentFrame(StandardFrameConstants::kFunctionOffset));
  return CAST(LoadFeedbackVector(function));
}

TNode<FeedbackVector> CodeStubAssembler::LoadFeedbackVectorFromBaseline() {
  return CAST(
      LoadFromParentFrame(BaselineFrameConstants::kFeedbackVectorFromFp));
}

TNode<Context> CodeStubAssembler::LoadContextFromBaseline() {
  return CAST(LoadFromParentFrame(InterpreterFrameConstants::kContextOffset));
}

TNode<FeedbackVector>
CodeStubAssembler::LoadFeedbackVectorForStubWithTrampoline() {
  TNode<RawPtrT> frame_pointer = LoadParentFramePointer();
  TNode<RawPtrT> parent_frame_pointer = Load<RawPtrT>(frame_pointer);
  TNode<JSFunction> function = CAST(
      LoadFullTagged(parent_frame_pointer,
                     IntPtrConstant(StandardFrameConstants::kFunctionOffset)));
  return CAST(LoadFeedbackVector(function));
}

void CodeStubAssembler::UpdateFeedback(TNode<Smi> feedback,
                                       TNode<HeapObject> maybe_feedback_vector,
                                       TNode<UintPtrT> slot_id,
                                       UpdateFeedbackMode mode) {
  switch (mode) {
    case UpdateFeedbackMode::kOptionalFeedback:
      MaybeUpdateFeedback(feedback, maybe_feedback_vector, slot_id);
      break;
    case UpdateFeedbackMode::kGuaranteedFeedback:
      CSA_DCHECK(this, IsFeedbackVector(maybe_feedback_vector));
      UpdateFeedback(feedback, CAST(maybe_feedback_vector), slot_id);
      break;
    case UpdateFeedbackMode::kNoFeedback:
#ifdef V8_JITLESS
      CSA_DCHECK(this, IsUndefined(maybe_feedback_vector));
      break;
#else
      UNREACHABLE();
#endif  // !V8_JITLESS
  }
}

void CodeStubAssembler::MaybeUpdateFeedback(TNode<Smi> feedback,
                                            TNode<HeapObject> maybe_vector,
                                            TNode<UintPtrT> slot_id) {
  Label end(this);
  GotoIf(IsUndefined(maybe_vector), &end);
  {
    UpdateFeedback(feedback, CAST(maybe_vector), slot_id);
    Goto(&end);
  }
  BIND(&end);
}

void CodeStubAssembler::UpdateFeedback(TNode<Smi> feedback,
                                       TNode<FeedbackVector> feedback_vector,
                                       TNode<UintPtrT> slot_id) {
  Label end(this);

  // This method is used for binary op and compare feedback. These
  // vector nodes are initialized with a smi 0, so we can simply OR
  // our new feedback in place.
  TNode<MaybeObject> feedback_element =
      LoadFeedbackVectorSlot(feedback_vector, slot_id);
  TNode<Smi> previous_feedback = CAST(feedback_element);
  TNode<Smi> combined_feedback = SmiOr(previous_feedback, feedback);

  GotoIf(SmiEqual(previous_feedback, combined_feedback), &end);
  {
    StoreFeedbackVectorSlot(feedback_vector, slot_id, combined_feedback,
                            SKIP_WRITE_BARRIER);
    ReportFeedbackUpdate(feedback_vector, slot_id, "UpdateFeedback");
    Goto(&end);
  }

  BIND(&end);
}

void CodeStubAssembler::ReportFeedbackUpdate(
    TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot_id,
    const char* reason) {
#ifdef V8_TRACE_FEEDBACK_UPDATES
  // Trace the update.
  CallRuntime(Runtime::kTraceUpdateFeedback, NoContextConstant(),
              feedback_vector, SmiTag(Signed(slot_id)), StringConstant(reason));
#endif  // V8_TRACE_FEEDBACK_UPDATES
}

void CodeStubAssembler::OverwriteFeedback(TVariable<Smi>* existing_feedback,
                                          int new_feedback) {
  if (existing_feedback == nullptr) return;
  *existing_feedback = SmiConstant(new_feedback);
}

void CodeStubAssembler::CombineFeedback(TVariable<Smi>* existing_feedback,
                                        int feedback) {
  if (existing_feedback == nullptr) return;
  *existing_feedback = SmiOr(existing_feedback->value(), SmiConstant(feedback));
}

void CodeStubAssembler::CombineFeedback(TVariable<Smi>* existing_feedback,
                                        TNode<Smi> feedback) {
  if (existing_feedback == nullptr) return;
  *existing_feedback = SmiOr(existing_feedback->value(), feedback);
}

void CodeStubAssembler::CheckForAssociatedProtector(TNode<Name> name,
                                                    Label* if_protector) {
  // This list must be kept in sync with LookupIterator::UpdateProtector!
  auto first_ptr = Unsigned(
      BitcastTaggedToWord(LoadRoot(RootIndex::kFirstNameForProtector)));
  auto last_ptr =
      Unsigned(BitcastTaggedToWord(LoadRoot(RootIndex::kLastNameForProtector)));
  auto name_ptr = Unsigned(BitcastTaggedToWord(name));
  GotoIf(IsInRange(name_ptr, first_ptr, last_ptr), if_protector);
}

void CodeStubAssembler::DCheckReceiver(ConvertReceiverMode mode,
                                       TNode<Object> receiver) {
  switch (mode) {
    case ConvertReceiverMode::kNullOrUndefined:
      CSA_DCHECK(this, IsNullOrUndefined(receiver));
      break;
    case ConvertReceiverMode::kNotNullOrUndefined:
      CSA_DCHECK(this, Word32BinaryNot(IsNullOrUndefined(receiver)));
      break;
    case ConvertReceiverMode::kAny:
      break;
  }
}

TNode<Map> CodeStubAssembler::LoadReceiverMap(TNode<Object> receiver) {
  TVARIABLE(Map, value);
  Label vtrue(this, Label::kDeferred), vfalse(this), end(this);
  Branch(TaggedIsSmi(receiver), &vtrue, &vfalse);

  BIND(&vtrue);
  {
    value = HeapNumberMapConstant();
    Goto(&end);
  }
  BIND(&vfalse);
  {
    value = LoadMap(UncheckedCast<HeapObject>(receiver));
    Goto(&end);
  }

  BIND(&end);
  return value.value();
}

TNode<IntPtrT> CodeStubAssembler::TryToIntptr(
    TNode<Object> key, Label* if_not_intptr,
    TVariable<Int32T>* var_instance_type) {
  TVARIABLE(IntPtrT, var_intptr_key);
  Label done(this, &var_intptr_key), key_is_smi(this), key_is_heapnumber(this);
  GotoIf(TaggedIsSmi(key), &key_is_smi);

  TNode<Int32T> instance_type = LoadInstanceType(CAST(key));
  if (var_instance_type != nullptr) {
    *var_instance_type = instance_type;
  }

  Branch(IsHeapNumberInstanceType(instance_type), &key_is_heapnumber,
         if_not_intptr);

  BIND(&key_is_smi);
  {
    var_intptr_key = SmiUntag(CAST(key));
    Goto(&done);
  }

  BIND(&key_is_heapnumber);
  {
    TNode<Float64T> value = LoadHeapNumberValue(CAST(key));
#if V8_TARGET_ARCH_64_BIT
    TNode<IntPtrT> int_value =
        TNode<IntPtrT>::UncheckedCast(TruncateFloat64ToInt64(value));
#else
    TNode<IntPtrT> int_value =
        TNode<IntPtrT>::UncheckedCast(RoundFloat64ToInt32(value));
#endif
    GotoIfNot(Float64Equal(value, RoundIntPtrToFloat64(int_value)),
              if_not_intptr);
#if V8_TARGET_ARCH_64_BIT
    // We can't rely on Is64() alone because 32-bit compilers rightly complain
    // about kMaxSafeIntegerUint64 not fitting into an intptr_t.
    DCHECK(Is64());
    // TODO(jkummerow): Investigate whether we can drop support for
    // negative indices.
    GotoIfNot(IsInRange(int_value, static_cast<intptr_t>(-kMaxSafeInteger),
                        static_cast<intptr_t>(kMaxSafeIntegerUint64)),
              if_not_intptr);
#else
    DCHECK(!Is64());
#endif
    var_intptr_key = int_value;
    Goto(&done);
  }

  BIND(&done);
  return var_intptr_key.value();
}

TNode<Context> CodeStubAssembler::LoadScriptContext(
    TNode<Context> context, TNode<IntPtrT> context_index) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<ScriptContextTable> script_context_table = CAST(
      LoadContextElement(native_context, Context::SCRIPT_CONTEXT_TABLE_INDEX));
  return LoadArrayElement(script_context_table, context_index);
}

namespace {

// Converts typed array elements kind to a machine representations.
MachineRepresentation ElementsKindToMachineRepresentation(ElementsKind kind) {
  switch (kind) {
    case UINT8_CLAMPED_ELEMENTS:
    case UINT8_ELEMENTS:
    case INT8_ELEMENTS:
      return MachineRepresentation::kWord8;
    case UINT16_ELEMENTS:
    case INT16_ELEMENTS:
    case FLOAT16_ELEMENTS:
      return MachineRepresentation::kWord16;
    case UINT32_ELEMENTS:
    case INT32_ELEMENTS:
      return MachineRepresentation::kWord32;
    case FLOAT32_ELEMENTS:
      return MachineRepresentation::kFloat32;
    case FLOAT64_ELEMENTS:
      return MachineRepresentation::kFloat64;
    default:
      UNREACHABLE();
  }
}

}  // namespace

// TODO(solanes): Since we can't use `if constexpr` until we enable C++17 we
// have to specialize the BigInt and Word32T cases. Since we can't partly
// specialize, we have to specialize all used combinations.
template <typename TIndex>
void CodeStubAssembler::StoreElementTypedArrayBigInt(TNode<RawPtrT> elements,
                                                     ElementsKind kind,
                                                     TNode<TIndex> index,
                                                     TNode<BigInt> value) {
  static_assert(std::is_same<TIndex, UintPtrT>::value ||
                    std::is_same<TIndex, IntPtrT>::value,
                "Only UintPtrT or IntPtrT indices is allowed");
  DCHECK(kind == BIGINT64_ELEMENTS || kind == BIGUINT64_ELEMENTS);
  TNode<IntPtrT> offset = ElementOffsetFromIndex(index, kind, 0);
  TVARIABLE(UintPtrT, var_low);
  // Only used on 32-bit platforms.
  TVARIABLE(UintPtrT, var_high);
  BigIntToRawBytes(value, &var_low, &var_high);

  MachineRepresentation rep = WordT::kMachineRepresentation;
#if defined(V8_TARGET_BIG_ENDIAN)
    if (!Is64()) {
      StoreNoWriteBarrier(rep, elements, offset, var_high.value());
      StoreNoWriteBarrier(rep, elements,
                          IntPtrAdd(offset, IntPtrConstant(kSystemPointerSize)),
                          var_low.value());
    } else {
      StoreNoWriteBarrier(rep, elements, offset, var_low.value());
    }
#else
    StoreNoWriteBarrier(rep, elements, offset, var_low.value());
    if (!Is64()) {
      StoreNoWriteBarrier(rep, elements,
                          IntPtrAdd(offset, IntPtrConstant(kSystemPointerSize)),
                          var_high.value());
    }
#endif
}

template <>
void CodeStubAssembler::StoreElementTypedArray(TNode<RawPtrT> elements,
                                               ElementsKind kind,
                                               TNode<UintPtrT> index,
                                               TNode<BigInt> value) {
  StoreElementTypedArrayBigInt(elements, kind, index, value);
}

template <>
void CodeStubAssembler::StoreElementTypedArray(TNode<RawPtrT> elements,
                                               ElementsKind kind,
                                               TNode<IntPtrT> index,
                                               TNode<BigInt> value) {
  StoreElementTypedArrayBigInt(elements, kind, index, value);
}

template <typename TIndex>
void CodeStubAssembler::StoreElementTypedArrayWord32(TNode<RawPtrT> elements,
                                                     ElementsKind kind,
                                                     TNode<TIndex> index,
                                                     TNode<Word32T> value) {
  static_assert(std::is_same<TIndex, UintPtrT>::value ||
                    std::is_same<TIndex, IntPtrT>::value,
                "Only UintPtrT or IntPtrT indices is allowed");
  DCHECK(IsTypedArrayElementsKind(kind));
  if (kind == UINT8_CLAMPED_ELEMENTS) {
    CSA_DCHECK(this, Word32Equal(value, Word32And(Int32Constant(0xFF), value)));
  }
  TNode<IntPtrT> offset = ElementOffsetFromIndex(index, kind, 0);
  // TODO(cbruni): Add OOB check once typed.
  MachineRepresentation rep = ElementsKindToMachineRepresentation(kind);
  StoreNoWriteBarrier(rep, elements, offset, value);
}

template <>
void CodeStubAssembler::StoreElementTypedArray(TNode<RawPtrT> elements,
                                               ElementsKind kind,
                                               TNode<UintPtrT> index,
                                               TNode<Word32T> value) {
  StoreElementTypedArrayWord32(elements, kind, index, value);
}

template <>
void CodeStubAssembler::StoreElementTypedArray(TNode<RawPtrT> elements,
                                               ElementsKind kind,
                                               TNode<IntPtrT> index,
                                               TNode<Word32T> value) {
  StoreElementTypedArrayWord32(elements, kind, index, value);
}

template <typename TArray, typename TIndex, typename TValue>
void CodeStubAssembler::StoreElementTypedArray(TNode<TArray> elements,
                                               ElementsKind kind,
                                               TNode<TIndex> index,
                                               TNode<TValue> value) {
  // TODO(v8:9708): Do we want to keep both IntPtrT and UintPtrT variants?
  static_assert(std::is_same<TIndex, Smi>::value ||
                    std::is_same<TIndex, UintPtrT>::value ||
                    std::is_same<TIndex, IntPtrT>::value,
                "Only Smi, UintPtrT or IntPtrT indices is allowed");
  static_assert(std::is_same<TArray, RawPtrT>::value ||
                    std::is_same<TArray, FixedArrayBase>::value,
                "Only RawPtrT or FixedArrayBase elements are allowed");
  static_assert(std::is_same<TValue, Float16T>::value ||
                    std::is_same<TValue, Int32T>::value ||
                    std::is_same<TValue, Float32T>::value ||
                    std::is_same<TValue, Float64T>::value ||
                    std::is_same<TValue, Object>::value,
                "Only Int32T, Float32T, Float64T or object value "
                "types are allowed");
  DCHECK(IsTypedArrayElementsKind(kind));
  TNode<IntPtrT> offset = ElementOffsetFromIndex(index, kind, 0);
  // TODO(cbruni): Add OOB check once typed.
  MachineRepresentation rep = ElementsKindToMachineRepresentation(kind);
  StoreNoWriteBarrier(rep, elements, offset, value);
}

template <typename TIndex>
void CodeStubAssembler::StoreElement(TNode<FixedArrayBase> elements,
                                     ElementsKind kind, TNode<TIndex> index,
                                     TNode<Object> value) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT indices are allowed");
  DCHECK(!IsDoubleElementsKind(kind));
  if (IsTypedArrayElementsKind(kind)) {
    StoreElementTypedArray(elements, kind, index, value);
  } else if (IsSmiElementsKind(kind)) {
    TNode<Smi> smi_value = CAST(value);
    StoreFixedArrayElement(CAST(elements), index, smi_value);
  } else {
    StoreFixedArrayElement(CAST(elements), index, value);
  }
}

template <typename TIndex>
void CodeStubAssembler::StoreElement(TNode<FixedArrayBase> elements,
                                     ElementsKind kind, TNode<TIndex> index,
                                     TNode<Float64T> value) {
  static_assert(
      std::is_same<TIndex, Smi>::value || std::is_same<TIndex, IntPtrT>::value,
      "Only Smi or IntPtrT indices are allowed");
  DCHECK(IsDoubleElementsKind(kind));
  StoreFixedDoubleArrayElement(CAST(elements), index, value);
}

template <typename TIndex, typename TValue>
void CodeStubAssembler::StoreElement(TNode<RawPtrT> elements, ElementsKind kind,
                                     TNode<TIndex> index, TNode<TValue> value) {
  static_assert(std::is_same<TIndex, Smi>::value ||
                    std::is_same<TIndex, IntPtrT>::value ||
                    std::is_same<TIndex, UintPtrT>::value,
                "Only Smi, IntPtrT or UintPtrT indices are allowed");
  static_assert(
      std::is_same<TValue, Float16T>::value ||
          std::is_same<TValue, Int32T>::value ||
          std::is_same<TValue, Word32T>::value ||
          std::is_same<TValue, Float32T>::value ||
          std::is_same<TValue, Float64T>::value ||
          std::is_same<TValue, BigInt>::value,
      "Only Int32T, Word32T, Float32T, Float64T or BigInt value types "
      "are allowed");

  DCHECK(IsTypedArrayElementsKind(kind));
  StoreElementTypedArray(elements, kind, index, value);
}
template V8_EXPORT_PRIVATE void CodeStubAssembler::StoreElement(TNode<RawPtrT>,
                                                                ElementsKind,
                                                                TNode<UintPtrT>,
                                                                TNode<Int32T>);
template V8_EXPORT_PRIVATE void CodeStubAssembler::StoreElement(TNode<RawPtrT>,
                                                                ElementsKind,
                                                                TNode<UintPtrT>,
                                                                TNode<Word32T>);
template V8_EXPORT_PRIVATE void CodeStubAssembler::StoreElement(
    TNode<RawPtrT>, ElementsKind, TNode<UintPtrT>, TNode<Float32T>);
template V8_EXPORT_PRIVATE void CodeStubAssembler::StoreElement(
    TNode<RawPtrT>, ElementsKind, TNode<UintPtrT>, TNode<Float64T>);
template V8_EXPORT_PRIVATE void CodeStubAssembler::StoreElement(TNode<RawPtrT>,
                                                                ElementsKind,
                                                                TNode<UintPtrT>,
                                                                TNode<BigInt>);
template V8_EXPORT_PRIVATE void CodeStubAssembler::StoreElement(
    TNode<RawPtrT>, ElementsKind, TNode<UintPtrT>, TNode<Float16T>);

TNode<Uint8T> CodeStubAssembler::Int32ToUint8Clamped(
    TNode<Int32T> int32_value) {
  Label done(this);
  TNode<Int32T> int32_zero = Int32Constant(0);
  TNode<Int32T> int32_255 = Int32Constant(255);
  TVARIABLE(Word32T, var_value, int32_value);
  GotoIf(Uint32LessThanOrEqual(int32_value, int32_255), &done);
  var_value = int32_zero;
  GotoIf(Int32LessThan(int32_value, int32_zero), &done);
  var_value = int32_255;
  Goto(&done);
  BIND(&done);
  return UncheckedCast<Uint8T>(var_value.value());
}

TNode<Uint8T> CodeStubAssembler::Float64ToUint8Clamped(
    TNode<Float64T> float64_value) {
  Label done(this);
  TVARIABLE(Word32T, var_value, Int32Constant(0));
  GotoIf(Float64LessThanOrEqual(float64_value, Float64Constant(0.0)), &done);
  var_value = Int32Constant(255);
  GotoIf(Float64LessThanOrEqual(Float64Constant(255.0), float64_value), &done);
  {
    TNode<Float64T> rounded_value = Float64RoundToEven(float64_value);
    var_value = TruncateFloat64ToWord32(rounded_value);
    Goto(&done);
  }
  BIND(&done);
  return UncheckedCast<Uint8T>(var_value.value());
}

template <>
TNode<Word32T> CodeStubAssembler::PrepareValueForWriteToTypedArray<Word32T>(
    TNode<Object> input, ElementsKind elements_kind, TNode<Context> context) {
  DCHECK(IsTypedArrayElementsKind(elements_kind));

  switch (elements_kind) {
    case UINT8_ELEMENTS:
    case INT8_ELEMENTS:
    case UINT16_ELEMENTS:
    case INT16_ELEMENTS:
    case UINT32_ELEMENTS:
    case INT32_ELEMENTS:
    case UINT8_CLAMPED_ELEMENTS:
      break;
    default:
      UNREACHABLE();
  }

  TVARIABLE(Word32T, var_result);
  TVARIABLE(Object, var_input, input);
  Label done(this, &var_result), if_smi(this), if_heapnumber_or_oddball(this),
      convert(this), loop(this, &var_input);
  Goto(&loop);
  BIND(&loop);
  GotoIf(TaggedIsSmi(var_input.value()), &if_smi);
  // We can handle both HeapNumber and Oddball here, since Oddball has the
  // same layout as the HeapNumber for the HeapNumber::value field. This
  // way we can also properly optimize stores of oddballs to typed arrays.
  TNode<HeapObject> heap_object = CAST(var_input.value());
  GotoIf(IsHeapNumber(heap_object), &if_heapnumber_or_oddball);
  STATIC_ASSERT_FIELD_OFFSETS_EQUAL(offsetof(HeapNumber, value_),
                                    offsetof(Oddball, to_number_raw_));
  Branch(HasInstanceType(heap_object, ODDBALL_TYPE), &if_heapnumber_or_oddball,
         &convert);

  BIND(&if_heapnumber_or_oddball);
  {
    TNode<Float64T> value =
        LoadObjectField<Float64T>(heap_object, offsetof(HeapNumber, value_));
    if (elements_kind == UINT8_CLAMPED_ELEMENTS) {
      var_result = Float64ToUint8Clamped(value);
    } else if (elements_kind == FLOAT16_ELEMENTS) {
      var_result = ReinterpretCast<Word32T>(TruncateFloat64ToFloat16(value));
    } else {
      var_result = TruncateFloat64ToWord32(value);
    }
    Goto(&done);
  }

  BIND(&if_smi);
  {
    TNode<Int32T> value = SmiToInt32(CAST(var_input.value()));
    if (elements_kind == UINT8_CLAMPED_ELEMENTS) {
      var_result = Int32ToUint8Clamped(value);
    } else if (elements_kind == FLOAT16_ELEMENTS) {
      var_result = ReinterpretCast<Word32T>(RoundInt32ToFloat16(value));
    } else {
      var_result = value;
    }
    Goto(&done);
  }

  BIND(&convert);
  {
    var_input = CallBuiltin(Builtin::kNonNumberToNumber, context, input);
    Goto(&loop);
  }

  BIND(&done);
  return var_result.value();
}

template <>
TNode<Float16T> CodeStubAssembler::PrepareValueForWriteToTypedArray<Float16T>(
    TNode<Object> input, ElementsKind elements_kind, TNode<Context> context) {
  DCHECK(IsTypedArrayElementsKind(elements_kind));
  CHECK_EQ(elements_kind, FLOAT16_ELEMENTS);

  TVARIABLE(Float16T, var_result);
  TVARIABLE(Object, var_input, input);
  Label done(this, &var_result), if_smi(this), if_heapnumber_or_oddball(this),
      convert(this), loop(this, &var_input);
  Goto(&loop);
  BIND(&loop);
  GotoIf(TaggedIsSmi(var_input.value()), &if_smi);
  // We can handle both HeapNumber and Oddball here, since Oddball has the
  // same layout as the HeapNumber for the HeapNumber::value field. This
  // way we can also properly optimize stores of oddballs to typed arrays.
  TNode<HeapObject> heap_object = CAST(var_input.value());
  GotoIf(IsHeapNumber(heap_object), &if_heapnumber_or_oddball);
  STATIC_ASSERT_FIELD_OFFSETS_EQUAL(offsetof(HeapNumber, value_),
                                    offsetof(Oddball, to_number_raw_));
  Branch(HasInstanceType(heap_object, ODDBALL_TYPE), &if_heapnumber_or_oddball,
         &convert);

  BIND(&if_heapnumber_or_oddball);
  {
    TNode<Float64T> value =
        LoadObjectField<Float64T>(heap_object, offsetof(HeapNumber, value_));
    var_result = TruncateFloat64ToFloat16(value);
    Goto(&done);
  }

  BIND(&if_smi);
  {
    TNode<Int32T> value = SmiToInt32(CAST(var_input.value()));
    var_result = RoundInt32ToFloat16(value);
    Goto(&done);
  }

  BIND(&convert);
  {
    var_input = CallBuiltin(Builtin::kNonNumberToNumber, context, input);
    Goto(&loop);
  }

  BIND(&done);
  return var_result.value();
}

template <>
TNode<Float32T> CodeStubAssembler::PrepareValueForWriteToTypedArray<Float32T>(
    TNode<Object> input, ElementsKind elements_kind, TNode<Context> context) {
  DCHECK(IsTypedArrayElementsKind(elements_kind));
  CHECK_EQ(elements_kind, FLOAT32_ELEMENTS);

  TVARIABLE(Float32T, var_result);
  TVARIABLE(Object, var_input, input);
  Label done(this, &var_result), if_smi(this), if_heapnumber_or_oddball(this),
      convert(this), loop(this, &var_input);
  Goto(&loop);
  BIND(&loop);
  GotoIf(TaggedIsSmi(var_input.value()), &if_smi);
  // We can handle both HeapNumber and Oddball here, since Oddball has the
  // same layout as the HeapNumber for the HeapNumber::value field. This
  // way we can also properly optimize stores of oddballs to typed arrays.
  TNode<HeapObject> heap_object = CAST(var_input.value());
  GotoIf(IsHeapNumber(heap_object), &if_heapnumber_or_oddball);
  STATIC_ASSERT_FIELD_OFFSETS_EQUAL(offsetof(HeapNumber, value_),
                                    offsetof(Oddball, to_number_raw_));
  Branch(HasInstanceType(heap_object, ODDBALL_TYPE), &if_heapnumber_or_oddball,
         &convert);

  BIND(&if_heapnumber_or_oddball);
  {
    TNode<Float64T> value =
        LoadObjectField<Float64T>(heap_object, offsetof(HeapNumber, value_));
    var_result = TruncateFloat64ToFloat32(value);
    Goto(&done);
  }

  BIND(&if_smi);
  {
    TNode<Int32T> value = SmiToInt32(CAST(var_input.value()));
    var_result = RoundInt32ToFloat32(value);
    Goto(&done);
  }

  BIND(&convert);
  {
    var_input = CallBuiltin(Builtin::kNonNumberToNumber, context, input);
    Goto(&loop);
  }

  BIND(&done);
  return var_result.value();
}

template <>
TNode<Float64T> CodeStubAssembler::PrepareValueForWriteToTypedArray<Float64T>(
    TNode<Object> input, ElementsKind elements_kind, TNode<Context> context) {
  DCHECK(IsTypedArrayElementsKind(elements_kind));
  CHECK_EQ(elements_kind, FLOAT64_ELEMENTS);

  TVARIABLE(Float64T, var_result);
  TVARIABLE(Object, var_input, input);
  Label done(this, &var_result), if_smi(this), if_heapnumber_or_oddball(this),
      convert(this), loop(this, &var_input);
  Goto(&loop);
  BIND(&loop);
  GotoIf(TaggedIsSmi(var_input.value()), &if_smi);
  // We can handle both HeapNumber and Oddball here, since Oddball has the
  // same layout as the HeapNumber for the HeapNumber::value field. This
  // way we can also properly optimize stores of oddballs to typed arrays.
  TNode<HeapObject> heap_object = CAST(var_input.value());
  GotoIf(IsHeapNumber(heap_object), &if_heapnumber_or_oddball);
  STATIC_ASSERT_FIELD_OFFSETS_EQUAL(offsetof(HeapNumber, value_),
                                    offsetof(Oddball, to_number_raw_));
  Branch(HasInstanceType(heap_object, ODDBALL_TYPE), &if_heapnumber_or_oddball,
         &convert);

  BIND(&if_heapnumber_or_oddball);
  {
    var_result =
        LoadObjectField<Float64T>(heap_object, offsetof(HeapNumber, value_));
    Goto(&done);
  }

  BIND(&if_smi);
  {
    TNode<Int32T> value = SmiToInt32(CAST(var_input.value()));
    var_result = ChangeInt32ToFloat64(value);
    Goto(&done);
  }

  BIND(&convert);
  {
    var_input = CallBuiltin(Builtin::kNonNumberToNumber, context, input);
    Goto(&loop);
  }

  BIND(&done);
  return var_result.value();
}

template <>
TNode<BigInt> CodeStubAssembler::PrepareValueForWriteToTypedArray<BigInt>(
    TNode<Object> input, ElementsKind elements_kind, TNode<Context> context) {
  DCHECK(elements_kind == BIGINT64_ELEMENTS ||
         elements_kind == BIGUINT64_ELEMENTS);
  return ToBigInt(context, input);
}

#if V8_ENABLE_WEBASSEMBLY
TorqueStructInt64AsInt32Pair CodeStubAssembler::BigIntToRawBytes(
    TNode<BigInt> value) {
  TVARIABLE(UintPtrT, var_low);
  // Only used on 32-bit platforms.
  TVARIABLE(UintPtrT, var_high);
  BigIntToRawBytes(value, &var_low, &var_high);
  return {var_low.value(), var_high.value()};
}
#endif  // V8_ENABLE_WEBASSEMBLY

void CodeStubAssembler::BigIntToRawBytes(TNode<BigInt> bigint,
                                         TVariable<UintPtrT>* var_low,
                                         TVariable<UintPtrT>* var_high) {
  Label done(this);
  *var_low = Unsigned(IntPtrConstant(0));
  *var_high = Unsigned(IntPtrConstant(0));
  TNode<Word32T> bitfield = LoadBigIntBitfield(bigint);
  TNode<Uint32T> length = DecodeWord32<BigIntBase::LengthBits>(bitfield);
  TNode<Uint32T> sign = DecodeWord32<BigIntBase::SignBits>(bitfield);
  GotoIf(Word32Equal(length, Int32Constant(0)), &done);
  *var_low = LoadBigIntDigit(bigint, 0);
  if (!Is64()) {
    Label load_done(this);
    GotoIf(Word32Equal(length, Int32Constant(1)), &load_done);
    *var_high = LoadBigIntDigit(bigint, 1);
    Goto(&load_done);
    BIND(&load_done);
  }
  GotoIf(Word32Equal(sign, Int32Constant(0)), &done);
  // Negative value. Simulate two's complement.
  if (!Is64()) {
    *var_high = Unsigned(IntPtrSub(IntPtrConstant(0), var_high->value()));
    Label no_carry(this);
    GotoIf(IntPtrEqual(var_low->value(), IntPtrConstant(0)), &no_carry);
    *var_high = Unsigned(IntPtrSub(var_high->value(), IntPtrConstant(1)));
    Goto(&no_carry);
    BIND(&no_carry);
  }
  *var_low = Unsigned(IntPtrSub(IntPtrConstant(0), var_low->value()));
  Goto(&done);
  BIND(&done);
}

template <>
void CodeStubAssembler::EmitElementStoreTypedArrayUpdateValue(
    TNode<Object> value, ElementsKind elements_kind,
    TNode<Word32T> converted_value, TVariable<Object>* maybe_converted_value) {
  switch (elements_kind) {
    case UINT8_ELEMENTS:
    case INT8_ELEMENTS:
    case UINT16_ELEMENTS:
    case INT16_ELEMENTS:
    case UINT8_CLAMPED_ELEMENTS:
      *maybe_converted_value =
          SmiFromInt32(UncheckedCast<Int32T>(converted_value));
      break;
    case UINT32_ELEMENTS:
      *maybe_converted_value =
          ChangeUint32ToTagged(UncheckedCast<Uint32T>(converted_value));
      break;
    case INT32_ELEMENTS:
      *maybe_converted_value =
          ChangeInt32ToTagged(UncheckedCast<Int32T>(converted_value));
      break;
    default:
      UNREACHABLE();
  }
}

template <>
void CodeStubAssembler::EmitElementStoreTypedArrayUpdateValue(
    TNode<Object> value, ElementsKind elements_kind,
    TNode<Float16T> converted_value, TVariable<Object>* maybe_converted_value) {
  Label dont_allocate_heap_number(this), end(this);
  GotoIf(TaggedIsSmi(value), &dont_allocate_heap_number);
  GotoIf(IsHeapNumber(CAST(value)), &dont_allocate_heap_number);
  {
    *maybe_converted_value =
        AllocateHeapNumberWithValue(ChangeFloat16ToFloat64(converted_value));
    Goto(&end);
  }
  BIND(&dont_allocate_heap_number);
  {
    *maybe_converted_value = value;
    Goto(&end);
  }
  BIND(&end);
}

template <>
void CodeStubAssembler::EmitElementStoreTypedArrayUpdateValue(
    TNode<Object> value, ElementsKind elements_kind,
    TNode<Float32T> converted_value, TVariable<Object>* maybe_converted_value) {
  Label dont_allocate_heap_number(this), end(this);
  GotoIf(TaggedIsSmi(value), &dont_allocate_heap_number);
  GotoIf(IsHeapNumber(CAST(value)), &dont_allocate_heap_number);
  {
    *maybe_converted_value =
        AllocateHeapNumberWithValue(ChangeFloat32ToFloat64(converted_value));
    Goto(&end);
  }
  BIND(&dont_allocate_heap_number);
  {
    *maybe_converted_value = value;
    Goto(&end);
  }
  BIND(&end);
}

template <>
void CodeStubAssembler::EmitElementStoreTypedArrayUpdateValue(
    TNode<Object> value, ElementsKind elements_kind,
    TNode<Float64T> converted_value, TVariable<Object>* maybe_converted_value) {
  Label dont_allocate_heap_number(this), end(this);
  GotoIf(TaggedIsSmi(value), &dont_allocate_heap_number);
  GotoIf(IsHeapNumber(CAST(value)), &dont_allocate_heap_number);
  {
    *maybe_converted_value = AllocateHeapNumberWithValue(converted_value);
    Goto(&end);
  }
  BIND(&dont_allocate_heap_number);
  {
    *maybe_converted_value = value;
    Goto(&end);
  }
  BIND(&end);
}

template <>
void CodeStubAssembler::EmitElementStoreTypedArrayUpdateValue(
    TNode<Object> value, ElementsKind elements_kind,
    TNode<BigInt> converted_value, TVariable<Object>* maybe_converted_value) {
  *maybe_converted_value = converted_value;
}

template <typename TValue>
void CodeStubAssembler::EmitElementStoreTypedArray(
    TNode<JSTypedArray> typed_array, TNode<IntPtrT> key, TNode<Object> value,
    ElementsKind elements_kind, KeyedAccessStoreMode store_mode, Label* bailout,
    TNode<Context> context, TVariable<Object>* maybe_converted_value) {
  Label done(this), update_value_and_bailout(this, Label::kDeferred);

  bool is_rab_gsab = false;
  if (IsRabGsabTypedArrayElementsKind(elements_kind)) {
    is_rab_gsab = true;
    // For the rest of the function, use the corresponding non-RAB/GSAB
    // ElementsKind.
    elements_kind = GetCorrespondingNonRabGsabElementsKind(elements_kind);
  }

  TNode<TValue> converted_value =
      PrepareValueForWriteToTypedArray<TValue>(value, elements_kind, context);

  // There must be no allocations between the buffer load and
  // and the actual store to backing store, because GC may decide that
  // the buffer is not alive or move the elements.
  // TODO(ishell): introduce DisallowGarbageCollectionCode scope here.

  // Check if buffer has been detached. (For RAB / GSAB this is part of loading
  // the length, so no additional check is needed.)
  TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(typed_array);
  if (!is_rab_gsab) {
    GotoIf(IsDetachedBuffer(buffer), &update_value_and_bailout);
  }

  // Bounds check.
  TNode<UintPtrT> length;
  if (is_rab_gsab) {
    length = LoadVariableLengthJSTypedArrayLength(
        typed_array, buffer,
        StoreModeIgnoresTypeArrayOOB(store_mode) ? &done
                                                 : &update_value_and_bailout);
  } else {
    length = LoadJSTypedArrayLength(typed_array);
  }

  if (StoreModeIgnoresTypeArrayOOB(store_mode)) {
    // Skip the store if we write beyond the length or
    // to a property with a negative integer index.
    GotoIfNot(UintPtrLessThan(key, length), &done);
  } else {
    DCHECK(StoreModeIsInBounds(store_mode));
    GotoIfNot(UintPtrLessThan(key, length), &update_value_and_bailout);
  }

  TNode<RawPtrT> data_ptr = LoadJSTypedArrayDataPtr(typed_array);
  StoreElement(data_ptr, elements_kind, key, converted_value);
  Goto(&done);

  if (!is_rab_gsab || !StoreModeIgnoresTypeArrayOOB(store_mode)) {
    BIND(&update_value_and_bailout);
    // We already prepared the incoming value for storing into a typed array.
    // This might involve calling ToNumber in some cases. We shouldn't call
    // ToNumber again in the runtime so pass the converted value to the runtime.
    // The prepared value is an untagged value. Convert it to a tagged value
    // to pass it to runtime. It is not possible to do the detached buffer check
    // before we prepare the value, since ToNumber can detach the ArrayBuffer.
    // The spec specifies the order of these operations.
    if (maybe_converted_value != nullptr) {
      EmitElementStoreTypedArrayUpdateValue(
          value, elements_kind, converted_value, maybe_converted_value);
    }
    Goto(bailout);
  }

  BIND(&done);
}

void CodeStubAssembler::EmitElementStore(
    TNode<JSObject> object, TNode<Object> key, TNode<Object> value,
    ElementsKind elements_kind, KeyedAccessStoreMode store_mode, Label* bailout,
    TNode<Context> context, TVariable<Object>* maybe_converted_value) {
  CSA_DCHECK(this, Word32BinaryNot(IsJSProxy(object)));

  TNode<FixedArrayBase> elements = LoadElements(object);
  if (!(IsSmiOrObjectElementsKind(elements_kind) ||
        IsSealedElementsKind(elements_kind) ||
        IsNonextensibleElementsKind(elements_kind))) {
    CSA_DCHECK(this, Word32BinaryNot(IsFixedCOWArrayMap(LoadMap(elements))));
  } else if (!StoreModeHandlesCOW(store_mode)) {
    GotoIf(IsFixedCOWArrayMap(LoadMap(elements)), bailout);
  }

  // TODO(ishell): introduce TryToIntPtrOrSmi() and use BInt.
  TNode<IntPtrT> intptr_key = TryToIntptr(key, bailout);

  // TODO(rmcilroy): TNodify the converted value once this funciton and
  // StoreElement are templated based on the type elements_kind type.
  if (IsTypedArrayOrRabGsabTypedArrayElementsKind(elements_kind)) {
    TNode<JSTypedArray> typed_array = CAST(object);
    switch (elements_kind) {
      case UINT8_ELEMENTS:
      case INT8_ELEMENTS:
      case UINT16_ELEMENTS:
      case INT16_ELEMENTS:
      case UINT32_ELEMENTS:
      case INT32_ELEMENTS:
      case UINT8_CLAMPED_ELEMENTS:
      case RAB_GSAB_UINT8_ELEMENTS:
      case RAB_GSAB_INT8_ELEMENTS:
      case RAB_GSAB_UINT16_ELEMENTS:
      case RAB_GSAB_INT16_ELEMENTS:
      case RAB_GSAB_UINT32_ELEMENTS:
      case RAB_GSAB_INT32_ELEMENTS:
      case RAB_GSAB_UINT8_CLAMPED_ELEMENTS:
        EmitElementStoreTypedArray<Word32T>(typed_array, intptr_key, value,
                                            elements_kind, store_mode, bailout,
                                            context, maybe_converted_value);
        break;
      case FLOAT32_ELEMENTS:
      case RAB_GSAB_FLOAT32_ELEMENTS:
        EmitElementStoreTypedArray<Float32T>(typed_array, intptr_key, value,
                                             elements_kind, store_mode, bailout,
                                             context, maybe_converted_value);
        break;
      case FLOAT64_ELEMENTS:
      case RAB_GSAB_FLOAT64_ELEMENTS:
        EmitElementStoreTypedArray<Float64T>(typed_array, intptr_key, value,
                                             elements_kind, store_mode, bailout,
                                             context, maybe_converted_value);
        break;
      case BIGINT64_ELEMENTS:
      case BIGUINT64_ELEMENTS:
      case RAB_GSAB_BIGINT64_ELEMENTS:
      case RAB_GSAB_BIGUINT64_ELEMENTS:
        EmitElementStoreTypedArray<BigInt>(typed_array, intptr_key, value,
                                           elements_kind, store_mode, bailout,
                                           context, maybe_converted_value);
        break;
      case FLOAT16_ELEMENTS:
      case RAB_GSAB_FLOAT16_ELEMENTS:
        EmitElementStoreTypedArray<Float16T>(typed_array, intptr_key, value,
                                             elements_kind, store_mode, bailout,
                                             context, maybe_converted_value);
        break;
      default:
        UNREACHABLE();
    }
    return;
  }
  DCHECK(IsFastElementsKind(elements_kind) ||
         IsSealedElementsKind(elements_kind) ||
         IsNonextensibleElementsKind(elements_kind));

  // In case value is stored into a fast smi array, assure that the value is
  // a smi before manipulating the backing store. Otherwise the backing store
  // may be left in an invalid state.
  std::optional<TNode<Float64T>> float_value;
  if (IsSmiElementsKind(elements_kind)) {
    GotoIfNot(TaggedIsSmi(value), bailout);
  } else if (IsDoubleElementsKind(elements_kind)) {
    float_value = TryTaggedToFloat64(value, bailout);
  }

  TNode<Smi> smi_length = Select<Smi>(
      IsJSArray(object),
      [=, this]() {
        // This is casting Number -> Smi which may not actually be safe.
        return CAST(LoadJSArrayLength(CAST(object)));
      },
      [=, this]() { return LoadFixedArrayBaseLength(elements); });

  TNode<UintPtrT> length = Unsigned(PositiveSmiUntag(smi_length));
  if (StoreModeCanGrow(store_mode) &&
      !(IsSealedElementsKind(elements_kind) ||
        IsNonextensibleElementsKind(elements_kind))) {
    elements = CheckForCapacityGrow(object, elements, elements_kind, length,
                                    intptr_key, bailout);
  } else {
    GotoIfNot(UintPtrLessThan(Unsigned(intptr_key), length), bailout);
  }

  // Cannot store to a hole in holey sealed elements so bailout.
  if (elements_kind == HOLEY_SEALED_ELEMENTS ||
      elements_kind == HOLEY_NONEXTENSIBLE_ELEMENTS) {
    TNode<Object> target_value =
        LoadFixedArrayElement(CAST(elements), intptr_key);
    GotoIf(IsTheHole(target_value), bailout);
  }

  // If we didn't grow {elements}, it might still be COW, in which case we
  // copy it now.
  if (!(IsSmiOrObjectElementsKind(elements_kind) ||
        IsSealedElementsKind(elements_kind) ||
        IsNonextensibleElementsKind(elements_kind))) {
    CSA_DCHECK(this, Word32BinaryNot(IsFixedCOWArrayMap(LoadMap(elements))));
  } else if (StoreModeHandlesCOW(store_mode)) {
    elements = CopyElementsOnWrite(object, elements, elements_kind,
                                   Signed(length), bailout);
  }

  CSA_DCHECK(this, Word32BinaryNot(IsFixedCOWArrayMap(LoadMap(elements))));
  if (float_value) {
    StoreElement(elements, elements_kind, intptr_key, float_value.value());
  } else {
    if (elements_kind == SHARED_ARRAY_ELEMENTS) {
      TVARIABLE(Object, shared_value, value);
      SharedValueBarrier(context, &shared_value);
      StoreElement(elements, elements_kind, intptr_key, shared_value.value());
    } else {
      StoreElement(elements, elements_kind, intptr_key, value);
    }
  }
}

TNode<FixedArrayBase> CodeStubAssembler::CheckForCapacityGrow(
    TNode<JSObject> object, TNode<FixedArrayBase> elements, ElementsKind kind,
    TNode<UintPtrT> length, TNode<IntPtrT> key, Label* bailout) {
  DCHECK(IsFastElementsKind(kind));
  TVARIABLE(FixedArrayBase, checked_elements);
  Label grow_case(this), no_grow_case(this), done(this),
      grow_bailout(this, Label::kDeferred);

  TNode<BoolT> condition;
  if (IsHoleyElementsKind(kind)) {
    condition = UintPtrGreaterThanOrEqual(key, length);
  } else {
    // We don't support growing here unless the value is being appended.
    condition = WordEqual(key, length);
  }
  Branch(condition, &grow_case, &no_grow_case);

  BIND(&grow_case);
  {
    TNode<IntPtrT> current_capacity =
        LoadAndUntagFixedArrayBaseLength(elements);
    checked_elements = elements;
    Label fits_capacity(this);
    // If key is negative, we will notice in Runtime::kGrowArrayElements.
    GotoIf(UintPtrLessThan(key, current_capacity), &fits_capacity);

    {
      TNode<FixedArrayBase> new_elements = TryGrowElementsCapacity(
          object, elements, kind, key, current_capacity, &grow_bailout);
      checked_elements = new_elements;
      Goto(&fits_capacity);
    }

    BIND(&grow_bailout);
    {
      GotoIf(IntPtrLessThan(key, IntPtrConstant(0)), bailout);
      TNode<Number> tagged_key = ChangeUintPtrToTagged(Unsigned(key));
      TNode<Object> maybe_elements = CallRuntime(
          Runtime::kGrowArrayElements, NoContextConstant(), object, tagged_key);
      GotoIf(TaggedIsSmi(maybe_elements), bailout);
      TNode<FixedArrayBase> new_elements = CAST(maybe_elements);
      CSA_DCHECK(this, IsFixedArrayWithKind(new_elements, kind));
      checked_elements = new_elements;
      Goto(&fits_capacity);
    }

    BIND(&fits_capacity);
    GotoIfNot(IsJSArray(object), &done);

    TNode<IntPtrT> new_length = IntPtrAdd(key, IntPtrConstant(1));
    StoreObjectFieldNoWriteBarrier(object, JSArray::kLengthOffset,
                                   SmiTag(new_length));
    Goto(&done);
  }

  BIND(&no_grow_case);
  {
    GotoIfNot(UintPtrLessThan(key, length), bailout);
    checked_elements = elements;
    Goto(&done);
  }

  BIND(&done);
  return checked_elements.value();
}

TNode<FixedArrayBase> CodeStubAssembler::CopyElementsOnWrite(
    TNode<HeapObject> object, TNode<FixedArrayBase> elements, ElementsKind kind,
    TNode<IntPtrT> length, Label* bailout) {
  TVARIABLE(FixedArrayBase, new_elements_var, elements);
  Label done(this);

  GotoIfNot(IsFixedCOWArrayMap(LoadMap(elements)), &done);
  {
    TNode<IntPtrT> capacity = LoadAndUntagFixedArrayBaseLength(elements);
    TNode<FixedArrayBase> new_elements = GrowElementsCapacity(
        object, elements, kind, kind, length, capacity, bailout);
    new_elements_var = new_elements;
    Goto(&done);
  }

  BIND(&done);
  return new_elements_var.value();
}

void CodeStubAssembler::TransitionElementsKind(TNode<JSObject> object,
                                               TNode<Map> map,
                                               ElementsKind from_kind,
                                               ElementsKind to_kind,
                                               Label* bailout) {
  DCHECK(!IsHoleyElementsKind(from_kind) || IsHoleyElementsKind(to_kind));
  if (AllocationSite::ShouldTrack(from_kind, to_kind)) {
    TrapAllocationMemento(object, bailout);
  }

  if (!IsSimpleMapChangeTransition(from_kind, to_kind)) {
    Comment("Non-simple map transition");
    TNode<FixedArrayBase> elements = LoadElements(object);

    Label done(this);
    GotoIf(TaggedEqual(elements, EmptyFixedArrayConstant()), &done);

    // TODO(ishell): Use BInt for elements_length and array_length.
    TNode<IntPtrT> elements_length = LoadAndUntagFixedArrayBaseLength(elements);
    TNode<IntPtrT> array_length = Select<IntPtrT>(
        IsJSArray(object),
        [=, this]() {
          CSA_DCHECK(this, IsFastElementsKind(LoadElementsKind(object)));
          return PositiveSmiUntag(LoadFastJSArrayLength(CAST(object)));
        },
        [=]() { return elements_length; });

    CSA_DCHECK(this, WordNotEqual(elements_length, IntPtrConstant(0)));

    GrowElementsCapacity(object, elements, from_kind, to_kind, array_length,
                         elements_length, bailout);
    Goto(&done);
    BIND(&done);
  }

  StoreMap(object, map);
}

void CodeStubAssembler::TrapAllocationMemento(TNode<JSObject> object,
                                              Label* memento_found) {
  DCHECK(V8_ALLOCATION_SITE_TRACKING_BOOL);
  Comment("[ TrapAllocationMemento");
  Label no_memento_found(this);
  Label top_check(this), map_check(this);

  TNode<ExternalReference> new_space_top_address = ExternalConstant(
      ExternalReference::new_space_allocation_top_address(isolate()));
  const int kMementoMapOffset =
      ALIGN_TO_ALLOCATION_ALIGNMENT(JSArray::kHeaderSize);
  const int kMementoLastWordOffset =
      kMementoMapOffset + AllocationMemento::kSize - kTaggedSize;

  // Bail out if the object is not in new space.
  TNode<IntPtrT> object_word = BitcastTaggedToWord(object);
  // TODO(v8:11641): Skip TrapAllocationMemento when allocation-site
  // tracking is disabled.
  TNode<IntPtrT> object_page_header = MemoryChunkFromAddress(object_word);
  {
    TNode<IntPtrT> page_flags = Load<IntPtrT>(
        object_page_header, IntPtrConstant(MemoryChunkLayout::kFlagsOffset));
    if (v8_flags.sticky_mark_bits) {
      // Pages with only old objects contain no mementos.
      GotoIfNot(
          WordEqual(WordAnd(page_flags,
                            IntPtrConstant(MemoryChunk::CONTAINS_ONLY_OLD)),
                    IntPtrConstant(0)),
          &no_memento_found);
    } else {
      GotoIf(WordEqual(
                 WordAnd(page_flags,
                         IntPtrConstant(MemoryChunk::kIsInYoungGenerationMask)),
                 IntPtrConstant(0)),
             &no_memento_found);
    }
    // TODO(v8:11799): Support allocation memento for a large object by
    // allocating additional word for the memento after the large object.
    GotoIf(WordNotEqual(WordAnd(page_flags,
                                IntPtrConstant(MemoryChunk::kIsLargePageMask)),
                        IntPtrConstant(0)),
           &no_memento_found);
  }

  TNode<IntPtrT> memento_last_word = IntPtrAdd(
      object_word, IntPtrConstant(kMementoLastWordOffset - kHeapObjectTag));
  TNode<IntPtrT> memento_last_word_page_header =
      MemoryChunkFromAddress(memento_last_word);

  TNode<IntPtrT> new_space_top = Load<IntPtrT>(new_space_top_address);
  TNode<IntPtrT> new_space_top_page_header =
      MemoryChunkFromAddress(new_space_top);

  // If the object is in new space, we need to check whether respective
  // potential memento object is on the same page as the current top.
  GotoIf(WordEqual(memento_last_word_page_header, new_space_top_page_header),
         &top_check);

  // The object is on a different page than allocation top. Bail out if the
  // object sits on the page boundary as no memento can follow and we cannot
  // touch the memory following it.
  Branch(WordEqual(object_page_header, memento_last_word_page_header),
         &map_check, &no_memento_found);

  // If top is on the same page as the current object, we need to check whether
  // we are below top.
  BIND(&top_check);
  {
    Branch(UintPtrGreaterThanOrEqual(memento_last_word, new_space_top),
           &no_memento_found, &map_check);
  }

  // Memento map check.
  BIND(&map_check);
  {
    TNode<AnyTaggedT> maybe_mapword =
        LoadObjectField(object, kMementoMapOffset);
    TNode<AnyTaggedT> memento_mapword =
        LoadRootMapWord(RootIndex::kAllocationMementoMap);
    Branch(TaggedEqual(maybe_mapword, memento_mapword), memento_found,
           &no_memento_found);
  }
  BIND(&no_memento_found);
  Comment("] TrapAllocationMemento");
}

TNode<IntPtrT> CodeStubAssembler::MemoryChunkFromAddress(
    TNode<IntPtrT> address) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return WordAnd(address,
                 IntPtrConstant(~MemoryChunk::GetAlignmentMaskForAssembler()));
}

TNode<IntPtrT> CodeStubAssembler::PageMetadataFromMemoryChunk(
    TNode<IntPtrT> address) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
#ifdef V8_ENABLE_SANDBOX
  TNode<RawPtrT> table = ExternalConstant(
      ExternalReference::memory_chunk_metadata_table_address());
  TNode<Uint32T> index = Load<Uint32T>(
      address, IntPtrConstant(MemoryChunkLayout::kMetadataIndexOffset));
  index = Word32And(
      index, UniqueUint32Constant(MemoryChunk::kMetadataPointerTableSizeMask));
  TNode<IntPtrT> offset = ChangeInt32ToIntPtr(
      Word32Shl(index, UniqueUint32Constant(kSystemPointerSizeLog2)));
  TNode<IntPtrT> metadata = Load<IntPtrT>(table, offset);
  // Check that the Metadata belongs to this Chunk, since an attacker with write
  // inside the sandbox could've swapped the index.
  TNode<IntPtrT> metadata_chunk = MemoryChunkFromAddress(Load<IntPtrT>(
      metadata, IntPtrConstant(MemoryChunkLayout::kAreaStartOffset)));
  CSA_CHECK(this, WordEqual(metadata_chunk, address));
  return metadata;
#else
  return Load<IntPtrT>(address,
                       IntPtrConstant(MemoryChunkLayout::kMetadataOffset));
#endif
}

TNode<IntPtrT> CodeStubAssembler::PageMetadataFromAddress(
    TNode<IntPtrT> address) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return PageMetadataFromMemoryChunk(MemoryChunkFromAddress(address));
}

TNode<AllocationSite> CodeStubAssembler::CreateAllocationSiteInFeedbackVector(
    TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot) {
  TNode<IntPtrT> size = IntPtrConstant(AllocationSite::kSizeWithWeakNext);
  TNode<HeapObject> site = Allocate(size, AllocationFlag::kPretenured);
  StoreMapNoWriteBarrier(site, RootIndex::kAllocationSiteWithWeakNextMap);
  // Should match AllocationSite::Initialize.
  TNode<WordT> field = UpdateWord<AllocationSite::ElementsKindBits>(
      IntPtrConstant(0), UintPtrConstant(GetInitialFastElementsKind()));
  StoreObjectFieldNoWriteBarrier(
      site, AllocationSite::kTransitionInfoOrBoilerplateOffset,
      SmiTag(Signed(field)));

  // Unlike literals, constructed arrays don't have nested sites
  TNode<Smi> zero = SmiConstant(0);
  StoreObjectFieldNoWriteBarrier(site, AllocationSite::kNestedSiteOffset, zero);

  // Pretenuring calculation field.
  StoreObjectFieldNoWriteBarrier(site, AllocationSite::kPretenureDataOffset,
                                 Int32Constant(0));

  // Pretenuring memento creation count field.
  StoreObjectFieldNoWriteBarrier(
      site, AllocationSite::kPretenureCreateCountOffset, Int32Constant(0));

  // Store an empty fixed array for the code dependency.
  StoreObjectFieldRoot(site, AllocationSite::kDependentCodeOffset,
                       DependentCode::kEmptyDependentCode);

  // Link the object to the allocation site list
  TNode<ExternalReference> site_list = ExternalConstant(
      ExternalReference::allocation_sites_list_address(isolate()));
  TNode<Object> next_site =
      LoadBufferObject(ReinterpretCast<RawPtrT>(site_list), 0);

  // TODO(mvstanton): This is a store to a weak pointer, which we may want to
  // mark as such in order to skip the write barrier, once we have a unified
  // system for weakness. For now we decided to keep it like this because having
  // an initial write barrier backed store makes this pointer strong until the
  // next GC, and allocation sites are designed to survive several GCs anyway.
  StoreObjectField(site, AllocationSite::kWeakNextOffset, next_site);
  StoreFullTaggedNoWriteBarrier(site_list, site);

  StoreFeedbackVectorSlot(feedback_vector, slot, site);
  return CAST(site);
}

TNode<MaybeObject> CodeStubAssembler::StoreWeakReferenceInFeedbackVector(
    TNode<FeedbackVector> feedback_vector, TNode<UintPtrT> slot,
    TNode<HeapObject> value, int additional_offset) {
  TNode<HeapObjectReference> weak_value = MakeWeak(value);
  StoreFeedbackVectorSlot(feedback_vector, slot, weak_value,
                          UPDATE_WRITE_BARRIER, additional_offset);
  return weak_value;
}

TNode<BoolT> CodeStubAssembler::HasBoilerplate(
    TNode<Object> maybe_literal_site) {
  return TaggedIsNotSmi(maybe_literal_site);
}

TNode<Smi> CodeStubAssembler::LoadTransitionInfo(
    TNode<AllocationSite> allocation_site) {
  TNode<Smi> transition_info = CAST(LoadObjectField(
      allocation_site, AllocationSite::kTransitionInfoOrBoilerplateOffset));
  return transition_info;
}

TNode<JSObject> CodeStubAssembler::LoadBoilerplate(
    TNode<AllocationSite> allocation_site) {
  TNode<JSObject> boilerplate = CAST(LoadObjectField(
      allocation_site, AllocationSite::kTransitionInfoOrBoilerplateOffset));
  return boilerplate;
}

TNode<Int32T> CodeStubAssembler::LoadElementsKind(
    TNode<AllocationSite> allocation_site) {
  TNode<Smi> transition_info = LoadTransitionInfo(allocation_site);
  TNode<Int32T> elements_kind =
      Signed(DecodeWord32<AllocationSite::ElementsKindBits>(
          SmiToInt32(transition_info)));
  CSA_DCHECK(this, IsFastElementsKind(elements_kind));
  return elements_kind;
}

TNode<Object> CodeStubAssembler::LoadNestedAllocationSite(
    TNode<AllocationSite> allocation_site) {
  return LoadObjectField(allocation_site, AllocationSite::kNestedSiteOffset);
}

template <typename TIndex>
void CodeStubAssembler::BuildFastLoop(
    const VariableList& vars, TVariable<TIndex>& var_index,
    TNode<TIndex> start_index, TNode<TIndex> end_index,
    const FastLoopBody<TIndex>& body, int increment,
    LoopUnrollingMode unrolling_mode, IndexAdvanceMode advance_mode) {
  // Update the index comparisons below in case we'd ever want to use Smi
  // indexes.
  static_assert(
      !std::is_same<TIndex, Smi>::value,
      "Smi indices are currently not supported because it's not clear whether "
      "the use case allows unsigned comparisons or not");
  DCHECK_NE(increment, 0);
  var_index = start_index;
  VariableList vars_copy(vars.begin(), vars.end(), zone());
  vars_copy.push_back(&var_index);
  Label loop(this, vars_copy);
  Label after_loop(this), done(this);

  auto loop_body = [&]() {
    if (advance_mode == IndexAdvanceMode::kPre) {
      Increment(&var_index, increment);
    }
    body(var_index.value());
    if (advance_mode == IndexAdvanceMode::kPost) {
      Increment(&var_index, increment);
    }
  };
  // The loops below are generated using the following trick:
  // Introduce an explicit second check of the termination condition before
  // the loop that helps turbofan generate better code. If there's only a
  // single check, then the CodeStubAssembler forces it to be at the beginning
  // of the loop requiring a backwards branch at the end of the loop (it's not
  // possible to force the loop header check at the end of the loop and branch
  // forward to it from the pre-header). The extra branch is slower in the
  // case that the loop actually iterates.
  if (unrolling_mode == LoopUnrollingMode::kNo) {
    TNode<BoolT> first_check = UintPtrOrSmiEqual(var_index.value(), end_index);
    int32_t first_check_val;
    if (TryToInt32Constant(first_check, &first_check_val)) {
      if (first_check_val) return;
      Goto(&loop);
    } else {
      Branch(first_check, &done, &loop);
    }

    BIND(&loop);
    {
      loop_body();
      CSA_DCHECK(this, increment > 0 ? UintPtrOrSmiLessThanOrEqual(
                                           var_index.value(), end_index)
                                     : UintPtrOrSmiLessThanOrEqual(
                                           end_index, var_index.value()));
      Branch(UintPtrOrSmiNotEqual(var_index.value(), end_index), &loop, &done);
    }
    BIND(&done);
  } else {
    // Check if there are at least two elements between start_index and
    // end_index.
    DCHECK_EQ(unrolling_mode, LoopUnrollingMode::kYes);
    if (increment > 0) {
      CSA_DCHECK(this, UintPtrOrSmiLessThanOrEqual(start_index, end_index));
      GotoIfNot(UintPtrOrSmiLessThanOrEqual(
                    IntPtrOrSmiAdd(start_index,
                                   IntPtrOrSmiConstant<TIndex>(increment)),
                    end_index),
                &done);
    } else {
      CSA_DCHECK(this, UintPtrOrSmiLessThanOrEqual(end_index, start_index));
      GotoIfNot(
          UintPtrOrSmiLessThanOrEqual(
              IntPtrOrSmiSub(end_index, IntPtrOrSmiConstant<TIndex>(increment)),
              start_index),
          &done);
    }

    TNode<TIndex> last_index =
        IntPtrOrSmiSub(end_index, IntPtrOrSmiConstant<TIndex>(increment));
    TNode<BoolT> first_check =
        increment > 0 ? UintPtrOrSmiLessThan(start_index, last_index)
                      : UintPtrOrSmiGreaterThan(start_index, last_index);
    int32_t first_check_val;
    if (TryToInt32Constant(first_check, &first_check_val)) {
      if (first_check_val) {
        Goto(&loop);
      } else {
        Goto(&after_loop);
      }
    } else {
      Branch(first_check, &loop, &after_loop);
    }

    BIND(&loop);
    {
      Comment("Unrolled Loop");
      loop_body();
      loop_body();
      TNode<BoolT> loop_check =
          increment > 0
              ? UintPtrOrSmiLessThan(var_index.value(), last_index)
              : UintPtrOrSmiGreaterThan(var_index.value(), last_index);
      Branch(loop_check, &loop, &after_loop);
    }
    BIND(&after_loop);
    {
      GotoIfNot(UintPtrOrSmiEqual(var_index.value(), last_index), &done);
      // Iteration count is odd.
      loop_body();
      Goto(&done);
    }
    BIND(&done);
  }
}

// Instantiate BuildFastLoop for IntPtrT, UintPtrT and RawPtrT.
template V8_EXPORT_PRIVATE void CodeStubAssembler::BuildFastLoop<IntPtrT>(
    const VariableList& vars, TVariable<IntPtrT>& var_index,
    TNode<IntPtrT> start_index, TNode<IntPtrT> end_index,
    const FastLoopBody<IntPtrT>& body, int increment,
    LoopUnrollingMode unrolling_mode, IndexAdvanceMode advance_mode);
template V8_EXPORT_PRIVATE void CodeStubAssembler::BuildFastLoop<UintPtrT>(
    const VariableList& vars, TVariable<UintPtrT>& var_index,
    TNode<UintPtrT> start_index, TNode<UintPtrT> end_index,
    const FastLoopBody<UintPtrT>& body, int increment,
    LoopUnrollingMode unrolling_mode, IndexAdvanceMode advance_mode);
template V8_EXPORT_PRIVATE void CodeStubAssembler::BuildFastLoop<RawPtrT>(
    const VariableList& vars, TVariable<RawPtrT>& var_index,
    TNode<RawPtrT> start_index, TNode<RawPtrT> end_index,
    const FastLoopBody<RawPtrT>& body, int increment,
    LoopUnrollingMode unrolling_mode, IndexAdvanceMode advance_mode);

template <typename TIndex>
void CodeStubAssembler::BuildFastArrayForEach(
    TNode<UnionOf<UnionOf<FixedArray, PropertyArray>, HeapObject>> array,
    ElementsKind kind, TNode<TIndex> first_element_inclusive,
    TNode<TIndex> last_element_exclusive, const FastArrayForEachBody& body,
    LoopUnrollingMode loop_unrolling_mode, ForEachDirection direction) {
  static_assert(FixedArray::kHeaderSize == FixedDoubleArray::kHeaderSize);
  CSA_SLOW_DCHECK(this, Word32Or(IsFixedArrayWithKind(array, kind),
                                 IsPropertyArray(array)));

  intptr_t first_val;
  bool constant_first =
      TryToIntPtrConstant(first_element_inclusive, &first_val);
  intptr_t last_val;
  bool constent_last = TryToIntPtrConstant(last_element_exclusive, &last_val);
  if (constant_first && constent_last) {
    intptr_t delta = last_val - first_val;
    DCHECK_GE(delta, 0);
    if (delta <= kElementLoopUnrollThreshold) {
      if (direction == ForEachDirection::kForward) {
        for (intptr_t i = first_val; i < last_val; ++i) {
          TNode<IntPtrT> index = IntPtrConstant(i);
          TNode<IntPtrT> offset = ElementOffsetFromIndex(
              index, kind, FixedArray::kHeaderSize - kHeapObjectTag);
          body(array, offset);
        }
      } else {
        for (intptr_t i = last_val - 1; i >= first_val; --i) {
          TNode<IntPtrT> index = IntPtrConstant(i);
          TNode<IntPtrT> offset = ElementOffsetFromIndex(
              index, kind, FixedArray::kHeaderSize - kHeapObjectTag);
          body(array, offset);
        }
      }
      return;
    }
  }

  TNode<IntPtrT> start = ElementOffsetFromIndex(
      first_element_inclusive, kind, FixedArray::kHeaderSize - kHeapObjectTag);
  TNode<IntPtrT> limit = ElementOffsetFromIndex(
      last_element_exclusive, kind, FixedArray::kHeaderSize - kHeapObjectTag);
  if (direction == ForEachDirection::kReverse) std::swap(start, limit);

  int increment = IsDoubleElementsKind(kind) ? kDoubleSize : kTaggedSize;
  BuildFastLoop<IntPtrT>(
      start, limit, [&](TNode<IntPtrT> offset) { body(array, offset); },
      direction == ForEachDirection::kReverse ? -increment : increment,
      loop_unrolling_mode,
      direction == ForEachDirection::kReverse ? IndexAdvanceMode::kPre
                                              : IndexAdvanceMode::kPost);
}

template <typename TIndex>
void CodeStubAssembler::GotoIfFixedArraySizeDoesntFitInNewSpace(
    TNode<TIndex> element_count, Label* doesnt_fit, int base_size) {
  GotoIf(FixedArraySizeDoesntFitInNewSpace(element_count, base_size),
         doesnt_fit);
}

void CodeStubAssembler::InitializeFieldsWithRoot(TNode<HeapObject> object,
                                                 TNode<IntPtrT> start_offset,
                                                 TNode<IntPtrT> end_offset,
                                                 RootIndex root_index) {
  CSA_SLOW_DCHECK(this, TaggedIsNotSmi(object));
  start_offset = IntPtrAdd(start_offset, IntPtrConstant(-kHeapObjectTag));
  end_offset = IntPtrAdd(end_offset, IntPtrConstant(-kHeapObjectTag));
  TNode<AnyTaggedT> root_value;
  if (root_index == RootIndex::kOnePointerFillerMap) {
    root_value = LoadRootMapWord(root_index);
  } else {
    root_value = LoadRoot(root_index);
  }
  BuildFastLoop<IntPtrT>(
      end_offset, start_offset,
      [=, this](TNode<IntPtrT> current) {
        StoreNoWriteBarrier(MachineRepresentation::kTagged, object, current,
                            root_value);
      },
      -kTaggedSize, LoopUnrollingMode::kYes, IndexAdvanceMode::kPre);
}

void CodeStubAssembler::BranchIfNumberRelationalComparison(Operation op,
                                                           TNode<Number> left,
                                                           TNode<Number> right,
                                                           Label* if_true,
                                                           Label* if_false) {
  Label do_float_comparison(this);
  TVARIABLE(Float64T, var_left_float);
  TVARIABLE(Float64T, var_right_float);

  Branch(
      TaggedIsSmi(left),
      [&] {
        TNode<Smi> smi_left = CAST(left);

        Branch(
            TaggedIsSmi(right),
            [&] {
              TNode<Smi> smi_right = CAST(right);

              // Both {left} and {right} are Smi, so just perform a fast
              // Smi comparison.
              switch (op) {
                case Operation::kEqual:
                  BranchIfSmiEqual(smi_left, smi_right, if_true, if_false);
                  break;
                case Operation::kLessThan:
                  BranchIfSmiLessThan(smi_left, smi_right, if_true, if_false);
                  break;
                case Operation::kLessThanOrEqual:
                  BranchIfSmiLessThanOrEqual(smi_left, smi_right, if_true,
                                             if_false);
                  break;
                case Operation::kGreaterThan:
                  BranchIfSmiLessThan(smi_right, smi_left, if_true, if_false);
                  break;
                case Operation::kGreaterThanOrEqual:
                  BranchIfSmiLessThanOrEqual(smi_right, smi_left, if_true,
                                             if_false);
                  break;
                default:
                  UNREACHABLE();
              }
            },
            [&] {
              var_left_float = SmiToFloat64(smi_left);
              var_right_float = LoadHeapNumberValue(CAST(right));
              Goto(&do_float_comparison);
            });
      },
      [&] {
        var_left_float = LoadHeapNumberValue(CAST(left));

        Branch(
            TaggedIsSmi(right),
            [&] {
              var_right_float = SmiToFloat64(CAST(right));
              Goto(&do_float_comparison);
            },
            [&] {
              var_right_float = LoadHeapNumberValue(CAST(right));
              Goto(&do_float_comparison);
            });
      });

  BIND(&do_float_comparison);
  {
    switch (op) {
      case Operation::kEqual:
        Branch(Float64Equal(var_left_float.value(), var_right_float.value()),
               if_true, if_false);
        break;
      case Operation::kLessThan:
        Branch(Float64LessThan(var_left_float.value(), var_right_float.value()),
               if_true, if_false);
        break;
      case Operation::kLessThanOrEqual:
        Branch(Float64LessThanOrEqual(var_left_float.value(),
                                      var_right_float.value()),
               if_true, if_false);
        break;
      case Operation::kGreaterThan:
        Branch(
            Float64GreaterThan(var_left_float.value(), var_right_float.value()),
            if_true, if_false);
        break;
      case Operation::kGreaterThanOrEqual:
        Branch(Float64GreaterThanOrEqual(var_left_float.value(),
                                         var_right_float.value()),
               if_true, if_false);
        break;
      default:
        UNREACHABLE();
    }
  }
}

void CodeStubAssembler::GotoIfNumberGreaterThanOrEqual(TNode<Number> left,
                                                       TNode<Number> right,
                                                       Label* if_true) {
  Label if_false(this);
  BranchIfNumberRelationalComparison(Operation::kGreaterThanOrEqual, left,
                                     right, if_true, &if_false);
  BIND(&if_false);
}

namespace {
Operation Reverse(Operation op) {
  switch (op) {
    case Operation::kLessThan:
      return Operation::kGreaterThan;
    case Operation::kLessThanOrEqual:
      return Operation::kGreaterThanOrEqual;
    case Operation::kGreaterThan:
      return Operation::kLessThan;
    case Operation::kGreaterThanOrEqual:
      return Operation::kLessThanOrEqual;
    default:
      break;
  }
  UNREACHABLE();
}
}  // anonymous namespace

TNode<Context> CodeStubAssembler::GotoIfHasContextExtensionUpToDepth(
    TNode<Context> context, TNode<Uint32T> depth, Label* target) {
  TVARIABLE(Context, cur_context, context);
  TVARIABLE(Uint32T, cur_depth, depth);

  Label context_search(this, {&cur_depth, &cur_context});
  Label exit_loop(this);
  Label no_extension(this);

  // Loop until the depth is 0.
  CSA_DCHECK(this, Word32NotEqual(cur_depth.value(), Int32Constant(0)));
  Goto(&context_search);
  BIND(&context_search);
  {
#if DEBUG
    // Const tracking let data is stored in the extension slot of a
    // ScriptContext - however, it's unrelated to the sloppy eval variable
    // extension. We should never iterate through a ScriptContext here.
    auto scope_info = LoadScopeInfo(cur_context.value());
    TNode<Uint32T> flags =
        LoadObjectField<Uint32T>(scope_info, ScopeInfo::kFlagsOffset);
    auto scope_type = DecodeWord32<ScopeInfo::ScopeTypeBits>(flags);
    CSA_DCHECK(this, Word32NotEqual(scope_type,
                                    Int32Constant(ScopeType::SCRIPT_SCOPE)));
    CSA_DCHECK(this, Word32NotEqual(scope_type,
                                    Int32Constant(ScopeType::REPL_MODE_SCOPE)));
#endif

    // Check if context has an extension slot.
    TNode<BoolT> has_extension =
        LoadScopeInfoHasExtensionField(LoadScopeInfo(cur_context.value()));
    GotoIfNot(has_extension, &no_extension);

    // Jump to the target if the extension slot is not an undefined value.
    TNode<Object> extension_slot =
        LoadContextElement(cur_context.value(), Context::EXTENSION_INDEX);
    Branch(TaggedNotEqual(extension_slot, UndefinedConstant()), target,
           &no_extension);

    BIND(&no_extension);
    {
      cur_depth = Unsigned(Int32Sub(cur_depth.value(), Int32Constant(1)));
      cur_context = CAST(
          LoadContextElement(cur_context.value(), Context::PREVIOUS_INDEX));

      Branch(Word32NotEqual(cur_depth.value(), Int32Constant(0)),
             &context_search, &exit_loop);
    }
  }
  BIND(&exit_loop);
  return cur_context.value();
}

void CodeStubAssembler::BigInt64Comparison(Operation op, TNode<Object>& left,
                                           TNode<Object>& right,
                                           Label* return_true,
                                           Label* return_false) {
  TVARIABLE(UintPtrT, left_raw);
  TVARIABLE(UintPtrT, right_raw);
  BigIntToRawBytes(CAST(left), &left_raw, &left_raw);
  BigIntToRawBytes(CAST(right), &right_raw, &right_raw);
  TNode<WordT> left_raw_value = left_raw.value();
  TNode<WordT> right_raw_value = right_raw.value();

  TNode<BoolT> condition;
  switch (op) {
    case Operation::kEqual:
    case Operation::kStrictEqual:
      condition = WordEqual(left_raw_value, right_raw_value);
      break;
    case Operation::kLessThan:
      condition = IntPtrLessThan(left_raw_value, right_raw_value);
      break;
    case Operation::kLessThanOrEqual:
      condition = IntPtrLessThanOrEqual(left_raw_value, right_raw_value);
      break;
    case Operation::kGreaterThan:
      condition = IntPtrGreaterThan(left_raw_value, right_raw_value);
      break;
    case Operation::kGreaterThanOrEqual:
      condition = IntPtrGreaterThanOrEqual(left_raw_value, right_raw_value);
      break;
    default:
      UNREACHABLE();
  }
  Branch(condition, return_true, return_false);
}

TNode<Boolean> CodeStubAssembler::RelationalComparison(
    Operation op, TNode<Object> left, TNode<Object> right,
    const LazyNode<Context>& context, TVariable<Smi>* var_type_feedback) {
  Label return_true(this), return_false(this), do_float_comparison(this),
      end(this);
  TVARIABLE(Boolean, var_result);
  TVARIABLE(Float64T, var_left_float);
  TVARIABLE(Float64T, var_right_float);

  // We might need to loop several times due to ToPrimitive and/or ToNumeric
  // conversions.
  TVARIABLE(Object, var_left, left);
  TVARIABLE(Object, var_right, right);
  VariableList loop_variable_list({&var_left, &var_right}, zone());
  if (var_type_feedback != nullptr) {
    // Initialize the type feedback to None. The current feedback is combined
    // with the previous feedback.
    *var_type_feedback = SmiConstant(CompareOperationFeedback::kNone);
    loop_variable_list.push_back(var_type_feedback);
  }
  Label loop(this, loop_variable_list);
  Goto(&loop);
  BIND(&loop);
  {
    left = var_left.value();
    right = var_right.value();

    Label if_left_smi(this), if_left_not_smi(this);
    Branch(TaggedIsSmi(left), &if_left_smi, &if_left_not_smi);

    BIND(&if_left_smi);
    {
      TNode<Smi> smi_left = CAST(left);
      Label if_right_smi(this), if_right_heapnumber(this),
          if_right_bigint(this, Label::kDeferred),
          if_right_not_numeric(this, Label::kDeferred);
      GotoIf(TaggedIsSmi(right), &if_right_smi);
      TNode<Map> right_map = LoadMap(CAST(right));
      GotoIf(IsHeapNumberMap(right_map), &if_right_heapnumber);
      TNode<Uint16T> right_instance_type = LoadMapInstanceType(right_map);
      Branch(IsBigIntInstanceType(right_instance_type), &if_right_bigint,
             &if_right_not_numeric);

      BIND(&if_right_smi);
      {
        TNode<Smi> smi_right = CAST(right);
        CombineFeedback(var_type_feedback,
                        CompareOperationFeedback::kSignedSmall);
        switch (op) {
          case Operation::kLessThan:
            BranchIfSmiLessThan(smi_left, smi_right, &return_true,
                                &return_false);
            break;
          case Operation::kLessThanOrEqual:
            BranchIfSmiLessThanOrEqual(smi_left, smi_right, &return_true,
                                       &return_false);
            break;
          case Operation::kGreaterThan:
            BranchIfSmiLessThan(smi_right, smi_left, &return_true,
                                &return_false);
            break;
          case Operation::kGreaterThanOrEqual:
            BranchIfSmiLessThanOrEqual(smi_right, smi_left, &return_true,
                                       &return_false);
            break;
          default:
            UNREACHABLE();
        }
      }

      BIND(&if_right_heapnumber);
      {
        CombineFeedback(var_type_feedback, CompareOperationFeedback::kNumber);
        var_left_float = SmiToFloat64(smi_left);
        var_right_float = LoadHeapNumberValue(CAST(right));
        Goto(&do_float_comparison);
      }

      BIND(&if_right_bigint);
      {
        OverwriteFeedback(var_type_feedback, CompareOperationFeedback::kAny);
        var_result = CAST(CallRuntime(Runtime::kBigIntCompareToNumber,
                                      NoContextConstant(),
                                      SmiConstant(Reverse(op)), right, left));
        Goto(&end);
      }

      BIND(&if_right_not_numeric);
      {
        OverwriteFeedback(var_type_feedback, CompareOperationFeedback::kAny);
        // Convert {right} to a Numeric; we don't need to perform the
        // dedicated ToPrimitive(right, hint Number) operation, as the
        // ToNumeric(right) will by itself already invoke ToPrimitive with
        // a Number hint.
        var_right = CallBuiltin(Builtin::kNonNumberToNumeric, context(), right);
        Goto(&loop);
      }
    }

    BIND(&if_left_not_smi);
    {
      TNode<Map> left_map = LoadMap(CAST(left));

      Label if_right_smi(this), if_right_not_smi(this);
      Branch(TaggedIsSmi(right), &if_right_smi, &if_right_not_smi);

      BIND(&if_right_smi);
      {
        Label if_left_heapnumber(this), if_left_bigint(this, Label::kDeferred),
            if_left_not_numeric(this, Label::kDeferred);
        GotoIf(IsHeapNumberMap(left_map), &if_left_heapnumber);
        TNode<Uint16T> left_instance_type = LoadMapInstanceType(left_map);
        Branch(IsBigIntInstanceType(left_instance_type), &if_left_bigint,
               &if_left_not_numeric);

        BIND(&if_left_heapnumber);
        {
          CombineFeedback(var_type_feedback, CompareOperationFeedback::kNumber);
          var_left_float = LoadHeapNumberValue(CAST(left));
          var_right_float = SmiToFloat64(CAST(right));
          Goto(&do_float_comparison);
        }

        BIND(&if_left_bigint);
        {
          OverwriteFeedback(var_type_feedback, CompareOperationFeedback::kAny);
          var_result = CAST(CallRuntime(Runtime::kBigIntCompareToNumber,
                                        NoContextConstant(), SmiConstant(op),
                                        left, right));
          Goto(&end);
        }

        BIND(&if_left_not_numeric);
        {
          OverwriteFeedback(var_type_feedback, CompareOperationFeedback::kAny);
          // Convert {left} to a Numeric; we don't need to perform the
          // dedicated ToPrimitive(left, hint Number) operation, as the
          // ToNumeric(left) will by itself already invoke ToPrimitive with
          // a Number hint.
          var_left = CallBuiltin(Builtin::kNonNumberToNumeric, context(), left);
          Goto(&loop);
        }
      }

      BIND(&if_right_not_smi);
      {
        TNode<Map> right_map = LoadMap(CAST(right));

        Label if_left_heapnumber(this), if_left_bigint(this, Label::kDeferred),
            if_left_string(this, Label::kDeferred),
            if_left_other(this, Label::kDeferred);
        GotoIf(IsHeapNumberMap(left_map), &if_left_heapnumber);
        TNode<Uint16T> left_instance_type = LoadMapInstanceType(left_map);
        GotoIf(IsBigIntInstanceType(left_instance_type), &if_left_bigint);
        Branch(IsStringInstanceType(left_instance_type), &if_left_string,
               &if_left_other);

        BIND(&if_left_heapnumber);
        {
          Label if_right_heapnumber(this),
              if_right_bigint(this, Label::kDeferred),
              if_right_not_numeric(this, Label::kDeferred);
          GotoIf(TaggedEqual(right_map, left_map), &if_right_heapnumber);
          TNode<Uint16T> right_instance_type = LoadMapInstanceType(right_map);
          Branch(IsBigIntInstanceType(right_instance_type), &if_right_bigint,
                 &if_right_not_numeric);

          BIND(&if_right_heapnumber);
          {
            CombineFeedback(var_type_feedback,
                            CompareOperationFeedback::kNumber);
            var_left_float = LoadHeapNumberValue(CAST(left));
            var_right_float = LoadHeapNumberValue(CAST(right));
            Goto(&do_float_comparison);
          }

          BIND(&if_right_bigint);
          {
            OverwriteFeedback(var_type_feedback,
                              CompareOperationFeedback::kAny);
            var_result = CAST(CallRuntime(
                Runtime::kBigIntCompareToNumber, NoContextConstant(),
                SmiConstant(Reverse(op)), right, left));
            Goto(&end);
          }

          BIND(&if_right_not_numeric);
          {
            OverwriteFeedback(var_type_feedback,
                              CompareOperationFeedback::kAny);
            // Convert {right} to a Numeric; we don't need to perform
            // dedicated ToPrimitive(right, hint Number) operation, as the
            // ToNumeric(right) will by itself already invoke ToPrimitive with
            // a Number hint.
            var_right =
                CallBuiltin(Builtin::kNonNumberToNumeric, context(), right);
            Goto(&loop);
          }
        }

        BIND(&if_left_bigint);
        {
          Label if_right_heapnumber(this), if_right_bigint(this),
              if_right_string(this), if_right_other(this);
          GotoIf(IsHeapNumberMap(right_map), &if_right_heapnumber);
          TNode<Uint16T> right_instance_type = LoadMapInstanceType(right_map);
          GotoIf(IsBigIntInstanceType(right_instance_type), &if_right_bigint);
          Branch(IsStringInstanceType(right_instance_type), &if_right_string,
                 &if_right_other);

          BIND(&if_right_heapnumber);
          {
            OverwriteFeedback(var_type_feedback,
                              CompareOperationFeedback::kAny);
            var_result = CAST(CallRuntime(Runtime::kBigIntCompareToNumber,
                                          NoContextConstant(), SmiConstant(op),
                                          left, right));
            Goto(&end);
          }

          BIND(&if_right_bigint);
          {
            if (Is64()) {
              Label if_both_bigint(this);
              GotoIfLargeBigInt(CAST(left), &if_both_bigint);
              GotoIfLargeBigInt(CAST(right), &if_both_bigint);

              CombineFeedback(var_type_feedback,
                              CompareOperationFeedback::kBigInt64);
              BigInt64Comparison(op, left, right, &return_true, &return_false);
              BIND(&if_both_bigint);
            }

            CombineFeedback(var_type_feedback,
                            CompareOperationFeedback::kBigInt);
            var_result = CAST(CallBuiltin(BigIntComparisonBuiltinOf(op),
                                          NoContextConstant(), left, right));
            Goto(&end);
          }

          BIND(&if_right_string);
          {
            OverwriteFeedback(var_type_feedback,
                              CompareOperationFeedback::kAny);
            var_result = CAST(CallRuntime(Runtime::kBigIntCompareToString,
                                          NoContextConstant(), SmiConstant(op),
                                          left, right));
            Goto(&end);
          }

          // {right} is not a Number, BigInt, or String.
          BIND(&if_right_other);
          {
            OverwriteFeedback(var_type_feedback,
                              CompareOperationFeedback::kAny);
            // Convert {right} to a Numeric; we don't need to perform
            // dedicated ToPrimitive(right, hint Number) operation, as the
            // ToNumeric(right) will by itself already invoke ToPrimitive with
            // a Number hint.
            var_right =
                CallBuiltin(Builtin::kNonNumberToNumeric, context(), right);
            Goto(&loop);
          }
        }

        BIND(&if_left_string);
        {
          TNode<Uint16T> right_instance_type = LoadMapInstanceType(right_map);

          Label if_right_not_string(this, Label::kDeferred);
          GotoIfNot(IsStringInstanceType(right_instance_type),
                    &if_right_not_string);

          // Both {left} and {right} are strings.
          CombineFeedback(var_type_feedback, CompareOperationFeedback::kString);
          Builtin builtin;
          switch (op) {
            case Operation::kLessThan:
              builtin = Builtin::kStringLessThan;
              break;
            case Operation::kLessThanOrEqual:
              builtin = Builtin::kStringLessThanOrEqual;
              break;
            case Operation::kGreaterThan:
              builtin = Builtin::kStringGreaterThan;
              break;
            case Operation::kGreaterThanOrEqual:
              builtin = Builtin::kStringGreaterThanOrEqual;
              break;
            default:
              UNREACHABLE();
          }
          var_result = CAST(CallBuiltin(builtin, TNode<Object>(), left, right));
          Goto(&end);

          BIND(&if_right_not_string);
          {
            OverwriteFeedback(var_type_feedback,
                              CompareOperationFeedback::kAny);
            // {left} is a String, while {right} isn't. Check if {right} is
            // a BigInt, otherwise call ToPrimitive(right, hint Number) if
            // {right} is a receiver, or ToNumeric(left) and then
            // ToNumeric(right) in the other cases.
            static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
            Label if_right_bigint(this),
                if_right_receiver(this, Label::kDeferred);
            GotoIf(IsBigIntInstanceType(right_instance_type), &if_right_bigint);
            GotoIf(IsJSReceiverInstanceType(right_instance_type),
                   &if_right_receiver);

            var_left =
                CallBuiltin(Builtin::kNonNumberToNumeric, context(), left);
            var_right = CallBuiltin(Builtin::kToNumeric, context(), right);
            Goto(&loop);

            BIND(&if_right_bigint);
            {
              var_result = CAST(CallRuntime(
                  Runtime::kBigIntCompareToString, NoContextConstant(),
                  SmiConstant(Reverse(op)), right, left));
              Goto(&end);
            }

            BIND(&if_right_receiver);
            {
              Builtin builtin =
                  Builtins::NonPrimitiveToPrimitive(ToPrimitiveHint::kNumber);
              var_right = CallBuiltin(builtin, context(), right);
              Goto(&loop);
            }
          }
        }

        BIND(&if_left_other);
        {
          // {left} is neither a Numeric nor a String, and {right} is not a Smi.
          if (var_type_feedback != nullptr) {
            // Collect NumberOrOddball feedback if {left} is an Oddball
            // and {right} is either a HeapNumber or Oddball. Otherwise collect
            // Any feedback.
            Label collect_any_feedback(this), collect_oddball_feedback(this),
                collect_feedback_done(this);
            GotoIfNot(InstanceTypeEqual(left_instance_type, ODDBALL_TYPE),
                      &collect_any_feedback);

            GotoIf(IsHeapNumberMap(right_map), &collect_oddball_feedback);
            TNode<Uint16T> right_instance_type = LoadMapInstanceType(right_map);
            Branch(InstanceTypeEqual(right_instance_type, ODDBALL_TYPE),
                   &collect_oddball_feedback, &collect_any_feedback);

            BIND(&collect_oddball_feedback);
            {
              CombineFeedback(var_type_feedback,
                              CompareOperationFeedback::kNumberOrOddball);
              Goto(&collect_feedback_done);
            }

            BIND(&collect_any_feedback);
            {
              OverwriteFeedback(var_type_feedback,
                                CompareOperationFeedback::kAny);
              Goto(&collect_feedback_done);
            }

            BIND(&collect_feedback_done);
          }

          // If {left} is a receiver, call ToPrimitive(left, hint Number).
          // Otherwise call ToNumeric(right) and then ToNumeric(left), the
          // order here is important as it's observable by user code.
          static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
          Label if_left_receiver(this, Label::kDeferred);
          GotoIf(IsJSReceiverInstanceType(left_instance_type),
                 &if_left_receiver);

          var_right = CallBuiltin(Builtin::kToNumeric, context(), right);
          var_left = CallBuiltin(Builtin::kNonNumberToNumeric, context(), left);
          Goto(&loop);

          BIND(&if_left_receiver);
          {
            Builtin builtin =
                Builtins::NonPrimitiveToPrimitive(ToPrimitiveHint::kNumber);
            var_left = CallBuiltin(builtin, context(), left);
            Goto(&loop);
          }
        }
      }
    }
  }

  BIND(&do_float_comparison);
  {
    switch (op) {
      case Operation::kLessThan:
        Branch(Float64LessThan(var_left_float.value(), var_right_float.value()),
               &return_true, &return_false);
        break;
      case Operation::kLessThanOrEqual:
        Branch(Float64LessThanOrEqual(var_left_float.value(),
                                      var_right_float.value()),
               &return_true, &return_false);
        break;
      case Operation::kGreaterThan:
        Branch(
            Float64GreaterThan(var_left_float.value(), var_right_float.value()),
            &return_true, &return_false);
        break;
      case Operation::kGreaterThanOrEqual:
        Branch(Float64GreaterThanOrEqual(var_left_float.value(),
                                         var_right_float.value()),
               &return_true, &return_false);
        break;
      default:
        UNREACHABLE();
    }
  }

  BIND(&return_true);
  {
    var_result = TrueConstant();
    Goto(&end);
  }

  BIND(&return_false);
  {
    var_result = FalseConstant();
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

TNode<Smi> CodeStubAssembler::CollectFeedbackForString(
    TNode<Int32T> instance_type) {
  TNode<Smi> feedback = SelectSmiConstant(
      Word32Equal(
          Word32And(instance_type, Int32Constant(kIsNotInternalizedMask)),
          Int32Constant(kInternalizedTag)),
      CompareOperationFeedback::kInternalizedString,
      CompareOperationFeedback::kString);
  return feedback;
}

void CodeStubAssembler::GenerateEqual_Same(TNode<Object> value, Label* if_equal,
                                           Label* if_notequal,
                                           TVariable<Smi>* var_type_feedback) {
  // In case of abstract or strict equality checks, we need additional checks
  // for NaN values because they are not considered equal, even if both the
  // left and the right hand side reference exactly the same value.

  Label if_smi(this), if_heapnumber(this);
  GotoIf(TaggedIsSmi(value), &if_smi);

  TNode<HeapObject> value_heapobject = CAST(value);
  TNode<Map> value_map = LoadMap(value_heapobject);
  GotoIf(IsHeapNumberMap(value_map), &if_heapnumber);

  // For non-HeapNumbers, all we do is collect type feedback.
  if (var_type_feedback != nullptr) {
    TNode<Uint16T> instance_type = LoadMapInstanceType(value_map);

    Label if_string(this), if_receiver(this), if_oddball(this), if_symbol(this),
        if_bigint(this);
    GotoIf(IsStringInstanceType(instance_type), &if_string);
    GotoIf(IsJSReceiverInstanceType(instance_type), &if_receiver);
    GotoIf(IsOddballInstanceType(instance_type), &if_oddball);
    Branch(IsBigIntInstanceType(instance_type), &if_bigint, &if_symbol);

    BIND(&if_string);
    {
      CSA_DCHECK(this, IsString(value_heapobject));
      CombineFeedback(var_type_feedback,
                      CollectFeedbackForString(instance_type));
      Goto(if_equal);
    }

    BIND(&if_symbol);
    {
      CSA_DCHECK(this, IsSymbol(value_heapobject));
      CombineFeedback(var_type_feedback, CompareOperationFeedback::kSymbol);
      Goto(if_equal);
    }

    BIND(&if_receiver);
    {
      CSA_DCHECK(this, IsJSReceiver(value_heapobject));
      CombineFeedback(var_type_feedback, CompareOperationFeedback::kReceiver);
      Goto(if_equal);
    }

    BIND(&if_bigint);
    {
      CSA_DCHECK(this, IsBigInt(value_heapobject));

      if (Is64()) {
        Label if_large_bigint(this);
        GotoIfLargeBigInt(CAST(value_heapobject), &if_large_bigint);
        CombineFeedback(var_type_feedback, CompareOperationFeedback::kBigInt64);
        Goto(if_equal);
        BIND(&if_large_bigint);
      }
      CombineFeedback(var_type_feedback, CompareOperationFeedback::kBigInt);
      Goto(if_equal);
    }

    BIND(&if_oddball);
    {
      CSA_DCHECK(this, IsOddball(value_heapobject));
      Label if_boolean(this), if_not_boolean(this);
      Branch(IsBooleanMap(value_map), &if_boolean, &if_not_boolean);

      BIND(&if_boolean);
      {
        CombineFeedback(var_type_feedback, CompareOperationFeedback::kBoolean);
        Goto(if_equal);
      }

      BIND(&if_not_boolean);
      {
        CSA_DCHECK(this, IsNullOrUndefined(value_heapobject));
        CombineFeedback(var_type_feedback,
                        CompareOperationFeedback::kReceiverOrNullOrUndefined);
        Goto(if_equal);
      }
    }
  } else {
    Goto(if_equal);
  }

  BIND(&if_heapnumber);
  {
    CombineFeedback(var_type_feedback, CompareOperationFeedback::kNumber);
    TNode<Float64T> number_value = LoadHeapNumberValue(value_heapobject);
    BranchIfFloat64IsNaN(number_value, if_notequal, if_equal);
  }

  BIND(&if_smi);
  {
    CombineFeedback(var_type_feedback, CompareOperationFeedback::kSignedSmall);
    Goto(if_equal);
  }
}

// ES6 section 7.2.12 Abstract Equality Comparison
TNode<Boolean> CodeStubAssembler::Equal(TNode<Object> left, TNode<Object> right,
                                        const LazyNode<Context>& context,
                                        TVariable<Smi>* var_type_feedback) {
  // This is a slightly optimized version of Object::Equals. Whenever you
  // change something functionality wise in here, remember to update the
  // Object::Equals method as well.

  Label if_equal(this), if_notequal(this), do_float_comparison(this),
      do_right_stringtonumber(this, Label::kDeferred), end(this);
  TVARIABLE(Boolean, result);
  TVARIABLE(Float64T, var_left_float);
  TVARIABLE(Float64T, var_right_float);

  // We can avoid code duplication by exploiting the fact that abstract equality
  // is symmetric.
  Label use_symmetry(this);

  // We might need to loop several times due to ToPrimitive and/or ToNumber
  // conversions.
  TVARIABLE(Object, var_left, left);
  TVARIABLE(Object, var_right, right);
  VariableList loop_variable_list({&var_left, &var_right}, zone());
  if (var_type_feedback != nullptr) {
    // Initialize the type feedback to None. The current feedback will be
    // combined with the previous feedback.
    OverwriteFeedback(var_type_feedback, CompareOperationFeedback::kNone);
    loop_variable_list.push_back(var_type_feedback);
  }
  Label loop(this, loop_variable_list);
  Goto(&loop);
  BIND(&loop);
  {
    left = var_left.value();
    right = var_right.value();

    Label if_notsame(this);
    GotoIf(TaggedNotEqual(left, right), &if_notsame);
    {
      // {left} and {right} reference the exact same value, yet we need special
      // treatment for HeapNumber, as NaN is not equal to NaN.
      GenerateEqual_Same(left, &if_equal, &if_notequal, var_type_feedback);
    }

    BIND(&if_notsame);
    Label if_left_smi(this), if_left_not_smi(this);
    Branch(TaggedIsSmi(left), &if_left_smi, &if_left_not_smi);

    BIND(&if_left_smi);
    {
      Label if_right_smi(this), if_right_not_smi(this);
      CombineFeedback(var_type_feedback,
                      CompareOperationFeedback::kSignedSmall);
      Branch(TaggedIsSmi(right), &if_right_smi, &if_right_not_smi);

      BIND(&if_right_smi);
      {
        // We have already checked for {left} and {right} being the same value,
        // so when we get here they must be different Smis.
        Goto(&if_notequal);
      }

      BIND(&if_right_not_smi);
      {
        TNode<Map> right_map = LoadMap(CAST(right));
        Label if_right_heapnumber(this), if_right_oddball(this),
            if_right_bigint(this, Label::kDeferred),
            if_right_receiver(this, Label::kDeferred);
        GotoIf(IsHeapNumberMap(right_map), &if_right_heapnumber);

        // {left} is Smi and {right} is not HeapNumber or Smi.
        TNode<Uint16T> right_type = LoadMapInstanceType(right_map);
        GotoIf(IsStringInstanceType(right_type), &do_right_stringtonumber);
        GotoIf(IsOddballInstanceType(right_type), &if_right_oddball);
        GotoIf(IsBigIntInstanceType(right_type), &if_right_bigint);
        GotoIf(IsJSReceiverInstanceType(right_type), &if_right_receiver);
        CombineFeedback(var_type_feedback, CompareOperationFeedback::kAny);
        Goto(&if_notequal);

        BIND(&if_right_heapnumber);
        {
          CombineFeedback(var_type_feedback, CompareOperationFeedback::kNumber);
          var_left_float = SmiToFloat64(CAST(left));
          var_right_float = LoadHeapNumberValue(CAST(right));
          Goto(&do_float_comparison);
        }

        BIND(&if_right_oddball);
        {
          Label if_right_boolean(this);
          GotoIf(IsBooleanMap(right_map), &if_right_boolean);
          CombineFeedback(var_type_feedback,
                          CompareOperationFeedback::kOddball);
          Goto(&if_notequal);

          BIND(&if_right_boolean);
          {
            CombineFeedback(var_type_feedback,
                            CompareOperationFeedback::kBoolean);
            var_right =
                LoadObjectField(CAST(right), offsetof(Oddball, to_number_));
            Goto(&loop);
          }
        }

        BIND(&if_right_bigint);
        {
          CombineFeedback(var_type_feedback, CompareOperationFeedback::kBigInt);
          result = CAST(CallRuntime(Runtime::kBigIntEqualToNumber,
                                    NoContextConstant(), right, left));
          Goto(&end);
        }

        BIND(&if_right_receiver);
        {
          CombineFeedback(var_type_feedback,
                          CompareOperationFeedback::kReceiver);
          var_right = CallBuiltin(Builtins::NonPrimitiveToPrimitive(),
                                  context(), right);
          Goto(&loop);
        }
      }
    }

    BIND(&if_left_not_smi);
    {
      GotoIf(TaggedIsSmi(right), &use_symmetry);

      Label if_left_symbol(this), if_left_number(this),
          if_left_string(this, Label::kDeferred),
          if_left_bigint(this, Label::kDeferred), if_left_oddball(this),
          if_left_receiver(this);

      TNode<Map> left_map = LoadMap(CAST(left));
      TNode<Map> right_map = LoadMap(CAST(right));
      TNode<Uint16T> left_type = LoadMapInstanceType(left_map);
      TNode<Uint16T> right_type = LoadMapInstanceType(right_map);

      GotoIf(IsStringInstanceType(left_type), &if_left_string);
      GotoIf(IsSymbolInstanceType(left_type), &if_left_symbol);
      GotoIf(IsHeapNumberInstanceType(left_type), &if_left_number);
      GotoIf(IsOddballInstanceType(left_type), &if_left_oddball);
      Branch(IsBigIntInstanceType(left_type), &if_left_bigint,
             &if_left_receiver);

      BIND(&if_left_string);
      {
        GotoIfNot(IsStringInstanceType(right_type), &use_symmetry);
        Label combine_feedback(this);
        BranchIfStringEqual(CAST(left), CAST(right), &combine_feedback,
                            &combine_feedback, &result);
        BIND(&combine_feedback);
        {
          CombineFeedback(var_type_feedback,
                          SmiOr(CollectFeedbackForString(left_type),
                                CollectFeedbackForString(right_type)));
          Goto(&end);
        }
      }

      BIND(&if_left_number);
      {
        Label if_right_not_number(this);

        CombineFeedback(var_type_feedback, CompareOperationFeedback::kNumber);
        GotoIf(Word32NotEqual(left_type, right_type), &if_right_not_number);

        var_left_float = LoadHeapNumberValue(CAST(left));
        var_right_float = LoadHeapNumberValue(CAST(right));
        Goto(&do_float_comparison);

        BIND(&if_right_not_number);
        {
          Label if_right_oddball(this);

          GotoIf(IsStringInstanceType(right_type), &do_right_stringtonumber);
          GotoIf(IsOddballInstanceType(right_type), &if_right_oddball);
          GotoIf(IsBigIntInstanceType(right_type), &use_symmetry);
          GotoIf(IsJSReceiverInstanceType(right_type), &use_symmetry);
          CombineFeedback(var_type_feedback, CompareOperationFeedback::kAny);
          Goto(&if_notequal);

          BIND(&if_right_oddball);
          {
            Label if_right_boolean(this);
            GotoIf(IsBooleanMap(right_map), &if_right_boolean);
            CombineFeedback(var_type_feedback,
                            CompareOperationFeedback::kOddball);
            Goto(&if_notequal);

            BIND(&if_right_boolean);
            {
              CombineFeedback(var_type_feedback,
                              CompareOperationFeedback::kBoolean);
              var_right =
                  LoadObjectField(CAST(right), offsetof(Oddball, to_number_));
              Goto(&loop);
            }
          }
        }
      }

      BIND(&if_left_bigint);
      {
        Label if_right_heapnumber(this), if_right_bigint(this),
            if_right_string(this), if_right_boolean(this);
        CombineFeedback(var_type_feedback, CompareOperationFeedback::kBigInt);

        GotoIf(IsHeapNumberMap(right_map), &if_right_heapnumber);
        GotoIf(IsBigIntInstanceType(right_type), &if_right_bigint);
        GotoIf(IsStringInstanceType(right_type), &if_right_string);
        GotoIf(IsBooleanMap(right_map), &if_right_boolean);
        Branch(IsJSReceiverInstanceType(right_type), &use_symmetry,
               &if_notequal);

        BIND(&if_right_heapnumber);
        {
          CombineFeedback(var_type_feedback, CompareOperationFeedback::kNumber);
          result = CAST(CallRuntime(Runtime::kBigIntEqualToNumber,
                                    NoContextConstant(), left, right));
          Goto(&end);
        }

        BIND(&if_right_bigint);
        {
          if (Is64()) {
            Label if_both_bigint(this);
            GotoIfLargeBigInt(CAST(left), &if_both_bigint);
            GotoIfLargeBigInt(CAST(right), &if_both_bigint);

            OverwriteFeedback(var_type_feedback,
                              CompareOperationFeedback::kBigInt64);
            BigInt64Comparison(Operation::kEqual, left, right, &if_equal,
                               &if_notequal);
            BIND(&if_both_bigint);
          }

          CombineFeedback(var_type_feedback, CompareOperationFeedback::kBigInt);
          result = CAST(CallBuiltin(Builtin::kBigIntEqual, NoContextConstant(),
                                    left, right));
          Goto(&end);
        }

        BIND(&if_right_string);
        {
          CombineFeedback(var_type_feedback, CompareOperationFeedback::kString);
          result = CAST(CallRuntime(Runtime::kBigIntEqualToString,
                                    NoContextConstant(), left, right));
          Goto(&end);
        }

        BIND(&if_right_boolean);
        {
          CombineFeedback(var_type_feedback,
                          CompareOperationFeedback::kBoolean);
          var_right =
              LoadObjectField(CAST(right), offsetof(Oddball, to_number_));
          Goto(&loop);
        }
      }

      BIND(&if_left_oddball);
      {
        Label if_left_boolean(this), if_left_not_boolean(this);
        GotoIf(IsBooleanMap(left_map), &if_left_boolean);
        if (var_type_feedback != nullptr) {
          CombineFeedback(var_type_feedback,
                          CompareOperationFeedback::kNullOrUndefined);
          GotoIf(IsUndetectableMap(left_map), &if_left_not_boolean);
        }
        Goto(&if_left_not_boolean);

        BIND(&if_left_not_boolean);
        {
          // {left} is either Null or Undefined. Check if {right} is
          // undetectable (which includes Null and Undefined).
          Label if_right_undetectable(this), if_right_number(this),
              if_right_oddball(this),
              if_right_not_number_or_oddball_or_undetectable(this);
          GotoIf(IsUndetectableMap(right_map), &if_right_undetectable);
          GotoIf(IsHeapNumberInstanceType(right_type), &if_right_number);
          GotoIf(IsOddballInstanceType(right_type), &if_right_oddball);
          Goto(&if_right_not_number_or_oddball_or_undetectable);

          BIND(&if_right_undetectable);
          {
            // If {right} is undetectable, it must be either also
            // Null or Undefined, or a Receiver (aka document.all).
            CombineFeedback(
                var_type_feedback,
                CompareOperationFeedback::kReceiverOrNullOrUndefined);
            Goto(&if_equal);
          }

          BIND(&if_right_number);
          {
            CombineFeedback(var_type_feedback,
                            CompareOperationFeedback::kNumber);
            Goto(&if_notequal);
          }

          BIND(&if_right_oddball);
          {
            CombineFeedback(var_type_feedback,
                            CompareOperationFeedback::kOddball);
            Goto(&if_notequal);
          }

          BIND(&if_right_not_number_or_oddball_or_undetectable);
          {
            if (var_type_feedback != nullptr) {
              // Track whether {right} is Null, Undefined or Receiver.
              CombineFeedback(
                  var_type_feedback,
                  CompareOperationFeedback::kReceiverOrNullOrUndefined);
              GotoIf(IsJSReceiverInstanceType(right_type), &if_notequal);
              CombineFeedback(var_type_feedback,
                              CompareOperationFeedback::kAny);
            }
            Goto(&if_notequal);
          }
        }

        BIND(&if_left_boolean);
        {
          CombineFeedback(var_type_feedback,
                          CompareOperationFeedback::kBoolean);

          // If {right} is a Boolean too, it must be a different Boolean.
          GotoIf(TaggedEqual(right_map, left_map), &if_notequal);

          // Otherwise, convert {left} to number and try again.
          var_left = LoadObjectField(CAST(left), offsetof(Oddball, to_number_));
          Goto(&loop);
        }
      }

      BIND(&if_left_symbol);
      {
        Label if_right_receiver(this);
        GotoIf(IsJSReceiverInstanceType(right_type), &if_right_receiver);
        // {right} is not a JSReceiver and also not the same Symbol as {left},
        // so the result is "not equal".
        if (var_type_feedback != nullptr) {
          Label if_right_symbol(this);
          GotoIf(IsSymbolInstanceType(right_type), &if_right_symbol);
          *var_type_feedback = SmiConstant(CompareOperationFeedback::kAny);
          Goto(&if_notequal);

          BIND(&if_right_symbol);
          {
            CombineFeedback(var_type_feedback,
                            CompareOperationFeedback::kSymbol);
            Goto(&if_notequal);
          }
        } else {
          Goto(&if_notequal);
        }

        BIND(&if_right_receiver);
        {
          // {left} is a Primitive and {right} is a JSReceiver, so swapping
          // the order is not observable.
          if (var_type_feedback != nullptr) {
            *var_type_feedback = SmiConstant(CompareOperationFeedback::kAny);
          }
          Goto(&use_symmetry);
        }
      }

      BIND(&if_left_receiver);
      {
        CSA_DCHECK(this, IsJSReceiverInstanceType(left_type));
        Label if_right_receiver(this), if_right_not_receiver(this);
        Branch(IsJSReceiverInstanceType(right_type), &if_right_receiver,
               &if_right_not_receiver);

        BIND(&if_right_receiver);
        {
          // {left} and {right} are different JSReceiver references.
          CombineFeedback(var_type_feedback,
                          CompareOperationFeedback::kReceiver);
          Goto(&if_notequal);
        }

        BIND(&if_right_not_receiver);
        {
          // Check if {right} is undetectable, which means it must be Null
          // or Undefined, since we already ruled out Receiver for {right}.
          Label if_right_undetectable(this),
              if_right_not_undetectable(this, Label::kDeferred);
          Branch(IsUndetectableMap(right_map), &if_right_undetectable,
                 &if_right_not_undetectable);

          BIND(&if_right_undetectable);
          {
            // When we get here, {right} must be either Null or Undefined.
            CSA_DCHECK(this, IsNullOrUndefined(right));
            if (var_type_feedback != nullptr) {
              *var_type_feedback = SmiConstant(
                  CompareOperationFeedback::kReceiverOrNullOrUndefined);
            }
            Branch(IsUndetectableMap(left_map), &if_equal, &if_notequal);
          }

          BIND(&if_right_not_undetectable);
          {
            // {right} is a Primitive, and neither Null or Undefined;
            // convert {left} to Primitive too.
            CombineFeedback(var_type_feedback, CompareOperationFeedback::kAny);
            var_left = CallBuiltin(Builtins::NonPrimitiveToPrimitive(),
                                   context(), left);
            Goto(&loop);
          }
        }
      }
    }

    BIND(&do_right_stringtonumber);
    {
      if (var_type_feedback != nullptr) {
        TNode<Map> right_map = LoadMap(CAST(right));
        TNode<Uint16T> right_type = LoadMapInstanceType(right_map);
        CombineFeedback(var_type_feedback,
                        CollectFeedbackForString(right_type));
      }
      var_right = CallBuiltin(Builtin::kStringToNumber, context(), right);
      Goto(&loop);
    }

    BIND(&use_symmetry);
    {
      var_left = right;
      var_right = left;
      Goto(&loop);
    }
  }

  BIND(&do_float_comparison);
  {
    Branch(Float64Equal(var_left_float.value(), var_right_float.value()),
           &if_equal, &if_notequal);
  }

  BIND(&if_equal);
  {
    result = TrueConstant();
    Goto(&end);
  }

  BIND(&if_notequal);
  {
    result = FalseConstant();
    Goto(&end);
  }

  BIND(&end);
  return result.value();
}

TNode<Boolean> CodeStubAssembler::StrictEqual(
    TNode<Object> lhs, TNode<Object> rhs, TVariable<Smi>* var_type_feedback) {
  // Pseudo-code for the algorithm below:
  //
  // if (lhs == rhs) {
  //   if (lhs->IsHeapNumber()) return Cast<HeapNumber>(lhs)->value() != NaN;
  //   return true;
  // }
  // if (!IsSmi(lhs)) {
  //   if (lhs->IsHeapNumber()) {
  //     if (IsSmi(rhs)) {
  //       return Smi::ToInt(rhs) == Cast<HeapNumber>(lhs)->value();
  //     } else if (rhs->IsHeapNumber()) {
  //       return Cast<HeapNumber>(rhs)->value() ==
  //       Cast<HeapNumber>(lhs)->value();
  //     } else {
  //       return false;
  //     }
  //   } else {
  //     if (IsSmi(rhs)) {
  //       return false;
  //     } else {
  //       if (lhs->IsString()) {
  //         if (rhs->IsString()) {
  //           return %StringEqual(lhs, rhs);
  //         } else {
  //           return false;
  //         }
  //       } else if (lhs->IsBigInt()) {
  //         if (rhs->IsBigInt()) {
  //           return %BigIntEqualToBigInt(lhs, rhs);
  //         } else {
  //           return false;
  //         }
  //       } else {
  //         return false;
  //       }
  //     }
  //   }
  // } else {
  //   if (IsSmi(rhs)) {
  //     return false;
  //   } else {
  //     if (rhs->IsHeapNumber()) {
  //       return Smi::ToInt(lhs) == Cast<HeapNumber>(rhs)->value();
  //     } else {
  //       return false;
  //     }
  //   }
  // }

  Label if_equal(this), if_notequal(this), if_not_equivalent_types(this),
      end(this);
  TVARIABLE(Boolean, result);

  OverwriteFeedback(var_type_feedback, CompareOperationFeedback::kNone);

  // Check if {lhs} and {rhs} refer to the same object.
  Label if_same(this), if_notsame(this);
  Branch(TaggedEqual(lhs, rhs), &if_same, &if_notsame);

  BIND(&if_same);
  {
    // The {lhs} and {rhs} reference the exact same value, yet we need special
    // treatment for HeapNumber, as NaN is not equal to NaN.
    GenerateEqual_Same(lhs, &if_equal, &if_notequal, var_type_feedback);
  }

  BIND(&if_notsame);
  {
    // The {lhs} and {rhs} reference different objects, yet for Smi, HeapNumber,
    // BigInt and String they can still be considered equal.

    // Check if {lhs} is a Smi or a HeapObject.
    Label if_lhsissmi(this), if_lhsisnotsmi(this);
    Branch(TaggedIsSmi(lhs), &if_lhsissmi, &if_lhsisnotsmi);

    BIND(&if_lhsisnotsmi);
    {
      // Load the map of {lhs}.
      TNode<Map> lhs_map = LoadMap(CAST(lhs));

      // Check if {lhs} is a HeapNumber.
      Label if_lhsisnumber(this), if_lhsisnotnumber(this);
      Branch(IsHeapNumberMap(lhs_map), &if_lhsisnumber, &if_lhsisnotnumber);

      BIND(&if_lhsisnumber);
      {
        // Check if {rhs} is a Smi or a HeapObject.
        Label if_rhsissmi(this), if_rhsisnotsmi(this);
        Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

        BIND(&if_rhsissmi);
        {
          // Convert {lhs} and {rhs} to floating point values.
          TNode<Float64T> lhs_value = LoadHeapNumberValue(CAST(lhs));
          TNode<Float64T> rhs_value = SmiToFloat64(CAST(rhs));

          CombineFeedback(var_type_feedback, CompareOperationFeedback::kNumber);

          // Perform a floating point comparison of {lhs} and {rhs}.
          Branch(Float64Equal(lhs_value, rhs_value), &if_equal, &if_notequal);
        }

        BIND(&if_rhsisnotsmi);
        {
          TNode<HeapObject> rhs_ho = CAST(rhs);
          // Load the map of {rhs}.
          TNode<Map> rhs_map = LoadMap(rhs_ho);

          // Check if {rhs} is also a HeapNumber.
          Label if_rhsisnumber(this), if_rhsisnotnumber(this);
          Branch(IsHeapNumberMap(rhs_map), &if_rhsisnumber, &if_rhsisnotnumber);

          BIND(&if_rhsisnumber);
          {
            // Convert {lhs} and {rhs} to floating point values.
            TNode<Float64T> lhs_value = LoadHeapNumberValue(CAST(lhs));
            TNode<Float64T> rhs_value = LoadHeapNumberValue(CAST(rhs));

            CombineFeedback(var_type_feedback,
                            CompareOperationFeedback::kNumber);

            // Perform a floating point comparison of {lhs} and {rhs}.
            Branch(Float64Equal(lhs_value, rhs_value), &if_equal, &if_notequal);
          }

          BIND(&if_rhsisnotnumber);
          Goto(&if_not_equivalent_types);
        }
      }

      BIND(&if_lhsisnotnumber);
      {
        // Check if {rhs} is a Smi or a HeapObject.
        Label if_rhsissmi(this), if_rhsisnotsmi(this);
        Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

        BIND(&if_rhsissmi);
        Goto(&if_not_equivalent_types);

        BIND(&if_rhsisnotsmi);
        {
          // Load the instance type of {lhs}.
          TNode<Uint16T> lhs_instance_type = LoadMapInstanceType(lhs_map);

          // Check if {lhs} is a String.
          Label if_lhsisstring(this, Label::kDeferred), if_lhsisnotstring(this);
          Branch(IsStringInstanceType(lhs_instance_type), &if_lhsisstring,
                 &if_lhsisnotstring);

          BIND(&if_lhsisstring);
          {
            // Load the instance type of {rhs}.
            TNode<Uint16T> rhs_instance_type = LoadInstanceType(CAST(rhs));

            // Check if {rhs} is also a String.
            Label if_rhsisstring(this, Label::kDeferred),
                if_rhsisnotstring(this);
            Branch(IsStringInstanceType(rhs_instance_type), &if_rhsisstring,
                   &if_rhsisnotstring);

            BIND(&if_rhsisstring);
            {
              if (var_type_feedback != nullptr) {
                TNode<Smi> lhs_feedback =
                    CollectFeedbackForString(lhs_instance_type);
                TNode<Smi> rhs_feedback =
                    CollectFeedbackForString(rhs_instance_type);
                *var_type_feedback = SmiOr(lhs_feedback, rhs_feedback);
              }
              BranchIfStringEqual(CAST(lhs), CAST(rhs), &end, &end, &result);
            }

            BIND(&if_rhsisnotstring);
            Goto(&if_not_equivalent_types);
          }

          BIND(&if_lhsisnotstring);
          {
            // Check if {lhs} is a BigInt.
            Label if_lhsisbigint(this), if_lhsisnotbigint(this);
            Branch(IsBigIntInstanceType(lhs_instance_type), &if_lhsisbigint,
                   &if_lhsisnotbigint);

            BIND(&if_lhsisbigint);
            {
              // Load the instance type of {rhs}.
              TNode<Uint16T> rhs_instance_type = LoadInstanceType(CAST(rhs));

              // Check if {rhs} is also a BigInt.
              Label if_rhsisbigint(this, Label::kDeferred),
                  if_rhsisnotbigint(this);
              Branch(IsBigIntInstanceType(rhs_instance_type), &if_rhsisbigint,
                     &if_rhsisnotbigint);

              BIND(&if_rhsisbigint);
              {
                if (Is64()) {
                  Label if_both_bigint(this);
                  GotoIfLargeBigInt(CAST(lhs), &if_both_bigint);
                  GotoIfLargeBigInt(CAST(rhs), &if_both_bigint);

                  OverwriteFeedback(var_type_feedback,
                                    CompareOperationFeedback::kBigInt64);
                  BigInt64Comparison(Operation::kStrictEqual, lhs, rhs,
                                     &if_equal, &if_notequal);
                  BIND(&if_both_bigint);
                }

                CombineFeedback(var_type_feedback,
                                CompareOperationFeedback::kBigInt);
                result = CAST(CallBuiltin(Builtin::kBigIntEqual,
                                          NoContextConstant(), lhs, rhs));
                Goto(&end);
              }

              BIND(&if_rhsisnotbigint);
              Goto(&if_not_equivalent_types);
            }

            BIND(&if_lhsisnotbigint);
            if (var_type_feedback != nullptr) {
              // Load the instance type of {rhs}.
              TNode<Map> rhs_map = LoadMap(CAST(rhs));
              TNode<Uint16T> rhs_instance_type = LoadMapInstanceType(rhs_map);

              Label if_lhsissymbol(this), if_lhsisreceiver(this),
                  if_lhsisoddball(this);
              GotoIf(IsJSReceiverInstanceType(lhs_instance_type),
                     &if_lhsisreceiver);
              GotoIf(IsBooleanMap(lhs_map), &if_not_equivalent_types);
              GotoIf(IsOddballInstanceType(lhs_instance_type),
                     &if_lhsisoddball);
              Branch(IsSymbolInstanceType(lhs_instance_type), &if_lhsissymbol,
                     &if_not_equivalent_types);

              BIND(&if_lhsisreceiver);
              {
                GotoIf(IsBooleanMap(rhs_map), &if_not_equivalent_types);
                OverwriteFeedback(var_type_feedback,
                                  CompareOperationFeedback::kReceiver);
                GotoIf(IsJSReceiverInstanceType(rhs_instance_type),
                       &if_notequal);
                OverwriteFeedback(
                    var_type_feedback,
                    CompareOperationFeedback::kReceiverOrNullOrUndefined);
                GotoIf(IsOddballInstanceType(rhs_instance_type), &if_notequal);
                Goto(&if_not_equivalent_types);
              }

              BIND(&if_lhsisoddball);
              {
                  static_assert(LAST_PRIMITIVE_HEAP_OBJECT_TYPE ==
                                ODDBALL_TYPE);
                  GotoIf(Int32LessThan(rhs_instance_type,
                                       Int32Constant(ODDBALL_TYPE)),
                         &if_not_equivalent_types);

                  // TODO(marja): This is wrong, since null == true will be
                  // detected as ReceiverOrNullOrUndefined, but true is not
                  // receiver or null or undefined.
                  OverwriteFeedback(
                      var_type_feedback,
                      CompareOperationFeedback::kReceiverOrNullOrUndefined);
                  Goto(&if_notequal);
              }

              BIND(&if_lhsissymbol);
              {
                GotoIfNot(IsSymbolInstanceType(rhs_instance_type),
                          &if_not_equivalent_types);
                OverwriteFeedback(var_type_feedback,
                                  CompareOperationFeedback::kSymbol);
                Goto(&if_notequal);
              }
            } else {
              Goto(&if_notequal);
            }
          }
        }
      }
    }

    BIND(&if_lhsissmi);
    {
      // We already know that {lhs} and {rhs} are not reference equal, and {lhs}
      // is a Smi; so {lhs} and {rhs} can only be strictly equal if {rhs} is a
      // HeapNumber with an equal floating point value.

      // Check if {rhs} is a Smi or a HeapObject.
      Label if_rhsissmi(this), if_rhsisnotsmi(this);
      Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

      BIND(&if_rhsissmi);
      CombineFeedback(var_type_feedback,
                      CompareOperationFeedback::kSignedSmall);
      Goto(&if_notequal);

      BIND(&if_rhsisnotsmi);
      {
        // Load the map of the {rhs}.
        TNode<Map> rhs_map = LoadMap(CAST(rhs));

        // The {rhs} could be a HeapNumber with the same value as {lhs}.
        GotoIfNot(IsHeapNumberMap(rhs_map), &if_not_equivalent_types);

        // Convert {lhs} and {rhs} to floating point values.
        TNode<Float64T> lhs_value = SmiToFloat64(CAST(lhs));
        TNode<Float64T> rhs_value = LoadHeapNumberValue(CAST(rhs));

        CombineFeedback(var_type_feedback, CompareOperationFeedback::kNumber);

        // Perform a floating point comparison of {lhs} and {rhs}.
        Branch(Float64Equal(lhs_value, rhs_value), &if_equal, &if_notequal);
      }
    }
  }

  BIND(&if_equal);
  {
    result = TrueConstant();
    Goto(&end);
  }

  BIND(&if_not_equivalent_types);
  {
    OverwriteFeedback(var_type_feedback, CompareOperationFeedback::kAny);
    Goto(&if_notequal);
  }

  BIND(&if_notequal);
  {
    result = FalseConstant();
    Goto(&end);
  }

  BIND(&end);
  return result.value();
}

void CodeStubAssembler::BranchIfStringEqual(TNode<String> lhs,
                                            TNode<IntPtrT> lhs_length,
                                            TNode<String> rhs,
                                            TNode<IntPtrT> rhs_length,
                                            Label* if_true, Label* if_false,
                                            TVariable<Boolean>* result) {
  // Callers must handle the case where {lhs} and {rhs} refer to the same
  // String object.
  CSA_DCHECK(this, TaggedNotEqual(lhs, rhs));

  Label length_equal(this), length_not_equal(this);
  Branch(IntPtrEqual(lhs_length, rhs_length), &length_equal, &length_not_equal);

  BIND(&length_not_equal);
  {
    if (result != nullptr) *result = FalseConstant();
    Goto(if_false);
  }

  BIND(&length_equal);
  {
    TNode<Boolean> value = CAST(CallBuiltin(
        Builtin::kStringEqual, NoContextConstant(), lhs, rhs, lhs_length));
    if (result != nullptr) {
      *result = value;
    }
    if (if_true == if_false) {
      Goto(if_true);
    } else {
      Branch(TaggedEqual(value, TrueConstant()), if_true, if_false);
    }
  }
}

// ECMA#sec-samevalue
// This algorithm differs from the Strict Equality Comparison Algorithm in its
// treatment of signed zeroes and NaNs.
void CodeStubAssembler::BranchIfSameValue(TNode<Object> lhs, TNode<Object> rhs,
                                          Label* if_true, Label* if_false,
                                          SameValueMode mode) {
  TVARIABLE(Float64T, var_lhs_value);
  TVARIABLE(Float64T, var_rhs_value);
  Label do_fcmp(this);

  // Immediately jump to {if_true} if {lhs} == {rhs}, because - unlike
  // StrictEqual - SameValue considers two NaNs to be equal.
  GotoIf(TaggedEqual(lhs, rhs), if_true);

  // Check if the {lhs} is a Smi.
  Label if_lhsissmi(this), if_lhsisheapobject(this);
  Branch(TaggedIsSmi(lhs), &if_lhsissmi, &if_lhsisheapobject);

  BIND(&if_lhsissmi);
  {
    // Since {lhs} is a Smi, the comparison can only yield true
    // iff the {rhs} is a HeapNumber with the same float64 value.
    Branch(TaggedIsSmi(rhs), if_false, [&] {
      GotoIfNot(IsHeapNumber(CAST(rhs)), if_false);
      var_lhs_value = SmiToFloat64(CAST(lhs));
      var_rhs_value = LoadHeapNumberValue(CAST(rhs));
      Goto(&do_fcmp);
    });
  }

  BIND(&if_lhsisheapobject);
  {
    // Check if the {rhs} is a Smi.
    Branch(
        TaggedIsSmi(rhs),
        [&] {
          // Since {rhs} is a Smi, the comparison can only yield true
          // iff the {lhs} is a HeapNumber with the same float64 value.
          GotoIfNot(IsHeapNumber(CAST(lhs)), if_false);
          var_lhs_value = LoadHeapNumberValue(CAST(lhs));
          var_rhs_value = SmiToFloat64(CAST(rhs));
          Goto(&do_fcmp);
        },
        [&] {
          // Now this can only yield true if either both {lhs} and {rhs} are
          // HeapNumbers with the same value, or both are Strings with the
          // same character sequence, or both are BigInts with the same
          // value.
          Label if_lhsisheapnumber(this), if_lhsisstring(this),
              if_lhsisbigint(this);
          const TNode<Map> lhs_map = LoadMap(CAST(lhs));
          GotoIf(IsHeapNumberMap(lhs_map), &if_lhsisheapnumber);
          if (mode != SameValueMode::kNumbersOnly) {
            const TNode<Uint16T> lhs_instance_type =
                LoadMapInstanceType(lhs_map);
            GotoIf(IsStringInstanceType(lhs_instance_type), &if_lhsisstring);
            GotoIf(IsBigIntInstanceType(lhs_instance_type), &if_lhsisbigint);
          }
          Goto(if_false);

          BIND(&if_lhsisheapnumber);
          {
            GotoIfNot(IsHeapNumber(CAST(rhs)), if_false);
            var_lhs_value = LoadHeapNumberValue(CAST(lhs));
            var_rhs_value = LoadHeapNumberValue(CAST(rhs));
            Goto(&do_fcmp);
          }

          if (mode != SameValueMode::kNumbersOnly) {
            BIND(&if_lhsisstring);
            {
              // Now we can only yield true if {rhs} is also a String
              // with the same sequence of characters.
              GotoIfNot(IsString(CAST(rhs)), if_false);
              BranchIfStringEqual(CAST(lhs), CAST(rhs), if_true, if_false);
            }

            BIND(&if_lhsisbigint);
            {
              GotoIfNot(IsBigInt(CAST(rhs)), if_false);
              const TNode<Object> result = CallRuntime(
                  Runtime::kBigIntEqualToBigInt, NoContextConstant(), lhs, rhs);
              Branch(IsTrue(result), if_true, if_false);
            }
          }
        });
  }

  BIND(&do_fcmp);
  {
    TNode<Float64T> lhs_value = UncheckedCast<Float64T>(var_lhs_value.value());
    TNode<Float64T> rhs_value = UncheckedCast<Float64T>(var_rhs_value.value());
    BranchIfSameNumberValue(lhs_value, rhs_value, if_true, if_false);
  }
}

void CodeStubAssembler::BranchIfSameNumberValue(TNode<Float64T> lhs_value,
                                                TNode<Float64T> rhs_value,
                                                Label* if_true,
                                                Label* if_false) {
  Label if_equal(this), if_notequal(this);
  Branch(Float64Equal(lhs_value, rhs_value), &if_equal, &if_notequal);

  BIND(&if_equal);
  {
    // We still need to handle the case when {lhs} and {rhs} are -0.0 and
    // 0.0 (or vice versa). Compare the high word to
    // distinguish between the two.
    const TNode<Uint32T> lhs_hi_word = Float64ExtractHighWord32(lhs_value);
    const TNode<Uint32T> rhs_hi_word = Float64ExtractHighWord32(rhs_value);

    // If x is +0 and y is -0, return false.
    // If x is -0 and y is +0, return false.
    Branch(Word32Equal(lhs_hi_word, rhs_hi_word), if_true, if_false);
  }

  BIND(&if_notequal);
  {
    // Return true iff both {rhs} and {lhs} are NaN.
    GotoIf(Float64Equal(lhs_value, lhs_value), if_false);
    Branch(Float64Equal(rhs_value, rhs_value), if_false, if_true);
  }
}

TNode<Boolean> CodeStubAssembler::HasProperty(TNode<Context> context,
                                              TNode<Object> object,
                                              TNode<Object> key,
                                              HasPropertyLookupMode mode) {
  Label call_runtime(this, Label::kDeferred), return_true(this),
      return_false(this), end(this), if_proxy(this, Label::kDeferred);

  CodeStubAssembler::LookupPropertyInHolder lookup_property_in_holder =
      [this, &return_true](
          TNode<HeapObject> receiver, TNode<HeapObject> holder,
          TNode<Map> holder_map, TNode<Int32T> holder_instance_type,
          TNode<Name> unique_name, Label* next_holder, Label* if_bailout) {
        TryHasOwnProperty(holder, holder_map, holder_instance_type, unique_name,
                          &return_true, next_holder, if_bailout);
      };

  CodeStubAssembler::LookupElementInHolder lookup_element_in_holder =
      [this, &return_true, &return_false](
          TNode<HeapObject> receiver, TNode<HeapObject> holder,
          TNode<Map> holder_map, TNode<Int32T> holder_instance_type,
          TNode<IntPtrT> index, Label* next_holder, Label* if_bailout) {
        TryLookupElement(holder, holder_map, holder_instance_type, index,
                         &return_true, &return_false, next_holder, if_bailout);
      };

  const bool kHandlePrivateNames = mode == HasPropertyLookupMode::kHasProperty;
  TryPrototypeChainLookup(object, object, key, lookup_property_in_holder,
                          lookup_element_in_holder, &return_false,
                          &call_runtime, &if_proxy, kHandlePrivateNames);

  TVARIABLE(Boolean, result);

  BIND(&if_proxy);
  {
    TNode<Name> name = CAST(CallBuiltin(Builtin::kToName, context, key));
    switch (mode) {
      case kHasProperty:
        GotoIf(IsPrivateSymbol(name), &call_runtime);

        result = CAST(
            CallBuiltin(Builtin::kProxyHasProperty, context, object, name));
        Goto(&end);
        break;
      case kForInHasProperty:
        Goto(&call_runtime);
        break;
    }
  }

  BIND(&return_true);
  {
    result = TrueConstant();
    Goto(&end);
  }

  BIND(&return_false);
  {
    result = FalseConstant();
    Goto(&end);
  }

  BIND(&call_runtime);
  {
    Runtime::FunctionId fallback_runtime_function_id;
    switch (mode) {
      case kHasProperty:
        fallback_runtime_function_id = Runtime::kHasProperty;
        break;
      case kForInHasProperty:
        fallback_runtime_function_id = Runtime::kForInHasProperty;
        break;
    }

    result =
        CAST(CallRuntime(fallback_runtime_function_id, context, object, key));
    Goto(&end);
  }

  BIND(&end);
  CSA_DCHECK(this, IsBoolean(result.value()));
  return result.value();
}

void CodeStubAssembler::ForInPrepare(TNode<HeapObject> enumerator,
                                     TNode<UintPtrT> slot,
                                     TNode<HeapObject> maybe_feedback_vector,
                                     TNode<FixedArray>* cache_array_out,
                                     TNode<Smi>* cache_length_out,
                                     UpdateFeedbackMode update_feedback_mode) {
  // Check if we're using an enum cache.
  TVARIABLE(FixedArray, cache_array);
  TVARIABLE(Smi, cache_length);
  Label if_fast(this), if_slow(this, Label::kDeferred), out(this);
  Branch(IsMap(enumerator), &if_fast, &if_slow);

  BIND(&if_fast);
  {
    // Load the enumeration length and cache from the {enumerator}.
    TNode<Map> map_enumerator = CAST(enumerator);
    TNode<Uint32T> enum_length = LoadMapEnumLength(map_enumerator);
    CSA_DCHECK(this, Word32NotEqual(enum_length,
                                    Uint32Constant(kInvalidEnumCacheSentinel)));
    TNode<DescriptorArray> descriptors = LoadMapDescriptors(map_enumerator);
    TNode<EnumCache> enum_cache = LoadObjectField<EnumCache>(
        descriptors, DescriptorArray::kEnumCacheOffset);
    TNode<FixedArray> enum_keys =
        LoadObjectField<FixedArray>(enum_cache, EnumCache::kKeysOffset);

    // Check if we have enum indices available.
    TNode<FixedArray> enum_indices =
        LoadObjectField<FixedArray>(enum_cache, EnumCache::kIndicesOffset);
    TNode<Uint32T> enum_indices_length =
        LoadAndUntagFixedArrayBaseLengthAsUint32(enum_indices);
    TNode<Smi> feedback = SelectSmiConstant(
        Uint32LessThanOrEqual(enum_length, enum_indices_length),
        static_cast<int>(ForInFeedback::kEnumCacheKeysAndIndices),
        static_cast<int>(ForInFeedback::kEnumCacheKeys));
    UpdateFeedback(feedback, maybe_feedback_vector, slot, update_feedback_mode);

    cache_array = enum_keys;
    cache_length = SmiFromUint32(enum_length);
    Goto(&out);
  }

  BIND(&if_slow);
  {
    // The {enumerator} is a FixedArray with all the keys to iterate.
    TNode<FixedArray> array_enumerator = CAST(enumerator);

    // Record the fact that we hit the for-in slow-path.
    UpdateFeedback(SmiConstant(ForInFeedback::kAny), maybe_feedback_vector,
                   slot, update_feedback_mode);

    cache_array = array_enumerator;
    cache_length = LoadFixedArrayBaseLength(array_enumerator);
    Goto(&out);
  }

  BIND(&out);
  *cache_array_out = cache_array.value();
  *cache_length_out = cache_length.value();
}

TNode<String> CodeStubAssembler::Typeof(
    TNode<Object> value, std::optional<TNode<UintPtrT>> slot_id,
    std::optional<TNode<HeapObject>> maybe_feedback_vector) {
  TVARIABLE(String, result_var);

  Label return_number(this, Label::kDeferred), if_oddball(this),
      return_function(this), return_undefined(this), return_object(this),
      return_string(this), return_bigint(this), return_symbol(this),
      return_result(this);

  GotoIf(TaggedIsSmi(value), &return_number);

  TNode<HeapObject> value_heap_object = CAST(value);
  TNode<Map> map = LoadMap(value_heap_object);

  GotoIf(IsHeapNumberMap(map), &return_number);

  TNode<Uint16T> instance_type = LoadMapInstanceType(map);

  GotoIf(InstanceTypeEqual(instance_type, ODDBALL_TYPE), &if_oddball);

  TNode<Int32T> callable_or_undetectable_mask =
      Word32And(LoadMapBitField(map),
                Int32Constant(Map::Bits1::IsCallableBit::kMask |
                              Map::Bits1::IsUndetectableBit::kMask));

  GotoIf(Word32Equal(callable_or_undetectable_mask,
                     Int32Constant(Map::Bits1::IsCallableBit::kMask)),
         &return_function);

  GotoIfNot(Word32Equal(callable_or_undetectable_mask, Int32Constant(0)),
            &return_undefined);

  GotoIf(IsJSReceiverInstanceType(instance_type), &return_object);

  GotoIf(IsStringInstanceType(instance_type), &return_string);

  GotoIf(IsBigIntInstanceType(instance_type), &return_bigint);

  GotoIf(IsSymbolInstanceType(instance_type), &return_symbol);

  Abort(AbortReason::kUnexpectedInstanceType);

  auto UpdateFeedback = [&](TypeOfFeedback::Result feedback) {
    if (maybe_feedback_vector.has_value()) {
      MaybeUpdateFeedback(SmiConstant(feedback), *maybe_feedback_vector,
                          *slot_id);
    }
  };
  BIND(&return_number);
  {
    result_var = HeapConstantNoHole(isolate()->factory()->number_string());
    UpdateFeedback(TypeOfFeedback::kNumber);
    Goto(&return_result);
  }

  BIND(&if_oddball);
  {
    TNode<String> type =
        CAST(LoadObjectField(value_heap_object, offsetof(Oddball, type_of_)));
    result_var = type;
    UpdateFeedback(TypeOfFeedback::kAny);
    Goto(&return_result);
  }

  BIND(&return_function);
  {
    result_var = HeapConstantNoHole(isolate()->factory()->function_string());
    UpdateFeedback(TypeOfFeedback::kFunction);
    Goto(&return_result);
  }

  BIND(&return_undefined);
  {
    result_var = HeapConstantNoHole(isolate()->factory()->undefined_string());
    UpdateFeedback(TypeOfFeedback::kAny);
    Goto(&return_result);
  }

  BIND(&return_object);
  {
    result_var = HeapConstantNoHole(isolate()->factory()->object_string());
    UpdateFeedback(TypeOfFeedback::kAny);
    Goto(&return_result);
  }

  BIND(&return_string);
  {
    result_var = HeapConstantNoHole(isolate()->factory()->string_string());
    UpdateFeedback(TypeOfFeedback::kString);
    Goto(&return_result);
  }

  BIND(&return_bigint);
  {
    result_var = HeapConstantNoHole(isolate()->factory()->bigint_string());
    UpdateFeedback(TypeOfFeedback::kAny);
    Goto(&return_result);
  }

  BIND(&return_symbol);
  {
    result_var = HeapConstantNoHole(isolate()->factory()->symbol_string());
    UpdateFeedback(TypeOfFeedback::kAny);
    Goto(&return_result);
  }

  BIND(&return_result);
  return result_var.value();
}

TNode<HeapObject> CodeStubAssembler::GetSuperConstructor(
    TNode<JSFunction> active_function) {
  TNode<Map> map = LoadMap(active_function);
  return LoadMapPrototype(map);
}

void CodeStubAssembler::FindNonDefaultConstructor(
    TNode<JSFunction> this_function, TVariable<Object>& constructor,
    Label* found_default_base_ctor, Label* found_something_else) {
  Label loop(this, &constructor);

  constructor = GetSuperConstructor(this_function);

  // Disable the optimization if the debugger is active, so that we can still
  // put breakpoints into default constructors.
  GotoIf(IsDebugActive(), found_something_else);

  // Disable the optimization if the array iterator has been changed. V8 uses
  // the array iterator for the spread in default ctors, even though it
  // shouldn't, according to the spec. This ensures that omitting default ctors
  // doesn't change the behavior. See crbug.com/v8/13249.
  GotoIf(IsArrayIteratorProtectorCellInvalid(), found_something_else);

  Goto(&loop);

  BIND(&loop);
  {
    // We know constructor can't be a SMI, since it's a prototype. If it's not a
    // JSFunction, the error will be thrown by the ThrowIfNotSuperConstructor
    // which follows this bytecode.
    GotoIfNot(IsJSFunction(CAST(constructor.value())), found_something_else);

    // If there are class fields, bail out. TODO(v8:13091): Handle them here.
    const TNode<SharedFunctionInfo> shared_function_info =
        LoadObjectField<SharedFunctionInfo>(
            CAST(constructor.value()), JSFunction::kSharedFunctionInfoOffset);
    const TNode<Uint32T> has_class_fields =
        DecodeWord32<SharedFunctionInfo::RequiresInstanceMembersInitializerBit>(
            LoadObjectField<Uint32T>(shared_function_info,
                                     SharedFunctionInfo::kFlagsOffset));

    GotoIf(Word32NotEqual(has_class_fields, Int32Constant(0)),
           found_something_else);

    // If there are private methods, bail out. TODO(v8:13091): Handle them here.
    TNode<Context> function_context =
        LoadJSFunctionContext(CAST(constructor.value()));
    TNode<ScopeInfo> scope_info = LoadScopeInfo(function_context);
    GotoIf(LoadScopeInfoClassScopeHasPrivateBrand(scope_info),
           found_something_else);

    const TNode<Uint32T> function_kind =
        LoadFunctionKind(CAST(constructor.value()));
    // A default base ctor -> stop the search.
    GotoIf(Word32Equal(
               function_kind,
               static_cast<uint32_t>(FunctionKind::kDefaultBaseConstructor)),
           found_default_base_ctor);

    // Something else than a default derived ctor (e.g., a non-default base
    // ctor, a non-default derived ctor, or a normal function) -> stop the
    // search.
    GotoIfNot(Word32Equal(function_kind,
                          static_cast<uint32_t>(
                              FunctionKind::kDefaultDerivedConstructor)),
              found_something_else);

    constructor = GetSuperConstructor(CAST(constructor.value()));

    Goto(&loop);
  }
  // We don't need to re-check the proctector, since the loop cannot call into
  // user code. Even if GetSuperConstructor returns a Proxy, we will throw since
  // it's not a constructor, and not invoke [[GetPrototypeOf]] on it.
  // TODO(v8:13091): make sure this is still valid after we handle class fields.
}

TNode<JSReceiver> CodeStubAssembler::SpeciesConstructor(
    TNode<Context> context, TNode<Object> object,
    TNode<JSReceiver> default_constructor) {
  Isolate* isolate = this->isolate();
  TVARIABLE(JSReceiver, var_result, default_constructor);

  // 2. Let C be ? Get(O, "constructor").
  TNode<Object> constructor =
      GetProperty(context, object, isolate->factory()->constructor_string());

  // 3. If C is undefined, return defaultConstructor.
  Label out(this);
  GotoIf(IsUndefined(constructor), &out);

  // 4. If Type(C) is not Object, throw a TypeError exception.
  ThrowIfNotJSReceiver(context, constructor,
                       MessageTemplate::kConstructorNotReceiver, "");

  // 5. Let S be ? Get(C, @@species).
  TNode<Object> species =
      GetProperty(context, constructor, isolate->factory()->species_symbol());

  // 6. If S is either undefined or null, return defaultConstructor.
  GotoIf(IsNullOrUndefined(species), &out);

  // 7. If IsConstructor(S) is true, return S.
  Label throw_error(this);
  GotoIf(TaggedIsSmi(species), &throw_error);
  GotoIfNot(IsConstructorMap(LoadMap(CAST(species))), &throw_error);
  var_result = CAST(species);
  Goto(&out);

  // 8. Throw a TypeError exception.
  BIND(&throw_error);
  ThrowTypeError(context, MessageTemplate::kSpeciesNotConstructor);

  BIND(&out);
  return var_result.value();
}

TNode<Boolean> CodeStubAssembler::InstanceOf(TNode<Object> object,
                                             TNode<Object> callable,
                                             TNode<Context> context) {
  TVARIABLE(Boolean, var_result);
  Label if_notcallable(this, Label::kDeferred),
      if_notreceiver(this, Label::kDeferred), if_otherhandler(this),
      if_nohandler(this, Label::kDeferred), return_true(this),
      return_false(this), return_result(this, &var_result);

  // Ensure that the {callable} is actually a JSReceiver.
  GotoIf(TaggedIsSmi(callable), &if_notreceiver);
  GotoIfNot(IsJSReceiver(CAST(callable)), &if_notreceiver);

  // Load the @@hasInstance property from {callable}.
  TNode<Object> inst_of_handler =
      GetProperty(context, callable, HasInstanceSymbolConstant());

  // Optimize for the likely case where {inst_of_handler} is the builtin
  // Function.prototype[@@hasInstance] method, and emit a direct call in
  // that case without any additional checking.
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<JSFunction> function_has_instance = CAST(
      LoadContextElement(native_context, Context::FUNCTION_HAS_INSTANCE_INDEX));
  GotoIfNot(TaggedEqual(inst_of_handler, function_has_instance),
            &if_otherhandler);
  {
    // Call to Function.prototype[@@hasInstance] directly without using the
    // Builtins::Call().
    var_result = CAST(CallJS(Builtin::kFunctionPrototypeHasInstance, context,
                             inst_of_handler,
                             UndefinedConstant(),  // new_target
                             callable, object));
    Goto(&return_result);
  }

  BIND(&if_otherhandler);
  {
    // Check if there's actually an {inst_of_handler}.
    GotoIf(IsNull(inst_of_handler), &if_nohandler);
    GotoIf(IsUndefined(inst_of_handler), &if_nohandler);

    // Call the {inst_of_handler} for {callable} and {object}.
    TNode<Object> result =
        Call(context, inst_of_handler, ConvertReceiverMode::kNotNullOrUndefined,
             callable, object);

    // Convert the {result} to a Boolean.
    BranchIfToBooleanIsTrue(result, &return_true, &return_false);
  }

  BIND(&if_nohandler);
  {
    // Ensure that the {callable} is actually Callable.
    GotoIfNot(IsCallable(CAST(callable)), &if_notcallable);

    // Use the OrdinaryHasInstance algorithm.
    var_result = CAST(
        CallBuiltin(Builtin::kOrdinaryHasInstance, context, callable, object));
    Goto(&return_result);
  }

  BIND(&if_notcallable);
  { ThrowTypeError(context, MessageTemplate::kNonCallableInInstanceOfCheck); }

  BIND(&if_notreceiver);
  { ThrowTypeError(context, MessageTemplate::kNonObjectInInstanceOfCheck); }

  BIND(&return_true);
  var_result = TrueConstant();
  Goto(&return_result);

  BIND(&return_false);
  var_result = FalseConstant();
  Goto(&return_result);

  BIND(&return_result);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::NumberInc(TNode<Number> value) {
  TVARIABLE(Number, var_result);
  TVARIABLE(Float64T, var_finc_value);
  Label if_issmi(this), if_isnotsmi(this), do_finc(this), end(this);
  Branch(TaggedIsSmi(value), &if_issmi, &if_isnotsmi);

  BIND(&if_issmi);
  {
    Label if_overflow(this);
    TNode<Smi> smi_value = CAST(value);
    TNode<Smi> one = SmiConstant(1);
    var_result = TrySmiAdd(smi_value, one, &if_overflow);
    Goto(&end);

    BIND(&if_overflow);
    {
      var_finc_value = SmiToFloat64(smi_value);
      Goto(&do_finc);
    }
  }

  BIND(&if_isnotsmi);
  {
    TNode<HeapNumber> heap_number_value = CAST(value);

    // Load the HeapNumber value.
    var_finc_value = LoadHeapNumberValue(heap_number_value);
    Goto(&do_finc);
  }

  BIND(&do_finc);
  {
    TNode<Float64T> finc_value = var_finc_value.value();
    TNode<Float64T> one = Float64Constant(1.0);
    TNode<Float64T> finc_result = Float64Add(finc_value, one);
    var_result = AllocateHeapNumberWithValue(finc_result);
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::NumberDec(TNode<Number> value) {
  TVARIABLE(Number, var_result);
  TVARIABLE(Float64T, var_fdec_value);
  Label if_issmi(this), if_isnotsmi(this), do_fdec(this), end(this);
  Branch(TaggedIsSmi(value), &if_issmi, &if_isnotsmi);

  BIND(&if_issmi);
  {
    TNode<Smi> smi_value = CAST(value);
    TNode<Smi> one = SmiConstant(1);
    Label if_overflow(this);
    var_result = TrySmiSub(smi_value, one, &if_overflow);
    Goto(&end);

    BIND(&if_overflow);
    {
      var_fdec_value = SmiToFloat64(smi_value);
      Goto(&do_fdec);
    }
  }

  BIND(&if_isnotsmi);
  {
    TNode<HeapNumber> heap_number_value = CAST(value);

    // Load the HeapNumber value.
    var_fdec_value = LoadHeapNumberValue(heap_number_value);
    Goto(&do_fdec);
  }

  BIND(&do_fdec);
  {
    TNode<Float64T> fdec_value = var_fdec_value.value();
    TNode<Float64T> minus_one = Float64Constant(-1.0);
    TNode<Float64T> fdec_result = Float64Add(fdec_value, minus_one);
    var_result = AllocateHeapNumberWithValue(fdec_result);
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::NumberAdd(TNode<Number> a, TNode<Number> b) {
  TVARIABLE(Number, var_result);
  Label float_add(this, Label::kDeferred), end(this);
  GotoIf(TaggedIsNotSmi(a), &float_add);
  GotoIf(TaggedIsNotSmi(b), &float_add);

  // Try fast Smi addition first.
  var_result = TrySmiAdd(CAST(a), CAST(b), &float_add);
  Goto(&end);

  BIND(&float_add);
  {
    var_result = ChangeFloat64ToTagged(
        Float64Add(ChangeNumberToFloat64(a), ChangeNumberToFloat64(b)));
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

TNode<Number> CodeStubAssembler::NumberSub(TNode<Number> a, TNode<Number> b) {
  TVARIABLE(Number, var_result);
  Label float_sub(this, Label::kDeferred), end(this);
  GotoIf(TaggedIsNotSmi(a), &float_sub);
  GotoIf(TaggedIsNotSmi(b), &float_sub);

  // Try fast Smi subtraction first.
  var_result = TrySmiSub(CAST(a), CAST(b), &float_sub);
  Goto(&end);

  BIND(&float_sub);
  {
    var_result = ChangeFloat64ToTagged(
        Float64Sub(ChangeNumberToFloat64(a), ChangeNumberToFloat64(b)));
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

void CodeStubAssembler::GotoIfNotNumber(TNode<Object> input,
                                        Label* is_not_number) {
  Label is_number(this);
  GotoIf(TaggedIsSmi(input), &is_number);
  Branch(IsHeapNumber(CAST(input)), &is_number, is_not_number);
  BIND(&is_number);
}

void CodeStubAssembler::GotoIfNumber(TNode<Object> input, Label* is_number) {
  GotoIf(TaggedIsSmi(input), is_number);
  GotoIf(IsHeapNumber(CAST(input)), is_number);
}

TNode<Word32T> CodeStubAssembler::NormalizeShift32OperandIfNecessary(
    TNode<Word32T> right32) {
  TVARIABLE(Word32T, result, right32);
  Label done(this);
  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  GotoIf(UniqueInt32Constant(Word32ShiftIsSafe()), &done);
  {
    result = Word32And(right32, Int32Constant(0x1F));
    Goto(&done);
  }
  BIND(&done);
  return result.value();
}

TNode<Number> CodeStubAssembler::BitwiseOp(TNode<Word32T> left32,
                                           TNode<Word32T> right32,
                                           Operation bitwise_op) {
  switch (bitwise_op) {
    case Operation::kBitwiseAnd:
      return ChangeInt32ToTagged(Signed(Word32And(left32, right32)));
    case Operation::kBitwiseOr:
      return ChangeInt32ToTagged(Signed(Word32Or(left32, right32)));
    case Operation::kBitwiseXor:
      return ChangeInt32ToTagged(Signed(Word32Xor(left32, right32)));
    case Operation::kShiftLeft:
      right32 = NormalizeShift32OperandIfNecessary(right32);
      return ChangeInt32ToTagged(Signed(Word32Shl(left32, right32)));
    case Operation::kShiftRight:
      right32 = NormalizeShift32OperandIfNecessary(right32);
      return ChangeInt32ToTagged(Signed(Word32Sar(left32, right32)));
    case Operation::kShiftRightLogical:
      right32 = NormalizeShift32OperandIfNecessary(right32);
      return ChangeUint32ToTagged(Unsigned(Word32Shr(left32, right32)));
    default:
      break;
  }
  UNREACHABLE();
}

TNode<Number> CodeStubAssembler::BitwiseSmiOp(TNode<Smi> left, TNode<Smi> right,
                                              Operation bitwise_op) {
  switch (bitwise_op) {
    case Operation::kBitwiseAnd:
      return SmiAnd(left, right);
    case Operation::kBitwiseOr:
      return SmiOr(left, right);
    case Operation::kBitwiseXor:
      return SmiXor(left, right);
    // Smi shift left and logical shift rihgt can have (Heap)Number output, so
    // perform int32 operation.
    case Operation::kShiftLeft:
    case Operation::kShiftRightLogical:
      return BitwiseOp(SmiToInt32(left), SmiToInt32(right), bitwise_op);
    // Arithmetic shift right of a Smi can't overflow to the heap number, so
    // perform int32 operation but don't check for overflow.
    case Operation::kShiftRight: {
      TNode<Int32T> left32 = SmiToInt32(left);
      TNode<Int32T> right32 =
          Signed(NormalizeShift32OperandIfNecessary(SmiToInt32(right)));
      return ChangeInt32ToTaggedNoOverflow(Word32Sar(left32, right32));
    }
    default:
      break;
  }
  UNREACHABLE();
}

TNode<JSObject> CodeStubAssembler::AllocateJSIteratorResult(
    TNode<Context> context, TNode<Object> value, TNode<Boolean> done) {
  CSA_DCHECK(this, IsBoolean(done));
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> map = CAST(
      LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX));
  TNode<HeapObject> result = Allocate(JSIteratorResult::kSize);
  StoreMapNoWriteBarrier(result, map);
  StoreObjectFieldRoot(result, JSIteratorResult::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(result, JSIteratorResult::kElementsOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldNoWriteBarrier(result, JSIteratorResult::kValueOffset, value);
  StoreObjectFieldNoWriteBarrier(result, JSIteratorResult::kDoneOffset, done);
  return CAST(result);
}

TNode<JSObject> CodeStubAssembler::AllocateJSIteratorResultForEntry(
    TNode<Context> context, TNode<Object> key, TNode<Object> value) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Smi> length = SmiConstant(2);
  int const elements_size = FixedArray::SizeFor(2);
  TNode<FixedArray> elements =
      UncheckedCast<FixedArray>(Allocate(elements_size));
  StoreObjectFieldRoot(elements, FixedArray::kMapOffset,
                       RootIndex::kFixedArrayMap);
  StoreObjectFieldNoWriteBarrier(elements, FixedArray::kLengthOffset, length);
  StoreFixedArrayElement(elements, 0, key);
  StoreFixedArrayElement(elements, 1, value);
  TNode<Map> array_map = CAST(LoadContextElement(
      native_context, Context::JS_ARRAY_PACKED_ELEMENTS_MAP_INDEX));
  TNode<HeapObject> array =
      Allocate(ALIGN_TO_ALLOCATION_ALIGNMENT(JSArray::kHeaderSize));
  StoreMapNoWriteBarrier(array, array_map);
  StoreObjectFieldRoot(array, JSArray::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldNoWriteBarrier(array, JSArray::kElementsOffset, elements);
  StoreObjectFieldNoWriteBarrier(array, JSArray::kLengthOffset, length);
  TNode<Map> iterator_map = CAST(
      LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX));
  TNode<HeapObject> result = Allocate(JSIteratorResult::kSize);
  StoreMapNoWriteBarrier(result, iterator_map);
  StoreObjectFieldRoot(result, JSIteratorResult::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(result, JSIteratorResult::kElementsOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldNoWriteBarrier(result, JSIteratorResult::kValueOffset, array);
  StoreObjectFieldRoot(result, JSIteratorResult::kDoneOffset,
                       RootIndex::kFalseValue);
  return CAST(result);
}

TNode<JSObject> CodeStubAssembler::AllocatePromiseWithResolversResult(
    TNode<Context> context, TNode<Object> promise, TNode<Object> resolve,
    TNode<Object> reject) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> map = CAST(LoadContextElement(
      native_context, Context::PROMISE_WITHRESOLVERS_RESULT_MAP_INDEX));
  TNode<HeapObject> result = Allocate(JSPromiseWithResolversResult::kSize);
  StoreMapNoWriteBarrier(result, map);
  StoreObjectFieldRoot(result,
                       JSPromiseWithResolversResult::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(result, JSPromiseWithResolversResult::kElementsOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldNoWriteBarrier(
      result, JSPromiseWithResolversResult::kPromiseOffset, promise);
  StoreObjectFieldNoWriteBarrier(
      result, JSPromiseWithResolversResult::kResolveOffset, resolve);
  StoreObjectFieldNoWriteBarrier(
      result, JSPromiseWithResolversResult::kRejectOffset, reject);
  return CAST(result);
}

TNode<JSReceiver> CodeStubAssembler::ArraySpeciesCreate(TNode<Context> context,
                                                        TNode<Object> o,
                                                        TNode<Number> len) {
  TNode<JSReceiver> constructor =
      CAST(CallRuntime(Runtime::kArraySpeciesConstructor, context, o));
  return Construct(context, constructor, len);
}

void CodeStubAssembler::ThrowIfArrayBufferIsDetached(
    TNode<Context> context, TNode<JSArrayBuffer> array_buffer,
    const char* method_name) {
  Label if_detached(this, Label::kDeferred), if_not_detached(this);
  Branch(IsDetachedBuffer(array_buffer), &if_detached, &if_not_detached);
  BIND(&if_detached);
  ThrowTypeError(context, MessageTemplate::kDetachedOperation, method_name);
  BIND(&if_not_detached);
}

void CodeStubAssembler::ThrowIfArrayBufferViewBufferIsDetached(
    TNode<Context> context, TNode<JSArrayBufferView> array_buffer_view,
    const char* method_name) {
  TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(array_buffer_view);
  ThrowIfArrayBufferIsDetached(context, buffer, method_name);
}

TNode<UintPtrT> CodeStubAssembler::LoadJSArrayBufferByteLength(
    TNode<JSArrayBuffer> array_buffer) {
  return LoadBoundedSizeFromObject(array_buffer,
                                   JSArrayBuffer::kRawByteLengthOffset);
}

TNode<UintPtrT> CodeStubAssembler::LoadJSArrayBufferMaxByteLength(
    TNode<JSArrayBuffer> array_buffer) {
  return LoadBoundedSizeFromObject(array_buffer,
                                   JSArrayBuffer::kRawMaxByteLengthOffset);
}

TNode<RawPtrT> CodeStubAssembler::LoadJSArrayBufferBackingStorePtr(
    TNode<JSArrayBuffer> array_buffer) {
  return LoadSandboxedPointerFromObject(array_buffer,
                                        JSArrayBuffer::kBackingStoreOffset);
}

TNode<JSArrayBuffer> CodeStubAssembler::LoadJSArrayBufferViewBuffer(
    TNode<JSArrayBufferView> array_buffer_view) {
  return LoadObjectField<JSArrayBuffer>(array_buffer_view,
                                        JSArrayBufferView::kBufferOffset);
}

TNode<UintPtrT> CodeStubAssembler::LoadJSArrayBufferViewByteLength(
    TNode<JSArrayBufferView> array_buffer_view) {
  return LoadBoundedSizeFromObject(array_buffer_view,
                                   JSArrayBufferView::kRawByteLengthOffset);
}

void CodeStubAssembler::StoreJSArrayBufferViewByteLength(
    TNode<JSArrayBufferView> array_buffer_view, TNode<UintPtrT> value) {
  StoreBoundedSizeToObject(array_buffer_view,
                           JSArrayBufferView::kRawByteLengthOffset, value);
}

TNode<UintPtrT> CodeStubAssembler::LoadJSArrayBufferViewByteOffset(
    TNode<JSArrayBufferView> array_buffer_view) {
  return LoadBoundedSizeFromObject(array_buffer_view,
                                   JSArrayBufferView::kRawByteOffsetOffset);
}

void CodeStubAssembler::StoreJSArrayBufferViewByteOffset(
    TNode<JSArrayBufferView> array_buffer_view, TNode<UintPtrT> value) {
  StoreBoundedSizeToObject(array_buffer_view,
                           JSArrayBufferView::kRawByteOffsetOffset, value);
}

TNode<UintPtrT> CodeStubAssembler::LoadJSTypedArrayLength(
    TNode<JSTypedArray> typed_array) {
  return LoadBoundedSizeFromObject(typed_array, JSTypedArray::kRawLengthOffset);
}

void CodeStubAssembler::StoreJSTypedArrayLength(TNode<JSTypedArray> typed_array,
                                                TNode<UintPtrT> value) {
  StoreBoundedSizeToObject(typed_array, JSTypedArray::kRawLengthOffset, value);
}

TNode<UintPtrT> CodeStubAssembler::LoadJSTypedArrayLengthAndCheckDetached(
    TNode<JSTypedArray> typed_array, Label* detached) {
  TVARIABLE(UintPtrT, result);
  TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(typed_array);

  Label variable_length(this), fixed_length(this), end(this);
  Branch(IsVariableLengthJSArrayBufferView(typed_array), &variable_length,
         &fixed_length);
  BIND(&variable_length);
  {
    result =
        LoadVariableLengthJSTypedArrayLength(typed_array, buffer, detached);
    Goto(&end);
  }

  BIND(&fixed_length);
  {
    Label not_detached(this);
    Branch(IsDetachedBuffer(buffer), detached, &not_detached);
    BIND(&not_detached);
    result = LoadJSTypedArrayLength(typed_array);
    Goto(&end);
  }
  BIND(&end);
  return result.value();
}

// ES #sec-integerindexedobjectlength
TNode<UintPtrT> CodeStubAssembler::LoadVariableLengthJSTypedArrayLength(
    TNode<JSTypedArray> array, TNode<JSArrayBuffer> buffer,
    Label* detached_or_out_of_bounds) {
  // byte_length already takes array's offset into account.
  TNode<UintPtrT> byte_length = LoadVariableLengthJSArrayBufferViewByteLength(
      array, buffer, detached_or_out_of_bounds);
  TNode<IntPtrT> element_size =
      RabGsabElementsKindToElementByteSize(LoadElementsKind(array));
  return Unsigned(IntPtrDiv(Signed(byte_length), element_size));
}

TNode<UintPtrT>
CodeStubAssembler::LoadVariableLengthJSArrayBufferViewByteLength(
    TNode<JSArrayBufferView> array, TNode<JSArrayBuffer> buffer,
    Label* detached_or_out_of_bounds) {
  Label is_gsab(this), is_rab(this), end(this);
  TVARIABLE(UintPtrT, result);
  TNode<UintPtrT> array_byte_offset = LoadJSArrayBufferViewByteOffset(array);

  Branch(IsSharedArrayBuffer(buffer), &is_gsab, &is_rab);
  BIND(&is_gsab);
  {
    // Non-length-tracking GSAB-backed ArrayBufferViews shouldn't end up here.
    CSA_DCHECK(this, IsLengthTrackingJSArrayBufferView(array));
    // Read the byte length from the BackingStore.
    const TNode<ExternalReference> byte_length_function =
        ExternalConstant(ExternalReference::gsab_byte_length());
    TNode<ExternalReference> isolate_ptr =
        ExternalConstant(ExternalReference::isolate_address());
    TNode<UintPtrT> buffer_byte_length = UncheckedCast<UintPtrT>(
        CallCFunction(byte_length_function, MachineType::UintPtr(),
                      std::make_pair(MachineType::Pointer(), isolate_ptr),
                      std::make_pair(MachineType::AnyTagged(), buffer)));
    // Since the SharedArrayBuffer can't shrink, and we've managed to create
    // this JSArrayBufferDataView without throwing an exception, we know that
    // buffer_byte_length >= array_byte_offset.
    CSA_CHECK(this,
              UintPtrGreaterThanOrEqual(buffer_byte_length, array_byte_offset));
    result = UintPtrSub(buffer_byte_length, array_byte_offset);
    Goto(&end);
  }

  BIND(&is_rab);
  {
    GotoIf(IsDetachedBuffer(buffer), detached_or_out_of_bounds);

    TNode<UintPtrT> buffer_byte_length = LoadJSArrayBufferByteLength(buffer);

    Label is_length_tracking(this), not_length_tracking(this);
    Branch(IsLengthTrackingJSArrayBufferView(array), &is_length_tracking,
           &not_length_tracking);

    BIND(&is_length_tracking);
    {
      // The backing RAB might have been shrunk so that the start of the
      // TypedArray is already out of bounds.
      GotoIfNot(UintPtrLessThanOrEqual(array_byte_offset, buffer_byte_length),
                detached_or_out_of_bounds);
      result = UintPtrSub(buffer_byte_length, array_byte_offset);
      Goto(&end);
    }

    BIND(&not_length_tracking);
    {
      // Check if the backing RAB has shrunk so that the buffer is out of
      // bounds.
      TNode<UintPtrT> array_byte_length =
          LoadJSArrayBufferViewByteLength(array);
      GotoIfNot(UintPtrGreaterThanOrEqual(
                    buffer_byte_length,
                    UintPtrAdd(array_byte_offset, array_byte_length)),
                detached_or_out_of_bounds);
      result = array_byte_length;
      Goto(&end);
    }
  }
  BIND(&end);
  return result.value();
}

void CodeStubAssembler::IsJSArrayBufferViewDetachedOrOutOfBounds(
    TNode<JSArrayBufferView> array_buffer_view, Label* detached_or_oob,
    Label* not_detached_nor_oob) {
  TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(array_buffer_view);

  GotoIf(IsDetachedBuffer(buffer), detached_or_oob);
  GotoIfNot(IsVariableLengthJSArrayBufferView(array_buffer_view),
            not_detached_nor_oob);
  GotoIf(IsSharedArrayBuffer(buffer), not_detached_nor_oob);

  {
    TNode<UintPtrT> buffer_byte_length = LoadJSArrayBufferByteLength(buffer);
    TNode<UintPtrT> array_byte_offset =
        LoadJSArrayBufferViewByteOffset(array_buffer_view);

    Label length_tracking(this), not_length_tracking(this);
    Branch(IsLengthTrackingJSArrayBufferView(array_buffer_view),
           &length_tracking, &not_length_tracking);

    BIND(&length_tracking);
    {
      // The backing RAB might have been shrunk so that the start of the
      // TypedArray is already out of bounds.
      Branch(UintPtrLessThanOrEqual(array_byte_offset, buffer_byte_length),
             not_detached_nor_oob, detached_or_oob);
    }

    BIND(&not_length_tracking);
    {
      // Check if the backing RAB has shrunk so that the buffer is out of
      // bounds.
      TNode<UintPtrT> array_byte_length =
          LoadJSArrayBufferViewByteLength(array_buffer_view);
      Branch(UintPtrGreaterThanOrEqual(
                 buffer_byte_length,
                 UintPtrAdd(array_byte_offset, array_byte_length)),
             not_detached_nor_oob, detached_or_oob);
    }
  }
}

TNode<BoolT> CodeStubAssembler::IsJSArrayBufferViewDetachedOrOutOfBoundsBoolean(
    TNode<JSArrayBufferView> array_buffer_view) {
  Label is_detached_or_out_of_bounds(this),
      not_detached_nor_out_of_bounds(this), end(this);
  TVARIABLE(BoolT, result);

  IsJSArrayBufferViewDetachedOrOutOfBounds(array_buffer_view,
                                           &is_detached_or_out_of_bounds,
                                           &not_detached_nor_out_of_bounds);
  BIND(&is_detached_or_out_of_bounds);
  {
    result = BoolConstant(true);
    Goto(&end);
  }
  BIND(&not_detached_nor_out_of_bounds);
  {
    result = BoolConstant(false);
    Goto(&end);
  }
  BIND(&end);
  return result.value();
}

void CodeStubAssembler::CheckJSTypedArrayIndex(
    TNode<JSTypedArray> typed_array, TNode<UintPtrT> index,
    Label* detached_or_out_of_bounds) {
  TNode<UintPtrT> len = LoadJSTypedArrayLengthAndCheckDetached(
      typed_array, detached_or_out_of_bounds);

  GotoIf(UintPtrGreaterThanOrEqual(index, len), detached_or_out_of_bounds);
}

// ES #sec-integerindexedobjectbytelength
TNode<UintPtrT> CodeStubAssembler::LoadVariableLengthJSTypedArrayByteLength(
    TNode<Context> context, TNode<JSTypedArray> array,
    TNode<JSArrayBuffer> buffer) {
  Label miss(this), end(this);
  TVARIABLE(UintPtrT, result);

  TNode<UintPtrT> length =
      LoadVariableLengthJSTypedArrayLength(array, buffer, &miss);
  TNode<IntPtrT> element_size =
      RabGsabElementsKindToElementByteSize(LoadElementsKind(array));
  // Conversion to signed is OK since length < JSArrayBuffer::kMaxByteLength.
  TNode<IntPtrT> byte_length = IntPtrMul(Signed(length), element_size);
  result = Unsigned(byte_length);
  Goto(&end);
  BIND(&miss);
  {
    result = UintPtrConstant(0);
    Goto(&end);
  }
  BIND(&end);
  return result.value();
}

TNode<IntPtrT> CodeStubAssembler::RabGsabElementsKindToElementByteSize(
    TNode<Int32T> elements_kind) {
  TVARIABLE(IntPtrT, result);
  Label elements_8(this), elements_16(this), elements_32(this),
      elements_64(this), not_found(this), end(this);
  int32_t elements_kinds[] = {
      RAB_GSAB_UINT8_ELEMENTS,    RAB_GSAB_UINT8_CLAMPED_ELEMENTS,
      RAB_GSAB_INT8_ELEMENTS,     RAB_GSAB_UINT16_ELEMENTS,
      RAB_GSAB_INT16_ELEMENTS,    RAB_GSAB_FLOAT16_ELEMENTS,
      RAB_GSAB_UINT32_ELEMENTS,   RAB_GSAB_INT32_ELEMENTS,
      RAB_GSAB_FLOAT32_ELEMENTS,  RAB_GSAB_FLOAT64_ELEMENTS,
      RAB_GSAB_BIGINT64_ELEMENTS, RAB_GSAB_BIGUINT64_ELEMENTS};
  Label* elements_kind_labels[] = {&elements_8,  &elements_8,  &elements_8,
                                   &elements_16, &elements_16, &elements_16,
                                   &elements_32, &elements_32, &elements_32,
                                   &elements_64, &elements_64, &elements_64};
  const size_t kTypedElementsKindCount =
      LAST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND -
      FIRST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND + 1;
  DCHECK_EQ(kTypedElementsKindCount, arraysize(elements_kinds));
  DCHECK_EQ(kTypedElementsKindCount, arraysize(elements_kind_labels));
  Switch(elements_kind, &not_found, elements_kinds, elements_kind_labels,
         kTypedElementsKindCount);
  BIND(&elements_8);
  {
    result = IntPtrConstant(1);
    Goto(&end);
  }
  BIND(&elements_16);
  {
    result = IntPtrConstant(2);
    Goto(&end);
  }
  BIND(&elements_32);
  {
    result = IntPtrConstant(4);
    Goto(&end);
  }
  BIND(&elements_64);
  {
    result = IntPtrConstant(8);
    Goto(&end);
  }
  BIND(&not_found);
  { Unreachable(); }
  BIND(&end);
  return result.value();
}

TNode<JSArrayBuffer> CodeStubAssembler::GetTypedArrayBuffer(
    TNode<Context> context, TNode<JSTypedArray> array) {
  Label call_runtime(this), done(this);
  TVARIABLE(Object, var_result);

  GotoIf(IsOnHeapTypedArray(array), &call_runtime);

  TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(array);
  GotoIf(IsDetachedBuffer(buffer), &call_runtime);
  var_result = buffer;
  Goto(&done);

  BIND(&call_runtime);
  {
    var_result = CallRuntime(Runtime::kTypedArrayGetBuffer, context, array);
    Goto(&done);
  }

  BIND(&done);
  return CAST(var_result.value());
}

CodeStubArguments::CodeStubArguments(CodeStubAssembler* assembler,
                                     TNode<IntPtrT> argc, TNode<RawPtrT> fp)
    : assembler_(assembler),
      argc_(argc),
      base_(),
      fp_(fp != nullptr ? fp : assembler_->LoadFramePointer()) {
  TNode<IntPtrT> offset = assembler_->IntPtrConstant(
      (StandardFrameConstants::kFixedSlotCountAboveFp + 1) *
      kSystemPointerSize);
  DCHECK_NOT_NULL(argc_);
  // base_ points to the first argument, not the receiver
  // whether present or not.
  base_ = assembler_->RawPtrAdd(fp_, offset);
}

TNode<Object> CodeStubArguments::GetReceiver() const {
  intptr_t offset = -kSystemPointerSize;
  return assembler_->LoadFullTagged(base_, assembler_->IntPtrConstant(offset));
}

void CodeStubArguments::SetReceiver(TNode<Object> object) const {
  intptr_t offset = -kSystemPointerSize;
  assembler_->StoreFullTaggedNoWriteBarrier(
      base_, assembler_->IntPtrConstant(offset), object);
}

TNode<RawPtrT> CodeStubArguments::AtIndexPtr(TNode<IntPtrT> index) const {
  TNode<IntPtrT> offset =
      assembler_->ElementOffsetFromIndex(index, SYSTEM_POINTER_ELEMENTS, 0);
  return assembler_->RawPtrAdd(base_, offset);
}

TNode<Object> CodeStubArguments::AtIndex(TNode<IntPtrT> index) const {
  CSA_DCHECK(assembler_, assembler_->UintPtrOrSmiLessThan(
                             index, GetLengthWithoutReceiver()));
  return assembler_->LoadFullTagged(AtIndexPtr(index));
}

TNode<Object> CodeStubArguments::AtIndex(int index) const {
  return AtIndex(assembler_->IntPtrConstant(index));
}

TNode<IntPtrT> CodeStubArguments::GetLengthWithoutReceiver() const {
  return assembler_->IntPtrSub(
      argc_, assembler_->IntPtrConstant(kJSArgcReceiverSlots));
}

TNode<IntPtrT> CodeStubArguments::GetLengthWithReceiver() const {
  return argc_;
}

TNode<Object> CodeStubArguments::GetOptionalArgumentValue(
    TNode<IntPtrT> index, TNode<Object> default_value) {
  CodeStubAssembler::TVariable<Object> result(assembler_);
  CodeStubAssembler::Label argument_missing(assembler_),
      argument_done(assembler_, &result);

  assembler_->GotoIf(
      assembler_->UintPtrGreaterThanOrEqual(index, GetLengthWithoutReceiver()),
      &argument_missing);
  result = AtIndex(index);
  assembler_->Goto(&argument_done);

  assembler_->BIND(&argument_missing);
  result = default_value;
  assembler_->Goto(&argument_done);

  assembler_->BIND(&argument_done);
  return result.value();
}

void CodeStubArguments::SetArgumentValue(TNode<IntPtrT> index,
                                         TNode<Object> value) {
  TNode<RawPtrT> slot = AtIndexPtr(index);
  assembler_->StoreFullTaggedNoWriteBarrier(slot, value);
}

void CodeStubArguments::ForEach(
    const CodeStubAssembler::VariableList& vars,
    const CodeStubArguments::ForEachBodyFunction& body, TNode<IntPtrT> first,
    TNode<IntPtrT> last) const {
  assembler_->Comment("CodeStubArguments::ForEach");
  if (first == nullptr) {
    first = assembler_->IntPtrConstant(0);
  }
  if (last == nullptr) {
    last = GetLengthWithoutReceiver();
  }
  TNode<RawPtrT> start = AtIndexPtr(first);
  TNode<RawPtrT> end = AtIndexPtr(last);
  const int increment = kSystemPointerSize;
  assembler_->BuildFastLoop<RawPtrT>(
      vars, start, end,
      [&](TNode<RawPtrT> current) {
        TNode<Object> arg = assembler_->LoadFullTagged(current);
        body(arg);
      },
      increment, CodeStubAssembler::LoopUnrollingMode::kNo,
      CodeStubAssembler::IndexAdvanceMode::kPost);
}

void CodeStubArguments::PopAndReturn(TNode<Object> value) {
  TNode<IntPtrT> pop_count = GetLengthWithReceiver();
  assembler_->PopAndReturn(pop_count, value);
}

TNode<BoolT> CodeStubAssembler::IsFastElementsKind(
    TNode<Int32T> elements_kind) {
  static_assert(FIRST_ELEMENTS_KIND == FIRST_FAST_ELEMENTS_KIND);
  return Uint32LessThanOrEqual(elements_kind,
                               Int32Constant(LAST_FAST_ELEMENTS_KIND));
}

TNode<BoolT> CodeStubAssembler::IsFastPackedElementsKind(
    TNode<Int32T> elements_kind) {
  static_assert(FIRST_ELEMENTS_KIND == FIRST_FAST_ELEMENTS_KIND);
  // ElementsKind values that are even are packed. See
  // internal::IsFastPackedElementsKind.
  static_assert((~PACKED_SMI_ELEMENTS & 1) == 1);
  static_assert((~PACKED_ELEMENTS & 1) == 1);
  static_assert((~PACKED_DOUBLE_ELEMENTS & 1) == 1);
  return Word32And(IsNotSetWord32(elements_kind, 1),
                   IsFastElementsKind(elements_kind));
}

TNode<BoolT> CodeStubAssembler::IsFastOrNonExtensibleOrSealedElementsKind(
    TNode<Int32T> elements_kind) {
  static_assert(FIRST_ELEMENTS_KIND == FIRST_FAST_ELEMENTS_KIND);
  static_assert(LAST_FAST_ELEMENTS_KIND + 1 == PACKED_NONEXTENSIBLE_ELEMENTS);
  static_assert(PACKED_NONEXTENSIBLE_ELEMENTS + 1 ==
                HOLEY_NONEXTENSIBLE_ELEMENTS);
  static_assert(HOLEY_NONEXTENSIBLE_ELEMENTS + 1 == PACKED_SEALED_ELEMENTS);
  static_assert(PACKED_SEALED_ELEMENTS + 1 == HOLEY_SEALED_ELEMENTS);
  return Uint32LessThanOrEqual(elements_kind,
                               Int32Constant(HOLEY_SEALED_ELEMENTS));
}

TNode<BoolT> CodeStubAssembler::IsDoubleElementsKind(
    TNode<Int32T> elements_kind) {
  static_assert(FIRST_ELEMENTS_KIND == FIRST_FAST_ELEMENTS_KIND);
  static_assert((PACKED_DOUBLE_ELEMENTS & 1) == 0);
  static_assert(PACKED_DOUBLE_ELEMENTS + 1 == HOLEY_DOUBLE_ELEMENTS);
  return Word32Equal(Word32Shr(elements_kind, Int32Constant(1)),
                     Int32Constant(PACKED_DOUBLE_ELEMENTS / 2));
}

TNode<BoolT> CodeStubAssembler::IsFastSmiOrTaggedElementsKind(
    TNode<Int32T> elements_kind) {
  static_assert(FIRST_ELEMENTS_KIND == FIRST_FAST_ELEMENTS_KIND);
  static_assert(PACKED_DOUBLE_ELEMENTS > TERMINAL_FAST_ELEMENTS_KIND);
  static_assert(HOLEY_DOUBLE_ELEMENTS > TERMINAL_FAST_ELEMENTS_KIND);
  return Uint32LessThanOrEqual(elements_kind,
                               Int32Constant(TERMINAL_FAST_ELEMENTS_KIND));
}

TNode<BoolT> CodeStubAssembler::IsFastSmiElementsKind(
    TNode<Int32T> elements_kind) {
  return Uint32LessThanOrEqual(elements_kind,
                               Int32Constant(HOLEY_SMI_ELEMENTS));
}

TNode<BoolT> CodeStubAssembler::IsHoleyFastElementsKind(
    TNode<Int32T> elements_kind) {
  CSA_DCHECK(this, IsFastElementsKind(elements_kind));

  static_assert(HOLEY_SMI_ELEMENTS == (PACKED_SMI_ELEMENTS | 1));
  static_assert(HOLEY_ELEMENTS == (PACKED_ELEMENTS | 1));
  static_assert(HOLEY_DOUBLE_ELEMENTS == (PACKED_DOUBLE_ELEMENTS | 1));
  return IsSetWord32(elements_kind, 1);
}

TNode<BoolT> CodeStubAssembler::IsHoleyFastElementsKindForRead(
    TNode<Int32T> elements_kind) {
  CSA_DCHECK(this, Uint32LessThanOrEqual(
                       elements_kind,
                       Int32Constant(LAST_ANY_NONEXTENSIBLE_ELEMENTS_KIND)));

  static_assert(HOLEY_SMI_ELEMENTS == (PACKED_SMI_ELEMENTS | 1));
  static_assert(HOLEY_ELEMENTS == (PACKED_ELEMENTS | 1));
  static_assert(HOLEY_DOUBLE_ELEMENTS == (PACKED_DOUBLE_ELEMENTS | 1));
  static_assert(HOLEY_NONEXTENSIBLE_ELEMENTS ==
                (PACKED_NONEXTENSIBLE_ELEMENTS | 1));
  static_assert(HOLEY_SEALED_ELEMENTS == (PACKED_SEALED_ELEMENTS | 1));
  static_assert(HOLEY_FROZEN_ELEMENTS == (PACKED_FROZEN_ELEMENTS | 1));
  return IsSetWord32(elements_kind, 1);
}

TNode<BoolT> CodeStubAssembler::IsElementsKindGreaterThan(
    TNode<Int32T> target_kind, ElementsKind reference_kind) {
  return Int32GreaterThan(target_kind, Int32Constant(reference_kind));
}

TNode<BoolT> CodeStubAssembler::IsElementsKindGreaterThanOrEqual(
    TNode<Int32T> target_kind, ElementsKind reference_kind) {
  return Int32GreaterThanOrEqual(target_kind, Int32Constant(reference_kind));
}

TNode<BoolT> CodeStubAssembler::IsElementsKindLessThanOrEqual(
    TNode<Int32T> target_kind, ElementsKind reference_kind) {
  return Int32LessThanOrEqual(target_kind, Int32Constant(reference_kind));
}

TNode<Int32T> CodeStubAssembler::GetNonRabGsabElementsKind(
    TNode<Int32T> elements_kind) {
  Label is_rab_gsab(this), end(this);
  TVARIABLE(Int32T, result);
  result = elements_kind;
  Branch(Int32GreaterThanOrEqual(elements_kind,
                                 Int32Constant(RAB_GSAB_UINT8_ELEMENTS)),
         &is_rab_gsab, &end);
  BIND(&is_rab_gsab);
  result = Int32Sub(elements_kind,
                    Int32Constant(RAB_GSAB_UINT8_ELEMENTS - UINT8_ELEMENTS));
  Goto(&end);
  BIND(&end);
  return result.value();
}

TNode<BoolT> CodeStubAssembler::IsDebugActive() {
  TNode<Uint8T> is_debug_active = Load<Uint8T>(
      ExternalConstant(ExternalReference::debug_is_active_address(isolate())));
  return Word32NotEqual(is_debug_active, Int32Constant(0));
}

TNode<BoolT> CodeStubAssembler::HasAsyncEventDelegate() {
  const TNode<RawPtrT> async_event_delegate = Load<RawPtrT>(ExternalConstant(
      ExternalReference::async_event_delegate_address(isolate())));
  return WordNotEqual(async_event_delegate, IntPtrConstant(0));
}

TNode<Uint32T> CodeStubAssembler::PromiseHookFlags() {
  return Load<Uint32T>(ExternalConstant(
    ExternalReference::promise_hook_flags_address(isolate())));
}

TNode<BoolT> CodeStubAssembler::IsAnyPromiseHookEnabled(TNode<Uint32T> flags) {
  uint32_t mask = Isolate::PromiseHookFields::HasContextPromiseHook::kMask |
                  Isolate::PromiseHookFields::HasIsolatePromiseHook::kMask;
  return IsSetWord32(flags, mask);
}

TNode<BoolT> CodeStubAssembler::IsIsolatePromiseHookEnabled(
    TNode<Uint32T> flags) {
  return IsSetWord32<Isolate::PromiseHookFields::HasIsolatePromiseHook>(flags);
}

#ifdef V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
TNode<BoolT> CodeStubAssembler::IsContextPromiseHookEnabled(
    TNode<Uint32T> flags) {
  return IsSetWord32<Isolate::PromiseHookFields::HasContextPromiseHook>(flags);
}
#endif

TNode<BoolT>
CodeStubAssembler::IsIsolatePromiseHookEnabledOrHasAsyncEventDelegate(
    TNode<Uint32T> flags) {
  uint32_t mask = Isolate::PromiseHookFields::HasIsolatePromiseHook::kMask |
                  Isolate::PromiseHookFields::HasAsyncEventDelegate::kMask;
  return IsSetWord32(flags, mask);
}

TNode<BoolT> CodeStubAssembler::
    IsIsolatePromiseHookEnabledOrDebugIsActiveOrHasAsyncEventDelegate(
        TNode<Uint32T> flags) {
  uint32_t mask = Isolate::PromiseHookFields::HasIsolatePromiseHook::kMask |
                  Isolate::PromiseHookFields::HasAsyncEventDelegate::kMask |
                  Isolate::PromiseHookFields::IsDebugActive::kMask;
  return IsSetWord32(flags, mask);
}

TNode<BoolT> CodeStubAssembler::NeedsAnyPromiseHooks(TNode<Uint32T> flags) {
  return Word32NotEqual(flags, Int32Constant(0));
}

TNode<Code> CodeStubAssembler::LoadBuiltin(TNode<Smi> builtin_id) {
  CSA_DCHECK(this, SmiBelow(builtin_id, SmiConstant(Builtins::kBuiltinCount)));

  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(SmiToBInt(builtin_id), SYSTEM_POINTER_ELEMENTS);

  TNode<ExternalReference> table = IsolateField(IsolateFieldId::kBuiltinTable);

  return CAST(BitcastWordToTagged(Load<RawPtrT>(table, offset)));
}

#ifdef V8_ENABLE_LEAPTIERING
TNode<JSDispatchHandleT> CodeStubAssembler::LoadBuiltinDispatchHandle(
    Builtin builtin) {
  return LoadBuiltinDispatchHandle(
      SmiConstant(JSBuiltinDispatchHandleRoot::to_idx(builtin)));
}

TNode<JSDispatchHandleT> CodeStubAssembler::LoadBuiltinDispatchHandle(
    RootIndex root_idx) {
  return LoadBuiltinDispatchHandle(
      SmiConstant(JSBuiltinDispatchHandleRoot::to_idx(root_idx)));
}

TNode<JSDispatchHandleT> CodeStubAssembler::LoadBuiltinDispatchHandle(
    TNode<Smi> builtin_id) {
  CSA_DCHECK(this, SmiBelow(builtin_id, SmiConstant(Builtins::kBuiltinCount)));

  TNode<IntPtrT> offset =
      ElementOffsetFromIndex(SmiToBInt(builtin_id), PACKED_SMI_ELEMENTS);

  TNode<ExternalReference> table =
      IsolateField(IsolateFieldId::kBuiltinDispatchTable);

  return Load<JSDispatchHandleT>(table, offset);
}
#endif  // V8_ENABLE_LEAPTIERING

TNode<Code> CodeStubAssembler::GetSharedFunctionInfoCode(
    TNode<SharedFunctionInfo> shared_info, TVariable<Uint16T>* data_type_out,
    Label* if_compile_lazy) {

  Label done(this);
  Label use_untrusted_data(this);
  Label unknown_data(this);
  TVARIABLE(Code, sfi_code);

  TNode<Object> sfi_data = LoadSharedFunctionInfoTrustedData(shared_info);
  GotoIf(TaggedEqual(sfi_data, SmiConstant(0)), &use_untrusted_data);
  {
    TNode<Uint16T> data_type = LoadInstanceType(CAST(sfi_data));
    if (data_type_out) {
      *data_type_out = data_type;
    }

    int32_t case_values[] = {
        BYTECODE_ARRAY_TYPE,
        CODE_TYPE,
        INTERPRETER_DATA_TYPE,
        UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA_TYPE,
        UNCOMPILED_DATA_WITH_PREPARSE_DATA_TYPE,
        UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA_WITH_JOB_TYPE,
        UNCOMPILED_DATA_WITH_PREPARSE_DATA_AND_JOB_TYPE,
#if V8_ENABLE_WEBASSEMBLY
        WASM_CAPI_FUNCTION_DATA_TYPE,
        WASM_EXPORTED_FUNCTION_DATA_TYPE,
        WASM_JS_FUNCTION_DATA_TYPE,
#endif  // V8_ENABLE_WEBASSEMBLY
    };
    Label check_is_bytecode_array(this);
    Label check_is_baseline_data(this);
    Label check_is_interpreter_data(this);
    Label check_is_uncompiled_data(this);
    Label check_is_wasm_function_data(this);
    Label* case_labels[] = {
        &check_is_bytecode_array,     &check_is_baseline_data,
        &check_is_interpreter_data,   &check_is_uncompiled_data,
        &check_is_uncompiled_data,    &check_is_uncompiled_data,
        &check_is_uncompiled_data,
#if V8_ENABLE_WEBASSEMBLY
        &check_is_wasm_function_data, &check_is_wasm_function_data,
        &check_is_wasm_function_data,
#endif  // V8_ENABLE_WEBASSEMBLY
    };
    static_assert(arraysize(case_values) == arraysize(case_labels));
    Switch(data_type, &unknown_data, case_values, case_labels,
           arraysize(case_labels));

    // IsBytecodeArray: Interpret bytecode
    BIND(&check_is_bytecode_array);
    sfi_code =
        HeapConstantNoHole(BUILTIN_CODE(isolate(), InterpreterEntryTrampoline));
    Goto(&done);

    // IsBaselineData: Execute baseline code
    BIND(&check_is_baseline_data);
    {
      TNode<Code> baseline_code = CAST(sfi_data);
      sfi_code = baseline_code;
      Goto(&done);
    }

    // IsInterpreterData: Interpret bytecode
    BIND(&check_is_interpreter_data);
    {
      TNode<Code> trampoline = CAST(LoadProtectedPointerField(
          CAST(sfi_data), InterpreterData::kInterpreterTrampolineOffset));
      sfi_code = trampoline;
    }
    Goto(&done);

    // IsUncompiledDataWithPreparseData | IsUncompiledDataWithoutPreparseData:
    // Compile lazy
    BIND(&check_is_uncompiled_data);
    sfi_code = HeapConstantNoHole(BUILTIN_CODE(isolate(), CompileLazy));
    Goto(if_compile_lazy ? if_compile_lazy : &done);

#if V8_ENABLE_WEBASSEMBLY
    // IsWasmFunctionData: Use the wrapper code
    BIND(&check_is_wasm_function_data);
    sfi_code = CAST(LoadObjectField(
        CAST(sfi_data), WasmExportedFunctionData::kWrapperCodeOffset));
    Goto(&done);
#endif  // V8_ENABLE_WEBASSEMBLY
  }

  BIND(&use_untrusted_data);
  {
    sfi_data = LoadSharedFunctionInfoUntrustedData(shared_info);
    Label check_instance_type(this);

    // IsSmi: Is builtin
    GotoIf(TaggedIsNotSmi(sfi_data), &check_instance_type);
    if (data_type_out) {
      *data_type_out = Uint16Constant(0);
    }
    if (if_compile_lazy) {
      GotoIf(SmiEqual(CAST(sfi_data), SmiConstant(Builtin::kCompileLazy)),
             if_compile_lazy);
    }
    sfi_code = LoadBuiltin(CAST(sfi_data));
    Goto(&done);

    // Switch on data's instance type.
    BIND(&check_instance_type);
    TNode<Uint16T> data_type = LoadInstanceType(CAST(sfi_data));
    if (data_type_out) {
      *data_type_out = data_type;
    }

    int32_t case_values[] = {
        FUNCTION_TEMPLATE_INFO_TYPE,
#if V8_ENABLE_WEBASSEMBLY
        ASM_WASM_DATA_TYPE,
        WASM_RESUME_DATA_TYPE,
#endif  // V8_ENABLE_WEBASSEMBLY
    };
    Label check_is_function_template_info(this);
    Label check_is_asm_wasm_data(this);
    Label check_is_wasm_resume(this);
    Label* case_labels[] = {
        &check_is_function_template_info,
#if V8_ENABLE_WEBASSEMBLY
        &check_is_asm_wasm_data,
        &check_is_wasm_resume,
#endif  // V8_ENABLE_WEBASSEMBLY
    };
    static_assert(arraysize(case_values) == arraysize(case_labels));
    Switch(data_type, &unknown_data, case_values, case_labels,
           arraysize(case_labels));

    // IsFunctionTemplateInfo: API call
    BIND(&check_is_function_template_info);
    sfi_code =
        HeapConstantNoHole(BUILTIN_CODE(isolate(), HandleApiCallOrConstruct));
    Goto(&done);

#if V8_ENABLE_WEBASSEMBLY
    // IsAsmWasmData: Instantiate using AsmWasmData
    BIND(&check_is_asm_wasm_data);
    sfi_code = HeapConstantNoHole(BUILTIN_CODE(isolate(), InstantiateAsmJs));
    Goto(&done);

    // IsWasmResumeData: Resume the suspended wasm continuation.
    BIND(&check_is_wasm_resume);
    sfi_code = HeapConstantNoHole(BUILTIN_CODE(isolate(), WasmResume));
    Goto(&done);
#endif  // V8_ENABLE_WEBASSEMBLY
  }

  BIND(&unknown_data);
  Unreachable();

  BIND(&done);
  return sfi_code.value();
}

TNode<RawPtrT> CodeStubAssembler::LoadCodeInstructionStart(
    TNode<Code> code, CodeEntrypointTag tag) {
#ifdef V8_ENABLE_SANDBOX
  // In this case, the entrypoint is stored in the code pointer table entry
  // referenced via the Code object's 'self' indirect pointer.
  return LoadCodeEntrypointViaCodePointerField(
      code, Code::kSelfIndirectPointerOffset, tag);
#else
  return LoadObjectField<RawPtrT>(code, Code::kInstructionStartOffset);
#endif
}

TNode<BoolT> CodeStubAssembler::IsMarkedForDeoptimization(TNode<Code> code) {
  static_assert(FIELD_SIZE(Code::kFlagsOffset) * kBitsPerByte == 32);
  return IsSetWord32<Code::MarkedForDeoptimizationField>(
      LoadObjectField<Int32T>(code, Code::kFlagsOffset));
}

TNode<JSFunction> CodeStubAssembler::AllocateFunctionWithContext(
    TNode<SharedFunctionInfo> shared_info,
#ifdef V8_ENABLE_LEAPTIERING
    TNode<JSDispatchHandleT> dispatch_handle,
#endif
    TNode<Context> context) {
  const TNode<Code> code = GetSharedFunctionInfoCode(shared_info);
  const TNode<Map> map = CAST(
      LoadContextElement(LoadNativeContext(context),
                         Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX));
  const TNode<HeapObject> fun = Allocate(JSFunction::kSizeWithoutPrototype);
  static_assert(JSFunction::kSizeWithoutPrototype ==
                (7 + V8_ENABLE_LEAPTIERING_BOOL) * kTaggedSize);
  StoreMapNoWriteBarrier(fun, map);
  StoreObjectFieldRoot(fun, JSObject::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(fun, JSObject::kElementsOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(fun, JSFunction::kFeedbackCellOffset,
                       RootIndex::kManyClosuresCell);
  StoreObjectFieldNoWriteBarrier(fun, JSFunction::kSharedFunctionInfoOffset,
                                 shared_info);
  StoreObjectFieldNoWriteBarrier(fun, JSFunction::kContextOffset, context);
  StoreCodePointerFieldNoWriteBarrier(fun, JSFunction::kCodeOffset, code);
#ifdef V8_ENABLE_LEAPTIERING
  CSA_DCHECK(this, TaggedEqual(code, ResolveJSDispatchHandle(dispatch_handle)));
  StoreObjectFieldNoWriteBarrier(fun, JSFunction::kDispatchHandleOffset,
                                 dispatch_handle);
#endif  // V8_ENABLE_LEAPTIERING

  return CAST(fun);
}

void CodeStubAssembler::CheckPrototypeEnumCache(TNode<JSReceiver> receiver,
                                                TNode<Map> receiver_map,
                                                Label* if_fast,
                                                Label* if_slow) {
  TVARIABLE(JSReceiver, var_object, receiver);
  TVARIABLE(Map, object_map, receiver_map);

  Label loop(this, {&var_object, &object_map}), done_loop(this);
  Goto(&loop);
  BIND(&loop);
  {
    // Check that there are no elements on the current {var_object}.
    Label if_no_elements(this);

    // The following relies on the elements only aliasing with JSProxy::target,
    // which is a JavaScript value and hence cannot be confused with an elements
    // backing store.
    static_assert(static_cast<int>(JSObject::kElementsOffset) ==
                  static_cast<int>(JSProxy::kTargetOffset));
    TNode<Object> object_elements =
        LoadObjectField(var_object.value(), JSObject::kElementsOffset);
    GotoIf(IsEmptyFixedArray(object_elements), &if_no_elements);
    GotoIf(IsEmptySlowElementDictionary(object_elements), &if_no_elements);

    // It might still be an empty JSArray.
    GotoIfNot(IsJSArrayMap(object_map.value()), if_slow);
    TNode<Number> object_length = LoadJSArrayLength(CAST(var_object.value()));
    Branch(TaggedEqual(object_length, SmiConstant(0)), &if_no_elements,
           if_slow);

    // Continue with {var_object}'s prototype.
    BIND(&if_no_elements);
    TNode<HeapObject> object = LoadMapPrototype(object_map.value());
    GotoIf(IsNull(object), if_fast);

    // For all {object}s but the {receiver}, check that the cache is empty.
    var_object = CAST(object);
    object_map = LoadMap(object);
    TNode<Uint32T> object_enum_length = LoadMapEnumLength(object_map.value());
    Branch(Word32Equal(object_enum_length, Uint32Constant(0)), &loop, if_slow);
  }
}

TNode<Map> CodeStubAssembler::CheckEnumCache(TNode<JSReceiver> receiver,
                                             Label* if_empty,
                                             Label* if_runtime) {
  Label if_fast(this), if_cache(this), if_no_cache(this, Label::kDeferred);
  TNode<Map> receiver_map = LoadMap(receiver);

  // Check if the enum length field of the {receiver} is properly initialized,
  // indicating that there is an enum cache.
  TNode<Uint32T> receiver_enum_length = LoadMapEnumLength(receiver_map);
  Branch(Word32Equal(receiver_enum_length,
                     Uint32Constant(kInvalidEnumCacheSentinel)),
         &if_no_cache, &if_cache);

  BIND(&if_no_cache);
  {
    // Avoid runtime-call for empty dictionary receivers.
    GotoIfNot(IsDictionaryMap(receiver_map), if_runtime);
    TNode<Smi> length;
    TNode<HeapObject> properties = LoadSlowProperties(receiver);

    // g++ version 8 has a bug when using `if constexpr(false)` with a lambda:
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85149
    // TODO(miladfarca): Use `if constexpr` once all compilers handle this
    // properly.
    CSA_DCHECK(this, Word32Or(IsPropertyDictionary(properties),
                              IsGlobalDictionary(properties)));
    if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
      length = Select<Smi>(
          IsPropertyDictionary(properties),
          [=, this] {
            return GetNumberOfElements(
                UncheckedCast<PropertyDictionary>(properties));
          },
          [=, this] {
            return GetNumberOfElements(
                UncheckedCast<GlobalDictionary>(properties));
          });

    } else {
      static_assert(static_cast<int>(NameDictionary::kNumberOfElementsIndex) ==
                    static_cast<int>(GlobalDictionary::kNumberOfElementsIndex));
      length = GetNumberOfElements(UncheckedCast<HashTableBase>(properties));
    }

    GotoIfNot(TaggedEqual(length, SmiConstant(0)), if_runtime);
    // Check that there are no elements on the {receiver} and its prototype
    // chain. Given that we do not create an EnumCache for dict-mode objects,
    // directly jump to {if_empty} if there are no elements and no properties
    // on the {receiver}.
    CheckPrototypeEnumCache(receiver, receiver_map, if_empty, if_runtime);
  }

  // Check that there are no elements on the fast {receiver} and its
  // prototype chain.
  BIND(&if_cache);
  CheckPrototypeEnumCache(receiver, receiver_map, &if_fast, if_runtime);

  BIND(&if_fast);
  return receiver_map;
}

TNode<Object> CodeStubAssembler::GetArgumentValue(TorqueStructArguments args,
                                                  TNode<IntPtrT> index) {
  return CodeStubArguments(this, args).GetOptionalArgumentValue(index);
}

void CodeStubAssembler::SetArgumentValue(TorqueStructArguments args,
                                         TNode<IntPtrT> index,
                                         TNode<Object> value) {
  CodeStubArguments(this, args).SetArgumentValue(index, value);
}

TorqueStructArguments CodeStubAssembler::GetFrameArguments(
    TNode<RawPtrT> frame, TNode<IntPtrT> argc,
    FrameArgumentsArgcType argc_type) {
  if (argc_type == FrameArgumentsArgcType::kCountExcludesReceiver) {
    argc = IntPtrAdd(argc, IntPtrConstant(kJSArgcReceiverSlots));
  }
  return CodeStubArguments(this, argc, frame).GetTorqueArguments();
}

void CodeStubAssembler::Print(const char* s) {
  PrintToStream(s, fileno(stdout));
}

void CodeStubAssembler::PrintErr(const char* s) {
  PrintToStream(s, fileno(stderr));
}

void CodeStubAssembler::PrintToStream(const char* s, int stream) {
  std::string formatted(s);
  formatted += "\n";
  CallRuntime(Runtime::kGlobalPrint, NoContextConstant(),
              StringConstant(formatted.c_str()), SmiConstant(stream));
}

void CodeStubAssembler::Print(const char* prefix,
                              TNode<MaybeObject> tagged_value) {
  PrintToStream(prefix, tagged_value, fileno(stdout));
}

void CodeStubAssembler::Print(const char* prefix, TNode<UintPtrT> value) {
  PrintToStream(prefix, value, fileno(stdout));
}

void CodeStubAssembler::Print(const char* prefix, TNode<Float64T> value) {
  PrintToStream(prefix, value, fileno(stdout));
}

void CodeStubAssembler::PrintErr(const char* prefix,
                                 TNode<MaybeObject> tagged_value) {
  PrintToStream(prefix, tagged_value, fileno(stderr));
}

void CodeStubAssembler::PrintToStream(const char* prefix,
                                      TNode<MaybeObject> tagged_value,
                                      int stream) {
  if (prefix != nullptr) {
    std::string formatted(prefix);
    formatted += ": ";
    Handle<String> string =
        isolate()->factory()->InternalizeString(formatted.c_str());
    CallRuntime(Runtime::kGlobalPrint, NoContextConstant(),
                HeapConstantNoHole(string), SmiConstant(stream));
  }
  // CallRuntime only accepts Objects, so do an UncheckedCast to object.
  // DebugPrint explicitly checks whether the tagged value is a
  // Tagged<MaybeObject>.
  TNode<Object> arg = UncheckedCast<Object>(tagged_value);
  CallRuntime(Runtime::kDebugPrint, NoContextConstant(), arg,
              SmiConstant(stream));
}

void CodeStubAssembler::PrintToStream(const char* prefix, TNode<UintPtrT> value,
                                      int stream) {
  if (prefix != nullptr) {
    std::string formatted(prefix);
    formatted += ": ";
    Handle<String> string =
        isolate()->factory()->InternalizeString(formatted.c_str());
    CallRuntime(Runtime::kGlobalPrint, NoContextConstant(),
                HeapConstantNoHole(string), SmiConstant(stream));
  }

  // We use 16 bit per chunk.
  TNode<Smi> chunks[4];
  for (int i = 0; i < 4; ++i) {
    chunks[i] = SmiFromUint32(ReinterpretCast<Uint32T>(Word32And(
        TruncateIntPtrToInt32(ReinterpretCast<IntPtrT>(value)), 0xFFFF)));
    value = WordShr(value, IntPtrConstant(16));
  }

  // Args are: <bits 63-48>, <bits 47-32>, <bits 31-16>, <bits 15-0>, stream.
  CallRuntime(Runtime::kDebugPrintWord, NoContextConstant(), chunks[3],
              chunks[2], chunks[1], chunks[0], SmiConstant(stream));
}

void CodeStubAssembler::PrintToStream(const char* prefix, TNode<Float64T> value,
                                      int stream) {
  if (prefix != nullptr) {
    std::string formatted(prefix);
    formatted += ": ";
    Handle<String> string =
        isolate()->factory()->InternalizeString(formatted.c_str());
    CallRuntime(Runtime::kGlobalPrint, NoContextConstant(),
                HeapConstantNoHole(string), SmiConstant(stream));
  }

  // We use word32 extraction instead of `BitcastFloat64ToInt64` to support 32
  // bit architectures, too.
  TNode<Uint32T> high = Float64ExtractHighWord32(value);
  TNode<Uint32T> low = Float64ExtractLowWord32(value);

  // We use 16 bit per chunk.
  TNode<Smi> chunks[4];
  chunks[0] = SmiFromUint32(ReinterpretCast<Uint32T>(Word32And(low, 0xFFFF)));
  chunks[1] = SmiFromUint32(ReinterpretCast<Uint32T>(
      Word32And(Word32Shr(low, Int32Constant(16)), 0xFFFF)));
  chunks[2] = SmiFromUint32(ReinterpretCast<Uint32T>(Word32And(high, 0xFFFF)));
  chunks[3] = SmiFromUint32(ReinterpretCast<Uint32T>(
      Word32And(Word32Shr(high, Int32Constant(16)), 0xFFFF)));

  // Args are: <bits 63-48>, <bits 47-32>, <bits 31-16>, <bits 15-0>, stream.
  CallRuntime(Runtime::kDebugPrintFloat, NoContextConstant(), chunks[3],
              chunks[2], chunks[1], chunks[0], SmiConstant(stream));
}

IntegerLiteral CodeStubAssembler::ConstexprIntegerLiteralAdd(
    const IntegerLiteral& lhs, const IntegerLiteral& rhs) {
  return lhs + rhs;
}
IntegerLiteral CodeStubAssembler::ConstexprIntegerLiteralLeftShift(
    const IntegerLiteral& lhs, const IntegerLiteral& rhs) {
  return lhs << rhs;
}
IntegerLiteral CodeStubAssembler::ConstexprIntegerLiteralBitwiseOr(
    const IntegerLiteral& lhs, const IntegerLiteral& rhs) {
  return lhs | rhs;
}

void CodeStubAssembler::PerformStackCheck(TNode<Context> context) {
  Label ok(this), stack_check_interrupt(this, Label::kDeferred);

  TNode<UintPtrT> stack_limit = UncheckedCast<UintPtrT>(
      Load(MachineType::Pointer(),
           ExternalConstant(ExternalReference::address_of_jslimit(isolate()))));
  TNode<BoolT> sp_within_limit = StackPointerGreaterThan(stack_limit);

  Branch(sp_within_limit, &ok, &stack_check_interrupt);

  BIND(&stack_check_interrupt);
  CallRuntime(Runtime::kStackGuard, context);
  Goto(&ok);

  BIND(&ok);
}

TNode<Object> CodeStubAssembler::CallRuntimeNewArray(
    TNode<Context> context, TNode<Object> receiver, TNode<Object> length,
    TNode<Object> new_target, TNode<Object> allocation_site) {
  // Runtime_NewArray receives arguments in the JS order (to avoid unnecessary
  // copy). Except the last two (new_target and allocation_site) which are add
  // on top of the stack later.
  return CallRuntime(Runtime::kNewArray, context, length, receiver, new_target,
                     allocation_site);
}

void CodeStubAssembler::TailCallRuntimeNewArray(TNode<Context> context,
                                                TNode<Object> receiver,
                                                TNode<Object> length,
                                                TNode<Object> new_target,
                                                TNode<Object> allocation_site) {
  // Runtime_NewArray receives arguments in the JS order (to avoid unnecessary
  // copy). Except the last two (new_target and allocation_site) which are add
  // on top of the stack later.
  return TailCallRuntime(Runtime::kNewArray, context, length, receiver,
                         new_target, allocation_site);
}

TNode<JSArray> CodeStubAssembler::ArrayCreate(TNode<Context> context,
                                              TNode<Number> length) {
  TVARIABLE(JSArray, array);
  Label allocate_js_array(this);

  Label done(this), next(this), runtime(this, Label::kDeferred);
  TNode<Smi> limit = SmiConstant(JSArray::kInitialMaxFastElementArray);
  CSA_DCHECK_BRANCH(this, ([=, this](Label* ok, Label* not_ok) {
                      BranchIfNumberRelationalComparison(
                          Operation::kGreaterThanOrEqual, length,
                          SmiConstant(0), ok, not_ok);
                    }));
  // This check also transitively covers the case where length is too big
  // to be representable by a SMI and so is not usable with
  // AllocateJSArray.
  BranchIfNumberRelationalComparison(Operation::kGreaterThanOrEqual, length,
                                     limit, &runtime, &next);

  BIND(&runtime);
  {
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<JSFunction> array_function =
        CAST(LoadContextElement(native_context, Context::ARRAY_FUNCTION_INDEX));
    array = CAST(CallRuntimeNewArray(context, array_function, length,
                                     array_function, UndefinedConstant()));
    Goto(&done);
  }

  BIND(&next);
  TNode<Smi> length_smi = CAST(length);

  TNode<Map> array_map = CAST(LoadContextElement(
      context, Context::JS_ARRAY_PACKED_SMI_ELEMENTS_MAP_INDEX));

  // TODO(delphick): Consider using
  // AllocateUninitializedJSArrayWithElements to avoid initializing an
  // array and then writing over it.
  array = AllocateJSArray(PACKED_SMI_ELEMENTS, array_map, length_smi,
                          SmiConstant(0));
  Goto(&done);

  BIND(&done);
  return array.value();
}

void CodeStubAssembler::SetPropertyLength(TNode<Context> context,
                                          TNode<Object> array,
                                          TNode<Number> length) {
  SetPropertyStrict(context, array, CodeStubAssembler::LengthStringConstant(),
                    length);
}

TNode<Smi> CodeStubAssembler::RefillMathRandom(
    TNode<NativeContext> native_context) {
  // Cache exhausted, populate the cache. Return value is the new index.
  const TNode<ExternalReference> refill_math_random =
      ExternalConstant(ExternalReference::refill_math_random());
  const TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());
  MachineType type_tagged = MachineType::AnyTagged();
  MachineType type_ptr = MachineType::Pointer();

  return CAST(CallCFunction(refill_math_random, type_tagged,
                            std::make_pair(type_ptr, isolate_ptr),
                            std::make_pair(type_tagged, native_context)));
}

TNode<String> CodeStubAssembler::TaggedToDirectString(TNode<Object> value,
                                                      Label* fail) {
  ToDirectStringAssembler to_direct(state(), CAST(value));
  to_direct.TryToDirect(fail);
  to_direct.PointerToData(fail);
  return CAST(value);
}

void CodeStubAssembler::RemoveFinalizationRegistryCellFromUnregisterTokenMap(
    TNode<JSFinalizationRegistry> finalization_registry,
    TNode<WeakCell> weak_cell) {
  const TNode<ExternalReference> remove_cell = ExternalConstant(
      ExternalReference::
          js_finalization_registry_remove_cell_from_unregister_token_map());
  const TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());

  CallCFunction(remove_cell, MachineType::Pointer(),
                std::make_pair(MachineType::Pointer(), isolate_ptr),
                std::make_pair(MachineType::AnyTagged(), finalization_registry),
                std::make_pair(MachineType::AnyTagged(), weak_cell));
}

PrototypeCheckAssembler::PrototypeCheckAssembler(
    compiler::CodeAssemblerState* state, Flags flags,
    TNode<NativeContext> native_context, TNode<Map> initial_prototype_map,
    base::Vector<DescriptorIndexNameValue> properties)
    : CodeStubAssembler(state),
      flags_(flags),
      native_context_(native_context),
      initial_prototype_map_(initial_prototype_map),
      properties_(properties) {}

void PrototypeCheckAssembler::CheckAndBranch(TNode<HeapObject> prototype,
                                             Label* if_unmodified,
                                             Label* if_modified) {
  TNode<Map> prototype_map = LoadMap(prototype);
  TNode<DescriptorArray> descriptors = LoadMapDescriptors(prototype_map);

  // The continuation of a failed fast check: if property identity checks are
  // enabled, we continue there (since they may still classify the prototype as
  // fast), otherwise we bail out.
  Label property_identity_check(this, Label::kDeferred);
  Label* if_fast_check_failed =
      ((flags_ & kCheckPrototypePropertyIdentity) == 0)
          ? if_modified
          : &property_identity_check;

  if ((flags_ & kCheckPrototypePropertyConstness) != 0) {
    // A simple prototype map identity check. Note that map identity does not
    // guarantee unmodified properties. It does guarantee that no new properties
    // have been added, or old properties deleted.

    GotoIfNot(TaggedEqual(prototype_map, initial_prototype_map_),
              if_fast_check_failed);

    // We need to make sure that relevant properties in the prototype have
    // not been tampered with. We do this by checking that their slots
    // in the prototype's descriptor array are still marked as const.

    TNode<Uint32T> combined_details;
    for (int i = 0; i < properties_.length(); i++) {
      // Assert the descriptor index is in-bounds.
      int descriptor = properties_[i].descriptor_index;
      CSA_DCHECK(this, Int32LessThan(Int32Constant(descriptor),
                                     LoadNumberOfDescriptors(descriptors)));

      // Assert that the name is correct. This essentially checks that
      // the descriptor index corresponds to the insertion order in
      // the bootstrapper.
      CSA_DCHECK(
          this,
          TaggedEqual(LoadKeyByDescriptorEntry(descriptors, descriptor),
                      CodeAssembler::LoadRoot(properties_[i].name_root_index)));

      TNode<Uint32T> details =
          DescriptorArrayGetDetails(descriptors, Uint32Constant(descriptor));

      if (i == 0) {
        combined_details = details;
      } else {
        combined_details = Word32And(combined_details, details);
      }
    }

    TNode<Uint32T> constness =
        DecodeWord32<PropertyDetails::ConstnessField>(combined_details);

    Branch(
        Word32Equal(constness,
                    Int32Constant(static_cast<int>(PropertyConstness::kConst))),
        if_unmodified, if_fast_check_failed);
  }

  if ((flags_ & kCheckPrototypePropertyIdentity) != 0) {
    // The above checks have failed, for whatever reason (maybe the prototype
    // map has changed, or a property is no longer const). This block implements
    // a more thorough check that can also accept maps which 1. do not have the
    // initial map, 2. have mutable relevant properties, but 3. still match the
    // expected value for all relevant properties.

    BIND(&property_identity_check);

    int max_descriptor_index = -1;
    for (int i = 0; i < properties_.length(); i++) {
      max_descriptor_index =
          std::max(max_descriptor_index, properties_[i].descriptor_index);
    }

    // If the greatest descriptor index is out of bounds, the map cannot be
    // fast.
    GotoIfNot(Int32LessThan(Int32Constant(max_descriptor_index),
                            LoadNumberOfDescriptors(descriptors)),
              if_modified);

    // Logic below only handles maps with fast properties.
    GotoIfMapHasSlowProperties(prototype_map, if_modified);

    for (int i = 0; i < properties_.length(); i++) {
      const DescriptorIndexNameValue& p = properties_[i];
      const int descriptor = p.descriptor_index;

      // Check if the name is correct. This essentially checks that
      // the descriptor index corresponds to the insertion order in
      // the bootstrapper.
      GotoIfNot(TaggedEqual(LoadKeyByDescriptorEntry(descriptors, descriptor),
                            CodeAssembler::LoadRoot(p.name_root_index)),
                if_modified);

      // Finally, check whether the actual value equals the expected value.
      TNode<Uint32T> details =
          DescriptorArrayGetDetails(descriptors, Uint32Constant(descriptor));
      TVARIABLE(Uint32T, var_details, details);
      TVARIABLE(Object, var_value);

      const int key_index = DescriptorArray::ToKeyIndex(descriptor);
      LoadPropertyFromFastObject(prototype, prototype_map, descriptors,
                                 IntPtrConstant(key_index), &var_details,
                                 &var_value);

      TNode<Object> actual_value = var_value.value();
      TNode<Object> expected_value =
          LoadContextElement(native_context_, p.expected_value_context_index);
      GotoIfNot(TaggedEqual(actual_value, expected_value), if_modified);
    }

    Goto(if_unmodified);
  }
}

//
// Begin of SwissNameDictionary macros
//

namespace {

// Provides load and store functions that abstract over the details of accessing
// the meta table in memory. Instead they allow using logical indices that are
// independent from the underlying entry size in the meta table of a
// SwissNameDictionary.
class MetaTableAccessor {
 public:
  MetaTableAccessor(CodeStubAssembler& csa, MachineType mt)
      : csa{csa}, mt{mt} {}

  TNode<Uint32T> Load(TNode<ByteArray> meta_table, TNode<IntPtrT> index) {
    TNode<IntPtrT> offset = OverallOffset(meta_table, index);

    return csa.UncheckedCast<Uint32T>(
        csa.LoadFromObject(mt, meta_table, offset));
  }

  TNode<Uint32T> Load(TNode<ByteArray> meta_table, int index) {
    return Load(meta_table, csa.IntPtrConstant(index));
  }

  void Store(TNode<ByteArray> meta_table, TNode<IntPtrT> index,
             TNode<Uint32T> data) {
    TNode<IntPtrT> offset = OverallOffset(meta_table, index);

#ifdef DEBUG
    int bits = mt.MemSize() * 8;
    TNode<UintPtrT> max_value = csa.UintPtrConstant((1ULL << bits) - 1);

    CSA_DCHECK(&csa, csa.UintPtrLessThanOrEqual(csa.ChangeUint32ToWord(data),
                                                max_value));
#endif

    csa.StoreToObject(mt.representation(), meta_table, offset, data,
                      StoreToObjectWriteBarrier::kNone);
  }

  void Store(TNode<ByteArray> meta_table, int index, TNode<Uint32T> data) {
    Store(meta_table, csa.IntPtrConstant(index), data);
  }

 private:
  TNode<IntPtrT> OverallOffset(TNode<ByteArray> meta_table,
                               TNode<IntPtrT> index) {
    // TODO(v8:11330): consider using ElementOffsetFromIndex().

    int offset_to_data_minus_tag = ByteArray::kHeaderSize - kHeapObjectTag;

    TNode<IntPtrT> overall_offset;
    int size = mt.MemSize();
    intptr_t constant;
    if (csa.TryToIntPtrConstant(index, &constant)) {
      intptr_t index_offset = constant * size;
      overall_offset =
          csa.IntPtrConstant(offset_to_data_minus_tag + index_offset);
    } else {
      TNode<IntPtrT> index_offset =
          csa.IntPtrMul(index, csa.IntPtrConstant(size));
      overall_offset = csa.IntPtrAdd(
          csa.IntPtrConstant(offset_to_data_minus_tag), index_offset);
    }

#ifdef DEBUG
    TNode<IntPtrT> byte_array_data_bytes =
        csa.SmiToIntPtr(csa.LoadFixedArrayBaseLength(meta_table));
    TNode<IntPtrT> max_allowed_offset = csa.IntPtrAdd(
        byte_array_data_bytes, csa.IntPtrConstant(offset_to_data_minus_tag));
    CSA_DCHECK(&csa, csa.UintPtrLessThan(overall_offset, max_allowed_offset));
#endif

    return overall_offset;
  }

  CodeStubAssembler& csa;
  MachineType mt;
};

// Type of functions that given a MetaTableAccessor, use its load and store
// functions to generate code for operating on the meta table.
using MetaTableAccessFunction = std::function<void(MetaTableAccessor&)>;

// Helper function for macros operating on the meta table of a
// SwissNameDictionary. Given a MetaTableAccessFunction, generates branching
// code and uses the builder to generate code for each of the three possible
// sizes per entry a meta table can have.
void GenerateMetaTableAccess(CodeStubAssembler* csa, TNode<IntPtrT> capacity,
                             MetaTableAccessFunction builder) {
  MetaTableAccessor mta8 = MetaTableAccessor(*csa, MachineType::Uint8());
  MetaTableAccessor mta16 = MetaTableAccessor(*csa, MachineType::Uint16());
  MetaTableAccessor mta32 = MetaTableAccessor(*csa, MachineType::Uint32());

  using Label = compiler::CodeAssemblerLabel;
  Label small(csa), medium(csa), done(csa);

  csa->GotoIf(
      csa->IntPtrLessThanOrEqual(
          capacity,
          csa->IntPtrConstant(SwissNameDictionary::kMax1ByteMetaTableCapacity)),
      &small);
  csa->GotoIf(
      csa->IntPtrLessThanOrEqual(
          capacity,
          csa->IntPtrConstant(SwissNameDictionary::kMax2ByteMetaTableCapacity)),
      &medium);

  builder(mta32);
  csa->Goto(&done);

  csa->Bind(&medium);
  builder(mta16);
  csa->Goto(&done);

  csa->Bind(&small);
  builder(mta8);
  csa->Goto(&done);
  csa->Bind(&done);
}

}  // namespace

TNode<IntPtrT> CodeStubAssembler::LoadSwissNameDictionaryNumberOfElements(
    TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity) {
  TNode<ByteArray> meta_table = LoadSwissNameDictionaryMetaTable(table);

  TVARIABLE(Uint32T, nof, Uint32Constant(0));
  MetaTableAccessFunction builder = [&](MetaTableAccessor& mta) {
    nof = mta.Load(meta_table,
                   SwissNameDictionary::kMetaTableElementCountFieldIndex);
  };

  GenerateMetaTableAccess(this, capacity, builder);
  return ChangeInt32ToIntPtr(nof.value());
}

TNode<IntPtrT>
CodeStubAssembler::LoadSwissNameDictionaryNumberOfDeletedElements(
    TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity) {
  TNode<ByteArray> meta_table = LoadSwissNameDictionaryMetaTable(table);

  TVARIABLE(Uint32T, nod, Uint32Constant(0));
  MetaTableAccessFunction builder = [&](MetaTableAccessor& mta) {
    nod =
        mta.Load(meta_table,
                 SwissNameDictionary::kMetaTableDeletedElementCountFieldIndex);
  };

  GenerateMetaTableAccess(this, capacity, builder);
  return ChangeInt32ToIntPtr(nod.value());
}

void CodeStubAssembler::StoreSwissNameDictionaryEnumToEntryMapping(
    TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity,
    TNode<IntPtrT> enum_index, TNode<Int32T> entry) {
  TNode<ByteArray> meta_table = LoadSwissNameDictionaryMetaTable(table);
  TNode<IntPtrT> meta_table_index = IntPtrAdd(
      IntPtrConstant(SwissNameDictionary::kMetaTableEnumerationDataStartIndex),
      enum_index);

  MetaTableAccessFunction builder = [&](MetaTableAccessor& mta) {
    mta.Store(meta_table, meta_table_index, Unsigned(entry));
  };

  GenerateMetaTableAccess(this, capacity, builder);
}

TNode<Uint32T>
CodeStubAssembler::SwissNameDictionaryIncreaseElementCountOrBailout(
    TNode<ByteArray> meta_table, TNode<IntPtrT> capacity,
    TNode<Uint32T> max_usable_capacity, Label* bailout) {
  TVARIABLE(Uint32T, used_var, Uint32Constant(0));

  MetaTableAccessFunction builder = [&](MetaTableAccessor& mta) {
    TNode<Uint32T> nof = mta.Load(
        meta_table, SwissNameDictionary::kMetaTableElementCountFieldIndex);
    TNode<Uint32T> nod =
        mta.Load(meta_table,
                 SwissNameDictionary::kMetaTableDeletedElementCountFieldIndex);
    TNode<Uint32T> used = Uint32Add(nof, nod);
    GotoIf(Uint32GreaterThanOrEqual(used, max_usable_capacity), bailout);
    TNode<Uint32T> inc_nof = Uint32Add(nof, Uint32Constant(1));
    mta.Store(meta_table, SwissNameDictionary::kMetaTableElementCountFieldIndex,
              inc_nof);
    used_var = used;
  };

  GenerateMetaTableAccess(this, capacity, builder);
  return used_var.value();
}

TNode<Uint32T> CodeStubAssembler::SwissNameDictionaryUpdateCountsForDeletion(
    TNode<ByteArray> meta_table, TNode<IntPtrT> capacity) {
  TVARIABLE(Uint32T, new_nof_var, Uint32Constant(0));

  MetaTableAccessFunction builder = [&](MetaTableAccessor& mta) {
    TNode<Uint32T> nof = mta.Load(
        meta_table, SwissNameDictionary::kMetaTableElementCountFieldIndex);
    TNode<Uint32T> nod =
        mta.Load(meta_table,
                 SwissNameDictionary::kMetaTableDeletedElementCountFieldIndex);

    TNode<Uint32T> new_nof = Uint32Sub(nof, Uint32Constant(1));
    TNode<Uint32T> new_nod = Uint32Add(nod, Uint32Constant(1));

    mta.Store(meta_table, SwissNameDictionary::kMetaTableElementCountFieldIndex,
              new_nof);
    mta.Store(meta_table,
              SwissNameDictionary::kMetaTableDeletedElementCountFieldIndex,
              new_nod);

    new_nof_var = new_nof;
  };

  GenerateMetaTableAccess(this, capacity, builder);
  return new_nof_var.value();
}

TNode<SwissNameDictionary> CodeStubAssembler::AllocateSwissNameDictionary(
    TNode<IntPtrT> at_least_space_for) {
  // Note that as AllocateNameDictionary, we return a table with initial
  // (non-zero) capacity even if |at_least_space_for| is 0.

  TNode<IntPtrT> capacity =
      IntPtrMax(IntPtrConstant(SwissNameDictionary::kInitialCapacity),
                SwissNameDictionaryCapacityFor(at_least_space_for));

  return AllocateSwissNameDictionaryWithCapacity(capacity);
}

TNode<SwissNameDictionary> CodeStubAssembler::AllocateSwissNameDictionary(
    int at_least_space_for) {
  return AllocateSwissNameDictionary(IntPtrConstant(at_least_space_for));
}

TNode<SwissNameDictionary>
CodeStubAssembler::AllocateSwissNameDictionaryWithCapacity(
    TNode<IntPtrT> capacity) {
  Comment("[ AllocateSwissNameDictionaryWithCapacity");
  CSA_DCHECK(this, WordIsPowerOfTwo(capacity));
  CSA_DCHECK(this, UintPtrGreaterThanOrEqual(
                       capacity,
                       IntPtrConstant(SwissNameDictionary::kInitialCapacity)));
  CSA_DCHECK(this,
             UintPtrLessThanOrEqual(
                 capacity, IntPtrConstant(SwissNameDictionary::MaxCapacity())));

  Comment("Size check.");
  intptr_t capacity_constant;
  if (ToParameterConstant(capacity, &capacity_constant)) {
    CHECK_LE(capacity_constant, SwissNameDictionary::MaxCapacity());
  } else {
    Label if_out_of_memory(this, Label::kDeferred), next(this);
    Branch(UintPtrGreaterThan(
               capacity, IntPtrConstant(SwissNameDictionary::MaxCapacity())),
           &if_out_of_memory, &next);

    BIND(&if_out_of_memory);
    CallRuntime(Runtime::kFatalProcessOutOfMemoryInAllocateRaw,
                NoContextConstant());
    Unreachable();

    BIND(&next);
  }

  // TODO(v8:11330) Consider adding dedicated handling for constant capacties,
  // similar to AllocateOrderedHashTableWithCapacity.

  // We must allocate the ByteArray first. Otherwise, allocating the ByteArray
  // may trigger GC, which may try to verify the un-initialized
  // SwissNameDictionary.
  Comment("Meta table allocation.");
  TNode<IntPtrT> meta_table_payload_size =
      SwissNameDictionaryMetaTableSizeFor(capacity);

  TNode<ByteArray> meta_table =
      AllocateNonEmptyByteArray(Unsigned(meta_table_payload_size));

  Comment("SwissNameDictionary allocation.");
  TNode<IntPtrT> total_size = SwissNameDictionarySizeFor(capacity);

  TNode<SwissNameDictionary> table =
      UncheckedCast<SwissNameDictionary>(Allocate(total_size));

  StoreMapNoWriteBarrier(table, RootIndex::kSwissNameDictionaryMap);

  Comment(
      "Initialize the hash, capacity, meta table pointer, and number of "
      "(deleted) elements.");

  StoreSwissNameDictionaryHash(table,
                               Uint32Constant(PropertyArray::kNoHashSentinel));
  StoreSwissNameDictionaryCapacity(table, TruncateIntPtrToInt32(capacity));
  StoreSwissNameDictionaryMetaTable(table, meta_table);

  // Set present and deleted element count without doing branching needed for
  // meta table access twice.
  MetaTableAccessFunction builder = [&](MetaTableAccessor& mta) {
    mta.Store(meta_table, SwissNameDictionary::kMetaTableElementCountFieldIndex,
              Uint32Constant(0));
    mta.Store(meta_table,
              SwissNameDictionary::kMetaTableDeletedElementCountFieldIndex,
              Uint32Constant(0));
  };
  GenerateMetaTableAccess(this, capacity, builder);

  Comment("Initialize the ctrl table.");

  TNode<IntPtrT> ctrl_table_start_offset_minus_tag =
      SwissNameDictionaryCtrlTableStartOffsetMT(capacity);

  TNode<IntPtrT> table_address_with_tag = BitcastTaggedToWord(table);
  TNode<IntPtrT> ctrl_table_size_bytes =
      IntPtrAdd(capacity, IntPtrConstant(SwissNameDictionary::kGroupWidth));
  TNode<IntPtrT> ctrl_table_start_ptr =
      IntPtrAdd(table_address_with_tag, ctrl_table_start_offset_minus_tag);
  TNode<IntPtrT> ctrl_table_end_ptr =
      IntPtrAdd(ctrl_table_start_ptr, ctrl_table_size_bytes);

  // |ctrl_table_size_bytes| (= capacity + kGroupWidth) is divisble by four:
  static_assert(SwissNameDictionary::kGroupWidth % 4 == 0);
  static_assert(SwissNameDictionary::kInitialCapacity % 4 == 0);

  // TODO(v8:11330) For all capacities except 4, we know that
  // |ctrl_table_size_bytes| is divisible by 8. Consider initializing the ctrl
  // table with WordTs in those cases. Alternatively, always initialize as many
  // bytes as possbible with WordT and then, if necessary, the remaining 4 bytes
  // with Word32T.

  constexpr uint8_t kEmpty = swiss_table::Ctrl::kEmpty;
  constexpr uint32_t kEmpty32 =
      (kEmpty << 24) | (kEmpty << 16) | (kEmpty << 8) | kEmpty;
  TNode<Int32T> empty32 = Int32Constant(kEmpty32);
  BuildFastLoop<IntPtrT>(
      ctrl_table_start_ptr, ctrl_table_end_ptr,
      [=, this](TNode<IntPtrT> current) {
        UnsafeStoreNoWriteBarrier(MachineRepresentation::kWord32, current,
                                  empty32);
      },
      sizeof(uint32_t), LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);

  Comment("Initialize the data table.");

  TNode<IntPtrT> data_table_start_offset_minus_tag =
      SwissNameDictionaryDataTableStartOffsetMT();
  TNode<IntPtrT> data_table_ptr =
      IntPtrAdd(table_address_with_tag, data_table_start_offset_minus_tag);
  TNode<IntPtrT> data_table_size = IntPtrMul(
      IntPtrConstant(SwissNameDictionary::kDataTableEntryCount * kTaggedSize),
      capacity);

  StoreFieldsNoWriteBarrier(data_table_ptr,
                            IntPtrAdd(data_table_ptr, data_table_size),
                            TheHoleConstant());

  Comment("AllocateSwissNameDictionaryWithCapacity ]");

  return table;
}

TNode<SwissNameDictionary> CodeStubAssembler::CopySwissNameDictionary(
    TNode<SwissNameDictionary> original) {
  Comment("[ CopySwissNameDictionary");

  TNode<IntPtrT> capacity =
      Signed(ChangeUint32ToWord(LoadSwissNameDictionaryCapacity(original)));

  // We must allocate the ByteArray first. Otherwise, allocating the ByteArray
  // may trigger GC, which may try to verify the un-initialized
  // SwissNameDictionary.
  Comment("Meta table allocation.");
  TNode<IntPtrT> meta_table_payload_size =
      SwissNameDictionaryMetaTableSizeFor(capacity);

  TNode<ByteArray> meta_table =
      AllocateNonEmptyByteArray(Unsigned(meta_table_payload_size));

  Comment("SwissNameDictionary allocation.");
  TNode<IntPtrT> total_size = SwissNameDictionarySizeFor(capacity);

  TNode<SwissNameDictionary> table =
      UncheckedCast<SwissNameDictionary>(Allocate(total_size));

  StoreMapNoWriteBarrier(table, RootIndex::kSwissNameDictionaryMap);

  Comment("Copy the hash and capacity.");

  StoreSwissNameDictionaryHash(table, LoadSwissNameDictionaryHash(original));
  StoreSwissNameDictionaryCapacity(table, TruncateIntPtrToInt32(capacity));
  StoreSwissNameDictionaryMetaTable(table, meta_table);
  // Not setting up number of (deleted elements), copying whole meta table
  // instead.

  TNode<ExternalReference> memcpy =
      ExternalConstant(ExternalReference::libc_memcpy_function());

  TNode<IntPtrT> old_table_address_with_tag = BitcastTaggedToWord(original);
  TNode<IntPtrT> new_table_address_with_tag = BitcastTaggedToWord(table);

  TNode<IntPtrT> ctrl_table_start_offset_minus_tag =
      SwissNameDictionaryCtrlTableStartOffsetMT(capacity);

  TNode<IntPtrT> ctrl_table_size_bytes =
      IntPtrAdd(capacity, IntPtrConstant(SwissNameDictionary::kGroupWidth));

  Comment("Copy the ctrl table.");
  {
    TNode<IntPtrT> old_ctrl_table_start_ptr = IntPtrAdd(
        old_table_address_with_tag, ctrl_table_start_offset_minus_tag);
    TNode<IntPtrT> new_ctrl_table_start_ptr = IntPtrAdd(
        new_table_address_with_tag, ctrl_table_start_offset_minus_tag);

    CallCFunction(
        memcpy, MachineType::Pointer(),
        std::make_pair(MachineType::Pointer(), new_ctrl_table_start_ptr),
        std::make_pair(MachineType::Pointer(), old_ctrl_table_start_ptr),
        std::make_pair(MachineType::UintPtr(), ctrl_table_size_bytes));
  }

  Comment("Copy the data table.");
  {
    TNode<IntPtrT> start_offset =
        IntPtrConstant(SwissNameDictionary::DataTableStartOffset());
    TNode<IntPtrT> data_table_size = IntPtrMul(
        IntPtrConstant(SwissNameDictionary::kDataTableEntryCount * kTaggedSize),
        capacity);

    BuildFastLoop<IntPtrT>(
        start_offset, IntPtrAdd(start_offset, data_table_size),
        [=, this](TNode<IntPtrT> offset) {
          TNode<Object> table_field = LoadObjectField(original, offset);
          StoreObjectField(table, offset, table_field);
        },
        kTaggedSize, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
  }

  Comment("Copy the meta table");
  {
    TNode<IntPtrT> old_meta_table_address_with_tag =
        BitcastTaggedToWord(LoadSwissNameDictionaryMetaTable(original));
    TNode<IntPtrT> new_meta_table_address_with_tag =
        BitcastTaggedToWord(meta_table);

    TNode<IntPtrT> meta_table_size =
        SwissNameDictionaryMetaTableSizeFor(capacity);

    TNode<IntPtrT> old_data_start =
        IntPtrAdd(old_meta_table_address_with_tag,
                  IntPtrConstant(ByteArray::kHeaderSize - kHeapObjectTag));
    TNode<IntPtrT> new_data_start =
        IntPtrAdd(new_meta_table_address_with_tag,
                  IntPtrConstant(ByteArray::kHeaderSize - kHeapObjectTag));

    CallCFunction(memcpy, MachineType::Pointer(),
                  std::make_pair(MachineType::Pointer(), new_data_start),
                  std::make_pair(MachineType::Pointer(), old_data_start),
                  std::make_pair(MachineType::UintPtr(), meta_table_size));
  }

  Comment("Copy the PropertyDetails table");
  {
    TNode<IntPtrT> property_details_start_offset_minus_tag =
        SwissNameDictionaryOffsetIntoPropertyDetailsTableMT(table, capacity,
                                                            IntPtrConstant(0));

    // Offset to property details entry
    TVARIABLE(IntPtrT, details_table_offset_minus_tag,
              property_details_start_offset_minus_tag);

    TNode<IntPtrT> start = ctrl_table_start_offset_minus_tag;

    VariableList in_loop_variables({&details_table_offset_minus_tag}, zone());
    BuildFastLoop<IntPtrT>(
        in_loop_variables, start, IntPtrAdd(start, ctrl_table_size_bytes),
        [&](TNode<IntPtrT> ctrl_table_offset) {
          TNode<Uint8T> ctrl = Load<Uint8T>(original, ctrl_table_offset);

          // TODO(v8:11330) Entries in the PropertyDetails table may be
          // uninitialized if the corresponding buckets in the data/ctrl table
          // are empty. Therefore, to avoid accessing un-initialized memory
          // here, we need to check the ctrl table to determine whether we
          // should copy a certain PropertyDetails entry or not.
          // TODO(v8:11330) If this function becomes performance-critical, we
          // may consider always initializing the PropertyDetails table entirely
          // during allocation, to avoid the branching during copying.
          Label done(this);
          // |kNotFullMask| catches kEmpty and kDeleted, both of which indicate
          // entries that we don't want to copy the PropertyDetails for.
          GotoIf(IsSetWord32(ctrl, swiss_table::kNotFullMask), &done);

          TNode<Uint8T> details =
              Load<Uint8T>(original, details_table_offset_minus_tag.value());

          StoreToObject(MachineRepresentation::kWord8, table,
                        details_table_offset_minus_tag.value(), details,
                        StoreToObjectWriteBarrier::kNone);
          Goto(&done);
          BIND(&done);

          details_table_offset_minus_tag =
              IntPtrAdd(details_table_offset_minus_tag.value(),
                        IntPtrConstant(kOneByteSize));
        },
        kOneByteSize, LoopUnrollingMode::kNo, IndexAdvanceMode::kPost);
  }

  Comment("CopySwissNameDictionary ]");

  return table;
}

TNode<IntPtrT> CodeStubAssembler::SwissNameDictionaryOffsetIntoDataTableMT(
    TNode<SwissNameDictionary> dict, TNode<IntPtrT> index, int field_index) {
  TNode<IntPtrT> data_table_start = SwissNameDictionaryDataTableStartOffsetMT();

  TNode<IntPtrT> offset_within_data_table = IntPtrMul(
      index,
      IntPtrConstant(SwissNameDictionary::kDataTableEntryCount * kTaggedSize));

  if (field_index != 0) {
    offset_within_data_table = IntPtrAdd(
        offset_within_data_table, IntPtrConstant(field_index * kTaggedSize));
  }

  return IntPtrAdd(data_table_start, offset_within_data_table);
}

TNode<IntPtrT>
CodeStubAssembler::SwissNameDictionaryOffsetIntoPropertyDetailsTableMT(
    TNode<SwissNameDictionary> dict, TNode<IntPtrT> capacity,
    TNode<IntPtrT> index) {
  CSA_DCHECK(this,
             WordEqual(capacity, ChangeUint32ToWord(
                                     LoadSwissNameDictionaryCapacity(dict))));

  TNode<IntPtrT> data_table_start = SwissNameDictionaryDataTableStartOffsetMT();

  TNode<IntPtrT> gw = IntPtrConstant(SwissNameDictionary::kGroupWidth);
  TNode<IntPtrT> data_and_ctrl_table_size = IntPtrAdd(
      IntPtrMul(capacity,
                IntPtrConstant(kOneByteSize +
                               SwissNameDictionary::kDataTableEntryCount *
                                   kTaggedSize)),
      gw);

  TNode<IntPtrT> property_details_table_start =
      IntPtrAdd(data_table_start, data_and_ctrl_table_size);

  CSA_DCHECK(
      this,
      WordEqual(FieldSliceSwissNameDictionaryPropertyDetailsTable(dict).offset,
                // Our calculation subtracted the tag, Torque's offset didn't.
                IntPtrAdd(property_details_table_start,
                          IntPtrConstant(kHeapObjectTag))));

  TNode<IntPtrT> offset_within_details_table = index;
  return IntPtrAdd(property_details_table_start, offset_within_details_table);
}

void CodeStubAssembler::StoreSwissNameDictionaryCapacity(
    TNode<SwissNameDictionary> table, TNode<Int32T> capacity) {
  StoreObjectFieldNoWriteBarrier<Word32T>(
      table, SwissNameDictionary::CapacityOffset(), capacity);
}

TNode<Name> CodeStubAssembler::LoadSwissNameDictionaryKey(
    TNode<SwissNameDictionary> dict, TNode<IntPtrT> entry) {
  TNode<IntPtrT> offset_minus_tag = SwissNameDictionaryOffsetIntoDataTableMT(
      dict, entry, SwissNameDictionary::kDataTableKeyEntryIndex);

  // TODO(v8:11330) Consider using LoadObjectField here.
  return CAST(Load<Object>(dict, offset_minus_tag));
}

TNode<Uint8T> CodeStubAssembler::LoadSwissNameDictionaryPropertyDetails(
    TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity,
    TNode<IntPtrT> entry) {
  TNode<IntPtrT> offset_minus_tag =
      SwissNameDictionaryOffsetIntoPropertyDetailsTableMT(table, capacity,
                                                          entry);
  // TODO(v8:11330) Consider using LoadObjectField here.
  return Load<Uint8T>(table, offset_minus_tag);
}

void CodeStubAssembler::StoreSwissNameDictionaryPropertyDetails(
    TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity,
    TNode<IntPtrT> entry, TNode<Uint8T> details) {
  TNode<IntPtrT> offset_minus_tag =
      SwissNameDictionaryOffsetIntoPropertyDetailsTableMT(table, capacity,
                                                          entry);

  // TODO(v8:11330) Consider using StoreObjectField here.
  StoreToObject(MachineRepresentation::kWord8, table, offset_minus_tag, details,
                StoreToObjectWriteBarrier::kNone);
}

void CodeStubAssembler::StoreSwissNameDictionaryKeyAndValue(
    TNode<SwissNameDictionary> dict, TNode<IntPtrT> entry, TNode<Object> key,
    TNode<Object> value) {
  static_assert(SwissNameDictionary::kDataTableKeyEntryIndex == 0);
  static_assert(SwissNameDictionary::kDataTableValueEntryIndex == 1);

  // TODO(v8:11330) Consider using StoreObjectField here.
  TNode<IntPtrT> key_offset_minus_tag =
      SwissNameDictionaryOffsetIntoDataTableMT(
          dict, entry, SwissNameDictionary::kDataTableKeyEntryIndex);
  StoreToObject(MachineRepresentation::kTagged, dict, key_offset_minus_tag, key,
                StoreToObjectWriteBarrier::kFull);

  TNode<IntPtrT> value_offset_minus_tag =
      IntPtrAdd(key_offset_minus_tag, IntPtrConstant(kTaggedSize));
  StoreToObject(MachineRepresentation::kTagged, dict, value_offset_minus_tag,
                value, StoreToObjectWriteBarrier::kFull);
}

TNode<Uint64T> CodeStubAssembler::LoadSwissNameDictionaryCtrlTableGroup(
    TNode<IntPtrT> address) {
  TNode<RawPtrT> ptr = ReinterpretCast<RawPtrT>(address);
  TNode<Uint64T> data = UnalignedLoad<Uint64T>(ptr, IntPtrConstant(0));

#ifdef V8_TARGET_LITTLE_ENDIAN
  return data;
#else
  // Reverse byte order.
  // TODO(v8:11330) Doing this without using dedicated instructions (which we
  // don't have access to here) will destroy any performance benefit Swiss
  // Tables have. So we just support this so that we don't have to disable the
  // test suite for SwissNameDictionary on big endian platforms.

  TNode<Uint64T> result = Uint64Constant(0);
  constexpr int count = sizeof(uint64_t);
  for (int i = 0; i < count; ++i) {
    int src_offset = i * 8;
    int dest_offset = (count - i - 1) * 8;

    TNode<Uint64T> mask = Uint64Constant(0xffULL << src_offset);
    TNode<Uint64T> src_data = Word64And(data, mask);

    TNode<Uint64T> shifted =
        src_offset < dest_offset
            ? Word64Shl(src_data, Uint64Constant(dest_offset - src_offset))
            : Word64Shr(src_data, Uint64Constant(src_offset - dest_offset));
    result = Unsigned(Word64Or(result, shifted));
  }
  return result;
#endif
}

void CodeStubAssembler::SwissNameDictionarySetCtrl(
    TNode<SwissNameDictionary> table, TNode<IntPtrT> capacity,
    TNode<IntPtrT> entry, TNode<Uint8T> ctrl) {
  CSA_DCHECK(this,
             WordEqual(capacity, ChangeUint32ToWord(
                                     LoadSwissNameDictionaryCapacity(table))));
  CSA_DCHECK(this, UintPtrLessThan(entry, capacity));

  TNode<IntPtrT> one = IntPtrConstant(1);
  TNode<IntPtrT> offset = SwissNameDictionaryCtrlTableStartOffsetMT(capacity);

  CSA_DCHECK(this,
             WordEqual(FieldSliceSwissNameDictionaryCtrlTable(table).offset,
                       IntPtrAdd(offset, one)));

  TNode<IntPtrT> offset_entry = IntPtrAdd(offset, entry);
  StoreToObject(MachineRepresentation::kWord8, table, offset_entry, ctrl,
                StoreToObjectWriteBarrier::kNone);

  TNode<IntPtrT> mask = IntPtrSub(capacity, one);
  TNode<IntPtrT> group_width = IntPtrConstant(SwissNameDictionary::kGroupWidth);

  // See SwissNameDictionary::SetCtrl for description of what's going on here.

  // ((entry - Group::kWidth) & mask) + 1
  TNode<IntPtrT> copy_entry_lhs =
      IntPtrAdd(WordAnd(IntPtrSub(entry, group_width), mask), one);
  // ((Group::kWidth - 1) & mask)
  TNode<IntPtrT> copy_entry_rhs = WordAnd(IntPtrSub(group_width, one), mask);
  TNode<IntPtrT> copy_entry = IntPtrAdd(copy_entry_lhs, copy_entry_rhs);
  TNode<IntPtrT> offset_copy_entry = IntPtrAdd(offset, copy_entry);

  // |entry| < |kGroupWidth| implies |copy_entry| == |capacity| + |entry|
  CSA_DCHECK(this, Word32Or(UintPtrGreaterThanOrEqual(entry, group_width),
                            WordEqual(copy_entry, IntPtrAdd(capacity, entry))));

  // |entry| >= |kGroupWidth| implies |copy_entry| == |entry|
  CSA_DCHECK(this, Word32Or(UintPtrLessThan(entry, group_width),
                            WordEqual(copy_entry, entry)));

  // TODO(v8:11330): consider using StoreObjectFieldNoWriteBarrier here.
  StoreToObject(MachineRepresentation::kWord8, table, offset_copy_entry, ctrl,
                StoreToObjectWriteBarrier::kNone);
}

void CodeStubAssembler::SwissNameDictionaryFindEntry(
    TNode<SwissNameDictionary> table, TNode<Name> key, Label* found,
    TVariable<IntPtrT>* var_found_entry, Label* not_found) {
  if (SwissNameDictionary::kUseSIMD) {
    SwissNameDictionaryFindEntrySIMD(table, key, found, var_found_entry,
                                     not_found);
  } else {
    SwissNameDictionaryFindEntryPortable(table, key, found, var_found_entry,
                                         not_found);
  }
}

void CodeStubAssembler::SwissNameDictionaryAdd(TNode<SwissNameDictionary> table,
                                               TNode<Name> key,
                                               TNode<Object> value,
                                               TNode<Uint8T> property_details,
                                               Label* needs_resize) {
  if (SwissNameDictionary::kUseSIMD) {
    SwissNameDictionaryAddSIMD(table, key, value, property_details,
                               needs_resize);
  } else {
    SwissNameDictionaryAddPortable(table, key, value, property_details,
                                   needs_resize);
  }
}

void CodeStubAssembler::SharedValueBarrier(
    TNode<Context> context, TVariable<Object>* var_shared_value) {
  // The barrier ensures that the value can be shared across Isolates.
  // The fast paths should be kept in sync with Object::Share.

  TNode<Object> value = var_shared_value->value();
  Label check_in_shared_heap(this), slow(this), skip_barrier(this), done(this);

  // Fast path: Smis are trivially shared.
  GotoIf(TaggedIsSmi(value), &done);
  // Fast path: Shared memory features imply shared RO space, so RO objects are
  // trivially shared.
  CSA_DCHECK(this, BoolConstant(ReadOnlyHeap::IsReadOnlySpaceShared()));
  TNode<IntPtrT> page_flags = LoadMemoryChunkFlags(CAST(value));
  GotoIf(WordNotEqual(
             WordAnd(page_flags, IntPtrConstant(MemoryChunk::READ_ONLY_HEAP)),
             IntPtrConstant(0)),
         &skip_barrier);

  // Fast path: Check if the HeapObject is already shared.
  TNode<Uint16T> value_instance_type =
      LoadMapInstanceType(LoadMap(CAST(value)));
  GotoIf(IsSharedStringInstanceType(value_instance_type), &skip_barrier);
  GotoIf(IsAlwaysSharedSpaceJSObjectInstanceType(value_instance_type),
         &skip_barrier);
  GotoIf(IsHeapNumberInstanceType(value_instance_type), &check_in_shared_heap);
  Goto(&slow);

  BIND(&check_in_shared_heap);
  {
    Branch(WordNotEqual(
               WordAnd(page_flags,
                       IntPtrConstant(MemoryChunk::IN_WRITABLE_SHARED_SPACE)),
               IntPtrConstant(0)),
           &skip_barrier, &slow);
  }

  // Slow path: Call out to runtime to share primitives and to throw on
  // non-shared JS objects.
  BIND(&slow);
  {
    *var_shared_value =
        CallRuntime(Runtime::kSharedValueBarrierSlow, context, value);
    Goto(&skip_barrier);
  }

  BIND(&skip_barrier);
  {
    CSA_DCHECK(
        this,
        WordNotEqual(
            WordAnd(LoadMemoryChunkFlags(CAST(var_shared_value->value())),
                    IntPtrConstant(MemoryChunk::READ_ONLY_HEAP |
                                   MemoryChunk::IN_WRITABLE_SHARED_SPACE)),
            IntPtrConstant(0)));
    Goto(&done);
  }

  BIND(&done);
}

TNode<ArrayList> CodeStubAssembler::AllocateArrayList(TNode<Smi> capacity) {
  TVARIABLE(ArrayList, result);
  Label empty(this), nonempty(this), done(this);

  Branch(SmiEqual(capacity, SmiConstant(0)), &empty, &nonempty);

  BIND(&nonempty);
  {
    CSA_DCHECK(this, SmiGreaterThan(capacity, SmiConstant(0)));

    intptr_t capacity_constant;
    if (ToParameterConstant(capacity, &capacity_constant)) {
      CHECK_LE(capacity_constant, ArrayList::kMaxCapacity);
    } else {
      Label if_out_of_memory(this, Label::kDeferred), next(this);
      Branch(SmiGreaterThan(capacity, SmiConstant(ArrayList::kMaxCapacity)),
             &if_out_of_memory, &next);

      BIND(&if_out_of_memory);
      CallRuntime(Runtime::kFatalProcessOutOfMemoryInvalidArrayLength,
                  NoContextConstant());
      Unreachable();

      BIND(&next);
    }

    TNode<IntPtrT> total_size = GetArrayAllocationSize(
        capacity, PACKED_ELEMENTS, ArrayList::Shape::kHeaderSize);
    TNode<HeapObject> array = Allocate(total_size);
    RootIndex map_index = RootIndex::kArrayListMap;
    DCHECK(RootsTable::IsImmortalImmovable(map_index));
    StoreMapNoWriteBarrier(array, map_index);
    StoreObjectFieldNoWriteBarrier(array, ArrayList::Shape::kCapacityOffset,
                                   capacity);
    StoreObjectFieldNoWriteBarrier(array, ArrayList::Shape::kLengthOffset,
                                   SmiConstant(0));

    TNode<IntPtrT> offset_of_first_element =
        IntPtrConstant(ArrayList::OffsetOfElementAt(0));
    BuildFastLoop<IntPtrT>(
        IntPtrConstant(0), SmiUntag(capacity),
        [=, this](TNode<IntPtrT> index) {
          TNode<IntPtrT> offset =
              IntPtrAdd(TimesTaggedSize(index), offset_of_first_element);
          StoreObjectFieldNoWriteBarrier(array, offset, UndefinedConstant());
        },
        1, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);

    result = UncheckedCast<ArrayList>(array);

    Goto(&done);
  }

  BIND(&empty);
  {
    result = EmptyArrayListConstant();
    Goto(&done);
  }

  BIND(&done);
  return result.value();
}

TNode<ArrayList> CodeStubAssembler::ArrayListEnsureSpace(TNode<ArrayList> array,
                                                         TNode<Smi> length) {
  Label overflow(this, Label::kDeferred);
  TNode<Smi> capacity = LoadFixedArrayBaseLength(array);
  TNode<Smi> requested_capacity = length;

  Label done(this);
  TVARIABLE(ArrayList, result_array, array);

  GotoIf(SmiGreaterThanOrEqual(capacity, requested_capacity), &done);

  // new_capacity = new_length;
  // new_capacity = capacity + max(capacity / 2, 2);
  //
  // Ensure calculation matches ArrayList::EnsureSpace.
  TNode<Smi> new_capacity = TrySmiAdd(
      requested_capacity, SmiMax(SmiShr(requested_capacity, 1), SmiConstant(2)),
      &overflow);
  TNode<ArrayList> new_array = AllocateArrayList(new_capacity);
  TNode<Smi> array_length = ArrayListGetLength(array);
  result_array = new_array;
  GotoIf(SmiEqual(array_length, SmiConstant(0)), &done);
  StoreObjectFieldNoWriteBarrier(new_array, ArrayList::Shape::kLengthOffset,
                                 array_length);
  CopyRange(new_array, ArrayList::OffsetOfElementAt(0), array,
            ArrayList::OffsetOfElementAt(0), SmiUntag(array_length));
  Goto(&done);

  BIND(&overflow);
  CallRuntime(Runtime::kFatalInvalidSize, NoContextConstant());
  Unreachable();

  BIND(&done);
  return result_array.value();
}

TNode<ArrayList> CodeStubAssembler::ArrayListAdd(TNode<ArrayList> array,
                                                 TNode<Object> object) {
  TNode<Smi> length = ArrayListGetLength(array);
  TNode<Smi> new_length = SmiAdd(length, SmiConstant(1));
  TNode<ArrayList> array_with_space = ArrayListEnsureSpace(array, new_length);

  CSA_DCHECK(this, SmiEqual(ArrayListGetLength(array_with_space), length));

  ArrayListSet(array_with_space, length, object);
  ArrayListSetLength(array_with_space, new_length);

  return array_with_space;
}

void CodeStubAssembler::ArrayListSet(TNode<ArrayList> array, TNode<Smi> index,
                                     TNode<Object> object) {
  UnsafeStoreArrayElement(array, index, object);
}

TNode<Smi> CodeStubAssembler::ArrayListGetLength(TNode<ArrayList> array) {
  return CAST(LoadObjectField(array, ArrayList::Shape::kLengthOffset));
}

void CodeStubAssembler::ArrayListSetLength(TNode<ArrayList> array,
                                           TNode<Smi> length) {
  StoreObjectField(array, ArrayList::Shape::kLengthOffset, length);
}

TNode<FixedArray> CodeStubAssembler::ArrayListElements(TNode<ArrayList> array) {
  static constexpr ElementsKind kind = ElementsKind::PACKED_ELEMENTS;
  TNode<IntPtrT> length = PositiveSmiUntag(ArrayListGetLength(array));
  TNode<FixedArray> elements = CAST(AllocateFixedArray(kind, length));
  CopyRange(elements, FixedArray::OffsetOfElementAt(0), array,
            ArrayList::OffsetOfElementAt(0), length);
  return elements;
}

#if V8_ENABLE_WEBASSEMBLY
TNode<RawPtrT> CodeStubAssembler::SwitchToTheCentralStack() {
  TNode<WordT> stack_limit_slot = IntPtrAdd(
      LoadFramePointer(),
      IntPtrConstant(WasmToJSWrapperConstants::kSecondaryStackLimitOffset));

  TNode<ExternalReference> do_switch = ExternalConstant(
      ExternalReference::wasm_switch_to_the_central_stack_for_js());
  TNode<RawPtrT> central_stack_sp = TNode<RawPtrT>::UncheckedCast(CallCFunction(
      do_switch, MachineType::Pointer(),
      std::make_pair(MachineType::Pointer(),
                     ExternalConstant(ExternalReference::isolate_address())),
      std::make_pair(MachineType::Pointer(), stack_limit_slot)));

  TNode<RawPtrT> old_sp = LoadStackPointer();
  SetStackPointer(central_stack_sp);
  StoreNoWriteBarrier(
      MachineType::PointerRepresentation(), LoadFramePointer(),
      IntPtrConstant(WasmToJSWrapperConstants::kCentralStackSPOffset),
      central_stack_sp);
  return old_sp;
}

void CodeStubAssembler::SwitchFromTheCentralStack(TNode<RawPtrT> old_sp) {
  TNode<WordT> stack_limit = Load<RawPtrT>(
      LoadFramePointer(),
      IntPtrConstant(WasmToJSWrapperConstants::kSecondaryStackLimitOffset));

  TNode<ExternalReference> do_switch = ExternalConstant(
      ExternalReference::wasm_switch_from_the_central_stack_for_js());
  CallCFunction(
      do_switch, MachineType::Pointer(),
      std::make_pair(MachineType::Pointer(),
                     ExternalConstant(ExternalReference::isolate_address())),
      std::make_pair(MachineType::Pointer(), stack_limit));

  StoreNoWriteBarrier(
      MachineType::PointerRepresentation(), LoadFramePointer(),
      IntPtrConstant(WasmToJSWrapperConstants::kCentralStackSPOffset),
      IntPtrConstant(0));
  SetStackPointer(old_sp);
}

TNode<RawPtrT> CodeStubAssembler::SwitchToTheCentralStackIfNeeded() {
  TVARIABLE(RawPtrT, old_sp, PointerConstant(nullptr));
  Label no_switch(this);
  Label end(this);  // -> return value of the call (kTaggedPointer)
  TNode<Uint8T> is_on_central_stack_flag = LoadUint8FromRootRegister(
      IntPtrConstant(IsolateData::is_on_central_stack_flag_offset()));
  GotoIf(is_on_central_stack_flag, &no_switch);
  old_sp = SwitchToTheCentralStack();
  Goto(&no_switch);
  Bind(&no_switch);
  return old_sp.value();
}

#endif

TNode<BoolT> CodeStubAssembler::IsMarked(TNode<Object> object) {
  TNode<IntPtrT> cell;
  TNode<IntPtrT> mask;
  GetMarkBit(BitcastTaggedToWordForTagAndSmiBits(object), &cell, &mask);
  // Marked only requires checking a single bit here.
  return WordNotEqual(WordAnd(Load<IntPtrT>(cell), mask), IntPtrConstant(0));
}

void CodeStubAssembler::GetMarkBit(TNode<IntPtrT> object, TNode<IntPtrT>* cell,
                                   TNode<IntPtrT>* mask) {
  TNode<IntPtrT> page = PageMetadataFromAddress(object);
  TNode<IntPtrT> bitmap =
      IntPtrAdd(page, IntPtrConstant(MemoryChunkLayout::kMarkingBitmapOffset));

  {
    // Temp variable to calculate cell offset in bitmap.
    TNode<WordT> r0;
    int shift = MarkingBitmap::kBitsPerCellLog2 + kTaggedSizeLog2 -
                MarkingBitmap::kBytesPerCellLog2;
    r0 = WordShr(object, IntPtrConstant(shift));
    r0 = WordAnd(
        r0,
        IntPtrConstant((MemoryChunk::GetAlignmentMaskForAssembler() >> shift) &
                       ~(MarkingBitmap::kBytesPerCell - 1)));
    *cell = IntPtrAdd(bitmap, Signed(r0));
  }
  {
    // Temp variable to calculate bit offset in cell.
    TNode<WordT> r1;
    r1 = WordShr(object, IntPtrConstant(kTaggedSizeLog2));
    r1 =
        WordAnd(r1, IntPtrConstant((1 << MarkingBitmap::kBitsPerCellLog2) - 1));
    // It seems that LSB(e.g. cl) is automatically used, so no manual masking
    // is needed. Uncomment the following line otherwise.
    // WordAnd(r1, IntPtrConstant((1 << kBitsPerByte) - 1)));
    *mask = WordShl(IntPtrConstant(1), r1);
  }
}

}  // namespace internal
}  // namespace v8
