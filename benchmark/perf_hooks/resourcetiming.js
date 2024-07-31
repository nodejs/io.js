'use strict';

const common = require('../common.js');

const {
  PerformanceObserver,
  performance,
} = require('perf_hooks');

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
  finalConnectionTimingInfo = null,
}) {
  if (finalConnectionTimingInfo !== null) {
    finalConnectionTimingInfo.domainLookupStartTime =
        finalConnectionTimingInfo.domainLookupStartTime || 0;
    finalConnectionTimingInfo.domainLookupEndTime =
        finalConnectionTimingInfo.domainLookupEndTime || 0;
    finalConnectionTimingInfo.connectionStartTime =
        finalConnectionTimingInfo.connectionStartTime || 0;
    finalConnectionTimingInfo.connectionEndTime =
        finalConnectionTimingInfo.connectionEndTime || 0;
    finalConnectionTimingInfo.secureConnectionStartTime =
        finalConnectionTimingInfo.secureConnectionStartTime || 0;
    finalConnectionTimingInfo.ALPNNegotiatedProtocol =
        finalConnectionTimingInfo.ALPNNegotiatedProtocol || [];
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

const bench = common.createBenchmark(main, {
  n: [1e6],
  observe: ['resource'],
});

function test() {
  const timingInfo = createTimingInfo({ finalConnectionTimingInfo: {} });
  performance.markResourceTiming(
    timingInfo,
    'http://localhost:8080',
    'fetch',
    {},
    '',
  );
}

function main({ n, observe }) {
  const obs = new PerformanceObserver(() => {
    bench.end(n);
  });
  obs.observe({ entryTypes: [observe], buffered: true });

  bench.start();
  for (let i = 0; i < n; i++)
    test();
}
