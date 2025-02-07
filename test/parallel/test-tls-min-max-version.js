'use strict';
const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}
const {
  hasOpenSSL,
  hasOpenSSL3,
} = require('../common/crypto');
const fixtures = require('../common/fixtures');
const { inspect } = require('util');

// Check min/max protocol versions.

const {
  assert, connect, keys, tls
} = require(fixtures.path('tls-connect'));
const DEFAULT_MIN_VERSION = tls.DEFAULT_MIN_VERSION;
const DEFAULT_MAX_VERSION = tls.DEFAULT_MAX_VERSION;


function test(cmin, cmax, cprot, smin, smax, sprot, proto, cerr, serr) {
  assert(proto || cerr || serr, 'test missing any expectations');

  let ciphers;
  if (hasOpenSSL3 && (proto === 'TLSv1' || proto === 'TLSv1.1' ||
      proto === 'TLSv1_1_method' || proto === 'TLSv1_method' ||
      sprot === 'TLSv1_1_method' || sprot === 'TLSv1_method')) {
    if (serr !== 'ERR_SSL_UNSUPPORTED_PROTOCOL')
      ciphers = 'ALL@SECLEVEL=0';
  }
  if (hasOpenSSL(3, 1) && cerr === 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION') {
    ciphers = 'DEFAULT@SECLEVEL=0';
  }
  // Report where test was called from. Strip leading garbage from
  //     at Object.<anonymous> (file:line)
  // from the stack location, we only want the file:line part.
  const where = inspect(new Error()).split('\n')[2].replace(/[^(]*/, '');
  connect({
    client: {
      checkServerIdentity: (servername, cert) => { },
      ca: `${keys.agent1.cert}\n${keys.agent6.ca}`,
      minVersion: cmin,
      maxVersion: cmax,
      secureProtocol: cprot,
      ciphers: ciphers
    },
    server: {
      cert: keys.agent6.cert,
      key: keys.agent6.key,
      minVersion: smin,
      maxVersion: smax,
      secureProtocol: sprot,
      ciphers: ciphers
    },
  }, common.mustCall((err, pair, cleanup) => {
    function u(_) { return _ === undefined ? 'U' : _; }
    console.log('test:', u(cmin), u(cmax), u(cprot), u(smin), u(smax), u(sprot),
                u(ciphers), 'expect', u(proto), u(cerr), u(serr));
    console.log('  ', where);
    if (!proto) {
      console.log('client', pair.client.err ? pair.client.err.code : undefined);
      console.log('server', pair.server.err ? pair.server.err.code : undefined);
      if (cerr) {
        assert(pair.client.err);
        // Accept these codes as aliases, the one reported depends on the
        // OpenSSL version.
        if (cerr === 'ERR_SSL_UNSUPPORTED_PROTOCOL' &&
            pair.client.err.code === 'ERR_SSL_VERSION_TOO_LOW')
          cerr = 'ERR_SSL_VERSION_TOO_LOW';
        assert.strictEqual(pair.client.err.code, cerr);
      }
      if (serr) {
        assert(pair.server.err);
        assert.strictEqual(pair.server.err.code, serr);
      }
      return cleanup();
    }

    assert.ifError(err);
    assert.ifError(pair.server.err);
    assert.ifError(pair.client.err);
    assert(pair.server.conn);
    assert(pair.client.conn);
    assert.strictEqual(pair.client.conn.getProtocol(), proto);
    assert.strictEqual(pair.server.conn.getProtocol(), proto);
    return cleanup();
  }));
}

const U = undefined;

// Default protocol is the max version.
test(U, U, U, U, U, U, DEFAULT_MAX_VERSION);

// Insecure or invalid protocols cannot be enabled.
test(U, U, U, U, U, 'SSLv2_method',
     U, U, 'ERR_TLS_INVALID_PROTOCOL_METHOD');
test(U, U, U, U, U, 'SSLv3_method',
     U, U, 'ERR_TLS_INVALID_PROTOCOL_METHOD');
test(U, U, 'SSLv2_method', U, U, U,
     U, 'ERR_TLS_INVALID_PROTOCOL_METHOD');
test(U, U, 'SSLv3_method', U, U, U,
     U, 'ERR_TLS_INVALID_PROTOCOL_METHOD');
test(U, U, 'hokey-pokey', U, U, U,
     U, 'ERR_TLS_INVALID_PROTOCOL_METHOD');
test(U, U, U, U, U, 'hokey-pokey',
     U, U, 'ERR_TLS_INVALID_PROTOCOL_METHOD');

// Regression test: this should not crash because node should not pass the error
// message (including unsanitized user input) to a printf-like function.
test(U, U, U, U, U, '%s_method',
     U, U, 'ERR_TLS_INVALID_PROTOCOL_METHOD');

// Cannot use secureProtocol and min/max versions simultaneously.
test(U, U, U, U, 'TLSv1.2', 'TLS1_2_method',
     U, U, 'ERR_TLS_PROTOCOL_VERSION_CONFLICT');
test(U, U, U, 'TLSv1.2', U, 'TLS1_2_method',
     U, U, 'ERR_TLS_PROTOCOL_VERSION_CONFLICT');
test(U, 'TLSv1.2', 'TLS1_2_method', U, U, U,
     U, 'ERR_TLS_PROTOCOL_VERSION_CONFLICT');
test('TLSv1.2', U, 'TLS1_2_method', U, U, U,
     U, 'ERR_TLS_PROTOCOL_VERSION_CONFLICT');

// TLS_method means "any supported protocol".
test(U, U, 'TLSv1_2_method', U, U, 'TLS_method', 'TLSv1.2');
test(U, U, 'TLSv1_1_method', U, U, 'TLS_method', 'TLSv1.1');
test(U, U, 'TLSv1_method', U, U, 'TLS_method', 'TLSv1');
test(U, U, 'TLS_method', U, U, 'TLSv1_2_method', 'TLSv1.2');
test(U, U, 'TLS_method', U, U, 'TLSv1_1_method', 'TLSv1.1');
test(U, U, 'TLS_method', U, U, 'TLSv1_method', 'TLSv1');

// OpenSSL 1.1.1 and 3.0 use a different error code and alert (sent to the
// client) when no protocols are enabled on the server.
const NO_PROTOCOLS_AVAILABLE_SERVER = hasOpenSSL3 ?
  'ERR_SSL_NO_PROTOCOLS_AVAILABLE' : 'ERR_SSL_INTERNAL_ERROR';
const NO_PROTOCOLS_AVAILABLE_SERVER_ALERT = hasOpenSSL3 ?
  'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION' : 'ERR_SSL_TLSV1_ALERT_INTERNAL_ERROR';

// SSLv23 also means "any supported protocol" greater than the default
// minimum (which is configurable via command line).
if (DEFAULT_MIN_VERSION === 'TLSv1.3') {
  test(U, U, 'TLSv1_2_method', U, U, 'SSLv23_method',
       U, NO_PROTOCOLS_AVAILABLE_SERVER_ALERT, NO_PROTOCOLS_AVAILABLE_SERVER);
} else {
  test(U, U, 'TLSv1_2_method', U, U, 'SSLv23_method', 'TLSv1.2');
}

if (DEFAULT_MIN_VERSION === 'TLSv1.3') {
  test(U, U, 'TLSv1_1_method', U, U, 'SSLv23_method',
       U, NO_PROTOCOLS_AVAILABLE_SERVER_ALERT, NO_PROTOCOLS_AVAILABLE_SERVER);
  test(U, U, 'TLSv1_method', U, U, 'SSLv23_method',
       U, NO_PROTOCOLS_AVAILABLE_SERVER_ALERT, NO_PROTOCOLS_AVAILABLE_SERVER);
  test(U, U, 'SSLv23_method', U, U, 'TLSv1_1_method',
       U, 'ERR_SSL_NO_PROTOCOLS_AVAILABLE', 'ERR_SSL_UNEXPECTED_MESSAGE');
  test(U, U, 'SSLv23_method', U, U, 'TLSv1_method',
       U, 'ERR_SSL_NO_PROTOCOLS_AVAILABLE', 'ERR_SSL_UNEXPECTED_MESSAGE');
}

if (DEFAULT_MIN_VERSION === 'TLSv1.2') {
  test(U, U, 'TLSv1_1_method', U, U, 'SSLv23_method',
       U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
       'ERR_SSL_UNSUPPORTED_PROTOCOL');
  test(U, U, 'TLSv1_method', U, U, 'SSLv23_method',
       U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
       'ERR_SSL_UNSUPPORTED_PROTOCOL');
  test(U, U, 'SSLv23_method', U, U, 'TLSv1_1_method',
       U, 'ERR_SSL_UNSUPPORTED_PROTOCOL', 'ERR_SSL_WRONG_VERSION_NUMBER');
  test(U, U, 'SSLv23_method', U, U, 'TLSv1_method',
       U, 'ERR_SSL_UNSUPPORTED_PROTOCOL', 'ERR_SSL_WRONG_VERSION_NUMBER');
}

if (DEFAULT_MIN_VERSION === 'TLSv1.1') {
  test(U, U, 'TLSv1_1_method', U, U, 'SSLv23_method', 'TLSv1.1');
  test(U, U, 'TLSv1_method', U, U, 'SSLv23_method',
       U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
       'ERR_SSL_UNSUPPORTED_PROTOCOL');
  test(U, U, 'SSLv23_method', U, U, 'TLSv1_1_method', 'TLSv1.1');
  test(U, U, 'SSLv23_method', U, U, 'TLSv1_method',
       U, 'ERR_SSL_UNSUPPORTED_PROTOCOL', 'ERR_SSL_WRONG_VERSION_NUMBER');
}

if (DEFAULT_MIN_VERSION === 'TLSv1') {
  test(U, U, 'TLSv1_1_method', U, U, 'SSLv23_method', 'TLSv1.1');
  test(U, U, 'TLSv1_method', U, U, 'SSLv23_method', 'TLSv1');
  test(U, U, 'SSLv23_method', U, U, 'TLSv1_1_method', 'TLSv1.1');
  test(U, U, 'SSLv23_method', U, U, 'TLSv1_method', 'TLSv1');
}

// TLSv1 thru TLSv1.2 are only supported with explicit configuration with API or
// CLI (--tls-v1.0 and --tls-v1.1).
test(U, U, 'TLSv1_2_method', U, U, 'TLSv1_2_method', 'TLSv1.2');
test(U, U, 'TLSv1_1_method', U, U, 'TLSv1_1_method', 'TLSv1.1');
test(U, U, 'TLSv1_method', U, U, 'TLSv1_method', 'TLSv1');

// The default default.
if (DEFAULT_MIN_VERSION === 'TLSv1.2') {
  test(U, U, 'TLSv1_1_method', U, U, U,
       U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
       'ERR_SSL_UNSUPPORTED_PROTOCOL');
  test(U, U, 'TLSv1_method', U, U, U,
       U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
       'ERR_SSL_UNSUPPORTED_PROTOCOL');

  if (DEFAULT_MAX_VERSION === 'TLSv1.2') {
    test(U, U, U, U, U, 'TLSv1_1_method',
         U, 'ERR_SSL_UNSUPPORTED_PROTOCOL', 'ERR_SSL_WRONG_VERSION_NUMBER');
    test(U, U, U, U, U, 'TLSv1_method',
         U, 'ERR_SSL_UNSUPPORTED_PROTOCOL', 'ERR_SSL_WRONG_VERSION_NUMBER');
  } else {
    // TLS1.3 client hellos are are not understood by TLS1.1 or below.
    test(U, U, U, U, U, 'TLSv1_1_method',
         U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
         'ERR_SSL_UNSUPPORTED_PROTOCOL');
    test(U, U, U, U, U, 'TLSv1_method',
         U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
         'ERR_SSL_UNSUPPORTED_PROTOCOL');
  }
}

// The default with --tls-v1.1.
if (DEFAULT_MIN_VERSION === 'TLSv1.1') {
  test(U, U, 'TLSv1_1_method', U, U, U, 'TLSv1.1');
  test(U, U, 'TLSv1_method', U, U, U,
       U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
       'ERR_SSL_UNSUPPORTED_PROTOCOL');
  test(U, U, U, U, U, 'TLSv1_1_method', 'TLSv1.1');

  if (DEFAULT_MAX_VERSION === 'TLSv1.2') {
    test(U, U, U, U, U, 'TLSv1_method',
         U, 'ERR_SSL_UNSUPPORTED_PROTOCOL', 'ERR_SSL_WRONG_VERSION_NUMBER');
  } else {
    // TLS1.3 client hellos are are not understood by TLS1.1 or below.
    test(U, U, U, U, U, 'TLSv1_method',
         U, 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION',
         'ERR_SSL_UNSUPPORTED_PROTOCOL');
  }
}

// The default with --tls-v1.0.
if (DEFAULT_MIN_VERSION === 'TLSv1') {
  test(U, U, 'TLSv1_1_method', U, U, U, 'TLSv1.1');
  test(U, U, 'TLSv1_method', U, U, U, 'TLSv1');
  test(U, U, U, U, U, 'TLSv1_1_method', 'TLSv1.1');
  test(U, U, U, U, U, 'TLSv1_method', 'TLSv1');
}

// TLS min/max are respected when set with no secureProtocol.
test('TLSv1', 'TLSv1.2', U, U, U, 'TLSv1_method', 'TLSv1');
test('TLSv1', 'TLSv1.2', U, U, U, 'TLSv1_1_method', 'TLSv1.1');
test('TLSv1', 'TLSv1.2', U, U, U, 'TLSv1_2_method', 'TLSv1.2');
test('TLSv1', 'TLSv1.2', U, U, U, 'TLS_method', 'TLSv1.2');

test(U, U, 'TLSv1_method', 'TLSv1', 'TLSv1.2', U, 'TLSv1');
test(U, U, 'TLSv1_1_method', 'TLSv1', 'TLSv1.2', U, 'TLSv1.1');
test(U, U, 'TLSv1_2_method', 'TLSv1', 'TLSv1.2', U, 'TLSv1.2');

test('TLSv1', 'TLSv1.1', U, 'TLSv1', 'TLSv1.3', U, 'TLSv1.1');
test('TLSv1', 'TLSv1.1', U, 'TLSv1', 'TLSv1.2', U, 'TLSv1.1');
test('TLSv1', 'TLSv1.2', U, 'TLSv1', 'TLSv1.1', U, 'TLSv1.1');
test('TLSv1', 'TLSv1.3', U, 'TLSv1', 'TLSv1.1', U, 'TLSv1.1');
test('TLSv1', 'TLSv1', U, 'TLSv1', 'TLSv1.1', U, 'TLSv1');
test('TLSv1', 'TLSv1.2', U, 'TLSv1', 'TLSv1', U, 'TLSv1');
test('TLSv1', 'TLSv1.3', U, 'TLSv1', 'TLSv1', U, 'TLSv1');
test('TLSv1.1', 'TLSv1.1', U, 'TLSv1', 'TLSv1.2', U, 'TLSv1.1');
test('TLSv1', 'TLSv1.2', U, 'TLSv1.1', 'TLSv1.1', U, 'TLSv1.1');
test('TLSv1', 'TLSv1.2', U, 'TLSv1', 'TLSv1.3', U, 'TLSv1.2');

// v-any client can connect to v-specific server
test('TLSv1', 'TLSv1.3', U, 'TLSv1.3', 'TLSv1.3', U, 'TLSv1.3');
test('TLSv1', 'TLSv1.3', U, 'TLSv1.2', 'TLSv1.3', U, 'TLSv1.3');
test('TLSv1', 'TLSv1.3', U, 'TLSv1.2', 'TLSv1.2', U, 'TLSv1.2');
test('TLSv1', 'TLSv1.3', U, 'TLSv1.1', 'TLSv1.1', U, 'TLSv1.1');
test('TLSv1', 'TLSv1.3', U, 'TLSv1', 'TLSv1', U, 'TLSv1');

// v-specific client can connect to v-any server
test('TLSv1.3', 'TLSv1.3', U, 'TLSv1', 'TLSv1.3', U, 'TLSv1.3');
test('TLSv1.2', 'TLSv1.2', U, 'TLSv1', 'TLSv1.3', U, 'TLSv1.2');
test('TLSv1.1', 'TLSv1.1', U, 'TLSv1', 'TLSv1.3', U, 'TLSv1.1');
test('TLSv1', 'TLSv1', U, 'TLSv1', 'TLSv1.3', U, 'TLSv1');
