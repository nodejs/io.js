'use strict';

// Flags: --expose-internals

const common = require('../common');
if (!common.isWindows) {
  common.skip('this test is Windows-specific.');
}

const fs = require('fs');
const path = require('path');
const tmpdir = require('../common/tmpdir');
const { describe, it } = require('node:test');
const { legacyMainResolve } = require('node:internal/modules/esm/resolve');
const { pathToFileURL } = require('node:url');
const { spawnPromisified } = require('../common');
const assert = require('assert');
const { execPath } = require('node:process');

describe('long path on Windows', () => {
  it('check long path in ReadPackageJSON', () => {
    // Module layout will be the following:
    //  package.json
    //  main
    //    main.js
    const packageDirNameLen = Math.max(260 - tmpdir.path.length, 1);
    const packageDirName = tmpdir.resolve('x'.repeat(packageDirNameLen));
    const packageDirPath = path.resolve(packageDirName);
    const packageJsonFilePath = path.join(packageDirPath, 'package.json');
    const mainDirName = 'main';
    const mainDirPath = path.resolve(packageDirPath, mainDirName);
    const mainJsFile = 'main.js';
    const mainJsFilePath = path.resolve(mainDirPath, mainJsFile);

    tmpdir.refresh();

    fs.mkdirSync(packageDirPath);
    fs.writeFileSync(
      packageJsonFilePath,
      `{"main":"${mainDirName}/${mainJsFile}"}`
    );
    fs.mkdirSync(mainDirPath);
    fs.writeFileSync(mainJsFilePath, 'console.log("hello world");');

    require('internal/modules/run_main').executeUserEntryPoint(packageDirPath);

    tmpdir.refresh();
  });

  it('check long path in LegacyMainResolve - 1', () => {
    // Module layout will be the following:
    //  package.json
    //  index.js
    const packageDirNameLen = Math.max(260 - tmpdir.path.length, 1);
    const packageDirPath = tmpdir.resolve('y'.repeat(packageDirNameLen));
    const packageJSPath = path.join(packageDirPath, 'package.json');
    const indexJSPath = path.join(packageDirPath, 'index.js');
    const packageConfig = { };

    tmpdir.refresh();

    fs.mkdirSync(packageDirPath);
    fs.writeFileSync(packageJSPath, '');
    fs.writeFileSync(indexJSPath, '');

    const packageJsonUrl = pathToFileURL(
      path.resolve(packageJSPath)
    );

    console.log(legacyMainResolve(packageJsonUrl, packageConfig, ''));
  });

  it('check long path in LegacyMainResolve - 2', () => {
    // Module layout will be the following:
    //  package.json
    //  main.js
    const packageDirNameLen = Math.max(260 - tmpdir.path.length, 1);
    const packageDirPath = tmpdir.resolve('z'.repeat(packageDirNameLen));
    const packageJSPath = path.join(packageDirPath, 'package.json');
    const indexJSPath = path.join(packageDirPath, 'main.js');
    const packageConfig = { main: 'main.js' };

    tmpdir.refresh();

    fs.mkdirSync(packageDirPath);
    fs.writeFileSync(packageJSPath, '');
    fs.writeFileSync(indexJSPath, '');

    const packageJsonUrl = pathToFileURL(
      path.resolve(packageJSPath)
    );

    console.log(legacyMainResolve(packageJsonUrl, packageConfig, ''));
  });

  it('check long path in GetNearestParentPackageJSON', async () => {
    // Module layout will be the following:
    // node_modules
    //   test-module
    //     package.json (path is less than 260 chars long)
    //     cjs
    //       package.json (path is more than 260 chars long)
    //       index.js
    const testModuleDirPath = 'node_modules/test-module';
    let cjsDirPath = path.join(testModuleDirPath, 'cjs');
    let modulePackageJSPath = path.join(testModuleDirPath, 'package.json');
    let cjsPackageJSPath = path.join(cjsDirPath, 'package.json');
    let cjsIndexJSPath = path.join(cjsDirPath, 'index.js');

    const tmpDirNameLen = Math.max(
      260 - tmpdir.path.length - cjsPackageJSPath.length, 1);
    const tmpDirPath = tmpdir.resolve('k'.repeat(tmpDirNameLen));

    cjsDirPath = path.join(tmpDirPath, cjsDirPath);
    modulePackageJSPath = path.join(tmpDirPath, modulePackageJSPath);
    cjsPackageJSPath = path.join(tmpDirPath, cjsPackageJSPath);
    cjsIndexJSPath = path.join(tmpDirPath, cjsIndexJSPath);

    tmpdir.refresh();

    fs.mkdirSync(cjsDirPath, { recursive: true });
    fs.writeFileSync(
      modulePackageJSPath,
      `{
        "type": "module"
      }`
    );
    fs.writeFileSync(
      cjsPackageJSPath,
      `{
        "type": "commonjs"
      }`
    );
    fs.writeFileSync(
      cjsIndexJSPath,
      'const fs = require("fs");'
    );
    const { code, signal, stderr } = await spawnPromisified(execPath, [cjsIndexJSPath]);
    assert.strictEqual(stderr.trim(), '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('check long path in GetNearestParentPackageJSONType', async () => {
    // Module layout will be the following:
    // node_modules
    //   test-module
    //     package.json (path is less than 260 chars long)
    //     cjs
    //       package.json (path is more than 260 chars long)
    //       index.js
    const testModuleDirPath = 'node_modules/test-module';
    let cjsDirPath = path.join(testModuleDirPath, 'cjs');
    let modulePackageJSPath = path.join(testModuleDirPath, 'package.json');
    let cjsPackageJSPath = path.join(cjsDirPath, 'package.json');
    let cjsIndexJSPath = path.join(cjsDirPath, 'index.js');

    const tmpDirNameLen = Math.max(260 - tmpdir.path.length - cjsPackageJSPath.length, 1);
    const tmpDirPath = tmpdir.resolve('l'.repeat(tmpDirNameLen));

    cjsDirPath = path.join(tmpDirPath, cjsDirPath);
    modulePackageJSPath = path.join(tmpDirPath, modulePackageJSPath);
    cjsPackageJSPath = path.join(tmpDirPath, cjsPackageJSPath);
    cjsIndexJSPath = path.join(tmpDirPath, cjsIndexJSPath);

    tmpdir.refresh();

    fs.mkdirSync(cjsDirPath, { recursive: true });
    fs.writeFileSync(
      modulePackageJSPath,
      `{
        "type": "module"
      }`
    );
    fs.writeFileSync(
      cjsPackageJSPath,
      `{
        "type": "commonjs"
      }`
    );

    fs.writeFileSync(cjsIndexJSPath, 'import fs from "node:fs/promises";');
    const { code, signal, stderr } = await spawnPromisified(execPath, [cjsIndexJSPath]);

    assert.ok(stderr.includes('Failed to load the ES module'));
    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });
});
