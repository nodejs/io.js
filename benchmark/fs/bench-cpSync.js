'use strict';

const common = require('../common');
const fs = require('fs');
const path = require('path');
const tmpdir = require('../../test/common/tmpdir');
tmpdir.refresh();

const bench = common.createBenchmark(main, {
  n: [1, 100, 10_000],
});

function main({ n }) {
  tmpdir.refresh();
  const options = { recursive: true };
  const src = path.join(__dirname, '../../test/fixtures/copy');
  const dest = tmpdir.resolve(`${process.pid}/subdir/cp-bench-${process.pid}`);
  bench.start();
  for (let i = 0; i < n; i++) {
    fs.cpSync(src, dest, options);
  }
  bench.end(n);
}
