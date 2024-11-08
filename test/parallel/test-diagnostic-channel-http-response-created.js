'use strict';
const common = require('../common');
const assert = require('assert');
const http = require('http');
const dc = require('diagnostics_channel');

const isOutgoingMessage = (object) => object instanceof http.OutgoingMessage;
const isIncomingMessage = (object) => object instanceof http.IncomingMessage;

dc.subscribe('http.server.response.created', common.mustCall(({
  request,
  response,
}) => {
  assert.strictEqual(request.headers.foo, 'bar');
  assert.strictEqual(response.getHeader('baz'), undefined);
  assert.strictEqual(isIncomingMessage(request), true);
  assert.strictEqual(isOutgoingMessage(response), true);
}));

dc.subscribe('http.server.response.finish', common.mustCall(({
  request,
  response,
}) => {
  assert.strictEqual(request.headers.foo, 'bar');
  assert.strictEqual(response.getHeader('baz'), 'bar');
  assert.strictEqual(isIncomingMessage(request), true);
  assert.strictEqual(isOutgoingMessage(response), true);
}));

const server = http.createServer(common.mustCall((_, res) => {
  res.setHeader('baz', 'bar');
  res.end('done');
}));

server.listen(() => {
  const { port } = server.address();
  http.get({
    port,
    headers: {
      'foo': 'bar',
    }
  }, common.mustCall(() => {
    server.close();
  }));
});
