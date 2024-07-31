// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
(function TestTables() {
  print(arguments.callee.name);
  var exporting_instance = (function() {
    var builder = new WasmModuleBuilder();
    var binary_type = builder.addType(kSig_i_ii);

    builder.addFunction('addition', kSig_i_ii)
        .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
        .exportFunc();

    builder.addFunction('succ', kSig_i_i)
        .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add])
        .exportFunc();

    builder.addTable(wasmRefNullType(binary_type), 1, 100).exportAs('table');

    return builder.instantiate({});
  })();

  // Wrong type for imported table.
  assertThrows(() => {
    var builder = new WasmModuleBuilder();
    var unary_type = builder.addType(kSig_i_i);
    builder.addImportedTable(
        'imports', 'table', 1, 100, wasmRefNullType(unary_type));
    builder.instantiate({imports: {table: exporting_instance.exports.table}})
  }, WebAssembly.LinkError, /imported table does not match the expected type/);

  // Type for imported table must match exactly.
  assertThrows(() => {
    var builder = new WasmModuleBuilder();
    builder.addImportedTable('imports', 'table', 1, 100, kWasmFuncRef);
    builder.instantiate({imports: {table: exporting_instance.exports.table}})
  }, WebAssembly.LinkError, /imported table does not match the expected type/);

  var instance = (function() {
    var builder = new WasmModuleBuilder();

    var unary_type = builder.addType(kSig_i_i);
    var binary_type = builder.addType(kSig_i_ii);

    builder.addImportedTable(
        'imports', 'table', 1, 100, wasmRefNullType(binary_type));

    var table =
        builder.addTable(wasmRefNullType(unary_type), 10).exportAs('table');
    builder.addTable(kWasmFuncRef, 1).exportAs('generic_table');

    builder
        .addFunction(
            'table_test', makeSig([wasmRefType(unary_type)], [kWasmI32]))
        // Set table[0] to input function, then retrieve it and call it.
        .addBody([
          kExprI32Const, 0, kExprLocalGet, 0, kExprTableSet, table.index,
          kExprI32Const, 42, kExprI32Const, 0, kExprTableGet, table.index,
          kExprCallRef, unary_type,
        ])
        .exportFunc();

    // Same, but with table[1] and call_indirect
    builder
        .addFunction(
            'table_indirect_test',
            makeSig([wasmRefType(unary_type)], [kWasmI32]))
        .addBody([
          kExprI32Const, 1, kExprLocalGet, 0, kExprTableSet, table.index,
          kExprI32Const, 42, kExprI32Const, 0, kExprCallIndirect, unary_type,
          table.index
        ])
        .exportFunc();

    // Instantiate with a table of the correct type.
    return builder.instantiate(
        {imports: {table: exporting_instance.exports.table}});
  })();

  // This module is valid.
  assertTrue(!!instance);

  // The correct function reference is preserved when setting it to and getting
  // it back from a table.
  assertEquals(
      43, instance.exports.table_test(exporting_instance.exports.succ));
  // Same for call indirect (the indirect call tables are also set correctly).
  assertEquals(
      43,
      instance.exports.table_indirect_test(exporting_instance.exports.succ));

  // Setting from JS API respects types.
  instance.exports.generic_table.set(0, exporting_instance.exports.succ);
  instance.exports.table.set(0, exporting_instance.exports.succ);
  assertThrows(
      () => instance.exports.table.set(0, exporting_instance.exports.addition),
      TypeError,
      /Argument 1 is invalid for table: assigned exported function has to be a subtype of the expected type/);
})();

(function TestNonNullableTables() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  var binary_type = builder.addType(kSig_i_ii);

  var addition = builder.addFunction('addition', binary_type).addBody([
    kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add
  ]);
  var subtraction =
      builder.addFunction('subtraction', binary_type)
          .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Sub])
          .exportFunc();

  var table = builder.addTable(wasmRefType(binary_type), 3, 3,
                               [kExprRefFunc, addition.index]);

  builder.addFunction('init', kSig_v_v)
      .addBody([
        kExprI32Const, 1, kExprRefFunc, subtraction.index, kExprTableSet,
        table.index
      ])
      .exportFunc();

  // (index, arg1, arg2) -> table[index](arg1, arg2)
  builder.addFunction('table_test', kSig_i_iii)
      .addBody([
        kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 0, kExprCallIndirect,
        binary_type, table.index
      ])
      .exportFunc();

  var instance = builder.instantiate({});

  assertTrue(!!instance);

  instance.exports.init();
  assertEquals(44, instance.exports.table_test(0, 33, 11));
  assertEquals(22, instance.exports.table_test(1, 33, 11));
})();

