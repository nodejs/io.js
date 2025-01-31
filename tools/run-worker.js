'use strict';
if (typeof require === 'undefined') {
  console.log('1..0 # Skipped: Not being run as CommonJS');
  process.exit(0);
}

const path = require('path');
const { Worker } = require('worker_threads');

// When --permission is enabled, the process
// aren't able to spawn any worker unless --allow-worker is passed.
// Therefore, we skip the permission tests for custom-suites-freestyle
if (process.permission && !process.permission.has('worker')) {
  console.log('1..0 # Skipped: Not being run with worker_threads permission');
  process.exit(0);
}

new Worker(path.resolve(process.cwd(), process.argv[2]))
  .on('exit', (code) => process.exitCode = code);
