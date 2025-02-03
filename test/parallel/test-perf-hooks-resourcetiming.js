'use strict';

const common = require('../common');
const assert = require('assert');
const util = require('util');
const {
  PerformanceObserver,
  PerformanceEntry,
  PerformanceResourceTiming,
  performance,
} = require('perf_hooks');

assert(PerformanceObserver);
assert(PerformanceEntry);
assert.throws(() => new PerformanceEntry(), { code: 'ERR_ILLEGAL_CONSTRUCTOR' });
assert(PerformanceResourceTiming);
assert(performance.clearResourceTimings);
assert(performance.markResourceTiming);

assert.deepStrictEqual(
  Object.getOwnPropertyDescriptor(PerformanceResourceTiming.prototype, Symbol.toStringTag),
  { configurable: true, enumerable: false, value: 'PerformanceResourceTiming', writable: false },
);

function createTimingInfo({
  startTime = 0,
  redirectStartTime = 0,
  redirectEndTime = 0,
  postRedirectStartTime = 0,
  finalServiceWorkerStartTime = 0,
  finalNetworkRequestStartTime = 0,
  finalNetworkResponseStartTime = 0,
  endTime = 0,
  encodedBodySize = 0,
  decodedBodySize = 0,
  finalConnectionTimingInfo = null
}) {
  if (finalConnectionTimingInfo !== null) {
    finalConnectionTimingInfo.domainLookupStartTime ||= 0;
    finalConnectionTimingInfo.domainLookupEndTime ||= 0;
    finalConnectionTimingInfo.connectionStartTime ||= 0;
    finalConnectionTimingInfo.connectionEndTime ||= 0;
    finalConnectionTimingInfo.secureConnectionStartTime ||= 0;
    finalConnectionTimingInfo.ALPNNegotiatedProtocol ||= [];
  }
  return {
    startTime,
    redirectStartTime,
    redirectEndTime,
    postRedirectStartTime,
    finalServiceWorkerStartTime,
    finalNetworkRequestStartTime,
    finalNetworkResponseStartTime,
    endTime,
    encodedBodySize,
    decodedBodySize,
    finalConnectionTimingInfo,
  };
}

// PerformanceResourceTiming should not be initialized externally
{
  assert.throws(() => new PerformanceResourceTiming(), {
    name: 'TypeError',
    message: 'Illegal constructor',
    code: 'ERR_ILLEGAL_CONSTRUCTOR',
  });
}

// Using performance.getEntries*()
{
  const timingInfo = createTimingInfo({ finalConnectionTimingInfo: {} });
  const customGlobal = {};
  const requestedUrl = 'http://localhost:8080';
  const cacheMode = 'local';
  const initiatorType = 'fetch';
  const resource = performance.markResourceTiming(
    timingInfo,
    requestedUrl,
    initiatorType,
    customGlobal,
    cacheMode,
    {},
    200,
    '',
  );

  assert(resource instanceof PerformanceEntry);
  assert(resource instanceof PerformanceResourceTiming);

  {
    const entries = performance.getEntries();
    assert.strictEqual(entries.length, 1);
    assert(entries[0] instanceof PerformanceResourceTiming);
  }

  {
    const entries = performance.getEntriesByType('resource');
    assert.strictEqual(entries.length, 1);
    assert(entries[0] instanceof PerformanceResourceTiming);
  }

  {
    const entries = performance.getEntriesByName(resource.name);
    assert.strictEqual(entries.length, 1);
    assert(entries[0] instanceof PerformanceResourceTiming);
  }

  performance.clearResourceTimings();
  assert.strictEqual(performance.getEntries().length, 0);
}

// Assert resource data based in timingInfo

