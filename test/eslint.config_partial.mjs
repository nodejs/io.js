/* eslint-disable @stylistic/js/max-len */

import {
  noRestrictedSyntaxCommonAll,
  requireEslintTool,
} from '../tools/eslint/eslint.config_utils.mjs';

const globals = requireEslintTool('globals');

export default [
  {
    files: ['test/**/*.{js,mjs,cjs}'],
    languageOptions: {
      globals: {
        ...globals.node,
        CloseEvent: true,
      },
    },
    rules: {
      'multiline-comment-style': [
        'error',
        'separate-lines',
      ],
      'prefer-const': 'error',
      'symbol-description': 'off',
      'no-restricted-syntax': [
        'error',
        ...noRestrictedSyntaxCommonAll,
        {
          selector: "CallExpression:matches([callee.name='deepStrictEqual'], [callee.property.name='deepStrictEqual']):matches([arguments.1.type='Literal']:not([arguments.1.regex]), [arguments.1.type='Identifier'][arguments.1.name='undefined'])",
          message: 'Use strictEqual instead of deepStrictEqual for literals or undefined.',
        },
        {
          selector: "CallExpression:matches([callee.name='notDeepStrictEqual'], [callee.property.name='notDeepStrictEqual']):matches([arguments.1.type='Literal']:not([arguments.1.regex]), [arguments.1.type='Identifier'][arguments.1.name='undefined'])",
          message: 'Use notStrictEqual instead of notDeepStrictEqual for literals or undefined.',
        },
        {
          selector: "CallExpression:matches([callee.name='deepStrictEqual'], [callee.property.name='deepStrictEqual'])[arguments.2.type='Literal']",
          message: 'Do not use a literal for the third argument of assert.deepStrictEqual()',
        },
        {
          selector: "CallExpression:matches([callee.name='doesNotThrow'], [callee.property.name='doesNotThrow'])",
          message: 'Do not use `assert.doesNotThrow()`. Write the code without the wrapper and add a comment instead.',
        },
        {
          selector: "CallExpression:matches([callee.name='doesNotReject'], [callee.property.name='doesNotReject'])",
          message: 'Do not use `assert.doesNotReject()`. Write the code without the wrapper and add a comment instead.',
        },
        {
          selector: "CallExpression:matches([callee.name='rejects'], [callee.property.name='rejects'])[arguments.length<2]",
          message: '`assert.rejects()` must be invoked with at least two arguments.',
        },
        {
          selector: "CallExpression[callee.property.name='strictEqual'][arguments.2.type='Literal']",
          message: 'Do not use a literal for the third argument of assert.strictEqual()',
        },
        {
          selector: "CallExpression:matches([callee.name='throws'], [callee.property.name='throws'])[arguments.1.type='Literal']:not([arguments.1.regex])",
          message: 'Use an object as second argument of `assert.throws()`.',
        },
        {
          selector: "CallExpression:matches([callee.name='throws'], [callee.property.name='throws'])[arguments.length<2]",
          message: '`assert.throws()` must be invoked with at least two arguments.',
        },
        {
          selector: "CallExpression:matches([callee.name='notDeepStrictEqual'], [callee.property.name='notDeepStrictEqual'])[arguments.0.type='Literal']:not([arguments.1.type='Literal']):not([arguments.1.type='ObjectExpression']):not([arguments.1.type='ArrayExpression']):not([arguments.1.type='UnaryExpression'])",
          message: 'The first argument should be the `actual`, not the `expected` value.',
        },
        {
          selector: "CallExpression:matches([callee.name='notStrictEqual'], [callee.property.name='notStrictEqual'])[arguments.0.type='Literal']:not([arguments.1.type='Literal']):not([arguments.1.type='ObjectExpression']):not([arguments.1.type='ArrayExpression']):not([arguments.1.type='UnaryExpression'])",
          message: 'The first argument should be the `actual`, not the `expected` value.',
        },
        {
          selector: "CallExpression:matches([callee.name='deepStrictEqual'], [callee.property.name='deepStrictEqual'])[arguments.0.type='Literal']:not([arguments.1.type='Literal']):not([arguments.1.type='ObjectExpression']):not([arguments.1.type='ArrayExpression']):not([arguments.1.type='UnaryExpression'])",
          message: 'The first argument should be the `actual`, not the `expected` value.',
        },
        {
          selector: "CallExpression:matches([callee.name='strictEqual'], [callee.property.name='strictEqual'])[arguments.0.type='Literal']:not([arguments.1.type='Literal']):not([arguments.1.type='ObjectExpression']):not([arguments.1.type='ArrayExpression']):not([arguments.1.type='UnaryExpression'])",
          message: 'The first argument should be the `actual`, not the `expected` value.',
        },
        {
          selector: "CallExpression[callee.name='isNaN']",
          message: 'Use `Number.isNaN()` instead of the global `isNaN()` function.',
        },
        {
          selector: "VariableDeclarator > CallExpression:matches([callee.name='debuglog'], [callee.property.name='debuglog']):not([arguments.0.value='test'])",
          message: "Use 'test' as debuglog value in tests.",
        },
        {
          selector: 'CallExpression:matches([callee.object.name="common"][callee.property.name=/^must(Not)?Call/],[callee.name="mustCall"],[callee.name="mustCallAtLeast"],[callee.name="mustNotCall"])>:first-child[type=/FunctionExpression$/][body.body.length=0]',
          message: 'Do not use an empty function, omit the parameter altogether.',
        },
        {
          selector: "ExpressionStatement>CallExpression:matches([callee.name='rejects'], [callee.object.name='assert'][callee.property.name='rejects'])",
          message: 'Calling `assert.rejects` without `await` or `.then(common.mustCall())` will not detect never-settling promises.',
        },
      ],

      // Stylistic rules.
      '@stylistic/js/comma-dangle': [
        'error',
        'always-multiline',
      ],

      // Custom rules in tools/eslint-rules.
      'node-core/prefer-assert-iferror': 'error',
      'node-core/prefer-assert-methods': 'error',
      'node-core/prefer-common-mustnotcall': 'error',
      'node-core/prefer-common-mustsucceed': 'error',
      'node-core/crypto-check': 'error',
      'node-core/eslint-check': 'error',
      'node-core/async-iife-no-unused-result': 'error',
      'node-core/inspector-check': 'error',
      // `common` module is mandatory in tests.
      'node-core/required-modules': [
        'error',
        { common: 'common(/index\\.(m)?js)?$' },
      ],
      'node-core/require-common-first': 'error',
      'node-core/no-duplicate-requires': 'off',
    },
  },
  {
    files: [
      'test/es-module/**/*.{js,mjs}',
      'test/parallel/**/*.{js,mjs}',
      'test/sequential/**/*.{js,mjs}',
    ],
    rules: {
      '@stylistic/js/comma-dangle': [
        'error',
        {
          arrays: 'always-multiline',
          exports: 'always-multiline',
          functions: 'only-multiline',
          imports: 'always-multiline',
          objects: 'only-multiline',
        },
      ],
    },
  },
  {
    files: [
      'test/{common,wpt}/**/*.{js,mjs,cjs}',
      'test/eslint.config_partial.mjs',
    ],
    rules: {
      'node-core/required-modules': 'off',
      'node-core/require-common-first': 'off',
    },
  },
  {
    files: [
      'test/es-module/test-esm-example-loader.js',
      'test/es-module/test-esm-type-flag.js',
      'test/es-module/test-esm-type-flag-alias.js',
      'test/es-module/test-require-module-detect-entry-point.js',
      'test/es-module/test-require-module-detect-entry-point-aou.js',
    ],
    languageOptions: {
      sourceType: 'module',
    },
  },
];
