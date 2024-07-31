'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const { spawnSyncAndExitWithoutError } = require('../common/child_process');

const commonArgs = [
  '--experimental-shadow-realm',
];

async function main() {
  // Verifies that --require preload modules are not enabled in the ShadowRealm.
  spawnSyncAndExitWithoutError(process.execPath, [
    ...commonArgs,
    '--require',
    fixtures.path('es-module-shadow-realm', 'preload.js'),
    fixtures.path('es-module-shadow-realm', 'preload-main.js'),
  ]);
}

main().then(common.mustCall());
