// Flags: --inspect=0 --experimental-network-inspection
'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('node:assert');
const { once } = require('node:events');
const { addresses } = require('../common/internet');
const fixtures = require('../common/fixtures');
const http = require('node:http');
const https = require('node:https');
const inspector = require('node:inspector/promises');

const session = new inspector.Session();
session.connect();

const requestHeaders = {
  'accept-language': 'en-US',
  'Cookie': ['k1=v1', 'k2=v2'],
  'age': 1000,
  'x-header1': ['value1', 'value2']
};

const setResponseHeaders = (res) => {
  res.setHeader('server', 'node');
  res.setHeader('etag', 12345);
  res.setHeader('Set-Cookie', ['key1=value1', 'key2=value2']);
  res.setHeader('x-header2', ['value1', 'value2']);
};

const kTimeout = 1000;
const kDelta = 200;

const handleRequest = (req, res) => {
  const path = req.url;
  switch (path) {
    case '/hello-world':
      setResponseHeaders(res);
      res.writeHead(200);
      // Ensure the header is sent.
      res.write('\n');

      setTimeout(() => {
        res.end('hello world\n');
      }, kTimeout);
      break;
    default:
      assert(false, `Unexpected path: ${path}`);
  }
};

const httpServer = http.createServer(handleRequest);

const httpsServer = https.createServer({
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem')
}, handleRequest);

const terminate = () => {
  session.disconnect();
  httpServer.close();
  httpsServer.close();
  inspector.close();
};

function verifyRequestWillBeSent({ method, params }, expect) {
  assert.strictEqual(method, 'Network.requestWillBeSent');

  assert.ok(params.requestId.startsWith('node-network-event-'));
  assert.strictEqual(params.request.url, expect.url);
  assert.strictEqual(params.request.method, 'GET');
  assert.strictEqual(typeof params.request.headers, 'object');
  assert.strictEqual(params.request.headers['accept-language'], 'en-US');
  assert.strictEqual(params.request.headers.cookie, 'k1=v1; k2=v2');
  assert.strictEqual(params.request.headers.age, '1000');
  assert.strictEqual(params.request.headers['x-header1'], 'value1, value2');
  assert.strictEqual(typeof params.timestamp, 'number');
  assert.strictEqual(typeof params.wallTime, 'number');

  return params;
}

function verifyResponseReceived({ method, params }, expect) {
  assert.strictEqual(method, 'Network.responseReceived');

  assert.ok(params.requestId.startsWith('node-network-event-'));
  assert.strictEqual(typeof params.timestamp, 'number');
  assert.strictEqual(params.type, 'Other');
  assert.strictEqual(params.response.status, 200);
  assert.strictEqual(params.response.statusText, 'OK');
  assert.strictEqual(params.response.url, expect.url);
  assert.strictEqual(typeof params.response.headers, 'object');
  assert.strictEqual(params.response.headers.server, 'node');
  assert.strictEqual(params.response.headers.etag, '12345');
  assert.strictEqual(params.response.headers['set-cookie'], 'key1=value1\nkey2=value2');
  assert.strictEqual(params.response.headers['x-header2'], 'value1, value2');

  return params;
}

function verifyLoadingFinished({ method, params }) {
  assert.strictEqual(method, 'Network.loadingFinished');

  assert.ok(params.requestId.startsWith('node-network-event-'));
  assert.strictEqual(typeof params.timestamp, 'number');
  return params;
}

function verifyLoadingFailed({ method, params }) {
  assert.strictEqual(method, 'Network.loadingFailed');

  assert.ok(params.requestId.startsWith('node-network-event-'));
  assert.strictEqual(typeof params.timestamp, 'number');
  assert.strictEqual(params.type, 'Other');
  assert.strictEqual(typeof params.errorText, 'string');
}

