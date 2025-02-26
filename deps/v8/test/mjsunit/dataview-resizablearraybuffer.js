// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function DataViewPrototype() {
  const rab = CreateResizableArrayBuffer(40, 80);
  const ab = new ArrayBuffer(80);

  const dvRab = new DataView(rab, 0, 3);
  const dvAb = new DataView(ab, 0, 3);
  assertEquals(dvRab.__proto__, dvAb.__proto__);
})();

(function DataViewByteLength() {
  const rab = CreateResizableArrayBuffer(40, 80);

  const dv = new DataView(rab, 0, 3);
  assertEquals(rab, dv.buffer);
  assertEquals(3, dv.byteLength);

  const emptyDv = new DataView(rab, 0, 0);
  assertEquals(rab, emptyDv.buffer);
  assertEquals(0, emptyDv.byteLength);

  const dvWithOffset = new DataView(rab, 2, 3);
  assertEquals(rab, dvWithOffset.buffer);
  assertEquals(3, dvWithOffset.byteLength);

  const emptyDvWithOffset = new DataView(rab, 2, 0);
  assertEquals(rab, emptyDvWithOffset.buffer);
  assertEquals(0, emptyDvWithOffset.byteLength);

  const lengthTracking = new DataView(rab);
  assertEquals(rab, lengthTracking.buffer);
  assertEquals(40, lengthTracking.byteLength);

  const offset = 8;
  const lengthTrackingWithOffset = new DataView(rab, offset);
  assertEquals(rab, lengthTrackingWithOffset.buffer);
  assertEquals(40 - offset, lengthTrackingWithOffset.byteLength);

  const emptyLengthTrackingWithOffset = new DataView(rab, 40);
  assertEquals(rab, emptyLengthTrackingWithOffset.buffer);
  assertEquals(0, emptyLengthTrackingWithOffset.byteLength);
})();

(function ConstructInvalid() {
  const rab = CreateResizableArrayBuffer(40, 80);

  // Length too big.
  assertThrows(() => { new DataView(rab, 0, 41); }, RangeError);

  // Offset too close to the end.
  assertThrows(() => { new DataView(rab, 39, 2); }, RangeError);

  // Offset beyond end.
  assertThrows(() => { new DataView(rab, 40, 1); }, RangeError);
})();

(function ConstructorParameterConversionShrinks() {
  const rab = CreateResizableArrayBuffer(40, 80);
  const evil = { valueOf: () => {
    rab.resize(10);
    return 0;
  }};

  assertThrows(() => { new DataView(rab, evil, 20);}, RangeError);
})();

(function ConstructorParameterConversionGrows() {
  const gsab = CreateResizableArrayBuffer(40, 80);
  const evil = { valueOf: () => {
    gsab.resize(50);
    return 0;
  }};

  // Constructing will fail unless we take the new size into account.
  const dv = new DataView(gsab, evil, 50);
  assertEquals(50, dv.byteLength);
})();

(function OrdinaryCreateFromConstructorShrinks() {
  {
    const rab = CreateResizableArrayBuffer(16, 40);
    const newTarget = function() {}.bind(null);
    Object.defineProperty(newTarget, "prototype", {
      get() {
        rab.resize(8);
        return DataView.prototype;
      }
    });
    assertThrows(() => {Reflect.construct(DataView, [rab, 0, 16], newTarget); },
                 RangeError);
  }
  {
    const rab = CreateResizableArrayBuffer(16, 40);
    const newTarget = function() {}.bind(null);
    Object.defineProperty(newTarget, "prototype", {
      get() {
        rab.resize(6);
        return DataView.prototype;
      }
    });
    assertThrows(() => {Reflect.construct(DataView, [rab, 8, 2], newTarget); },
                 RangeError);
  }
})();

(function DataViewByteLengthWhenResizedOutOfBounds1() {
  const rab = CreateResizableArrayBuffer(16, 40);

  const fixedLength = new DataView(rab, 0, 8);
  const lengthTracking = new DataView(rab);

  assertEquals(8, fixedLength.byteLength);
  assertEquals(16, lengthTracking.byteLength);

  assertEquals(0, fixedLength.byteOffset);
  assertEquals(0, lengthTracking.byteOffset);

  rab.resize(2);

  assertThrows(() => { fixedLength.byteLength; }, TypeError);
  assertEquals(2, lengthTracking.byteLength);

  assertThrows(() => { fixedLength.byteOffset; }, TypeError);
  assertEquals(0, lengthTracking.byteOffset);

  rab.resize(8);

  assertEquals(8, fixedLength.byteLength);
  assertEquals(8, lengthTracking.byteLength);

  assertEquals(0, fixedLength.byteOffset);
  assertEquals(0, lengthTracking.byteOffset);

  rab.resize(40);

  assertEquals(8, fixedLength.byteLength);
  assertEquals(40, lengthTracking.byteLength);

  assertEquals(0, fixedLength.byteOffset);
  assertEquals(0, lengthTracking.byteOffset);

  rab.resize(0);

  assertThrows(() => { fixedLength.byteLength; }, TypeError);
  assertEquals(0, lengthTracking.byteLength);

  assertThrows(() => { fixedLength.byteOffset; }, TypeError);
  assertEquals(0, lengthTracking.byteOffset);
})();

