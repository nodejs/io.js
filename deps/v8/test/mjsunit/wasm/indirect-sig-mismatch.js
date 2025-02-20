// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const kTableSize = 3;

var m_i_v;
var m_i_i;
var m_i_ii;

(function BuildExportedMethods() {
  print("BuildingExportedMethods...");
  let builder = new WasmModuleBuilder();

  let sig_v_v = builder.addType(kSig_v_v);
  let sig_i_ii = builder.addType(kSig_i_ii);
  let sig_i_i = builder.addType(kSig_i_i);
  let sig_i_v = builder.addType(kSig_i_v);

  print("exported[i_v]  = " + sig_i_v);
  print("exported[i_i]  = " + sig_i_i);
  print("exported[i_ii] = " + sig_i_ii);

  builder.addFunction("", sig_i_v)
    .addBody([
      kExprI32Const, 41])  // --
    .exportAs("m_i_v");

  builder.addFunction("m_i_i", sig_i_i)
    .addBody([
      kExprI32Const, 42])  // --
    .exportAs("m_i_i");

  builder.addFunction("m_i_ii", sig_i_ii)
    .addBody([
      kExprI32Const, 43])  // --
    .exportAs("m_i_ii");

  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  m_i_v = instance.exports.m_i_v;
  m_i_i = instance.exports.m_i_i;
  m_i_ii = instance.exports.m_i_ii;

  m_i_v.sig = kSig_i_v;
  m_i_i.sig = kSig_i_i;
  m_i_ii.sig = kSig_i_ii;
})();

function caller_module() {
  let builder = new WasmModuleBuilder();

  let sig_i_v = builder.addType(kSig_i_v);
  let sig_i_ii = builder.addType(kSig_i_ii);
  let sig_i_i = builder.addType(kSig_i_i);

  print("imported[i_v]  = " + sig_i_v);
  print("imported[i_i]  = " + sig_i_i);
  print("imported[i_ii] = " + sig_i_ii);


  builder.addFunction("call1", sig_i_i)
    .addBody([
      kExprLocalGet, 0, // --
      kExprCallIndirect, sig_i_v, kTableZero])  // --
    .exportAs("call1");

  builder.addFunction("call2", sig_i_i)
    .addBody([
      kExprI32Const, 11, // --
      kExprLocalGet, 0,
      kExprCallIndirect, sig_i_i, kTableZero])  // --
    .exportAs("call2");

  builder.addFunction("call3", sig_i_i)
    .addBody([
      kExprI32Const, 21,
      kExprI32Const, 22,
      kExprLocalGet, 0,
      kExprCallIndirect, sig_i_ii, kTableZero])  // --
    .exportAs("call3");

  builder.addImportedTable("imp", "table", kTableSize, kTableSize);

  return builder.toModule();
}

function call(func, ...args) {
  try {
    return "" + func.apply(undefined, args);
  } catch (e) {
    return "!" + e;
  }
}

function DoCalls(table, calls) {
  for (func of calls) {
    print("func = " + func);
    for (var i = 0; i < 4; i++) {
      print("  i = " + i);
      var expectThrow = true;
      var exp = null;
      if (i < table.length) {
        exported = table.get(i);
        expectThrow = (exported.sig != func.sig);
        print("     exp=" + exported);
        print("     throw=" + expectThrow);
      } else {
        print("     exp=<oob>");
      }
      print("     result=" + call(func, i));
      if (expectThrow) {
        assertThrows(() => func(i), WebAssembly.RuntimeError);
      } else {
        assertEquals(exported(0), func(i));
      }
    }
  }
}

(function TestExecute() {
  print("TestExecute");
  let module = caller_module();
  let table = new WebAssembly.Table({element: "anyfunc",
    initial: kTableSize, maximum: kTableSize});
  let instance = new WebAssembly.Instance(module, {imp: {table: table}});
  instance.exports.call1.sig = kSig_i_v;
  instance.exports.call2.sig = kSig_i_i;
  instance.exports.call3.sig = kSig_i_ii;

  let exports = [m_i_v, m_i_i, m_i_ii];
  let calls = [instance.exports.call1,
               instance.exports.call2,
               instance.exports.call3];

  for (f0 of exports) {
    for (f1 of exports) {
      for (f2 of exports) {
        table.set(0, f0);
        table.set(1, f1);
        table.set(2, f2);

        DoCalls(table, calls);
      }
    }
  }
})();
