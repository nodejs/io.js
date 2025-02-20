// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

function* gen() {
  yield 42;
  yield 43;
}

function* longerGen() {
  yield 42;
  yield 43;
  yield 44;
  yield 45;
}

function TestHelperPrototypeSurface(helper) {
  const proto = Object.getPrototypeOf(helper);
  assertEquals('Iterator Helper', proto[Symbol.toStringTag]);
  assertTrue(Object.hasOwn(proto, 'next'));
  assertTrue(Object.hasOwn(proto, 'return'));
  assertEquals('function', typeof proto.next);
  assertEquals('function', typeof proto.return);
  assertEquals(0, proto.next.length);
  assertEquals(0, proto.return.length);
  assertEquals('next', proto.next.name);
  assertEquals('return', proto.return.name);
}

// --- Test map helper

(function TestMap() {
  const iter = gen();
  assertEquals('function', typeof iter.map);
  assertEquals(1, iter.map.length);
  assertEquals('map', iter.map.name);
  let counters = [];
  const mapIter = iter.map((x, i) => {
    counters.push(i);
    return x*2;
  });
  TestHelperPrototypeSurface(mapIter);
  assertEquals({value: 84, done: false }, mapIter.next());
  assertEquals({value: 86, done: false }, mapIter.next());
  assertEquals([0,1], counters);
  assertEquals({value: undefined, done: true }, mapIter.next());
})();

(function TestIteratorConstructorinMap() {
  const iter = gen();
  const mapIter = iter.map((x, i) => {
    counters.push(i);
    return x * 2;
  });
  TestHelperPrototypeSurface(mapIter);

  assertThrows(() => {
    for (let i = 0; i < 3; i++) {
      function func() {}
      Object.defineProperty(mapIter, 58, {set: func});
    }
  });
})();

(function TestMapNoReturnInIterator() {
  const IteratorNoReturn = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
  };

  Object.setPrototypeOf(IteratorNoReturn, Iterator.prototype);
  const mapIter = IteratorNoReturn.map((x, i) => {
    return x;
  });
  TestHelperPrototypeSurface(mapIter);
  assertEquals({value: 1, done: false}, mapIter.next());
  assertEquals({value: undefined, done: true}, mapIter.return());
})();

(function TestMapThrowInReturnInIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const mapIter = IteratorThrowReturn.map((x, i) => {
    return x;
  });
  assertThrows(() => {
    mapIter.return();
  });
})();

(function TestMapThrowInReturnButExhaustedIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const mapIter = IteratorThrowReturn.map((x, i) => {
    return x;
  });
  assertEquals({value: 1, done: false}, mapIter.next());
  assertEquals({value: 2, done: false}, mapIter.next());
  assertEquals({value: 3, done: false}, mapIter.next());
  assertEquals({value: undefined, done: true}, mapIter.next());
  assertEquals({value: undefined, done: true}, mapIter.return());
})();

(function TestMapReturnNotReturnAfterExhaustion() {
  let returnCount = 0;

  class TestIterator extends Iterator {
    next() {
      return {done: false, value: 1};
    }
    return() {
      ++returnCount;
      return {};
    }
  }

  let iterator = new TestIterator().map((x, i) => {
    return x;
  });
  assertEquals(0, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
})();

(function TestMapMarkIteratorExhaustedAfterThrow() {
  const IteratorThrow = {
    i: 1,
    next() {
      if (this.i == 3) {
        throw new Error('Throw return');
      } else {
        return {value: this.i++, done: false};
      }
    },
  };

  Object.setPrototypeOf(IteratorThrow, Iterator.prototype);
  const mapIter = IteratorThrow.map((x, i) => {
    return x;
  });
  assertEquals({value: 1, done: false}, mapIter.next());
  assertEquals({value: 2, done: false}, mapIter.next());
  assertThrows(() => {
    mapIter.next();
  });
  assertEquals({value: undefined, done: true}, mapIter.next());
})();

(function TestMapNotCallableNext() {
  const iterator = {
    i: 1,
    next() {
      undefined;
    },
    return () {
      return {};
    },
  };

  Object.setPrototypeOf(iterator, Iterator.prototype);
  const mapIter = iterator.map((x, i) => {
    return x;
  });
  assertThrows(() => {
    mapIter.next();
  });
})();

(function TestMapOnNullNext() {
  assertThrows(() => {
    [...Iterator.prototype.map.call({next: null}, (x, i) => {
      return x;
    })];
  }, TypeError);
})();

(function TestMapOnUndefinedNext() {
  assertThrows(() => {
    [...Iterator.prototype.map.call({next: undefined}, (x, i) => {
      return x;
    })];
  }, TypeError);
})();

(function TestMapOnNullThisValue() {
  assertThrows(() => {
    Iterator.prototype.map.call(null, (x, i) => {
      return x;
    });
  }, TypeError);
})();

(function TestMapOnUndefinedThisValue() {
  assertThrows(() => {
    Iterator.prototype.map.call(undefined, (x, i) => {
      return x;
    });
  }, TypeError);
})();

// --- Test filter helper

(function TestFilter() {
  const iter = gen();
  assertEquals('function', typeof iter.filter);
  assertEquals(1, iter.filter.length);
  assertEquals('filter', iter.filter.name);
  const filterIter = iter.filter((x, i) => {
    return x%2 == 0;
  });
  TestHelperPrototypeSurface(filterIter);
  assertEquals({value: 42, done: false }, filterIter.next());
  assertEquals({value: undefined, done: true }, filterIter.next());
})();

(function TestFilterLastElement() {
  const iter = gen();
  const filterIter = iter.filter((x, i) => {
    return x == 43;
  });
  TestHelperPrototypeSurface(filterIter);
  assertEquals({value: 43, done: false }, filterIter.next());
  assertEquals({value: undefined, done: true }, filterIter.next());
})();

(function TestFilterAllElement() {
  const iter = gen();
  const filterIter = iter.filter((x, i) => {
    return x == x;
  });
  TestHelperPrototypeSurface(filterIter);
  assertEquals({value: 42, done: false }, filterIter.next());
  assertEquals({value: 43, done: false }, filterIter.next());
  assertEquals({value: undefined, done: true }, filterIter.next());
})();

(function TestFilterNoElement() {
  const iter = gen();
  const filterIter = iter.filter((x, i) => {
    return x == 0;
  });
  TestHelperPrototypeSurface(filterIter);
  assertEquals({value: undefined, done: true }, filterIter.next());
})();

(function TestIteratorConstructorinFilter() {
  const iter = gen();
  const filterIter = iter.filter((x, i) => {
    return x == 0;
  });
  TestHelperPrototypeSurface(filterIter);

  assertThrows(() => {
    for (let i = 0; i < 3; i++) {
      function func() {}
      Object.defineProperty(filterIter, 58, {set: func});
    }
  });
})();

(function TestFilterThrowInReturnInIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 1) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const filterIter = IteratorThrowReturn.filter((x, i) => {
    return x == 1;
  });
  assertThrows(() => {
    filterIter.return();
  });
})();

(function TestMapThrowInReturnEmptyIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 1) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const filterIter = IteratorThrowReturn.filter((x, i) => {
    return x == 0;
  });
  assertEquals({value: undefined, done: true}, filterIter.next());
  assertEquals({value: undefined, done: true}, filterIter.return());
})();

(function TestMapThrowInReturnButExhaustedIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 1) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const filterIter = IteratorThrowReturn.filter((x, i) => {
    return x == 1;
  });
  assertEquals({value: 1, done: false}, filterIter.next());
  assertThrows(() => {
    filterIter.return();
  });
})();

(function TestFilterReturnNotReturnAfterExhaustion() {
  let returnCount = 0;

  class TestIterator extends Iterator {
    next() {
      return {done: false, value: 1};
    }
    return() {
      ++returnCount;
      return {};
    }
  }

  let iterator = new TestIterator().filter((x, i) => {
    return x == x;
  });
  assertEquals(0, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
})();

(function TestFilterMarkIteratorExhaustedAfterThrow() {
  const IteratorThrow = {
    i: 1,
    next() {
      if (this.i == 3) {
        throw new Error('Throw return');
      } else {
        return {value: this.i++, done: false};
      }
    },
  };

  Object.setPrototypeOf(IteratorThrow, Iterator.prototype);
  const filterIter = IteratorThrow.filter((x, i) => {
    return x == x;
  });
  assertEquals({value: 1, done: false}, filterIter.next());
  assertEquals({value: 2, done: false}, filterIter.next());
  assertThrows(() => {
    filterIter.next();
  });
  assertEquals({value: undefined, done: true}, filterIter.next());
})();

(function TestFilterOnNullNext() {
  assertThrows(() => {
    [...Iterator.prototype.filter.call({next: null}, (x, i) => {
      return x == x;
    })];
  }, TypeError);
})();

(function TestFilterOnUndefinedNext() {
  assertThrows(() => {
    [...Iterator.prototype.filter.call({next: undefined}, (x, i) => {
      return x == x;
    })];
  }, TypeError);
})();

(function TestFilterOnNullThisValue() {
  assertThrows(() => {
    Iterator.prototype.filter.call(null, (x, i) => {
      return x == x;
    });
  }, TypeError);
})();

(function TestFilterOnUndefinedThisValue() {
  assertThrows(() => {
    Iterator.prototype.filter.call(undefined, (x, i) => {
      return x == x;
    });
  }, TypeError);
})();

// --- Test take helper

(function TestTake() {
  const iter = gen();
  assertEquals('function', typeof iter.take);
  assertEquals(1, iter.take.length);
  assertEquals('take', iter.take.name);
  const takeIter = iter.take(1);
  TestHelperPrototypeSurface(takeIter);
  assertEquals({value: 42, done: false }, takeIter.next());
  assertEquals({value: undefined, done: true }, takeIter.next());
})();

(function TestTakeAllElements() {
  const iter = gen();
  const takeIter = iter.take(2);
  TestHelperPrototypeSurface(takeIter);
  assertEquals({value: 42, done: false }, takeIter.next());
  assertEquals({value: 43, done: false }, takeIter.next());
  assertEquals({value: undefined, done: true }, takeIter.next());
})();

(function TestTakeNoElements() {
  const iter = gen();
  const takeIter = iter.take(0);
  TestHelperPrototypeSurface(takeIter);
  assertEquals({value: undefined, done: true }, takeIter.next());
})();

(function TestTakeMoreElements() {
  const iter = gen();
  const takeIter = iter.take(4);
  TestHelperPrototypeSurface(takeIter);
  assertEquals({value: 42, done: false }, takeIter.next());
  assertEquals({value: 43, done: false }, takeIter.next());
  assertEquals({value: undefined, done: true }, takeIter.next());
})();

(function TestTakeNegativeLimit() {
  const iter = gen();
  assertThrows(() => {iter.take(-3);});
})();

(function TestTakeInfinityLimit() {
  const iter = gen();
  const takeIter = iter.take(Number.POSITIVE_INFINITY);
  TestHelperPrototypeSurface(takeIter);
  assertEquals({value: 42, done: false }, takeIter.next());
  assertEquals({value: 43, done: false }, takeIter.next());
  assertEquals({value: undefined, done: true }, takeIter.next());
})();

(function TestTakeReturnInNormalIterator() {
  const NormalIterator = {
    i: 1,
    next() {
      if (this.i <= 3) {
                return {value: this.i++, done: false};
              } else {
                return {value: undefined, done: true};
              }
    },
    return() {return {value: undefined, done: true};},
  };

  Object.setPrototypeOf(NormalIterator, Iterator.prototype);
  const takeIter = NormalIterator.take(1);
  TestHelperPrototypeSurface(takeIter);
  assertEquals({value: 1, done: false }, takeIter.next());
  assertEquals({value: undefined, done: true }, takeIter.next());
})();

(function TestTakeNoReturnInIterator() {
  const IteratorNoReturn = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
  };

  Object.setPrototypeOf(IteratorNoReturn, Iterator.prototype);
  const takeIter = IteratorNoReturn.take(1);
  TestHelperPrototypeSurface(takeIter);
  assertEquals({value: 1, done: false}, takeIter.next());
  assertEquals({value: undefined, done: true}, takeIter.next());
})();

(function TestTakeThrowInReturnInIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const takeIter = IteratorThrowReturn.take(0);
  assertThrows(() => {
    takeIter.next();
  });
})();

(function TestTakeNonObjectReturnInIterator() {
  const IteratorNonObjectReturn = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      return 42;
    },
  };

  Object.setPrototypeOf(IteratorNonObjectReturn, Iterator.prototype);
  const takeIter = IteratorNonObjectReturn.take(0);
  assertThrows(() => {
    takeIter.next();
  });
})();

(function TestIteratorConstructorinTake() {
  const iter = gen();
  const takeIter = iter.take(4);
  TestHelperPrototypeSurface(takeIter);

  assertThrows(() => {
    for (let i = 0; i < 3; i++) {
      function func() {}
      Object.defineProperty(takeIter, 58, {set: func});
    }
  });
})();

(function TestTakeThrowInReturnInIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const takeIter = IteratorThrowReturn.take(5);
  assertEquals({value: 1, done: false}, takeIter.next());
  assertEquals({value: 2, done: false}, takeIter.next());
  assertEquals({value: 3, done: false}, takeIter.next());
  assertEquals({value: undefined, done: true}, takeIter.next());
  assertEquals({value: undefined, done: true}, takeIter.return());
})();

(function TestTakeThrowInReturnButNotExhaustedIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 1) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const takeIter = IteratorThrowReturn.take(1);
  assertEquals({value: 1, done: false}, takeIter.next());
  assertThrows(() => {
    takeIter.return();
  });
})();

(function TestTakeThrowInReturnButExhaustedIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 1) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const takeIter = IteratorThrowReturn.take(1);
  assertEquals({value: 1, done: false}, takeIter.next());
  assertThrows(() => {
    takeIter.return();
  });
})();

