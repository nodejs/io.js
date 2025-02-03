'use strict';

const common = require('../common');
const fs = require('fs');
const tmpdir = require('../../test/common/tmpdir');
tmpdir.refresh();

const tmpfile = tmpdir.resolve(`.existing-file-${process.pid}`);
fs.writeFileSync(tmpfile, 'this-is-for-a-benchmark', 'utf8');

const bench = common.createBenchmark(main, {
  type: ['existing', 'non-existing', 'non-flat-existing'],
  method: ['access', 'accessSync'],
  n: [1e5],
});

function runBench(n, path) {
  for (let i = 0; i < n; i++) {
    try {
      fs.accessSync(path);
    } catch {
      // do nothing
    }
  }
}

function runAsyncBench(n, path) {
  (function r(cntr, path) {
    if (cntr-- <= 0)
      return bench.end(n);
    fs.access(path, () => {
      r(cntr, path);
    });
  })(n, path);
}

function main({ n, type, method }) {
  let path;

  switch (type) {
    case 'existing':
      path = __filename;
      break;
    case 'non-flat-existing':
      path = tmpfile;
      break;
    case 'non-existing':
      path = tmpdir.resolve(`.non-existing-file-${process.pid}`);
      break;
    default:
      new Error('Invalid type');
  }

  if (method === 'access') {
    // Warmup the filesystem - it doesn't need to use the async method
    runBench(n, path);

    bench.start();
    runAsyncBench(n, path);
  } else {
    // warmup
    runBench(n, path);

    bench.start();
    runBench(n, path);
    bench.end(n);
  }
}
