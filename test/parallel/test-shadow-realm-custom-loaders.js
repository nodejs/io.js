'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');

const commonArgs = [
  '--experimental-shadow-realm',
  '--no-warnings',
];

async function main() {
  // Verifies that custom loaders are not enabled in the ShadowRealm.
  const child = await common.spawnPromisified(process.execPath, [
    ...commonArgs,
    '--experimental-loader',
    fixtures.fileURL('es-module-loaders', 'loader-resolve-shortcircuit.mjs'),
    '--experimental-loader',
    fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
    fixtures.path('es-module-shadow-realm', 'custom-loaders.js'),
  ]);

  assert.strictEqual(child.stderr, '');
  assert.strictEqual(child.code, 0);
}

main().then(common.mustCall());
