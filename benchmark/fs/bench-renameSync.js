'use strict';

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../../test/common/tmpdir');

const bench = common.createBenchmark(main, {
  type: ['invalid', 'valid'],
  n: [2e3],
});

function main({ n, type }) {
  switch (type) {
    case 'invalid': {
      let hasError = false;
      bench.start();
      for (let i = 0; i < n; i++) {
        try {
          fs.renameSync(tmpdir.resolve(`.non-existing-file-${i}`), tmpdir.resolve(`.new-file-${i}`));
        } catch {
          hasError = true;
        }
      }
      bench.end(n);
      assert.ok(hasError);
      break;
    }
    case 'valid': {
      tmpdir.refresh();
      for (let i = 0; i < n; i++) {
        fs.writeFileSync(tmpdir.resolve(`.existing-file-${i}`), 'bench', 'utf8');
      }

      bench.start();
      for (let i = 0; i < n; i++) {
        fs.renameSync(
          tmpdir.resolve(`.existing-file-${i}`),
          tmpdir.resolve(`.new-existing-file-${i}`),
        );
      }

      bench.end(n);
      break;
    }
    default:
      throw new Error('Invalid type');
  }
}
