// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter-intrinsics-generator.h"

#include "src/builtins/builtins.h"
#include "src/heap/factory-inl.h"
#include "src/interpreter/interpreter-assembler.h"
#include "src/interpreter/interpreter-intrinsics.h"
#include "src/objects/js-generator.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

class IntrinsicsGenerator {
 public:
  explicit IntrinsicsGenerator(InterpreterAssembler* assembler)
      : isolate_(assembler->isolate()),
        zone_(assembler->zone()),
        assembler_(assembler) {}
  IntrinsicsGenerator(const IntrinsicsGenerator&) = delete;
  IntrinsicsGenerator& operator=(const IntrinsicsGenerator&) = delete;

  TNode<Object> InvokeIntrinsic(
      TNode<Uint32T> function_id, TNode<Context> context,
      const InterpreterAssembler::RegListNodePair& args);

 private:
  TNode<Object> IntrinsicAsBuiltinCall(
      const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
      Builtin name, int arg_count);
  void AbortIfArgCountMismatch(int expected, TNode<Word32T> actual);

#define DECLARE_INTRINSIC_HELPER(name, lower_case, count)               \
  TNode<Object> name(const InterpreterAssembler::RegListNodePair& args, \
                     TNode<Context> context, int arg_count);
  INTRINSICS_LIST(DECLARE_INTRINSIC_HELPER)
#undef DECLARE_INTRINSIC_HELPER

  Isolate* isolate() { return isolate_; }
  Zone* zone() { return zone_; }
  Factory* factory() { return isolate()->factory(); }

  Isolate* isolate_;
  Zone* zone_;
  InterpreterAssembler* assembler_;
};

TNode<Object> GenerateInvokeIntrinsic(
    InterpreterAssembler* assembler, TNode<Uint32T> function_id,
    TNode<Context> context, const InterpreterAssembler::RegListNodePair& args) {
  IntrinsicsGenerator generator(assembler);
  return generator.InvokeIntrinsic(function_id, context, args);
}

#define __ assembler_->

TNode<Object> IntrinsicsGenerator::InvokeIntrinsic(
    TNode<Uint32T> function_id, TNode<Context> context,
    const InterpreterAssembler::RegListNodePair& args) {
  InterpreterAssembler::Label abort(assembler_), end(assembler_);
  InterpreterAssembler::TVariable<Object> result(assembler_);

#define MAKE_LABEL(name, lower_case, count) \
  InterpreterAssembler::Label lower_case(assembler_);
  INTRINSICS_LIST(MAKE_LABEL)
#undef MAKE_LABEL

#define LABEL_POINTER(name, lower_case, count) &lower_case,
  InterpreterAssembler::Label* labels[] = {INTRINSICS_LIST(LABEL_POINTER)};
#undef LABEL_POINTER

#define CASE(name, lower_case, count) \
  static_cast<int32_t>(IntrinsicsHelper::IntrinsicId::k##name),
  int32_t cases[] = {INTRINSICS_LIST(CASE)};
#undef CASE

  __ Switch(function_id, &abort, cases, labels, arraysize(cases));
#define HANDLE_CASE(name, lower_case, expected_arg_count)            \
  __ BIND(&lower_case);                                              \
  {                                                                  \
    if (v8_flags.debug_code && expected_arg_count >= 0) {            \
      AbortIfArgCountMismatch(expected_arg_count, args.reg_count()); \
    }                                                                \
    TNode<Object> value = name(args, context, expected_arg_count);   \
    if (value) {                                                     \
      result = value;                                                \
      __ Goto(&end);                                                 \
    }                                                                \
  }
  INTRINSICS_LIST(HANDLE_CASE)
#undef HANDLE_CASE

  __ BIND(&abort);
  {
    __ Abort(AbortReason::kUnexpectedFunctionIDForInvokeIntrinsic);
    result = __ UndefinedConstant();
    __ Goto(&end);
  }

  __ BIND(&end);
  return result.value();
}

TNode<Object> IntrinsicsGenerator::IntrinsicAsBuiltinCall(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    Builtin builtin, int arg_count) {
  switch (arg_count) {
    case 1:
      return __ CallBuiltin(builtin, context,
                            __ LoadRegisterFromRegisterList(args, 0));
    case 2:
      return __ CallBuiltin(builtin, context,
                            __ LoadRegisterFromRegisterList(args, 0),
                            __ LoadRegisterFromRegisterList(args, 1));
    case 3:
      return __ CallBuiltin(builtin, context,
                            __ LoadRegisterFromRegisterList(args, 0),
                            __ LoadRegisterFromRegisterList(args, 1),
                            __ LoadRegisterFromRegisterList(args, 2));
    default:
      UNREACHABLE();
  }
}

TNode<Object> IntrinsicsGenerator::CopyDataProperties(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context, Builtin::kCopyDataProperties,
                                arg_count);
}

