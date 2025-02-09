// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
const assert = require('assert');
const { execFile } = require('child_process');

// Does node think that i18n was enabled?
let enablei18n = process.config.variables.v8_enable_i18n_support;
if (enablei18n === undefined) {
  enablei18n = 0;
}

// Returns true if no specific locale ids were configured (i.e. "all")
// Else, returns true if loc is in the configured list
// Else, returns false
function haveLocale(loc) {
  const locs = process.config.variables.icu_locales.split(',');
  return locs.includes(loc);
}

// Always run these. They should always pass, even if the locale
// param is ignored.
assert.strictEqual('Ç'.toLocaleLowerCase('el'), 'ç');
assert.strictEqual('Ç'.toLocaleLowerCase('tr'), 'ç');
assert.strictEqual('Ç'.toLowerCase(), 'ç');

assert.strictEqual('ç'.toLocaleUpperCase('el'), 'Ç');
assert.strictEqual('ç'.toLocaleUpperCase('tr'), 'Ç');
assert.strictEqual('ç'.toUpperCase(), 'Ç');

if (!common.hasIntl) {
  const erMsg =
    `"Intl" object is NOT present but v8_enable_i18n_support is ${enablei18n}`;
  assert.strictEqual(enablei18n, 0, erMsg);
  common.skip('Intl tests because Intl object not present.');
} else {
  const erMsg =
    `"Intl" object is present but v8_enable_i18n_support is ${
      enablei18n}. Is this test out of date?`;
  assert.strictEqual(enablei18n, 1, erMsg);

  // Construct a new date at the beginning of Unix time
  const date0 = new Date(0);

  // Use the GMT time zone
  const GMT = 'Etc/GMT';

  // Construct an English formatter. Should format to "Jan 70"
  const dtf = new Intl.DateTimeFormat(['en'], {
    timeZone: GMT,
    month: 'short',
    year: '2-digit'
  });

  // If list is specified and doesn't contain 'en' then return.
  if (process.config.variables.icu_locales && !haveLocale('en')) {
    common.printSkipMessage(
      'detailed Intl tests because English is not listed as supported.');
    // Smoke test. Does it format anything, or fail?
    console.log(`Date(0) formatted to: ${dtf.format(date0)}`);
    return;
  }

  // Check casing
  {
    assert.strictEqual('I'.toLocaleLowerCase('tr'), 'ı');
  }

  // Check with toLocaleString
  {
    const localeString = dtf.format(date0);
    assert.strictEqual(localeString, 'Jan 70');
  }
  // Options to request GMT
  const optsGMT = { timeZone: GMT };

  // Test format
  {
    const localeString = date0.toLocaleString(['en'], optsGMT);
    assert.strictEqual(localeString, '1/1/1970, 12:00:00 AM');
  }
  // number format
  {
    const numberFormat = new Intl.NumberFormat(['en']).format(12345.67890);
    assert.strictEqual(numberFormat, '12,345.679');
  }

  // Regression test for https://github.com/nodejs/node/issues/51752
  {
    assert(new Intl.Segmenter().segment());
  }

  // If list is specified and doesn't contain 'en-US' then return.
  if (process.config.variables.icu_locales && !haveLocale('en-US')) {
    common.printSkipMessage('detailed Intl tests because American English is ' +
                            'not listed as supported.');
    return;
  }
  // Number format resolved options
  {
    const numberFormat = new Intl.NumberFormat('en-US', { style: 'percent' });
    const resolvedOptions = numberFormat.resolvedOptions();
    assert.strictEqual(resolvedOptions.locale, 'en-US');
    assert.strictEqual(resolvedOptions.style, 'percent');
  }
  // Significant Digits
  {
    const loc = ['en-US'];
    const opts = { maximumSignificantDigits: 4 };
    const num = 10.001;
    const numberFormat = new Intl.NumberFormat(loc, opts).format(num);
    assert.strictEqual(numberFormat, '10');
  }

  const collOpts = { sensitivity: 'base', ignorePunctuation: true };
  const coll = new Intl.Collator(['en'], collOpts);

  // Ignore punctuation
  assert.strictEqual(coll.compare('blackbird', 'black-bird'), 0);
  // Compare less
  assert.strictEqual(coll.compare('blackbird', 'red-bird'), -1);
  // Compare greater
  assert.strictEqual(coll.compare('bluebird', 'blackbird'), 1);
  // Ignore case
  assert.strictEqual(coll.compare('Bluebird', 'bluebird'), 0);
  // `ffi` ligature (contraction)
  assert.strictEqual(coll.compare('\ufb03', 'ffi'), 0);

  {
    // Regression test for https://github.com/nodejs/node/issues/27379
    const env = { ...process.env, LC_ALL: 'ja' };
    execFile(
      process.execPath, ['-p', 'new Date().toLocaleString()'],
      { env },
      common.mustSucceed()
    );
  }

  {
    // Regression test for https://github.com/nodejs/node/issues/27418
    const env = { ...process.env, LC_ALL: 'fr@EURO' };
    execFile(
      process.execPath,
      ['-p', 'new Intl.NumberFormat().resolvedOptions().locale'],
      { env },
      common.mustSucceed()
    );
  }
}
