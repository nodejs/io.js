// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging --allow-natives-syntax

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function TypedArrayPrototype() {
  const gsab = CreateGrowableSharedArrayBuffer(40, 80);
  const sab = new SharedArrayBuffer(80);

  for (let ctor of ctors) {
    const ta_gsab = new ctor(gsab, 0, 3);
    const ta_sab = new ctor(sab, 0, 3);
    assertEquals(ta_gsab.__proto__, ta_sab.__proto__);
  }
})();

(function TypedArrayLengthAndByteLength() {
  const gsab = CreateGrowableSharedArrayBuffer(40, 80);

  for (let ctor of ctors) {
    const ta = new ctor(gsab, 0, 3);
    assertEquals(gsab, ta.buffer);
    assertEquals(3, ta.length);
    assertEquals(3 * ctor.BYTES_PER_ELEMENT, ta.byteLength);

    const empty_ta = new ctor(gsab, 0, 0);
    assertEquals(gsab, empty_ta.buffer);
    assertEquals(0, empty_ta.length);
    assertEquals(0, empty_ta.byteLength);

    const ta_with_offset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 3);
    assertEquals(gsab, ta_with_offset.buffer);
    assertEquals(3, ta_with_offset.length);
    assertEquals(3 * ctor.BYTES_PER_ELEMENT, ta_with_offset.byteLength);

    const empty_ta_with_offset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 0);
    assertEquals(gsab, empty_ta_with_offset.buffer);
    assertEquals(0, empty_ta_with_offset.length);
    assertEquals(0, empty_ta_with_offset.byteLength);

    const length_tracking_ta = new ctor(gsab);
    assertEquals(gsab, length_tracking_ta.buffer);
    assertEquals(40 / ctor.BYTES_PER_ELEMENT, length_tracking_ta.length);
    assertEquals(40, length_tracking_ta.byteLength);

    const offset = 8;
    const length_tracking_ta_with_offset = new ctor(gsab, offset);
    assertEquals(gsab, length_tracking_ta_with_offset.buffer);
    assertEquals((40 - offset) / ctor.BYTES_PER_ELEMENT,
                 length_tracking_ta_with_offset.length);
    assertEquals(40 - offset, length_tracking_ta_with_offset.byteLength);

    const length_tracking_ta_zero = new ctor(gsab, 40);
    assertEquals(gsab, length_tracking_ta_zero.buffer);
    assertEquals(0, length_tracking_ta_zero.length);
    assertEquals(0, length_tracking_ta_zero.byteLength);
  }
})();

(function ConstructInvalid() {
  const gsab = CreateGrowableSharedArrayBuffer(40, 80);

  for (let ctor of ctors) {
    // Length too big.
    assertThrows(() => { new ctor(gsab, 0, 40 / ctor.BYTES_PER_ELEMENT + 1); },
                 RangeError);

    // Offset too close to the end.
    assertThrows(() => { new ctor(gsab, 40 - ctor.BYTES_PER_ELEMENT, 2); },
                 RangeError);

    // Offset beyond end.
    assertThrows(() => { new ctor(gsab, 40, 1); }, RangeError);

    if (ctor.BYTES_PER_ELEMENT > 1) {
      // Offset not a multiple of ctor.BYTES_PER_ELEMENT.
      assertThrows(() => { new ctor(gsab, 1, 1); }, RangeError);
      assertThrows(() => { new ctor(gsab, 1); }, RangeError);
    }
  }

  // Verify the error messages.
  assertThrows(() => { new Int16Array(gsab, 1, 1); }, RangeError,
               /start offset of Int16Array should be a multiple of 2/);

  assertThrows(() => { new Int16Array(gsab, 38, 2); }, RangeError,
               /Invalid typed array length: 2/);
})();

(function ConstructFromTypedArray() {
  AllBigIntMatchedCtorCombinations((targetCtor, sourceCtor) => {
    const gsab = CreateGrowableSharedArrayBuffer(
        4 * sourceCtor.BYTES_PER_ELEMENT,
        8 * sourceCtor.BYTES_PER_ELEMENT);
    const fixedLength = new sourceCtor(gsab, 0, 4);
    const fixedLengthWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new sourceCtor(gsab, 0);
    const lengthTrackingWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taFull = new sourceCtor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taFull, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, ...] << lengthTracking
    //                    [3, 4, ...] << lengthTrackingWithOffset

    assertEquals([1, 2, 3, 4], ToNumbers(new targetCtor(fixedLength)));
    assertEquals([3, 4], ToNumbers(new targetCtor(fixedLengthWithOffset)));
    assertEquals([1, 2, 3, 4], ToNumbers(new targetCtor(lengthTracking)));
    assertEquals([3, 4], ToNumbers(new targetCtor(lengthTrackingWithOffset)));

    // Grow.
    gsab.grow(6 * sourceCtor.BYTES_PER_ELEMENT);

    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taFull, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4, 5, 6]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, 5, 6, ...] << lengthTracking
    //                    [3, 4, 5, 6, ...] << lengthTrackingWithOffset

    assertEquals([1, 2, 3, 4], ToNumbers(new targetCtor(fixedLength)));
    assertEquals([3, 4], ToNumbers(new targetCtor(fixedLengthWithOffset)));
    assertEquals([1, 2, 3, 4, 5, 6],
                 ToNumbers(new targetCtor(lengthTracking)));
    assertEquals([3, 4, 5, 6],
                 ToNumbers(new targetCtor(lengthTrackingWithOffset)));
  });

  AllBigIntUnmatchedCtorCombinations((targetCtor, sourceCtor) => {
    const gsab = CreateGrowableSharedArrayBuffer(
        4 * sourceCtor.BYTES_PER_ELEMENT,
        8 * sourceCtor.BYTES_PER_ELEMENT);
    const fixedLength = new sourceCtor(gsab, 0, 4);
    const fixedLengthWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new sourceCtor(gsab, 0);
    const lengthTrackingWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT);

    assertThrows(() => { new targetCtor(fixedLength); }, TypeError);
    assertThrows(() => { new targetCtor(fixedLengthWithOffset); }, TypeError);
    assertThrows(() => { new targetCtor(lengthTracking); }, TypeError);
    assertThrows(() => { new targetCtor(lengthTrackingWithOffset); },
                 TypeError);
  });

})();

(function ConstructFromTypedArraySpeciesConstructorNotCalled() {
  class MySharedArrayBuffer extends SharedArrayBuffer {
    constructor(...params) {
      super(...params);
    }
    static get [Symbol.species]() {
      throw new Error('This should not be called!');
    }
  };

  AllBigIntMatchedCtorCombinations((targetCtor, sourceCtor) => {
    const gsab = new MySharedArrayBuffer(
      4 * sourceCtor.BYTES_PER_ELEMENT,
      {maxByteLength: 8 * sourceCtor.BYTES_PER_ELEMENT});
    // Write some data into the array.
    const taWrite = new sourceCtor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    const fixedLength = new sourceCtor(gsab, 0, 4);
    assertEquals([0, 2, 4, 6], ToNumbers(new targetCtor(fixedLength)));

    const fixedLengthWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT, 2);
    assertEquals([4, 6], ToNumbers(new targetCtor(fixedLengthWithOffset)));

    const lengthTracking = new sourceCtor(gsab, 0);
    assertEquals([0, 2, 4, 6], ToNumbers(new targetCtor(lengthTracking)));

    const lengthTrackingWithOffset = new sourceCtor(
      gsab, 2 * sourceCtor.BYTES_PER_ELEMENT);
    assertEquals([4, 6], ToNumbers(new targetCtor(lengthTrackingWithOffset)));
  });
})();

(function TypedArrayLengthWhenGrown1() {
  const gsab = CreateGrowableSharedArrayBuffer(16, 40);

  // Create TAs which cover the bytes 0-7.
  let tas_and_lengths = [];
  for (let ctor of ctors) {
    const length = 8 / ctor.BYTES_PER_ELEMENT;
    tas_and_lengths.push([new ctor(gsab, 0, length), length]);
  }

  for (let [ta, length] of tas_and_lengths) {
    assertEquals(length, ta.length);
    assertEquals(length * ta.BYTES_PER_ELEMENT, ta.byteLength);
  }

  gsab.grow(20);

  for (let [ta, length] of tas_and_lengths) {
    assertEquals(length, ta.length);
    assertEquals(length * ta.BYTES_PER_ELEMENT, ta.byteLength);
  }

  gsab.grow(40);

  for (let [ta, length] of tas_and_lengths) {
    assertEquals(length, ta.length);
    assertEquals(length * ta.BYTES_PER_ELEMENT, ta.byteLength);
  }
})();

// The previous test with offsets.
(function TypedArrayLengthWhenGrown2() {
  const gsab = CreateGrowableSharedArrayBuffer(20, 40);

  // Create TAs which cover the bytes 8-15.
  let tas_and_lengths = [];
  for (let ctor of ctors) {
    const length = 8 / ctor.BYTES_PER_ELEMENT;
    tas_and_lengths.push([new ctor(gsab, 8, length), length]);
  }

  for (let [ta, length] of tas_and_lengths) {
    assertEquals(length, ta.length);
    assertEquals(length * ta.BYTES_PER_ELEMENT, ta.byteLength);
  }

  gsab.grow(20);

  for (let [ta, length] of tas_and_lengths) {
    assertEquals(length, ta.length);
    assertEquals(length * ta.BYTES_PER_ELEMENT, ta.byteLength);
  }

  gsab.grow(40);

  for (let [ta, length] of tas_and_lengths) {
    assertEquals(length, ta.length);
    assertEquals(length * ta.BYTES_PER_ELEMENT, ta.byteLength);
  }
})();

(function LengthTracking1() {
  const gsab = CreateGrowableSharedArrayBuffer(16, 40);

  let tas = [];
  for (let ctor of ctors) {
    tas.push(new ctor(gsab));
  }

  for (let ta of tas) {
    assertEquals(16 / ta.BYTES_PER_ELEMENT, ta.length);
    assertEquals(16, ta.byteLength);
  }

  gsab.grow(24);
  for (let ta of tas) {
    assertEquals(24 / ta.BYTES_PER_ELEMENT, ta.length);
    assertEquals(24, ta.byteLength);
  }

  // Grow to a number which is not a multiple of all byte_lengths.
  gsab.grow(26);
  for (let ta of tas) {
    const expected_length = Math.floor(26 / ta.BYTES_PER_ELEMENT);
    assertEquals(expected_length, ta.length);
    assertEquals(expected_length * ta.BYTES_PER_ELEMENT, ta.byteLength);
  }

  gsab.grow(40);

  for (let ta of tas) {
    assertEquals(40 / ta.BYTES_PER_ELEMENT, ta.length);
    assertEquals(40, ta.byteLength);
  }
})();

// The previous test with offsets.
(function LengthTracking2() {
  const gsab = CreateGrowableSharedArrayBuffer(16, 40);

  const offset = 8;
  let tas = [];
  for (let ctor of ctors) {
    tas.push(new ctor(gsab, offset));
  }

  for (let ta of tas) {
    assertEquals((16 - offset) / ta.BYTES_PER_ELEMENT, ta.length);
    assertEquals(16 - offset, ta.byteLength);
  }

  gsab.grow(24);
  for (let ta of tas) {
    assertEquals((24 - offset) / ta.BYTES_PER_ELEMENT, ta.length);
    assertEquals(24 - offset, ta.byteLength);
  }

  // Grow to a number which is not a multiple of all byte_lengths.
  gsab.grow(26);
  for (let ta of tas) {
    const expected_length = Math.floor((26 - offset)/ ta.BYTES_PER_ELEMENT);
    assertEquals(expected_length, ta.length);
    assertEquals(expected_length * ta.BYTES_PER_ELEMENT, ta.byteLength);
  }

  gsab.grow(40);

  for (let ta of tas) {
    assertEquals((40 - offset) / ta.BYTES_PER_ELEMENT, ta.length);
    assertEquals(40 - offset, ta.byteLength);
  }
})();

(function LoadWithFeedback() {
  function ReadElement2(ta) {
    return ta[2];
  }
  %EnsureFeedbackVectorForFunction(ReadElement2);

  const gsab = CreateGrowableSharedArrayBuffer(16, 40);

  const i8a = new Int8Array(gsab, 0, 4);
  for (let i = 0; i < 3; ++i) {
    assertEquals(0, ReadElement2(i8a));
  }

  // Within-bounds write
  for (let i = 0; i < 4; ++i) {
    i8a[i] = i;
  }

  // Within-bounds read
  for (let i = 0; i < 3; ++i) {
    assertEquals(2, ReadElement2(i8a));
  }

  gsab.grow(20);

  // Within-bounds read
  for (let i = 0; i < 3; ++i) {
    assertEquals(2, ReadElement2(i8a));
  }

  gsab.grow(40);

  // Within-bounds read
  for (let i = 0; i < 3; ++i) {
    assertEquals(2, ReadElement2(i8a));
  }
})();

(function LoadAndStoreWithFeedback() {
  function ReadElement(ta, i) {
    return ta[i];
  }

  function HasElement(ta, i) {
    return i in ta;
  }

  function WriteElement(ta, i, v) {
    ta[i] = v;
  }

  %EnsureFeedbackVectorForFunction(ReadElement);
  %EnsureFeedbackVectorForFunction(HasElement);
  %EnsureFeedbackVectorForFunction(WriteElement);

  const gsab = CreateGrowableSharedArrayBuffer(16, 40);

  const i8a = new Int8Array(gsab); // length-tracking
  assertEquals(16, i8a.length);

  // Within-bounds read
  for (let i = 0; i < i8a.length; ++i) {
    assertEquals(0, ReadElement(i8a, i));
    assertTrue(HasElement(i8a, i));
  }
  assertFalse(HasElement(i8a, 17));

  // Within-bounds write
  for (let i = 0; i < i8a.length; ++i) {
    WriteElement(i8a, i, i);
  }

  // Within-bounds read
  for (let i = 0; i < i8a.length; ++i) {
    assertEquals(i, ReadElement(i8a, i));
  }

  let old_length = i8a.length;
  gsab.grow(20);
  assertEquals(20, i8a.length);

  for (let i = 0; i < i8a.length; ++i) {
    if (i < old_length) {
      assertEquals(i, ReadElement(i8a, i));
    } else {
      assertEquals(0, ReadElement(i8a, i));
    }
    assertTrue(HasElement(i8a, i));
  }
  assertFalse(HasElement(i8a, 21));

  // Within-bounds write
  for (let i = 0; i < i8a.length; ++i) {
    WriteElement(i8a, i, i + 1);
  }

  // Within-bounds read
  for (let i = 0; i < i8a.length; ++i) {
    assertEquals(i + 1, ReadElement(i8a, i));
  }
})();

