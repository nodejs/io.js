// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/operator-properties.h"

#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/opcodes.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {
namespace compiler {

// static
bool OperatorProperties::HasContextInput(const Operator* op) {
  IrOpcode::Value opcode = static_cast<IrOpcode::Value>(op->opcode());
  return IrOpcode::IsJsOpcode(opcode);
}

// static
bool OperatorProperties::NeedsExactContext(const Operator* op) {
  DCHECK(HasContextInput(op));
  IrOpcode::Value const opcode = static_cast<IrOpcode::Value>(op->opcode());
  switch (opcode) {
#define CASE(Name, ...) case IrOpcode::k##Name:
    // Binary/unary operators, calls and constructor calls only
    // need the context to generate exceptions or lookup fields
    // on the native context, so passing any context is fine.
    JS_SIMPLE_BINOP_LIST(CASE)
    JS_CALL_OP_LIST(CASE)
    JS_CONSTRUCT_OP_LIST(CASE)
    JS_SIMPLE_UNOP_LIST(CASE)
#undef CASE
    case IrOpcode::kJSCloneObject:
    case IrOpcode::kJSCreate:
    case IrOpcode::kJSCreateLiteralArray:
    case IrOpcode::kJSCreateEmptyLiteralArray:
    case IrOpcode::kJSCreateLiteralObject:
    case IrOpcode::kJSCreateEmptyLiteralObject:
    case IrOpcode::kJSCreateArrayFromIterable:
    case IrOpcode::kJSCreateLiteralRegExp:
    case IrOpcode::kJSGetTemplateObject:
    case IrOpcode::kJSForInEnumerate:
    case IrOpcode::kJSForInNext:
    case IrOpcode::kJSForInPrepare:
    case IrOpcode::kJSGeneratorRestoreContext:
    case IrOpcode::kJSGeneratorRestoreContinuation:
    case IrOpcode::kJSGeneratorRestoreInputOrDebugPos:
    case IrOpcode::kJSGeneratorRestoreRegister:
    case IrOpcode::kJSGetSuperConstructor:
    case IrOpcode::kJSLoadGlobal:
    case IrOpcode::kJSLoadMessage:
    case IrOpcode::kJSStackCheck:
    case IrOpcode::kJSStoreMessage:
    case IrOpcode::kJSGetIterator:
      return false;

    case IrOpcode::kJSCallRuntime:
      return Runtime::NeedsExactContext(CallRuntimeParametersOf(op).id());

    case IrOpcode::kJSCreateArguments:
      // For mapped arguments we need to access slots of context-allocated
      // variables if there's aliasing with formal parameters.
      return CreateArgumentsTypeOf(op) == CreateArgumentsType::kMappedArguments;

    case IrOpcode::kJSCreateBlockContext:
    case IrOpcode::kJSCreateClosure:
    case IrOpcode::kJSCreateFunctionContext:
    case IrOpcode::kJSCreateGeneratorObject:
    case IrOpcode::kJSCreateCatchContext:
    case IrOpcode::kJSCreateWithContext:
    case IrOpcode::kJSDebugger:
    case IrOpcode::kJSDefineKeyedOwnProperty:
    case IrOpcode::kJSDeleteProperty:
    case IrOpcode::kJSGeneratorStore:
    case IrOpcode::kJSGetImportMeta:
    case IrOpcode::kJSHasProperty:
    case IrOpcode::kJSHasContextExtension:
    case IrOpcode::kJSLoadContext:
    case IrOpcode::kJSLoadModule:
    case IrOpcode::kJSLoadNamed:
    case IrOpcode::kJSLoadNamedFromSuper:
    case IrOpcode::kJSLoadProperty:
    case IrOpcode::kJSStoreContext:
    case IrOpcode::kJSStoreScriptContext:
    case IrOpcode::kJSDefineKeyedOwnPropertyInLiteral:
    case IrOpcode::kJSStoreGlobal:
    case IrOpcode::kJSStoreInArrayLiteral:
    case IrOpcode::kJSStoreModule:
    case IrOpcode::kJSSetNamedProperty:
    case IrOpcode::kJSDefineNamedOwnProperty:
    case IrOpcode::kJSSetKeyedProperty:
    case IrOpcode::kJSFindNonDefaultConstructorOrConstruct:
      return true;

    case IrOpcode::kJSAsyncFunctionEnter:
    case IrOpcode::kJSAsyncFunctionReject:
    case IrOpcode::kJSAsyncFunctionResolve:
    case IrOpcode::kJSCreateArrayIterator:
    case IrOpcode::kJSCreateAsyncFunctionObject:
    case IrOpcode::kJSCreateBoundFunction:
    case IrOpcode::kJSCreateCollectionIterator:
    case IrOpcode::kJSCreateIterResultObject:
    case IrOpcode::kJSCreateStringIterator:
    case IrOpcode::kJSCreateKeyValueArray:
    case IrOpcode::kJSCreateObject:
    case IrOpcode::kJSCreatePromise:
    case IrOpcode::kJSCreateTypedArray:
    case IrOpcode::kJSCreateArray:
    case IrOpcode::kJSFulfillPromise:
    case IrOpcode::kJSObjectIsArray:
    case IrOpcode::kJSPerformPromiseThen:
    case IrOpcode::kJSPromiseResolve:
    case IrOpcode::kJSRegExpTest:
    case IrOpcode::kJSRejectPromise:
    case IrOpcode::kJSResolvePromise:
      // These operators aren't introduced by BytecodeGraphBuilder and
      // thus we don't bother checking them. If you ever introduce one
      // of these early in the BytecodeGraphBuilder make sure to check
      // whether they are context-sensitive.
      break;

#define CASE(Name) case IrOpcode::k##Name:
      // Non-JavaScript operators don't have a notion of "context".
      COMMON_OP_LIST(CASE)
      CONTROL_OP_LIST(CASE)
      MACHINE_OP_LIST(CASE)
      MACHINE_SIMD128_OP_LIST(CASE)
      IF_WASM(MACHINE_SIMD256_OP_LIST, CASE)
      SIMPLIFIED_OP_LIST(CASE)
      break;
#undef CASE
  }
  UNREACHABLE();
}

// static
bool OperatorProperties::HasFrameStateInput(const Operator* op) {
  switch (op->opcode()) {
    case IrOpcode::kCheckpoint:
    case IrOpcode::kFrameState:
      return true;
    case IrOpcode::kJSCallRuntime: {
      const CallRuntimeParameters& p = CallRuntimeParametersOf(op);
      return Linkage::NeedsFrameStateInput(p.id());
    }

    // Strict equality cannot lazily deoptimize.
    case IrOpcode::kJSStrictEqual:
      return false;

    // Generator creation cannot call back into arbitrary JavaScript.
    case IrOpcode::kJSCreateGeneratorObject:
      return false;

    // Binary operations
    case IrOpcode::kJSAdd:
    case IrOpcode::kJSSubtract:
    case IrOpcode::kJSMultiply:
    case IrOpcode::kJSDivide:
    case IrOpcode::kJSModulus:
    case IrOpcode::kJSExponentiate:

    // Bitwise operations
    case IrOpcode::kJSBitwiseOr:
    case IrOpcode::kJSBitwiseXor:
    case IrOpcode::kJSBitwiseAnd:

    // Shift operations
    case IrOpcode::kJSShiftLeft:
    case IrOpcode::kJSShiftRight:
    case IrOpcode::kJSShiftRightLogical:

    // Compare operations
    case IrOpcode::kJSEqual:
    case IrOpcode::kJSGreaterThan:
    case IrOpcode::kJSGreaterThanOrEqual:
    case IrOpcode::kJSLessThan:
    case IrOpcode::kJSLessThanOrEqual:
    case IrOpcode::kJSHasProperty:
    case IrOpcode::kJSHasInPrototypeChain:
    case IrOpcode::kJSInstanceOf:
    case IrOpcode::kJSOrdinaryHasInstance:

    // Object operations
    case IrOpcode::kJSCreate:
    case IrOpcode::kJSCreateArguments:
    case IrOpcode::kJSCreateArray:
    case IrOpcode::kJSCreateTypedArray:
    case IrOpcode::kJSCreateLiteralArray:
    case IrOpcode::kJSCreateArrayFromIterable:
    case IrOpcode::kJSCreateLiteralObject:
    case IrOpcode::kJSCreateLiteralRegExp:
    case IrOpcode::kJSCreateObject:
    case IrOpcode::kJSCloneObject:

    // Property access operations
    case IrOpcode::kJSDeleteProperty:
    case IrOpcode::kJSLoadGlobal:
    case IrOpcode::kJSLoadNamed:
    case IrOpcode::kJSLoadNamedFromSuper:
    case IrOpcode::kJSLoadProperty:
    case IrOpcode::kJSDefineKeyedOwnPropertyInLiteral:
    case IrOpcode::kJSStoreInArrayLiteral:
    case IrOpcode::kJSStoreGlobal:
    case IrOpcode::kJSSetNamedProperty:
    case IrOpcode::kJSDefineNamedOwnProperty:
    case IrOpcode::kJSSetKeyedProperty:
    case IrOpcode::kJSDefineKeyedOwnProperty:

    // Conversions
    case IrOpcode::kJSToLength:
    case IrOpcode::kJSToName:
    case IrOpcode::kJSToNumber:
    case IrOpcode::kJSToNumberConvertBigInt:
    case IrOpcode::kJSToBigInt:
    case IrOpcode::kJSToBigIntConvertNumber:
    case IrOpcode::kJSToNumeric:
    case IrOpcode::kJSToObject:
    case IrOpcode::kJSToString:
    case IrOpcode::kJSParseInt:

    // Call operations
    case IrOpcode::kJSConstructForwardVarargs:
    case IrOpcode::kJSConstruct:
    case IrOpcode::kJSConstructWithArrayLike:
    case IrOpcode::kJSConstructWithSpread:
    case IrOpcode::kJSConstructForwardAllArgs:
    case IrOpcode::kJSCallForwardVarargs:
    case IrOpcode::kJSCall:
    case IrOpcode::kJSCallWithArrayLike:
    case IrOpcode::kJSCallWithSpread:
#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kJSWasmCall:
#endif  // V8_ENABLE_WEBASSEMBLY

    // Misc operations
    case IrOpcode::kJSAsyncFunctionEnter:
    case IrOpcode::kJSAsyncFunctionReject:
    case IrOpcode::kJSAsyncFunctionResolve:
    case IrOpcode::kJSForInEnumerate:
    case IrOpcode::kJSForInNext:
    case IrOpcode::kJSStackCheck:
    case IrOpcode::kJSDebugger:
    case IrOpcode::kJSGetSuperConstructor:
    case IrOpcode::kJSFindNonDefaultConstructorOrConstruct:
    case IrOpcode::kJSBitwiseNot:
    case IrOpcode::kJSDecrement:
    case IrOpcode::kJSIncrement:
    case IrOpcode::kJSNegate:
    case IrOpcode::kJSPromiseResolve:
    case IrOpcode::kJSRejectPromise:
    case IrOpcode::kJSResolvePromise:
    case IrOpcode::kJSPerformPromiseThen:
    case IrOpcode::kJSObjectIsArray:
    case IrOpcode::kJSRegExpTest:
    case IrOpcode::kJSGetImportMeta:

    // Iterator protocol operations
    case IrOpcode::kJSGetIterator:
      return true;

    default:
      return false;
  }
}


// static
int OperatorProperties::GetTotalInputCount(const Operator* op) {
  return op->ValueInputCount() + GetContextInputCount(op) +
         GetFrameStateInputCount(op) + op->EffectInputCount() +
         op->ControlInputCount();
}


// static
bool OperatorProperties::IsBasicBlockBegin(const Operator* op) {
  Operator::Opcode const opcode = op->opcode();
  return opcode == IrOpcode::kStart || opcode == IrOpcode::kEnd ||
         opcode == IrOpcode::kDead || opcode == IrOpcode::kLoop ||
         opcode == IrOpcode::kMerge || opcode == IrOpcode::kIfTrue ||
         opcode == IrOpcode::kIfFalse || opcode == IrOpcode::kIfSuccess ||
         opcode == IrOpcode::kIfException || opcode == IrOpcode::kIfValue ||
         opcode == IrOpcode::kIfDefault;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