// The previous test with offsets.
(function DataViewByteLengthWhenResizedOutOfBounds2() {
  const rab = CreateResizableArrayBuffer(20, 40);

  const fixedLengthWithOffset = new DataView(rab, 8, 8);
  const lengthTrackingWithOffset = new DataView(rab, 8);

  assertEquals(8, fixedLengthWithOffset.byteLength);
  assertEquals(12, lengthTrackingWithOffset.byteLength);

  assertEquals(8, fixedLengthWithOffset.byteOffset);
  assertEquals(8, lengthTrackingWithOffset.byteOffset);

  rab.resize(10);

  assertThrows(() => { fixedLengthWithOffset.byteLength }, TypeError);
  assertEquals(2, lengthTrackingWithOffset.byteLength);

  assertThrows(() => { fixedLengthWithOffset.byteOffset }, TypeError);
  assertEquals(8, lengthTrackingWithOffset.byteOffset);

  rab.resize(16);

  assertEquals(8, fixedLengthWithOffset.byteLength);
  assertEquals(8, lengthTrackingWithOffset.byteLength);

  assertEquals(8, fixedLengthWithOffset.byteOffset);
  assertEquals(8, lengthTrackingWithOffset.byteOffset);

  rab.resize(40);

  assertEquals(8, fixedLengthWithOffset.byteLength);
  assertEquals(32, lengthTrackingWithOffset.byteLength);

  assertEquals(8, fixedLengthWithOffset.byteOffset);
  assertEquals(8, lengthTrackingWithOffset.byteOffset);

  rab.resize(6);

  assertThrows(() => { fixedLengthWithOffset.byteLength }, TypeError);
  assertThrows(() => { lengthTrackingWithOffset.byteLength }, TypeError);

  assertThrows(() => { fixedLengthWithOffset.byteOffset }, TypeError);
  assertThrows(() => { lengthTrackingWithOffset.byteOffset }, TypeError);
})();

(function GetAndSet() {
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

  // Shrink so that fixed length TAs go out of bounds.
  rab.resize(30);

  assertAllDataViewMethodsThrow(fixedLength, 0, TypeError);
  assertAllDataViewMethodsThrow(fixedLengthWithOffset, 0, TypeError);

  testDataViewMethodsUpToSize(lengthTracking, 30);
  testDataViewMethodsUpToSize(lengthTrackingWithOffset, 30 - 2);

  // Shrink so that the TAs with offset go out of bounds.
  rab.resize(1);

  assertAllDataViewMethodsThrow(fixedLength, 0, TypeError);
  assertAllDataViewMethodsThrow(fixedLengthWithOffset, 0, TypeError);
  assertAllDataViewMethodsThrow(lengthTrackingWithOffset, 0, TypeError);

  testDataViewMethodsUpToSize(lengthTracking, 1);
  assertAllDataViewMethodsThrow(lengthTracking, 2, RangeError);

  // Shrink to zero.
  rab.resize(0);

  assertAllDataViewMethodsThrow(fixedLength, 0, TypeError);
  assertAllDataViewMethodsThrow(fixedLengthWithOffset, 0, TypeError);
  assertAllDataViewMethodsThrow(lengthTrackingWithOffset, 0, TypeError);

  testDataViewMethodsUpToSize(lengthTracking, 0);
  assertAllDataViewMethodsThrow(lengthTracking, 1, RangeError);

  // Grow so that all views are back in-bounds.
  rab.resize(34);

  testDataViewMethodsUpToSize(fixedLength, 32);
  assertAllDataViewMethodsThrow(fixedLength, 33, RangeError);

  testDataViewMethodsUpToSize(fixedLengthWithOffset, 32);
  assertAllDataViewMethodsThrow(fixedLengthWithOffset, 33, RangeError);

  testDataViewMethodsUpToSize(lengthTracking, 34);
  assertAllDataViewMethodsThrow(lengthTracking, 35, RangeError);

  testDataViewMethodsUpToSize(lengthTrackingWithOffset, 34 - 2);
  assertAllDataViewMethodsThrow(lengthTrackingWithOffset, 34 - 2 + 1,
                                RangeError);
})();

