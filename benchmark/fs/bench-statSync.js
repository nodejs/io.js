'use strict';

const common = require('../common');
const fs = require('fs');

const bench = common.createBenchmark(main, {
  n: [1e4],
  statSyncType: ['fstatSync', 'lstatSync', 'statSync'],
});


function main({ n, statSyncType }) {
  const arg = (statSyncType === 'fstatSync' ?
    fs.openSync(__filename, 'r') :
    __dirname);
  const fn = fs[statSyncType];

  bench.start();
  for (let i = 0; i < n; i++) {
    fn(arg);
  }
  bench.end(n);

  if (statSyncType === 'fstatSync')
    fs.closeSync(arg);
}
