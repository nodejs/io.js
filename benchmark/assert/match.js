'use strict';

const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  n: [25, 2e7],
  method: ['match', 'doesNotMatch'],
});

function main({ n, method }) {
  const fn = assert[method];
  const actual = 'Example of string that will match';
  const expected = method === 'match' ? /will match/ : /will not match/;

  bench.start();
  for (let i = 0; i < n; ++i) {
    fn(actual, expected);
  }
  bench.end(n);
}
