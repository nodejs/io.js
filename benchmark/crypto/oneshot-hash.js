'use strict';

const common = require('../common.js');
const { createHash, hash } = require('crypto');
const path = require('path');
const filepath = path.resolve(__dirname, '../../test/fixtures/snapshot/typescript.js');
const fs = require('fs');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  length: [1000, 100_000],
  method: ['md5', 'sha1', 'sha256'],
  type: ['string', 'buffer'],
  n: [100_000, 1000],
}, {
  combinationFilter: ({ length, n }) => {
    return length * n <= 100_000 * 1000;
  },
});

function main({ length, type, method, n }) {
  let data = fs.readFileSync(filepath);
  if (type === 'string') {
    data = data.toString().slice(0, length);
  } else {
    data = Uint8Array.prototype.slice.call(data, 0, length);
  }

  const oneshotHash = hash ?
    (method, input) => hash(method, input, 'hex') :
    (method, input) => createHash(method).update(input).digest('hex');
  const array = [];
  for (let i = 0; i < n; i++) {
    array.push(null);
  }
  bench.start();
  for (let i = 0; i < n; i++) {
    array[i] = oneshotHash(method, data);
  }
  bench.end(n);
  assert.strictEqual(typeof array[n - 1], 'string');
}