(function TestTakeReturnNotReturnAfterExhaustion() {
  let returnCount = 0;

  class TestIterator extends Iterator {
    next() {
      return {done: false, value: 1};
    }
    return() {
      ++returnCount;
      return {};
    }
  }

  let iterator = new TestIterator().take(1);
  assertEquals(0, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
})();

(function TestTakeMarkIteratorExhaustedAfterThrow() {
  const IteratorThrow = {
    i: 1,
    next() {
      if (this.i == 3) {
        throw new Error('Throw return');
      } else {
        return {value: this.i++, done: false};
      }
    },
  };

  Object.setPrototypeOf(IteratorThrow, Iterator.prototype);
  const takeIter = IteratorThrow.take(4);
  assertEquals({value: 1, done: false}, takeIter.next());
  assertEquals({value: 2, done: false}, takeIter.next());
  assertThrows(() => {
    takeIter.next();
  });
  assertEquals({value: undefined, done: true}, takeIter.next());
})();

(function TestTakeOnNullNext() {
  assertThrows(() => {
    [...Iterator.prototype.take.call({next: null}, 1)];
  }, TypeError);
})();

(function TestTakeOnUndefinedNext() {
  assertThrows(() => {
    [...Iterator.prototype.take.call({next: undefined}, 1)];
  }, TypeError);
})();

(function TestTakeOnNullThisValue() {
  assertThrows(() => {
    Iterator.prototype.take.call(null, 1);
  }, TypeError);
})();

(function TestTakeOnUndefinedThisValue() {
  assertThrows(() => {
    Iterator.prototype.take.call(undefined, 1);
  }, TypeError);
})();

// --- Test drop helper

(function TestDrop() {
  const iter = longerGen();
  assertEquals('function', typeof iter.drop);
  assertEquals(1, iter.drop.length);
  assertEquals('drop', iter.drop.name);
  const dropIter = iter.drop(1);
  TestHelperPrototypeSurface(dropIter);
  assertEquals({value: 43, done: false}, dropIter.next());
  assertEquals({value: 44, done: false}, dropIter.next());
  assertEquals({value: 45, done: false}, dropIter.next());
  assertEquals({value: undefined, done: true}, dropIter.next());
})();

(function TestDropAllElements() {
  const iter = longerGen();
  const dropIter = iter.drop(4);
  TestHelperPrototypeSurface(dropIter);
  assertEquals({value: undefined, done: true}, dropIter.next());
})();

(function TestDropNoElements() {
  const iter = longerGen();
  const dropIter = iter.drop(0);
  TestHelperPrototypeSurface(dropIter);
  assertEquals({value: 42, done: false}, dropIter.next());
  assertEquals({value: 43, done: false}, dropIter.next());
  assertEquals({value: 44, done: false}, dropIter.next());
  assertEquals({value: 45, done: false}, dropIter.next());
  assertEquals({value: undefined, done: true}, dropIter.next());
})();

(function TestDropNegativeLimit() {
  const iter = longerGen();
  assertThrows(() => {
    iter.drop(-3);
  });
})();

(function TestDropInfinityLimit() {
  const iter = longerGen();
  const dropIter = iter.drop(Number.POSITIVE_INFINITY);
  TestHelperPrototypeSurface(dropIter);
  assertEquals({value: undefined, done: true}, dropIter.next());
})();

(function TestIteratorConstructorinDrop() {
  const iter = gen();
  const dropIter = iter.drop(1);
  TestHelperPrototypeSurface(dropIter);

  assertThrows(() => {
    for (let i = 0; i < 3; i++) {
      function func() {}
      Object.defineProperty(dropIter, 58, {set: func});
    }
  });
})();

(function TestDropReturnThrows() {
  class TestIterator extends Iterator {
    next() {
      return {done: false, value: 1};
    }
    get return() {
      throw new Test262Error;
    }
  }

  let iterator = new TestIterator().drop(1);
  iterator.next();
  assertThrows(() => {iterator.return()});
})();

(function TestDropReturnNotReturnAfterExhaustion() {
  let returnCount = 0;

  class TestIterator extends Iterator {
    next() {
      return {done: false, value: 1};
    }
    return() {
      ++returnCount;
      return {};
    }
  }

  let iterator = new TestIterator().drop(0);
  assertEquals(0, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
  iterator.return();
  assertEquals(1, returnCount);

  returnCount = 0;

  iterator = new TestIterator().drop(1);
  assertEquals(0, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
  iterator.return();
  assertEquals(1, returnCount);

  returnCount = 0;

  iterator = new TestIterator().drop(1).drop(1).drop(1).drop(1).drop(1);
  assertEquals(0, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
})();

(function TestDropOnExhausetdIterator() {
  class TestIterator extends Iterator {
    next() {
      return {done: true, value: undefined};
    }
    return() {
      throw new Error('Throw return');
    }
  }

  let iterator = new TestIterator().drop(0);
  assertThrows(() => {iterator.return()});
  iterator.next();
  iterator.return();

  iterator = new TestIterator().drop(1);
  iterator.next();
  iterator.return();

  iterator = new TestIterator().drop(1);
  assertThrows(() => {iterator.return()});
  iterator.next();
  iterator.return();

  iterator = new TestIterator().drop(1).drop(1).drop(1).drop(1).drop(1);
  assertThrows(() => {iterator.return()});
  iterator.next();
  iterator.return();
})();

(function TestDropMarkIteratorExhaustedAfterThrow() {
  const IteratorThrow = {
    i: 1,
    next() {
      if (this.i == 3) {
        throw new Error('Throw return');
      } else {
        return {value: this.i++, done: false};
      }
    },
  };

  Object.setPrototypeOf(IteratorThrow, Iterator.prototype);
  const dropIter = IteratorThrow.drop(1);
  assertEquals({value: 2, done: false}, dropIter.next());
  assertThrows(() => {
    dropIter.next();
  });
  assertEquals({value: undefined, done: true}, dropIter.next());
})();

(function TestDropOnNullNext() {
  assertThrows(() => {
    [...Iterator.prototype.drop.call({next: null}, 1)];
  }, TypeError);
})();

(function TestDropOnUndefinedNext() {
  assertThrows(() => {
    [...Iterator.prototype.drop.call({next: undefined}, 1)];
  }, TypeError);
})();

(function TestDropOnNullThisValue() {
  assertThrows(() => {
    Iterator.prototype.drop.call(null, 1);
  }, TypeError);
})();

(function TestDropOnUndefinedThisValue() {
  assertThrows(() => {
    Iterator.prototype.drop.call(undefined, 1);
  }, TypeError);
})();

// --- Test flatMap helper

(function TestFlatMap() {
  const iter = ['It\'s Sunny in', '', 'California'].values();
  assertEquals('function', typeof iter.flatMap);
  assertEquals(1, iter.flatMap.length);
  assertEquals('flatMap', iter.flatMap.name);
  const flatMapIter = iter.flatMap(value => value.split(' ').values());
  TestHelperPrototypeSurface(flatMapIter);
  assertEquals({value: 'It\'s', done: false}, flatMapIter.next());
  assertEquals({value: 'Sunny', done: false}, flatMapIter.next());
  assertEquals({value: 'in', done: false}, flatMapIter.next());
  assertEquals({value: '', done: false}, flatMapIter.next());
  assertEquals({value: 'California', done: false}, flatMapIter.next());
  assertEquals({value: undefined, done: true}, flatMapIter.next());
})();

(function TestFlatMapShortInnerIterators() {
  const iter = ['It\'s', 'Sunny', 'in', '', 'California'].values();
  const flatMapIter = iter.flatMap(value => value.split(' ').values());
  TestHelperPrototypeSurface(flatMapIter);
  assertEquals({value: 'It\'s', done: false}, flatMapIter.next());
  assertEquals({value: 'Sunny', done: false}, flatMapIter.next());
  assertEquals({value: 'in', done: false}, flatMapIter.next());
  assertEquals({value: '', done: false}, flatMapIter.next());
  assertEquals({value: 'California', done: false}, flatMapIter.next());
  assertEquals({value: undefined, done: true}, flatMapIter.next());
})();

(function TestFlatMapLongInnerIterators() {
  const iter = ['It\'s Sunny', 'in California'].values();
  const flatMapIter = iter.flatMap(value => value.split(' ').values());
  TestHelperPrototypeSurface(flatMapIter);
  assertEquals({value: 'It\'s', done: false}, flatMapIter.next());
  assertEquals({value: 'Sunny', done: false}, flatMapIter.next());
  assertEquals({value: 'in', done: false}, flatMapIter.next());
  assertEquals({value: 'California', done: false}, flatMapIter.next());
  assertEquals({value: undefined, done: true}, flatMapIter.next());
})();

(function TestFlatMapThrowReturnInnerIterator() {
  const outerIterator = {
    i: 1,
    next() {
      if (this.i == 1) {
        const innerIterator = {
          j: 1,
          next() {
            if (this.j <= 2) {
              return {value: this.j++, done: false};
            } else {
              return {value: undefined, done: true};
            }
          },
          return () {
            throw new Error('Throw return');
          },
        };
        Object.setPrototypeOf(
            innerIterator,
            Object.getPrototypeOf(
                Object.getPrototypeOf([][Symbol.iterator]())));
        this.i++;
        return {value: innerIterator, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      return {value: undefined, done: true};
    },
  };
  Object.setPrototypeOf(outerIterator, Iterator.prototype);
  const flatMapIter = outerIterator.flatMap(value => value);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertEquals({value: 2, done: false}, flatMapIter.next());
  assertThrows(() => {flatMapIter.return()});
})();

(function TestFlatMapThrowReturnOuterIterator() {
  const outerIterator = {
    i: 1,
    next() {
      if (this.i == 1) {
        const innerIterator = {
          j: 1,
          next() {
            if (this.j <= 2) {
              return {value: this.j++, done: false};
            } else {
              return {value: undefined, done: true};
            }
          },
          return () {
            return {value: undefined, done: true};
          },
        };
        Object.setPrototypeOf(
            innerIterator,
            Object.getPrototypeOf(
                Object.getPrototypeOf([][Symbol.iterator]())));
        this.i++;
        return {value: innerIterator, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };
  Object.setPrototypeOf(outerIterator, Iterator.prototype);
  const flatMapIter = outerIterator.flatMap(value => value);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertEquals({value: 2, done: false}, flatMapIter.next());
  assertThrows(() => {flatMapIter.return()});
})();

(function TestFlatMapNonObjectReturnInnerIterator() {
  const outerIterator = {
    i: 1,
    next() {
      if (this.i == 1) {
        const innerIterator = {
          j: 1,
          next() {
            if (this.j <= 2) {
              return {value: this.j++, done: false};
            } else {
              return {value: undefined, done: true};
            }
          },
          return () {
            return 42;
          },
        };
        Object.setPrototypeOf(
            innerIterator,
            Object.getPrototypeOf(
                Object.getPrototypeOf([][Symbol.iterator]())));
        this.i++;
        return {value: innerIterator, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      return {value: undefined, done: true};
    },
  };
  Object.setPrototypeOf(outerIterator, Iterator.prototype);
  const flatMapIter = outerIterator.flatMap(value => value);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertEquals({value: 2, done: false}, flatMapIter.next());
  assertThrows(() => {flatMapIter.return()});
})();

(function TestFlatMapNonObjectReturnOuterIterator() {
  const outerIterator = {
    i: 1,
    next() {
      if (this.i == 1) {
        const innerIterator = {
          j: 1,
          next() {
            if (this.j <= 2) {
              return {value: this.j++, done: false};
            } else {
              return {value: undefined, done: true};
            }
          },
          return () {
            return {value: undefined, done: true};
          },
        };
        Object.setPrototypeOf(
            innerIterator,
            Object.getPrototypeOf(
                Object.getPrototypeOf([][Symbol.iterator]())));
        this.i++;
        return {value: innerIterator, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      return 42;
    },
  };
  Object.setPrototypeOf(outerIterator, Iterator.prototype);
  const flatMapIter = outerIterator.flatMap(value => value);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertEquals({value: 2, done: false}, flatMapIter.next());
  assertThrows(() => {flatMapIter.return()});
})();

(function TestFlatMapThrowReturnBothIterators() {
  const outerIterator = {
    i: 1,
    next() {
      if (this.i == 1) {
        const innerIterator = {
          j: 1,
          next() {
            if (this.j <= 2) {
              return {value: this.j++, done: false};
            } else {
              return {value: undefined, done: true};
            }
          },
          return () {
            throw new Error('Throw return');
          },
        };
        Object.setPrototypeOf(
            innerIterator,
            Object.getPrototypeOf(
                Object.getPrototypeOf([][Symbol.iterator]())));
        this.i++;
        return {value: innerIterator, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };
  Object.setPrototypeOf(outerIterator, Iterator.prototype);
  const flatMapIter = outerIterator.flatMap(value => value);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertEquals({value: 2, done: false}, flatMapIter.next());
  assertThrows(() => {flatMapIter.return()});
})();

(function TestFlatMapNonObjectReturnBothIterators() {
  const outerIterator = {
    i: 1,
    next() {
      if (this.i == 1) {
        const innerIterator = {
          j: 1,
          next() {
            if (this.j <= 2) {
              return {value: this.j++, done: false};
            } else {
              return {value: undefined, done: true};
            }
          },
          return () {
            return 42;
          },
        };
        Object.setPrototypeOf(
            innerIterator,
            Object.getPrototypeOf(
                Object.getPrototypeOf([][Symbol.iterator]())));
        this.i++;
        return {value: innerIterator, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      return 42;
    },
  };
  Object.setPrototypeOf(outerIterator, Iterator.prototype);
  const flatMapIter = outerIterator.flatMap(value => value);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertEquals({value: 2, done: false}, flatMapIter.next());
  assertThrows(() => {flatMapIter.return()});
})();

(function TestIteratorConstructorinFlatMap() {
  const iter = ['It\'s Sunny', 'in California'].values();
  const flatMapIter = iter.flatMap(value => value.split(' ').values());
  TestHelperPrototypeSurface(flatMapIter);

  assertThrows(() => {
    for (let i = 0; i < 3; i++) {
      function func() {}
      Object.defineProperty(flatMapIter, 58, {set: func});
    }
  });
})();

(function TestIteratorReturnInIteratorsFlatMapwithNoInnerIterator() {
  let returnCount = 0;

  class TestIterator extends Iterator {
    next() {
      return {done: false, value: 1};
    }
    return() {
      ++returnCount;
      return {};
    }
  }

  let iterator = new TestIterator().flatMap(() => []);
  assertEquals(0, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
  iterator.return();
  assertEquals(1, returnCount);
})();

(function TestFlatMapThrowInReturnWithNoInnerIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const flatMapIter = IteratorThrowReturn.flatMap(x => [x]);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertEquals({value: 2, done: false}, flatMapIter.next());
  assertEquals({value: 3, done: false}, flatMapIter.next());
  assertEquals({value: undefined, done: true}, flatMapIter.next());
  assertEquals({value: undefined, done: true}, flatMapIter.return());
})();

(function TestFlatMapThrowInReturnButNotExhaustedIteratorWithNoInnerIterator() {
  const IteratorThrowReturn = {
    i: 1,
    next() {
      if (this.i <= 1) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(IteratorThrowReturn, Iterator.prototype);
  const flatMapIter = IteratorThrowReturn.flatMap(x => [x]);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertThrows(() => {
    flatMapIter.return();
  });
})();

(function TestFlatMapMarkIteratorExhaustedAfterThrow() {
  const IteratorThrow = {
    i: 1,
    next() {
      if (this.i == 3) {
        throw new Error('Throw return');
      } else {
        return {value: this.i++, done: false};
      }
    },
  };

  Object.setPrototypeOf(IteratorThrow, Iterator.prototype);
  const flatMapIter = IteratorThrow.flatMap(x => [x]);
  assertEquals({value: 1, done: false}, flatMapIter.next());
  assertEquals({value: 2, done: false}, flatMapIter.next());
  assertThrows(() => {
    flatMapIter.next();
  });
  assertEquals({value: undefined, done: true}, flatMapIter.next());
})();

(function TestFlatMapOnNullNext() {
  assertThrows(() => {
    [...Iterator.prototype.flatMap.call({next: null}, x => [x])];
  }, TypeError);
})();

(function TestFlatMapOnUndefinedNext() {
  assertThrows(() => {
    [...Iterator.prototype.flatMap.call({next: undefined}, x => [x])];
  }, TypeError);
})();

(function TestFlatMapOnNullThisValue() {
  assertThrows(() => {
    Iterator.prototype.flatMap.call(null, x => [x]);
  }, TypeError);
})();

(function TestFlatMapOnUndefinedThisValue() {
  assertThrows(() => {
    Iterator.prototype.flatMap.call(undefined, x => [x]);
  }, TypeError);
})();

// --- Test reduce helper

(function TestReduceWithInitialValue() {
  const iter = gen();
  assertEquals('function', typeof iter.reduce);
  assertEquals(1, iter.reduce.length);
  assertEquals('reduce', iter.reduce.name);
  const reduceResult = iter.reduce((x, sum) => {
    return sum + x;
  }, 1);
  assertEquals(86, reduceResult);
})();

(function TestReduceWithoutInitialvalue() {
  const iter = gen();
  const reduceResult = iter.reduce((x, sum) => {
    return sum + x;
  });
  assertEquals(85, reduceResult);
})();

(function TestReduceWithoutInitialvalueAndNoIteratorValue() {
  const iterator = {
    i: 1,
    next() {
      if (this.i == 1) {
        return {value: undefined, done: true};
      }
    },
    return () {
      return {value: undefined, done: true};
    },
  };
  Object.setPrototypeOf(iterator, Iterator.prototype);

  assertThrows(() => {iterator.reduce((x, sum) => {
                 return sum + x;
               })});
})();

// --- Test toArray helper

(function TestToArray() {
  const iter = gen();
  assertEquals('function', typeof iter.toArray);
  assertEquals(0, iter.toArray.length);
  assertEquals('toArray', iter.toArray.name);
  const toArrayResult = iter.toArray();
  assertEquals([42, 43], toArrayResult);
})();

(function TestToArrayEmptyIterator() {
  const iter = gen();
  const toArrayResult = iter.take(0).toArray();
  assertEquals([], toArrayResult);
})();

// --- Test forEach helper

(function TestForEach() {
  const iter = gen();
  assertEquals('function', typeof iter.forEach);
  assertEquals(1, iter.forEach.length);
  assertEquals('forEach', iter.forEach.name);

  const log = [];
  const fn = (value) => log.push(value);

  assertEquals(undefined, iter.forEach(fn));
  assertEquals([42, 43], log);
})();

// --- Test some helper

(function TestSome() {
  const iter = gen();
  assertEquals('function', typeof iter.some);
  assertEquals(1, iter.some.length);
  assertEquals('some', iter.some.name);

  assertEquals(true, iter.some(v => v == 42));
})();

(function TestSomeOnConsumedIterator() {
  const iter = gen();

  assertEquals(true, iter.some(v => v == 42));
  assertEquals(false, iter.some(v => v == 43));
})();

(function TestSomeOnNotConsumedIterator() {
  const iter = gen();
  const secondIter = gen();

  assertEquals(true, iter.some(v => v == 43));
  assertEquals(true, secondIter.some(v => v == 42));
})();

(function TestSomeOnIteratorWithThrowAsReturn() {
  const iter = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(iter, Iterator.prototype);
  assertThrows(() => {iter.some(v => v == 1)});
})();

// --- Test every helper

(function TestEvery() {
  const iter = gen();
  assertEquals('function', typeof iter.every);
  assertEquals(1, iter.every.length);
  assertEquals('every', iter.every.name);

  assertEquals(true, iter.every(v => v >= 42));
})();

(function TestEveryOnConsumedIterator() {
  const iter = gen();

  assertEquals(true, iter.every(v => v >= 42));
  assertEquals(true, iter.every(v => false));
})();

(function TestEveryOnNotConsumedIterator() {
  const iter = gen();
  const secondIter = gen();

  assertEquals(false, iter.every(v => v >= 43));
  assertEquals(true, secondIter.every(v => v >= 42));
})();

(function TestEveryOnIteratorWithThrowAsReturn() {
  const iter = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(iter, Iterator.prototype);
  assertThrows(() => {iter.every(v => v == 0)});
})();

// --- Test find helper

(function TestFind() {
  const iter = gen();
  assertEquals('function', typeof iter.find);
  assertEquals(1, iter.find.length);
  assertEquals('find', iter.find.name);

  assertEquals(42, iter.find(v => v >= 42));
})();

(function TestFindNoValueFound() {
  const iter = gen();

  assertEquals(undefined, iter.find(v => v > 44));
})();

(function TestFindOnConsumedIterator() {
  const iter = gen();

  assertEquals(42, iter.find(v => v >= 42));
  assertEquals(undefined, iter.find(v => v == 43));
})();

(function TestFindOnNotConsumedIterator() {
  const iter = gen();
  const secondIter = gen();

  assertEquals(43, iter.find(v => v >= 43));
  assertEquals(42, secondIter.find(v => v >= 42));
})();

(function TestFindOnIteratorWithThrowAsReturn() {
  const iter = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(iter, Iterator.prototype);
  assertThrows(() => {iter.find(v => v == 1)});
})();

(function TestFindOnIteratorWithThrowAsReturnNoValueFound() {
  const iter = {
    i: 1,
    next() {
      if (this.i <= 3) {
        return {value: this.i++, done: false};
      } else {
        return {value: undefined, done: true};
      }
    },
    return () {
      throw new Error('Throw return');
    },
  };

  Object.setPrototypeOf(iter, Iterator.prototype);
  assertEquals(undefined, iter.find(v => v > 4));
})();

// --- Test toStringTag and constructor
// https://github.com/tc39/test262/pull/3970

(function TestIteratorPrototypeToStringTag() {
  const descriptor =
      Object.getOwnPropertyDescriptor(Iterator.prototype, Symbol.toStringTag);
  assertTrue(descriptor.configurable);
  assertFalse(descriptor.enumerable);
  assertEquals(typeof descriptor.get, 'function');
  assertEquals(typeof descriptor.set, 'function');
  assertEquals(descriptor.value, undefined);
  assertEquals(descriptor.writable, undefined);
})();

(function TestIteratorPrototypeToStringTagSetter() {
  let IteratorPrototype =
      Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]()))
  let GeneratorPrototype = Object.getPrototypeOf(function*() {}.prototype);

  let sentinel = 'a';

  let {get, set} =
      Object.getOwnPropertyDescriptor(Iterator.prototype, Symbol.toStringTag);

  assertEquals(Iterator.prototype[Symbol.toStringTag], 'Iterator');
  assertEquals(get.call(), 'Iterator');

  // 1. If _this_ is not an Object, then
  //   a. Throw a *TypeError* exception.
  assertThrows(() => set.call(undefined, ''));
  assertThrows(() => set.call(null, ''));
  assertThrows(() => set.call(true, ''));

  // 2. If _this_ is _home_, then
  //   a. NOTE: Throwing here emulates assignment to a non-writable data
  //   property on the _home_ object in strict mode code. b. Throw a *TypeError*
  //   exception.
  assertThrows(() => set.call(IteratorPrototype, ''));
  assertThrows(() => IteratorPrototype[Symbol.toStringTag] = '');

  assertEquals(Iterator.prototype[Symbol.toStringTag], 'Iterator');
  assertEquals(get.call(), 'Iterator');

  // 3. If _desc_ is *undefined*, then
  //   a. Perform ? CreateDataPropertyOrThrow(_this_, _p_, _v_).
  let o = {};
  set.call(o, sentinel);
  assertEquals(o[Symbol.toStringTag], sentinel);

  assertEquals(Iterator.prototype[Symbol.toStringTag], 'Iterator');
  assertEquals(get.call(), 'Iterator');

  // 4. Else,
  //   a. Perform ? Set(_this_, _p_, _v_, *true*).
  let proto = Object.create(IteratorPrototype);
  proto[Symbol.toStringTag] = sentinel;
  assertEquals(proto[Symbol.toStringTag], sentinel);

  assertEquals(Iterator.prototype[Symbol.toStringTag], 'Iterator');
  assertEquals(get.call(), 'Iterator');
})();

(function TestIteratorPrototypeConstructor() {
  const descriptor =
      Object.getOwnPropertyDescriptor(Iterator.prototype, 'constructor');
  assertTrue(descriptor.configurable);
  assertFalse(descriptor.enumerable);
  assertEquals(typeof descriptor.get, 'function');
  assertEquals(typeof descriptor.set, 'function');
  assertEquals(descriptor.value, undefined);
  assertEquals(descriptor.writable, undefined);
})();

(function TestIteratorPrototypeConstructorSetter() {
  let IteratorPrototype =
      Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]()))
  let GeneratorPrototype = Object.getPrototypeOf(function*() {}.prototype);

  let sentinel = {};

  let {get, set} =
      Object.getOwnPropertyDescriptor(Iterator.prototype, 'constructor');

  assertEquals(Iterator.prototype.constructor, Iterator);
  assertEquals(get.call(), Iterator);

  // 1. If _this_ is not an Object, then
  //   a. Throw a *TypeError* exception.
  assertThrows(() => set.call(undefined, ''));
  assertThrows(() => set.call(null, ''));
  assertThrows(() => set.call(true, ''));

  // 2. If _this_ is _home_, then
  //   a. NOTE: Throwing here emulates assignment to a non-writable data
  //   property on the _home_ object in strict mode code. b. Throw a *TypeError*
  //   exception.
  assertThrows(() => set.call(IteratorPrototype, ''));
  assertThrows(() => IteratorPrototype.constructor = '');

  assertEquals(Iterator.prototype.constructor, Iterator);
  assertEquals(get.call(), Iterator);

  // 3. If _desc_ is *undefined*, then
  //   a. Perform ? CreateDataPropertyOrThrow(_this_, _p_, _v_).
  let o = {};
  set.call(o, sentinel);
  assertEquals(o.constructor, sentinel);

  assertEquals(Iterator.prototype.constructor, Iterator);
  assertEquals(get.call(), Iterator);

  // 4. Else,
  //   a. Perform ? Set(_this_, _p_, _v_, *true*).
  let proto = Object.create(IteratorPrototype);
  proto.constructor = sentinel;
  assertEquals(proto.constructor, sentinel);

  assertEquals(Iterator.prototype.constructor, Iterator);
  assertEquals(get.call(), Iterator);
})();
