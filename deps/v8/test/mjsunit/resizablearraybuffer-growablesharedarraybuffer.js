// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-float16array

"use strict";

d8.file.execute('test/mjsunit/typedarray-helpers.js');

function resizeHelper(ab, value) {
  const return_value = ab.resize(value);
  assertEquals(undefined, return_value);
  assertEquals(value, ab.byteLength);
}

function growHelper(ab, value) {
  const return_value = ab.grow(value);
  assertEquals(undefined, return_value);
  assertEquals(value, ab.byteLength);
}

(function TestRABBasics() {
  const rab = CreateResizableArrayBuffer(10, 20);
  assertTrue(rab instanceof ArrayBuffer);
  assertFalse(rab instanceof SharedArrayBuffer);
  assertEquals(10, rab.byteLength);
  assertEquals(20, rab.maxByteLength);
})();

(function TestRABCtorByteLengthEqualsMax() {
  const rab = CreateResizableArrayBuffer(10, 10);
  assertEquals(10, rab.byteLength);
  assertEquals(10, rab.maxByteLength);
})();

(function TestRABCtorByteLengthZero() {
  const rab = CreateResizableArrayBuffer(0, 10);
  assertEquals(0, rab.byteLength);
  assertEquals(10, rab.maxByteLength);
})();

(function TestRABCtorByteLengthAndMaxZero() {
  const rab = CreateResizableArrayBuffer(0, 0);
  assertEquals(0, rab.byteLength);
  assertEquals(0, rab.maxByteLength);
})();

const arrayBufferCtors = [[ArrayBuffer, (b) => b.resizable],
                          [SharedArrayBuffer, (b) => b.growable]];

(function TestOptionsBagNotObject() {
  for (let [ctor, resizable] of arrayBufferCtors) {
    const buffer = new ctor(10, 'this is not an options bag');
    assertFalse(resizable(buffer));
  }
})();

(function TestOptionsBagMaxByteLengthGetterThrows() {
  let evil = {};
  Object.defineProperty(evil, 'maxByteLength',
                        {get: () => { throw new Error('thrown'); }});
  for (let [ctor, resizable] of arrayBufferCtors) {
    let caught = false;
    try {
      new ctor(10, evil);
    } catch(e) {
      assertEquals('thrown', e.message);
      caught = true;
    }
    assertTrue(caught);
  }
})();

(function TestMaxByteLengthNonExisting() {
  for (let [ctor, resizable] of arrayBufferCtors) {
    const buffer = new ctor(10, {});
    assertFalse(resizable(buffer));
  }
})();

(function TestMaxByteLengthUndefinedOrNan() {
  for (let [ctor, resizable] of arrayBufferCtors) {
    const buffer1 = new ctor(10, {maxByteLength: undefined});
    assertFalse(resizable(buffer1));
    const buffer2 = new ctor(0, {maxByteLength: NaN});
    assertTrue(resizable(buffer2));
    assertEquals(0, buffer2.byteLength);
    assertEquals(0, buffer2.maxByteLength);
  }
})();

(function TestMaxByteLengthBooleanNullOrString() {
  for (let [ctor, resizable] of arrayBufferCtors) {
    const buffer1 = new ctor(0, {maxByteLength: true});
    assertTrue(resizable(buffer1));
    assertEquals(0, buffer1.byteLength);
    assertEquals(1, buffer1.maxByteLength);
    const buffer2 = new ctor(0, {maxByteLength: false});
    assertTrue(resizable(buffer2));
    assertEquals(0, buffer2.byteLength);
    assertEquals(0, buffer2.maxByteLength);
    const buffer3 = new ctor(0, {maxByteLength: null});
    assertTrue(resizable(buffer3));
    assertEquals(0, buffer3.byteLength);
    assertEquals(0, buffer3.maxByteLength);
    const buffer4 = new ctor(0, {maxByteLength: '100'});
    assertTrue(resizable(buffer4));
    assertEquals(0, buffer4.byteLength);
    assertEquals(100, buffer4.maxByteLength);
  }
})();

