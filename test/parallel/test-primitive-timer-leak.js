'use strict';
// Flags: --expose-gc
require('../common');
const { onGC } = require('../common/gc');

// See https://github.com/nodejs/node/issues/53335
const poller = setInterval(() => {
  globalThis.gc();
}, 100);

let count = 0;

for (let i = 0; i < 10; i++) {
  const timer = setTimeout(() => {}, 0);
  onGC(timer, {
    ongc: () => {
      if (++count === 10) {
        clearInterval(poller);
      }
    }
  });
  console.log(+timer);
}
