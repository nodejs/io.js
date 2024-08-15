'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const http2 = require('http2');
const stream = require('stream');
const { duplexPair } = require('stream');

// Basic test
{
  const [ clientSide, serverSide ] = duplexPair();

  const client = http2.connect('http://example.com', {
    createConnection: () => clientSide,
  });

  const session = http2.performServerHandshake(serverSide);

  session.on('stream', common.mustCall((stream, headers) => {
    assert.strictEqual(headers[':path'], '/test');
    stream.respond({
      ':status': 200,
    });
    stream.end('hi!');
  }));

  const req = client.request({ ':path': '/test' });
  req.on('response', common.mustCall());
  req.end();
}

// Double bind should fail
{
  const socket = new stream.Duplex({
    read() {},
    write() {},
  });

  http2.performServerHandshake(socket);

  assert.throws(() => {
    http2.performServerHandshake(socket);
  }, { code: 'ERR_HTTP2_SOCKET_BOUND' });
}
