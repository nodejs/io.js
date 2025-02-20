// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function ConstructorThrowsIfBufferDetached() {
  const rab = CreateResizableArrayBuffer(40, 80);
  %ArrayBufferDetach(rab);

  for (let ctor of ctors) {
    assertThrows(() => { ctor(rab); }, TypeError);
    assertThrows(() => { ctor(rab, 8); }, TypeError);
    assertThrows(() => { ctor(rab, 8, 1); }, TypeError);
  }
})();

(function TypedArrayLengthAndByteLength() {
  const rab = CreateResizableArrayBuffer(40, 80);

  let tas = [];
  for (let ctor of ctors) {
    tas.push(new ctor(rab, 0, 3));
    tas.push(new ctor(rab, 8, 3));
    tas.push(new ctor(rab));
    tas.push(new ctor(rab, 8));
  }

  %ArrayBufferDetach(rab);

  for (let ta of tas) {
    assertEquals(0, ta.length);
    assertEquals(0, ta.byteLength);
  }
})();

(function AccessDetachedTypedArray() {
  const rab = CreateResizableArrayBuffer(16, 40);

  const i8a = new Int8Array(rab, 0, 4);

  // Initial values
  for (let i = 0; i < 4; ++i) {
    assertEquals(0, i8a[i]);
  }

  // Within-bounds write
  for (let i = 0; i < 4; ++i) {
    i8a[i] = i;
  }

  %ArrayBufferDetach(rab);

  // OOB read
  for (let i = 0; i < 4; ++i) {
    assertEquals(undefined, i8a[i]);
  }

  // OOB write (has no effect)
  for (let i = 0; i < 4; ++i) {
    i8a[i] = 10;
  }

  for (let i = 0; i < 4; ++i) {
    assertEquals(undefined, i8a[i]);
  }
})();

(function LoadFromOutOfScopeTypedArrayWithFeedback() {
  function ReadElement2(ta) {
    return ta[2];
  }
  %EnsureFeedbackVectorForFunction(ReadElement2);

  const rab = CreateResizableArrayBuffer(16, 40);

  const i8a = new Int8Array(rab, 0, 4);
  assertEquals(0, ReadElement2(i8a));

  // Within-bounds write
  for (let i = 0; i < 4; ++i) {
    i8a[i] = i;
  }

  %ArrayBufferDetach(rab);

  // OOB read
  for (let i = 0; i < 3; ++i) {
    assertEquals(undefined, ReadElement2(i8a));
  }
})();

(function StoreToOutOfScopeTypedArrayWithFeedback() {
  function WriteElement2(ta, i) {
    ta[2] = i;
  }
  %EnsureFeedbackVectorForFunction(WriteElement2);

  const rab = CreateResizableArrayBuffer(16, 40);

  const i8a = new Int8Array(rab, 0, 4);
  assertEquals(0, i8a[2]);

  // Within-bounds write
  for (let i = 0; i < 3; ++i) {
    WriteElement2(i8a, 3);
  }

  %ArrayBufferDetach(rab);

  // OOB write (has no effect)
  for (let i = 0; i < 3; ++i) {
    WriteElement2(i8a, 4);
  }

  // OOB read
  for (let i = 0; i < 3; ++i) {
    assertEquals(undefined, i8a[2]);
  }
})();

(function FillParameterConversionDetaches() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => { %ArrayBufferDetach(rab); return 1;}};
    // The length is read after converting the first parameter ('value'), so the
    // detaching parameter has to be the 2nd ('start') or 3rd ('end').
    assertThrows(function() {
      TypedArrayFillHelper(fixedLength, 1, 0, evil);
    }, TypeError);
  }

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => { %ArrayBufferDetach(rab); return 1;}};
    // The length is read after converting the first parameter ('value'), so the
    // detaching parameter has to be the 2nd ('start') or 3rd ('end').
    // Assert that this doesn't throw (since the buffer is detached, we cannot
    // assert anything about the contents):
    ArrayFillHelper(fixedLength, 1, 0, evil);
    assertEquals(0, fixedLength.length);
  }
})();

(function CopyWithinParameterConversionDetaches() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => { %ArrayBufferDetach(rab); return 2;}};
    assertThrows(function() {
      fixedLength.copyWithin(evil, 0, 1);
    }, TypeError);
  }
})();

