// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function Test1() {
  print("Test1...");
  var module = (function () {
    var builder = new WasmModuleBuilder();

    var sig_index = builder.addType(kSig_i_ii);
    builder.addImport("q", "add", sig_index);
    var f = builder.addFunction("add", sig_index)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1, kExprCallFunction, 0
      ]);
    print("internal add index = " + f.index);
    builder.addFunction("sub", sig_index)
      .addBody([
        kExprLocalGet, 0,             // --
        kExprLocalGet, 1,             // --
        kExprI32Sub,                  // --
      ]);
    builder.addFunction("main", kSig_i_iii)
      .addBody([
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kExprLocalGet, 0,
        kExprCallIndirect, sig_index, kTableZero
      ])
      .exportFunc()
    builder.appendToTable([1, 2, 3]);

    return builder.instantiate({q: {add: function(a, b) {
      print("  --extadd");
      return a + b | 0;
    }}});
  })();

  // Check the module exists.
  assertFalse(module === undefined);
  assertFalse(module === null);
  assertFalse(module === 0);

  assertEquals("object", typeof module.exports);
  assertEquals("function", typeof module.exports.main);

  print(" --x1--");
  assertEquals(19, module.exports.main(0, 12, 7));
  print(" --y1--");
  assertEquals(5, module.exports.main(1, 12, 7));
  print(" --z1--");
  assertTraps(kTrapFuncSigMismatch, () => module.exports.main(2, 12, 33));
  print(" --w1--");
  assertTraps(kTrapTableOutOfBounds, () => module.exports.main(3, 12, 33));
})();

(function Test2() {
  print("Test2...");
  var module = (function () {
    var builder = new WasmModuleBuilder();

    var sig_i_ii = builder.addType(kSig_i_ii);
    var sig_i_i = builder.addType(kSig_i_i);
    var mul = builder.addImport("q", "mul", sig_i_ii);
    var add = builder.addFunction("add", sig_i_ii)
        .addBody([
          kExprLocalGet, 0,  // --
          kExprLocalGet, 1,  // --
          kExprI32Add        // --
        ]);
    var popcnt = builder.addFunction("popcnt", sig_i_i)
        .addBody([
          kExprLocalGet, 0,  // --
          kExprI32Popcnt     // --
        ]);
    var main = builder.addFunction("main", kSig_i_iii)
        .addBody([
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kExprLocalGet, 0,
          kExprCallIndirect, sig_i_ii, kTableZero
        ])
        .exportFunc();
    builder.appendToTable([mul, add.index, popcnt.index, main.index]);

    return builder.instantiate({q: {mul: function(a, b) { return a * b | 0; }}});
  })();

  print(" --x2--");
  assertEquals(-6, module.exports.main(0, -2, 3));
  print(" --y2--");
  assertEquals(99, module.exports.main(1, 22, 77));
  print(" --z2--");
  assertTraps(kTrapFuncSigMismatch, () => module.exports.main(2, 12, 33));
  print(" --q2--");
  assertTraps(kTrapFuncSigMismatch, () => module.exports.main(3, 12, 33));
  print(" --t2--");
  assertTraps(kTrapTableOutOfBounds, () => module.exports.main(4, 12, 33));
})();


function AddFunctions(builder) {
  var mul = builder.addFunction("mul", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,  // --
      kExprLocalGet, 1,  // --
      kExprI32Mul        // --
    ]);
  var add = builder.addFunction("add", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,  // --
      kExprLocalGet, 1,  // --
      kExprI32Add        // --
    ]);
  var sub = builder.addFunction("sub", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,  // --
      kExprLocalGet, 1,  // --
      kExprI32Sub        // --
    ]);
  return {mul: mul, add: add, sub: sub};
}


(function Test3() {
  print("Test3...");
  var module = (function () {
    var builder = new WasmModuleBuilder();

    var f = AddFunctions(builder);
    builder.addFunction("main", kSig_i_ii)
      .addBody([
        kExprI32Const, 33,  // --
        kExprLocalGet, 0,   // --
        kExprLocalGet, 1,   // --
        kExprCallIndirect, 0, kTableZero])  // --
      .exportAs("main");

    builder.appendToTable([f.mul.index, f.add.index, f.sub.index]);

    return builder.instantiate();
  })();

  assertEquals(33, module.exports.main(1, 0));
  assertEquals(66, module.exports.main(2, 0));
  assertEquals(34, module.exports.main(1, 1));
  assertEquals(35, module.exports.main(2, 1));
  assertEquals(32, module.exports.main(1, 2));
  assertEquals(31, module.exports.main(2, 2));
  assertTraps(kTrapTableOutOfBounds, () => module.exports.main(12, 3));
})();

(function ConstBaseTest() {
  print("ConstBaseTest...");
  function instanceWithTable(base, length) {
    var builder = new WasmModuleBuilder();

    var f = AddFunctions(builder);
    builder.addFunction("main", kSig_i_ii)
      .addBody([
        kExprI32Const, 33,  // --
        kExprLocalGet, 0,   // --
        kExprLocalGet, 1,   // --
        kExprCallIndirect, 0, kTableZero])  // --
      .exportAs("main");

    builder.setTableBounds(length, length);
    builder.addActiveElementSegment(0, wasmI32Const(base),
                                    [f.add.index, f.sub.index, f.mul.index]);

    return builder.instantiate();
  }

  for (var i = 0; i < 5; i++) {
    print(" base = " + i);
    var module = instanceWithTable(i, 10);
    main = module.exports.main;
    for (var j = 0; j < i; j++) {
      assertTraps(kTrapFuncSigMismatch, "main(12, " + j + ")");
    }
    assertEquals(34, main(1, i + 0));
    assertEquals(35, main(2, i + 0));
    assertEquals(32, main(1, i + 1));
    assertEquals(31, main(2, i + 1));
    assertEquals(33, main(1, i + 2));
    assertEquals(66, main(2, i + 2));
    assertTraps(kTrapTableOutOfBounds, () => main(12, 10));
  }
})();

(function GlobalBaseTest() {
  print("GlobalBaseTest...");

  var builder = new WasmModuleBuilder();

  var f = AddFunctions(builder);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprI32Const, 33,  // --
      kExprLocalGet, 0,   // --
      kExprLocalGet, 1,   // --
      kExprCallIndirect, 0, kTableZero])  // --
    .exportAs("main");

  builder.setTableBounds(10, 10);
  var g = builder.addImportedGlobal("fff", "base", kWasmI32);
  builder.addActiveElementSegment(0, [kExprGlobalGet, g],
                                  [f.mul.index, f.add.index, f.sub.index]);

  var module = new WebAssembly.Module(builder.toBuffer());

  for (var i = 0; i < 5; i++) {
    print(" base = " + i);
    var instance = new WebAssembly.Instance(module, {fff: {base: i}});
    main = instance.exports.main;
    for (var j = 0; j < i; j++) {
      assertTraps(kTrapFuncSigMismatch, "main(12, " + j + ")");
    }
    assertEquals(33, main(1, i + 0));
    assertEquals(66, main(2, i + 0));
    assertEquals(34, main(1, i + 1));
    assertEquals(35, main(2, i + 1));
    assertEquals(32, main(1, i + 2));
    assertEquals(31, main(2, i + 2));
    assertTraps(kTrapTableOutOfBounds, () => main(12, 10));
  }
})();
