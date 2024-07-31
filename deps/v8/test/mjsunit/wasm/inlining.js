// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-inlining --no-liftoff --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// TODO(12166): Consider running tests with --trace-wasm and inspecting their
// output, or implementing testing infrastructure with --allow-natives-syntax.

(function SimpleInliningTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // f(x) = x - 1
  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub]);
  // g(x) = f(5) + x
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprI32Const, 5, kExprCallFunction, callee.index,
              kExprLocalGet, 0, kExprI32Add])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(14, instance.exports.main(10));
})();

(function MultiReturnTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // f(x) = (x - 1, x + 1)
  let callee = builder.addFunction("callee", kSig_ii_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub,
              kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add]);
  // g(x) = { let (a, b) = f(x); a * b }
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprCallFunction, callee.index, kExprI32Mul])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(9 * 11, instance.exports.main(10));
})();

(function VoidReturnTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let global = builder.addGlobal(kWasmI32, true, false);

  let callee = builder.addFunction("callee", kSig_v_i)
    .addBody([kExprLocalGet, 0, kExprGlobalSet, global.index]);

  builder.addFunction("main", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprCallFunction, callee.index,
              kExprGlobalGet, global.index])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(10, instance.exports.main(10));
})();

(function NoReturnTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprI32Const, 42, kExprThrow, tag]);

  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprTry, kWasmI32,
        kExprI32Const, 10, kExprCallFunction, callee.index,
        kExprI32Const, 10, kExprI32Add,
      kExprCatch, tag,
        kExprI32Const, 1, kExprI32Add,
      kExprEnd])
    .exportFunc();

  let instance = builder.instantiate();
  assertEquals(43, instance.exports.main(10));
})();

(function LoopInLoopTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let fact = builder.addFunction("fact", kSig_i_i)
    .addLocals(kWasmI32, 1)
    .addBody([// result = 1;
              kExprI32Const, 1, kExprLocalSet, 1,
              kExprLoop, kWasmVoid,
                kExprLocalGet, 1,
                // if input == 1 return result;
                kExprLocalGet, 0, kExprI32Const, 1, kExprI32Eq, kExprBrIf, 1,
                // result *= input;
                kExprLocalGet, 0, kExprI32Mul, kExprLocalSet, 1,
                // input -= 1;
                kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub,
                kExprLocalSet, 0,
                kExprBr, 0,
              kExprEnd,
              kExprUnreachable]);

  builder.addFunction("main", kSig_i_i)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 1,
        // if input == 0 return sum;
        kExprLocalGet, 0, kExprI32Const, 0, kExprI32Eq, kExprBrIf, 1,
        // sum += fact(input);
        kExprLocalGet, 0, kExprCallFunction, fact.index,
        kExprI32Add, kExprLocalSet, 1,
        // input -= 1;
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub,
        kExprLocalSet, 0,
        kExprBr, 0,
      kExprEnd,
      kExprUnreachable])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(33, instance.exports.main(4));
})();

(function InfiniteLoopTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprLoop, kWasmVoid,
                kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
                kExprLocalSet, 0, kExprBr, 0,
              kExprEnd,
              kExprLocalGet, 0]);

  builder.addFunction("main", kSig_i_i)
    .addBody([kExprI32Const, 5, kExprCallFunction, callee.index,
              kExprLocalGet, 0, kExprI32Add])
    .exportAs("main");

  builder.instantiate();
})();

(function TailCallInCalleeTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // f(x) = g(x - 1)
  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub,
              kExprReturnCall, 1]);
  // g(x) = x * 2
  builder.addFunction("inner_callee", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 2, kExprI32Mul]);
  // h(x) = f(x) + 5
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprCallFunction, callee.index,
              kExprI32Const, 5, kExprI32Add])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(23, instance.exports.main(10));
})();

(function MultipleCallAndReturnSitesTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // f(x) = x >= 0 ? x - 1 : x + 1
  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 0, kExprI32GeS,
              kExprIf, kWasmI32,
                kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub,
              kExprElse,
                kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
              kExprEnd]);
  // g(x) = f(x) * f(-x)
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprCallFunction, callee.index,
              kExprI32Const, 0, kExprLocalGet, 0, kExprI32Sub,
              kExprCallFunction, callee.index,
              kExprI32Mul])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(-81, instance.exports.main(10));
})();

