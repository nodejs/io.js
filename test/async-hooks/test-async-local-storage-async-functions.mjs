import '../common/index.mjs';
import { strictEqual, notStrictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

async function foo() {}

const asyncLocalStorage = new AsyncLocalStorage();

async function testOut() {
  await foo();
  strictEqual(asyncLocalStorage.getStore(), undefined);
}

async function testAwait() {
  await foo();
  notStrictEqual(asyncLocalStorage.getStore(), undefined);
  strictEqual(asyncLocalStorage.getStore().get('key'), 'value');
  await asyncLocalStorage.exit(testOut);
}

asyncLocalStorage.run(new Map(), () => {
  const store = asyncLocalStorage.getStore();
  store.set('key', 'value');
  testAwait(); // should not reject
});
strictEqual(asyncLocalStorage.getStore(), undefined);