(function HasWithOffsetsWithFeedback() {
  function GetElements(ta) {
    let result = '';
    for (let i = 0; i < 8; ++i) {
      result += (i in ta) + ',';
      //           ^ feedback will be here
    }
    return result;
  }
  %EnsureFeedbackVectorForFunction(GetElements);

  const gsab = CreateGrowableSharedArrayBuffer(4, 8);
  const fixedLength = new Int8Array(gsab, 0, 4);
  const fixedLengthWithOffset = new Int8Array(gsab, 1, 3);
  const lengthTracking = new Int8Array(gsab, 0);
  const lengthTrackingWithOffset = new Int8Array(gsab, 1);

  assertEquals('true,true,true,true,false,false,false,false,',
               GetElements(fixedLength));
  assertEquals('true,true,true,false,false,false,false,false,',
              GetElements(fixedLengthWithOffset));
  assertEquals('true,true,true,true,false,false,false,false,',
              GetElements(lengthTracking));
  assertEquals('true,true,true,false,false,false,false,false,',
              GetElements(lengthTrackingWithOffset));

  gsab.grow(8);

  assertEquals('true,true,true,true,false,false,false,false,',
               GetElements(fixedLength));
  assertEquals('true,true,true,false,false,false,false,false,',
               GetElements(fixedLengthWithOffset));
  assertEquals('true,true,true,true,true,true,true,true,',
               GetElements(lengthTracking));
  assertEquals('true,true,true,true,true,true,true,false,',
               GetElements(lengthTrackingWithOffset));
})();

(function EnumerateElements() {
  let gsab = CreateGrowableSharedArrayBuffer(100, 200);
  for (let ctor of ctors) {
    const ta = new ctor(gsab, 0, 3);
    let keys = '';
    for (const key in ta) {
      keys += key;
    }
    assertEquals('012', keys);
  }
}());

(function IterateTypedArray() {
  const no_elements = 10;
  const offset = 2;

  function TestIteration(ta, expected) {
    let values = [];
    for (const value of ta) {
      values.push(Number(value));
    }
    assertEquals(expected, values);
  }

  for (let ctor of ctors) {
    const buffer_byte_length = no_elements * ctor.BYTES_PER_ELEMENT;
    // We can use the same GSAB for all the TAs below, since we won't modify it
    // after writing the initial values.
    const gsab = CreateGrowableSharedArrayBuffer(buffer_byte_length,
                                                 2 * buffer_byte_length);
    const byte_offset = offset * ctor.BYTES_PER_ELEMENT;

    // Write some data into the array.
    let ta_write = new ctor(gsab);
    for (let i = 0; i < no_elements; ++i) {
      WriteToTypedArray(ta_write, i, i % 128);
    }

    // Create various different styles of TypedArrays with the GSAB as the
    // backing store and iterate them.
    const ta = new ctor(gsab, 0, 3);
    TestIteration(ta, [0, 1, 2]);

    const empty_ta = new ctor(gsab, 0, 0);
    TestIteration(empty_ta, []);

    const ta_with_offset = new ctor(gsab, byte_offset, 3);
    TestIteration(ta_with_offset, [2, 3, 4]);

    const empty_ta_with_offset = new ctor(gsab, byte_offset, 0);
    TestIteration(empty_ta_with_offset, []);

    const length_tracking_ta = new ctor(gsab);
    {
      let expected = [];
      for (let i = 0; i < no_elements; ++i) {
        expected.push(i % 128);
      }
      TestIteration(length_tracking_ta, expected);
    }

    const length_tracking_ta_with_offset = new ctor(gsab, byte_offset);
    {
      let expected = [];
      for (let i = offset; i < no_elements; ++i) {
        expected.push(i % 128);
      }
      TestIteration(length_tracking_ta_with_offset, expected);
    }

    const empty_length_tracking_ta_with_offset = new ctor(gsab, buffer_byte_length);
    TestIteration(empty_length_tracking_ta_with_offset, []);
  }
}());

// Helpers for iteration tests.
function CreateGsab(buffer_byte_length, ctor) {
  const gsab = CreateGrowableSharedArrayBuffer(buffer_byte_length,
                                               2 * buffer_byte_length);
  // Write some data into the array.
  let ta_write = new ctor(gsab);
  for (let i = 0; i < buffer_byte_length / ctor.BYTES_PER_ELEMENT; ++i) {
    WriteToTypedArray(ta_write, i, i % 128);
  }
  return gsab;
}

function TestIterationAndGrow(ta, expected, gsab, grow_after,
                              new_byte_length) {
  let values = [];
  let grown = false;
  for (const value of ta) {
    if (value instanceof Array) {
      // When iterating via entries(), the values will be arrays [key, value].
      values.push([value[0], Number(value[1])]);
    } else {
      values.push(Number(value));
    }
    if (!grown && values.length == grow_after) {
      gsab.grow(new_byte_length);
      grown = true;
    }
  }
  assertEquals(expected, values);
  assertTrue(grown);
}

(function IterateTypedArrayAndGrowMidIteration() {
  const no_elements = 10;
  const offset = 2;

  for (let ctor of ctors) {
    const buffer_byte_length = no_elements * ctor.BYTES_PER_ELEMENT;
    const byte_offset = offset * ctor.BYTES_PER_ELEMENT;

    // Create various different styles of TypedArrays with the gsab as the
    // backing store and iterate them.

    // Fixed-length TAs aren't affected by resizing.
    let gsab = CreateGsab(buffer_byte_length, ctor);
    const ta = new ctor(gsab, 0, 3);
    TestIterationAndGrow(ta, [0, 1, 2], gsab, 2, buffer_byte_length * 2);

    gsab = CreateGsab(buffer_byte_length, ctor);
    const ta_with_offset = new ctor(gsab, byte_offset, 3);
    TestIterationAndGrow(ta_with_offset, [2, 3, 4], gsab, 2,
                         buffer_byte_length * 2);

    gsab = CreateGsab(buffer_byte_length, ctor);
    const length_tracking_ta = new ctor(gsab);
    {
      let expected = [];
      for (let i = 0; i < no_elements; ++i) {
        expected.push(i % 128);
      }
      // After resizing, the new memory contains zeros.
      for (let i = 0; i < no_elements; ++i) {
        expected.push(0);
      }

      TestIterationAndGrow(length_tracking_ta, expected, gsab, 2,
                           buffer_byte_length * 2);
    }

    gsab = CreateGsab(buffer_byte_length, ctor);
    const length_tracking_ta_with_offset = new ctor(gsab, byte_offset);
    {
      let expected = [];
      for (let i = offset; i < no_elements; ++i) {
        expected.push(i % 128);
      }
      for (let i = 0; i < no_elements; ++i) {
        expected.push(0);
      }
      TestIterationAndGrow(length_tracking_ta_with_offset, expected, gsab, 2,
                           buffer_byte_length * 2);
    }
  }
}());

(function IterateTypedArrayAndGrowJustBeforeIterationWouldEnd() {
  const no_elements = 10;
  const offset = 2;

  // We need to recreate the gsab between all TA tests, since we grow it.
  for (let ctor of ctors) {
    const buffer_byte_length = no_elements * ctor.BYTES_PER_ELEMENT;
    const byte_offset = offset * ctor.BYTES_PER_ELEMENT;

    // Create various different styles of TypedArrays with the gsab as the
    // backing store and iterate them.

    let gsab = CreateGsab(buffer_byte_length, ctor);
    const length_tracking_ta = new ctor(gsab);
    {
      let expected = [];
      for (let i = 0; i < no_elements; ++i) {
        expected.push(i % 128);
      }
      // After resizing, the new memory contains zeros.
      for (let i = 0; i < no_elements; ++i) {
        expected.push(0);
      }

      TestIterationAndGrow(length_tracking_ta, expected, gsab, no_elements,
                           buffer_byte_length * 2);
    }

    gsab = CreateGsab(buffer_byte_length, ctor);
    const length_tracking_ta_with_offset = new ctor(gsab, byte_offset);
    {
      let expected = [];
      for (let i = offset; i < no_elements; ++i) {
        expected.push(i % 128);
      }
      for (let i = 0; i < no_elements; ++i) {
        expected.push(0);
      }
      TestIterationAndGrow(length_tracking_ta_with_offset, expected, gsab,
                           no_elements - offset, buffer_byte_length * 2);
    }
  }
}());

(function Destructuring() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    let ta_write = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(ta_write, i, i);
    }

    {
      let [a, b, c, d, e] = fixedLength;
      assertEquals([0, 1, 2, 3], ToNumbers([a, b, c, d]));
      assertEquals(undefined, e);
    }

    {
      let [a, b, c] = fixedLengthWithOffset;
      assertEquals([2, 3], ToNumbers([a, b]));
      assertEquals(undefined, c);
    }

    {
      let [a, b, c, d, e] = lengthTracking;
      assertEquals([0, 1, 2, 3], ToNumbers([a, b, c, d]));
      assertEquals(undefined, e);
    }

    {
      let [a, b, c] = lengthTrackingWithOffset;
      assertEquals([2, 3], ToNumbers([a, b]));
      assertEquals(undefined, c);
    }

    // Grow. The new memory is zeroed.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);

    {
      let [a, b, c, d, e] = fixedLength;
      assertEquals([0, 1, 2, 3], ToNumbers([a, b, c, d]));
      assertEquals(undefined, e);
    }

    {
      let [a, b, c] = fixedLengthWithOffset;
      assertEquals([2, 3], ToNumbers([a, b]));
      assertEquals(undefined, c);
    }

    {
      let [a, b, c, d, e, f, g] = lengthTracking;
      assertEquals([0, 1, 2, 3, 0, 0], ToNumbers([a, b, c, d, e, f]));
      assertEquals(undefined, g);
    }

    {
      let [a, b, c, d, e] = lengthTrackingWithOffset;
      assertEquals([2, 3, 0, 0], ToNumbers([a, b, c, d]));
      assertEquals(undefined, e);
    }
  }
}());

function TestFill(helper) {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    assertEquals([0, 0, 0, 0], ReadDataFromBuffer(gsab, ctor));

    helper(fixedLength, 1);
    assertEquals([1, 1, 1, 1], ReadDataFromBuffer(gsab, ctor));

    helper(fixedLengthWithOffset, 2);
    assertEquals([1, 1, 2, 2], ReadDataFromBuffer(gsab, ctor));

    helper(lengthTracking, 3);
    assertEquals([3, 3, 3, 3], ReadDataFromBuffer(gsab, ctor));

    helper(lengthTrackingWithOffset, 4);
    assertEquals([3, 3, 4, 4], ReadDataFromBuffer(gsab, ctor));

    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);

    helper(fixedLength, 13);
    assertEquals([13, 13, 13, 13, 0, 0], ReadDataFromBuffer(gsab, ctor));

    helper(fixedLengthWithOffset, 14);
    assertEquals([13, 13, 14, 14, 0, 0], ReadDataFromBuffer(gsab, ctor));

    helper(lengthTracking, 15);
    assertEquals([15, 15, 15, 15, 15, 15], ReadDataFromBuffer(gsab, ctor));

    helper(lengthTrackingWithOffset, 16);
    assertEquals([15, 15, 16, 16, 16, 16], ReadDataFromBuffer(gsab, ctor));

    // Filling with non-undefined start & end.
    helper(fixedLength, 17, 1, 3);
    assertEquals([15, 17, 17, 16, 16, 16], ReadDataFromBuffer(gsab, ctor));

    helper(fixedLengthWithOffset, 18, 1, 2);
    assertEquals([15, 17, 17, 18, 16, 16], ReadDataFromBuffer(gsab, ctor));

    helper(lengthTracking, 19, 1, 3);
    assertEquals([15, 19, 19, 18, 16, 16], ReadDataFromBuffer(gsab, ctor));

    helper(lengthTrackingWithOffset, 20, 1, 2);
    assertEquals([15, 19, 19, 20, 16, 16], ReadDataFromBuffer(gsab, ctor));
  }
}
TestFill(TypedArrayFillHelper);
TestFill(ArrayFillHelper);

function At(atHelper) {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    let ta_write = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(ta_write, i, i);
    }

    assertEquals(3, atHelper(fixedLength, -1));
    assertEquals(3, atHelper(lengthTracking, -1));
    assertEquals(3, atHelper(fixedLengthWithOffset, -1));
    assertEquals(3, atHelper(lengthTrackingWithOffset, -1));

    // Grow. New memory is zeroed.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals(3, atHelper(fixedLength, -1));
    assertEquals(0, atHelper(lengthTracking, -1));
    assertEquals(3, atHelper(fixedLengthWithOffset, -1));
    assertEquals(0, atHelper(lengthTrackingWithOffset, -1));
  }
}
At(TypedArrayAtHelper);
At(ArrayAtHelper);

