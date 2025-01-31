// Flags: --expose-internals
'use strict';

// This test verifies that if the binary is compiled with code cache,
// and the cache is used when built in modules are compiled.
// Otherwise, verifies that no cache is used when compiling builtins.

require('../common');
const { isMainThread } = require('worker_threads');
const assert = require('assert');
const {
  internalBinding
} = require('internal/test/binding');
const {
  getCacheUsage,
  builtinCategories: { canBeRequired }
} = internalBinding('builtins');

for (const key of canBeRequired) {
  require(`node:${key}`);
}

// The computation has to be delayed until we have done loading modules
const {
  compiledWithoutCache,
  compiledWithCache,
  compiledInSnapshot
} = getCacheUsage();

function extractModules(list) {
  return list.filter((m) => m.startsWith('NativeModule'))
  .map((m) => m.replace('NativeModule ', ''));
}

const loadedModules = extractModules(process.moduleLoadList);

// Cross-compiled binaries do not have code cache, verifies that the builtins
// are all compiled without cache and we are doing the bookkeeping right.
if (!process.features.cached_builtins) {
  assert(!process.config.variables.node_use_node_code_cache ||
         process.execArgv.includes('--no-node-snapshot'));

  if (isMainThread) {
    assert.deepStrictEqual(compiledWithCache, new Set());
    for (const key of loadedModules) {
      assert(compiledWithoutCache.has(key),
             `"${key}" should've been compiled without code cache`);
    }
  } else {
    // TODO(joyeecheung): create a list of modules whose cache can be shared
    // from the main thread to the worker thread and check that their
    // cache are hit
    assert.notDeepStrictEqual(compiledWithCache, new Set());
  }
} else {  // Native compiled
  assert(process.config.variables.node_use_node_code_cache);

  const wrong = [];
  for (const key of loadedModules) {
    if (key.startsWith('internal/deps/v8/tools')) {
      continue;
    }
    if (!compiledWithCache.has(key) &&
      compiledInSnapshot.indexOf(key) === -1) {
      wrong.push(`"${key}" should've been compiled **with** code cache`);
    }
  }
  assert.strictEqual(wrong.length, 0, wrong.join('\n'));
}
