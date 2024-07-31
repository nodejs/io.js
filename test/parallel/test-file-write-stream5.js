'use strict';

// Test 'uncork' for WritableStream.
// Refs: https://github.com/nodejs/node/issues/50979

const common = require('../common');
const fs = require('fs');
const assert = require('assert');
const test = require('node:test');
const tmpdir = require('../common/tmpdir');

const filepath = tmpdir.resolve('write_stream.txt');
tmpdir.refresh();

const data = 'data';

test('writable stream uncork', () => {
  const fileWriteStream = fs.createWriteStream(filepath);

  fileWriteStream.on('finish', common.mustCall(() => {
    const writtenData = fs.readFileSync(filepath, 'utf8');
    assert.strictEqual(writtenData, data);
  }));
  fileWriteStream.cork();
  fileWriteStream.write(data, common.mustCall());
  fileWriteStream.uncork();
  fileWriteStream.end();
});