// The corresponding tests for Array.prototype.slice are in
// typedarray-growablesharedarraybuffer-array-methods.js.
(function Slice() {
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

    const fixedLengthSlice = fixedLength.slice();
    assertEquals([0, 1, 2, 3], ToNumbers(fixedLengthSlice));
    assertTrue(fixedLengthSlice.buffer instanceof ArrayBuffer);
    assertFalse(fixedLengthSlice.buffer instanceof SharedArrayBuffer);
    assertFalse(fixedLengthSlice.buffer.resizable);

    const fixedLengthWithOffsetSlice = fixedLengthWithOffset.slice();
    assertEquals([2, 3], ToNumbers(fixedLengthWithOffsetSlice));
    assertTrue(fixedLengthWithOffsetSlice.buffer instanceof ArrayBuffer);
    assertFalse(fixedLengthWithOffsetSlice.buffer instanceof SharedArrayBuffer);
    assertFalse(fixedLengthWithOffsetSlice.buffer.resizable);

    const lengthTrackingSlice = lengthTracking.slice();
    assertEquals([0, 1, 2, 3], ToNumbers(lengthTrackingSlice));
    assertTrue(lengthTrackingSlice.buffer instanceof ArrayBuffer);
    assertFalse(lengthTrackingSlice.buffer instanceof SharedArrayBuffer);
    assertFalse(lengthTrackingSlice.buffer.resizable);

    const lengthTrackingWithOffsetSlice = lengthTrackingWithOffset.slice();
    assertEquals([2, 3], ToNumbers(lengthTrackingWithOffsetSlice));
    assertTrue(lengthTrackingWithOffsetSlice.buffer instanceof ArrayBuffer);
    assertFalse(lengthTrackingWithOffsetSlice.buffer instanceof
        SharedArrayBuffer);
    assertFalse(lengthTrackingWithOffsetSlice.buffer.resizable);

    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals([0, 1, 2, 3], ToNumbers(fixedLength.slice()));
    assertEquals([2, 3], ToNumbers(fixedLengthWithOffset.slice()));
    assertEquals([0, 1, 2, 3, 0, 0], ToNumbers(lengthTracking.slice()));
    assertEquals([2, 3, 0, 0], ToNumbers(lengthTrackingWithOffset.slice()));

    // Verify that the previously created slices aren't affected by the growing.
    assertEquals([0, 1, 2, 3], ToNumbers(fixedLengthSlice));
    assertEquals([2, 3], ToNumbers(fixedLengthWithOffsetSlice));
    assertEquals([0, 1, 2, 3], ToNumbers(lengthTrackingSlice));
    assertEquals([2, 3], ToNumbers(lengthTrackingWithOffsetSlice));
  }
})();

function SliceParameterConversionGrows(sliceHelper) {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i + 1);
    }
    const evil = { valueOf: () => { gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
                                    return 0; }};
    assertEquals([1, 2, 3, 4], ToNumbers(sliceHelper(lengthTracking, evil)));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, gsab.byteLength);
  }
}
SliceParameterConversionGrows(TypedArraySliceHelper);
SliceParameterConversionGrows(ArraySliceHelper);

// The corresponding test for Array.prototype.slice is not possible, since it
// doesn't call the species constructor if the "original array" is not an Array.
(function SliceSpeciesCreateResizes() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);

    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 1);
    }

    let resizeWhenConstructorCalled = false;
    class MyArray extends ctor {
      constructor(...params) {
        super(...params);
        if (resizeWhenConstructorCalled) {
          gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
        }
      }
    };

    const fixedLength = new MyArray(gsab, 0, 4);
    resizeWhenConstructorCalled = true;
    const a = fixedLength.slice();
    assertEquals(4, a.length);
    assertEquals([1, 1, 1, 1], ToNumbers(a));

    assertEquals(6 * ctor.BYTES_PER_ELEMENT, gsab.byteLength);
  }

  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);

    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 1);
    }

    let resizeWhenConstructorCalled = false;
    class MyArray extends ctor {
      constructor(...params) {
        super(...params);
        if (resizeWhenConstructorCalled) {
          gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
        }
      }
    };

    const lengthTracking = new MyArray(gsab);
    resizeWhenConstructorCalled = true;
    const a = lengthTracking.slice();
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, gsab.byteLength);
    // The length of the resulting TypedArray is determined before
    // TypedArraySpeciesCreate is called, and it doesn't change.
    assertEquals(4, a.length);
    assertEquals([1, 1, 1, 1], ToNumbers(a));
  }
})();

function TestCopyWithin(helper) {
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

    // Orig. array: [0, 1, 2, 3]
    //              [0, 1, 2, 3] << fixedLength
    //                    [2, 3] << fixedLengthWithOffset
    //              [0, 1, 2, 3, ...] << lengthTracking
    //                    [2, 3, ...] << lengthTrackingWithOffset

    helper(fixedLength, 0, 2);
    assertEquals([2, 3, 2, 3], ToNumbers(fixedLength));

    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    helper(fixedLengthWithOffset, 0, 1);
    assertEquals([3, 3], ToNumbers(fixedLengthWithOffset));

    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    helper(lengthTracking, 0, 2);
    assertEquals([2, 3, 2, 3], ToNumbers(lengthTracking));

    helper(lengthTrackingWithOffset, 0, 1);
    assertEquals([3, 3], ToNumbers(lengthTrackingWithOffset));

    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    // Orig. array: [0, 1, 2, 3, 4, 5]
    //              [0, 1, 2, 3] << fixedLength
    //                    [2, 3] << fixedLengthWithOffset
    //              [0, 1, 2, 3, 4, 5, ...] << lengthTracking
    //                    [2, 3, 4, 5, ...] << lengthTrackingWithOffset

    helper(fixedLength, 0, 2);
    assertEquals([2, 3, 2, 3], ToNumbers(fixedLength));

    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    helper(fixedLengthWithOffset, 0, 1);
    assertEquals([3, 3], ToNumbers(fixedLengthWithOffset));

    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    //              [0, 1, 2, 3, 4, 5, ...] << lengthTracking
    //        target ^     ^ start
    helper(lengthTracking, 0, 2);
    assertEquals([2, 3, 4, 5, 4, 5], ToNumbers(lengthTracking));

    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    //                    [2, 3, 4, 5, ...] << lengthTrackingWithOffset
    //              target ^  ^ start
    helper(lengthTrackingWithOffset, 0, 1);
    assertEquals([3, 4, 5, 5], ToNumbers(lengthTrackingWithOffset));
  }
}
TestCopyWithin(TypedArrayCopyWithinHelper);
TestCopyWithin(ArrayCopyWithinHelper);

function CopyWithinParameterConversionGrows(helper) {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, i);
    }

    const evil = { valueOf: () => { gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
                                    WriteToTypedArray(lengthTracking, 4, 4);
                                    WriteToTypedArray(lengthTracking, 5, 5);
                                    return 0;} };
    // Orig. array: [0, 1, 2, 3]  [4, 5]
    //               ^     ^       ^ new elements
    //          target     start
    helper(lengthTracking, evil, 2);
    // Only elements up to the original length are copied.
    assertEquals([2, 3, 2, 3, 4, 5], ToNumbers(lengthTracking));
  }
}
CopyWithinParameterConversionGrows(TypedArrayCopyWithinHelper);
CopyWithinParameterConversionGrows(ArrayCopyWithinHelper);

function EntriesKeysValues(keysHelper, valuesFromEntries, valuesFromValues) {
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
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    assertEquals([0, 2, 4, 6], valuesFromEntries(fixedLength));
    assertEquals([0, 2, 4, 6], valuesFromValues(fixedLength));
    assertEquals([0, 1, 2, 3], Array.from(keysHelper(fixedLength)));

    assertEquals([4, 6], valuesFromEntries(fixedLengthWithOffset));
    assertEquals([4, 6], valuesFromValues(fixedLengthWithOffset));
    assertEquals([0, 1], Array.from(keysHelper(fixedLengthWithOffset)));

    assertEquals([0, 2, 4, 6], valuesFromEntries(lengthTracking));
    assertEquals([0, 2, 4, 6], valuesFromValues(lengthTracking));
    assertEquals([0, 1, 2, 3], Array.from(keysHelper(lengthTracking)));

    assertEquals([4, 6], valuesFromEntries(lengthTrackingWithOffset));
    assertEquals([4, 6], valuesFromValues(lengthTrackingWithOffset));
    assertEquals([0, 1], Array.from(keysHelper(lengthTrackingWithOffset)));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset

    assertEquals([0, 2, 4, 6], valuesFromEntries(fixedLength));
    assertEquals([0, 2, 4, 6], valuesFromValues(fixedLength));
    assertEquals([0, 1, 2, 3], Array.from(keysHelper(fixedLength)));

    assertEquals([4, 6], valuesFromEntries(fixedLengthWithOffset));
    assertEquals([4, 6], valuesFromValues(fixedLengthWithOffset));
    assertEquals([0, 1], Array.from(keysHelper(fixedLengthWithOffset)));

    assertEquals([0, 2, 4, 6, 8, 10], valuesFromEntries(lengthTracking));
    assertEquals([0, 2, 4, 6, 8, 10], valuesFromValues(lengthTracking));
    assertEquals([0, 1, 2, 3, 4, 5], Array.from(keysHelper(lengthTracking)));

    assertEquals([4, 6, 8, 10], valuesFromEntries(lengthTrackingWithOffset));
    assertEquals([4, 6, 8, 10], valuesFromValues(lengthTrackingWithOffset));
    assertEquals([0, 1, 2, 3],
                 Array.from(keysHelper(lengthTrackingWithOffset)));
  }
}
EntriesKeysValues(
    TypedArrayKeysHelper, ValuesFromTypedArrayEntries,
    ValuesFromTypedArrayValues);
EntriesKeysValues(
    ArrayKeysHelper, ValuesFromArrayEntries, ValuesFromArrayValues);

function EntriesKeysValuesGrowMidIteration(
  entriesHelper, keysHelper, valuesHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  // Iterating with entries() (the 4 loops below).
  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);

    // The fixed length array is not affected by resizing.
    TestIterationAndGrow(entriesHelper(fixedLength),
                         [[0, 0], [1, 2], [2, 4], [3, 6]],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);

    // The fixed length array is not affected by resizing.
    TestIterationAndGrow(entriesHelper(fixedLengthWithOffset),
                         [[0, 4], [1, 6]],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);

    TestIterationAndGrow(entriesHelper(lengthTracking),
                         [[0, 0], [1, 2], [2, 4], [3, 6], [4, 0], [5, 0]],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    TestIterationAndGrow(entriesHelper(lengthTrackingWithOffset),
                         [[0, 4], [1, 6], [2, 0], [3, 0]],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  // Iterating with keys() (the 4 loops below).
  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);

    // The fixed length array is not affected by resizing.
    TestIterationAndGrow(keysHelper(fixedLength),
                         [0, 1, 2, 3],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);

    // The fixed length array is not affected by resizing.
    TestIterationAndGrow(keysHelper(fixedLengthWithOffset),
                         [0, 1],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);

    TestIterationAndGrow(keysHelper(lengthTracking),
                         [0, 1, 2, 3, 4, 5],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    TestIterationAndGrow(keysHelper(lengthTrackingWithOffset),
                         [0, 1, 2, 3],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  // Iterating with values() (the 4 loops below).
  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);

    // The fixed length array is not affected by resizing.
    TestIterationAndGrow(valuesHelper(fixedLength),
                         [0, 2, 4, 6],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);

    // The fixed length array is not affected by resizing.
    TestIterationAndGrow(valuesHelper(fixedLengthWithOffset),
                         [4, 6],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);

    TestIterationAndGrow(valuesHelper(lengthTracking),
                         [0, 2, 4, 6, 0, 0],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }

  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    TestIterationAndGrow(valuesHelper(lengthTrackingWithOffset),
                         [4, 6, 0, 0],
                         gsab, 2, 6 * ctor.BYTES_PER_ELEMENT);
  }
}
EntriesKeysValuesGrowMidIteration(
  TypedArrayEntriesHelper, TypedArrayKeysHelper, TypedArrayValuesHelper);
EntriesKeysValuesGrowMidIteration(
  ArrayEntriesHelper, ArrayKeysHelper, ArrayValuesHelper);

function EverySome(everyHelper, someHelper) {
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
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    function div3(n) {
      return Number(n) % 3 == 0;
    }

    function even(n) {
      return Number(n) % 2 == 0;
    }

    function over10(n) {
      return Number(n) > 10;
    }

    assertFalse(everyHelper(fixedLength, div3));
    assertTrue(everyHelper(fixedLength, even));
    assertTrue(someHelper(fixedLength, div3));
    assertFalse(someHelper(fixedLength, over10));

    assertFalse(everyHelper(fixedLengthWithOffset, div3));
    assertTrue(everyHelper(fixedLengthWithOffset, even));
    assertTrue(someHelper(fixedLengthWithOffset, div3));
    assertFalse(someHelper(fixedLengthWithOffset, over10));

    assertFalse(everyHelper(lengthTracking, div3));
    assertTrue(everyHelper(lengthTracking, even));
    assertTrue(someHelper(lengthTracking, div3));
    assertFalse(someHelper(lengthTracking, over10));

    assertFalse(everyHelper(lengthTrackingWithOffset, div3));
    assertTrue(everyHelper(lengthTrackingWithOffset, even));
    assertTrue(someHelper(lengthTrackingWithOffset, div3));
    assertFalse(someHelper(lengthTrackingWithOffset, over10));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset

    assertFalse(everyHelper(fixedLength, div3));
    assertTrue(everyHelper(fixedLength, even));
    assertTrue(someHelper(fixedLength, div3));
    assertFalse(someHelper(fixedLength, over10));

    assertFalse(everyHelper(fixedLengthWithOffset, div3));
    assertTrue(everyHelper(fixedLengthWithOffset, even));
    assertTrue(someHelper(fixedLengthWithOffset, div3));
    assertFalse(someHelper(fixedLengthWithOffset, over10));

    assertFalse(everyHelper(lengthTracking, div3));
    assertTrue(everyHelper(lengthTracking, even));
    assertTrue(someHelper(lengthTracking, div3));
    assertFalse(someHelper(lengthTracking, over10));

    assertFalse(everyHelper(lengthTrackingWithOffset, div3));
    assertTrue(everyHelper(lengthTrackingWithOffset, even));
    assertTrue(someHelper(lengthTrackingWithOffset, div3));
    assertFalse(someHelper(lengthTrackingWithOffset, over10));
  }
}
EverySome(TypedArrayEveryHelper, TypedArraySomeHelper);
EverySome(ArrayEveryHelper, ArraySomeHelper);

