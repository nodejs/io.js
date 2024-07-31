// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-set-methods

(function TestUnionSet() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(45);
  otherSet.add(46);
  otherSet.add(47);

  const resultSet = new Set();
  resultSet.add(42);
  resultSet.add(43);
  resultSet.add(44);
  resultSet.add(45);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const unionArray = Array.from(firstSet.union(otherSet));

  assertEquals(resultArray, unionArray);
})();

(function TestUnionSetWithDuplicates() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(45);
  otherSet.add(44);
  otherSet.add(43);

  const resultSet = new Set();
  resultSet.add(42);
  resultSet.add(43);
  resultSet.add(44);
  resultSet.add(45);

  const resultArray = Array.from(resultSet);
  const unionArray = Array.from(firstSet.union(otherSet));

  assertEquals(resultArray, unionArray);
})();

(function TestUnionMap() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const other = new Map();
  other.set(45);
  other.set(46);
  other.set(47);

  const resultSet = new Set();
  resultSet.add(42);
  resultSet.add(43);
  resultSet.add(44);
  resultSet.add(45);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const unionArray = Array.from(firstSet.union(other));

  assertEquals(resultArray, unionArray);
})();

(function TestUnionSetLikeObject() {
  const SetLike = {
    arr: [45, 46, 47],
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
  resultSet.add(44);
  resultSet.add(45);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const unionArray = Array.from(firstSet.union(SetLike));

  assertEquals(resultArray, unionArray);
})();

(function TestUnionSetLikeObjectNotIterableKeys() {
  const SetLike = {
    arr: [45, 46, 47],
    size: 3,
    keys() {
      return 42;
    },
    has(key) {
      return this.arr.indexOf(key) != -1;
    }
  };

  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  assertThrows(() => {
    firstSet.union(SetLike);
  });
})();

(function TestUnionSetLikeObjectNotCallableKeys() {
  const SetLike = {
    arr: [45, 46, 47],
    size: 3,
    keys: 42,
    has(key) {
      return this.arr.indexOf(key) != -1;
    }
  };

  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  assertThrows(() => {
    firstSet.union(SetLike);
  });
})();

(function TestUnionSetLikeObjectNotCallableHas() {
  const SetLike = {
    arr: [45, 46, 47],
    size: 3,
    keys() {
      return this.arr[Symbol.iterator]();
    },
    has: 42
  };

  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  assertThrows(() => {
    firstSet.union(SetLike);
  });
})();

(function TestUnionAfterClearingTheReceiver() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(45);
  otherSet.add(46);
  otherSet.add(47);

  Object.defineProperty(otherSet, 'size', {
    get: function() {
      firstSet.clear();
      return 3;
    },

  });

  const resultSet = new Set();
  resultSet.add(45);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const unionArray = Array.from(firstSet.union(otherSet));

  assertEquals(resultArray, unionArray);
})();

(function TestUnionAfterRewritingKeys() {
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

  const resultArray = [42, 43, 46, 47];

  const unionArray = Array.from(firstSet.union(otherSet));

  assertEquals(resultArray, unionArray);
})();

(function TestUnionSetLikeAfterRewritingKeys() {
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

  const resultArray = [42, 43, 46, 47];

  const unionArray = Array.from(firstSet.union(setLike));

  assertEquals(resultArray, unionArray);
})();