(function TestAnyRefTable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array_type = builder.addArray(kWasmI32);
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);

  let table = builder.addTable(kWasmAnyRef, 4, 4);
  builder.addActiveElementSegment(
    table, wasmI32Const(0),
    [[...wasmI32Const(111), ...wasmI32Const(222),
      kGCPrefix, kExprArrayNewFixed, array_type, 2],
     [...wasmI32Const(-31), kGCPrefix, kExprRefI31],
     [...wasmI32Const(10), kGCPrefix, kExprStructNew, struct_type],
     [kExprRefNull, kEqRefCode]],
    kWasmAnyRef);

  // return ...static_cast<array_type>(table[0])
  builder.addFunction("array_getter", kSig_ii_v)
    .addLocals(wasmRefNullType(array_type), 1)
    .addBody([
      kExprI32Const, 0, kExprTableGet, 0,
      kGCPrefix, kExprRefCast, array_type,
      kExprLocalSet, 0,
      kExprLocalGet, 0,
      ...wasmI32Const(0), kGCPrefix, kExprArrayGet, array_type,
      kExprLocalGet, 0,
      ...wasmI32Const(1), kGCPrefix, kExprArrayGet, array_type])
    .exportFunc();

  // return static_cast<i31>(table[1])
  builder.addFunction("i31_getter", kSig_i_v)
   .addBody([
     kExprI32Const, 1, kExprTableGet, 0,
     kGCPrefix, kExprRefCast, kI31RefCode,
     kGCPrefix, kExprI31GetS])
   .exportFunc();

  // return static_cast<struct_type>(table[2]).field_0
  builder.addFunction("struct_getter", kSig_i_v)
    .addBody([
      kExprI32Const, 2, kExprTableGet, 0,
      kGCPrefix, kExprRefCast, struct_type,
      kGCPrefix, kExprStructGet, struct_type, 0])
    .exportFunc();

  // return table[3] == null
  builder.addFunction("null_getter", kSig_i_v)
    .addBody([kExprI32Const, 3, kExprTableGet, 0, kExprRefIsNull])
    .exportFunc();

  let instance = builder.instantiate({});

  assertTrue(!!instance);

  assertEquals([111, 222], instance.exports.array_getter());
  assertEquals(-31, instance.exports.i31_getter());
  assertEquals(10, instance.exports.struct_getter());
  assertEquals(1, instance.exports.null_getter());
})();

(function TestAnyRefTableNotNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array_type = builder.addArray(kWasmI32);
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);

  let table = builder.addTable(wasmRefType(kWasmAnyRef), 3, 6,
      [...wasmI32Const(111), ...wasmI32Const(222),
       kGCPrefix, kExprArrayNewFixed, array_type, 2])
    .exportAs("table");
  builder.addActiveElementSegment(
    table, wasmI32Const(0),
    [[...wasmI32Const(111), ...wasmI32Const(222),
      kGCPrefix, kExprArrayNewFixed, array_type, 2],
     [...wasmI32Const(-31), kGCPrefix, kExprRefI31],
     [...wasmI32Const(10), kGCPrefix, kExprStructNew, struct_type]],
     wasmRefType(kWasmAnyRef));

  // return ...cast<array_type>(table.get(0))
  builder.addFunction("array_getter", kSig_ii_v)
    .addLocals(wasmRefNullType(array_type), 1)
    .addBody([
      kExprI32Const, 0, kExprTableGet, 0,
      kGCPrefix, kExprRefCast, array_type,
      kExprLocalSet, 0,
      kExprLocalGet, 0,
      ...wasmI32Const(0), kGCPrefix, kExprArrayGet, array_type,
      kExprLocalGet, 0,
      ...wasmI32Const(1), kGCPrefix, kExprArrayGet, array_type])
    .exportFunc();

  // return cast<i31>(table.get(1))
  builder.addFunction("i31_getter", kSig_i_v)
   .addBody([
     kExprI32Const, 1, kExprTableGet, 0,
     kGCPrefix, kExprRefCast, kI31RefCode,
     kGCPrefix, kExprI31GetS])
   .exportFunc();

  // return cast<struct_type>(table.get(param<0>))[0]
  builder.addFunction("struct_getter", kSig_i_i)
    .addBody([
      kExprLocalGet, 0, kExprTableGet, 0,
      kGCPrefix, kExprRefCast, struct_type,
      kGCPrefix, kExprStructGet, struct_type, 0])
    .exportFunc();

  builder.addFunction("grow_table", kSig_v_v)
    .addBody([
      ...wasmI32Const(20), kGCPrefix, kExprStructNew, struct_type,
      kExprI32Const, 1,
      kNumericPrefix, kExprTableGrow, 0,
      kExprDrop,
    ])
    .exportFunc();

  builder.addFunction("create_struct", makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct_type,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasmTable = instance.exports.table;

  assertEquals([111, 222], instance.exports.array_getter());
  assertEquals(-31, instance.exports.i31_getter());
  assertEquals(10, instance.exports.struct_getter(2));
  assertTraps(kTrapTableOutOfBounds, () => instance.exports.struct_getter(3));
  instance.exports.grow_table();
  assertEquals(20, instance.exports.struct_getter(3));
  assertThrows(() => wasmTable.grow(1), TypeError,
               /Argument 1 must be specified for non-nullable element type/);
  wasmTable.grow(1, instance.exports.create_struct(33));
  assertEquals(33, instance.exports.struct_getter(4));
  // undefined is ok for (ref any), but not null.
  wasmTable.set(4, undefined);
  assertThrows(() => wasmTable.set(4, null), TypeError,
               /Argument 1 is invalid/);
})();

