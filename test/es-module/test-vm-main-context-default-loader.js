'use strict';

const common = require('../common');

// Can't process.chdir() in worker.
common.skipIfWorker();

const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const url = require('url');
const fs = require('fs');
const {
  compileFunction,
  Script,
  createContext,
  constants: { USE_MAIN_CONTEXT_DEFAULT_LOADER },
} = require('vm');
const assert = require('assert');

common.expectWarning('ExperimentalWarning',
                     'vm.USE_MAIN_CONTEXT_DEFAULT_LOADER is an experimental feature and might change at any time');

assert(
  !process.execArgv.includes('--experimental-vm-modules'),
  'This test must be run without --experimental-vm-modules');

assert.strictEqual(typeof USE_MAIN_CONTEXT_DEFAULT_LOADER, 'symbol');

async function testNotFoundErrors(options) {
  // Import user modules.
  const script = new Script('import("./message.mjs")', options);
  // Use try-catch for better async stack traces in the logs.
  await assert.rejects(script.runInThisContext(), { code: 'ERR_MODULE_NOT_FOUND' });

  const imported = compileFunction('return import("./message.mjs")', [], options)();
  // Use try-catch for better async stack traces in the logs.
  await assert.rejects(imported, { code: 'ERR_MODULE_NOT_FOUND' });
}

async function testLoader(options) {
  {
    // Import built-in modules
    const script = new Script('import("fs")', options);
    let result = await script.runInThisContext();
    assert.strictEqual(result.constants.F_OK, fs.constants.F_OK);

    const imported = compileFunction('return import("fs")', [], options)();
    result = await imported;
    assert.strictEqual(result.constants.F_OK, fs.constants.F_OK);
  }

  const moduleUrl = fixtures.fileURL('es-modules', 'message.mjs');
  fs.copyFileSync(moduleUrl, tmpdir.resolve('message.mjs'));

  {
    const namespace = await import(moduleUrl);
    const script = new Script('import("./message.mjs")', options);
    const result = await script.runInThisContext();
    assert.deepStrictEqual(result, namespace);
  }

  {
    const namespace = await import(moduleUrl);
    const imported = compileFunction('return import("./message.mjs")', [], options)();
    const result = await imported;
    assert.deepStrictEqual(result, namespace);
  }
}

async function main() {
  {
    // Importing with absolute path as filename.
    tmpdir.refresh();
    const filename = tmpdir.resolve('index.js');
    const options = {
      filename,
      importModuleDynamically: USE_MAIN_CONTEXT_DEFAULT_LOADER
    };
    await testNotFoundErrors(options);
    await testLoader(options);
  }

  {
    // Importing with file:// URL as filename.
    tmpdir.refresh();
    // We use a search parameter to bypass caching.
    const filename = url.pathToFileURL(tmpdir.resolve('index.js')).href + '?t=1';
    const options = {
      filename,
      importModuleDynamically: USE_MAIN_CONTEXT_DEFAULT_LOADER
    };
    await testNotFoundErrors(options);
    await testLoader(options);
  }

  {
    // For undefined or non-path/URL filenames, import() should resolve to the cwd.
    tmpdir.refresh();
    process.chdir(tmpdir.path);
    const undefinedOptions = {
      importModuleDynamically: USE_MAIN_CONTEXT_DEFAULT_LOADER
    };
    const nonPathOptions = {
      filename: 'non-path',
      importModuleDynamically: USE_MAIN_CONTEXT_DEFAULT_LOADER
    };
    // Run the error tests first to avoid caching.
    await testNotFoundErrors(undefinedOptions);
    await testNotFoundErrors(nonPathOptions);

    // createContext() with null referrer also resolves to cwd.
    {
      const options = {
        importModuleDynamically: USE_MAIN_CONTEXT_DEFAULT_LOADER,
      };
      const ctx = createContext({}, options);
      const s = new Script('Promise.resolve("import(\'./message.mjs\')").then(eval)', {
        importModuleDynamically: common.mustNotCall(),
      });
      await assert.rejects(s.runInContext(ctx), { code: 'ERR_MODULE_NOT_FOUND' });
    }

    await testLoader(undefinedOptions);
    await testLoader(nonPathOptions);

    {
      const options = {
        importModuleDynamically: USE_MAIN_CONTEXT_DEFAULT_LOADER,
      };
      const ctx = createContext({}, options);
      const moduleUrl = fixtures.fileURL('es-modules', 'message.mjs');
      const namespace = await import(moduleUrl.href);
      const script = new Script('Promise.resolve("import(\'./message.mjs\')").then(eval)', {
        importModuleDynamically: common.mustNotCall(),
      });
      const result = await script.runInContext(ctx);
      assert.deepStrictEqual(result, namespace);
    }
  }
}

main().catch(common.mustNotCall());
