// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --no-always-turbofan --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/gc-js-interop-helpers.js');

let {struct, array} = CreateWasmObjects();
for (const wasm_obj of [struct, array]) {

  // Test constructors of the global object as function.
  testThrowsRepeated(() => AggregateError(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, Array(wasm_obj)[0]));
  testThrowsRepeated(() => ArrayBuffer(wasm_obj), TypeError);
  testThrowsRepeated(() => BigInt(wasm_obj), TypeError);
  testThrowsRepeated(() => BigInt64Array(wasm_obj), TypeError);
  testThrowsRepeated(() => BigUint64Array(wasm_obj), TypeError);
  repeated(() => assertEquals(true, Boolean(wasm_obj)));
  testThrowsRepeated(() => DataView(wasm_obj), TypeError);
  repeated(() => {
    let date = Date(wasm_obj);
    assertEquals('string', typeof date);
  });
  testThrowsRepeated(() => Error(wasm_obj), TypeError);
  testThrowsRepeated(() => EvalError(wasm_obj), TypeError);
  testThrowsRepeated(() => Float64Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Function(wasm_obj), TypeError);
  testThrowsRepeated(() => Int8Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Int16Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Int32Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Map(wasm_obj), TypeError);
  testThrowsRepeated(() => Number(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, Object(wasm_obj)));
  testThrowsRepeated(() => Promise(wasm_obj), TypeError);
  testThrowsRepeated(() => Proxy(wasm_obj), TypeError);
  testThrowsRepeated(() => RangeError(wasm_obj), TypeError);
  testThrowsRepeated(() => ReferenceError(wasm_obj), TypeError);
  testThrowsRepeated(() => RegExp(wasm_obj), TypeError);
  testThrowsRepeated(() => Set(wasm_obj), TypeError);
  testThrowsRepeated(() => SharedArrayBuffer(wasm_obj), TypeError);
  testThrowsRepeated(() => String(wasm_obj), TypeError);
  testThrowsRepeated(() => Symbol(wasm_obj), TypeError);
  testThrowsRepeated(() => SyntaxError(wasm_obj), TypeError);
  testThrowsRepeated(() => TypeError(wasm_obj), TypeError);
  testThrowsRepeated(() => Uint8Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Uint16Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Uint32Array(wasm_obj), TypeError);
  testThrowsRepeated(() => URIError(wasm_obj), TypeError);
  testThrowsRepeated(() => WeakMap(wasm_obj), TypeError);
  testThrowsRepeated(() => WeakRef(wasm_obj), TypeError);
  testThrowsRepeated(() => WeakSet(wasm_obj), TypeError);

  // Test constructors of the global object with new.
  testThrowsRepeated(() => new AggregateError(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, new Array(wasm_obj)[0]));
  testThrowsRepeated(() => new ArrayBuffer(wasm_obj), TypeError);
  testThrowsRepeated(() => new BigInt(wasm_obj), TypeError);
  repeated(() => assertEquals(new BigInt64Array(),
                              new BigInt64Array(wasm_obj)));
  testThrowsRepeated(() => new BigInt64Array([wasm_obj]), TypeError);
  repeated(() => assertEquals(new BigUint64Array(),
                              new BigUint64Array(wasm_obj)));
  testThrowsRepeated(() => new BigUint64Array([wasm_obj]), TypeError);
  repeated(() => assertEquals(true, (new Boolean(wasm_obj)).valueOf()));
  testThrowsRepeated(() => new DataView(wasm_obj), TypeError);
  testThrowsRepeated(() => new Date(wasm_obj), TypeError);
  testThrowsRepeated(() => new Error(wasm_obj), TypeError);
  testThrowsRepeated(() => new EvalError(wasm_obj), TypeError);
  repeated(() => assertEquals(new Float64Array(),
                              new Float64Array(wasm_obj)));
  testThrowsRepeated(() => new Float64Array([wasm_obj]), TypeError);
  testThrowsRepeated(() => new Function(wasm_obj), TypeError);
  repeated(() => assertEquals(new Int8Array(),
                              new Int8Array(wasm_obj)));
  testThrowsRepeated(() => new Int8Array([wasm_obj]), TypeError);
  repeated(() => assertEquals(new Int16Array(),
                              new Int16Array(wasm_obj)));
  testThrowsRepeated(() => new Int16Array([wasm_obj]), TypeError);
  repeated(() => assertEquals(new Int32Array(),
                              new Int32Array(wasm_obj)));
  testThrowsRepeated(() => new Int32Array([wasm_obj]), TypeError);
  testThrowsRepeated(() => new Map(wasm_obj), TypeError);
  testThrowsRepeated(() => new Number(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, new Object(wasm_obj)));
  testThrowsRepeated(() => new Promise(wasm_obj), TypeError);
  testThrowsRepeated(() => new Proxy(wasm_obj), TypeError);
  testThrowsRepeated(() => new RangeError(wasm_obj), TypeError);
  testThrowsRepeated(() => new ReferenceError(wasm_obj), TypeError);
  testThrowsRepeated(() => new RegExp(wasm_obj), TypeError);
  testThrowsRepeated(() => new Set(wasm_obj), TypeError);
  testThrowsRepeated(() => new SharedArrayBuffer(wasm_obj), TypeError);
  testThrowsRepeated(() => new String(wasm_obj), TypeError);
  testThrowsRepeated(() => new Symbol(wasm_obj), TypeError);
  testThrowsRepeated(() => new SyntaxError(wasm_obj), TypeError);
  testThrowsRepeated(() => new TypeError(wasm_obj), TypeError);
  repeated(() => assertEquals(new Uint8Array(),
                              new Uint8Array(wasm_obj)));
  testThrowsRepeated(() => new Uint8Array([wasm_obj]), TypeError);
  repeated(() => assertEquals(new Uint16Array(),
                              new Uint16Array(wasm_obj)));
  testThrowsRepeated(() => new Uint16Array([wasm_obj]), TypeError);
  repeated(() => assertEquals(new Uint32Array(),
                              new Uint32Array(wasm_obj)));
  testThrowsRepeated(() => new URIError(wasm_obj), TypeError);
  testThrowsRepeated(() => new WeakMap(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, new WeakRef(wasm_obj).deref()));
  testThrowsRepeated(() => new WeakSet(wasm_obj), TypeError);

  // Ensure no statement re-assigned wasm_obj by accident.
  assertTrue(wasm_obj == struct || wasm_obj == array);
}
