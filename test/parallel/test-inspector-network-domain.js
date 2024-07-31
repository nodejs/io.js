// Flags: --inspect=0 --experimental-network-inspection
'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('node:assert');
const fixtures = require('../common/fixtures');
const http = require('node:http');
const https = require('node:https');
const inspector = require('node:inspector/promises');

const session = new inspector.Session();
session.connect();

const httpServer = http.createServer((req, res) => {
  const path = req.url;
  switch (path) {
    case '/hello-world':
      res.writeHead(200);
      res.end('hello world\n');
      break;
    default:
      assert(false, `Unexpected path: ${path}`);
  }
});

const httpsServer = https.createServer({
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem')
}, (req, res) => {
  const path = req.url;
  switch (path) {
    case '/hello-world':
      res.writeHead(200);
      res.end('hello world\n');
      break;
    default:
      assert(false, `Unexpected path: ${path}`);
  }
});

const terminate = () => {
  session.disconnect();
  httpServer.close();
  httpsServer.close();
  inspector.close();
};

const testHttpGet = () => new Promise((resolve, reject) => {
  session.on('Network.requestWillBeSent', common.mustCall(({ params }) => {
    assert.ok(params.requestId.startsWith('node-network-event-'));
    assert.strictEqual(params.request.url, 'http://127.0.0.1/hello-world');
    assert.strictEqual(params.request.method, 'GET');
    assert.strictEqual(typeof params.timestamp, 'number');
    assert.strictEqual(typeof params.wallTime, 'number');
  }));
  session.on('Network.responseReceived', common.mustCall(({ params }) => {
    assert.ok(params.requestId.startsWith('node-network-event-'));
    assert.strictEqual(typeof params.timestamp, 'number');
  }));
  session.on('Network.loadingFinished', common.mustCall(({ params }) => {
    assert.ok(params.requestId.startsWith('node-network-event-'));
    assert.strictEqual(typeof params.timestamp, 'number');
    resolve();
  }));

  http.get({
    host: '127.0.0.1',
    port: httpServer.address().port,
    path: '/hello-world',
  }, common.mustCall());
});

const testHttpsGet = () => new Promise((resolve, reject) => {
  session.on('Network.requestWillBeSent', common.mustCall(({ params }) => {
    assert.ok(params.requestId.startsWith('node-network-event-'));
    assert.strictEqual(params.request.url, 'https://127.0.0.1/hello-world');
    assert.strictEqual(params.request.method, 'GET');
    assert.strictEqual(typeof params.timestamp, 'number');
    assert.strictEqual(typeof params.wallTime, 'number');
  }));
  session.on('Network.responseReceived', common.mustCall(({ params }) => {
    assert.ok(params.requestId.startsWith('node-network-event-'));
    assert.strictEqual(typeof params.timestamp, 'number');
  }));
  session.on('Network.loadingFinished', common.mustCall(({ params }) => {
    assert.ok(params.requestId.startsWith('node-network-event-'));
    assert.strictEqual(typeof params.timestamp, 'number');
    resolve();
  }));

  https.get({
    host: '127.0.0.1',
    port: httpsServer.address().port,
    path: '/hello-world',
    rejectUnauthorized: false,
  }, common.mustCall());
});

const testNetworkInspection = async () => {
  await testHttpGet();
  session.removeAllListeners();
  await testHttpsGet();
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
