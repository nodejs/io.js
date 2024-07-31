'use strict';
const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');
const fixtures = require('../common/fixtures');

{
  const options = {
    key: fixtures.readKey('agent11-key.pem'),
    cert: fixtures.readKey('agent11-cert.pem'),
    ciphers: 'DEFAULT'
  };

  // Should throw error as key is too small because openssl v3 doesn't allow it
  assert.throws(() => tls.createServer(options, common.mustNotCall()),
                /key too small/i);

  // Reducing SECLEVEL to 0 in ciphers retains compatibility with previous versions of OpenSSL like using a small key.
  // As ciphers are getting set before the cert and key get loaded.
  options.ciphers = 'DEFAULT:@SECLEVEL=0';
  assert.ok(tls.createServer(options, common.mustNotCall()));
}
