// Flags: --test-only
'use strict';
const common = require('../../../common');
const { test, describe, it } = require('node:test');

// These tests should be skipped based on the 'only' option.
test('only = undefined', common.mustNotCall());
test('only = undefined, skip = string', { skip: 'skip message' }, common.mustNotCall());
test('only = undefined, skip = true', { skip: true }, common.mustNotCall());
test('only = undefined, skip = false', { skip: false }, common.mustNotCall());
test('only = false', { only: false }, common.mustNotCall());
test('only = false, skip = string', { only: false, skip: 'skip message' }, common.mustNotCall());
test('only = false, skip = true', { only: false, skip: true }, common.mustNotCall());
test('only = false, skip = false', { only: false, skip: false }, common.mustNotCall());

// These tests should be skipped based on the 'skip' option.
test('only = true, skip = string', { only: true, skip: 'skip message' }, common.mustNotCall());
test('only = true, skip = true', { only: true, skip: true }, common.mustNotCall());

// An 'only' test with subtests.
test('only = true, with subtests', { only: true }, common.mustCall(async (t) => {
  // These subtests should run.
  await t.test('running subtest 1', common.mustCall());
  await t.test('running subtest 2', common.mustCall());

  // Switch the context to only execute 'only' tests.
  t.runOnly(true);
  await t.test('skipped subtest 1', common.mustNotCall());
  await t.test('skipped subtest 2'), common.mustNotCall();
  await t.test('running subtest 3', { only: true }, common.mustCall());

  // Switch the context back to execute all tests.
  t.runOnly(false);
  await t.test('running subtest 4', common.mustCall(async (t) => {
    // These subtests should run.
    await t.test('running sub-subtest 1', common.mustCall());
    await t.test('running sub-subtest 2', common.mustCall());

    // Switch the context to only execute 'only' tests.
    t.runOnly(true);
    await t.test('skipped sub-subtest 1', common.mustNotCall());
    await t.test('skipped sub-subtest 2', common.mustNotCall());
  }));

  // Explicitly do not run these tests.
  await t.test('skipped subtest 3', { only: false }, common.mustNotCall());
  await t.test('skipped subtest 4', { skip: true }, common.mustNotCall());
}));

describe.only('describe only = true, with subtests', common.mustCall(() => {
  it.only('`it` subtest 1 should run', common.mustCall());

  it('`it` subtest 2 should not run', common.mustNotCall());
}));

describe.only('describe only = true, with a mixture of subtests', common.mustCall(() => {
  it.only('`it` subtest 1', common.mustCall());

  it.only('`it` async subtest 1', common.mustCall(async () => {}));

  it('`it` subtest 2 only=true', { only: true }, common.mustCall());

  it('`it` subtest 2 only=false', { only: false }, common.mustNotCall());

  it.skip('`it` subtest 3 skip', common.mustNotCall());

  it.todo('`it` subtest 4 todo', { only: false }, common.mustNotCall());

  test.only('`test` subtest 1', common.mustCall());

  test.only('`test` async subtest 1', common.mustCall(async () => {}));

  test('`test` subtest 2 only=true', { only: true }, common.mustCall());

  test('`test` subtest 2 only=false', { only: false }, common.mustNotCall());

  test.skip('`test` subtest 3 skip', common.mustNotCall());

  test.todo('`test` subtest 4 todo', { only: false }, common.mustNotCall());
}));

describe.only('describe only = true, with subtests', common.mustCall(() => {
  test.only('subtest should run', common.mustCall());

  test('async subtest should not run', common.mustNotCall());

  test('subtest should be skipped', { only: false }, common.mustNotCall());
}));


describe('describe only = undefined, with nested only subtest', common.mustCall(() => {
  test('subtest should not run', common.mustNotCall());
  describe('nested describe', common.mustCall(() => {
    test('subtest should not run', common.mustNotCall());
    test.only('nested test should run', common.mustCall());
  }));
}));


describe('describe only = undefined, with subtests', common.mustCall(() => {
  test('async subtest should not run', common.mustNotCall());
}));

describe('describe only = false, with subtests', { only: false }, common.mustNotCall(() => {
  test('async subtest should not run', common.mustNotCall());
}));


describe.only('describe only = true, with nested subtests', common.mustCall(() => {
  test('async subtest should run', common.mustCall());
  describe('nested describe', common.mustCall(() => {
    test('nested test should run', common.mustCall());
  }));
}));

describe('describe only = false, with nested only subtests', { only: false }, common.mustNotCall(() => {
  test('async subtest should not run', common.mustNotCall());
  describe('nested describe', common.mustNotCall(() => {
    test.only('nested test should run', common.mustNotCall());
  }));
}));

test('nested tests with run only',{only: true}, common.mustCall(async (t) => {
  // Within this test, all subtests are run by default.
  await t.test('running subtest - 1');

  // The test context can be updated to run subtests with the 'only' option.
  await t.runOnly(true);
  await t.test('this subtest is now skipped - 2', common.mustNotCall());
  await t.test('this subtest is run  - 3', { only: true }, common.mustCall(async (t) => {
    await t.test('this subtest is run - 4', common.mustCall());
    await t.test('this subtest is run - 5', { only: true }, common.mustCall());
  }));

  // Switch the context back to execute all tests.
  await t.runOnly(false);
  await t.test('this subtest is now run - 6');

  // Explicitly do not run these tests.
  await t.test('skipped subtest - 7', { only: false }, common.mustNotCall());
  await t.test('skipped subtest - 8', { skip: true }, common.mustNotCall());
}))
