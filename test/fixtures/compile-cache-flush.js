'use strict';

const { flushCompileCache, getCompileCacheDir } = require('module');
const { spawnSync } = require('child_process');
const assert = require('assert');

if (process.argv[2] !== 'child') {
  // The test should be run with the compile cache already enabled and NODE_DEBUG_NATIVE=COMPILE_CACHE.
  assert(getCompileCacheDir());
  assert(process.env.NODE_DEBUG_NATIVE.includes('COMPILE_CACHE'));

  flushCompileCache();

  const child1 = spawnSync(process.execPath, [__filename, 'child']);
  console.log(child1.stderr.toString().trim().split('\n').map(line => `[child1]${line}`).join('\n'));

  flushCompileCache();

  const child2 = spawnSync(process.execPath, [__filename, 'child']);
  console.log(child2.stderr.toString().trim().split('\n').map(line => `[child2]${line}`).join('\n'));
}
