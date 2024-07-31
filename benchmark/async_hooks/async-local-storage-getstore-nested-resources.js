'use strict';
const common = require('../common.js');
const { AsyncLocalStorage, AsyncResource } = require('async_hooks');

/**
 * This benchmark verifies the performance of
 * `AsyncLocalStorage.getStore()` on propagation through async
 * resource scopes.
 *
 * - AsyncLocalStorage.run()
 *  - AsyncResource.runInAsyncScope
 *    - AsyncResource.runInAsyncScope
 *      ...
 *        - AsyncResource.runInAsyncScope
 *          - AsyncLocalStorage.getStore()
 */
const bench = common.createBenchmark(main, {
  resourceCount: [10, 100, 1000],
  n: [5e5],
});

function runBenchmark(store, n) {
  for (let i = 0; i < n; i++) {
    store.getStore();
  }
}

function runInAsyncScopes(resourceCount, cb, i = 0) {
  if (i === resourceCount) {
    cb();
  } else {
    const resource = new AsyncResource('noop');
    resource.runInAsyncScope(() => {
      runInAsyncScopes(resourceCount, cb, i + 1);
    });
  }
}

function main({ n, resourceCount }) {
  const store = new AsyncLocalStorage();
  runInAsyncScopes(resourceCount, () => {
    bench.start();
    runBenchmark(store, n);
    bench.end(n);
  });
}
