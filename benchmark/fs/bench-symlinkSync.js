'use strict';

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../../test/common/tmpdir');

if (process.platform === 'win32') {
  console.log('Skipping: Windows does not play well with `symlink`');
  process.exit(0);
}

const bench = common.createBenchmark(main, {
  type: ['valid', 'invalid'],
  n: [1e3],
});

function main({ n, type }) {
  switch (type) {
    case 'valid': {
      tmpdir.refresh();
      bench.start();
      for (let i = 0; i < n; i++) {
        fs.symlinkSync(tmpdir.resolve('.non-existent-symlink-file'), tmpdir.resolve(`.valid-${i}`), 'file');
      }
      bench.end(n);
      break;
    }

    case 'invalid': {
      let hasError = false;
      bench.start();
      for (let i = 0; i < n; i++) {
        try {
          fs.symlinkSync(
            tmpdir.resolve('.non-existent-symlink-file'),
            __filename,
            'file',
          );
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
