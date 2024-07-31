'use strict';

const { WPTRunner } = require('../common/wpt');

const runner = new WPTRunner('resource-timing');

runner.pretendGlobalThisAs('Window');
runner.setInitScript(`
  global.resource = performance.markResourceTiming({
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
      ALPNNegotiatedProtocol: '',
    },
    finalNetworkRequestStartTime: 0,
    finalNetworkResponseStartTime: 0,
    encodedBodySize: 0,
    decodedBodySize: 0,
  }, 'https://nodejs.org', '', global, '', {}, 200, '');
`);

runner.runJsTests();
