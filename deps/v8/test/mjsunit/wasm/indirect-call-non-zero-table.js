// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function IndirectCallToNonZeroTable() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  const placeholder = builder.addTable(kWasmAnyFunc, 3).index;
  const table1 = builder.addTable(kWasmAnyFunc, 3).index;
  const table2 = builder.addTable(kWasmAnyFunc, 5).index;
  const sig_index = builder.addType(kSig_i_v);
  const other_sig = builder.addType(kSig_i_i);

  const v1 = 16;
  const v2 = 26;
  const v3 = 36;
  const v4 = 46;
  const v5 = 56;

  const f_unreachable = builder.addFunction('unreachable', sig_index)
    .addBody([kExprUnreachable]).index;
  const f1 = builder.addFunction('f1', sig_index)
    .addBody([kExprI32Const, v1])
    .index;
  const f2 = builder.addFunction('f2', sig_index)
    .addBody([kExprI32Const, v2])
    .index;
  const f3 = builder.addFunction('f3', sig_index)
    .addBody([kExprI32Const, v3])
    .index;
  const f4 = builder.addFunction('f4', sig_index)
    .addBody([kExprI32Const, v4])
    .index;
  const f5 = builder.addFunction('f5', sig_index)
    .addBody([kExprI32Const, v5])
    .index;

  builder.addFunction('call1', kSig_i_i)
    .addBody([kExprLocalGet, 0,   // function index
      kExprCallIndirect, sig_index, table1])
    .exportAs('call1');
  builder.addFunction('return_call1', kSig_i_i)
    .addBody([kExprLocalGet, 0,   // function index
      kExprReturnCallIndirect, sig_index, table1])
    .exportAs('return_call1');
  builder.addFunction('call2', kSig_i_i)
    .addBody([kExprLocalGet, 0,   // function index
      kExprCallIndirect, sig_index, table2])
    .exportAs('call2');
  builder.addFunction('return_call2', kSig_i_i)
    .addBody([kExprLocalGet, 0,   // function index
      kExprReturnCallIndirect, sig_index, table2])
    .exportAs('return_call2');

  builder.addFunction('call_invalid_sig', kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 0,   // function index + param
      kExprCallIndirect, other_sig, table2])
    .exportAs('call_invalid_sig');
  builder.addFunction('return_call_invalid_sig', kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 0,   // function index + param
      kExprReturnCallIndirect, other_sig, table2])
    .exportAs('return_call_invalid_sig');

  // We want to crash if we call through the table with index 0.
  builder.addActiveElementSegment(placeholder, wasmI32Const(0),
    [f_unreachable, f_unreachable, f_unreachable]);
  builder.addActiveElementSegment(table1, wasmI32Const(0),
                                  [f1, f2, f3]);
  // Keep one slot in table2 uninitialized. We should trap if we call it.
  builder.addActiveElementSegment(table2, wasmI32Const(1),
    [f_unreachable, f_unreachable, f4, f5]);

  const instance = builder.instantiate();

  assertEquals(v1, instance.exports.call1(0));
  assertEquals(v2, instance.exports.call1(1));
  assertEquals(v3, instance.exports.call1(2));
  assertTraps(kTrapTableOutOfBounds, () => instance.exports.call1(3));
  assertEquals(v1, instance.exports.return_call1(0));
  assertEquals(v2, instance.exports.return_call1(1));
  assertEquals(v3, instance.exports.return_call1(2));
  assertTraps(kTrapTableOutOfBounds, () => instance.exports.return_call1(3));

  // Try to call through the uninitialized table entry.
  assertTraps(kTrapFuncSigMismatch, () => instance.exports.call2(0));
  assertEquals(v4, instance.exports.call2(3));
  assertEquals(v5, instance.exports.call2(4));
  assertTraps(kTrapFuncSigMismatch,
    () => instance.exports.call_invalid_sig(4));
  assertTraps(kTrapFuncSigMismatch, () => instance.exports.return_call2(0));
  assertEquals(v4, instance.exports.return_call2(3));
  assertEquals(v5, instance.exports.return_call2(4));
  assertTraps(kTrapFuncSigMismatch,
    () => instance.exports.return_call_invalid_sig(4));
})();

(function IndirectCallToImportedNonZeroTable() {
  print(arguments.callee.name);

  const table_size = 10;
  const placeholder = new WebAssembly.Table(
    { initial: table_size, maximum: table_size, element: "anyfunc" });
  const table = new WebAssembly.Table(
    { initial: table_size, maximum: table_size, element: "anyfunc" });

  const builder = new WasmModuleBuilder();
  builder.addImportedTable("m", "placeholder", table_size, table_size);
  const t1 = builder.addImportedTable("m", "table", table_size, table_size);

  // We initialize the module twice and put the function f1 in the table at
  // the index defined by {g}. Thereby we can initialize the table at different
  // slots for different instances. The function f1 also returns {g} so that we
  // can see that actually different functions get called.
  const g = builder.addImportedGlobal("m", "base", kWasmI32);

  const sig_index = builder.addType(kSig_i_v);
  const f1 = builder.addFunction("foo", sig_index)
    .addBody([kExprGlobalGet, g, kExprI32Const, 12, kExprI32Add]);

  builder.addFunction('call', kSig_i_i)
    .addBody([kExprLocalGet, 0,   // function index
      kExprCallIndirect, sig_index, t1])
    .exportAs('call');

  builder.addActiveElementSegment(t1, [kExprGlobalGet, g], [f1.index]);
  const base1 = 3;
  const base2 = 5;

  const instance1 = builder.instantiate({
    m: {
      placeholder: placeholder,
      table: table,
      base: base1
    }
  });

  const instance2 = builder.instantiate({
    m: {
      placeholder: placeholder,
      table: table,
      base: base2
    }
  });

  assertEquals(base1 + 12, instance1.exports.call(base1));
  assertEquals(base2 + 12, instance1.exports.call(base2));
  assertEquals(base1 + 12, instance2.exports.call(base1));
  assertEquals(base2 + 12, instance2.exports.call(base2));
})();

function js_div(a, b) { return (a / b) | 0; }

(function CallImportedFunction() {
  let kTableSize = 10;
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();

  let div = builder.addImport("q", "js_div", kSig_i_ii);
  builder.addImportedTable("q", "placeholder", kTableSize, kTableSize);
  let table_index = builder.addImportedTable("q", "table", kTableSize,
                                             kTableSize);
  let g = builder.addImportedGlobal("q", "base", kWasmI32);

  let sig_index = builder.addType(kSig_i_ii);
  builder.addFunction("placeholder", sig_index)
    .addBody([kExprLocalGet, 0]);

  builder.addActiveElementSegment(table_index, [kExprGlobalGet, g], [div]);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprI32Const, 55,  // --
      kExprLocalGet, 0,   // --
      kExprLocalGet, 1,   // --
      kExprCallIndirect, 0, table_index])  // --
    .exportAs("main");

  let m = new WebAssembly.Module(builder.toBuffer());

  let table = new WebAssembly.Table({
    element: "anyfunc",
    initial: kTableSize,
    maximum: kTableSize
  });
  let placeholder = new WebAssembly.Table({
    element: "anyfunc",
    initial: kTableSize,
    maximum: kTableSize
  });

  let instance = new WebAssembly.Instance(m, {
    q: {
      base: 0, table: table, placeholder: placeholder,
      js_div: js_div
    }
  });

  assertEquals(13, instance.exports.main(4, 0));
})();
