'use strict'

const neo = require('neostandard')

module.exports = [
  ...neo({
    ignores: [
      'lib/llhttp',
      'test/fixtures/wpt',
      'test/fixtures/cache-tests',
      'undici-fetch.js'
    ],
    noJsx: true,
    ts: true
  }),
  {
    rules: {
      '@stylistic/comma-dangle': ['error', {
        arrays: 'never',
        objects: 'never',
        imports: 'never',
        exports: 'never',
        functions: 'never'
      }],
      '@typescript-eslint/no-redeclare': 'off'
    }
  }
]
