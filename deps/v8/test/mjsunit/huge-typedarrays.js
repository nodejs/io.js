// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax
// Enable memory64 so we can also test the ArrayBuffer of a 16GB Wasm memory.

// Flags: --js-staging

// This file tests all TypedArray method that an be tested without iterating the
// whole TypedArray.
// This excludes `filter`, `forEach`, `join`, `map`, `reduce`, `reduceRight`,
// `reverse`, `sort`, `toLocaleString`, `toReversed`, `toSorted`, `toString`,
// and `with`.

// typedarray-helpers provides test functions for atomics.
d8.file.execute('test/mjsunit/typedarray-helpers.js');

const kB = 1024;
const MB = 1024 * kB;
const GB = 1024 * MB;

const kMaxArrayBufferByteLength = %ArrayBufferMaxByteLength();

const kHasWasm = (typeof WebAssembly != "undefined");
function makeWasmMemory(length) {
  const kWasmPageSize = 64 * 1024;
  assertTrue(kHasWasm);
  assertEquals(0, length % kWasmPageSize);
  let num_pages = BigInt(length / kWasmPageSize);
  let wasm_mem = new WebAssembly.Memory(
      {initial: num_pages, maximum: num_pages, address: 'i64'});
  return wasm_mem.buffer;
}

const kTestConfigs = [
  // 2GB+1 should flush out bugs where the length is capped to 31 bits
  // (resulting in length 1).
  [Int8Array, num_elems => new Int8Array(num_elems), 2 * GB + 1],
  // 4GB+1 should flush out bugs where the length is capped to 32 bits
  // (resulting in length 1).
  [Int8Array, num_elems => new Int8Array(num_elems), 4 * GB + 1],
  // Test the maximum array buffer length.
  [Int8Array, num_elems => new Int8Array(num_elems), kMaxArrayBufferByteLength],
  // And then test some huge arrays of different type.
  [Uint8Array, num_elems => new Uint8Array(num_elems), 21 * GB],
  [
    Int16Array, num_elems => new Uint16Array(new ArrayBuffer(num_elems * 2)),
    21 * GB
  ],
  [Int32Array, num_elems => new Int32Array(num_elems), 21 * GB]
].concat(kHasWasm ? [
  // Test a large ArrayBuffer constructed from Wasm.
  [Int8Array, num_elems => new Int8Array(makeWasmMemory(num_elems)), 16*GB]
] : []);

function isOom(e) {
  return (e instanceof RangeError) &&
          (e.message.includes('Out of memory') ||
           e.message.includes('could not allocate memory') ||
           e.message.includes('Array buffer allocation failed'));
}

function ignoreOOM(fn) {
  try {
    return fn();
  } catch (e) {
    // Uncomment the next line in local debugging to ensure that the tests
    // actually run.
    //throw e;
    if (isOom(e)) return undefined;
    throw e;
  }
}

function* GetTestConfigs() {
  for (let [type, constructor, length] of kTestConfigs) {
    // Ignore configuration that cannot work on this platform.
    if (length > kMaxArrayBufferByteLength) continue;
    print(`- ${type.name} of ${length} bytes`);
    let elem_size = type.BYTES_PER_ELEMENT;
    let num_elems = length / elem_size;
    let arr = ignoreOOM(() => constructor(num_elems));
    if (!arr) continue;
    assertEquals(length, arr.byteLength);
    assertEquals(num_elems, arr.length);
    assertEquals(0, arr.byteOffset);
    yield {
      type: type,
      length: length,
      arr: arr,
      elem_size: elem_size,
      num_elems: num_elems
    };
  }
}

