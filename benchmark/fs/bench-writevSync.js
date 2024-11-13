'use strict';

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../../test/common/tmpdir');
tmpdir.refresh();

const path = tmpdir.resolve(`new-file-${process.pid}`);
fs.writeFileSync(path, 'Some content.');

const bench = common.createBenchmark(main, {
  type: ['valid', 'invalid'],
  n: [1e5],
});

const buffer = Buffer.from('Benchmark data.');

function main({ n, type }) {
  let fd;
  let result;

  switch (type) {
    case 'valid':
      fd = fs.openSync(path, 'r+');

      bench.start();
      for (let i = 0; i < n; i++) {
        result = fs.writevSync(fd, [buffer]);
      }

      bench.end(n);
      assert.ok(result);
      fs.closeSync(fd);
      break;
    case 'invalid': {
      fd = 1 << 30;
      let hasError = false;
      bench.start();
      for (let i = 0; i < n; i++) {
        try {
          result = fs.writevSync(fd, [buffer]);
        } catch {
          hasError = true;
        }
      }

      bench.end(n);
      assert.ok(hasError);
      break;
    }
    default:
      throw new Error('Invalid type');
  }
}
