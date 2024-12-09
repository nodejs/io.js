import { requireEslintTool } from './eslint.config_utils.mjs';

const globals = requireEslintTool('globals');

export default [
  {
    files: ['tools/**/*.{js,mjs,cjs,ts,mts,cts}'],
    languageOptions: {
      globals: {
        ...globals.node,
      },
    },
    rules: {
      'camelcase': [
        'error',
        {
          properties: 'never',
          ignoreDestructuring: true,
          allow: ['child_process'],
        },
      ],
      'prefer-arrow-callback': 'error',
    },
  },
  {
    files: ['tools/**/*.{ts,mts,cts}'],
    rules: {
      '@typescript-eslint/no-unused-vars': [
        'error',
        { args: 'after-used' },
      ],
    },
  },
  {
    files: ['tools/**/*.{js,mjs,cjs}'],
    rules: {
      'no-unused-vars': [
        'error',
        { args: 'after-used' },
      ],
    },
  },
];