(function TestMaxByteLengthDouble() {
  for (let [ctor, resizable] of arrayBufferCtors) {
    const buffer1 = new ctor(0, {maxByteLength: -0.0});
    assertTrue(resizable(buffer1));
    assertEquals(0, buffer1.byteLength);
    assertEquals(0, buffer1.maxByteLength);
    const buffer2 = new ctor(0, {maxByteLength: -0.1});
    assertTrue(resizable(buffer2));
    assertEquals(0, buffer2.byteLength);
    assertEquals(0, buffer2.maxByteLength);
    const buffer3 = new ctor(0, {maxByteLength: 1.2});
    assertTrue(resizable(buffer3));
    assertEquals(0, buffer3.byteLength);
    assertEquals(1, buffer3.maxByteLength);
    assertThrows(() => { new ctor(0, {maxByteLength: -1.5}) });
    assertThrows(() => { new ctor(0, {maxByteLength: -1}) });
  }
})();

(function TestMaxByteLengthThrows() {
  const evil = {valueOf: () => { throw new Error('thrown');}};
  for (let [ctor, resizable] of arrayBufferCtors) {
    let caught = false;
    try {
      new ctor(0, {maxByteLength: evil});
    } catch (e) {
      assertEquals('thrown', e.message);
      caught = true;
    }
    assertTrue(caught);
  }
})();

(function TestByteLengthThrows() {
  const evil1 = {valueOf: () => { throw new Error('byteLength throws');}};
  const evil2 = {valueOf: () => { throw new Error('maxByteLength throws');}};
  for (let [ctor, resizable] of arrayBufferCtors) {
    let caught = false;
    try {
      new ctor(evil1, {maxByteLength: evil2});
    } catch (e) {
      assertEquals('byteLength throws', e.message);
      caught = true;
    }
    assertTrue(caught);
  }
})();

(function TestAllocatingOutrageouslyMuchThrows() {
  assertThrows(() => { CreateResizableArrayBuffer(0, 2 ** 100);}, RangeError);
})();

(function TestRABCtorOperationOrder() {
  let log = '';
  const mock_length = {valueOf: function() {
      log += 'valueof length, '; return 10; }};
  const mock_max_length = {valueOf: function() {
      log += 'valueof max_length, '; return 10; }};
  CreateResizableArrayBuffer(mock_length, mock_max_length);

  assertEquals('valueof length, valueof max_length, ', log);
})();

(function TestGSABCtorOperationOrder() {
  let log = '';
  const mock_length = {valueOf: function() {
      log += 'valueof length, '; return 10; }};
  const mock_max_length = {valueOf: function() {
      log += 'valueof max_length, '; return 10; }};
  CreateResizableArrayBuffer(mock_length, mock_max_length);

  assertEquals('valueof length, valueof max_length, ', log);
})();

(function TestByteLengthGetterReceiverChecks() {
  const name = 'byteLength';
  const ab_getter = Object.getOwnPropertyDescriptor(
      ArrayBuffer.prototype, name).get;
  const sab_getter = Object.getOwnPropertyDescriptor(
      SharedArrayBuffer.prototype, name).get;

  const ab = new ArrayBuffer(40);
  const sab = new SharedArrayBuffer(40);
  const rab = CreateResizableArrayBuffer(40, 40);
  const gsab = CreateGrowableSharedArrayBuffer(40, 40);

  assertEquals(40, ab_getter.call(ab));
  assertEquals(40, ab_getter.call(rab));
  assertEquals(40, sab_getter.call(sab));
  assertEquals(40, sab_getter.call(gsab));

  assertThrows(() => { ab_getter.call(sab);});
  assertThrows(() => { ab_getter.call(gsab);});

  assertThrows(() => { sab_getter.call(ab);});
  assertThrows(() => { sab_getter.call(rab);});
})();

