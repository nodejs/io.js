// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --no-wasm-lazy-compilation
// Flags: --no-wasm-inlining-call-indirect --no-wasm-loop-unrolling
// Flags: --no-wasm-loop-peeling

// This tests are meant to examine if Turbofan CsaLoadElimination works
// correctly for wasm. The TurboFan graphs can be examined with --trace-turbo.
d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Fresh objects, known offsets
(function LoadEliminationtFreshKnownTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true),
                                  makeField(kWasmI32, true)]);

  builder.addFunction("main", makeSig([kWasmI32], [kWasmI32]))
    .addLocals(wasmRefNullType(struct), 1)
    .addBody([
      kExprI32Const, 10,  // local1 = struct(10, 100);
      kExprI32Const, 100,
      kGCPrefix, kExprStructNew, struct,
      kExprLocalSet, 1,
      kExprLocalGet, 0,  // Split control based on an unknown value
      kExprIf, kWasmI32,
        kExprLocalGet, 1,  // local1.field1 = 42
        kExprI32Const, 42,
        kGCPrefix, kExprStructSet, struct, 1,
        kExprLocalGet, 1,  // local1.field1
        kGCPrefix, kExprStructGet, struct, 1,
      kExprElse,
        kExprLocalGet, 1,  // local1.field1 = 11
        kExprI32Const, 11,
        kGCPrefix, kExprStructSet, struct, 1,
        kExprLocalGet, 1,  // local1.field1 = 22
        kExprI32Const, 22,
        kGCPrefix, kExprStructSet, struct, 1,
        kExprLocalGet, 1,    // local1.field1 + local1.field1
        kGCPrefix, kExprStructGet, struct, 1,
        kExprLocalGet, 1,
        kGCPrefix, kExprStructGet, struct, 1,
        kExprI32Add,
      kExprEnd,
      kExprLocalGet, 1,  // return if-result * (local1.field1 + local1.field0)
      kGCPrefix, kExprStructGet, struct, 0,
      kExprLocalGet, 1,
      kGCPrefix, kExprStructGet, struct, 1,
      kExprI32Add,
      kExprI32Mul
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(instance.exports.main(1), 42 * (42 + 10));
  assertEquals(instance.exports.main(0), (22 + 22) * (22 + 10));
})();

(function LoadEliminationtConstantKnownTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  let replaced_value = 55
  let param_1_value = 42
  let init_value_1 = 5
  let init_value_2 = 17

  let tester = builder.addFunction("tester", makeSig(
      [wasmRefType(struct), wasmRefType(struct)], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructGet, struct, 0,

      kExprLocalGet, 0,
      kExprI32Const, replaced_value,
      kGCPrefix, kExprStructSet, struct, 0,

      // We should eliminate this load and replace it with replaced_value
      kExprLocalGet, 0,
      kGCPrefix, kExprStructGet, struct, 0,

      kExprLocalGet, 1,
      kExprI32Const, param_1_value,
      kGCPrefix, kExprStructSet, struct, 0,

      // Although we could eliminate this load before, we cannot anymore,
      // because the parameters may alias.
      kExprLocalGet, 0,
      kGCPrefix, kExprStructGet, struct, 0,

      kExprI32Add, kExprI32Add
    ]);

  function buildStruct(value) {
    return [kExprI32Const, value,
            kGCPrefix, kExprStructNew, struct];
  }

  builder.addFunction("main_non_aliasing", kSig_i_v)
    .addBody([
      ...buildStruct(init_value_1), ...buildStruct(init_value_2),
      kExprCallFunction, tester.index])
    .exportFunc();

  builder.addFunction("main_aliasing", kSig_i_v)
    .addLocals(wasmRefNullType(struct), 1)
    .addBody([
      ...buildStruct(init_value_1), kExprLocalSet, 0,
      kExprLocalGet, 0, kExprRefAsNonNull,
      kExprLocalGet, 0, kExprRefAsNonNull,
      kExprCallFunction, tester.index])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(init_value_1 + replaced_value + replaced_value,
               instance.exports.main_non_aliasing());
  assertEquals(init_value_1 + replaced_value + param_1_value,
               instance.exports.main_aliasing());
})();

(function LoadEliminationtArbitraryKnownTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  let initial_value = 19;
  let replacing_value_1 = 55;
  let replacing_value_2 = 37;

  let id = builder.addFunction("id", makeSig([wasmRefNullType(struct)],
                                             [wasmRefNullType(struct)]))
      .addBody([kExprLocalGet, 0])

  builder.addFunction("main", kSig_i_v)
    .addLocals(wasmRefNullType(struct), 2)
    .addBody([
      // We store a fresh struct in local0
      kExprI32Const, initial_value,
      kGCPrefix, kExprStructNew, struct,
      kExprLocalSet, 0,

      // We pass it through a function and store it to local1. local1 may now
      // alias with anything.
      kExprLocalGet, 0, kExprCallFunction, id.index, kExprLocalSet, 1,

      kExprLocalGet, 0,
      kExprI32Const, replacing_value_1,
      kGCPrefix, kExprStructSet, struct, 0,

      // We should eliminate this load.
      kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct, 0,

      kExprLocalGet, 1,
      kExprI32Const, replacing_value_2,
      kGCPrefix, kExprStructSet, struct, 0,

      // We should not eliminate this load.
      kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct, 0,

      kExprI32Add])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(replacing_value_1 + replacing_value_2, instance.exports.main());
})();

(function LoadEliminationtFreshUnknownTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI64, true);

  // parameter: unknown array index
  builder.addFunction("main", makeSig([kWasmI32], [kWasmI32]))
    .addLocals(wasmRefNullType(array), 1)
    .addBody([
      kExprI32Const, 5,
      kGCPrefix, kExprArrayNewDefault, array,
      kExprLocalSet, 1,

      kExprLocalGet, 1,  // a[i] = i for i = {0..4}
      kExprI32Const, 0,
      kExprI64Const, 0,
      kGCPrefix, kExprArraySet, array,

      kExprLocalGet, 1,
      kExprI32Const, 1,
      kExprI64Const, 1,
      kGCPrefix, kExprArraySet, array,

      kExprLocalGet, 1,
      kExprI32Const, 2,
      kExprI64Const, 2,
      kGCPrefix, kExprArraySet, array,

      kExprLocalGet, 1,
      kExprI32Const, 3,
      kExprI64Const, 3,
      kGCPrefix, kExprArraySet, array,

      kExprLocalGet, 1,
      kExprI32Const, 4,
      kExprI64Const, 4,
      kGCPrefix, kExprArraySet, array,

      // Get a constant index a[4] before setting unknown indices
      kExprLocalGet, 1,
      kExprI32Const, 4,
      kGCPrefix, kExprArrayGet, array,

      kExprLocalGet, 1,  // Set a[local0] = 33
      kExprLocalGet, 0,
      kExprI64Const, 33,
      kGCPrefix, kExprArraySet, array,

      kExprLocalGet, 1,  // Get a[local0]
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayGet, array,

      kExprLocalGet, 1,  // Known index load cannot be eliminated anymore
      kExprI32Const, 3,
      kGCPrefix, kExprArrayGet, array,

      // A load from different unknown index a[local0 + 1] cannot be eliminated
      kExprLocalGet, 1,
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Add,
      kGCPrefix, kExprArrayGet, array,

      kExprI64Add, // return a[4] * (a[local0] - (a[3] + a[local0 + 1]))
      kExprI64Sub,
      kExprI64Mul,
      kExprI32ConvertI64  // To not have to worry about BigInts in JS world
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(4 * (33 - (3 + 1)), instance.exports.main(0));
  assertEquals(4 * (33 - (3 + 2)), instance.exports.main(1));
  assertEquals(4 * (33 - (3 + 3)), instance.exports.main(2));
  assertEquals(4 * (33 - (33 + 4)), instance.exports.main(3));
})();

(function LoadEliminationtAllBetsAreOffTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let array = builder.addArray(kWasmI32, true);

  let value_0 = 19;
  let value_1 = 55;
  let value_2 = 2;

  let id = builder.addFunction("id", makeSig([wasmRefNullType(array)],
                                             [wasmRefNullType(array)]))
      .addBody([kExprLocalGet, 0])

  // parameters: array, index
  let tester = builder.addFunction("tester",
      makeSig([wasmRefType(array), kWasmI32], [kWasmI32]))
    .addLocals(wasmRefNullType(struct), 1)
    .addLocals(wasmRefNullType(array), 1)
    .addBody([
      // We store a fresh struct in local1
      kExprI32Const, 0,
      kGCPrefix, kExprStructNew, struct,
      kExprLocalSet, 2,

      // We pass the array parameter through a function and store it to local2.
      kExprLocalGet, 0, kExprCallFunction, id.index, kExprLocalSet, 3,

      // Set the parameter array, the fresh struct, then the arbitrary array to
      // an unknown offset.
      kExprLocalGet, 0,
      kExprI32Const, 5,
      kExprI32Const, value_0,
      kGCPrefix, kExprArraySet, array,

      kExprLocalGet, 2,
      kExprI32Const, value_1,
      kGCPrefix, kExprStructSet, struct, 0,

      kExprLocalGet, 3,
      kExprLocalGet, 1,
      kExprI32Const, value_2,
      kGCPrefix, kExprArraySet, array,

      // Neither load can be eliminated.
      kExprLocalGet, 0,
      kExprI32Const, 5,
      kGCPrefix, kExprArrayGet, array,

      kExprLocalGet, 2,
      kGCPrefix, kExprStructGet, struct, 0,

      kExprI32Add]);

  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprI32Const, 10,
      kGCPrefix, kExprArrayNewDefault, array,
      kExprI32Const, 7,
      kExprCallFunction, tester.index,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(value_0 + value_1, instance.exports.main());
})();

(function WasmLoadEliminationArrayLength() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);
  builder.addFunction("producer", makeSig([kWasmI32], [wasmRefType(array)]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayNewDefault, array])
    .exportFunc();
  let side_effect = builder.addFunction("side_effect", kSig_v_v).addBody([]);
  builder.addFunction("tester", makeSig([wasmRefType(array)], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayLen,
              kExprI32Const, 1, kExprI32Add,
              kGCPrefix, kExprArrayNewDefault, array,
              kExprCallFunction, side_effect.index,  // unknown side-effect
              kGCPrefix, kExprArrayLen,
              kExprLocalGet, 0, kGCPrefix, kExprArrayLen,
              kExprI32Mul])
    .exportFunc();
  let instance = builder.instantiate();
  assertEquals(10 * 11,
               instance.exports.tester(instance.exports.producer(10)));
})();

(function WasmLoadEliminationUnrelatedTypes() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct1 = builder.addStruct([makeField(kWasmI32, true)]);
  let struct2 = builder.addStruct([makeField(kWasmI32, true),
                                   makeField(kWasmI64, true)]);

  builder.addFunction("tester",
      makeSig([wasmRefType(struct1), wasmRefType(struct2)], [kWasmI32]))
    // f(x, y) { y.f = x.f + 10; return y.f * x.f }
    // x.f load in the state should survive y.f store.
    .addBody([kExprLocalGet, 1,
              kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct1, 0,
              kExprI32Const, 10, kExprI32Add,
              kGCPrefix, kExprStructSet, struct2, 0,
              kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct1, 0,
              kExprLocalGet, 1, kGCPrefix, kExprStructGet, struct2, 0,
              kExprI32Mul]);

  builder.instantiate()
})();

(function EscapeAnalysisWithLoadElimination() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct1 = builder.addStruct([makeField(kWasmI32, true)]);
  let struct2 = builder.addStruct([makeField(wasmRefNullType(struct1), true)]);

  // TF should eliminate both allocations in this function.
  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct1,
      kGCPrefix, kExprStructNew, struct2,
      kGCPrefix, kExprStructGet, struct2, 0,
      kGCPrefix, kExprStructGet, struct1, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(42, instance.exports.main(42));
})();

(function EscapeAnalysisWithInterveningEffect() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let struct1 = builder.addStruct([makeField(kWasmI32, true)]);
  let struct2 = builder.addStruct([makeField(wasmRefNullType(struct1), true)]);

  let nop = builder.addFunction("nop", kSig_v_v).addBody([]);

  // TF should eliminate both allocations in this function, despite the
  // intervening effectful call.
  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct1,
      kExprCallFunction, nop.index,
      kGCPrefix, kExprStructNew, struct2,
      kExprLocalGet, 0,
      kExprReturn])
    .exportFunc();

  let instance = builder.instantiate({});
  assertEquals(42, instance.exports.main(42));
})();

