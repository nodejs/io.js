// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const kMaxByteLength = %ArrayBufferMaxByteLength();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Int8Array.BYTES_PER_ELEMENT;
    try { new Int8Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Int8Array', foo());
  assertContains('new Int8Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Int8Array', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Uint8Array.BYTES_PER_ELEMENT;
    try { new Uint8Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Uint8Array', foo());
  assertContains('new Uint8Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Uint8Array', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Uint8ClampedArray.BYTES_PER_ELEMENT;
    try { new Uint8ClampedArray(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Uint8ClampedArray', foo());
  assertContains('new Uint8ClampedArray', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Uint8ClampedArray', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Int16Array.BYTES_PER_ELEMENT;
    try { new Int16Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Int16Array', foo());
  assertContains('new Int16Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Int16Array', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Uint16Array.BYTES_PER_ELEMENT;
    try { new Uint16Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Uint16Array', foo());
  assertContains('new Uint16Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Uint16Array', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Int32Array.BYTES_PER_ELEMENT;
    try { new Int32Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Int32Array', foo());
  assertContains('new Int32Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Int32Array', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Uint32Array.BYTES_PER_ELEMENT;
    try { new Uint32Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Uint32Array', foo());
  assertContains('new Uint32Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Uint32Array', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Float32Array.BYTES_PER_ELEMENT;
    try { new Float32Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Float32Array', foo());
  assertContains('new Float32Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Float32Array', foo());
})();


(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / Float64Array.BYTES_PER_ELEMENT;
    try { new Float64Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new Float64Array', foo());
  assertContains('new Float64Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new Float64Array', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / BigInt64Array.BYTES_PER_ELEMENT;
    try { new BigInt64Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new BigInt64Array', foo());
  assertContains('new BigInt64Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new BigInt64Array', foo());
})();

(function() {
  function foo() {
    const kMaxLength = kMaxByteLength / BigUint64Array.BYTES_PER_ELEMENT;
    try { new BigUint64Array(kMaxLength + 1); } catch (e) { return e.stack; }
  }

  %PrepareFunctionForOptimization(foo);
  assertContains('new BigUint64Array', foo());
  assertContains('new BigUint64Array', foo());
  %OptimizeFunctionOnNextCall(foo);
  assertContains('new BigUint64Array', foo());
})();
