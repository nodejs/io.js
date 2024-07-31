// Flags: --test-reporter=spec
'use strict';
const {
  after,
  afterEach,
  before,
  beforeEach,
  describe,
  it,
} = require('node:test');

describe('skip all hooks in this suite', { skip: true }, () => {
  before(() => {
    console.log('BEFORE 1');
  });

  beforeEach(() => {
    console.log('BEFORE EACH 1');
  });

  after(() => {
    console.log('AFTER 1');
  });

  afterEach(() => {
    console.log('AFTER EACH 1');
  });

  it('should not run');
});

describe('suite runs with mixture of skipped tests', () => {
  before(() => {
    console.log('BEFORE 2');
  });

  beforeEach(() => {
    console.log('BEFORE EACH 2');
  });

  after(() => {
    console.log('AFTER 2');
  });

  afterEach(() => {
    console.log('AFTER EACH 2');
  });

  it('should not run', { skip: true });

  it('should run 1', () => {
    console.log('should run 1');
  });

  it('should not run', { skip: true });

  it('should run 2', () => {
    console.log('should run 2');
  });
});
