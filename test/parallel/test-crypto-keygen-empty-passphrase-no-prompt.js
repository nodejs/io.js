'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const {
  createPrivateKey,
  generateKeyPair,
} = require('crypto');
const {
  testSignVerify,
  hasOpenSSL3,
} = require('../common/crypto');

// Passing an empty passphrase string should not cause OpenSSL's default
// passphrase prompt in the terminal.
// See https://github.com/nodejs/node/issues/35898.
for (const type of ['pkcs1', 'pkcs8']) {
  generateKeyPair('rsa', {
    modulusLength: 1024,
    privateKeyEncoding: {
      type,
      format: 'pem',
      cipher: 'aes-256-cbc',
      passphrase: ''
    }
  }, common.mustSucceed((publicKey, privateKey) => {
    assert.strictEqual(publicKey.type, 'public');

    for (const passphrase of ['', Buffer.alloc(0)]) {
      const privateKeyObject = createPrivateKey({
        passphrase,
        key: privateKey
      });
      assert.strictEqual(privateKeyObject.asymmetricKeyType, 'rsa');
    }

    // Encrypting with an empty passphrase is not the same as not encrypting
    // the key, and not specifying a passphrase should fail when decoding it.
    assert.throws(() => {
      return testSignVerify(publicKey, privateKey);
    }, hasOpenSSL3 ? {
      name: 'Error',
      code: 'ERR_OSSL_CRYPTO_INTERRUPTED_OR_CANCELLED',
      message: 'error:07880109:common libcrypto routines::interrupted or cancelled'
    } : {
      name: 'TypeError',
      code: 'ERR_MISSING_PASSPHRASE',
      message: 'Passphrase required for encrypted key'
    });
  }));
}
