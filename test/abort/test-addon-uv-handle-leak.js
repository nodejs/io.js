'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const cp = require('child_process');
const { spawnSync } = require('child_process');

// This is a sibling test to test/addons/uv-handle-leak.

const bindingPath = path.resolve(
  __dirname, '..', 'addons', 'uv-handle-leak', 'build',
  `${common.buildType}/binding.node`);

if (!fs.existsSync(bindingPath))
  common.skip('binding not built yet');

if (process.argv[2] === 'child') {

  const { Worker } = require('worker_threads');

  // The worker thread loads and then unloads `bindingPath`. Because of this the
  // symbols in `bindingPath` are lost when the worker thread quits, but the
  // number of open handles in the worker thread's event loop is assessed in the
  // main thread afterwards, and the names of the callbacks associated with the
  // open handles is retrieved at that time as well. Thus, we require
  // `bindingPath` here so that the symbols and their names survive the life
  // cycle of the worker thread.
  require(bindingPath);

  new Worker(`
  const binding = require(${JSON.stringify(bindingPath)});

  binding.leakHandle();
  binding.leakHandle(0);
  binding.leakHandle(0x42);
  `, { eval: true });
} else {
  const child = cp.spawnSync(process.execPath, [__filename, 'child']);
  const stderr = child.stderr.toString();

  assert.strictEqual(child.stdout.toString(), '');

  const lines = stderr.split('\n');

  let state = 'initial';

  // Parse output that is formatted like this:

  // uv loop at [0x559b65ed5770] has open handles:
  // [0x7f2de0018430] timer (active)
  //         Close callback: 0x7f2df31de220 CloseCallback(uv_handle_s*) [...]
  //         Data: 0x7f2df33df140 example_instance [...]
  //         (First field): 0x7f2df33dedc0 vtable for ExampleOwnerClass [...]
  // [0x7f2de000b870] timer
  //         Close callback: 0x7f2df31de220 CloseCallback(uv_handle_s*) [...]
  //         Data: (nil)
  // [0x7f2de000b910] timer
  //         Close callback: 0x7f2df31de220 CloseCallback(uv_handle_s*) [...]
  //         Data: 0x42
  // uv loop at [0x559b65ed5770] has 3 open handles in total

  function isGlibc() {
    try {
      const lddOut = spawnSync('ldd', [process.execPath]).stdout;
      const libcInfo = lddOut.toString().split('\n').map(
        (line) => line.match(/libc\.so.+=>\s*(\S+)\s/)).filter((info) => info);
      if (libcInfo.length === 0)
        return false;
      const nmOut = spawnSync('nm', ['-D', libcInfo[0][1]]).stdout;
      if (/gnu_get_libc_version/.test(nmOut))
        return true;
    } catch {
      return false;
    }
  }


  if (!(common.isFreeBSD ||
        common.isAIX ||
        common.isIBMi ||
        (common.isLinux && !isGlibc()) ||
        common.isWindows)) {
    assert(stderr.includes('ExampleOwnerClass'), stderr);
    assert(stderr.includes('CloseCallback'), stderr);
    assert(stderr.includes('example_instance'), stderr);
  }

  while (lines.length > 0) {
    const line = lines.shift().trim();
    if (line.length === 0) {
      continue;  // Skip empty lines.
    }

    switch (state) {
      case 'initial':
        assert.match(line, /^uv loop at \[.+\] has open handles:$/);
        state = 'handle-start';
        break;
      case 'handle-start':
        if (/^uv loop at \[.+\] has \d+ open handles in total$/.test(line)) {
          state = 'source-line';
          break;
        }
        assert.match(line, /^\[.+\] timer( \(active\))?$/);
        state = 'close-callback';
        break;
      case 'close-callback':
        assert.match(line, /^Close callback:/);
        state = 'data';
        break;
      case 'data':
        assert.match(line, /^Data: .+$/);
        state = 'maybe-first-field';
        break;
      case 'maybe-first-field':
        if (!/^\(First field\)/.test(line)) {
          lines.unshift(line);
        }
        state = 'handle-start';
        break;
      case 'source-line':
        assert.match(line, /CheckedUvLoopClose/);
        state = 'assertion-failure';
        break;
      case 'assertion-failure':
        assert.match(line, /Assertion failed:/);
        state = 'done';
        break;
      case 'done':
        break;
    }
  }

  assert.strictEqual(state, 'done');
}
