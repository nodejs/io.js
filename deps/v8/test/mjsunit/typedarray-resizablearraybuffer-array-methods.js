// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

'use strict';

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function ArrayConcatDefault() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength

    function helper(receiver, ...params) {
      return ToNumbers(Array.prototype.concat.call(receiver, ...params));
    }

    // TypedArrays aren't concat spreadable by default.
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));

    // OOBness doesn't matter since the TA is added as a single item.
    rab.resize(0);
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));

    // The same for detached buffers.
    %ArrayBufferDetach(rab);
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));
  }
})();

(function ArrayConcatConcatSpreadable() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    fixedLength[Symbol.isConcatSpreadable] = true;
    fixedLengthWithOffset[Symbol.isConcatSpreadable] = true;
    lengthTracking[Symbol.isConcatSpreadable] = true;
    lengthTrackingWithOffset[Symbol.isConcatSpreadable] = true;

    const taWrite = new ctor(rab);
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

    // Shrink so that fixed length TAs go out of bounds.
    rab.resize(3 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [1, 2, 3]
    //              [1, 2, 3, ...] << lengthTracking
    //                    [3, ...] << lengthTrackingWithOffset

    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 1, 2, 3, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 3, 5, 6],
                 helper([0], lengthTrackingWithOffset, [5, 6]));

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [1]
    //              [1, ...] << lengthTracking

    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 1, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTrackingWithOffset, [5, 6]));

    // Shrink to zero.
    rab.resize(0);

    // Orig. array: []
    //              [...] << lengthTracking

    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTrackingWithOffset, [5, 6]));

    // Grow so that all TAs are back in-bounds.
    rab.resize(6 * ctor.BYTES_PER_ELEMENT);
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

    // After detaching, all TAs behave like zero length.
    %ArrayBufferDetach(rab);
    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTrackingWithOffset, [5, 6]));
  }
})();

// Hand-crafted test to hit a somewhat esoteric code path in Array.p.concat.
(function ArrayConcatConcatDictionaryElementsProto() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    const taWrite = new ctor(rab);
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

    // Shrink so that fixed length TAs go out of bounds.
    rab.resize(3 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [1, 2, 3]
    //              [1, 2, 3, ...] << lengthTracking
    //                    [3, ...] << lengthTrackingWithOffset

    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([1, 2, 3], helper(lengthTracking));
    assertArrayContents([3], helper(lengthTrackingWithOffset));

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [1]
    //              [1, ...] << lengthTracking

    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([1], helper(lengthTracking));
    assertArrayContents([], helper(lengthTrackingWithOffset));

    // Shrink to zero.
    rab.resize(0);

    // Orig. array: []
    //              [...] << lengthTracking

    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([], helper(lengthTracking));
    assertArrayContents([], helper(lengthTrackingWithOffset));

    // Grow so that all TAs are back in-bounds.
    rab.resize(6 * ctor.BYTES_PER_ELEMENT);
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

    // After detaching, all TAs behave like zero length.
    %ArrayBufferDetach(rab);
    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([], helper(lengthTracking));
    assertArrayContents([], helper(lengthTrackingWithOffset));
  }
})();

(function ArrayPushPopShiftUnshiftSplice() {
  // These functions always fail since setting the length fails.
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    function testAllFuncsThrow() {
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

    testAllFuncsThrow();

    rab.resize(0);

    testAllFuncsThrow();

    %ArrayBufferDetach(rab);

    testAllFuncsThrow();
  }
})();