// Test operator[], at, set, slice.
(function testSimpleAccessors() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr, num_elems} = test;
    let slice = (start, end) => Array.from(arr.slice(start, end));

    for (let offset of [13, num_elems - 7]) {
      assertEquals(0, arr[offset]);
      assertEquals(0, arr.at(offset));
      arr[offset] = 11;
      assertEquals(0, arr[offset - 1]);
      assertEquals(0, arr.at(offset - 1));
      assertEquals(11, arr[offset]);
      assertEquals(11, arr.at(offset));
      assertEquals(0, arr[offset + 1]);
      assertEquals(0, arr.at(offset + 1));
      assertEquals([0, 11, 0], slice(offset - 1, offset + 2));
      arr.set([17, 21], offset + 1);
      assertEquals([0, 11, 17, 21, 0], slice(offset - 1, offset + 4));
    }
  }
})();

// Test the `copyWithin` method.
(function testMethodCopyWithin() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr, num_elems} = test;

    arr[2] = 2;
    arr.copyWithin(3, 1, 3);
    assertEquals([0, 2, 0, 2, 0], Array.from(arr.slice(1, 6)));
    arr.copyWithin(num_elems - 5, 1);
    assertEquals([0, 2, 0, 2, 0], Array.from(arr.slice(-5)));
  }
})();

// Test the `entries`, `keys`, and `values` methods.
(function testMethodsEntriesAndKeysAndValues() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr, num_elems} = test;

    // We can only test small indexes, because advancing the iterator returned
    // by `entries()` to the larger indexes would take too long.
    arr[2] = 7;
    arr[4] = 9;
    for (let [iterator, expected] of [
             [arr.entries(), [[0, 0], [1, 0], [2, 7], [3, 0], [4, 9], [5, 0]]],
             [arr.keys(), [0, 1, 2, 3, 4, 5]],
             [arr.values(), [0, 0, 7, 0, 9, 0]],
    ]) {
      for (let value of iterator) {
        assertEquals(expected.shift(), value);
        if (expected.length == 0) break;
      }
    }
  }
})();

// Test the `every` method.
(function testMethodEvery() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr} = test;

    // Note: We cannot test an `every` that succeeds, because that would just
    // take too long. We are aborting after the element at `kChangedIndex`.
    let kChangedIndex = 1113;
    let kChangedValue = 47;
    last_checked_index = -1;
    arr[kChangedIndex] = kChangedValue;
    assertFalse(arr.every((element, index, array) => {
      assertSame(arr, array);
      assertEquals(last_checked_index + 1, index);
      last_checked_index = index;
      assertEquals(index == kChangedIndex ? kChangedValue : 0, element);
      return element == 0;
    }));
    assertEquals(kChangedIndex, last_checked_index);
  }
})();

// Test the `fill` method.
(function testMethodFill() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr, num_elems} = test;

    arr.fill(13, num_elems - 3, num_elems - 1);
    assertEquals([0, 13, 13, 0], Array.from(arr.slice(-4)));
    arr.fill(17, num_elems - 2);
    assertEquals([0, 13, 17, 17], Array.from(arr.slice(-4)));
  }
})();

// Test `find`, `findIndex`, `findLast`, and `findLastIndex` methods.
(function testMethodsFindAndFindIndexAndFindLastAndFindLastIndex() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr, num_elems} = test;

    arr[13] = 11;
    arr[num_elems - 13] = 15;
    assertEquals(11, arr.find(e => e != 0));
    assertEquals(13, arr.findIndex(e => e != 0));
    assertEquals(15, arr.findLast(e => e != 0));
    assertEquals(num_elems - 13, arr.findLastIndex(e => e != 0));

    // Check that the callback arguments are as expected.
    let last_checked_index = num_elems;
    let callback = (elem, idx, array) => {
      assertSame(array, arr);
      assertEquals(last_checked_index - 1, idx);
      last_checked_index = idx;
      assertEquals(idx == num_elems - 13 ? 15 : 0, elem);
      return elem != 0;
    };
    assertEquals(15, arr.findLast(callback));
    assertEquals(num_elems - 13, last_checked_index);

    last_checked_index = num_elems;
    assertEquals(num_elems - 13, arr.findLastIndex(callback));
    assertEquals(num_elems - 13, last_checked_index);
  }
})();

