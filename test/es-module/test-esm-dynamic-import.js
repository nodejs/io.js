'use strict';
const common = require('../common');
const { pathToFileURL } = require('url');
const assert = require('assert');

const relativePath = '../fixtures/es-modules/test-esm-ok.mjs';
const absolutePath = require.resolve(relativePath);
const targetURL = pathToFileURL(absolutePath);

function expectModuleError(result, code, message) {
  Promise.resolve(result).catch(common.mustCall((error) => {
    assert.strictEqual(error.code, code);
    if (message) assert.strictEqual(error.message, message);
  }));
}

function expectOkNamespace(result) {
  Promise.resolve(result)
    .then(common.mustCall((ns) => {
      const expected = { default: true };
      Object.defineProperty(expected, Symbol.toStringTag, {
        value: 'Module'
      });
      Object.setPrototypeOf(expected, Object.getPrototypeOf(ns));
      assert.deepStrictEqual(ns, expected);
    }));
}

function expectFsNamespace(result) {
  Promise.resolve(result)
    .then(common.mustCall((ns) => {
      assert.strictEqual(typeof ns.default.writeFile, 'function');
      assert.strictEqual(typeof ns.writeFile, 'function');
    }));
}

// For direct use of import expressions inside of CJS or ES modules, including
// via eval, all kinds of specifiers should work without issue.
(function testScriptOrModuleImport() {
  // Importing another file, both direct & via eval
  // expectOkNamespace(import(relativePath));
  expectOkNamespace(eval(`import("${relativePath}")`));
  expectOkNamespace(eval(`import("${relativePath}")`));
  expectOkNamespace(eval(`import(${JSON.stringify(targetURL)})`));

  // Importing a built-in, both direct & via eval
  expectFsNamespace(import('fs'));
  expectFsNamespace(eval('import("fs")'));
  expectFsNamespace(eval('import("fs")'));
  expectFsNamespace(import('node:fs'));

  expectModuleError(import('node:unknown'),
                    'ERR_UNKNOWN_BUILTIN_MODULE');
  expectModuleError(import('node:internal/test/binding'),
                    'ERR_UNKNOWN_BUILTIN_MODULE');
  expectModuleError(import('./not-an-existing-module.mjs'),
                    'ERR_MODULE_NOT_FOUND');
  expectModuleError(import('http://example.com/foo.js'),
                    'ERR_UNSUPPORTED_ESM_URL_SCHEME');
  if (common.isWindows) {
    const msg =
      'Only URLs with a scheme in: file, data, and node are supported by the default ' +
      'ESM loader. On Windows, absolute paths must be valid file:// URLs. ' +
      "Received protocol 'c:'";
    expectModuleError(import('C:\\example\\foo.mjs'),
                      'ERR_UNSUPPORTED_ESM_URL_SCHEME',
                      msg);
  }
  // If the specifier is an origin-relative URL, it should
  // be treated as a file: URL.
  expectOkNamespace(import(targetURL.pathname));

  // If the referrer is a realm record, there is no way to resolve the
  // specifier.
  // TODO(legendecas): https://github.com/tc39/ecma262/pull/3195
  expectModuleError(Promise.resolve('import("node:fs")').then(eval),
                    'ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING');
})();
