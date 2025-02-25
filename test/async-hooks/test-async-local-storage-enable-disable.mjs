import '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import { AsyncLocalStorage } from 'async_hooks';

const asyncLocalStorage = new AsyncLocalStorage();

asyncLocalStorage.run(new Map(), () => {
  asyncLocalStorage.getStore().set('foo', 'bar');
  process.nextTick(() => {
    strictEqual(asyncLocalStorage.getStore().get('foo'), 'bar');
    process.nextTick(() => {
      strictEqual(asyncLocalStorage.getStore(), undefined);
    });

    asyncLocalStorage.disable();
    strictEqual(asyncLocalStorage.getStore(), undefined);

    // Calls to exit() should not mess with enabled status
    asyncLocalStorage.exit(() => {
      strictEqual(asyncLocalStorage.getStore(), undefined);
    });
    strictEqual(asyncLocalStorage.getStore(), undefined);

    process.nextTick(() => {
      strictEqual(asyncLocalStorage.getStore(), undefined);
      asyncLocalStorage.run(new Map().set('bar', 'foo'), () => {
        strictEqual(asyncLocalStorage.getStore().get('bar'), 'foo');
      });
    });
  });
});
