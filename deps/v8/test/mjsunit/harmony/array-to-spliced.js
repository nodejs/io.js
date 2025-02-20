// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertEquals(2, Array.prototype.toSpliced.length);
assertEquals("toSpliced", Array.prototype.toSpliced.name);

function TerribleCopy(input) {
  let copy;
  if (Array.isArray(input)) {
    copy = [...input];
  } else {
    copy = { length: input.length };
    for (let i = 0; i < input.length; i++) {
      copy[i] = input[i];
    }
  }
  return copy;
}

function AssertToSplicedAndSpliceSameResult(input, ...args) {
  const orig = TerribleCopy(input);
  const s = Array.prototype.toSpliced.apply(input, args);
  const copy = TerribleCopy(input);
  Array.prototype.splice.apply(copy, args);

  // The in-place spliced version should be pairwise equal to the toSpliced,
  // modulo being an actual Array if the input is generic.
  if (Array.isArray(input)) {
    assertEquals(copy, s);
  } else {
    assertEquals(copy.length, s.length);
    for (let i = 0; i < copy.length; i++) {
      assertEquals(copy[i], s[i]);
    }
  }

  // The original input should be unchanged.
  assertEquals(orig, input);

  // The result of toSpliced() is a copy.
  assertFalse(s === input);
}

function TestToSplicedBasicBehaviorHelper(input, itemsToInsert) {
  const startIndices = [0, 1, -1, -100, Infinity, -Infinity];
  const deleteCounts = [0, 1, 2, 3];

  AssertToSplicedAndSpliceSameResult(input);

  for (let startIndex of startIndices) {
    AssertToSplicedAndSpliceSameResult(input, startIndex);
    for (let deleteCount of deleteCounts) {
      AssertToSplicedAndSpliceSameResult(input, startIndex, deleteCount);
      AssertToSplicedAndSpliceSameResult(input, startIndex, deleteCount,
                                         ...itemsToInsert);
    }
  }
}

// Smi packed
TestToSplicedBasicBehaviorHelper([1,2,3,4], [5,6]);

// Double packed
TestToSplicedBasicBehaviorHelper([1.1,2.2,3.3,4.4], [5.5,6.6]);

// Packed
TestToSplicedBasicBehaviorHelper([true,false,1,42.42], [null,"foo"]);

// Generic
TestToSplicedBasicBehaviorHelper({ length: 4,
                                   get "0"() { return "hello"; },
                                   get "1"() { return "cursed"; },
                                   get "2"() { return "java"; },
                                   get "3"() { return "script" } },
                                 ["foo","bar"]);

(function TestReadOrder() {
  const order = [];
  const a = { length: 4,
              get "0"() { order.push("1st"); return "1st"; },
              get "1"() { order.push("2nd"); return "2nd"; },
              get "2"() { order.push("3rd"); return "3rd"; },
              get "3"() { order.push("4th"); return "4th"; } };
  const s = Array.prototype.toSpliced.call(a);
  assertEquals(["1st","2nd","3rd","4th"], s);
  assertEquals(["1st","2nd","3rd","4th"], order);
})();

(function TestTooBig() {
  const a = { length: Math.pow(2, 32) };
  assertThrows(() => Array.prototype.toSpliced.call(a), RangeError);
})();

(function TestNoSpecies() {
  class MyArray extends Array {
    static get [Symbol.species]() { return MyArray; }
  }
  assertEquals(Array, (new MyArray()).toSpliced().constructor);
})();

(function TestEmpty() {
  assertEquals([], [].toSpliced());
})();

function TestFastSourceEmpty(input, itemsToInsert) {
  // Create an empty input Array of the same ElementsKind with splice().
  TestToSplicedBasicBehaviorHelper(input.splice(), itemsToInsert);
}

// Packed
TestFastSourceEmpty([1,2,3,4], [5,6]);

// Double packed
TestFastSourceEmpty([1.1,2.2,3.3,4.4], [5.5,6.6]);

// Packed
TestFastSourceEmpty([true,false,1,42.42], [null,"foo"]);

// All tests after this have an invalidated elements-on-prototype protector.
(function TestNoHoles() {
  const a = [,,,,];
  Array.prototype[3] = "on proto";
  const s = a.toSpliced();
  assertEquals([undefined,undefined,undefined,"on proto"], s);
  for (let i = 0; i < a.length; i++) {
    assertFalse(a.hasOwnProperty(i));
    assertTrue(s.hasOwnProperty(i));
  }
})();
(function TestMidIterationShenanigans() {
  {
    const a = { length: 4,
                "0": 1,
                get "1"() { a.length = 1; return 2; },
                "2": 3,
                "3": 4,
                __proto__: Array.prototype };
    // The length is cached before iteration, so mid-iteration resizing does not
    // affect the copied array length.
    const s = a.toSpliced();
    assertEquals(1, a.length);
    assertEquals([1,2,3,4], s);
  }

  {
    // Values can be changed mid-iteration.
    const a = { length: 4,
                "0": 1,
                get "1"() { a[3] = "poof"; return 2; },
                "2": 3,
                "3": 4,
                __proto__: Array.prototype };
    const s = a.toSpliced();
    assertEquals([1,2,3,"poof"], s);
  }
})();

assertEquals(Array.prototype[Symbol.unscopables].toSpliced, true);
