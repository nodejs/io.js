'use strict';

// Tests below are not from WPT.

const common = require('../common');

if (!common.hasIntl)
  common.skip('missing Intl');

const assert = require('assert');
const { domainToASCII, domainToUnicode } = require('url');

const tests = require('../fixtures/url-idna');
const fixtures = require('../common/fixtures');
const wptToASCIITests = require(
  fixtures.path('wpt', 'url', 'resources', 'toascii.json'),
);

{
  const expectedError = { code: 'ERR_MISSING_ARGS', name: 'TypeError' };
  assert.throws(() => domainToASCII(), expectedError);
  assert.throws(() => domainToUnicode(), expectedError);
  assert.strictEqual(domainToASCII(undefined), 'undefined');
  assert.strictEqual(domainToUnicode(undefined), 'undefined');
}

{
  for (const [i, { ascii, unicode }] of tests.entries()) {
    assert.strictEqual(ascii, domainToASCII(unicode),
                       `domainToASCII(${i + 1})`);
    assert.strictEqual(unicode, domainToUnicode(ascii),
                       `domainToUnicode(${i + 1})`);
    assert.strictEqual(ascii, domainToASCII(domainToUnicode(ascii)),
                       `domainToASCII(domainToUnicode(${i + 1}))`);
    assert.strictEqual(unicode, domainToUnicode(domainToASCII(unicode)),
                       `domainToUnicode(domainToASCII(${i + 1}))`);
  }
}

{
  for (const [i, test] of wptToASCIITests.entries()) {
    if (typeof test === 'string')
      continue; // skip comments
    const { comment, input, output } = test;
    let caseComment = `Case ${i + 1}`;
    if (comment)
      caseComment += ` (${comment})`;
    if (output === null) {
      assert.strictEqual(domainToASCII(input), '', caseComment);
      assert.strictEqual(domainToUnicode(input), '', caseComment);
    } else {
      assert.strictEqual(domainToASCII(input), output, caseComment);
      const roundtripped = domainToASCII(domainToUnicode(input));
      assert.strictEqual(roundtripped, output, caseComment);
    }
  }
}
