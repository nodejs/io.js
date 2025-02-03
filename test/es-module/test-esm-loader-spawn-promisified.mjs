import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';

describe('Loader hooks throwing errors', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('throws on nonexistent modules', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      'import "nonexistent/file.mjs"',
    ]);

    assert.match(stderr, /ERR_MODULE_NOT_FOUND/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('throws on unknown extensions', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      `import ${JSON.stringify(fixtures.fileURL('/es-modules/file.unknown'))}`,
    ]);

    assert.match(stderr, /ERR_UNKNOWN_FILE_EXTENSION/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('throws on invalid return values', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      'import "esmHook/badReturnVal.mjs"',
    ]);

    assert.match(stderr, /ERR_INVALID_RETURN_VALUE/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('throws on boolean false', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      'import "esmHook/format.false"',
    ]);

    assert.match(stderr, /ERR_INVALID_RETURN_PROPERTY_VALUE/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });
  it('throws on boolean true', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      'import "esmHook/format.true"',
    ]);

    assert.match(stderr, /ERR_INVALID_RETURN_PROPERTY_VALUE/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('throws on invalid returned object', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      'import "esmHook/badReturnFormatVal.mjs"',
    ]);

    assert.match(stderr, /ERR_INVALID_RETURN_PROPERTY_VALUE/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('throws on unsupported format', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      'import "esmHook/unsupportedReturnFormatVal.mjs"',
    ]);

    assert.match(stderr, /ERR_UNKNOWN_MODULE_FORMAT/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('throws on invalid format property type', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      'import "esmHook/badReturnSourceVal.mjs"',
    ]);

    assert.match(stderr, /ERR_INVALID_RETURN_PROPERTY_VALUE/);
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it('rejects dynamic imports for all of the error cases checked above', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      `import assert from 'node:assert';
      await Promise.allSettled([
        import('nonexistent/file.mjs'),
        import(${JSON.stringify(fixtures.fileURL('/es-modules/file.unknown'))}),
        import('esmHook/badReturnVal.mjs'),
        import('esmHook/format.false'),
        import('esmHook/format.true'),
        import('esmHook/badReturnFormatVal.mjs'),
        import('esmHook/unsupportedReturnFormatVal.mjs'),
        import('esmHook/badReturnSourceVal.mjs'),
      ]).then((results) => {
        assert.strictEqual(results.every((result) => result.status === 'rejected'), true);
      })`,
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });
});

describe('Loader hooks parsing modules', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('can parse .js files as ESM', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      `import assert from 'node:assert';
      await import(${JSON.stringify(fixtures.fileURL('/es-module-loaders/js-as-esm.js'))})
      .then((parsedModule) => {
        assert.strictEqual(typeof parsedModule, 'object');
        assert.strictEqual(parsedModule.namedExport, 'named-export');
      })`,
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('can define .ext files as ESM', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      `import assert from 'node:assert';
      await import(${JSON.stringify(fixtures.fileURL('/es-modules/file.ext'))})
      .then((parsedModule) => {
        assert.strictEqual(typeof parsedModule, 'object');
        const { default: defaultExport } = parsedModule;
        assert.strictEqual(typeof defaultExport, 'function');
        assert.strictEqual(defaultExport.name, 'iAmReal');
        assert.strictEqual(defaultExport(), true);
      })`,
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('can predetermine the format in the custom loader resolve hook', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      `import assert from 'node:assert';
      await import('esmHook/preknownFormat.pre')
      .then((parsedModule) => {
        assert.strictEqual(typeof parsedModule, 'object');
        assert.strictEqual(parsedModule.default, 'hello world');
      })`,
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('can provide source for a nonexistent file', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      `import assert from 'node:assert';
      await import('esmHook/virtual.mjs')
      .then((parsedModule) => {
        assert.strictEqual(typeof parsedModule, 'object');
        assert.strictEqual(typeof parsedModule.default, 'undefined');
        assert.strictEqual(parsedModule.message, 'WOOHOO!');
      })`,
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('ensures that loaders have a separate context from userland', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      `import assert from 'node:assert';
      await import(${JSON.stringify(fixtures.fileURL('/es-modules/stateful.mjs'))})
      .then(({ default: count }) => {
        assert.strictEqual(count(), 1);
      });`,
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('ensures that user loaders are not bound to the internal loader', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/loader-this-value-inside-hook-functions.mjs'),
      '--input-type=module',
      '--eval',
      ';', // Actual test is inside the loader module.
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('throw maximum call stack error on the loader', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('/es-module-loaders/hooks-custom.mjs'),
      '--input-type=module',
      '--eval',
      'await import("esmHook/maximumCallStack.mjs")',
    ]);

    assert(stderr.includes('Maximum call stack size exceeded'));
    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });
});