(function TestMaxByteLengthGetterReceiverChecks() {
  const name = 'maxByteLength';
  const ab_getter = Object.getOwnPropertyDescriptor(
      ArrayBuffer.prototype, name).get;
  const sab_getter = Object.getOwnPropertyDescriptor(
      SharedArrayBuffer.prototype, name).get;

  const ab = new ArrayBuffer(40);
  const sab = new SharedArrayBuffer(40);
  const rab = CreateResizableArrayBuffer(20, 40);
  const gsab = CreateGrowableSharedArrayBuffer(20, 40);

  assertEquals(40, ab_getter.call(ab));
  assertEquals(40, ab_getter.call(rab));
  assertEquals(40, sab_getter.call(sab));
  assertEquals(40, sab_getter.call(gsab));

  assertThrows(() => { ab_getter.call(sab);});
  assertThrows(() => { ab_getter.call(gsab);});

  assertThrows(() => { sab_getter.call(ab);});
  assertThrows(() => { sab_getter.call(rab);});
})();

(function TestResizableGetterReceiverChecks() {
  const ab_getter = Object.getOwnPropertyDescriptor(
      ArrayBuffer.prototype, 'resizable').get;
  const sab_getter = Object.getOwnPropertyDescriptor(
      SharedArrayBuffer.prototype, 'growable').get;

  const ab = new ArrayBuffer(40);
  const sab = new SharedArrayBuffer(40);
  const rab = CreateResizableArrayBuffer(40, 40);
  const gsab = CreateGrowableSharedArrayBuffer(40, 40);

  assertEquals(false, ab_getter.call(ab));
  assertEquals(true, ab_getter.call(rab));
  assertEquals(false, sab_getter.call(sab));
  assertEquals(true, sab_getter.call(gsab));

  assertThrows(() => { ab_getter.call(sab);});
  assertThrows(() => { ab_getter.call(gsab);});

  assertThrows(() => { sab_getter.call(ab);});
  assertThrows(() => { sab_getter.call(rab);});
})();

(function TestByteLengthAndMaxByteLengthOfDetached() {
  const rab = CreateResizableArrayBuffer(10, 20);
  %ArrayBufferDetach(rab);
  assertEquals(0, rab.byteLength);
  assertEquals(0, rab.maxByteLength);
})();

(function TestResizeAndGrowReceiverChecks() {
  const rab_resize = ArrayBuffer.prototype.resize;
  const gsab_grow = SharedArrayBuffer.prototype.grow;

  const ab = new ArrayBuffer(40);
  const sab = new SharedArrayBuffer(40);
  const rab = CreateResizableArrayBuffer(10, 40);
  const gsab = CreateGrowableSharedArrayBuffer(10, 40);

  rab_resize.call(rab, 20);
  gsab_grow.call(gsab, 20);
  assertThrows(() => { rab_resize.call(ab, 30);});
  assertThrows(() => { rab_resize.call(sab, 30);});
  assertThrows(() => { rab_resize.call(gsab, 30);});

  assertThrows(() => { gsab_grow.call(ab, 30);});
  assertThrows(() => { gsab_grow.call(sab, 30);});
  assertThrows(() => { gsab_grow.call(rab, 30);});
})();

(function TestRABResizeToMax() {
  const rab = CreateResizableArrayBuffer(10, 20);
  resizeHelper(rab, 20);
})();

(function TestRABResizeToSameSize() {
  const rab = CreateResizableArrayBuffer(10, 20);
  resizeHelper(rab, 10);
})();

(function TestRABResizeToSmaller() {
  const rab = CreateResizableArrayBuffer(10, 20);
  resizeHelper(rab, 5);
})();

(function TestRABResizeToZero() {
  const rab = CreateResizableArrayBuffer(10, 20);
  resizeHelper(rab, 0);
})();

(function TestRABResizeZeroToZero() {
  const rab = CreateResizableArrayBuffer(0, 20);
  resizeHelper(rab, 0);
})();

