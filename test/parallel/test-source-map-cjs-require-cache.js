// Flags: --enable-source-maps --max-old-space-size=10 --expose-gc

/**
 * This test verifies that the source map of a CJS module is cleared after the
 * CJS module is reclaimed by GC.
 */

'use strict';
require('../common');
const { gcUntil } = require('../common/gc');
const assert = require('node:assert');
const { findSourceMap } = require('node:module');

const moduleId = require.resolve('../fixtures/source-map/no-throw.js');
const moduleIdRepeat = require.resolve('../fixtures/source-map/no-throw2.js');

function run(moduleId) {
  require(moduleId);
  delete require.cache[moduleId];
  const idx = module.children.findIndex((child) => child.id === moduleId);
  assert.ok(idx >= 0);
  module.children.splice(idx, 1);

  // Verify that the source map is still available
  assert.notStrictEqual(findSourceMap(moduleId), undefined);
}

// Run the test in a function scope so that every variable can be reclaimed by GC.
run(moduleId);

// Run until the source map is cleared by GC, or fail the test after determined iterations.
gcUntil('SourceMap of deleted CJS module is cleared', () => {
  // Repetitively load a second module with --max-old-space-size=10 to make GC more aggressive.
  run(moduleIdRepeat);
  // Verify that the source map is cleared.
  return findSourceMap(moduleId) == null;
});
