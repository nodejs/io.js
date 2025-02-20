// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

d8.file.execute('test/mjsunit/typedarray-helpers.js');

const TAProto = Object.getPrototypeOf(Int8Array.prototype);

function AssertToSortedAndSortSameResult(input, ...args) {
  const orig = input.slice();
  const s = TAProto.toSorted.apply(input, args);
  const copy = input.slice();
  TAProto.sort.apply(copy, args);

  // The in-place sorted version should be pairwise equal to the toSorted
  // version.
  assertEquals(copy, s);

  // The original input should be unchanged.
  assertEquals(orig, input);

  // The result of toSorted() is a copy.
  assertFalse(s === input);
}

function TestToSortedBasicBehaviorHelper(input) {
  // No custom comparator.
  AssertToSortedAndSortSameResult(input);
  // Custom comparator.
  AssertToSortedAndSortSameResult(input, (x, y) => {
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
  });
}

(function TestSurface() {
  for (let TA of ctors) {
    assertEquals(1, TA.prototype.toSorted.length);
    assertEquals("toSorted", TA.prototype.toSorted.name);
  }
})();

(function TestBasic() {
  for (let TA of ctors) {
    let a = new TA(4);
    for (let i = 0; i < 4; i++) {
      WriteToTypedArray(a, i, (Math.random() * 100)|0);
    }
    TestToSortedBasicBehaviorHelper(a);
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
      WriteToTypedArray(taWrite, i, (Math.random() * 100)|0);
    }

    // a, b, c, d, below represent random values.
    //
    // Orig. array: [a, b, c, d]
    //              [a, b, c, d] << fixedLength
    //                    [c, d] << fixedLengthWithOffset
    //              [a, b, c, d, ...] << lengthTracking
    //                    [c, d, ...] << lengthTrackingWithOffset

    TestToSortedBasicBehaviorHelper(fixedLength);
    TestToSortedBasicBehaviorHelper(fixedLengthWithOffset);
    TestToSortedBasicBehaviorHelper(lengthTracking);
    TestToSortedBasicBehaviorHelper(lengthTrackingWithOffset);

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * TA.BYTES_PER_ELEMENT);
    WriteToTypedArray(taWrite, 0, 0);

    assertThrows(() => { fixedLength.toSorted(); }, TypeError);
    assertThrows(() => { fixedLengthWithOffset.toSorted(); }, TypeError);
    TestToSortedBasicBehaviorHelper(lengthTracking);
    assertThrows(() => { lengthTrackingWithOffset.toSorted(); }, TypeError);

    // Shrink to zero.
    rab.resize(0);

    assertThrows(() => { fixedLength.toSorted(); }, TypeError);
    assertThrows(() => { fixedLengthWithOffset.toSorted(); }, TypeError);
    TestToSortedBasicBehaviorHelper(lengthTracking);
    assertThrows(() => { lengthTrackingWithOffset.toSorted(); }, TypeError);

    // Grow so that all TAs are back in-bounds.
    rab.resize(6 * TA.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, (Math.random() * 100)|0);
    }

    // Orig. array: [a, b, c, d, e, f]
    //              [a, b, c, d] << fixedLength
    //                    [c, d] << fixedLengthWithOffset
    //              [a, b, c, d, e, f, ...] << lengthTracking
    //                    [c, d, e, f, ...] << lengthTrackingWithOffset

    TestToSortedBasicBehaviorHelper(fixedLength);
    TestToSortedBasicBehaviorHelper(fixedLengthWithOffset);
    TestToSortedBasicBehaviorHelper(lengthTracking);
    TestToSortedBasicBehaviorHelper(lengthTrackingWithOffset);
  }
})();

(function TestComparatorShrinks() {
  for (let TA of ctors) {
    const rab = CreateResizableArrayBuffer(4 * TA.BYTES_PER_ELEMENT,
                                           8 * TA.BYTES_PER_ELEMENT);
    const lengthTracking = new TA(rab, 0);
    for (let i = 0; i < 4; i++) {
      WriteToTypedArray(lengthTracking, i, (Math.random() * 100)|0)
    }

    let resized = false;
    const evilComparator = (x, y) => {
      if (!resized) {
        resized = true;
        rab.resize(2 * TA.BYTES_PER_ELEMENT);
      }
      if (x < y) return -1;
      if (x > y) return 1;
      return 0;
    };

    // Shrinks don't affect toSorted because sorting is done on a snapshot taken
    // at the beginning.
    let s = lengthTracking.toSorted(evilComparator);
    assertEquals(4, s.length);
    // Source shrunk.
    assertEquals(2, lengthTracking.length);
  }
})();

