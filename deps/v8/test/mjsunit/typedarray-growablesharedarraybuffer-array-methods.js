// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

'use strict';

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function ArrayConcatDefault() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);

    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4, ...] << lengthTracking

    function helper(receiver, ...params) {
      return ToNumbers(Array.prototype.concat.call(receiver, ...params));
    }

    // TypedArrays aren't concat spreadable.
    assertEquals([lengthTracking, 5, 6, 7],
                 helper(lengthTracking, [5, 6], [7]));

    // Resizing doesn't matter since the TA is added as a single item.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals([lengthTracking, 5, 6, 7],
                 helper(lengthTracking, [5, 6], [7]));
  }
})();

(function ArrayConcatConcatSpreadable() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    fixedLength[Symbol.isConcatSpreadable] = true;
    fixedLengthWithOffset[Symbol.isConcatSpreadable] = true;
    lengthTracking[Symbol.isConcatSpreadable] = true;
    lengthTrackingWithOffset[Symbol.isConcatSpreadable] = true;

    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, ...] << lengthTracking
    //                    [3, 4, ...] << lengthTrackingWithOffset

    function helper(receiver, ...params) {
      return ToNumbers(Array.prototype.concat.call(receiver, ...params));
    }

    assertEquals([0, 1, 2, 3, 4, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 3, 4, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 1, 2, 3, 4, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 3, 4, 5, 6],
                 helper([0], lengthTrackingWithOffset, [5, 6]));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }
    // Orig. array: [1, 2, 3, 4, 5, 6]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, 5, 6, ...] << lengthTracking
    //                    [3, 4, 5, 6, ...] << lengthTrackingWithOffset

    assertEquals([0, 1, 2, 3, 4, 7, 8], helper([0], fixedLength, [7, 8]));
    assertEquals([0, 3, 4, 7, 8], helper([0], fixedLengthWithOffset, [7, 8]));
    assertEquals([0, 1, 2, 3, 4, 5, 6, 7, 8],
                 helper([0], lengthTracking, [7, 8]));
    assertEquals([0, 3, 4, 5, 6, 7, 8],
                 helper([0], lengthTrackingWithOffset, [7, 8]));
  }
})();

// Hand-crafted test to hit a somewhat esoteric code path in Array.p.concat.
(function ArrayConcatConcatDictionaryElementsProto() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, ...] << lengthTracking
    //                    [3, 4, ...] << lengthTrackingWithOffset

    const largeIndex = 5000;
    function helper(ta) {
      const newArray = [];
      newArray[largeIndex] = 11111;  // Force dictionary mode.
      assertTrue(%HasDictionaryElements(newArray));
      newArray.__proto__ = ta;
      return Array.prototype.concat.call([], newArray);
    }

    function assertArrayContents(expectedStart, array) {
      for (let i = 0; i < expectedStart.length; ++i) {
        assertEquals(expectedStart[i], Number(array[i]));
      }
      assertEquals(largeIndex + 1, array.length);
      // Don't check every index to keep the test run time reasonable.
      for (let i = expectedStart.length; i < largeIndex - 1; i += 153) {
        assertEquals(undefined, array[i]);
      }
      assertEquals(11111, Number(array[largeIndex]));
    }

    assertArrayContents([1, 2, 3, 4], helper(fixedLength));
    assertArrayContents([3, 4], helper(fixedLengthWithOffset));
    assertArrayContents([1, 2, 3, 4], helper(lengthTracking));
    assertArrayContents([3, 4], helper(lengthTrackingWithOffset));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }
    // Orig. array: [1, 2, 3, 4, 5, 6]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, 5, 6, ...] << lengthTracking
    //                    [3, 4, 5, 6, ...] << lengthTrackingWithOffset

    assertArrayContents([1, 2, 3, 4], helper(fixedLength));
    assertArrayContents([3, 4], helper(fixedLengthWithOffset));
    assertArrayContents([1, 2, 3, 4, 5, 6], helper(lengthTracking));
    assertArrayContents([3, 4, 5, 6], helper(lengthTrackingWithOffset));
  }
})();

(function ArrayPushPopShiftUnshiftSplice() {
  // These functions always fail since setting the length fails.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
      8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    for (let func of [Array.prototype.push, Array.prototype.unshift,
                      Array.prototype.splice]) {
      assertThrows(() => {
          func.call(fixedLength, 0); }, TypeError);
      assertThrows(() => {
          func.call(fixedLengthWithOffset, 0); }, TypeError);
      assertThrows(() => {
          func.call(lengthTracking, 0); }, TypeError);
      assertThrows(() => {
        func.call(lengthTrackingWithOffset, 0); }, TypeError);
    }

    for (let func of [Array.prototype.pop, Array.prototype.shift]) {
      assertThrows(() => {
          func.call(fixedLength); }, TypeError);
      assertThrows(() => {
          func.call(fixedLengthWithOffset); }, TypeError);
      assertThrows(() => {
          func.call(lengthTracking); }, TypeError);
      assertThrows(() => {
        func.call(lengthTrackingWithOffset); }, TypeError);
    }
  }
})();

