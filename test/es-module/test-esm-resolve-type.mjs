// Flags: --expose-internals

/**
 * This test ensures defaultResolve returns the found module format in the
 * return object in the form:
 * { url: <url_value>, format: <'module'|'commonjs'|undefined> };
 */

import * as common from '../common/index.mjs';
import tmpdir from '../common/tmpdir.js';
import * as fixtures from '../common/fixtures.mjs';
import path from 'path';
import fs from 'fs';
import url from 'url';
import process from 'process';

if (!common.isMainThread) {
  common.skip(
    'test-esm-resolve-type.mjs: process.chdir is not available in Workers'
  );
}

import assert from 'assert';
import internalResolve from 'node:internal/modules/esm/resolve';
const {
  defaultResolve: resolve
} = internalResolve;

const rel = (file) => tmpdir.resolve(file);
const previousCwd = process.cwd();
const nmDir = rel('node_modules');

try {
  tmpdir.refresh();
  process.chdir(tmpdir.path);
  /**
   * ensure that resolving by full path does not return the format
   * with the defaultResolver
   */
  [
    [ '/es-modules/package-ends-node_modules/index.js', 'module' ],
    [ '/es-modules/package-type-module/index.js', 'module' ],
    [ '/es-modules/package-type-commonjs/index.js', 'commonjs' ],
    [ '/es-modules/package-without-type/index.js', null ],
    [ '/es-modules/package-without-pjson/index.js', null ],
  ].forEach(([ testScript, expectedType ]) => {
    const resolvedPath = path.resolve(fixtures.path(testScript));
    const resolveResult = resolve(url.pathToFileURL(resolvedPath));
    assert.strictEqual(resolveResult.format, expectedType);
  });

  /**
   * create a test module and try to resolve it by module name.
   * check the result is as expected
   *
   * for test-module-ne: everything .js that is not 'module' is 'commonjs'
   */
  for (const [ moduleName, moduleExtension, moduleType, expectedResolvedType ] of
    [ [ 'test-module-mainjs', 'js', 'module', 'module'],
      [ 'test-module-mainmjs', 'mjs', 'module', 'module'],
      [ 'test-module-cjs', 'js', 'commonjs', 'commonjs'],
      [ 'test-module-ne', 'js', undefined, null],
    ]) {
    process.chdir(previousCwd);
    tmpdir.refresh();
    process.chdir(tmpdir.path);
    const createDir = (path) => {
      if (!fs.existsSync(path)) {
        fs.mkdirSync(path);
      }
    };

    const mDir = rel(`node_modules/${moduleName}`);
    const subDir = rel(`node_modules/${moduleName}/subdir`);
    const pkg = rel(`node_modules/${moduleName}/package.json`);
    const script = rel(`node_modules/${moduleName}/subdir/mainfile.${moduleExtension}`);

    createDir(nmDir);
    createDir(mDir);
    createDir(subDir);
    const pkgJsonContent = {
      ...(moduleType !== undefined) && { type: moduleType },
      main: `subdir/mainfile.${moduleExtension}`
    };
    fs.writeFileSync(pkg, JSON.stringify(pkgJsonContent));
    fs.writeFileSync(script,
                     'export function esm-resolve-tester() {return 42}');

    const resolveResult = resolve(`${moduleName}`);
    assert.strictEqual(resolveResult.format, expectedResolvedType);

    fs.rmSync(nmDir, { recursive: true, force: true });
  }

  // Helpers
  const createDir = (path) => {
    if (!fs.existsSync(path)) {
      fs.mkdirSync(path);
    }
  };

  {
    // Create a dummy dual package
    //
    /**
     * this creates the following directory structure:
     *
     * ./node_modules:
     *   |-> my-dual-package
     *       |-> es
     *           |-> index.js
     *           |-> package.json [2]
     *       |-> lib
     *           |-> index.js
     *       |->package.json [1]
     *
     * in case the package is imported:
     *    import * as my-package from 'my-dual-package'
     * it will cause the resolve method to return:
     *  {
     *     url: '<base_path>/node_modules/my-dual-package/es/index.js',
     *     format: 'module'
     *  }
     *
     *  following testcase ensures that resolve works correctly in this case
     *  returning the information as specified above. Source for 'url' value
     *  is [1], source for 'format' value is [2]
     */

    const moduleName = 'my-dual-package';

    const mDir = rel(`node_modules/${moduleName}`);
    const esSubDir = rel(`node_modules/${moduleName}/es`);
    const cjsSubDir = rel(`node_modules/${moduleName}/lib`);
    const pkg = rel(`node_modules/${moduleName}/package.json`);
    const esmPkg = rel(`node_modules/${moduleName}/es/package.json`);
    const esScript = rel(`node_modules/${moduleName}/es/index.js`);
    const cjsScript = rel(`node_modules/${moduleName}/lib/index.js`);

    createDir(nmDir);
    createDir(mDir);
    createDir(esSubDir);
    createDir(cjsSubDir);

    const mainPkgJsonContent = {
      type: 'commonjs',
      exports: {
        '.': {
          'require': './lib/index.js',
          'import': './es/index.js',
          'default': './lib/index.js'
        },
        './package.json': './package.json',
      }
    };
    const esmPkgJsonContent = {
      type: 'module'
    };

    fs.writeFileSync(pkg, JSON.stringify(mainPkgJsonContent));
    fs.writeFileSync(esmPkg, JSON.stringify(esmPkgJsonContent));
    fs.writeFileSync(
      esScript,
      'export function esm-resolve-tester() {return 42}'
    );
    fs.writeFileSync(
      cjsScript,
      'module.exports = {esm-resolve-tester: () => {return 42}}'
    );

    // test the resolve
    const resolveResult = resolve(`${moduleName}`);
    assert.strictEqual(resolveResult.format, 'module');
    assert.ok(resolveResult.url.includes('my-dual-package/es/index.js'));
  }

  // TestParameters are ModuleName, mainRequireScript, mainImportScript,
  // mainPackageType, subdirPkgJsonType, expectedResolvedFormat, mainSuffix
  [
    [ 'mjs-mod-mod', 'index.js', 'index.mjs', 'module', 'module', 'module'],
    [ 'mjs-com-com', 'idx.js', 'idx.mjs', 'commonjs', 'commonjs', 'module'],
    [ 'mjs-mod-com', 'index.js', 'imp.mjs', 'module', 'commonjs', 'module'],
    [ 'cjs-mod-mod', 'index.cjs', 'imp.cjs', 'module', 'module', 'commonjs'],
    [ 'js-com-com', 'index.js', 'imp.js', 'commonjs', 'commonjs', 'commonjs'],
    [ 'js-com-mod', 'index.js', 'imp.js', 'commonjs', 'module', 'module'],
    [ 'qmod', 'index.js', 'imp.js', 'commonjs', 'module', 'module', '?k=v'],
    [ 'hmod', 'index.js', 'imp.js', 'commonjs', 'module', 'module', '#Key'],
    [ 'qhmod', 'index.js', 'imp.js', 'commonjs', 'module', 'module', '?k=v#h'],
    [ 'ts-mod-com', 'index.js', 'imp.ts', 'module', 'commonjs', undefined],
  ].forEach((testVariant) => {
    const [
      moduleName,
      mainRequireScript,
      mainImportScript,
      mainPackageType,
      subdirPackageType,
      expectedResolvedFormat,
      mainSuffix = '' ] = testVariant;

    const mDir = rel(`node_modules/${moduleName}`);
    const subDir = rel(`node_modules/${moduleName}/subdir`);
    const pkg = rel(`node_modules/${moduleName}/package.json`);
    const subdirPkg = rel(`node_modules/${moduleName}/subdir/package.json`);
    const esScript = rel(`node_modules/${moduleName}/subdir/${mainImportScript}`);
    const cjsScript = rel(`node_modules/${moduleName}/subdir/${mainRequireScript}`);

    createDir(nmDir);
    createDir(mDir);
    createDir(subDir);

    const mainPkgJsonContent = {
      type: mainPackageType,
      exports: {
        '.': {
          'require': `./subdir/${mainRequireScript}${mainSuffix}`,
          'import': `./subdir/${mainImportScript}${mainSuffix}`,
          'default': `./subdir/${mainRequireScript}${mainSuffix}`
        },
        './package.json': './package.json',
      }
    };
    const subdirPkgJsonContent = {
      type: `${subdirPackageType}`
    };

    fs.writeFileSync(pkg, JSON.stringify(mainPkgJsonContent));
    fs.writeFileSync(subdirPkg, JSON.stringify(subdirPkgJsonContent));
    fs.writeFileSync(
      esScript,
      'export function esm-resolve-tester() {return 42}'
    );
    fs.writeFileSync(
      cjsScript,
      'module.exports = {esm-resolve-tester: () => {return 42}}'
    );

    // test the resolve
    const resolveResult = resolve(`${moduleName}`);
    assert.strictEqual(resolveResult.format, expectedResolvedFormat);
    assert.ok(resolveResult.url.endsWith(`${moduleName}/subdir/${mainImportScript}${mainSuffix}`));
  });

} finally {
  process.chdir(previousCwd);
  fs.rmSync(nmDir, { recursive: true, force: true });
}
