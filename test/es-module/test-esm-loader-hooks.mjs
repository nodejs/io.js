import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';

describe('Loader hooks', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('are called with all expected arguments', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('es-module-loaders/hooks-input.mjs'),
      fixtures.path('es-modules/json-modules.mjs'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');
    assert.match(lines[0], /{"url":"file:\/\/\/.*\/json-modules\.mjs","format":"test","shortCircuit":true}/);
    assert.match(lines[1], /{"source":{"type":"Buffer","data":\[.*\]},"format":"module","shortCircuit":true}/);
    assert.match(lines[2], /{"url":"file:\/\/\/.*\/experimental\.json","format":"test","shortCircuit":true}/);
    assert.match(lines[3], /{"source":{"type":"Buffer","data":\[.*\]},"format":"json","shortCircuit":true}/);
    assert.strictEqual(lines[4], '');
    assert.strictEqual(lines.length, 5);
  });

  it('are called with all expected arguments using register function', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader=data:text/javascript,',
      '--input-type=module',
      '--eval',
      "import { register } from 'node:module';" +
      `register(${JSON.stringify(fixtures.fileURL('es-module-loaders/hooks-input.mjs'))});` +
      `await import(${JSON.stringify(fixtures.fileURL('es-modules/json-modules.mjs'))});`,
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');
    assert.match(lines[0], /{"url":"file:\/\/\/.*\/json-modules\.mjs","format":"test","shortCircuit":true}/);
    assert.match(lines[1], /{"source":{"type":"Buffer","data":\[.*\]},"format":"module","shortCircuit":true}/);
    assert.match(lines[2], /{"url":"file:\/\/\/.*\/experimental\.json","format":"test","shortCircuit":true}/);
    assert.match(lines[3], /{"source":{"type":"Buffer","data":\[.*\]},"format":"json","shortCircuit":true}/);
    assert.strictEqual(lines[4], '');
    assert.strictEqual(lines.length, 5);
  });

  describe('should handle never-settling hooks in ESM files', { concurrency: !process.env.TEST_PARALLEL }, () => {
    it('top-level await of a never-settling resolve without warning', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/never-resolve.mjs'),
      ]);

      assert.strictEqual(stderr, '');
      assert.match(stdout, /^should be output\r?\n$/);
      assert.strictEqual(code, 13);
      assert.strictEqual(signal, null);
    });

    it('top-level await of a never-settling resolve with warning', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/never-resolve.mjs'),
      ]);

      assert.match(stderr, /Warning: Detected unsettled top-level await at.+never-resolve\.mjs:5/);
      assert.match(stdout, /^should be output\r?\n$/);
      assert.strictEqual(code, 13);
      assert.strictEqual(signal, null);
    });

    it('top-level await of a never-settling load without warning', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/never-load.mjs'),
      ]);

      assert.strictEqual(stderr, '');
      assert.match(stdout, /^should be output\r?\n$/);
      assert.strictEqual(code, 13);
      assert.strictEqual(signal, null);
    });

    it('top-level await of a never-settling load with warning', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/never-load.mjs'),
      ]);

      assert.match(stderr, /Warning: Detected unsettled top-level await at.+never-load\.mjs:5/);
      assert.match(stdout, /^should be output\r?\n$/);
      assert.strictEqual(code, 13);
      assert.strictEqual(signal, null);
    });

    it('top-level await of a race of never-settling hooks', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/race.mjs'),
      ]);

      assert.strictEqual(stderr, '');
      assert.match(stdout, /^true\r?\n$/);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('import.meta.resolve of a never-settling resolve', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/import.meta.never-resolve.mjs'),
      ]);

      assert.strictEqual(stderr, '');
      assert.match(stdout, /^should be output\r?\n$/);
      assert.strictEqual(code, 13);
      assert.strictEqual(signal, null);
    });
  });

  describe('should handle never-settling hooks in CJS files', { concurrency: !process.env.TEST_PARALLEL }, () => {
    it('never-settling resolve', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/never-resolve.cjs'),
      ]);

      assert.strictEqual(stderr, '');
      assert.match(stdout, /^should be output\r?\n$/);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });


    it('never-settling load', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/never-load.cjs'),
      ]);

      assert.strictEqual(stderr, '');
      assert.match(stdout, /^should be output\r?\n$/);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('race of never-settling hooks', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/never-settling-resolve-step/loader.mjs'),
        fixtures.path('es-module-loaders/never-settling-resolve-step/race.cjs'),
      ]);

      assert.strictEqual(stderr, '');
      assert.match(stdout, /^true\r?\n$/);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });
  });

  it('should work without worker permission', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-permission',
      '--allow-fs-read',
      '*',
      '--experimental-loader',
      fixtures.fileURL('empty.js'),
      fixtures.path('es-modules/esm-top-level-await.mjs'),
    ]);

    assert.strictEqual(stderr, '');
    assert.match(stdout, /^1\r?\n2\r?\n$/);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should allow loader hooks to spawn workers when allowed by the CLI flags', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-permission',
      '--allow-worker',
      '--allow-fs-read',
      '*',
      '--experimental-loader',
      `data:text/javascript,import{Worker}from"worker_threads";new Worker(${encodeURIComponent(JSON.stringify(fixtures.path('empty.js')))}).unref()`,
      fixtures.path('es-modules/esm-top-level-await.mjs'),
    ]);

    assert.strictEqual(stderr, '');
    assert.match(stdout, /^1\r?\n2\r?\n$/);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should not allow loader hooks to spawn workers if restricted by the CLI flags', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-permission',
      '--allow-fs-read',
      '*',
      '--experimental-loader',
      `data:text/javascript,import{Worker}from"worker_threads";new Worker(${encodeURIComponent(JSON.stringify(fixtures.path('empty.js')))}).unref()`,
      fixtures.path('es-modules/esm-top-level-await.mjs'),
    ]);

    assert.match(stderr, /code: 'ERR_ACCESS_DENIED'/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('should not leak internals or expose import.meta.resolve', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('es-module-loaders/loader-edge-cases.mjs'),
      fixtures.path('empty.js'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should be fine to call `process.exit` from a custom async hook', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      'data:text/javascript,export function load(a,b,next){if(a==="data:exit")process.exit(42);return next(a,b)}',
      '--input-type=module',
      '--eval',
      'import "data:exit"',
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 42);
    assert.strictEqual(signal, null);
  });

  it('should be fine to call `process.exit` from a custom sync hook', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      'data:text/javascript,export function resolve(a,b,next){if(a==="exit:")process.exit(42);return next(a,b)}',
      '--input-type=module',
      '--eval',
      'import "data:text/javascript,import.meta.resolve(%22exit:%22)"',
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 42);
    assert.strictEqual(signal, null);
  });

  it('should be fine to call `process.exit` from the loader thread top-level', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      'data:text/javascript,process.exit(42)',
      fixtures.path('empty.js'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 42);
    assert.strictEqual(signal, null);
  });

  describe('should handle a throwing top-level body', () => {
    it('should handle regular Error object', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw new Error("error message")',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /Error: error message\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle null', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw null',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\nnull\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle undefined', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw undefined',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\nundefined\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle boolean', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw true',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\ntrue\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle empty plain object', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw {}',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\n\{\}\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle plain object', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw {fn(){},symbol:Symbol("symbol"),u:undefined}',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\n\{ fn: \[Function: fn\], symbol: Symbol\(symbol\), u: undefined \}\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle number', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw 1',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\n1\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle bigint', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw 1n',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\n1\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle string', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw "literal string"',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\nliteral string\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle symbol', async () => {
      const { code, signal, stdout } = await spawnPromisified(execPath, [
        '--experimental-loader',
        'data:text/javascript,throw Symbol("symbol descriptor")',
        fixtures.path('empty.js'),
      ]);

      // Throwing a symbol doesn't produce any output
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle function', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        'data:text/javascript,throw function fnName(){}',
        fixtures.path('empty.js'),
      ]);

      assert.match(stderr, /\n\[Function: fnName\]\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });
  });

  describe('globalPreload', () => {
    it('should emit warning', async () => {
      const { stderr } = await spawnPromisified(execPath, [
        '--experimental-loader',
        'data:text/javascript,export function globalPreload(){}',
        '--experimental-loader',
        'data:text/javascript,export function globalPreload(){return""}',
        fixtures.path('empty.js'),
      ]);

      assert.strictEqual(stderr.match(/`globalPreload` has been removed; use `initialize` instead/g).length, 1);
    });

    it('should not emit warning when initialize is supplied', async () => {
      const { stderr } = await spawnPromisified(execPath, [
        '--experimental-loader',
        'data:text/javascript,export function globalPreload(){}export function initialize(){}',
        fixtures.path('empty.js'),
      ]);

      assert.doesNotMatch(stderr, /`globalPreload` has been removed; use `initialize` instead/);
    });
  });

  it('should be fine to call `process.removeAllListeners("beforeExit")` from the main thread', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      'data:text/javascript,export function load(a,b,c){return new Promise(d=>setTimeout(()=>d(c(a,b)),99))}',
      '--input-type=module',
      '--eval',
      'setInterval(() => process.removeAllListeners("beforeExit"),1).unref();await import("data:text/javascript,")',
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  describe('`initialize`/`register`', () => {
    it('should invoke `initialize` correctly', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--experimental-loader',
        fixtures.fileURL('es-module-loaders/hooks-initialize.mjs'),
        '--input-type=module',
        '--eval',
        'import os from "node:os";',
      ]);

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout.split('\n'), ['hooks initialize 1', '']);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should allow communicating with loader via `register` ports', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--input-type=module',
        '--eval',
        `
        import {MessageChannel} from 'node:worker_threads';
        import {register} from 'node:module';
        import {once} from 'node:events';
        const {port1, port2} = new MessageChannel();
        port1.on('message', (msg) => {
          console.log('message', msg);
        });
        const result = register(
          ${JSON.stringify(fixtures.fileURL('es-module-loaders/hooks-initialize-port.mjs'))},
          {data: port2, transferList: [port2]},
        );
        console.log('register', result);

        const timeout = setTimeout(() => {}, 2**31 - 1); // to keep the process alive.
        await Promise.all([
          once(port1, 'message').then(() => once(port1, 'message')),
          import('node:os'),
        ]);
        clearTimeout(timeout);
        port1.close();
        `,
      ]);

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout.split('\n'), [ 'register undefined',
                                                   'message initialize',
                                                   'message resolve node:os',
                                                   '' ]);

      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should have `register` accept URL objects as `parentURL`', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--import',
        `data:text/javascript,${encodeURIComponent(
          'import{ register } from "node:module";' +
          'import { pathToFileURL } from "node:url";' +
          'register("./hooks-initialize.mjs", pathToFileURL("./"));'
        )}`,
        '--input-type=module',
        '--eval',
        `
        import {register} from 'node:module';
        register(
          ${JSON.stringify(fixtures.fileURL('es-module-loaders/loader-load-foo-or-42.mjs'))},
          new URL('data:'),
        );

        import('node:os').then((result) => {
          console.log(JSON.stringify(result));
        });
        `,
      ], { cwd: fixtures.fileURL('es-module-loaders/') });

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout.split('\n').sort(), ['hooks initialize 1', '{"default":"foo"}', ''].sort());

      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should have `register` work with cjs', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--input-type=commonjs',
        '--eval',
        `
        'use strict';
        const {register} = require('node:module');
        register(
          ${JSON.stringify(fixtures.fileURL('es-module-loaders/hooks-initialize.mjs'))},
        );
        register(
          ${JSON.stringify(fixtures.fileURL('es-module-loaders/loader-load-foo-or-42.mjs'))},
        );

        import('node:os').then((result) => {
          console.log(JSON.stringify(result));
        });
        `,
      ]);

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout.split('\n').sort(), ['hooks initialize 1', '{"default":"foo"}', ''].sort());

      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('`register` should work with `require`', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--require',
        fixtures.path('es-module-loaders/register-loader.cjs'),
        '--input-type=module',
        '--eval',
        'import "node:os";',
      ]);

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout.split('\n'), ['resolve passthru', 'resolve passthru', '']);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('`register` should work with `import`', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--import',
        fixtures.fileURL('es-module-loaders/register-loader.mjs'),
        '--input-type=module',
        '--eval',
        'import "node:os"',
      ]);

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout.split('\n'), ['resolve passthru', '']);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should execute `initialize` in sequence', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--input-type=module',
        '--eval',
        `
        import {register} from 'node:module';
        console.log('result 1', register(
          ${JSON.stringify(fixtures.fileURL('es-module-loaders/hooks-initialize.mjs'))}
        ));
        console.log('result 2', register(
          ${JSON.stringify(fixtures.fileURL('es-module-loaders/hooks-initialize.mjs'))}
        ));

        await import('node:os');
        `,
      ]);

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout.split('\n'), [ 'hooks initialize 1',
                                                   'result 1 undefined',
                                                   'hooks initialize 2',
                                                   'result 2 undefined',
                                                   '' ]);
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    });

    it('should handle `initialize` returning never-settling promise', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--input-type=module',
        '--eval',
        `
        import {register} from 'node:module';
        register('data:text/javascript,export function initialize(){return new Promise(()=>{})}');
        `,
      ]);

      assert.strictEqual(stderr, '');
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 13);
      assert.strictEqual(signal, null);
    });

    it('should handle `initialize` returning rejecting promise', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--input-type=module',
        '--eval',
        `
        import {register} from 'node:module';
        register('data:text/javascript,export function initialize(){return Promise.reject()}');
        `,
      ]);

      assert.match(stderr, /undefined\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should handle `initialize` throwing null', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--input-type=module',
        '--eval',
        `
        import {register} from 'node:module';
        register('data:text/javascript,export function initialize(){throw null}');
        `,
      ]);

      assert.match(stderr, /null\r?\n/);
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });

    it('should be fine to call `process.exit` from a initialize hook', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--input-type=module',
        '--eval',
        `
        import {register} from 'node:module';
        register('data:text/javascript,export function initialize(){process.exit(42);}');
        `,
      ]);

      assert.strictEqual(stderr, '');
      assert.strictEqual(stdout, '');
      assert.strictEqual(code, 42);
      assert.strictEqual(signal, null);
    });
  });

  it('should use CJS loader to respond to require.resolve calls by default', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('es-module-loaders/loader-resolve-passthru.mjs'),
      fixtures.path('require-resolve.js'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, 'resolve passthru\n');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should use ESM loader to respond to require.resolve calls when opting in', async () => {
    const readFile = async () => {};
    const fileURLToPath = () => {};
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      `data:text/javascript,import{readFile}from"node:fs/promises";import{fileURLToPath}from"node:url";export ${
        async function load(url, context, nextLoad) {
          const result = await nextLoad(url, context);
          if (url.endsWith('/common/index.js')) {
            result.source = '"use strict";module.exports=require("node:module").createRequire(' +
                     `${JSON.stringify(url)})(${JSON.stringify(fileURLToPath(url))});\n`;
          } else if (url.startsWith('file:') && (context.format == null || context.format === 'commonjs')) {
            result.source = await readFile(new URL(url));
          }
          return result;
        }}`,
      '--experimental-loader',
      fixtures.fileURL('es-module-loaders/loader-resolve-passthru.mjs'),
      fixtures.path('require-resolve.js'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, 'resolve passthru\n'.repeat(10));
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  describe('should use hooks', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--import',
      fixtures.fileURL('es-module-loaders/builtin-named-exports.mjs'),
      fixtures.path('es-modules/require-esm-throws-with-loaders.js'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should support source maps in commonjs translator', async () => {
    const readFile = async () => {};
    const hook = `
    import { readFile } from 'node:fs/promises';
    export ${
  async function load(url, context, nextLoad) {
    const resolved = await nextLoad(url, context);
    if (context.format === 'commonjs') {
      resolved.source = await readFile(new URL(url));
    }
    return resolved;
  }
}`;

    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--enable-source-maps',
      '--import',
      `data:text/javascript,${encodeURIComponent(`
      import{ register } from "node:module";
      register(${
  JSON.stringify('data:text/javascript,' + encodeURIComponent(hook))
});
      `)}`,
      fixtures.path('source-map/throw-on-require.js'),
    ]);

    assert.strictEqual(stdout, '');
    assert.match(stderr, /throw-on-require\.ts:9:9/);
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('should handle mixed of opt-in modules and non-opt-in ones', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      `data:text/javascript,const fixtures=${JSON.stringify(fixtures.path('empty.js'))};export ${
        encodeURIComponent(function resolve(s, c, n) {
          if (s.endsWith('entry-point')) {
            return {
              shortCircuit: true,
              url: 'file:///c:/virtual-entry-point',
              format: 'commonjs',
            };
          }
          return n(s, c);
        })
      }export ${
        encodeURIComponent(async function load(u, c, n) {
          if (u === 'file:///c:/virtual-entry-point') {
            return {
              shortCircuit: true,
              source: `"use strict";require(${JSON.stringify(fixtures)});console.log("Hello");`,
              format: 'commonjs',
            };
          }
          return n(u, c);
        })}`,
      'entry-point',
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, 'Hello\n');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });
});
