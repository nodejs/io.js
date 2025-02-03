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

const manPage = path.join(rootDir, 'doc', 'node.1');
const manPageText = fs.readFileSync(manPage, { encoding: 'utf8' });

// Documented in /doc/api/deprecations.md
const deprecated = [
  '--debug',
  '--debug-brk',
];


const manPagesOptions = new Set();

for (const [, envVar] of manPageText.matchAll(/\.It Fl (-[a-zA-Z0-9._-]+)/g)) {
  manPagesOptions.add(envVar);
}

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
    // assert(!manPagesOptions.has(envVar.slice(1)), `Option ${envVar} should not be documented`)
    manPagesOptions.delete(envVar.slice(1));
    continue;
  }

  // Internal API options are documented in /doc/contributing/internal-api.md
  if (new RegExp(`####.*\`${envVar}[[=\\s\\b\`]`).test(internalApiText) === true) {
    manPagesOptions.delete(envVar.slice(1));
    continue;
  }

  // CLI options
  if (!isV8Option && !hasTrueAsDefaultValue) {
    if (new RegExp(`###.*\`${envVar}[[=\\s\\b\`]`).test(cliText) === false) {
      assert(false, `Should have option ${envVar} documented`);
    } else {
      manPagesOptions.delete(envVar.slice(1));
    }
  }

  if (!hasTrueAsDefaultValue && new RegExp(`###.*\`--no${envVar.slice(1)}[[=\\s\\b\`]`).test(cliText) === true) {
    assert(false, `Should not have option --no${envVar.slice(1)} documented`);
  }

  if (!isV8Option && hasTrueAsDefaultValue) {
    if (new RegExp(`###.*\`--no${envVar.slice(1)}[[=\\s\\b\`]`).test(cliText) === false) {
      assert(false, `Should have option --no${envVar.slice(1)} documented`);
    } else {
      manPagesOptions.delete(`-no${envVar.slice(1)}`);
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
    } else {
      manPagesOptions.delete(envVar.slice(1));
    }
  }
}

{
  const sections = /^## (.+)$/mg;
  const cliOptionPattern = /^### (?:`-\w.*`, )?`([^`]+)`/mg;
  let match;
  let previousIndex = 0;
  do {
    const sectionTitle = match?.[1];
    match = sections.exec(cliText);
    const filteredCLIText = cliText.slice(previousIndex, match?.index);
    const options = Array.from(filteredCLIText.matchAll(cliOptionPattern), (match) => match[1]);
    assert.deepStrictEqual(options, options.toSorted(), `doc/api/cli.md ${sectionTitle} subsections are not in alphabetical order`);
    previousIndex = match?.index;
  } while (match);
}

// add alias handling
manPagesOptions.delete('-trace-events-enabled');

assert.strictEqual(manPagesOptions.size, 0, `Man page options not documented: ${[...manPagesOptions]}`);
