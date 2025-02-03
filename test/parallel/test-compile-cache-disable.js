'use strict';

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');

{
  tmpdir.refresh();

  spawnSyncAndAssert(
    process.execPath,
    ['-r', fixtures.path('compile-cache-wrapper.js'), fixtures.path('empty.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: undefined,
        NODE_TEST_COMPILE_CACHE_DIR: tmpdir.path,
        NODE_DISABLE_COMPILE_CACHE: '1',
      },
      cwd: tmpdir.path
    },
    {
      stdout(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /dir before enableCompileCache: undefined/);
        assert.match(output, /Compile cache already disabled/);
        assert.match(output, /dir after enableCompileCache: undefined/);
        return true;
      },
      stderr(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /Disabled by NODE_DISABLE_COMPILE_CACHE/);
        return true;
      }
    });
}
