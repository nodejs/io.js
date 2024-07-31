import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.js';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';

// This test ensures that the register function can register loaders
// programmatically.

const commonArgs = [
  '--no-warnings',
  '--input-type=module',
  '--loader=data:text/javascript,',
];

const commonEvals = {
  import: (module) => `await import(${JSON.stringify(module)});`,
  register: (loader, parentURL = 'file:///') => `register(${JSON.stringify(loader)}, ${JSON.stringify(parentURL)});`,
  dynamicImport: (module) => `await import(${JSON.stringify(`data:text/javascript,${encodeURIComponent(module)}`)});`,
  staticImport: (module) => `import ${JSON.stringify(`data:text/javascript,${encodeURIComponent(module)}`)};`,
};

describe('ESM: programmatically register loaders', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('works with only a dummy CLI argument', async () => {
    const parentURL = fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs');
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      ...commonArgs,
      '--eval',
      "import { register } from 'node:module';" +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs')) +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs')) +
      `register(${JSON.stringify('./loader-resolve-passthru.mjs')}, ${JSON.stringify({ parentURL })});` +
      `register(${JSON.stringify('./loader-load-passthru.mjs')}, ${JSON.stringify({ parentURL })});` +
      commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');

    assert.match(lines[0], /resolve passthru/);
    assert.match(lines[1], /resolve passthru/);
    assert.match(lines[2], /load passthru/);
    assert.match(lines[3], /load passthru/);
    assert.match(lines[4], /Hello from dynamic import/);

    assert.strictEqual(lines[5], '');
  });

  describe('registering via --import', { concurrency: !process.env.TEST_PARALLEL }, () => {
    for (const moduleType of ['mjs', 'cjs']) {
      it(`should programmatically register a loader from a ${moduleType.toUpperCase()} file`, async () => {
        const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
          ...commonArgs,
          '--import', fixtures.fileURL('es-module-loaders', `register-loader.${moduleType}`).href,
          '--eval', commonEvals.staticImport('console.log("entry point")'),
        ]);

        assert.strictEqual(stderr, '');
        assert.strictEqual(code, 0);
        assert.strictEqual(signal, null);

        const [
          passthruStdout,
          entryPointStdout,
        ] = stdout.split('\n');

        assert.match(passthruStdout, /resolve passthru/);
        assert.match(entryPointStdout, /entry point/);
      });
    }
  });

  it('programmatically registered loaders are appended to an existing chaining', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      ...commonArgs,
      '--loader',
      fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
      '--eval',
      "import { register } from 'node:module';" +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs')) +
      commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');

    assert.match(lines[0], /resolve passthru/);
    assert.match(lines[1], /resolve passthru/);
    assert.match(lines[2], /load passthru/);
    assert.match(lines[3], /Hello from dynamic import/);

    assert.strictEqual(lines[4], '');
  });

  it('works registering loaders across files', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      ...commonArgs,
      '--eval',
      commonEvals.import(fixtures.fileURL('es-module-loaders', 'register-programmatically-loader-load.mjs')) +
      commonEvals.import(fixtures.fileURL('es-module-loaders', 'register-programmatically-loader-resolve.mjs')) +
      commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');

    assert.match(lines[0], /resolve passthru/);
    assert.match(lines[1], /load passthru/);
    assert.match(lines[2], /Hello from dynamic import/);

    assert.strictEqual(lines[3], '');
  });

  it('works registering loaders across virtual files', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      ...commonArgs,
      '--eval',
      commonEvals.import(fixtures.fileURL('es-module-loaders', 'register-programmatically-loader-load.mjs')) +
      commonEvals.dynamicImport(
        commonEvals.import(fixtures.fileURL('es-module-loaders', 'register-programmatically-loader-resolve.mjs')) +
        commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
      ),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');

    assert.match(lines[0], /resolve passthru/);
    assert.match(lines[1], /load passthru/);
    assert.match(lines[2], /Hello from dynamic import/);

    assert.strictEqual(lines[3], '');
  });

  it('works registering the same loaders more them once', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      ...commonArgs,
      '--eval',
      "import { register } from 'node:module';" +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs')) +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs')) +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs')) +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs')) +
      commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');

    assert.match(lines[0], /resolve passthru/);
    assert.match(lines[1], /resolve passthru/);
    assert.match(lines[2], /load passthru/);
    assert.match(lines[3], /load passthru/);
    assert.match(lines[4], /Hello from dynamic import/);

    assert.strictEqual(lines[5], '');
  });

  it('works registering loaders as package name', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      ...commonArgs,
      '--eval',
      "import { register } from 'node:module';" +
      commonEvals.register('resolve', fixtures.fileURL('es-module-loaders', 'package.json')) +
      commonEvals.register('load', fixtures.fileURL('es-module-loaders', 'package.json')) +
      commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');

    // Resolve occurs twice because it is first used to resolve the `load` loader
    // _AND THEN_ the `register` module.
    assert.match(lines[0], /resolve passthru/);
    assert.match(lines[1], /resolve passthru/);
    assert.match(lines[2], /load passthru/);
    assert.match(lines[3], /Hello from dynamic import/);

    assert.strictEqual(lines[4], '');
  });

  it('works without a CLI flag', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--input-type=module',
      '--eval',
      "import { register } from 'node:module';" +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'loader-load-passthru.mjs')) +
      commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);

    const lines = stdout.split('\n');

    assert.match(lines[0], /load passthru/);
    assert.match(lines[1], /Hello from dynamic import/);

    assert.strictEqual(lines[2], '');
  });

  it('does not work with a loader specifier that does not exist', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      ...commonArgs,
      '--eval',
      "import { register } from 'node:module';" +
      commonEvals.register('./not-found.mjs', import.meta.url) +
      commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
    ]);

    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
    assert.match(stderr, /ERR_MODULE_NOT_FOUND/);
  });

  it('does not work with a loader that got syntax errors', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      ...commonArgs,
      '--eval',
      "import { register } from 'node:module';" +
      commonEvals.register(fixtures.fileURL('es-module-loaders', 'syntax-error.mjs')) +
      commonEvals.dynamicImport('console.log("Hello from dynamic import");'),
    ]);

    assert.strictEqual(stdout, '');
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
    assert.match(stderr, /SyntaxError/);
  });
});