async function testHttpGet() {
  const url = `http://127.0.0.1:${httpServer.address().port}/hello-world`;
  const requestWillBeSentFuture = once(session, 'Network.requestWillBeSent')
    .then(([event]) => verifyRequestWillBeSent(event, { url }));

  const responseReceivedFuture = once(session, 'Network.responseReceived')
    .then(([event]) => verifyResponseReceived(event, { url }));

  const loadingFinishedFuture = once(session, 'Network.loadingFinished')
    .then(([event]) => verifyLoadingFinished(event));

  http.get({
    host: '127.0.0.1',
    port: httpServer.address().port,
    path: '/hello-world',
    headers: requestHeaders
  }, common.mustCall((res) => {
    // Dump the response.
    res.on('data', () => {});
    res.on('end', () => {});
  }));

  await requestWillBeSentFuture;
  const responseReceived = await responseReceivedFuture;
  const loadingFinished = await loadingFinishedFuture;

  const delta = (loadingFinished.timestamp - responseReceived.timestamp) * 1000;
  assert.ok(delta > kDelta);
}

async function testHttpsGet() {
  const url = `https://127.0.0.1:${httpsServer.address().port}/hello-world`;
  const requestWillBeSentFuture = once(session, 'Network.requestWillBeSent')
    .then(([event]) => verifyRequestWillBeSent(event, { url }));

  const responseReceivedFuture = once(session, 'Network.responseReceived')
    .then(([event]) => verifyResponseReceived(event, { url }));

  const loadingFinishedFuture = once(session, 'Network.loadingFinished')
    .then(([event]) => verifyLoadingFinished(event));

  https.get({
    host: '127.0.0.1',
    port: httpsServer.address().port,
    path: '/hello-world',
    rejectUnauthorized: false,
    headers: requestHeaders,
  }, common.mustCall((res) => {
    // Dump the response.
    res.on('data', () => {});
    res.on('end', () => {});
  }));

  await requestWillBeSentFuture;
  const responseReceived = await responseReceivedFuture;
  const loadingFinished = await loadingFinishedFuture;

  const delta = (loadingFinished.timestamp - responseReceived.timestamp) * 1000;
  assert.ok(delta > kDelta);
}

async function testHttpError() {
  const url = `http://${addresses.INVALID_HOST}/`;
  const requestWillBeSentFuture = once(session, 'Network.requestWillBeSent')
    .then(([event]) => verifyRequestWillBeSent(event, { url }));
  session.on('Network.responseReceived', common.mustNotCall());
  session.on('Network.loadingFinished', common.mustNotCall());

  const loadingFailedFuture = once(session, 'Network.loadingFailed')
    .then(([event]) => verifyLoadingFailed(event));

  http.get({
    host: addresses.INVALID_HOST,
    headers: requestHeaders,
  }, common.mustNotCall()).on('error', common.mustCall());

  await requestWillBeSentFuture;
  await loadingFailedFuture;
}

async function testHttpsError() {
  const url = `https://${addresses.INVALID_HOST}/`;
  const requestWillBeSentFuture = once(session, 'Network.requestWillBeSent')
    .then(([event]) => verifyRequestWillBeSent(event, { url }));
  session.on('Network.responseReceived', common.mustNotCall());
  session.on('Network.loadingFinished', common.mustNotCall());

  const loadingFailedFuture = once(session, 'Network.loadingFailed')
    .then(([event]) => verifyLoadingFailed(event));

  https.get({
    host: addresses.INVALID_HOST,
    headers: requestHeaders,
  }, common.mustNotCall()).on('error', common.mustCall());

  await requestWillBeSentFuture;
  await loadingFailedFuture;
}

const testNetworkInspection = async () => {
  await testHttpGet();
  session.removeAllListeners();
  await testHttpsGet();
  session.removeAllListeners();
  await testHttpError();
  session.removeAllListeners();
  await testHttpsError();
  session.removeAllListeners();
};

httpServer.listen(0, () => {
  httpsServer.listen(0, async () => {
    try {
      await session.post('Network.enable');
      await testNetworkInspection();
      await session.post('Network.disable');
    } catch (e) {
      assert.fail(e);
    } finally {
      terminate();
    }
  });
});
