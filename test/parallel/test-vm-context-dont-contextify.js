'use strict';

// Check vm.constants.DONT_CONTEXTIFY works.

const common = require('../common');

const assert = require('assert');
const vm = require('vm');
const fixtures = require('../common/fixtures');

{
  // Check identity of the returned object.
  const context = vm.createContext(vm.constants.DONT_CONTEXTIFY);
  // The globalThis in the new context should be reference equal to the returned object.
  assert.strictEqual(vm.runInContext('globalThis', context), context);
  assert(vm.isContext(context));
  assert.strictEqual(typeof context.Array, 'function');  // Can access builtins directly.
  assert.deepStrictEqual(Object.keys(context), []);  // Properties on the global proxy are not enumerable
}

{
  // Check that vm.createContext can return the original context if re-passed.
  const context = vm.createContext(vm.constants.DONT_CONTEXTIFY);
  const context2 = new vm.createContext(context);
  assert.strictEqual(context, context2);
}

{
  // Check that the context is vanilla and that Script.runInContext works.
  const context = vm.createContext(vm.constants.DONT_CONTEXTIFY);
  const result =
    new vm.Script('globalThis.hey = 1; Object.freeze(globalThis); globalThis.process')
      .runInContext(context);
  assert.strictEqual(globalThis.hey, undefined);  // Should not leak into current context.
  assert.strictEqual(result, undefined);  // Vanilla context has no Node.js globals
}

{
  // Check Script.runInNewContext works.
  const result =
    new vm.Script('globalThis.hey = 1; Object.freeze(globalThis); globalThis.process')
      .runInNewContext(vm.constants.DONT_CONTEXTIFY);
  assert.strictEqual(globalThis.hey, undefined);  // Should not leak into current context.
  assert.strictEqual(result, undefined);  // Vanilla context has no Node.js globals
}

{
  // Check that vm.runInNewContext() works
  const result = vm.runInNewContext(
    'globalThis.hey = 1; Object.freeze(globalThis); globalThis.process',
    vm.constants.DONT_CONTEXTIFY);
  assert.strictEqual(globalThis.hey, undefined);  // Should not leak into current context.
  assert.strictEqual(result, undefined);  // Vanilla context has no Node.js globals
}

{
  // Check that the global object of vanilla contexts work as expected.
  const context = vm.createContext(vm.constants.DONT_CONTEXTIFY);

  // Check mutation via globalThis.
  vm.runInContext('globalThis.foo = 1;', context);
  assert.strictEqual(globalThis.foo, undefined);  // Should not pollute the current context.
  assert.strictEqual(context.foo, 1);
  assert.strictEqual(vm.runInContext('globalThis.foo', context), 1);
  assert.strictEqual(vm.runInContext('foo', context), 1);

  // Check mutation from outside.
  context.foo = 2;
  assert.strictEqual(context.foo, 2);
  assert.strictEqual(vm.runInContext('globalThis.foo', context), 2);
  assert.strictEqual(vm.runInContext('foo', context), 2);

  // Check contextual mutation.
  vm.runInContext('bar = 1;', context);
  assert.strictEqual(globalThis.bar, undefined);  // Should not pollute the current context.
  assert.strictEqual(context.bar, 1);
  assert.strictEqual(vm.runInContext('globalThis.bar', context), 1);
  assert.strictEqual(vm.runInContext('bar', context), 1);

  // Check adding new property from outside.
  context.baz = 1;
  assert.strictEqual(context.baz, 1);
  assert.strictEqual(vm.runInContext('globalThis.baz', context), 1);
  assert.strictEqual(vm.runInContext('baz', context), 1);

  // Check mutation via Object.defineProperty().
  vm.runInContext('Object.defineProperty(globalThis, "qux", {' +
    'enumerable: false, configurable: false, get() { return 1; } })', context);
  assert.strictEqual(globalThis.qux, undefined);  // Should not pollute the current context.
  assert.strictEqual(context.qux, 1);
  assert.strictEqual(vm.runInContext('qux', context), 1);
  const desc = Object.getOwnPropertyDescriptor(context, 'qux');
  assert.strictEqual(desc.enumerable, false);
  assert.strictEqual(desc.configurable, false);
  assert.strictEqual(typeof desc.get, 'function');
  assert.throws(() => { context.qux = 1; }, { name: 'TypeError' });
  assert.throws(() => { Object.defineProperty(context, 'qux', { value: 1 }); }, { name: 'TypeError' });
  // Setting a value without a setter fails silently.
  assert.strictEqual(vm.runInContext('qux = 2; qux', context), 1);
  assert.throws(() => {
    vm.runInContext('Object.defineProperty(globalThis, "qux", { value: 1 });');
  }, { name: 'TypeError' });
}

