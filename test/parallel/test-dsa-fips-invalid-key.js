'use strict';
const common = require('../common');

if (!common.hasCrypto) {
  common.skip('no crypto');
}

const fixtures = require('../common/fixtures');
const crypto = require('crypto');

if (!crypto.getFips()) {
  common.skip('node compiled without FIPS OpenSSL.');
}

const assert = require('assert');

const input = 'hello';

const dsapri = fixtures.readKey('dsa_private_1025.pem');
const sign = crypto.createSign('SHA1');
sign.update(input);

assert.throws(function() {
  sign.sign(dsapri);
}, /PEM_read_bio_PrivateKey failed/);
