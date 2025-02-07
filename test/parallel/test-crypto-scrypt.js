'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');

if (typeof crypto.scrypt !== 'function' || typeof crypto.scryptSync !== 'function')
  common.skip('no scrypt support');

const good = [
  // Zero-length key is legal, functions as a parameter validation check.
  {
    pass: '',
    salt: '',
    keylen: 0,
    N: 16,
    p: 1,
    r: 1,
    expected: '',
  },
  // Test vectors from https://tools.ietf.org/html/rfc7914#page-13 that
  // should pass.  Note that the test vector with N=1048576 is omitted
  // because it takes too long to complete and uses over 1 GiB of memory.
  {
    pass: '',
    salt: '',
    keylen: 64,
    N: 16,
    p: 1,
    r: 1,
    expected:
        '77d6576238657b203b19ca42c18a0497f16b4844e3074ae8dfdffa3fede21442' +
        'fcd0069ded0948f8326a753a0fc81f17e8d3e0fb2e0d3628cf35e20c38d18906',
  },
  {
    pass: 'password',
    salt: 'NaCl',
    keylen: 64,
    N: 1024,
    p: 16,
    r: 8,
    expected:
        'fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b373162' +
        '2eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0640',
  },
  {
    pass: 'pleaseletmein',
    salt: 'SodiumChloride',
    keylen: 64,
    N: 16384,
    p: 1,
    r: 8,
    expected:
        '7023bdcb3afd7348461c06cd81fd38ebfda8fbba904f8e3ea9b543f6545da1f2' +
        'd5432955613f0fcf62d49705242a9af9e61e85dc0d651e40dfcf017b45575887',
  },
  {
    pass: '',
    salt: '',
    keylen: 64,
    cost: 16,
    parallelization: 1,
    blockSize: 1,
    expected:
        '77d6576238657b203b19ca42c18a0497f16b4844e3074ae8dfdffa3fede21442' +
        'fcd0069ded0948f8326a753a0fc81f17e8d3e0fb2e0d3628cf35e20c38d18906',
  },
  {
    pass: 'password',
    salt: 'NaCl',
    keylen: 64,
    cost: 1024,
    parallelization: 16,
    blockSize: 8,
    expected:
        'fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b373162' +
        '2eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0640',
  },
  {
    pass: 'pleaseletmein',
    salt: 'SodiumChloride',
    keylen: 64,
    cost: 16384,
    parallelization: 1,
    blockSize: 8,
    expected:
        '7023bdcb3afd7348461c06cd81fd38ebfda8fbba904f8e3ea9b543f6545da1f2' +
        'd5432955613f0fcf62d49705242a9af9e61e85dc0d651e40dfcf017b45575887',
  },
];

// Test vectors that contain invalid parameters.
const bad = [
  { N: 1, p: 1, r: 1 },         // N < 2
  { N: 3, p: 1, r: 1 },         // Not power of 2.
];

// Test vectors that contain incompatible options.
const incompatibleOptions = [
  { N: 1, cost: 1 },             // Both N and cost
  { p: 1, parallelization: 1 },  // Both p and parallelization
  { r: 1, blockSize: 1 },        // Both r and blocksize
];