function EntriesKeysValues(entriesHelper, keysHelper, valuesHelper, oobThrows) {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    %ArrayBufferDetach(rab);

    // TypedArray.prototype.{entries, keys, values} throw right away when
    // called. Array.prototype.{entries, keys, values} don't throw, but when
    // we try to iterate the returned ArrayIterator, that throws.
    if (oobThrows) {
      assertThrows(() => { entriesHelper(fixedLength); });
      assertThrows(() => { valuesHelper(fixedLength); });
      assertThrows(() => { keysHelper(fixedLength); });

      assertThrows(() => { entriesHelper(fixedLengthWithOffset); });
      assertThrows(() => { valuesHelper(fixedLengthWithOffset); });
      assertThrows(() => { keysHelper(fixedLengthWithOffset); });

      assertThrows(() => { entriesHelper(lengthTracking); });
      assertThrows(() => { valuesHelper(lengthTracking); });
      assertThrows(() => { keysHelper(lengthTracking); });

      assertThrows(() => { entriesHelper(lengthTrackingWithOffset); });
      assertThrows(() => { valuesHelper(lengthTrackingWithOffset); });
      assertThrows(() => { keysHelper(lengthTrackingWithOffset); });
    } else {
      entriesHelper(fixedLength);
      valuesHelper(fixedLength);
      keysHelper(fixedLength);

      entriesHelper(fixedLengthWithOffset);
      valuesHelper(fixedLengthWithOffset);
      keysHelper(fixedLengthWithOffset);

      entriesHelper(lengthTracking);
      valuesHelper(lengthTracking);
      keysHelper(lengthTracking);

      entriesHelper(lengthTrackingWithOffset);
      valuesHelper(lengthTrackingWithOffset);
      keysHelper(lengthTrackingWithOffset);
    }
    assertThrows(() => { Array.from(entriesHelper(fixedLength)); });
    assertThrows(() => { Array.from(valuesHelper(fixedLength)); });
    assertThrows(() => { Array.from(keysHelper(fixedLength)); });

    assertThrows(() => { Array.from(entriesHelper(fixedLengthWithOffset)); });
    assertThrows(() => { Array.from(valuesHelper(fixedLengthWithOffset)); });
    assertThrows(() => { Array.from(keysHelper(fixedLengthWithOffset)); });

    assertThrows(() => { Array.from(entriesHelper(lengthTracking)); });
    assertThrows(() => { Array.from(valuesHelper(lengthTracking)); });
    assertThrows(() => { Array.from(keysHelper(lengthTracking)); });

    assertThrows(() => {
      Array.from(entriesHelper(lengthTrackingWithOffset)); });
    assertThrows(() => { Array.from(valuesHelper(lengthTrackingWithOffset)); });
    assertThrows(() => { Array.from(keysHelper(lengthTrackingWithOffset)); });
  }
}
EntriesKeysValues(
  TypedArrayEntriesHelper, TypedArrayKeysHelper, TypedArrayValuesHelper, true);
EntriesKeysValues(
  ArrayEntriesHelper, ArrayKeysHelper, ArrayValuesHelper, false);

