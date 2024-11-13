'use strict';
// Flags: --expose-gc

const common = require('../common');
const { gcUntil } = require('../common/gc');

// On IBMi, the rss memory always returns zero
if (common.isIBMi)
  common.skip('On IBMi, the rss memory always returns zero');

const v8 = require('v8');

const before = process.memoryUsage.rss();

for (let i = 0; i < 1000000; i++) {
  v8.serialize('');
}

async function main() {
  await gcUntil('RSS should go down', () => {
    const after = process.memoryUsage.rss();
    if (common.isASan) {
      console.log(`ASan: before=${before} after=${after}`);
      return after < before * 10;
    } else if (process.config.variables.node_builtin_modules_path) {
      console.log(`node_builtin_modules_path: before=${before} after=${after}`);
      return after < before * 10;
    }
    console.log(`before=${before} after=${after}`);
    return after < before * 10;
  });
}

main();
