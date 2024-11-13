'use strict';

require('../common');

const assert = require('node:assert');
const zlib = require('node:zlib');
const { test } = require('node:test');

// windowBits is a special case in zlib. On the compression side, 0 is invalid.
// On the decompression side, it indicates that zlib should use the value from
// the header of the compressed stream.
test('zlib should support zero windowBits', (t) => {
  const inflate = zlib.createInflate({ windowBits: 0 });
  assert.ok(inflate instanceof zlib.Inflate);

  const gunzip = zlib.createGunzip({ windowBits: 0 });
  assert.ok(gunzip instanceof zlib.Gunzip);

  const unzip = zlib.createUnzip({ windowBits: 0 });
  assert.ok(unzip instanceof zlib.Unzip);
});

test('windowBits should be valid', () => {
  assert.throws(() => zlib.createGzip({ windowBits: 0 }), {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "options.windowBits" is out of range. ' +
      'It must be >= 9 and <= 15. Received 0'
  });
});
