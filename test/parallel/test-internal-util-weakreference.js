// Flags: --expose-internals --expose-gc
'use strict';
require('../common');
const { gcUntil } = require('../common/gc');
const assert = require('assert');
const { WeakReference } = require('internal/util');

let obj = { hello: 'world' };
const ref = new WeakReference(obj);
assert.strictEqual(ref.get(), obj);

async function main() {
  obj = null;
  await gcUntil(
    'Reference is garbage collected',
    () => ref.get() === undefined);
}

main();