(function ArraySlice() {
  const sliceHelper = ArraySliceHelper;
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    const fixedLengthSlice = sliceHelper(fixedLength);
    assertEquals([0, 1, 2, 3], ToNumbers(fixedLengthSlice));

    const fixedLengthWithOffsetSlice = sliceHelper(fixedLengthWithOffset);
    assertEquals([2, 3], ToNumbers(fixedLengthWithOffsetSlice));

    const lengthTrackingSlice = sliceHelper(lengthTracking);
    assertEquals([0, 1, 2, 3], ToNumbers(lengthTrackingSlice));

    const lengthTrackingWithOffsetSlice = sliceHelper(lengthTrackingWithOffset);
    assertEquals([2, 3], ToNumbers(lengthTrackingWithOffsetSlice));

    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals([0, 1, 2, 3], ToNumbers(sliceHelper(fixedLength)));
    assertEquals([2, 3], ToNumbers(sliceHelper(fixedLengthWithOffset)));
    assertEquals([0, 1, 2, 3, 0, 0], ToNumbers(sliceHelper(lengthTracking)));
    assertEquals([2, 3, 0, 0],
                 ToNumbers(sliceHelper(lengthTrackingWithOffset)));

    // Verify that the previously created slices aren't affected by the growing.
    assertEquals([0, 1, 2, 3], ToNumbers(fixedLengthSlice));
    assertEquals([2, 3], ToNumbers(fixedLengthWithOffsetSlice));
    assertEquals([0, 1, 2, 3], ToNumbers(lengthTrackingSlice));
    assertEquals([2, 3], ToNumbers(lengthTrackingWithOffsetSlice));
  }
})();

(function ArrayFlatFlatMapFrom() {
  const flatHelper = ArrayFlatHelper;
  const flatMapHelper = ArrayFlatMapHelper;

  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    function mapper(n) {
      if (typeof n == 'bigint') {
        return n + 1n;
      }
      return n + 1;
    }

    const fixedLengthFlat = flatHelper(fixedLength);
    assertEquals([0, 1, 2, 3], ToNumbers(fixedLengthFlat));
    assertTrue(fixedLengthFlat instanceof Array);

    const fixedLengthWithOffsetFlat = flatHelper(fixedLengthWithOffset);
    assertEquals([2, 3], ToNumbers(fixedLengthWithOffsetFlat));
    assertTrue(fixedLengthWithOffsetFlat instanceof Array);

    const lengthTrackingFlat = flatHelper(lengthTracking);
    assertEquals([0, 1, 2, 3], ToNumbers(lengthTrackingFlat));
    assertTrue(lengthTrackingFlat instanceof Array);

    const lengthTrackingWithOffsetFlat = flatHelper(lengthTrackingWithOffset);
    assertEquals([2, 3], ToNumbers(lengthTrackingWithOffsetFlat));
    assertTrue(lengthTrackingWithOffsetFlat instanceof Array);

    assertEquals([1, 2, 3, 4],
                 ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals([3, 4],
                 ToNumbers(flatMapHelper(fixedLengthWithOffset, mapper)));
    assertEquals([1, 2, 3, 4],
                 ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals([3, 4],
                 ToNumbers(flatMapHelper(lengthTrackingWithOffset, mapper)));

    assertEquals([0, 1, 2, 3], ToNumbers(Array.from(fixedLength)));
    assertEquals([2, 3], ToNumbers(Array.from(fixedLengthWithOffset)));
    assertEquals([0, 1, 2, 3], ToNumbers(Array.from(lengthTracking)));
    assertEquals([2, 3], ToNumbers(Array.from(lengthTrackingWithOffset)));

    // Grow. New memory is zeroed.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals([0, 1, 2, 3], ToNumbers(flatHelper(fixedLength)));
    assertEquals([2, 3], ToNumbers(flatHelper(fixedLengthWithOffset)));
    assertEquals([0, 1, 2, 3, 0, 0], ToNumbers(flatHelper(lengthTracking)));
    assertEquals([2, 3, 0, 0],
        ToNumbers(flatHelper(lengthTrackingWithOffset)));

    assertEquals([1, 2, 3, 4],
                 ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals([3, 4],
                 ToNumbers(flatMapHelper(fixedLengthWithOffset, mapper)));
    assertEquals([1, 2, 3, 4, 1, 1],
                 ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals([3, 4, 1, 1],
                 ToNumbers(flatMapHelper(lengthTrackingWithOffset, mapper)));

    assertEquals([0, 1, 2, 3], ToNumbers(Array.from(fixedLength)));
    assertEquals([2, 3], ToNumbers(Array.from(fixedLengthWithOffset)));
    assertEquals([0, 1, 2, 3, 0, 0], ToNumbers(Array.from(lengthTracking)));
    assertEquals([2, 3, 0, 0], ToNumbers(Array.from(lengthTrackingWithOffset)));
  }
})();

(function ArrayFlatParameterConversionGrows() {
  const flatHelper = ArrayFlatHelper;
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    const evil = { valueOf: () => { gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
                                    return 0; }};
    // The original length is used.
    assertEquals([1, 2, 3, 4], ToNumbers(flatHelper(lengthTracking, evil)));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, gsab.byteLength);
  }
})();

(function ArrayFlatMapMapperGrows() {
  const flatMapHelper = ArrayFlatMapHelper;

  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    function mapper(n) {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return n;
    }
    assertEquals([1, 2, 3, 4], ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, gsab.byteLength);
  }
})();

(function ArrayFromMapperGrows() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    function mapper(n) {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return n;
    }
    // We keep iterating after the TA has grown.
    assertEquals([1, 2, 3, 4, 0, 0],
                 ToNumbers(Array.from(lengthTracking, mapper)));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, gsab.byteLength);
  }
})();