(function TailCallInCallerTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // f(x) = x > 0 ? g(x) + 1: g(x - 1);
  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 0, kExprI32GeS,
              kExprIf, kWasmI32,
                kExprLocalGet, 0, kExprCallFunction, 1, kExprI32Const, 1,
                kExprI32Add,
              kExprElse,
                kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub,
                kExprReturnCall, 1,
              kExprEnd]);
  // g(x) = x * 2
  builder.addFunction("inner_callee", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 2, kExprI32Mul]);
  // h(x) = f(x + 5)
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 5, kExprI32Add,
              kExprReturnCall, callee.index])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(31, instance.exports.main(10));
  assertEquals(-12, instance.exports.main(-10));
})();

(function HandledInHandledTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprTry, kWasmI32,
                kExprI32Const, 42,
                kExprThrow, tag,
              kExprCatchAll,
                kExprLocalGet, 0,
              kExprEnd]);

  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprTry, kWasmI32,
                kExprLocalGet, 0,
                kExprCallFunction, callee.index,
              kExprCatchAll,
                kExprLocalGet, 1,
              kExprEnd])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(10, instance.exports.main(10, 20));
})();

(function HandledInUnhandledTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprTry, kWasmI32,
                kExprI32Const, 42,
                kExprThrow, tag,
              kExprCatchAll,
                kExprLocalGet, 0,
              kExprEnd]);

  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprLocalGet, 0,
              kExprCallFunction, callee.index,])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(10, instance.exports.main(10, 20));
})();

(function UnhandledInUnhandledTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprI32Const, 42, kExprThrow, tag]);

  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprLocalGet, 0,
              kExprCallFunction, callee.index])
    .exportAs("main");

  let instance = builder.instantiate();
  assertThrows(() => instance.exports.main(10, 20), WebAssembly.Exception);
})();

// This is the most interesting of the exception tests, as it requires rewiring
// the unhandled calls in the callee (including the 'throw' builtin) to the
// handler in the caller.
(function UnhandledInHandledTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kWasmI32,
        kExprLocalGet, 0, kExprThrow, tag,
      kExprElse,
        kExprCallFunction, 1,
      kExprEnd]);

  builder.addFunction("unreachable", kSig_i_v)
    .addBody([kExprUnreachable]);

  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprTry, kWasmI32,
                kExprLocalGet, 0,
                kExprCallFunction, callee.index,
              kExprCatchAll,
                kExprLocalGet, 1,
              kExprEnd])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(20, instance.exports.main(10, 20));
})();

// Inlining should behave correctly when there are no throwing nodes in the
// callee.
(function NoThrowInHandledTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([
      kExprLocalGet, 0, kExprI32Const, 0, kExprI32GeS,
      kExprIf, kWasmI32,
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
      kExprElse,
        kExprLocalGet, 0, kExprI32Const, 2, kExprI32Sub,
      kExprEnd]);

  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprTry, kWasmI32,
                kExprLocalGet, 0,
                kExprCallFunction, callee.index,
              kExprCatchAll,
                kExprLocalGet, 1,
              kExprEnd])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(11, instance.exports.main(10, 20));
})();

// Things get more complex if we also need to reload the memory context.
(function UnhandledInHandledWithMemoryTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let sig = builder.addType(kSig_i_i);

  builder.addMemory(10, 100);

  let inner_callee = builder.addFunction("inner_callee", kSig_i_i)
    .addBody([kExprLocalGet, 0]).exportFunc();

  // f(x, y) = { do { y += 1; x -= 1; } while (x > 0); return y; }
  let callee = builder.addFunction("callee", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add,
      kExprRefFunc, inner_callee.index, kExprCallRef, sig]);
  // g(x) = f(5, x) + x
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprTry, kWasmI32,
                kExprI32Const, 5, kExprLocalGet, 0,
                kExprCallFunction, callee.index,
              kExprCatchAll,
                kExprI32Const, 0,
              kExprEnd,
              kExprLocalGet, 0, kExprI32Add,
              kExprI32Const, 10, kExprI32LoadMem, 0, 0, kExprI32Add])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(25, instance.exports.main(10));
})();

