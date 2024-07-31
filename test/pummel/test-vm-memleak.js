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
// Flags: --max_old_space_size=32 --expose_gc

const common = require('../common');

if (common.isASan) {
  common.skip('ASan messes with memory measurements');
}

const assert = require('assert');
const vm = require('vm');

const baselineRss = process.memoryUsage.rss();

const start = Date.now();

const interval = setInterval(function() {
  try {
    vm.runInNewContext('throw 1;');
  } catch {
    // Continue regardless of error.
  }

  global.gc();
  const rss = process.memoryUsage.rss();
  assert.ok(rss < baselineRss + 32 * 1024 * 1024,
            `memory usage: ${rss} baseline: ${baselineRss}`);

  // Stop after 5 seconds.
  if (Date.now() - start > 5 * 1000) {
    clearInterval(interval);

    testContextLeak();
  }
}, 1);

function testContextLeak() {
  // TODO: This needs a comment explaining what it's doing. Will it crash the
  // test if there is a memory leak? Or what?
  for (let i = 0; i < 1000; i++)
    vm.createContext({});
}
