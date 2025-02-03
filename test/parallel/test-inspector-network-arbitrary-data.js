// Flags: --inspect=0 --experimental-network-inspection
'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const inspector = require('node:inspector/promises');
const { Network } = require('node:inspector');
const test = require('node:test');
const assert = require('node:assert');
const { waitUntil } = require('../common/inspector-helper');

const session = new inspector.Session();
session.connect();

test('should emit Network.requestWillBeSent with unicode', async () => {
  await session.post('Network.enable');
  const expectedValue = 'CJK 汉字 🍱 🧑‍🧑‍🧒‍🧒';

  const requestWillBeSentFuture = waitUntil(session, 'Network.requestWillBeSent')
    .then(([event]) => {
      assert.strictEqual(event.params.request.url, expectedValue);
      assert.strictEqual(event.params.request.method, expectedValue);
      assert.strictEqual(event.params.request.headers.mKey, expectedValue);
    });

  Network.requestWillBeSent({
    requestId: '1',
    timestamp: 1,
    wallTime: 1,
    request: {
      url: expectedValue,
      method: expectedValue,
      headers: {
        mKey: expectedValue,
      },
    },
  });

  await requestWillBeSentFuture;
});
