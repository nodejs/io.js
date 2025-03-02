// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestGetAndSet() {
function addTableWithAccessors(builder, type, size, name) {
  const table = builder.addTable(type, size);
  const set_sig = makeSig([kWasmI32, type], []);
  builder.addFunction('set_' + name, set_sig)
      .addBody([kExprLocalGet, 0,
          kExprLocalGet, 1,
          kExprTableSet, table.index])
      .exportFunc();

  const get_sig = makeSig([kWasmI32], [type]);
  builder.addFunction('get_' + name, get_sig)
      .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
      .exportFunc();
}

const builder = new WasmModuleBuilder();

addTableWithAccessors(builder, kWasmAnyFunc, 10, 'table_func1');
addTableWithAccessors(builder, kWasmExternRef, 20, 'table_ref1');
addTableWithAccessors(builder, kWasmExternRef, 9, 'table_ref2');
addTableWithAccessors(builder, kWasmAnyFunc, 12, 'table_func2');

let exports = builder.instantiate().exports;
const dummy_ref = {foo : 1, bar : 3};
const dummy_func = exports.set_table_func1;

(function testTableGetInitialValue() {
  print(arguments.callee.name);
  // Tables are initialized with `null`.
  assertSame(null, exports.get_table_func1(1));
  assertSame(null, exports.get_table_func2(2));
  assertSame(null, exports.get_table_ref1(3));
  assertSame(null, exports.get_table_ref2(4));
})();

(function testTableGetOOB() {
  print(arguments.callee.name);
  assertSame(null, exports.get_table_func2(11));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_func1(11));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_func2(21));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_func1(-1));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_func2(-2));
  assertSame(null, exports.get_table_ref1(14));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_ref2(14));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_ref1(44));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_ref2(-1));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_ref1(-4));
})();

(function testTableSetOOB() {
  print(arguments.callee.name);
  exports.set_table_func2(11, dummy_func);
  assertTraps(kTrapTableOutOfBounds, () => exports.set_table_func1(11, dummy_func));
  assertTraps(kTrapTableOutOfBounds, () => exports.set_table_func2(21, dummy_func));
  exports.set_table_ref1(14, dummy_ref);
  assertTraps(kTrapTableOutOfBounds, () => exports.set_table_ref2(14, dummy_ref));
  assertTraps(kTrapTableOutOfBounds, () => exports.set_table_ref1(44, dummy_ref));
})();

(function testTableSet() {
  print(arguments.callee.name);
  assertSame(null, exports.get_table_func1(3));
  exports.set_table_func1(3, dummy_func);
  assertSame(dummy_func, exports.get_table_func1(3));
  assertSame(null, exports.get_table_func2(3));

  assertSame(null, exports.get_table_func2(7));
  exports.set_table_func2(7, dummy_func);
  assertSame(dummy_func, exports.get_table_func2(7));
  assertSame(null, exports.get_table_func1(7));

  assertSame(null, exports.get_table_ref1(3));
  exports.set_table_ref1(3, dummy_ref);
  assertSame(dummy_ref, exports.get_table_ref1(3));
  assertSame(null, exports.get_table_ref2(3));

  assertSame(null, exports.get_table_ref2(7));
  exports.set_table_ref2(7, dummy_ref);
  assertSame(dummy_ref, exports.get_table_ref2(7));
  assertSame(null, exports.get_table_ref1(7));
})();

(function testSetFunctionTableInvalidType() {
  print(arguments.callee.name);
  assertThrows(() => exports.set_table_func2(7, dummy_ref), TypeError);
})();
})();

(function testGetFunctionFromInitializedTable() {
  print(arguments.callee.name);
  const value1 = 11;
  const value2 = 22;
  const value3 = 13;

  const builder = new WasmModuleBuilder();
  const t1 = builder.addTable(kWasmAnyFunc, 10).index;
  const t2 = builder.addTable(kWasmAnyFunc, 12).index;

  const f1 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, value1]);
  const f2 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, value2]);
  const f3 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, value3]);
  builder.addFunction('get_t1', kSig_a_i)
      .addBody([kExprLocalGet, 0, kExprTableGet, t1])
      .exportFunc();
  builder.addFunction('get_t2', kSig_a_i)
      .addBody([kExprLocalGet, 0, kExprTableGet, t2])
      .exportFunc();

  const offset1 = 3;
  const offset2 = 9;
  builder.addActiveElementSegment(t1, wasmI32Const(offset1),
                                  [f1.index, f2.index]);
  builder.addActiveElementSegment(t2, wasmI32Const(offset2),
                                  [f3.index, f1.index]);

  const instance = builder.instantiate();

  assertEquals(value1, instance.exports.get_t1(offset1)());
  assertEquals(value2, instance.exports.get_t1(offset1 + 1)());
  assertEquals(value3, instance.exports.get_t2(offset2)());
  assertEquals(value1, instance.exports.get_t2(offset2 + 1)());
})();

(function testRefFuncInTableIsCallable() {
  print(arguments.callee.name);
  const expected = 54;
  const index = 3;
  const builder = new WasmModuleBuilder();
  const table_index = builder.addTable(kWasmAnyFunc, 15, 15).index;
  const sig_index = builder.addType(kSig_i_v);
  const function_index = builder.addFunction('hidden', sig_index)
                             .addBody([kExprI32Const, expected])
                             .index;
  builder.addDeclarativeElementSegment([function_index]);

  builder.addFunction('main', kSig_i_v)
      .addBody([
        kExprI32Const, index,                      // entry index
        kExprRefFunc, function_index,              // function reference
        kExprTableSet, table_index,                // --
        kExprI32Const, index,                      // entry index
        kExprCallIndirect, sig_index, table_index  // --
      ])
      .exportFunc();

  const instance = builder.instantiate();
  assertEquals(expected, instance.exports.main());
})();
