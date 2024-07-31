'use strict';

const common = require('../common.js');

const {
  PerformanceObserver,
  performance,
} = require('perf_hooks');

const bench = common.createBenchmark(main, {
  n: [1e6],
  observe: ['all', 'measure'],
});

function test() {
  performance.mark('a');
  performance.mark('b');
  performance.measure('a to b', 'a', 'b');
}

function main({ n, observe }) {
  const entryTypes = observe === 'all' ?
    [ 'mark', 'measure' ] :
    [ observe ];
  const obs = new PerformanceObserver(() => {
    bench.end(n);
  });
  obs.observe({ entryTypes, buffered: true });

  bench.start();
  performance.mark('start');
  for (let i = 0; i < 1e5; i++)
    test();
}
