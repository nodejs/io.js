'use strict';

const common = require('../common');
const assert = require('assert');
const util = require('util');
const { performance, PerformanceObserver } = require('perf_hooks');

const perfObserver = new PerformanceObserver(common.mustCall((items) => {
  const entries = items.getEntries();
  assert.ok(entries.length === 1);
  for (const entry of entries) {
    assert.ok(util.inspect(entry).includes('this is detail'));
  }
}));

perfObserver.observe({ entryTypes: ['measure'] });

performance.measure('sample', {
  detail: 'this is detail',
});