(function AllocationFolding() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  let struct_index = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_2 = builder.addStruct([
    makeField(wasmRefType(struct_index), false),
    makeField(wasmRefType(struct_index), false)
  ]);

  let global = builder.addGlobal(
      wasmRefNullType(struct_2), true, false, [kExprRefNull, struct_2]);

  // The three alocations should be folded.
  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct_index,
      kExprI32Const, 43,
      kGCPrefix, kExprStructNew, struct_index,
      kGCPrefix, kExprStructNew, struct_2,
      kExprGlobalSet, global.index,
      kExprLocalGet, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate();
  assertEquals(10, instance.exports.main(10));
})();

(function PathBasedTypedOptimization() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  let super_struct = builder.addStruct([makeField(kWasmI32, true)]);
  let mid_struct = builder.addStruct(
    [makeField(kWasmI32, true), makeField(kWasmI32, true)], super_struct);
  let sub_struct = builder.addStruct(
    [makeField(kWasmI32, true), makeField(kWasmI32, true),
     makeField(kWasmI32, true)],
    mid_struct);

  let addToLocal = [kExprLocalGet, 1, kExprI32Add, kExprLocalSet, 1];

  builder.addFunction(
      "pathBasedTypes", makeSig([wasmRefNullType(super_struct)], [kWasmI32]))
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprRefTest, sub_struct,

      // These casts have to be preserved.
      kExprLocalGet, 0,
      kGCPrefix, kExprRefCast, mid_struct,
      kGCPrefix, kExprRefCast, sub_struct,
      kGCPrefix, kExprStructGet, sub_struct, 1,
      ...addToLocal,

      kExprIf, kWasmVoid,
        // Both these casts should be optimized away.
        kExprLocalGet, 0,
        kGCPrefix, kExprRefCast, mid_struct,
        kGCPrefix, kExprRefCast, sub_struct,
        kGCPrefix, kExprStructGet, sub_struct, 1,
        ...addToLocal,

        kExprBlock, kWasmRefNull, super_struct,
          kExprLocalGet, 0,
          // This should also get optimized away.
          kGCPrefix, kExprBrOnCastFail, 0b11, 0, super_struct,
              mid_struct,
          // So should this, despite being represented by a TypeGuard alias.
          kGCPrefix, kExprRefCast, sub_struct,
          kGCPrefix, kExprStructGet, sub_struct, 1,
          ...addToLocal,
          kExprLocalGet, 0,  // Due to the branch result type.
        kExprEnd,
        kExprDrop,
      kExprElse,
        // This (always trapping) cast should be optimized away.
        // (If the ref.test in the start block returns 0 the cast to sub_struct
        // in that block will already fail.)
        kExprLocalGet, 0,
        kGCPrefix, kExprRefCast, sub_struct,
        kGCPrefix, kExprStructGet, sub_struct, 1,
        ...addToLocal,
      kExprEnd,
      // This cast should be optimized away.
      kExprLocalGet, 0,
      kGCPrefix, kExprRefCast, sub_struct,
      kGCPrefix, kExprStructGet, sub_struct, 1,
      kExprLocalGet, 1, kExprI32Add
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertTraps(kTrapIllegalCast, () => wasm.pathBasedTypes(null));
})();

(function IndependentCastNullRefType() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct_super = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_b = builder.addStruct([makeField(kWasmI32, true)], struct_super);
  let struct_a = builder.addStruct(
    [makeField(kWasmI32, true), makeField(kWasmI32, true)], struct_super);

  let callee_sig = makeSig([wasmRefNullType(struct_a)], [kWasmI32]);
  let callee = builder.addFunction("callee", callee_sig)
    .addBody([
      // Cast from struct_a to struct_b via common base type struct_super.
      kExprLocalGet, 0,
      kGCPrefix, kExprRefCastNull, struct_super,
      kGCPrefix, kExprRefCastNull, struct_b, // annotated as 'ref null none'
      kExprRefIsNull,
    ]);

  builder.addFunction("main", kSig_i_i)
    .addLocals(wasmRefNullType(struct_a), 1)
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kWasmVoid,
      kExprI32Const, 10,
      kExprI32Const, 100,
      kGCPrefix, kExprStructNew, struct_a,
      kExprLocalSet, 1,
      kExprEnd,
      kExprLocalGet, 1,
      kExprCallFunction, callee.index
    ]).exportFunc();

  let instance = builder.instantiate({});
  // main calls 'callee(null)'
  // -> (ref.is_null (ref.cast struct_b (ref.cast struct_super (local.get 0))))
  //    returns true.
  assertEquals(1, instance.exports.main(0));
  // main calls 'callee(struct.new struct_a)'
  // -> (ref.cast struct_b) traps.
  assertTraps(kTrapIllegalCast, () => instance.exports.main(1));
})();

