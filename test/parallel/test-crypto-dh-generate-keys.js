'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const { hasOpenSSL3 } = require('../common/crypto');

{
  const size = crypto.getFips() || hasOpenSSL3 ? 1024 : 256;

  function unlessInvalidState(f) {
    try {
      return f();
    } catch (err) {
      if (err.code !== 'ERR_CRYPTO_INVALID_STATE') {
        throw err;
      }
    }
  }

  function testGenerateKeysChangesKeys(setup, expected) {
    const dh = crypto.createDiffieHellman(size);
    setup(dh);
    const firstPublicKey = unlessInvalidState(() => dh.getPublicKey());
    const firstPrivateKey = unlessInvalidState(() => dh.getPrivateKey());
    dh.generateKeys();
    const secondPublicKey = dh.getPublicKey();
    const secondPrivateKey = dh.getPrivateKey();
    function changed(shouldChange, first, second) {
      if (shouldChange) {
        assert.notDeepStrictEqual(first, second);
      } else {
        assert.deepStrictEqual(first, second);
      }
    }
    changed(expected.includes('public'), firstPublicKey, secondPublicKey);
    changed(expected.includes('private'), firstPrivateKey, secondPrivateKey);
  }

  // Both the private and the public key are missing: generateKeys() generates both.
  testGenerateKeysChangesKeys(() => {
    // No setup.
  }, ['public', 'private']);

  // Neither key is missing: generateKeys() does nothing.
  testGenerateKeysChangesKeys((dh) => {
    dh.generateKeys();
  }, []);

  // Only the public key is missing: generateKeys() generates only the public key.
  testGenerateKeysChangesKeys((dh) => {
    dh.setPrivateKey(Buffer.from('01020304', 'hex'));
  }, ['public']);

  // The public key is outdated: generateKeys() generates only the public key.
  testGenerateKeysChangesKeys((dh) => {
    const oldPublicKey = dh.generateKeys();
    dh.setPrivateKey(Buffer.from('01020304', 'hex'));
    assert.deepStrictEqual(dh.getPublicKey(), oldPublicKey);
  }, ['public']);
}
