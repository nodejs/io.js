'use strict';

const common = require('../common');
if (!process.config.variables.node_use_amaro) common.skip('Requires Amaro');
const assert = require('assert');
const vm = require('node:vm');
const { stripTypeScriptTypes } = require('node:module');
const { test } = require('node:test');

common.expectWarning(
  'ExperimentalWarning',
  'stripTypeScriptTypes is an experimental feature and might change at any time',
);

const sourceToBeTransformed = `
  namespace MathUtil {
    export const add = (a: number, b: number) => a + b;
  }`;
const sourceToBeTransformedMapping = 'UACY;aACK,MAAM,CAAC,GAAW,IAAc,IAAI;AACnD,GAFU,aAAA';

test('stripTypeScriptTypes', () => {
  const source = 'const x: number = 1;';
  const result = stripTypeScriptTypes(source);
  assert.strictEqual(result, 'const x         = 1;');
});

test('stripTypeScriptTypes explicit', () => {
  const source = 'const x: number = 1;';
  const result = stripTypeScriptTypes(source, { mode: 'strip' });
  assert.strictEqual(result, 'const x         = 1;');
});

test('stripTypeScriptTypes code is not a string', () => {
  assert.throws(() => stripTypeScriptTypes({}),
                { code: 'ERR_INVALID_ARG_TYPE' });
});

test('stripTypeScriptTypes invalid mode', () => {
  const source = 'const x: number = 1;';
  assert.throws(() => stripTypeScriptTypes(source, { mode: 'invalid' }), { code: 'ERR_INVALID_ARG_VALUE' });
});

test('stripTypeScriptTypes sourceMap throws when mode is strip', () => {
  const source = 'const x: number = 1;';
  assert.throws(() => stripTypeScriptTypes(source,
                                           { mode: 'strip', sourceMap: true }),
                { code: 'ERR_INVALID_ARG_VALUE' });
});

test('stripTypeScriptTypes sourceUrl throws when mode is strip', () => {
  const source = 'const x: number = 1;';
  const result = stripTypeScriptTypes(source, { mode: 'strip', sourceUrl: 'foo.ts' });
  assert.strictEqual(result, 'const x         = 1;\n\n//# sourceURL=foo.ts');
});

test('stripTypeScriptTypes source map when mode is transform', () => {
  const result = stripTypeScriptTypes(sourceToBeTransformed, { mode: 'transform', sourceMap: true });
  const script = new vm.Script(result);
  const sourceMap =
    {
      version: 3,
      sources: [''],
      names: [],
      mappings: sourceToBeTransformedMapping,
    };
  const inlinedSourceMap = Buffer.from(JSON.stringify(sourceMap)).toString('base64');
  assert.strictEqual(script.sourceMapURL, `data:application/json;base64,${inlinedSourceMap}`);
});

test('stripTypeScriptTypes source map when mode is transform and sourceUrl', () => {
  const result = stripTypeScriptTypes(sourceToBeTransformed, {
    mode: 'transform',
    sourceMap: true,
    sourceUrl: 'test.ts'
  });
  const script = new vm.Script(result);
  const sourceMap =
    {
      version: 3,
      sources: ['test.ts'],
      names: [],
      mappings: sourceToBeTransformedMapping,
    };
  const inlinedSourceMap = Buffer.from(JSON.stringify(sourceMap)).toString('base64');
  assert.strictEqual(script.sourceMapURL, `data:application/json;base64,${inlinedSourceMap}`);
});

test('stripTypeScriptTypes source map when mode is transform and sourceUrl with non-latin-1 chars', () => {
  const sourceUrl = 'dir%20with $unusual"chars?\'åß∂ƒ©∆¬…`.cts';
  const result = stripTypeScriptTypes(sourceToBeTransformed, {
    mode: 'transform',
    sourceMap: true,
    sourceUrl,
  });
  const script = new vm.Script(result);
  const sourceMap =
    {
      version: 3,
      sources: [sourceUrl],
      names: [],
      mappings: sourceToBeTransformedMapping,
    };
  const inlinedSourceMap = Buffer.from(JSON.stringify(sourceMap)).toString('base64');
  assert.strictEqual(script.sourceMapURL, `data:application/json;base64,${inlinedSourceMap}`);
});
