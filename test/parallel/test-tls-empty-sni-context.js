'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const { hasOpenSSL } = require('../common/crypto');
const assert = require('assert');
const tls = require('tls');

const options = {
  SNICallback: (name, callback) => {
    callback(null, tls.createSecureContext());
  }
};

const server = tls.createServer(options, (c) => {
  assert.fail('Should not be called');
}).on('tlsClientError', common.mustCall((err, c) => {
  assert.match(err.message, /no suitable signature algorithm/i);
  server.close();
})).listen(0, common.mustCall(() => {
  const c = tls.connect({
    port: server.address().port,
    rejectUnauthorized: false,
    servername: 'any.name'
  }, common.mustNotCall());

  c.on('error', common.mustCall((err) => {
    const expectedErr = hasOpenSSL(3, 2) ?
      'ERR_SSL_SSL/TLS_ALERT_HANDSHAKE_FAILURE' : 'ERR_SSL_SSLV3_ALERT_HANDSHAKE_FAILURE';
    assert.strictEqual(err.code, expectedErr);
  }));
}));
