'use strict';
const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const assert = require('assert');
const crypto = require('crypto');
const {
  hasOpenSSL3,
  hasOpenSSL,
} = require('../common/crypto');

{
  const size = crypto.getFips() || hasOpenSSL3 ? 1024 : 256;
  const dh1 = crypto.createDiffieHellman(size);
  const p1 = dh1.getPrime('buffer');
  const dh2 = crypto.createDiffieHellman(p1, 'buffer');
  const key1 = dh1.generateKeys();
  const key2 = dh2.generateKeys('hex');
  const secret1 = dh1.computeSecret(key2, 'hex', 'base64');
  const secret2 = dh2.computeSecret(key1, 'latin1', 'buffer');

  // Test Diffie-Hellman with two parties sharing a secret,
  // using various encodings as we go along
  assert.strictEqual(secret2.toString('base64'), secret1);
  assert.strictEqual(dh1.verifyError, 0);
  assert.strictEqual(dh2.verifyError, 0);

  // Create "another dh1" using generated keys from dh1,
  // and compute secret again
  const dh3 = crypto.createDiffieHellman(p1, 'buffer');
  const privkey1 = dh1.getPrivateKey();
  dh3.setPublicKey(key1);
  dh3.setPrivateKey(privkey1);

  assert.deepStrictEqual(dh1.getPrime(), dh3.getPrime());
  assert.deepStrictEqual(dh1.getGenerator(), dh3.getGenerator());
  assert.deepStrictEqual(dh1.getPublicKey(), dh3.getPublicKey());
  assert.deepStrictEqual(dh1.getPrivateKey(), dh3.getPrivateKey());
  assert.strictEqual(dh3.verifyError, 0);

  const secret3 = dh3.computeSecret(key2, 'hex', 'base64');

  assert.strictEqual(secret1, secret3);

  // computeSecret works without a public key set at all.
  const dh4 = crypto.createDiffieHellman(p1, 'buffer');
  dh4.setPrivateKey(privkey1);

  assert.deepStrictEqual(dh1.getPrime(), dh4.getPrime());
  assert.deepStrictEqual(dh1.getGenerator(), dh4.getGenerator());
  assert.deepStrictEqual(dh1.getPrivateKey(), dh4.getPrivateKey());
  assert.strictEqual(dh4.verifyError, 0);

  const secret4 = dh4.computeSecret(key2, 'hex', 'base64');

  assert.strictEqual(secret1, secret4);

  let wrongBlockLength;
  if (hasOpenSSL3) {
    wrongBlockLength = {
      message: 'error:1C80006B:Provider routines::wrong final block length',
      code: 'ERR_OSSL_WRONG_FINAL_BLOCK_LENGTH',
      library: 'Provider routines',
      reason: 'wrong final block length'
    };
  } else {
    wrongBlockLength = {
      message: 'error:0606506D:digital envelope' +
        ' routines:EVP_DecryptFinal_ex:wrong final block length',
      code: 'ERR_OSSL_EVP_WRONG_FINAL_BLOCK_LENGTH',
      library: 'digital envelope routines',
      reason: 'wrong final block length'
    };
  }

  // Run this one twice to make sure that the dh3 clears its error properly
  {
    const c = crypto.createDecipheriv('aes-128-ecb', crypto.randomBytes(16), '');
    assert.throws(() => {
      c.final('utf8');
    }, wrongBlockLength);
  }

  {
    const c = crypto.createDecipheriv('aes-128-ecb', crypto.randomBytes(16), '');
    assert.throws(() => {
      c.final('utf8');
    }, wrongBlockLength);
  }

  {
    // Error message was changed in OpenSSL 3.0.x from 3.0.12, and 3.1.x from 3.1.4.
    const hasOpenSSL3WithNewErrorMessage = (hasOpenSSL(3, 0, 12) && !hasOpenSSL(3, 1, 0)) ||
                                           (hasOpenSSL(3, 1, 4));
    assert.throws(() => {
      dh3.computeSecret('');
    }, { message: hasOpenSSL3 && !hasOpenSSL3WithNewErrorMessage ?
      'Unspecified validation error' :
      'Supplied key is too small' });
  }
}

// Through a fluke of history, g=0 defaults to DH_GENERATOR (2).
{
  const g = 0;
  crypto.createDiffieHellman('abcdef', g);
  crypto.createDiffieHellman('abcdef', 'hex', g);
}

{
  crypto.createDiffieHellman('abcdef', Buffer.from([2]));  // OK
}
