'use strict';

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  // Note that test-timers-immediate-unref-nested-once works instead.
  common.skip('Worker bootstrapping works differently -> different timing');
}

// This immediate should not execute as it was unrefed
// and nothing else is keeping the event loop alive
setImmediate(common.mustNotCall()).unref();
