'use strict';

const common = require('../common');
if (common.isWindows) {
  common.skip('no signals on Windows');
}
const { isMainThread } = require('worker_threads');
if (!isMainThread) {
  common.skip('No signal handling available in Workers');
}

const initHooks = require('./init-hooks');
const verifyGraph = require('./verify-graph');
const { exec } = require('child_process');

const hooks = initHooks();

hooks.enable();
const interval = setInterval(() => {}, 9999); // Keep event loop open
process.on('SIGUSR2', common.mustCall(onsigusr2, 2));

let count = 0;
exec(`kill -USR2 ${process.pid}`);

function onsigusr2() {
  count++;

  if (count === 1) {
    // Trigger same signal handler again
    exec(`kill -USR2 ${process.pid}`);
  } else {
    // Install another signal handler
    process.removeAllListeners('SIGUSR2');
    process.on('SIGUSR2', common.mustCall(onsigusr2Again));

    exec(`kill -USR2 ${process.pid}`);
  }
}

function onsigusr2Again() {
  clearInterval(interval); // Let the event loop close
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  verifyGraph(
    hooks,
    [ { type: 'SIGNALWRAP', id: 'signal:1', triggerAsyncId: null },
      { type: 'PROCESSWRAP', id: 'process:1', triggerAsyncId: null },
      { type: 'PIPEWRAP', id: 'pipe:1', triggerAsyncId: null },
      { type: 'PIPEWRAP', id: 'pipe:2', triggerAsyncId: null },
      { type: 'PIPEWRAP', id: 'pipe:3', triggerAsyncId: null },
      { type: 'PROCESSWRAP', id: 'process:2', triggerAsyncId: 'signal:1' },
      { type: 'PIPEWRAP', id: 'pipe:4', triggerAsyncId: 'signal:1' },
      { type: 'PIPEWRAP', id: 'pipe:5', triggerAsyncId: 'signal:1' },
      { type: 'PIPEWRAP', id: 'pipe:6', triggerAsyncId: 'signal:1' },
      { type: 'SIGNALWRAP', id: 'signal:2',
        // TEST_THREAD_ID is set by tools/test.py. Adjust test results depending
        // on whether the test was invoked via test.py or from the shell
        // directly.
        triggerAsyncId: process.env.TEST_THREAD_ID ? 'signal:1' : 'pipe:2' },
      { type: 'PROCESSWRAP', id: 'process:3', triggerAsyncId: 'signal:1' },
      { type: 'PIPEWRAP', id: 'pipe:7', triggerAsyncId: 'signal:1' },
      { type: 'PIPEWRAP', id: 'pipe:8', triggerAsyncId: 'signal:1' },
      { type: 'PIPEWRAP', id: 'pipe:9', triggerAsyncId: 'signal:1' } ],
  );
}
