// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-set-methods

(function TestDifferenceSetFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  const resultSet = new Set();
  resultSet.add(43);

  const resultArray = Array.from(resultSet);
  const differenceArray = Array.from(firstSet.difference(otherSet));

  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceSetSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(45);

  const resultSet = new Set();
  resultSet.add(43);
  resultSet.add(44);

  const resultArray = Array.from(resultSet);
  const differenceArray = Array.from(firstSet.difference(otherSet));

  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceMapFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);
  other.set(46);
  other.set(47);

  const resultSet = new Set();
  resultSet.add(43);

  const resultArray = Array.from(resultSet);
  const differenceArray = Array.from(firstSet.difference(other));

  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceMapSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const other = new Map();
  other.set(42);

  const resultSet = new Set();
  resultSet.add(43);
  resultSet.add(44);

  const resultArray = Array.from(resultSet);
  const differenceArray = Array.from(firstSet.difference(other));

  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceSetLikeObjectFirstShorter() {
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

  const resultArray = Array.from(resultSet);
  const differenceArray = Array.from(firstSet.difference(SetLike));

  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceSetLikeObjectSecondShorter() {
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
  const differenceArray = Array.from(firstSet.difference(SetLike));

  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceSetEqualLength() {
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
  resultSet.add(44);

  const resultArray = Array.from(resultSet);
  const differenceArray = Array.from(firstSet.difference(SetLike));

  assertEquals(arrIndex, [0, 1, -1]);
  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceSetShrinking() {
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
  const differenceArray = Array.from(firstSet.difference(otherSet));

  assertEquals(resultArray, differenceArray);
})();

(function TestIsDifferenceAfterClearingTheReceiver() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(43);

  Object.defineProperty(otherSet, 'size', {
    get: function() {
      firstSet.clear();
      return 1;
    },

  });

  const resultSet = new Set();

  const resultArray = Array.from(resultSet);
  const differenceArray = Array.from(firstSet.difference(otherSet));

  assertEquals(resultArray, differenceArray);
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

  assertEquals([42, 44], Array.from(firstSet.difference(setLike)));
  assertEquals(0, firstSet.size);
})();

(function TestDifferenceAfterRewritingKeys() {
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

  const resultArray = [43];

  const differenceArray = Array.from(firstSet.difference(otherSet));

  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceSetLikeAfterRewritingKeys() {
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

  const resultArray = [43];

  const differenceArray = Array.from(firstSet.difference(setLike));

  assertEquals(resultArray, differenceArray);
})();

(function TestThrowRangeErrorIfSizeIsLowerThanZero() {
  const setLike = {
    arr: [42, 46, 47],
    size: -1,
    keys() {
      return this.arr[Symbol.iterator]();
    },
    has(key) {
      return this.arr.indexOf(key) != -1;
    }
  };

  assertThrows(() => {
    new Set().difference(setLike);
  }, RangeError, "'-1' is an invalid size");
})();

(function TestDifferenceSetLikeWithInfiniteSize() {
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

  const resultSet = new Set();

  const resultArray = Array.from(resultSet);
  const differenceArray = Array.from(firstSet.difference(setLike));

  assertEquals(resultArray, differenceArray);
})();

(function TestDifferenceSetLikeWithNegativeInfiniteSize() {
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
    new Set().difference(setLike);
  }, RangeError, '\'-Infinity\' is an invalid size');
})();