(function TestStructRefTable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);

  let table = builder.addTable(wasmRefNullType(struct_type), 4, 4);
  builder.addActiveElementSegment(
    table, wasmI32Const(0),
    [[...wasmI32Const(10), kGCPrefix, kExprStructNew, struct_type],
     [...wasmI32Const(11), kGCPrefix, kExprStructNew, struct_type],
     [kExprRefNull, struct_type]],
     wasmRefNullType(struct_type));

  builder.addFunction("struct_getter", kSig_i_i)
    .addBody([
      kExprLocalGet, 0, kExprTableGet, 0,
      kGCPrefix, kExprStructGet, struct_type, 0])
    .exportFunc();

  builder.addFunction("null_getter", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprTableGet, 0, kExprRefIsNull])
    .exportFunc();

  let instance = builder.instantiate({});
  assertTrue(!!instance);

  assertEquals(10, instance.exports.struct_getter(0));
  assertEquals(11, instance.exports.struct_getter(1));
  assertEquals(1, instance.exports.null_getter(2));
})();

(function TestI31RefTable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let table = builder.addTable(kWasmI31Ref, 4, 4);
  builder.addActiveElementSegment(
    table, wasmI32Const(0),
    [[...wasmI32Const(10), kGCPrefix, kExprRefI31],
     [...wasmI32Const(-42), kGCPrefix, kExprRefI31],
     [kExprRefNull, kI31RefCode]],
     kWasmI31Ref);

  builder.addFunction("i31GetI32", kSig_i_i)
    .addBody([
      kExprLocalGet, 0, kExprTableGet, 0,
      kGCPrefix, kExprI31GetS])
    .exportFunc();

  builder.addFunction("i31GetNull", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprTableGet, 0, kExprRefIsNull])
    .exportFunc();

  let instance = builder.instantiate({});
  assertTrue(!!instance);

  assertEquals(0, instance.exports.i31GetNull(0));
  assertEquals(0, instance.exports.i31GetNull(1));
  assertEquals(1, instance.exports.i31GetNull(2));
  assertEquals(1, instance.exports.i31GetNull(3));
  assertEquals(10, instance.exports.i31GetI32(0));
  assertEquals(-42, instance.exports.i31GetI32(1));
  assertTraps(kTrapNullDereference, () => instance.exports.i31GetI32(2));
  assertTraps(kTrapNullDereference, () => instance.exports.i31GetI32(3));
})();

(function TestArrayRefTable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array_type = builder.addArray(kWasmI32);

  let table = builder.addTable(wasmRefNullType(array_type), 4, 4);
  builder.addActiveElementSegment(
    table, wasmI32Const(0),
    [[...wasmI32Const(10), ...wasmI32Const(11), ...wasmI32Const(12),
      kGCPrefix, kExprArrayNewFixed, array_type, 3],
     [kGCPrefix, kExprArrayNewFixed, array_type, 0],
     [kExprRefNull, array_type]],
     wasmRefNullType(array_type));

  builder.addFunction("array_getter", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0, kExprTableGet, 0,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGet, array_type])
    .exportFunc();

  builder.addFunction("null_getter", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprTableGet, 0, kExprRefIsNull])
    .exportFunc();

  let instance = builder.instantiate({});
  assertTrue(!!instance);

  assertEquals(10, instance.exports.array_getter(0, 0));
  assertEquals(11, instance.exports.array_getter(0, 1));
  assertEquals(12, instance.exports.array_getter(0, 2));
  assertEquals(0, instance.exports.null_getter(1));
  assertTraps(kTrapArrayOutOfBounds,
              () => instance.exports.array_getter(1, 0));
  assertEquals(1, instance.exports.null_getter(2));
})();

