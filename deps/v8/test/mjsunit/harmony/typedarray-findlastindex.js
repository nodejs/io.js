// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var typedArrayConstructors = [
  Uint8Array,
  Int8Array,
  Uint16Array,
  Int16Array,
  Uint32Array,
  Int32Array,
  Uint8ClampedArray,
  Float32Array,
  Float64Array];

for (var constructor of typedArrayConstructors) {

assertEquals(1, constructor.prototype.findLastIndex.length);

var a = new constructor([21, 22, 23, 24]);
assertEquals(-1, a.findLastIndex(function() { return false; }));
assertEquals(-1, a.findLastIndex(function(val) { return 121 === val; }));
assertEquals(3, a.findLastIndex(function() { return true; }));
assertEquals(1, a.findLastIndex(function(val) { return 22 === val; }), undefined);
assertEquals(2, a.findLastIndex(function(val) { return 23 === val; }), null);
assertEquals(3, a.findLastIndex(function(val) { return 24 === val; }));


//
// Test predicate is not called when array is empty
//
(function() {
  var a = new constructor([]);
  var l = -1;
  var o = -1;
  var v = -1;
  var k = -1;

  a.findLastIndex(function(val, key, obj) {
    o = obj;
    l = obj.length;
    v = val;
    k = key;

    return false;
  });

  assertEquals(-1, l);
  assertEquals(-1, o);
  assertEquals(-1, v);
  assertEquals(-1, k);
})();


//
// Test predicate is called with correct arguments
//
(function() {
  var a = new constructor([5]);
  var l = -1;
  var o = -1;
  var v = -1;
  var k = -1;

  var index = a.findLastIndex(function(val, key, obj) {
    o = obj;
    l = obj.length;
    v = val;
    k = key;

    return false;
  });

  assertArrayEquals(a, o);
  assertEquals(a.length, l);
  assertEquals(5, v);
  assertEquals(0, k);
  assertEquals(-1, index);
})();


//
// Test predicate is called array.length times
//
(function() {
  var a = new constructor([1, 2, 3, 4, 5]);
  var l = 0;

  a.findLastIndex(function() {
    l++;
    return false;
  });

  assertEquals(a.length, l);
})();


//
// Test array modifications
//
(function() {
  a = new constructor([1, 2, 3]);
  a.findLastIndex(function(val, key) { a[key] = ++val; return false; });
  assertArrayEquals([2, 3, 4], a);
  assertEquals(3, a.length);
})();


//
// Test thisArg
//
(function() {
  // Test String as a thisArg
  var index = new constructor([1, 2, 3]).findLastIndex(function(val, key) {
    return this.charAt(Number(key)) === String(val);
  }, "321");
  assertEquals(1, index);

  // Test object as a thisArg
  var thisArg = {
    elementAt: function(key) {
      return this[key];
    }
  };
  Array.prototype.push.apply(thisArg, [3, 2, 1]);

  index = new constructor([1, 2, 3]).findLastIndex(function(val, key) {
    return this.elementAt(key) === val;
  }, thisArg);
  assertEquals(1, index);

  // Create a new object in each function call when receiver is a
  // primitive value. See ECMA-262, Annex C.
  a = [];
  new constructor([1, 2]).findLastIndex(function() { a.push(this) }, "");
  assertTrue(a[0] !== a[1]);

  // Do not create a new object otherwise.
  a = [];
  new constructor([1, 2]).findLastIndex(function() { a.push(this) }, {});
  assertEquals(a[0], a[1]);

  // In strict mode primitive values should not be coerced to an object.
  a = [];
  new constructor([1, 2]).findLastIndex(function() { 'use strict'; a.push(this); }, "");
  assertEquals("", a[0]);
  assertEquals(a[0], a[1]);

})();

// Test exceptions
assertThrows('constructor.prototype.findLastIndex.call(null, function() { })',
  TypeError);
assertThrows('constructor.prototype.findLastIndex.call(undefined, function() { })',
  TypeError);
assertThrows('constructor.prototype.findLastIndex.apply(null, function() { }, [])',
  TypeError);
assertThrows('constructor.prototype.findLastIndex.apply(undefined, function() { }, [])',
  TypeError);
assertThrows('constructor.prototype.findLastIndex.apply([], function() { }, [])',
  TypeError);
assertThrows('constructor.prototype.findLastIndex.apply({}, function() { }, [])',
  TypeError);
assertThrows('constructor.prototype.findLastIndex.apply("", function() { }, [])',
  TypeError);

assertThrows('new constructor([]).findLastIndex(null)', TypeError);
assertThrows('new constructor([]).findLastIndex(undefined)', TypeError);
assertThrows('new constructor([]).findLastIndex(0)', TypeError);
assertThrows('new constructor([]).findLastIndex(true)', TypeError);
assertThrows('new constructor([]).findLastIndex(false)', TypeError);
assertThrows('new constructor([]).findLastIndex("")', TypeError);
assertThrows('new constructor([]).findLastIndex({})', TypeError);
assertThrows('new constructor([]).findLastIndex([])', TypeError);
assertThrows('new constructor([]).findLastIndex(/\d+/)', TypeError);

// Shadowing length doesn't affect findLastIndex, unlike Array.prototype.findLastIndex
a = new constructor([1, 2]);
Object.defineProperty(a, 'length', {value: 1});
var x = 0;
assertEquals(a.findLastIndex(function(elt) { x += elt; return false; }), -1);
assertEquals(x, 3);
assertEquals(Array.prototype.findLastIndex.call(a,
    function(elt) { x += elt; return false; }), -1);
assertEquals(x, 4);

// Detached Operation
var tmp = {
  [Symbol.toPrimitive]() {
    assertUnreachable("Parameter should not be processed when " +
                      "array.[[ViewedArrayBuffer]] is detached.");
    return 0;
  }
};
var array = new constructor([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
%ArrayBufferDetach(array.buffer);
assertThrows(() => array.findLastIndex(tmp), TypeError);

//
// Test detaching in predicate.
//
(function() {

var array = new constructor([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
var values = [];
assertEquals(array.findLastIndex((value, idx) => {
  values.push(value);
  if (value === 5) {
    %ArrayBufferDetach(array.buffer);
  }
}), -1);
assertEquals(values, [10, 9, 8, 7, 6, 5, undefined, undefined, undefined, undefined]);

var array = new constructor([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
assertEquals(array.findLastIndex((value, idx) => {
  if (value !== undefined) {
    %ArrayBufferDetach(array.buffer);
  }
  return idx === 0;
}), 0);
})();
}
