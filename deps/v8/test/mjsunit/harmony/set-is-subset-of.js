// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

(function TestIsSubsetOfSetFirstShorterIsSubset() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(43);
  otherSet.add(47);

  assertEquals(firstSet.isSubsetOf(otherSet), true);
})();

(function TestIsSubsetOfSetFirstShorterIsNotSubset() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  assertEquals(firstSet.isSubsetOf(otherSet), false);
})();

(function TestIsSubsetOfSetSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(44);

  assertEquals(firstSet.isSubsetOf(otherSet), false);
})();

(function TestIsSubsetOfMapFirstShorterIsSubset() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);
  other.set(43);
  other.set(47);

  assertEquals(firstSet.isSubsetOf(other), true);
})();

(function TestIsSubsetOfMapFirstShorterIsNotSubset() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);
  other.set(46);
  other.set(47);

  assertEquals(firstSet.isSubsetOf(other), false);
})();

(function TestIsSubsetOfMapSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const other = new Map();
  other.set(42);
  other.set(43);

  assertEquals(firstSet.isSubsetOf(other), false);
})();

(function TestIsSubsetOfSetLikeObjectFirstShorterIsSubset() {
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
  firstSet.add(45);

  assertEquals(firstSet.isSubsetOf(SetLike), true);
})();

(function TestIsSubsetOfSetLikeObjectFirstShorterIsNotSubset() {
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

  assertEquals(firstSet.isSubsetOf(SetLike), false);
})();

(function TestIsSubsetOfSetLikeObjectSecondShorter() {
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

  assertEquals(firstSet.isSubsetOf(SetLike), false);
})();

(function TestIsSubsetOfSetEqualLengthIsSubset() {
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
  firstSet.add(45);

  assertEquals(firstSet.isSubsetOf(SetLike), true);
})();

(function TestIsSubsetOfSetEqualLengthIsNotSubset() {
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
  firstSet.add(45);

  assertEquals(firstSet.isSubsetOf(SetLike), false);
})();

(function TestIsSubsetOfAfterClearingTheReceiver() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(43);
  otherSet.add(47);

  Object.defineProperty(otherSet, 'size', {
    get: function() {
      firstSet.clear();
      return 3;
    },

  });

  assertEquals(firstSet.isSubsetOf(otherSet), true);
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
      if (key == 42) {
        // Cause a table transition in the receiver.
        firstSet.clear();
      }
      // Return true so we keep iterating the transitioned receiver.
      return true;
    }
  };

  assertTrue(firstSet.isSubsetOf(setLike));
  assertEquals(0, firstSet.size);
})();

(function TestIsSubsetOfAfterRewritingKeys() {
  const firstSet = new Set();
  firstSet.add(42);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(43);

  otherSet.keys =
      () => {
        firstSet.clear();
        return otherSet[Symbol.iterator]();
      }

  assertEquals(firstSet.isSubsetOf(otherSet), true);
})();

(function TestIsSubsetOfAfterRewritingKeys() {
  const firstSet = new Set();
  firstSet.add(42);

  const setLike = {
    arr: [42, 43],
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

  assertEquals(firstSet.isSubsetOf(setLike), true);
})();

(function TestIsSubsetOfSetLikeWithInfiniteSize() {
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

  assertEquals(firstSet.isSubsetOf(setLike), true);
})();

(function TestIsSubsetOfSetLikeWithNegativeInfiniteSize() {
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
    new Set().isSubsetOf(setLike);
  }, RangeError, '\'-Infinity\' is an invalid size');
})();

(function TestIsSubsetOfSetLikeWithLargeSize() {
  let setLike = {
    size: 2 ** 31,
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

  assertEquals(firstSet.isSubsetOf(setLike), true);
})();
