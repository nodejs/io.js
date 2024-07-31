// Flags: --test-skip-pattern=disabled --test-skip-pattern=/no/i
'use strict';
const common = require('../../../common');
const {
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
describe('top level describe', common.mustCall());
describe.skip('top level skipped describe disabled', common.mustNotCall());
describe.skip('top level skipped describe enabled', common.mustNotCall());
test('this will NOt call', common.mustNotCall());
