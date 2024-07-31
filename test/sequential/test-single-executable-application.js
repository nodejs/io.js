'use strict';

require('../common');

const {
  generateSEA,
  skipIfSingleExecutableIsNotSupported,
} = require('../common/sea');

skipIfSingleExecutableIsNotSupported();

// This tests the creation of a single executable application.

const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const { copyFileSync, writeFileSync, existsSync } = require('fs');
const { spawnSyncAndAssert, spawnSyncAndExitWithoutError } = require('../common/child_process');
const { join } = require('path');
const assert = require('assert');

const inputFile = fixtures.path('sea.js');
const requirableFile = tmpdir.resolve('requirable.js');
const configFile = tmpdir.resolve('sea-config.json');
const seaPrepBlob = tmpdir.resolve('sea-prep.blob');
const outputFile = tmpdir.resolve(process.platform === 'win32' ? 'sea.exe' : 'sea');

tmpdir.refresh();

writeFileSync(requirableFile, `
module.exports = {
  hello: 'world',
};
`);

writeFileSync(configFile, `
{
  "main": "sea.js",
  "output": "sea-prep.blob",
  "disableExperimentalSEAWarning": false
}
`);

// Copy input to working directory
copyFileSync(inputFile, tmpdir.resolve('sea.js'));
spawnSyncAndExitWithoutError(
  process.execPath,
  ['--experimental-sea-config', 'sea-config.json'],
  { cwd: tmpdir.path });

assert(existsSync(seaPrepBlob));

generateSEA(outputFile, process.execPath, seaPrepBlob);

spawnSyncAndAssert(
  outputFile,
  [ '-a', '--b=c', 'd' ],
  {
    env: {
      COMMON_DIRECTORY: join(__dirname, '..', 'common'),
      NODE_DEBUG_NATIVE: 'SEA',
      ...process.env,
    }
  },
  {
    stdout: 'Hello, world! 😊\n'
  });
