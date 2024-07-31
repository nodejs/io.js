'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const { createDecipheriv, randomBytes } = require('crypto');

common.expectWarning({
  DeprecationWarning: [
    ['Using AES-GCM authentication tags of less than 128 bits without ' +
     'specifying the authTagLength option when initializing decryption is ' +
     'deprecated.',
     'DEP0182'],
  ]
});

const key = randomBytes(32);
const iv = randomBytes(16);
const tag = randomBytes(12);
createDecipheriv('aes-256-gcm', key, iv).setAuthTag(tag);
