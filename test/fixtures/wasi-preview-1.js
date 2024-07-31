'use strict';

const assert = require('assert');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const fs = require('fs');
const path = require('path');
const common = require('../common');
const { WASI } = require('wasi');

function returnOnExitEnvToValue(env) {
  const envValue = env.RETURN_ON_EXIT;
  if (envValue === undefined) {
    return undefined;
  }

  return envValue === 'true';
}

common.expectWarning('ExperimentalWarning',
                      'WASI is an experimental feature and might change at any time');

tmpdir.refresh();
const wasmDir = path.join(__dirname, '..', 'wasi', 'wasm');
const wasiPreview1 = new WASI({
  version: 'preview1',
  args: ['foo', '-bar', '--baz=value'],
  env: process.env,
  preopens: {
    '/sandbox': fixtures.path('wasi'),
    '/tmp': tmpdir.path,
  },
  returnOnExit: returnOnExitEnvToValue(process.env),
});

// Validate the getImportObject helper
assert.strictEqual(wasiPreview1.wasiImport,
                    wasiPreview1.getImportObject().wasi_snapshot_preview1);
const modulePathPreview1 = path.join(wasmDir, `${process.argv[2]}.wasm`);
const bufferPreview1 = fs.readFileSync(modulePathPreview1);

(async () => {
  const { instance: instancePreview1 } =
    await WebAssembly.instantiate(bufferPreview1,
                                  wasiPreview1.getImportObject());

  wasiPreview1.start(instancePreview1);
})().then(common.mustCall());
