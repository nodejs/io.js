import { mustCall } from '../common/index.mjs';
import { ok, deepStrictEqual, strictEqual } from 'assert';

import importer from '../fixtures/es-modules/pkgimports/importer.js';
import { requireFixture } from '../fixtures/pkgexports.mjs';

const { requireImport, importImport } = importer;

[requireImport, importImport].forEach((loadFixture) => {
  const isRequire = loadFixture === requireImport;

  const maybeWrapped = isRequire ? (exports) => exports :
    (exports) => ({ ...exports, 'module.exports': exports.default });

  const internalImports = new Map([
    // Base case
    ['#test', maybeWrapped({ default: 'test' })],
    // import / require conditions
    ['#branch', maybeWrapped({ default: isRequire ? 'requirebranch' : 'importbranch' })],
    // Subpath imports
    ['#subpath/x.js', maybeWrapped({ default: 'xsubpath' })],
    // External imports
    ['#external', maybeWrapped({ default: 'asdf' })],
    // External subpath imports
    ['#external/subpath/asdf.js', maybeWrapped({ default: 'asdf' })],
    // Trailing pattern imports
    ['#subpath/asdf.asdf', maybeWrapped({ default: 'test' })],
    // Leading slash
    ['#subpath//asdf.asdf', maybeWrapped({ default: 'test' })],
    // Double slash
    ['#subpath/as//df.asdf', maybeWrapped({ default: 'test' })],
  ]);

  for (const [validSpecifier, expected] of internalImports) {
    if (validSpecifier === null) continue;

    loadFixture(validSpecifier)
      .then(mustCall((actual) => {
        deepStrictEqual({ ...actual }, expected);
      }));
  }

  const invalidImportTargets = new Set([
    // Target steps below the package base
    ['#belowbase', '#belowbase'],
    // Target is a URL
    ['#url', '#url'],
  ]);

  for (const [specifier, subpath] of invalidImportTargets) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, 'ERR_INVALID_PACKAGE_TARGET');
      assertStartsWith(err.message, 'Invalid "imports"');
      assertIncludes(err.message, subpath);
      assertNotIncludes(err.message, 'targets must start with');
    }));
  }

  const invalidImportSpecifiers = new Map([
    // Backtracking below the package base
    ['#subpath/sub/../../../belowbase', 'request is not a valid match in pattern'],
    // Percent-encoded slash errors
    ['#external/subpath/x%2Fy', 'must not include encoded "/" or "\\"'],
    ['#external/subpath/x%5Cy', 'must not include encoded "/" or "\\"'],
    // Target must have a name
    ['#', '#'],
    // Initial slash target must have a leading name
    ['#/initialslash', '#/initialslash'],
    // Percent-encoded target paths
    ['#encodedslash', 'must not include encoded "/" or "\\"'],
    ['#encodedbackslash', 'must not include encoded "/" or "\\"'],
  ]);

  for (const [specifier, expected] of invalidImportSpecifiers) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, 'ERR_INVALID_MODULE_SPECIFIER');
      assertStartsWith(err.message, 'Invalid module');
      assertIncludes(err.message, expected);
    }));
  }

  const undefinedImports = new Set([
    // EOL subpaths
    '#external/invalidsubpath/x',
    // Missing import
    '#missing',
    // Explicit null import
    '#null',
    '#subpath/null',
    // No condition match import
    '#nullcondition',
    // Null subpath shadowing
    '#subpath/nullshadow/x',
    // Null pattern
    '#subpath/internal/test',
    '#subpath/internal//test',
  ]);

  for (const specifier of undefinedImports) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, 'ERR_PACKAGE_IMPORT_NOT_DEFINED');
      assertStartsWith(err.message, 'Package import ');
      assertIncludes(err.message, specifier);
    }));
  }

  // Handle not found for the defined imports target not existing
  const nonDefinedImports = new Set([
    '#notfound',
    '#subpath//null',
    '#subpath/////null',
    '#subpath//internal/test',
    '#subpath//internal//test',
    '#subpath/////internal/////test',
  ]);
  for (const specifier of nonDefinedImports) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code,
                  isRequire ? 'MODULE_NOT_FOUND' : 'ERR_MODULE_NOT_FOUND');
    }));
  }
});

// CJS resolver must still support #package packages in node_modules
requireFixture('#cjs').then(mustCall((actual) => {
  strictEqual(actual.default, 'cjs backcompat');
}));

function assertStartsWith(actual, expected) {
  const start = actual.toString().slice(0, expected.length);
  strictEqual(start, expected);
}

function assertIncludes(actual, expected) {
  ok(actual.toString().indexOf(expected) !== -1,
     `${JSON.stringify(actual)} includes ${JSON.stringify(expected)}`);
}

function assertNotIncludes(actual, expected) {
  ok(actual.toString().indexOf(expected) === -1,
     `${JSON.stringify(actual)} doesn't include ${JSON.stringify(expected)}`);
}
