'use strict';

require('../common');

const assert = require('node:assert');
const zlib = require('node:zlib');
const { test } = require('node:test');

// Regression test for https://github.com/nodejs/node/issues/30976
test('Writes to a stream should finish even after the readable side has been ended.', async (t) => {
  const { promise, resolve } = Promise.withResolvers();
  const data = zlib.deflateRawSync('Welcome');
  const inflate = zlib.createInflateRaw();
  const writeCallback = t.mock.fn();
  inflate.resume();
  inflate.write(data, writeCallback);
  inflate.write(Buffer.from([0x00]), writeCallback);
  inflate.write(Buffer.from([0x00]), writeCallback);
  inflate.flush(resolve);
  await promise;
  assert.strictEqual(writeCallback.mock.callCount(), 3);
});
