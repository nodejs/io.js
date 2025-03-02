// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Test flag loading and merging.
 */

'use strict';

const assert = require('assert');
const sinon = require('sinon');

const helpers = require('./helpers.js');
const scriptMutator = require('../script_mutator.js');

const sandbox = sinon.createSandbox();

describe('Flag loading', () => {
  beforeEach(() => {
    helpers.deterministicRandom(sandbox);

    // Zero settings for all mutators.
    this.settings = scriptMutator.defaultSettings();
    for (const key of Object.keys(this.settings)) {
      this.settings[key] = 0.0;
    }
  });

  afterEach(() => {
    sandbox.restore();
  });

  it('drops blocked flags', () => {
    const inputs = [
        'flags/blocked_flags/input1.js',
        'flags/blocked_flags/input2.js'];
    const sources = inputs.map(input => helpers.loadV8TestData(input));
    const mutator = new scriptMutator.ScriptMutator(this.settings, helpers.DB_DIR);
    const mutated = mutator.mutateMultiple(sources);
    assert.deepEqual(['--future', '--js-staging'], mutated.flags);
  });
});