function EveryGrowMidIteration(everyHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndGrow(n) {
    if (n == undefined) {
      values.push(n);
    } else {
      values.push(Number(n));
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return true;
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertTrue(everyHelper(fixedLength, CollectValuesAndGrow));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertTrue(everyHelper(fixedLengthWithOffset, CollectValuesAndGrow));
    assertEquals([4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertTrue(everyHelper(lengthTracking, CollectValuesAndGrow));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertTrue(everyHelper(lengthTrackingWithOffset, CollectValuesAndGrow));
    assertEquals([4, 6], values);
  }
}
EveryGrowMidIteration(TypedArrayEveryHelper);
EveryGrowMidIteration(ArrayEveryHelper);

function SomeGrowMidIteration(someHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndGrow(n) {
    if (n == undefined) {
      values.push(n);
    } else {
      values.push(Number(n));
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return false;
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertFalse(someHelper(fixedLength, CollectValuesAndGrow));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    gsab = gsab;
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertFalse(someHelper(fixedLengthWithOffset, CollectValuesAndGrow));
    assertEquals([4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertFalse(someHelper(lengthTracking, CollectValuesAndGrow));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertFalse(someHelper(lengthTrackingWithOffset, CollectValuesAndGrow));
    assertEquals([4, 6], values);
  }
}
SomeGrowMidIteration(TypedArraySomeHelper);
SomeGrowMidIteration(ArraySomeHelper);

function FindFindIndexFindLastFindLastIndex(
  findHelper, findIndexHelper, findLastHelper, findLastIndexHelper) {
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
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    function isTwoOrFour(n) {
      return n == 2 || n == 4;
    }

    assertEquals(2, Number(findHelper(fixedLength, isTwoOrFour)));
    assertEquals(4, Number(findHelper(fixedLengthWithOffset, isTwoOrFour)));
    assertEquals(2, Number(findHelper(lengthTracking, isTwoOrFour)));
    assertEquals(4, Number(findHelper(lengthTrackingWithOffset, isTwoOrFour)));

    assertEquals(1, findIndexHelper(fixedLength, isTwoOrFour));
    assertEquals(0, findIndexHelper(fixedLengthWithOffset, isTwoOrFour));
    assertEquals(1, findIndexHelper(lengthTracking, isTwoOrFour));
    assertEquals(0, findIndexHelper(lengthTrackingWithOffset, isTwoOrFour));

    assertEquals(4, Number(findLastHelper(fixedLength, isTwoOrFour)));
    assertEquals(4, Number(findLastHelper(fixedLengthWithOffset, isTwoOrFour)));
    assertEquals(4, Number(findLastHelper(lengthTracking, isTwoOrFour)));
    assertEquals(4,
                 Number(findLastHelper(lengthTrackingWithOffset, isTwoOrFour)));

    assertEquals(2, findLastIndexHelper(fixedLength, isTwoOrFour));
    assertEquals(0, findLastIndexHelper(fixedLengthWithOffset, isTwoOrFour));
    assertEquals(2, findLastIndexHelper(lengthTracking, isTwoOrFour));
    assertEquals(0, findLastIndexHelper(lengthTrackingWithOffset, isTwoOrFour));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 0);
    }
    WriteToTypedArray(taWrite, 4, 2);
    WriteToTypedArray(taWrite, 5, 4);

    // Orig. array: [0, 0, 0, 0, 2, 4]
    //              [0, 0, 0, 0] << fixedLength
    //                    [0, 0] << fixedLengthWithOffset
    //              [0, 0, 0, 0, 2, 4, ...] << lengthTracking
    //                    [0, 0, 2, 4, ...] << lengthTrackingWithOffset

    assertEquals(undefined, findHelper(fixedLength, isTwoOrFour));
    assertEquals(undefined, findHelper(fixedLengthWithOffset, isTwoOrFour));
    assertEquals(2, Number(findHelper(lengthTracking, isTwoOrFour)));
    assertEquals(2, Number(findHelper(lengthTrackingWithOffset, isTwoOrFour)));

    assertEquals(-1, findIndexHelper(fixedLength, isTwoOrFour));
    assertEquals(-1, findIndexHelper(fixedLengthWithOffset, isTwoOrFour));
    assertEquals(4, findIndexHelper(lengthTracking, isTwoOrFour));
    assertEquals(2, findIndexHelper(lengthTrackingWithOffset, isTwoOrFour));

    assertEquals(undefined, findLastHelper(fixedLength, isTwoOrFour));
    assertEquals(undefined, findLastHelper(fixedLengthWithOffset, isTwoOrFour));
    assertEquals(4, Number(findLastHelper(lengthTracking, isTwoOrFour)));
    assertEquals(4,
                 Number(findLastHelper(lengthTrackingWithOffset, isTwoOrFour)));

    assertEquals(-1, findLastIndexHelper(fixedLength, isTwoOrFour));
    assertEquals(-1, findLastIndexHelper(fixedLengthWithOffset, isTwoOrFour));
    assertEquals(5, findLastIndexHelper(lengthTracking, isTwoOrFour));
    assertEquals(3, findLastIndexHelper(lengthTrackingWithOffset, isTwoOrFour));
  }
}
FindFindIndexFindLastFindLastIndex(
    TypedArrayFindHelper, TypedArrayFindIndexHelper, TypedArrayFindLastHelper,
    TypedArrayFindLastIndexHelper);
FindFindIndexFindLastFindLastIndex(
    ArrayFindHelper, ArrayFindIndexHelper, ArrayFindLastHelper,
    ArrayFindLastIndexHelper);

function FindGrowMidIteration(findHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndGrow(n) {
    if (n == undefined) {
      values.push(n);
    } else {
      values.push(Number(n));
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return false;
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(undefined, findHelper(fixedLength, CollectValuesAndGrow));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(undefined,
                 findHelper(fixedLengthWithOffset, CollectValuesAndGrow));
    assertEquals([4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(undefined, findHelper(lengthTracking, CollectValuesAndGrow));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(undefined,
                 findHelper(lengthTrackingWithOffset, CollectValuesAndGrow));
    assertEquals([4, 6], values);
  }
}
FindGrowMidIteration(TypedArrayFindHelper);
FindGrowMidIteration(ArrayFindHelper);

function FindIndexGrowMidIteration(findIndexHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndGrow(n) {
    if (n == undefined) {
      values.push(n);
    } else {
      values.push(Number(n));
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return false;
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(-1, findIndexHelper(fixedLength, CollectValuesAndGrow));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(-1,
                 findIndexHelper(fixedLengthWithOffset, CollectValuesAndGrow));
    assertEquals([4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(-1, findIndexHelper(lengthTracking, CollectValuesAndGrow));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(-1,
        findIndexHelper(lengthTrackingWithOffset, CollectValuesAndGrow));
    assertEquals([4, 6], values);
  }
}
FindIndexGrowMidIteration(TypedArrayFindIndexHelper);
FindIndexGrowMidIteration(ArrayFindIndexHelper);

function FindLastGrowMidIteration(findLastHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndGrow(n) {
    if (n == undefined) {
      values.push(n);
    } else {
      values.push(Number(n));
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return false;
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(undefined, findLastHelper(fixedLength, CollectValuesAndGrow));
    assertEquals([6, 4, 2, 0], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(undefined,
                 findLastHelper(fixedLengthWithOffset, CollectValuesAndGrow));
    assertEquals([6, 4], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(undefined,
                 findLastHelper(lengthTracking, CollectValuesAndGrow));
    assertEquals([6, 4, 2, 0], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(undefined,
      findLastHelper(lengthTrackingWithOffset, CollectValuesAndGrow));
    assertEquals([6, 4], values);
  }
}
FindLastGrowMidIteration(TypedArrayFindLastHelper);
FindLastGrowMidIteration(ArrayFindLastHelper);

function FindLastIndexGrowMidIteration(findLastIndexHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndGrow(n) {
    if (n == undefined) {
      values.push(n);
    } else {
      values.push(Number(n));
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return false;
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(-1, findLastIndexHelper(fixedLength, CollectValuesAndGrow));
    assertEquals([6, 4, 2, 0], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(-1,
        findLastIndexHelper(fixedLengthWithOffset, CollectValuesAndGrow));
    assertEquals([6, 4], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(-1,
                 findLastIndexHelper(lengthTracking, CollectValuesAndGrow));
    assertEquals([6, 4, 2, 0], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals(-1,
        findLastIndexHelper(lengthTrackingWithOffset, CollectValuesAndGrow));
    assertEquals([6, 4], values);
  }
}
FindLastIndexGrowMidIteration(TypedArrayFindLastIndexHelper);
FindLastIndexGrowMidIteration(ArrayFindLastIndexHelper);

function Filter(filterHelper) {
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

    // Orig. array: [0, 1, 2, 3]
    //              [0, 1, 2, 3] << fixedLength
    //                    [2, 3] << fixedLengthWithOffset
    //              [0, 1, 2, 3, ...] << lengthTracking
    //                    [2, 3, ...] << lengthTrackingWithOffset

    function isEven(n) {
      return n != undefined && Number(n) % 2 == 0;
    }

    assertEquals([0, 2], ToNumbers(filterHelper(fixedLength, isEven)));
    assertEquals([2], ToNumbers(filterHelper(fixedLengthWithOffset, isEven)));
    assertEquals([0, 2], ToNumbers(filterHelper(lengthTracking, isEven)));
    assertEquals([2],
        ToNumbers(filterHelper(lengthTrackingWithOffset, isEven)));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    // Orig. array: [0, 1, 2, 3, 4, 5]
    //              [0, 1, 2, 3] << fixedLength
    //                    [2, 3] << fixedLengthWithOffset
    //              [0, 1, 2, 3, 4, 5, ...] << lengthTracking
    //                    [2, 3, 4, 5, ...] << lengthTrackingWithOffset

    assertEquals([0, 2], ToNumbers(filterHelper(fixedLength, isEven)));
    assertEquals([2], ToNumbers(filterHelper(fixedLengthWithOffset, isEven)));
    assertEquals([0, 2, 4], ToNumbers(filterHelper(lengthTracking, isEven)));
    assertEquals([2, 4],
        ToNumbers(filterHelper(lengthTrackingWithOffset, isEven)));
  }
}
Filter(TypedArrayFilterHelper);
Filter(ArrayFilterHelper);

function FilterGrowMidIteration(filterHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndGrow(n) {
    if (n == undefined) {
      values.push(n);
    } else {
      values.push(Number(n));
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return false;
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([],
        ToNumbers(filterHelper(fixedLength, CollectValuesAndGrow)));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([],
        ToNumbers(filterHelper(fixedLengthWithOffset, CollectValuesAndGrow)));
    assertEquals([4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    values = [];
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([],
        ToNumbers(filterHelper(lengthTracking, CollectValuesAndGrow)));
    assertEquals([0, 2, 4, 6], values);
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    values = [];
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([],
        ToNumbers(filterHelper(lengthTrackingWithOffset, CollectValuesAndGrow)));
    assertEquals([4, 6], values);
  }
}
FilterGrowMidIteration(TypedArrayFilterHelper);
FilterGrowMidIteration(ArrayFilterHelper);

function ForEachReduceReduceRight(
    forEachHelper, reduceHelper, reduceRightHelper) {
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
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    function Helper(array) {
      const forEachValues = [];
      const reduceValues = [];
      const reduceRightValues = [];

      forEachHelper(array, (n) => { forEachValues.push(n);});

      reduceHelper(array, (acc, n) => {
        reduceValues.push(n);
      }, "initial value");

      reduceRightHelper(array, (acc, n) => {
        reduceRightValues.push(n);
      }, "initial value");

      assertEquals(reduceValues, forEachValues);
      reduceRightValues.reverse();
      assertEquals(reduceValues, reduceRightValues);
      return ToNumbers(forEachValues);
    }

    assertEquals([0, 2, 4, 6], Helper(fixedLength));
    assertEquals([4, 6], Helper(fixedLengthWithOffset));
    assertEquals([0, 2, 4, 6], Helper(lengthTracking));
    assertEquals([4, 6], Helper(lengthTrackingWithOffset));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset

    assertEquals([0, 2, 4, 6], Helper(fixedLength));
    assertEquals([4, 6], Helper(fixedLengthWithOffset));
    assertEquals([0, 2, 4, 6, 8, 10], Helper(lengthTracking));
    assertEquals([4, 6, 8, 10], Helper(lengthTrackingWithOffset));
  }
}
ForEachReduceReduceRight(TypedArrayForEachHelper, TypedArrayReduceHelper,
                         TypedArrayReduceRightHelper);
ForEachReduceReduceRight(ArrayForEachHelper, ArrayReduceHelper,
                         ArrayReduceRightHelper);

function ForEachReduceReduceRightGrowMidIteration(
    forEachHelper, reduceHelper, reduceRightHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndResize(n) {
    if (typeof n == 'bigint') {
      values.push(Number(n));
    } else {
      values.push(n);
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return true;
  }

  function ForEachHelper(array) {
    values = [];
    forEachHelper(array, CollectValuesAndResize);
    return values;
  }

  function ReduceHelper(array) {
    values = [];
    reduceHelper(array, (acc, n) => { CollectValuesAndResize(n); },
                 "initial value");
    return values;
  }

  function ReduceRightHelper(array) {
    values = [];
    reduceRightHelper(array, (acc, n) => { CollectValuesAndResize(n); },
                      "initial value");
    return values;
  }

  // Test for forEach.

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2, 4, 6], ForEachHelper(fixedLength));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4, 6], ForEachHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2, 4, 6], ForEachHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4, 6], ForEachHelper(lengthTrackingWithOffset));
  }

  // Test for reduce.

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2, 4, 6], ReduceHelper(fixedLength));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4, 6], ReduceHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2, 4, 6], ReduceHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4, 6], ReduceHelper(lengthTrackingWithOffset));
  }

  // Test for reduceRight.

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([6, 4, 2, 0], ReduceRightHelper(fixedLength));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([6, 4], ReduceRightHelper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([6, 4, 2, 0], ReduceRightHelper(lengthTracking));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([6, 4], ReduceRightHelper(lengthTrackingWithOffset));
  }
}
ForEachReduceReduceRightGrowMidIteration(TypedArrayForEachHelper,
    TypedArrayReduceHelper, TypedArrayReduceRightHelper);
ForEachReduceReduceRightGrowMidIteration(ArrayForEachHelper,
    ArrayReduceHelper, ArrayReduceRightHelper);

function Includes(helper) {
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
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    assertTrue(helper(fixedLength, 2));
    assertFalse(helper(fixedLength, undefined));
    assertTrue(helper(fixedLength, 2, 1));
    assertFalse(helper(fixedLength, 2, 2));
    assertTrue(helper(fixedLength, 2, -3));
    assertFalse(helper(fixedLength, 2, -2));

    assertFalse(helper(fixedLengthWithOffset, 2));
    assertTrue(helper(fixedLengthWithOffset, 4));
    assertFalse(helper(fixedLengthWithOffset, undefined));
    assertTrue(helper(fixedLengthWithOffset, 4, 0));
    assertFalse(helper(fixedLengthWithOffset, 4, 1));
    assertTrue(helper(fixedLengthWithOffset, 4, -2));
    assertFalse(helper(fixedLengthWithOffset, 4, -1));

    assertTrue(helper(lengthTracking, 2));
    assertFalse(helper(lengthTracking, undefined));
    assertTrue(helper(lengthTracking, 2, 1));
    assertFalse(helper(lengthTracking, 2, 2));
    assertTrue(helper(lengthTracking, 2, -3));
    assertFalse(helper(lengthTracking, 2, -2));

    assertFalse(helper(lengthTrackingWithOffset, 2));
    assertTrue(helper(lengthTrackingWithOffset, 4));
    assertFalse(helper(lengthTrackingWithOffset, undefined));
    assertTrue(helper(lengthTrackingWithOffset, 4, 0));
    assertFalse(helper(lengthTrackingWithOffset, 4, 1));
    assertTrue(helper(lengthTrackingWithOffset, 4, -2));
    assertFalse(helper(lengthTrackingWithOffset, 4, -1));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset

    assertTrue(helper(fixedLength, 2));
    assertFalse(helper(fixedLength, undefined));
    assertFalse(helper(fixedLength, 8));

    assertFalse(helper(fixedLengthWithOffset, 2));
    assertTrue(helper(fixedLengthWithOffset, 4));
    assertFalse(helper(fixedLengthWithOffset, undefined));
    assertFalse(helper(fixedLengthWithOffset, 8));

    assertTrue(helper(lengthTracking, 2));
    assertFalse(helper(lengthTracking, undefined));
    assertTrue(helper(lengthTracking, 8));

    assertFalse(helper(lengthTrackingWithOffset, 2));
    assertTrue(helper(lengthTrackingWithOffset, 4));
    assertFalse(helper(lengthTrackingWithOffset, undefined));
    assertTrue(helper(lengthTrackingWithOffset, 8));
  }
}
Includes(TypedArrayIncludesHelper);
Includes(ArrayIncludesHelper);

function IncludesParameterConversionGrows(helper) {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, 1);
    }

    let evil = { valueOf: () => {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return 0;
    }};
    assertFalse(helper(lengthTracking, 0));
    // The TA grew but we only look at the data until the original length.
    assertFalse(helper(lengthTracking, 0, evil));
  }

  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    WriteToTypedArray(lengthTracking, 0, 1);

    let evil = { valueOf: () => {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return -4;
    }};
    assertTrue(helper(lengthTracking, 1, -4));
    // The TA grew but the start index conversion is done based on the original
    // length.
    assertTrue(helper(lengthTracking, 1, evil));
  }
}
IncludesParameterConversionGrows(TypedArrayIncludesHelper);
IncludesParameterConversionGrows(ArrayIncludesHelper);

