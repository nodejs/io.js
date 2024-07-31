// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --no-always-turbofan --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/gc-js-interop-helpers.js');

let {struct, array} = CreateWasmObjects();
for (const wasm_obj of [struct, array]) {

  // Test Array.
  repeated(() => assertEquals([], Array.from(wasm_obj)));
  repeated(() => assertFalse(Array.isArray(wasm_obj)));
  repeated(() => assertEquals([wasm_obj], Array.of(wasm_obj)));
  testThrowsRepeated(() => [1, 2].at(wasm_obj), TypeError);
  repeated(() => assertEquals([1, wasm_obj], [1].concat(wasm_obj)));
  testThrowsRepeated(() => [1, 2].copyWithin(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].every(wasm_obj), TypeError);
  repeated(
      () => assertEquals([1, wasm_obj, 3], [1, 2, 3].fill(wasm_obj, 1, 2)));
  testThrowsRepeated(() => [1, 2].filter(wasm_obj), TypeError);
  repeated(
      () => assertEquals(
          [wasm_obj], [undefined, wasm_obj, null].filter(function(v) {
            return v == this;
          }, wasm_obj)));
  testThrowsRepeated(() => [1, 2].find(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].findIndex(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].findLast(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].findLastIndex(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].flat(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].flatMap(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].forEach(wasm_obj), TypeError);
  repeated(() => {
    let res = [];
    [1, 2].forEach(function(x) {
      res.push(this);
    }, wasm_obj);
    assertEquals([wasm_obj, wasm_obj], res);
  });
  repeated(() => assertTrue([wasm_obj].includes(wasm_obj)));
  repeated(() => assertFalse([1].includes(wasm_obj)));
  repeated(() => assertEquals(1, [0, wasm_obj, 2].indexOf(wasm_obj)));
  testThrowsRepeated(() => ['a', 'b'].join(wasm_obj), TypeError);
  repeated(() => assertEquals(1, [0, wasm_obj, 2].lastIndexOf(wasm_obj)));
  testThrowsRepeated(() => [1, 2].map(wasm_obj), TypeError);
  repeated(() => assertEquals([wasm_obj, wasm_obj], [1, 2].map(function() {
             return this;
           }, wasm_obj)));
  repeated(() => {
    let arr = [1];
    arr.push(wasm_obj, 3);
    assertEquals([1, wasm_obj, 3], arr);
  });
  testThrowsRepeated(() => [1, 2].reduce(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, [].reduce(() => null, wasm_obj)));
  testThrowsRepeated(() => [1, 2].reduceRight(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].slice(wasm_obj, 2), TypeError);
  testThrowsRepeated(() => [1, 2].some(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].sort(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].splice(1, wasm_obj), TypeError);
  repeated(() => {
    let arr = [1, 2];
    arr.unshift(wasm_obj);
    assertEquals([wasm_obj, 1, 2], arr);
  });
  repeated(() => assertEquals(Int8Array.from([]), Int8Array.from(wasm_obj)));
  testThrowsRepeated(() => Int8Array.of(wasm_obj), TypeError);
  for (let ArrayType
           of [Int8Array, Int16Array, Int32Array, Uint8Array, Uint16Array,
               Uint32Array]) {
    let array = ArrayType.of(1, 2, 3);
    testThrowsRepeated(() => array.at(wasm_obj), TypeError);
    testThrowsRepeated(() => array.copyWithin(wasm_obj), TypeError);
    testThrowsRepeated(() => array.fill(wasm_obj, 0, 1), TypeError);
    testThrowsRepeated(() => array.filter(wasm_obj), TypeError);
    testThrowsRepeated(() => array.find(wasm_obj), TypeError);
    testThrowsRepeated(() => array.findIndex(wasm_obj), TypeError);
    testThrowsRepeated(() => array.findLast(wasm_obj), TypeError);
    testThrowsRepeated(() => array.findLastIndex(wasm_obj), TypeError);
    testThrowsRepeated(() => array.forEach(wasm_obj), TypeError);
    repeated(() => assertFalse(array.includes(wasm_obj)));
    repeated(() => assertEquals(-1, array.indexOf(wasm_obj)));
    testThrowsRepeated(() => array.join(wasm_obj), TypeError);
    repeated(() => assertEquals(-1, array.lastIndexOf(wasm_obj)));
    testThrowsRepeated(() => array.map(wasm_obj), TypeError);
    testThrowsRepeated(() => array.map(() => wasm_obj), TypeError);
    testThrowsRepeated(() => array.reduce(wasm_obj), TypeError);
    testThrowsRepeated(() => array.reduceRight(wasm_obj), TypeError);
    repeated(() => array.set(wasm_obj));
    testThrowsRepeated(() => array.set([wasm_obj]), TypeError);
    testThrowsRepeated(() => array.slice(wasm_obj, 1), TypeError);
    testThrowsRepeated(() => array.some(wasm_obj), TypeError);
    testThrowsRepeated(() => array.sort(wasm_obj), TypeError);
    testThrowsRepeated(() => array.subarray(0, wasm_obj), TypeError);
  }

  // Test Map.
  for (let MapType of [Map, WeakMap]) {
    repeated(() => {
      let val = new String('a');
      let map = new MapType([[val, wasm_obj], [wasm_obj, val]]);
      assertSame(wasm_obj, map.get(val));
      assertEquals(val, map.get(wasm_obj));
      assertTrue(map.has(wasm_obj));
      map.delete(wasm_obj);
      assertFalse(map.has(wasm_obj));
      assertThrows(() => map.forEach(wasm_obj), TypeError);
      map.set(wasm_obj, wasm_obj);
      assertSame(wasm_obj, map.get(wasm_obj));
    });
  }

  // Test Set.
  for (let SetType of [Set, WeakSet]) {
    repeated(() => {
      let set = new SetType([new String('a'), wasm_obj]);
      set.add(wasm_obj);
      assertTrue(set.has(wasm_obj));
      set.delete(wasm_obj);
      assertFalse(set.has(wasm_obj));
    });
  }

  // Test ArrayBuffer.
  repeated(() => assertFalse(ArrayBuffer.isView(wasm_obj)));
  testThrowsRepeated(
      () => (new ArrayBuffer(32)).slice(wasm_obj, wasm_obj), TypeError);
  testThrowsRepeated(
      () => (new SharedArrayBuffer(32)).slice(wasm_obj, wasm_obj), TypeError);

  // Test Dataview.
  let arrayBuf = new ArrayBuffer(32);
  let dataView = new DataView(arrayBuf);
  testThrowsRepeated(() => dataView.getBigInt64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getBigUint64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getFloat32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getFloat64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getInt8(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getInt16(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getInt32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getUint8(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getUint16(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getUint32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setBigInt64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setBigUint64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setFloat32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setFloat64(0, wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setInt8(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setInt16(0, wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setInt32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setUint8(0, wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setUint16(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setUint32(0, wasm_obj), TypeError);

  // Ensure no statement re-assigned wasm_obj by accident.
  assertTrue(wasm_obj == struct || wasm_obj == array);
}
