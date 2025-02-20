// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

const notSharedErrorMessage =
    'TypeError: [object Object] is not a shared typed array.';

(function AtomicsWait() {
  // Test that trying to wait on a non-shared ArrayBuffer fails, even
  // when done on a worker thread.
  const workerScript = function() {
    onmessage = function({data:msg}) {
      const rab = new ArrayBuffer(100, {maxByteLength: 200});
      const i32a = new Int32Array(rab, 0);
      try {
        Atomics.wait(i32a, 0, 0, 5000);
        postMessage('Didn\'t get an error');
      } catch (e) {
        postMessage('Got error: ' + e);
      }
    };
  }

  const worker = new Worker(workerScript, {type: 'function'});
  worker.postMessage('start');
  assertEquals('Got error: ' + notSharedErrorMessage, worker.getMessage());
  worker.terminate();
})();

(function AtomicsWaitAsync() {
  // Test that trying to waitAsync on a non-shared ArrayBuffer fails.
  for (let ctor of [Int32Array, BigInt64Array, MyBigInt64Array]) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab, 0);

    const initialValue = false;  // Can be converted to both Number and BigInt.
    assertThrows(() => { Atomics.waitAsync(lengthTracking, 0, initialValue); },
                 TypeError);
  }
})();

(function AtomicsWaitFailWithWrongArrayTypes() {
  const rab = CreateResizableArrayBuffer(400, 800);

  const i8a = new Int8Array(rab);
  const i16a = new Int16Array(rab);
  const ui8a = new Uint8Array(rab);
  const ui8ca = new Uint8ClampedArray(rab);
  const ui16a = new Uint16Array(rab);
  const ui32a = new Uint32Array(rab);
  const f32a = new Float32Array(rab);
  const f64a = new Float64Array(rab);
  const myui8 = new MyUint8Array(rab);
  const bui64 = new BigUint64Array(rab);

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
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    TestAtomicsOperations(fixedLength, 0);
    TestAtomicsOperations(fixedLengthWithOffset, 0);
    TestAtomicsOperations(lengthTracking, 0);
    TestAtomicsOperations(lengthTrackingWithOffset, 0);

    AssertAtomicsOperationsThrow(fixedLength, 4, RangeError);
    AssertAtomicsOperationsThrow(fixedLengthWithOffset, 2, RangeError);
    AssertAtomicsOperationsThrow(lengthTracking, 4, RangeError);
    AssertAtomicsOperationsThrow(lengthTrackingWithOffset, 2, RangeError);

    // Shrink so that fixed length TAs go out of bounds.
    rab.resize(3 * ctor.BYTES_PER_ELEMENT);

    AssertAtomicsOperationsThrow(fixedLength, 0, TypeError);
    AssertAtomicsOperationsThrow(fixedLengthWithOffset, 0, TypeError);
    TestAtomicsOperations(lengthTracking, 0);
    TestAtomicsOperations(lengthTrackingWithOffset, 0);

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * ctor.BYTES_PER_ELEMENT);

    AssertAtomicsOperationsThrow(fixedLength, 0, TypeError);
    AssertAtomicsOperationsThrow(fixedLengthWithOffset, 0, TypeError);
    AssertAtomicsOperationsThrow(lengthTrackingWithOffset, 0, TypeError);
    TestAtomicsOperations(lengthTracking, 0);

    // Shrink to zero.
    rab.resize(0);

    AssertAtomicsOperationsThrow(fixedLength, 0, TypeError);
    AssertAtomicsOperationsThrow(fixedLengthWithOffset, 0, TypeError);
    AssertAtomicsOperationsThrow(lengthTracking, 0, RangeError);
    AssertAtomicsOperationsThrow(lengthTrackingWithOffset, 0, TypeError);

    // Grow so that all TAs are back in-bounds.
    rab.resize(6 * ctor.BYTES_PER_ELEMENT);

    TestAtomicsOperations(fixedLength, 0);
    TestAtomicsOperations(fixedLengthWithOffset, 0);
    TestAtomicsOperations(lengthTracking, 0);
    TestAtomicsOperations(lengthTrackingWithOffset, 0);

    AssertAtomicsOperationsThrow(fixedLength, 4, RangeError);
    AssertAtomicsOperationsThrow(fixedLengthWithOffset, 2, RangeError);

    TestAtomicsOperations(lengthTracking, 5);
    TestAtomicsOperations(lengthTrackingWithOffset, 3);
  }
})();

(function AtomicsFailWithNonIntegerArray() {
  const rab = CreateResizableArrayBuffer(400, 800);

  const ui8ca = new Uint8ClampedArray(rab);
  const f32a = new Float32Array(rab);
  const f64a = new Float64Array(rab);
  const mf32a = new MyFloat32Array(rab);

  [ui8ca, f32a, f64a, mf32a].forEach((ta) => {
      AssertAtomicsOperationsThrow(ta, 0, TypeError); });
})();

const oneParameterFuncs = [(ta, index) => { Atomics.load(ta, index); }];

const twoParameterFuncs = [
    (ta, index, value) => { Atomics.store(ta, index, value); },
    (ta, index, value) => { Atomics.add(ta, index, value); },
    (ta, index, value) => { Atomics.sub(ta, index, value); },
    (ta, index, value) => { Atomics.and(ta, index, value); },
    (ta, index, value) => { Atomics.or(ta, index, value); },
    (ta, index, value) => { Atomics.xor(ta, index, value); },
    (ta, index, value) => { Atomics.exchange(ta, index, value); },
  ];

