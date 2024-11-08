'use strict';

// Flags: --expose-internals

require('../common');

const { describe, it } = require('node:test');
const path = require('node:path');
const assert = require('node:assert');
const { pathToFileURL } = require('node:url');
const { legacyMainResolve } = require('node:internal/modules/esm/resolve');

const fixtures = require('../common/fixtures.js');

describe('legacyMainResolve', () => {
  it('should resolve using packageConfig.main', () => {
    const packageJsonUrl = pathToFileURL(
      path.resolve(
        fixtures.path('/es-modules/legacy-main-resolver'),
        'package.json'
      )
    );

    const paths = [
      ['./index-js/index.js', './index-js/index.js'],
      ['./index-js/index', './index-js/index.js'],
      ['./index-json/index', './index-json/index.json'],
      ['./index-node/index', './index-node/index.node'],
      ['./index-js', './index-js/index.js'],
      ['./index-json', './index-json/index.json'],
      ['./index-node', './index-node/index.node'],
    ];

    for (const [main, expected] of paths) {
      const packageConfig = { main };
      const base = path.resolve(
        fixtures.path('/es-modules/legacy-main-resolver')
      );

      assert.strictEqual(
        legacyMainResolve(packageJsonUrl, packageConfig, base).href,
        pathToFileURL(path.join(base, expected)).href
      );
    }
  });

  it('should resolve using packageJsonUrl', () => {
    const paths = [
      ['index-js', './index-js/index.js'],
      ['index-json', './index-json/index.json'],
      ['index-node', './index-node/index.node'],
    ];

    for (const [folder, expected] of paths) {
      const packageJsonUrl = pathToFileURL(
        path.resolve(
          fixtures.path('/es-modules/legacy-main-resolver'),
          folder,
          'package.json'
        )
      );
      const packageConfig = { main: undefined };
      const base = path.resolve(
        fixtures.path('/es-modules/legacy-main-resolver')
      );

      assert.strictEqual(
        legacyMainResolve(packageJsonUrl, packageConfig, base).href,
        pathToFileURL(path.join(base, expected)).href
      );
    }
  });

  it('should throw when packageJsonUrl is not URL', () => {
    assert.throws(
      () =>
        legacyMainResolve(
          path.resolve(
            fixtures.path('/es-modules/legacy-main-resolver/index-node'),
            'package.json'
          ),
          {},
          ''
        ),
      { code: 'ERR_INTERNAL_ASSERTION' },
    );
  });

  it('should throw when packageConfigMain is invalid URL', () => {
    assert.throws(
      () =>
        legacyMainResolve(
          pathToFileURL(
            path.resolve(
              // Is invalid because this path does not point to a file
              fixtures.path('/es-modules/legacy-main-resolver/index-node')
            )
          ),
          { main: './invalid/index.js' },
          ''
        ),
      { code: 'ERR_INVALID_URL' },
    );
  });

  it('should throw when packageJsonUrl is invalid URL', () => {
    assert.throws(
      () =>
        legacyMainResolve(
          pathToFileURL(
            path.resolve(
              // Is invalid because this path does not point to a file
              fixtures.path('/es-modules/legacy-main-resolver/index-node')
            )
          ),
          { main: undefined },
          ''
        ),
      { code: 'ERR_INVALID_URL' },
    );
  });

  it('should throw when cannot resolve to a file', () => {
    const packageJsonUrl = pathToFileURL(
      path.resolve(
        fixtures.path('/es-modules/legacy-main-resolver'),
        'package.json'
      )
    );
    assert.throws(
      () => legacyMainResolve(packageJsonUrl, { main: null }, packageJsonUrl),
      { message: /index\.js/, code: 'ERR_MODULE_NOT_FOUND' },
    );
  });

  it('should not crash when cannot resolve to a file that contains special chars', () => {
    const packageJsonUrl = pathToFileURL('/c/file%20with%20percents/package.json');
    assert.throws(
      () => legacyMainResolve(packageJsonUrl, { main: null }, packageJsonUrl),
      { message: /index\.js/, code: 'ERR_MODULE_NOT_FOUND' },
    );
  });

  it('should report main file on error message when not found', () => {
    const packageJsonUrl = pathToFileURL(
      path.resolve(
        fixtures.path('/es-modules/legacy-main-resolver'),
        'package.json'
      )
    );
    assert.throws(
      () => legacyMainResolve(packageJsonUrl, { main: './index.node' }, packageJsonUrl),
      { message: /index\.node/, code: 'ERR_MODULE_NOT_FOUND' },
    );
  });

  it('should throw when cannot resolve to a file (base not defined)', () => {
    const packageJsonUrl = pathToFileURL(
      path.resolve(
        fixtures.path('/es-modules/legacy-main-resolver'),
        'package.json'
      )
    );
    assert.throws(
      () => legacyMainResolve(packageJsonUrl, { main: null }, undefined),
      { message: /"base" argument must be/, code: 'ERR_INVALID_ARG_TYPE' },
    );
  });

  it('should interpret main as a path, not a URL', () => {
    const packageJsonUrl = fixtures.fileURL('/es-modules/legacy-main-resolver/package.json');
    assert.deepStrictEqual(
      legacyMainResolve(packageJsonUrl, { main: '../folder%25with percentage#/' }, packageJsonUrl),
      fixtures.fileURL('/es-modules/folder%25with percentage#/index.js'),
    );
  });
});
