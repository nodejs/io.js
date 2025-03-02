// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

assertEquals('function', typeof Iterator);
assertEquals(0, Iterator.length);
assertEquals('Iterator', Iterator.name);

// Abstract base class, can't be instantiated.
assertThrows(() => new Iterator(), TypeError);

// Can be used as superclass though.
class MyIterator extends Iterator {
  next() {
    return {value: 42, done: false};
  }
}
const myIter = new MyIterator();
assertTrue(myIter instanceof MyIterator);

function* gen() {
  yield 42;
}
const genIter = gen();
assertTrue(genIter instanceof Iterator);
assertSame(
    Object.getPrototypeOf(
        Object.getPrototypeOf(Object.getPrototypeOf(genIter))),
    Iterator.prototype);
