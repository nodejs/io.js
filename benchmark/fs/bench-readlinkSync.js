'use strict';

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../../test/common/tmpdir');

if (process.platform === 'win32') {
  console.log('Skipping: Windows does not play well with `symlinkSync`');
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
      const tmpfile = tmpdir.resolve(`.readlink-file-${process.pid}`);
      fs.writeFileSync(tmpfile, 'data', 'utf8');
      let returnValue;
      for (let i = 0; i < n; i++) {
        fs.symlinkSync(tmpfile, tmpdir.resolve(`.readlink-sync-${i}`), 'file');
      }
      bench.start();
      for (let i = 0; i < n; i++) {
        returnValue = fs.readlinkSync(tmpdir.resolve(`.readlink-sync-${i}`), { encoding: 'utf8' });
      }
      bench.end(n);
      assert.ok(returnValue);
      break;
    }

    case 'invalid': {
      let hasError = false;
      bench.start();
      for (let i = 0; i < n; i++) {
        try {
          fs.readlinkSync(tmpdir.resolve('.non-existing-file-for-readlinkSync'));
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
