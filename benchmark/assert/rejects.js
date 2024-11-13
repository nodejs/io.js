'use strict';

const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  n: [25, 2e5],
  method: ['rejects', 'doesNotReject'],
});

async function main({ n, method }) {
  const fn = assert[method];
  const shouldReject = method === 'rejects';

  bench.start();
  for (let i = 0; i < n; ++i) {
    await fn(async () => {
      const err = new Error(`assert.${method}`);
      if (shouldReject) {
        throw err;
      } else {
        return err;
      }
    });
  }
  bench.end(n);
}
