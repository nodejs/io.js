// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-loop-rotation --noliftoff --nowasm-tier-up

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestTrivialLoop1() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_v_i)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 0,
        kExprBrIf, 0,
      kExprEnd,
    ])
    .exportFunc();
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  instance.exports.main(1);
  instance.exports.main(10);
  instance.exports.main(100);
})();

(function TestTrivialLoop2() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_v_i)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 0,
        kExprBrIf, 1,
        kExprBr, 0,
      kExprEnd,
    ])
    .exportFunc();
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  instance.exports.main(1);
  instance.exports.main(10);
  instance.exports.main(100);
})();

(function TestNonRotatedLoopWithStore() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, undefined);
  builder.addFunction("main", kSig_v_i)
    .addBody([
      kExprLoop, kWasmVoid,
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 0,
        kExprBrIf, 1,
        kExprI32Const, 0,
        kExprI32Const, 0,
        kExprI32StoreMem, 0, 0,
        kExprBr, 0,
      kExprEnd,
    ])
    .exportFunc();
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  instance.exports.main(1);
  instance.exports.main(10);
  instance.exports.main(100);
})();