(function ArraySlice() {
  const sliceHelper = ArraySliceHelper;

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    const fixedLengthSlice = sliceHelper(fixedLength);
    assertEquals([0, 1, 2, 3], ToNumbers(fixedLengthSlice));
    assertTrue(fixedLengthSlice instanceof Array);

    const fixedLengthWithOffsetSlice = sliceHelper(fixedLengthWithOffset);
    assertEquals([2, 3], ToNumbers(fixedLengthWithOffsetSlice));
    assertTrue(fixedLengthWithOffsetSlice instanceof Array);

    const lengthTrackingSlice = sliceHelper(lengthTracking);
    assertEquals([0, 1, 2, 3], ToNumbers(lengthTrackingSlice));
    assertTrue(lengthTrackingSlice instanceof Array);

    const lengthTrackingWithOffsetSlice = sliceHelper(lengthTrackingWithOffset);
    assertEquals([2, 3], ToNumbers(lengthTrackingWithOffsetSlice));
    assertTrue(lengthTrackingWithOffsetSlice instanceof Array);

    // Shrink so that fixed length TAs go out of bounds.
    rab.resize(3 * ctor.BYTES_PER_ELEMENT);

    assertEquals([], ToNumbers(sliceHelper(fixedLength)));
    assertEquals([], ToNumbers(sliceHelper(fixedLengthWithOffset)));

    assertEquals([0, 1, 2], ToNumbers(sliceHelper(lengthTracking)));
    assertEquals([2], ToNumbers(sliceHelper(lengthTrackingWithOffset)));

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * ctor.BYTES_PER_ELEMENT);

    assertEquals([], sliceHelper(fixedLength));
    assertEquals([], sliceHelper(fixedLengthWithOffset));
    assertEquals([], sliceHelper(lengthTrackingWithOffset));

    assertEquals([0], ToNumbers(sliceHelper(lengthTracking)));

     // Shrink to zero.
    rab.resize(0);

    assertEquals([], sliceHelper(fixedLength));
    assertEquals([], sliceHelper(fixedLengthWithOffset));
    assertEquals([], sliceHelper(lengthTrackingWithOffset));
    assertEquals([], sliceHelper(lengthTracking));

    // Verify that the previously created slices aren't affected by the
    // shrinking.
    assertEquals([0, 1, 2, 3], ToNumbers(fixedLengthSlice));
    assertEquals([2, 3], ToNumbers(fixedLengthWithOffsetSlice));
    assertEquals([0, 1, 2, 3], ToNumbers(lengthTrackingSlice));
    assertEquals([2, 3], ToNumbers(lengthTrackingWithOffsetSlice));

    // Grow so that all TAs are back in-bounds. New memory is zeroed.
    rab.resize(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals([0, 0, 0, 0], ToNumbers(sliceHelper(fixedLength)));
    assertEquals([0, 0], ToNumbers(sliceHelper(fixedLengthWithOffset)));
    assertEquals([0, 0, 0, 0, 0, 0], ToNumbers(sliceHelper(lengthTracking)));
    assertEquals([0, 0, 0, 0],
        ToNumbers(sliceHelper(lengthTrackingWithOffset)));
  }
})();

(function ArraySliceParameterConversionShrinks() {
  const sliceHelper = ArraySliceHelper;

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const evil = { valueOf: () => { rab.resize(2 * ctor.BYTES_PER_ELEMENT);
                                    return 0; }};
    assertEquals(new Array(4), sliceHelper(fixedLength, evil));
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    const evil = { valueOf: () => { rab.resize(2 * ctor.BYTES_PER_ELEMENT);
                                    return 0; }};
    assertEquals([1, 2, undefined, undefined],
                 ToNumbers(sliceHelper(lengthTracking, evil)));
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

(function ArraySliceParameterConversionDetaches() {
  const sliceHelper = ArraySliceHelper;

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const evil = { valueOf: () => { %ArrayBufferDetach(rab);
                                    return 0; }};
    assertEquals(new Array(4), sliceHelper(fixedLength, evil));
    assertEquals(0, rab.byteLength);
  }
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    const evil = { valueOf: () => { %ArrayBufferDetach(rab);
                                    return 0; }};
    assertEquals(
        [undefined, undefined, undefined, undefined],
        ToNumbers(sliceHelper(lengthTracking, evil)));
    assertEquals(0, rab.byteLength);
  }
})();