(function TestRABGrowBeyondMaxThrows() {
  const rab = CreateResizableArrayBuffer(10, 20);
  assertEquals(10, rab.byteLength);
  assertThrows(() => {rab.grow(21)});
  assertEquals(10, rab.byteLength);
})();

(function TestRABResizeMultipleTimes() {
  const rab = CreateResizableArrayBuffer(10, 20);
  const sizes = [15, 7, 7, 0, 8, 20, 20, 10];
  for (let s of sizes) {
    resizeHelper(rab, s);
  }
})();

(function TestRABResizeParameters() {
  const rab = CreateResizableArrayBuffer(10, 20);
  rab.resize('15');
  assertEquals(15, rab.byteLength);
  rab.resize({valueOf: function() { return 16; }});
  assertEquals(16, rab.byteLength);
  rab.resize(17.9);
  assertEquals(17, rab.byteLength);
})();

(function TestRABResizeInvalidParameters() {
  const rab = CreateResizableArrayBuffer(10, 20);
  assertThrows(() => { rab.resize(-1) }, RangeError);
  assertThrows(() => { rab.resize({valueOf: function() {
      throw new Error('length param'); }})});
  assertEquals(10, rab.byteLength);

  // Various non-numbers are converted to NaN which is treated as 0.
  rab.resize('string');
  assertEquals(0, rab.byteLength);
  rab.resize();
  assertEquals(0, rab.byteLength);
})();

(function TestRABResizeDetached() {
  const rab = CreateResizableArrayBuffer(10, 20);
  %ArrayBufferDetach(rab);
  assertThrows(() => { rab.resize(15) }, TypeError);
})();

(function DetachInsideResizeParameterConversion() {
  const rab = CreateResizableArrayBuffer(40, 80);

  const evil = {
    valueOf: () => { %ArrayBufferDetach(rab); return 20; }
  };

  assertThrows(() => { rab.resize(evil); });
})();

(function ResizeInsideResizeParameterConversion() {
  const rab = CreateResizableArrayBuffer(40, 80);

  const evil = {
    valueOf: () => { rab.resize(10); return 20; }
  };

  rab.resize(evil);
  assertEquals(20, rab.byteLength);
})();


(function TestRABNewMemoryAfterResizeInitializedToZero() {
  const maybe_page_size = 4096;
  const rab = CreateResizableArrayBuffer(maybe_page_size, 2 * maybe_page_size);
  const i8a = new Int8Array(rab);
  rab.resize(2 * maybe_page_size);
  for (let i = 0; i < 2 * maybe_page_size; ++i) {
    assertEquals(0, i8a[i]);
  }
})();

(function TestRABMemoryInitializedToZeroAfterShrinkAndGrow() {
  const maybe_page_size = 4096;
  const rab = CreateResizableArrayBuffer(maybe_page_size, 2 * maybe_page_size);
  const i8a = new Int8Array(rab);
  for (let i = 0; i < maybe_page_size; ++i) {
    i8a[i] = 1;
  }
  rab.resize(maybe_page_size / 2);
  rab.resize(maybe_page_size);
  for (let i = maybe_page_size / 2; i < maybe_page_size; ++i) {
    assertEquals(0, i8a[i]);
  }
})();

(function TestGSABBasics() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  assertFalse(gsab instanceof ArrayBuffer);
  assertTrue(gsab instanceof SharedArrayBuffer);
  assertEquals(10, gsab.byteLength);
  assertEquals(20, gsab.maxByteLength);
})();

(function TestGSABCtorByteLengthEqualsMax() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 10);
  assertEquals(10, gsab.byteLength);
  assertEquals(10, gsab.maxByteLength);
})();

(function TestGSABCtorByteLengthZero() {
  const gsab = CreateGrowableSharedArrayBuffer(0, 10);
  assertEquals(0, gsab.byteLength);
  assertEquals(10, gsab.maxByteLength);
})();

