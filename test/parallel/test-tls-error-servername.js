'use strict';

// This tests the errors thrown from TLSSocket.prototype.setServername

const common = require('../common');
const fixtures = require('../common/fixtures');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { connect, TLSSocket } = require('tls');
const { duplexPair } = require('stream');
const [ clientSide, serverSide ] = duplexPair();

const key = fixtures.readKey('agent1-key.pem');
const cert = fixtures.readKey('agent1-cert.pem');
const ca = fixtures.readKey('ca1-cert.pem');

const client = connect({
  socket: clientSide,
  ca,
  host: 'agent1'  // Hostname from certificate
});

[undefined, null, 1, true, {}].forEach((value) => {
  assert.throws(() => {
    client.setServername(value);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "name" argument must be of type string.' +
             common.invalidArgTypeHelper(value)
  });
});

const server = new TLSSocket(serverSide, {
  isServer: true,
  key,
  cert,
  ca
});

assert.throws(() => {
  server.setServername('localhost');
}, {
  code: 'ERR_TLS_SNI_FROM_SERVER',
  message: 'Cannot issue SNI from a TLS server-side socket'
});
