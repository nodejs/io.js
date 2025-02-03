'use strict';

require('../common');
const assert = require('assert');
const { test, suite } = require('node:test');

function makeBlock(f) {
  const args = Array.prototype.slice.call(arguments, 1);
  return function() {
    return f.apply(this, args);
  };
}

suite('equalArrayPairs', () => {
  const equalArrayPairs = [
    [new Uint8Array(1e5), new Uint8Array(1e5)],
    [new Uint16Array(1e5), new Uint16Array(1e5)],
    [new Uint32Array(1e5), new Uint32Array(1e5)],
    [new Uint8ClampedArray(1e5), new Uint8ClampedArray(1e5)],
    [new Int8Array(1e5), new Int8Array(1e5)],
    [new Int16Array(1e5), new Int16Array(1e5)],
    [new Int32Array(1e5), new Int32Array(1e5)],
    [new Float32Array(1e5), new Float32Array(1e5)],
    [new Float64Array(1e5), new Float64Array(1e5)],
    [new Float32Array([+0.0]), new Float32Array([+0.0])],
    [new Uint8Array([1, 2, 3, 4]).subarray(1), new Uint8Array([2, 3, 4])],
    [new Uint16Array([1, 2, 3, 4]).subarray(1), new Uint16Array([2, 3, 4])],
    [new Uint32Array([1, 2, 3, 4]).subarray(1, 3), new Uint32Array([2, 3])],
    [new ArrayBuffer(3), new ArrayBuffer(3)],
    [new SharedArrayBuffer(3), new SharedArrayBuffer(3)],
  ];

  for (const arrayPair of equalArrayPairs) {
    test('', () => {
      // eslint-disable-next-line no-restricted-properties
      assert.deepEqual(arrayPair[0], arrayPair[1]);
      assert.deepStrictEqual(arrayPair[0], arrayPair[1]);
    });
  }
});

suite('looseEqualArrayPairs', () => {
  const looseEqualArrayPairs = [
    [new Float32Array([+0.0]), new Float32Array([-0.0])],
    [new Float64Array([+0.0]), new Float64Array([-0.0])],
  ];

  for (const arrayPair of looseEqualArrayPairs) {
    test('', () => {
      // eslint-disable-next-line no-restricted-properties
      assert.deepEqual(arrayPair[0], arrayPair[1]);
      assert.throws(
        makeBlock(assert.deepStrictEqual, arrayPair[0], arrayPair[1]),
        assert.AssertionError
      );
    });
  }
});

suite('notEqualArrayPairs', () => {
  const notEqualArrayPairs = [
    [new ArrayBuffer(3), new SharedArrayBuffer(3)],
    [new Int16Array(256), new Uint16Array(256)],
    [new Int16Array([256]), new Uint16Array([256])],
    [new Float64Array([+0.0]), new Float32Array([-0.0])],
    [new Uint8Array(2), new Uint8Array(3)],
    [new Uint8Array([1, 2, 3]), new Uint8Array([4, 5, 6])],
    [new Uint8ClampedArray([300, 2, 3]), new Uint8Array([300, 2, 3])],
    [new Uint16Array([2]), new Uint16Array([3])],
    [new Uint16Array([0]), new Uint16Array([256])],
    [new Int16Array([0]), new Uint16Array([256])],
    [new Int16Array([-256]), new Uint16Array([0xff00])], // same bits
    [new Int32Array([-256]), new Uint32Array([0xffffff00])], // ditto
    [new Float32Array([0.1]), new Float32Array([0.0])],
    [new Float32Array([0.1]), new Float32Array([0.1, 0.2])],
    [new Float64Array([0.1]), new Float64Array([0.0])],
    [new Uint8Array([1, 2, 3]).buffer, new Uint8Array([4, 5, 6]).buffer],
    [
      new Uint8Array(new SharedArrayBuffer(3)).fill(1).buffer,
      new Uint8Array(new SharedArrayBuffer(3)).fill(2).buffer,
    ],
    [new ArrayBuffer(2), new ArrayBuffer(3)],
    [new SharedArrayBuffer(2), new SharedArrayBuffer(3)],
    [new ArrayBuffer(2), new SharedArrayBuffer(3)],
    [
      new Uint8Array(new ArrayBuffer(3)).fill(1).buffer,
      new Uint8Array(new SharedArrayBuffer(3)).fill(2).buffer,
    ],
    [new ArrayBuffer(3), new SharedArrayBuffer(3)],
    [new SharedArrayBuffer(2), new ArrayBuffer(2)],
  ];

  for (const arrayPair of notEqualArrayPairs) {
    test('', () => {
      assert.throws(
        // eslint-disable-next-line no-restricted-properties
        makeBlock(assert.deepEqual, arrayPair[0], arrayPair[1]),
        assert.AssertionError
      );
      assert.throws(
        makeBlock(assert.deepStrictEqual, arrayPair[0], arrayPair[1]),
        assert.AssertionError
      );
      // TODO(puskin94): remove emitWarning override once the partialDeepStrictEqual method is not experimental anymore
      // Suppress warnings, necessary otherwise the tools/pseudo-tty.py runner will fail
      const originalEmitWarning = process.emitWarning;
      process.emitWarning = () => {};
      assert.throws(
        makeBlock(assert.partialDeepStrictEqual, arrayPair[0], arrayPair[1]),
        assert.AssertionError
      );
      process.emitWarning = originalEmitWarning; // Restore original process.emitWarning
    });
  }
});
