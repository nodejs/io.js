// Flags: --test-name-pattern=enabled --test-name-pattern=yes --test-name-pattern=/pattern/i --test-name-pattern=/^DescribeForMatchWithAncestors\sNestedDescribeForMatchWithAncestors\sNestedTest$/
'use strict';
const common = require('../../../common');
const {
  after,
  afterEach,
  before,
  beforeEach,
  describe,
  it,
  test,
} = require('node:test');

test('top level test disabled', common.mustNotCall());
test('top level skipped test disabled', { skip: true }, common.mustNotCall());
test('top level skipped test enabled', { skip: true }, common.mustNotCall());
it('top level it enabled', common.mustCall());
it('top level it disabled', common.mustNotCall());
it.skip('top level skipped it disabled', common.mustNotCall());
it.skip('top level skipped it enabled', common.mustNotCall());
describe('top level describe never disabled', common.mustCall());
describe.skip('top level skipped describe disabled', common.mustNotCall());
describe.skip('top level skipped describe enabled', common.mustNotCall());
test('top level runs because name includes PaTtErN', common.mustCall());

test('top level test enabled', common.mustCall(async (t) => {
  t.beforeEach(common.mustCall());
  t.afterEach(common.mustCall());
  await t.test(
    'nested test runs because name includes PATTERN',
    common.mustCall(),
  );
}));

describe('top level describe enabled', () => {
  before(common.mustCall());
  beforeEach(common.mustCall(3));
  afterEach(common.mustCall(3));
  after(common.mustCall());

  it('nested it not disabled', common.mustCall());
  it('nested it enabled', common.mustCall());
  describe('nested describe not disabled', common.mustCall());
  describe('nested describe enabled', common.mustCall(() => {
    it('is enabled', common.mustCall());
  }));
});

describe('yes', function() {
  it('no', () => {});
  it('yes', () => {});

  describe('maybe', function() {
    it('no', () => {});
    it('yes', () => {});
  });
});

describe('no', function() {
  it('no', () => {});
  it('yes', () => {});

  describe('maybe', function() {
    it('no', () => {});
    it('yes', () => {});
  });
});

describe('no with todo', { todo: true }, () => {
  it('no', () => {});
  it('yes', () => {});

  describe('maybe', function() {
    it('no', () => {});
    it('yes', () => {});
  });
});

describe('DescribeForMatchWithAncestors', () => {
  it('NestedTest', () => common.mustNotCall());

  describe('NestedDescribeForMatchWithAncestors', () => {
    it('NestedTest', common.mustCall());
  });
})

describe('DescribeForMatchWithAncestors', () => {
  it('NestedTest', () => common.mustNotCall());
})
