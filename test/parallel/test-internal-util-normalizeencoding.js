// Flags: --expose-internals
'use strict';

require('../common');
const assert = require('assert');
const util = require('internal/util');

const tests = [
  [undefined, util.ENCODINGS.UTF8],
  [null, util.ENCODINGS.UTF8],
  ['', util.ENCODINGS.UTF8],
  ['utf8', util.ENCODINGS.UTF8],
  ['utf-8', util.ENCODINGS.UTF8],
  ['UTF-8', util.ENCODINGS.UTF8],
  ['UTF8', util.ENCODINGS.UTF8],
  ['Utf8', util.ENCODINGS.UTF8],
  ['uTf-8', util.ENCODINGS.UTF8],
  ['utF-8', util.ENCODINGS.UTF8],
  ['ucs2', util.ENCODINGS.UTF16LE],
  ['UCS2', util.ENCODINGS.UTF16LE],
  ['UcS2', util.ENCODINGS.UTF16LE],
  ['ucs-2', util.ENCODINGS.UTF16LE],
  ['UCS-2', util.ENCODINGS.UTF16LE],
  ['UcS-2', util.ENCODINGS.UTF16LE],
  ['utf16le', util.ENCODINGS.UTF16LE],
  ['utf-16le', util.ENCODINGS.UTF16LE],
  ['UTF-16LE', util.ENCODINGS.UTF16LE],
  ['UTF16LE', util.ENCODINGS.UTF16LE],
  ['binary', util.ENCODINGS.LATIN1],
  ['BINARY', util.ENCODINGS.LATIN1],
  ['latin1', util.ENCODINGS.LATIN1],
  ['LaTiN1', util.ENCODINGS.LATIN1],
  ['base64', util.ENCODINGS.BASE64],
  ['BASE64', util.ENCODINGS.BASE64],
  ['Base64', util.ENCODINGS.BASE64],
  ['base64url', util.ENCODINGS.BASE64URL],
  ['BASE64url', util.ENCODINGS.BASE64URL],
  ['Base64url', util.ENCODINGS.BASE64URL],
  ['hex', util.ENCODINGS.HEX],
  ['HEX', util.ENCODINGS.HEX],
  ['ASCII', util.ENCODINGS.ASCII],
  ['AsCii', util.ENCODINGS.ASCII],
  ['foo', undefined],
  [1, undefined],
  [false, undefined],
  [NaN, undefined],
  [0, undefined],
  [[], undefined],
  [{}, undefined],
];

tests.forEach((e, i) => {
  const res = util.normalizeEncoding(e[0]);
  assert.strictEqual(res, e[1], `#${i} failed: expected ${e[1]}, got ${res}`);
});