(function IncludesSpecialValues() {
  for (let ctor of floatCtors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    lengthTracking[0] = -Infinity;
    lengthTracking[1] = Infinity;
    lengthTracking[2] = NaN;
    assertTrue(lengthTracking.includes(-Infinity));
    assertTrue(lengthTracking.includes(Infinity));
    assertTrue(lengthTracking.includes(NaN));
  }
})();

function IndexOfLastIndexOf(indexOfHelper, lastIndexOfHelper) {
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
      WriteToTypedArray(taWrite, i, Math.floor(i / 2));
    }

    // Orig. array: [0, 0, 1, 1]
    //              [0, 0, 1, 1] << fixedLength
    //                    [1, 1] << fixedLengthWithOffset
    //              [0, 0, 1, 1, ...] << lengthTracking
    //                    [1, 1, ...] << lengthTrackingWithOffset

    assertEquals(0, indexOfHelper(fixedLength, 0));
    assertEquals(1, indexOfHelper(fixedLength, 0, 1));
    assertEquals(-1, indexOfHelper(fixedLength, 0, 2));
    assertEquals(-1, indexOfHelper(fixedLength, 0, -2));
    assertEquals(1, indexOfHelper(fixedLength, 0, -3));
    assertEquals(2, indexOfHelper(fixedLength, 1, 1));
    assertEquals(2, indexOfHelper(fixedLength, 1, -3));
    assertEquals(2, indexOfHelper(fixedLength, 1, -2));
    assertEquals(-1, indexOfHelper(fixedLength, undefined));

    assertEquals(1, lastIndexOfHelper(fixedLength, 0));
    assertEquals(1, lastIndexOfHelper(fixedLength, 0, 1));
    assertEquals(1, lastIndexOfHelper(fixedLength, 0, 2));
    assertEquals(1, lastIndexOfHelper(fixedLength, 0, -2));
    assertEquals(1, lastIndexOfHelper(fixedLength, 0, -3));
    assertEquals(-1, lastIndexOfHelper(fixedLength, 1, 1));
    assertEquals(2, lastIndexOfHelper(fixedLength, 1, -2));
    assertEquals(-1, lastIndexOfHelper(fixedLength, 1, -3));
    assertEquals(-1, lastIndexOfHelper(fixedLength, undefined));

    assertEquals(-1, indexOfHelper(fixedLengthWithOffset, 0));
    assertEquals(0, indexOfHelper(fixedLengthWithOffset, 1));
    assertEquals(0, indexOfHelper(fixedLengthWithOffset, 1, -2));
    assertEquals(1, indexOfHelper(fixedLengthWithOffset, 1, -1));
    assertEquals(-1, indexOfHelper(fixedLengthWithOffset, undefined));

    assertEquals(-1, lastIndexOfHelper(fixedLengthWithOffset, 0));
    assertEquals(1, lastIndexOfHelper(fixedLengthWithOffset, 1));
    assertEquals(0, lastIndexOfHelper(fixedLengthWithOffset, 1, -2));
    assertEquals(1, lastIndexOfHelper(fixedLengthWithOffset, 1, -1));
    assertEquals(-1, lastIndexOfHelper(fixedLengthWithOffset, undefined));

    assertEquals(0, indexOfHelper(lengthTracking, 0));
    assertEquals(-1, indexOfHelper(lengthTracking, 0, 2));
    assertEquals(2, indexOfHelper(lengthTracking, 1, -3));
    assertEquals(-1, indexOfHelper(lengthTracking, undefined));

    assertEquals(1, lastIndexOfHelper(lengthTracking, 0));
    assertEquals(1, lastIndexOfHelper(lengthTracking, 0, 2));
    assertEquals(1, lastIndexOfHelper(lengthTracking, 0, -3));
    assertEquals(-1, lastIndexOfHelper(lengthTracking, 1, 1));
    assertEquals(2, lastIndexOfHelper(lengthTracking, 1, 2));
    assertEquals(-1, lastIndexOfHelper(lengthTracking, 1, -3));
    assertEquals(-1, lastIndexOfHelper(lengthTracking, undefined));

    assertEquals(-1, indexOfHelper(lengthTrackingWithOffset, 0));
    assertEquals(0, indexOfHelper(lengthTrackingWithOffset, 1));
    assertEquals(1, indexOfHelper(lengthTrackingWithOffset, 1, 1));
    assertEquals(0, indexOfHelper(lengthTrackingWithOffset, 1, -2));
    assertEquals(-1, indexOfHelper(lengthTrackingWithOffset, undefined));

    assertEquals(-1, lastIndexOfHelper(lengthTrackingWithOffset, 0));
    assertEquals(1, lastIndexOfHelper(lengthTrackingWithOffset, 1));
    assertEquals(1, lastIndexOfHelper(lengthTrackingWithOffset, 1, 1));
    assertEquals(0, lastIndexOfHelper(lengthTrackingWithOffset, 1, -2));
    assertEquals(1, lastIndexOfHelper(lengthTrackingWithOffset, 1, -1));
    assertEquals(-1, lastIndexOfHelper(lengthTrackingWithOffset, undefined));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, Math.floor(i / 2));
    }

    // Orig. array: [0, 0, 1, 1, 2, 2]
    //              [0, 0, 1, 1] << fixedLength
    //                    [1, 1] << fixedLengthWithOffset
    //              [0, 0, 1, 1, 2, 2, ...] << lengthTracking
    //                    [1, 1, 2, 2, ...] << lengthTrackingWithOffset

    assertEquals(2, indexOfHelper(fixedLength, 1));
    assertEquals(-1, indexOfHelper(fixedLength, 2));
    assertEquals(-1, indexOfHelper(fixedLength, undefined));

    assertEquals(3, lastIndexOfHelper(fixedLength, 1));
    assertEquals(-1, lastIndexOfHelper(fixedLength, 2));
    assertEquals(-1, lastIndexOfHelper(fixedLength, undefined));

    assertEquals(-1, indexOfHelper(fixedLengthWithOffset, 0));
    assertEquals(0, indexOfHelper(fixedLengthWithOffset, 1));
    assertEquals(-1, indexOfHelper(fixedLengthWithOffset, 2));
    assertEquals(-1, indexOfHelper(fixedLengthWithOffset, undefined));

    assertEquals(-1, lastIndexOfHelper(fixedLengthWithOffset, 0));
    assertEquals(1, lastIndexOfHelper(fixedLengthWithOffset, 1));
    assertEquals(-1, lastIndexOfHelper(fixedLengthWithOffset, 2));
    assertEquals(-1, lastIndexOfHelper(fixedLengthWithOffset, undefined));

    assertEquals(2, indexOfHelper(lengthTracking, 1));
    assertEquals(4, indexOfHelper(lengthTracking, 2));
    assertEquals(-1, indexOfHelper(lengthTracking, undefined));

    assertEquals(3, lastIndexOfHelper(lengthTracking, 1));
    assertEquals(5, lastIndexOfHelper(lengthTracking, 2));
    assertEquals(-1, lastIndexOfHelper(lengthTracking, undefined));

    assertEquals(-1, indexOfHelper(lengthTrackingWithOffset, 0));
    assertEquals(0, indexOfHelper(lengthTrackingWithOffset, 1));
    assertEquals(2, indexOfHelper(lengthTrackingWithOffset, 2));
    assertEquals(-1, indexOfHelper(lengthTrackingWithOffset, undefined));

    assertEquals(-1, lastIndexOfHelper(lengthTrackingWithOffset, 0));
    assertEquals(1, lastIndexOfHelper(lengthTrackingWithOffset, 1));
    assertEquals(3, lastIndexOfHelper(lengthTrackingWithOffset, 2));
    assertEquals(-1, lastIndexOfHelper(lengthTrackingWithOffset, undefined));
  }
}
IndexOfLastIndexOf(TypedArrayIndexOfHelper, TypedArrayLastIndexOfHelper);
IndexOfLastIndexOf(ArrayIndexOfHelper, ArrayLastIndexOfHelper);

function IndexOfParameterConversionGrows(indexOfHelper) {
  // Growing + length-tracking TA.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, 1);
    }

    let evil = { valueOf: () => {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return 0;
    }};
    assertEquals(-1, indexOfHelper(lengthTracking, 0));
    // The TA grew but we only look at the data until the original length.
    assertEquals(-1, indexOfHelper(lengthTracking, 0, evil));
  }

  // Growing + length-tracking TA, index conversion.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    WriteToTypedArray(lengthTracking, 0, 1);

    let evil = { valueOf: () => {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return -4;
    }};
    assertEquals(0, indexOfHelper(lengthTracking, 1, -4));
    // The TA grew but the start index conversion is done based on the original
    // length.
    assertEquals(0, indexOfHelper(lengthTracking, 1, evil));
  }
}
IndexOfParameterConversionGrows(TypedArrayIndexOfHelper);
IndexOfParameterConversionGrows(ArrayIndexOfHelper);

function LastIndexOfParameterConversionGrows(lastIndexOfHelper) {
  // Growing + length-tracking TA.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(lengthTracking, i, 1);
    }

    let evil = { valueOf: () => {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return -1;
    }};
    assertEquals(-1, lastIndexOfHelper(lengthTracking, 0));
    // Because lastIndexOf iterates from the given index downwards, it's not
    // possible to test that "we only look at the data until the original
    // length" without also testing that the index conversion happening with the
    // original length.
    assertEquals(-1, lastIndexOfHelper(lengthTracking, 0, evil));
  }

  // Growing + length-tracking TA, index conversion.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);

    let evil = { valueOf: () => {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return -4;
    }};
    assertEquals(0, lastIndexOfHelper(lengthTracking, 0, -4));
    // The TA grew but the start index conversion is done based on the original
    // length.
    assertEquals(0, lastIndexOfHelper(lengthTracking, 0, evil));
  }
}
LastIndexOfParameterConversionGrows(TypedArrayLastIndexOfHelper);
LastIndexOfParameterConversionGrows(ArrayLastIndexOfHelper);