function EveryDetachMidIteration(everyHelper, hasUndefined) {
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

  let values = [];
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
    return true;
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    values = [];
    detachAfter = 2;
    assertTrue(everyHelper(fixedLength, CollectValuesAndDetach));
    if (hasUndefined) {
      assertEquals([0, 2, undefined, undefined], values);
    } else {
      assertEquals([0, 2], values);
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    detachAfter = 1;
    assertTrue(everyHelper(fixedLengthWithOffset, CollectValuesAndDetach));
    if (hasUndefined) {
      assertEquals([4, undefined], values);
    } else {
      assertEquals([4], values);
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    detachAfter = 2;
    assertTrue(everyHelper(lengthTracking, CollectValuesAndDetach));
    if (hasUndefined) {
      assertEquals([0, 2, undefined, undefined], values);
    } else {
      assertEquals([0, 2], values);
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    detachAfter = 1;
    assertTrue(everyHelper(lengthTrackingWithOffset, CollectValuesAndDetach));
    if (hasUndefined) {
      assertEquals([4, undefined], values);
    } else {
      assertEquals([4], values);
    }
  }
}
EveryDetachMidIteration(TypedArrayEveryHelper, true);
EveryDetachMidIteration(ArrayEveryHelper, false);

function SomeDetachMidIteration(someHelper, hasUndefined) {
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
    assertFalse(someHelper(fixedLength, CollectValuesAndDetach));
    if (hasUndefined) {
      assertEquals([0, 2, undefined, undefined], values);
    } else {
      assertEquals([0, 2], values);
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    detachAfter = 1;
    assertFalse(someHelper(fixedLengthWithOffset, CollectValuesAndDetach));
    if (hasUndefined) {
      assertEquals([4, undefined], values);
    } else {
      assertEquals([4], values);
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    detachAfter = 2;
    assertFalse(someHelper(lengthTracking, CollectValuesAndDetach));
    if (hasUndefined) {
      assertEquals([0, 2, undefined, undefined], values);
    } else {
      assertEquals([0, 2], values);
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    detachAfter = 1;
    assertFalse(someHelper(lengthTrackingWithOffset, CollectValuesAndDetach));
    if (hasUndefined) {
      assertEquals([4, undefined], values);
    } else {
      assertEquals([4], values);
    }
  }
}
SomeDetachMidIteration(TypedArraySomeHelper, true);
SomeDetachMidIteration(ArraySomeHelper, false);

function FindDetachMidIteration(findHelper) {
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
    assertEquals(undefined, findHelper(fixedLength, CollectValuesAndDetach));
    assertEquals([0, 2, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    detachAfter = 1;
    assertEquals(undefined,
                 findHelper(fixedLengthWithOffset, CollectValuesAndDetach));
    assertEquals([4, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    detachAfter = 2;
    assertEquals(undefined, findHelper(lengthTracking, CollectValuesAndDetach));
    assertEquals([0, 2, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    detachAfter = 1;
    assertEquals(undefined,
                 findHelper(lengthTrackingWithOffset, CollectValuesAndDetach));
    assertEquals([4, undefined], values);
  }
}
FindDetachMidIteration(TypedArrayFindHelper);
FindDetachMidIteration(ArrayFindHelper);

function FindIndexDetachMidIteration(findIndexHelper) {
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
    assertEquals(-1, findIndexHelper(fixedLength, CollectValuesAndDetach));
    assertEquals([0, 2, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    detachAfter = 1;
    assertEquals(-1,
                 findIndexHelper(fixedLengthWithOffset, CollectValuesAndDetach));
    assertEquals([4, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    detachAfter = 2;
    assertEquals(-1, findIndexHelper(lengthTracking, CollectValuesAndDetach));
    assertEquals([0, 2, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    detachAfter = 1;
    assertEquals(-1,
        findIndexHelper(lengthTrackingWithOffset, CollectValuesAndDetach));
    assertEquals([4, undefined], values);
  }
}
FindIndexDetachMidIteration(TypedArrayFindIndexHelper);
FindIndexDetachMidIteration(ArrayFindIndexHelper);

function FindLastDetachMidIteration(findLastHelper) {
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
    assertEquals(undefined,
                 findLastHelper(fixedLength, CollectValuesAndDetach));
    assertEquals([6, 4, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    detachAfter = 1;
    assertEquals(undefined,
      findLastHelper(fixedLengthWithOffset, CollectValuesAndDetach));
    assertEquals([6, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    detachAfter = 2;
    assertEquals(undefined,
                 findLastHelper(lengthTracking, CollectValuesAndDetach));
    assertEquals([6, 4, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    detachAfter = 1;
    assertEquals(undefined,
        findLastHelper(lengthTrackingWithOffset, CollectValuesAndDetach));
    assertEquals([6, undefined], values);
  }
}
FindLastDetachMidIteration(TypedArrayFindLastHelper);
FindLastDetachMidIteration(ArrayFindLastHelper);

function FindLastIndexDetachMidIteration(findLastIndexHelper) {
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
    assertEquals(-1, findLastIndexHelper(fixedLength, CollectValuesAndDetach));
    assertEquals([6, 4, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    detachAfter = 1;
    assertEquals(-1,
        findLastIndexHelper(fixedLengthWithOffset, CollectValuesAndDetach));
    assertEquals([6, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    detachAfter = 2;
    assertEquals(-1,
        findLastIndexHelper(lengthTracking, CollectValuesAndDetach));
    assertEquals([6, 4, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    detachAfter = 1;
    assertEquals(-1,
        findLastIndexHelper(lengthTrackingWithOffset, CollectValuesAndDetach));
    assertEquals([6, undefined], values);
  }
}
FindLastIndexDetachMidIteration(TypedArrayFindLastIndexHelper);
FindLastIndexDetachMidIteration(ArrayFindLastIndexHelper);

// The corresponding tests for Array.prototype.filter are in
// typedarray-resizablearraybuffer-array-methods.js.
(function FilterDetachMidIteration() {
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
    assertEquals([], ToNumbers(fixedLength.filter(CollectValuesAndDetach)));
    assertEquals([0, 2, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    detachAfter = 1;
    assertEquals([], ToNumbers(fixedLengthWithOffset.filter(CollectValuesAndDetach)));
    assertEquals([4, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    values = [];
    detachAfter = 2;
    assertEquals([], ToNumbers(lengthTracking.filter(CollectValuesAndDetach)));
    assertEquals([0, 2, undefined, undefined], values);
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    detachAfter = 1;
    assertEquals([], ToNumbers(lengthTrackingWithOffset.filter(CollectValuesAndDetach)));
    assertEquals([4, undefined], values);
  }
})();

(function ForEachReduceReduceRightDetachMidIteration() {
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
    array.forEach(CollectValuesAndDetach);
    return values;
  }

  function ReduceHelper(array) {
    values = [];
    array.reduce((acc, n) => { CollectValuesAndDetach(n); }, "initial value");
    return values;
  }

  function ReduceRightHelper(array) {
    values = [];
    array.reduceRight((acc, n) => { CollectValuesAndDetach(n); },
                      "initial value");
    return values;
  }

  // Test for forEach.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    detachAfter = 2;
    assertEquals([0, 2, undefined, undefined], ForEachHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    detachAfter = 1;
    assertEquals([4, undefined], ForEachHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    detachAfter = 2;
    assertEquals([0, 2, undefined, undefined], ForEachHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    detachAfter = 1;
    assertEquals([4, undefined], ForEachHelper(lengthTrackingWithOffset));
  }

  // Tests for reduce.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    detachAfter = 2;
    assertEquals([0, 2, undefined, undefined], ReduceHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    detachAfter = 1;
    assertEquals([4, undefined], ReduceHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    detachAfter = 2;
    assertEquals([0, 2, undefined, undefined], ReduceHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    detachAfter = 1;
    assertEquals([4, undefined], ReduceHelper(lengthTrackingWithOffset));
  }

  // Tests for reduceRight.

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    detachAfter = 2;
    assertEquals([6, 4, undefined, undefined], ReduceRightHelper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    detachAfter = 1;
    assertEquals([6, undefined], ReduceRightHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    detachAfter = 2;
   assertEquals([6, 4, undefined, undefined], ReduceRightHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    detachAfter = 1;
    assertEquals([6, undefined], ReduceRightHelper(lengthTrackingWithOffset));
  }
})();

function IncludesParameterConversionDetaches(includesHelper) {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertFalse(includesHelper(fixedLength, undefined));
    // The TA is detached so it includes only "undefined".
    assertTrue(includesHelper(fixedLength, undefined, evil));
  }

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertTrue(includesHelper(fixedLength, 0));
    // The TA is detached so it includes only "undefined".
    assertFalse(includesHelper(fixedLength, 0, evil));
  }
}
IncludesParameterConversionDetaches(TypedArrayIncludesHelper);
IncludesParameterConversionDetaches(ArrayIncludesHelper);

function IndexOfParameterConversionDetaches(indexOfHelper) {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertEquals(0, indexOfHelper(lengthTracking, 0));
    // The buffer is detached so indexOf returns -1.
    assertEquals(-1, indexOfHelper(lengthTracking, 0, evil));
  }

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertEquals(0, indexOfHelper(lengthTracking, 0));
    // The buffer is detached so indexOf returns -1, also for undefined).
    assertEquals(-1, indexOfHelper(lengthTracking, undefined, evil));
  }
}
IndexOfParameterConversionDetaches(TypedArrayIndexOfHelper);
IndexOfParameterConversionDetaches(ArrayIndexOfHelper);

function LastIndexOfParameterConversionDetaches(lastIndexOfHelper) {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 2;
    }};
    assertEquals(3, lastIndexOfHelper(lengthTracking, 0));
    // The buffer is detached so lastIndexOf returns -1.
    assertEquals(-1, lastIndexOfHelper(lengthTracking, 0, evil));
  }

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 2;
    }};
    assertEquals(3, lastIndexOfHelper(lengthTracking, 0));
    // The buffer is detached so lastIndexOf returns -1, also for undefined).
    assertEquals(-1, lastIndexOfHelper(lengthTracking, undefined, evil));
  }
}
LastIndexOfParameterConversionDetaches(TypedArrayLastIndexOfHelper);
LastIndexOfParameterConversionDetaches(ArrayLastIndexOfHelper);

(function JoinToLocaleString() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    %ArrayBufferDetach(rab);

    assertThrows(() => { fixedLength.join(); });
    assertThrows(() => { fixedLength.toLocaleString(); });
    assertThrows(() => { fixedLengthWithOffset.join(); });
    assertThrows(() => { fixedLengthWithOffset.toLocaleString(); });
    assertThrows(() => { lengthTracking.join(); });
    assertThrows(() => { lengthTracking.toLocaleString(); });
    assertThrows(() => { lengthTrackingWithOffset.join(); });
    assertThrows(() => { lengthTrackingWithOffset.toLocaleString(); });
 }
})();

(function ArrayJoinToLocaleString() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    %ArrayBufferDetach(rab);

    assertEquals('', ArrayJoinHelper(fixedLength));
    assertEquals('', ArrayToLocaleStringHelper(fixedLength));
    assertEquals('', ArrayJoinHelper(fixedLengthWithOffset));
    assertEquals('', ArrayToLocaleStringHelper(fixedLengthWithOffset));
    assertEquals('', ArrayJoinHelper(lengthTracking));
    assertEquals('', ArrayToLocaleStringHelper(lengthTracking));
    assertEquals('', ArrayJoinHelper(lengthTrackingWithOffset));
    assertEquals('', ArrayToLocaleStringHelper(lengthTrackingWithOffset));
 }
})();

function JoinParameterConversionDetaches(joinHelper) {
  // Detaching + fixed-length TA.
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { toString: () => {
      %ArrayBufferDetach(rab);
      return '.';
    }};
    // We iterate 4 elements, since it was the starting length, but the TA is
    // OOB right after parameter conversion, so all elements are converted to
    // the empty string.
    assertEquals('...', joinHelper(fixedLength, evil));
  }

  // Detaching + length-tracking TA.
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);

    let evil = { toString: () => {
      %ArrayBufferDetach(rab);
      return '.';
    }};
    // We iterate 4 elements, since it was the starting length, but the TA is
    // OOB right after parameter conversion, so all elements are converted to
    // the empty string.
    assertEquals('...', joinHelper(lengthTracking, evil));
  }
}
JoinParameterConversionDetaches(TypedArrayJoinHelper);
JoinParameterConversionDetaches(ArrayJoinHelper);

function ToLocaleStringNumberPrototypeToLocaleStringDetaches(
    toLocaleStringHelper) {
  const oldNumberPrototypeToLocaleString = Number.prototype.toLocaleString;
  const oldBigIntPrototypeToLocaleString = BigInt.prototype.toLocaleString;

  // Detaching + fixed-length TA.
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let detachAfter = 2;
    Number.prototype.toLocaleString = function() {
      --detachAfter;
      if (detachAfter == 0) {
        %ArrayBufferDetach(rab);
      }
      return oldNumberPrototypeToLocaleString.call(this);
    }
    BigInt.prototype.toLocaleString = function() {
      --detachAfter;
      if (detachAfter == 0) {
        %ArrayBufferDetach(rab);
      }
      return oldBigIntPrototypeToLocaleString.call(this);
    }

    // We iterate 4 elements, since it was the starting length. The TA goes
    // OOB after 2 elements.
    assertEquals('0,0,,', toLocaleStringHelper(fixedLength));
  }

  // Detaching + length-tracking TA.
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);

    let detachAfter = 2;
    Number.prototype.toLocaleString = function() {
      --detachAfter;
      if (detachAfter == 0) {
        %ArrayBufferDetach(rab);
      }
      return oldNumberPrototypeToLocaleString.call(this);
    }
    BigInt.prototype.toLocaleString = function() {
      --detachAfter;
      if (detachAfter == 0) {
        %ArrayBufferDetach(rab);
      }
      return oldBigIntPrototypeToLocaleString.call(this);
    }

    // We iterate 4 elements, since it was the starting length. The TA goes
    // OOB after 2 elements.
    assertEquals('0,0,,', toLocaleStringHelper(lengthTracking));
  }

  Number.prototype.toLocaleString = oldNumberPrototypeToLocaleString;
  BigInt.prototype.toLocaleString = oldBigIntPrototypeToLocaleString;
}
ToLocaleStringNumberPrototypeToLocaleStringDetaches(
    TypedArrayToLocaleStringHelper);
ToLocaleStringNumberPrototypeToLocaleStringDetaches(ArrayToLocaleStringHelper);

function MapDetachMidIteration(mapHelper, hasUndefined) {
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
  function CollectValuesAndDetach(n, ix, ta) {
    if (typeof n == 'bigint') {
      values.push(Number(n));
    } else {
      values.push(n);
    }
    if (values.length == detachAfter) {
      %ArrayBufferDetach(rab);
    }
    // We still need to return a valid BigInt / non-BigInt, even if
    // n is `undefined`.
    if (IsBigIntTypedArray(ta)) {
      return 0n;
    } else {
      return 0;
    }
  }

  function Helper(array) {
    values = [];
    mapHelper(array, CollectValuesAndDetach);
    return values;
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    detachAfter = 2;
    if (hasUndefined) {
      assertEquals([0, 2, undefined, undefined], Helper(fixedLength));
    } else {
      assertEquals([0, 2], Helper(fixedLength));
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    detachAfter = 1;
    if (hasUndefined) {
      assertEquals([4, undefined], Helper(fixedLengthWithOffset));
    } else {
      assertEquals([4], Helper(fixedLengthWithOffset));
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    detachAfter = 2;
    if (hasUndefined) {
      assertEquals([0, 2, undefined, undefined], Helper(lengthTracking));
    } else {
      assertEquals([0, 2], Helper(lengthTracking));
    }
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    detachAfter = 1;
    if (hasUndefined) {
      assertEquals([4, undefined], Helper(lengthTrackingWithOffset));
    } else {
      assertEquals([4], Helper(lengthTrackingWithOffset));
    }
  }
}
MapDetachMidIteration(TypedArrayMapHelper, true);
MapDetachMidIteration(ArrayMapHelper, false);

(function MapSpeciesCreateDetaches() {
  let values;
  let rab;
  function CollectValues(n, ix, ta) {
    if (typeof n == 'bigint') {
      values.push(Number(n));
    } else {
      values.push(n);
    }
    // We still need to return a valid BigInt / non-BigInt, even if
    // n is `undefined`.
    if (IsBigIntTypedArray(ta)) {
      return 0n;
    }
    return 0;
  }

  function Helper(array) {
    values = [];
    array.map(CollectValues);
    return values;
  }

  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);

    let detachWhenConstructorCalled = false;
    class MyArray extends ctor {
      constructor(...params) {
        super(...params);
        if (detachWhenConstructorCalled) {
          %ArrayBufferDetach(rab);
        }
      }
    };

    const fixedLength = new MyArray(rab, 0, 4);
    detachWhenConstructorCalled = true;
    assertEquals([undefined, undefined, undefined, undefined],
                 Helper(fixedLength));
  }

  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);

    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    let detachWhenConstructorCalled = false;
    class MyArray extends ctor {
      constructor(...params) {
        super(...params);
        if (detachWhenConstructorCalled) {
          %ArrayBufferDetach(rab);
        }
      }
    };

    const lengthTracking = new MyArray(rab);
    detachWhenConstructorCalled = true;
    assertEquals([undefined, undefined, undefined, undefined],
                 Helper(lengthTracking));
  }
})();

(function SetSourceLengthGetterDetachesTarget() {
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

  let rab;
  function CreateSourceProxy(length) {
    return new Proxy({}, {
      get(target, prop, receiver) {
        if (prop == 'length') {
          %ArrayBufferDetach(rab);
          return length;
        }
        return true; // Can be converted to both BigInt and Number.
      }
    });
  }

  // Tests where the length getter detaches -> these are no-op.
  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    fixedLength.set(CreateSourceProxy(1));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    fixedLengthWithOffset.set(CreateSourceProxy(1));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    lengthTracking.set(CreateSourceProxy(1));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    lengthTrackingWithOffset.set(CreateSourceProxy(1));
  }

  // Tests where the length getter returns a zero -> these don't throw.
  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    fixedLength.set(CreateSourceProxy(0));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    fixedLengthWithOffset.set(CreateSourceProxy(0));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    lengthTracking.set(CreateSourceProxy(0));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    lengthTrackingWithOffset.set(CreateSourceProxy(0));
  }
})();

(function SetDetachTargetMidIteration() {
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

  let rab;
  // Detaching will happen when we're calling Get for the `detachAt`:th data
  // element, but we haven't yet written it to the target.
  let detachAt;
  function CreateSourceProxy(length) {
    let requestedIndices = [];
    return new Proxy({}, {
      get(target, prop, receiver) {
        if (prop == 'length') {
          return length;
        }
        requestedIndices.push(prop);
        if (requestedIndices.length == detachAt) {
          %ArrayBufferDetach(rab);
        }
        return true; // Can be converted to both BigInt and Number.
      }
    });
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);
    detachAt = 2;
    fixedLength.set(CreateSourceProxy(4));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    detachAt = 2;
    fixedLengthWithOffset.set(CreateSourceProxy(2));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTracking = new ctor(rab, 0);
    detachAt = 2;
    lengthTracking.set(CreateSourceProxy(2));
  }

  for (let ctor of ctors) {
    rab = CreateRabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    detachAt = 2;
    lengthTrackingWithOffset.set(CreateSourceProxy(2));
  }
})();

(function Subarray() {
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
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    const fixedLengthSubFull = fixedLength.subarray(0);
    assertEquals([0, 2, 4, 6], ToNumbers(fixedLengthSubFull));
    const fixedLengthWithOffsetSubFull = fixedLengthWithOffset.subarray(0);
    assertEquals([4, 6], ToNumbers(fixedLengthWithOffsetSubFull));
    const lengthTrackingSubFull = lengthTracking.subarray(0);
    assertEquals([0, 2, 4, 6], ToNumbers(lengthTrackingSubFull));
    const lengthTrackingWithOffsetSubFull =
        lengthTrackingWithOffset.subarray(0);
    assertEquals([4, 6], ToNumbers(lengthTrackingWithOffsetSubFull));

    %ArrayBufferDetach(rab);

    // The previously created subarrays are OOB.
    assertEquals(0, fixedLengthSubFull.length);
    assertEquals(0, fixedLengthWithOffsetSubFull.length);
    assertEquals(0, lengthTrackingSubFull.length);
    assertEquals(0, lengthTrackingWithOffsetSubFull.length);

    // Trying to create new subarrays fails.
    assertThrows(() => { fixedLength.subarray(0); }, TypeError);
    assertThrows(() => { fixedLengthWithOffset.subarray(0); }, TypeError);
    assertThrows(() => { lengthTracking.subarray(0); }, TypeError);
    assertThrows(() => { lengthTrackingWithOffset.subarray(0); }, TypeError);
  }
})();

(function SubarrayParameterConversionDetaches() {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //              [0, 2, 4, 6, ...] << lengthTracking
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

  // Fixed-length TA + first parameter conversion detaches. Can't construct
  // even zero-length TAs with a detached buffer.
  for (let ctor of ctors) {
    const rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLength.subarray(evil, 0); }, TypeError);
  }

  // Length-tracking TA + first parameter conversion detaches. Can't construct
  // even zero-length TAs with a detached buffer.
  for (let ctor of ctors) {
    const rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLength.subarray(evil, 0); }, TypeError);
  }

  // Fixed-length TA + second parameter conversion detaches. Can't construct
  // even zero-length TAs with a detached buffer.
  for (let ctor of ctors) {
    const rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLength.subarray(0, evil); }, TypeError);
  }

  // Length-tracking TA + second parameter conversion detaches. Can't construct
  // even zero-length TAs with a detached buffer.
  for (let ctor of ctors) {
    const rab = CreateRabForTest(ctor);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { fixedLength.subarray(0, evil); }, TypeError);
  }
})();

