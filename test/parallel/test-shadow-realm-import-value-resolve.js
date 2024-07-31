// Flags: --experimental-shadow-realm
'use strict';
const common = require('../common');
const assert = require('assert');
const path = require('path');

common.skipIfWorker('process.chdir is not supported in workers.');

async function main() {
  const realm = new ShadowRealm();

  const dirname = __dirname;
  // Set process cwd to the parent directory of __dirname.
  const cwd = path.dirname(dirname);
  process.chdir(cwd);
  // Hardcode the relative path to ensure the string is still a valid relative
  // URL string.
  const relativePath = './fixtures/es-module-shadow-realm/re-export-state-counter.mjs';

  // Make sure that the module can not be resolved relative to __filename.
  assert.throws(() => require.resolve(relativePath), { code: 'MODULE_NOT_FOUND' });

  // Resolve relative to the current working directory.
  const getCounter = await realm.importValue(relativePath, 'getCounter');
  assert.strictEqual(typeof getCounter, 'function');
}

main().then(common.mustCall());
