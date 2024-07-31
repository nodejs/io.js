'use strict';
const common = require('../common');
const { addresses } = require('../common/internet');
const assert = require('assert');
const http = require('http');
const net = require('net');
const dc = require('diagnostics_channel');

const isHTTPServer = (server) => server instanceof http.Server;
const isIncomingMessage = (object) => object instanceof http.IncomingMessage;
const isOutgoingMessage = (object) => object instanceof http.OutgoingMessage;
const isNetSocket = (socket) => socket instanceof net.Socket;
const isError = (error) => error instanceof Error;

dc.subscribe('http.client.request.start', common.mustCall(({ request }) => {
  assert.strictEqual(isOutgoingMessage(request), true);
}, 2));

dc.subscribe('http.client.request.error', common.mustCall(({ request, error }) => {
  assert.strictEqual(isOutgoingMessage(request), true);
  assert.strictEqual(isError(error), true);
}));

dc.subscribe('http.client.response.finish', common.mustCall(({
  request,
  response
}) => {
  assert.strictEqual(isOutgoingMessage(request), true);
  assert.strictEqual(isIncomingMessage(response), true);
}));

dc.subscribe('http.server.request.start', common.mustCall(({
  request,
  response,
  socket,
  server,
}) => {
  assert.strictEqual(isIncomingMessage(request), true);
  assert.strictEqual(isOutgoingMessage(response), true);
  assert.strictEqual(isNetSocket(socket), true);
  assert.strictEqual(isHTTPServer(server), true);
}));

dc.subscribe('http.server.response.finish', common.mustCall(({
  request,
  response,
  socket,
  server,
}) => {
  assert.strictEqual(isIncomingMessage(request), true);
  assert.strictEqual(isOutgoingMessage(response), true);
  assert.strictEqual(isNetSocket(socket), true);
  assert.strictEqual(isHTTPServer(server), true);
}));

const server = http.createServer(common.mustCall((req, res) => {
  res.end('done');
}));

server.listen(async () => {
  const { port } = server.address();
  const invalidRequest = http.get({
    host: addresses.INVALID_HOST,
  });
  await new Promise((resolve) => {
    invalidRequest.on('error', resolve);
  });
  http.get(`http://localhost:${port}`, (res) => {
    res.resume();
    res.on('end', () => {
      server.close();
    });
  });
});
