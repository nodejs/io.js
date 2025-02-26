// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

d8.file.execute('test/mjsunit/typedarray-helpers.js');

function CheckToReversed(orig) {
  let r = orig.toReversed();
  assertEquals(orig.length, r.length);
  for (let i = 0; i < r.length; i++) {
    assertEquals(orig[orig.length - i - 1], r[i]);
  }
  assertFalse(orig === r);
}

(function TestSurface() {
  for (let TA of ctors) {
    assertEquals(0, TA.prototype.toReversed.length);
    assertEquals("toReversed", TA.prototype.toReversed.name);
  }
})();

(function TestBasic() {
  for (let TA of ctors) {
    let a = new TA(4);
    for (let i = 0; i < 4; i++) {
      a[i] = i + "";
    }
    CheckToReversed(a);
  }
})();

(function TestNonTypedArray() {
  for (let TA of ctors) {
    assertThrows(() => { TA.prototype.toReversed.call([1,2,3,4]); }, TypeError);
  }
})();

(function TestResizableBuffer() {
  for (let TA of ctors) {
    const rab = CreateResizableArrayBuffer(4 * TA.BYTES_PER_ELEMENT,
                                           8 * TA.BYTES_PER_ELEMENT);
    const fixedLength = new TA(rab, 0, 4);
    const fixedLengthWithOffset = new TA(rab, 2 * TA.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new TA(rab, 0);
    const lengthTrackingWithOffset = new TA(rab, 2 * TA.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taWrite = new TA(rab);
    for (let i = 0; i < 4; i++) {
      WriteToTypedArray(taWrite, i, i);
    }

    // Orig. array: [0, 1, 2, 3]
    //              [0, 1, 2, 3] << fixedLength
    //                    [2, 3] << fixedLengthWithOffset
    //              [0, 1, 2, 3, ...] << lengthTracking
    //                    [2, 3, ...] << lengthTrackingWithOffset

    CheckToReversed(fixedLength);
    CheckToReversed(fixedLengthWithOffset);
    CheckToReversed(lengthTracking);
    CheckToReversed(lengthTrackingWithOffset);

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * TA.BYTES_PER_ELEMENT);
    WriteToTypedArray(taWrite, 0, 0);

    assertThrows(() => { fixedLength.toReversed(); }, TypeError);
    assertThrows(() => { fixedLengthWithOffset.toReversed(); }, TypeError);
    CheckToReversed(lengthTracking);
    assertThrows(() => { lengthTrackingWithOffset.toReversed(); }, TypeError);

    // Shrink to zero.
    rab.resize(0);

    assertThrows(() => { fixedLength.toReversed(); }, TypeError);
    assertThrows(() => { fixedLengthWithOffset.toReversed(); }, TypeError);
    CheckToReversed(lengthTracking);
    assertThrows(() => { lengthTrackingWithOffset.toReversed(); }, TypeError);

    // Grow so that all TAs are back in-bounds.
    rab.resize(6 * TA.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    // Orig. array: [0, 1, 2, 3, 4, 5]
    //              [0, 1, 2, 3] << fixedLength
    //                    [2, 3] << fixedLengthWithOffset
    //              [0, 1, 2, 3, 4, 5, ...] << lengthTracking
    //                    [2, 3, 4, 5, ...] << lengthTrackingWithOffset

    CheckToReversed(fixedLength);
    CheckToReversed(fixedLengthWithOffset);
    CheckToReversed(lengthTracking);
    CheckToReversed(lengthTrackingWithOffset);
  }
})();

(function TestGrowableSAB() {
  for (let TA of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * TA.BYTES_PER_ELEMENT,
                                           8 * TA.BYTES_PER_ELEMENT);
    const fixedLength = new TA(gsab, 0, 4);
    const fixedLengthWithOffset = new TA(gsab, 2 * TA.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new TA(gsab, 0);
    const lengthTrackingWithOffset = new TA(gsab, 2 * TA.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taWrite = new TA(gsab);
    for (let i = 0; i < 4; i++) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset
    CheckToReversed(fixedLength);
    CheckToReversed(fixedLengthWithOffset);
    CheckToReversed(lengthTracking);
    CheckToReversed(lengthTrackingWithOffset);

    // Grow.
    gsab.grow(6 * TA.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset
    CheckToReversed(fixedLength);
    CheckToReversed(fixedLengthWithOffset);
    CheckToReversed(lengthTracking);
    CheckToReversed(lengthTrackingWithOffset);
  }
})();

(function TestDetached() {
  for (let TA of ctors) {
    let a = new TA(4);
    %ArrayBufferDetach(a.buffer);
    assertThrows(() => { a.toReversed(); }, TypeError);
  }
})();

(function TestNoSpecies() {
  class MyUint8Array extends Uint8Array {
    static get [Symbol.species]() { return MyUint8Array; }
  }
  assertEquals(Uint8Array, (new MyUint8Array()).toReversed().constructor);
})();
