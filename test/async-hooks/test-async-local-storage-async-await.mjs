import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

const asyncLocalStorage = new AsyncLocalStorage();

async function test() {
  asyncLocalStorage.getStore().set('foo', 'bar');
  await Promise.resolve();
  strictEqual(asyncLocalStorage.getStore().get('foo'), 'bar');
}

async function main() {
  await asyncLocalStorage.run(new Map(), test);
  strictEqual(asyncLocalStorage.getStore(), undefined);
}

main();