(function IndexOfLastIndexOfSpecialValues() {
  for (let ctor of floatCtors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    lengthTracking[0] = -Infinity;
    lengthTracking[1] = -Infinity;
    lengthTracking[2] = Infinity;
    lengthTracking[3] = Infinity;
    lengthTracking[4] = NaN;
    lengthTracking[5] = NaN;
    assertEquals(0, lengthTracking.indexOf(-Infinity));
    assertEquals(1, lengthTracking.lastIndexOf(-Infinity));
    assertEquals(2, lengthTracking.indexOf(Infinity));
    assertEquals(3, lengthTracking.lastIndexOf(Infinity));
    // NaN is never found.
    assertEquals(-1, lengthTracking.indexOf(NaN));
    assertEquals(-1, lengthTracking.lastIndexOf(NaN));
  }
})();

function JoinToLocaleString(joinHelper, toLocaleStringHelper) {
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
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    assertEquals('0,2,4,6', joinHelper(fixedLength));
    assertEquals('0,2,4,6', toLocaleStringHelper(fixedLength));
    assertEquals('4,6', joinHelper(fixedLengthWithOffset));
    assertEquals('4,6', toLocaleStringHelper(fixedLengthWithOffset));
    assertEquals('0,2,4,6', joinHelper(lengthTracking));
    assertEquals('0,2,4,6', toLocaleStringHelper(lengthTracking));
    assertEquals('4,6', joinHelper(lengthTrackingWithOffset));
    assertEquals('4,6', toLocaleStringHelper(lengthTrackingWithOffset));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset

    assertEquals('0,2,4,6', joinHelper(fixedLength));
    assertEquals('0,2,4,6', toLocaleStringHelper(fixedLength));
    assertEquals('4,6', joinHelper(fixedLengthWithOffset));
    assertEquals('4,6', toLocaleStringHelper(fixedLengthWithOffset));
    assertEquals('0,2,4,6,8,10', joinHelper(lengthTracking));
    assertEquals('0,2,4,6,8,10', toLocaleStringHelper(lengthTracking));
    assertEquals('4,6,8,10', joinHelper(lengthTrackingWithOffset));
    assertEquals('4,6,8,10', toLocaleStringHelper(lengthTrackingWithOffset));
 }
}
JoinToLocaleString(TypedArrayJoinHelper, TypedArrayToLocaleStringHelper);
JoinToLocaleString(ArrayJoinHelper, ArrayToLocaleStringHelper);

function JoinParameterConversionGrows(joinHelper) {
  // Growing + fixed-length TA.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);

    let evil = { toString: () => {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return '.';
    }};
    assertEquals('0.0.0.0', joinHelper(fixedLength, evil));
  }

  // Growing + length-tracking TA.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);

    let evil = { toString: () => {
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      return '.';
    }};
    // We iterate 4 elements, since it was the starting length.
    assertEquals('0.0.0.0', joinHelper(lengthTracking, evil));
  }
}
JoinParameterConversionGrows(TypedArrayJoinHelper);

function ToLocaleStringNumberPrototypeToLocaleStringGrows(
    toLocaleStringHelper) {
  const oldNumberPrototypeToLocaleString = Number.prototype.toLocaleString;
  const oldBigIntPrototypeToLocaleString = BigInt.prototype.toLocaleString;

  // Growing + fixed-length TA.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);

    let growAfter = 2;
    Number.prototype.toLocaleString = function() {
      --growAfter;
      if (growAfter == 0) {
        gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      }
      return oldNumberPrototypeToLocaleString.call(this);
    }
    BigInt.prototype.toLocaleString = function() {
      --growAfter;
      if (growAfter == 0) {
        gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      }
      return oldBigIntPrototypeToLocaleString.call(this);
    }

    // We iterate 4 elements since it was the starting length. Resizing doesn't
    // affect the TA.
    assertEquals('0,0,0,0', toLocaleStringHelper(fixedLength));
  }

  // Growing + length-tracking TA.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);

    let growAfter = 2;
    Number.prototype.toLocaleString = function() {
      --growAfter;
      if (growAfter == 0) {
        gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      }
      return oldNumberPrototypeToLocaleString.call(this);
    }
    BigInt.prototype.toLocaleString = function() {
      --growAfter;
      if (growAfter == 0) {
        gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
      }
      return oldBigIntPrototypeToLocaleString.call(this);
    }

    // We iterate 4 elements since it was the starting length.
    assertEquals('0,0,0,0', toLocaleStringHelper(lengthTracking));
  }

  Number.prototype.toLocaleString = oldNumberPrototypeToLocaleString;
  BigInt.prototype.toLocaleString = oldBigIntPrototypeToLocaleString;
}
ToLocaleStringNumberPrototypeToLocaleStringGrows(
    TypedArrayToLocaleStringHelper);
ToLocaleStringNumberPrototypeToLocaleStringGrows(ArrayToLocaleStringHelper);

function TestMap(mapHelper) {
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
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    function Helper(array) {
      const values = [];
      function GatherValues(n, ix) {
        assertEquals(values.length, ix);
        values.push(n);
        if (typeof n == 'bigint') {
          return n + 1n;
        }
        return n + 1;
      }
      const newValues = mapHelper(array, GatherValues);
      for (let i = 0; i < values.length; ++i) {
        if (typeof values[i] == 'bigint') {
          assertEquals(newValues[i], values[i] + 1n);
        } else {
          assertEquals(newValues[i], values[i] + 1);
        }
      }
      return ToNumbers(values);
    }

    assertEquals([0, 2, 4, 6], Helper(fixedLength));
    assertEquals([4, 6], Helper(fixedLengthWithOffset));
    assertEquals([0, 2, 4, 6], Helper(lengthTracking));
    assertEquals([4, 6], Helper(lengthTrackingWithOffset));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset

    assertEquals([0, 2, 4, 6], Helper(fixedLength));
    assertEquals([4, 6], Helper(fixedLengthWithOffset));
    assertEquals([0, 2, 4, 6, 8, 10], Helper(lengthTracking));
    assertEquals([4, 6, 8, 10], Helper(lengthTrackingWithOffset));
  }
}
TestMap(TypedArrayMapHelper);
TestMap(ArrayMapHelper);

function MapGrowMidIteration(mapHelper) {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let values;
  let gsab;
  let growAfter;
  let growTo;
  function CollectValuesAndResize(n) {
    if (typeof n == 'bigint') {
      values.push(Number(n));
    } else {
      values.push(n);
    }
    if (values.length == growAfter) {
      gsab.grow(growTo);
    }
    return n;
  }

  function Helper(array) {
    values = [];
    mapHelper(array, CollectValuesAndResize);
    return values;
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2, 4, 6], Helper(fixedLength));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4, 6], Helper(fixedLengthWithOffset));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    growAfter = 2;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([0, 2, 4, 6], Helper(lengthTracking));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    growAfter = 1;
    growTo = 5 * ctor.BYTES_PER_ELEMENT;
    assertEquals([4, 6], Helper(lengthTrackingWithOffset));
  }
}
MapGrowMidIteration(TypedArrayMapHelper);
MapGrowMidIteration(ArrayMapHelper);

(function MapSpeciesCreateGrows() {
  let values;
  let gsab;
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
    gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    let resizeWhenConstructorCalled = false;
    class MyArray extends ctor {
      constructor(...params) {
        super(...params);
        if (resizeWhenConstructorCalled) {
          gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
        }
      }
    };

    const fixedLength = new MyArray(gsab, 0, 4);
    resizeWhenConstructorCalled = true;
    assertEquals([0, 1, 2, 3], Helper(fixedLength));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, gsab.byteLength);
  }

  for (let ctor of ctors) {
    gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);

    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i);
    }

    let resizeWhenConstructorCalled = false;
    class MyArray extends ctor {
      constructor(...params) {
        super(...params);
        if (resizeWhenConstructorCalled) {
          gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
        }
      }
    };

    const lengthTracking = new MyArray(gsab);
    resizeWhenConstructorCalled = true;
    assertEquals([0, 1, 2, 3], Helper(lengthTracking));
    assertEquals(6 * ctor.BYTES_PER_ELEMENT, gsab.byteLength);
  }
})();

function Reverse(reverseHelper) {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    const wholeArrayView = new ctor(gsab);
    function WriteData() {
      // Write some data into the array.
      for (let i = 0; i < wholeArrayView.length; ++i) {
        WriteToTypedArray(wholeArrayView, i, 2 * i);
      }
    }
    WriteData();

    // Orig. array: [0, 2, 4, 6]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, ...] << lengthTracking
    //                    [4, 6, ...] << lengthTrackingWithOffset

    reverseHelper(fixedLength);
    assertEquals([6, 4, 2, 0], ToNumbers(wholeArrayView));
    reverseHelper(fixedLengthWithOffset);
    assertEquals([6, 4, 0, 2], ToNumbers(wholeArrayView));
    reverseHelper(lengthTracking);
    assertEquals([2, 0, 4, 6], ToNumbers(wholeArrayView));
    reverseHelper(lengthTrackingWithOffset);
    assertEquals([2, 0, 6, 4], ToNumbers(wholeArrayView));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    WriteData();

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset

    reverseHelper(fixedLength);
    assertEquals([6, 4, 2, 0, 8, 10], ToNumbers(wholeArrayView));
    reverseHelper(fixedLengthWithOffset);
    assertEquals([6, 4, 0, 2, 8, 10], ToNumbers(wholeArrayView));
    reverseHelper(lengthTracking);
    assertEquals([10, 8, 2, 0, 4, 6], ToNumbers(wholeArrayView));
    reverseHelper(lengthTrackingWithOffset);
    assertEquals([10, 8, 6, 4, 0, 2], ToNumbers(wholeArrayView));
  }
}
Reverse(TypedArrayReverseHelper);
Reverse(ArrayReverseHelper);

(function SetWithGrowableTarget() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taFull = new ctor(gsab);

    // Orig. array: [0, 0, 0, 0]
    //              [0, 0, 0, 0] << fixedLength
    //                    [0, 0] << fixedLengthWithOffset
    //              [0, 0, 0, 0, ...] << lengthTracking
    //                    [0, 0, ...] << lengthTrackingWithOffset

    SetHelper(fixedLength, [1, 2]);
    assertEquals([1, 2, 0, 0], ToNumbers(taFull));
    SetHelper(fixedLength, [3, 4], 1);
    assertEquals([1, 3, 4, 0], ToNumbers(taFull));
    assertThrows(() => { SetHelper(fixedLength, [0, 0, 0, 0, 0])}, RangeError);
    assertThrows(() => { SetHelper(fixedLength, [0, 0, 0, 0], 1)}, RangeError);
    assertEquals([1, 3, 4, 0], ToNumbers(taFull));

    SetHelper(fixedLengthWithOffset, [5, 6]);
    assertEquals([1, 3, 5, 6], ToNumbers(taFull));
    SetHelper(fixedLengthWithOffset, [7], 1);
    assertEquals([1, 3, 5, 7], ToNumbers(taFull));
    assertThrows(() => { SetHelper(fixedLengthWithOffset, [0, 0, 0])},
                 RangeError);
    assertThrows(() => { SetHelper(fixedLengthWithOffset, [0, 0], 1)},
                 RangeError);
    assertEquals([1, 3, 5, 7], ToNumbers(taFull));

    SetHelper(lengthTracking, [8, 9]);
    assertEquals([8, 9, 5, 7], ToNumbers(taFull));
    SetHelper(lengthTracking, [10, 11], 1);
    assertEquals([8, 10, 11, 7], ToNumbers(taFull));
    assertThrows(() => { SetHelper(lengthTracking, [0, 0, 0, 0, 0])},
                 RangeError);
    assertThrows(() => { SetHelper(lengthTracking, [0, 0, 0, 0], 1)},
                 RangeError);
    assertEquals([8, 10, 11, 7], ToNumbers(taFull));

    SetHelper(lengthTrackingWithOffset, [12, 13]);
    assertEquals([8, 10, 12, 13], ToNumbers(taFull));
    SetHelper(lengthTrackingWithOffset, [14], 1);
    assertEquals([8, 10, 12, 14], ToNumbers(taFull));
    assertThrows(() => { SetHelper(lengthTrackingWithOffset, [0, 0, 0])},
                 RangeError);
    assertThrows(() => { SetHelper(lengthTrackingWithOffset, [0, 0], 1)},
                 RangeError);
    assertEquals([8, 10, 12, 14], ToNumbers(taFull));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [8, 10, 12, 14, 0, 0]
    //              [8, 10, 12, 14] << fixedLength
    //                     [12, 14] << fixedLengthWithOffset
    //              [8, 10, 12, 14, 0, 0, ...] << lengthTracking
    //                     [12, 14, 0, 0, ...] << lengthTrackingWithOffset
    SetHelper(fixedLength, [21, 22]);
    assertEquals([21, 22, 12, 14, 0, 0], ToNumbers(taFull));
    SetHelper(fixedLength, [23, 24], 1);
    assertEquals([21, 23, 24, 14, 0, 0], ToNumbers(taFull));
    assertThrows(() => { SetHelper(fixedLength, [0, 0, 0, 0, 0])}, RangeError);
    assertThrows(() => { SetHelper(fixedLength, [0, 0, 0, 0], 1)}, RangeError);
    assertEquals([21, 23, 24, 14, 0, 0], ToNumbers(taFull));

    SetHelper(fixedLengthWithOffset, [25, 26]);
    assertEquals([21, 23, 25, 26, 0, 0], ToNumbers(taFull));
    SetHelper(fixedLengthWithOffset, [27], 1);
    assertEquals([21, 23, 25, 27, 0, 0], ToNumbers(taFull));
    assertThrows(() => { SetHelper(fixedLengthWithOffset, [0, 0, 0])},
                 RangeError);
    assertThrows(() => { SetHelper(fixedLengthWithOffset, [0, 0], 1)},
                 RangeError);
    assertEquals([21, 23, 25, 27, 0, 0], ToNumbers(taFull));

    SetHelper(lengthTracking, [28, 29, 30, 31, 32, 33]);
    assertEquals([28, 29, 30, 31, 32, 33], ToNumbers(taFull));
    SetHelper(lengthTracking, [34, 35, 36, 37, 38], 1);
    assertEquals([28, 34, 35, 36, 37, 38], ToNumbers(taFull));
    assertThrows(() => { SetHelper(lengthTracking, [0, 0, 0, 0, 0, 0, 0])},
                 RangeError);
    assertThrows(() => { SetHelper(lengthTracking, [0, 0, 0, 0, 0, 0], 1)},
                 RangeError);
    assertEquals([28, 34, 35, 36, 37, 38], ToNumbers(taFull));

    SetHelper(lengthTrackingWithOffset, [39, 40, 41, 42]);
    assertEquals([28, 34, 39, 40, 41, 42], ToNumbers(taFull));
    SetHelper(lengthTrackingWithOffset, [43, 44, 45], 1);
    assertEquals([28, 34, 39, 43, 44, 45], ToNumbers(taFull));
    assertThrows(() => { SetHelper(lengthTrackingWithOffset, [0, 0, 0, 0, 0])},
                 RangeError);
    assertThrows(() => { SetHelper(lengthTrackingWithOffset, [0, 0, 0, 0], 1)},
                 RangeError);
    assertEquals([28, 34, 39, 43, 44, 45], ToNumbers(taFull));
  }
})();

