'use strict';
const { test, describe, it } = require('node:test');

describe('should NOT print --test-only diagnostic warning - describe-only-false', {only: false}, () => {
    it('only false in describe');
  });
  
  describe('should NOT print --test-only diagnostic warning - it-only-false', () => {
    it('only false in the subtest', {only: false});
  });
  
  describe('should NOT print --test-only diagnostic warning - no-only', () => {
    it('no only');
  });
  
  test('should NOT print --test-only diagnostic warning - test-only-false', {only: false}, async (t) => {
    await t.test('only false in parent test');
  });
  
  test('should NOT print --test-only diagnostic warning - t.test-only-false', async (t) => {
    await t.test('only false in subtest', {only: false});
  });
  
  test('should NOT print --test-only diagnostic warning - no-only', async (t) => {
    await t.test('no only');
  });
  
  test('should print --test-only diagnostic warning - test-only-true', {only: true}, async (t) => {
    await t.test('only true in parent test');
  })
  
  test('should print --test-only diagnostic warning - t.test-only-true', async (t) => {
    await t.test('only true in subtest', {only: true});
  });
  
  test('should print --test-only diagnostic warning - 2 levels of only', async (t) => {
    await t.test('only true in parent test', {only: false}, async (t) => {
      await t.test('only true in subtest', {only: true});
    });
  })
