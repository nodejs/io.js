'use strict';

const common = require('../common');
const fs = require('fs');
const tmpdir = require('../../test/common/tmpdir');
tmpdir.refresh();

const bench = common.createBenchmark(main, {
  type: ['existing', 'non-existing'],
  n: [1e4],
});

function main({ n, type }) {
  switch (type) {
    case 'existing': {
      for (let i = 0; i < n; i++) {
        fs.mkdirSync(tmpdir.resolve(`rmdirsync-bench-dir-${i}`));
      }

      bench.start();
      for (let i = 0; i < n; i++) {
        fs.rmdirSync(tmpdir.resolve(`rmdirsync-bench-dir-${i}`));
      }
      bench.end(n);
      break;
    }
    case 'non-existing': {
      bench.start();
      for (let i = 0; i < n; i++) {
        try {
          fs.rmdirSync(tmpdir.resolve(`.non-existent-folder-${i}`));
        } catch {
          // do nothing
        }
      }
      bench.end(n);
      break;
    }
    default:
      new Error('Invalid type');
  }
}
