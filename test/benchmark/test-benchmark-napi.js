'use strict';

const common = require('../common');

if (common.isWindows) {
  common.skip('vcbuild.bat doesn\'t build the n-api benchmarks yet');
}

if (!common.isMainThread) {
  common.skip('addons are not supported in workers');
}

if (common.isDebug) {
  common.skip('benchmark does not work with debug build yet');
}
const runBenchmark = require('../common/benchmark');

runBenchmark('napi', { NODEJS_BENCHMARK_ZERO_ALLOWED: 1 });
