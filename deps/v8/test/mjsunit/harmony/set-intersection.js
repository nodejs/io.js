// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-set-methods

(function TestIntersectionSetFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(otherSet));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionSetSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(45);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(otherSet));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionMapFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);
  other.set(46);
  other.set(47);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(other));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionMapSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const other = new Map();
  other.set(42);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(other));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionSetLikeObjectFirstShorter() {
  const SetLike = {
    arr: [42, 43, 45],
    size: 3,
    keys() {
      return this.arr[Symbol.iterator]();
    },
    has(key) {
      return this.arr.indexOf(key) != -1;
    }
  };

  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const resultSet = new Set();
  resultSet.add(42);
  resultSet.add(43);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(SetLike));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionSetLikeObjectSecondShorter() {
  const SetLike = {
    arr: [42, 43],
    size: 3,
    keys() {
      return this.arr[Symbol.iterator]();
    },
    has(key) {
      return this.arr.indexOf(key) != -1;
    }
  };

  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const resultSet = new Set();
  resultSet.add(42);
  resultSet.add(43);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(SetLike));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionSetEqualLength() {
  let arrIndex = [];
  const SetLike = {
    arr: [42, 43, 45],
    size: 3,
    keys() {
      return this.arr[Symbol.iterator]();
    },
    has(key) {
      arrIndex.push(this.arr.indexOf(key));
      return this.arr.indexOf(key) != -1;
    }
  };

  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const resultSet = new Set();
  resultSet.add(42);
  resultSet.add(43);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(SetLike));

  assertEquals(arrIndex, [0, 1, -1]);
  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionAfterClearingTheReceiver() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  Object.defineProperty(otherSet, 'size', {
    get: function() {
      firstSet.clear();
      return 3;
    },

  });

  const resultSet = new Set();

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(otherSet));

  assertEquals(resultArray, intersectionArray);
})();

(function TestTableTransition() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const setLike = {
    size: 5,
    keys() {
      return [1, 2, 3, 4, 5].keys();
    },
    has(key) {
      if (key == 43) {
        // Cause a table transition in the receiver.
        firstSet.clear();
        return true;
      }
      return false;
    }
  };

  assertEquals([43], Array.from(firstSet.intersection(setLike)));
  assertEquals(0, firstSet.size);
})();

(function TestIntersectionAfterRewritingKeys() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  otherSet.keys =
      () => {
        firstSet.clear();
        return otherSet[Symbol.iterator]();
      }

  const resultArray = [42];

  const intersectionArray = Array.from(firstSet.intersection(otherSet));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionSetLikeAfterRewritingKeys() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const setLike = {
    arr: [42, 46, 47],
    size: 3,
    keys() {
      return this.arr[Symbol.iterator]();
    },
    has(key) {
      return this.arr.indexOf(key) != -1;
    }
  };

  setLike.keys =
      () => {
        firstSet.clear();
        return setLike.arr[Symbol.iterator]();
      }

  const resultArray = [42];

  const intersectionArray = Array.from(firstSet.intersection(setLike));

  assertEquals(resultArray, intersectionArray);
})();

(function TestEvilBiggerOther() {
  const firstSet = new Set([1,2,3,4]);
  const secondSet = new Set([43]);

  const evil = {
    has(v) { return secondSet.has(v); },
    keys() {
      firstSet.clear();
      firstSet.add(43);
      return secondSet.keys();
    },
    get size() { return secondSet.size; }
  };

  assertEquals([43], Array.from(firstSet.intersection(evil)));
})();

(function TestIntersectionSetLikeWithInfiniteSize() {
  let setLike = {
    size: Infinity,
    has(v) {
      return true;
    },
    keys() {
      throw new Error('Unexpected call to |keys| method');
    },
  };

  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const resultArray = [42, 43];
  const intersectionArray = Array.from(firstSet.intersection(setLike));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionSetLikeWithNegativeInfiniteSize() {
  let setLike = {
    size: -Infinity,
    has(v) {
      return true;
    },
    keys() {
      throw new Error('Unexpected call to |keys| method');
    },
  };

  assertThrows(() => {
    new Set().intersection(setLike);
  }, RangeError, '\'-Infinity\' is an invalid size');
})();