(function TailCallInCatchBlock() {
  // A tail call frame replaces the caller frame, so an exception should not be
  // catchable in the caller frame.
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprI32Const, 42, kExprThrow, tag])

  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprTry, kWasmI32,
        kExprLocalGet, 0, kExprReturnCall, callee.index,
      kExprCatch, tag,
      kExprEnd])
    .exportFunc();

  let instance = builder.instantiate();

  assertThrows(() => instance.exports.main(10), WebAssembly.Exception);
})();

(function TailCallInNestedCallInCatchBlock() {
  // In contrast to the previous test, if there is an intermediate frame, the
  // parent frame should be able to catch the exception.
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprI32Const, 42, kExprThrow, tag]);

  let intermediate = builder.addFunction("intermediate", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprReturnCall, callee.index]);

  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprTry, kWasmI32,
        kExprLocalGet, 0, kExprCallFunction, intermediate.index,
      kExprCatch, tag,
      kExprEnd])
    .exportFunc();

  let instance = builder.instantiate();

  assertEquals(42, instance.exports.main(10));
})();

(function CallInTailCallInCatchBlock() {
  // A tail call removes the current frame, so an exception should not be
  // caught even from within a nested inlined call.
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let tag = builder.addTag(kSig_v_i);

  let callee = builder.addFunction("callee", kSig_i_i)
    .addBody([kExprI32Const, 42, kExprThrow, tag]);

  let intermediate = builder.addFunction("intermediate", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
              kExprCallFunction, callee.index]);

  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprTry, kWasmI32,
        kExprLocalGet, 0, kExprReturnCall, intermediate.index,
      kExprCatch, tag,
      kExprEnd])
    .exportFunc();

  let instance = builder.instantiate();

  assertThrows(() => instance.exports.main(10), WebAssembly.Exception);
})();

(function LoopUnrollingTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // f(x, y) = { do { y += 1; x -= 1; } while (x > 0); return y; }
  let callee = builder.addFunction("callee", kSig_i_ii)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 1, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 1,
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub, kExprLocalSet, 0,
        kExprLocalGet, 0, kExprI32Const, 0, kExprI32GtS, kExprBrIf, 0,
      kExprEnd,
      kExprLocalGet, 1
    ]);
  // g(x) = f(5, x) + x
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprI32Const, 5, kExprLocalGet, 0,
              kExprCallFunction, callee.index,
              kExprLocalGet, 0, kExprI32Add])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(25, instance.exports.main(10));
})();

(function ThrowInLoopTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let tag = builder.addTag(kSig_v_i);

  // f(x, y) {
  //   do {
  //     if (x < 0) throw x;
  //     y++; x--;
  //   } while (x > 0);
  //   return y;
  // }
  let callee = builder.addFunction("callee", kSig_i_ii)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0, kExprI32Const, 0, kExprI32LtS,
        kExprIf, kWasmVoid,
          kExprLocalGet, 0, kExprThrow, tag,
        kExprEnd,
        kExprLocalGet, 1, kExprI32Const, 1, kExprI32Add, kExprLocalSet, 1,
        kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub, kExprLocalSet, 0,
        kExprLocalGet, 0, kExprI32Const, 0, kExprI32GtS, kExprBrIf, 0,
      kExprEnd,
      kExprLocalGet, 1
    ]);
  // g(x) = (try { f(x, 5) } catch(x) { x }) + x
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprTry, kWasmI32,
              kExprLocalGet, 0, kExprI32Const, 5,
                kExprCallFunction, callee.index,
              kExprCatch, tag,
              kExprEnd,
              kExprLocalGet, 0, kExprI32Add])
    .exportAs("main");

  let instance = builder.instantiate();
  assertEquals(25, instance.exports.main(10));
  assertEquals(-20, instance.exports.main(-10));
})();

(function InlineSubtypeSignatureTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  let callee = builder
    .addFunction("callee", makeSig([wasmRefNullType(struct)], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct, 0]);

  // When inlining "callee", TF should pass the real parameter type (ref 0) and
  // thus eliminate the null check for struct.get.
  builder.addFunction("main", makeSig([wasmRefType(struct)], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kExprCallFunction, callee.index])
    .exportFunc();

  builder.instantiate({});
})();

(function InliningAndEscapeAnalysisTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  let callee = builder
    .addFunction("callee", makeSig([wasmRefNullType(struct)], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct, 0]);

  // The allocation should be removed.
  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
      kGCPrefix, kExprStructNew, struct,
      kExprCallFunction, callee.index])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(11, instance.exports.main(10));
})();

