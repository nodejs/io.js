import { requireEslintTool } from '../tools/eslint/eslint.config_utils.mjs';

const globals = requireEslintTool('globals');

export default [
  {
    files: ['benchmark/**/*.{js,mjs,cjs}'],
    languageOptions: {
      globals: {
        ...globals.node,
      },
    },
    rules: {
      'prefer-arrow-callback': 'error',
    },
  },
];
