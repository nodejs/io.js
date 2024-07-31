'use strict';

const common = require('../common');
const assert = require('assert');
const { Readable } = require('stream');

const buf = Buffer.alloc(8192);

const readable = new Readable({
  highWaterMark: 16 * 1024,
  read: common.mustCall(function() {
    this.push(buf);
  }, 31)
});

let i = 0;

readable.on('readable', common.mustCall(function() {
  if (i++ === 10) {
    // We will just terminate now.
    process.removeAllListeners('readable');
    return;
  }

  const data = readable.read();
  // TODO(mcollina): there is something odd in the highWaterMark logic
  // investigate.
  if (i === 1) {
    assert.strictEqual(data.length, 8192 * 2);
  } else {
    assert.strictEqual(data.length, 8192 * 3);
  }
}, 11));
