// Test version set to preview1
'use strict';

const { spawnSyncAndAssert } = require('./child_process');
const fixtures = require('./fixtures');
const childPath = fixtures.path('wasi-preview-1.js');

function testWasiPreview1(args, spawnArgs = {}, expectations = {}) {
  const newEnv = {
    ...process.env,
    NODE_DEBUG_NATIVE: 'wasi',
    NODE_PLATFORM: process.platform,
    ...spawnArgs.env,
  };
  spawnArgs.env = newEnv;

  console.log('Testing with --turbo-fast-api-calls:', ...args);
  spawnSyncAndAssert(
    process.execPath, [
      '--turbo-fast-api-calls',
      childPath,
      ...args,
    ],
    spawnArgs,
    expectations,
  );

  console.log('Testing with --no-turbo-fast-api-calls:', ...args);
  spawnSyncAndAssert(
    process.execPath,
    [
      '--no-turbo-fast-api-calls',
      childPath,
      ...args,
    ],
    spawnArgs,
    expectations,
  );
}

module.exports = {
  testWasiPreview1,
};