(function SetSourceLengthGetterGrowsTarget() {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let gsab;
  let growTo;
  function CreateSourceProxy(length) {
    return new Proxy({}, {
      get(target, prop, receiver) {
        if (prop == 'length') {
          gsab.grow(growTo);
          return length;
        }
        return true; // Can be converted to both BigInt and Number.
      }
    });
  }

  // Test that we still throw for lengthTracking TAs if the source length is
  // too large, even though we resized in the length getter (we check against
  // the original length).
  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    growTo = 6 * ctor.BYTES_PER_ELEMENT;
    assertThrows(() => { lengthTracking.set(CreateSourceProxy(6)); },
                 RangeError);
    assertEquals([0, 2, 4, 6, 0, 0], ToNumbers(new ctor(gsab)));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    growTo = 6 * ctor.BYTES_PER_ELEMENT;
    assertThrows(() => { lengthTrackingWithOffset.set(CreateSourceProxy(6)); },
                 RangeError);
    assertEquals([0, 2, 4, 6, 0, 0], ToNumbers(new ctor(gsab)));
  }
})();

(function SetGrowTargetMidIteration() {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //                    [4, 6] << fixedLengthWithOffset
  //              [0, 2, 4, 6, ...] << lengthTracking
  //                    [4, 6, ...] << lengthTrackingWithOffset
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  let gsab;
  // Growing will happen when we're calling Get for the `growAt`:th data
  // element, but we haven't yet written it to the target.
  let growAt;
  let growTo;
  function CreateSourceProxy(length) {
    let requestedIndices = [];
    return new Proxy({}, {
      get(target, prop, receiver) {
        if (prop == 'length') {
          return length;
        }
        requestedIndices.push(prop);
        if (requestedIndices.length == growAt) {
          gsab.grow(growTo);
        }
        return true; // Can be converted to both BigInt and Number.
      }
    });
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);
    growAt = 2;
    growTo = 6 * ctor.BYTES_PER_ELEMENT;
    fixedLength.set(CreateSourceProxy(4));
    assertEquals([1, 1, 1, 1], ToNumbers(fixedLength));
    assertEquals([1, 1, 1, 1, 0, 0], ToNumbers(new ctor(gsab)));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    growAt = 1;
    growTo = 6 * ctor.BYTES_PER_ELEMENT;
    fixedLengthWithOffset.set(CreateSourceProxy(2));
    assertEquals([1, 1], ToNumbers(fixedLengthWithOffset));
    assertEquals([0, 2, 1, 1, 0, 0], ToNumbers(new ctor(gsab)));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);
    growAt = 2;
    growTo = 6 * ctor.BYTES_PER_ELEMENT;
    lengthTracking.set(CreateSourceProxy(2));
    assertEquals([1, 1, 4, 6, 0, 0], ToNumbers(lengthTracking));
    assertEquals([1, 1, 4, 6, 0, 0], ToNumbers(new ctor(gsab)));
  }

  for (let ctor of ctors) {
    gsab = CreateGsabForTest(ctor);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    growAt = 1;
    growTo = 6 * ctor.BYTES_PER_ELEMENT;
    lengthTrackingWithOffset.set(CreateSourceProxy(2));
    assertEquals([1, 1, 0, 0], ToNumbers(lengthTrackingWithOffset));
    assertEquals([0, 2, 1, 1, 0, 0], ToNumbers(new ctor(gsab)));
  }
})();

(function SetWithGrowableSource() {
  for (let targetIsGrowable of [false, true]) {
    for (let targetCtor of ctors) {
      for (let sourceCtor of ctors) {
        const gsab = CreateGrowableSharedArrayBuffer(
            4 * sourceCtor.BYTES_PER_ELEMENT,
            8 * sourceCtor.BYTES_PER_ELEMENT);
        const fixedLength = new sourceCtor(gsab, 0, 4);
        const fixedLengthWithOffset = new sourceCtor(
            gsab, 2 * sourceCtor.BYTES_PER_ELEMENT, 2);
        const lengthTracking = new sourceCtor(gsab, 0);
        const lengthTrackingWithOffset = new sourceCtor(
            gsab, 2 * sourceCtor.BYTES_PER_ELEMENT);

        // Write some data into the array.
        const taFull = new sourceCtor(gsab);
        for (let i = 0; i < 4; ++i) {
          WriteToTypedArray(taFull, i, i + 1);
        }

        // Orig. array: [1, 2, 3, 4]
        //              [1, 2, 3, 4] << fixedLength
        //                    [3, 4] << fixedLengthWithOffset
        //              [1, 2, 3, 4, ...] << lengthTracking
        //                    [3, 4, ...] << lengthTrackingWithOffset

        const targetAb = targetIsGrowable ?
          new ArrayBuffer(6 * targetCtor.BYTES_PER_ELEMENT) :
          new ArrayBuffer(6 * targetCtor.BYTES_PER_ELEMENT,
                         {maxByteLength: 8 * targetCtor.BYTES_PER_ELEMENT});
        const target = new targetCtor(targetAb);

        if (IsBigIntTypedArray(target) != IsBigIntTypedArray(taFull)) {
          // Can't mix BigInt and non-BigInt types.
          continue;
        }

        SetHelper(target, fixedLength);
        assertEquals([1, 2, 3, 4, 0, 0], ToNumbers(target));

        SetHelper(target, fixedLengthWithOffset);
        assertEquals([3, 4, 3, 4, 0, 0], ToNumbers(target));

        SetHelper(target, lengthTracking, 1);
        assertEquals([3, 1, 2, 3, 4, 0], ToNumbers(target));

        SetHelper(target, lengthTrackingWithOffset, 1);
        assertEquals([3, 3, 4, 3, 4, 0], ToNumbers(target));

        // Grow.
        gsab.grow(6 * sourceCtor.BYTES_PER_ELEMENT);

        for (let i = 0; i < 6; ++i) {
          WriteToTypedArray(taFull, i, i + 1);
        }

        // Orig. array: [1, 2, 3, 4, 5, 6]
        //              [1, 2, 3, 4] << fixedLength
        //                    [3, 4] << fixedLengthWithOffset
        //              [1, 2, 3, 4, 5, 6, ...] << lengthTracking
        //                    [3, 4, 5, 6, ...] << lengthTrackingWithOffset

        SetHelper(target, fixedLength);
        assertEquals([1, 2, 3, 4, 4, 0], ToNumbers(target));

        SetHelper(target, fixedLengthWithOffset);
        assertEquals([3, 4, 3, 4, 4, 0], ToNumbers(target));

        SetHelper(target, lengthTracking, 0);
        assertEquals([1, 2, 3, 4, 5, 6], ToNumbers(target));

        SetHelper(target, lengthTrackingWithOffset, 1);
        assertEquals([1, 3, 4, 5, 6, 6], ToNumbers(target));
      }
    }
  }
})();

(function Subarray() {
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

    // Relative offsets
    assertEquals([4, 6], ToNumbers(fixedLength.subarray(-2)));
    assertEquals([6], ToNumbers(fixedLengthWithOffset.subarray(-1)));
    assertEquals([4, 6], ToNumbers(lengthTracking.subarray(-2)));
    assertEquals([6], ToNumbers(lengthTrackingWithOffset.subarray(-1)));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }

    // Orig. array: [0, 2, 4, 6, 8, 10]
    //              [0, 2, 4, 6] << fixedLength
    //                    [4, 6] << fixedLengthWithOffset
    //              [0, 2, 4, 6, 8, 10, ...] << lengthTracking
    //                    [4, 6, 8, 10, ...] << lengthTrackingWithOffset

    assertEquals([0, 2, 4, 6], ToNumbers(fixedLength.subarray(0)));
    assertEquals([4, 6], ToNumbers(fixedLengthWithOffset.subarray(0)));
    assertEquals([0, 2, 4, 6, 8, 10], ToNumbers(lengthTracking.subarray(0)));
    assertEquals([4, 6, 8, 10],
                 ToNumbers(lengthTrackingWithOffset.subarray(0)));

    assertEquals(4, fixedLengthSubFull.length);
    assertEquals(2, fixedLengthWithOffsetSubFull.length);

    // Subarrays of length-tracking TAs that don't pass an explicit end argument
    // are also length-tracking.
    assertEquals(lengthTracking.length, lengthTrackingSubFull.length);
    assertEquals(lengthTrackingWithOffset.length,
                 lengthTrackingWithOffsetSubFull.length);
  }
})();

(function SubarrayParameterConversionGrows() {
  // Orig. array: [0, 2, 4, 6]
  //              [0, 2, 4, 6] << fixedLength
  //              [0, 2, 4, 6, ...] << lengthTracking
  function CreateGsabForTest(ctor) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    // Write some data into the array.
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, 2 * i);
    }
    return gsab;
  }

  // Growing + fixed-length TA. Growing won't affect anything.
  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const fixedLength = new ctor(gsab, 0, 4);

    const evil = { valueOf: () => { gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
                                    return 0;}};
    assertEquals([0, 2, 4, 6], ToNumbers(fixedLength.subarray(evil)));
  }

  // Growing + length-tracking TA. The length computation is done with the
  // original length.
  for (let ctor of ctors) {
    const gsab = CreateGsabForTest(ctor);
    const lengthTracking = new ctor(gsab, 0);

    const evil = { valueOf: () => { gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
                                    return 0;}};

    assertEquals([0, 2, 4, 6], ToNumbers(
      lengthTracking.subarray(evil, lengthTracking.length)));
  }
})();

// This function cannot be reused between TypedArray.protoype.sort and
// Array.prototype.sort, since the default sorting functions differ.
(function SortWithDefaultComparison() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    const taFull = new ctor(gsab, 0);
    function WriteUnsortedData() {
      // Write some data into the array.
      for (let i = 0; i < taFull.length; ++i) {
        WriteToTypedArray(taFull, i, 10 - 2 * i);
      }
    }
    // Orig. array: [10, 8, 6, 4]
    //              [10, 8, 6, 4] << fixedLength
    //                     [6, 4] << fixedLengthWithOffset
    //              [10, 8, 6, 4, ...] << lengthTracking
    //                     [6, 4, ...] << lengthTrackingWithOffset

    WriteUnsortedData();
    fixedLength.sort();
    assertEquals([4, 6, 8, 10], ToNumbers(taFull));

    WriteUnsortedData();
    fixedLengthWithOffset.sort();
    assertEquals([10, 8, 4, 6], ToNumbers(taFull));

    WriteUnsortedData();
    lengthTracking.sort();
    assertEquals([4, 6, 8, 10], ToNumbers(taFull));

    WriteUnsortedData();
    lengthTrackingWithOffset.sort();
    assertEquals([10, 8, 4, 6], ToNumbers(taFull));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [10, 8, 6, 4, 2, 0]
    //              [10, 8, 6, 4] << fixedLength
    //                     [6, 4] << fixedLengthWithOffset
    //              [10, 8, 6, 4, 2, 0, ...] << lengthTracking
    //                     [6, 4, 2, 0, ...] << lengthTrackingWithOffset

    WriteUnsortedData();
    fixedLength.sort();
    assertEquals([4, 6, 8, 10, 2, 0], ToNumbers(taFull));

    WriteUnsortedData();
    fixedLengthWithOffset.sort();
    assertEquals([10, 8, 4, 6, 2, 0], ToNumbers(taFull));

    WriteUnsortedData();
    lengthTracking.sort();
    assertEquals([0, 2, 4, 6, 8, 10], ToNumbers(taFull));

    WriteUnsortedData();
    lengthTrackingWithOffset.sort();
    assertEquals([10, 8, 0, 2, 4, 6], ToNumbers(taFull));
  }
})();

// The default comparison function for Array.prototype.sort is the string sort.
(function ArraySortWithDefaultComparison() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    const taFull = new ctor(gsab, 0);
    function WriteUnsortedData() {
      // Write some data into the array.
      for (let i = 0; i < taFull.length; ++i) {
        WriteToTypedArray(taFull, i, 10 - 2 * i);
      }
    }
    // Orig. array: [10, 8, 6, 4]
    //              [10, 8, 6, 4] << fixedLength
    //                     [6, 4] << fixedLengthWithOffset
    //              [10, 8, 6, 4, ...] << lengthTracking
    //                     [6, 4, ...] << lengthTrackingWithOffset

    WriteUnsortedData();
    ArraySortHelper(fixedLength);
    assertEquals([10, 4, 6, 8], ToNumbers(taFull));

    WriteUnsortedData();
    ArraySortHelper(fixedLengthWithOffset);
    assertEquals([10, 8, 4, 6], ToNumbers(taFull));

    WriteUnsortedData();
    ArraySortHelper(lengthTracking);
    assertEquals([10, 4, 6, 8], ToNumbers(taFull));

    WriteUnsortedData();
    ArraySortHelper(lengthTrackingWithOffset);
    assertEquals([10, 8, 4, 6], ToNumbers(taFull));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [10, 8, 6, 4, 2, 0]
    //              [10, 8, 6, 4] << fixedLength
    //                     [6, 4] << fixedLengthWithOffset
    //              [10, 8, 6, 4, 2, 0, ...] << lengthTracking
    //                     [6, 4, 2, 0, ...] << lengthTrackingWithOffset

    WriteUnsortedData();
    ArraySortHelper(fixedLength);
    assertEquals([10, 4, 6, 8, 2, 0], ToNumbers(taFull));

    WriteUnsortedData();
    ArraySortHelper(fixedLengthWithOffset);
    assertEquals([10, 8, 4, 6, 2, 0], ToNumbers(taFull));

    WriteUnsortedData();
    ArraySortHelper(lengthTracking);
    assertEquals([0, 10, 2, 4, 6, 8], ToNumbers(taFull));

    WriteUnsortedData();
    ArraySortHelper(lengthTrackingWithOffset);
    assertEquals([10, 8, 0, 2, 4, 6], ToNumbers(taFull));
  }
})();