const threeParameterFuncs = [
    (ta, index, value1, value2) => {
        Atomics.compareExchange(ta, index, value1, value2); }
  ];

(function TestAtomicsParameterConversionShrinks() {
  let rab;
  let resizeTo;
  const evilIndex = { valueOf: () => { rab.resize(resizeTo); return 2; }};
  // false can be converted both to Number and BigInt.
  const evilValue = { valueOf: () => { rab.resize(resizeTo); return false; }};

  for (let func of oneParameterFuncs) {
    // Fixed-length TA + first parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const fixedLength = new ctor(rab, 0, 4);

      assertThrows(() => { func(fixedLength, evilIndex); }, TypeError);
    }
    // Length tracking TA + first parameter resizes.
    for (let ctor of intCtors) {
        rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                         8 * ctor.BYTES_PER_ELEMENT);
        resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
        const lengthTracking = new ctor(rab);

        assertThrows(() => { func(lengthTracking, evilIndex); }, TypeError);
    }
  }

  for (let func of twoParameterFuncs) {
    // Fixed-length TA + first parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const fixedLength = new ctor(rab, 0, 4);
      const one = IsBigIntTypedArray(fixedLength) ? 1n : 1;

      assertThrows(() => { func(fixedLength, evilIndex, one); }, TypeError);
    }
    // Fixed-length TA + second parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const fixedLength = new ctor(rab, 0, 4);

      assertThrows(() => { func(fixedLength, 0, evilValue); }, TypeError);
    }
    // Length tracking TA + first parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const lengthTracking = new ctor(rab);
      const one = IsBigIntTypedArray(lengthTracking) ? 1n : 1;

      assertThrows(() => { func(lengthTracking, evilIndex, one); }, TypeError);
    }
    // Length tracking TA + second parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const lengthTracking = new ctor(rab);

      assertThrows(() => { func(lengthTracking, 2, evilValue); }, TypeError);
    }
  }

  for (let func of threeParameterFuncs) {
    // Fixed-length TA + first parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const fixedLength = new ctor(rab, 0, 4);
      const one = IsBigIntTypedArray(fixedLength) ? 1n : 1;

      assertThrows(() => { func(fixedLength, evilIndex, one, one); },
                   TypeError);
    }
    // Fixed-length TA + second parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const fixedLength = new ctor(rab, 0, 4);
      const one = IsBigIntTypedArray(fixedLength) ? 1n : 1;

      assertThrows(() => { func(fixedLength, 0, evilValue, one); },
                   TypeError);
    }
    // Fixed-length TA + third parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const fixedLength = new ctor(rab, 0, 4);
      const one = IsBigIntTypedArray(fixedLength) ? 1n : 1;

      assertThrows(() => { func(fixedLength, 0, one, evilValue); },
                   TypeError);
    }
    // Length tracking TA + first parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const lengthTracking = new ctor(rab);
      const one = IsBigIntTypedArray(lengthTracking) ? 1n : 1;

      assertThrows(() => { func(lengthTracking, evilIndex, one, one); },
                   TypeError);
    }
    // Length tracking TA + second parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const lengthTracking = new ctor(rab);
      const one = IsBigIntTypedArray(lengthTracking) ? 1n : 1;

      assertThrows(() => { func(lengthTracking, 2, evilValue, one); },
                   TypeError);
    }
    // Length tracking TA + third parameter resizes.
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
      const lengthTracking = new ctor(rab);
      const one = IsBigIntTypedArray(lengthTracking) ? 1n : 1;

      assertThrows(() => { func(lengthTracking, 2, one, evilValue); },
                   TypeError);
    }
  }
})();

(function TestAtomicsParameterConversionDetaches() {
  let rab;
  const evilIndex = { valueOf: () => { %ArrayBufferDetach(rab); return 0; }};
  // false can be converted both to Number and BigInt.
  const evilValue = {
      valueOf: () => { %ArrayBufferDetach(rab); return false; }};

  for (let func of oneParameterFuncs) {
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      const fixedLength = new ctor(rab, 0, 4);

      assertThrows(() => { func(fixedLength, evilIndex); }, TypeError);
    }
  }

  for (let func of twoParameterFuncs) {
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      const fixedLength = new ctor(rab, 0, 4);
      const one = IsBigIntTypedArray(fixedLength) ? 1n : 1;

      assertThrows(() => { func(fixedLength, evilIndex, one); }, TypeError);
    }
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      const fixedLength = new ctor(rab, 0, 4);

      assertThrows(() => { func(fixedLength, 0, evilValue); }, TypeError);
    }
  }

  for (let func of threeParameterFuncs) {
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      const fixedLength = new ctor(rab, 0, 4);
      const one = IsBigIntTypedArray(fixedLength) ? 1n : 1;

      assertThrows(
          () => { func(fixedLength, evilIndex, one, one); },
          TypeError);
    }
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      const fixedLength = new ctor(rab, 0, 4);
      const one = IsBigIntTypedArray(fixedLength) ? 1n : 1;

      assertThrows(
          () => { func(fixedLength, 0, evilValue, one); },
          TypeError);
    }
    for (let ctor of intCtors) {
      rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                       8 * ctor.BYTES_PER_ELEMENT);
      const fixedLength = new ctor(rab, 0, 4);
      const one = IsBigIntTypedArray(fixedLength) ? 1n : 1;

      assertThrows(
          () => { Atomics.compareExchange(fixedLength, 0, one, evilValue); },
          TypeError);
    }
  }
})();
