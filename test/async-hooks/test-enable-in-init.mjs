import { mustNotCall, mustCall } from '../common/index.mjs';
import { createHook } from 'async_hooks';
import { access } from 'fs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);

const nestedHook = createHook({
  init: mustNotCall()
});
let nestedCall = false;

createHook({
  init: mustCall(() => {
    nestedHook.enable();
    if (!nestedCall) {
      nestedCall = true;
      access(__filename, mustCall());
    }
  }, 2)
}).enable();

access(__filename, mustCall());
