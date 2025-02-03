// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-float16array

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function AtomicsWait() {
  const gsab = CreateGrowableSharedArrayBuffer(16, 32);
  const i32a = new Int32Array(gsab);

  const workerScript = function() {
      onmessage = function({data:msg}) {
        const i32a = new Int32Array(msg.gsab, msg.offset);
        const result = Atomics.wait(i32a, 0, 0, 5000);
        postMessage(result);
      };
  }

  const worker = new Worker(workerScript, {type: 'function'});
  worker.postMessage({gsab: gsab, offset: 0});

  // Spin until the worker is waiting on the futex.
  while (%AtomicsNumWaitersForTesting(i32a, 0) != 1) {}

  Atomics.notify(i32a, 0, 1);
  assertEquals("ok", worker.getMessage());
  worker.terminate();
})();

(function AtomicsWaitAfterGrowing() {
  // Test that Atomics.wait works on indices that are valid only after growing.
  const gsab = CreateGrowableSharedArrayBuffer(4 * 4, 8 * 4);
  const i32a = new Int32Array(gsab);
  gsab.grow(6 * 4);
  const index = 5;

  const workerScript = function() {
      onmessage = function({data:msg}) {
        const i32a = new Int32Array(msg.gsab, msg.offset);
        const result = Atomics.wait(i32a, 5, 0, 5000);
        postMessage(result);
      };
  }

  const worker = new Worker(workerScript, {type: 'function'});
  worker.postMessage({gsab: gsab, offset: 0});

  // Spin until the worker is waiting on the futex.
  while (%AtomicsNumWaitersForTesting(i32a, index) != 1) {}

  Atomics.notify(i32a, index, 1);
  assertEquals("ok", worker.getMessage());
  worker.terminate();
})();

(function AtomicsWaitAsync() {
  for (let ctor of [Int32Array, BigInt64Array, MyBigInt64Array]) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab, 0);

    const initialValue = false;  // Can be converted to both Number and BigInt.
    const result = Atomics.waitAsync(lengthTracking, 0, initialValue);

    let wokeUp = false;
    result.value.then(
     (value) => { assertEquals("ok", value); wokeUp = true; },
     () => { assertUnreachable(); });

    assertEquals(true, result.async);

    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);

    const notifyReturnValue = Atomics.notify(lengthTracking, 0, 1);
    assertEquals(1, notifyReturnValue);

    function continuation() {
      assertTrue(wokeUp);
    }

    setTimeout(continuation, 0);
  }
})();

(function AtomicsWaitAsyncAfterGrowing() {
  for (let ctor of [Int32Array, BigInt64Array, MyBigInt64Array]) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab, 0);
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    const index = 5;

    const initialValue = false;  // Can be converted to both Number and BigInt.
    const result = Atomics.waitAsync(lengthTracking, index, initialValue);

    let wokeUp = false;
    result.value.then(
     (value) => { assertEquals("ok", value); wokeUp = true; },
     () => { assertUnreachable(); });

    assertEquals(true, result.async);

    const notifyReturnValue = Atomics.notify(lengthTracking, index, 1);
    assertEquals(1, notifyReturnValue);

    function continuation() {
      assertTrue(wokeUp);
    }

    setTimeout(continuation, 0);
  }
})();

(function AtomicsWaitFailWithWrongArrayTypes() {
  const gsab = CreateGrowableSharedArrayBuffer(400, 800);

  const i8a = new Int8Array(gsab);
  const i16a = new Int16Array(gsab);
  const ui8a = new Uint8Array(gsab);
  const ui8ca = new Uint8ClampedArray(gsab);
  const ui16a = new Uint16Array(gsab);
  const ui32a = new Uint32Array(gsab);
  const f32a = new Float32Array(gsab);
  const f64a = new Float64Array(gsab);
  const myui8 = new MyUint8Array(gsab);
  const bui64 = new BigUint64Array(gsab);

  [i8a, i16a, ui8a, ui8ca, ui16a, ui32a, f32a, f64a, myui8, bui64].forEach(
      function(ta) {
        // Can be converted both to Number and BigInt.
        const exampleValue = false;
        assertThrows(() => { Atomics.wait(ta, 0, exampleValue); },
                     TypeError);
        assertThrows(() => { Atomics.notify(ta, 0, 1); },
                     TypeError);
        assertThrows(() => { Atomics.waitAsync(ta, 0, exampleValue); },
                     TypeError);
      });
})();

(function TestAtomics() {
  for (let ctor of intCtors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab, 0);
    TestAtomicsOperations(lengthTracking, 0);

    AssertAtomicsOperationsThrow(lengthTracking, 4, RangeError);
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    TestAtomicsOperations(lengthTracking, 4);
  }
})();

(function AtomicsFailWithNonIntegerArray() {
  const gsab = CreateGrowableSharedArrayBuffer(400, 800);

  const ui8ca = new Uint8ClampedArray(gsab);
  const f32a = new Float32Array(gsab);
  const f64a = new Float64Array(gsab);
  const mf32a = new MyFloat32Array(gsab);

  [ui8ca, f32a, f64a, mf32a].forEach((ta) => {
      AssertAtomicsOperationsThrow(ta, 0, TypeError); });
})();
