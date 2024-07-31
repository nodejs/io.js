// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var tests = [];

// Tests that will be called with each TypedArray constructor.

tests.push(function TestConstructSmallObject(constr) {
  var myObject = { 0: 5, 1: 6, length: 2 };

  arr = new constr(myObject);

  assertEquals(2, arr.length);
  assertEquals(5, arr[0]);
  assertEquals(6, arr[1]);
});

tests.push(function TestConstructLargeObject(constr) {
  var myObject = {};
  const n = 128;
  for (var i = 0; i < n; i++) {
    myObject[i] = i;
  }
  myObject.length = n;

  arr = new constr(myObject);

  assertEquals(n, arr.length);
  for (var i = 0; i < n; i++) {
    assertEquals(i, arr[i]);
  }
});

tests.push(function TestConstructFromArrayWithSideEffects(constr) {
  var arr = [{ valueOf() { arr[1] = 20; return 1; }}, 2];

  var ta = new constr(arr);

  assertEquals(1, ta[0]);
  assertEquals(2, ta[1]);
});

tests.push(function TestConstructFromArrayWithSideEffectsHoley(constr) {
  var arr = [{ valueOf() { arr[1] = 20; return 1; }}, 2, , 4];

  var ta = new constr(arr);

  assertEquals(1, ta[0]);
  assertEquals(2, ta[1]);
  // ta[2] will be the default value, but we aren't testing that here.
  assertEquals(4, ta[3]);
});

tests.push(function TestConstructFromArrayHoleySmi(constr) {
  var arr = [0, 1, , 3];

  var ta = new constr(arr);

  assertArrayEquals([0, 1, defaultValue(constr), 3], ta);
});

tests.push(function TestConstructFromArrayHoleyDouble(constr) {
  var arr = [0.0, 1.0, , 3.0];

  var ta = new constr(arr);

  assertArrayEquals([0, 1, defaultValue(constr), 3], ta);
});

tests.push(function TestConstructFromArrayHoleySmiWithOtherPrototype(constr) {
  var arr = [0, 1, , 3];
  Object.setPrototypeOf(arr, { 2: 2 });

  var ta = new constr(arr);

  assertArrayEquals([0, 1, 2, 3], ta);
});

tests.push(function TestConstructFromArrayWithProxyPrototype(constr) {
  var arr = [0, 1, , 3];
  var proxy = new Proxy([], {
    get: function(target, name) {
      if (name === Symbol.iterator) return undefined;
      if (name == 2) return 2;
      return target[name];
    }
  });
  Object.setPrototypeOf(arr, proxy);

  var ta = new constr(arr);

  assertArrayEquals([0, 1, 2, 3], ta);
});

tests.push(function TestConstructFromArrayHoleySmiWithSubclass(constr) {
  class SubArray extends Array {}
  var arr = new SubArray(0, 1);
  arr[3] = 3;

  var ta = new constr(arr);

  assertArrayEquals([0, 1, defaultValue(constr), 3], ta);
});

tests.push(function TestConstructFromArrayNoIteratorWithGetter(constr) {
  var arr = [1, 2, 3];
  arr[Symbol.iterator] = undefined;

  Object.defineProperty(arr, "2", {
    get() {
      return 22;
    }
  });

  var ta = new constr(arr);

  assertArrayEquals([1, 2, 22], ta);
});

tests.push(function TestConstructFromArrayNullIterator(constr) {
  var arr = [1, 2, 3];
  arr[Symbol.iterator] = null;

  var ta = new Uint8Array(arr);

  assertArrayEquals([1, 2, 3], ta);
});

tests.push(function TestConstructFromArrayUndefinedIterator(constr) {
  var arr = [1, 2, 3];
  arr[Symbol.iterator] = undefined;

  var ta = new Uint8Array(arr);

  assertArrayEquals([1, 2, 3], ta);
});

tests.push(function TestConstructFromArrayNonCallableIterator(constr) {
  var arr = [1, 2, 3];
  arr[Symbol.iterator] = 1;

  assertThrows(() => new Uint8Array(arr), TypeError);
});

tests.push(function TestConstructFromArray(constr) {
  var n = 64;
  var jsArray = [];
  for (var i = 0; i < n; i++) {
    jsArray[i] = i;
  }

  var arr = new constr(jsArray);

  assertEquals(n, arr.length);
  for (var i = 0; i < n; i++) {
    assertEquals(i, arr[i]);
  }
});

tests.push(function TestConstructFromTypedArray(constr) {
  var n = 64;
  var ta = new constr(n);
  for (var i = 0; i < ta.length; i++) {
    ta[i] = i;
  }

  var arr = new constr(ta);

  assertEquals(n, arr.length);
  for (var i = 0; i < n; i++) {
    assertEquals(i, arr[i]);
  }
});

