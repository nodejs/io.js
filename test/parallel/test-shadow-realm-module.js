// Flags: --experimental-shadow-realm
'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');

async function main() {
  const realm = new ShadowRealm();
  const mod = fixtures.fileURL('es-module-shadow-realm', 'state-counter.mjs');
  const getCounter = await realm.importValue(mod, 'getCounter');
  assert.strictEqual(getCounter(), 0);
  const getCounter1 = await realm.importValue(mod, 'getCounter');
  // Returned value is a newly wrapped function.
  assert.notStrictEqual(getCounter, getCounter1);
  // Verify that the module state is shared between two `importValue` calls.
  assert.strictEqual(getCounter1(), 1);
  assert.strictEqual(getCounter(), 2);

  const { getCounter: getCounterThisRealm } = await import(mod);
  assert.notStrictEqual(getCounterThisRealm, getCounter);
  // Verify that the module state is not shared between two realms.
  assert.strictEqual(getCounterThisRealm(), 0);
  assert.strictEqual(getCounter(), 3);

  // Verify that shadow realm rejects to import a non-existing module.
  await assert.rejects(realm.importValue('non-exists', 'exports'), TypeError);
}

main().then(common.mustCall());
