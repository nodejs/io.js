// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function DataViewPrototype() {
  const gsab = CreateGrowableSharedArrayBuffer(40, 80);
  const sab = new SharedArrayBuffer(80);

  const dvGsab = new DataView(gsab, 0, 3);
  const dvSab = new DataView(sab, 0, 3);
  assertEquals(dvGsab.__proto__, dvSab.__proto__);
})();

(function DataViewByteLength() {
  const gsab = CreateGrowableSharedArrayBuffer(40, 80);

  const dv = new DataView(gsab, 0, 3);
  assertEquals(gsab, dv.buffer);
  assertEquals(3, dv.byteLength);

  const emptyDv = new DataView(gsab, 0, 0);
  assertEquals(gsab, emptyDv.buffer);
  assertEquals(0, emptyDv.byteLength);

  const dvWithOffset = new DataView(gsab, 2, 3);
  assertEquals(gsab, dvWithOffset.buffer);
  assertEquals(3, dvWithOffset.byteLength);

  const emptyDvWithOffset = new DataView(gsab, 2, 0);
  assertEquals(gsab, emptyDvWithOffset.buffer);
  assertEquals(0, emptyDvWithOffset.byteLength);

  const lengthTracking = new DataView(gsab);
  assertEquals(gsab, lengthTracking.buffer);
  assertEquals(40, lengthTracking.byteLength);

  const offset = 8;
  const lengthTrackingWithOffset = new DataView(gsab, offset);
  assertEquals(gsab, lengthTrackingWithOffset.buffer);
  assertEquals(40 - offset, lengthTrackingWithOffset.byteLength);

  const emptyLengthTrackingWithOffset = new DataView(gsab, 40);
  assertEquals(gsab, emptyLengthTrackingWithOffset.buffer);
  assertEquals(0, emptyLengthTrackingWithOffset.byteLength);
})();

(function ConstructInvalid() {
  const gsab = CreateGrowableSharedArrayBuffer(40, 80);

  // Length too big.
  assertThrows(() => { new DataView(gsab, 0, 41); }, RangeError);

  // Offset too close to the end.
  assertThrows(() => { new DataView(gsab, 39, 2); }, RangeError);

  // Offset beyond end.
  assertThrows(() => { new DataView(gsab, 40, 1); }, RangeError);
})();

(function ConstructorParameterConversionGrows() {
  const gsab = CreateGrowableSharedArrayBuffer(40, 80);
  const evil = { valueOf: () => {
    gsab.grow(50);
    return 0;
  }};

  // Constructing will fail unless we take the new size into account.
  const dv = new DataView(gsab, evil, 50);
  assertEquals(50, dv.byteLength);
})();

(function GetAndSet() {
  const gsab = CreateGrowableSharedArrayBuffer(64, 128);
  const fixedLength = new DataView(gsab, 0, 32);
  const fixedLengthWithOffset = new DataView(gsab, 2, 32);
  const lengthTracking = new DataView(gsab, 0);
  const lengthTrackingWithOffset = new DataView(gsab, 2);

  testDataViewMethodsUpToSize(fixedLength, 32);
  assertAllDataViewMethodsThrow(fixedLength, 33, RangeError);

  testDataViewMethodsUpToSize(fixedLengthWithOffset, 32);
  assertAllDataViewMethodsThrow(fixedLengthWithOffset, 33, RangeError);

  testDataViewMethodsUpToSize(lengthTracking, 64);
  assertAllDataViewMethodsThrow(lengthTracking, 65, RangeError);

  testDataViewMethodsUpToSize(lengthTrackingWithOffset, 64 - 2);
  assertAllDataViewMethodsThrow(lengthTrackingWithOffset, 64 - 2 + 1,
                                RangeError);

  // Grow.
  gsab.grow(100);

  testDataViewMethodsUpToSize(fixedLength, 32);
  assertAllDataViewMethodsThrow(fixedLength, 33, RangeError);

  testDataViewMethodsUpToSize(fixedLengthWithOffset, 32);
  assertAllDataViewMethodsThrow(fixedLengthWithOffset, 33, RangeError);

  testDataViewMethodsUpToSize(lengthTracking, 100);
  assertAllDataViewMethodsThrow(lengthTracking, 101, RangeError);

  testDataViewMethodsUpToSize(lengthTrackingWithOffset, 100 - 2);
  assertAllDataViewMethodsThrow(lengthTrackingWithOffset, 100 - 2 + 1,
                                RangeError);
})();
