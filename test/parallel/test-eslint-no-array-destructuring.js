'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const { RuleTester } = require('../../tools/eslint/node_modules/eslint');
const rule = require('../../tools/eslint-rules/no-array-destructuring');

const USE_OBJ_DESTRUCTURING =
  'Use object destructuring instead of array destructuring.';
const USE_ARRAY_METHODS =
  'Use primordials.ArrayPrototypeSlice to avoid unsafe array iteration.';

new RuleTester()
  .run('no-array-destructuring', rule, {
    valid: [
      'const first = [1, 2, 3][0];',
      'const {0:first} = [1, 2, 3];',
      '({1:elem} = array);',
      'function name(param, { 0: key, 1: value },) {}',
    ],
    invalid: [
      {
        code: 'const [Array] = args;',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'const {0:Array} = args;'
      },
      {
        code: 'const [ , res] = args;',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'const {  1:res} = args;',
      },
      {
        code: '[, elem] = options;',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: '({ 1:elem} = options);',
      },
      {
        code: 'const {values:[elem]} = options;',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'const {values:{0:elem}} = options;',
      },
      {
        code: '[[[elem]]] = options;',
        errors: [
          { message: USE_OBJ_DESTRUCTURING },
          { message: USE_OBJ_DESTRUCTURING },
          { message: USE_OBJ_DESTRUCTURING },
        ],
        output: '({0:[[elem]]} = options);',
      },
      {
        code: '[, ...rest] = options;',
        errors: [{ message: USE_ARRAY_METHODS }],
      },
      {
        code: 'for(const [key, value] of new Map);',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'for(const {0:key, 1:value} of new Map);',
      },
      {
        code: 'let [first,,,fourth] = array;',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'let {0:first,3:fourth} = array;',
      },
      {
        code: 'let [,second,,fourth] = array;',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'let {1:second,3:fourth} = array;',
      },
      {
        code: 'let [ ,,,fourth ] = array;',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'let { 3:fourth } = array;',
      },
      {
        code: 'let [,,,fourth, fifth,, minorFall, majorLift,...music] = arr;',
        errors: [{ message: USE_ARRAY_METHODS }],
      },
      {
        code: 'function map([key, value]) {}',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'function map({0:key, 1:value}) {}',
      },
      {
        code: 'function map([key, value],) {}',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'function map({0:key, 1:value},) {}',
      },
      {
        code: '(function([key, value]) {})',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: '(function({0:key, 1:value}) {})',
      },
      {
        code: '(function([key, value] = [null, 0]) {})',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: '(function({0:key, 1:value} = [null, 0]) {})',
      },
      {
        code: 'function map([key, ...values]) {}',
        errors: [{ message: USE_ARRAY_METHODS }],
      },
      {
        code: 'function map([key, value], ...args) {}',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'function map({0:key, 1:value}, ...args) {}',
      },
      {
        code: 'async function map([key, value], ...args) {}',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'async function map({0:key, 1:value}, ...args) {}',
      },
      {
        code: 'async function* generator([key, value], ...args) {}',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'async function* generator({0:key, 1:value}, ...args) {}',
      },
      {
        code: 'function* generator([key, value], ...args) {}',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'function* generator({0:key, 1:value}, ...args) {}',
      },
      {
        code: 'const cb = ([key, value], ...args) => {}',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'const cb = ({0:key, 1:value}, ...args) => {}',
      },
      {
        code: 'class name{ method([key], ...args){} }',
        errors: [{ message: USE_OBJ_DESTRUCTURING }],
        output: 'class name{ method({0:key}, ...args){} }',
      },
    ]
  });
