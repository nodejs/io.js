'use strict';

// Flags: --expose-internals

const common = require('../common');
if (!common.isWindows) {
  common.skip('this test is Windows-specific.');
}

const fs = require('fs');
const path = require('path');
const tmpdir = require('../common/tmpdir');

// https://github.com/nodejs/node/issues/50753
// Make a path that is more than 260 chars long.
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
