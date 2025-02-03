'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const { getFips } = require('crypto');

if (getFips()) {
  common.skip('some benchmarks are FIPS-incompatible');
}

const runBenchmark = require('../common/benchmark');

runBenchmark('crypto', { NODEJS_BENCHMARK_ZERO_ALLOWED: 1 });
