'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');

if (process.argv[2] === 'child') {
  process.abort();
} else {
  const child = cp.spawnSync(`${process.execPath}`, [`${__filename}`, 'child']);
  const stderr = child.stderr.toString();

  assert.strictEqual(child.stdout.toString(), '');
  const { nativeStack, jsStack } = common.getPrintedStackTrace(stderr);

  if (!nativeStack.every((frame, index) => frame.startsWith(`${index + 1}:`))) {
    assert.fail(`Each frame should start with a frame number:\n${stderr}`);
  }

  // For systems that don't support backtraces, the native stack is
  // going to be empty.
  if (!common.isWindows && nativeStack.length > 0) {
    const { getBinaryPath } = require('../common/shared-lib-util');
    if (!nativeStack.some((frame) => frame.includes(`[${getBinaryPath()}]`))) {
      assert.fail(`Some native stack frame include the binary name:\n${stderr}`);
    }
  }

  if (jsStack.length > 0) {
    assert(jsStack.some((frame) => frame.includes(__filename)));
  }
}
