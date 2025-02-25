// Flags: --expose_gc

// This test ensures that AsyncLocalStorage gets gced once it was disabled
// and no strong references remain in userland.

import { mustCall } from '../common/index.mjs';
import { AsyncLocalStorage } from 'async_hooks';
import onGC from '../common/ongc.js';

let asyncLocalStorage = new AsyncLocalStorage();

asyncLocalStorage.run({}, () => {
  asyncLocalStorage.disable();

  onGC(asyncLocalStorage, { ongc: mustCall() });
});

asyncLocalStorage = null;
globalThis.gc();
