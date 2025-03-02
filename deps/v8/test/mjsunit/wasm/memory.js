// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Basic tests.

var outOfUint32RangeValue = 1e12;

function assertMemoryIsValid(memory) {
  assertSame(WebAssembly.Memory.prototype, memory.__proto__);
  assertSame(WebAssembly.Memory, memory.constructor);
  assertTrue(memory instanceof Object);
  assertTrue(memory instanceof WebAssembly.Memory);
}

(function TestConstructor() {
  assertTrue(WebAssembly.Memory instanceof Function);
  assertSame(WebAssembly.Memory, WebAssembly.Memory.prototype.constructor);
  assertTrue(WebAssembly.Memory.prototype.grow instanceof Function);
  let desc = Object.getOwnPropertyDescriptor(WebAssembly.Memory.prototype, 'buffer');
  assertTrue(desc.get instanceof Function);
  assertSame(undefined, desc.set);

  assertThrows(() => new WebAssembly.Memory(), TypeError);
  assertThrows(() => new WebAssembly.Memory(1), TypeError);
  assertThrows(() => new WebAssembly.Memory(""), TypeError);

  assertThrows(() => new WebAssembly.Memory({initial: -1}), TypeError);
  assertThrows(() => new WebAssembly.Memory({initial: outOfUint32RangeValue}), TypeError);

  assertThrows(() => new WebAssembly.Memory({initial: 10, maximum: -1}), TypeError);
  assertThrows(() => new WebAssembly.Memory({initial: 10, maximum: outOfUint32RangeValue}), TypeError);
  assertThrows(() => new WebAssembly.Memory({initial: 10, maximum: 9}), RangeError);

  let memory = new WebAssembly.Memory({initial: 1});
  assertMemoryIsValid(memory);
})();

(function TestConstructorWithMaximum() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 10});
  assertMemoryIsValid(memory);
})();

(function TestMaximumIsUndefined() {
  // New memory with maximum = undefined, which means maximum = 0.
  let memory = new WebAssembly.Memory({initial: 0, maximum: undefined});
  assertMemoryIsValid(memory);
})();

(function TestMaximumIsReadOnce() {
  var a = true;
  var desc = {initial: 10};
  Object.defineProperty(desc, 'maximum', {get: function() {
    if (a) {
      a = false;
      return 16;
    }
    else {
      // Change the return value on the second call so it throws.
      return -1;
    }
  }});
  let memory = new WebAssembly.Memory(desc);
  assertMemoryIsValid(memory);
})();

(function TestMaximumDoesNotHasProperty() {
  var hasPropertyWasCalled = false;
  var desc = {initial: 10};
  var proxy = new Proxy({maximum: 16}, {
    has: function(target, name) { hasPropertyWasCalled = true; }
  });
  Object.setPrototypeOf(desc, proxy);
  let memory = new WebAssembly.Memory(desc);
  assertMemoryIsValid(memory);
  assertFalse(hasPropertyWasCalled);
})();

(function TestBuffer() {
  let memory = new WebAssembly.Memory({initial: 1});
  assertTrue(memory.buffer instanceof Object);
  assertTrue(memory.buffer instanceof ArrayBuffer);
  assertThrows(() => {'use strict'; memory.buffer = memory.buffer}, TypeError)
  assertThrows(() => ({__proto__: memory}).buffer, TypeError)
})();

(function TestMemoryGrow() {
  let memory = new WebAssembly.Memory({initial: 1, maximum:30});
  assertEquals(1, memory.grow(9));
  assertTrue(memory.buffer instanceof ArrayBuffer);
  assertTrue(10*kPageSize == memory.buffer.byteLength);
  assertMemoryIsValid(memory);
  assertThrows(() => memory.grow(21));
})();
