'use strict';

const common = require('../common');

const bench = common.createBenchmark(main, {
  n: [5e6],
  pos: ['start', 'middle', 'end'],
  size: [10, 100, 500],
}, { flags: ['--expose-internals'] });

function main({ n, pos, size }) {
  const { spliceOne } = require('internal/util');
  const arr = new Array(size);
  arr.fill('');
  let index;
  switch (pos) {
    case 'end':
      index = size - 1;
      break;
    case 'middle':
      index = Math.floor(size / 2);
      break;
    default: // start
      index = 0;
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    spliceOne(arr, index);
    arr.push('');
  }
  bench.end(n);
}
