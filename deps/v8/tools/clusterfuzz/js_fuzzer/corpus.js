// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Corpus
 */


const assert = require('assert');
const fs = require('fs');
const path = require('path');
const program = require('commander');

const exceptions = require('./exceptions.js');
const random = require('./random.js');
const sourceHelpers = require('./source_helpers.js');

// We drop files containing dropped flags with a high probability.
DROP_DISCOURAGED_FILES_PROB = 0.8;

function* walkDirectory(directory, filter) {
  // Generator for recursively walk a directory.
  for (const filePath of fs.readdirSync(directory)) {
    const currentPath = path.join(directory, filePath);
    const stat = fs.lstatSync(currentPath);
    if (stat.isFile()) {
      if (!filter || filter(currentPath)) {
        yield currentPath;
      }
      continue;
    }

    if (stat.isDirectory()) {
      for (let childFilePath of walkDirectory(currentPath, filter)) {
        yield childFilePath;
      }
    }
  }
}

class Corpus extends sourceHelpers.BaseCorpus {
  // Input corpus.
  constructor(inputDir, corpusName, extraStrict=false) {
    super(inputDir);
    this.extraStrict = extraStrict;

    // Filter for permitted JS files.
    function isPermittedJSFile(absPath) {
      return (absPath.endsWith('.js') &&
              !exceptions.isTestSkippedAbs(absPath));
    }

    // Cache relative paths of all files in corpus.
    this.skippedFiles = [];
    this.softSkippedFiles = [];
    this.permittedFiles = [];
    const directory = path.join(inputDir, corpusName);
    for (const absPath of walkDirectory(directory, isPermittedJSFile)) {
      const relPath = path.relative(this.inputDir, absPath);
      if (exceptions.isTestSkippedRel(relPath)) {
        this.skippedFiles.push(relPath);
      } else if (exceptions.isTestSoftSkippedAbs(absPath) ||
          exceptions.isTestSoftSkippedRel(relPath)) {
        this.softSkippedFiles.push(relPath);
      } else {
        this.permittedFiles.push(relPath);
      }
    }
    random.shuffle(this.softSkippedFiles);
    random.shuffle(this.permittedFiles);
  }

  // Relative paths of all files in corpus.
  *relFiles() {
    for (const relPath of this.permittedFiles) {
      yield relPath;
    }
    for (const relPath of this.softSkippedFiles) {
      yield relPath;
    }
  }

  // Relative paths of all files in corpus including generated skipped.
  *relFilesForGenSkipped() {
    for (const relPath of this.relFiles()) {
      yield relPath;
    }
    for (const relPath of this.skippedFiles) {
      yield relPath;
    }
  }

  /**
   * Returns "count" relative test paths, randomly selected from soft-skipped
   * and permitted files. Permitted files have a 4 times higher chance to
   * be chosen.
   */
  getRandomTestcasePaths(count) {
    return random.twoBucketSample(
        this.softSkippedFiles, this.permittedFiles, 4, count);
  }

  loadTestcase(relPath, strict, label) {
    const start = Date.now();
    try {
      const source = sourceHelpers.loadSource(this, relPath, strict);
      if (program.verbose) {
        const duration = Date.now() - start;
        console.log(`Parsing ${relPath} ${label} took ${duration} ms.`);
      }
      if (exceptions.hasFlagsDiscouragingFiles(source.flags) &&
          random.choose(module.exports.DROP_DISCOURAGED_FILES_PROB)) {
        return undefined
      }
      return source;
    } catch (e) {
      console.log(`WARNING: failed to ${label} parse ${relPath}`);
      console.log(e);
    }
    return undefined;
  }

  *loadTestcases(relPaths) {
    for (const relPath of relPaths) {
      if (this.extraStrict) {
        // When re-generating the files marked sloppy, we additionally test if
        // the file parses in strict mode.
        this.loadTestcase(relPath, true, 'strict');
      }
      const source = this.loadTestcase(relPath, false, 'sloppy');
      if (source) {
        yield source;
      }
    }
  }

  getRandomTestcases(count) {
    return Array.from(this.loadTestcases(this.getRandomTestcasePaths(count)));
  }

  getAllTestcases() {
    return this.loadTestcases(this.relFilesForGenSkipped());
  }
}

class FuzzilliCorpus extends Corpus {
  constructor(inputDir, corpusName, extraStrict=false) {
    super(inputDir, corpusName, extraStrict);
    this.flagMap = new Map();
  }

  get diffFuzzLabel() {
    // Sources from Fuzzilli use the same universal label for differential
    // fuzzing because the input file path is random and volatile. It can't
    // be used to map to particular content.
    return "fuzzilli_source";
  }

  loadFlags(relPath, data) {
    // Create the settings path based on the location of the test file, e.g.
    // .../fuzzilli/fuzzdir-1/settings.json for
    // .../fuzzilli/fuzzdir-1/corpus/program_x.js
    const pathComponents = relPath.split(path.sep);
    assert(pathComponents.length > 1);
    assert(pathComponents[0] == 'fuzzilli');
    const settingsPath = path.join(
        this.inputDir, pathComponents[0], pathComponents[1], 'settings.json');

    // Use cached flags if already loaded.
    let flags = this.flagMap.get(settingsPath);
    if (flags == undefined) {
      assert(fs.existsSync(settingsPath));
      const settings = JSON.parse(fs.readFileSync(settingsPath, 'utf-8'));
      flags = settings["processArguments"];
      this.flagMap.set(settingsPath, flags);
    }
    return flags;
  }
}

class V8Corpus extends Corpus {
  loadFlags(relPath, data) {
    const result = [];
    let count = 0;
    for (const line of data.split('\n')) {
      if (count++ > 40) {
        // No need to process the whole file. Flags are always added after the
        // copyright header.
        break;
      }
      const match = line.match(/\/\/ Flags:\s*(.*)\s*/);
      if (!match) {
        continue;
      }
      for (const flag of match[1].split(/\s+/)) {
        result.push(flag);
      }
    }
    return result;
  }
}

const CORPUS_CLASSES = {
  'fuzzilli': FuzzilliCorpus,
  'v8': V8Corpus,
};

function create(inputDir, corpusName, extraStrict=false) {
  const cls = CORPUS_CLASSES[corpusName] || Corpus;
  return new cls(inputDir, corpusName, extraStrict);
}

module.exports = {
  DROP_DISCOURAGED_FILES_PROB: DROP_DISCOURAGED_FILES_PROB,
  create: create,
  walkDirectory: walkDirectory,
}
