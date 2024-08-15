// Flags: --experimental-require-module --abort-on-uncaught-exception

import { mustCall } from '../common/index.mjs';
const fn = mustCall(() => {
  console.log('hello');
});
fn();
