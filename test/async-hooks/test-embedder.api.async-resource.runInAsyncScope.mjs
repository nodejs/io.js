import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncResource } from 'async_hooks';

// Ensure that asyncResource.makeCallback returns the callback return value.
const a = new AsyncResource('foobar');
const ret = a.runInAsyncScope(() => {
  return 1729;
});
strictEqual(ret, 1729);
