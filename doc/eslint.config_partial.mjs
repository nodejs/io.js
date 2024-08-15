import {
  noRestrictedSyntaxCommonAll,
  noRestrictedSyntaxCommonLib,
  requireEslintTool,
} from '../tools/eslint/eslint.config_utils.mjs';
import { builtinModules as builtin } from 'node:module';

const globals = requireEslintTool('globals');

export default [
  {
    files: ['doc/**/*.md/*.{js,mjs,cjs}'],
    rules: {
      // Ease some restrictions in doc examples.
      'no-restricted-properties': 'off',
      'no-restricted-syntax': [
        'error',
        ...noRestrictedSyntaxCommonAll,
        ...noRestrictedSyntaxCommonLib,
        {
          selector: `CallExpression[callee.name="require"][arguments.0.type="Literal"]:matches(${builtin.map((name) => `[arguments.0.value="${name}"]`).join(',')}),ImportDeclaration:matches(${builtin.map((name) => `[source.value="${name}"]`).join(',')})`,
          message: 'Use `node:` prefix.',
        },
      ],
      'no-undef': 'off',
      'no-unused-expressions': 'off',
      'no-unused-vars': 'off',
      'symbol-description': 'off',

      // Add new ECMAScript features gradually.
      'prefer-const': 'error',
      'prefer-rest-params': 'error',
      'prefer-template': 'error',

      // Stylistic rules.
      '@stylistic/js/no-multiple-empty-lines': [
        'error',
        {
          max: 1,
          maxEOF: 0,
          maxBOF: 0,
        },
      ],
    },
  },
  {
    files: ['doc/api_assets/*.js'],
    languageOptions: {
      globals: {
        ...globals.browser,
      },
    },
  },
];
