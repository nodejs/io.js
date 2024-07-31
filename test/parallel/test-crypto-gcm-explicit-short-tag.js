// Flags: --pending-deprecation
'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { createDecipheriv, randomBytes } = require('crypto');

common.expectWarning({
  DeprecationWarning: []
});

const key = randomBytes(32);
const iv = randomBytes(16);

{
  // Full 128-bit tag.

  const tag = randomBytes(16);
  createDecipheriv('aes-256-gcm', key, iv).setAuthTag(tag);
}

{
  // Shortened tag with explicit length option.

  const tag = randomBytes(12);
  createDecipheriv('aes-256-gcm', key, iv, {
    authTagLength: tag.byteLength
  }).setAuthTag(tag);
}

{
  // Shortened tag with explicit but incorrect length option.

  const tag = randomBytes(12);
  assert.throws(() => {
    createDecipheriv('aes-256-gcm', key, iv, {
      authTagLength: 14
    }).setAuthTag(tag);
  }, {
    name: 'TypeError',
    message: 'Invalid authentication tag length: 12',
    code: 'ERR_CRYPTO_INVALID_AUTH_TAG'
  });
}
