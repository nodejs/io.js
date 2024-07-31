// Flags: --experimental-shadow-realm --max-old-space-size=20
'use strict';

/**
 * Verifying modules imported by ShadowRealm instances can be correctly
 * garbage collected.
 */

const common = require('../common');
const fixtures = require('../common/fixtures');
const { runAndBreathe } = require('../common/gc');

const mod = fixtures.fileURL('es-module-shadow-realm', 'state-counter.mjs');

runAndBreathe(async () => {
  const realm = new ShadowRealm();
  await realm.importValue(mod, 'getCounter');
}, 100).then(common.mustCall());
