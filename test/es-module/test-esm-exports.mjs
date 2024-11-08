import { mustCall } from '../common/index.mjs';
import { ok, deepStrictEqual, strictEqual } from 'assert';
import { sep } from 'path';

import { requireFixture, importFixture } from '../fixtures/pkgexports.mjs';
import fromInside from '../fixtures/node_modules/pkgexports/lib/hole.js';

[requireFixture, importFixture].forEach((loadFixture) => {
  const isRequire = loadFixture === requireFixture;

  const maybeWrapped = isRequire ? (exports) => exports :
    (exports) => ({ ...exports, 'module.exports': exports.default });

  const validSpecifiers = new Map([
    // A simple mapping of a path.
    ['pkgexports/valid-cjs', maybeWrapped({ default: 'asdf' })],
    // A mapping pointing to a file that needs special encoding (%20) in URLs.
    ['pkgexports/space', maybeWrapped({ default: 'encoded path' })],
    // Verifying that normal packages still work with exports turned on.
    isRequire ? ['baz/index', { default: 'eye catcher' }] : [null],
    // Fallbacks
    ['pkgexports/fallbackdir/asdf.js', maybeWrapped({ default: 'asdf' })],
    ['pkgexports/fallbackfile', maybeWrapped({ default: 'asdf' })],
    // Conditional split for require
    ['pkgexports/condition', isRequire ? { default: 'encoded path' } :
      maybeWrapped({ default: 'asdf' })],
    // String exports sugar
    ['pkgexports-sugar', maybeWrapped({ default: 'main' })],
    // Conditional object exports sugar
    ['pkgexports-sugar2', isRequire ? { default: 'not-exported' } :
      maybeWrapped({ default: 'main' })],
    // Resolve self
    ['pkgexports/resolve-self', isRequire ?
      { default: 'self-cjs' } : { default: 'self-mjs' }],
    // Resolve self sugar
    ['pkgexports-sugar', maybeWrapped({ default: 'main' })],
    // Path patterns
    ['pkgexports/subpath/sub-dir1', maybeWrapped({ default: 'main' })],
    ['pkgexports/subpath/sub-dir1.js', maybeWrapped({ default: 'main' })],
    ['pkgexports/features/dir1', maybeWrapped({ default: 'main' })],
    ['pkgexports/dir1/dir1/trailer', maybeWrapped({ default: 'main' })],
    ['pkgexports/dir2/dir2/trailer', maybeWrapped({ default: 'index' })],
    ['pkgexports/a/dir1/dir1', maybeWrapped({ default: 'main' })],
    ['pkgexports/a/b/dir1/dir1', maybeWrapped({ default: 'main' })],

    // Deprecated:
    // Double slashes:
    ['pkgexports/a//dir1/dir1', maybeWrapped({ default: 'main' })],
    // double slash target
    ['pkgexports/doubleslash', maybeWrapped({ default: 'asdf' })],
    // Null target with several slashes
    ['pkgexports/sub//internal/test.js', maybeWrapped({ default: 'internal only' })],
    ['pkgexports/sub//internal//test.js', maybeWrapped({ default: 'internal only' })],
    ['pkgexports/sub/////internal/////test.js', maybeWrapped({ default: 'internal only' })],
    // trailing slash
    ['pkgexports/trailing-pattern-slash/',
     maybeWrapped({ default: 'trailing-pattern-slash' })],
  ]);

  if (!isRequire) {
    // No exports or main field
    validSpecifiers.set('no_exports', { default: 'index' });
    // Main field without extension
    validSpecifiers.set('default_index', { default: 'main' });
  }

  for (const [validSpecifier, expected] of validSpecifiers) {
    if (validSpecifier === null) continue;

    loadFixture(validSpecifier)
      .then(mustCall((actual) => {
        deepStrictEqual({ ...actual }, expected);
      }));
  }

  const undefinedExports = new Map([
    // There's no such export - so there's nothing to do.
    ['pkgexports/missing', './missing'],
    // The file exists but isn't exported. The exports is a number which counts
    // as a non-null value without any properties, just like `{}`.
    ['pkgexports-number/hidden.js', './hidden.js'],
    // Sugar cases still encapsulate
    ['pkgexports-sugar/not-exported.js', './not-exported.js'],
    ['pkgexports-sugar2/not-exported.js', './not-exported.js'],
    // Conditional exports with no match are "not exported" errors
    ['pkgexports/invalid1', './invalid1'],
    ['pkgexports/invalid4', './invalid4'],
    // Null mapping
    ['pkgexports/sub/internal/test.js', './sub/internal/test.js'],
    ['pkgexports/sub/internal//test.js', './sub/internal//test.js'],
    ['pkgexports/null', './null'],
    ['pkgexports//null', './/null'],
    ['pkgexports/////null', './////null'],
    ['pkgexports/null/subpath', './null/subpath'],
    // Empty fallback
    ['pkgexports/nofallback1', './nofallback1'],
    // Non pattern matches
    ['pkgexports/trailer', './trailer'],
  ]);

  const invalidExports = new Map([
    // This path steps back inside the package but goes through an exports
    // target that escapes the package, so we still catch that as invalid
    ['pkgexports/belowdir/pkgexports/asdf.js', './belowdir/'],
    // This target file steps below the package
    ['pkgexports/belowfile', './belowfile'],
    // Invalid targets
    ['pkgexports/invalid2', './invalid2'],
    ['pkgexports/invalid3', './invalid3'],
    ['pkgexports/invalid5', 'invalid5'],
    // Missing / invalid fallbacks
    ['pkgexports/nofallback2', './nofallback2'],
    // Reaching into nested node_modules
    ['pkgexports/nodemodules', './nodemodules'],
    // Self resolve invalid
    ['pkgexports/resolve-self-invalid', './invalid2'],
  ]);

  const invalidSpecifiers = new Map([
    // Even though 'pkgexports/sub/asdf.js' works, alternate "path-like"
    // variants do not to prevent confusion and accidental loopholes.
    ['pkgexports/sub/./../asdf.js', './sub/./../asdf.js'],
    // Cannot reach into node_modules, even percent encoded
    ['pkgexports/sub/no%64e_modules', './sub/no%64e_modules'],
    // Cannot backtrack below exposed path, even with percent encoded "."
    ['pkgexports/sub/%2e./asdf', './asdf'],
  ]);

  for (const [specifier, subpath] of undefinedExports) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, 'ERR_PACKAGE_PATH_NOT_EXPORTED');
      assertStartsWith(err.message, 'Package subpath ');
      assertIncludes(err.message, subpath);
    }));
  }

  for (const [specifier, subpath] of invalidExports) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, 'ERR_INVALID_PACKAGE_TARGET');
      assertStartsWith(err.message, 'Invalid "exports"');
      assertIncludes(err.message, subpath);
      if (!subpath.startsWith('./')) {
        assertIncludes(err.message, 'targets must start with');
      }
    }));
  }

  for (const [specifier, subpath] of invalidSpecifiers) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, 'ERR_INVALID_MODULE_SPECIFIER');
      assertStartsWith(err.message, 'Invalid module ');
      assertIncludes(err.message, 'is not a valid match in pattern');
      assertIncludes(err.message, subpath);
    }));
  }

  // Conditional export, even with no match, should still be used instead
  // of falling back to main
  if (isRequire) {
    loadFixture('pkgexports-main').catch(mustCall((err) => {
      strictEqual(err.code, 'ERR_PACKAGE_PATH_NOT_EXPORTED');
      assertStartsWith(err.message, 'No "exports" main ');
    }));
  }

  const notFoundExports = new Map([
    // Non-existing file
    ['pkgexports/sub/not-a-file.js', `pkgexports${sep}not-a-file.js`],
    // No extension lookups
    ['pkgexports/no-ext', `pkgexports${sep}asdf`],
    // Pattern specificity
    ['pkgexports/dir2/trailer', `subpath${sep}dir2.js`],
    // Pattern double $$ escaping!
    ['pkgexports/a/$$', `subpath${sep}$$.js`],
  ]);

  if (!isRequire) {
    const onDirectoryImport = (err) => {
      strictEqual(err.code, 'ERR_UNSUPPORTED_DIR_IMPORT');
      assertStartsWith(err.message, 'Directory import');
    };
    loadFixture('pkgexports/subpath/dir1').catch(mustCall(onDirectoryImport));
    loadFixture('pkgexports/subpath/dir2').catch(mustCall(onDirectoryImport));
  }

  for (const [specifier, request] of notFoundExports) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, (isRequire ? '' : 'ERR_') + 'MODULE_NOT_FOUND');
      assertIncludes(err.message, request);
      assertStartsWith(err.message, 'Cannot find module');
    }));
  }

  // The use of %2F and %5C escapes in paths fails loading
  loadFixture('pkgexports/sub/..%2F..%2Fbar.js').catch(mustCall((err) => {
    strictEqual(err.code, 'ERR_INVALID_MODULE_SPECIFIER');
  }));
  loadFixture('pkgexports/sub/..%5C..%5Cbar.js').catch(mustCall((err) => {
    strictEqual(err.code, 'ERR_INVALID_MODULE_SPECIFIER');
  }));

  // Package export with numeric index properties must throw a validation error
  loadFixture('pkgexports-numeric').catch(mustCall((err) => {
    strictEqual(err.code, 'ERR_INVALID_PACKAGE_CONFIG');
  }));

  // Sugar conditional exports main mixed failure case
  loadFixture('pkgexports-sugar-fail').catch(mustCall((err) => {
    strictEqual(err.code, 'ERR_INVALID_PACKAGE_CONFIG');
    assertStartsWith(err.message, 'Invalid package');
    assertIncludes(err.message, '"exports" cannot contain some keys starting ' +
    'with \'.\' and some not. The exports object must either be an object of ' +
    'package subpath keys or an object of main entry condition name keys ' +
    'only.');
  }));
});

const { requireFromInside, importFromInside } = fromInside;
[importFromInside, requireFromInside].forEach((loadFromInside) => {
  const isRequire = loadFromInside === requireFromInside;
  const maybeWrapped = isRequire ? (exports) => exports :
    (exports) => ({ ...exports, 'module.exports': exports.default });

  const validSpecifiers = new Map([
    // A file not visible from outside of the package
    ['../not-exported.js', maybeWrapped({ default: 'not-exported' })],
    // Part of the public interface
    ['pkgexports/valid-cjs', maybeWrapped({ default: 'asdf' })],
  ]);
  for (const [validSpecifier, expected] of validSpecifiers) {
    if (validSpecifier === null) continue;

    loadFromInside(validSpecifier)
      .then(mustCall((actual) => {
        deepStrictEqual({ ...actual }, expected);
      }));
  }
});

function assertStartsWith(actual, expected) {
  const start = actual.toString().slice(0, expected.length);
  strictEqual(start, expected);
}

function assertIncludes(actual, expected) {
  ok(actual.toString().indexOf(expected) !== -1,
     `${JSON.stringify(actual)} includes ${JSON.stringify(expected)}`);
}
