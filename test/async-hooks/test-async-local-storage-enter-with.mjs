import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

const asyncLocalStorage = new AsyncLocalStorage();

setImmediate(() => {
  const store = { foo: 'bar' };
  asyncLocalStorage.enterWith(store);

  strictEqual(asyncLocalStorage.getStore(), store);
  setTimeout(() => {
    strictEqual(asyncLocalStorage.getStore(), store);
  }, 10);
});

setTimeout(() => {
  strictEqual(asyncLocalStorage.getStore(), undefined);
}, 10);
