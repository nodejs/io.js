'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/prefer-primordials');

new RuleTester({
  languageOptions: {
    sourceType: 'script',
  },
})
  .run('prefer-primordials', rule, {
    valid: [
      'new Array()',
      'JSON.stringify({})',
      'class A { *[Symbol.iterator] () { yield "a"; } }',
      'const a = { *[Symbol.iterator] () { yield "a"; } }',
      'Object.defineProperty(o, Symbol.toStringTag, { value: "o" })',
      'parseInt("10")',
      `
        const { Reflect } = primordials;
        module.exports = function() {
          const { ownKeys } = Reflect;
        }
      `,
      {
        code: 'const { Array } = primordials; new Array()',
        options: [{ name: 'Array' }]
      },
      {
        code: 'const { JSONStringify } = primordials; JSONStringify({})',
        options: [{ name: 'JSON' }]
      },
      {
        code: 'const { SymbolFor } = primordials; SymbolFor("xxx")',
        options: [{ name: 'Symbol' }]
      },
      {
        code: `
          const { SymbolIterator } = primordials;
          class A { *[SymbolIterator] () { yield "a"; } }
        `,
        options: [{ name: 'Symbol' }]
      },
      {
        code: `
          const { Symbol } = primordials;
          const a = { *[Symbol.iterator] () { yield "a"; } }
        `,
        options: [{ name: 'Symbol', ignore: ['iterator'] }]
      },
      {
        code: `
          const { ObjectDefineProperty, Symbol } = primordials;
          ObjectDefineProperty(o, Symbol.toStringTag, { value: "o" });
          const val = Symbol.toStringTag;
          const { toStringTag } = Symbol;
        `,
        options: [{ name: 'Symbol', ignore: ['toStringTag'] }]
      },
      {
        code: 'const { Symbol } = primordials; Symbol.for("xxx")',
        options: [{ name: 'Symbol', ignore: ['for'] }]
      },
      {
        code: 'const { NumberParseInt } = primordials; NumberParseInt("xxx")',
        options: [{ name: 'parseInt', into: 'Number' }]
      },
      {
        code: `
          const { ReflectOwnKeys } = primordials;
          module.exports = function() {
            ReflectOwnKeys({})
          }
        `,
        options: [{ name: 'Reflect' }],
      },
      {
        code: 'const { Map } = primordials; new Map()',
        options: [{ name: 'Map', into: 'Safe' }],
      },
      {
        code: `
          const { Function } = primordials;
          const rename = Function;
          const obj = { rename };
        `,
        options: [{ name: 'Function' }],
      },
      {
        code: `
          const { Function } = primordials;
          let rename;
          rename = Function;
          const obj = { rename };
        `,
        options: [{ name: 'Function' }],
      },
      {
        code: 'function identifier() {}',
        options: [{ name: 'identifier' }]
      },
      {
        code: 'function* identifier() {}',
        options: [{ name: 'identifier' }]
      },
      {
        code: 'class identifier {}',
        options: [{ name: 'identifier' }]
      },
      {
        code: 'new class { identifier(){} }',
        options: [{ name: 'identifier' }]
      },
      {
        code: 'const a = { identifier: \'4\' }',
        options: [{ name: 'identifier' }]
      },
      {
        code: 'identifier:{const a = 4}',
        options: [{ name: 'identifier' }]
      },
      {
        code: 'switch(0){case identifier:}',
        options: [{ name: 'identifier' }]
      },
    ],
    invalid: [
      {
        code: 'new Array()',
        options: [{ name: 'Array' }],
        errors: [{ message: /const { Array } = primordials/ }]
      },
      {
        code: 'JSON.parse("{}")',
        options: [{ name: 'JSON' }],
        errors: [
          { message: /const { JSONParse } = primordials/ },
        ]
      },
      {
        code: 'const { JSON } = primordials; JSON.parse("{}")',
        options: [{ name: 'JSON' }],
        errors: [{ message: /const { JSONParse } = primordials/ }]
      },
      {
        code: 'Symbol.for("xxx")',
        options: [{ name: 'Symbol' }],
        errors: [
          { message: /const { SymbolFor } = primordials/ },
        ]
      },
      {
        code: 'const { Symbol } = primordials; Symbol.for("xxx")',
        options: [{ name: 'Symbol' }],
        errors: [{ message: /const { SymbolFor } = primordials/ }]
      },
      {
        code: `
          const { Symbol } = primordials;
          class A { *[Symbol.iterator] () { yield "a"; } }
        `,
        options: [{ name: 'Symbol' }],
        errors: [{ message: /const { SymbolIterator } = primordials/ }]
      },
      {
        code: `
          const { Symbol } = primordials;
          const a = { *[Symbol.iterator] () { yield "a"; } }
        `,
        options: [{ name: 'Symbol' }],
        errors: [{ message: /const { SymbolIterator } = primordials/ }]
      },
      {
        code: `
          const { SymbolAsyncDispose } = primordials;
          const a = { [SymbolAsyncDispose] () {} }
        `,
        options: [{ name: 'Symbol', polyfilled: ['asyncDispose', 'dispose'] }],
        errors: [{ message: /const { SymbolAsyncDispose } = require\("internal\/util"\)/ }]
      },
      {
        code: `
          const { SymbolDispose } = primordials;
          const a = { [SymbolDispose] () {} }
        `,
        options: [{ name: 'Symbol', polyfilled: ['asyncDispose', 'dispose'] }],
        errors: [{ message: /const { SymbolDispose } = require\("internal\/util"\)/ }]
      },
      {
        code: `
          const { ObjectDefineProperty, Symbol } = primordials;
          ObjectDefineProperty(o, Symbol.toStringTag, { value: "o" })
        `,
        options: [{ name: 'Symbol' }],
        errors: [{ message: /const { SymbolToStringTag } = primordials/ }]
      },
      {
        code: `
          const { Number } = primordials;
          Number.parseInt('10')
        `,
        options: [{ name: 'Number' }],
        errors: [{ message: /const { NumberParseInt } = primordials/ }]
      },
      {
        code: 'parseInt("10")',
        options: [{ name: 'parseInt', into: 'Number' }],
        errors: [{ message: /const { NumberParseInt } = primordials/ }]
      },
      {
        code: `
          module.exports = function() {
            const { ownKeys } = Reflect;
          }
        `,
        options: [{ name: 'Reflect' }],
        errors: [{ message: /const { ReflectOwnKeys } = primordials/ }]
      },
      {
        code: `
          const { Reflect } = primordials;
          module.exports = function() {
            const { ownKeys } = Reflect;
          }
        `,
        options: [{ name: 'Reflect' }],
        errors: [{ message: /const { ReflectOwnKeys } = primordials/ }]
      },
      {
        code: 'new Map()',
        options: [{ name: 'Map', into: 'Safe' }],
        errors: [{ message: /const { SafeMap } = primordials/ }]
      },
      {
        code: `
          const { Function } = primordials;
          const noop = Function.prototype;
        `,
        options: [{ name: 'Function' }],
        errors: [{ message: /const { FunctionPrototype } = primordials/ }]
      },
      {
        code: `
          const obj = { Function };
        `,
        options: [{ name: 'Function' }],
        errors: [{ message: /const { Function } = primordials/ }]
      },
      {
        code: `
          const rename = Function;
        `,
        options: [{ name: 'Function' }],
        errors: [{ message: /const { Function } = primordials/ }]
      },
      {
        code: `
          const rename = Function;
          const obj = { rename };
        `,
        options: [{ name: 'Function' }],
        errors: [{ message: /const { Function } = primordials/ }]
      },
      {
        code: `
          let rename;
          rename = Function;
          const obj = { rename };
        `,
        options: [{ name: 'Function' }],
        errors: [{ message: /const { Function } = primordials/ }]
      },
    ]
  });
