import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

const asyncLocalStorage = new AsyncLocalStorage();

asyncLocalStorage.run({}, (runArg) => {
  strictEqual(runArg, 'foo');
  asyncLocalStorage.exit((exitArg) => {
    strictEqual(exitArg, 'bar');
  }, 'bar');
}, 'foo');
