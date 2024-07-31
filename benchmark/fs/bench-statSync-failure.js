'use strict';

const common = require('../common');
const fs = require('fs');
const path = require('path');

const bench = common.createBenchmark(main, {
  n: [1e4],
  statSyncType: ['fstatSync', 'lstatSync', 'statSync'],
  throwType: [ 'throw', 'noThrow' ],
}, {
  // fstatSync does not support throwIfNoEntry
  combinationFilter: ({ statSyncType, throwType }) => !(statSyncType === 'fstatSync' && throwType === 'noThrow'),
});


function main({ n, statSyncType, throwType }) {
  const arg = (statSyncType === 'fstatSync' ?
    (1 << 30) :
    path.join(__dirname, 'non.existent'));

  const fn = fs[statSyncType];

  bench.start();
  for (let i = 0; i < n; i++) {
    if (throwType === 'noThrow') {
      fn(arg, { throwIfNoEntry: false });
    } else {
      try {
        fn(arg);
      } catch {
        // Continue regardless of error.
      }
    }
  }
  bench.end(n);
}
