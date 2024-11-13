'use strict';
const common = require('../common');
const assert = require('assert');
const exec = require('child_process').execFile;
const { describe, it } = require('node:test');

const mjsFile = require.resolve('../fixtures/es-modules/mjs-file.mjs');
const cjsFile = require.resolve('../fixtures/es-modules/cjs-file.cjs');
const packageWithoutTypeMain =
  require.resolve('../fixtures/es-modules/package-without-type/index.js');
const packageTypeCommonJsMain =
  require.resolve('../fixtures/es-modules/package-type-commonjs/index.js');
const packageTypeModuleMain =
  require.resolve('../fixtures/es-modules/package-type-module/index.js');

// Check that running `node` without options works
expect('', mjsFile, '.mjs file');
expect('', cjsFile, '.cjs file');
expect('', packageTypeModuleMain, 'package-type-module');
expect('', packageTypeCommonJsMain, 'package-type-commonjs');
expect('', packageWithoutTypeMain, 'package-without-type');

// Check that --input-type isn't allowed for files
describe('ESM type field errors', { concurrency: true }, () => {
  it('.cjs file', () => {
    expect('', cjsFile, '.cjs file');
  });

  it('.mjs file', () => {
    expect('', mjsFile, '.mjs file');
  });

  it('package.json with "type": "module"', () => {
    expect('', packageTypeModuleMain, 'package-type-module');
  });

  it('package.json with "type": "commonjs"', () => {
    expect('', packageTypeCommonJsMain, 'package-type-commonjs');
  });

  it('package.json with no "type" field', () => {
    expect('', packageWithoutTypeMain, 'package-without-type');
  });

  it('--input-type=module disallowed for files', () => {
    expect(
      '--input-type=module',
      packageTypeModuleMain,
      'ERR_INPUT_TYPE_NOT_ALLOWED',
      true,
    );
  });
});

function expect(opt = '', inputFile, want, wantsError = false) {
  const argv = [inputFile];
  const opts = {
    env: Object.assign({}, process.env, { NODE_OPTIONS: opt }),
    maxBuffer: 1e6,
  };
  exec(process.execPath, argv, opts, common.mustCall((err, stdout, stderr) => {
    if (wantsError) {
      stdout = stderr;
    } else {
      assert.ifError(err);
    }
    if (stdout.includes(want)) return;

    const o = JSON.stringify(opt);
    assert.fail(`For ${o}, failed to find ${want} in: <\n${stdout}\n>`);
  }));
}