// Test the `includes`, `indexOf`, and `lastIndexOf` methods.
(function testMethodIncludesAndIndexOfAndLastIndexOf() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr, num_elems} = test;

    arr[13] = 11;
    arr[num_elems - 13] = 15;
    assertTrue(arr.includes(11));
    assertTrue(arr.includes(15, num_elems - 15));
    assertFalse(arr.includes(16, num_elems - 15));

    assertEquals(13, arr.indexOf(11));
    assertEquals(num_elems - 13, arr.indexOf(15, num_elems - 15));
    assertEquals(-1, arr.indexOf(16, num_elems - 15));

    assertEquals(13, arr.lastIndexOf(11, 25));
    assertEquals(num_elems - 13, arr.lastIndexOf(15));
    assertEquals(-1, arr.lastIndexOf(16, 25));
  }
})();

// Test the `some` method.
(function testMethodSome() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr} = test;

    // We need to set an element with a small index to avoid iterating for too
    // long.
    arr[13] = 11;
    let last_checked_index = -1;
    assertTrue(arr.some((elem, index, array) => {
      assertSame(array, arr);
      assertEquals(last_checked_index + 1, index);
      last_checked_index = index;
      assertEquals(index == 13 ? 11 : 0, elem);
      return elem != 0;
    }));
    assertEquals(13, last_checked_index);
  }
})();

// Test the `subarray` method.
(function testMethodSubarray() {
  print(arguments.callee.name);
  for (let test of GetTestConfigs()) {
    let {arr, num_elems} = test;

    arr[13] = 11;
    arr[num_elems - 3] = 15;

    let sub = (begin, end) => Array.from(arr.subarray(begin, end));
    assertEquals([0, 11, 0], sub(12, 15));
    assertEquals([0, 15, 0], sub(num_elems - 4, num_elems - 1));
    assertEquals([0, 15, 0, 0], sub(num_elems - 4));
  }
})();

// Test atomics.
(function testAtomics() {
  print(arguments.callee.name);
  // Create one big shared ArrayBuffer.
  const length = 21 * GB;
  // Skip if the platform does not support such big ArrayBuffers.
  if (length > kMaxArrayBufferByteLength) return;
  let sab = ignoreOOM(() => new SharedArrayBuffer(length));
  if (!sab) return;

  let int32_arr = new Int32Array(sab);
  const num_int32_elems = length / Int32Array.BYTES_PER_ELEMENT;
  TestAtomicsOperations(int32_arr, 13);
  TestAtomicsOperations(int32_arr, num_int32_elems - 13);
  AssertAtomicsOperationsThrow(int32_arr, num_int32_elems, RangeError);

  // Test wait / notify.
  let worker = new Worker(function() {
    onmessage = function({data:msg}) {
      if (msg.action == 'wait') {
        let ta = new Int32Array(msg.buf);
        postMessage(Atomics.wait(ta, msg.index, msg.value, msg.timeout));
        return;
      }
      postMessage(`Unknown action: ${msg.data.action}`);
    }
  }, {type: 'function'});

  for (let index of [3, num_int32_elems - 3]) {
    worker.postMessage(
        {action: 'wait', buf: sab, index: index, value: 0, timeout: 100});
    assertEquals('timed-out', worker.getMessage());

    worker.postMessage(
        {action: 'wait', buf: sab, index: index, value: 1, timeout: 100});
    assertEquals('not-equal', worker.getMessage());

    worker.postMessage(
        {action: 'wait', buf: sab, index: index, value: 0, timeout: 10000});
    let timeout = performance.now() + 10000;
    while (true) {
      let woken = Atomics.notify(int32_arr, index, 1);
      if (woken == 1) break;
      assertEquals(0, woken);
      if (performance.now() > timeout) throw new Error('could not wake');
    }
    assertEquals('ok', worker.getMessage());
  }
})();
