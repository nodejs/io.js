'use strict';

// Flags: --experimental-vm-modules

const common = require('../common');
const assert = require('assert');
const {
  Module,
  SourceTextModule,
  SyntheticModule,
  createContext,
  compileFunction,
} = require('vm');
const util = require('util');

(async function test1() {
  const context = createContext({
    foo: 'bar',
    baz: undefined,
    typeofProcess: undefined,
  });
  const m = new SourceTextModule(
    'baz = foo; typeofProcess = typeof process; typeof Object;',
    { context }
  );
  assert.strictEqual(m.status, 'unlinked');
  await m.link(common.mustNotCall());
  assert.strictEqual(m.status, 'linked');
  assert.strictEqual(await m.evaluate(), undefined);
  assert.strictEqual(m.status, 'evaluated');
  assert.deepStrictEqual(context, {
    foo: 'bar',
    baz: 'bar',
    typeofProcess: 'undefined'
  });
}().then(common.mustCall()));

(async () => {
  const m = new SourceTextModule(`
    globalThis.vmResultFoo = "foo";
    globalThis.vmResultTypeofProcess = Object.prototype.toString.call(process);
  `);
  await m.link(common.mustNotCall());
  await m.evaluate();
  assert.strictEqual(globalThis.vmResultFoo, 'foo');
  assert.strictEqual(globalThis.vmResultTypeofProcess, '[object process]');
  delete globalThis.vmResultFoo;
  delete globalThis.vmResultTypeofProcess;
})().then(common.mustCall());

(async () => {
  const m = new SourceTextModule('while (true) {}');
  await m.link(common.mustNotCall());
  await m.evaluate({ timeout: 500 })
    .then(() => assert(false), () => {});
})().then(common.mustCall());

// Check the generated identifier for each module
(async () => {
  const context1 = createContext({ });
  const context2 = createContext({ });

  const m1 = new SourceTextModule('1', { context: context1 });
  assert.strictEqual(m1.identifier, 'vm:module(0)');
  const m2 = new SourceTextModule('2', { context: context1 });
  assert.strictEqual(m2.identifier, 'vm:module(1)');
  const m3 = new SourceTextModule('3', { context: context2 });
  assert.strictEqual(m3.identifier, 'vm:module(0)');
})().then(common.mustCall());

// Check inspection of the instance
{
  const context = createContext({ foo: 'bar' });
  const m = new SourceTextModule('1', { context });

  assert.strictEqual(
    util.inspect(m),
    `SourceTextModule {
  status: 'unlinked',
  identifier: 'vm:module(0)',
  context: { foo: 'bar' }
}`
  );

  assert.strictEqual(util.inspect(m, { depth: -1 }), '[SourceTextModule]');

  for (const value of [null, { __proto__: null }, SourceTextModule.prototype]) {
    assert.throws(
      () => m[util.inspect.custom].call(value),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "this" argument must be an instance of Module/,
      },
    );
  }
}

{
  const context = createContext({ foo: 'bar' });
  const m = new SyntheticModule([], () => {}, { context });

  assert.strictEqual(
    util.inspect(m),
    `SyntheticModule {
  status: 'unlinked',
  identifier: 'vm:module(0)',
  context: { foo: 'bar' }
}`
  );

  assert.strictEqual(util.inspect(m, { depth: -1 }), '[SyntheticModule]');
}

// Check dependencies getter returns same object every time
{
  const m = new SourceTextModule('');
  const dep = m.dependencySpecifiers;
  assert.notStrictEqual(dep, undefined);
  assert.strictEqual(dep, m.dependencySpecifiers);
}

// Check the impossibility of creating an abstract instance of the Module.
{
  assert.throws(() => new Module(), {
    message: 'Module is not a constructor',
    name: 'TypeError'
  });
}

// Check to throws invalid exportNames
{
  assert.throws(() => new SyntheticModule(undefined, () => {}, {}), {
    message: 'The "exportNames" argument must be an ' +
        'Array of unique strings.' +
        ' Received undefined',
    name: 'TypeError'
  });
}

// Check to throws duplicated exportNames
// https://github.com/nodejs/node/issues/32806
{
  assert.throws(() => new SyntheticModule(['x', 'x'], () => {}, {}), {
    message: 'The property \'exportNames.x\' is duplicated. Received \'x\'',
    name: 'TypeError',
  });
}

// Check to throws invalid evaluateCallback
{
  assert.throws(() => new SyntheticModule([], undefined, {}), {
    message: 'The "evaluateCallback" argument must be of type function.' +
      ' Received undefined',
    name: 'TypeError'
  });
}

// Check to throws invalid options
{
  assert.throws(() => new SyntheticModule([], () => {}, null), {
    message: 'The "options" argument must be of type object.' +
      ' Received null',
    name: 'TypeError'
  });
}

// Test compileFunction importModuleDynamically
{
  const module = new SyntheticModule([], () => {});
  module.link(() => {});
  const f = compileFunction('return import("x")', [], {
    importModuleDynamically(specifier, referrer) {
      assert.strictEqual(specifier, 'x');
      assert.strictEqual(referrer, f);
      return module;
    },
  });
  f().then((ns) => {
    assert.strictEqual(ns, module.namespace);
  });
}
