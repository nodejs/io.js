'use strict';
const common = require('../common');
if (process.config.variables.node_without_node_options)
  common.skip('missing NODE_OPTIONS support');

// Test options specified by env variable.

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const rootDir = path.resolve(__dirname, '..', '..');
const cliMd = path.join(rootDir, 'doc', 'api', 'cli.md');
const cliText = fs.readFileSync(cliMd, { encoding: 'utf8' });

const internalApiMd = path.join(rootDir, 'doc', 'contributing', 'internal-api.md');
const internalApiText = fs.readFileSync(internalApiMd, { encoding: 'utf8' });

const nodeOptionsCC = fs.readFileSync(path.resolve(rootDir, 'src', 'node_options.cc'), 'utf8');
const addOptionRE = /AddOption[\s\n\r]*\([\s\n\r]*"([^"]+)"(.*?)\);/gs;

const nodeOptionsText = cliText.match(/<!-- node-options-node start -->(.*)<!-- node-options-others end -->/s)[1];
const v8OptionsText = cliText.match(/<!-- v8-options start -->(.*)<!-- v8-options end -->/s)[1];

// Documented in /doc/api/deprecations.md
const deprecated = [
  '--debug',
  '--debug-brk',
];

for (const [, envVar, config] of nodeOptionsCC.matchAll(addOptionRE)) {
  let hasTrueAsDefaultValue = false;
  let isInNodeOption = false;
  let isV8Option = false;
  let isNoOp = false;

  if (config.includes('NoOp{}')) {
    isNoOp = true;
  }

  if (config.includes('kAllowedInEnvvar')) {
    isInNodeOption = true;
  }
  if (config.includes('kDisallowedInEnvvar')) {
    isInNodeOption = false;
  }

  if (config.includes('V8Option{}')) {
    isV8Option = true;
  }

  if (/^\s*true\s*$/.test(config.split(',').pop())) {
    hasTrueAsDefaultValue = true;
  }

  if (
    envVar.startsWith('[') ||
    deprecated.includes(envVar) ||
    isNoOp
  ) {
    continue;
  }

  // Internal API options are documented in /doc/contributing/internal-api.md
  if (new RegExp(`####.*\`${envVar}[[=\\s\\b\`]`).test(internalApiText) === true) {
    continue;
  }

  // CLI options
  if (!isV8Option && !hasTrueAsDefaultValue) {
    if (new RegExp(`###.*\`${envVar}[[=\\s\\b\`]`).test(cliText) === false) {
      assert(false, `Should have option ${envVar} documented`);
    }
  }

  if (!hasTrueAsDefaultValue && new RegExp(`###.*\`--no${envVar.slice(1)}[[=\\s\\b\`]`).test(cliText) === true) {
    assert(false, `Should not have option --no${envVar.slice(1)} documented`);
  }

  if (!isV8Option && hasTrueAsDefaultValue) {
    if (new RegExp(`###.*\`--no${envVar.slice(1)}[[=\\s\\b\`]`).test(cliText) === false) {
      assert(false, `Should have option --no${envVar.slice(1)} documented`);
    }
  }

  // NODE_OPTIONS
  if (isInNodeOption && !hasTrueAsDefaultValue && new RegExp(`\`${envVar}\``).test(nodeOptionsText) === false) {
    assert(false, `Should have option ${envVar} in NODE_OPTIONS documented`);
  }

  if (isInNodeOption && hasTrueAsDefaultValue && new RegExp(`\`--no${envVar.slice(1)}\``).test(cliText) === false) {
    assert(false, `Should have option --no${envVar.slice(1)} in NODE_OPTIONS documented`);
  }

  if (!hasTrueAsDefaultValue && new RegExp(`\`--no${envVar.slice(1)}\``).test(cliText) === true) {
    assert(false, `Should not have option --no${envVar.slice(1)} in NODE_OPTIONS documented`);
  }

  // V8 options
  if (isV8Option) {
    if (new RegExp(`###.*\`${envVar}[[=\\s\\b\`]`).test(v8OptionsText) === false) {
      assert(false, `Should have option ${envVar} in V8 options documented`);
    }
  }
}
