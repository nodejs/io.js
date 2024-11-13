'use strict';
const SlowBuffer = require('buffer').SlowBuffer;
const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  size: [512, 4096, 16386],
  type: ['fast'],
  method: ['for', 'forOf', 'iterator'],
  n: [1e3],
});

const methods = {
  'for': benchFor,
  'forOf': benchForOf,
  'iterator': benchIterator,
};

function main({ size, type, method, n }) {
  const buffer = type === 'fast' ?
    Buffer.alloc(size) :
    SlowBuffer(size).fill(0);

  const fn = methods[method];

  bench.start();
  fn(buffer, n);
  bench.end(n);
}

function benchFor(buffer, n) {
  for (let k = 0; k < n; k++) {
    for (let i = 0; i < buffer.length; i++) {
      assert.strictEqual(buffer[i], 0);
    }
  }
}

function benchForOf(buffer, n) {
  for (let k = 0; k < n; k++) {
    for (const b of buffer) {
      assert.strictEqual(b, 0);
    }
  }
}

function benchIterator(buffer, n) {
  for (let k = 0; k < n; k++) {
    const iter = buffer[Symbol.iterator]();
    let cur = iter.next();

    while (!cur.done) {
      assert.strictEqual(cur.value, 0);
      cur = iter.next();
    }

  }
}
