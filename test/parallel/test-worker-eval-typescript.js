'use strict';
require('../common');
const assert = require('assert');
const { Worker } = require('worker_threads');
const { test } = require('node:test');
const { once } = require('events');

const esmHelloWorld = `
    import worker from 'worker_threads';
    const foo: string = 'Hello, World!';
    worker.parentPort.postMessage(foo);
`;

const cjsHelloWorld = `
    const { parentPort } = require('worker_threads');
    const foo: string = 'Hello, World!';
    parentPort.postMessage(foo);
`;

const disableTypeScriptWarningFlag = '--disable-warning=ExperimentalWarning';

test('Worker eval module typescript without input-type', async () => {
  const w = new Worker(esmHelloWorld, { eval: true, execArgv: [disableTypeScriptWarningFlag] });
  assert.deepStrictEqual(await once(w, 'message'), ['Hello, World!']);
});

test('Worker eval module typescript with --input-type=module-typescript', async () => {
  const w = new Worker(esmHelloWorld, { eval: true, execArgv: ['--input-type=module-typescript',
                                                               disableTypeScriptWarningFlag] });
  assert.deepStrictEqual(await once(w, 'message'), ['Hello, World!']);
});

test('Worker eval module typescript with --input-type=commonjs-typescript', async () => {
  const w = new Worker(esmHelloWorld, { eval: true, execArgv: ['--input-type=commonjs-typescript',
                                                               disableTypeScriptWarningFlag] });

  const [err] = await once(w, 'error');
  assert.strictEqual(err.name, 'SyntaxError');
  assert.match(err.message, /Cannot use import statement outside a module/);
});

test('Worker eval module typescript with --input-type=module', async () => {
  const w = new Worker(esmHelloWorld, { eval: true, execArgv: ['--input-type=module',
                                                               disableTypeScriptWarningFlag] });
  const [err] = await once(w, 'error');
  assert.strictEqual(err.name, 'SyntaxError');
  assert.match(err.message, /Missing initializer in const declaration/);
});

test('Worker eval commonjs typescript without input-type', async () => {
  const w = new Worker(cjsHelloWorld, { eval: true, execArgv: [disableTypeScriptWarningFlag] });
  assert.deepStrictEqual(await once(w, 'message'), ['Hello, World!']);
});

test('Worker eval commonjs typescript with --input-type=commonjs-typescript', async () => {
  const w = new Worker(cjsHelloWorld, { eval: true, execArgv: ['--input-type=commonjs-typescript',
                                                               disableTypeScriptWarningFlag] });
  assert.deepStrictEqual(await once(w, 'message'), ['Hello, World!']);
});

test('Worker eval commonjs typescript with --input-type=module-typescript', async () => {
  const w = new Worker(cjsHelloWorld, { eval: true, execArgv: ['--input-type=module-typescript',
                                                               disableTypeScriptWarningFlag] });
  const [err] = await once(w, 'error');
  assert.strictEqual(err.name, 'ReferenceError');
  assert.match(err.message, /require is not defined in ES module scope, you can use import instead/);
});
