'use strict';
const common = require('../common');

// Test for https://github.com/nodejs/node/issues/40814

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { hasOpenSSL3 } = require('../common/crypto');

if (!hasOpenSSL3) {
  common.skip('only openssl3'); // https://github.com/nodejs/node/pull/42793#issuecomment-1107491901
}

const assert = require('assert');
const crypto = require('crypto');

const { privateKey, publicKey } = crypto.generateKeyPairSync('rsa', {
  modulusLength: 2048,
  publicKeyEncoding: {
    type: 'spki',
    format: 'pem'
  },
  privateKeyEncoding: {
    type: 'pkcs8',
    format: 'pem',
    cipher: 'aes-128-ecb',
    passphrase: 'abcdef'
  }
});
assert.notStrictEqual(privateKey.toString(), '');

const msg = 'The quick brown fox jumps over the lazy dog';

const encryptedString = crypto.privateEncrypt({
  key: privateKey,
  passphrase: 'abcdef'
}, Buffer.from(msg)).toString('base64');
const decryptedString = crypto.publicDecrypt(publicKey, Buffer.from(encryptedString, 'base64')).toString();
console.log(`Encrypted: ${encryptedString}`);
console.log(`Decrypted: ${decryptedString}`);

assert.notStrictEqual(encryptedString, '');
assert.strictEqual(decryptedString, msg);
