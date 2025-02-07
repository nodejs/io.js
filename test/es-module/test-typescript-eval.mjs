import { skip, spawnPromisified } from '../common/index.mjs';
import { doesNotMatch, match, strictEqual } from 'node:assert';
import { test } from 'node:test';

if (!process.config.variables.node_use_amaro) skip('Requires Amaro');

test('eval TypeScript ESM syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--eval',
    `import util from 'node:util'
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`]);

  match(result.stderr, /Type Stripping is an experimental feature and might change at any time/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('eval TypeScript ESM syntax with input-type module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=module-typescript',
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
    '--eval',
    `const util = require('node:util');
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`]);
  match(result.stdout, /Hello, TypeScript!/);
  match(result.stderr, /ExperimentalWarning: Type Stripping is an experimental/);
  strictEqual(result.code, 0);
});

test('eval TypeScript CommonJS syntax with input-type commonjs-typescript', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=commonjs-typescript',
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
    '--eval',
    `import util from 'node:util'
    const text: string = 'Hello, TypeScript!'
    console.log(text);`]);
  match(result.stderr, /ExperimentalWarning: Type Stripping is an experimental/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('expect fail eval TypeScript CommonJS syntax with input-type module-typescript', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=module-typescript',
    '--eval',
    `const util = require('node:util');
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`]);

  strictEqual(result.stdout, '');
  match(result.stderr, /require is not defined in ES module scope, you can use import instead/);
  strictEqual(result.code, 1);
});

test('expect fail eval TypeScript ESM syntax with input-type commonjs-typescript', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=commonjs-typescript',
    '--eval',
    `import util from 'node:util'
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`]);
  strictEqual(result.stdout, '');
  match(result.stderr, /Cannot use import statement outside a module/);
  strictEqual(result.code, 1);
});

test('check syntax error is thrown when passing unsupported syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--eval',
    'enum Foo { A, B, C }']);
  strictEqual(result.stdout, '');
  match(result.stderr, /SyntaxError/);
  doesNotMatch(result.stderr, /ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX/);
  strictEqual(result.code, 1);
});

test('check syntax error is thrown when passing unsupported syntax with --input-type=module-typescript', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=module-typescript',
    '--eval',
    'enum Foo { A, B, C }']);
  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX/);
  strictEqual(result.code, 1);
});

test('check syntax error is thrown when passing unsupported syntax with --input-type=commonjs-typescript', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=commonjs-typescript',
    '--eval',
    'enum Foo { A, B, C }']);
  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX/);
  strictEqual(result.code, 1);
});

test('should not parse TypeScript with --type-module=commonjs', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=commonjs',
    '--eval',
    `enum Foo {}`]);

  strictEqual(result.stdout, '');
  match(result.stderr, /SyntaxError/);
  doesNotMatch(result.stderr, /ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX/);
  strictEqual(result.code, 1);
});

test('should not parse TypeScript with --type-module=module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=module',
    '--eval',
    `enum Foo {}`]);

  strictEqual(result.stdout, '');
  match(result.stderr, /SyntaxError/);
  doesNotMatch(result.stderr, /ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX/);
  strictEqual(result.code, 1);
});

test('check warning is emitted when eval TypeScript CommonJS syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--eval',
    `const util = require('node:util');
    const text: string = 'Hello, TypeScript!'
    console.log(util.styleText('red', text));`]);
  match(result.stderr, /ExperimentalWarning: Type Stripping is an experimental/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('code is throwing a non Error is rethrown', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--eval',
    `throw null;`]);
  doesNotMatch(result.stderr, /node:internal\/process\/execution/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('code is throwing an error with customized accessors', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--eval',
    `throw Object.defineProperty(new Error, "stack", { set() {throw this} });`]);

  match(result.stderr, /Error/);
  match(result.stderr, /at \[eval\]:1:29/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('typescript code is throwing an error', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--eval',
    `const foo: string =  'Hello, TypeScript!'; throw new Error(foo);`]);

  match(result.stderr, /Hello, TypeScript!/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('typescript ESM code is throwing a syntax error at runtime', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--eval',
    'import util from "node:util"; function foo(){}; throw new SyntaxError(foo<Number>(1));']);
  // Trick by passing ambiguous syntax to see if evaluated in TypeScript or JavaScript
  // If evaluated in JavaScript `foo<Number>(1)` is evaluated as `foo < Number > (1)`
  // result in false
  // If evaluated in TypeScript `foo<Number>(1)` is evaluated as `foo(1)`
  match(result.stderr, /SyntaxError: false/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('typescript CJS code is throwing a syntax error at runtime', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--eval',
    'const util = require("node:util"); function foo(){}; throw new SyntaxError(foo<Number>(1));']);
  // Trick by passing ambiguous syntax to see if evaluated in TypeScript or JavaScript
  // If evaluated in JavaScript `foo<Number>(1)` is evaluated as `foo < Number > (1)`
  // result in false
  // If evaluated in TypeScript `foo<Number>(1)` is evaluated as `foo(1)`
  match(result.stderr, /SyntaxError: false/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('check syntax error is thrown when passing invalid syntax with --input-type=commonjs-typescript', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=commonjs-typescript',
    '--eval',
    'function foo(){ await Promise.resolve(1); }']);
  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_INVALID_TYPESCRIPT_SYNTAX/);
  strictEqual(result.code, 1);
});

test('check syntax error is thrown when passing invalid syntax with --input-type=module-typescript', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--input-type=module-typescript',
    '--eval',
    'function foo(){ await Promise.resolve(1); }']);
  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_INVALID_TYPESCRIPT_SYNTAX/);
  strictEqual(result.code, 1);
});
