// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Test helpers.
 */

'use strict';

const assert = require('assert');
const path = require('path');
const fs = require('fs');

const corpus = require('../corpus.js');
const sourceHelpers = require('../source_helpers.js');

const BASE_DIR = path.join(path.dirname(__dirname), 'test_data');
const DB_DIR = path.join(BASE_DIR, 'fake_db');

const TEST_CORPUS = new sourceHelpers.BaseCorpus(BASE_DIR);
const FUZZILLI_TEST_CORPUS = corpus.create(BASE_DIR, 'fuzzilli');
const V8_TEST_CORPUS = corpus.create(BASE_DIR, 'v8');

const HEADER = `// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

`;

/**
 * Create a function that returns one of `probs` when called. It rotates
 * through the values. Useful to replace `random.random()` in tests using
 * the probabilities that trigger different interesting cases.
 */
function cycleProbabilitiesFun(probs) {
  let index = 0;
  return () => {
    index = index % probs.length;
    return probs[index++];
  };
}

/**
 * Replace Math.random with a deterministic pseudo-random function.
 */
function deterministicRandom(sandbox) {
    let seed = 1;
    function random() {
        const x = Math.sin(seed++) * 10000;
        return x - Math.floor(x);
    }
    sandbox.stub(Math, 'random').callsFake(() => { return random(); });
}

function loadTestData(relPath) {
  return sourceHelpers.loadSource(TEST_CORPUS, relPath);
}

function loadFuzzilliTestData(relPath) {
  return sourceHelpers.loadSource(FUZZILLI_TEST_CORPUS, relPath);
}

function loadV8TestData(relPath) {
  return sourceHelpers.loadSource(V8_TEST_CORPUS, relPath);
}

function assertExpectedResult(expectedPath, result) {
  const absPath = path.join(BASE_DIR, expectedPath);
  if (process.env.GENERATE) {
    let header = HEADER;
    if (fs.existsSync(absPath)) {
      // Keep the old copyright header if the file already exists.
      const previous = fs.readFileSync(absPath, 'utf-8').trim().split('\n');
      previous.splice(3);
      header = previous.join('\n') + '\n\n';
    }
    fs.writeFileSync(absPath, header + result.trim() + '\n');
    return;
  }

  // Omit copyright header when comparing files.
  const expected = fs.readFileSync(absPath, 'utf-8').trim().split('\n');
  expected.splice(0, 4);
  assert.strictEqual(expected.join('\n'), result.trim());
}

module.exports = {
  BASE_DIR: BASE_DIR,
  DB_DIR: DB_DIR,
  TEST_CORPUS: TEST_CORPUS,
  assertExpectedResult: assertExpectedResult,
  cycleProbabilitiesFun: cycleProbabilitiesFun,
  deterministicRandom: deterministicRandom,
  loadFuzzilliTestData: loadFuzzilliTestData,
  loadTestData: loadTestData,
  loadV8TestData: loadV8TestData,
}