(function TestComparatorGrows() {
  for (let TA of ctors) {
    const rab = CreateResizableArrayBuffer(4 * TA.BYTES_PER_ELEMENT,
                                           8 * TA.BYTES_PER_ELEMENT);
    const lengthTracking = new TA(rab, 0);
    for (let i = 0; i < 4; i++) {
      WriteToTypedArray(lengthTracking, i, (Math.random() * 100)|0)
    }

    let resized = false;
    const evilComparator = (x, y) => {
      if (!resized) {
        resized = true;
        rab.resize(6 * TA.BYTES_PER_ELEMENT);
      }
      if (x < y) return -1;
      if (x > y) return 1;
      return 0;
    };

    // Grows also don't affect toSorted because sorting is done on a snapshot
    // taken at the beginning.
    let s = lengthTracking.toSorted(evilComparator);
    assertEquals(4, s.length);
    // Source grew.
    assertEquals(6, lengthTracking.length);
  }
})();

(function TestComparatorDetaches() {
  for (let TA of ctors) {
    const rab = CreateResizableArrayBuffer(4 * TA.BYTES_PER_ELEMENT,
                                           8 * TA.BYTES_PER_ELEMENT);
    const lengthTracking = new TA(rab, 0);
    for (let i = 0; i < 4; i++) {
      WriteToTypedArray(lengthTracking, i, (Math.random() * 100)|0)
    }

    let detached = false;
    const evilComparator = (x, y) => {
      if (!detached) {
        detached = true;
        %ArrayBufferDetach(rab);
      }
      if (x < y) return -1;
      if (x > y) return 1;
      return 0;
    };

    // Detaching also don't affect toSorted because sorting is done on a snapshot
    // taken at the beginning.
    let s = lengthTracking.toSorted(evilComparator);
    assertEquals(4, s.length);
    // Source is detached.
    assertEquals(0, lengthTracking.length);
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
      WriteToTypedArray(taWrite, i, (Math.random() * 100)|0);
    }

    // Orig. array: [a, b, c, d]
    //              [a, b, c, d] << fixedLength
    //                    [c, d] << fixedLengthWithOffset
    //              [a, b, c, d, ...] << lengthTracking
    //                    [c, d, ...] << lengthTrackingWithOffset
    TestToSortedBasicBehaviorHelper(fixedLength);
    TestToSortedBasicBehaviorHelper(fixedLengthWithOffset);
    TestToSortedBasicBehaviorHelper(lengthTracking);
    TestToSortedBasicBehaviorHelper(lengthTrackingWithOffset);

    // Grow.
    gsab.grow(6 * TA.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, (Math.random() * 100)|0);
    }

    // Orig. array: [a, b, c, d, e, f]
    //              [a, b, c, d] << fixedLength
    //                    [c, d] << fixedLengthWithOffset
    //              [a, b, c, d, e, f, ...] << lengthTracking
    //                    [c, d, e, f, ...] << lengthTrackingWithOffset
    TestToSortedBasicBehaviorHelper(fixedLength);
    TestToSortedBasicBehaviorHelper(fixedLengthWithOffset);
    TestToSortedBasicBehaviorHelper(lengthTracking);
    TestToSortedBasicBehaviorHelper(lengthTrackingWithOffset);
  }
})();

(function TestComparatorGrows() {
  for (let TA of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * TA.BYTES_PER_ELEMENT,
                                                 8 * TA.BYTES_PER_ELEMENT);
    const lengthTracking = new TA(gsab, 0);
    for (let i = 0; i < 4; i++) {
      WriteToTypedArray(lengthTracking, i, (Math.random() * 100)|0)
    }

    let resized = false;
    const evilComparator = (x, y) => {
      if (!resized) {
        resized = true;
        gsab.grow(6 * TA.BYTES_PER_ELEMENT);
      }
      if (x < y) return -1;
      if (x > y) return 1;
      return 0;
    };

    // Grows also don't affect toSorted because sorting is done on a snapshot
    // taken at the beginning.
    let s = lengthTracking.toSorted(evilComparator);
    assertEquals(4, s.length);
    // Source grew.
    assertEquals(6, lengthTracking.length);
  }
})();

(function TestNonTypedArray() {
  for (let TA of ctors) {
    assertThrows(() => { TA.prototype.toSorted.call([1,2,3,4]); }, TypeError);
  }
})();

(function TestDetached() {
  for (let TA of ctors) {
    let a = new TA(4);
    %ArrayBufferDetach(a.buffer);
    assertThrows(() => { a.toSorted(); }, TypeError);
  }
})();

(function TestNoSpecies() {
  class MyUint8Array extends Uint8Array {
    static get [Symbol.species]() { return MyUint8Array; }
  }
  assertEquals(Uint8Array, (new MyUint8Array()).toSorted().constructor);
})();
