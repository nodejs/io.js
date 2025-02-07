'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const { kMaxLength } = require('buffer');
const assert = require('assert');
const {
  createSecretKey,
  hkdf,
  hkdfSync,
  getHashes
} = require('crypto');
const { hasOpenSSL3 } = require('../common/crypto');

{
  assert.throws(() => hkdf(), {
    code: 'ERR_INVALID_ARG_TYPE',
    message: /The "digest" argument must be of type string/
  });

  [1, {}, [], false, Infinity].forEach((i) => {
    assert.throws(() => hkdf(i, 'a'), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "digest" argument must be of type string/
    });
    assert.throws(() => hkdfSync(i, 'a'), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "digest" argument must be of type string/
    });
  });

  [1, {}, [], false, Infinity].forEach((i) => {
    assert.throws(() => hkdf('sha256', i), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "ikm" argument must be /
    });
    assert.throws(() => hkdfSync('sha256', i), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "ikm" argument must be /
    });
  });

  [1, {}, [], false, Infinity].forEach((i) => {
    assert.throws(() => hkdf('sha256', 'secret', i), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "salt" argument must be /
    });
    assert.throws(() => hkdfSync('sha256', 'secret', i), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "salt" argument must be /
    });
  });

  [1, {}, [], false, Infinity].forEach((i) => {
    assert.throws(() => hkdf('sha256', 'secret', 'salt', i), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "info" argument must be /
    });
    assert.throws(() => hkdfSync('sha256', 'secret', 'salt', i), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "info" argument must be /
    });
  });

  ['test', {}, [], false].forEach((i) => {
    assert.throws(() => hkdf('sha256', 'secret', 'salt', 'info', i), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "length" argument must be of type number/
    });
    assert.throws(() => hkdfSync('sha256', 'secret', 'salt', 'info', i), {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "length" argument must be of type number/
    });
  });

  assert.throws(() => hkdf('sha256', 'secret', 'salt', 'info', -1), {
    code: 'ERR_OUT_OF_RANGE'
  });
  assert.throws(() => hkdfSync('sha256', 'secret', 'salt', 'info', -1), {
    code: 'ERR_OUT_OF_RANGE'
  });
  assert.throws(() => hkdf('sha256', 'secret', 'salt', 'info',
                           kMaxLength + 1), {
    code: 'ERR_OUT_OF_RANGE'
  });
  assert.throws(() => hkdfSync('sha256', 'secret', 'salt', 'info',
                               kMaxLength + 1), {
    code: 'ERR_OUT_OF_RANGE'
  });

  assert.throws(() => hkdfSync('unknown', 'a', '', '', 10), {
    code: 'ERR_CRYPTO_INVALID_DIGEST'
  });

  assert.throws(() => hkdf('unknown', 'a', '', '', 10, common.mustNotCall()), {
    code: 'ERR_CRYPTO_INVALID_DIGEST'
  });

  assert.throws(() => hkdf('unknown', 'a', '', Buffer.alloc(1025), 10,
                           common.mustNotCall()), {
    code: 'ERR_OUT_OF_RANGE'
  });

  assert.throws(() => hkdfSync('unknown', 'a', '', Buffer.alloc(1025), 10), {
    code: 'ERR_OUT_OF_RANGE'
  });

  assert.throws(
    () => hkdf('sha512', 'a', '', '', 64 * 255 + 1, common.mustNotCall()), {
      code: 'ERR_CRYPTO_INVALID_KEYLEN'
    });

  assert.throws(
    () => hkdfSync('sha512', 'a', '', '', 64 * 255 + 1), {
      code: 'ERR_CRYPTO_INVALID_KEYLEN'
    });
}

const algorithms = [
  ['sha256', 'secret', 'salt', 'info', 10],
  ['sha256', '', '', '', 10],
  ['sha256', '', 'salt', '', 10],
  ['sha512', 'secret', 'salt', '', 15],
];
if (!hasOpenSSL3)
  algorithms.push(['whirlpool', 'secret', '', 'info', 20]);

algorithms.forEach(([ hash, secret, salt, info, length ]) => {
  {
    const syncResult = hkdfSync(hash, secret, salt, info, length);
    assert(syncResult instanceof ArrayBuffer);
    let is_async = false;
    hkdf(hash, secret, salt, info, length,
         common.mustSucceed((asyncResult) => {
           assert(is_async);
           assert(asyncResult instanceof ArrayBuffer);
           assert.deepStrictEqual(syncResult, asyncResult);
         }));
    // Keep this after the hkdf call above. This verifies
    // that the callback is invoked asynchronously.
    is_async = true;
  }

  {
    const buf_secret = Buffer.from(secret);
    const buf_salt = Buffer.from(salt);
    const buf_info = Buffer.from(info);

    const syncResult = hkdfSync(hash, buf_secret, buf_salt, buf_info, length);
    hkdf(hash, buf_secret, buf_salt, buf_info, length,
         common.mustSucceed((asyncResult) => {
           assert.deepStrictEqual(syncResult, asyncResult);
         }));
  }

  {
    const key_secret = createSecretKey(Buffer.from(secret));
    const buf_salt = Buffer.from(salt);
    const buf_info = Buffer.from(info);

    const syncResult = hkdfSync(hash, key_secret, buf_salt, buf_info, length);
    hkdf(hash, key_secret, buf_salt, buf_info, length,
         common.mustSucceed((asyncResult) => {
           assert.deepStrictEqual(syncResult, asyncResult);
         }));
  }

  {
    const ta_secret = new Uint8Array(Buffer.from(secret));
    const ta_salt = new Uint16Array(Buffer.from(salt));
    const ta_info = new Uint32Array(Buffer.from(info));

    const syncResult = hkdfSync(hash, ta_secret, ta_salt, ta_info, length);
    hkdf(hash, ta_secret, ta_salt, ta_info, length,
         common.mustSucceed((asyncResult) => {
           assert.deepStrictEqual(syncResult, asyncResult);
         }));
  }

  {
    const ta_secret = new Uint8Array(Buffer.from(secret));
    const ta_salt = new Uint16Array(Buffer.from(salt));
    const ta_info = new Uint32Array(Buffer.from(info));

    const syncResult = hkdfSync(
      hash,
      ta_secret.buffer,
      ta_salt.buffer,
      ta_info.buffer,
      length);
    hkdf(hash, ta_secret, ta_salt, ta_info, length,
         common.mustSucceed((asyncResult) => {
           assert.deepStrictEqual(syncResult, asyncResult);
         }));
  }

  {
    const ta_secret = new Uint8Array(Buffer.from(secret));
    const sa_salt = new SharedArrayBuffer(0);
    const sa_info = new SharedArrayBuffer(1);

    const syncResult = hkdfSync(
      hash,
      ta_secret.buffer,
      sa_salt,
      sa_info,
      length);
    hkdf(hash, ta_secret, sa_salt, sa_info, length,
         common.mustSucceed((asyncResult) => {
           assert.deepStrictEqual(syncResult, asyncResult);
         }));
  }
});


if (!hasOpenSSL3) {
  const kKnownUnsupported = ['shake128', 'shake256'];
  getHashes()
    .filter((hash) => !kKnownUnsupported.includes(hash))
    .forEach((hash) => {
      assert(hkdfSync(hash, 'key', 'salt', 'info', 5));
    });
}