function SortCallbackDetaches(sortHelper) {
  function WriteUnsortedData(taFull) {
    for (let i = 0; i < taFull.length; ++i) {
      WriteToTypedArray(taFull, i, 10 - i);
    }
  }

  let rab;
  function CustomComparison(a, b) {
    %ArrayBufferDetach(rab);
    return 0;
  }

  function AssertIsDetached(ta) {
    assertEquals(0, ta.byteLength);
    assertEquals(0, ta.byteOffset);
    assertEquals(0, ta.length);
  }

  // Fixed length TA.
  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const taFull = new ctor(rab, 0);
    WriteUnsortedData(taFull);

    sortHelper(fixedLength, CustomComparison);
    AssertIsDetached(fixedLength);
  }

  // Length-tracking TA.
  for (let ctor of ctors) {
    rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                     8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab, 0);
    const taFull = new ctor(rab, 0);
    WriteUnsortedData(taFull);

    sortHelper(lengthTracking, CustomComparison);
    AssertIsDetached(lengthTracking);
  }
}
SortCallbackDetaches(TypedArraySortHelper);
SortCallbackDetaches(ArraySortHelper);

(function ObjectDefineProperty() {
  for (let helper of
      [ObjectDefinePropertyHelper, ObjectDefinePropertiesHelper]) {
    for (let ctor of ctors) {
      const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                             8 * ctor.BYTES_PER_ELEMENT);
      const fixedLength = new ctor(rab, 0, 4);
      const fixedLengthWithOffset = new ctor(
          rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
      const lengthTracking = new ctor(rab, 0);
      const lengthTrackingWithOffset = new ctor(
          rab, 2 * ctor.BYTES_PER_ELEMENT);

      // Orig. array: [0, 0, 0, 0]
      //              [0, 0, 0, 0] << fixedLength
      //                    [0, 0] << fixedLengthWithOffset
      //              [0, 0, 0, 0, ...] << lengthTracking
      //                    [0, 0, ...] << lengthTrackingWithOffset

      %ArrayBufferDetach(rab);

      assertThrows(() => { helper(fixedLength, 0, 8); }, TypeError);
      assertThrows(() => { helper(fixedLengthWithOffset, 0, 8); }, TypeError);
      assertThrows(() => { helper(lengthTracking, 0, 8); }, TypeError);
      assertThrows(() => { helper(lengthTrackingWithOffset, 0, 8); },
                   TypeError);
    }
  }
})();

