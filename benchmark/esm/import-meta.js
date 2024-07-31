'use strict';

const path = require('path');
const { pathToFileURL, fileURLToPath } = require('url');
const common = require('../common');
const assert = require('assert');
const bench = common.createBenchmark(main, {
  n: [1000],
});

const file = pathToFileURL(
  path.resolve(__filename, '../../fixtures/esm-dir-file.mjs'),
);
async function load(array, n) {
  for (let i = 0; i < n; i++) {
    array[i] = await import(`${file}?i=${i}`);
  }
  return array;
}

function main({ n }) {
  const array = [];
  for (let i = 0; i < n; ++i) {
    array.push({ dirname: '', filename: '', i: 0 });
  }

  bench.start();
  load(array, n).then((arr) => {
    bench.end(n);
    assert.strictEqual(arr[n - 1].filename, fileURLToPath(file));
  });
}