function checkFrozen(context) {
  // Check mutation via globalThis.
  vm.runInContext('globalThis.foo = 1', context);  // Invoking setters on freezed object fails silently.
  assert.strictEqual(context.foo, undefined);
  assert.strictEqual(vm.runInContext('globalThis.foo', context), undefined);
  assert.throws(() => {
    vm.runInContext('foo', context);  // It should not be looked up contextually.
  }, {
    name: 'ReferenceError'
  });

  // Check mutation from outside.
  assert.throws(() => {
    context.foo = 2;
  }, { name: 'TypeError' });
  assert.strictEqual(context.foo, undefined);
  assert.strictEqual(vm.runInContext('globalThis.foo', context), undefined);
  assert.throws(() => {
    vm.runInContext('foo', context);  // It should not be looked up contextually.
  }, {
    name: 'ReferenceError'
  });

  // Check contextual mutation.
  vm.runInContext('bar = 1', context);  // Invoking setters on freezed object fails silently.
  assert.strictEqual(context.bar, undefined);
  assert.strictEqual(vm.runInContext('globalThis.bar', context), undefined);
  assert.throws(() => {
    vm.runInContext('bar', context);  // It should not be looked up contextually.
  }, {
    name: 'ReferenceError'
  });

  // Check mutation via Object.defineProperty().
  assert.throws(() => {
    vm.runInContext('Object.defineProperty(globalThis, "qux", {' +
      'enumerable: false, configurable: false, get() { return 1; } })', context);
  }, {
    name: 'TypeError'
  });
  assert.strictEqual(context.qux, undefined);
  assert.strictEqual(vm.runInContext('globalThis.qux', context), undefined);
  assert.strictEqual(Object.getOwnPropertyDescriptor(context, 'qux'), undefined);
  assert.throws(() => { Object.defineProperty(context, 'qux', { value: 1 }); }, { name: 'TypeError' });
  assert.throws(() => {
    vm.runInContext('qux', context);
  }, {
    name: 'ReferenceError'
  });
}

{
  // Check freezing the vanilla context's global object from within the context.
  const context = vm.createContext(vm.constants.DONT_CONTEXTIFY);
  // Only vanilla contexts' globals can be freezed. Contextified global objects cannot be freezed
  // due to the presence of interceptors.
  vm.runInContext('Object.freeze(globalThis)', context);
  checkFrozen(context);
}

{
  // Check freezing the vanilla context's global object from outside the context.
  const context = vm.createContext(vm.constants.DONT_CONTEXTIFY);
  Object.freeze(context);
  checkFrozen(context);
}

// Check importModuleDynamically works.
(async function() {
  {
    const moduleUrl = fixtures.fileURL('es-modules', 'message.mjs');
    const namespace = await import(moduleUrl.href);
    // Check dynamic import works
    const context = vm.createContext(vm.constants.DONT_CONTEXTIFY);
    const script = new vm.Script(`import(${JSON.stringify(moduleUrl)})`, {
      importModuleDynamically: vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER,
    });
    const promise = script.runInContext(context);
    assert.strictEqual(await promise, namespace);
  }
})().catch(common.mustNotCall());
