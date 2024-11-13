'use strict';
const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

if (!common.opensslCli)
  common.skip('node compiled without OpenSSL CLI');

const assert = require('assert');
const net = require('net');
const tls = require('tls');
const fixtures = require('../common/fixtures');

let clientClosed = false;
let errorReceived = false;
function canCloseServer() {
  return clientClosed && errorReceived;
}

function loadPEM(n) {
  return fixtures.readKey(`${n}.pem`, 'utf-8');
}

const opts = {
  key: loadPEM('agent2-key'),
  cert: loadPEM('agent2-cert')
};

const max_iter = 20;
let iter = 0;

const errorHandler = common.mustCall((err) => {
  let expectedErrorCode = 'ERR_SSL_WRONG_VERSION_NUMBER';
  let expectedErrorReason = 'wrong version number';
  if (common.hasOpenSSL(3, 2)) {
    expectedErrorCode = 'ERR_SSL_PACKET_LENGTH_TOO_LONG';
    expectedErrorReason = 'packet length too long';
  };

  assert.strictEqual(err.code, expectedErrorCode);
  assert.strictEqual(err.library, 'SSL routines');
  if (!common.hasOpenSSL3) assert.strictEqual(err.function, 'ssl3_get_record');
  assert.strictEqual(err.reason, expectedErrorReason);
  errorReceived = true;
  if (canCloseServer())
    server.close();
});
const server = tls.createServer(opts, common.mustCall(function(s) {
  s.pipe(s);
  s.on('error', errorHandler);
}, 2));

server.listen(0, common.mustCall(function() {
  sendClient();
}));

server.on('tlsClientError', common.mustNotCall());

server.on('error', common.mustNotCall());

function sendClient() {
  const client = tls.connect(server.address().port, {
    rejectUnauthorized: false
  });
  client.on('data', common.mustCall(function() {
    if (iter++ === 2) sendBADTLSRecord();
    if (iter < max_iter) {
      client.write('a');
      return;
    }
    client.end();
  }, max_iter));
  client.write('a', common.mustCall());
  client.on('error', common.mustNotCall());
  client.on('close', common.mustCall(function() {
    clientClosed = true;
    if (canCloseServer())
      server.close();
  }));
}


function sendBADTLSRecord() {
  const BAD_RECORD = Buffer.from([0xff, 0xff, 0xff, 0xff, 0xff, 0xff]);
  const socket = net.connect(server.address().port);
  const client = tls.connect({
    socket: socket,
    rejectUnauthorized: false
  }, common.mustCall(function() {
    client.write('x');
    client.on('data', (data) => {
      socket.end(BAD_RECORD);
    });
  }));
  client.on('error', common.mustCall((err) => {
    let expectedErrorCode = 'ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION';
    let expectedErrorReason = 'tlsv1 alert protocol version';
    if (common.hasOpenSSL(3, 2)) {
      expectedErrorCode = 'ERR_SSL_TLSV1_ALERT_RECORD_OVERFLOW';
      expectedErrorReason = 'tlsv1 alert record overflow';
    }
    assert.strictEqual(err.code, expectedErrorCode);
    assert.strictEqual(err.library, 'SSL routines');
    if (!common.hasOpenSSL3)
      assert.strictEqual(err.function, 'ssl3_read_bytes');
    assert.strictEqual(err.reason, expectedErrorReason);
  }));
}
