import Module from 'node:module';
import { fileURLToPath, URL } from 'node:url';

import benchmarkConfig from './benchmark/eslint.config_partial.mjs';
import docConfig from './doc/eslint.config_partial.mjs';
import libConfig from './lib/eslint.config_partial.mjs';
import testConfig from './test/eslint.config_partial.mjs';
import toolsConfig from './tools/eslint/eslint.config_partial.mjs';
import {
  noRestrictedSyntaxCommonAll,
  noRestrictedSyntaxCommonLib,
  requireEslintTool,
  resolveEslintTool,
} from './tools/eslint/eslint.config_utils.mjs';
import nodeCore from './tools/eslint/eslint-plugin-node-core.js';

const js = requireEslintTool('@eslint/js');
const babelEslintParser = requireEslintTool('@babel/eslint-parser');
const babelPluginSyntaxImportAttributes = resolveEslintTool('@babel/plugin-syntax-import-attributes');
const jsdoc = requireEslintTool('eslint-plugin-jsdoc');
const markdown = requireEslintTool('eslint-plugin-markdown');
const stylisticJs = requireEslintTool('@stylistic/eslint-plugin-js');

nodeCore.RULES_DIR = fileURLToPath(new URL('./tools/eslint-rules', import.meta.url));

// The Module._resolveFilename() monkeypatching is to make it so that ESLint is able to
// dynamically load extra modules that we install with it.
const ModuleResolveFilename = Module._resolveFilename;
const hacks = [
  'eslint-formatter-tap',
];
Module._resolveFilename = (request, parent, isMain, options) => {
  if (hacks.includes(request) && parent.id.endsWith('__placeholder__.js')) {
    return resolveEslintTool(request);
  }
  return ModuleResolveFilename(request, parent, isMain, options);
};

