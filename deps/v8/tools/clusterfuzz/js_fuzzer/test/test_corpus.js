// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Corpus loading.
 */

'use strict';

const assert = require('assert');
const sinon = require('sinon');

const corpus = require('../corpus.js');
const exceptions = require('../exceptions.js');
const helpers = require('./helpers.js');
const sourceHelpers = require('../source_helpers.js');

const sandbox = sinon.createSandbox();

function testSoftSkipped(count, softSkipped, paths) {
  sandbox.stub(exceptions, 'getSoftSkipped').callsFake(() => {
    return softSkipped;
  });
  const mjsunit = corpus.create('test_data', 'mjsunit_softskipped');
  const cases = mjsunit.getRandomTestcasePaths(count);
  assert.deepEqual(paths, cases);
}

describe('Loading corpus', () => {
  afterEach(() => {
    sandbox.restore();
  });

  it('keeps all tests with no soft-skipped tests', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0.9);
    testSoftSkipped(
        3,
        [],
        ['mjsunit_softskipped/permitted.js',
         'mjsunit_softskipped/object-literal.js',
         'mjsunit_softskipped/regress/binaryen-123.js']);
  });

  it('choose one test with no soft-skipped tests', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0.9);
    testSoftSkipped(
        1,
        [],
        ['mjsunit_softskipped/permitted.js']);
  });

  it('keeps soft-skipped tests', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0.9);
    testSoftSkipped(
        1,
        [/^binaryen.*\.js/, 'object-literal.js'],
        ['mjsunit_softskipped/permitted.js']);
  });

  it('keeps no generated soft-skipped tests', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0.9);
    const softSkipped = [
      // Correctly listed full relative path of test case.
      'mjsunit_softskipped/regress/binaryen-123.js',
      // Only basename doesn't match.
      'object-literal.js',
      // Only pieces of the path don't match.
      'mjsunit_softskipped',
    ];
    sandbox.stub(exceptions, 'getGeneratedSoftSkipped').callsFake(
        () => { return new Set(softSkipped); });
    testSoftSkipped(
        2,
        // None soft-skipped for basenames and regexps.
        [],
        // Only binaryen-123.js gets filtered out.
        ['mjsunit_softskipped/object-literal.js',
         'mjsunit_softskipped/permitted.js']);
  });

  it('keeps soft-skipped tests by chance', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0);
    testSoftSkipped(
        3,
        [/^binaryen.*\.js/, 'object-literal.js'],
        ['mjsunit_softskipped/object-literal.js',
         'mjsunit_softskipped/regress/binaryen-123.js',
         'mjsunit_softskipped/permitted.js']);
  });

  it('keeps soft-skipped paths', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0.9);
    sandbox.stub(exceptions, 'getSoftSkippedPaths').callsFake(() => {
      return [/not_/];
    });
    const crashtests = corpus.create('test_data', 'crashtests_softskipped');
    const cases = crashtests.getRandomTestcasePaths(2);
    assert.deepEqual(
        ['crashtests_softskipped/test.js',
         'crashtests_softskipped/not_great.js'],
        cases);
  });

  it('caches relative paths', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0);
    sandbox.stub(exceptions, 'getSoftSkipped').callsFake(
        () => { return ['object-literal.js']; });
    const generatedSoftSkipped = [
      'mjsunit_softskipped/regress/binaryen-123.js',
    ];
    sandbox.stub(exceptions, 'getGeneratedSoftSkipped').callsFake(
        () => { return new Set(generatedSoftSkipped); });
    const mjsunit = corpus.create('test_data' , 'mjsunit_softskipped');
    assert.deepEqual(
        ['mjsunit_softskipped/object-literal.js',
         'mjsunit_softskipped/regress/binaryen-123.js'],
        mjsunit.softSkippedFiles);
    assert.deepEqual(
        ['mjsunit_softskipped/permitted.js'],
        mjsunit.permittedFiles);
    assert.deepEqual(
        ['mjsunit_softskipped/permitted.js',
         'mjsunit_softskipped/object-literal.js',
         'mjsunit_softskipped/regress/binaryen-123.js'],
        Array.from(mjsunit.relFiles()));
  });

  it('drops discouraged files', () => {
    // Test that files with dropped flags are also dropped. This
    // test sets this probability to 1.
    sandbox.stub(Math, 'random').callsFake(() => 0.9);
    sandbox.stub(corpus, 'DROP_DISCOURAGED_FILES_PROB').value(1);
    const v8 = corpus.create('test_data/flags/corpus', 'v8');
    const cases = v8.getRandomTestcases(3).map(x => x.relPath);
    assert.deepEqual(['v8/input3.js'], cases);
  })

  it('keeps discouraged files', () => {
    // Test that files with dropped flags are sometimes kept. This
    // test sets this probability to 0.
    sandbox.stub(Math, 'random').callsFake(() => 0.9);
    sandbox.stub(corpus, 'DROP_DISCOURAGED_FILES_PROB').value(0);
    const v8 = corpus.create('test_data/flags/corpus', 'v8');
    const cases = v8.getRandomTestcases(3).map(x => x.relPath);
    assert.deepEqual(['v8/input2.js', 'v8/input1.js', 'v8/input3.js'], cases);
  })
});


describe('Fuzzilli corpus', () => {
  afterEach(() => {
    sandbox.restore();
  });

  it('loads sources with all flags', () => {
    const fuzzilli = corpus.create(helpers.BASE_DIR, 'fuzzilli');
    const source = sourceHelpers.loadSource(
        fuzzilli, 'fuzzilli/fuzzdir-1/corpus/program_1.js');
    const expectedFlags = [
      '--expose-gc',
      '--fuzzing',
      '--fuzzilli-flag1',
      '--random-seed=123',
      '--fuzzilli-flag2'
    ];
    assert.deepEqual(expectedFlags, source.flags);
  });
});
