import { mustCall } from '../common/index.mjs';

import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';

// This test verifies that async local storage works with thenables

const store = new AsyncLocalStorage();
const data = Symbol('verifier');

const then = mustCall((cb) => {
  strictEqual(store.getStore(), data);
  setImmediate(cb);
}, 4);

function thenable() {
  return {
    then
  };
}

// Await a thenable
store.run(data, async () => {
  strictEqual(store.getStore(), data);
  await thenable();
  strictEqual(store.getStore(), data);
});

// Returning a thenable in an async function
store.run(data, async () => {
  try {
    strictEqual(store.getStore(), data);
    return thenable();
  } finally {
    strictEqual(store.getStore(), data);
  }
});

// Resolving a thenable
store.run(data, () => {
  strictEqual(store.getStore(), data);
  Promise.resolve(thenable());
  strictEqual(store.getStore(), data);
});

// Returning a thenable in a then handler
store.run(data, () => {
  strictEqual(store.getStore(), data);
  Promise.resolve().then(() => thenable());
  strictEqual(store.getStore(), data);
});
