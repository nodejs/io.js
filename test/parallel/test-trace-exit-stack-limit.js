'use strict';

// This tests that --stack-trace-limit can be used to tweak the stack trace size of --trace-exit.
require('../common');
const fixture = require('../common/fixtures');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');

// When the stack trace limit is bigger than the stack trace size, it should output them all.
spawnSyncAndAssert(
  process.execPath,
  ['--trace-exit', '--stack-trace-limit=50', fixture.path('deep-exit.js')],
  {
    env: {
      ...process.env,
      STACK_DEPTH: 30
    }
  },
  {
    stderr(output) {
      const matches = [...output.matchAll(/at recurse/g)];
      assert.strictEqual(matches.length, 30);
    }
  });

// When the stack trace limit is smaller than the stack trace size, it should truncate the stack size.
spawnSyncAndAssert(
  process.execPath,
  ['--trace-exit', '--stack-trace-limit=30', fixture.path('deep-exit.js')],
  {
    env: {
      ...process.env,
      STACK_DEPTH: 30
    }
  },
  {
    stderr(output) {
      const matches = [...output.matchAll(/at recurse/g)];
      // The top frame is process.exit(), so one frame from recurse() is truncated.
      assert.strictEqual(matches.length, 29);
    }
  });
