'use strict';
const common = require('../common.js');

const bench = common.createBenchmark(main, {
  n: [1e5],
  nListener: [1, 5, 10],
});

function main({ n, nListener }) {
  const target = new EventTarget();
  const listeners = [];
  for (let k = 0; k < nListener; k += 1)
    listeners.push(() => {});

  bench.start();
  for (let i = 0; i < n; i += 1) {
    for (let k = listeners.length; --k >= 0;) {
      target.addEventListener('abort', listeners[k]);
    }
    for (let k = listeners.length; --k >= 0;) {
      target.removeEventListener('abort', listeners[k]);
    }
  }
  bench.end(n);
}