// Test vectors where 128*N*r exceeds maxmem.
const toobig = [
  { N: 2 ** 16, p: 1, r: 1 },   // N >= 2**(r*16)
  { N: 2, p: 2 ** 30, r: 1 },   // p > (2**30-1)/r
  { N: 2 ** 20, p: 1, r: 8 },
  { N: 2 ** 10, p: 1, r: 8, maxmem: 2 ** 20 },
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
    args: [''],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"salt"/ },
  },
  {
    args: ['', null],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"salt"/ },
  },
  {
    args: ['', ''],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"keylen"/ },
  },
  {
    args: ['', '', null],
    expected: { code: 'ERR_INVALID_ARG_TYPE', message: /"keylen"/ },
  },
  {
    args: ['', '', .42],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
  {
    args: ['', '', -42],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
  {
    args: ['', '', 2 ** 31],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
  {
    args: ['', '', 2147485780],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
  {
    args: ['', '', 2 ** 32],
    expected: { code: 'ERR_OUT_OF_RANGE', message: /"keylen"/ },
  },
];

for (const options of good) {
  const { pass, salt, keylen, expected } = options;
  const actual = crypto.scryptSync(pass, salt, keylen, options);
  assert.strictEqual(actual.toString('hex'), expected);
  crypto.scrypt(pass, salt, keylen, options, common.mustSucceed((actual) => {
    assert.strictEqual(actual.toString('hex'), expected);
  }));
}

for (const options of bad) {
  const expected = {
    message: /Invalid scrypt param/,
  };
  assert.throws(() => crypto.scrypt('pass', 'salt', 1, options, () => {}),
                expected);
  assert.throws(() => crypto.scryptSync('pass', 'salt', 1, options),
                expected);
}

for (const options of incompatibleOptions) {
  const [short, long] = Object.keys(options).sort((a, b) => a.length - b.length);
  const expected = {
    message: `Option "${short}" cannot be used in combination with option "${long}"`,
    code: 'ERR_INCOMPATIBLE_OPTION_PAIR',
  };
  assert.throws(() => crypto.scrypt('pass', 'salt', 1, options, () => {}),
                expected);
  assert.throws(() => crypto.scryptSync('pass', 'salt', 1, options),
                expected);
}

for (const options of toobig) {
  const expected = {
    message: /Invalid scrypt params:.*memory limit exceeded/,
    code: 'ERR_CRYPTO_INVALID_SCRYPT_PARAMS',
  };
  assert.throws(() => crypto.scrypt('pass', 'salt', 1, options, () => {}),
                expected);
  assert.throws(() => crypto.scryptSync('pass', 'salt', 1, options),
                expected);
}

{
  const defaults = { N: 16384, p: 1, r: 8 };
  const expected = crypto.scryptSync('pass', 'salt', 1, defaults);
  const actual = crypto.scryptSync('pass', 'salt', 1);
  assert.deepStrictEqual(actual.toString('hex'), expected.toString('hex'));
  crypto.scrypt('pass', 'salt', 1, common.mustSucceed((actual) => {
    assert.deepStrictEqual(actual.toString('hex'), expected.toString('hex'));
  }));
}

for (const { args, expected } of badargs) {
  assert.throws(() => crypto.scrypt(...args), expected);
  assert.throws(() => crypto.scryptSync(...args), expected);
}

{
  const expected = { code: 'ERR_INVALID_ARG_TYPE' };
  assert.throws(() => crypto.scrypt('', '', 42, null), expected);
  assert.throws(() => crypto.scrypt('', '', 42, {}, null), expected);
  assert.throws(() => crypto.scrypt('', '', 42, {}), expected);
  assert.throws(() => crypto.scrypt('', '', 42, {}, {}), expected);
}

{
  // Values for maxmem that do not fit in 32 bits but that are still safe
  // integers should be allowed.
  crypto.scrypt('', '', 4, { maxmem: 2 ** 52 },
                common.mustSucceed((actual) => {
                  assert.strictEqual(actual.toString('hex'), 'd72c87d0');
                }));

  // Values that exceed Number.isSafeInteger should not be allowed.
  assert.throws(() => crypto.scryptSync('', '', 0, { maxmem: 2 ** 53 }), {
    code: 'ERR_OUT_OF_RANGE'
  });
}

{
  // Regression test for https://github.com/nodejs/node/issues/28836.

  function testParameter(name, value) {
    let accessCount = 0;

    // Find out how often the value is accessed.
    crypto.scryptSync('', '', 1, {
      get [name]() {
        accessCount++;
        return value;
      }
    });

    // Try to crash the process on the last access.
    assert.throws(() => {
      crypto.scryptSync('', '', 1, {
        get [name]() {
          if (--accessCount === 0)
            return '';
          return value;
        }
      });
    }, {
      code: 'ERR_INVALID_ARG_TYPE'
    });
  }

  [
    ['N', 16384], ['cost', 16384],
    ['r', 8], ['blockSize', 8],
    ['p', 1], ['parallelization', 1],
  ].forEach((arg) => testParameter(...arg));
}
