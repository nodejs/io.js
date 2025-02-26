// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


var arrayIteratorPrototype = [].entries().__proto__;
var iteratorPrototype = arrayIteratorPrototype.__proto__;

assertSame(Object.prototype, Object.getPrototypeOf(iteratorPrototype));
assertTrue(Object.isExtensible(iteratorPrototype));
assertSame(12, Object.getOwnPropertyNames(iteratorPrototype).length);
assertSame(2, Object.getOwnPropertySymbols(iteratorPrototype).length);
assertSame(Symbol.iterator,
             Object.getOwnPropertySymbols(iteratorPrototype)[0]);

var descr = Object.getOwnPropertyDescriptor(iteratorPrototype, Symbol.iterator);
assertTrue(descr.configurable);
assertFalse(descr.enumerable);
assertTrue(descr.writable);

var iteratorFunction = descr.value;
assertSame('function', typeof iteratorFunction);
assertSame(0, iteratorFunction.length);
assertSame('[Symbol.iterator]', iteratorFunction.name);

var obj = {};
assertSame(obj, iteratorFunction.call(obj));
assertSame(iteratorPrototype, iteratorPrototype[Symbol.iterator]());

var mapIteratorPrototype = new Map().entries().__proto__;
var setIteratorPrototype = new Set().values().__proto__;
var stringIteratorPrototype = 'abc'[Symbol.iterator]().__proto__;
assertSame(iteratorPrototype, mapIteratorPrototype.__proto__);
assertSame(iteratorPrototype, setIteratorPrototype.__proto__);
assertSame(iteratorPrototype, stringIteratorPrototype.__proto__);

var typedArrays = [
  Float32Array,
  Float64Array,
  Int16Array,
  Int32Array,
  Int8Array,
  Uint16Array,
  Uint32Array,
  Uint8Array,
  Uint8ClampedArray,
];

for (var constructor of typedArrays) {
  var array = new constructor();
  var iterator = array[Symbol.iterator]();
  assertSame(iteratorPrototype, iterator.__proto__.__proto__);
}

function* gen() {}
assertSame(iteratorPrototype, gen.prototype.__proto__.__proto__);
var g = gen();
assertSame(gen.prototype, g.__proto__);
assertSame(iteratorPrototype, g.__proto__.__proto__.__proto__);
