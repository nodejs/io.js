// Flags: --expose-internals
'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const { hasMultiLocalhost } = require('../common/net');
if (!hasMultiLocalhost()) {
  common.skip('platform-specific test.');
}

const http2 = require('http2');
const assert = require('assert');

const server = http2.createServer((req, res) => {
  console.log(`Connect from: ${req.connection.remoteAddress}`);
  assert.strictEqual(req.connection.remoteAddress, '127.0.0.2');

  req.on('end', common.mustCall(() => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end(`You are from: ${req.connection.remoteAddress}`);
  }));
  req.resume();
});

server.listen(0, '127.0.0.1', common.mustCall(() => {
  const options = { localAddress: '127.0.0.2', family: 4 };

  const client = http2.connect(
    'http://localhost:' + server.address().port,
    options
  );
  const req = client.request({
    ':path': '/'
  });
  req.on('data', () => req.resume());
  req.on('end', common.mustCall(function() {
    client.close();
    req.close();
    server.close();
  }));
  req.end();
}));