(function ObjectDefinePropertyParameterConversionDetaches() {
  const helper = ObjectDefinePropertyHelper;
  // Fixed length.
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const evil = {toString: () => {
      %ArrayBufferDetach(rab);
      return 0;
    }};
    assertThrows(() => { helper(fixedLength, evil, 8); }, TypeError);
  }
  // Length tracking.
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab, 0);
    const evil = {toString: () => {
        %ArrayBufferDetach(rab);
        return 0;
    }};
    assertThrows(() => { helper(lengthTracking, evil, 8); }, TypeError);
  }
})();

(function FunctionApply() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);

    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    function func(...args) {
      return [...args];
    }

    assertEquals([0, 1, 2, 3], ToNumbers(func.apply(null, fixedLength)));
    assertEquals([2, 3], ToNumbers(func.apply(null, fixedLengthWithOffset)));
    assertEquals([0, 1, 2, 3], ToNumbers(func.apply(null, lengthTracking)));
    assertEquals([2, 3], ToNumbers(func.apply(null, lengthTrackingWithOffset)));

    %ArrayBufferDetach(rab);

    assertEquals([], ToNumbers(func.apply(null, fixedLength)));
    assertEquals([], ToNumbers(func.apply(null, fixedLengthWithOffset)));
    assertEquals([], ToNumbers(func.apply(null, lengthTracking)));
    assertEquals([], ToNumbers(func.apply(null, lengthTrackingWithOffset)));
  }
})();

