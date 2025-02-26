// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let kSig_v_w = makeSig([kWasmStringRef], []);
let kSig_iw_i = makeSig([kWasmI32], [kWasmI32, kWasmStringRef]);

(function TestStringViewIterStack() {
  let builder = new WasmModuleBuilder();
  let wrapper = builder.addStruct([makeField(kWasmStringViewIter, true)]);

  let global = builder.addGlobal(wasmRefNullType(wrapper), true, false);

  builder.addFunction("iterate", kSig_v_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      ...GCInstr(kExprStringAsIter),
      kGCPrefix, kExprStructNew, wrapper,
      kExprGlobalSet, global.index
    ]);

  // The following functions perform a stringview operation and have the
  // value 42 on the stack to ensure that the value stack is preserved on each
  // of these operations.

  builder.addFunction("advance", kSig_ii_i)
    .exportFunc()
    .addBody([
      kExprI32Const, 42,
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, wrapper, 0,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringViewIterAdvance)
    ]);

  builder.addFunction("rewind", kSig_ii_i)
    .exportFunc()
    .addBody([
      kExprI32Const, 42,
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, wrapper, 0,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringViewIterRewind)
    ]);

  builder.addFunction("slice", kSig_iw_i)
    .exportFunc()
    .addBody([
      kExprI32Const, 42,
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, wrapper, 0,
      kExprLocalGet, 0,
      ...GCInstr(kExprStringViewIterSlice)
    ]);

  let instance = builder.instantiate();

  let str = 'ascii string';
  instance.exports.iterate(str);
  for (let i = 0; i < str.length; i++) {
    assertEquals([42, 1], instance.exports.advance(1));
  }
  assertEquals([42, 0], instance.exports.advance(1));

  for (let i = 0; i < str.length; i++) {
    assertEquals([42, 1], instance.exports.rewind(1));
  }
  assertEquals([42, 0], instance.exports.rewind(1));

  for (let i = 0; i < str.length; i++) {
    assertEquals([42, str.substring(0, i)], instance.exports.slice(i));
  }
})();
