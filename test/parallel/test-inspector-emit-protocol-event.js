// Flags: --inspect=0 --experimental-network-inspection
'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const inspector = require('node:inspector/promises');
const assert = require('node:assert');

const EXPECTED_EVENTS = {
  Network: [
    {
      name: 'requestWillBeSent',
      params: {
        requestId: 'request-id-1',
        request: {
          url: 'https://nodejs.org/en',
          method: 'GET',
          headers: {},
        },
        timestamp: 1000,
        wallTime: 1000,
      },
      expected: {
        requestId: 'request-id-1',
        request: {
          url: 'https://nodejs.org/en',
          method: 'GET',
          headers: {},
        },
        timestamp: 1000,
        wallTime: 1000,
      }
    },
    {
      name: 'responseReceived',
      params: {
        requestId: 'request-id-1',
        timestamp: 1000,
        type: 'Other',
        response: {
          url: 'https://nodejs.org/en',
          status: 200,
          statusText: '',
          headers: { host: 'nodejs.org' }
        }
      },
      expected: {
        requestId: 'request-id-1',
        timestamp: 1000,
        type: 'Other',
        response: {
          url: 'https://nodejs.org/en',
          status: 200,
          statusText: '',
          headers: { host: 'nodejs.org' }
        }
      }
    },
    {
      name: 'loadingFinished',
      params: {
        requestId: 'request-id-1',
        timestamp: 1000,
      }
    },
    {
      name: 'loadingFailed',
      params: {
        requestId: 'request-id-1',
        timestamp: 1000,
        type: 'Document',
        errorText: 'Failed to load resource'
      }
    },
  ]
};

// Check that all domains and events are present in the inspector object.
for (const [domain, events] of Object.entries(EXPECTED_EVENTS)) {
  if (!(domain in inspector)) {
    assert.fail(`Expected domain ${domain} to be present in inspector`);
  }
  const actualEventNames = Object.keys(inspector[domain]);
  const expectedEventNames = events.map((event) => event.name);
  assert.deepStrictEqual(actualEventNames, expectedEventNames, `Expected ${domain} to have events ${expectedEventNames}, but got ${actualEventNames}`);
}

// Check that all events throw when called with a non-object argument.
for (const [domain, events] of Object.entries(EXPECTED_EVENTS)) {
  for (const event of events) {
    assert.throws(() => inspector[domain][event.name]('params'), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "params" argument must be of type object. Received type string (\'params\')'
    });
  }
}

const runAsyncTest = async () => {
  const session = new inspector.Session();
  session.connect();

  // Check that all events emit the expected parameters.
  await session.post('Network.enable');
  for (const [domain, events] of Object.entries(EXPECTED_EVENTS)) {
    for (const event of events) {
      session.on(`${domain}.${event.name}`, common.mustCall(({ params }) => {
        if (event.name === 'requestWillBeSent') {
          // Initiator is automatically captured and contains caller info.
          // No need to validate it.
          delete params.initiator;
        }
        assert.deepStrictEqual(params, event.expected ?? event.params);
      }));
      inspector[domain][event.name](event.params);
    }
  }

  // Check tht no events are emitted after disabling the domain.
  await session.post('Network.disable');
  session.on('Network.requestWillBeSent', common.mustNotCall());
  inspector.Network.requestWillBeSent({});
};

runAsyncTest().then(common.mustCall()).catch((e) => {
  assert.fail(e);
});
