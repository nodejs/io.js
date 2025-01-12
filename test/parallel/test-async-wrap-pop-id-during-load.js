'use strict';

require('../common');
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');

test('async function stack overflow test', async (t) => {
  if (process.argv[2] === 'async') {
    async function fn() {
      fn();
      throw new Error();
    }
    await (async function() { await fn(); })();
  }

  const ret = spawnSync(
    process.execPath,
    ['--unhandled-rejections=none', '--stack_size=150', __filename, 'async'],
    { maxBuffer: Infinity }
  );

  // Expecting exit code 7, as the test triggers a stack overflow
  assert.strictEqual(ret.status, 7,
                     `EXIT CODE: ${ret.status}, STDERR:\n${ret.stderr}`);
  const stderr = ret.stderr.toString('utf8', 0, 2048);
  assert.doesNotMatch(stderr, /async.*hook/i);
  assert.ok(stderr.includes('Maximum call stack size exceeded'), stderr);
});
