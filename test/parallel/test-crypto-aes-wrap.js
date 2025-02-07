'use strict';
const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}

// Tests that the AES wrap and unwrap functions are working correctly.

const assert = require('assert');
const crypto = require('crypto');

const ivShort = Buffer.from('3fd838af', 'hex');
const ivLong = Buffer.from('3fd838af4093d749', 'hex');
const key1 = Buffer.from('b26f309fbe57e9b3bb6ae5ef31d54450', 'hex');
const key2 = Buffer.from('40978085d68091f7dfca0d7dfc7a5ee76d2cc7f2f345a304', 'hex');
const key3 = Buffer.from('29c9eab5ed5ad44134a1437fe2e673b4d88a5b7c72e68454fea08721392b7323', 'hex');

[
  {
    algorithm: 'aes128-wrap',
    key: key1,
    iv: ivLong,
    text: '12345678123456781234567812345678'
  },
  {
    algorithm: 'id-aes128-wrap-pad',
    key: key1,
    iv: ivShort,
    text: '12345678123456781234567812345678123'
  },
  {
    algorithm: 'aes192-wrap',
    key: key2,
    iv: ivLong,
    text: '12345678123456781234567812345678'
  },
  {
    algorithm: 'id-aes192-wrap-pad',
    key: key2,
    iv: ivShort,
    text: '12345678123456781234567812345678123'
  },
  {
    algorithm: 'aes256-wrap',
    key: key3,
    iv: ivLong,
    text: '12345678123456781234567812345678'
  },
  {
    algorithm: 'id-aes256-wrap-pad',
    key: key3,
    iv: ivShort,
    text: '12345678123456781234567812345678123'
  },
].forEach(({ algorithm, key, iv, text }) => {
  const cipher = crypto.createCipheriv(algorithm, key, iv);
  const decipher = crypto.createDecipheriv(algorithm, key, iv);
  const msg = decipher.update(cipher.update(text, 'utf8'), 'buffer', 'utf8');
  assert.strictEqual(msg, text, `${algorithm} test case failed`);
});