(function Int64Lowering() {
  print(arguments.callee.name);

  let kSig_l_li = makeSig([kWasmI64, kWasmI32], [kWasmI64]);

  let builder = new WasmModuleBuilder();

  let callee = builder.addFunction("callee", kSig_l_li)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprI64SConvertI32, kExprI64Add]);

  builder.addFunction("main", kSig_l_li)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprCallFunction, callee.index])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(BigInt(21), instance.exports.main(BigInt(10), 11));
})();

(function InliningRecursiveTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let factorial = builder
    .addFunction("factorial", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32LeS,
              kExprIf, kWasmVoid, kExprI32Const, 1, kExprReturn, kExprEnd,
              kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub,
              kExprCallFunction, 0,
              kExprLocalGet, 0, kExprI32Mul]);

  builder.addFunction("main", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprCallFunction, factorial.index])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(1, instance.exports.main(1));
  // {factorial} should not be fully inlined in the trace.
  assertEquals(120, instance.exports.main(5));
})();

// When inlining a function with a tail call into a regular call, the tail call
// has to be transformed into a call. That new call node (or its projections)
// has to be typed.
(function CallFromTailCallMustBeTyped() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let tail_call = builder
    .addFunction("tail_call", makeSig([], [kWasmFuncRef]))
    .addBody([kExprReturnCall, 0]);

  let tail_call_multi = builder
    .addFunction("tail_call", makeSig([], [kWasmFuncRef, kWasmFuncRef]))
    .addBody([kExprReturnCall, 1]);

  builder
    .addFunction("main", makeSig([], [wasmRefType(kWasmFuncRef), kWasmFuncRef,
                                      wasmRefType(kWasmFuncRef)]))
    .addBody([
      kExprCallFunction, tail_call.index, kExprRefAsNonNull,
      kExprCallFunction, tail_call_multi.index, kExprRefAsNonNull])

  builder.instantiate({});
})();

(function InliningTrapFromCallee() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // Add some types to have an index offset.
  for (let i = 0; i < 10; ++i) {
    builder.addFunction(null, makeSig([], [])).addBody([]);
  }

  let callee = builder.addFunction('callee', kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32DivU,
    ]);

  let intermediate = builder.addFunction('intermediate', kSig_i_ii)
    .addBody([
      // Some nops, so that the call doesn't have the same offset as the div
      // in the callee.
      kExprNop, kExprNop,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, callee.index,
    ])
    .exportFunc();

  let caller = builder.addFunction('main', kSig_ii_ii)
    .addBody([
      // Some nops, so that the call doesn't have the same offset as the div
      // in the callee.
      kExprNop, kExprNop, kExprNop, kExprNop, kExprNop,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, intermediate.index,
      // If it didn't trap, call it again without intermediate function and with
      // swapped arguments.
      kExprLocalGet, 1,
      kExprLocalGet, 0,
      kExprCallFunction, callee.index,
    ])
    .exportFunc();

  let wire_bytes = builder.toBuffer();
  let module = new WebAssembly.Module(wire_bytes);
  let instance = new WebAssembly.Instance(module, {});
  TestStackTrace(instance.exports.main);
  // Serialize and deserialize the module to verify that the inlining positions
  // are properly "transformed" here.
  print("Repeat test with serialized module.")
  module = %DeserializeWasmModule(%SerializeWasmModule(module), wire_bytes);
  instance = new WebAssembly.Instance(module, {});
  TestStackTrace(instance.exports.main);

  function TestStackTrace(main) {
    assertEquals([7, 0], main(21, 3));
    assertTraps(kTrapDivByZero, () => main(1, 0));
    try {
      main(1, 0);
      assertUnreachable();
    } catch(e) {
      assertMatches(/RuntimeError: divide by zero/, e.stack);
      let expected_entries = [
        // [name, index, offset]
        ['callee', '' + callee.index, '0x8c'],
        ['intermediate', '' + intermediate.index, '0x96'],
        ['main', '' + caller.index, '0xa4'],
      ];
      CheckCallStack(e, expected_entries);
    }

    try {
      main(0, 1);
      assertUnreachable();
    } catch(e) {
      assertMatches(/RuntimeError: divide by zero/, e.stack);
      let expected_entries = [
        // [name, index, offset]
        ['callee', '' + callee.index, '0x8c'],
        ['main', '' + caller.index, '0xaa'],
      ];
      CheckCallStack(e, expected_entries);
    }
  }

  function CheckCallStack(error, expected_entries) {
    print(error.stack);
    let regex = /at ([^ ]+) \(wasm[^\[]+\[([0-9]+)\]:(0x[0-9a-f]+)\)/g;
    let entries = [...error.stack.matchAll(regex)];
    for (let i = 0; i < expected_entries.length; ++i) {
      let actual = entries[i];
      print(`match = ${actual[0]}`);
      let expected = expected_entries[i];
      assertEquals(expected[0], actual[1]);
      assertEquals(expected[1], actual[2]);
      assertEquals(expected[2], actual[3]);
    }
    assertEquals(expected_entries.length, entries.length);
  }
})();

