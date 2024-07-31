'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const h2 = require('http2');

const server = h2.createServer();

server.listen(0, common.mustCall(function() {
  const proxyClient = h2.connect(`http://localhost:${server.address().port}`);

  const request = proxyClient.request({
    ':method': 'CONNECT',
    ':authority': 'example.com:80'
  });

  request.on('response', common.mustCall((connectResponse) => {
    assert.strictEqual(connectResponse[':status'], 200);

    const proxiedClient = h2.connect('http://example.com', {
      createConnection: () => request // Tunnel via first request stream
    });

    const proxiedRequest = proxiedClient.request();
    proxiedRequest.on('response', common.mustCall((proxiedResponse) => {
      assert.strictEqual(proxiedResponse[':status'], 204);

      proxiedClient.close();
      proxyClient.close();
      server.close();
    }));
  }));
}));

server.once('connect', common.mustCall((req, res) => {
  assert.strictEqual(req.headers[':method'], 'CONNECT');
  res.writeHead(200); // Accept the CONNECT tunnel

  // Handle this stream as a new 'proxied' connection (pretend to forward
  // but actually just unwrap the tunnel ourselves):
  server.emit('connection', res.stream);
}));

// Handle the 'proxied' request itself:
server.once('request', common.mustCall((req, res) => {
  res.writeHead(204);
  res.end();
}));