(function TestRefTableInvalidSegmentType() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct_type_a = builder.addStruct([makeField(kWasmI32, false)]);
  let struct_type_b = builder.addStruct([makeField(kWasmI64, false)]);

  let table = builder.addTable(wasmRefNullType(struct_type_a), 4, 4);
  builder.addActiveElementSegment(
    table, wasmI32Const(0),
    [[...wasmI32Const(10), kGCPrefix, kExprStructNew, struct_type_b]],
     wasmRefNullType(struct_type_b)); // Mismatches table type.

  assertThrows(() => builder.instantiate({}),
               WebAssembly.CompileError,
               /Element segment .* is not a subtype of referenced table/);
})();

(function TestRefTableInvalidSegmentExpressionType() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct_type_a = builder.addStruct([makeField(kWasmI32, false)]);
  let struct_type_b = builder.addStruct([makeField(kWasmI64, false)]);

  let table = builder.addTable(wasmRefNullType(struct_type_a), 4, 4);
  builder.addActiveElementSegment(
    table, wasmI32Const(0),
    [[...wasmI64Const(10), kGCPrefix, kExprStructNew, struct_type_b]],
     wasmRefNullType(struct_type_a));

  assertThrows(() => builder.instantiate({}),
               WebAssembly.CompileError,
               /expected \(ref null 0\), got \(ref 1\)/);
})();

