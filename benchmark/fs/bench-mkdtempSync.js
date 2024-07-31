'use strict';

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../../test/common/tmpdir');

const bench = common.createBenchmark(main, {
  type: ['valid-string', 'valid-buffer', 'invalid'],
  n: [1e4],
});

function main({ n, type }) {
  tmpdir.refresh();
  const options = { encoding: 'utf8' };
  let prefix;
  let out = true;

  switch (type) {
    case 'valid-string':
      prefix = tmpdir.resolve(`${Date.now()}`);
      break;
    case 'valid-buffer':
      prefix = Buffer.from(tmpdir.resolve(`${Date.now()}`));
      break;
    case 'invalid':
      prefix = tmpdir.resolve('non-existent', 'foo', 'bar');
      break;
    default:
      new Error('Invalid type');
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    try {
      out = fs.mkdtempSync(prefix, options);
    } catch {
      // do nothing
    }
  }
  bench.end(n);
  assert.ok(out);
}
