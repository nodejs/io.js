// Flags: --experimental-vm-modules --max-old-space-size=16 --trace-gc
'use strict';

// This tests that vm.SourceTextModule() does not leak.
// See: https://github.com/nodejs/node/issues/33439
const common = require('../common');
const { checkIfCollectableByCounting } = require('../common/gc');
const vm = require('vm');

const outer = 32;
const inner = 128;

checkIfCollectableByCounting(async (i) => {
  for (let j = 0; j < inner; j++) {
    // Try to reach the maximum old space size.
    const m = new vm.SourceTextModule(`
      const bar = new Array(512).fill("----");
      export { bar };
    `);
    await m.link(() => {});
    await m.evaluate();
  }
  return inner;
}, vm.SourceTextModule, outer).then(common.mustCall());
