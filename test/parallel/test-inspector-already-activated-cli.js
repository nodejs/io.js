// Flags: --inspect=0
'use strict';

const common = require('../common');
common.skipIfInspectorDisabled();

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');
const inspector = require('inspector');
const wsUrl = inspector.url();
assert(wsUrl.startsWith('ws://'));
assert.throws(() => {
  inspector.open(0, undefined, false);
}, {
  code: 'ERR_INSPECTOR_ALREADY_ACTIVATED'
});
assert.strictEqual(inspector.url(), wsUrl);
inspector.close();
assert.strictEqual(inspector.url(), undefined);
