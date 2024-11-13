'use strict';
const common = require('../common.js');
const { AsyncLocalStorage } = require('async_hooks');

/**
 * This benchmark verifies the performance of
 * `AsyncLocalStorage.getStore()` on multiple `AsyncLocalStorage` instances
 * nested `AsyncLocalStorage.run()`s.
 *
 * - AsyncLocalStorage1.run()
 *   - AsyncLocalStorage2.run()
 *    ...
 *      - AsyncLocalStorageN.run()
 *        - AsyncLocalStorage1.getStore()
 */
const bench = common.createBenchmark(main, {
  storageCount: [1, 10, 100],
  n: [1e4],
});

function runBenchmark(store, n) {
  for (let idx = 0; idx < n; idx++) {
    store.getStore();
  }
}

function runStores(stores, value, cb, idx = 0) {
  if (idx === stores.length) {
    cb();
  } else {
    stores[idx].run(value, () => {
      runStores(stores, value, cb, idx + 1);
    });
  }
}

function main({ n, storageCount }) {
  const stores = new Array(storageCount).fill(0).map(() => new AsyncLocalStorage());
  const contextValue = {};

  runStores(stores, contextValue, () => {
    bench.start();
    runBenchmark(stores[0], n);
    bench.end(n);
  });
}