(function InliningTrapFromCalleeWithNestedTailCall() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // Add some types to have an index offset.
  for (let i = 0; i < 10; ++i) {
    builder.addFunction(null, makeSig([], [])).addBody([]);
  }

  let callee = builder.addFunction('callee', kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32DivU,
    ]);

  let intermediate = builder.addFunction('intermediate', kSig_i_ii)
    .addBody([
      // Some nops, so that the call doesn't have the same offset as the div
      // in the callee.
      kExprNop, kExprNop,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprReturnCall, callee.index,
    ])
    .exportFunc();

  let caller = builder.addFunction('main', kSig_ii_ii)
    .addBody([
      // Some nops, so that the call doesn't have the same offset as the div
      // in the callee.
      kExprNop, kExprNop, kExprNop, kExprNop, kExprNop,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, intermediate.index,
      // If it didn't trap, call it again without intermediate function and with
      // swapped arguments.
      kExprLocalGet, 1,
      kExprLocalGet, 0,
      kExprCallFunction, callee.index,
    ])
    .exportFunc();

  let wire_bytes = builder.toBuffer();
  let module = new WebAssembly.Module(wire_bytes);
  let instance = new WebAssembly.Instance(module, {});
  TestStackTrace(instance.exports.main);
  // Serialize and deserialize the module to verify that the inlining positions
  // are properly "transformed" here.
  print("Repeat test with serialized module.")
  module = %DeserializeWasmModule(%SerializeWasmModule(module), wire_bytes);
  instance = new WebAssembly.Instance(module, {});
  TestStackTrace(instance.exports.main);

  function TestStackTrace(main) {
    assertEquals([7, 0], main(21, 3));
    assertTraps(kTrapDivByZero, () => main(1, 0));
    // Test stack trace for trap.
    try {
      main(1, 0);
      assertUnreachable();
    } catch(e) {
      assertMatches(/RuntimeError: divide by zero/, e.stack);
      let expected_entries = [
        // [name, index, offset]
        ['callee', '' + callee.index, '0x8c'],
        ['main', '' + caller.index, '0xa4'],
      ];
      CheckCallStack(e, expected_entries);
    }

    try {
      main(0, 1);
      assertUnreachable();
    } catch(e) {
      assertMatches(/RuntimeError: divide by zero/, e.stack);
      let expected_entries = [
        // [name, index, offset]
        ['callee', '' + callee.index, '0x8c'],
        ['main', '' + caller.index, '0xaa'],
      ];
      CheckCallStack(e, expected_entries);
    }
  }

  function CheckCallStack(error, expected_entries) {
    print(error.stack);
    let regex = /at ([^ ]+) \(wasm[^\[]+\[([0-9]+)\]:(0x[0-9a-f]+)\)/g;
    let entries = [...error.stack.matchAll(regex)];
    for (let i = 0; i < expected_entries.length; ++i) {
      let actual = entries[i];
      print(`match = ${actual[0]}`);
      let expected = expected_entries[i];
      assertEquals(expected[0], actual[1]);
      assertEquals(expected[1], actual[2]);
      assertEquals(expected[2], actual[3]);
    }
    assertEquals(expected_entries.length, entries.length);
  }
})();