(function TestGSABCtorByteLengthAndMaxZero() {
  const gsab = CreateGrowableSharedArrayBuffer(0, 0);
  assertEquals(0, gsab.byteLength);
  assertEquals(0, gsab.maxByteLength);
})();

(function TestAllocatingOutrageouslyMuchThrows() {
  assertThrows(() => { CreateGrowableSharedArrayBuffer(0, 2 ** 100);},
               RangeError);
})();

(function TestGSABGrowToMax() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  assertEquals(10, gsab.byteLength);
  growHelper(gsab, 20);
})();

(function TestGSABGrowToSameSize() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  assertEquals(10, gsab.byteLength);
  growHelper(gsab, 10);
})();

(function TestGSABGrowToSmallerThrows() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  assertEquals(10, gsab.byteLength);
  assertThrows(() => {gsab.grow(5)});
  assertEquals(10, gsab.byteLength);
})();

(function TestGSABGrowToZeroThrows() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  assertEquals(10, gsab.byteLength);
  assertThrows(() => {gsab.grow(0)});
  assertEquals(10, gsab.byteLength);
})();

(function TestGSABGrowBeyondMaxThrows() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  assertEquals(10, gsab.byteLength);
  assertThrows(() => {gsab.grow(21)});
  assertEquals(10, gsab.byteLength);
})();

(function TestGSABGrowMultipleTimes() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  assertEquals(10, gsab.byteLength);
  const sizes = [15, 7, 7, 0, 8, 20, 20, 10];
  for (let s of sizes) {
    const current_size = gsab.byteLength;
    if (s < gsab.byteLength) {
      assertThrows(() => {gsab.grow(s)});
      assertEquals(current_size, gsab.byteLength);
    } else {
      growHelper(gsab, s);
    }
  }
})();

(function TestGSABGrowParameters() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  gsab.grow('15');
  assertEquals(15, gsab.byteLength);
  gsab.grow({valueOf: function() { return 16; }});
  assertEquals(16, gsab.byteLength);
  gsab.grow(17.9);
  assertEquals(17, gsab.byteLength);
})();

(function TestGSABGrowInvalidParameters() {
  const gsab = CreateGrowableSharedArrayBuffer(0, 20);
  assertThrows(() => { gsab.grow(-1) }, RangeError);
  assertThrows(() => { gsab.grow({valueOf: function() {
      throw new Error('length param'); }})});
  assertEquals(0, gsab.byteLength);

  // Various non-numbers are converted to NaN which is treated as 0.
  gsab.grow('string');
  assertEquals(0, gsab.byteLength);
  gsab.grow();
  assertEquals(0, gsab.byteLength);
})();

(function TestGSABMemoryInitializedToZeroAfterGrow() {
  const maybe_page_size = 4096;
  const gsab = CreateGrowableSharedArrayBuffer(maybe_page_size,
                                             2 * maybe_page_size);
  const i8a = new Int8Array(gsab);
  gsab.grow(2 * maybe_page_size);
  assertEquals(2 * maybe_page_size, i8a.length);
  for (let i = 0; i < 2 * maybe_page_size; ++i) {
    assertEquals(0, i8a[i]);
  }
})();

(function GrowGSABOnADifferentThread() {
  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  assertEquals(10, gsab.byteLength);
  function workerCode() {
    function assert(thing) {
      if (!thing) {
        postMessage('error');
      }
    }
    onmessage = function({data:params}) {
      const gsab = params.gsab;
      assert(!(gsab instanceof ArrayBuffer));
      assert(gsab instanceof SharedArrayBuffer);
      assert(10 == gsab.byteLength);
      assert(20 == gsab.maxByteLength);
      gsab.grow(15);
      postMessage('ok');
    }
  }
  const w = new Worker(workerCode, {type: 'function'});
  w.postMessage({gsab: gsab});
  assertEquals('ok', w.getMessage());
  assertEquals(15, gsab.byteLength);
})();

