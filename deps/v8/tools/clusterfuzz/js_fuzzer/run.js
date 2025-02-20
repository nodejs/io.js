// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Description of this file.
 */

'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const program = require('commander');

const differentialScriptMutator = require('./differential_script_mutator.js');
const random = require('./random.js');
const runner = require('./runner.js');
const scriptMutator = require('./script_mutator.js');
const sourceHelpers = require('./source_helpers.js');


// Base implementations for default or differential fuzzing.
const SCRIPT_MUTATORS = {
  default: scriptMutator.ScriptMutator,
  foozzie: differentialScriptMutator.DifferentialScriptMutator,
};

function collect(value, total) {
  total.push(value);
  return total;
}

function overrideSettings(settings, settingOverrides) {
  for (const setting of settingOverrides) {
    const parts = setting.split('=');
    settings[parts[0]] = parseFloat(parts[1]);
  }
}

function main() {
  Error.stackTraceLimit = Infinity;

  program
    .version('0.0.1')
    .option('-i, --input_dir <path>', 'Input directory.')
    .option('-o, --output_dir <path>', 'Output directory.')
    .option('-n, --no_of_files <n>', 'Output directory.', parseInt)
    .option('-c, --mutate_corpus <name>', 'Mutate single files in a corpus.')
    .option('-e, --extra_strict', 'Additionally parse files in strict mode.')
    .option('-m, --mutate <path>', 'Mutate a file and output results.')
    .option('-s, --setting [setting]', 'Settings overrides.', collect, [])
    .option('-v, --verbose', 'More verbose printing.')
    .option('-z, --zero_settings', 'Zero all settings.')
    .parse(process.argv);

  const settings = scriptMutator.defaultSettings();
  if (program.zero_settings) {
    for (const key of Object.keys(settings)) {
      settings[key] = 0.0;
    }
  }

  if (program.setting.length > 0) {
    overrideSettings(settings, program.setting);
  }

  let app_name = process.env.APP_NAME;
  if (app_name && app_name.endsWith('.exe')) {
    app_name = app_name.substr(0, app_name.length - 4);
  }

  if (app_name === 'd8' ||
      app_name === 'v8_simple_inspector_fuzzer' ||
      app_name === 'v8_foozzie.py') {
    // V8 supports running the raw d8 executable, the inspector fuzzer or
    // the differential fuzzing harness 'foozzie'.
    settings.engine = 'v8';
  } else if (app_name === 'ch') {
    settings.engine = 'chakra';
  } else if (app_name === 'js') {
    settings.engine = 'spidermonkey';
  } else if (app_name === 'jsc') {
    settings.engine = 'jsc';
  } else {
    console.log('ERROR: Invalid APP_NAME');
    process.exit(1);
  }

  const mode = process.env.FUZZ_MODE || 'default';
  assert(mode in SCRIPT_MUTATORS, `Unknown mode ${mode}`);
  const mutator = new SCRIPT_MUTATORS[mode](settings);

  if (program.mutate) {
    const absPath = path.resolve(program.mutate);
    const baseDir = path.dirname(absPath);
    const fileName = path.basename(absPath);
    const corpus = new sourceHelpers.BaseCorpus(baseDir);
    const input = sourceHelpers.loadSource(
        corpus, fileName, program.extra_strict);
    const mutated = mutator.mutateMultiple([input]);
    console.log(mutated.code);
    return;
  }

  let testRunner;
  if (program.mutate_corpus) {
    testRunner = new runner.SingleCorpusRunner(
        program.input_dir, program.mutate_corpus, program.extra_strict);
  } else {
    testRunner = new mutator.runnerClass(
        program.input_dir, settings.engine, program.no_of_files);
  }

  for (const [i, inputs] of testRunner.enumerateInputs()) {
    const outputPath = path.join(program.output_dir, 'fuzz-' + i + '.js');

    const start = Date.now();
    const paths = inputs.map(input => input.relPath);

    try {
      const mutated = mutator.mutateMultiple(inputs);
      fs.writeFileSync(outputPath, mutated.code);

      if (settings.engine === 'v8' && mutated.flags && mutated.flags.length > 0) {
        const flagsPath = path.join(program.output_dir, 'flags-' + i + '.js');
        fs.writeFileSync(flagsPath, mutated.flags.join(' '));
      }
    } catch (e) {
      if (e.message.startsWith('ENOSPC')) {
        console.log('ERROR: No space left. Bailing out...');
        console.log(e);
        return;
      }
      console.log(`ERROR: Exception during mutate: ${paths}`);
      console.log(e);
      continue;
    } finally {
      if (program.verbose) {
        const duration = Date.now() - start;
        console.log(`Mutating ${paths} took ${duration} ms.`);
      }
    }
    if ((i + 1)  % 10 == 0) {
      console.log('Up to ', i + 1);
    }
  }
}

main();
