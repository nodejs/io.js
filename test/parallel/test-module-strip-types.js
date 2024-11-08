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
  const source = `
  namespace MathUtil {
    export const add = (a: number, b: number) => a + b;
  }`;
  const result = stripTypeScriptTypes(source, { mode: 'transform', sourceMap: true });
  const script = new vm.Script(result);
  const sourceMap =
    {
      version: 3,
      sources: [
        '<anon>',
      ],
      sourcesContent: [
        '\n  namespace MathUtil {\n    export const add = (a: number, b: number) => a + b;\n  }',
      ],
      names: [],
      mappings: ';UACY;aACK,MAAM,CAAC,GAAW,IAAc,IAAI;AACnD,GAFU,aAAA'
    };
  assert(script.sourceMapURL, `sourceMappingURL=data:application/json;base64,${JSON.stringify(sourceMap)}`);
});

test('stripTypeScriptTypes source map when mode is transform and sourceUrl', () => {
  const source = `
  namespace MathUtil {
    export const add = (a: number, b: number) => a + b;
  }`;
  const result = stripTypeScriptTypes(source, { mode: 'transform', sourceMap: true, sourceUrl: 'test.ts' });
  const script = new vm.Script(result);
  const sourceMap =
    {
      version: 3,
      sources: [
        'test.ts',
      ],
      sourcesContent: [
        '\n  namespace MathUtil {\n    export const add = (a: number, b: number) => a + b;\n  }',
      ],
      names: [],
      mappings: ';UACY;aACK,MAAM,CAAC,GAAW,IAAc,IAAI;AACnD,GAFU,aAAA'
    };
  assert(script.sourceMapURL, `sourceMappingURL=data:application/json;base64,${JSON.stringify(sourceMap)}`);
});