// The corresponding tests for Array.prototype.slice are in
// typedarray-resizablearraybuffer-array-methods.js.
(function SliceParameterConversionDetaches() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const evil = { valueOf: () => { rab.resize(2 * ctor.BYTES_PER_ELEMENT);
                                    return 0; }};
    assertThrows(() => { fixedLength.slice(evil); }, TypeError);
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
    assertEquals([1, 2, 0, 0], ToNumbers(lengthTracking.slice(evil)));
    assertEquals(2 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

function AtParameterConversionDetaches(atHelper) {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => { %ArrayBufferDetach(rab); return 0;}};
    assertEquals(undefined, atHelper(fixedLength, evil));
  }

  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(rab);

    let evil = { valueOf: () => { %ArrayBufferDetach(rab); return -1;}};
    // The TypedArray is *not* out of bounds since it's length-tracking.
    assertEquals(undefined, atHelper(lengthTracking, evil));
  }
}
AtParameterConversionDetaches(TypedArrayAtHelper);
AtParameterConversionDetaches(ArrayAtHelper);

(function TypedArrayFrom() {
  AllBigIntMatchedCtorCombinations((targetCtor, sourceCtor) => {
    const rab = CreateResizableArrayBuffer(
        4 * sourceCtor.BYTES_PER_ELEMENT,
        8 * sourceCtor.BYTES_PER_ELEMENT);
    const fixedLength = new sourceCtor(rab, 0, 4);
    const fixedLengthWithOffset = new sourceCtor(
        rab, 2 * sourceCtor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new sourceCtor(rab, 0);
    const lengthTrackingWithOffset = new sourceCtor(
        rab, 2 * sourceCtor.BYTES_PER_ELEMENT);

    %ArrayBufferDetach(rab);

    assertThrows(() => { targetCtor.from(fixedLength); }, TypeError);
    assertThrows(() => { targetCtor.from(fixedLengthWithOffset); }, TypeError);
    assertThrows(() => { targetCtor.from(lengthTracking); }, TypeError);
    assertThrows(() => { targetCtor.from(lengthTrackingWithOffset); },
                 TypeError);
  });

  AllBigIntUnmatchedCtorCombinations((targetCtor, sourceCtor) => {
    const rab = CreateResizableArrayBuffer(
        4 * sourceCtor.BYTES_PER_ELEMENT,
        8 * sourceCtor.BYTES_PER_ELEMENT);
    const fixedLength = new sourceCtor(rab, 0, 4);
    const fixedLengthWithOffset = new sourceCtor(
        rab, 2 * sourceCtor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new sourceCtor(rab, 0);
    const lengthTrackingWithOffset = new sourceCtor(
        rab, 2 * sourceCtor.BYTES_PER_ELEMENT);

    %ArrayBufferDetach(rab);

    assertThrows(() => { targetCtor.from(fixedLength); }, TypeError);
    assertThrows(() => { targetCtor.from(fixedLengthWithOffset); }, TypeError);
    assertThrows(() => { targetCtor.from(lengthTracking); }, TypeError);
    assertThrows(() => { targetCtor.from(lengthTrackingWithOffset); },
                 TypeError);
  });
})();
