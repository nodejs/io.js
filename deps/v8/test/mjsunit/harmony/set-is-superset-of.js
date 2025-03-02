// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

(function TestIsSupersetOfSetFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(44);

  assertEquals(firstSet.isSupersetOf(otherSet), false);
})();

(function TestIsSupersetOfSetSecondShorterIsSuperset() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);

  assertEquals(firstSet.isSupersetOf(otherSet), true);
})();

(function TestIsSupersetOfSetSecondShorterIsNotSuperset() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(46);

  assertEquals(firstSet.isSupersetOf(otherSet), false);
})();

(function TestIsSupersetOfMapFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);
  other.set(43);
  other.set(47);

  assertEquals(firstSet.isSupersetOf(other), false);
})();

(function TestIsSupersetOfMapSecondShorterIsSuperset() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);

  assertEquals(firstSet.isSupersetOf(other), true);
})();

(function TestIsSupersetOfMapSecondShorterIsNotSuperset() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(44);

  assertEquals(firstSet.isSupersetOf(other), false);
})();

(function TestIsSupersetOfSetLikeObjectFirstShorter() {
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

  assertEquals(firstSet.isSupersetOf(SetLike), false);
})();

(function TestIsSupersetOfSetLikeObjectFirstShorterIsSuperset() {
  const SetLike = {
    arr: [42],
    size: 1,
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

  assertEquals(firstSet.isSupersetOf(SetLike), true);
})();

(function TestIsSupersetOfSetLikeObjectFirstShorterIsNotSuperset() {
  const SetLike = {
    arr: [44],
    size: 1,
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

  assertEquals(firstSet.isSupersetOf(SetLike), false);
})();

(function TestIsSupersetOfSetEqualLengthIsSuperset() {
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

  assertEquals(firstSet.isSupersetOf(SetLike), true);
})();

(function TestIsSupersetOfSetEqualLengthIsNotSuperset() {
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

  assertEquals(firstSet.isSupersetOf(SetLike), false);
})();

(function TestIsSupersetOfAfterClearingTheReceiver() {
  const firstSet = new Set();
  firstSet.add(42);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(44);

  Object.defineProperty(otherSet, 'size', {
    get: function() {
      firstSet.clear();
      return 2;
    },

  });

  assertEquals(firstSet.isSupersetOf(otherSet), false);
})();

(function TestIsSupersetOfAfterRewritingKeys() {
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

  assertEquals(firstSet.isSupersetOf(otherSet), false);
})();

(function TestIsSupersetOfAfterRewritingKeys() {
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

  assertEquals(firstSet.isSupersetOf(setLike), false);
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

  assertTrue(firstSet.isSupersetOf(evil));
})();

(function TestIsSupersetOfSetLikeWithInfiniteSize() {
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

  assertEquals(firstSet.isSupersetOf(setLike), false);
})();

(function TestIsSupersetOfSetLikeWithNegativeInfiniteSize() {
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
    new Set().isSupersetOf(setLike);
  }, RangeError, '\'-Infinity\' is an invalid size');
})();

(function TestIsSupersetOfSetLikeWithLargeSize() {
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

  assertEquals(firstSet.isSupersetOf(setLike), false);
})();
