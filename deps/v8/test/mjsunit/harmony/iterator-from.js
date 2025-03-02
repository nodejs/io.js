// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

assertEquals('function', typeof Iterator.from);
assertEquals(1, Iterator.from.length);
assertEquals('from', Iterator.from.name);

function TestValidIteratorWrapperSurface(iter) {
  const proto = Object.getPrototypeOf(iter);
  assertTrue(Object.hasOwn(proto, 'next'));
  assertTrue(Object.hasOwn(proto, 'return'));
  assertEquals('function', typeof proto.next);
  assertEquals('function', typeof proto.return);
  assertEquals(0, proto.next.length);
  assertEquals(0, proto.return.length);
  assertEquals('next', proto.next.name);
  assertEquals('return', proto.return.name);
  assertNotSame(iter, Iterator.prototype);
  assertSame(Object.getPrototypeOf(proto), Iterator.prototype);
}

(function TestIteratorFromString() {
  let iter = Iterator.from('abc');
  assertEquals({value:'a', done:false}, iter.next());
  assertEquals({value:'b', done:false}, iter.next());
  assertEquals({value:'c', done:false}, iter.next());
  assertEquals({value:undefined, done:true}, iter.next());
})();

(function TestIteratorFromManual() {
  // Make the result objects so their identities can be used for testing
  // passthrough of next().
  let nextResult = { value: 42, done: false };
  let returnResult = { value: 'ha ha ha... yes!' };
  let iter = {
    next() { return nextResult; },
    ['return']() { return returnResult; }
  };
  let wrapper = Iterator.from(iter);
  TestValidIteratorWrapperSurface(wrapper);
  assertSame(iter.next(), wrapper.next());
  assertSame(iter.return(), wrapper.return());
})();

(function TestIteratorFromNotWrapped() {
  let obj = {
    *[Symbol.iterator]() {
      yield 42;
      yield 'ha ha ha... yes';
    }
  };
  // Objects that have iterators aren't wrapped.
  let gen = obj[Symbol.iterator]();
  let wrapper = Iterator.from(obj);
  assertSame(Object.getPrototypeOf(gen), Object.getPrototypeOf(wrapper));
  assertEquals({value: 42, done: false }, wrapper.next());
  assertEquals({value: 'ha ha ha... yes', done: false }, wrapper.next());
  assertEquals({value: undefined, done: true }, wrapper.next());
})();

assertThrows(() => {
  Iterator.from({[Symbol.iterator]: "not callable"});
}, TypeError);

assertThrows(() => {
  Iterator.from({[Symbol.iterator]() {
    return "not an object";
  }});
}, TypeError);

assertThrows(() => {
  Iterator.from({
    [Symbol.iterator]: 0,
    next() {
      return 42;
    },
  });
}, TypeError);
