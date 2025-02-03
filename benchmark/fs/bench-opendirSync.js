'use strict';

const common = require('../common');
const fs = require('fs');
const path = require('path');
const tmpdir = require('../../test/common/tmpdir');
tmpdir.refresh();

const testFiles = fs.readdirSync('test', { withFileTypes: true })
  .filter((f) => f.isDirectory())
  .map((f) => path.join(f.parentPath, f.name));
const bench = common.createBenchmark(main, {
  type: ['existing', 'non-existing'],
  n: [1e3],
});

function main({ n, type }) {
  let files;

  switch (type) {
    case 'existing':
      files = testFiles;
      break;
    case 'non-existing':
      files = [tmpdir.resolve(`.non-existing-file-${Date.now()}`)];
      break;
    default:
      new Error('Invalid type');
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < files.length; j++) {
      try {
        const dir = fs.opendirSync(files[j]);
        dir.closeSync();
      } catch {
        // do nothing
      }
    }
  }
  bench.end(n);
}
