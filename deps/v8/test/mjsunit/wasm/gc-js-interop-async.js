// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --no-always-turbofan --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/gc-js-interop-helpers.js');

let {struct, array} = CreateWasmObjects();
for (const wasm_obj of [struct, array]) {

  repeated(() => assertThrowsAsync(Promise.all(wasm_obj), TypeError));
  repeated(() => Promise.all([wasm_obj]));
  repeated(() => assertThrowsAsync(Promise.allSettled(wasm_obj), TypeError));
  repeated(
      () => Promise.allSettled([wasm_obj])
                .then((info) => assertEquals('fulfilled', info[0].status)));
  repeated(() => assertThrowsAsync(Promise.any(wasm_obj), TypeError));
  repeated(() => Promise.any([wasm_obj]));
  repeated(() => assertThrowsAsync(Promise.race(wasm_obj), TypeError));
  repeated(() => Promise.race([wasm_obj]));
  // Using wasm objects in Promise.resolve and Promise.reject should work as
  // for any other object.
  repeated(
      () => (new Promise((resolve, reject) => resolve(wasm_obj)))
                .then((v) => assertSame(wasm_obj, v)));
  repeated(
      () => (new Promise((resolve, reject) => reject(wasm_obj)))
                .then(() => assertUnreachable())
                .catch((v) => assertSame(wasm_obj, v)));
  // Wasm objects can also be passed as a result in a then chain.
  repeated(
      () => (new Promise((resolve) => resolve({})))
                .then(() => wasm_obj)
                .then((v) => assertSame(wasm_obj, v)));
  // If the `then` argument isn't a callback, it will simply be replaced with
  // an identity function (x) => x.
  repeated(
      () => (new Promise((resolve) => resolve({})))
                .then(wasm_obj)  // The value itself doesn't have any impact.
                .then((v) => assertEquals({}, v), () => assertUnreachable()));
  // If the `catch` argument isn't a callback, it will be replaced with a
  // thrower function (x) => { throw x; }.
  repeated(
      () => (new Promise((resolve, reject) => reject({})))
                .then(() => null)
                .catch(wasm_obj)  // The value itself doesn't have any impact.
                .then(() => assertUnreachable(), (v) => assertEquals({}, v)));
  // `finally(wasm_obj)` behaves just like `then(wasm_obj, wasm_obj)`
  repeated(
      () => (new Promise((resolve, reject) => resolve({})))
                .finally(wasm_obj)
                .then((v) => assertEquals({}, v), () => assertUnreachable()));
  repeated(
      () => (new Promise((resolve, reject) => reject({})))
                .finally(wasm_obj)
                .then(() => assertUnreachable(), (v) => assertEquals({}, v)));

  // Ensure no statement re-assigned wasm_obj by accident.
  assertTrue(wasm_obj == struct || wasm_obj == array);
}

repeated(async function testAsync() {
  for (let wasm_obj of [struct, array]) {
    let async_wasm_obj = await wasm_obj;
    assertSame(wasm_obj, async_wasm_obj);
  }
});
