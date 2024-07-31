'use strict';
const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  type: [
    'Int8Array',
    'Uint8Array',
    'Float32Array',
    'Uint32Array',
  ],
  n: [25000],
  strict: [0, 1],
  method: [
    'deepEqual',
    'notDeepEqual',
  ],
  len: [1e2, 5e3],
});

function main({ type, n, len, method, strict }) {
  const clazz = global[type];
  const actual = new clazz(len);
  const expected = new clazz(len);

  if (strict) {
    method = method.replace('eep', 'eepStrict');
  }
  const fn = assert[method];

  if (method.includes('not')) {
    expected[Math.floor(len / 2)] = 123;
  }

  bench.start();
  for (let i = 0; i < n; ++i) {
    actual[0] = i;
    expected[0] = i;
    const pos = Math.ceil(len / 2) + 1;
    actual[pos] = i;
    expected[pos] = i;
    fn(actual, expected);
  }
  bench.end(n);
}
