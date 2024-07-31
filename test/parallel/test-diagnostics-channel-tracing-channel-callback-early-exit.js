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

// While subscribe occurs _before_ the callback executes,
// no async events should be published.
channel.traceCallback(setImmediate, 0, {}, null, common.mustCall());
channel.subscribe(handlers);
