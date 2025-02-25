import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

async function main() {
  const asyncLocalStorage = new AsyncLocalStorage();
  const err = new Error();
  const next = () => Promise.resolve()
    .then(() => {
      strictEqual(asyncLocalStorage.getStore().get('a'), 1);
      throw err;
    });
  await new Promise((resolve, reject) => {
    asyncLocalStorage.run(new Map(), () => {
      const store = asyncLocalStorage.getStore();
      store.set('a', 1);
      next().then(resolve, reject);
    });
  })
    .catch((e) => {
      strictEqual(asyncLocalStorage.getStore(), undefined);
      strictEqual(e, err);
    });
  strictEqual(asyncLocalStorage.getStore(), undefined);
}

main();
