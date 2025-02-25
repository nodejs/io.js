import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

const asyncLocalStorage = new AsyncLocalStorage();
const outer = {};
const inner = {};

function testInner() {
  strictEqual(asyncLocalStorage.getStore(), outer);

  asyncLocalStorage.run(inner, () => {
    strictEqual(asyncLocalStorage.getStore(), inner);
  });
  strictEqual(asyncLocalStorage.getStore(), outer);

  asyncLocalStorage.exit(() => {
    strictEqual(asyncLocalStorage.getStore(), undefined);
  });
  strictEqual(asyncLocalStorage.getStore(), outer);
}

asyncLocalStorage.run(outer, testInner);
strictEqual(asyncLocalStorage.getStore(), undefined);
