// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let instance = (() => {
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let array = builder.addArray(kWasmF64, true);
  let sig = builder.addType(makeSig([kWasmI32], [kWasmI32]));

  let func = builder.addFunction('inc', sig)
                 .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add])
                 .exportAs('inc');

  builder.addFunction('struct_producer', makeSig([], [kWasmStructRef]))
      .addBody([kGCPrefix, kExprStructNewDefault, struct])
      .exportFunc();

  builder.addFunction('array_producer', makeSig([], [kWasmArrayRef]))
      .addBody([
        kExprI32Const, 10,
        kGCPrefix, kExprArrayNewDefault, array
      ])
      .exportFunc();

  builder.addFunction('i31_as_eq_producer', makeSig([], [kWasmEqRef]))
      .addBody([kExprI32Const, 5, kGCPrefix, kExprRefI31])
      .exportFunc();

  builder.addFunction('func_producer', makeSig([], [wasmRefType(sig)]))
      .addBody([kExprRefFunc, func.index])
      .exportFunc();

  let test_types = {
    struct: kWasmStructRef,
    array: kWasmArrayRef,
    raw_struct: struct,
    raw_array: array,
    typed_func: sig,
    i31: kWasmI31Ref,
    eq: kWasmEqRef,
    func: kWasmFuncRef,
    any: kWasmAnyRef,
    extern: kWasmExternRef,
    none: kWasmNullRef,
    nofunc: kWasmNullFuncRef,
    noextern: kWasmNullExternRef,
  };

  for (key in test_types) {
    let type = wasmRefNullType(test_types[key]);
    builder.addFunction(key + '_id', makeSig([type], [type]))
        .addBody([kExprLocalGet, 0])
        .exportFunc();
    builder.addFunction(key + '_null', makeSig([], [type]))
        .addBody([kExprRefNull, ...wasmSignedLeb(test_types[key])])
        .exportFunc();
  }

  return builder.instantiate({});
})();

// Wasm-exposed null is the same as JS null.
assertEquals(instance.exports.struct_null(), null);

// We can roundtrip a struct as structref.
instance.exports.struct_id(instance.exports.struct_producer());
// We cannot roundtrip an array as structref.
assertThrows(
    () => instance.exports.struct_id(instance.exports.array_producer()),
    TypeError,
    'type incompatibility when transforming from/to JS');
// We can roundtrip null as structref.
instance.exports.struct_id(instance.exports.struct_null());
// We cannot roundtrip an i31 as structref.
assertThrows(
    () => instance.exports.struct_id(instance.exports.i31_as_eq_producer()),
    TypeError,
    'type incompatibility when transforming from/to JS');

// We can roundtrip a struct as eqref.
instance.exports.eq_id(instance.exports.struct_producer());
// We can roundtrip an array as eqref.
instance.exports.eq_id(instance.exports.array_producer());
// We can roundtrip an i31 as eqref/i31ref.
instance.exports.eq_id(instance.exports.i31_as_eq_producer());
instance.exports.i31_id(instance.exports.i31_as_eq_producer());
// We can roundtrip any null as any null supertype.
instance.exports.eq_id(instance.exports.struct_null());
instance.exports.eq_id(instance.exports.eq_null());
instance.exports.eq_id(instance.exports.func_null());
instance.exports.eq_id(instance.exports.any_null());
instance.exports.any_id(instance.exports.struct_null());
instance.exports.any_id(instance.exports.eq_null());
instance.exports.any_id(instance.exports.func_null());
instance.exports.any_id(instance.exports.any_null());
instance.exports.i31_id(instance.exports.struct_null());
instance.exports.i31_id(instance.exports.eq_null());
instance.exports.i31_id(instance.exports.func_null());
instance.exports.i31_id(instance.exports.any_null());
instance.exports.struct_id(instance.exports.struct_null());
instance.exports.struct_id(instance.exports.eq_null());
instance.exports.struct_id(instance.exports.func_null());
instance.exports.struct_id(instance.exports.any_null());
// We cannot roundtrip a func as eqref.
assertThrows(
    () => instance.exports.eq_id(instance.exports.func_producer()), TypeError,
    'type incompatibility when transforming from/to JS');

// We can roundtrip a typed function.
instance.exports.typed_func_id(instance.exports.func_producer());
// We can roundtrip any null as typed funcion.
instance.exports.typed_func_id(instance.exports.struct_null());
// We cannot roundtrip a struct as typed funcion.
assertThrows(
    () => instance.exports.typed_func_id(instance.exports.struct_producer()),
    TypeError, 'type incompatibility when transforming from/to JS');

// We can roundtrip a func.
instance.exports.func_id(instance.exports.func_producer());
// We can roundtrip any null as func.
instance.exports.func_id(instance.exports.struct_null());
// We cannot roundtrip an i31 as func.
assertThrows(
    () => instance.exports.func_id(instance.exports.i31_as_eq_producer()),
    TypeError,
    'type incompatibility when transforming from/to JS');

// We can directly roundtrip structs or arrays.
instance.exports.raw_struct_id(instance.exports.struct_producer());
instance.exports.raw_array_id(instance.exports.array_producer());

// We cannot roundtrip an array as struct and vice versa.
assertThrows(
  () => instance.exports.raw_struct_id(instance.exports.array_producer()),
  TypeError,
  'type incompatibility when transforming from/to JS');
assertThrows(
  () => instance.exports.raw_array_id(instance.exports.struct_producer()),
  TypeError,
  'type incompatibility when transforming from/to JS');

// We can roundtrip an extern.
assertEquals(null, instance.exports.extern_id(instance.exports.extern_null()));

// We can roundtrip null typed as one of the three null types though wasm.
for (const nullType of ["none", "nofunc", "noextern"]) {
  instance.exports[`${nullType}_id`](instance.exports[`${nullType}_null`]());
}
