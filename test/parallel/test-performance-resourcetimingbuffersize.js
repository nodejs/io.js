'use strict';

const common = require('../common');
const assert = require('assert');

const { performance } = require('perf_hooks');

const timingInfo = {
  startTime: 0,
  endTime: 0,
  finalServiceWorkerStartTime: 0,
  redirectStartTime: 0,
  redirectEndTime: 0,
  postRedirectStartTime: 0,
  finalConnectionTimingInfo: {
    domainLookupStartTime: 0,
    domainLookupEndTime: 0,
    connectionStartTime: 0,
    connectionEndTime: 0,
    secureConnectionStartTime: 0,
    ALPNNegotiatedProtocol: 0,
  },
  finalNetworkRequestStartTime: 0,
  finalNetworkResponseStartTime: 0,
  encodedBodySize: 0,
  decodedBodySize: 0,
};
const requestedUrl = 'https://nodejs.org';
const initiatorType = '';
const cacheMode = '';

async function main() {
  const args = [
    timingInfo,
    requestedUrl,
    initiatorType,
    globalThis,
    cacheMode,
  ];
  // Invalid buffer size values are converted to 0.
  const invalidValues = [ null, undefined, true, false, -1, 0.5, Infinity, NaN, '', 'foo', {}, [], () => {} ];
  for (const value of invalidValues) {
    performance.setResourceTimingBufferSize(value);
    performance.markResourceTiming(...args);
    assert.strictEqual(performance.getEntriesByType('resource').length, 0);
    performance.clearResourceTimings();
  }
  // Wait for the buffer full event to be cleared.
  await waitBufferFullEvent();

  performance.setResourceTimingBufferSize(1);
  performance.markResourceTiming(...args);
  // Trigger a resourcetimingbufferfull event.
  performance.markResourceTiming(...args);
  assert.strictEqual(performance.getEntriesByType('resource').length, 1);
  await waitBufferFullEvent();

  // Apply a new buffer size limit
  performance.setResourceTimingBufferSize(0);
  // Buffer is not cleared on `performance.setResourceTimingBufferSize`.
  assert.strictEqual(performance.getEntriesByType('resource').length, 1);

  performance.clearResourceTimings();
  assert.strictEqual(performance.getEntriesByType('resource').length, 0);
  // Trigger a resourcetimingbufferfull event.
  performance.markResourceTiming(...args);
  // New entry is not added to the global buffer.
  assert.strictEqual(performance.getEntriesByType('resource').length, 0);
  await waitBufferFullEvent();

  // Apply a new buffer size limit
  performance.setResourceTimingBufferSize(1);
  performance.markResourceTiming(...args);
  assert.strictEqual(performance.getEntriesByType('resource').length, 1);
}

function waitBufferFullEvent() {
  return new Promise((resolve) => {
    const listener = common.mustCall((event) => {
      assert.strictEqual(event.type, 'resourcetimingbufferfull');
      performance.removeEventListener('resourcetimingbufferfull', listener);
      resolve();
    });
    performance.addEventListener('resourcetimingbufferfull', listener);
  });
}

main();
