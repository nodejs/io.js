// Flags: --expose-internals --experimental-permission --allow-fs-read=test/common* --allow-fs-read=tools* --allow-fs-read=test/parallel* --allow-child-process
'use strict';

const common = require('../common');
common.skipIfWorker();

if (!common.hasCrypto) {
  common.skip('no crypto');
}

const { internalBinding } = require('internal/test/binding');
const assert = require('node:assert');
const path = require('node:path');
const fixtures = require('../common/fixtures');

const blockedFile = fixtures.path('permission', 'deny', 'protected-file.md');
const internalFsBinding = internalBinding('fs');

// Run this inside a for loop to trigger the fast API
for (let i = 0; i < 10_000; i++) {
  assert.throws(() => {
    internalFsBinding.internalModuleStat(internalFsBinding, blockedFile);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  });
}
