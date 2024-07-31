'use strict';

const common = require('../common');
const dc = require('diagnostics_channel');

const channel = dc.tracingChannel('test');

const handlers = {
  start: common.mustNotCall(),
  end: common.mustNotCall(),
  asyncStart: common.mustNotCall(),
  asyncEnd: common.mustNotCall(),
  error: common.mustNotCall()
};

// While subscribe occurs _before_ the promise resolves,
// no async events should be published.
channel.tracePromise(() => {
  return new Promise(setImmediate);
}, {});
channel.subscribe(handlers);
