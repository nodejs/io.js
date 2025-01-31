'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const {
  generateKeyPair,
} = require('crypto');
const {
  assertApproximateSize,
  testEncryptDecrypt,
  testSignVerify,
  pkcs1EncExp,
  hasOpenSSL3,
} = require('../common/crypto');

// Test async RSA key generation with an encrypted private key.
{
  generateKeyPair('rsa', {
    publicExponent: 0x10001,
    modulusLength: 512,
    publicKeyEncoding: {
      type: 'pkcs1',
      format: 'der'
    },
    privateKeyEncoding: {
      type: 'pkcs1',
      format: 'pem',
      cipher: 'aes-256-cbc',
      passphrase: 'secret'
    }
  }, common.mustSucceed((publicKeyDER, privateKey) => {
    assert(Buffer.isBuffer(publicKeyDER));
    assertApproximateSize(publicKeyDER, 74);

    assert.strictEqual(typeof privateKey, 'string');
    assert.match(privateKey, pkcs1EncExp('AES-256-CBC'));

    // Since the private key is encrypted, signing shouldn't work anymore.
    const publicKey = {
      key: publicKeyDER,
      type: 'pkcs1',
      format: 'der',
    };
    const expectedError = hasOpenSSL3 ? {
      name: 'Error',
      message: 'error:07880109:common libcrypto routines::interrupted or ' +
               'cancelled'
    } : {
      name: 'TypeError',
      code: 'ERR_MISSING_PASSPHRASE',
      message: 'Passphrase required for encrypted key'
    };
    assert.throws(() => testSignVerify(publicKey, privateKey), expectedError);

    const key = { key: privateKey, passphrase: 'secret' };
    testEncryptDecrypt(publicKey, key);
    testSignVerify(publicKey, key);
  }));
}
