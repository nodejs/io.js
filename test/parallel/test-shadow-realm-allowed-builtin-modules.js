// Flags: --experimental-shadow-realm
'use strict';
const common = require('../common');
const assert = require('assert');

async function main() {
  // Verifies that builtin modules can not be imported in the ShadowRealm.
  const realm = new ShadowRealm();
  // The error object created inside the ShadowRealm with the error code
  // property is not copied on the realm boundary. Only the error message
  // is copied. Simply check the error message here.
  await assert.rejects(realm.importValue('fs', 'readFileSync'), {
    message: /Cannot find package 'fs'/,
  });
  // As above, we can only validate the error message, not the error code.
  await assert.rejects(realm.importValue('node:fs', 'readFileSync'), {
    message: /No such built-in module: node:fs/,
  });
}

main().then(common.mustCall());
