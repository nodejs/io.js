'use strict';

require('../common');

const {
  generateSEA,
  skipIfSingleExecutableIsNotSupported,
} = require('../common/sea');

skipIfSingleExecutableIsNotSupported();

// This tests the snapshot support in single executable applications.

const tmpdir = require('../common/tmpdir');
const { writeFileSync, existsSync } = require('fs');
const {
  spawnSyncAndAssert, spawnSyncAndExitWithoutError,
} = require('../common/child_process');
const assert = require('assert');

const configFile = tmpdir.resolve('sea-config.json');
const seaPrepBlob = tmpdir.resolve('sea-prep.blob');
const outputFile = tmpdir.resolve(process.platform === 'win32' ? 'sea.exe' : 'sea');

{
  tmpdir.refresh();

  // FIXME(joyeecheung): currently `worker_threads` cannot be loaded during the
  // snapshot building process because internal/worker.js is accessing isMainThread at
  // the top level (and there are maybe more code that access these at the top-level),
  // and have to be loaded in the deserialized snapshot main function.
  // Change these states to be accessed on-demand.
  const code = `
  const {
    setDeserializeMainFunction,
  } = require('v8').startupSnapshot;
  setDeserializeMainFunction(() => {
    const { Worker } = require('worker_threads');
    new Worker("console.log('Hello from Worker')", { eval: true });
  });
  `;

  writeFileSync(tmpdir.resolve('snapshot.js'), code, 'utf-8');
  writeFileSync(configFile, `
  {
    "main": "snapshot.js",
    "output": "sea-prep.blob",
    "useSnapshot": true
  }
  `);

  spawnSyncAndExitWithoutError(
    process.execPath,
    ['--experimental-sea-config', 'sea-config.json'],
    {
      cwd: tmpdir.path,
      env: {
        NODE_DEBUG_NATIVE: 'SEA',
        ...process.env,
      },
    });

  assert(existsSync(seaPrepBlob));

  generateSEA(outputFile, process.execPath, seaPrepBlob);

  spawnSyncAndAssert(
    outputFile,
    {
      env: {
        NODE_DEBUG_NATIVE: 'SEA',
        ...process.env,
      }
    },
    {
      trim: true,
      stdout: 'Hello from Worker'
    }
  );
}