tests.push(function TestTypedArrayMaxLength(constr) {
  var maxLength = %ArrayBufferMaxByteLength() / constr.BYTES_PER_ELEMENT;
  var myObject = { 0: 5, 1: 6, length: maxLength + 1 };

  assertThrows(() => new constr(myObject), RangeError);
});

tests.push(function TestProxyHoleConverted(constr) {
  var source = {0: 0, 2: 2, length: 3};
  var proxy = new Proxy(source, {});

  var converted = new constr(proxy);

  assertArrayEquals([0, defaultValue(constr), 2], converted);
});

tests.push(function TestProxyToObjectValueOfCalled(constr) {
  var thrower = { valueOf: function() { throw new TypeError(); } };
  var source = {0: 0, 1: thrower, length: 2};
  var proxy = new Proxy(source, {});

  assertThrows(() => new constr(proxy), TypeError);
});

tests.push(function TestObjectValueOfCalled(constr) {
  var thrower = { valueOf: function() { throw new TypeError(); } };

  var obj = {0: 0, 1: thrower, length: 2};
  assertThrows(() => new constr(obj), TypeError);
});

tests.push(function TestSmiPackedArray(constr) {
  var ta = new constr([1, 2, 3, 4, 127]);

  assertEquals(5 * constr.BYTES_PER_ELEMENT, ta.byteLength);
  assertArrayEquals([1, 2, 3, 4, 127], ta);
});

tests.push(function TestOffsetIsUsed(constr) {
  TestOffsetIsUsedRunner(constr, 4);
  TestOffsetIsUsedRunner(constr, 16);
  TestOffsetIsUsedRunner(constr, 32);
  TestOffsetIsUsedRunner(constr, 128);
});

tests.push(function TestLengthIsNonSmiNegativeNumber(constr) {
  var ta = new constr({length: -%MaxSmi() - 2});

  assertEquals(0, ta.length);
});

// Helpers for above tests.

function TestOffsetIsUsedRunner(constr, n) {
  var buffer = new ArrayBuffer(constr.BYTES_PER_ELEMENT * n);

  var whole_ta = new constr(buffer);
  assertEquals(n, whole_ta.length);
  for (var i = 0; i < whole_ta.length; i++) {
    whole_ta[i] = i;
  }

  var half_ta = new constr(buffer, constr.BYTES_PER_ELEMENT * n / 2);
  assertEquals(n / 2, half_ta.length);

  var arr = new constr(half_ta);

  assertEquals(n / 2, arr.length);
  for (var i = 0; i < arr.length; i++) {
    assertEquals(n / 2 + i, arr[i]);
  }
}

function defaultValue(constr) {
  if (constr == Float32Array || constr == Float64Array) return NaN;
  return 0;
}

tests.forEach(f => Test(f));


function Test(func) {
  func(Uint8Array);
  func(Int8Array);
  func(Uint16Array);
  func(Int16Array);
  func(Uint32Array);
  func(Int32Array);
  func(Float32Array);
  func(Float64Array);
  func(Uint8ClampedArray);
}

// Other, standalone tests.

(function TestUint8ClampedIsNotBitCopied() {
  var arr = new Int8Array([-1.0, 0, 1.1, 255, 256]);
  assertArrayEquals([-1, 0, 1, -1, 0], arr);
  var expected = new Uint8ClampedArray([0, 0, 1, 0, 0]);

  var converted = new Uint8ClampedArray(arr);

  assertArrayEquals([0, 0, 1, 0, 0], converted);
})();

(function TestInt8ArrayCopying() {
  var source = new Uint8Array([0, 1, 127, 128, 255, 256]);
  assertArrayEquals([0, 1, 127, 128, 255, 0], source);

  var converted = new Int8Array(source);

  assertArrayEquals([0, 1, 127, -128, -1, 0], converted);
})();

(function TestInt16ArrayCopying() {
  var source = new Uint16Array([0, 1, 32767, 32768, 65535, 65536]);
  assertArrayEquals([0, 1, 32767, 32768, 65535, 0], source);

  var converted = new Int16Array(source);

  assertArrayEquals([0, 1, 32767, -32768, -1, 0], converted);
})();

(function TestInt32ArrayCopying() {
  var source =
      new Uint32Array([0, 1, 2147483647, 2147483648, 4294967295, 4294967296]);
  assertArrayEquals([0, 1, 2147483647, 2147483648, 4294967295, 0], source);

  var converted = new Int32Array(source);

  assertArrayEquals([0, 1, 2147483647, -2147483648, -1, 0], converted);
})();
