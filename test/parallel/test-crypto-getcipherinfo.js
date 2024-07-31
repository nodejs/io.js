'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const {
  getCiphers,
  getCipherInfo
} = require('crypto');

const assert = require('assert');

const ciphers = getCiphers();

assert.strictEqual(getCipherInfo(-1), undefined);
assert.strictEqual(getCipherInfo('cipher that does not exist'), undefined);

for (const cipher of ciphers) {
  const info = getCipherInfo(cipher);
  assert(info);
  const info2 = getCipherInfo(info.nid);
  assert.deepStrictEqual(info, info2);
}

const info = getCipherInfo('aes-128-cbc');
assert.strictEqual(info.name, 'aes-128-cbc');
assert.strictEqual(info.nid, 419);
assert.strictEqual(info.blockSize, 16);
assert.strictEqual(info.ivLength, 16);
assert.strictEqual(info.keyLength, 16);
assert.strictEqual(info.mode, 'cbc');

[null, undefined, [], {}].forEach((arg) => {
  assert.throws(() => getCipherInfo(arg), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

[null, '', 1, true].forEach((options) => {
  assert.throws(
    () => getCipherInfo('aes-192-cbc', options), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
});

[null, '', {}, [], true].forEach((len) => {
  assert.throws(
    () => getCipherInfo('aes-192-cbc', { keyLength: len }), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
  assert.throws(
    () => getCipherInfo('aes-192-cbc', { ivLength: len }), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
});

assert(!getCipherInfo('aes-128-cbc', { keyLength: 12 }));
assert(getCipherInfo('aes-128-cbc', { keyLength: 16 }));
assert(!getCipherInfo('aes-128-cbc', { ivLength: 12 }));
assert(getCipherInfo('aes-128-cbc', { ivLength: 16 }));

assert(!getCipherInfo('aes-128-ccm', { ivLength: 1 }));
assert(!getCipherInfo('aes-128-ccm', { ivLength: 14 }));
for (let n = 7; n <= 13; n++)
  assert(getCipherInfo('aes-128-ccm', { ivLength: n }));

assert(!getCipherInfo('aes-128-ocb', { ivLength: 16 }));
for (let n = 1; n < 16; n++)
  assert(getCipherInfo('aes-128-ocb', { ivLength: n }));
