// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


assertEquals(0, Array.prototype.toReversed.length);
assertEquals("toReversed", Array.prototype.toReversed.name);

(function TestSmiPacked() {
  let a = [1,2,3,4];
  let r = a.toReversed();
  assertEquals([1,2,3,4], a);
  assertEquals([4,3,2,1], r);
  assertFalse(a === r);
})();

(function TestDoublePacked() {
  let a = [1.1,2.2,3.3,4.4];
  let r = a.toReversed();
  assertEquals([1.1,2.2,3.3,4.4], a);
  assertEquals([4.4,3.3,2.2,1.1], r);
  assertFalse(a === r);
})();

(function TestPacked() {
  let a = [true,false,1,42.42];
  let r = a.toReversed();
  assertEquals([true,false,1,42.42], a);
  assertEquals([42.42,1,false,true], r);
  assertFalse(a === r);
})();

(function TestSmiHoley() {
  let a = [1,,3,4];
  let r = a.toReversed();
  assertEquals([1,,3,4], a);
  assertEquals([4,3,undefined,1], r);
  assertFalse(a.hasOwnProperty(1));
  assertTrue(r.hasOwnProperty(2));
  assertFalse(a === r);
})();

(function TestDoubleHoley() {
  let a = [1.1,,3.3,4.4];
  let r = a.toReversed();
  assertEquals([1.1,,3.3,4.4], a);
  assertEquals([4.4,3.3,undefined,1.1], r);
  assertFalse(a.hasOwnProperty(1));
  assertTrue(r.hasOwnProperty(2));
  assertFalse(a === r);
})();

(function TestHoley() {
  let a = [true,false,,1,42.42];
  let r = a.toReversed();
  assertEquals([true,false,,1,42.42], a);
  assertEquals([42.42,1,undefined,false,true], r);
  assertFalse(a.hasOwnProperty(2));
  assertTrue(r.hasOwnProperty(2));
  assertFalse(a === r);
})();

(function TestGeneric() {
  let a = { length: 4,
            get "0"() { return "hello"; },
            get "1"() { return "cursed"; },
            get "2"() { return "java"; },
            get "3"() { return "script" } };
  let r = Array.prototype.toReversed.call(a);
  assertEquals("hello", a[0]);
  assertEquals(["script","java","cursed","hello"], r);
  assertFalse(a === r);
  assertTrue(Array.isArray(r));
  assertEquals(Array, r.constructor);
})();

(function TestReadOrder() {
  let order = [];
  let a = { length: 4,
            get "0"() { order.push("4th"); return "4th"; },
            get "1"() { order.push("3rd"); return "3rd"; },
            get "2"() { order.push("2nd"); return "2nd"; },
            get "3"() { order.push("1st"); return "1st"; } };
  let r = Array.prototype.toReversed.call(a);
  assertEquals(["1st","2nd","3rd","4th"], r);
  assertEquals(["1st","2nd","3rd","4th"], order);
})();

(function TestEmpty() {
  assertEquals([], [].toReversed());
})();

(function TestTooBig() {
  let a = { length: Math.pow(2, 32) };
  assertThrows(() => Array.prototype.toReversed.call(a), RangeError);
})();

(function TestNoSpecies() {
  class MyArray extends Array {
    static get [Symbol.species]() { return MyArray; }
  }
  assertEquals(Array, (new MyArray()).toReversed().constructor);
})();

// All tests after this have an invalidated elements-on-prototype protector.
(function TestNoHoles() {
  let a = [,,,,];
  Array.prototype[3] = "on proto";
  let r = a.toReversed();
  assertEquals(["on proto",undefined,undefined,undefined], r);
  for (let i = 0; i < a.length; i++) {
    assertFalse(a.hasOwnProperty(i));
    assertTrue(r.hasOwnProperty(i));
  }
})();

(function TestMidIterationShenanigans() {
  let a = { length: 4,
            "0": 1,
            get "1"() { a.length = 1; return 2; },
            "2": 3,
            "3": 4,
            __proto__: Array.prototype };
  // The length is cached before iteration, so mid-iteration resizing does not
  // affect the copied array length.
  let r = a.toReversed();
  assertEquals(1, a.length);
  assertEquals([4,3,2,1], r);

  // Values can be changed mid-iteration.
  a = { length: 4,
        "0": 1,
        get "1"() { a[0] = "poof"; return 2; },
        "2": 3,
        "3": 4,
        __proto__: Array.prototype };
  r = a.toReversed();
  assertEquals([4,3,2,"poof"], r);
})();

assertEquals(Array.prototype[Symbol.unscopables].toReversed, true);
