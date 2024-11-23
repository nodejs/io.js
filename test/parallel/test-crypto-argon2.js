// Flags: --expose-internals
'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('node:assert');
const crypto = require('node:crypto');

function runArgon2(password, salt, keylen, options) {
  const syncResult =
    crypto.argon2Sync(password, salt, keylen, options);

  crypto.argon2(password, salt, keylen, options,
                common.mustSucceed((asyncResult) => {
                  assert.deepStrictEqual(asyncResult, syncResult);
                }));

  return syncResult;
}

const password = Buffer.alloc(32, 0x01);
const salt = Buffer.alloc(16, 0x02);
const secret = Buffer.alloc(8, 0x03);
const ad = Buffer.alloc(12, 0x04);
const defaults = { password, salt, keylen: 32, iter: 3, lanes: 4, memcost: 64 << 10 };

const good = [
  [
    { algorithm: "ARGON2D" },
    'bf37a2a7530e053a8bd2784a9d50dc7de451e33bd581096922bc7f9ef66020ed',
  ],
  [
    { algorithm: "ARGON2I" },
    '96334882febdd85eb9b2cf367479fbad2d5b87cf79f9076f51b23589560a2e0a',
  ],
  [
    { algorithm: "ARGON2ID" },
    'fedd802b86b17230843c6d3f025c81d3f472fbf9daaf26897fa88844732167ec',
  ],
  [
    { secret },
    '71d8a1361e3660f8e8158021a6870f223d42342c6a194429252c6d6ad19c25d6',
  ],
  [
    { ad },
    '7da97d5dd2ba433dbb1b26e00a496caa673501b9a437e863855201eb51cea6ef',
  ],
  [
    { secret, ad },
    '47a677ebc68412b8e17949c921fdca065352114ffb2a09eebe61adc451eecf1e',
  ],
  [
    { iter: 2 },
    '2139b90863e8bbffff1459c9f08f6460db08def592991e78f11b6c5765bd1fac',
  ],
  [
    { lanes: 3 },
    '8ba6f33b93c8219cd31385966898fd39c8e570671c6a3c180ee0109a27d62a4b',
  ],
  [
    { memcost: 32 << 10 },
    '2beec876bf5b1098e8349d094385eda1dd472733d103e0f1afad4d28f948e808',
  ],
];

// Test vectors that should fail.
const bad = [
  [{ iter: 0 }, "iter"], // iter < 2
  [{ iter: 2 ** 32 }, "iter"], // iter > 2^(32)-1
  [{ lanes: 0 }, "lanes"], // lanes < 1
  [{ lanes: 2 ** 24 }, "lanes"], // lanes > 2^(24)-1
  [{ lanes: 4, memcost: 16 }, "memcost"], // memcost < 8 * lanes
  [{ memcost: 2 ** 32 }, "memcost"], // memcost > 2^(32)-1
];

const badargs = [
  {
    args: [],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"password"/ },
  },
  {
    args: [null],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"password"/ },
  },
  {
    args: [password],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"salt"/ },
  },
  {
    args: [password, null],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"salt"/ },
  },
  {
    args: [password, salt.subarray(0, 7)],
    expected: { code: 'ERR_CRYPTO_INVALID_SALT_LENGTH', message: /salt length/ },
  },
  {
    args: [password, salt],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"keylen"/ },
  },
  {
    args: [password, salt, null],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"keylen"/ },
  },
  {
    args: [password, salt, .42],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
  {
    args: [password, salt, -42],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
  {
    args: [password, salt, 3],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
  {
    args: [password, salt, 2 ** 32],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
];

for (const [overrides, expected] of good) {
  const { password, salt, keylen, ...options } = { ...defaults, ...overrides };
  const actual = runArgon2(password, salt, keylen, options);
  assert.strictEqual(actual.toString('hex'), expected);
}

for (const [options, param] of bad) {
  const expected = {
    code:'ERR_OUT_OF_RANGE',
    message: new RegExp(`The value of "${param}" is out of range.`),
  };
  assert.throws(() => crypto.argon2(password, salt, 32, options, () => {}),
                expected);
  assert.throws(() => crypto.argon2Sync(password, salt, 32, options),
                expected);
}

{
  const expected = '07a0f408fd6bb1190be8e1600f680304e4680f72af121c3399308f48aba67ce4';
  const actual = crypto.argon2Sync('', salt, 32, defaults);
  assert.strictEqual(actual.toString('hex'), expected);
}

{
  const expected = crypto.argon2Sync(password, salt, 32, defaults);
  const actual = crypto.argon2Sync(password, salt, 32);
  assert.deepStrictEqual(actual.toString('hex'), expected.toString('hex'));
  crypto.argon2(password, salt, 32, common.mustSucceed((actual) => {
    assert.deepStrictEqual(actual.toString('hex'), expected.toString('hex'));
  }));
}

for (const { args, expected } of badargs) {
  assert.throws(() => crypto.argon2(...args), expected);
  assert.throws(() => crypto.argon2Sync(...args), expected);
}

{
  const expected = { code: 'ERR_INVALID_ARG_TYPE' };
  assert.throws(() => crypto.argon2(password, salt, 32, null), expected);
  assert.throws(() => crypto.argon2(password, salt, 32, {}, null), expected);
  assert.throws(() => crypto.argon2(password, salt, 32, {}), expected);
  assert.throws(() => crypto.argon2(password, salt, 32, {}, {}), expected);
}
