'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const { hasOpenSSL3 } = require('../common/crypto');

// https://github.com/nodejs/node/issues/32738
// XXX(bnoordhuis) validateInt32() throwing ERR_OUT_OF_RANGE and RangeError
// instead of ERR_INVALID_ARG_TYPE and TypeError is questionable, IMO.
assert.throws(() => crypto.createDiffieHellman(13.37), {
  code: 'ERR_OUT_OF_RANGE',
  name: 'RangeError',
  message: 'The value of "sizeOrKey" is out of range. ' +
           'It must be an integer. Received 13.37',
});

assert.throws(() => crypto.createDiffieHellman('abcdef', 13.37), {
  code: 'ERR_OUT_OF_RANGE',
  name: 'RangeError',
  message: 'The value of "generator" is out of range. ' +
           'It must be an integer. Received 13.37',
});

for (const bits of [-1, 0, 1]) {
  if (hasOpenSSL3) {
    assert.throws(() => crypto.createDiffieHellman(bits), {
      code: 'ERR_OSSL_DH_MODULUS_TOO_SMALL',
      name: 'Error',
      message: /modulus too small/,
    });
  } else {
    assert.throws(() => crypto.createDiffieHellman(bits), {
      code: 'ERR_OSSL_BN_BITS_TOO_SMALL',
      name: 'Error',
      message: /bits too small/,
    });
  }
}

for (const g of [-1, 1]) {
  const ex = {
    code: 'ERR_OSSL_DH_BAD_GENERATOR',
    name: 'Error',
    message: /(?:bad[_ ]generator)/i,
  };
  assert.throws(() => crypto.createDiffieHellman('abcdef', g), ex);
  assert.throws(() => crypto.createDiffieHellman('abcdef', 'hex', g), ex);
}

for (const g of [Buffer.from([]),
                 Buffer.from([0]),
                 Buffer.from([1])]) {
  const ex = {
    code: 'ERR_OSSL_DH_BAD_GENERATOR',
    name: 'Error',
    message: /(?:bad[_ ]generator)/i,
  };
  assert.throws(() => crypto.createDiffieHellman('abcdef', g), ex);
  assert.throws(() => crypto.createDiffieHellman('abcdef', 'hex', g), ex);
}

[
  [0x1, 0x2],
  () => { },
  /abc/,
  {},
].forEach((input) => {
  assert.throws(
    () => crypto.createDiffieHellman(input),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
    }
  );
});

// Invalid test: curve argument is undefined
assert.throws(
  () => crypto.createECDH(),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "curve" argument must be of type string. ' +
            'Received undefined'
  });

assert.throws(
  function() {
    crypto.getDiffieHellman('unknown-group');
  },
  {
    name: 'Error',
    code: 'ERR_CRYPTO_UNKNOWN_DH_GROUP',
    message: 'Unknown DH group'
  },
  'crypto.getDiffieHellman(\'unknown-group\') ' +
  'failed to throw the expected error.'
);

assert.throws(
  () => crypto.createDiffieHellman('', true),
  {
    code: 'ERR_INVALID_ARG_TYPE'
  }
);
[true, Symbol(), {}, () => {}, []].forEach((generator) => assert.throws(
  () => crypto.createDiffieHellman('', 'base64', generator),
  { code: 'ERR_INVALID_ARG_TYPE' }
));