TNode<Object>
IntrinsicsGenerator::CopyDataPropertiesWithExcludedPropertiesOnStack(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  TNode<IntPtrT> offset = __ TimesSystemPointerSize(__ IntPtrConstant(1));
  auto base = __ Signed(__ IntPtrSub(args.base_reg_location(), offset));
  TNode<IntPtrT> excluded_property_count = __ IntPtrSub(
      __ ChangeInt32ToIntPtr(args.reg_count()), __ IntPtrConstant(1));
  return __ CallBuiltin(
      Builtin::kCopyDataPropertiesWithExcludedPropertiesOnStack, context,
      __ LoadRegisterFromRegisterList(args, 0), excluded_property_count, base);
}

TNode<Object> IntrinsicsGenerator::CreateIterResultObject(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context, Builtin::kCreateIterResultObject,
                                arg_count);
}

TNode<Object> IntrinsicsGenerator::CreateAsyncFromSyncIterator(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  TNode<Object> sync_iterator = __ LoadRegisterFromRegisterList(args, 0);
  return __ CreateAsyncFromSyncIterator(context, sync_iterator);
}

TNode<Object> IntrinsicsGenerator::CreateJSGeneratorObject(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context, Builtin::kCreateGeneratorObject,
                                arg_count);
}

TNode<Object> IntrinsicsGenerator::GeneratorGetResumeMode(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  TNode<JSGeneratorObject> generator =
      __ CAST(__ LoadRegisterFromRegisterList(args, 0));
  const TNode<Object> value =
      __ LoadObjectField(generator, JSGeneratorObject::kResumeModeOffset);

  return value;
}

TNode<Object> IntrinsicsGenerator::GeneratorClose(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  TNode<JSGeneratorObject> generator =
      __ CAST(__ LoadRegisterFromRegisterList(args, 0));
  __ StoreObjectFieldNoWriteBarrier(
      generator, JSGeneratorObject::kContinuationOffset,
      __ SmiConstant(JSGeneratorObject::kGeneratorClosed));
  return __ UndefinedConstant();
}

TNode<Object> IntrinsicsGenerator::GetImportMetaObject(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return __ GetImportMetaObject(context);
}

TNode<Object> IntrinsicsGenerator::AsyncFunctionAwaitCaught(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context,
                                Builtin::kAsyncFunctionAwaitCaught, arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncFunctionAwaitUncaught(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(
      args, context, Builtin::kAsyncFunctionAwaitUncaught, arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncFunctionEnter(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context, Builtin::kAsyncFunctionEnter,
                                arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncFunctionReject(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context, Builtin::kAsyncFunctionReject,
                                arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncFunctionResolve(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context, Builtin::kAsyncFunctionResolve,
                                arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncGeneratorAwaitCaught(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context,
                                Builtin::kAsyncGeneratorAwaitCaught, arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncGeneratorAwaitUncaught(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(
      args, context, Builtin::kAsyncGeneratorAwaitUncaught, arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncGeneratorReject(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context, Builtin::kAsyncGeneratorReject,
                                arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncGeneratorResolve(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(args, context, Builtin::kAsyncGeneratorResolve,
                                arg_count);
}

TNode<Object> IntrinsicsGenerator::AsyncGeneratorYieldWithAwait(
    const InterpreterAssembler::RegListNodePair& args, TNode<Context> context,
    int arg_count) {
  return IntrinsicAsBuiltinCall(
      args, context, Builtin::kAsyncGeneratorYieldWithAwait, arg_count);
}

void IntrinsicsGenerator::AbortIfArgCountMismatch(int expected,
                                                  TNode<Word32T> actual) {
  InterpreterAssembler::Label match(assembler_);
  TNode<BoolT> comparison = __ Word32Equal(actual, __ Int32Constant(expected));
  __ GotoIf(comparison, &match);
  __ Abort(AbortReason::kWrongArgumentCountForInvokeIntrinsic);
  __ Goto(&match);
  __ BIND(&match);
}

#undef __

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
