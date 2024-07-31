'use strict';

// From: https://github.com/w3c/web-platform-tests/blob/39a67e2fff/encoding/textdecoder-utf16-surrogates.html
// With the twist that we specifically test for Node.js error codes

const common = require('../common');

if (!common.hasIntl)
  common.skip('missing Intl');

const assert = require('assert');

const bad = [
  {
    encoding: 'utf-16le',
    input: [0x00, 0xd8],
    expected: '\uFFFD',
    name: 'lone surrogate lead'
  },
  {
    encoding: 'utf-16le',
    input: [0x00, 0xdc],
    expected: '\uFFFD',
    name: 'lone surrogate trail'
  },
  {
    encoding: 'utf-16le',
    input: [0x00, 0xd8, 0x00, 0x00],
    expected: '\uFFFD\u0000',
    name: 'unmatched surrogate lead'
  },
  {
    encoding: 'utf-16le',
    input: [0x00, 0xdc, 0x00, 0x00],
    expected: '\uFFFD\u0000',
    name: 'unmatched surrogate trail'
  },
  {
    encoding: 'utf-16le',
    input: [0x00, 0xdc, 0x00, 0xd8],
    expected: '\uFFFD\uFFFD',
    name: 'swapped surrogate pair'
  },
];

for (const t of bad) {
  assert.throws(
    () => {
      new TextDecoder(t.encoding, { fatal: true })
        .decode(new Uint8Array(t.input));
    }, {
      code: 'ERR_ENCODING_INVALID_ENCODED_DATA',
      name: 'TypeError'
    }
  );
}
