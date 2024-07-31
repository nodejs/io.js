'use strict';
const common = require('../common');
const assert = require('assert');

if (!common.hasCrypto) {
  common.skip('missing crypto');
  return;
}
const crypto = require('crypto');

// 'ClassName' : ['args', 'for', 'constructor']
const TEST_CASES = {
  'Hash': ['sha1'],
  'Hmac': ['sha1', 'Node'],
  'Cipheriv': ['des-ede3-cbc', '0123456789abcd0123456789', '12345678'],
  'Decipheriv': ['des-ede3-cbc', '0123456789abcd0123456789', '12345678'],
  'Sign': ['RSA-SHA1'],
  'Verify': ['RSA-SHA1'],
  'DiffieHellman': [1024],
  'DiffieHellmanGroup': ['modp5'],
  'ECDH': ['prime256v1'],
};

if (!common.hasFipsCrypto) {
  TEST_CASES.DiffieHellman = [common.hasOpenSSL3 ? 1024 : 256];
}

for (const [clazz, args] of Object.entries(TEST_CASES)) {
  assert(crypto[`create${clazz}`](...args) instanceof crypto[clazz]);
}
