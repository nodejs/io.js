// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-set-methods

(function TestSymmetricDifferenceSetFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  const resultSet = new Set();
  resultSet.add(43);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(otherSet));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(44);

  const resultSet = new Set();
  resultSet.add(43);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(otherSet));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceMapFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);
  other.set(46);
  other.set(47);

  const resultSet = new Set();
  resultSet.add(43);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(other));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceMapSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const other = new Map();
  other.set(42);
  other.set(43);
  other.set(44);

  const resultSet = new Set();

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(other));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetLikeObjectFirstShorter() {
  const SetLike = {
    arr: [42, 44, 45],
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
  resultSet.add(43);
  resultSet.add(44);
  resultSet.add(45);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(SetLike));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetLikeObjectSecondShorter() {
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
  resultSet.add(44);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(SetLike));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetEqualLength() {
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
  firstSet.add(44);

  const resultSet = new Set();
  resultSet.add(44);
  resultSet.add(45);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(SetLike));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetShrinking() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);
  firstSet.add(45);
  firstSet.add(46);
  firstSet.add(47);
  firstSet.add(48);
  firstSet.add(49);

  const otherSet = new Set();
  otherSet.add(43);
  otherSet.add(44);
  otherSet.add(45);
  otherSet.add(46);
  otherSet.add(47);
  otherSet.add(48);
  otherSet.add(49);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(otherSet));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceGrowFastPath() {
  const s1 = new Set([1, 2, 3]).symmetricDifference(new Set([4, 5, 6]));
  assertEquals([1, 2, 3, 4, 5, 6], Array.from(s1));

  const s2 = new Set([1, 2, 3]).symmetricDifference(new Set([3, 4, 5, 6]));
  assertEquals([1, 2, 4, 5, 6], Array.from(s2));

  const s3 =
      new Set([1, 2, 3]).symmetricDifference(new Map([[4, 4], [5, 5], [6, 6]]));
  assertEquals([1, 2, 3, 4, 5, 6], Array.from(s3));

  const s4 = new Set([1, 2, 3]).symmetricDifference(
      new Map([[3, 3], [4, 4], [5, 5], [6, 6]]));
  assertEquals([1, 2, 4, 5, 6], Array.from(s4));
})();

(function TestSymmetricDifferenceAfterClearingTheReceiver() {
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
  resultSet.add(42);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(otherSet));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceAfterRewritingKeys() {
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

  const resultArray = [42, 46, 47];

  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(otherSet));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetLikeAfterRewritingKeys() {
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

  const resultArray = [42, 46, 47];

  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(setLike));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestEvilIterator() {
  const firstSet = new Set([1,2,3,4]);

  let i = 0;
  const evil = {
    has(v) { return v === 43; },
    keys() {
      return {
        next() {
          if (i++ === 0) {
            firstSet.clear();
            firstSet.add(43);
            firstSet.add(42);
            return { value: 43, done: false };
          } else {
            return { value: undefined, done: true };
          }
        }
      };
    },
    get size() { return 1; }
  };

  assertEquals([1,2,3,4], Array.from(firstSet.symmetricDifference(evil)));
})();
