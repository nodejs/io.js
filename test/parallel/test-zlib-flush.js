'use strict';

require('../common');

const assert = require('node:assert');
const zlib = require('node:zlib');
const { test } = require('node:test');

test('zlib flush', async () => {
  const { promise, resolve } = Promise.withResolvers();

  const opts = { level: 0 };
  const deflater = zlib.createDeflate(opts);
  const chunk = Buffer.from('/9j/4AAQSkZJRgABAQEASA==', 'base64');
  const expectedNone = Buffer.from([0x78, 0x01]);
  const blkhdr = Buffer.from([0x00, 0x10, 0x00, 0xef, 0xff]);
  const adler32 = Buffer.from([0x00, 0x00, 0x00, 0xff, 0xff]);
  const expectedFull = Buffer.concat([blkhdr, chunk, adler32]);
  let actualNone;
  let actualFull;

  deflater.write(chunk, function() {
    deflater.flush(zlib.constants.Z_NO_FLUSH, function() {
      actualNone = deflater.read();
      deflater.flush(function() {
        const bufs = [];
        let buf;
        while ((buf = deflater.read()) !== null)
          bufs.push(buf);
        actualFull = Buffer.concat(bufs);
        resolve();
      });
    });
  });
  await promise;
  assert.deepStrictEqual(actualNone, expectedNone);
  assert.deepStrictEqual(actualFull, expectedFull);
});
