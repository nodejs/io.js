import { spawnPromisified } from '../common/index.mjs';
import { match, strictEqual } from 'node:assert';
import { test } from 'node:test';

test('eval TypeScript ESM syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=module',
    '--experimental-strip-types',
    '--eval',
    `import util from 'node:util'
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`]);

  match(result.stderr, /Type Stripping is an experimental feature and might change at any time/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('eval TypeScript CommonJS syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=commonjs',
    '--experimental-strip-types',
    '--eval',
    `const util = require('node:util');
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`,
    '--no-warnings']);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.stderr, '');
  strictEqual(result.code, 0);
});

test('eval TypeScript CommonJS syntax by default', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--eval',
    `const util = require('node:util');
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`,
    '--no-warnings']);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('TypeScript ESM syntax not specified', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--eval',
    `import util from 'node:util'
    const text: string = 'Hello, TypeScript!'
    console.log(text);`]);
  match(result.stderr, /ExperimentalWarning: Type Stripping is an experimental/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('expect fail eval TypeScript CommonJS syntax with input-type module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--input-type=module',
    '--eval',
    `const util = require('node:util');
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`]);

  strictEqual(result.stdout, '');
  match(result.stderr, /require is not defined in ES module scope, you can use import instead/);
  strictEqual(result.code, 1);
});

test('expect fail eval TypeScript CommonJS syntax with input-type module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--input-type=commonjs',
    '--eval',
    `import util from 'node:util'
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`]);
  strictEqual(result.stdout, '');
  match(result.stderr, /Cannot use import statement outside a module/);
  strictEqual(result.code, 1);
});
