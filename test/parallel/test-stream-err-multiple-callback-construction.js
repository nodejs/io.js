'use strict';
const common = require('../common');
const stream = require('stream');
const assert = require('assert');

class TestWritable extends stream.Writable {
  _write(_chunk, _encoding, callback) {
    callback();
  }

  _final(callback) {
    process.nextTick(callback);
    process.nextTick(callback);
  }
}

const writable = new TestWritable();

writable.on('finish', common.mustCall());
writable.on('error', common.mustCall((error) => {
  assert.strictEqual(error.message, 'Callback called multiple times');
}));

writable.write('some data');
writable.end();
