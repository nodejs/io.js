'use strict';

const common = require('../common');
const util = require('util');

const bench = common.createBenchmark(main, {
  n: [5e3],
  len: [1e2, 1e5],
  type: [
    'denseArray',
    'sparseArray',
    'mixedArray',
    'denseArray_showHidden',
  ],
});

function main({ n, len, type }) {
  let arr = Array(len);
  let opts;

  switch (type) {
    case 'denseArray_showHidden':
      opts = { showHidden: true };
      arr = arr.fill('denseArray');
      break;
    case 'denseArray':
      arr = arr.fill('denseArray');
      break;
    case 'sparseArray':
      break;
    case 'mixedArray':
      for (let i = 0; i < n; i += 2)
        arr[i] = i;
      break;
    default:
      throw new Error(`Unsupported type ${type}`);
  }
  bench.start();
  for (let i = 0; i < n; i++) {
    util.inspect(arr, opts);
  }
  bench.end(n);
}
