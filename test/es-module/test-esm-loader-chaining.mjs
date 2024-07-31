import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';

const setupArgs = [
  '--no-warnings',
  '--input-type=module',
  '--eval',
];
const commonInput = 'import os from "node:os"; console.log(os)';
const commonArgs = [
  ...setupArgs,
  commonInput,
];

describe('ESM: loader chaining', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should load unadulterated source when there are no loaders', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        ...setupArgs,
        'import fs from "node:fs"; console.log(typeof fs?.constants?.F_OK )',
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(stderr, '');
    assert.match(stdout, /number/); // node:fs is an object
    assert.strictEqual(code, 0);
  });

  it('should load properly different source when only load changes something', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(stderr, '');
    assert.match(stdout, /load passthru/);
    assert.match(stdout, /resolve passthru/);
    assert.match(stdout, /foo/);
    assert.strictEqual(code, 0);
  });

  it('should result in proper output from multiple changes in resolve hooks', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-shortcircuit.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-foo.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-42.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(stderr, '');
    assert.match(stdout, /resolve 42/); // It did go thru resolve-42
    assert.match(stdout, /foo/); // LIFO, so resolve-foo won
    assert.strictEqual(code, 0);
  });

  it('should respect modified context within resolve chain', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-shortcircuit.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-42.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-receiving-modified-context.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-passing-modified-context.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(stderr, '');
    assert.match(stdout, /bar/);
    assert.strictEqual(code, 0);
  });

  it('should accept only the correct arguments', async () => {
    const { stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-log-args.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-with-too-many-args.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.match(stdout, /^resolve arg count: 3$/m);
    assert.match(stdout, /specifier: 'node:os'/);
    assert.match(stdout, /next: \[AsyncFunction: nextResolve\]/);

    assert.match(stdout, /^load arg count: 3$/m);
    assert.match(stdout, /url: 'node:os'/);
    assert.match(stdout, /next: \[AsyncFunction: nextLoad\]/);
  });

  it('should result in proper output from multiple changes in resolve hooks', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-shortcircuit.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-42.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-foo.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(stderr, '');
    assert.match(stdout, /resolve foo/); // It did go thru resolve-foo
    assert.match(stdout, /42/); // LIFO, so resolve-42 won
    assert.strictEqual(code, 0);
  });

  it('should provide the correct "next" fn when multiple calls to next within same loader', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-shortcircuit.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-foo.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-multiple-next-calls.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    const countFoos = stdout.match(/resolve foo/g)?.length;

    assert.strictEqual(stderr, '');
    assert.strictEqual(countFoos, 2);
    assert.strictEqual(code, 0);
  });

  it('should use the correct `name` for next<HookName>\'s function', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-shortcircuit.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-42.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(stderr, '');
    assert.match(stdout, /next<HookName>: nextResolve/);
    assert.strictEqual(code, 0);
  });

  it('should throw for incomplete resolve chain, citing errant loader & hook', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-incomplete.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );
    assert.match(stdout, /resolve passthru/);
    assert.match(stderr, /ERR_LOADER_CHAIN_INCOMPLETE/);
    assert.match(stderr, /loader-resolve-incomplete\.mjs/);
    assert.match(stderr, /'resolve'/);
    assert.strictEqual(code, 1);
  });

  it('should NOT throw when nested resolve hook signaled a short circuit', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-shortcircuit.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-next-modified.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout.trim(), 'foo');
    assert.strictEqual(code, 0);
  });

  it('should NOT throw when nested load hook signaled a short circuit', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-shortcircuit.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-42.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-next-modified.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(stderr, '');
    assert.match(stdout, /421/);
    assert.strictEqual(code, 0);
  });

  it('should allow loaders to influence subsequent loader resolutions', async () => {
    const { code, stderr } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-strip-xxx.mjs'),
        '--loader',
        'xxx/loader-resolve-strip-yyy.mjs',
        ...commonArgs,
      ],
      { encoding: 'utf8', cwd: fixtures.path('es-module-loaders') },
    );

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
  });

  it('should throw when the resolve chain is broken', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-incomplete.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-foo-or-42.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.doesNotMatch(stdout, /resolve passthru/);
    assert.match(stderr, /ERR_LOADER_CHAIN_INCOMPLETE/);
    assert.match(stderr, /loader-resolve-incomplete\.mjs/);
    assert.match(stderr, /'resolve'/);
    assert.strictEqual(code, 1);
  });

  it('should throw for incomplete load chain, citing errant loader & hook', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-incomplete.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.match(stdout, /load passthru/);
    assert.match(stderr, /ERR_LOADER_CHAIN_INCOMPLETE/);
    assert.match(stderr, /loader-load-incomplete\.mjs/);
    assert.match(stderr, /'load'/);
    assert.strictEqual(code, 1);
  });

  it('should throw when the load chain is broken', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-incomplete.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.doesNotMatch(stdout, /load passthru/);
    assert.match(stderr, /ERR_LOADER_CHAIN_INCOMPLETE/);
    assert.match(stderr, /loader-load-incomplete\.mjs/);
    assert.match(stderr, /'load'/);
    assert.strictEqual(code, 1);
  });

  it('should throw when invalid `specifier` argument passed to `nextResolve`', async () => {
    const { code, stderr } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-bad-next-specifier.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(code, 1);
    assert.match(stderr, /ERR_INVALID_ARG_TYPE/);
    assert.match(stderr, /loader-resolve-bad-next-specifier\.mjs/);
    assert.match(stderr, /'resolve' hook's nextResolve\(\) specifier/);
  });

  it('should throw when resolve hook is invalid', async () => {
    const { code, stderr } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-null-return.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(code, 1);
    assert.match(stderr, /ERR_INVALID_RETURN_VALUE/);
    assert.match(stderr, /loader-resolve-null-return\.mjs/);
    assert.match(stderr, /'resolve' hook's nextResolve\(\)/);
    assert.match(stderr, /an object/);
    assert.match(stderr, /got null/);
  });

  it('should throw when invalid `context` argument passed to `nextResolve`', async () => {
    const { code, stderr } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-bad-next-context.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.match(stderr, /ERR_INVALID_ARG_TYPE/);
    assert.match(stderr, /loader-resolve-bad-next-context\.mjs/);
    assert.match(stderr, /'resolve' hook's nextResolve\(\) context/);
    assert.strictEqual(code, 1);
  });

  it('should throw when load hook is invalid', async () => {
    const { code, stderr } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-null-return.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.strictEqual(code, 1);
    assert.match(stderr, /ERR_INVALID_RETURN_VALUE/);
    assert.match(stderr, /loader-load-null-return\.mjs/);
    assert.match(stderr, /'load' hook's nextLoad\(\)/);
    assert.match(stderr, /an object/);
    assert.match(stderr, /got null/);
  });

  it('should throw when invalid `url` argument passed to `nextLoad`', async () => {
    const { code, stderr } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-bad-next-url.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.match(stderr, /ERR_INVALID_ARG_TYPE/);
    assert.match(stderr, /loader-load-bad-next-url\.mjs/);
    assert.match(stderr, /'load' hook's nextLoad\(\) url/);
    assert.strictEqual(code, 1);
  });

  it('should throw when invalid `url` argument passed to `nextLoad`', async () => {
    const { code, stderr } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-impersonating-next-url.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );

    assert.match(stderr, /ERR_INVALID_ARG_VALUE/);
    assert.match(stderr, /loader-load-impersonating-next-url\.mjs/);
    assert.match(stderr, /'load' hook's nextLoad\(\) url/);
    assert.strictEqual(code, 1);
  });

  it('should throw when invalid `context` argument passed to `nextLoad`', async () => {
    const { code, stderr } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-bad-next-context.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );
    assert.match(stderr, /ERR_INVALID_ARG_TYPE/);
    assert.match(stderr, /loader-load-bad-next-context\.mjs/);
    assert.match(stderr, /'load' hook's nextLoad\(\) context/);
    assert.strictEqual(code, 1);
  });

  it('should allow loaders to influence subsequent loader `import()` calls in `resolve`', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-strip-xxx.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-dynamic-import.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );
    assert.strictEqual(stderr, '');
    assert.match(stdout, /resolve dynamic import/); // It did go thru resolve-dynamic
    assert.strictEqual(code, 0);
  });

  it('should allow loaders to influence subsequent loader `import()` calls in `load`', async () => {
    const { code, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-resolve-strip-xxx.mjs'),
        '--loader',
        fixtures.fileURL('es-module-loaders', 'loader-load-dynamic-import.mjs'),
        ...commonArgs,
      ],
      { encoding: 'utf8' },
    );
    assert.strictEqual(stderr, '');
    assert.match(stdout, /load dynamic import/); // It did go thru load-dynamic
    assert.strictEqual(code, 0);
  });
});
