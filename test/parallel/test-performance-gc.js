// Flags: --expose-gc --no-warnings
'use strict';

const common = require('../common');
const assert = require('assert');
const {
  PerformanceObserver,
  constants
} = require('perf_hooks');

const {
  NODE_PERFORMANCE_GC_MAJOR,
  NODE_PERFORMANCE_GC_MINOR,
  NODE_PERFORMANCE_GC_INCREMENTAL,
  NODE_PERFORMANCE_GC_WEAKCB,
  NODE_PERFORMANCE_GC_FLAGS_FORCED
} = constants;

const kinds = [
  NODE_PERFORMANCE_GC_MAJOR,
  NODE_PERFORMANCE_GC_MINOR,
  NODE_PERFORMANCE_GC_INCREMENTAL,
  NODE_PERFORMANCE_GC_WEAKCB,
];

// Adding an observer should force at least one gc to appear
{
  const obs = new PerformanceObserver(common.mustCallAtLeast((list) => {
    const entry = list.getEntries()[0];
    assert(entry);
    assert.strictEqual(entry.name, 'gc');
    assert.strictEqual(entry.entryType, 'gc');
    assert(kinds.includes(entry.kind));
    assert(kinds.includes(entry.detail.kind));
    assert.strictEqual(entry.flags, NODE_PERFORMANCE_GC_FLAGS_FORCED);
    assert.strictEqual(entry.detail.flags, NODE_PERFORMANCE_GC_FLAGS_FORCED);
    assert.strictEqual(typeof entry.startTime, 'number');
    assert(entry.startTime < 1e4, 'startTime should be relative to performance.timeOrigin.');
    assert.strictEqual(typeof entry.duration, 'number');
    obs.disconnect();
  }));
  obs.observe({ entryTypes: ['gc'] });
  globalThis.gc();
  // Keep the event loop alive to witness the GC async callback happen.
  setImmediate(() => setImmediate(() => 0));
}

// GC should not keep the event loop alive
{
  let didCall = false;
  process.on('beforeExit', () => {
    assert(!didCall);
    didCall = true;
    globalThis.gc();
  });
}
