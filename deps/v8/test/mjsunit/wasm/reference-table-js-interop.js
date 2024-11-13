// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let tableTypes = {
  "anyref": kWasmAnyRef,
  "eqref": kWasmEqRef,
  "structref": kWasmStructRef,
  "arrayref": kWasmArrayRef,
  "i31ref": kWasmI31Ref,
};

// Test table consistency check.
for (let [typeName, type] of Object.entries(tableTypes)) {
  print("TestTableTypeCheck_" + typeName);
  let builder = new WasmModuleBuilder();
  const size = 10;
  builder.addImportedTable("imports", "table", size, size, type);

  for (let typeName2 in tableTypes) {
    let table = new WebAssembly.Table({
      initial: size, maximum: size, element: typeName2
    });
    if (typeName == typeName2) {
      builder.instantiate({ imports: { table } });
    } else {
      let err = 'WebAssembly.Instance(): Import #0 "imports" "table": ' +
                'imported table does not match the expected type';
      assertThrows(() => builder.instantiate({ imports: { table } }),
                   WebAssembly.LinkError,
                   err);
    }
  }
}

// Test table usage from JS and Wasm.
for (let [typeName, type] of Object.entries(tableTypes)) {
  print("TestImportedTable_" + typeName);
  let builder = new WasmModuleBuilder();

  const size = 10;
  const maxSize = 20;
  let table = new WebAssembly.Table({
    initial: size, maximum: maxSize, element: typeName
  });

  let creatorSig = builder.addType(makeSig([], [type]));
  let creatorAnySig = builder.addType(makeSig([], [kWasmAnyRef]));
  let struct = builder.addStruct([makeField(kWasmI32, false)]);
  let array = builder.addArray(kWasmI32, true);

  builder.addImportedTable("imports", "table", size, maxSize, type);
  builder.addFunction("tableSet",
                      makeSig([kWasmI32, wasmRefType(creatorSig)], []))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallRef, creatorSig,
      kExprTableSet, 0,
    ])
    .exportFunc();
  builder.addFunction("tableGet", makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0, kExprTableGet, 0,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  let getValSig = makeSig([kWasmI32], [kWasmI32]);
  builder.addFunction("tableGetStructVal", getValSig)
    .addBody([
      kExprLocalGet, 0, kExprTableGet, 0,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();
  builder.addFunction("tableGetArrayVal", getValSig)
    .addBody([
      kExprLocalGet, 0, kExprTableGet, 0,
      kGCPrefix, kExprRefCast, array,
      kExprI32Const, 0,
      kGCPrefix, kExprArrayGet, array,
    ])
    .exportFunc();

  builder.addFunction("exported",
                      makeSig([wasmRefType(creatorSig)], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kExprCallRef, creatorSig,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();
  builder.addFunction("exportedAny",
                      makeSig([wasmRefType(creatorAnySig)], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kExprCallRef, creatorAnySig,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction("createNull", creatorSig)
    .addBody([kExprRefNull, kNullRefCode])
    .exportFunc();
  let i31Sig = typeName != "structref" && typeName != "arrayref"
               ? creatorSig : creatorAnySig;
  builder.addFunction("createI31", i31Sig)
    .addBody([kExprI32Const, 12, kGCPrefix, kExprRefI31])
    .exportFunc();
  let structSig = typeName != "arrayref" && typeName != "i31ref"
                  ? creatorSig : creatorAnySig;
  builder.addFunction("createStruct", structSig)
    .addBody([kExprI32Const, 12, kGCPrefix, kExprStructNew, struct])
    .exportFunc();
  let arraySig = typeName != "structref" && typeName != "i31ref"
                 ? creatorSig : creatorAnySig;
  builder.addFunction("createArray", arraySig)
    .addBody([
      kExprI32Const, 12,
      kGCPrefix, kExprArrayNewFixed, array, 1
    ])
    .exportFunc();

  if (typeName == "anyref") {
    builder.addFunction("tableSetFromExtern",
                      makeSig([kWasmI32, kWasmExternRef], []))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kGCPrefix, kExprAnyConvertExtern,
      kExprTableSet, 0,
    ])
    .exportFunc();
  }

  let instance = builder.instantiate({ imports: { table } });
  let wasm = instance.exports;

  // Set null.
  table.set(0, null);
  assertEquals(null, wasm.tableGet(0));
  assertEquals(null, table.get(0));
  wasm.tableSet(1, wasm.createNull);
  assertEquals(null, wasm.tableGet(1));
  assertEquals(null, table.get(1));
  // Set i31.
  if (typeName != "structref" && typeName != "arrayref") {
    table.set(2, wasm.exported(wasm.createI31));
    assertSame(table.get(2), wasm.tableGet(2));
    wasm.tableSet(3, wasm.createI31);
    assertSame(table.get(3), wasm.tableGet(3));
    assertSame(table.get(2), table.get(3)); // The same smi.
  }
  // Set struct.
  if (typeName != "arrayref" && typeName != "i31ref") {
    table.set(4, wasm.exported(wasm.createStruct));
    assertSame(table.get(4), wasm.tableGet(4));
    assertEquals(12, wasm.tableGetStructVal(4));
    wasm.tableSet(5, wasm.createStruct);
    assertSame(table.get(5), wasm.tableGet(5));
    assertEquals(12, wasm.tableGetStructVal(5));
    assertNotSame(table.get(4), table.get(5));
  }
  // Set array.
  if (typeName != "structref" && typeName != "i31ref") {
    table.set(6, wasm.exported(wasm.createArray));
    assertSame(table.get(6), wasm.tableGet(6));
    assertEquals(12, wasm.tableGetArrayVal(6));
    wasm.tableSet(7, wasm.createArray);
    assertSame(table.get(7), wasm.tableGet(7));
    assertEquals(12, wasm.tableGetArrayVal(7));
    assertNotSame(table.get(6), table.get(7));
  }

  // Set stringref.
  if (typeName == "anyref") {
    table.set(8, "TestString");
    assertEquals("TestString", wasm.tableGet(8));
    assertEquals("TestString", table.get(8));
    let largeString = "Another test string, this time larger to prevent"
                    + " any kind of short string optimization.";
    wasm.tableSetFromExtern(9, largeString);
    assertEquals(largeString, wasm.tableGet(9));
    assertEquals(largeString, table.get(9));
  }

  if (typeName != "arrayref" && typeName != "i31ref") {
    // Grow table with explicit value.
    table.grow(2, wasm.exported(wasm.createStruct));
    assertEquals(12, wasm.tableGetStructVal(size));
    assertEquals(12, wasm.tableGetStructVal(size + 1));
    assertTraps(kTrapTableOutOfBounds, () => wasm.tableGetStructVal(size + 2));
    // Grow by 1 without initial value.
    table.grow(1, null);
    if (typeName == "anyref") {
      // Undefined is fine for anyref.
      table.grow(1, undefined);
    } else {
      // But not for any other wasm internal type.
      assertThrows(() => table.grow(1, undefined), TypeError);
      // No-argument will use the default value.
      table.grow(1);
    }
  }
  if (typeName == "anyref") {
    table.grow(1, "Grow using a string");
    assertEquals("Grow using a string", wasm.tableGet(14));
    assertEquals("Grow using a string", table.get(14));
  }
  if (typeName == "i31ref" || typeName == "anyref") {
    table.set(0, 123);
    assertEquals(123, table.get(0));
    table.set(1, -123);
    assertEquals(-123, table.get(1));
    if (typeName == "i31ref") {
      assertThrows(() => table.set(0, 1 << 31), TypeError);
    } else {
      // anyref can reference boxed numbers as well.
      table.set(0, 1 << 31)
      assertEquals(1 << 31, table.get(0));
    }
  }

  // Set from JS with wrapped wasm value of incompatible type.
  let invalidValues = {
    "anyref": [],
    "eqref": [],
    "structref": ["I31", "Array"],
    "arrayref": ["I31", "Struct"],
    "i31ref": ["Struct", "Array"],
  };
  for (let invalidType of invalidValues[typeName]) {
    print(`Test invalid type ${invalidType} for ${typeName}`);
    let invalid_value = wasm.exportedAny(wasm[`create${invalidType}`]);
    assertThrows(() => table.grow(1, invalid_value), TypeError);
    assertThrows(() => table.set(1, invalid_value), TypeError);
  }
}
