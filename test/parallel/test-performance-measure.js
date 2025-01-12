'use strict';

const common = require('../common');
const assert = require('assert');

const { PerformanceObserver, performance } = require('perf_hooks');
const DELAY = 1000;
const ALLOWED_MARGIN = 10;

const expected = ['Start to Now', 'A to Now', 'A to B'];
const obs = new PerformanceObserver(common.mustCall((items) => {
  items.getEntries().forEach(({ name, duration }) => {
    // TODO(mrjithil) remove after 55431 fixed
    const msg = `${DELAY} | ${ALLOWED_MARGIN} | ${duration}`;
    assert.ok(duration > (DELAY - ALLOWED_MARGIN), msg);
    assert.strictEqual(expected.shift(), name);
  });
}));
obs.observe({ entryTypes: ['measure'] });

performance.mark('A');
setTimeout(common.mustCall(() => {
  performance.measure('Start to Now');
  performance.measure('A to Now', 'A');

  performance.mark('B');
  performance.measure('A to B', 'A', 'B');
}), DELAY);
