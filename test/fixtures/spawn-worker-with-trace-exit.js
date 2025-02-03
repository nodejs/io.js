'use strict';

const { Worker, isMainThread } = require('worker_threads')

// Tests that valid per-isolate/env NODE_OPTIONS are allowed and
// work in child workers.
if (isMainThread) {
  new Worker(__filename, {
    env: {
      ...process.env,
      NODE_OPTIONS: '--trace-exit'
    }
  })
} else {
  setImmediate(() => {
    process.nextTick(() => {
      process.exit(0);
    });
  });
}
