'use strict';

// This tests --build-snapshot-config.

require('../common');
const assert = require('assert');
const {
  spawnSyncAndAssert,
  spawnSyncAndExit,
  spawnSyncAndExitWithoutError,
} = require('../common/child_process');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const fs = require('fs');

const blobPath = tmpdir.resolve('snapshot.blob');
const builderScript = fixtures.path('snapshot', 'mutate-fs.js');
const checkFile = fixtures.path('snapshot', 'check-mutate-fs.js');
const configPath = tmpdir.resolve('snapshot.json');
tmpdir.refresh();
{
  // Relative path.
  spawnSyncAndExit(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot-config',
    'snapshot.json',
  ], {
    cwd: tmpdir.path
  }, {
    signal: null,
    status: 1,
    trim: true,
    stderr: /Cannot read snapshot configuration from snapshot\.json/
  });

  // Absolute path.
  spawnSyncAndExit(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot-config',
    configPath,
  ], {
    cwd: tmpdir.path
  }, {
    signal: null,
    status: 1,
    trim: true,
    stderr: /Cannot read snapshot configuration from .+snapshot\.json/
  });
}

function writeConfig(config) {
  fs.writeFileSync(configPath, JSON.stringify(config, null, 2), 'utf8');
}

{
  tmpdir.refresh();
  // Config without "builder" field should be rejected.
  writeConfig({});
  spawnSyncAndExit(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot-config',
    configPath,
  ], {
    cwd: tmpdir.path
  }, {
    signal: null,
    status: 1,
    trim: true,
    stderr: /"builder" field of .+snapshot\.json is not a non-empty string/
  });
}

let sizeWithCache;
{
  tmpdir.refresh();
  // Create a working snapshot.
  writeConfig({ builder: builderScript });
  spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot-config',
    configPath,
  ], {
    cwd: tmpdir.path
  });
  const stats = fs.statSync(blobPath);
  assert(stats.isFile());
  sizeWithCache = stats.size;

  // Check the snapshot.
  spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
    checkFile,
  ], {
    cwd: tmpdir.path
  });
}

let sizeWithoutCache;
{
  tmpdir.refresh();
  // Create a working snapshot.
  writeConfig({ builder: builderScript, withoutCodeCache: true });
  spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot-config',
    configPath,
  ], {
    env: {
      ...process.env,
      NODE_DEBUG_NATIVE: 'CODE_CACHE'
    },
    cwd: tmpdir.path
  });
  const stats = fs.statSync(blobPath);
  assert(stats.isFile());
  sizeWithoutCache = stats.size;
  assert(sizeWithoutCache < sizeWithCache,
         `sizeWithoutCache = ${sizeWithoutCache} >= sizeWithCache ${sizeWithCache}`);
  // Check the snapshot.
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    checkFile,
  ], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
      NODE_DEBUG_NATIVE: 'CODE_CACHE'
    },
  }, {
    stderr: /snapshot contains 0 code cache/
  });
}
