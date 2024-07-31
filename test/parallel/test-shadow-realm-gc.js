// Flags: --experimental-shadow-realm --max-old-space-size=20
'use strict';

/**
 * Verifying ShadowRealm instances can be correctly garbage collected.
 */

const common = require('../common');
const { runAndBreathe } = require('../common/gc');
const assert = require('assert');
const { isMainThread, Worker } = require('worker_threads');

runAndBreathe(() => {
  const realm = new ShadowRealm();
  realm.evaluate('new TextEncoder(); 1;');
}, 100).then(common.mustCall());

// Test it in worker too.
if (isMainThread) {
  const worker = new Worker(__filename);
  worker.on('exit', common.mustCall((code) => {
    assert.strictEqual(code, 0);
  }));
}
