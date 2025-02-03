'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const assert = require('node:assert');
const fs = require('node:fs');
const { findPackageJSON } = require('node:module');
const path = require('node:path');
const { describe, it } = require('node:test');


describe('findPackageJSON', () => { // Throws when no arguments are provided
  it('should throw when no arguments are provided', () => {
    assert.throws(
      () => findPackageJSON(),
      { code: 'ERR_MISSING_ARGS' }
    );
  });

  it('should throw when parentLocation is invalid', () => {
    for (const invalid of [null, {}, [], Symbol(), () => {}, true, false, 1, 0]) {
      assert.throws(
        () => findPackageJSON('', invalid),
        { code: 'ERR_INVALID_ARG_TYPE' },
      );
    }
  });

  it('should accept a file URL (string), like from `import.meta.resolve()`', () => {
    const importMetaUrl = `${fixtures.fileURL('whatever.ext')}`;
    const specifierBase = './packages/root-types-field';
    assert.strictEqual(
      findPackageJSON(`${specifierBase}/index.js`, importMetaUrl),
      path.toNamespacedPath(fixtures.path(specifierBase, 'package.json')),
    );
  });

  it('should accept a file URL instance', () => {
    const importMetaUrl = fixtures.fileURL('whatever.ext');
    const specifierBase = './packages/root-types-field';
    assert.strictEqual(
      findPackageJSON(
        new URL(`${specifierBase}/index.js`, importMetaUrl),
        importMetaUrl,
      ),
      path.toNamespacedPath(fixtures.path(specifierBase, 'package.json')),
    );
  });

  it('should be able to crawl up (CJS)', () => {
    const pathToMod = fixtures.path('packages/nested/sub-pkg-cjs/index.js');
    const parentPkg = require(pathToMod);

    const pathToParent = path.toNamespacedPath(fixtures.path('packages/nested/package.json'));
    assert.strictEqual(parentPkg, pathToParent);
  });

  it('should be able to crawl up (ESM)', () => {
    const pathToMod = fixtures.path('packages/nested/sub-pkg-esm/index.js');
    const parentPkg = require(pathToMod).default; // This test is a CJS file

    const pathToParent = path.toNamespacedPath(fixtures.path('packages/nested/package.json'));
    assert.strictEqual(parentPkg, pathToParent);
  });

  it('can require via package.json', () => {
    const pathToMod = fixtures.path('packages/cjs-main-no-index/other.js');
    // `require()` falls back to package.json values like "main" to resolve when there is no index
    const answer = require(pathToMod);

    assert.strictEqual(answer, 43);
  });

  it('should be able to resolve both root and closest package.json', () => {
    tmpdir.refresh();

    fs.writeFileSync(tmpdir.resolve('entry.mjs'), `
      import { findPackageJSON } from 'node:module';
      import fs from 'node:fs';

      const readPJSON = (locus) => JSON.parse(fs.readFileSync(locus, 'utf8'));

      const { secretNumberPkgRoot } = readPJSON(findPackageJSON('pkg', import.meta.url));
      const { secretNumberSubfolder } = readPJSON(findPackageJSON(import.meta.resolve('pkg')));
      const { secretNumberSubfolder2 } = readPJSON(findPackageJSON(import.meta.resolve('pkg2')));
      const { secretNumberPkg2 } = readPJSON(findPackageJSON('pkg2', import.meta.url));

      console.log(secretNumberPkgRoot, secretNumberSubfolder, secretNumberSubfolder2, secretNumberPkg2);
    `);

    const secretNumberPkgRoot = Math.ceil(Math.random() * 999);
    const secretNumberSubfolder = Math.ceil(Math.random() * 999);
    const secretNumberSubfolder2 = Math.ceil(Math.random() * 999);
    const secretNumberPkg2 = Math.ceil(Math.random() * 999);

    fs.mkdirSync(tmpdir.resolve('node_modules/pkg/subfolder'), { recursive: true });
    fs.writeFileSync(
      tmpdir.resolve('node_modules/pkg/subfolder/index.js'),
      '',
    );
    fs.writeFileSync(
      tmpdir.resolve('node_modules/pkg/subfolder/package.json'),
      JSON.stringify({
        type: 'module',
        secretNumberSubfolder,
      }),
    );
    fs.writeFileSync(
      tmpdir.resolve('node_modules/pkg/package.json'),
      JSON.stringify({
        name: 'pkg',
        exports: './subfolder/index.js',
        secretNumberPkgRoot,
      }),
    );

    fs.mkdirSync(tmpdir.resolve('node_modules/pkg/subfolder2'));
    fs.writeFileSync(
      tmpdir.resolve('node_modules/pkg/subfolder2/package.json'),
      JSON.stringify({
        type: 'module',
        secretNumberSubfolder2,
      }),
    );
    fs.writeFileSync(
      tmpdir.resolve('node_modules/pkg/subfolder2/index.js'),
      '',
    );

    fs.mkdirSync(tmpdir.resolve('node_modules/pkg2'));
    fs.writeFileSync(
      tmpdir.resolve('node_modules/pkg2/package.json'),
      JSON.stringify({
        name: 'pkg',
        main: tmpdir.resolve('node_modules/pkg/subfolder2/index.js'),
        secretNumberPkg2,
      }),
    );

    common.spawnPromisified(process.execPath, [tmpdir.resolve('entry.mjs')]).then(common.mustCall((result) => {
      console.error(result.stderr);
      console.log(result.stdout);
      assert.deepStrictEqual(result, {
        stdout: `${secretNumberPkgRoot} ${secretNumberSubfolder} ${secretNumberSubfolder2} ${secretNumberPkg2}\n`,
        stderr: '',
        code: 0,
        signal: null,
      });
    }));
  });

  it('should work within a loader', async () => {
    const specifierBase = './packages/root-types-field';
    const target = fixtures.fileURL(specifierBase, 'index.js');
    const foundPjsonPath = path.toNamespacedPath(fixtures.path(specifierBase, 'package.json'));
    const { code, stderr, stdout } = await common.spawnPromisified(process.execPath, [
      '--no-warnings',
      '--loader',
      [
        'data:text/javascript,',
        'import fs from "node:fs";',
        'import module from "node:module";',
        encodeURIComponent(`fs.writeSync(1, module.findPackageJSON(${JSON.stringify(target)}));`),
        'export const resolve = async (s, c, n) => n(s);',
      ].join(''),
      '--eval',
      'import "node:os";', // Can be anything that triggers the resolve hook chain
    ]);

    assert.strictEqual(stderr, '');
    assert.ok(stdout.includes(foundPjsonPath), stdout);
    assert.strictEqual(code, 0);
  });

  it('should work with an async resolve hook registered', async () => {
    const specifierBase = './packages/root-types-field';
    const target = fixtures.fileURL(specifierBase, 'index.js');
    const foundPjsonPath = path.toNamespacedPath(fixtures.path(specifierBase, 'package.json'));
    const { code, stderr, stdout } = await common.spawnPromisified(process.execPath, [
      '--no-warnings',
      '--loader',
      'data:text/javascript,export const resolve = async (s, c, n) => n(s);',
      '--print',
      `require("node:module").findPackageJSON(${JSON.stringify(target)})`,
    ]);

    assert.strictEqual(stderr, '');
    assert.ok(stdout.includes(foundPjsonPath), stdout);
    assert.strictEqual(code, 0);
  });
});
