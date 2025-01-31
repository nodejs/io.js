// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

const common = require('../common');

if (common.isWindows) {
  common.skip('SIGUSR1 and SIGHUP signals are not supported');
}

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('Signal handling in Workers is not supported');
}

console.log(`process.pid: ${process.pid}`);

process.on('SIGUSR1', common.mustCall());

process.on('SIGUSR1', common.mustCall(function() {
  setTimeout(function() {
    console.log('End.');
    process.exit(0);
  }, 5);
}));

let i = 0;
setInterval(function() {
  console.log(`running process...${++i}`);

  if (i === 5) {
    process.kill(process.pid, 'SIGUSR1');
  }
}, 1);

// Test on condition where a watcher for SIGNAL
// has been previously registered, and `process.listeners(SIGNAL).length === 1`
process.on('SIGHUP', common.mustNotCall());
process.removeAllListeners('SIGHUP');
process.on('SIGHUP', common.mustCall());
process.kill(process.pid, 'SIGHUP');