(function GetParameterConversionShrinks() {
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLength = new DataView(rab, 0, 64);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 0;
    }};
    assertThrows(() => { fixedLength.getUint8(evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLengthWithOffset = new DataView(rab, 2, 64);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 0;
    }};
    assertThrows(() => { fixedLengthWithOffset.getUint8(evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTracking = new DataView(rab);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 12;
    }};
    // The DataView is not out of bounds but the index is.
    assertThrows(() => { lengthTracking.getUint8(evil); }, RangeError);
    assertEquals(0, lengthTracking.getUint8(2));
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTrackingWithOffset = new DataView(rab, 2);

    let evil = { valueOf: () => {
      rab.resize(10);
      return 12;
    }};
    // The DataView is not out of bounds but the index is.
    assertThrows(() => { lengthTrackingWithOffset.getUint8(evil); },
                 RangeError);
    evil = { valueOf: () => {
      rab.resize(0);
      return 0;
    }};
    // Now the DataView is out of bounds.
    assertThrows(() => { lengthTrackingWithOffset.getUint8(evil); }, TypeError);
  }
})();

(function SetParameterConversionShrinks() {
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLength = new DataView(rab, 0, 64);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 0;
    }};
    assertThrows(() => { fixedLength.setUint8(evil, 0); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLengthWithOffset = new DataView(rab, 2, 64);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 0;
    }};
    assertThrows(() => { fixedLengthWithOffset.setUint8(evil, 0); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTracking = new DataView(rab);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 12;
    }};
    lengthTracking.setUint8(12, 0); // Does not throw.
    // The DataView is not out of bounds but the index is.
    assertThrows(() => { lengthTracking.setUint8(evil, 0); }, RangeError);
    lengthTracking.setUint8(2, 0); // Does not throw.
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTrackingWithOffset = new DataView(rab, 2);

    let evil = { valueOf: () => {
      rab.resize(10);
      return 12;
    }};
    lengthTrackingWithOffset.setUint8(12, 0); // Does not throw.
    // The DataView is not out of bounds but the index is.
    assertThrows(() => { lengthTrackingWithOffset.setUint8(evil, 0); },
                 RangeError);
    evil = { valueOf: () => {
      rab.resize(0);
      return 0;
    }};
    // Now the DataView is out of bounds.
    assertThrows(() => { lengthTrackingWithOffset.setUint8(evil, 0); },
                 TypeError);
  }

  // The same tests as before, except now the "resizing" parameter is the second
  // one, not the first one.
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLength = new DataView(rab, 0, 64);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 0;
    }};
    assertThrows(() => { fixedLength.setUint8(0, evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const fixedLengthWithOffset = new DataView(rab, 2, 64);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 0;
    }};
    assertThrows(() => { fixedLengthWithOffset.setUint8(0, evil); }, TypeError);
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTracking = new DataView(rab);

    const evil = { valueOf: () => {
      rab.resize(10);
      return 0;
    }};
    lengthTracking.setUint8(12, 0); // Does not throw.
    // The DataView is not out of bounds but the index is.
    assertThrows(() => { lengthTracking.setUint8(12, evil); }, RangeError);
    lengthTracking.setUint8(2, 0); // Does not throw.
  }
  {
    const rab = CreateResizableArrayBuffer(640, 1280);
    const lengthTrackingWithOffset = new DataView(rab, 2);

    let evil = { valueOf: () => {
      rab.resize(10);
      return 0;
    }};
    // The DataView is not out of bounds but the index is.
    assertThrows(() => { lengthTrackingWithOffset.setUint8(12, evil); },
                 RangeError);
    evil = { valueOf: () => {
      rab.resize(0);
      return 0;
    }};
    // Now the DataView is out of bounds.
    assertThrows(() => { lengthTrackingWithOffset.setUint8(0, evil); },
                 TypeError);
  }
})();

(function DataViewsAndRabGsabDataViews() {
  // Internally we differentiate between JSDataView and JSRabGsabDataView. Test
  // that they're indistinguishable externally.
  const ab = new ArrayBuffer(10);
  const rab = new ArrayBuffer(10, {maxByteLength: 20});

  const dv1 = new DataView(ab);
  const dv2 = new DataView(rab);

  assertEquals(DataView.prototype, dv1.__proto__);
  assertEquals(DataView.prototype, dv2.__proto__);
  assertEquals(DataView, dv1.constructor);
  assertEquals(DataView, dv2.constructor);

  class MyDataView extends DataView {
    constructor(buffer) {
      super(buffer);
    }
  }

  const dv3 = new MyDataView(ab);
  const dv4 = new MyDataView(rab);

  assertEquals(MyDataView.prototype, dv3.__proto__);
  assertEquals(MyDataView.prototype, dv4.__proto__);
  assertEquals(MyDataView, dv3.constructor);
  assertEquals(MyDataView, dv4.constructor);
})();
