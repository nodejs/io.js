// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
d8.file.execute("test/mjsunit/wasm/exceptions-utils.js");

// Check that exceptions thrown by a return_call cannot be caught inside the
// frame that does the call.
(function TryReturnCallCatch() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  let throw_ = builder.addFunction("throw", kSig_v_v)
      .addBody([kExprThrow, except]);
  builder.addFunction("try_return_call", kSig_v_v)
      .addBody([
        kExprTry, kWasmVoid,
          kExprReturnCall, throw_.index,
        kExprCatch, except,
        kExprEnd
      ]).exportFunc();
  let instance = builder.instantiate();
  assertWasmThrows(instance, except, [], () => instance.exports.try_return_call());
})();

// Check that exceptions thrown by a return_call_indirect cannot be caught
// inside the frame that does the call.
(function TryReturnCallIndirectCatch() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  let sig = builder.addType(kSig_v_v);
  let throw_ = builder.addFunction("throw", kSig_v_v)
      .addBody([kExprThrow, except]);
  let table = builder.addTable(kWasmAnyFunc, 1, 1);
  builder.addActiveElementSegment(table.index, wasmI32Const(0), [0]);
  builder.addFunction("try_return_call_indirect", kSig_v_v)
      .addBody([
        kExprTry, kWasmVoid,
          kExprI32Const, throw_.index,
          kExprReturnCallIndirect, sig, 0,
        kExprCatch, except,
        kExprEnd
      ]).exportFunc();
  let instance = builder.instantiate();
  assertWasmThrows(instance, except, [], () => instance.exports.try_return_call_indirect());
})();

// Check that exceptions thrown by a return_call cannot be delegated inside the
// frame that does the call.
(function TryReturnCallDelegate() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  let throw_ = builder.addFunction("throw", kSig_v_v)
      .addBody([kExprThrow, except]);
  builder.addFunction("try_return_call_delegate", kSig_v_v)
      .addBody([
        kExprTry, kWasmVoid,
          kExprTry, kWasmVoid,
            kExprReturnCall, throw_.index,
          kExprDelegate, 0,
        kExprCatch, except,
        kExprEnd
      ]).exportFunc();
  let instance = builder.instantiate();
  assertWasmThrows(instance, except, [], () => instance.exports.try_return_call_delegate());
})();

// Check that exceptions thrown by a return_call to a JS import cannot be caught
// inside the frame that does the call.
(function TryReturnCallImportJSCatch() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let throw_import = builder.addImport("m", "throw_", kSig_v_v);
  let except = builder.addTag(kSig_v_v);
  builder.addExportOfKind("ex", kExternalTag, except);
  builder.addFunction("return_call", kSig_v_v)
      .addBody([
          kExprTry, kWasmVoid,
            kExprReturnCall, throw_import,
          kExprCatchAll,
          kExprEnd
      ]).exportFunc();
  let throw_ = () => { throw new WebAssembly.Exception(instance.exports.ex, []); };
  let instance = builder.instantiate({m: {throw_}});
  assertWasmThrows(instance, except, [], () => instance.exports.return_call());
})();

// Check that the exception can be caught in the caller's caller.
(function TryReturnCallCatchInCaller() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  let throw_ = builder.addFunction("throw", kSig_v_v)
      .addBody([kExprThrow, except]);
  builder.addFunction("caller", kSig_v_v)
      .addBody([
          kExprTry, kWasmVoid,
            kExprCallFunction, 2,
          kExprCatch, except,
          kExprEnd
      ]).exportFunc();
  builder.addFunction("return_call", kSig_v_v)
      .addBody([
          kExprReturnCall, throw_.index,
      ]);
  let instance = builder.instantiate();
  assertDoesNotThrow(() => instance.exports.caller());
})();
