'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');

// Test the honorCipherOrder property

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const mustCall = common.mustCall;
const tls = require('tls');
const util = require('util');

// We explicitly set TLS version to 1.2 so as to be safe when the
// default method is updated in the future
const SSL_Method = 'TLSv1_2_method';
const localhost = '127.0.0.1';

function test(honorCipherOrder, clientCipher, expectedCipher, defaultCiphers) {
  const soptions = {
    secureProtocol: SSL_Method,
    key: fixtures.readKey('agent2-key.pem'),
    cert: fixtures.readKey('agent2-cert.pem'),
    ciphers: 'AES256-SHA256:AES128-GCM-SHA256:AES128-SHA256:' +
             'ECDHE-RSA-AES128-GCM-SHA256',
    honorCipherOrder: honorCipherOrder,
  };

  const server = tls.createServer(soptions, mustCall(function(clearTextStream) {
    // End socket to send CLOSE_NOTIFY and TCP FIN packet, otherwise
    // it may hang for ~30 seconds in FIN_WAIT_1 state (at least on macOS).
    clearTextStream.end();
  }));
  server.listen(0, localhost, mustCall(function() {
    const coptions = {
      rejectUnauthorized: false,
      secureProtocol: SSL_Method
    };
    if (clientCipher) {
      coptions.ciphers = clientCipher;
    }
    const port = this.address().port;
    const savedDefaults = tls.DEFAULT_CIPHERS;
    tls.DEFAULT_CIPHERS = defaultCiphers || savedDefaults;
    const client = tls.connect(port, localhost, coptions, mustCall(function() {
      const cipher = client.getCipher();
      client.end();
      server.close();
      const msg = util.format(
        'honorCipherOrder=%j, clientCipher=%j, expect=%j, got=%j',
        honorCipherOrder, clientCipher, expectedCipher, cipher.name);
      assert.strictEqual(cipher.name, expectedCipher, msg);
    }));
    tls.DEFAULT_CIPHERS = savedDefaults;
  }));
}

// Client explicitly has the preference of cipher suites, not the default.
test(false, 'AES128-GCM-SHA256:AES256-SHA256:AES128-SHA256',
     'AES128-GCM-SHA256');

// Server has the preference of cipher suites, and AES256-SHA256 is
// the server's top choice.
test(true, 'AES128-GCM-SHA256:AES256-SHA256:AES128-SHA256',
     'AES256-SHA256');
test(undefined, 'AES128-GCM-SHA256:AES256-SHA256:AES128-SHA256',
     'AES256-SHA256');

// Server has the preference of cipher suites. AES128-GCM-SHA256 is given
// higher priority over AES128-SHA256 among client cipher suites.
test(true, 'AES128-SHA256:AES128-GCM-SHA256', 'AES128-GCM-SHA256');
test(undefined, 'AES128-SHA256:AES128-GCM-SHA256', 'AES128-GCM-SHA256');


// As client has only one cipher, server has no choice, irrespective
// of honorCipherOrder.
test(true, 'AES128-SHA256', 'AES128-SHA256');
test(undefined, 'AES128-SHA256', 'AES128-SHA256');

// Client did not explicitly set ciphers and client offers
// tls.DEFAULT_CIPHERS. All ciphers of the server are included in the
// default list so the negotiated cipher is selected according to the
// server's top preference of AES256-SHA256.
test(true, tls.DEFAULT_CIPHERS, 'AES256-SHA256');
test(true, null, 'AES256-SHA256');
test(undefined, null, 'AES256-SHA256');

// Ensure that `tls.DEFAULT_CIPHERS` is used when its a limited cipher set.
test(true, null, 'ECDHE-RSA-AES128-GCM-SHA256', 'ECDHE-RSA-AES128-GCM-SHA256');
