'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const {
  generateKeyPair,
} = require('crypto');
const {
  testSignVerify,
  spkiExp,
  sec1EncExp,
  hasOpenSSL3,
} = require('../common/crypto');

{
  // Test async explicit elliptic curve key generation with an encrypted
  // private key.
  generateKeyPair('ec', {
    namedCurve: 'prime256v1',
    paramEncoding: 'explicit',
    publicKeyEncoding: {
      type: 'spki',
      format: 'pem'
    },
    privateKeyEncoding: {
      type: 'sec1',
      format: 'pem',
      cipher: 'aes-128-cbc',
      passphrase: 'secret'
    }
  }, common.mustSucceed((publicKey, privateKey) => {
    assert.strictEqual(typeof publicKey, 'string');
    assert.match(publicKey, spkiExp);
    assert.strictEqual(typeof privateKey, 'string');
    assert.match(privateKey, sec1EncExp('AES-128-CBC'));

    // Since the private key is encrypted, signing shouldn't work anymore.
    assert.throws(() => testSignVerify(publicKey, privateKey),
                  hasOpenSSL3 ? {
                    message: 'error:07880109:common libcrypto ' +
                             'routines::interrupted or cancelled'
                  } : {
                    name: 'TypeError',
                    code: 'ERR_MISSING_PASSPHRASE',
                    message: 'Passphrase required for encrypted key'
                  });

    testSignVerify(publicKey, { key: privateKey, passphrase: 'secret' });
  }));
}