// default values
{
  const timingInfo = createTimingInfo({ finalConnectionTimingInfo: {} });
  const customGlobal = {};
  const requestedUrl = 'http://localhost:8080';
  const cacheMode = 'local';
  const initiatorType = 'fetch';
  const resource = performance.markResourceTiming(
    timingInfo,
    requestedUrl,
    initiatorType,
    customGlobal,
    cacheMode,
    {},
    200,
    '',
  );

  assert(resource instanceof PerformanceEntry);
  assert(resource instanceof PerformanceResourceTiming);

  assert.strictEqual(resource.entryType, 'resource');
  assert.strictEqual(resource.name, requestedUrl);
  assert.ok(typeof resource.cacheMode === 'undefined', 'cacheMode does not have a getter');
  assert.strictEqual(resource.startTime, timingInfo.startTime);
  assert.strictEqual(resource.duration, 0);
  assert.strictEqual(resource.initiatorType, initiatorType);
  assert.strictEqual(resource.workerStart, 0);
  assert.strictEqual(resource.redirectStart, 0);
  assert.strictEqual(resource.redirectEnd, 0);
  assert.strictEqual(resource.fetchStart, 0);
  assert.strictEqual(resource.domainLookupStart, 0);
  assert.strictEqual(resource.domainLookupEnd, 0);
  assert.strictEqual(resource.connectStart, 0);
  assert.strictEqual(resource.connectEnd, 0);
  assert.strictEqual(resource.secureConnectionStart, 0);
  assert.deepStrictEqual(resource.nextHopProtocol, []);
  assert.strictEqual(resource.requestStart, 0);
  assert.strictEqual(resource.responseStart, 0);
  assert.strictEqual(resource.responseEnd, 0);
  assert.strictEqual(resource.encodedBodySize, 0);
  assert.strictEqual(resource.decodedBodySize, 0);
  assert.strictEqual(resource.transferSize, 0);
  assert.strictEqual(resource.deliveryType, '');
  assert.strictEqual(resource.responseStatus, 200);
  assert.deepStrictEqual(resource.toJSON(), {
    name: requestedUrl,
    entryType: 'resource',
    startTime: 0,
    duration: 0,
    initiatorType,
    nextHopProtocol: [],
    workerStart: 0,
    redirectStart: 0,
    redirectEnd: 0,
    fetchStart: 0,
    domainLookupStart: 0,
    domainLookupEnd: 0,
    connectStart: 0,
    connectEnd: 0,
    secureConnectionStart: 0,
    requestStart: 0,
    responseStart: 0,
    responseEnd: 0,
    transferSize: 0,
    encodedBodySize: 0,
    decodedBodySize: 0,
    responseStatus: 200,
    deliveryType: '',
  });
  assert.strictEqual(util.inspect(performance.getEntries()), `[
  PerformanceResourceTiming {
    name: 'http://localhost:8080',
    entryType: 'resource',
    startTime: 0,
    duration: 0,
    initiatorType: 'fetch',
    nextHopProtocol: [],
    workerStart: 0,
    redirectStart: 0,
    redirectEnd: 0,
    fetchStart: 0,
    domainLookupStart: 0,
    domainLookupEnd: 0,
    connectStart: 0,
    connectEnd: 0,
    secureConnectionStart: 0,
    requestStart: 0,
    responseStart: 0,
    responseEnd: 0,
    transferSize: 0,
    encodedBodySize: 0,
    decodedBodySize: 0,
    deliveryType: '',
    responseStatus: 200
  }
]`);
  assert.strictEqual(util.inspect(resource), `PerformanceResourceTiming {
  name: 'http://localhost:8080',
  entryType: 'resource',
  startTime: 0,
  duration: 0,
  initiatorType: 'fetch',
  nextHopProtocol: [],
  workerStart: 0,
  redirectStart: 0,
  redirectEnd: 0,
  fetchStart: 0,
  domainLookupStart: 0,
  domainLookupEnd: 0,
  connectStart: 0,
  connectEnd: 0,
  secureConnectionStart: 0,
  requestStart: 0,
  responseStart: 0,
  responseEnd: 0,
  transferSize: 0,
  encodedBodySize: 0,
  decodedBodySize: 0,
  deliveryType: '',
  responseStatus: 200
}`);

  assert(resource instanceof PerformanceEntry);
  assert(resource instanceof PerformanceResourceTiming);

  performance.clearResourceTimings();
  const entries = performance.getEntries();
  assert.strictEqual(entries.length, 0);
}

// custom getters math
{
  const timingInfo = createTimingInfo({
    endTime: 100,
    startTime: 50,
    encodedBodySize: 150,
  });
  const customGlobal = {};
  const requestedUrl = 'http://localhost:8080';
  const cacheMode = '';
  const initiatorType = 'fetch';
  const resource = performance.markResourceTiming(
    timingInfo,
    requestedUrl,
    initiatorType,
    customGlobal,
    cacheMode,
    {},
    200,
    '',
  );

  assert(resource instanceof PerformanceEntry);
  assert(resource instanceof PerformanceResourceTiming);

  assert.strictEqual(resource.entryType, 'resource');
  assert.strictEqual(resource.name, requestedUrl);
  assert.ok(typeof resource.cacheMode === 'undefined', 'cacheMode does not have a getter');
  assert.strictEqual(resource.startTime, timingInfo.startTime);
  // Duration should be the timingInfo endTime - startTime
  assert.strictEqual(resource.duration, 50);
  // TransferSize should be encodedBodySize + 300 when cacheMode is empty
  assert.strictEqual(resource.transferSize, 450);

  assert(resource instanceof PerformanceEntry);
  assert(resource instanceof PerformanceResourceTiming);

  performance.clearResourceTimings();
  const entries = performance.getEntries();
  assert.strictEqual(entries.length, 0);
}

// Using PerformanceObserver
{
  const obs = new PerformanceObserver(common.mustCall((list) => {
    {
      const entries = list.getEntries();
      assert.strictEqual(entries.length, 1);
      assert(entries[0] instanceof PerformanceResourceTiming);
    }
    {
      const entries = list.getEntriesByType('resource');
      assert.strictEqual(entries.length, 1);
      assert(entries[0] instanceof PerformanceResourceTiming);
    }
    {
      const entries = list.getEntriesByName('http://localhost:8080');
      assert.strictEqual(entries.length, 1);
      assert(entries[0] instanceof PerformanceResourceTiming);
    }
    obs.disconnect();
  }));
  obs.observe({ entryTypes: ['resource'] });

  const timingInfo = createTimingInfo({ finalConnectionTimingInfo: {} });
  const customGlobal = {};
  const requestedUrl = 'http://localhost:8080';
  const cacheMode = 'local';
  const initiatorType = 'fetch';
  const resource = performance.markResourceTiming(
    timingInfo,
    requestedUrl,
    initiatorType,
    customGlobal,
    cacheMode,
    {},
    200,
    ''
  );

  assert(resource instanceof PerformanceEntry);
  assert(resource instanceof PerformanceResourceTiming);

  performance.clearResourceTimings();
  const entries = performance.getEntries();
  assert.strictEqual(entries.length, 0);
}