(function TestMultiModuleRefTableEquivalentTypes() {
  print(arguments.callee.name);
  let exporting_instance = (() => {
    let builder = new WasmModuleBuilder();
    let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
    builder.addTable(wasmRefNullType(struct_type), 1, 100).exportAs('table');
    return builder.instantiate({});
  })();

  // Mismatching struct definition.
  assertThrows(() => {
    let builder = new WasmModuleBuilder();
    let struct_type = builder.addStruct([makeField(kWasmI64, false)]);
    builder.addImportedTable(
        'imports', 'table', 1, 100, wasmRefNullType(struct_type));
    builder.instantiate({imports: {table: exporting_instance.exports.table}})
  }, WebAssembly.LinkError, /imported table does not match the expected type/);

  // Mismatching nullability.
  assertThrows(() => {
    let builder = new WasmModuleBuilder();
    let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
    builder.addImportedTable(
        'imports', 'table', 1, 100, wasmRefType(struct_type));
    builder.instantiate({imports: {table: exporting_instance.exports.table}})
  }, WebAssembly.LinkError, /imported table does not match the expected type/);

  // Equivalent struct type.
  let builder = new WasmModuleBuilder();
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let struct_type_invalid = builder.addStruct([makeField(kWasmI64, false)]);
  let struct_type_sub = builder.addStruct(
      [makeField(kWasmI32, false), makeField(kWasmI32, false)], struct_type);
  builder.addImportedTable(
      'imports', 'table', 1, 100, wasmRefNullType(struct_type));

  builder.addFunction("invalid_struct", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI64Const, 44,
      kGCPrefix, kExprStructNew, struct_type_invalid,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();

  builder.addFunction("valid_struct", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 44,
      kGCPrefix, kExprStructNew, struct_type,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();

    builder.addFunction("valid_struct_sub", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 55,
      kExprI32Const, 66,
      kGCPrefix, kExprStructNew, struct_type_sub,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();

  let table = exporting_instance.exports.table;
  let instance = builder.instantiate({imports: {table}});

  assertThrows(() => table.grow(5, undefined), TypeError);
  table.grow(5);
  assertThrows(() => table.set(1, instance.exports.invalid_struct()),
               TypeError);
  table.set(1, instance.exports.valid_struct());
  table.set(2, instance.exports.valid_struct_sub());
  table.set(3, null);
  assertThrows(() => table.set(1, undefined), TypeError);
})();

(function TestMultiModuleRefTableSuperType() {
  print(arguments.callee.name);
  let exporting_instance = (() => {
    let builder = new WasmModuleBuilder();
    let struct_type_base = builder.addStruct([makeField(kWasmI32, false)]);
    let struct_type =
        builder.addStruct([makeField(kWasmI32, false)], struct_type_base);
    builder.addTable(wasmRefNullType(struct_type), 1, 100).exportAs('table');
    return builder.instantiate({});
  })();

  let builder = new WasmModuleBuilder();
  let struct_type_base = builder.addStruct([makeField(kWasmI32, false)]);
  let struct_type =
      builder.addStruct([makeField(kWasmI32, false)], struct_type_base);
  builder.addImportedTable(
      'imports', 'table', 1, 100, wasmRefNullType(struct_type));

  builder.addFunction("struct_base", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 66,
      kGCPrefix, kExprStructNew, struct_type_base,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();

  let table = exporting_instance.exports.table;
  let instance = builder.instantiate({imports: {table}});
  assertThrows(() => table.set(0, instance.exports.struct_base()), TypeError);
  assertThrows(() => table.grow(1, instance.exports.struct_base()), TypeError);
})();

(function TestRefTableNotNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let table = builder.addTable(wasmRefType(struct_type), 2, 6,
      [...wasmI32Const(111),
       kGCPrefix, kExprStructNew, struct_type])
    .exportAs("table");

  builder.addFunction("create_struct", makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct_type,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction("struct_getter", kSig_i_i)
    .addBody([
      kExprLocalGet, 0, kExprTableGet, 0,
      kGCPrefix, kExprStructGet, struct_type, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasmTable = instance.exports.table;

  // Initial values.
  assertEquals(111, instance.exports.struct_getter(0));
  assertEquals(111, instance.exports.struct_getter(1));
  assertTraps(kTrapTableOutOfBounds, () => instance.exports.struct_getter(2));

  assertThrows(() => wasmTable.grow(1), TypeError,
               /Argument 1 must be specified for non-nullable element type/);
  wasmTable.grow(1, instance.exports.create_struct(222));
  assertEquals(222, instance.exports.struct_getter(2));
  assertThrows(() => wasmTable.set(2, undefined), TypeError,
               /Argument 1 is invalid/);
  assertThrows(() => wasmTable.set(2, null), TypeError,
               /Argument 1 is invalid/);
  wasmTable.set(2, instance.exports.create_struct(333));
  assertEquals(333, instance.exports.struct_getter(2));
})();

(function TestTypedTableCallIndirect() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let super_struct = builder.addStruct([makeField(kWasmI32, false)]);
  let sub_struct = builder.addStruct(
    [makeField(kWasmI32, false), makeField(kWasmI32, false)], super_struct);
  let super_sig = builder.addType(
    makeSig([kWasmI32], [wasmRefType(super_struct)]), kNoSuperType, false);
  let sub_sig = builder.addType(
    makeSig([kWasmI32], [wasmRefType(sub_struct)]), super_sig);

  let super_func = builder.addFunction("super_func", super_sig)
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructNew, super_struct]);
  let sub_func = builder.addFunction("super_func", sub_sig)
    .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
              kExprLocalGet, 0, kExprI32Const, 2, kExprI32Add,
              kGCPrefix, kExprStructNew, sub_struct]);

  let table = builder.addTable(wasmRefNullType(super_sig), 10, 10);
  builder.addActiveElementSegment(
    table.index, wasmI32Const(0),
    [[kExprRefFunc, super_func.index], [kExprRefFunc, sub_func.index]],
    wasmRefType(super_sig));

  // Parameters: index, value.
  builder.addFunction("call_indirect_super", kSig_i_ii)
    .addBody([kExprLocalGet, 1, kExprLocalGet, 0,
              kExprCallIndirect, super_sig, table.index,
              kGCPrefix, kExprStructGet, super_struct, 0])
    .exportFunc();
  builder.addFunction("call_indirect_sub", kSig_i_ii)
    .addBody([kExprLocalGet, 1, kExprLocalGet, 0,
              kExprCallIndirect, sub_sig, table.index,
              kGCPrefix, kExprStructGet, sub_struct, 0])
    .exportFunc();

  let instance = builder.instantiate();

  // No type check needed, null check needed.
  assertEquals(10, instance.exports.call_indirect_super(0, 10));
  assertEquals(11, instance.exports.call_indirect_super(1, 10));
  assertTraps(kTrapFuncSigMismatch,
              () => instance.exports.call_indirect_super(2, 10));
  // Type check and null check needed.
  assertEquals(11, instance.exports.call_indirect_sub(1, 10));
  assertTraps(kTrapFuncSigMismatch,
              () => instance.exports.call_indirect_sub(0, 10));
  assertTraps(kTrapFuncSigMismatch,
              () => instance.exports.call_indirect_sub(2, 10));
})();
