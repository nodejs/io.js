// Flags: --expose-internals --permission --allow-fs-read=test/common* --allow-fs-read=tools* --allow-fs-read=test/parallel* --allow-child-process
'use strict';

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

if (!common.hasCrypto) {
  common.skip('no crypto');
}

const { internalBinding } = require('internal/test/binding');
const fixtures = require('../common/fixtures');

const blockedFile = fixtures.path('permission', 'deny', 'protected-file.md');
const internalFsBinding = internalBinding('fs');

// Run this inside a for loop to trigger the fast API
for (let i = 0; i < 10_000; i++) {
  // internalModuleStat does not use permission model.
  // doesNotThrow
  internalFsBinding.internalModuleStat(internalFsBinding, blockedFile);
}
