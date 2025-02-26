// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function ConstructorThrowsIfBufferDetached() {
  const rab = CreateResizableArrayBuffer(40, 80);
  %ArrayBufferDetach(rab);

  assertThrows(() => { new DataView(rab); }, TypeError);
})();

(function TypedArrayLengthAndByteLength() {
  const rab = CreateResizableArrayBuffer(40, 80);

  let dvs = [];
  dvs.push(new DataView(rab, 0, 3));
  dvs.push(new DataView(rab, 8, 3));
  dvs.push(new DataView(rab));
  dvs.push(new DataView(rab, 8));

  %ArrayBufferDetach(rab);

  for (let dv of dvs) {
    assertThrows(() => { dv.byteLength });
  }
})();

(function AccessDetachedDataView() {
  const rab = CreateResizableArrayBuffer(64, 128);

  const fixedLength = new DataView(rab, 0, 32);
  const fixedLengthWithOffset = new DataView(rab, 2, 32);
  const lengthTracking = new DataView(rab, 0);
  const lengthTrackingWithOffset = new DataView(rab, 2);

  testDataViewMethodsUpToSize(fixedLength, 32);
  assertAllDataViewMethodsThrow(fixedLength, 33, RangeError);

  testDataViewMethodsUpToSize(fixedLengthWithOffset, 32);
  assertAllDataViewMethodsThrow(fixedLengthWithOffset, 33, RangeError);

  testDataViewMethodsUpToSize(lengthTracking, 64);
  assertAllDataViewMethodsThrow(lengthTracking, 65, RangeError);

  testDataViewMethodsUpToSize(lengthTrackingWithOffset, 64 - 2);
  assertAllDataViewMethodsThrow(lengthTrackingWithOffset, 64 - 2 + 1,
                                RangeError);

  %ArrayBufferDetach(rab);

  assertAllDataViewMethodsThrow(fixedLength, 0, TypeError);
  assertAllDataViewMethodsThrow(fixedLengthWithOffset, 0, TypeError);
  assertAllDataViewMethodsThrow(lengthTracking, 0, TypeError);
  assertAllDataViewMethodsThrow(lengthTrackingWithOffset, 0, TypeError);
})();

(function GetParameterConversionDetaches() {
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLength = new DataView(rab, 0, 64);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLength.getUint8(evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLengthWithOffset = new DataView(rab, 2, 64);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLengthWithOffset.getUint8(evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTracking = new DataView(rab);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { lengthTracking.getUint8(evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTrackingWithOffset = new DataView(rab, 2);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { lengthTrackingWithOffset.getUint8(evil); },
                 TypeError);
  }
})();

(function SetParameterConversionDetaches() {
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLength = new DataView(rab, 0, 64);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLength.setUint8(evil, 0); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLengthWithOffset = new DataView(rab, 2, 64);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLengthWithOffset.setUint8(evil, 0); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTracking = new DataView(rab);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { lengthTracking.setUint8(evil, 0); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTrackingWithOffset = new DataView(rab, 2);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { lengthTrackingWithOffset.setUint8(evil, 0); },
                 TypeError);
  }

  // The same tests as before, except now the "detaching" parameter is the
  // second one, not the first one.
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLength = new DataView(rab, 0, 64);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLength.setUint8(0, evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLengthWithOffset = new DataView(rab, 2, 64);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLengthWithOffset.setUint8(0, evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTracking = new DataView(rab);

    const evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { lengthTracking.setUint8(0, evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTrackingWithOffset = new DataView(rab, 2);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { lengthTrackingWithOffset.setUint8(0, evil); },
                 TypeError);
  }
})();
