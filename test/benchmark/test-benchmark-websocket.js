'use strict';

const common = require('../common');
if (!common.enoughTestMem)
  common.skip('Insufficient memory for Websocket benchmark test');

const runBenchmark = require('../common/benchmark');

runBenchmark('websocket', { NODEJS_BENCHMARK_ZERO_ALLOWED: 1 });
