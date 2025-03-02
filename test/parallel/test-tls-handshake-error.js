'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');

const fixtures = require('../common/fixtures');

const server = tls.createServer({
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem'),
  rejectUnauthorized: true
}, common.mustNotCall()).listen(0, common.mustCall(function() {
  assert.throws(() => {
    tls.connect({
      port: this.address().port,
      ciphers: 'no-such-cipher'
    }, common.mustNotCall());
  }, /no cipher match/i);

  server.close();
}));
