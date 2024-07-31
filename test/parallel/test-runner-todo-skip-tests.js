'use strict';
const common = require('../common');
const { strictEqual } = require('node:assert');
const { run, suite, test } = require('node:test');

if (!process.env.NODE_TEST_CONTEXT) {
  const stream = run({ files: [__filename] });

  stream.on('test:fail', common.mustNotCall());
  stream.on('test:pass', common.mustCall((event) => {
    strictEqual(event.skip, true);
    strictEqual(event.todo, undefined);
  }, 4));
} else {
  test('test options only', { skip: true, todo: true }, common.mustNotCall());

  test('test context calls only', common.mustCall((t) => {
    t.todo();
    t.skip();
  }));

  test('todo test with context skip', { todo: true }, common.mustCall((t) => {
    t.skip();
  }));

  // Note - there is no test for the skip option and t.todo() because the skip
  // option prevents the test from running at all. This is verified by other
  // tests.

  // Suites don't have the context methods, so only test the options combination.
  suite('suite options only', { skip: true, todo: true }, common.mustNotCall());
}