(function StaticCastOfKnownNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct_super = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_b = builder.addStruct([makeField(kWasmI32, true)], struct_super);
  let struct_a = builder.addStruct(
    [makeField(kWasmI32, true), makeField(kWasmI32, true)], struct_super);

  let callee_sig = makeSig([wasmRefNullType(struct_super)], [kWasmI32]);
  let callee = builder.addFunction("callee", callee_sig)
    .addBody([
      kExprBlock, kWasmRefNull, struct_super,
      kExprLocalGet, 0,
      kExprBrOnNonNull, 0,
      // local.get 0 is known to be null until end of block.
      kExprLocalGet, 0,
      // This cast is a no-op and shold be optimized away.
      kGCPrefix, kExprRefCastNull, struct_b,
      kExprEnd,
      kExprRefIsNull,
    ]);

  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprRefNull, struct_a,
      kExprCallFunction, callee.index
    ]).exportFunc();

  let instance = builder.instantiate({});
  assertEquals(1, instance.exports.main());
})();

(function AssertNullAfterCastIncompatibleTypes() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct_super = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_b = builder.addStruct([makeField(kWasmI32, true)], struct_super);
  let struct_a = builder.addStruct(
    [makeField(kWasmI32, true), makeField(kWasmI32, true)], struct_super);
  let callee_sig = makeSig([wasmRefNullType(struct_super)], [kWasmI32]);

  builder.addFunction("mkStruct", makeSig([], [kWasmExternRef]))
    .addBody([kGCPrefix, kExprStructNewDefault, struct_a,
              kGCPrefix, kExprExternConvertAny])
    .exportFunc();

  let callee = builder.addFunction("callee", callee_sig)
    .addBody([
       kExprLocalGet, 0, kGCPrefix, kExprRefCast, struct_b,
       kExprRefAsNonNull,
       kGCPrefix, kExprStructGet, struct_b, 0]);

  builder.addFunction("main", makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprAnyConvertExtern,
              kGCPrefix, kExprRefCast, struct_a,
              kExprCallFunction, callee.index])
    .exportFunc();

  let instance = builder.instantiate({});
  assertTraps(kTrapIllegalCast,
              () => instance.exports.main(instance.exports.mkStruct()));
})();

(function StructGetMultipleNullChecks() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true),
                                  makeField(kWasmI32, true)]);

  builder.addFunction("main",
                      makeSig([kWasmI32, wasmRefNullType(struct)], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kWasmI32,
        kExprLocalGet, 1,
        kGCPrefix, kExprStructGet, struct, 0,
        kExprLocalGet, 1,
        // The null check should be removed for this struct.
        kGCPrefix, kExprStructGet, struct, 1,
        kExprI32Add,
      kExprElse,
        kExprLocalGet, 1,
        kGCPrefix, kExprStructGet, struct, 0,
      kExprEnd,
      kExprLocalGet, 1,
      // The null check here could be removed if we compute type intersections.
      kGCPrefix, kExprStructGet, struct, 1,
      kExprI32Mul])
    .exportFunc();

  builder.instantiate({});
})();

(function StructSetMultipleNullChecks() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true),
                                  makeField(kWasmI32, true)]);

  builder.addFunction("structSetMultiple",
                      makeSig([wasmRefNullType(struct)], []))
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 42,
      kGCPrefix, kExprStructSet, struct, 0,
      kExprLocalGet, 0,
      kExprI32Const, 43,
      kGCPrefix, kExprStructSet, struct, 1,
    ])
    .exportFunc();

  let wasm = builder.instantiate({}).exports;
  assertTraps(kTrapNullDereference, () => wasm.structSetMultiple(null));
})();