function SortWithCustomComparison(sortHelper) {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    const taFull = new ctor(gsab, 0);
    function WriteUnsortedData() {
      // Write some data into the array.
      for (let i = 0; i < taFull.length; ++i) {
        WriteToTypedArray(taFull, i, 10 - i);
      }
    }
    function CustomComparison(a, b) {
      // Sort all odd numbers before even numbers.
      a = Number(a);
      b = Number(b);
      if (a % 2 == 1 && b % 2 == 0) {
        return -1;
      }
      if (a % 2 == 0 && b % 2 == 1) {
        return 1;
      }
      if (a < b) {
        return -1;
      }
      if (a > b) {
        return 1;
      }
      return 0;
    }

    // Orig. array: [10, 9, 8, 7]
    //              [10, 9, 8, 7] << fixedLength
    //                     [8, 7] << fixedLengthWithOffset
    //              [10, 9, 8, 7, ...] << lengthTracking
    //                     [8, 7, ...] << lengthTrackingWithOffset

    WriteUnsortedData();
    sortHelper(fixedLength, CustomComparison);
    assertEquals([7, 9, 8, 10], ToNumbers(taFull));

    WriteUnsortedData();
    sortHelper(fixedLengthWithOffset, CustomComparison);
    assertEquals([10, 9, 7, 8], ToNumbers(taFull));

    WriteUnsortedData();
    sortHelper(lengthTracking, CustomComparison);
    assertEquals([7, 9, 8, 10], ToNumbers(taFull));

    WriteUnsortedData();
    sortHelper(lengthTrackingWithOffset, CustomComparison);
    assertEquals([10, 9, 7, 8], ToNumbers(taFull));

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [10, 9, 8, 7, 6, 5]
    //              [10, 9, 8, 7] << fixedLength
    //                     [8, 7] << fixedLengthWithOffset
    //              [10, 9, 8, 7, 6, 5, ...] << lengthTracking
    //                     [8, 7, 6, 5, ...] << lengthTrackingWithOffset

    WriteUnsortedData();
    sortHelper(fixedLength, CustomComparison);
    assertEquals([7, 9, 8, 10, 6, 5], ToNumbers(taFull));

    WriteUnsortedData();
    sortHelper(fixedLengthWithOffset, CustomComparison);
    assertEquals([10, 9, 7, 8, 6, 5], ToNumbers(taFull));

    WriteUnsortedData();
    sortHelper(lengthTracking, CustomComparison);
    assertEquals([5, 7, 9, 6, 8, 10], ToNumbers(taFull));

    WriteUnsortedData();
    sortHelper(lengthTrackingWithOffset, CustomComparison);
    assertEquals([10, 9, 5, 7, 6, 8], ToNumbers(taFull));
  }
}
SortWithCustomComparison(TypedArraySortHelper);
SortWithCustomComparison(ArraySortHelper);

function SortCallbackGrows(sortHelper) {
  function WriteUnsortedData(taFull) {
    for (let i = 0; i < taFull.length; ++i) {
      WriteToTypedArray(taFull, i, 10 - i);
    }
  }

  let gsab;
  let growTo;
  function CustomComparison(a, b) {
    gsab.grow(growTo);
    if (a < b) {
      return -1;
    }
    if (a > b) {
      return 1;
    }
    return 0;
  }

  // Fixed length TA.
  for (let ctor of ctors) {
    gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    growTo = 6 * ctor.BYTES_PER_ELEMENT;
    const fixedLength = new ctor(gsab, 0, 4);
    const taFull = new ctor(gsab, 0);
    WriteUnsortedData(taFull);

    sortHelper(fixedLength, CustomComparison);

    // Growing doesn't affect the sorting.
    assertEquals([7, 8, 9, 10, 0, 0], ToNumbers(taFull));
  }

  // Length-tracking TA.
  for (let ctor of ctors) {
    gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    growTo = 6 * ctor.BYTES_PER_ELEMENT;
    const lengthTracking = new ctor(gsab, 0);
    const taFull = new ctor(gsab, 0);
    WriteUnsortedData(taFull);

    sortHelper(lengthTracking, CustomComparison);

    // Growing doesn't affect the sorting. Only the elements that were part of
    // the original TA are sorted.
    assertEquals([7, 8, 9, 10, 0, 0], ToNumbers(taFull));
  }
}
SortCallbackGrows(TypedArraySortHelper);
SortCallbackGrows(ArraySortHelper);

(function ObjectDefinePropertyDefineProperties() {
  for (let helper of
      [ObjectDefinePropertyHelper, ObjectDefinePropertiesHelper]) {
    for (let ctor of ctors) {
      const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                   8 * ctor.BYTES_PER_ELEMENT);
      const fixedLength = new ctor(gsab, 0, 4);
      const fixedLengthWithOffset = new ctor(
          gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
      const lengthTracking = new ctor(gsab, 0);
      const lengthTrackingWithOffset = new ctor(
          gsab, 2 * ctor.BYTES_PER_ELEMENT);
      const taFull = new ctor(gsab, 0);

      // Orig. array: [0, 0, 0, 0]
      //              [0, 0, 0, 0] << fixedLength
      //                    [0, 0] << fixedLengthWithOffset
      //              [0, 0, 0, 0, ...] << lengthTracking
      //                    [0, 0, ...] << lengthTrackingWithOffset

      helper(fixedLength, 0, 1);
      assertEquals([1, 0, 0, 0], ToNumbers(taFull));
      helper(fixedLengthWithOffset, 0, 2);
      assertEquals([1, 0, 2, 0], ToNumbers(taFull));
      helper(lengthTracking, 1, 3);
      assertEquals([1, 3, 2, 0], ToNumbers(taFull));
      helper(lengthTrackingWithOffset, 1, 4);
      assertEquals([1, 3, 2, 4], ToNumbers(taFull));

      assertThrows(() => { helper(fixedLength, 4, 8); }, TypeError);
      assertThrows(() => { helper(fixedLengthWithOffset, 2, 8); }, TypeError);
      assertThrows(() => { helper(lengthTracking, 4, 8); }, TypeError);
      assertThrows(() => { helper(lengthTrackingWithOffset, 2, 8); },
                   TypeError);

      // Grow.
      gsab.grow(6 * ctor.BYTES_PER_ELEMENT);

      helper(fixedLength, 0, 9);
      assertEquals([9, 3, 2, 4, 0, 0], ToNumbers(taFull));
      helper(fixedLengthWithOffset, 0, 10);
      assertEquals([9, 3, 10, 4, 0, 0], ToNumbers(taFull));
      helper(lengthTracking, 1, 11);
      assertEquals([9, 11, 10, 4, 0, 0], ToNumbers(taFull));
      helper(lengthTrackingWithOffset, 2, 12);
      assertEquals([9, 11, 10, 4, 12, 0], ToNumbers(taFull));

      // Trying to define properties out of the fixed-length bounds throws.
      assertThrows(() => { helper(fixedLength, 5, 13); }, TypeError);
      assertThrows(() => { helper(fixedLengthWithOffset, 3, 13); }, TypeError);
      assertEquals([9, 11, 10, 4, 12, 0], ToNumbers(taFull));

      helper(lengthTracking, 4, 14);
      assertEquals([9, 11, 10, 4, 14, 0], ToNumbers(taFull));
      helper(lengthTrackingWithOffset, 3, 15);
      assertEquals([9, 11, 10, 4, 14, 15], ToNumbers(taFull));

      assertThrows(() => { helper(fixedLength, 6, 8); }, TypeError);
      assertThrows(() => { helper(fixedLengthWithOffset, 4, 8); }, TypeError);
      assertThrows(() => { helper(lengthTracking, 6, 8); }, TypeError);
      assertThrows(() => { helper(lengthTrackingWithOffset, 4, 8); },
                   TypeError);

    }
  }
})();

(function ObjectDefinePropertyParameterConversionGrows() {
  const helper = ObjectDefinePropertyHelper;
  // Length tracking.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab, 0);
    const evil = {toString: () => {
        gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
        return 4;  // Index valid after resize.
    }};
    helper(lengthTracking, evil, 8);
    assertEquals([0, 0, 0, 0, 8, 0], ToNumbers(lengthTracking));
  }
})();

(function ObjectFreeze() {
  // Freezing non-OOB non-zero-length TAs throws.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(
        gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(
        gsab, 2 * ctor.BYTES_PER_ELEMENT);

    assertThrows(() => { Object.freeze(fixedLength); }, TypeError);
    assertThrows(() => { Object.freeze(fixedLengthWithOffset); }, TypeError);
    assertThrows(() => { Object.freeze(lengthTracking); }, TypeError);
    assertThrows(() => { Object.freeze(lengthTrackingWithOffset); }, TypeError);
  }
  // Freezing zero-length TAs doesn't throw.
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 0);
    const fixedLengthWithOffset = new ctor(
        gsab, 2 * ctor.BYTES_PER_ELEMENT, 0);
    // Zero-length because the offset is at the end:
    const lengthTrackingWithOffset = new ctor(
        gsab, 4 * ctor.BYTES_PER_ELEMENT);

    Object.freeze(fixedLength);
    Object.freeze(fixedLengthWithOffset);
    assertThrows(() => { Object.freeze(lengthTrackingWithOffset); }, TypeError);
  }
})();

(function FunctionApply() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);

    const taWrite = new ctor(gsab);
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

    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals([0, 1, 2, 3], ToNumbers(func.apply(null, fixedLength)));
    assertEquals([2, 3], ToNumbers(func.apply(null, fixedLengthWithOffset)));
    assertEquals([0, 1, 2, 3, 0, 0],
                 ToNumbers(func.apply(null, lengthTracking)));
    assertEquals([2, 3, 0, 0],
                 ToNumbers(func.apply(null, lengthTrackingWithOffset)));
  }
})();

(function TypedArrayFrom() {
  AllBigIntMatchedCtorCombinations((targetCtor, sourceCtor) => {
    const gsab = CreateGrowableSharedArrayBuffer(
        4 * sourceCtor.BYTES_PER_ELEMENT,
        8 * sourceCtor.BYTES_PER_ELEMENT);
    const fixedLength = new sourceCtor(gsab, 0, 4);
    const fixedLengthWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new sourceCtor(gsab, 0);
    const lengthTrackingWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT);

    // Write some data into the array.
    const taFull = new sourceCtor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taFull, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, ...] << lengthTracking
    //                    [3, 4, ...] << lengthTrackingWithOffset

    assertEquals([1, 2, 3, 4], ToNumbers(targetCtor.from(fixedLength)));
    assertEquals([3, 4], ToNumbers(targetCtor.from(fixedLengthWithOffset)));
    assertEquals([1, 2, 3, 4], ToNumbers(targetCtor.from(lengthTracking)));
    assertEquals([3, 4], ToNumbers(targetCtor.from(lengthTrackingWithOffset)));

    // Grow.
    gsab.grow(6 * sourceCtor.BYTES_PER_ELEMENT);

    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taFull, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4, 5, 6]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, 5, 6, ...] << lengthTracking
    //                    [3, 4, 5, 6, ...] << lengthTrackingWithOffset

    assertEquals([1, 2, 3, 4], ToNumbers(targetCtor.from(fixedLength)));
    assertEquals([3, 4], ToNumbers(targetCtor.from(fixedLengthWithOffset)));
    assertEquals([1, 2, 3, 4, 5, 6],
                 ToNumbers(targetCtor.from(lengthTracking)));
    assertEquals([3, 4, 5, 6],
                 ToNumbers(targetCtor.from(lengthTrackingWithOffset)));
  });

  AllBigIntUnmatchedCtorCombinations((targetCtor, sourceCtor) => {
    const gsab = CreateGrowableSharedArrayBuffer(
        4 * sourceCtor.BYTES_PER_ELEMENT,
        8 * sourceCtor.BYTES_PER_ELEMENT);
    const fixedLength = new sourceCtor(gsab, 0, 4);
    const fixedLengthWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new sourceCtor(gsab, 0);
    const lengthTrackingWithOffset = new sourceCtor(
        gsab, 2 * sourceCtor.BYTES_PER_ELEMENT);

    assertThrows(() => { targetCtor.from(fixedLength); }, TypeError);
    assertThrows(() => { targetCtor.from(fixedLengthWithOffset); }, TypeError);
    assertThrows(() => { targetCtor.from(lengthTracking); }, TypeError);
    assertThrows(() => { targetCtor.from(lengthTrackingWithOffset); },
                 TypeError);
  });
})();

(function ArrayBufferSizeNotMultipleOfElementSize() {
  // The buffer size is a prime, not multiple of anything.
  const gsab = CreateGrowableSharedArrayBuffer(11, 20);
  for (let ctor of ctors) {
    if (ctor.BYTES_PER_ELEMENT == 1) continue;

    // This should not throw.
    new ctor(gsab);
  }
})();

(function SetValueToNumberResizesToInBounds() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(0,
                                                 1 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab, 0);

    const evil = { valueOf: () => {
      // Grow so that `lengthTracking` is no longer OOB.
      gsab.grow(1 * ctor.BYTES_PER_ELEMENT);
      if (IsBigIntTypedArray(lengthTracking)) {
        return 2n;
      }
      return 2;
    }};

    lengthTracking[0] = evil;
    assertEquals([2], ToNumbers(lengthTracking));
  }
})();