export default [
  // #region ignores
  {
    ignores: [
      '**/node_modules/**',
      'benchmark/fixtures/**',
      'benchmark/tmp/**',
      'doc/**/*.js',
      'doc/changelogs/CHANGELOG_V1*.md',
      '!doc/api_assets/*.js',
      '!doc/changelogs/CHANGELOG_V18.md',
      'lib/punycode.js',
      'test/.tmp.*/**',
      'test/addons/??_*',
      'test/fixtures/**',
      'test/message/esm_display_syntax_error.mjs',
      'tools/github_reporter/**',
      'tools/icu/**',
      'tools/lint-md/lint-md.mjs',
    ],
  },
  // #endregion
  // #region general config
  js.configs.recommended,
  jsdoc.configs['flat/recommended'],
  {
    files: ['**/*.{js,cjs}'],
    languageOptions: {
      // The default is `commonjs` but it's not supported by the Babel parser.
      sourceType: 'script',
    },
  },
  {
    plugins: {
      jsdoc,
      '@stylistic/js': stylisticJs,
      'node-core': nodeCore,
    },
    languageOptions: {
      parser: babelEslintParser,
      parserOptions: {
        babelOptions: {
          plugins: [
            babelPluginSyntaxImportAttributes,
          ],
        },
        requireConfigFile: false,
      },
    },
  },
  // #endregion
  // #region general globals
  {
    languageOptions: {
      globals: {
        ByteLengthQueuingStrategy: 'readonly',
        CompressionStream: 'readonly',
        CountQueuingStrategy: 'readonly',
        CustomEvent: 'readonly',
        crypto: 'readonly',
        Crypto: 'readonly',
        CryptoKey: 'readonly',
        DecompressionStream: 'readonly',
        EventSource: 'readable',
        fetch: 'readonly',
        FormData: 'readonly',
        navigator: 'readonly',
        ReadableStream: 'readonly',
        ReadableStreamDefaultReader: 'readonly',
        ReadableStreamBYOBReader: 'readonly',
        ReadableStreamBYOBRequest: 'readonly',
        ReadableByteStreamController: 'readonly',
        ReadableStreamDefaultController: 'readonly',
        Response: 'readonly',
        TextDecoderStream: 'readonly',
        TextEncoderStream: 'readonly',
        TransformStream: 'readonly',
        TransformStreamDefaultController: 'readonly',
        ShadowRealm: 'readonly',
        SubtleCrypto: 'readonly',
        WritableStream: 'readonly',
        WritableStreamDefaultWriter: 'readonly',
        WritableStreamDefaultController: 'readonly',
        WebSocket: 'readonly',
      },
    },
  },
  // #endregion
  // #region general rules
  {
    rules: {
      // ESLint built-in rules
      // https://eslint.org/docs/latest/rules/
      'accessor-pairs': 'error',
      'array-callback-return': 'error',
      'block-scoped-var': 'error',
      'capitalized-comments': ['error', 'always', {
        line: {
          // Ignore all lines that have less characters than 20 and all lines
          // that start with something that looks like a variable name or code.
          ignorePattern: '.{0,20}$|[a-z]+ ?[0-9A-Z_.(/=:[#-]|std|http|ssh|ftp',
          ignoreInlineComments: true,
          ignoreConsecutiveComments: true,
        },
        block: {
          ignorePattern: '.*',
        },
      }],
      'default-case-last': 'error',
      'dot-notation': 'error',
      'eqeqeq': ['error', 'smart'],
      'func-name-matching': 'error',
      'func-style': ['error', 'declaration', { allowArrowFunctions: true }],
      'no-constant-condition': ['error', { checkLoops: false }],
      'no-constructor-return': 'error',
      'no-duplicate-imports': 'error',
      'no-else-return': 'error',
      'no-lonely-if': 'error',
      'no-mixed-requires': 'error',
      'no-new-require': 'error',
      'no-path-concat': 'error',
      'no-proto': 'error',
      'no-redeclare': ['error', { builtinGlobals: false }],
      'no-restricted-modules': ['error', 'sys'],
      'no-restricted-properties': [
        'error',
        {
          object: 'assert',
          property: 'deepEqual',
          message: 'Use `assert.deepStrictEqual()`.',
        },
        {
          object: 'assert',
          property: 'notDeepEqual',
          message: 'Use `assert.notDeepStrictEqual()`.',
        },
        {
          object: 'assert',
          property: 'equal',
          message: 'Use `assert.strictEqual()` rather than `assert.equal()`.',
        },
        {
          object: 'assert',
          property: 'notEqual',
          message: 'Use `assert.notStrictEqual()` rather than `assert.notEqual()`.',
        },
        {
          property: '__defineGetter__',
          message: '__defineGetter__ is deprecated.',
        },
        {
          property: '__defineSetter__',
          message: '__defineSetter__ is deprecated.',
        },
        {
          property: 'webcrypto',
          message: 'Use `globalThis.crypto`.',
        },
      ],
      'no-restricted-syntax': [
        'error',
        ...noRestrictedSyntaxCommonAll,
        ...noRestrictedSyntaxCommonLib,
      ],
      'no-self-compare': 'error',
      'no-template-curly-in-string': 'error',
      'no-throw-literal': 'error',
      'no-undef': ['error', { typeof: true }],
      'no-undef-init': 'error',
      'no-unused-expressions': ['error', { allowShortCircuit: true }],
      'no-unused-vars': ['error', { args: 'none', caughtErrors: 'all' }],
      'no-use-before-define': ['error', {
        classes: true,
        functions: false,
        variables: false,
      }],
      'no-useless-call': 'error',
      'no-useless-concat': 'error',
      'no-useless-constructor': 'error',
      'no-useless-return': 'error',
      'no-var': 'error',
      'no-void': 'error',
      'one-var': ['error', { initialized: 'never' }],
      'prefer-const': ['error', { ignoreReadBeforeAssign: true }],
      'prefer-object-has-own': 'error',
      'strict': ['error', 'global'],
      'symbol-description': 'error',
      'unicode-bom': 'error',
      'valid-typeof': ['error', { requireStringLiterals: true }],

      // ESLint recommended rules that we disable.
      'no-inner-declarations': 'off',

      // JSDoc recommended rules that we disable.
      'jsdoc/require-jsdoc': 'off',
      'jsdoc/require-param-description': 'off',
      'jsdoc/newline-after-description': 'off',
      'jsdoc/require-returns-description': 'off',
      'jsdoc/valid-types': 'off',
      'jsdoc/no-defaults': 'off',
      'jsdoc/no-undefined-types': 'off',
      'jsdoc/require-param': 'off',
      'jsdoc/check-tag-names': 'off',
      'jsdoc/require-returns': 'off',

      // Stylistic rules.
      '@stylistic/js/arrow-parens': 'error',
      '@stylistic/js/arrow-spacing': 'error',
      '@stylistic/js/block-spacing': 'error',
      '@stylistic/js/brace-style': ['error', '1tbs', { allowSingleLine: true }],
      '@stylistic/js/comma-dangle': ['error', 'always-multiline'],
      '@stylistic/js/comma-spacing': 'error',
      '@stylistic/js/comma-style': 'error',
      '@stylistic/js/computed-property-spacing': 'error',
      '@stylistic/js/dot-location': ['error', 'property'],
      '@stylistic/js/eol-last': 'error',
      '@stylistic/js/func-call-spacing': 'error',
      '@stylistic/js/indent': ['error', 2, {
        ArrayExpression: 'first',
        CallExpression: { arguments: 'first' },
        FunctionDeclaration: { parameters: 'first' },
        FunctionExpression: { parameters: 'first' },
        MemberExpression: 'off',
        ObjectExpression: 'first',
        SwitchCase: 1,
      }],
      '@stylistic/js/key-spacing': 'error',
      '@stylistic/js/keyword-spacing': 'error',
      '@stylistic/js/linebreak-style': 'error',
      '@stylistic/js/max-len': ['error', {
        code: 120,
        ignorePattern: '^// Flags:',
        ignoreRegExpLiterals: true,
        ignoreTemplateLiterals: true,
        ignoreUrls: true,
        tabWidth: 2,
      }],
      '@stylistic/js/new-parens': 'error',
      '@stylistic/js/no-confusing-arrow': 'error',
      '@stylistic/js/no-extra-parens': ['error', 'functions'],
      '@stylistic/js/no-multi-spaces': ['error', { ignoreEOLComments: true }],
      '@stylistic/js/no-multiple-empty-lines': ['error', { max: 2, maxEOF: 0, maxBOF: 0 }],
      '@stylistic/js/no-tabs': 'error',
      '@stylistic/js/no-trailing-spaces': 'error',
      '@stylistic/js/no-whitespace-before-property': 'error',
      '@stylistic/js/object-curly-newline': 'error',
      '@stylistic/js/object-curly-spacing': ['error', 'always'],
      '@stylistic/js/one-var-declaration-per-line': 'error',
      '@stylistic/js/operator-linebreak': ['error', 'after'],
      '@stylistic/js/padding-line-between-statements': [
        'error',
        { blankLine: 'always', prev: 'function', next: 'function' },
      ],
      '@stylistic/js/quotes': ['error', 'single', { avoidEscape: true }],
      '@stylistic/js/quote-props': ['error', 'consistent'],
      '@stylistic/js/rest-spread-spacing': 'error',
      '@stylistic/js/semi': 'error',
      '@stylistic/js/semi-spacing': 'error',
      '@stylistic/js/space-before-blocks': ['error', 'always'],
      '@stylistic/js/space-before-function-paren': ['error', {
        anonymous: 'never',
        named: 'never',
        asyncArrow: 'always',
      }],
      '@stylistic/js/space-in-parens': 'error',
      '@stylistic/js/space-infix-ops': 'error',
      '@stylistic/js/space-unary-ops': 'error',
      '@stylistic/js/spaced-comment': ['error', 'always', {
        'block': { 'balanced': true },
        'exceptions': ['-'],
      }],
      '@stylistic/js/template-curly-spacing': 'error',

      // Custom rules in tools/eslint-rules.
      'node-core/no-unescaped-regexp-dot': 'error',
      'node-core/no-duplicate-requires': 'error',
      'node-core/prefer-proto': 'error',
    },
  },
  // #endregion
  // #region markdown config
  {
    files: ['**/*.md'],
    plugins: {
      markdown,
    },
    processor: 'markdown/markdown',
  },
  {
    files: ['**/*.md/*.{js,cjs}'],
    languageOptions: {
      parserOptions: {
        ecmaFeatures: { impliedStrict: true },
      },
    },
    rules: { strict: 'off' },
  },
  {
    files: [
      '**/*.md/*.mjs',
      'doc/api/esm.md/*.js',
      'doc/api/packages.md/*.js',
    ],
    languageOptions: {
      sourceType: 'module',
    },
    rules: { 'no-restricted-globals': [
      'error',
      {
        name: '__filename',
        message: 'Use import.meta.url instead.',
      },
      {
        name: '__dirname',
        message: 'Not available in ESM.',
      },
      {
        name: 'exports',
        message: 'Not available in ESM.',
      },
      {
        name: 'module',
        message: 'Not available in ESM.',
      },
      {
        name: 'require',
        message: 'Use import instead.',
      },
      {
        name: 'Buffer',
        message: "Import 'Buffer' instead of using the global.",
      },
      {
        name: 'process',
        message: "Import 'process' instead of using the global.",
      },
    ] },
  },
  // #endregion
  // #region partials
  ...benchmarkConfig,
  ...docConfig,
  ...libConfig,
  ...testConfig,
  ...toolsConfig,
  // #endregion
];
