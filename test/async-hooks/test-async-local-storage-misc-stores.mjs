import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

const asyncLocalStorage = new AsyncLocalStorage();

asyncLocalStorage.run('hello node', () => {
  strictEqual(asyncLocalStorage.getStore(), 'hello node');
});

const runStore = { hello: 'node' };
asyncLocalStorage.run(runStore, () => {
  strictEqual(asyncLocalStorage.getStore(), runStore);
});
