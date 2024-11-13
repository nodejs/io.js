'use strict';

const common = require('../common.js');
const fs = require('fs');

const bench = common.createBenchmark(main, {
  encoding: ['undefined', 'utf8', 'ascii'],
  path: ['existing', 'non-existing'],
  hasFileDescriptor: ['true', 'false'],
  n: [1e4],
});

function main({ n, encoding, path, hasFileDescriptor }) {
  const enc = encoding === 'undefined' ? undefined : encoding;
  let file;
  let shouldClose = false;

  if (hasFileDescriptor === 'true') {
    shouldClose = path === 'existing';
    file = path === 'existing' ? fs.openSync(__filename) : -1;
  } else {
    file = path === 'existing' ? __filename : '/tmp/not-found';
  }
  bench.start();
  for (let i = 0; i < n; ++i) {
    try {
      fs.readFileSync(file, enc);
    } catch {
      // do nothing
    }
  }
  bench.end(n);
  if (shouldClose) {
    fs.closeSync(file);
  }
}