(function InliningTrapFromCalleeWithSingleTailCall() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // Add some types to have an index offset.
  for (let i = 0; i < 10; ++i) {
    builder.addFunction(null, makeSig([], [])).addBody([]);
  }

  let callee = builder.addFunction('callee', kSig_ii_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32DivU,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32Mul,
    ]);

  let caller = builder.addFunction('main', kSig_ii_ii)
    .addBody([
      // Some nops, so that the call doesn't have the same offset as the div
      // in the callee.
      kExprNop, kExprNop,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprReturnCall, callee.index,
    ])
    .exportFunc();

  let wire_bytes = builder.toBuffer();
  let module = new WebAssembly.Module(wire_bytes);
  let instance = new WebAssembly.Instance(module, {});
  TestStackTrace(instance.exports.main);
  // Serialize and deserialize the module to verify that the inlining positions
  // are properly "transformed" here.
  print("Repeat test with serialized module.")
  module = %DeserializeWasmModule(%SerializeWasmModule(module), wire_bytes);
  instance = new WebAssembly.Instance(module, {});
  TestStackTrace(instance.exports.main);

  function TestStackTrace(main) {
    assertEquals([7, 63], main(21, 3));
    try {
      main(1, 0);
      assertUnreachable();
    } catch(e) {
      assertMatches(/RuntimeError: divide by zero/, e.stack);
      let expected_entries = [
        // [name, index, offset]
        ['callee', '' + callee.index, '0x77'],
      ];
      CheckCallStack(e, expected_entries);
    }
  }

  function CheckCallStack(error, expected_entries) {
    print(error.stack);
    let regex = /at ([^ ]+) \(wasm[^\[]+\[([0-9]+)\]:(0x[0-9a-f]+)\)/g;
    let entries = [...error.stack.matchAll(regex)];
    for (let i = 0; i < expected_entries.length; ++i) {
      let actual = entries[i];
      print(`match = ${actual[0]}`);
      let expected = expected_entries[i];
      assertEquals(expected[0], actual[1]);
      assertEquals(expected[1], actual[2]);
      assertEquals(expected[2], actual[3]);
    }
    assertEquals(expected_entries.length, entries.length);
  }
})();

(function InliningTrapFromCalleeWithMultipleCalls() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // Add some types to have an index offset.
  for (let i = 0; i < 10; ++i) {
    builder.addFunction(null, makeSig([], [])).addBody([]);
  }

  let callee0 = builder.addFunction('callee0', kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32DivU
    ]);

  let callee1 = builder.addFunction('callee1', kSig_i_ii)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprCallFunction,
              callee0.index]);

  let callee2 = builder.addFunction('callee2', kSig_i_ii)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprReturnCall,
              callee1.index])

  let callee3 = builder.addFunction('callee3', kSig_i_ii)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprReturnCall,
              callee2.index])

  let callee4 = builder.addFunction('callee4', kSig_i_ii)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprCallFunction,
              callee3.index]);

  let main = builder.addFunction('main', kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprReturnCall, callee4.index,
    ])
    .exportFunc();

  let wire_bytes = builder.toBuffer();
  let module = new WebAssembly.Module(wire_bytes);
  let instance = new WebAssembly.Instance(module, {});
  TestStackTrace(instance.exports.main);
  // Serialize and deserialize the module to verify that the inlining positions
  // are properly "transformed" here.
  print("Repeat test with serialized module.")
  module = %DeserializeWasmModule(%SerializeWasmModule(module), wire_bytes);
  instance = new WebAssembly.Instance(module, {});
  TestStackTrace(instance.exports.main);

  function TestStackTrace(main) {
    assertEquals(7, main(21, 3));
    try {
      main(1, 0);
      assertUnreachable();
    } catch(e) {
      assertMatches(/RuntimeError: divide by zero/, e.stack);
      let expected_entries = [
        // [name, index, offset]
        ['callee0', '' + callee0.index, '0x91'],
        ['callee1', '' + callee1.index, '0x99'],
        ['callee4', '' + callee4.index, '0xb4'],
      ];
      CheckCallStack(e, expected_entries);
    }
  }

  function CheckCallStack(error, expected_entries) {
    print(error.stack);
    let regex = /at ([^ ]+) \(wasm[^\[]+\[([0-9]+)\]:(0x[0-9a-f]+)\)/g;
    let entries = [...error.stack.matchAll(regex)];
    for (let i = 0; i < expected_entries.length; ++i) {
      let actual = entries[i];
      print(`match = ${actual[0]}`);
      let expected = expected_entries[i];
      assertEquals(expected[0], actual[1]);
      assertEquals(expected[1], actual[2]);
      assertEquals(expected[2], actual[3]);
    }
    assertEquals(expected_entries.length, entries.length);
  }
})();
