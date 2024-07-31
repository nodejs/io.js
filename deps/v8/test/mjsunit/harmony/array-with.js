// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


"use strict";

assertEquals(2, Array.prototype.with.length);
assertEquals("with", Array.prototype.with.name);

const values = [1, 1.1, true, false, undefined, null, "foo", {}];

(function TestSmiPacked() {
  let a = [1,2,3,4];
  for (let v of values) {
    let b = a.with(1, v);
    assertEquals([1,2,3,4], a);
    assertEquals([1,v,3,4], b);
    assertFalse(a === b);
    let c = a.with(-1, v);
    assertEquals([1,2,3,4], a);
    assertEquals([1,2,3,v], c);
    assertFalse(a === c);
  }
})();

(function TestDoublePacked() {
  let a = [1.1,2.2,3.3,4.4];
  for (let v of values) {
    let b = a.with(1, v);
    assertEquals([1.1,2.2,3.3,4.4], a);
    assertEquals([1.1,v,3.3,4.4], b);
    assertFalse(a === b);
    let c = a.with(-1, v);
    assertEquals([1.1,2.2,3.3,4.4], a);
    assertEquals([1.1,2.2,3.3,v], c);
    assertFalse(a === c);
  }
})();

(function TestPacked() {
  let a = [true,false,1,42.42];
  for (let v of values) {
    let b = a.with(1, v);
    assertEquals([true,false,1,42.42], a);
    assertEquals([true,v,1,42.42], b);
    assertFalse(a === b);
    let c = a.with(-1, v);
    assertEquals([true,false,1,42.42], a);
    assertEquals([true,false,1,v], c);
    assertFalse(a === c);
  }
})();

(function TestGeneric() {
  let a = { length: 4,
            get "0"() { return "hello"; },
            get "1"() { return "cursed"; },
            get "2"() { return "java"; },
            get "3"() { return "script" } };
  for (let v of values) {
    let b = Array.prototype.with.call(a, 1, v);
    assertEquals("cursed", a[1]);
    assertEquals(["hello",v,"java","script"], b);
    assertFalse(a === b);
    assertTrue(Array.isArray(b));
    assertEquals(Array, b.constructor);
    let c = Array.prototype.with.call(a, -1, v);
    assertEquals("script", a[a.length-1]);
    assertEquals(["hello","cursed","java",v], c);
    assertFalse(a === c);
    assertTrue(Array.isArray(c));
    assertEquals(Array, c.constructor);
  }
})();

(function TestTooBig() {
  let a = { length: Math.pow(2, 32) };
  assertThrows(() => Array.prototype.with.call(a, 1, 42), RangeError);
})();

(function TestNoSpecies() {
  class MyArray extends Array {
    static get [Symbol.species]() { return MyArray; }
  }
  let a = new MyArray();
  a.length = 4;
  assertEquals(Array, a.with(1,42).constructor);
})();

(function TestOutOfBounds() {
  let a = [1,2,3,4];
  assertThrows(() => { a.with(a.length, 42); }, RangeError);
  assertThrows(() => { a.with(-a.length - 1, 42); }, RangeError);
})();

(function TestIndexShenanigans() {
  let a = [1,2,3,4];
  // The index can resize and do other shenanigans since it's coerced before
  // iteration.
  let b = a.with({ valueOf() { a.length = 2; return 1; } }, 42);
  assertEquals([1,2], a);
  assertEquals([1,42,undefined,undefined], b);

  // Holey.
  a = [1,2,3,4,,,];
  let c = a.with({ valueOf() { a.length = 2; return 1; } }, 42);
  assertEquals([1,2], a);
  assertEquals([1,42,undefined,undefined,undefined,undefined], c);

  a = [1,2,3,4];
  let d = a.with({ valueOf() { a[2] = "barf"; return 1; } }, 42);
  assertEquals([1,2,"barf",4], a);
  assertEquals([1,42,"barf",4], d);
})();

(function TestValuePassthrough() {
  let a = [1,2,3,4];
  // The value isn't used in any meaningful sense by with.
  a.with(1, { valueOf() { assertUnreachable(); } });
})();

// All tests after this have an invalidated elements-on-prototype protector.
(function TestNoHoles() {
  let a = [,,,,];
  Array.prototype[3] = "on proto";
  for (let v of values) {
    let b = a.with(1,v);
    assertEquals([undefined,v,undefined,"on proto"], b);
    assertTrue(b.hasOwnProperty('0'));
    assertTrue(b.hasOwnProperty('1'));
    assertTrue(b.hasOwnProperty('2'));
    assertTrue(b.hasOwnProperty('3'));
  }
})();
