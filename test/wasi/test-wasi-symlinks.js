'use strict';
const common = require('../common');
const fs = require('fs');
const path = require('path');

if (process.argv[2] === 'wasi-child') {
  common.expectWarning('ExperimentalWarning',
                       'WASI is an experimental feature and might change at any time');

  const { WASI } = require('wasi');
  const wasmDir = path.join(__dirname, 'wasm');
  const wasi = new WASI({
    version: 'preview1',
    args: [],
    env: process.env,
    preopens: {
      '/sandbox': process.argv[4],
      '/tmp': process.argv[5],
    },
  });
  const importObject = { wasi_snapshot_preview1: wasi.wasiImport };
  const modulePath = path.join(wasmDir, `${process.argv[3]}.wasm`);
  const buffer = fs.readFileSync(modulePath);

  (async () => {
    const { instance } = await WebAssembly.instantiate(buffer, importObject);

    wasi.start(instance);
  })().then(common.mustCall());
} else {
  if (!common.canCreateSymLink()) {
    common.skip('insufficient privileges');
  }

  const assert = require('assert');
  const cp = require('child_process');
  const tmpdir = require('../common/tmpdir');

  // Setup the sandbox environment.
  tmpdir.refresh();
  const sandbox = tmpdir.resolve('sandbox');
  const sandboxedFile = path.join(sandbox, 'input.txt');
  const externalFile = tmpdir.resolve('outside.txt');
  const sandboxedDir = path.join(sandbox, 'subdir');
  const sandboxedFileInSubdir = path.join(sandboxedDir, 'input-in-subdir.txt');
  const sandboxedSymlink = path.join(sandboxedDir, 'input_link.txt');
  const escapingSymlink = path.join(sandboxedDir, 'outside.txt');
  const loopSymlink1 = path.join(sandboxedDir, 'loop1');
  const loopSymlink2 = path.join(sandboxedDir, 'loop2');
  const sandboxedTmp = tmpdir.resolve('tmp');

  fs.mkdirSync(sandbox);
  fs.mkdirSync(sandboxedDir);
  fs.mkdirSync(sandboxedTmp);
  fs.writeFileSync(sandboxedFile, 'hello from input.txt', 'utf8');
  fs.writeFileSync(sandboxedFileInSubdir, 'hello from input in subdir.txt',
                   'utf8');
  fs.writeFileSync(externalFile, 'this should be inaccessible', 'utf8');
  fs.symlinkSync(path.join('.', 'input-in-subdir.txt'),
                 sandboxedSymlink, 'file');
  fs.symlinkSync(path.join('..', '..', 'outside.txt'), escapingSymlink, 'file');
  fs.symlinkSync('loop2', loopSymlink1, 'file');
  fs.symlinkSync('loop1', loopSymlink2, 'file');

  function runWASI(options) {
    console.log('executing', options.test);
    const opts = { env: { ...process.env, NODE_DEBUG_NATIVE: 'wasi' } };
    const child = cp.spawnSync(process.execPath, [
      __filename,
      'wasi-child',
      options.test,
      sandbox,
      sandboxedTmp,
    ], opts);
    console.log(child.stderr.toString());
    assert.strictEqual(child.status, 0);
    assert.strictEqual(child.signal, null);
    assert.strictEqual(child.stdout.toString(), options.stdout || '');
  }

  runWASI({ test: 'create_symlink',
            stdout: 'hello from input in subdir.txt' });
  runWASI({ test: 'follow_symlink',
            stdout: 'hello from input in subdir.txt' });
  runWASI({ test: 'link' });
  runWASI({ test: 'symlink_escape' });
  runWASI({ test: 'symlink_loop' });
}
