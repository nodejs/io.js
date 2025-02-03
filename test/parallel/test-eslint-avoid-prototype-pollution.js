'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/avoid-prototype-pollution');

new RuleTester()
  .run('property-descriptor-no-prototype-pollution', rule, {
    valid: [
      'ObjectDefineProperties({}, {})',
      'ObjectCreate(null, {})',
      'ObjectDefineProperties({}, { key })',
      'ObjectCreate(null, { key })',
      'ObjectDefineProperties({}, { ...spread })',
      'ObjectCreate(null, { ...spread })',
      'ObjectDefineProperties({}, { key: valueDescriptor })',
      'ObjectCreate(null, { key: valueDescriptor })',
      'ObjectDefineProperties({}, { key: { ...{}, __proto__: null } })',
      'ObjectCreate(null, { key: { ...{}, __proto__: null } })',
      'ObjectDefineProperties({}, { key: { __proto__: null } })',
      'ObjectCreate(null, { key: { __proto__: null } })',
      'ObjectDefineProperties({}, { key: { __proto__: null, enumerable: true } })',
      'ObjectCreate(null, { key: { __proto__: null, enumerable: true } })',
      'ObjectDefineProperties({}, { key: { "__proto__": null } })',
      'ObjectCreate(null, { key: { "__proto__": null } })',
      'ObjectDefineProperties({}, { key: { \'__proto__\': null } })',
      'ObjectCreate(null, { key: { \'__proto__\': null } })',
      'ObjectDefineProperty({}, "key", ObjectCreate(null))',
      'ReflectDefineProperty({}, "key", ObjectCreate(null))',
      'ObjectDefineProperty({}, "key", valueDescriptor)',
      'ReflectDefineProperty({}, "key", valueDescriptor)',
      'ObjectDefineProperty({}, "key", { __proto__: null })',
      'ReflectDefineProperty({}, "key", { __proto__: null })',
      'ObjectDefineProperty({}, "key", { __proto__: null, enumerable: true })',
      'ReflectDefineProperty({}, "key", { __proto__: null, enumerable: true })',
      'ObjectDefineProperty({}, "key", { "__proto__": null })',
      'ReflectDefineProperty({}, "key", { "__proto__": null })',
      'ObjectDefineProperty({}, "key", { \'__proto__\': null })',
      'ReflectDefineProperty({}, "key", { \'__proto__\': null })',
      'async function myFn() { return { __proto__: null } }',
      'async function myFn() { function myFn() { return {} } return { __proto__: null } }',
      'const myFn = async function myFn() { return { __proto__: null } }',
      'const myFn = async function () { return { __proto__: null } }',
      'const myFn = async () => { return { __proto__: null } }',
      'const myFn = async () => ({ __proto__: null })',
      'function myFn() { return {} }',
      'const myFn = function myFn() { return {} }',
      'const myFn = function () { return {} }',
      'const myFn = () => { return {} }',
      'const myFn = () => ({})',
      'StringPrototypeReplace("some string", "some string", "some replacement")',
      'StringPrototypeReplaceAll("some string", "some string", "some replacement")',
      'StringPrototypeSplit("some string", "some string")',
      'new Proxy({}, otherObject)',
      'new Proxy({}, someFactory())',
      'new Proxy({}, { __proto__: null })',
      'new Proxy({}, { __proto__: null, ...{} })',
      'async function name(){return await SafePromiseAll([])}',
      'async function name(){const val = await SafePromiseAll([])}',
    ],
    invalid: [
      {
        code: 'ObjectDefineProperties({}, ObjectGetOwnPropertyDescriptors({}))',
        errors: [{ message: /prototype pollution/ }],
      },
      {
        code: 'ObjectCreate(null, ObjectGetOwnPropertyDescriptors({}))',
        errors: [{ message: /prototype pollution/ }],
      },
      {
        code: 'ObjectDefineProperties({}, { key: {} })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectCreate(null, { key: {} })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectDefineProperties({}, { key: { [void 0]: { ...{ __proto__: null } } } })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectCreate(null, { key: { [void 0]: { ...{ __proto__: null } } } })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectDefineProperties({}, { key: { __proto__: Object.prototype } })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectCreate(null, { key: { __proto__: Object.prototype } })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectDefineProperties({}, { key: { [`__proto__`]: null } })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectCreate(null, { key: { [`__proto__`]: null } })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectDefineProperties({}, { key: { enumerable: true } })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectCreate(null, { key: { enumerable: true } })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectDefineProperty({}, "key", {})',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ReflectDefineProperty({}, "key", {})',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectDefineProperty({}, "key", ObjectGetOwnPropertyDescriptor({}, "key"))',
        errors: [{
          message: /prototype pollution/,
          suggestions: [{
            desc: 'Wrap the property descriptor in a null-prototype object',
            output: 'ObjectDefineProperty({}, "key", { __proto__: null,...ObjectGetOwnPropertyDescriptor({}, "key") })',
          }],
        }],
      },
      {
        code: 'ReflectDefineProperty({}, "key", ObjectGetOwnPropertyDescriptor({}, "key"))',
        errors: [{
          message: /prototype pollution/,
          suggestions: [{
            desc: 'Wrap the property descriptor in a null-prototype object',
            output:
              'ReflectDefineProperty({}, "key", { __proto__: null,...ObjectGetOwnPropertyDescriptor({}, "key") })',
          }],
        }],
      },
      {
        code: 'ObjectDefineProperty({}, "key", ReflectGetOwnPropertyDescriptor({}, "key"))',
        errors: [{
          message: /prototype pollution/,
          suggestions: [{
            desc: 'Wrap the property descriptor in a null-prototype object',
            output:
              'ObjectDefineProperty({}, "key", { __proto__: null,...ReflectGetOwnPropertyDescriptor({}, "key") })',
          }],
        }],
      },
      {
        code: 'ReflectDefineProperty({}, "key", ReflectGetOwnPropertyDescriptor({}, "key"))',
        errors: [{
          message: /prototype pollution/,
          suggestions: [{
            desc: 'Wrap the property descriptor in a null-prototype object',
            output:
              'ReflectDefineProperty({}, "key", { __proto__: null,...ReflectGetOwnPropertyDescriptor({}, "key") })',
          }],
        }],
      },
      {
        code: 'ObjectDefineProperty({}, "key", { __proto__: Object.prototype })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ReflectDefineProperty({}, "key", { __proto__: Object.prototype })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectDefineProperty({}, "key", { [`__proto__`]: null })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ReflectDefineProperty({}, "key", { [`__proto__`]: null })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ObjectDefineProperty({}, "key", { enumerable: true })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'ReflectDefineProperty({}, "key", { enumerable: true })',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'async function myFn(){ return {} }',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'async function myFn(){ async function someOtherFn() { return { __proto__: null } } return {} }',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'async function myFn(){ if (true) { return {} } return { __proto__: null } }',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'const myFn = async function myFn(){ return {} }',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'const myFn = async function (){ return {} }',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'const myFn = async () => { return {} }',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'const myFn = async () => ({})',
        errors: [{ message: /null-prototype/ }],
      },
      {
        code: 'RegExpPrototypeTest(/some regex/, "some string")',
        errors: [{
          message: /looks up the "exec" property/,
          suggestions: [{
            desc: 'Use RegexpPrototypeExec instead',
            output: 'RegExpPrototypeExec(/some regex/, "some string") !== null',
          }],
        }],
      },
      {
        code: 'RegExpPrototypeSymbolMatch(/some regex/, "some string")',
        errors: [{ message: /looks up the "exec" property/ }],
      },
      {
        code: 'RegExpPrototypeSymbolMatchAll(/some regex/, "some string")',
        errors: [{ message: /looks up the "exec" property/ }],
      },
      {
        code: 'RegExpPrototypeSymbolSearch(/some regex/, "some string")',
        errors: [{ message: /SafeStringPrototypeSearch/ }],
      },
      {
        code: 'StringPrototypeMatch("some string", /some regex/)',
        errors: [{ message: /looks up the Symbol\.match property/ }],
      },
      {
        code: 'let v = StringPrototypeMatch("some string", /some regex/)',
        errors: [{ message: /looks up the Symbol\.match property/ }],
      },
      {
        code: 'let v = StringPrototypeMatch("some string", new RegExp("some regex"))',
        errors: [{ message: /looks up the Symbol\.match property/ }],
      },
      {
        code: 'StringPrototypeMatchAll("some string", /some regex/)',
        errors: [{ message: /looks up the Symbol\.matchAll property/ }],
      },
      {
        code: 'let v = StringPrototypeMatchAll("some string", new RegExp("some regex"))',
        errors: [{ message: /looks up the Symbol\.matchAll property/ }],
      },
      {
        code: 'StringPrototypeReplace("some string", /some regex/, "some replacement")',
        errors: [{ message: /looks up the Symbol\.replace property/ }],
      },
      {
        code: 'StringPrototypeReplace("some string", new RegExp("some regex"), "some replacement")',
        errors: [{ message: /looks up the Symbol\.replace property/ }],
      },
      {
        code: 'StringPrototypeReplaceAll("some string", /some regex/, "some replacement")',
        errors: [{ message: /looks up the Symbol\.replace property/ }],
      },
      {
        code: 'StringPrototypeReplaceAll("some string", new RegExp("some regex"), "some replacement")',
        errors: [{ message: /looks up the Symbol\.replace property/ }],
      },
      {
        code: 'StringPrototypeSearch("some string", /some regex/)',
        errors: [{ message: /SafeStringPrototypeSearch/ }],
      },
      {
        code: 'StringPrototypeSplit("some string", /some regex/)',
        errors: [{ message: /looks up the Symbol\.split property/ }],
      },
      {
        code: 'new Proxy({}, {})',
        errors: [{ message: /null-prototype/ }]
      },
      {
        code: 'new Proxy({}, { [`__proto__`]: null })',
        errors: [{ message: /null-prototype/ }]
      },
      {
        code: 'new Proxy({}, { __proto__: Object.prototype })',
        errors: [{ message: /null-prototype/ }]
      },
      {
        code: 'new Proxy({}, { ...{ __proto__: null } })',
        errors: [{ message: /null-prototype/ }]
      },
      {
        code: 'PromisePrototypeCatch(promise, ()=>{})',
        errors: [{ message: /\bPromisePrototypeThen\b/ }]
      },
      {
        code: 'PromiseAll([])',
        errors: [{ message: /\bSafePromiseAll\b/ }]
      },
      {
        code: 'async function fn(){await SafePromiseAll([])}',
        errors: [{ message: /\bSafePromiseAllReturnVoid\b/ }]
      },
      {
        code: 'async function fn(){await SafePromiseAllSettled([])}',
        errors: [{ message: /\bSafePromiseAllSettledReturnVoid\b/ }]
      },
      {
        code: 'PromiseAllSettled([])',
        errors: [{ message: /\bSafePromiseAllSettled\b/ }]
      },
      {
        code: 'PromiseAny([])',
        errors: [{ message: /\bSafePromiseAny\b/ }]
      },
      {
        code: 'PromiseRace([])',
        errors: [{ message: /\bSafePromiseRace\b/ }]
      },
      {
        code: 'ArrayPrototypeConcat([])',
        errors: [{ message: /\bisConcatSpreadable\b/ }]
      },
    ]
  });
