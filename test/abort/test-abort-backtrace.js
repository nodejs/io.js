'use strict';
require('../common');
const assert = require('assert');
const cp = require('child_process');

function getPrintedStackTrace(stderr) {
  const lines = stderr.split('\n');

  let state = 'initial';
  const result = {
    message: [],
    nativeStack: [],
    jsStack: [],
  };
  for (let i = 0; i < lines.length; ++i) {
    const line = lines[i].trim();
    if (line.length === 0) {
      continue;  // Skip empty lines.
    }

    switch (state) {
      case 'initial':
        result.message.push(line);
        if (line.includes('Native stack trace')) {
          state = 'native-stack';
        } else {
          result.message.push(line);
        }
        break;
      case 'native-stack':
        if (line.includes('JavaScript stack trace')) {
          state = 'js-stack';
        } else {
          result.nativeStack.push(line);
        }
        break;
      case 'js-stack':
        result.jsStack.push(line);
        break;
    }
  }
  return result;
}

if (process.argv[2] === 'child') {
  process.abort();
} else {
  const child = cp.spawnSync(`${process.execPath}`, [`${__filename}`, 'child']);
  const stderr = child.stderr.toString();

  assert.strictEqual(child.stdout.toString(), '');
  const { nativeStack, jsStack } = getPrintedStackTrace(stderr);

  if (!nativeStack.every((frame, index) => frame.startsWith(`${index + 1}:`))) {
    assert.fail(`Each frame should start with a frame number:\n${stderr}`);
  }

  // For systems that don't support backtraces, the native stack is
  // going to be empty.
  if (process.platform !== 'win32' && nativeStack.length > 0) {
    const { getBinaryPath } = require('../common/shared-lib-util');
    if (!nativeStack.some((frame) => frame.includes(`[${getBinaryPath()}]`))) {
      assert.fail(`Some native stack frame include the binary name:\n${stderr}`);
    }
  }

  if (jsStack.length > 0) {
    assert(jsStack.some((frame) => frame.includes(__filename)));
  }
}
