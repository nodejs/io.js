import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

const asyncLocalStorage = new AsyncLocalStorage();
const asyncLocalStorage2 = new AsyncLocalStorage();

setTimeout(() => {
  asyncLocalStorage.run(new Map(), () => {
    asyncLocalStorage2.run(new Map(), () => {
      const store = asyncLocalStorage.getStore();
      const store2 = asyncLocalStorage2.getStore();
      store.set('hello', 'world');
      store2.set('hello', 'foo');
      setTimeout(() => {
        strictEqual(asyncLocalStorage.getStore().get('hello'), 'world');
        strictEqual(asyncLocalStorage2.getStore().get('hello'), 'foo');
        asyncLocalStorage.exit(() => {
          strictEqual(asyncLocalStorage.getStore(), undefined);
          strictEqual(asyncLocalStorage2.getStore().get('hello'), 'foo');
        });
        strictEqual(asyncLocalStorage.getStore().get('hello'), 'world');
        strictEqual(asyncLocalStorage2.getStore().get('hello'), 'foo');
      }, 200);
    });
  });
}, 100);

setTimeout(() => {
  asyncLocalStorage.run(new Map(), () => {
    const store = asyncLocalStorage.getStore();
    store.set('hello', 'earth');
    setTimeout(() => {
      strictEqual(asyncLocalStorage.getStore().get('hello'), 'earth');
    }, 100);
  });
}, 100);
