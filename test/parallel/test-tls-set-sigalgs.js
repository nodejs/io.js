'use strict';
const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}
const { hasOpenSSL } = require('../common/crypto');
const fixtures = require('../common/fixtures');

// Test sigalgs: option for TLS.

const {
  assert, connect, keys
} = require(fixtures.path('tls-connect'));

function test(csigalgs, ssigalgs, shared_sigalgs, cerr, serr) {
  assert(shared_sigalgs || serr || cerr, 'test missing any expectations');
  connect({
    client: {
      checkServerIdentity: (servername, cert) => { },
      ca: `${keys.agent1.cert}\n${keys.agent6.ca}`,
      cert: keys.agent2.cert,
      key: keys.agent2.key,
      sigalgs: csigalgs
    },
    server: {
      cert: keys.agent6.cert,
      key: keys.agent6.key,
      ca: keys.agent2.ca,
      context: {
        requestCert: true,
        rejectUnauthorized: true
      },
      sigalgs: ssigalgs
    },
  }, common.mustCall((err, pair, cleanup) => {
    if (shared_sigalgs) {
      assert.ifError(err);
      assert.ifError(pair.server.err);
      assert.ifError(pair.client.err);
      assert(pair.server.conn);
      assert(pair.client.conn);
      assert.deepStrictEqual(
        pair.server.conn.getSharedSigalgs(),
        shared_sigalgs
      );
    } else {
      if (serr) {
        assert(pair.server.err);
        assert.strictEqual(pair.server.err.code, serr);
      }

      if (cerr) {
        assert(pair.client.err);
        assert.strictEqual(pair.client.err.code, cerr);
      }
    }

    return cleanup();
  }));
}

// Have shared sigalgs
test('RSA-PSS+SHA384', 'RSA-PSS+SHA384', ['RSA-PSS+SHA384']);
test('RSA-PSS+SHA256:RSA-PSS+SHA512:ECDSA+SHA256',
     'RSA-PSS+SHA256:ECDSA+SHA256',
     ['RSA-PSS+SHA256', 'ECDSA+SHA256']);

// Do not have shared sigalgs.
const handshakeErr = hasOpenSSL(3, 2) ?
  'ERR_SSL_SSL/TLS_ALERT_HANDSHAKE_FAILURE' : 'ERR_SSL_SSLV3_ALERT_HANDSHAKE_FAILURE';
test('RSA-PSS+SHA384', 'ECDSA+SHA256',
     undefined, handshakeErr,
     'ERR_SSL_NO_SHARED_SIGNATURE_ALGORITHMS');

test('RSA-PSS+SHA384:ECDSA+SHA256', 'ECDSA+SHA384:RSA-PSS+SHA256',
     undefined, handshakeErr,
     'ERR_SSL_NO_SHARED_SIGNATURE_ALGORITHMS');
