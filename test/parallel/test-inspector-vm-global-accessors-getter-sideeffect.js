'use strict';
const common = require('../common');
common.skipIfInspectorDisabled();

// Test that if there is a side effect in a getter invoked through the vm
// global proxy, Runtime.evaluate recognizes that.

const assert = require('assert');
const inspector = require('inspector');
const vm = require('vm');

const session = new inspector.Session();
session.connect();

const context = vm.createContext({
  get a() {
    globalThis.foo = '1';
    return 100;
  }
});

session.post('Runtime.evaluate', {
  expression: 'a',
  throwOnSideEffect: true,
  contextId: 2 // context's id
}, (error, res) => {
  assert.ifError(error);
  const { exception } = res.exceptionDetails;
  assert.strictEqual(exception.className, 'EvalError');
  assert.match(exception.description, /Possible side-effect/);

  assert(context);  // Keep 'context' alive and make linter happy.
});