(function ArrayFlatFlatMapFrom() {
  const flatHelper = ArrayFlatHelper;
  const flatMapHelper = ArrayFlatMapHelper;

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taWrite = new ctor(rab);
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

    // Shrink so that fixed length TAs go out of bounds.
    rab.resize(3 * ctor.BYTES_PER_ELEMENT);

    assertEquals([], ToNumbers(flatHelper(fixedLength)));
    assertEquals([], ToNumbers(flatHelper(fixedLengthWithOffset)));

    assertEquals([0, 1, 2], ToNumbers(flatHelper(lengthTracking)));
    assertEquals([2], ToNumbers(flatHelper(lengthTrackingWithOffset)));

    assertEquals([],
                 ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(fixedLengthWithOffset, mapper)));
    assertEquals([1, 2, 3],
                 ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals([3],
                 ToNumbers(flatMapHelper(lengthTrackingWithOffset, mapper)));

    // Array.from works via the iterator, and the iterator for TypedArrays is
    // defined in terms or %TypedArray%.prototype.values. It throws if the TA is
    // OOB, thus, Array.from also throws.
    assertThrows(() => { Array.from(fixedLength); }, TypeError);
    assertThrows(() => { Array.from(fixedLengthWithOffset); }, TypeError);
    assertEquals([0, 1, 2], ToNumbers(Array.from(lengthTracking)));
    assertEquals([2], ToNumbers(Array.from(lengthTrackingWithOffset)));

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * ctor.BYTES_PER_ELEMENT);

    assertEquals([], flatHelper(fixedLength));
    assertEquals([], flatHelper(fixedLengthWithOffset));
    assertEquals([0], ToNumbers(flatHelper(lengthTracking)));
    assertEquals([], flatHelper(lengthTrackingWithOffset));

    assertEquals([],
                 ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(fixedLengthWithOffset, mapper)));
    assertEquals([1],
                 ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(lengthTrackingWithOffset, mapper)));

    assertThrows(() => { Array.from(fixedLength); }, TypeError);
    assertThrows(() => { Array.from(fixedLengthWithOffset); }, TypeError);
    assertEquals([0], ToNumbers(Array.from(lengthTracking)));
    assertThrows(() => { Array.from(lengthTrackingWithOffset) }, TypeError);

     // Shrink to zero.
    rab.resize(0);

    assertEquals([], flatHelper(fixedLength));
    assertEquals([], flatHelper(fixedLengthWithOffset));
    assertEquals([], flatHelper(lengthTracking));
    assertEquals([], flatHelper(lengthTrackingWithOffset));

    assertEquals([],
                 ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(fixedLengthWithOffset, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(lengthTrackingWithOffset, mapper)));

    assertThrows(() => { Array.from(fixedLength); }, TypeError);
    assertThrows(() => { Array.from(fixedLengthWithOffset); }, TypeError);
    assertEquals([], ToNumbers(Array.from(lengthTracking)));
    assertThrows(() => { Array.from(lengthTrackingWithOffset) }, TypeError);

    // Grow so that all TAs are back in-bounds. New memory is zeroed.
    rab.resize(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals([0, 0, 0, 0], ToNumbers(flatHelper(fixedLength)));
    assertEquals([0, 0], ToNumbers(flatHelper(fixedLengthWithOffset)));
    assertEquals([0, 0, 0, 0, 0, 0], ToNumbers(flatHelper(lengthTracking)));
    assertEquals([0, 0, 0, 0],
        ToNumbers(flatHelper(lengthTrackingWithOffset)));

    assertEquals([1, 1, 1, 1],
                 ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals([1, 1],
                 ToNumbers(flatMapHelper(fixedLengthWithOffset, mapper)));
    assertEquals([1, 1, 1, 1, 1, 1],
                 ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals([1, 1, 1, 1],
                 ToNumbers(flatMapHelper(lengthTrackingWithOffset, mapper)));

    assertEquals([0, 0, 0, 0], ToNumbers(Array.from(fixedLength)));
    assertEquals([0, 0], ToNumbers(Array.from(fixedLengthWithOffset)));
    assertEquals([0, 0, 0, 0, 0, 0], ToNumbers(Array.from(lengthTracking)));
    assertEquals([0, 0, 0, 0], ToNumbers(Array.from(lengthTrackingWithOffset)));

    %ArrayBufferDetach(rab);

    assertEquals([], flatHelper(fixedLength));
    assertEquals([], flatHelper(fixedLengthWithOffset));
    assertEquals([], flatHelper(lengthTracking));
    assertEquals([], flatHelper(lengthTrackingWithOffset));

    assertEquals([],
                 ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(fixedLengthWithOffset, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals([],
                 ToNumbers(flatMapHelper(lengthTrackingWithOffset, mapper)));

    assertThrows(() => { Array.from(fixedLength); }, TypeError);
    assertThrows(() => { Array.from(fixedLengthWithOffset); }, TypeError);
    assertThrows(() => { Array.from(lengthTracking); }, TypeError);
    assertThrows(() => { Array.from(lengthTrackingWithOffset) }, TypeError);
  }
})();

(function ArrayFlatParameterConversionShrinks() {
  const flatHelper = ArrayFlatHelper;

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const evil = { valueOf: () => { rab.resize(2 * ctor.BYTES_PER_ELEMENT);
                                    return 0; }};
    assertEquals([], flatHelper(fixedLength, evil));
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    const evil = { valueOf: () => { rab.resize(2 * ctor.BYTES_PER_ELEMENT);
                                    return 0; }};
    assertEquals([1, 2], ToNumbers(flatHelper(lengthTracking, evil)));
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

(function ArrayFlatParameterConversionGrows() {
  const flatHelper = ArrayFlatHelper;
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    const evil = { valueOf: () => { rab.resize(6 * ctor.BYTES_PER_ELEMENT);
                                    return 0; }};
    // The original length is used.
    assertEquals([1, 2, 3, 4], ToNumbers(flatHelper(lengthTracking, evil)));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

(function ArrayFlatParameterConversionDetaches() {
  const flatHelper = ArrayFlatHelper;
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const evil = { valueOf: () => { %ArrayBufferDetach(rab);
                                    return 0; }};
    assertEquals([], flatHelper(fixedLength, evil));
    assertEquals(0, rab.byteLength);
  }
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    const evil = { valueOf: () => { %ArrayBufferDetach(rab);
                                    return 0; }};
    assertEquals([], ToNumbers(flatHelper(lengthTracking, evil)));
    assertEquals(0, rab.byteLength);
  }
})();

(function ArrayFlatMapMapperShrinks() {
  const flatMapHelper = ArrayFlatMapHelper;
  let rab;
  let resizeTo;
  function mapper(n) {
    rab.resize(resizeTo);
    return n;
  }

  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
    const fixedLength = new ctor(rab, 0, 4);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(fixedLength, i, i + 1);
    }
    assertEquals([1], ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    assertEquals([1, 2], ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

(function ArrayFlatMapMapperGrows() {
  const flatMapHelper = ArrayFlatMapHelper;

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    function mapper(n) {
      rab.resize(6 * ctor.BYTES_PER_ELEMENT);
      return n;
    }
    assertEquals([1, 2, 3, 4], ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

(function ArrayFlatMapMapperDetaches() {
  const flatMapHelper = ArrayFlatMapHelper;
  let rab;
  function mapper(n) {
    %ArrayBufferDetach(rab);
    return n;
  }

  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(fixedLength, i, i + 1);
    }
    assertEquals([1], ToNumbers(flatMapHelper(fixedLength, mapper)));
    assertEquals(0, rab.byteLength);
  }
  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    assertEquals([1], ToNumbers(flatMapHelper(lengthTracking, mapper)));
    assertEquals(0, rab.byteLength);
  }
})();

(function ArrayFromMapperShrinks() {
  let rab;
  let resizeTo;
  function mapper(n) {
    rab.resize(resizeTo);
    return n;
  }

  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
    const fixedLength = new ctor(rab, 0, 4);
    assertThrows(() => { Array.from(fixedLength, mapper); }, TypeError);
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    assertEquals([1, 2], ToNumbers(Array.from(lengthTracking, mapper)));
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

(function ArrayFromMapperGrows() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    function mapper(n) {
      rab.resize(6 * ctor.BYTES_PER_ELEMENT);
      return n;
    }
    // We keep iterating after the TA has grown.
    assertEquals([1, 2, 3, 4, 0, 0],
                 ToNumbers(Array.from(lengthTracking, mapper)));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

(function ArrayFromMapperDetaches() {
  let rab;
  function mapper(n) {
    %ArrayBufferDetach(rab);
    return n;
  }

  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    assertThrows(() => { Array.from(fixedLength, mapper); }, TypeError);
    assertEquals(0, rab.byteLength);
  }
  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);
    assertThrows(() => { Array.from(lengthTracking, mapper); }, TypeError);
    assertEquals(0, rab.byteLength);
  }
})();

(function ArrayForEachReduceReduceRightShrinkMidIteration() {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateRabForTest(ctor) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return rab;
  }

  let values;
  let rab;
  let resizeAfter;
  let resizeTo;
  function CollectValuesAndResize(n) {
    if (typeof n == 'bigint') {
      values.push(Number(n));
    } else {
      values.push(n);
    }
    if (values.length == resizeAfter) {
      rab.resize(resizeTo);
    }
    return true;
  }

  function ForEachHelper(array) {
    values = [];
    ArrayForEachHelper(array, CollectValuesAndResize);
    return values;
  }

  function ReduceHelper(array) {
    values = [];
    ArrayReduceHelper(array,
                 (acc, n) => { CollectValuesAndResize(n); }, "initial value");
    return values;
  }

  function ReduceRightHelper(array) {
    values = [];
    ArrayReduceRightHelper(array, (acc, n) => { CollectValuesAndResize(n); },
                      "initial value");
    return values;
  }

  // Test for forEach.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    resizeAfter = 2;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2], ForEachHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    resizeAfter = 1;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4], ForEachHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    resizeAfter = 2;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2, 4], ForEachHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    resizeAfter = 1;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4], ForEachHelper(lengthTrackingWithOffset));
  }

  // Tests for reduce.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    resizeAfter = 2;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2], ReduceHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    resizeAfter = 1;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4], ReduceHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    resizeAfter = 2;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2, 4], ReduceHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    resizeAfter = 1;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4], ReduceHelper(lengthTrackingWithOffset));
  }

  // Tests for reduceRight.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    resizeAfter = 2;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([6, 4], ReduceRightHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    resizeAfter = 1;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([6], ReduceRightHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    resizeAfter = 2;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    // Unaffected by the shrinking, since we've already iterated past the point.
    assertEquals([6, 4, 2, 0], ReduceRightHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    resizeAfter = 1;
    resizeTo = 2 * ctor.BYTES_PER_ELEMENT;
    assertEquals([6, 2, 0], ReduceRightHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    resizeAfter = 1;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    // Unaffected by the shrinking, since we've already iterated past the point.
    assertEquals([6, 4], ReduceRightHelper(lengthTrackingWithOffset));
  }
})();

(function ArrayForEachReduceReduceRightDetachMidIteration() {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateRabForTest(ctor) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return rab;
  }

  let values;
  let rab;
  let detachAfter;
  function CollectValuesAndDetach(n) {
    if (typeof n == 'bigint') {
      values.push(Number(n));
    } else {
      values.push(n);
    }
    if (values.length == detachAfter) {
      %ArrayBufferDetach(rab);
    }
    return true;
  }

  function ForEachHelper(array) {
    values = [];
    ArrayForEachHelper(array, CollectValuesAndDetach);
    return values;
  }

  function ReduceHelper(array) {
    values = [];
    ArrayReduceHelper(array, (acc, n) => { CollectValuesAndDetach(n); },
                      "initial value");
    return values;
  }

  function ReduceRightHelper(array) {
    values = [];
    ArrayReduceRightHelper(array, (acc, n) => { CollectValuesAndDetach(n); },
                           "initial value");
    return values;
  }

  // Test for forEach.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    detachAfter = 2;
    assertEquals([0, 2], ForEachHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    detachAfter = 1;
    assertEquals([4], ForEachHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    detachAfter = 2;
    assertEquals([0, 2], ForEachHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    detachAfter = 1;
    assertEquals([4], ForEachHelper(lengthTrackingWithOffset));
  }

  // Tests for reduce.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    detachAfter = 2;
    assertEquals([0, 2], ReduceHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    detachAfter = 1;
    assertEquals([4], ReduceHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    detachAfter = 2;
    assertEquals([0, 2], ReduceHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    detachAfter = 1;
    assertEquals([4], ReduceHelper(lengthTrackingWithOffset));
  }

  // Tests for reduceRight.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    detachAfter = 2;
    assertEquals([6, 4], ReduceRightHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    detachAfter = 1;
    assertEquals([6], ReduceRightHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    detachAfter = 2;
   assertEquals([6, 4], ReduceRightHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    detachAfter = 1;
    assertEquals([6], ReduceRightHelper(lengthTrackingWithOffset));
  }
})();

(function FilterShrinkMidIteration() {
  const filterHelper = ArrayFilterHelper;
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateRabForTest(ctor) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return rab;
  }

  let values;
  let rab;
  let resizeAfter;
  let resizeTo;
  function CollectValuesAndResize(n) {
    if (typeof n == 'bigint') {
      values.push(Number(n));
    } else {
      values.push(n);
    }
    if (values.length == resizeAfter) {
      rab.resize(resizeTo);
    }
    return false;
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    values = [];
    resizeAfter = 2;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([],
                 ToNumbers(filterHelper(fixedLength, CollectValuesAndResize)));
    assertEquals([0, 2], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    resizeAfter = 1;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([],ToNumbers(filterHelper(
        fixedLengthWithOffset, CollectValuesAndResize)));
    assertEquals([4], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    resizeAfter = 2;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([], ToNumbers(filterHelper(
        lengthTracking, CollectValuesAndResize)));
    assertEquals([0, 2, 4], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    resizeAfter = 1;
    resizeTo = 3 * ctor.BYTES_PER_ELEMENT;
    assertEquals([], ToNumbers(filterHelper(
        lengthTrackingWithOffset, CollectValuesAndResize)));
    assertEquals([4], values);
  }
})();

(function FilterDetachMidIteration() {
  const filterHelper = ArrayFilterHelper;
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateRabForTest(ctor) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return rab;
  }

  let values;
  let rab;
  let detachAfter;
  function CollectValuesAndDetach(n) {
    if (n == undefined) {
      values.push(n);
    } else {
      values.push(Number(n));
    }
    if (values.length == detachAfter) {
       %ArrayBufferDetach(rab);
    }
    return false;
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    values = [];
    detachAfter = 2;
    assertEquals([],
        ToNumbers(filterHelper(fixedLength, CollectValuesAndDetach)));
    assertEquals([0, 2], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    detachAfter = 1;
    assertEquals([],
        ToNumbers(filterHelper(fixedLengthWithOffset, CollectValuesAndDetach)));
    assertEquals([4], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    detachAfter = 2;
    assertEquals([],
        ToNumbers(filterHelper(lengthTracking, CollectValuesAndDetach)));
    assertEquals([0, 2], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    detachAfter = 1;
    assertEquals([],
        ToNumbers(filterHelper(lengthTrackingWithOffset, CollectValuesAndDetach)));
    assertEquals([4], values);
  }
})();
