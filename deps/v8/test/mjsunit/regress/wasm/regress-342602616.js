// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-wasm-lazy-compilation
// Flags: --no-wasm-loop-peeling --no-wasm-loop-unrolling

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig0 = builder.addType(kSig_v_v);
builder.startRecGroup();
let $array1 = builder.addArray(wasmRefType(6), true, kNoSuperType, true);
let $array2 = builder.addArray(wasmRefNullType(9), false, kNoSuperType, false);
let $sig3 = builder.addType(makeSig([], [kWasmF64]));
let $struct4 = builder.addStruct([makeField(kWasmI64, false), makeField(kWasmI8, true)], kNoSuperType, true);
let $sig5 = builder.addType(makeSig([wasmRefType(9)], [wasmRefType($array2)]), kNoSuperType, false);
let $sig6 = builder.addType(kSig_i_v, kNoSuperType, false);
let $struct7 = builder.addStruct([makeField(kWasmF64, true), makeField(kWasmEqRef, false), makeField(wasmRefType($sig6), false), makeField(kWasmI8, false)], kNoSuperType, false);
let $sig8 = builder.addType(makeSig([wasmRefType(9)], [kWasmF32]));
let $sig10 = builder.addType(makeSig([wasmRefNullType($array1), kWasmI32, wasmRefType(9)], []), kNoSuperType, false);
let $struct15 = builder.addStruct([makeField(kWasmI8, false), makeField(kWasmI16, true), makeField(kWasmI32, true), makeField(kWasmI64, true), makeField(kWasmI16, true)], kNoSuperType, false);
builder.endRecGroup();
let $sig175 = builder.addType(makeSig([kWasmStructRef, kWasmI32], [kWasmFuncRef]));
let $func322 = builder.addFunction("f322", $sig175).exportFunc();
let $mem0 = builder.addMemory(16, 17, true);
let $global14 = builder.addGlobal(kWasmI32, false, false, wasmI32Const(-2078671));

// func $func322: [kWasmStructRef, kWasmI32] -> [kWasmFuncRef]
$func322.addLocals(kWasmI32, 1)
  .addLocals(wasmRefType($struct15), 1)
  .addBody([
    kGCPrefix, kExprStructNewDefault, $struct15,
    kExprLocalSet, 3,
    kExprBlock, kFuncRefCode,
      kExprBlock, kWasmVoid,
        kExprLoop, kWasmRefNull, kI31RefCode,
          kExprGlobalGet, $global14.index,
          ...wasmF64Const(524287.133),
          ...wasmF64Const(-120),
          kExprF64CopySign,
          kExprF64StoreMem, 3, 22,
          kExprLocalGet, 3,
          kGCPrefix, kExprStructGetS, $struct15, 4,
          kExprLocalTee, 2,  // $var2
          kExprBrIf, 1,
          kExprUnreachable,
        kExprEnd,
        kExprDrop,
      kExprEnd,
      kExprLocalGet, 2,  // $var2
      kExprDrop,
      kExprLoop, kWasmVoid,
        kExprBlock, kWasmVoid,
          ...wasmF64Const(0),
          kExprLoop, kWasmF64,
            kExprBlock, kWasmF64,
              ...wasmI32Const(-2147483648),
              kExprLocalGet, 1,  // $var1
              kExprLocalGet, 2,  // $var2
              kExprLocalTee, 1,  // $var1
              ...wasmI64Const(-108n),
              ...wasmI32Const(-32767),
              kGCPrefix, kExprStructNew, $struct15,
              kExprI32Const, 0,
              kExprIf, kWasmVoid,
                kExprBr, 4,
              kExprEnd,
              kExprDrop,
              ...wasmF64Const(-144115188075855870),
            kExprEnd,
          kExprEnd,
          kExprUnreachable,
        kExprEnd,
      kExprEnd,
      kExprUnreachable,
    kExprEnd,
  ]);

const instance = builder.instantiate({});
