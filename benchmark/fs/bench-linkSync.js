'use strict';

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../../test/common/tmpdir');

tmpdir.refresh();
const tmpfile = tmpdir.resolve(`.bench-file-data-${Date.now()}`);
fs.writeFileSync(tmpfile, 'bench-file', 'utf-8');

const bench = common.createBenchmark(main, {
  type: ['valid', 'invalid'],
  n: [1e3],
});

function main({ n, type }) {
  switch (type) {
    case 'valid': {
      bench.start();
      for (let i = 0; i < n; i++) {
        fs.linkSync(tmpfile, tmpdir.resolve(`.valid-${i}`), 'file');
      }
      bench.end(n);

      break;
    }

    case 'invalid': {
      let hasError = false;
      bench.start();
      for (let i = 0; i < n; i++) {
        try {
          fs.linkSync(
            tmpdir.resolve(`.non-existing-file-for-linkSync-${i}`),
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
