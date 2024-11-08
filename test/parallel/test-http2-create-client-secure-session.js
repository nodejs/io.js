// Flags: --expose-internals

'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const fixtures = require('../common/fixtures');
const h2 = require('http2');
const { kSocket } = require('internal/http2/util');
const tls = require('tls');

function loadKey(keyname) {
  return fixtures.readKey(keyname, 'binary');
}

function onStream(stream, headers) {
  const socket = stream.session[kSocket];

  assert(stream.session.encrypted);
  assert.strictEqual(stream.session.alpnProtocol, 'h2');
  const originSet = stream.session.originSet;
  assert(Array.isArray(originSet));
  assert.strictEqual(originSet[0],
                     `https://${socket.servername}:${socket.remotePort}`);

  assert(headers[':authority'].startsWith(socket.servername));
  stream.respond({ 'content-type': 'application/json' });
  stream.end(JSON.stringify({
    servername: socket.servername,
    alpnProtocol: socket.alpnProtocol
  }));
}

function verifySecureSession(key, cert, ca, opts) {
  const server = h2.createSecureServer({ cert, key });
  server.on('stream', common.mustCall(onStream));
  server.on('close', common.mustCall());
  server.listen(0, common.mustCall(() => {
    opts ||= {};
    opts.secureContext = tls.createSecureContext({ ca });
    const client = h2.connect(`https://localhost:${server.address().port}`,
                              opts);
    // Verify that a 'secureConnect' listener is attached
    assert.strictEqual(client.socket.listenerCount('secureConnect'), 1);
    const req = client.request();

    client.on('connect', common.mustCall(() => {
      assert(client.encrypted);
      assert.strictEqual(client.alpnProtocol, 'h2');
      const originSet = client.originSet;
      assert(Array.isArray(originSet));
      assert.strictEqual(originSet.length, 1);
      assert.strictEqual(
        originSet[0],
        `https://${opts.servername || 'localhost'}:${server.address().port}`);
    }));

    req.on('response', common.mustCall((headers) => {
      assert.strictEqual(headers[':status'], 200);
      assert.strictEqual(headers['content-type'], 'application/json');
      assert(headers.date);
    }));

    let data = '';
    req.setEncoding('utf8');
    req.on('data', (d) => data += d);
    req.on('end', common.mustCall(() => {
      const jsonData = JSON.parse(data);
      assert.strictEqual(jsonData.servername,
                         opts.servername || 'localhost');
      assert.strictEqual(jsonData.alpnProtocol, 'h2');
      server.close(common.mustSucceed());
      client[kSocket].destroy();
    }));
  }));
}

// The server can be connected as 'localhost'.
verifySecureSession(
  loadKey('agent8-key.pem'),
  loadKey('agent8-cert.pem'),
  loadKey('fake-startcom-root-cert.pem'));

// Custom servername is specified.
verifySecureSession(
  loadKey('agent1-key.pem'),
  loadKey('agent1-cert.pem'),
  loadKey('ca1-cert.pem'),
  { servername: 'agent1' });

verifySecureSession(
  loadKey('agent8-key.pem'),
  loadKey('agent8-cert.pem'),
  loadKey('fake-startcom-root-cert.pem'),
  { servername: '' });
