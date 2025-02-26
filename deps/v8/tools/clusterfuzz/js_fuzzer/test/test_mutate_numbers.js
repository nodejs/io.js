// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Tests for mutating variables
 */

'use strict';

const babelTypes = require('@babel/types');
const sinon = require('sinon');

const arrayMutator = require('../mutators/array_mutator.js');
const common = require('../mutators/common.js');
const helpers = require('./helpers.js');
const scriptMutator = require('../script_mutator.js');
const sourceHelpers = require('../source_helpers.js');
const numberMutator = require('../mutators/number_mutator.js');
const random = require('../random.js');

const sandbox = sinon.createSandbox();

describe('Mutate numbers', () => {
  beforeEach(() => {
    sandbox.stub(common, 'nearbyRandomNumber').callsFake(
        () => { return babelTypes.numericLiteral(-3) });
    sandbox.stub(common, 'randomInterestingNumber').callsFake(
        () => { return babelTypes.numericLiteral(-4) });
    sandbox.stub(random, 'randInt').callsFake(() => { return -5 });

    // Interesting cases from number mutator.
    const interestingProbs = [0.009, 0.05, 0.5];
    sandbox.stub(random, 'random').callsFake(
        helpers.cycleProbabilitiesFun(interestingProbs));
  });

  afterEach(() => {
    sandbox.restore();
  });

  it('test multiple mutations', () => {
    // Ensure large arrays are not mutated. For testing make arrays > 2 large.
    sandbox.stub(arrayMutator, 'MAX_ARRAY_LENGTH').value(2);
    sandbox.stub(numberMutator, 'SKIP_LARGE_ARRAYS_PROB').value(1);

    const source = helpers.loadTestData('mutate_numbers.js');

    const settings = scriptMutator.defaultSettings();
    settings['MUTATE_NUMBERS'] = 1.0;

    const mutator = new numberMutator.NumberMutator(settings);
    mutator.mutate(source);

    const mutated = sourceHelpers.generateCode(source);
    helpers.assertExpectedResult(
        'mutate_numbers_expected.js', mutated);
  });

  it('test getters and setters', () => {
    // Ensure that numbers in getters and setters stay positive.
    const source = helpers.loadTestData('mutate_numbers_class.js');

    const settings = scriptMutator.defaultSettings();
    settings['MUTATE_NUMBERS'] = 1.0;

    const mutator = new numberMutator.NumberMutator(settings);
    mutator.mutate(source);

    const mutated = sourceHelpers.generateCode(source);
    helpers.assertExpectedResult(
        'mutate_numbers_class_expected.js', mutated);
  });
});
