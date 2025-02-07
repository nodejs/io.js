// Flags: --permission --allow-fs-read=*
'use strict';

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const { createRequire } = require('node:module');
const assert = require('node:assert');
const fixtures = require('../common/fixtures');
const loadFixture = createRequire(fixtures.path('node_modules'));
// When a permission is set by cli, the process shouldn't be able
// to require native addons unless --allow-addons is sent
{
  // doesNotThrow
  const msg = loadFixture('pkgexports/no-addons');
  assert.strictEqual(msg, 'not using native addons');
}
