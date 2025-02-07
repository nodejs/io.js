'use strict';

const common = require('../common');
const assert = require('assert');
const _module = require('module'); // Avoid collision with globalThis.module

// Current directory gets highest priority for local modules
function testFirstInPath(moduleName, isLocalModule) {
  const assertFunction = isLocalModule ?
    assert.strictEqual :
    assert.notStrictEqual;

  let paths = _module._resolveLookupPaths(moduleName);

  assertFunction(paths[0], '.');

  paths = _module._resolveLookupPaths(moduleName, null);
  assertFunction(paths?.[0], '.');
}

testFirstInPath('./lodash', true);

// Relative path on Windows, but a regular file name elsewhere
testFirstInPath('.\\lodash', common.isWindows);
