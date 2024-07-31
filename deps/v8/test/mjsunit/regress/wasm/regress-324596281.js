// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --wasm-wrapper-tiering-budget=1

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TierUpImportedFunction() {
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let sig =
    builder.addType(makeSig([wasmRefType(struct)], [wasmRefType(struct)]));
  let imp = builder.addImport('m', 'f', sig);
  builder.addFunction('main', kSig_i_i).exportFunc().addBody([
    kGCPrefix, kExprStructNewDefault, struct,
    kExprLoop, sig,
    kExprCallFunction, imp,
    kExprLocalGet, 0,
    kExprI32Const, 1,
    kExprI32Sub,
    kExprLocalTee, 0,
    kExprI32Eqz,
    kExprI32Eqz,
    kExprBrIf, 0,
    kExprEnd,
    kGCPrefix, kExprStructGet, struct, 0,
  ]);

  let instance = builder.instantiate({ m: { f: (x) => x } });
  // The argument is the iteration count: enough to tier up the wasm-to-js
  // wrapper.
  instance.exports.main(2);
})();

(function TierUpIndirectlyCalledFunction() {
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let sig =
    builder.addType(makeSig([wasmRefType(struct)], [wasmRefType(struct)]));
  let imp = builder.addImport('m', 'f', sig);
  builder.addTable(kWasmAnyFunc, 10, 10);
  builder.addActiveElementSegment(0, wasmI32Const(0), [imp]);
  builder.addFunction('main', kSig_i_i).exportFunc().addBody([
    kGCPrefix, kExprStructNewDefault, struct,
    kExprLoop, sig,
    kExprI32Const, 0,
    kExprCallIndirect, sig, kTableZero,
    kExprLocalGet, 0,
    kExprI32Const, 1,
    kExprI32Sub,
    kExprLocalTee, 0,
    kExprI32Eqz,
    kExprI32Eqz,
    kExprBrIf, 0,
    kExprEnd,
    kGCPrefix, kExprStructGet, struct, 0,
  ]);

  let instance = builder.instantiate({ m: { f: (x) => x } });
  // The argument is the iteration count: enough to tier up the wasm-to-js
  // wrapper.
  instance.exports.main(2);
})();
