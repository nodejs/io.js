// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-array-find-last
// Flags: --js-float16array

var typedArrayConstructors = [
  Uint8Array,
  Int8Array,
  Uint16Array,
  Int16Array,
  Uint32Array,
  Int32Array,
  Uint8ClampedArray,
  Float16Array,
  Float32Array,
  Float64Array];

for (var constructor of typedArrayConstructors) {

assertEquals(1, constructor.prototype.findLast.length);

var a = new constructor([21, 22, 23, 24]);
assertEquals(undefined, a.findLast(function() { return false; }));
assertEquals(24, a.findLast(function() { return true; }));
assertEquals(undefined, a.findLast(function(val) { return 121 === val; }));
assertEquals(24, a.findLast(function(val) { return 24 === val; }));
assertEquals(23, a.findLast(function(val) { return 23 === val; }), null);
assertEquals(22, a.findLast(function(val) { return 22 === val; }), undefined);


//
// Test predicate is not called when array is empty
//
(function() {
  var a = new constructor([]);
  var l = -1;
  var o = -1;
  var v = -1;
  var k = -1;

  a.findLast(function(val, key, obj) {
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

  var found = a.findLast(function(val, key, obj) {
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
  assertEquals(undefined, found);
})();


//
// Test predicate is called array.length times
//
(function() {
  var a = new constructor([1, 2, 3, 4, 5]);
  var l = 0;
  var found = a.findLast(function() {
    l++;
    return false;
  });

  assertEquals(a.length, l);
  assertEquals(undefined, found);
})();


//
// Test array modifications
//
(function() {
  a = new constructor([1, 2, 3]);
  found = a.findLast(function(val, key) { a[key] = ++val; return false; });
  assertArrayEquals([2, 3, 4], a);
  assertEquals(3, a.length);
  assertEquals(undefined, found);
})();

//
// Test thisArg
//
(function() {
  // Test String as a thisArg
  var found = new constructor([1, 2, 3]).findLast(function(val, key) {
    return this.charAt(Number(key)) === String(val);
  }, "321");
  assertEquals(2, found);

  // Test object as a thisArg
  var thisArg = {
    elementAt: function(key) {
      return this[key];
    }
  };
  Array.prototype.push.apply(thisArg, [3, 2, 1]);

  found = new constructor([1, 2, 3]).findLast(function(val, key) {
    return this.elementAt(key) === val;
  }, thisArg);
  assertEquals(2, found);

  // Create a new object in each function call when receiver is a
  // primitive value. See ECMA-262, Annex C.
  a = [];
  new constructor([1, 2]).findLast(function() { a.push(this) }, "");
  assertTrue(a[0] !== a[1]);

  // Do not create a new object otherwise.
  a = [];
  new constructor([1, 2]).findLast(function() { a.push(this) }, {});
  assertEquals(a[0], a[1]);

  // In strict mode primitive values should not be coerced to an object.
  a = [];
  new constructor([1, 2]).findLast(function() { 'use strict'; a.push(this); }, "");
  assertEquals("", a[0]);
  assertEquals(a[0], a[1]);

})();

// Test exceptions
assertThrows('constructor.prototype.findLast.call(null, function() { })',
  TypeError);
assertThrows('constructor.prototype.findLast.call(undefined, function() { })',
  TypeError);
assertThrows('constructor.prototype.findLast.apply(null, function() { }, [])',
  TypeError);
assertThrows('constructor.prototype.findLast.apply(undefined, function() { }, [])',
  TypeError);
assertThrows('constructor.prototype.findLast.apply([], function() { }, [])',
  TypeError);
assertThrows('constructor.prototype.findLast.apply({}, function() { }, [])',
  TypeError);
assertThrows('constructor.prototype.findLast.apply("", function() { }, [])',
  TypeError);

assertThrows('new constructor([]).findLast(null)', TypeError);
assertThrows('new constructor([]).findLast(undefined)', TypeError);
assertThrows('new constructor([]).findLast(0)', TypeError);
assertThrows('new constructor([]).findLast(true)', TypeError);
assertThrows('new constructor([]).findLast(false)', TypeError);
assertThrows('new constructor([]).findLast("")', TypeError);
assertThrows('new constructor([]).findLast({})', TypeError);
assertThrows('new constructor([]).findLast([])', TypeError);
assertThrows('new constructor([]).findLast(/\d+/)', TypeError);

// Shadowing length doesn't affect findLast, unlike Array.prototype.findLast
a = new constructor([1, 2]);
Object.defineProperty(a, 'length', {value: 1});
var x = 0;
assertEquals(a.findLast(function(elt) { x += elt; return false; }), undefined);
assertEquals(x, 3);
assertEquals(Array.prototype.findLast.call(a,
    function(elt) { x += elt; return false; }), undefined);
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

assertThrows(() => array.findLast(tmp), TypeError);

//
// Test detaching in predicate.
//
(function() {

var array = new constructor([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
var values = [];
assertEquals(array.findLast((value) => {
  values.push(value);
  if (value === 5) {
    %ArrayBufferDetach(array.buffer);
  }
}), undefined);
assertEquals(values, [10, 9, 8, 7, 6, 5, undefined, undefined, undefined, undefined]);

var array = new constructor([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
assertEquals(array.findLast((value, idx) => {
  if (value !== undefined) {
    %ArrayBufferDetach(array.buffer);
  }
  return idx === 0;
}), undefined);
})();
}
