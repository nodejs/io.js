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

// While subscribe occurs _before_ the sync call ends,
// no end event should be published.
channel.traceSync(() => {
  channel.subscribe(handlers);
}, {});
