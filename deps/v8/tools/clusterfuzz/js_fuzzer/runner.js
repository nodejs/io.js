// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Main execution flow.
 */

'use strict';

const path = require('path');

const corpus = require('./corpus.js');
const random = require('./random.js');

// Maximum number of test inputs to use for one fuzz test.
const MAX_TEST_INPUTS_PER_TEST = 10;

/**
 * Returns an array of maxium `count` parsed input sources, randomly
 * selected from a primary corpus and a list of secondary corpora.
 */
function getRandomInputs(primaryCorpus, secondaryCorpora, count) {
  count = random.randInt(2, count);

  // Choose 40%-80% of inputs from primary corpus.
  const primaryCount = Math.floor(random.uniform(0.4, 0.8) * count);
  count -= primaryCount;

  let inputs = primaryCorpus.getRandomTestcases(primaryCount);

  // Split remainder equally between the secondary corpora.
  const secondaryCount = Math.floor(count / secondaryCorpora.length);

  for (let i = 0; i < secondaryCorpora.length; i++) {
    let currentCount = secondaryCount;
    if (i == secondaryCorpora.length - 1) {
      // Last one takes the remainder.
      currentCount = count;
    }

    count -= currentCount;
    if (currentCount) {
      inputs = inputs.concat(
          secondaryCorpora[i].getRandomTestcases(currentCount));
    }
  }

  return random.shuffle(inputs);
}

class Runner {
  *inputGen() {
    throw new Error("Not implemented.");
  }

  *enumerateInputs() {
    let i = 0;
    for (const value of this.inputGen()) {
      yield [i, value];
      i++;
    }
  }
}

/**
 * Runner that randomly selects a number of tests from all corpora.
 */
class RandomCorpusRunner extends Runner {
  constructor(inputDir, primary, numFiles,
              maxTestInputs=MAX_TEST_INPUTS_PER_TEST) {
    super();
    inputDir = path.resolve(inputDir);
    this.primary = primary;
    this.numFiles = numFiles;
    this.maxTestInputs = maxTestInputs;
    this.corpora = {
      'v8': corpus.create(inputDir, 'v8'),
      'chakra': corpus.create(inputDir, 'chakra'),
      'spidermonkey': corpus.create(inputDir, 'spidermonkey'),
      'jsc': corpus.create(inputDir, 'WebKit/JSTests'),
      'crash': corpus.create(inputDir, 'CrashTests'),
    };
  }

  *inputGen() {
    const primary = this.corpora[this.primary];
    const secondary = Object.values(this.corpora);

    for (let i = 0; i < this.numFiles; i++) {
      const inputs = getRandomInputs(
          primary,
          random.shuffle(secondary),
          this.maxTestInputs);

      if (inputs.length > 0) {
        yield inputs;
      }
    }
  }
}

/**
 * Like above, including the Fuzzilli corpus.
 */
class RandomCorpusRunnerWithFuzzilli extends RandomCorpusRunner {
  constructor(inputDir, primary, numFiles,
              maxTestInputs=MAX_TEST_INPUTS_PER_TEST) {
    super(inputDir, primary, numFiles, maxTestInputs);
    this.corpora['fuzzilli'] = corpus.create(inputDir, 'fuzzilli');
  }
}

/**
 * Runner that enumerates all tests from a particular corpus.
 */
class SingleCorpusRunner extends Runner {
  constructor(inputDir, corpusName, extraStrict) {
    super();
    this.corpus = corpus.create(
      path.resolve(inputDir), corpusName, extraStrict);
  }

  *inputGen() {
    for (const input of this.corpus.getAllTestcases()) {
      yield [input];
    }
  }
}

module.exports = {
  RandomCorpusRunner: RandomCorpusRunner,
  RandomCorpusRunnerWithFuzzilli: RandomCorpusRunnerWithFuzzilli,
  SingleCorpusRunner: SingleCorpusRunner,
};