(function Slice() {
  const rab = CreateResizableArrayBuffer(10, 20);
  const sliced1 = rab.slice();
  assertEquals(10, sliced1.byteLength);
  assertTrue(sliced1 instanceof ArrayBuffer);
  assertFalse(sliced1 instanceof SharedArrayBuffer);
  assertFalse(sliced1.resizable);

  const gsab = CreateGrowableSharedArrayBuffer(10, 20);
  const sliced2 = gsab.slice();
  assertEquals(10, sliced2.byteLength);
  assertFalse(sliced2 instanceof ArrayBuffer);
  assertTrue(sliced2 instanceof SharedArrayBuffer);
  assertFalse(sliced2.growable);
})();

(function SliceSpeciesConstructorReturnsResizable() {
  class MyArrayBuffer extends ArrayBuffer {
    static get [Symbol.species]() { return MyResizableArrayBuffer; }
  }

  class MyResizableArrayBuffer extends ArrayBuffer {
    constructor(byteLength) {
      super(byteLength, {maxByteLength: byteLength * 2});
    }
  }

  const ab = new MyArrayBuffer(20);
  const sliced1 = ab.slice();
  assertTrue(sliced1.resizable);

  class MySharedArrayBuffer extends SharedArrayBuffer {
    static get [Symbol.species]() { return MyGrowableSharedArrayBuffer; }
  }

  class MyGrowableSharedArrayBuffer extends SharedArrayBuffer {
    constructor(byteLength) {
      super(byteLength, {maxByteLength: byteLength * 2});
    }
  }

  const sab = new MySharedArrayBuffer(20);
  const sliced2 = sab.slice();
  assertTrue(sliced2.growable);
})();

(function SliceSpeciesConstructorResizes() {
  let rab;
  let resizeWhenConstructorCalled = false;
  class MyArrayBuffer extends ArrayBuffer {
    constructor(...params) {
      super(...params);
      if (resizeWhenConstructorCalled) {
        rab.resize(2);
      }
    }
  }
  rab = new MyArrayBuffer(4, {maxByteLength: 8});
  const taWrite = new Uint8Array(rab);
  for (let i = 0; i < 4; ++i) {
    taWrite[i] = 1;
  }
  assertEquals([1, 1, 1, 1], ToNumbers(taWrite));
  resizeWhenConstructorCalled = true;
  const sliced = rab.slice();
  assertEquals(2, rab.byteLength);
  assertEquals(4, sliced.byteLength);
  assertEquals([1, 1, 0, 0], ToNumbers(new Uint8Array(sliced)));
})();

(function DecommitMemory() {
  const pageSize = 4096;
  const rab = new ArrayBuffer(6 * pageSize, {maxByteLength: 12 * pageSize});
  const ta = new Uint8Array(rab);
  for (let i = 0; i < 6 * pageSize; ++i) {
    ta[i] = 1;
  }
  for (let i = 0; i < 6 * pageSize; ++i) {
    assertEquals(1, ta[i]);
  }

  // This resize decommits.
  rab.resize(2 * pageSize);

  for (let i = 0; i < 2 * pageSize; ++i) {
    assertEquals(1, ta[i]);
  }

  for (let i = 2 * pageSize; i < 6 * pageSize; ++i) {
    assertEquals(undefined, ta[i]);
  }

  // Test that the pages get re-committed and can be used normally.
  rab.resize(12 * pageSize);

  for (let i = 0; i < 2 * pageSize; ++i) {
    assertEquals(1, ta[i]);
  }
  // Test that the decommitted and then again committed memory is zeroed.
  for (let i = 2 * pageSize; i < 12 * pageSize; ++i) {
    assertEquals(0, ta[i]);
  }
  for (let i = 0; i < 12 * pageSize; ++i) {
    ta[i] = 2;
  }
  for (let i = 0; i < 12 * pageSize; ++i) {
    assertEquals(2, ta[i]);
  }
})();
