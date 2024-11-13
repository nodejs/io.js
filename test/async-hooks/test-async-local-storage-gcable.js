'use strict';
// Flags: --expose_gc

// This test ensures that AsyncLocalStorage gets gced once it was disabled
// and no strong references remain in userland.

const common = require('../common');
const { AsyncLocalStorage } = require('async_hooks');
const { onGC } = require('../common/gc');

let asyncLocalStorage = new AsyncLocalStorage();

asyncLocalStorage.run({}, () => {
  asyncLocalStorage.disable();

  onGC(asyncLocalStorage, { ongc: common.mustCall() });
});

asyncLocalStorage = null;
global.gc();
