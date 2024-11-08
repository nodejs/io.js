'use strict';

const os = require('node:os');

const s390x = os.arch() === 's390x';

module.exports = {
  'algorithm-discards-context.https.window.js': {
    'skip': 'Not relevant in Node.js context',
  },
  'derive_bits_keys/derived_bits_length.https.any.js': {
    'fail': {
      // See https://github.com/nodejs/node/pull/55296
      // The fix is pending a decision whether truncation in ECDH/X* will be removed from the spec entirely
      'expected': [
        "ECDH derivation with 230 as 'length' parameter",
        "X25519 derivation with 230 as 'length' parameter",
      ],
    },
  },
  'historical.any.js': {
    'skip': 'Not relevant in Node.js context',
  },
  'getRandomValues.any.js': {
    'fail': {
      'note': 'Node.js does not support Float16Array',
      'expected': [
        'Float16 arrays',
      ],
    },
  },
  'sign_verify/eddsa_small_order_points.https.any.js': {
    'fail': {
      'note': 'see https://github.com/nodejs/node/issues/54572',
      'expected': [
        'Ed25519 Verification checks with small-order key of order - Test 1',
        'Ed25519 Verification checks with small-order key of order - Test 2',
        'Ed25519 Verification checks with small-order key of order - Test 12',
        'Ed25519 Verification checks with small-order key of order - Test 13',
        ...(s390x ? [] : [
          'Ed25519 Verification checks with small-order key of order - Test 0',
          'Ed25519 Verification checks with small-order key of order - Test 11',
        ]),
      ],
    },
  },
};
