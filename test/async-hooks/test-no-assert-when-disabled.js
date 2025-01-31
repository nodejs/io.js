'use strict';
// Flags: --no-force-async-hooks-checks --expose-internals
const common = require('../common');
const { isMainThread } = require('worker_threads');
if (!isMainThread) {
  common.skip('Workers don\'t inherit per-env state like the check flag');
}

const async_hooks = require('internal/async_hooks');

// Negative asyncIds and invalid type name
async_hooks.emitInit(-1, null, -1, {});
async_hooks.emitBefore(-1, -1);
async_hooks.emitAfter(-1);
async_hooks.emitDestroy(-1);
