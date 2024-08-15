'use strict';

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../../test/common/tmpdir');

if (process.platform === 'win32') {
  console.log('Skipping: Windows does not have `getuid` or `getgid`');
  process.exit(0);
}

const bench = common.createBenchmark(main, {
  type: ['existing', 'non-existing'],
  method: ['chownSync', 'lchownSync'],
  n: [1e4],
});

function main({ n, type, method }) {
  const uid = process.getuid();
  const gid = process.getgid();
  const fsMethod = fs[method];

  switch (type) {
    case 'existing': {
      tmpdir.refresh();
      const tmpfile = tmpdir.resolve(`.existing-file-${process.pid}`);
      fs.writeFileSync(tmpfile, 'this-is-for-a-benchmark', 'utf8');
      bench.start();
      for (let i = 0; i < n; i++) {
        fsMethod(tmpfile, uid, gid);
      }
      bench.end(n);
      break;
    }
    case 'non-existing': {
      const path = tmpdir.resolve(`.non-existing-file-${Date.now()}`);
      let hasError = false;
      bench.start();
      for (let i = 0; i < n; i++) {
        try {
          fs[method](path, uid, gid);
        } catch {
          hasError = true;
        }
      }
      bench.end(n);
      assert.ok(hasError);
      break;
    }
    default:
      new Error('Invalid type');
  }
}
