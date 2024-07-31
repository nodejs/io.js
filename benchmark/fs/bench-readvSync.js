'use strict';

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../../test/common/tmpdir');
tmpdir.refresh();

const exptectedBuff = Buffer.from('Benchmark Data');
const expectedLength = exptectedBuff.length;

const bufferArr = [Buffer.alloc(expectedLength)];

const filename = tmpdir.resolve('readv_sync.txt');
fs.writeFileSync(filename, exptectedBuff);

const bench = common.createBenchmark(main, {
  type: ['valid', 'invalid'],
  n: [1e5],
});

function main({ n, type }) {
  let fd;
  let result;

  switch (type) {
    case 'valid':
      fd = fs.openSync(filename, 'r');

      bench.start();
      for (let i = 0; i < n; i++) {
        result = fs.readvSync(fd, bufferArr, 0);
      }

      bench.end(n);
      assert.strictEqual(result, expectedLength);
      fs.closeSync(fd);
      break;
    case 'invalid': {
      fd = 1 << 30;
      let hasError = false;
      bench.start();
      for (let i = 0; i < n; i++) {
        try {
          result = fs.readvSync(fd, bufferArr, 0);
        } catch {
          hasError = true;
        }
      }

      bench.end(n);
      assert(hasError);
      break;
    }
    default:
      throw new Error('Invalid type');
  }
}
