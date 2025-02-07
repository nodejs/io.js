'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}
const { hasOpenSSL } = require('../common/crypto');

const {
  assert, connect, keys, tls
} = require(fixtures.path('tls-connect'));

// Use ec10 and agent10, they are the only identities with intermediate CAs.
const client = keys.ec10;
const server = keys.agent10;

// The certificates aren't for "localhost", so override the identity check.
function checkServerIdentity(hostname, cert) {
  assert.strictEqual(hostname, 'localhost');
  assert.strictEqual(cert.subject.CN, 'agent10.example.com');
}

// Split out the single end-entity cert and the subordinate CA for later use.
split(client.cert, client);
split(server.cert, server);

function split(file, into) {
  const certs = /([^]*END CERTIFICATE-----\r?\n)(-----BEGIN[^]*)/.exec(file);
  assert.strictEqual(certs.length, 3);
  into.single = certs[1];
  into.subca = certs[2];
}

// Typical setup, nothing special, complete cert chains sent to peer.
connect({
  client: {
    key: client.key,
    cert: client.cert,
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// As above, but without requesting client's cert.
connect({
  client: {
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// Request cert from TLS1.2 client that doesn't have one.
connect({
  client: {
    maxVersion: 'TLSv1.2',
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.strictEqual(pair.server.err.code,
                     'ERR_SSL_PEER_DID_NOT_RETURN_A_CERTIFICATE');
  const expectedErr = hasOpenSSL(3, 2) ?
    'ERR_SSL_SSL/TLS_ALERT_HANDSHAKE_FAILURE' : 'ERR_SSL_SSLV3_ALERT_HANDSHAKE_FAILURE';
  assert.strictEqual(pair.client.err.code,
                     expectedErr);
  return cleanup();
});

// Request cert from TLS1.3 client that doesn't have one.
if (tls.DEFAULT_MAX_VERSION === 'TLSv1.3') connect({
  client: {
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.strictEqual(pair.server.err.code,
                     'ERR_SSL_PEER_DID_NOT_RETURN_A_CERTIFICATE');

  // TLS1.3 client completes handshake before server, and its only after the
  // server handshakes, requests certs, gets back a zero-length list of certs,
  // and sends a fatal Alert to the client that the client discovers there has
  // been a fatal error.
  pair.client.conn.once('error', common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_SSL_TLSV13_ALERT_CERTIFICATE_REQUIRED');
    cleanup();
  }));
});

// Typical configuration error, incomplete cert chains sent, we have to know the
// peer's subordinate CAs in order to verify the peer.
connect({
  client: {
    key: client.key,
    cert: client.single,
    ca: [server.ca, server.subca],
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.single,
    ca: [client.ca, client.subca],
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// Like above, but provide root CA and subordinate CA as multi-PEM.
connect({
  client: {
    key: client.key,
    cert: client.single,
    ca: server.ca + '\n' + server.subca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.single,
    ca: client.ca + '\n' + client.subca,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// Like above, but provide multi-PEM in an array.
connect({
  client: {
    key: client.key,
    cert: client.single,
    ca: [server.ca + '\n' + server.subca],
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.single,
    ca: [client.ca + '\n' + client.subca],
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// Fail to complete server's chain
connect({
  client: {
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.single,
  },
}, function(err, pair, cleanup) {
  assert.strictEqual(err.code, 'UNABLE_TO_VERIFY_LEAF_SIGNATURE');
  return cleanup();
});

// Fail to complete client's chain.
connect({
  client: {
    key: client.key,
    cert: client.single,
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(pair.client.error);
  assert.ifError(pair.server.error);
  assert.strictEqual(err.code, 'ECONNRESET');
  return cleanup();
});

// Fail to find CA for server.
connect({
  client: {
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
  },
}, function(err, pair, cleanup) {
  assert.strictEqual(err.code, 'UNABLE_TO_GET_ISSUER_CERT_LOCALLY');
  return cleanup();
});

// Server sent their CA, but CA cannot be trusted if it is not locally known.
connect({
  client: {
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert + '\n' + server.ca,
  },
}, function(err, pair, cleanup) {
  assert.strictEqual(err.code, 'SELF_SIGNED_CERT_IN_CHAIN');
  return cleanup();
});

// Server sent their CA, wrongly, but its OK since we know the CA locally.
connect({
  client: {
    checkServerIdentity,
    ca: server.ca,
  },
  server: {
    key: server.key,
    cert: server.cert + '\n' + server.ca,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// Fail to complete client's chain.
connect({
  client: {
    key: client.key,
    cert: client.single,
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.strictEqual(err.code, 'ECONNRESET');
  return cleanup();
});

// Fail to find CA for client.
connect({
  client: {
    key: client.key,
    cert: client.cert,
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.strictEqual(err.code, 'ECONNRESET');
  return cleanup();
});

// Confirm support for "BEGIN TRUSTED CERTIFICATE".
connect({
  client: {
    key: client.key,
    cert: client.cert,
    ca: server.ca.replace(/CERTIFICATE/g, 'TRUSTED CERTIFICATE'),
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// Confirm support for "BEGIN TRUSTED CERTIFICATE".
connect({
  client: {
    key: client.key,
    cert: client.cert,
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca.replace(/CERTIFICATE/g, 'TRUSTED CERTIFICATE'),
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// Confirm support for "BEGIN X509 CERTIFICATE".
connect({
  client: {
    key: client.key,
    cert: client.cert,
    ca: server.ca.replace(/CERTIFICATE/g, 'X509 CERTIFICATE'),
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca,
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});

// Confirm support for "BEGIN X509 CERTIFICATE".
connect({
  client: {
    key: client.key,
    cert: client.cert,
    ca: server.ca,
    checkServerIdentity,
  },
  server: {
    key: server.key,
    cert: server.cert,
    ca: client.ca.replace(/CERTIFICATE/g, 'X509 CERTIFICATE'),
    requestCert: true,
  },
}, function(err, pair, cleanup) {
  assert.ifError(err);
  return cleanup();
});
