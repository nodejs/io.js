'use strict';
const common = require('../common');
const timers = require('timers');

// Delete global APIs to make sure they're not relied on by the internal timers
// code
delete globalThis.setTimeout;
delete globalThis.clearTimeout;
delete globalThis.setInterval;
delete globalThis.clearInterval;
delete globalThis.setImmediate;
delete globalThis.clearImmediate;

const timeoutCallback = () => { timers.clearTimeout(timeout); };
const timeout = timers.setTimeout(common.mustCall(timeoutCallback), 1);

const intervalCallback = () => { timers.clearInterval(interval); };
const interval = timers.setInterval(common.mustCall(intervalCallback), 1);

const immediateCallback = () => { timers.clearImmediate(immediate); };
const immediate = timers.setImmediate(immediateCallback);
