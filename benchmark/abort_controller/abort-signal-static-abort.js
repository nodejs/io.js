'use strict';
const common = require('../common.js');

const bench = common.createBenchmark(main, {
  n: [5e6],
  kind: ['default-reason', 'same-reason'],
});

function main({ n, kind }) {
  switch (kind) {
    case 'default-reason':
      bench.start();
      for (let i = 0; i < n; ++i)
        AbortSignal.abort();
      bench.end(n);
      break;
    case 'same-reason': {
      const reason = new Error('same reason');

      bench.start();
      for (let i = 0; i < n; ++i)
        AbortSignal.abort(reason);
      bench.end(n);
      break;
    }
    default:
      throw new Error('Invalid kind');
  }
}
