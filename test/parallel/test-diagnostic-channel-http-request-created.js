'use strict';
const common = require('../common');
const assert = require('assert');
const http = require('http');
const dc = require('diagnostics_channel');

const isHTTPServer = (server) => server instanceof http.Server;
const isOutgoingMessage = (object) => object instanceof http.OutgoingMessage;

dc.subscribe('http.client.request.created', common.mustCall(({ request }) => {
  assert.strictEqual(request.getHeader('foo'), 'bar');
  assert.strictEqual(request.getHeader('baz'), undefined);
  assert.strictEqual(isOutgoingMessage(request), true);
  assert.strictEqual(isHTTPServer(server), true);
}));

dc.subscribe('http.client.request.start', common.mustCall(({ request }) => {
  assert.strictEqual(request.getHeader('foo'), 'bar');
  assert.strictEqual(request.getHeader('baz'), 'bar');
  assert.strictEqual(isOutgoingMessage(request), true);
}));

const server = http.createServer(common.mustCall((_, res) => {
  res.end('done');
}));

server.listen(async () => {
  const { port } = server.address();
  const req = http.request({
    port,
    headers: {
      'foo': 'bar',
    }
  }, common.mustCall(() => {
    server.close();
  }));
  req.setHeader('baz', 'bar');
  req.end();
});
