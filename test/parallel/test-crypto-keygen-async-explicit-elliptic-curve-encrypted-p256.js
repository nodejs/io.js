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
  pkcs8EncExp,
} = require('../common/crypto');

const { hasOpenSSL3 } = require('../common/crypto');

// Test async elliptic curve key generation, e.g. for ECDSA, with an encrypted
// private key with paramEncoding explicit.
{
  generateKeyPair('ec', {
    namedCurve: 'P-256',
    paramEncoding: 'explicit',
    publicKeyEncoding: {
      type: 'spki',
      format: 'pem'
    },
    privateKeyEncoding: {
      type: 'pkcs8',
      format: 'pem',
      cipher: 'aes-128-cbc',
      passphrase: 'top secret'
    }
  }, common.mustSucceed((publicKey, privateKey) => {
    assert.strictEqual(typeof publicKey, 'string');
    assert.match(publicKey, spkiExp);
    assert.strictEqual(typeof privateKey, 'string');
    assert.match(privateKey, pkcs8EncExp);

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

    testSignVerify(publicKey, {
      key: privateKey,
      passphrase: 'top secret'
    });
  }));
}