(function ArrayLenMultipleNullChecks() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);

  builder.addFunction("arrayLenMultiple",
                      makeSig([wasmRefNullType(array)], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayLen,
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayLen,
    ])
    .exportFunc();

  let wasm = builder.instantiate({}).exports;
  assertTraps(kTrapNullDereference, () => wasm.arrayLenMultiple(null));
})();

(function RedundantExternalizeInternalize() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);

  builder.addFunction('createArray',
      makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixed, array, 1,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('get', makeSig([kWasmExternRef, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      // The following two operations are optimized away.
      kGCPrefix, kExprExternConvertAny,
      kGCPrefix, kExprAnyConvertExtern,
      //
      kGCPrefix, kExprRefCastNull, array,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGet, array,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let wasmArray = wasm.createArray(10);
  assertEquals(10, wasm.get(wasmArray, 0));
})();

(function RedundantIsNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);

  builder.addFunction('checkIsNullAfterNonNullCast',
      makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprRefCast, kExternRefCode,
      kExprDrop,
      kExprLocalGet, 0,
      kExprRefIsNull,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertTraps(kTrapIllegalCast, () => wasm.checkIsNullAfterNonNullCast(null));
  assertEquals(0, wasm.checkIsNullAfterNonNullCast("not null"));
})();

(function RefTestUnrelated() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let other = builder.addStruct([makeField(kWasmI64, true)]);

  builder.addFunction('refTestUnrelatedNull',
      makeSig([kWasmAnyRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprRefCastNull, other,
      kExprDrop,
      kExprLocalGet, 0,
      // This ref.test will only succeed if the input is null.
      kGCPrefix, kExprRefTestNull, struct,
    ])
    .exportFunc();

    builder.addFunction('refTestUnrelated',
    makeSig([kWasmAnyRef], [kWasmI32]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprRefCastNull, other,
    kExprDrop,
    kExprLocalGet, 0,
    // This ref.test always returns 0.
    kGCPrefix, kExprRefTest, struct,
  ])
  .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertEquals(1, wasm.refTestUnrelatedNull(null));
  assertTraps(kTrapIllegalCast, () => wasm.refTestUnrelatedNull("not null"));
  assertEquals(0, wasm.refTestUnrelated(null));
  assertTraps(kTrapIllegalCast, () => wasm.refTestUnrelated("not null"));
})();

(function RefFuncIsNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let fct = builder.addFunction('dummy', makeSig([], []))
      .addBody([]).exportFunc();

  builder.addFunction('refFuncIsNull',
      makeSig([], [kWasmI32]))
    .addLocals(kWasmFuncRef, 1)
    .addBody([
      kExprRefFunc, fct.index,
      kExprLocalSet, 0,
      kExprLocalGet, 0,
      kExprRefIsNull,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertEquals(0, wasm.refFuncIsNull());
})();


(function ArrayNewRefTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array_base = builder.addArray(kWasmI32, true);
  let array_sub = builder.addArray(kWasmI32, true, array_base);
  let array_other = builder.addArray(kWasmI64, true);

  builder.addFunction('arrayNewRefTest',
      makeSig([], [kWasmI32, kWasmI32, kWasmI32]))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprArrayNewFixed, array_sub, 1,
      kExprLocalSet, 0,
      // All these checks can be statically inferred.
      kExprLocalGet, 0, kGCPrefix, kExprRefTest, array_base,
      kExprLocalGet, 0, kGCPrefix, kExprRefTest, array_sub,
      kExprLocalGet, 0, kGCPrefix, kExprRefTest, array_other,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertEquals([1, 1, 0], wasm.arrayNewRefTest());
})();

(function TypePropagationPhi() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array_base = builder.addArray(kWasmI32, true);
  let array_sub = builder.addArray(kWasmI32, true, array_base);

  builder.addFunction('typePhi',
      makeSig([kWasmI32], [kWasmI32]))
    .addLocals(kWasmArrayRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kArrayRefCode,
        kExprLocalGet, 0,
        kGCPrefix, kExprArrayNewFixed, array_base, 1,
      kExprElse,
        kExprLocalGet, 0,
        kGCPrefix, kExprArrayNewFixed, array_sub, 1,
      kExprEnd,
      // While the two inputs to the phi have different types (ref $array_base)
      // and (ref $array_sub), they both share the information of being not
      // null, so the ref.is_null can be optimized away. Due to escape analysis,
      // the whole function can be simplified to just returning 0.
      kExprRefIsNull,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertEquals(0, wasm.typePhi(0));
  assertEquals(0, wasm.typePhi(1));
})();

(function TypePropagationLoopPhiOptimizable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct_base = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_sub = builder.addStruct([makeField(kWasmI32, true)], struct_base);

  // This function counts all the structs stored in local[1] which are of type
  // struct_sub (which in this case are all the values).
  builder.addFunction('loopPhiOptimizable',
      makeSig([kWasmI32], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1) // local with changing type
    .addLocals(kWasmI32, 1)    // result
    .addBody([
      kGCPrefix, kExprStructNewDefault, struct_sub,
      kExprLocalSet, 1,
      kExprLoop, kWasmVoid,
        // result += ref.test (local.get 1)
        kExprLocalGet, 1,
        kGCPrefix, kExprRefTest, struct_sub,
        kExprLocalGet, 2,
        kExprI32Add,
        kExprLocalSet, 2,
        // local[1] = new struct_sub
        kGCPrefix, kExprStructNewDefault, struct_sub,
        kExprLocalSet, 1, // This will cause a loop phi.
        // if (--(local.get 0)) continue;
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 0,
        kExprBrIf, 0,
      kExprEnd,
      kExprLocalGet, 2,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertEquals(1, wasm.loopPhiOptimizable(1));
  assertEquals(2, wasm.loopPhiOptimizable(2));
  assertEquals(20, wasm.loopPhiOptimizable(20));
})();

(function TypePropagationLoopPhiCheckRequired() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct_base = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_sub = builder.addStruct([makeField(kWasmI32, true)], struct_base);

  // This function counts all the structs stored in local[1] which are of type
  // struct_sub (which in this case is only the first).
  builder.addFunction('loopPhiCheckRequired',
      makeSig([kWasmI32], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1) // local with changing type
    .addLocals(kWasmI32, 1)    // result
    .addBody([
      kGCPrefix, kExprStructNewDefault, struct_sub,
      kExprLocalSet, 1,
      kExprLoop, kWasmVoid,
        // result += ref.test (local.get 1)
        kExprLocalGet, 1,
        kGCPrefix, kExprRefTest, struct_sub,
        kExprLocalGet, 2,
        kExprI32Add,
        kExprLocalSet, 2,
        // local[1] = new struct_base
        kGCPrefix, kExprStructNewDefault, struct_base,
        kExprLocalSet, 1, // This will cause a loop phi.
        // if (--(local.get 0)) continue;
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 0,
        kExprBrIf, 0,
      kExprEnd,
      kExprLocalGet, 2,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertEquals(1, wasm.loopPhiCheckRequired(1));
  assertEquals(1, wasm.loopPhiCheckRequired(2));
  assertEquals(1, wasm.loopPhiCheckRequired(20));
})();

(function TypePropagationLoopPhiCheckRequiredUnrelated() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  // Differently to the test above, here the two types merged in the loop phi
  // are not in a subtype hierarchy, meaning that the loop phi needs to merge
  // them to a more generic ref struct.
  let struct_a = builder.addStruct([makeField(kWasmI32, true)]);
  let struct_b = builder.addStruct([makeField(kWasmI64, true)]);

  // This function counts all the structs stored in local[1] which are of type
  // struct_a (which in this case is only the first).
  builder.addFunction('loopPhiCheckRequiredUnrelated',
      makeSig([kWasmI32], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1) // local with changing type
    .addLocals(kWasmI32, 1)    // result
    .addBody([
      kGCPrefix, kExprStructNewDefault, struct_a,
      kExprLocalSet, 1,
      kExprLoop, kWasmVoid,
        // result += ref.test (local.get 1)
        kExprLocalGet, 1,
        kGCPrefix, kExprRefTest, struct_a,
        kExprLocalGet, 2,
        kExprI32Add,
        kExprLocalSet, 2,
        // local[1] = new struct_base
        kGCPrefix, kExprStructNewDefault, struct_b,
        kExprLocalSet, 1, // This will cause a loop phi.
        // if (--(local.get 0)) continue;
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 0,
        kExprBrIf, 0,
      kExprEnd,
      kExprLocalGet, 2,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertEquals(1, wasm.loopPhiCheckRequiredUnrelated(1));
  assertEquals(1, wasm.loopPhiCheckRequiredUnrelated(2));
  assertEquals(1, wasm.loopPhiCheckRequiredUnrelated(20));
})();

(function TypePropagationCallRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let sig = builder.addType(makeSig([kWasmI32], [wasmRefNullType(struct)]));

  builder.addFunction('callee', sig)
  .addBody([
    // local.get[0] ? null : new struct();
    kExprLocalGet, 0,
    kExprIf, kWasmVoid,
      kExprRefNull, struct,
      kExprReturn,
    kExprEnd,
    kGCPrefix, kExprStructNewDefault, struct,
  ])
  .exportFunc();

  builder.addFunction('callTypedWasm',
      makeSig([kWasmI32, wasmRefType(sig)], []))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallRef, sig,
      kExprLocalSet, 2,
      kExprLocalGet, 2,
      // Can be optimized away based on the signature of the callee.
      kGCPrefix, kExprRefCastNull, kStructRefCode,
      // Can be converted into a check for not null.
      kGCPrefix, kExprRefCast, struct,
      kExprDrop,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  wasm.callTypedWasm(0, wasm.callee);
  assertTraps(kTrapIllegalCast, () => wasm.callTypedWasm(1, wasm.callee));
})();

(function TypePropagationDeadBranch() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('deadBranch',
      makeSig([kWasmI32, kWasmStructRef], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprLocalGet, 1,
      kExprLocalSet, 2,
      kExprLocalGet, 0,
      kExprIf, kWasmVoid,
        kExprLocalGet, 2,
        // This cast always traps -> dead branch.
        kGCPrefix, kExprRefCast, kArrayRefCode,
        kExprDrop,
      kExprElse,
        kExprLocalGet, 2,
        kGCPrefix, kExprRefCast, struct,
        kExprDrop,
      kExprEnd,
      kExprLocalGet, 2,
      // This is the same cast as in the else branch. As the end of the true
      // branch is unreachable, this cast can be safely eliminated.
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;
  assertTraps(kTrapIllegalCast, () => wasm.deadBranch(0, null));
})();

(function TypePropagationDeadByRefTestTrue() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('deadBranchBasedOnRefTestTrue',
      makeSig([kWasmStructRef], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalSet, 1,
      kExprLocalGet, 1,
      // This test always succeeds, the true branch is always taken.
      kGCPrefix, kExprRefTestNull, kStructRefCode,
      kExprIf, kWasmVoid,
        kExprLocalGet, 1,
        kGCPrefix, kExprRefCast, struct,
        kExprDrop,
      kExprEnd,
      kExprLocalGet, 1,
      // This is the same cast as in the true branch. As the true branch is
      // guaranteed to be taken, the cast can be eliminated.
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;
  assertTraps(kTrapIllegalCast, () => wasm.deadBranchBasedOnRefTestTrue(null));
})();

(function TypePropagationDeadByRefTestFalse() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('deadBranchBasedOnRefTestFalse',
      makeSig([kWasmStructRef], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalSet, 1,
      kExprLocalGet, 1,
      // This test is always false, the else branch is always taken.
      kGCPrefix, kExprRefTest, kArrayRefCode,
      kExprIf, kWasmVoid,
      kExprElse,
        kExprLocalGet, 1,
        kGCPrefix, kExprRefCast, struct,
        kExprDrop,
      kExprEnd,
      kExprLocalGet, 1,
      // This is the same cast as in the else branch. As the else branch is
      // guaranteed to be taken, the cast can be eliminated.
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;
  assertTraps(kTrapIllegalCast, () => wasm.deadBranchBasedOnRefTestFalse(null));
})();

(function TypePropagationDeadByIsNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('deadBranchBasedOnIsNull',
      makeSig([wasmRefType(kWasmAnyRef)], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalSet, 1,
      kExprLocalGet, 1,
      // This is always false, the else branch is always taken.
      kExprRefIsNull,
      kExprIf, kWasmVoid,
      kExprElse,
        kExprLocalGet, 1,
        kGCPrefix, kExprRefCast, struct,
        kExprDrop,
      kExprEnd,
      kExprLocalGet, 1,
      // This is the same cast as in the else branch. As the true branch is
      // never taken, the cast can be eliminated.
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;
  assertTraps(kTrapIllegalCast, () => wasm.deadBranchBasedOnIsNull({}));
})();
